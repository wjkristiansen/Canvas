// TaskScheduler.h
//
// TaskScheduler is a system for managing task allocation, dependency tracking, scheduling,
// and retirement using RingBuffer for memory management.
//
// Task Structure:
//   Each task is a contiguous memory block allocated from a RingBuffer, with the following layout:
//   [ TaskHeader | DependencyID[0..N-1] | TaskPayload ]
//
//   TaskHeader (private nested struct) contains:
//   - Size: total size of the task record (including header, dependencies, and payload)
//   - State: Unscheduled, Scheduled, Ready, Executing, Completed, or Retired
//   - DependencyCount: number of prerequisite tasks
//   - TaskID: unique identifier for external DAG tracking
//   - Function: callback to execute when task becomes ready
//
// Task Execution:
//   - All tasks require a callback function (TaskFunc)
//   - Callbacks execute synchronously when dependencies are satisfied
//   - Tasks can complete immediately or defer completion by calling CompleteTask() later
//   - Completion triggers execution of dependent tasks
//
// Dependencies:
//   - Tracked via TaskID, not raw pointers, to avoid dangling references
//   - Dependencies may only be added to unscheduled tasks
//   - Only scheduled tasks may be depended upon
//   - These rules prevent cycles and enable forward-only retirement
//
// Memory Management:
//   - Uses Canvas::RingBuffer (see RingBuffer.h) for circular memory allocation
//   - Multiple ring buffers may be active simultaneously
//   - If a ring buffer cannot fit a new task, a new ring is allocated
//   - Tasks retire in submission order; memory is reclaimed via ProcessRetirements()
//   - Empty rings are automatically removed (at least one ring is always retained)
//
// Thread Safety:
//   - All public methods are thread-safe (protected by internal mutex)
//   - Callbacks execute synchronously within the calling thread
//
// This class forms the foundation for Canvas's task graph execution system.
// It is designed for extensibility, performance, and clarity.

#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <functional>
#include "Gem.hpp"
#include "RingBuffer.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
// Task state enumeration
enum class TaskState : uint32_t
{
    Unscheduled = 0,  // Task allocated but not yet scheduled
    Scheduled = 1,    // Task scheduled, waiting for dependencies
    Ready = 2,        // All dependencies satisfied, ready to execute
    Executing = 3,    // Task is currently being executed
    Completed = 4,    // Task execution finished, ready to retire
    Retired = 5       // Task retired and memory can be reclaimed
};

//------------------------------------------------------------------------------------------------
// Unique identifier for tasks (monotonically increasing)
using TaskID = uint64_t;
constexpr TaskID InvalidTaskID = 0;

//------------------------------------------------------------------------------------------------
// Forward declaration
class TaskScheduler;

//------------------------------------------------------------------------------------------------
// Task execution callback signature
// Parameters: taskId, payload pointer, payload size, scheduler reference
// The callback can call scheduler.CompleteTask(taskId) when done
using TaskFunc = std::function<void(TaskID taskId, void* payload, size_t payloadSize, TaskScheduler& scheduler)>;

//------------------------------------------------------------------------------------------------
// TaskScheduler: Main class for managing task allocation and scheduling
class TaskScheduler
{
private:
    //------------------------------------------------------------------------------------------------
    // TaskHeader: Stored at the beginning of each task allocation
    // Memory layout: [TaskHeader][DependencyID array][Payload data]
    struct TaskHeader
    {
        uint32_t Size;                  // Total size of task record (header + dependencies + payload)
        TaskState State;                // Current state of the task
        uint32_t MaxDependencyCount;    // Maximum number of dependencies (array size)
        uint32_t ActualDependencyCount; // Actual number of dependencies set
        uint32_t OutstandingDependencies; // Number of dependencies not yet completed
        TaskID TaskId;                  // Unique identifier for this task
        size_t PayloadSize;             // Size of the payload (for callback)
        TaskFunc Function;              // Callback function (all tasks must have one)
        
        // Get pointer to dependency array (immediately after header)
        TaskID* GetDependencies()
        {
            return reinterpret_cast<TaskID*>(this + 1);
        }
        
        const TaskID* GetDependencies() const
        {
            return reinterpret_cast<const TaskID*>(this + 1);
        }
        
        // Get pointer to payload data (after header and dependencies, aligned to 16 bytes)
        void* GetPayload()
        {
            uintptr_t addr = reinterpret_cast<uintptr_t>(this + 1) + (MaxDependencyCount * sizeof(TaskID));
            // Align to 16-byte boundary
            addr = (addr + 15) & ~uintptr_t(15);
            return reinterpret_cast<void*>(addr);
        }
        
        const void* GetPayload() const
        {
            uintptr_t addr = reinterpret_cast<uintptr_t>(this + 1) + (MaxDependencyCount * sizeof(TaskID));
            // Align to 16-byte boundary
            addr = (addr + 15) & ~uintptr_t(15);
            return reinterpret_cast<const void*>(addr);
        }
    };

    //------------------------------------------------------------------------------------------------
    // Helper to calculate total task size (header + dependencies + aligned payload)
    static size_t CalculateTaskSize(size_t payloadSize, uint32_t dependencyCount);

public:
    // Constructor: initialSize is the size of the first ring buffer
    explicit TaskScheduler(size_t initialRingSize = 64 * 1024);
    ~TaskScheduler();
    
    // Non-copyable, non-movable (contains mutex)
    TaskScheduler(const TaskScheduler&) = delete;
    TaskScheduler& operator=(const TaskScheduler&) = delete;
    TaskScheduler(TaskScheduler&&) = delete;
    TaskScheduler& operator=(TaskScheduler&&) = delete;
    
    // Allocate a new task with specified payload size and maximum dependency count
    // maxDependencyCount reserves space for dependencies, but they don't need to be set immediately
    // Use AddDependency() to add dependencies before scheduling
    // taskFunc: callback to execute when task becomes ready (can be nullptr for manual execution)
    // Returns TaskID directly (no handles needed)
    // All tasks must have a callback function
    TaskID AllocateTask(
        size_t payloadSize,
        uint32_t maxDependencyCount,
        TaskFunc taskFunc);
    
    // Add a dependency to an unscheduled task
    // The dependency must be a TaskID of an already-scheduled task
    // Cannot add dependencies once the task is scheduled
    Gem::Result AddDependency(TaskID taskId, TaskID dependencyId);
    
    // Schedule a task (mark it as ready for dependency tracking)
    // All dependencies added so far must already be scheduled
    // Once scheduled, no new dependencies can be added
    // If task has a callback and all dependencies are satisfied, it will be dispatched immediately
    Gem::Result ScheduleTask(TaskID taskId);
    
    // Mark a task as completed (call this from task callback when execution finishes)
    // Task moves to Completed state and checks if any dependents become ready
    // Dependent tasks with callbacks will be automatically dispatched
    Gem::Result CompleteTask(TaskID taskId);
    
    // Get payload pointer for a task (for filling in data before scheduling)
    void* GetPayload(TaskID taskId);
    
    // Get typed payload pointer
    template<typename T>
    T* GetPayloadAs(TaskID taskId) { return static_cast<T*>(GetPayload(taskId)); }
    
    // Check if a task exists and is in the specified state
    bool IsTaskInState(TaskID taskId, TaskState state) const;
    
    // Get the current state of a task
    Gem::Result GetTaskState(TaskID taskId, TaskState& outState) const;
    
    // Process all retired tasks at the head of each ring buffer
    // This reclaims memory by advancing head pointers
    void ProcessRetirements();
    
    // Get statistics for monitoring and debugging
    struct Statistics
    {
        size_t RingCount;           // Number of active ring buffers
        size_t TotalAllocated;      // Total bytes allocated across all rings
        size_t TotalUsed;           // Total bytes used (not yet retired)
        size_t ActiveTaskCount;     // Number of active tasks
        TaskID NextTaskId;          // Next task ID that will be assigned
        size_t ExecutingTaskCount;  // Number of tasks currently executing
    };
    
    Statistics GetStatistics() const;
    
    // Reset the scheduler (retire all tasks and clear all rings)
    void Reset();

private:
    // Helper structure to track which ring a task was allocated from
    struct TaskAllocation
    {
        TaskHeader* Header;
        RingBuffer* Ring;
        size_t Size;
    };
    
    mutable std::mutex m_Mutex;                         // Protects all internal state
    std::vector<std::unique_ptr<RingBuffer>> m_Rings;   // Active ring buffers
    std::unordered_map<TaskID, TaskAllocation> m_TaskMap; // Fast lookup from ID to allocation info
    std::unordered_map<TaskID, std::vector<TaskID>> m_Dependents; // Tasks waiting on each task
    TaskID m_NextTaskId;                                // Next task ID to assign
    size_t m_InitialRingSize;                           // Size of first ring
    size_t m_NextRingSize;                              // Size of next ring to allocate
    

    
    // Allocate a new ring buffer (doubles in size each time)
    RingBuffer* AllocateNewRing();
    
    // Find the task header for a given task ID (assumes mutex is held)
    TaskHeader* FindTaskHeader(TaskID taskId) const;
    
    // Check if a task should transition to Ready state and execute callback if present
    void CheckAndExecuteTask(TaskID taskId);
    
    // Notify dependents that a task has completed
    void NotifyDependents(TaskID taskId);
    
    // Generate next unique task ID
    TaskID GenerateTaskID();
};

} // namespace Canvas