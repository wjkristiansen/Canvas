// TaskScheduler.cpp

#include "TaskScheduler.h"
#include <algorithm>
#include <cassert>

namespace Canvas
{

//------------------------------------------------------------------------------------------------
// TaskScheduler - Task Size Calculation
//------------------------------------------------------------------------------------------------

size_t TaskScheduler::CalculateTaskSize(size_t payloadSize, uint32_t dependencyCount)
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
// TaskScheduler Implementation
//------------------------------------------------------------------------------------------------

TaskScheduler::TaskScheduler(size_t initialRingSize)
    : m_NextTaskId(1) // Start at 1, reserve 0 for InvalidTaskID
    , m_InitialRingSize(initialRingSize)
    , m_NextRingSize(initialRingSize)
{
    if (initialRingSize == 0)
    {
        throw Gem::GemError(Gem::Result::InvalidArg);
    }
    
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

TaskScheduler::~TaskScheduler()
{
    // RAII: all resources cleaned up automatically
}

TaskID TaskScheduler::AllocateTask(
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
                return InvalidTaskID;
            }
            
            ptr = newRing->TryAllocate(taskSize);
            
            if (ptr == nullptr)
            {
                // Task is too large even for a new ring
                return InvalidTaskID;
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
        
        // Initialize dependency array to InvalidTaskID
        if (maxDependencyCount > 0)
        {
            TaskID* deps = header->GetDependencies();
            std::fill(deps, deps + maxDependencyCount, InvalidTaskID);
        }
        
        // Register in task map with allocation info
        m_TaskMap[taskId] = TaskAllocation{ header, allocatedRing, taskSize };
        
        return taskId;
    }
    catch (const std::bad_alloc&)
    {
        return InvalidTaskID;
    }
    catch (...)
    {
        return InvalidTaskID;
    }
}

Gem::Result TaskScheduler::AddDependency(TaskID taskId, TaskID dependencyId)
{
    if (taskId == InvalidTaskID || dependencyId == InvalidTaskID)
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
    
    // Verify the dependency exists
    TaskHeader* depHeader = FindTaskHeader(dependencyId);
    if (depHeader == nullptr)
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

Gem::Result TaskScheduler::ScheduleTask(TaskID taskId)
{
    if (taskId == InvalidTaskID)
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
        // Task already scheduled or retired
        return Gem::Result::InvalidArg;
    }
    
    // Verify all dependencies that have been added are at least scheduled
    const TaskID* deps = header->GetDependencies();
    uint32_t outstandingCount = 0;
    
    for (uint32_t i = 0; i < header->ActualDependencyCount; ++i)
    {
        TaskID depId = deps[i];
        if (depId == InvalidTaskID)
        {
            continue; // Skip unset dependencies
        }
        
        TaskHeader* depHeader = FindTaskHeader(depId);
        if (depHeader == nullptr || depHeader->State == TaskState::Unscheduled)
        {
            // Dependency not found or not yet at least scheduled
            return Gem::Result::InvalidArg;
        }
        
        // Track this task as dependent on the dependency
        // Only count as outstanding if dependency hasn't completed yet
        if (depHeader->State != TaskState::Completed && depHeader->State != TaskState::Retired)
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

bool TaskScheduler::IsTaskInState(TaskID taskId, TaskState state) const
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    
    TaskHeader* header = FindTaskHeader(taskId);
    if (header == nullptr)
    {
        return false;
    }
    
    return header->State == state;
}

Gem::Result TaskScheduler::GetTaskState(TaskID taskId, TaskState& outState) const
{
    if (taskId == InvalidTaskID)
    {
        return Gem::Result::InvalidArg;
    }
    
    std::lock_guard<std::mutex> lock(m_Mutex);
    
    TaskHeader* header = FindTaskHeader(taskId);
    if (header == nullptr)
    {
        return Gem::Result::NotFound;
    }
    
    outState = header->State;
    return Gem::Result::Success;
}

void TaskScheduler::ProcessRetirements()
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
        
        // Retire consecutive head tasks that are in Retired state
        for (const auto& [taskId, allocPtr] : tasks)
        {
            if (allocPtr->Header->State != TaskState::Retired)
            {
                break; // Head task not retired yet, can't retire further
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

TaskScheduler::Statistics TaskScheduler::GetStatistics() const
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

void TaskScheduler::Reset()
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    
    // Clear all tasks and rings
    m_TaskMap.clear();
    m_Rings.clear();
    m_Dependents.clear();
    
    // Reset task ID counter
    m_NextTaskId = 1;
    
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

void* TaskScheduler::GetPayload(TaskID taskId)
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    
    TaskHeader* header = FindTaskHeader(taskId);
    if (header == nullptr)
    {
        return nullptr;
    }
    
    return header->GetPayload();
}

Gem::Result TaskScheduler::CompleteTask(TaskID taskId)
{
    if (taskId == InvalidTaskID)
    {
        return Gem::Result::InvalidArg;
    }
    
    std::lock_guard<std::mutex> lock(m_Mutex);
    
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

void TaskScheduler::CheckAndExecuteTask(TaskID taskId)
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

void TaskScheduler::NotifyDependents(TaskID taskId)
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

RingBuffer* TaskScheduler::AllocateNewRing()
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

TaskScheduler::TaskHeader* TaskScheduler::FindTaskHeader(TaskID taskId) const
{
    auto it = m_TaskMap.find(taskId);
    if (it == m_TaskMap.end())
    {
        return nullptr;
    }
    return it->second.Header;
}

TaskID TaskScheduler::GenerateTaskID()
{
    return m_NextTaskId++;
}

} // namespace Canvas
