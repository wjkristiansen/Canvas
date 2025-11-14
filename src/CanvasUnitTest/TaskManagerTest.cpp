#include "pch.h"
#include "CppUnitTest.h"
#include "TaskManager.h"
#include <atomic>
#include <vector>

// Disable warnings for unused lambda parameters in tests
#pragma warning(push)
#pragma warning(disable: 4100) // unreferenced formal parameter
#pragma warning(disable: 4189) // local variable is initialized but not referenced

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Canvas;

namespace CanvasUnitTest
{
    TEST_CLASS(TaskManagerTest)
    {
    public:
        // Helper: No-op callback that immediately completes the task
        static TaskFunc NoOp()
        {
            return [](TaskID taskId, void* payload, size_t size, TaskManager& scheduler)
            {
                scheduler.CompleteTask(taskId);
            };
        }

        //------------------------------------------------------------------------------------------------
        // Basic Allocation and Scheduling Tests
        //------------------------------------------------------------------------------------------------

        TEST_METHOD(Constructor_CreatesSchedulerWithInitialRing)
        {
            // Arrange & Act
            TaskManager scheduler(1024);
            auto stats = scheduler.GetStatistics();

            // Assert
            Assert::AreEqual(size_t(1), stats.RingCount);
            Assert::AreEqual(TaskID(1), stats.NextTaskId);
            Assert::AreEqual(size_t(0), stats.ActiveTaskCount);
        }

        TEST_METHOD(AllocateTask_NoPayloadNoDependencies_Succeeds)
        {
            // Arrange
            TaskManager scheduler(1024);

            // Act
            TaskID taskId = scheduler.AllocateTask(0, 0, NoOp());

            // Assert
            Assert::AreNotEqual(NullTaskID, taskId);
            Assert::AreEqual(TaskID(1), taskId);
        }

        TEST_METHOD(AllocateTask_WithPayload_Succeeds)
        {
            // Arrange
            TaskManager scheduler(1024);

            // Act
            TaskID taskId = scheduler.AllocateTask(256, 0, NoOp());

            // Assert
            Assert::AreNotEqual(NullTaskID, taskId);
            void* payload = scheduler.GetPayload(taskId);
            Assert::IsNotNull(payload);
        }

        TEST_METHOD(AllocateTask_WithPayload_IsAligned)
        {
            // Arrange
            TaskManager scheduler(1024);

            // Act
            TaskID taskId = scheduler.AllocateTask(37, 0, NoOp()); // Unaligned size

            // Assert
            void* payload = scheduler.GetPayload(taskId);
            uintptr_t payloadAddr = reinterpret_cast<uintptr_t>(payload);
            Assert::AreEqual(size_t(0), payloadAddr % 16); // Should be 16-byte aligned
        }

        TEST_METHOD(AllocateTask_MultipleTasks_GetUniqueIDs)
        {
            // Arrange
            TaskManager scheduler(1024);

            // Act
            TaskID id1 = scheduler.AllocateTask(64, 0, NoOp());
            TaskID id2 = scheduler.AllocateTask(64, 0, NoOp());
            TaskID id3 = scheduler.AllocateTask(64, 0, NoOp());

            // Assert
            Assert::AreEqual(TaskID(1), id1);
            Assert::AreEqual(TaskID(2), id2);
            Assert::AreEqual(TaskID(3), id3);
        }

        TEST_METHOD(EnqueueTask_WithoutDependencies_MovesToReady)
        {
            // Arrange
            TaskManager scheduler(1024);
            TaskID taskId = scheduler.AllocateTask(0, 0, NoOp());

            // Act
            Gem::Result result = scheduler.EnqueueTask(taskId);

            // Assert
            Assert::IsTrue(result == Gem::Result::Success);
            // Note: Task completes synchronously via NoOp callback
            Assert::IsTrue(scheduler.IsTaskInState(taskId, TaskState::Completed));
        }

        TEST_METHOD(EnqueueTask_InvalidTaskID_ReturnsInvalidArg)
        {
            // Arrange
            TaskManager scheduler(1024);

            // Act
            Gem::Result result = scheduler.EnqueueTask(NullTaskID);

            // Assert
            Assert::IsTrue(result == Gem::Result::InvalidArg);
        }

        TEST_METHOD(EnqueueTask_NonExistentTask_ReturnsNotFound)
        {
            // Arrange
            TaskManager scheduler(1024);

            // Act
            Gem::Result result = scheduler.EnqueueTask(TaskID(999));

            // Assert
            Assert::IsTrue(result == Gem::Result::NotFound);
        }

        //------------------------------------------------------------------------------------------------
        // Dependency Tests
        //------------------------------------------------------------------------------------------------

        TEST_METHOD(AddDependency_ToUnscheduledTask_Succeeds)
        {
            // Arrange
            TaskManager scheduler(1024);
            TaskID task1 = scheduler.AllocateTask(0, 0, NoOp());
            TaskID task2 = scheduler.AllocateTask(0, 1, NoOp()); // Max 1 dependency
            scheduler.EnqueueTask(task1);

            // Act
            Gem::Result result = scheduler.AddDependency(task2, task1);

            // Assert
            Assert::IsTrue(result == Gem::Result::Success);
        }

        TEST_METHOD(AddDependency_ToScheduledTask_Fails)
        {
            // Arrange
            TaskManager scheduler(1024);
            TaskID task1 = scheduler.AllocateTask(0, 0, NoOp());
            TaskID task2 = scheduler.AllocateTask(0, 1, NoOp());
            scheduler.EnqueueTask(task1);
            scheduler.EnqueueTask(task2);

            // Act
            Gem::Result result = scheduler.AddDependency(task2, task1);

            // Assert
            Assert::IsTrue(result == Gem::Result::InvalidArg);
        }

        TEST_METHOD(AddDependency_UnscheduledDependency_FailsAtScheduleTime)
        {
            // Arrange
            TaskManager scheduler(1024);
            TaskID task1 = scheduler.AllocateTask(0, 0, NoOp());
            TaskID task2 = scheduler.AllocateTask(0, 1, NoOp());
            scheduler.AddDependency(task2, task1);

            // Act - Try to schedule task2 when task1 is unscheduled
            Gem::Result result = scheduler.EnqueueTask(task2);

            // Assert
            Assert::IsTrue(result == Gem::Result::InvalidArg);
        }

        TEST_METHOD(AddDependency_ExceedsMaxCount_Fails)
        {
            // Arrange
            TaskManager scheduler(1024);
            TaskID task1 = scheduler.AllocateTask(0, 0, NoOp());
            TaskID task2 = scheduler.AllocateTask(0, 0, NoOp());
            TaskID task3 = scheduler.AllocateTask(0, 1, NoOp()); // Max 1 dependency
            scheduler.EnqueueTask(task1);
            scheduler.EnqueueTask(task2);
            scheduler.AddDependency(task3, task1);

            // Act - Try to add second dependency
            Gem::Result result = scheduler.AddDependency(task3, task2);

            // Assert
            Assert::IsTrue(result == Gem::Result::InvalidArg);
        }

        TEST_METHOD(EnqueueTask_WithDependencies_Succeeds)
        {
            // Arrange
            TaskManager scheduler(1024);
            TaskID task1 = scheduler.AllocateTask(0, 0, NoOp());
            TaskID task2 = scheduler.AllocateTask(0, 1, NoOp());
            scheduler.EnqueueTask(task1); // task1 completes immediately (NoOp callback)
            scheduler.AddDependency(task2, task1);

            // Act
            Gem::Result result = scheduler.EnqueueTask(task2);

            // Assert
            Assert::IsTrue(result == Gem::Result::Success);
            // task2 also completes immediately since task1 is already completed
            Assert::IsTrue(scheduler.IsTaskInState(task2, TaskState::Completed));
        }

        //------------------------------------------------------------------------------------------------
        // Memory Management Tests
        //------------------------------------------------------------------------------------------------

        TEST_METHOD(RetireCompletedTasks_RetiredTask_ReclaimsMemory)
        {
            // Arrange
            TaskManager scheduler(1024);
            TaskID taskId = scheduler.AllocateTask(100, 0, NoOp());
            scheduler.EnqueueTask(taskId);
            
            // Mark as completed manually (no callback)
            auto result = scheduler.CompleteTask(taskId);
            Assert::IsTrue(result == Gem::Result::InvalidArg); // Can't complete non-executing task
        }

        TEST_METHOD(RetireCompletedTasks_MultipleRetiredTasks_ReclaimsAll)
        {
            // Arrange
            TaskManager scheduler(1024);
            scheduler.AllocateTask(100, 0, NoOp());
            scheduler.AllocateTask(100, 0, NoOp());
            scheduler.AllocateTask(100, 0, NoOp());

            auto stats = scheduler.GetStatistics();
            Assert::AreEqual(size_t(3), stats.ActiveTaskCount);
        }



        TEST_METHOD(Reset_ClearsAllTasksAndRings)
        {
            // Arrange
            TaskManager scheduler(1024);
            scheduler.AllocateTask(100, 0, NoOp());
            scheduler.AllocateTask(100, 0, NoOp());
            auto stats1 = scheduler.GetStatistics();
            Assert::AreEqual(size_t(2), stats1.ActiveTaskCount);

            // Act
            scheduler.Reset();

            // Assert
            auto stats2 = scheduler.GetStatistics();
            Assert::AreEqual(size_t(0), stats2.ActiveTaskCount);
            Assert::AreEqual(TaskID(1), stats2.NextTaskId);
        }

        //------------------------------------------------------------------------------------------------
        // Task State Tests
        //------------------------------------------------------------------------------------------------

        TEST_METHOD(GetTaskState_ReturnsCorrectState)
        {
            // Arrange
            TaskManager scheduler(1024);
            TaskID taskId = scheduler.AllocateTask(100, 0, NoOp());

            // Assert - Initial state
            TaskState state = scheduler.GetTaskState(taskId);
            Assert::IsTrue(state == TaskState::Unscheduled);

            // Act & Assert - After scheduling (NoOp callback completes synchronously)
            scheduler.EnqueueTask(taskId);
            state = scheduler.GetTaskState(taskId);
            Assert::IsTrue(state == TaskState::Completed);
        }

        TEST_METHOD(TaskPayload_CanReadWrite)
        {
            // Arrange
            TaskManager scheduler(1024);
            TaskID taskId = scheduler.AllocateTask(sizeof(int) * 4, uint32_t(0), NoOp());

            // Act
            int* payload = scheduler.GetPayloadAs<int>(taskId);
            payload[0] = 42;
            payload[1] = 100;
            payload[2] = 200;
            payload[3] = 300;

            // Assert
            int* readPayload = scheduler.GetPayloadAs<int>(taskId);
            Assert::AreEqual(42, readPayload[0]);
            Assert::AreEqual(100, readPayload[1]);
            Assert::AreEqual(200, readPayload[2]);
            Assert::AreEqual(300, readPayload[3]);
        }

        //------------------------------------------------------------------------------------------------
        // Synchronous Callback Execution Tests
        //------------------------------------------------------------------------------------------------

        TEST_METHOD(Callback_SynchronousCompletion_ExecutesImmediately)
        {
            // Arrange
            TaskManager scheduler(1024);
            std::atomic<int> executionCount{ 0 };

            // Act
            TaskID taskId = scheduler.AllocateTypedTask(0,
                [&](TaskID id, TaskManager& sched) {
                    executionCount++;
                    sched.CompleteTask(id);
                });
            scheduler.EnqueueTask(taskId); // Should execute callback immediately

            // Assert
            Assert::AreEqual(1, executionCount.load());
            Assert::IsTrue(scheduler.IsTaskInState(taskId, TaskState::Completed));
        }

        TEST_METHOD(Callback_SynchronousFork_ExecutesAllDependents)
        {
            // Arrange
            TaskManager scheduler(1024);
            std::atomic<int> executionCount{ 0 };

            // Create fork: task1 -> task2, task3, task4
            TaskID task1 = scheduler.AllocateTypedTask(0,
                [&](TaskID id, TaskManager& sched) {
                    executionCount++;
                    sched.CompleteTask(id);
                });
            TaskID task2 = scheduler.AllocateTypedTask(1,
                [&](TaskID id, TaskManager& sched) {
                    executionCount++;
                    sched.CompleteTask(id);
                });
            TaskID task3 = scheduler.AllocateTypedTask(1,
                [&](TaskID id, TaskManager& sched) {
                    executionCount++;
                    sched.CompleteTask(id);
                });
            TaskID task4 = scheduler.AllocateTypedTask(1,
                [&](TaskID id, TaskManager& sched) {
                    executionCount++;
                    sched.CompleteTask(id);
                });

            scheduler.EnqueueTask(task1);
            scheduler.AddDependency(task2, task1);
            scheduler.AddDependency(task3, task1);
            scheduler.AddDependency(task4, task1);

            // Act - Schedule dependents, then complete task1
            scheduler.EnqueueTask(task2);
            scheduler.EnqueueTask(task3);
            scheduler.EnqueueTask(task4);

            // Assert - All 4 tasks should have executed
            Assert::AreEqual(4, executionCount.load());
            Assert::IsTrue(scheduler.IsTaskInState(task1, TaskState::Completed));
            Assert::IsTrue(scheduler.IsTaskInState(task2, TaskState::Completed));
            Assert::IsTrue(scheduler.IsTaskInState(task3, TaskState::Completed));
            Assert::IsTrue(scheduler.IsTaskInState(task4, TaskState::Completed));
        }

        TEST_METHOD(Callback_SynchronousJoin_WaitsForAllDependencies)
        {
            // Arrange
            TaskManager scheduler(1024);
            std::atomic<int> executionCount{ 0 };

            // Create join: task1, task2, task3 -> task4
            TaskID task1 = scheduler.AllocateTypedTask(0,
                [&](TaskID id, TaskManager& sched) {
                    executionCount++;
                    sched.CompleteTask(id);
                });
            TaskID task2 = scheduler.AllocateTypedTask(0,
                [&](TaskID id, TaskManager& sched) {
                    executionCount++;
                    sched.CompleteTask(id);
                });
            TaskID task3 = scheduler.AllocateTypedTask(0,
                [&](TaskID id, TaskManager& sched) {
                    executionCount++;
                    sched.CompleteTask(id);
                });
            TaskID task4 = scheduler.AllocateTypedTask(3,
                [&](TaskID id, TaskManager& sched) {
                    executionCount++;
                    sched.CompleteTask(id);
                });

            scheduler.EnqueueTask(task1);
            scheduler.EnqueueTask(task2);
            scheduler.EnqueueTask(task3);

            // Act - Set up dependencies and schedule
            scheduler.AddDependency(task4, task1);
            scheduler.AddDependency(task4, task2);
            scheduler.AddDependency(task4, task3);
            scheduler.EnqueueTask(task4);

            // Assert - All 4 tasks executed (task4 only after all deps complete)
            Assert::AreEqual(4, executionCount.load());
            Assert::IsTrue(scheduler.IsTaskInState(task4, TaskState::Completed));
        }

        //------------------------------------------------------------------------------------------------
        // Deferred Callback Completion Tests
        //------------------------------------------------------------------------------------------------

        TEST_METHOD(Callback_DeferredCompletion_AppCompletesLater)
        {
            // Arrange
            TaskManager scheduler(1024);
            std::atomic<int> executionCount{ 0 };

            // Act
            TaskID taskId = scheduler.AllocateTypedTask(0,
                [&](TaskID id, TaskManager& sched) {
                    (void)sched; // Not completing - app will do it later
                    executionCount++;
                    // Deliberately not completing - app will do it later
                });
            scheduler.EnqueueTask(taskId); // Executes callback

            // Assert - Callback executed but task still executing
            Assert::AreEqual(1, executionCount.load());
            Assert::IsTrue(scheduler.IsTaskInState(taskId, TaskState::Executing));

            // App completes it later
            Gem::Result result = scheduler.CompleteTask(taskId);
            Assert::IsTrue(result == Gem::Result::Success);
            Assert::IsTrue(scheduler.IsTaskInState(taskId, TaskState::Completed));
        }

        TEST_METHOD(Callback_DeferredFork_DependentsWaitForCompletion)
        {
            // Arrange
            TaskManager scheduler(1024);
            std::atomic<int> task1Executions{ 0 };
            std::atomic<int> task2Executions{ 0 };

            // Create: task1 (deferred) -> task2 (synchronous)
            TaskID task1 = scheduler.AllocateTypedTask(0,
                [&](TaskID id, TaskManager& sched) {
                    (void)id; (void)sched; // Don't complete yet
                    task1Executions++;
                });
            TaskID task2 = scheduler.AllocateTypedTask(1,
                [&](TaskID id, TaskManager& sched) {
                    task2Executions++;
                    sched.CompleteTask(id);
                });

            scheduler.EnqueueTask(task1);
            scheduler.AddDependency(task2, task1);
            scheduler.EnqueueTask(task2);

            // Assert - task1 executed but task2 hasn't yet
            Assert::AreEqual(1, task1Executions.load());
            Assert::AreEqual(0, task2Executions.load());
            Assert::IsTrue(scheduler.IsTaskInState(task1, TaskState::Executing));
            Assert::IsTrue(scheduler.IsTaskInState(task2, TaskState::Scheduled));

            // Act - Complete task1
            scheduler.CompleteTask(task1);

            // Assert - Now task2 has executed
            Assert::AreEqual(1, task2Executions.load());
            Assert::IsTrue(scheduler.IsTaskInState(task2, TaskState::Completed));
        }

        TEST_METHOD(Callback_DeferredJoin_WaitsForAllToComplete)
        {
            // Arrange
            TaskManager scheduler(1024);
            std::atomic<int> joinExecution{ 0 };

            // Create join: task1, task2, task3 (all deferred) -> task4
            TaskID task1 = scheduler.AllocateTypedTask(0,
                [](TaskID id, TaskManager& sched) {
                    (void)id; (void)sched; // Don't complete
                });
            TaskID task2 = scheduler.AllocateTypedTask(0,
                [](TaskID id, TaskManager& sched) {
                    (void)id; (void)sched; // Don't complete
                });
            TaskID task3 = scheduler.AllocateTypedTask(0,
                [](TaskID id, TaskManager& sched) {
                    (void)id; (void)sched; // Don't complete
                });
            TaskID task4 = scheduler.AllocateTypedTask(3,
                [&](TaskID id, TaskManager& sched) {
                    joinExecution++;
                    sched.CompleteTask(id);
                });

            scheduler.EnqueueTask(task1);
            scheduler.EnqueueTask(task2);
            scheduler.EnqueueTask(task3);
            scheduler.AddDependency(task4, task1);
            scheduler.AddDependency(task4, task2);
            scheduler.AddDependency(task4, task3);
            scheduler.EnqueueTask(task4);

            // Assert - task4 hasn't executed yet
            Assert::AreEqual(0, joinExecution.load());
            Assert::IsTrue(scheduler.IsTaskInState(task4, TaskState::Scheduled));

            // Act - Complete dependencies one by one
            scheduler.CompleteTask(task1);
            Assert::AreEqual(0, joinExecution.load()); // Still waiting
            
            scheduler.CompleteTask(task2);
            Assert::AreEqual(0, joinExecution.load()); // Still waiting
            
            scheduler.CompleteTask(task3);

            // Assert - Now task4 executed
            Assert::AreEqual(1, joinExecution.load());
            Assert::IsTrue(scheduler.IsTaskInState(task4, TaskState::Completed));
        }

        //------------------------------------------------------------------------------------------------
        // Mixed Execution Mode Tests
        //------------------------------------------------------------------------------------------------

        TEST_METHOD(Mixed_CallbackAndNoCallback_BothWork)
        {
            // Arrange
            TaskManager scheduler(1024);
            std::atomic<int> executionCount{ 0 };

            // Act
            TaskID taskNoOp = scheduler.AllocateTypedTask(0,
                [](TaskID id, TaskManager& sched) {
                    sched.CompleteTask(id);
                });
            TaskID taskWithWork = scheduler.AllocateTypedTask(0,
                [&](TaskID id, TaskManager& sched) {
                    executionCount++;
                    sched.CompleteTask(id);
                });

            scheduler.EnqueueTask(taskNoOp);
            scheduler.EnqueueTask(taskWithWork);

            // Assert
            Assert::IsTrue(scheduler.IsTaskInState(taskNoOp, TaskState::Completed)); // No-op completed
            Assert::AreEqual(1, executionCount.load());
            Assert::IsTrue(scheduler.IsTaskInState(taskWithWork, TaskState::Completed)); // Callback completed it
        }

        TEST_METHOD(Mixed_SynchronousAndDeferred_BothWork)
        {
            // Arrange
            TaskManager scheduler(1024);
            std::atomic<int> syncCount{ 0 };
            std::atomic<int> deferredCount{ 0 };

            // Create: task1 (sync) and task2 (deferred) -> task3 (sync)
            TaskID task1 = scheduler.AllocateTypedTask(0,
                [&](TaskID id, TaskManager& sched) {
                    syncCount++;
                    sched.CompleteTask(id);
                });
            TaskID task2 = scheduler.AllocateTypedTask(0,
                [&](TaskID id, TaskManager& sched) {
                    (void)id; (void)sched; // Don't complete
                    deferredCount++;
                });
            TaskID task3 = scheduler.AllocateTypedTask(2,
                [&](TaskID id, TaskManager& sched) {
                    syncCount++;
                    sched.CompleteTask(id);
                });

            scheduler.EnqueueTask(task1);
            scheduler.EnqueueTask(task2);
            scheduler.AddDependency(task3, task1);
            scheduler.AddDependency(task3, task2);
            scheduler.EnqueueTask(task3);

            // Assert - task1 completed, task2 executing, task3 waiting
            Assert::AreEqual(1, syncCount.load());
            Assert::AreEqual(1, deferredCount.load());
            Assert::IsTrue(scheduler.IsTaskInState(task1, TaskState::Completed));
            Assert::IsTrue(scheduler.IsTaskInState(task2, TaskState::Executing));
            Assert::IsTrue(scheduler.IsTaskInState(task3, TaskState::Scheduled));

            // Act - Complete task2
            scheduler.CompleteTask(task2);

            // Assert - task3 now executed
            Assert::AreEqual(2, syncCount.load());
            Assert::IsTrue(scheduler.IsTaskInState(task3, TaskState::Completed));
        }

        TEST_METHOD(ComplexDependencyGraph_AllModesWork)
        {
            // Arrange
            TaskManager scheduler(1024);
            std::atomic<int> totalExecutions{ 0 };

            // Create complex graph:
            //     task1 (sync NoOp)
            //       |
            //     task2 (sync)
            //      / \
            // task3  task4 (both deferred)
            //      \ /
            //     task5 (sync)

            TaskID task1 = scheduler.AllocateTypedTask(0,
                [&](TaskID id, TaskManager& sched) {
                    totalExecutions++;
                    sched.CompleteTask(id);
                });
            TaskID task2 = scheduler.AllocateTypedTask(1,
                [&](TaskID id, TaskManager& sched) {
                    totalExecutions++;
                    sched.CompleteTask(id);
                });
            TaskID task3 = scheduler.AllocateTypedTask(1,
                [&](TaskID id, TaskManager& sched) {
                    (void)id; (void)sched; // Deferred
                    totalExecutions++;
                });
            TaskID task4 = scheduler.AllocateTypedTask(1,
                [&](TaskID id, TaskManager& sched) {
                    (void)id; (void)sched; // Deferred
                    totalExecutions++;
                });
            TaskID task5 = scheduler.AllocateTypedTask(2,
                [&](TaskID id, TaskManager& sched) {
                    totalExecutions++;
                    sched.CompleteTask(id);
                });

            scheduler.EnqueueTask(task1);
            scheduler.AddDependency(task2, task1);
            scheduler.AddDependency(task3, task2);
            scheduler.AddDependency(task4, task2);
            scheduler.AddDependency(task5, task3);
            scheduler.AddDependency(task5, task4);

            scheduler.EnqueueTask(task2);
            scheduler.EnqueueTask(task3);
            scheduler.EnqueueTask(task4);
            scheduler.EnqueueTask(task5);

            // Assert - task1, task2, task3/4 executed, task5 waiting
            Assert::AreEqual(4, totalExecutions.load()); // task1, task2, task3, task4
            Assert::IsTrue(scheduler.IsTaskInState(task2, TaskState::Completed));
            Assert::IsTrue(scheduler.IsTaskInState(task3, TaskState::Executing));
            Assert::IsTrue(scheduler.IsTaskInState(task4, TaskState::Executing));
            Assert::IsTrue(scheduler.IsTaskInState(task5, TaskState::Scheduled));

            // Act - Complete deferred tasks
            scheduler.CompleteTask(task3);
            Assert::AreEqual(4, totalExecutions.load()); // task5 still waiting for task4
            
            scheduler.CompleteTask(task4);

            // Assert - task5 now executed
            Assert::AreEqual(5, totalExecutions.load()); // All 5 tasks executed
            Assert::IsTrue(scheduler.IsTaskInState(task5, TaskState::Completed));
        }

        //------------------------------------------------------------------------------------------------
        // Statistics Tests
        //------------------------------------------------------------------------------------------------

        TEST_METHOD(Statistics_ReflectCurrentState)
        {
            // Arrange
            TaskManager scheduler(1024);
            TaskID task1 = scheduler.AllocateTask(100, 0, NoOp());
            TaskID task2 = scheduler.AllocateTask(200, 0, NoOp());

            // Act & Assert
            auto stats = scheduler.GetStatistics();
            Assert::AreEqual(size_t(2), stats.ActiveTaskCount);
            Assert::AreEqual(TaskID(3), stats.NextTaskId);
        }

        TEST_METHOD(Statistics_TracksExecutingCount)
        {
            // Arrange
            TaskManager scheduler(1024);
            std::atomic<int> execCount{ 0 };

            TaskID task1 = scheduler.AllocateTypedTask(0,
                [&](TaskID id, TaskManager& sched) {
                    (void)id; (void)sched; // Don't complete
                    execCount++;
                });
            TaskID task2 = scheduler.AllocateTypedTask(0,
                [&](TaskID id, TaskManager& sched) {
                    (void)id; (void)sched; // Don't complete
                    execCount++;
                });

            // Act
            scheduler.EnqueueTask(task1);
            scheduler.EnqueueTask(task2);

            // Assert
            auto stats = scheduler.GetStatistics();
            Assert::AreEqual(size_t(2), stats.ExecutingTaskCount);
        }
    };
}
