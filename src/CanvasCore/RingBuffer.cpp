// RingBuffer.cpp

#include "RingBuffer.h"
#include "Gem.hpp"
#include <algorithm>

namespace Canvas
{

RingBuffer::RingBuffer(size_t size)
    : m_Size(AlignSize(size))
    , m_Head(0)
    , m_Tail(0)
{
    if (m_Size == 0)
    {
        throw Gem::GemError(Gem::Result::InvalidArg);
    }
    
    try
    {
        m_Buffer = std::make_unique<uint8_t[]>(m_Size);
    }
    catch (const std::bad_alloc&)
    {
        throw Gem::GemError(Gem::Result::OutOfMemory);
    }
}

RingBuffer::~RingBuffer()
{
    // RAII: m_Buffer automatically cleaned up
}

RingBuffer::RingBuffer(RingBuffer&& other) noexcept
    : m_Buffer(std::move(other.m_Buffer))
    , m_Size(other.m_Size)
    , m_Head(other.m_Head)
    , m_Tail(other.m_Tail)
{
    other.m_Size = 0;
    other.m_Head = 0;
    other.m_Tail = 0;
}

RingBuffer& RingBuffer::operator=(RingBuffer&& other) noexcept
{
    if (this != &other)
    {
        m_Buffer = std::move(other.m_Buffer);
        m_Size = other.m_Size;
        m_Head = other.m_Head;
        m_Tail = other.m_Tail;
        
        other.m_Size = 0;
        other.m_Head = 0;
        other.m_Tail = 0;
    }
    return *this;
}

size_t RingBuffer::AlignSize(size_t size)
{
    constexpr size_t alignment = 16;
    return (size + alignment - 1) & ~(alignment - 1);
}

void* RingBuffer::TryAllocate(size_t size)
{
    const size_t alignedSize = AlignSize(size);
    
    if (alignedSize > m_Size)
    {
        // Requested size is larger than entire buffer
        return nullptr;
    }
    
    // Check if we have space
    size_t availableSpace;
    
    if (m_Tail >= m_Head)
    {
        // Linear case: can we fit at the tail?
        availableSpace = m_Size - m_Tail;
        
        if (availableSpace >= alignedSize)
        {
            // Fits at the tail
            void* ptr = m_Buffer.get() + m_Tail;
            m_Tail += alignedSize;
            return ptr;
        }
        else if (m_Head >= alignedSize)
        {
            // Doesn't fit at tail, but would fit at beginning
            // Wrap around (leave unused space at end)
            void* ptr = m_Buffer.get();
            m_Tail = alignedSize;
            return ptr;
        }
        else
        {
            // Doesn't fit anywhere
            return nullptr;
        }
    }
    else
    {
        // Wrapped case: space is between head and tail
        availableSpace = m_Head - m_Tail;
        
        if (availableSpace >= alignedSize)
        {
            void* ptr = m_Buffer.get() + m_Tail;
            m_Tail += alignedSize;
            return ptr;
        }
        else
        {
            // Not enough space
            return nullptr;
        }
    }
}

bool RingBuffer::RetireHead(size_t size)
{
    if (IsEmpty())
    {
        return false;
    }
    
    const size_t alignedSize = AlignSize(size);
    
    // Advance head
    m_Head += alignedSize;
    
    // If head reached or passed tail, reset both (buffer is now empty)
    if (m_Head >= m_Tail && m_Tail != 0)
    {
        m_Head = 0;
        m_Tail = 0;
    }
    // If head reached end of buffer, wrap to beginning
    else if (m_Head >= m_Size)
    {
        m_Head = 0;
    }
    
    return true;
}

bool RingBuffer::IsFull() const
{
    if (m_Tail >= m_Head)
    {
        // Linear case: full if tail is at end and head is not at 0
        return (m_Tail == m_Size && m_Head != 0);
    }
    else
    {
        // Wrapped case: full if tail is right behind head
        return (m_Tail + AlignSize(1) > m_Head);
    }
}

size_t RingBuffer::GetUsedSize() const
{
    if (m_Tail >= m_Head)
    {
        return m_Tail - m_Head;
    }
    else
    {
        return (m_Size - m_Head) + m_Tail;
    }
}

bool RingBuffer::ContainsPointer(const void* ptr) const
{
    if (ptr == nullptr || m_Buffer == nullptr)
    {
        return false;
    }
    
    const uint8_t* p = static_cast<const uint8_t*>(ptr);
    const uint8_t* bufStart = m_Buffer.get();
    const uint8_t* bufEnd = bufStart + m_Size;
    
    return (p >= bufStart && p < bufEnd);
}

} // namespace Canvas
