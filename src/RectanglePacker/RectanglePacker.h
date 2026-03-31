#pragma once
//================================================================================================
// CRectanglePacker - Simple shelf-based rectangle bin packing
//================================================================================================

#include <cstdint>
#include <vector>

namespace Canvas
{

class CRectanglePacker
{
public:
    struct Rect
    {
        uint32_t X, Y, Width, Height;
        Rect() : X(0), Y(0), Width(0), Height(0) {}
        Rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h) : X(x), Y(y), Width(w), Height(h) {}
    };
    
    CRectanglePacker(uint32_t textureWidth, uint32_t textureHeight);
    
    // Try to allocate a rectangle
    // Returns true if successful, false if no space available
    bool Allocate(uint32_t width, uint32_t height, Rect &outRect);
    
    // Get remaining free area (for reallocation checks)
    uint32_t GetFreeArea() const;
    uint32_t GetTotalArea() const { return m_TextureWidth * m_TextureHeight; }
    
private:
    uint32_t m_TextureWidth, m_TextureHeight;
    std::vector<Rect> m_AllocatedRects;
    
    // Check if rectangle overlaps any allocated rect
    bool CanAllocate(const Rect &candidate) const;
};

} // namespace Canvas
