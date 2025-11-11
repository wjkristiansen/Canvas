// TaskManager.h
//
// TaskManager is a system for managing task allocation, dependency tracking, scheduling,
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
//   TaskManager scheduler(64 * 1024);
//   TaskID task = scheduler.AllocateTypedTask(0,
//       [](TaskID id, TaskManager& sched, int value) {
//           printf("Processing value: %d\n", value);
//           sched.CompleteTask(id);
//       }, 42);
//   scheduler.EnqueueTask(task);
//
// Example 2: Plain function (most efficient)
//   void ProcessData(TaskID id, TaskManager& sched, std::string data) {
//       printf("Data: %s\n", data.c_str());
//       sched.CompleteTask(id);
//   }
//   TaskID task = scheduler.AllocateTypedTask(0, ProcessData, std::string("Hello"));
//   scheduler.EnqueueTask(task);
//
// Example 3: Task with deferred completion
//   TaskID task = scheduler.AllocateTypedTask(0,
//       [](TaskID id, TaskManager& sched, AsyncOperation* op) {
//           op->Start([id, &sched]() {
//               // Called later when async work completes
//               sched.CompleteTask(id);
//           });
//       }, &myAsyncOp);
//   scheduler.EnqueueTask(task);
//
// Example 4: Fork pattern - one task, multiple dependents
//   TaskID root = scheduler.AllocateTypedTask(0, LoadData);
//   TaskID child1 = scheduler.AllocateTypedTask(1, ProcessDataA);
//   TaskID child2 = scheduler.AllocateTypedTask(1, ProcessDataB);
//   scheduler.AddDependency(child1, root);
//   scheduler.AddDependency(child2, root);
//   scheduler.EnqueueTask(root);
//   scheduler.EnqueueTask(child1);
//   scheduler.EnqueueTask(child2);
//
// Example 5: Join pattern - multiple tasks, one dependent
//   TaskID task1 = scheduler.AllocateTypedTask(0, LoadTextureA);
//   TaskID task2 = scheduler.AllocateTypedTask(0, LoadTextureB);
//   TaskID join = scheduler.AllocateTypedTask(2, CombineTextures);
//   scheduler.AddDependency(join, task1);
//   scheduler.AddDependency(join, task2);
//   scheduler.EnqueueTask(task1);
//   scheduler.EnqueueTask(task2);
//   scheduler.EnqueueTask(join);
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
//   scheduler.EnqueueTask(t1);
//   scheduler.EnqueueTask(t2);
//   scheduler.EnqueueTask(t3);
//   scheduler.EnqueueTask(t4);
//
// Example 7: Low-level API with manual payload management
//   struct MyPayload { int x; float y; std::string s; };
//   TaskID task = scheduler.AllocateTask(sizeof(MyPayload), 0,
//       [](TaskID id, void* payload, size_t size, TaskManager& sched) {
//           MyPayload* data = static_cast<MyPayload*>(payload);
//           printf("%d, %f, %s\n", data->x, data->y, data->s.c_str());
//           data->~MyPayload();  // Manual cleanup required for non-trivial types
//           sched.CompleteTask(id);
//       });
//   MyPayload* payload = scheduler.GetPayloadAs<MyPayload>(task);
//   new (payload) MyPayload{42, 3.14f, "test"};  // Placement new required
//   scheduler.EnqueueTask(task);
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
    Invalid = 0xffffffff, // Not a valid task id
    Unscheduled = 0,  // Task allocated but not yet scheduled
    Scheduled = 1,    // Task scheduled, waiting for dependencies
    Ready = 2,        // All dependencies satisfied, ready to execute
    Executing = 3,    // Task is currently being executed
    Completed = 4     // Task execution finished, ready to retire (memory reclaimed by RingBuffer)
};

//------------------------------------------------------------------------------------------------
// Unique identifier for tasks (monotonically increasing)
using TaskID = uint64_t;
constexpr TaskID NullTaskID = 0;

//------------------------------------------------------------------------------------------------
// Forward declaration
class TaskManager;

//------------------------------------------------------------------------------------------------
// Task execution callback signature
// Parameters: taskId, payload pointer, payload size, scheduler reference
// The callback can call scheduler.CompleteTask(taskId) when done
//
// NOTE: Use stateless lambdas or regular functions. For passing data to tasks,
// use the typed API (AllocateTypedTask) which safely stores arguments in the task payload.
using TaskFunc = void(*)(TaskID taskId, void* payload, size_t payloadSize, TaskManager& scheduler);

//------------------------------------------------------------------------------------------------
// TaskScheduler: Main class for managing task allocation and scheduling
class TaskManager
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
    explicit TaskManager(size_t initialRingSize = 64 * 1024);
    ~TaskManager();
    
    // Non-copyable, non-movable (contains mutex)
    TaskManager(const TaskManager&) = delete;
    TaskManager& operator=(const TaskManager&) = delete;
    TaskManager(TaskManager&&) = delete;
    TaskManager& operator=(TaskManager&&) = delete;
    
    // Low-level task allocation with manual payload management
    // Use this when you need direct control over payload memory
    TaskID AllocateTask(
        size_t payloadSize,
        uint32_t maxDependencyCount,
        TaskFunc taskFunc);
    
    // Low-level convenience: allocate and schedule in one call (untyped)
    // Adds the provided dependencies (if any) and schedules the task.
    // Returns NullTaskID on failure.
    TaskID AllocateAndEnqueueTask(
        const TaskID* pDependencies,
        uint32_t dependencyCount,
        size_t payloadSize,
        TaskFunc taskFunc)
    {
        TaskID id = AllocateTask(payloadSize, dependencyCount, taskFunc);
        if (id == NullTaskID) return NullTaskID;
        for (uint32_t i = 0; i < dependencyCount; ++i)
        {
            TaskID dep = pDependencies ? pDependencies[i] : NullTaskID;
            if (dep == NullTaskID) continue;
            if (AddDependency(id, dep) != Gem::Result::Success)
                return NullTaskID;
        }
        return (EnqueueTask(id) == Gem::Result::Success) ? id : NullTaskID;
    }
    
    //============================================================================
    // Typed task API
    //============================================================================
    
    // Allocate a typed task with space for up to maxDependencyCount dependencies.
    // The task is NOT scheduled; caller must add dependencies (optional) and then call EnqueueTask.
    template<typename Func, typename... Args>
    TaskID AllocateTypedTask(
        uint32_t maxDependencyCount,
        Func&& func,
        Args&&... args)
    {
        using PayloadData = std::tuple<std::decay_t<Func>, std::decay_t<Args>...>;
        auto wrapper = [](TaskID taskId, void* payload, size_t /*size*/, TaskManager& scheduler)
        {
            auto* data = static_cast<PayloadData*>(payload);
            auto& funcRef = std::get<0>(*data);
            std::apply([&](auto& /*func*/, auto&... unpackedArgs) {
                funcRef(taskId, scheduler, unpackedArgs...);
            }, *data);
            data->~PayloadData();
        };
        TaskID id = AllocateTask(sizeof(PayloadData), maxDependencyCount, wrapper);
        if (id == NullTaskID) return NullTaskID;
        new (GetPayload(id)) PayloadData(std::forward<Func>(func), std::forward<Args>(args)...);
        return id;
    }

    // Allocate and schedule a typed task with N dependencies (atomically allocate + schedule)
    // Executes immediately when all dependencies are already satisfied.
    template<typename Func, typename... Args>
    TaskID AllocateAndEnqueueTypedTask(
        const TaskID* dependencies,
        uint32_t dependencyCount,
        Func&& func,
        Args&&... args)
    {
        // Fast path: Check if all dependencies are satisfied using high-water mark first
        TaskID completedHighWater = m_CompletedHighWater.load(std::memory_order_acquire);
        bool allDependenciesSatisfied = true;
        
        for (uint32_t i = 0; i < dependencyCount; ++i)
        {
            TaskID dep = dependencies ? dependencies[i] : NullTaskID;
            if (dep == NullTaskID) continue;
            
            // Fast check: if dependency is below high-water mark, it's completed
            if (dep <= completedHighWater)
                continue;
            
            // Need to check hash maps or task map
            allDependenciesSatisfied = false;
            break;
        }
        
        // Ultra-fast path: Zero dependencies or all below high-water mark
        if (allDependenciesSatisfied)
        {
            TaskID id = GenerateTaskID();
            
            // Execute immediately (no bookkeeping needed!)
            std::forward<Func>(func)(id, *this, std::forward<Args>(args)...);
            
            // Update high-water mark (lock-free completion tracking)
            // This works because tasks complete in-order for zero-dependency immediate execution
            TaskID expected = id - 1;
            if (m_CompletedHighWater.compare_exchange_strong(expected, id, std::memory_order_release))
            {
                // Successfully advanced high-water mark, no hash insert needed
            }
            else
            {
                // Out-of-order completion or concurrent tasks, use hash map
                std::lock_guard<std::mutex> lock(m_Mutex);
                m_CompletedImmediate.insert(id);
            }
            
            return id;
        }
        
        // Slower path: Need to check dependencies more carefully
        uint32_t outstanding = 0;
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            for (uint32_t i = 0; i < dependencyCount; ++i)
            {
                TaskID dep = dependencies ? dependencies[i] : NullTaskID;
                if (dep == NullTaskID) continue;
                
                // Check high-water mark again (might have advanced)
                if (dep <= m_CompletedHighWater.load(std::memory_order_acquire))
                    continue;
                
                TaskHeader* depHeader = FindTaskHeader(dep);
                if (depHeader == nullptr)
                {
                    // Check completed-immediate hash map
                    if (m_CompletedImmediate.find(dep) != m_CompletedImmediate.end())
                        continue;
                    // Unknown dependency
                    return NullTaskID;
                }
                if (depHeader->State != TaskState::Completed)
                    ++outstanding;
            }

            if (outstanding == 0)
            {
                TaskID id = GenerateTaskID();
                m_ExecutingImmediate.insert(id);
                m_Mutex.unlock();
                std::forward<Func>(func)(id, *this, std::forward<Args>(args)...);
                m_Mutex.lock();
                if (m_ExecutingImmediate.erase(id) > 0)
                {
                    m_CompletedImmediate.insert(id);
                    NotifyDependents(id);
                }
                return id;
            }
        }
        
        // Fallback: ring-backed storage (tuple for function + args)
        using PayloadData = std::tuple<std::decay_t<Func>, std::decay_t<Args>...>;
        auto wrapper = [](TaskID taskId, void* payload, size_t /*size*/, TaskManager& scheduler)
        {
            auto* data = static_cast<PayloadData*>(payload);
            auto& funcRef = std::get<0>(*data);
            std::apply([&](auto& /*func*/, auto&... unpackedArgs) {
                funcRef(taskId, scheduler, unpackedArgs...);
            }, *data);
            data->~PayloadData();
        };
        TaskID taskId = AllocateTask(sizeof(PayloadData), dependencyCount, wrapper);
        if (taskId == NullTaskID) return NullTaskID;
        new (GetPayload(taskId)) PayloadData(std::forward<Func>(func), std::forward<Args>(args)...);
        
        for (uint32_t i = 0; i < dependencyCount; ++i)
        {
            TaskID dep = dependencies ? dependencies[i] : NullTaskID;
            if (dep == NullTaskID) continue;
            bool skip = false;
            {
                std::lock_guard<std::mutex> lock(m_Mutex);
                skip = (m_CompletedImmediate.find(dep) != m_CompletedImmediate.end());
            }
            if (!skip && AddDependency(taskId, dep) != Gem::Result::Success)
                return NullTaskID;
        }
        
        return (EnqueueTask(taskId) == Gem::Result::Success) ? taskId : NullTaskID;
    }
    
    // Add a dependency to an unscheduled task
    // The dependency must be a TaskID of an already-scheduled task
    // Cannot add dependencies once the task is scheduled
    Gem::Result AddDependency(TaskID taskId, TaskID dependencyId);
    
    // Schedule a task (mark it as ready for dependency tracking)
    // All dependencies added so far must already be scheduled
    // Once scheduled, no new dependencies can be added
    // If task has a callback and all dependencies are satisfied, it will be dispatched immediately
    Gem::Result EnqueueTask(TaskID taskId);
    
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
    // Returns TaskState::Invalid if the task ID is invalid or not found
    TaskState GetTaskState(TaskID taskId) const;
    
    // Get high-water marks for lock-free fast-path checks
    // All tasks with TaskID <= returned value are guaranteed to be in that state
    TaskID GetCompletedHighWater() const 
    { 
        return m_CompletedHighWater.load(std::memory_order_acquire); 
    }
    
    TaskID GetRetiredHighWater() const 
    { 
        return m_RetiredHighWater.load(std::memory_order_acquire); 
    }
    
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
    std::unordered_set<TaskID> m_CompletedImmediate;               // IDs of completed immediate tasks (above high-water)
    
    // High-water marks for lock-free fast-path queries
    // All tasks with TaskID <= the high-water mark are guaranteed to be in that state
    std::atomic<TaskID> m_CompletedHighWater;           // Highest contiguous completed TaskID
    std::atomic<TaskID> m_RetiredHighWater;             // Highest contiguous retired TaskID
    
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
