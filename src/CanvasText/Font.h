//================================================================================================
// CanvasText - Font Data Structures
//
// Core data types for TrueType font parsing and glyph management.
// All parsing is self-contained with no external dependencies.
//================================================================================================

#pragma once
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <cstring>

// Windows.h pollutes the global namespace with GetGlyphOutline → GetGlyphOutlineA/W.
// Undo it so our method name is not mangled.
#ifdef GetGlyphOutline
#undef GetGlyphOutline
#endif

namespace Canvas
{

//------------------------------------------------------------------------------------------------
// Glyph Components - Quadratic Bézier curve segments
//------------------------------------------------------------------------------------------------

struct GlyphPoint
{
    float X, Y;
    bool IsOnCurve;  // true = on-curve (line-to or curve endpoint), false = off-curve (control point)
    
    GlyphPoint() : X(0), Y(0), IsOnCurve(false) {}
    GlyphPoint(float x, float y, bool onCurve) : X(x), Y(y), IsOnCurve(onCurve) {}
};

struct GlyphContour
{
    std::vector<GlyphPoint> Points;
    
    // Compute axis-aligned bounding box of this contour
    void GetBounds(float &minX, float &minY, float &maxX, float &maxY) const;
};

//------------------------------------------------------------------------------------------------
// Glyph Outline - all contours of a single glyph
//------------------------------------------------------------------------------------------------

struct GlyphOutline
{
    std::vector<GlyphContour> Contours;
    
    // Glyph metrics (from hhea/hmtx tables)
    float AdvanceWidth;         // Horizontal advance
    float LeftSideBearing;      // Left bearing
    float RightSideBearing;     // Right bearing (computed from outline)
    
    // Computed bounding box (in font coordinates)
    float XMin, YMin, XMax, YMax;
    
    GlyphOutline() 
        : AdvanceWidth(0), LeftSideBearing(0), RightSideBearing(0)
        , XMin(0), YMin(0), XMax(0), YMax(0) 
    {}
    
    // Compute bounding box from all contours
    void ComputeBounds();
    
    // Check if glyph is empty (no contours)
    bool IsEmpty() const { return Contours.empty(); }
};

//------------------------------------------------------------------------------------------------
// TrueType Font File Data
//
// Keeps raw font data in memory for fast glyph extraction.
// Parse only required tables: cmap, loca, glyf, hhea, hmtx
//------------------------------------------------------------------------------------------------

class CTrueTypeFont
{
public:
    // Font metrics (from hhea table)
    struct FontMetrics
    {
        float Ascender;         // Typographic ascender
        float Descender;        // Typographic descender
        float LineGap;          // Line gap
        int16_t NumberOfHMetrics;  // Number of advance widths in hmtx table
    };

private:
    // Raw font data buffer
    std::vector<uint8_t> m_FontData;
    
    // Table directory entries (name -> offset, length)
    std::unordered_map<std::string, std::pair<uint32_t, uint32_t>> m_TableDirectory;
    
    // Font metrics
    FontMetrics m_Metrics;
    
    // Character map (cmap): Unicode codepoint -> glyph index
    std::unordered_map<uint32_t, uint16_t> m_CharacterMap;
    
    // Glyph location table (loca): glyph index -> byte offset in glyf table
    std::vector<uint32_t> m_GlyphLocations;
    
    // Raw table offsets and sizes (cached for performance)
    uint32_t m_GlyfTableOffset;
    uint32_t m_GlyfTableSize;
    uint32_t m_HmtxTableOffset;
    uint32_t m_HmtxTableSize;
    
    // Number of glyphs in font
    uint16_t m_NumGlyphs;

    // Unitsquare per em (scale factor)
    uint16_t m_UnitsPerEm;

public:
    CTrueTypeFont();
    ~CTrueTypeFont();
    
    // Load font from file data
    // Returns true on success, false if malformed
    bool LoadFromBuffer(const uint8_t *pData, size_t dataSize);
    
    // Get glyph index for Unicode codepoint
    uint16_t GetGlyphIndex(uint32_t codepoint) const;
    
    // Extract glyph outline by glyph index
    bool GetGlyphOutline(uint16_t glyphIndex, GlyphOutline &outOutline) const;
    
    // Get font metrics
    const FontMetrics& GetMetrics() const { return m_Metrics; }
    uint16_t GetUnitsPerEm() const { return m_UnitsPerEm; }
    uint16_t GetNumGlyphs() const { return m_NumGlyphs; }
    
    // Convert font units to normalized coordinates (0.0 = one unit height)
    float NormalizeX(float fontUnits) const;
    float NormalizeY(float fontUnits) const;

private:
    // Helper: find table in font
    bool FindTable(const char *pTableName, uint32_t &outOffset, uint32_t &outSize) const;
    
    // Parse table directory from font file
    bool ParseTableDirectory();
    
    // Parse cmap table (character -> glyph index mapping)
    bool ParseCmapTable();
    
    // Parse hhea table (horizontal header)
    bool ParseHheaTable();
    
    // Parse loca table (glyph locations)
    bool ParseLocaTable();
    
    // Parse glyf table entry for specific glyph
    bool ParseGlyphEntry(uint16_t glyphIndex, GlyphOutline &outOutline) const;
    
    // Parse composite glyph (numberOfContours < 0) by resolving component references
    bool ParseCompositeGlyph(uint32_t offset, uint32_t endOffset, GlyphOutline &outOutline) const;
    
    // Parse hmtx table entries for a range of glyphs
    bool ParseHmtxEntry(uint16_t glyphIndex, float &outAdvanceWidth, float &outLeftBearing) const;
    
    // Read helpers
    static uint8_t ReadU8(const uint8_t *pData, uint32_t &offset);
    static int8_t ReadI8(const uint8_t *pData, uint32_t &offset);
    static uint16_t ReadU16(const uint8_t *pData, uint32_t &offset);
    static int16_t ReadI16(const uint8_t *pData, uint32_t &offset);
    static uint32_t ReadU32(const uint8_t *pData, uint32_t &offset);
    static int32_t ReadI32(const uint8_t *pData, uint32_t &offset);
};

} // namespace Canvas

//------------------------------------------------------------------------------------------------
// Inline implementations
//------------------------------------------------------------------------------------------------

inline void Canvas::GlyphContour::GetBounds(float &minX, float &minY, float &maxX, float &maxY) const
{
    if (Points.empty())
    {
        minX = minY = maxX = maxY = 0;
        return;
    }
    minX = maxX = Points[0].X;
    minY = maxY = Points[0].Y;
    
    for (size_t i = 1; i < Points.size(); ++i)
    {
        if (Points[i].X < minX) minX = Points[i].X;
        if (Points[i].X > maxX) maxX = Points[i].X;
        if (Points[i].Y < minY) minY = Points[i].Y;
        if (Points[i].Y > maxY) maxY = Points[i].Y;
    }
}

inline void Canvas::GlyphOutline::ComputeBounds()
{
    if (Contours.empty())
    {
        XMin = YMin = XMax = YMax = 0;
        return;
    }
    
    Contours[0].GetBounds(XMin, YMin, XMax, YMax);
    for (size_t i = 1; i < Contours.size(); ++i)
    {
        float minX, minY, maxX, maxY;
        Contours[i].GetBounds(minX, minY, maxX, maxY);
        if (minX < XMin) XMin = minX;
        if (minY < YMin) YMin = minY;
        if (maxX > XMax) XMax = maxX;
        if (maxY > YMax) YMax = maxY;
    }
}

inline Canvas::CTrueTypeFont::CTrueTypeFont()
    : m_GlyfTableOffset(0), m_GlyfTableSize(0)
    , m_HmtxTableOffset(0), m_HmtxTableSize(0)
    , m_NumGlyphs(0), m_UnitsPerEm(1000)
{
    std::memset(&m_Metrics, 0, sizeof(m_Metrics));
}

inline Canvas::CTrueTypeFont::~CTrueTypeFont()
{
}

inline float Canvas::CTrueTypeFont::NormalizeX(float fontUnits) const
{
    return fontUnits / m_UnitsPerEm;
}

inline float Canvas::CTrueTypeFont::NormalizeY(float fontUnits) const
{
    return fontUnits / m_UnitsPerEm;
}
