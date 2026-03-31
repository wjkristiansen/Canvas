//================================================================================================
// CRectanglePacker - Implementation
//================================================================================================

#include "RectanglePacker.h"
#include <algorithm>

namespace Canvas
{

CRectanglePacker::CRectanglePacker(uint32_t textureWidth, uint32_t textureHeight)
    : m_TextureWidth(textureWidth), m_TextureHeight(textureHeight)
{
}

bool CRectanglePacker::CanAllocate(const Rect &candidate) const
{
    // Check bounds
    if (candidate.X + candidate.Width > m_TextureWidth ||
        candidate.Y + candidate.Height > m_TextureHeight)
        return false;
    
    // Check overlap with allocated rectangles
    for (const auto &rect : m_AllocatedRects)
    {
        if (!(candidate.X + candidate.Width <= rect.X ||
              candidate.X >= rect.X + rect.Width ||
              candidate.Y + candidate.Height <= rect.Y ||
              candidate.Y >= rect.Y + rect.Height))
        {
            return false; // Overlaps
        }
    }
    
    return true;
}

bool CRectanglePacker::Allocate(uint32_t width, uint32_t height, Rect &outRect)
{
    // Simple shelf-based packing: place rects left-to-right, then create new shelf
    
    uint32_t tryY = 0;
    
    while (tryY + height <= m_TextureHeight)
    {
        // Find rightmost occupied x at this y level
        uint32_t rightmostX = 0;
        
        for (const auto &rect : m_AllocatedRects)
        {
            if (rect.Y <= tryY && tryY < rect.Y + rect.Height)
            {
                rightmostX = std::max(rightmostX, rect.X + rect.Width);
            }
        }
        
        // Try to place at this shelf
        if (rightmostX + width <= m_TextureWidth)
        {
            Rect candidate(rightmostX, tryY, width, height);
            if (CanAllocate(candidate))
            {
                outRect = candidate;
                m_AllocatedRects.push_back(candidate);
                return true;
            }
        }
        
        // Find next shelf
        uint32_t nextY = m_TextureHeight;
        for (const auto &rect : m_AllocatedRects)
        {
            uint32_t bottom = rect.Y + rect.Height;
            if (rect.Y <= tryY && bottom > tryY)
                nextY = std::min(nextY, bottom);
        }
        
        if (nextY >= m_TextureHeight)
            break;
        
        tryY = nextY;
    }
    
    return false;
}

uint32_t CRectanglePacker::GetFreeArea() const
{
    uint32_t totalAllocated = 0;
    for (const auto &rect : m_AllocatedRects)
        totalAllocated += rect.Width * rect.Height;
    
    return GetTotalArea() - totalAllocated;
}

} // namespace Canvas
