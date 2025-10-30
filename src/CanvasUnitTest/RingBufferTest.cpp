// RingBufferTest.cpp

#include "pch.h"
#include "CppUnitTest.h"
#include "RingBuffer.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Canvas;

namespace CanvasUnitTest
{
    TEST_CLASS(RingBufferTest)
    {
    public:

        TEST_METHOD(Constructor_CreatesBufferWithAlignedSize)
        {
            // Arrange & Act
            RingBuffer buffer(1000);

            // Assert
            Assert::AreEqual(size_t(1008), buffer.GetSize()); // Aligned to 16 bytes
            Assert::IsTrue(buffer.IsEmpty());
            Assert::AreEqual(size_t(0), buffer.GetUsedSize());
        }

        TEST_METHOD(Constructor_ThrowsOnZeroSize)
        {
            // Assert
            Assert::ExpectException<Gem::GemError>([]()
            {
                RingBuffer buffer(0);
            });
        }

        TEST_METHOD(TryAllocate_SimpleAllocation_Succeeds)
        {
            // Arrange
            RingBuffer buffer(1024);

            // Act
            void* ptr = buffer.TryAllocate(100);

            // Assert
            Assert::IsNotNull(ptr);
            Assert::IsFalse(buffer.IsEmpty());
            Assert::AreEqual(size_t(112), buffer.GetUsedSize()); // 100 aligned to 112
        }

        TEST_METHOD(TryAllocate_MultipleAllocations_Succeed)
        {
            // Arrange
            RingBuffer buffer(1024);

            // Act
            void* ptr1 = buffer.TryAllocate(64);
            void* ptr2 = buffer.TryAllocate(128);
            void* ptr3 = buffer.TryAllocate(256);

            // Assert
            Assert::IsNotNull(ptr1);
            Assert::IsNotNull(ptr2);
            Assert::IsNotNull(ptr3);
            Assert::AreNotEqual(ptr1, ptr2);
            Assert::AreNotEqual(ptr2, ptr3);
            Assert::AreEqual(size_t(64 + 128 + 256), buffer.GetUsedSize());
        }

        TEST_METHOD(TryAllocate_TooLarge_ReturnsNull)
        {
            // Arrange
            RingBuffer buffer(256);

            // Act
            void* ptr = buffer.TryAllocate(500);

            // Assert
            Assert::IsNull(ptr);
            Assert::IsTrue(buffer.IsEmpty());
        }

        TEST_METHOD(TryAllocate_AllocationsAre16ByteAligned)
        {
            // Arrange
            RingBuffer buffer(1024);

            // Act
            void* ptr1 = buffer.TryAllocate(1);   // Should be aligned to 16
            void* ptr2 = buffer.TryAllocate(37);  // Should be aligned to 48
            void* ptr3 = buffer.TryAllocate(100); // Should be aligned to 112

            // Assert
            Assert::AreEqual(size_t(0), reinterpret_cast<uintptr_t>(ptr1) % 16);
            Assert::AreEqual(size_t(0), reinterpret_cast<uintptr_t>(ptr2) % 16);
            Assert::AreEqual(size_t(0), reinterpret_cast<uintptr_t>(ptr3) % 16);
        }

        TEST_METHOD(RetireHead_SingleBlock_Succeeds)
        {
            // Arrange
            RingBuffer buffer(1024);
            buffer.TryAllocate(100);

            // Act
            bool result = buffer.RetireHead(100);

            // Assert
            Assert::IsTrue(result);
            Assert::IsTrue(buffer.IsEmpty());
            Assert::AreEqual(size_t(0), buffer.GetUsedSize());
        }

        TEST_METHOD(RetireHead_MultipleBlocks_RetiresInOrder)
        {
            // Arrange
            RingBuffer buffer(1024);
            buffer.TryAllocate(64);
            buffer.TryAllocate(128);
            buffer.TryAllocate(256);

            // Act & Assert - Retire first block
            Assert::IsTrue(buffer.RetireHead(64));
            Assert::IsFalse(buffer.IsEmpty());
            Assert::AreEqual(size_t(128 + 256), buffer.GetUsedSize());

            // Retire second block
            Assert::IsTrue(buffer.RetireHead(128));
            Assert::AreEqual(size_t(256), buffer.GetUsedSize());

            // Retire third block
            Assert::IsTrue(buffer.RetireHead(256));
            Assert::IsTrue(buffer.IsEmpty());
        }

        TEST_METHOD(RetireHead_EmptyBuffer_ReturnsFalse)
        {
            // Arrange
            RingBuffer buffer(1024);

            // Act
            bool result = buffer.RetireHead(100);

            // Assert
            Assert::IsFalse(result);
        }

        TEST_METHOD(WrapAround_AllocationsWrapToBeginning)
        {
            // Arrange
            RingBuffer buffer(256);
            void* ptr1 = buffer.TryAllocate(200);
            buffer.RetireHead(200);

            // Act - This should wrap to beginning
            void* ptr2 = buffer.TryAllocate(200);

            // Assert
            Assert::IsNotNull(ptr2);
            Assert::AreEqual(ptr1, ptr2); // Should reuse the same address
        }

        TEST_METHOD(WrapAround_PartialWrap_WorksCorrectly)
        {
            // Arrange
            RingBuffer buffer(512);
            buffer.TryAllocate(450); // Takes most of the buffer, leaves ~62 bytes
            
            // Act - This is too large for remaining space, would need to wrap
            void* ptr = buffer.TryAllocate(100); // Needs 112 bytes (aligned)

            // Assert - Should fail because head hasn't been retired yet (can't wrap)
            Assert::IsNull(ptr);
        }

        TEST_METHOD(ContainsPointer_ValidPointer_ReturnsTrue)
        {
            // Arrange
            RingBuffer buffer(1024);
            void* ptr = buffer.TryAllocate(100);

            // Act & Assert
            Assert::IsTrue(buffer.ContainsPointer(ptr));
        }

        TEST_METHOD(ContainsPointer_NullPointer_ReturnsFalse)
        {
            // Arrange
            RingBuffer buffer(1024);

            // Act & Assert
            Assert::IsFalse(buffer.ContainsPointer(nullptr));
        }

        TEST_METHOD(ContainsPointer_ExternalPointer_ReturnsFalse)
        {
            // Arrange
            RingBuffer buffer(1024);
            int externalVar = 42;

            // Act & Assert
            Assert::IsFalse(buffer.ContainsPointer(&externalVar));
        }

        TEST_METHOD(IsFull_BufferNotFull_ReturnsFalse)
        {
            // Arrange
            RingBuffer buffer(1024);
            buffer.TryAllocate(100);

            // Act & Assert
            Assert::IsFalse(buffer.IsFull());
        }

        TEST_METHOD(GetSize_ReturnsAlignedSize)
        {
            // Arrange & Act
            RingBuffer buffer(1000);

            // Assert
            Assert::AreEqual(size_t(1008), buffer.GetSize()); // Rounded up to 16-byte alignment
        }

        TEST_METHOD(MoveConstructor_TransfersOwnership)
        {
            // Arrange
            RingBuffer buffer1(1024);
            void* ptr1 = buffer1.TryAllocate(100);

            // Act
            RingBuffer buffer2(std::move(buffer1));

            // Assert
            Assert::AreEqual(size_t(1024), buffer2.GetSize());
            Assert::IsTrue(buffer2.ContainsPointer(ptr1));
            Assert::AreEqual(size_t(0), buffer1.GetSize()); // Moved-from state
        }

        TEST_METHOD(MoveAssignment_TransfersOwnership)
        {
            // Arrange
            RingBuffer buffer1(1024);
            RingBuffer buffer2(512);
            void* ptr1 = buffer1.TryAllocate(100);

            // Act
            buffer2 = std::move(buffer1);

            // Assert
            Assert::AreEqual(size_t(1024), buffer2.GetSize());
            Assert::IsTrue(buffer2.ContainsPointer(ptr1));
            Assert::AreEqual(size_t(0), buffer1.GetSize()); // Moved-from state
        }

        TEST_METHOD(StressTest_ManyAllocationsAndRetirements)
        {
            // Arrange
            RingBuffer buffer(4096);
            const int iterations = 100;

            // Act & Assert - Allocate and retire repeatedly
            for (int i = 0; i < iterations; i++)
            {
                void* ptr = buffer.TryAllocate(32);
                Assert::IsNotNull(ptr);
                Assert::IsTrue(buffer.RetireHead(32));
                Assert::IsTrue(buffer.IsEmpty());
            }
        }
    };
}
