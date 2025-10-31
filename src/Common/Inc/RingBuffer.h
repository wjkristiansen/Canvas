// RingBuffer.h
//
// RingBuffer is a general-purpose circular memory buffer for efficient allocation
// of variable-sized memory blocks without fragmentation.
//
// Features:
//   - Allocates blocks sequentially within a contiguous buffer
//   - Supports retirement of blocks in FIFO order from the head
//   - Automatically wraps around when reaching the end
//   - All blocks must be retired before memory can be reclaimed
//
// Memory Layout:
//   Each allocation returns a pointer to user-accessible memory.
//   The user is responsible for tracking allocation sizes.
//
// Usage Pattern:
//   1. TryAllocate() - Request memory from the tail
//   2. Use the allocated memory
//   3. RetireHead() - Free memory from the head when done
//
// Thread Safety:
//   RingBuffer is NOT thread-safe. External synchronization required.

#pragma once

#include <cstdint>
#include <memory>

namespace Canvas
{

//------------------------------------------------------------------------------------------------
// RingBuffer: Manages a contiguous block of memory as a circular buffer
class RingBuffer
{
public:
    // Constructor: allocates a ring buffer of the specified size (will be aligned to 16 bytes)
    explicit RingBuffer(size_t size);
    ~RingBuffer();
    
    // Non-copyable, movable
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;
    RingBuffer(RingBuffer&&) noexcept;
    RingBuffer& operator=(RingBuffer&&) noexcept;
    
    // Attempt to allocate a block of memory with the specified size
    // Returns nullptr if insufficient space
    // Size will be aligned to 16-byte boundary
    void* TryAllocate(size_t size);
    
    // Retire the head block with the specified size and advance the head pointer
    // Returns true if successful, false if buffer is empty
    // Caller must provide the exact size that was allocated
    bool RetireHead(size_t size);
    
    // Check if this ring is completely empty (all blocks retired)
    bool IsEmpty() const { return m_Head == m_Tail; }
    
    // Check if this ring is completely full (no space for any allocation)
    bool IsFull() const;
    
    // Get the total size of this ring buffer
    size_t GetSize() const { return m_Size; }
    
    // Get number of bytes currently allocated (not yet retired)
    size_t GetUsedSize() const;
    
    // Check if a pointer is within this ring's buffer range
    bool ContainsPointer(const void* ptr) const;
    
private:
    std::unique_ptr<uint8_t[]> m_Buffer;
    size_t m_Size;      // Total size of buffer
    size_t m_Head;      // Offset to first active block
    size_t m_Tail;      // Offset to next allocation point
    
    // Align size to 16-byte boundary
    static size_t AlignSize(size_t size);
};

} // namespace Canvas
