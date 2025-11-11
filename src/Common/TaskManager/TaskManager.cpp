// TaskManager.cpp

#include "pch.h"
#include "TaskManager.h"
#include <algorithm>
#include <cassert>

namespace Canvas
{

//------------------------------------------------------------------------------------------------
// TaskManager - Task Size Calculation
//------------------------------------------------------------------------------------------------

size_t TaskManager::CalculateTaskSize(size_t payloadSize, uint32_t dependencyCount)
{
    size_t totalSize = sizeof(TaskHeader);
    totalSize += dependencyCount * sizeof(TaskID);
    
    // Align to 16 bytes before adding payload (ensures payload is 16-byte aligned)
    totalSize = (totalSize + 15) & ~size_t(15);
    
    totalSize += payloadSize;
    
    // Align final size to 16 bytes for RingBuffer
    constexpr size_t alignment = 16;
    return (totalSize + alignment - 1) & ~(alignment - 1);
}

//------------------------------------------------------------------------------------------------
// TaskManager Implementation
//------------------------------------------------------------------------------------------------

TaskManager::TaskManager(size_t initialRingSize)
    : m_NextTaskId(1) // Start at 1, reserve 0 for NullTaskID
    , m_InitialRingSize(initialRingSize)
    , m_NextRingSize(initialRingSize)
{
    if (initialRingSize == 0)
    {
        throw Gem::GemError(Gem::Result::InvalidArg);
    }
    
    // Initialize high-water marks to 0 (no tasks completed/retired yet)
    m_CompletedHighWater.store(0, std::memory_order_release);
    m_RetiredHighWater.store(0, std::memory_order_release);
    
    try
    {
        // Allocate the initial ring buffer
        m_Rings.push_back(std::make_unique<RingBuffer>(initialRingSize));
    }
    catch (const Gem::GemError&)
    {
        throw; // Re-throw GemError as-is
    }
    catch (const std::exception&)
    {
        throw Gem::GemError(Gem::Result::OutOfMemory);
    }
}

TaskManager::~TaskManager()
{
    // RAII: all resources cleaned up automatically
}

TaskID TaskManager::AllocateTask(
    size_t payloadSize,
    uint32_t maxDependencyCount,
    TaskFunc taskFunc)
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    
    try
    {
        // Calculate total task size
        const size_t taskSize = CalculateTaskSize(payloadSize, maxDependencyCount);
        
        // Try to allocate from existing rings
        void* ptr = nullptr;
        RingBuffer* allocatedRing = nullptr;
        
        for (auto& ring : m_Rings)
        {
            ptr = ring->TryAllocate(taskSize);
            if (ptr != nullptr)
            {
                allocatedRing = ring.get();
                break;
            }
        }
        
        // If no space in existing rings, allocate a new one
        if (ptr == nullptr)
        {
            RingBuffer* newRing = AllocateNewRing();
            if (newRing == nullptr)
            {
                return NullTaskID;
            }
            
            ptr = newRing->TryAllocate(taskSize);
            
            if (ptr == nullptr)
            {
                // Task is too large even for a new ring
                return NullTaskID;
            }
            
            allocatedRing = newRing;
        }
        
        // Initialize task header at allocated memory
        TaskHeader* header = reinterpret_cast<TaskHeader*>(ptr);
        header->Size = static_cast<uint32_t>(taskSize);
        header->State = TaskState::Unscheduled;
        header->MaxDependencyCount = maxDependencyCount;
        header->ActualDependencyCount = 0;
        header->OutstandingDependencies = 0;
        header->PayloadSize = payloadSize;
        
        // Assign task ID and callback
        TaskID taskId = GenerateTaskID();
        header->TaskId = taskId;
        header->Function = taskFunc;
        
        // Initialize dependency array to NullTaskID
        if (maxDependencyCount > 0)
        {
            TaskID* deps = header->GetDependencies();
            std::fill(deps, deps + maxDependencyCount, NullTaskID);
        }
        
        // Register in task map with allocation info
        m_TaskMap[taskId] = TaskAllocation{ header, allocatedRing, taskSize };
        
        return taskId;
    }
    catch (const std::bad_alloc&)
    {
        return NullTaskID;
    }
    catch (...)
    {
        return NullTaskID;
    }
}

Gem::Result TaskManager::AddDependency(TaskID taskId, TaskID dependencyId)
{
    if (taskId == NullTaskID || dependencyId == NullTaskID)
    {
        return Gem::Result::InvalidArg;
    }
    
    std::lock_guard<std::mutex> lock(m_Mutex);
    
    TaskHeader* header = FindTaskHeader(taskId);
    if (header == nullptr)
    {
        return Gem::Result::NotFound;
    }
    
    if (header->State != TaskState::Unscheduled)
    {
        // Cannot add dependencies once scheduled
        return Gem::Result::InvalidArg;
    }
    
    // Verify the dependency exists (either ring-backed or an immediate task)
    TaskHeader* depHeader = FindTaskHeader(dependencyId);
    const bool isImmediateCompleted = (m_CompletedImmediate.find(dependencyId) != m_CompletedImmediate.end());
    const bool isImmediateExecuting = (m_ExecutingImmediate.find(dependencyId) != m_ExecutingImmediate.end());
    if (depHeader == nullptr && !isImmediateCompleted && !isImmediateExecuting)
    {
        return Gem::Result::NotFound;
    }
    
    // Dependency can be in any state - we'll validate at schedule time that it's at least scheduled
    
    // Check if we have space for more dependencies
    if (header->ActualDependencyCount >= header->MaxDependencyCount)
    {
        return Gem::Result::InvalidArg;
    }
    
    // Add the dependency
    TaskID* deps = header->GetDependencies();
    deps[header->ActualDependencyCount] = dependencyId;
    header->ActualDependencyCount++;
    
    return Gem::Result::Success;
}

Gem::Result TaskManager::EnqueueTask(TaskID taskId)
{
    if (taskId == NullTaskID)
    {
        return Gem::Result::InvalidArg;
    }
    
    std::lock_guard<std::mutex> lock(m_Mutex);
    
    // Completed-immediate tasks: scheduling is a no-op
    if (m_CompletedImmediate.find(taskId) != m_CompletedImmediate.end())
    {
        return Gem::Result::Success;
    }
    
    TaskHeader* header = FindTaskHeader(taskId);
    if (header == nullptr)
    {
        return Gem::Result::NotFound;
    }
    
    if (header->State != TaskState::Unscheduled)
    {
        // Task already scheduled or retired
        return Gem::Result::InvalidArg;
    }
    
    // Verify all dependencies that have been added are at least scheduled
    const TaskID* deps = header->GetDependencies();
    uint32_t outstandingCount = 0;
    
    for (uint32_t i = 0; i < header->ActualDependencyCount; ++i)
    {
        TaskID depId = deps[i];
        if (depId == NullTaskID)
        {
            continue; // Skip unset dependencies
        }
        
        TaskHeader* depHeader = FindTaskHeader(depId);
        const bool depImmediateCompleted = (m_CompletedImmediate.find(depId) != m_CompletedImmediate.end());
        const bool depImmediateExecuting = (m_ExecutingImmediate.find(depId) != m_ExecutingImmediate.end());
        if ((depHeader == nullptr && !depImmediateCompleted && !depImmediateExecuting) || (depHeader != nullptr && depHeader->State == TaskState::Unscheduled))
        {
            // Dependency not found or not yet at least scheduled
            return Gem::Result::InvalidArg;
        }
        
        // Track this task as dependent on the dependency
        // Only count as outstanding if dependency hasn't completed yet
        if ((depHeader != nullptr && depHeader->State != TaskState::Completed) || depImmediateExecuting)
        {
            m_Dependents[depId].push_back(taskId);
            outstandingCount++;
        }
    }
    
    // Set outstanding dependency count
    header->OutstandingDependencies = outstandingCount;
    
    // Mark as scheduled
    header->State = TaskState::Scheduled;
    
    // If no outstanding dependencies, move to Ready state and execute callback
    if (outstandingCount == 0)
    {
        header->State = TaskState::Ready;
        header->State = TaskState::Executing;
        
        TaskFunc callback = header->Function;
        void* payload = header->GetPayload();
        size_t payloadSize = header->PayloadSize;
        
        // Unlock mutex before calling callback to avoid deadlock
        m_Mutex.unlock();
        callback(taskId, payload, payloadSize, *this);
        m_Mutex.lock();
    }
    
    return Gem::Result::Success;
}

bool TaskManager::IsTaskInState(TaskID taskId, TaskState state) const
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    
    // Immediate-completed tasks report Completed state
    if (state == TaskState::Completed && m_CompletedImmediate.find(taskId) != m_CompletedImmediate.end())
    {
        return true;
    }
    
    TaskHeader* header = FindTaskHeader(taskId);
    if (header == nullptr)
    {
        return false;
    }
    
    return header->State == state;
}

TaskState TaskManager::GetTaskState(TaskID taskId) const
{
    if (taskId == NullTaskID)
    {
        return TaskState::Invalid;
    }
    
    // Fast path: Check completed high-water mark (lock-free)
    if (taskId <= m_CompletedHighWater.load(std::memory_order_acquire))
    {
        return TaskState::Completed;
    }
    
    std::lock_guard<std::mutex> lock(m_Mutex);
    
    // Check completed-immediate hash map (for tasks above high-water mark)
    if (m_CompletedImmediate.find(taskId) != m_CompletedImmediate.end())
    {
        return TaskState::Completed;
    }
    
    TaskHeader* header = FindTaskHeader(taskId);
    if (header == nullptr)
    {
        return TaskState::Invalid;
    }
    
    return header->State;
}

void TaskManager::RetireCompletedTasks()
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    
    // Collect TaskIDs that will be retired
    std::vector<TaskID> retiredTaskIds;
    
    // Group tasks by ring and find head tasks (lowest address in each ring)
    std::unordered_map<RingBuffer*, std::vector<std::pair<TaskID, const TaskAllocation*>>> tasksByRing;
    
    for (const auto& [taskId, alloc] : m_TaskMap)
    {
        tasksByRing[alloc.Ring].push_back({ taskId, &alloc });
    }
    
    // For each ring, sort tasks by address and retire from head
    for (auto& ringPtr : m_Rings)
    {
        RingBuffer* ring = ringPtr.get();
        
        auto& tasks = tasksByRing[ring];
        if (tasks.empty())
        {
            continue;
        }
        
        // Sort by header address (ascending) to find tasks in allocation order
        std::sort(tasks.begin(), tasks.end(), [](const auto& a, const auto& b) {
            return a.second->Header < b.second->Header;
        });
        
        // Retire consecutive head tasks that are in Completed state
        for (const auto& [taskId, allocPtr] : tasks)
        {
            if (allocPtr->Header->State != TaskState::Completed)
            {
                break; // Head task not completed yet, can't retire further
            }
            
            // Retire this task from the ring
            if (ring->RetireHead(allocPtr->Size))
            {
                retiredTaskIds.push_back(taskId);
            }
            else
            {
                break; // Something went wrong
            }
        }
    }
    
    // Remove retired tasks from the map
    for (TaskID taskId : retiredTaskIds)
    {
        m_TaskMap.erase(taskId);
    }
    
    // Update retired high-water mark
    // Find the highest contiguous retired TaskID
    if (!retiredTaskIds.empty())
    {
        std::sort(retiredTaskIds.begin(), retiredTaskIds.end());
        
        TaskID currentHighWater = m_RetiredHighWater.load(std::memory_order_acquire);
        TaskID newHighWater = currentHighWater;
        
        for (TaskID id : retiredTaskIds)
        {
            if (id == newHighWater + 1)
            {
                newHighWater = id;
            }
            else if (id > newHighWater + 1)
            {
                break; // Gap found, can't advance further
            }
        }
        
        if (newHighWater > currentHighWater)
        {
            m_RetiredHighWater.store(newHighWater, std::memory_order_release);
        }
    }
    
    // Clean up completed-immediate hash map
    // All tasks in m_CompletedImmediate are completed and can be safely removed.
    // They were added via the ultra-fast path and don't need ring buffer retirement.
    // Just clear the entire set since completed tasks don't need to be tracked anymore.
    m_CompletedImmediate.clear();
    
    // Remove empty rings (keep at least one)
    auto ringIt = m_Rings.begin();
    while (ringIt != m_Rings.end())
    {
        if ((*ringIt)->IsEmpty() && m_Rings.size() > 1)
        {
            ringIt = m_Rings.erase(ringIt);
        }
        else
        {
            ++ringIt;
        }
    }
}

TaskManager::Statistics TaskManager::GetStatistics() const
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    
    Statistics stats{};
    stats.RingCount = m_Rings.size();
    stats.ActiveTaskCount = m_TaskMap.size();
    stats.NextTaskId = m_NextTaskId;
    
    // Count executing tasks
    stats.ExecutingTaskCount = 0;
    for (const auto& [id, alloc] : m_TaskMap)
    {
        if (alloc.Header->State == TaskState::Executing)
        {
            stats.ExecutingTaskCount++;
        }
    }
    
    for (const auto& ring : m_Rings)
    {
        stats.TotalAllocated += ring->GetSize();
        stats.TotalUsed += ring->GetUsedSize();
    }
    
    return stats;
}

void TaskManager::Reset()
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    
    // Clear all tasks and rings
    m_TaskMap.clear();
    m_Rings.clear();
    m_Dependents.clear();
    m_CompletedImmediate.clear();
    m_ExecutingImmediate.clear();
    
    // Reset task ID counter
    m_NextTaskId = 1;
    
    // Reset high-water marks
    m_CompletedHighWater.store(0, std::memory_order_release);
    m_RetiredHighWater.store(0, std::memory_order_release);
    
    // Reset ring size
    m_NextRingSize = m_InitialRingSize;
    
    try
    {
        // Allocate a fresh initial ring
        m_Rings.push_back(std::make_unique<RingBuffer>(m_InitialRingSize));
    }
    catch (...)
    {
        // If we can't allocate, leave the scheduler in an empty but valid state
        // The next allocation will attempt to create a ring
    }
}

void* TaskManager::GetPayload(TaskID taskId)
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    
    TaskHeader* header = FindTaskHeader(taskId);
    if (header == nullptr)
    {
        return nullptr;
    }
    
    return header->GetPayload();
}

Gem::Result TaskManager::CompleteTask(TaskID taskId)
{
    if (taskId == NullTaskID)
    {
        return Gem::Result::InvalidArg;
    }
    
    std::lock_guard<std::mutex> lock(m_Mutex);
    
    // Immediate-completed tasks: CompleteTask is a no-op
    if (m_CompletedImmediate.find(taskId) != m_CompletedImmediate.end())
    {
        return Gem::Result::Success;
    }
    // Immediate-executing tasks: transition to completed and notify dependents
    auto execIt = m_ExecutingImmediate.find(taskId);
    if (execIt != m_ExecutingImmediate.end())
    {
        m_ExecutingImmediate.erase(execIt);
        m_CompletedImmediate.insert(taskId);
        NotifyDependents(taskId);
        return Gem::Result::Success;
    }
    
    TaskHeader* header = FindTaskHeader(taskId);
    if (header == nullptr)
    {
        return Gem::Result::NotFound;
    }
    
    if (header->State != TaskState::Executing)
    {
        // Can only complete tasks that are executing
        return Gem::Result::InvalidArg;
    }
    
    // Mark as completed
    header->State = TaskState::Completed;
    
    // Notify dependent tasks and check if they can now execute
    NotifyDependents(taskId);
    
    return Gem::Result::Success;
}

void TaskManager::CheckAndExecuteTask(TaskID taskId)
{
    // Assumes mutex is held
    
    TaskHeader* header = FindTaskHeader(taskId);
    if (header == nullptr || header->State != TaskState::Scheduled)
    {
        return; // Task not found or not in Scheduled state
    }
    
    // Check if outstanding dependencies have reached zero
    if (header->OutstandingDependencies == 0)
    {
        header->State = TaskState::Ready;
        header->State = TaskState::Executing;
        
        TaskFunc callback = header->Function;
        void* payload = header->GetPayload();
        size_t payloadSize = header->PayloadSize;
        
        // Unlock mutex before calling callback to avoid deadlock
        m_Mutex.unlock();
        callback(taskId, payload, payloadSize, *this);
        m_Mutex.lock();
    }
}

void TaskManager::NotifyDependents(TaskID taskId)
{
    // Assumes mutex is held
    
    auto it = m_Dependents.find(taskId);
    if (it == m_Dependents.end())
    {
        return; // No dependents
    }
    
    // Decrement outstanding dependency count for each dependent
    for (TaskID dependentId : it->second)
    {
        TaskHeader* depHeader = FindTaskHeader(dependentId);
        if (depHeader != nullptr && depHeader->OutstandingDependencies > 0)
        {
            depHeader->OutstandingDependencies--;
            CheckAndExecuteTask(dependentId);
        }
    }
}

RingBuffer* TaskManager::AllocateNewRing()
{
    try
    {
        auto newRing = std::make_unique<RingBuffer>(m_NextRingSize);
        RingBuffer* ringPtr = newRing.get();
        m_Rings.push_back(std::move(newRing));
        
        // Double the size for next ring (exponential growth)
        m_NextRingSize *= 2;
        
        return ringPtr;
    }
    catch (...)
    {
        return nullptr;
    }
}

TaskManager::TaskHeader* TaskManager::FindTaskHeader(TaskID taskId) const
{
    auto it = m_TaskMap.find(taskId);
    if (it == m_TaskMap.end())
    {
        return nullptr;
    }
    return it->second.Header;
}

TaskID TaskManager::GenerateTaskID()
{
    return m_NextTaskId++;
}

} // namespace Canvas
