#pragma once
//================================================================================================
// CanvasText - SDF/MSDF Generator
//
// CPU-side generation of signed distance field (SDF) or multi-channel SDF (MSDF) bitmaps.
// Given a glyph outline, generates a distance field texture that can be sampled efficiently
// on the GPU.
//
// Reference:
// - SDF: Christensen, "Distance Antialiased Vector Graphics Rendering", 2012
// - MSDF: Chlumsky, "Multi-channel Signed Distance Fields", 2018
//================================================================================================

#include "Font.h"
#include <vector>
#include <cmath>

namespace Canvas
{

//------------------------------------------------------------------------------------------------
// SDF Bitmap - intermediate representation of generated SDF/MSDF texture
//------------------------------------------------------------------------------------------------

struct SDFBitmap
{
    // Bitmap dimensions
    uint32_t Width;
    uint32_t Height;
    
    // Bytes per pixel: 1 for SDF (R8), 3 for MSDF (RGB8)
    uint32_t BytesPerPixel;
    
    // Pixel data (contiguous array, row-major)
    std::vector<uint8_t> Data;
    
    // Bounds in glyph space (scale factor for distance field)
    float MinX, MinY, MaxX, MaxY;
    
    // Glyph metrics in normalized space
    float AdvanceWidth;      // For text layout
    float LeftBearing;       // For text layout
    float BaselineOffset;    // Distance from glyph top to baseline (fonts use descenders)
    
    SDFBitmap()
        : Width(0), Height(0), BytesPerPixel(1)
        , MinX(0), MinY(0), MaxX(0), MaxY(0)
        , AdvanceWidth(0), LeftBearing(0), BaselineOffset(0)
    {}
    
    // Get pixel pointer (row-major)
    uint8_t* GetPixel(uint32_t x, uint32_t y, uint32_t channel = 0)
    {
        if (x >= Width || y >= Height || channel >= BytesPerPixel)
            return nullptr;
        return &Data[(y * Width + x) * BytesPerPixel + channel];
    }
    
    const uint8_t* GetPixel(uint32_t x, uint32_t y, uint32_t channel = 0) const
    {
        if (x >= Width || y >= Height || channel >= BytesPerPixel)
            return nullptr;
        return &Data[(y * Width + x) * BytesPerPixel + channel];
    }
};

//------------------------------------------------------------------------------------------------
// SDF Generator
//
// Core algorithm computes signed distance for each pixel to glyph edges.
//------------------------------------------------------------------------------------------------

class CSDFGenerator
{
public:
    // Configuration for SDF generation
    struct Config
    {
        uint32_t TextureSize;       // Size of output texture (square, power of 2 recommended)
        float SampleRange;          // Distance range in pixels (e.g., 4.0)
        bool GenerateMSDF;          // If true, generate 3-channel MSDF; else single-channel SDF
        
        Config()
            : TextureSize(64), SampleRange(4.0f), GenerateMSDF(false)
        {}
    };
    
    // Generate SDF bitmap for glyph outline
    // Returns true on success, false if glyph is empty or invalid
    bool Generate(const GlyphOutline &outline, const CTrueTypeFont &font, 
                  const Config &config, SDFBitmap &outBitmap);

private:
    // Internal: compute signed distance from point to edge
    static float SignedDistance(float px, float py, const GlyphPoint &p0, const GlyphPoint &p1);
    
    // Internal: compute signed distance from point to quadratic Bézier curve
    static float SignedDistanceToCurve(float px, float py,
                                       float cx0, float cy0, bool on0,
                                       float cx1, float cy1, bool on1,
                                       float cx2, float cy2, bool on2);
    
    // Internal: compute winding number for point-in-polygon test
    static int ComputeWindingNumber(float px, float py, const GlyphContour &contour);
    
    // Internal: scale point from font space to SDF bitmap space
    static void TransformPoint(float &x, float &y,
                              float minX, float minY, float width, float height,
                              uint32_t bitmapSize);
};

} // namespace Canvas
