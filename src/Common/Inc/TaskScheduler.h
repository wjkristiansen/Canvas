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
//   - Tasks retire in submission order; memory is reclaimed via RetireCompletedTasks()
//   - Empty rings are automatically removed (at least one ring is always retained)
//
// Thread Safety:
//   - All public methods are thread-safe (protected by internal mutex)
//   - Callbacks execute synchronously within the calling thread
//
// This class forms the foundation for Canvas's task graph execution system.
// It is designed for extensibility, performance, and clarity.
//
//------------------------------------------------------------------------------------------------
// USAGE EXAMPLES
//------------------------------------------------------------------------------------------------
//
// Example 1: Simple task with synchronous completion (recommended API)
//   TaskScheduler scheduler(64 * 1024);
//   TaskID task = scheduler.AllocateTypedTask(0,
//       [](TaskID id, TaskScheduler& sched, int value) {
//           printf("Processing value: %d\n", value);
//           sched.CompleteTask(id);
//       }, 42);
//   scheduler.ScheduleTask(task);
//
// Example 2: Plain function (most efficient)
//   void ProcessData(TaskID id, TaskScheduler& sched, std::string data) {
//       printf("Data: %s\n", data.c_str());
//       sched.CompleteTask(id);
//   }
//   TaskID task = scheduler.AllocateTypedTask(0, ProcessData, std::string("Hello"));
//   scheduler.ScheduleTask(task);
//
// Example 3: Task with deferred completion
//   TaskID task = scheduler.AllocateTypedTask(0,
//       [](TaskID id, TaskScheduler& sched, AsyncOperation* op) {
//           op->Start([id, &sched]() {
//               // Called later when async work completes
//               sched.CompleteTask(id);
//           });
//       }, &myAsyncOp);
//   scheduler.ScheduleTask(task);
//
// Example 4: Fork pattern - one task, multiple dependents
//   TaskID root = scheduler.AllocateTypedTask(0, LoadData);
//   TaskID child1 = scheduler.AllocateTypedTask(1, ProcessDataA);
//   TaskID child2 = scheduler.AllocateTypedTask(1, ProcessDataB);
//   scheduler.AddDependency(child1, root);
//   scheduler.AddDependency(child2, root);
//   scheduler.ScheduleTask(root);
//   scheduler.ScheduleTask(child1);
//   scheduler.ScheduleTask(child2);
//
// Example 5: Join pattern - multiple tasks, one dependent
//   TaskID task1 = scheduler.AllocateTypedTask(0, LoadTextureA);
//   TaskID task2 = scheduler.AllocateTypedTask(0, LoadTextureB);
//   TaskID join = scheduler.AllocateTypedTask(2, CombineTextures);
//   scheduler.AddDependency(join, task1);
//   scheduler.AddDependency(join, task2);
//   scheduler.ScheduleTask(task1);
//   scheduler.ScheduleTask(task2);
//   scheduler.ScheduleTask(join);
//
// Example 6: Diamond dependency graph
//   //       T1
//   //      /  \
//   //    T2    T3
//   //      \  /
//   //       T4
//   TaskID t1 = scheduler.AllocateTypedTask(0, TaskFunc1);
//   TaskID t2 = scheduler.AllocateTypedTask(1, TaskFunc2);
//   TaskID t3 = scheduler.AllocateTypedTask(1, TaskFunc3);
//   TaskID t4 = scheduler.AllocateTypedTask(2, TaskFunc4);
//   scheduler.AddDependency(t2, t1);
//   scheduler.AddDependency(t3, t1);
//   scheduler.AddDependency(t4, t2);
//   scheduler.AddDependency(t4, t3);
//   scheduler.ScheduleTask(t1);
//   scheduler.ScheduleTask(t2);
//   scheduler.ScheduleTask(t3);
//   scheduler.ScheduleTask(t4);
//
// Example 7: Low-level API with manual payload management
//   struct MyPayload { int x; float y; std::string s; };
//   TaskID task = scheduler.AllocateTask(sizeof(MyPayload), 0,
//       [](TaskID id, void* payload, size_t size, TaskScheduler& sched) {
//           MyPayload* data = static_cast<MyPayload*>(payload);
//           printf("%d, %f, %s\n", data->x, data->y, data->s.c_str());
//           data->~MyPayload();  // Manual cleanup required for non-trivial types
//           sched.CompleteTask(id);
//       });
//   MyPayload* payload = scheduler.GetPayloadAs<MyPayload>(task);
//   new (payload) MyPayload{42, 3.14f, "test"};  // Placement new required
//   scheduler.ScheduleTask(task);
//
// Example 8: Memory management
//   scheduler.RetireCompletedTasks();  // Reclaim memory from completed tasks
//   auto stats = scheduler.GetStatistics();
//   printf("Active tasks: %zu, Memory used: %zu bytes\n", 
//          stats.ActiveTaskCount, stats.TotalUsed);
//
//------------------------------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <tuple>
#include <utility>
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
    Completed = 4     // Task execution finished, ready to retire (memory reclaimed by RingBuffer)
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
//
// NOTE: Use stateless lambdas or regular functions. For passing data to tasks,
// use the typed API (AllocateTypedTask) which safely stores arguments in the task payload.
using TaskFunc = void(*)(TaskID taskId, void* payload, size_t payloadSize, TaskScheduler& scheduler);

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
    
    // Low-level task allocation with manual payload management
    // Use this when you need direct control over payload memory
    TaskID AllocateTask(
        size_t payloadSize,
        uint32_t maxDependencyCount,
        TaskFunc taskFunc);
    
    // Type-safe task allocation - recommended API for typed callbacks
    // Automatically stores func and args in payload and passes them to callback
    // Example: AllocateTypedTask(2, [](TaskID id, TaskScheduler& s, int x, float y) { ... }, 42, 3.14f);
    // The callback signature must be: (TaskID, TaskScheduler&, Args...)
    template<typename Func, typename... Args>
    TaskID AllocateTypedTask(
        uint32_t maxDependencyCount,
        Func&& func,
        Args&&... args)
    {
        // Store both function and args in the payload
        using PayloadData = std::tuple<std::decay_t<Func>, std::decay_t<Args>...>;
        constexpr size_t payloadSize = sizeof(PayloadData);
        
        // Create stateless wrapper that extracts func+args from payload
        // This lambda is stateless (no captures) so it converts to function pointer
        auto wrapper = [](TaskID taskId, void* payload, size_t size, TaskScheduler& scheduler)
        {
            (void)size; // Size is known at compile-time
            auto* data = static_cast<PayloadData*>(payload);
            
            // Extract function and args
            auto& funcRef = std::get<0>(*data);
            
            // Call user function with unpacked args (skipping first element which is func)
            std::apply([&](auto& /* funcInTuple */, auto&... unpackedArgs) {
                funcRef(taskId, scheduler, unpackedArgs...);
            }, *data);
            
            // Destroy the payload when task completes
            data->~PayloadData();
        };
        
        // Allocate task with wrapper
        TaskID taskId = AllocateTask(payloadSize, maxDependencyCount, wrapper);
        
        // Construct func+args tuple in payload
        void* payloadPtr = GetPayload(taskId);
        new (payloadPtr) PayloadData(std::forward<Func>(func), std::forward<Args>(args)...);
        
        return taskId;
    }

    // Atomic allocate + schedule with explicit dependencies. If all dependencies are
    // already satisfied, executes immediately without ring allocation.
    template<typename Func, typename... Args>
    TaskID AllocateAndScheduleTypedTask(
        const TaskID* dependencies,
        uint32_t dependencyCount,
        Func&& func,
        Args&&... args)
    {
        // Determine if any dependency is still outstanding
        uint32_t outstanding = 0;
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            for (uint32_t i = 0; i < dependencyCount; ++i)
            {
                TaskID dep = dependencies ? dependencies[i] : InvalidTaskID;
                if (dep == InvalidTaskID) continue;
                TaskHeader* depHeader = FindTaskHeader(dep);
                if (depHeader == nullptr)
                {
                    // Consider completed-immediate deps satisfied
                    if (m_CompletedImmediate.find(dep) != m_CompletedImmediate.end())
                        continue;
                    // Unknown dependency
                    return InvalidTaskID;
                }
                if (depHeader->State != TaskState::Completed)
                {
                    ++outstanding;
                }
            }
            if (outstanding == 0)
            {
                // Execute immediately, no ring allocation. Track as executing to preserve ordering.
                TaskID id = GenerateTaskID();
                m_ExecutingImmediate.insert(id);
                m_Mutex.unlock();
                std::forward<Func>(func)(id, *this, std::forward<Args>(args)...);
                m_Mutex.lock();
                // If user callback didn't call CompleteTask(id), auto-complete now.
                if (m_ExecutingImmediate.erase(id) > 0)
                {
                    m_CompletedImmediate.insert(id);
                    NotifyDependents(id);
                }
                return id;
            }
        }

        // Fallback: allocate ring-backed task, attach deps, and schedule
        using PayloadData = std::tuple<std::decay_t<Func>, std::decay_t<Args>...>;
        constexpr size_t payloadSize = sizeof(PayloadData);
        auto wrapper = [](TaskID taskId, void* payload, size_t /*size*/, TaskScheduler& scheduler)
        {
            auto* data = static_cast<PayloadData*>(payload);
            auto& funcRef = std::get<0>(*data);
            std::apply([&](auto& /* funcInTuple */, auto&... unpackedArgs) {
                funcRef(taskId, scheduler, unpackedArgs...);
            }, *data);
            data->~PayloadData();
        };

        TaskID taskId = AllocateTask(payloadSize, dependencyCount, wrapper);
        if (taskId == InvalidTaskID) return InvalidTaskID;
        void* payloadPtr = GetPayload(taskId);
        new (payloadPtr) PayloadData(std::forward<Func>(func), std::forward<Args>(args)...);

        for (uint32_t i = 0; i < dependencyCount; ++i)
        {
            TaskID dep = dependencies ? dependencies[i] : InvalidTaskID;
            if (dep == InvalidTaskID) continue;
            // Skip deps that are already completed immediate
            bool skip = false;
            {
                std::lock_guard<std::mutex> lock(m_Mutex);
                skip = (m_CompletedImmediate.find(dep) != m_CompletedImmediate.end());
            }
            if (!skip)
            {
                if (AddDependency(taskId, dep) != Gem::Result::Success)
                {
                    return InvalidTaskID;
                }
            }
        }
        if (ScheduleTask(taskId) != Gem::Result::Success)
        {
            return InvalidTaskID;
        }
        return taskId;
    }
    
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
    
    // Retire completed tasks at the head of each ring buffer and reclaim their memory
    // Only tasks in Completed state at the head of a ring can be retired
    // This advances ring buffer head pointers to free memory
    void RetireCompletedTasks();
    
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
    std::unordered_set<TaskID> m_ExecutingImmediate;               // Immediate tasks currently executing
    std::unordered_set<TaskID> m_CompletedImmediate;               // IDs of completed immediate tasks
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
