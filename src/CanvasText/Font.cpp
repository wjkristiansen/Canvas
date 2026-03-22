//================================================================================================
// CanvasText - TrueType Font Parser Implementation
//
// Parses only required TrueType tables for glyph extraction.
// Specification: https://docs.microsoft.com/en-us/typography/opentype/spec/
//================================================================================================

#include "Font.h"
#include <cstring>
#include <algorithm>

namespace Canvas
{

//------------------------------------------------------------------------------------------------
// Binary Reader Helpers (big-endian, as per TrueType spec)
//------------------------------------------------------------------------------------------------

inline uint8_t CTrueTypeFont::ReadU8(const uint8_t *pData, uint32_t &offset)
{
    uint8_t value = pData[offset];
    offset += 1;
    return value;
}

inline int8_t CTrueTypeFont::ReadI8(const uint8_t *pData, uint32_t &offset)
{
    int8_t value = (int8_t)pData[offset];
    offset += 1;
    return value;
}

inline uint16_t CTrueTypeFont::ReadU16(const uint8_t *pData, uint32_t &offset)
{
    uint16_t value = (uint16_t)pData[offset] << 8 | pData[offset + 1];
    offset += 2;
    return value;
}

inline int16_t CTrueTypeFont::ReadI16(const uint8_t *pData, uint32_t &offset)
{
    int16_t value = (int16_t)((uint16_t)pData[offset] << 8 | pData[offset + 1]);
    offset += 2;
    return value;
}

inline uint32_t CTrueTypeFont::ReadU32(const uint8_t *pData, uint32_t &offset)
{
    uint32_t value = (uint32_t)pData[offset] << 24 
                   | (uint32_t)pData[offset + 1] << 16 
                   | (uint32_t)pData[offset + 2] << 8 
                   | pData[offset + 3];
    offset += 4;
    return value;
}

inline int32_t CTrueTypeFont::ReadI32(const uint8_t *pData, uint32_t &offset)
{
    int32_t value = (int32_t)((uint32_t)pData[offset] << 24 
                            | (uint32_t)pData[offset + 1] << 16 
                            | (uint32_t)pData[offset + 2] << 8 
                            | pData[offset + 3]);
    offset += 4;
    return value;
}

//------------------------------------------------------------------------------------------------
// Font Loading
//------------------------------------------------------------------------------------------------

bool CTrueTypeFont::LoadFromBuffer(const uint8_t *pData, size_t dataSize)
{
    if (!pData || dataSize < 12)
        return false;
    
    // Copy data
    m_FontData.assign(pData, pData + dataSize);
    
    // Parse table directory and required tables
    if (!ParseTableDirectory())
        return false;
    
    // Parse required tables
    if (!ParseHheaTable())
        return false;
    
    if (!ParseCmapTable())
        return false;
    
    if (!ParseLocaTable())
        return false;
    
    return true;
}

bool CTrueTypeFont::ParseTableDirectory()
{
    // TrueType file starts with:
    // - sfntVersion (4 bytes): 0x00010000 for v1.0, or 'OTTO' for CFF
    // - numTables (2 bytes)
    // - searchRange (2 bytes)
    // - entrySelector (2 bytes)
    // - rangeShift (2 bytes)
    // Followed by table entries (16 bytes each): tag (4), checksum (4), offset (4), length (4)
    
    const uint8_t *pData = m_FontData.data();
    size_t dataSize = m_FontData.size();
    
    if (dataSize < 12)
        return false;
    
    uint32_t offset = 0;
    uint32_t sfntVersion = ReadU32(pData, offset);
    uint16_t numTables = ReadU16(pData, offset);
    
    // Skip searchRange, entrySelector, rangeShift
    offset += 6;
    
    // Check version (0x00010000 for TrueType v1.0 or CFF)
    if (sfntVersion != 0x00010000 && sfntVersion != 0x4F54544F) // 'OTTO'
        return false;
    
    // Read table entries
    for (uint16_t i = 0; i < numTables; ++i)
    {
        if (offset + 16 > dataSize)
            return false;
        
        // Read tag (4 bytes)
        char tag[4];
        tag[0] = (char)pData[offset];
        tag[1] = (char)pData[offset + 1];
        tag[2] = (char)pData[offset + 2];
        tag[3] = (char)pData[offset + 3];
        
        uint32_t tableOffset, tableLength;
        offset += 4;
        offset += 4; // skip checksum
        tableOffset = ReadU32(pData, offset);
        tableLength = ReadU32(pData, offset);
        
        std::string tagStr(tag, 4);
        m_TableDirectory[tagStr] = { tableOffset, tableLength };
    }
    
    return m_TableDirectory.size() > 0;
}

bool CTrueTypeFont::FindTable(const char *pTableName, uint32_t &outOffset, uint32_t &outSize) const
{
    std::string name(pTableName);
    auto it = m_TableDirectory.find(name);
    if (it == m_TableDirectory.end())
        return false;
    
    outOffset = it->second.first;
    outSize = it->second.second;
    return true;
}

bool CTrueTypeFont::ParseHheaTable()
{
    uint32_t tableOffset, tableSize;
    if (!FindTable("hhea", tableOffset, tableSize))
        return false;
    
    if (tableSize < 36)
        return false;
    
    const uint8_t *pData = m_FontData.data();
    uint32_t offset = tableOffset;
    
    // hhea table structure:
    // uint16 majorVersion (2)
    // uint16 minorVersion (2)
    // int16 ascender (2)
    // int16 descender (2)
    // int16 lineGap (2)
    // uint16 advanceWidthMax (2)
    // int16 minLeftSideBearing (2)
    // int16 minRightSideBearing (2)
    // int16 xMaxExtent (2)
    // int16 caretSlopeRise (2)
    // int16 caretSlopeRun (2)
    // ... reserved (10 bytes) ...
    // int16 metricDataFormat (2)
    // uint16 numberOfHMetrics (2)
    
    ReadU16(pData, offset); // majorVersion
    ReadU16(pData, offset); // minorVersion
    m_Metrics.Ascender = (float)ReadI16(pData, offset);
    m_Metrics.Descender = (float)ReadI16(pData, offset);
    m_Metrics.LineGap = (float)ReadI16(pData, offset);
    
    // Calculate m_UnitsPerEm from head table if available
    uint32_t headOffset, headSize;
    if (FindTable("head", headOffset, headSize))
    {
        uint32_t headPos = headOffset + 18; // unitsPerEm is at offset 18
        if (headSize >= 20 && headPos + 2 <= m_FontData.size())
        {
            uint32_t pos = headPos;
            m_UnitsPerEm = ReadU16(pData, pos);
        }
    }
    
    // Skip ahead to numberOfHMetrics (at offset 34 from start)
    offset = tableOffset + 34;
    m_Metrics.NumberOfHMetrics = (int16_t)ReadU16(pData, offset);
    
    // Also read maxp table to get number of glyphs
    uint32_t maxpOffset, maxpSize;
    if (FindTable("maxp", maxpOffset, maxpSize))
    {
        if (maxpSize >= 6)
        {
            uint32_t pos = maxpOffset + 4;
            m_NumGlyphs = ReadU16(pData, pos);
        }
    }
    
    return true;
}

bool CTrueTypeFont::ParseCmapTable()
{
    uint32_t tableOffset, tableSize;
    if (!FindTable("cmap", tableOffset, tableSize))
        return false;
    
    if (tableSize < 4)
        return false;
    
    const uint8_t *pData = m_FontData.data();
    
    // cmap table structure:
    // uint16 version (2)
    // uint16 numTables (2)
    // Followed by subtable entries (6 bytes each): platformID (2), platformSpecificID (2), offset (4)
    
    uint32_t offset = tableOffset + 2; // skip version
    uint16_t numSubtables = ReadU16(pData, offset);
    
    // Find preferred subtable: platformID=3 (Windows), platformSpecificID=1 (Unicode BMP)
    // or platformID=0 (Unicode)
    uint32_t preferredSubtableOffset = 0;
    bool found = false;
    
    for (uint16_t i = 0; i < numSubtables; ++i)
    {
        uint16_t platformID = ReadU16(pData, offset);
        uint16_t platformSpecificID = ReadU16(pData, offset);
        uint32_t subtableOffset = ReadU32(pData, offset);
        
        // Prefer Windows Unicode BMP, but accept Unicode subtables
        if (!found || (platformID == 3 && platformSpecificID == 1))
        {
            preferredSubtableOffset = tableOffset + subtableOffset;
            found = true;
        }
    }
    
    if (!found)
        return false;
    
    // Parse the selected subtable (format 4 is most common and sufficient for our needs)
    offset = preferredSubtableOffset;
    uint16_t format = ReadU16(pData, offset);
    
    if (format == 4)
    {
        // Format 4: Segment mapping to delta values
        (void)ReadU16(pData, offset); // length field (not needed for parsing)
        offset += 2; // skip language
        
        uint16_t segCountX2 = ReadU16(pData, offset);
        uint16_t segCount = segCountX2 / 2;
        offset += 6; // skip searchRange, entrySelector, rangeShift
        
        // Read endCode array
        std::vector<uint16_t> endCode(segCount);
        for (uint16_t i = 0; i < segCount; ++i)
            endCode[i] = ReadU16(pData, offset);
        
        offset += 2; // skip reservedPad
        
        // Read startCode array
        std::vector<uint16_t> startCode(segCount);
        for (uint16_t i = 0; i < segCount; ++i)
            startCode[i] = ReadU16(pData, offset);
        
        // Read idDelta array
        std::vector<int16_t> idDelta(segCount);
        for (uint16_t i = 0; i < segCount; ++i)
            idDelta[i] = ReadI16(pData, offset);
        
        // idRangeOffset array and glyphIdArray follow
        std::vector<uint16_t> idRangeOffsets(segCount);
        uint32_t idRangeOffsetStart = offset;
        
        for (uint16_t i = 0; i < segCount; ++i)
            idRangeOffsets[i] = ReadU16(pData, offset);
        
        // Build character map using segment data
        for (uint16_t i = 0; i < segCount; ++i)
        {
            for (uint32_t codepoint = startCode[i]; codepoint <= endCode[i]; ++codepoint)
            {
                uint16_t glyphIndex = 0;
                
                if (idRangeOffsets[i] == 0)
                {
                    // Use idDelta
                    glyphIndex = (uint16_t)(codepoint + idDelta[i]);
                }
                else
                {
                    // Use glyphIdArray
                    uint32_t arrayOffset = idRangeOffsetStart + (i * 2) + idRangeOffsets[i] 
                                         + (codepoint - startCode[i]) * 2;
                    if (arrayOffset + 2 <= m_FontData.size())
                    {
                        uint32_t pos = arrayOffset;
                        uint16_t arrayValue = ReadU16(pData, pos);
                        if (arrayValue != 0)
                            glyphIndex = (uint16_t)(arrayValue + idDelta[i]);
                    }
                }
                
                if (codepoint <= 0x10FFFF) // Valid Unicode range
                    m_CharacterMap[codepoint] = glyphIndex;
            }
        }
        
        return true;
    }
    
    // Format 12 (UCS-4): another common format with larger Unicode support
    if (format == 12)
    {
        offset = preferredSubtableOffset;
        ReadU16(pData, offset); // skip format
        offset += 2; // skip reserved
        (void)ReadU32(pData, offset); // length field (not needed for parsing)
        offset += 4; // skip language
        
        uint32_t numGroups = ReadU32(pData, offset);
        
        for (uint32_t i = 0; i < numGroups; ++i)
        {
            uint32_t startCharCode = ReadU32(pData, offset);
            uint32_t endCharCode = ReadU32(pData, offset);
            uint32_t startGlyphID = ReadU32(pData, offset);
            
            for (uint32_t codepoint = startCharCode; codepoint <= endCharCode; ++codepoint)
            {
                uint16_t glyphIndex = (uint16_t)(startGlyphID + (codepoint - startCharCode));
                m_CharacterMap[codepoint] = glyphIndex;
            }
        }
        
        return true;
    }
    
    return false;
}

bool CTrueTypeFont::ParseLocaTable()
{
    uint32_t tableOffset, tableSize;
    if (!FindTable("loca", tableOffset, tableSize))
        return false;
    
    const uint8_t *pData = m_FontData.data();
    uint32_t offset = tableOffset;
    
    // Determine if table uses 16-bit or 32-bit offsets from head table
    bool shortFormat = true;
    uint32_t headOffset, headSize;
    if (FindTable("head", headOffset, headSize))
    {
        if (headSize >= 52)
        {
            uint32_t headPos = headOffset + 50; // indexToLocFormat is at offset 50
            int16_t indexToLocFormat = ReadI16(pData, headPos);
            shortFormat = (indexToLocFormat == 0);
        }
    }
    
    m_GlyphLocations.clear();
    
    if (shortFormat)
    {
        // 16-bit offsets (in units of 2 bytes)
        uint16_t numLocations = (uint16_t)(tableSize / 2);
        m_GlyphLocations.reserve(numLocations);
        
        for (uint16_t i = 0; i < numLocations; ++i)
            m_GlyphLocations.push_back((uint32_t)ReadU16(pData, offset) * 2);
    }
    else
    {
        // 32-bit offsets
        uint16_t numLocations = (uint16_t)(tableSize / 4);
        m_GlyphLocations.reserve(numLocations);
        
        for (uint16_t i = 0; i < numLocations; ++i)
            m_GlyphLocations.push_back(ReadU32(pData, offset));
    }
    
    // Cache glyf table offset/size
    if (!FindTable("glyf", m_GlyfTableOffset, m_GlyfTableSize))
        return false;
    
    // Cache hmtx table offset/size
    if (!FindTable("hmtx", m_HmtxTableOffset, m_HmtxTableSize))
        return false;
    
    return m_GlyphLocations.size() > 0;
}

uint16_t CTrueTypeFont::GetGlyphIndex(uint32_t codepoint) const
{
    auto it = m_CharacterMap.find(codepoint);
    return (it != m_CharacterMap.end()) ? it->second : 0;
}

bool CTrueTypeFont::ParseHmtxEntry(uint16_t glyphIndex, float &outAdvanceWidth, float &outLeftBearing) const
{
    // hmtx table contains a pair for each glyph: advanceWidth (ushort), lsb (short)
    // But numberOfHMetrics glyphs have unique advanceWidth; remaining share last one
    
    if (m_HmtxTableOffset == 0)
        return false;
    
    const uint8_t *pData = m_FontData.data();
    
    uint16_t metricsIndex = std::min((uint16_t)glyphIndex, (uint16_t)(m_Metrics.NumberOfHMetrics - 1));
    uint32_t hmtxOffset = m_HmtxTableOffset + (metricsIndex * 4);
    
    if (hmtxOffset + 4 > m_FontData.size())
        return false;
    
    uint32_t offset = hmtxOffset;
    outAdvanceWidth = (float)ReadU16(pData, offset);
    outLeftBearing = (float)ReadI16(pData, offset);
    
    return true;
}

bool CTrueTypeFont::GetGlyphOutline(uint16_t glyphIndex, GlyphOutline &outOutline) const
{
    if (glyphIndex >= m_GlyphLocations.size() - 1)
        return false;
    
    // Get metrics (advance width, left side bearing)
    if (!ParseHmtxEntry(glyphIndex, outOutline.AdvanceWidth, outOutline.LeftSideBearing))
        return false;
    
    uint32_t glyphOffset = m_GlyfTableOffset + m_GlyphLocations[glyphIndex];
    uint32_t nextGlyphOffset = m_GlyfTableOffset + m_GlyphLocations[glyphIndex + 1];
    
    // Empty glyph (e.g., space character)
    if (glyphOffset == nextGlyphOffset)
    {
        outOutline.XMin = outOutline.YMin = outOutline.XMax = outOutline.YMax = 0;
        return true;
    }
    
    if (glyphOffset >= m_FontData.size() || nextGlyphOffset > m_FontData.size())
        return false;
    
    return ParseGlyphEntry(glyphIndex, outOutline);
}

bool CTrueTypeFont::ParseGlyphEntry(uint16_t glyphIndex, GlyphOutline &outOutline) const
{
    const uint8_t *pData = m_FontData.data();
    
    uint32_t glyphOffset = m_GlyfTableOffset + m_GlyphLocations[glyphIndex];
    uint32_t nextGlyphOffset = m_GlyfTableOffset + m_GlyphLocations[glyphIndex + 1];
    
    uint32_t glyphSize = nextGlyphOffset - glyphOffset;
    if (glyphSize < 10)
        return false; // Minimum glyph header is 10 bytes
    
    uint32_t offset = glyphOffset;
    
    int16_t numberOfContours = ReadI16(pData, offset);
    int16_t xMin = ReadI16(pData, offset);
    int16_t yMin = ReadI16(pData, offset);
    int16_t xMax = ReadI16(pData, offset);
    int16_t yMax = ReadI16(pData, offset);
    
    outOutline.XMin = (float)xMin;
    outOutline.YMin = (float)yMin;
    outOutline.XMax = (float)xMax;
    outOutline.YMax = (float)yMax;
    
    if (numberOfContours == 0)
        return true; // Empty glyph
    
    if (numberOfContours < 0)
        return ParseCompositeGlyph(offset, nextGlyphOffset, outOutline);
    
    // Read endPtsOfContours array
    std::vector<uint16_t> endPts(numberOfContours);
    for (int16_t i = 0; i < numberOfContours; ++i)
        endPts[i] = ReadU16(pData, offset);
    
    // Total number of points
    uint16_t totalPoints = endPts.back() + 1;
    
    // Skip instruction length and instructions
    uint16_t instructionLength = ReadU16(pData, offset);
    offset += instructionLength;
    
    // Read flags
    std::vector<uint8_t> flags(totalPoints);
    uint16_t flagIndex = 0;
    
    while (flagIndex < totalPoints && offset < nextGlyphOffset)
    {
        uint8_t flag = ReadU8(pData, offset);
        flags[flagIndex++] = flag;
        
        if (flag & 0x08) // Repeat flag
        {
            uint8_t repeatCount = ReadU8(pData, offset);
            while (repeatCount-- > 0 && flagIndex < totalPoints)
                flags[flagIndex++] = flag;
        }
    }
    
    // Read X coordinates
    std::vector<int16_t> xCoords(totalPoints);
    int16_t currentX = 0;
    
    for (uint16_t i = 0; i < totalPoints; ++i)
    {
        int16_t delta = 0;
        
        if (flags[i] & 0x02) // xShortVector
        {
            if (offset >= nextGlyphOffset) break;
            delta = (int16_t)ReadU8(pData, offset);
            if (!(flags[i] & 0x10)) // xIsSameOrPositive
                delta = -delta;
        }
        else if (flags[i] & 0x10) // xIsSameOrPositive
        {
            // No delta
            delta = 0;
        }
        else
        {
            if (offset + 1 >= nextGlyphOffset) break;
            delta = ReadI16(pData, offset);
        }
        
        currentX += delta;
        xCoords[i] = currentX;
    }
    
    // Read Y coordinates
    std::vector<int16_t> yCoords(totalPoints);
    int16_t currentY = 0;
    
    for (uint16_t i = 0; i < totalPoints; ++i)
    {
        int16_t delta = 0;
        
        if (flags[i] & 0x04) // yShortVector
        {
            if (offset >= nextGlyphOffset) break;
            delta = (int16_t)ReadU8(pData, offset);
            if (!(flags[i] & 0x20)) // yIsSameOrPositive
                delta = -delta;
        }
        else if (flags[i] & 0x20) // yIsSameOrPositive
        {
            // No delta
            delta = 0;
        }
        else
        {
            if (offset + 1 >= nextGlyphOffset) break;
            delta = ReadI16(pData, offset);
        }
        
        currentY += delta;
        yCoords[i] = currentY;
    }
    
    // Build contours and point list from flags
    outOutline.Contours.clear();
    outOutline.Contours.reserve(numberOfContours);
    
    uint16_t pointIndex = 0;
    for (int16_t c = 0; c < numberOfContours; ++c)
    {
        GlyphContour contour;
        uint16_t endPoint = endPts[c];
        
        while (pointIndex <= endPoint && pointIndex < totalPoints)
        {
            bool isOn = (flags[pointIndex] & 0x01) != 0;
            float x = (float)xCoords[pointIndex];
            float y = (float)yCoords[pointIndex];
            
            contour.Points.push_back(GlyphPoint(x, y, isOn));
            pointIndex++;
        }
        
        outOutline.Contours.push_back(contour);
    }
    
    return true;
}

bool CTrueTypeFont::ParseCompositeGlyph(uint32_t offset, uint32_t endOffset, GlyphOutline &outOutline) const
{
    const uint8_t *pData = m_FontData.data();
    
    // TrueType composite glyph flags
    constexpr uint16_t ARG_1_AND_2_ARE_WORDS  = 0x0001;
    constexpr uint16_t ARGS_ARE_XY_VALUES     = 0x0002;
    constexpr uint16_t WE_HAVE_A_SCALE        = 0x0008;
    constexpr uint16_t MORE_COMPONENTS        = 0x0020;
    constexpr uint16_t WE_HAVE_AN_X_AND_Y_SCALE = 0x0040;
    constexpr uint16_t WE_HAVE_A_TWO_BY_TWO   = 0x0080;
    
    uint16_t flags;
    
    do
    {
        if (offset + 4 > endOffset)
            return false;
        
        flags = ReadU16(pData, offset);
        uint16_t componentGlyphIndex = ReadU16(pData, offset);
        
        // Read translation arguments
        float dx = 0.0f, dy = 0.0f;
        if (flags & ARGS_ARE_XY_VALUES)
        {
            if (flags & ARG_1_AND_2_ARE_WORDS)
            {
                dx = (float)ReadI16(pData, offset);
                dy = (float)ReadI16(pData, offset);
            }
            else
            {
                dx = (float)ReadI8(pData, offset);
                dy = (float)ReadI8(pData, offset);
            }
        }
        else
        {
            // Arguments are point indices (match points between parent/child);
            // skip them — uncommon for basic Latin glyphs.
            if (flags & ARG_1_AND_2_ARE_WORDS)
                offset += 4;
            else
                offset += 2;
        }
        
        // Read optional transform (scale / 2x2 matrix)
        float scaleX = 1.0f, scaleY = 1.0f, scale01 = 0.0f, scale10 = 0.0f;
        if (flags & WE_HAVE_A_SCALE)
        {
            int16_t raw = ReadI16(pData, offset);
            scaleX = scaleY = (float)raw / 16384.0f; // F2Dot14 format
        }
        else if (flags & WE_HAVE_AN_X_AND_Y_SCALE)
        {
            scaleX = (float)ReadI16(pData, offset) / 16384.0f;
            scaleY = (float)ReadI16(pData, offset) / 16384.0f;
        }
        else if (flags & WE_HAVE_A_TWO_BY_TWO)
        {
            scaleX  = (float)ReadI16(pData, offset) / 16384.0f;
            scale01 = (float)ReadI16(pData, offset) / 16384.0f;
            scale10 = (float)ReadI16(pData, offset) / 16384.0f;
            scaleY  = (float)ReadI16(pData, offset) / 16384.0f;
        }
        
        // Recursively load the referenced glyph outline
        if (componentGlyphIndex < m_GlyphLocations.size() - 1)
        {
            GlyphOutline componentOutline;
            if (ParseGlyphEntry(componentGlyphIndex, componentOutline))
            {
                // Transform and append all contours from the component
                for (auto &contour : componentOutline.Contours)
                {
                    for (auto &pt : contour.Points)
                    {
                        float tx = scaleX * pt.X + scale01 * pt.Y + dx;
                        float ty = scale10 * pt.X + scaleY * pt.Y + dy;
                        pt.X = tx;
                        pt.Y = ty;
                    }
                    outOutline.Contours.push_back(std::move(contour));
                }
            }
        }
        
    } while (flags & MORE_COMPONENTS);
    
    return !outOutline.Contours.empty();
}

} // namespace Canvas
