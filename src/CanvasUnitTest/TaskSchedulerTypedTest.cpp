// TaskSchedulerTypedTest.cpp
// Tests for the type-safe templated task allocation API

#include "pch.h"
#include "CppUnitTest.h"
#include "TaskScheduler.h"
#include <atomic>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Canvas;

namespace CanvasUnitTest
{
    TEST_CLASS(TaskSchedulerTypedTest)
    {
    public:

        //------------------------------------------------------------------------------------------------
        // Basic Typed API Tests
        //------------------------------------------------------------------------------------------------

        TEST_METHOD(TypedAPI_NoArgs_ExecutesCorrectly)
        {
            // Arrange
            TaskScheduler scheduler(1024);
            std::atomic<int> count{ 0 };

            // Act - Task with no arguments
            TaskID taskId = scheduler.AllocateTypedTask(0,
                [&](TaskID id, TaskScheduler& sched) {
                    count++;
                    sched.CompleteTask(id);
                });

            scheduler.ScheduleTask(taskId);

            // Assert
            Assert::AreEqual(1, count.load());
        }

        TEST_METHOD(TypedAPI_SingleArg_PassesValueCorrectly)
        {
            // Arrange
            TaskScheduler scheduler(1024);
            int receivedValue = 0;

            // Act - Task with one integer argument
            TaskID taskId = scheduler.AllocateTypedTask(0,
                [&](TaskID id, TaskScheduler& sched, int x) {
                    receivedValue = x;
                    sched.CompleteTask(id);
                }, 42);

            scheduler.ScheduleTask(taskId);

            // Assert
            Assert::AreEqual(42, receivedValue);
        }

        TEST_METHOD(TypedAPI_MultipleArgs_PassesAllCorrectly)
        {
            // Arrange
            TaskScheduler scheduler(1024);
            int receivedInt = 0;
            float receivedFloat = 0.0f;
            bool receivedBool = false;

            // Act - Task with multiple arguments of different types
            TaskID taskId = scheduler.AllocateTypedTask(0,
                [&](TaskID id, TaskScheduler& sched, int x, float y, bool z) {
                    receivedInt = x;
                    receivedFloat = y;
                    receivedBool = z;
                    sched.CompleteTask(id);
                }, 100, 3.14f, true);

            scheduler.ScheduleTask(taskId);

            // Assert
            Assert::AreEqual(100, receivedInt);
            Assert::AreEqual(3.14f, receivedFloat, 0.001f);
            Assert::IsTrue(receivedBool);
        }

        TEST_METHOD(TypedAPI_StringArg_CopiesCorrectly)
        {
            // Arrange
            TaskScheduler scheduler(1024);
            std::string receivedStr;

            // Act - Task with string argument
            std::string testStr = "Hello, TaskScheduler!";
            TaskID taskId = scheduler.AllocateTypedTask(0,
                [&](TaskID id, TaskScheduler& sched, std::string s) {
                    receivedStr = s;
                    sched.CompleteTask(id);
                }, testStr);

            scheduler.ScheduleTask(taskId);

            // Assert
            Assert::AreEqual(testStr, receivedStr);
        }

        //------------------------------------------------------------------------------------------------
        // Dependencies with Typed API
        //------------------------------------------------------------------------------------------------

        TEST_METHOD(TypedAPI_WithDependency_ExecutesInOrder)
        {
            // Arrange
            TaskScheduler scheduler(1024);
            std::vector<int> executionOrder;

            // Act - Create two tasks with dependency
            TaskID task1 = scheduler.AllocateTypedTask(0,
                [&](TaskID id, TaskScheduler& sched, int value) {
                    executionOrder.push_back(value);
                    sched.CompleteTask(id);
                }, 1);

            TaskID task2 = scheduler.AllocateTypedTask(1,
                [&](TaskID id, TaskScheduler& sched, int value) {
                    executionOrder.push_back(value);
                    sched.CompleteTask(id);
                }, 2);

            scheduler.AddDependency(task2, task1);
            scheduler.ScheduleTask(task1);
            scheduler.ScheduleTask(task2);

            // Assert
            Assert::AreEqual(size_t(2), executionOrder.size());
            Assert::AreEqual(1, executionOrder[0]);
            Assert::AreEqual(2, executionOrder[1]);
        }

        TEST_METHOD(TypedAPI_Fork_BothDependentsExecute)
        {
            // Arrange
            TaskScheduler scheduler(1024);
            std::atomic<int> count{ 0 };

            // Act - Fork pattern: one task, two dependents
            TaskID root = scheduler.AllocateTypedTask(0,
                [](TaskID id, TaskScheduler& sched) {
                    sched.CompleteTask(id);
                });

            TaskID child1 = scheduler.AllocateTypedTask(1,
                [&](TaskID id, TaskScheduler& sched, int value) {
                    count += value;
                    sched.CompleteTask(id);
                }, 10);

            TaskID child2 = scheduler.AllocateTypedTask(1,
                [&](TaskID id, TaskScheduler& sched, int value) {
                    count += value;
                    sched.CompleteTask(id);
                }, 20);

            scheduler.AddDependency(child1, root);
            scheduler.AddDependency(child2, root);
            scheduler.ScheduleTask(root);
            scheduler.ScheduleTask(child1);
            scheduler.ScheduleTask(child2);

            // Assert
            Assert::AreEqual(30, count.load());
        }

        TEST_METHOD(TypedAPI_Join_WaitsForAllDependencies)
        {
            // Arrange
            TaskScheduler scheduler(1024);
            std::atomic<int> sum{ 0 };

            // Act - Join pattern: two tasks, one dependent
            TaskID task1 = scheduler.AllocateTypedTask(0,
                [&](TaskID id, TaskScheduler& sched, int value) {
                    sum += value;
                    sched.CompleteTask(id);
                }, 5);

            TaskID task2 = scheduler.AllocateTypedTask(0,
                [&](TaskID id, TaskScheduler& sched, int value) {
                    sum += value;
                    sched.CompleteTask(id);
                }, 7);

            TaskID join = scheduler.AllocateTypedTask(2,
                [&](TaskID id, TaskScheduler& sched, int multiplier) {
                    int current = sum.load();
                    sum.store(current * multiplier);
                    sched.CompleteTask(id);
                }, 2);

            scheduler.AddDependency(join, task1);
            scheduler.AddDependency(join, task2);
            scheduler.ScheduleTask(task1);
            scheduler.ScheduleTask(task2);
            scheduler.ScheduleTask(join);

            // Assert - (5 + 7) * 2 = 24
            Assert::AreEqual(24, sum.load());
        }

        //------------------------------------------------------------------------------------------------
        // Deferred Completion with Typed API
        //------------------------------------------------------------------------------------------------

        TEST_METHOD(TypedAPI_DeferredCompletion_WorksCorrectly)
        {
            // Arrange
            TaskScheduler scheduler(1024);
            std::atomic<bool> executed{ false };
            TaskID deferredTaskId = InvalidTaskID;

            // Act - Task that doesn't complete immediately
            deferredTaskId = scheduler.AllocateTypedTask(0,
                [&](TaskID id, TaskScheduler& sched, int value) {
                    (void)sched; (void)value; // Unused in this test
                    deferredTaskId = id;
                    executed = true;
                    // Don't call CompleteTask yet
                }, 99);

            scheduler.ScheduleTask(deferredTaskId);

            // Assert - Task executed but not completed
            Assert::IsTrue(executed.load());
            TaskState state;
            scheduler.GetTaskState(deferredTaskId, state);
            Assert::IsTrue(state == TaskState::Executing);

            // Complete manually
            scheduler.CompleteTask(deferredTaskId);
            scheduler.GetTaskState(deferredTaskId, state);
            Assert::IsTrue(state == TaskState::Completed);
        }

        //------------------------------------------------------------------------------------------------
        // Mixed API Tests
        //------------------------------------------------------------------------------------------------

        TEST_METHOD(MixedAPI_TypedAndUntypedTasks_WorkTogether)
        {
            // Arrange
            TaskScheduler scheduler(1024);
            std::atomic<int> sum{ 0 };

            // Act - Mix typed and untyped allocations
            
            // Typed task
            TaskID task1 = scheduler.AllocateTypedTask(0,
                [&](TaskID id, TaskScheduler& sched, int value) {
                    sum += value;
                    sched.CompleteTask(id);
                }, 10);

            // Untyped task (old API) - using stateless lambda and passing sum via payload
            struct PayloadData {
                std::atomic<int>* sumPtr;
                int value;
            };
            TaskID task2 = scheduler.AllocateTask(sizeof(PayloadData), uint32_t(0),
                [](TaskID id, void* payload, size_t size, TaskScheduler& sched) {
                    (void)size;
                    PayloadData* data = static_cast<PayloadData*>(payload);
                    *data->sumPtr += data->value;
                    sched.CompleteTask(id);
                });
            auto* payload2 = scheduler.GetPayloadAs<PayloadData>(task2);
            payload2->sumPtr = &sum;
            payload2->value = 20;

            scheduler.ScheduleTask(task1);
            scheduler.ScheduleTask(task2);

            // Assert
            Assert::AreEqual(30, sum.load());
        }

        //------------------------------------------------------------------------------------------------
        // Complex Scenarios
        //------------------------------------------------------------------------------------------------

        TEST_METHOD(TypedAPI_ComplexDependencyGraph_AllTypedTasks)
        {
            // Arrange
            TaskScheduler scheduler(2048);
            std::vector<int> executionOrder;

            // Act - Create a complex graph with typed tasks
            //       T1
            //      /  \
            //    T2    T3
            //      \  /
            //       T4

            TaskID t1 = scheduler.AllocateTypedTask(0,
                [&](TaskID id, TaskScheduler& sched, int value) {
                    executionOrder.push_back(value);
                    sched.CompleteTask(id);
                }, 1);

            TaskID t2 = scheduler.AllocateTypedTask(1,
                [&](TaskID id, TaskScheduler& sched, int value) {
                    executionOrder.push_back(value);
                    sched.CompleteTask(id);
                }, 2);

            TaskID t3 = scheduler.AllocateTypedTask(1,
                [&](TaskID id, TaskScheduler& sched, int value) {
                    executionOrder.push_back(value);
                    sched.CompleteTask(id);
                }, 3);

            TaskID t4 = scheduler.AllocateTypedTask(2,
                [&](TaskID id, TaskScheduler& sched, int value) {
                    executionOrder.push_back(value);
                    sched.CompleteTask(id);
                }, 4);

            scheduler.AddDependency(t2, t1);
            scheduler.AddDependency(t3, t1);
            scheduler.AddDependency(t4, t2);
            scheduler.AddDependency(t4, t3);

            scheduler.ScheduleTask(t1);
            scheduler.ScheduleTask(t2);
            scheduler.ScheduleTask(t3);
            scheduler.ScheduleTask(t4);

            // Assert - T1 first, T2 and T3 in any order, T4 last
            Assert::AreEqual(size_t(4), executionOrder.size());
            Assert::AreEqual(1, executionOrder[0]);
            Assert::AreEqual(4, executionOrder[3]);
            // T2 and T3 can be in any order (both are 2 or 3)
            Assert::IsTrue((executionOrder[1] == 2 && executionOrder[2] == 3) ||
                          (executionOrder[1] == 3 && executionOrder[2] == 2));
        }

        TEST_METHOD(TypedAPI_MultipleTasksSameType_AllExecute)
        {
            // Arrange
            TaskScheduler scheduler(4096);
            std::atomic<int> sum{ 0 };
            const int numTasks = 100;

            // Act - Create many tasks of the same type
            std::vector<TaskID> tasks;
            for (int i = 0; i < numTasks; i++)
            {
                TaskID id = scheduler.AllocateTypedTask(0,
                    [&](TaskID taskId, TaskScheduler& sched, int value) {
                        sum += value;
                        sched.CompleteTask(taskId);
                    }, i);
                tasks.push_back(id);
            }

            for (TaskID id : tasks)
            {
                scheduler.ScheduleTask(id);
            }

            // Assert - Sum of 0..99 = 4950
            Assert::AreEqual(4950, sum.load());
        }

        //------------------------------------------------------------------------------------------------
        // Edge Cases
        //------------------------------------------------------------------------------------------------

        TEST_METHOD(TypedAPI_LargeStruct_WorksCorrectly)
        {
            // Arrange
            struct LargeData
            {
                int values[100];
                float data[50];
            };

            TaskScheduler scheduler(8192);
            bool received = false;

            // Act
            LargeData testData{};
            testData.values[0] = 999;
            testData.data[0] = 1.23f;

            TaskID taskId = scheduler.AllocateTypedTask(0,
                [&](TaskID id, TaskScheduler& sched, LargeData data) {
                    Assert::AreEqual(999, data.values[0]);
                    Assert::AreEqual(1.23f, data.data[0], 0.001f);
                    received = true;
                    sched.CompleteTask(id);
                }, testData);

            scheduler.ScheduleTask(taskId);

            // Assert
            Assert::IsTrue(received);
        }

        TEST_METHOD(TypedAPI_MultipleArgTypes_AllWorkCorrectly)
        {
            // Arrange
            TaskScheduler scheduler(1024);
            std::atomic<bool> intTaskDone{ false };
            std::atomic<bool> floatTaskDone{ false };
            std::atomic<bool> stringTaskDone{ false };

            // Act - Create typed tasks with different argument types
            TaskID task1 = scheduler.AllocateTypedTask(0,
                [&](TaskID id, TaskScheduler& sched, int value) {
                    Assert::AreEqual(42, value);
                    intTaskDone = true;
                    sched.CompleteTask(id);
                }, 42);

            TaskID task2 = scheduler.AllocateTypedTask(0,
                [&](TaskID id, TaskScheduler& sched, float value) {
                    Assert::AreEqual(3.14f, value);
                    floatTaskDone = true;
                    sched.CompleteTask(id);
                }, 3.14f);

            TaskID task3 = scheduler.AllocateTypedTask(0,
                [&](TaskID id, TaskScheduler& sched, std::string value) {
                    Assert::AreEqual(std::string("hello"), value);
                    stringTaskDone = true;
                    sched.CompleteTask(id);
                }, std::string("hello"));

            scheduler.ScheduleTask(task1);
            scheduler.ScheduleTask(task2);
            scheduler.ScheduleTask(task3);

            // Assert - All tasks executed
            Assert::IsTrue(intTaskDone.load());
            Assert::IsTrue(floatTaskDone.load());
            Assert::IsTrue(stringTaskDone.load());
        }

        //------------------------------------------------------------------------------------------------
        // Plain Function Tests (Recommended Pattern)
        //------------------------------------------------------------------------------------------------

        // Helper functions for testing plain function usage
        static void PlainFunc_NoArgs(TaskID id, TaskScheduler& sched)
        {
            s_plainFuncCounter++;
            sched.CompleteTask(id);
        }

        static void PlainFunc_OneArg(TaskID id, TaskScheduler& sched, int value)
        {
            s_plainFuncResult = value * 2;
            sched.CompleteTask(id);
        }

        static void PlainFunc_MultipleArgs(TaskID id, TaskScheduler& sched, int a, float b, std::string c)
        {
            s_plainFuncResult = a;
            s_plainFuncFloat = b;
            s_plainFuncString = c;
            sched.CompleteTask(id);
        }

        // Static members for communicating with plain functions
        static inline std::atomic<int> s_plainFuncCounter{ 0 };
        static inline int s_plainFuncResult{ 0 };
        static inline float s_plainFuncFloat{ 0.0f };
        static inline std::string s_plainFuncString;

        TEST_METHOD(TypedAPI_PlainFunction_NoArgs_Works)
        {
            // Arrange
            TaskScheduler scheduler(1024);
            s_plainFuncCounter = 0;

            // Act - Use plain function with no arguments
            TaskID taskId = scheduler.AllocateTypedTask(0, PlainFunc_NoArgs);
            scheduler.ScheduleTask(taskId);

            // Assert
            Assert::AreEqual(1, s_plainFuncCounter.load());
        }

        TEST_METHOD(TypedAPI_PlainFunction_OneArg_PassesCorrectly)
        {
            // Arrange
            TaskScheduler scheduler(1024);
            s_plainFuncResult = 0;

            // Act - Use plain function with one argument
            TaskID taskId = scheduler.AllocateTypedTask(0, PlainFunc_OneArg, 21);
            scheduler.ScheduleTask(taskId);

            // Assert
            Assert::AreEqual(42, s_plainFuncResult);
        }

        TEST_METHOD(TypedAPI_PlainFunction_MultipleArgs_AllPassCorrectly)
        {
            // Arrange
            TaskScheduler scheduler(1024);
            s_plainFuncResult = 0;
            s_plainFuncFloat = 0.0f;
            s_plainFuncString.clear();

            // Act - Use plain function with multiple arguments
            TaskID taskId = scheduler.AllocateTypedTask(0, PlainFunc_MultipleArgs, 
                100, 3.14f, std::string("test"));
            scheduler.ScheduleTask(taskId);

            // Assert
            Assert::AreEqual(100, s_plainFuncResult);
            Assert::AreEqual(3.14f, s_plainFuncFloat, 0.001f);
            Assert::AreEqual(std::string("test"), s_plainFuncString);
        }

        TEST_METHOD(TypedAPI_PlainFunction_WithDependencies_ExecutesInOrder)
        {
            // Arrange
            TaskScheduler scheduler(1024);
            s_plainFuncCounter = 0;

            // Act - Create dependency chain with plain functions
            TaskID task1 = scheduler.AllocateTypedTask(0, PlainFunc_NoArgs);
            TaskID task2 = scheduler.AllocateTypedTask(1, PlainFunc_NoArgs);
            TaskID task3 = scheduler.AllocateTypedTask(1, PlainFunc_NoArgs);

            scheduler.AddDependency(task2, task1);
            scheduler.AddDependency(task3, task2);
            scheduler.ScheduleTask(task1);
            scheduler.ScheduleTask(task2);
            scheduler.ScheduleTask(task3);

            // Assert - All three executed
            Assert::AreEqual(3, s_plainFuncCounter.load());
        }

        TEST_METHOD(TypedAPI_MixedPlainAndLambda_BothWork)
        {
            // Arrange
            TaskScheduler scheduler(1024);
            s_plainFuncResult = 0;
            std::atomic<int> lambdaResult{ 0 };

            // Act - Mix plain function and lambda
            TaskID task1 = scheduler.AllocateTypedTask(0, PlainFunc_OneArg, 10);
            
            TaskID task2 = scheduler.AllocateTypedTask(0,
                [&](TaskID id, TaskScheduler& sched, int value) {
                    lambdaResult = value * 3;
                    sched.CompleteTask(id);
                }, 7);

            scheduler.ScheduleTask(task1);
            scheduler.ScheduleTask(task2);

            // Assert
            Assert::AreEqual(20, s_plainFuncResult); // 10 * 2
            Assert::AreEqual(21, lambdaResult.load()); // 7 * 3
        }
    };
}
