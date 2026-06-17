//================================================================================================
// CanvasTextTest - Unit tests for the text rendering subsystem
//
// Tests cover CPU-only logic: UTF-8 decoding, glyph layout, rectangle packing,
// SDF generation, and font loading/outline extraction.
// No GPU device is required for any test in this file.
//================================================================================================

#include "pch.h"
// Internal CanvasText headers (unit tests are allowed to use these)
#include <fstream>
#include "Font.h"
#include "SDFGenerator.h"
#include "RectanglePacker.h"
#include "TextLayout.h"
#include "GlyphAtlas.h"

// System font to exercise real font parsing (tests gracefully skip if absent).
// Must match the font the app actually loads (segoeui.ttf, with arial.ttf as fallback).
static constexpr const wchar_t* kSystemFontPath = L"C:\\Windows\\Fonts\\segoeui.ttf";

using namespace Canvas;

namespace CanvasUnitTest
{

//================================================================================================
// Helper: load a system font into a CTrueTypeFont, returning false if unavailable
//================================================================================================
static bool LoadSystemFont(CTrueTypeFont& outFont)
{
    std::ifstream f(kSystemFontPath, std::ios::binary | std::ios::ate);
    if (!f.is_open())
        return false;

    std::streamsize sz = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(sz));
    f.read(reinterpret_cast<char*>(data.data()), sz);
    return outFont.LoadFromBuffer(data.data(), data.size());
}

//================================================================================================
// Helper: build a minimal square triangle outline (ascii 'box') for SDF tests
//================================================================================================
static GlyphOutline MakeSquareOutline(float side = 500.0f)
{
    GlyphOutline o;
    o.AdvanceWidth = side;
    o.LeftSideBearing = 0;
    o.XMin = 0; o.YMin = 0; o.XMax = side; o.YMax = side;

    GlyphContour c;
    c.Points.push_back({  0.0f,    0.0f,  true });
    c.Points.push_back({ side,    0.0f,  true });
    c.Points.push_back({ side,   side,   true });
    c.Points.push_back({  0.0f,   side,  true });
    o.Contours.push_back(c);
    return o;
}

//================================================================================================
TEST(TextUTF8DecodeTest, EmptyString)
{
    std::vector<uint32_t> codepoints;
    EXPECT_TRUE(Succeeded(CTextLayout::DecodeUtf8("", codepoints)));
    EXPECT_EQ((size_t)0, codepoints.size());
}

TEST(TextUTF8DecodeTest, PlainASCII)
{
    std::vector<uint32_t> codepoints;
    EXPECT_TRUE(Succeeded(CTextLayout::DecodeUtf8("Hello", codepoints)));
    EXPECT_EQ((size_t)5, codepoints.size());
    EXPECT_EQ((uint32_t)'H', codepoints[0]);
    EXPECT_EQ((uint32_t)'e', codepoints[1]);
    EXPECT_EQ((uint32_t)'o', codepoints[4]);
}

TEST(TextUTF8DecodeTest, SpacePreserved)
{
    // Space (0x20) must survive decoding - it was the root cause of the blank-screen bug
    std::vector<uint32_t> codepoints;
    EXPECT_TRUE(Succeeded(CTextLayout::DecodeUtf8("A B", codepoints)));
    EXPECT_EQ((size_t)3, codepoints.size());
    EXPECT_EQ((uint32_t)' ', codepoints[1]);
}

TEST(TextUTF8DecodeTest, Newline)
{
    std::vector<uint32_t> codepoints;
    EXPECT_TRUE(Succeeded(CTextLayout::DecodeUtf8("a\nb", codepoints)));
    EXPECT_EQ((size_t)3, codepoints.size());
    EXPECT_EQ((uint32_t)'\n', codepoints[1]);
}

TEST(TextUTF8DecodeTest, TwoByteUTF8)
{
    // U+00E9 LATIN SMALL LETTER E WITH ACUTE: 0xC3 0xA9
    const char utf8[] = { '\xC3', '\xA9', '\0' };
    std::vector<uint32_t> codepoints;
    EXPECT_TRUE(Succeeded(CTextLayout::DecodeUtf8(utf8, codepoints)));
    EXPECT_EQ((size_t)1, codepoints.size());
    EXPECT_EQ((uint32_t)0x00E9u, codepoints[0]);
}

TEST(TextUTF8DecodeTest, ThreeByteUTF8)
{
    // U+4E16 CJK: 0xE4 0xB8 0x96
    const char utf8[] = { '\xE4', '\xB8', '\x96', '\0' };
    std::vector<uint32_t> codepoints;
    EXPECT_TRUE(Succeeded(CTextLayout::DecodeUtf8(utf8, codepoints)));
    EXPECT_EQ((size_t)1, codepoints.size());
    EXPECT_EQ((uint32_t)0x4E16u, codepoints[0]);
}

TEST(TextUTF8DecodeTest, MixedASCIIAndMultibyte)
{
    // "Hi" + U+00E9 + "!" -> 4 codepoints
    const char utf8[] = { 'H', 'i', '\xC3', '\xA9', '!', '\0' };
    std::vector<uint32_t> codepoints;
    EXPECT_TRUE(Succeeded(CTextLayout::DecodeUtf8(utf8, codepoints)));
    EXPECT_EQ((size_t)4, codepoints.size());
    EXPECT_EQ((uint32_t)'H', codepoints[0]);
    EXPECT_EQ((uint32_t)0x00E9u, codepoints[2]);
    EXPECT_EQ((uint32_t)'!', codepoints[3]);
}

//================================================================================================
namespace {

// Fabricate an atlas entry for a 40x48 pixel glyph at a known atlas location

static GlyphAtlasEntry MakeEntry(float bw = 40.0f, float bh = 48.0f)
{
    GlyphAtlasEntry e;
    e.AdvanceWidth  = 0.5f;   // Normalized (0.5 em)
    e.LeftBearing   = 2.0f;
    e.TopBearing    = 4.0f;
    e.BitmapWidth   = bw;
    e.BitmapHeight  = bh;
    e.AtlasU0       = 0.1f;  e.AtlasV0 = 0.2f;
    e.AtlasU1       = 0.3f;  e.AtlasV1 = 0.4f;
    return e;
}

} // anonymous namespace

TEST(TextLayoutGlyphTest, NormalGlyphProduces6Vertices)
{
    GlyphAtlasEntry entry = MakeEntry();
    Math::FloatVector3 pos(10.0f, 20.0f, 0.0f);
    std::vector<TextVertex> verts;

    CTextLayout::LayoutGlyph(0x41, CTrueTypeFont{}, entry, pos, Math::FloatVector4(1.0f, 1.0f, 1.0f, 1.0f), 1.0f, verts);

    EXPECT_EQ((size_t)6, verts.size());
}

TEST(TextLayoutGlyphTest, ZeroSizeGlyphProducesNoVertices)
{
    // BitmapWidth/Height == 0 -> whitespace glyph, no quad should be emitted
    GlyphAtlasEntry entry = MakeEntry(0.0f, 0.0f);
    Math::FloatVector3 pos(0.0f, 0.0f, 0.0f);
    std::vector<TextVertex> verts;

    CTextLayout::LayoutGlyph(' ', CTrueTypeFont{}, entry, pos, Math::FloatVector4(1.0f, 1.0f, 1.0f, 1.0f), 1.0f, verts);

    EXPECT_EQ((size_t)0, verts.size());
}

TEST(TextLayoutGlyphTest, ZeroSizeGlyphReturnsAdvance)
{
    GlyphAtlasEntry entry = MakeEntry(0.0f, 0.0f);
    entry.AdvanceWidth = 0.35f;
    Math::FloatVector3 pos(0.0f, 0.0f, 0.0f);
    std::vector<TextVertex> verts;

    float adv = CTextLayout::LayoutGlyph(' ', CTrueTypeFont{}, entry, pos, Math::FloatVector4(1.0f, 1.0f, 1.0f, 1.0f), 1.0f, verts);

    EXPECT_EQ(0.35f, adv) << "Advance must be returned even for zero-size (whitespace) glyph";
}

TEST(TextLayoutGlyphTest, VertexPositionsMatchExpected)
{
    GlyphAtlasEntry entry = MakeEntry(40.0f, 48.0f);
    entry.LeftBearing = 0.0f;
    entry.TopBearing  = 0.0f;
    Math::FloatVector3 pos(100.0f, 200.0f, 0.5f);
    std::vector<TextVertex> verts;

    CTextLayout::LayoutGlyph(0x41, CTrueTypeFont{}, entry, pos, Math::FloatVector4(0.0f, 0.0f, 1.0f, 1.0f), 1.0f, verts);

    EXPECT_EQ(6u, (unsigned)verts.size());

    // Top-left of quad (fontSize=1.0 so em fractions == pixels directly)
    float x0 = 100.0f, y0 = 200.0f, x1 = 140.0f, y1 = 248.0f;

    // All 6 vertices must have X in [x0, x1] and Y in [y0, y1]
    for (const auto& v : verts)
    {
        EXPECT_TRUE(v.Position.X >= x0 && v.Position.X <= x1);
        EXPECT_TRUE(v.Position.Y >= y0 && v.Position.Y <= y1);
        EXPECT_NEAR(0.5f, v.Position.Z, 1e-6f);
    }
}

TEST(TextLayoutGlyphTest, TexCoordsMatchAtlasEntry)
{
    GlyphAtlasEntry entry = MakeEntry();
    Math::FloatVector3 pos(0.0f, 0.0f, 0.0f);
    std::vector<TextVertex> verts;

    CTextLayout::LayoutGlyph(0x41, CTrueTypeFont{}, entry, pos, Math::FloatVector4(1.0f, 1.0f, 1.0f, 1.0f), 1.0f, verts);

    // All UVs must lie within the atlas entry's UV bounds
    for (const auto& v : verts)
    {
        EXPECT_TRUE(v.TexCoord.X >= entry.AtlasU0 - 1e-5f && v.TexCoord.X <= entry.AtlasU1 + 1e-5f);
        EXPECT_TRUE(v.TexCoord.Y >= entry.AtlasV0 - 1e-5f && v.TexCoord.Y <= entry.AtlasV1 + 1e-5f);
    }
}

TEST(TextLayoutGlyphTest, ColorIsStoredInVertices)
{
    GlyphAtlasEntry entry = MakeEntry();
    Math::FloatVector3 pos(0.0f, 0.0f, 0.0f);
    std::vector<TextVertex> verts;
    Math::FloatVector4 color(0.87f, 0.68f, 0.93f, 0.49f);

    CTextLayout::LayoutGlyph(0x41, CTrueTypeFont{}, entry, pos, color, 1.0f, verts);

    for (const auto& v : verts)
    {
        EXPECT_NEAR(color.X, v.Color[0], 1e-6f);
        EXPECT_NEAR(color.Y, v.Color[1], 1e-6f);
        EXPECT_NEAR(color.Z, v.Color[2], 1e-6f);
        EXPECT_NEAR(color.W, v.Color[3], 1e-6f);
    }
}

//================================================================================================
TEST(TextRectanglePackerTest, SingleAllocation)
{
    CRectanglePacker packer(512, 512);
    CRectanglePacker::Rect r;
    EXPECT_TRUE(packer.Allocate(64, 64, r));
    EXPECT_EQ(0u, r.X);
    EXPECT_EQ(0u, r.Y);
    EXPECT_EQ(64u, r.Width);
    EXPECT_EQ(64u, r.Height);
}

TEST(TextRectanglePackerTest, TwoNonOverlappingAllocations)
{
    CRectanglePacker packer(512, 512);
    CRectanglePacker::Rect r0, r1;
    EXPECT_TRUE(packer.Allocate(64, 64, r0));
    EXPECT_TRUE(packer.Allocate(64, 64, r1));

    // Rectangles must not overlap
    bool xSep = r0.X + r0.Width <= r1.X || r1.X + r1.Width <= r0.X;
    bool ySep = r0.Y + r0.Height <= r1.Y || r1.Y + r1.Height <= r0.Y;
    EXPECT_TRUE(xSep || ySep) << "Allocated rectangles must not overlap";
}

TEST(TextRectanglePackerTest, AllRectsStayWithinBounds)
{
    CRectanglePacker packer(128, 128);
    for (int i = 0; i < 4; ++i)
    {
        CRectanglePacker::Rect r;
        if (!packer.Allocate(64, 64, r))
            break;
        EXPECT_TRUE(r.X + r.Width  <= 128u);
        EXPECT_TRUE(r.Y + r.Height <= 128u);
    }
}

TEST(TextRectanglePackerTest, OverflowReturnsFalse)
{
    CRectanglePacker packer(64, 64);
    CRectanglePacker::Rect r;
    EXPECT_TRUE(packer.Allocate(64, 64, r));
    // Atlas is full - next allocation must fail
    EXPECT_FALSE(packer.Allocate(1, 1, r)) << "Allocation must fail when atlas is full";
}

TEST(TextRectanglePackerTest, OversizeAllocationReturnsFalse)
{
    CRectanglePacker packer(64, 64);
    CRectanglePacker::Rect r;
    EXPECT_FALSE(packer.Allocate(128, 128, r)) << "Allocation larger than atlas must fail";
}

//================================================================================================
TEST(TextSDFGeneratorTest, EmptyOutlineReturnsFalse)
{
    // This is the space-character case that caused the blank-screen bug:
    // CacheGlyph passed an empty outline to Generate, which must return false,
    // allowing CacheGlyph to fall back to a metrics-only entry.
    GlyphOutline outline; // IsEmpty() == true
    CTrueTypeFont font;
    CSDFGenerator gen;
    CSDFGenerator::Config cfg;
    SDFBitmap bmp;
    EXPECT_FALSE(gen.Generate(outline, font, cfg, bmp)) << "Generate must return false for an empty (outline-less) glyph";
}

TEST(TextSDFGeneratorTest, ValidOutlineReturnsTrue)
{
    CTrueTypeFont font; // metrics not needed for SDF pixel loop
    GlyphOutline outline = MakeSquareOutline(500.0f);
    CSDFGenerator gen;
    CSDFGenerator::Config cfg;
    cfg.TextureSize = 16;
    SDFBitmap bmp;
    EXPECT_TRUE(gen.Generate(outline, font, cfg, bmp)) << "Generate must succeed for a valid outline";
}

TEST(TextSDFGeneratorTest, OutputBitmapDimensionsMatchConfig)
{
    CTrueTypeFont font;
    GlyphOutline outline = MakeSquareOutline();
    CSDFGenerator gen;
    CSDFGenerator::Config cfg;
    cfg.TextureSize = 32;
    SDFBitmap bmp;
    gen.Generate(outline, font, cfg, bmp);
    EXPECT_EQ(32u, bmp.Width);
    EXPECT_EQ(32u, bmp.Height);
}

TEST(TextSDFGeneratorTest, OutputDataSizeMatchesBitmap)
{
    CTrueTypeFont font;
    GlyphOutline outline = MakeSquareOutline();
    CSDFGenerator gen;
    CSDFGenerator::Config cfg;
    cfg.TextureSize  = 16;
    cfg.GenerateMSDF = false;
    SDFBitmap bmp;
    gen.Generate(outline, font, cfg, bmp);
    size_t expected = (size_t)bmp.Width * bmp.Height * bmp.BytesPerPixel;
    EXPECT_EQ(expected, bmp.Data.size());
}

TEST(TextSDFGeneratorTest, SingleChannelSDFBytesPerPixelIsOne)
{
    CTrueTypeFont font;
    GlyphOutline outline = MakeSquareOutline();
    CSDFGenerator gen;
    CSDFGenerator::Config cfg;
    cfg.GenerateMSDF = false;
    SDFBitmap bmp;
    gen.Generate(outline, font, cfg, bmp);
    EXPECT_EQ(1u, bmp.BytesPerPixel);
}

TEST(TextSDFGeneratorTest, AdvanceWidthNormalizedByUnitsPerEm)
{
    // AdvanceWidth must be normalised (outline.AdvanceWidth / UnitsPerEm).
    // With a synthetic outline, font.UnitsPerEm defaults to 0 which would
    // divide-by-zero, so we use a real font for this check.
    CTrueTypeFont font;
    if (!LoadSystemFont(font))
    {
        std::cerr << "Skipped: system font not available" << std::endl;
        return;
    }

    uint16_t idx = font.GetGlyphIndex('A');
    GlyphOutline outline;
    EXPECT_TRUE(font.GetGlyphOutline(idx, outline));

    CSDFGenerator gen;
    CSDFGenerator::Config cfg;
    cfg.TextureSize = 16;
    SDFBitmap bmp;
    EXPECT_TRUE(gen.Generate(outline, font, cfg, bmp));

    // AdvanceWidth from SDF is NormalizeX(outline.AdvanceWidth) = fontUnits / UnitsPerEm
    // It must be in (0, 3] for any real font (most advances are 0.3-0.7 em)
    EXPECT_TRUE(bmp.AdvanceWidth > 0.0f && bmp.AdvanceWidth <= 3.0f) << "AdvanceWidth from SDF must be a normalised em fraction";
}

//================================================================================================
TEST(TextFontLoadTest, LoadFromBuffer_Success)
{
    CTrueTypeFont font;
    if (!LoadSystemFont(font))
    {
        std::cerr << "Skipped: system font not available" << std::endl;
        return;
    }
    // Basic sanity: UnitsPerEm should be a common value (1000 or 2048)
    uint16_t upm = font.GetUnitsPerEm();
    EXPECT_TRUE(upm >= 256 && upm <= 4096) << "UnitsPerEm out of expected range";
}

TEST(TextFontLoadTest, SpaceGlyphHasEmptyOutline)
{
    // This is the key regression test for the blank-screen bug.
    // The space character (U+0020) has no contours; its outline must be empty
    // so SDFGenerator::Generate returns false and CacheGlyph can create a
    // metrics-only (no-quad) entry rather than failing entirely.
    CTrueTypeFont font;
    if (!LoadSystemFont(font))
    {
        std::cerr << "Skipped: system font not available" << std::endl;
        return;
    }

    uint16_t spaceIdx = font.GetGlyphIndex(' ');
    GlyphOutline outline;
    // GetGlyphOutline returns true even for space (metrics-only glyph)
    EXPECT_TRUE(font.GetGlyphOutline(spaceIdx, outline)) << "GetGlyphOutline must succeed for space";
    EXPECT_TRUE(outline.IsEmpty()) << "Space glyph must have an empty outline (no contours)";
    // But it must still carry a non-zero advance width
    EXPECT_TRUE(outline.AdvanceWidth > 0.0f) << "Space glyph must have a positive advance width";
}

TEST(TextFontLoadTest, RegularGlyphHasNonEmptyOutline)
{
    CTrueTypeFont font;
    if (!LoadSystemFont(font))
    {
        std::cerr << "Skipped: system font not available" << std::endl;
        return;
    }

    uint16_t idx = font.GetGlyphIndex('A');
    GlyphOutline outline;
    EXPECT_TRUE(font.GetGlyphOutline(idx, outline));
    EXPECT_FALSE(outline.IsEmpty()) << "'A' glyph must have at least one contour";
    EXPECT_TRUE(!outline.Contours.empty() && !outline.Contours[0].Points.empty());
}

TEST(TextFontLoadTest, AdvanceWidthPositiveForPrintableGlyphs)
{
    CTrueTypeFont font;
    if (!LoadSystemFont(font))
    {
        std::cerr << "Skipped: system font not available" << std::endl;
        return;
    }

    for (char ch : std::string("Hello Canvas"))
    {
        uint16_t idx = font.GetGlyphIndex((uint32_t)(uint8_t)ch);
        GlyphOutline outline;
        font.GetGlyphOutline(idx, outline);
        EXPECT_TRUE(outline.AdvanceWidth > 0.0f) << "Every printable glyph must have a positive advance width";
    }
}

TEST(TextFontLoadTest, NormalizeXDividesUnitsPerEm)
{
    CTrueTypeFont font;
    if (!LoadSystemFont(font))
    {
        std::cerr << "Skipped: system font not available" << std::endl;
        return;
    }

    uint16_t upm = font.GetUnitsPerEm();
    float normalized = font.NormalizeX(static_cast<float>(upm));
    EXPECT_NEAR(1.0f, normalized, 1e-5f) << "NormalizeX(UnitsPerEm) must equal 1.0";
}

} // namespace CanvasUnitTest

