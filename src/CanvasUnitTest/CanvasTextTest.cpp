//================================================================================================
// CanvasTextTest - Unit tests for the text rendering subsystem
//
// Tests cover CPU-only logic: UTF-8 decoding, glyph layout, rectangle packing,
// SDF generation, and font loading/outline extraction.
// No GPU device is required for any test in this file.
//================================================================================================

#include "pch.h"
#include "CppUnitTest.h"

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

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
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
TEST_CLASS(TextUTF8DecodeTest)
{
public:

    TEST_METHOD(EmptyString)
    {
        std::vector<uint32_t> codepoints;
        Assert::IsTrue(Succeeded(CTextLayout::DecodeUtf8("", codepoints)));
        Assert::AreEqual((size_t)0, codepoints.size());
    }

    TEST_METHOD(PlainASCII)
    {
        std::vector<uint32_t> codepoints;
        Assert::IsTrue(Succeeded(CTextLayout::DecodeUtf8("Hello", codepoints)));
        Assert::AreEqual((size_t)5, codepoints.size());
        Assert::AreEqual((uint32_t)'H', codepoints[0]);
        Assert::AreEqual((uint32_t)'e', codepoints[1]);
        Assert::AreEqual((uint32_t)'o', codepoints[4]);
    }

    TEST_METHOD(SpacePreserved)
    {
        // Space (0x20) must survive decoding – it was the root cause of the blank-screen bug
        std::vector<uint32_t> codepoints;
        Assert::IsTrue(Succeeded(CTextLayout::DecodeUtf8("A B", codepoints)));
        Assert::AreEqual((size_t)3, codepoints.size());
        Assert::AreEqual((uint32_t)' ', codepoints[1]);
    }

    TEST_METHOD(Newline)
    {
        std::vector<uint32_t> codepoints;
        Assert::IsTrue(Succeeded(CTextLayout::DecodeUtf8("a\nb", codepoints)));
        Assert::AreEqual((size_t)3, codepoints.size());
        Assert::AreEqual((uint32_t)'\n', codepoints[1]);
    }

    TEST_METHOD(TwoByteUTF8)
    {
        // U+00E9 LATIN SMALL LETTER E WITH ACUTE: 0xC3 0xA9
        const char utf8[] = { '\xC3', '\xA9', '\0' };
        std::vector<uint32_t> codepoints;
        Assert::IsTrue(Succeeded(CTextLayout::DecodeUtf8(utf8, codepoints)));
        Assert::AreEqual((size_t)1, codepoints.size());
        Assert::AreEqual((uint32_t)0x00E9u, codepoints[0]);
    }

    TEST_METHOD(ThreeByteUTF8)
    {
        // U+4E16 CJK: 0xE4 0xB8 0x96
        const char utf8[] = { '\xE4', '\xB8', '\x96', '\0' };
        std::vector<uint32_t> codepoints;
        Assert::IsTrue(Succeeded(CTextLayout::DecodeUtf8(utf8, codepoints)));
        Assert::AreEqual((size_t)1, codepoints.size());
        Assert::AreEqual((uint32_t)0x4E16u, codepoints[0]);
    }

    TEST_METHOD(MixedASCIIAndMultibyte)
    {
        // "Hi" + U+00E9 + "!" → 4 codepoints
        const char utf8[] = { 'H', 'i', '\xC3', '\xA9', '!', '\0' };
        std::vector<uint32_t> codepoints;
        Assert::IsTrue(Succeeded(CTextLayout::DecodeUtf8(utf8, codepoints)));
        Assert::AreEqual((size_t)4, codepoints.size());
        Assert::AreEqual((uint32_t)'H',    codepoints[0]);
        Assert::AreEqual((uint32_t)0x00E9u, codepoints[2]);
        Assert::AreEqual((uint32_t)'!',    codepoints[3]);
    }
};

//================================================================================================
TEST_CLASS(TextLayoutGlyphTest)
{
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

public:

    TEST_METHOD(NormalGlyphProduces6Vertices)
    {
        GlyphAtlasEntry entry = MakeEntry();
        Math::FloatVector3 pos(10.0f, 20.0f, 0.0f);
        std::vector<TextVertex> verts;

        CTextLayout::LayoutGlyph(0x41, CTrueTypeFont{}, entry, pos, 0xFFFFFFFF, 1.0f, verts);

        Assert::AreEqual((size_t)6, verts.size());
    }

    TEST_METHOD(ZeroSizeGlyphProducesNoVertices)
    {
        // BitmapWidth/Height == 0 → whitespace glyph, no quad should be emitted
        GlyphAtlasEntry entry = MakeEntry(0.0f, 0.0f);
        Math::FloatVector3 pos(0.0f, 0.0f, 0.0f);
        std::vector<TextVertex> verts;

        CTextLayout::LayoutGlyph(' ', CTrueTypeFont{}, entry, pos, 0xFFFFFFFF, 1.0f, verts);

        Assert::AreEqual((size_t)0, verts.size());
    }

    TEST_METHOD(ZeroSizeGlyphReturnsAdvance)
    {
        GlyphAtlasEntry entry = MakeEntry(0.0f, 0.0f);
        entry.AdvanceWidth = 0.35f;
        Math::FloatVector3 pos(0.0f, 0.0f, 0.0f);
        std::vector<TextVertex> verts;

        float adv = CTextLayout::LayoutGlyph(' ', CTrueTypeFont{}, entry, pos, 0xFFFFFFFF, 1.0f, verts);

        Assert::AreEqual(0.35f, adv, L"Advance must be returned even for zero-size (whitespace) glyph");
    }

    TEST_METHOD(VertexPositionsMatchExpected)
    {
        GlyphAtlasEntry entry = MakeEntry(40.0f, 48.0f);
        entry.LeftBearing = 0.0f;
        entry.TopBearing  = 0.0f;
        Math::FloatVector3 pos(100.0f, 200.0f, 0.5f);
        std::vector<TextVertex> verts;

        CTextLayout::LayoutGlyph(0x41, CTrueTypeFont{}, entry, pos, 0xFFFF0000, 1.0f, verts);

        Assert::AreEqual(6u, (unsigned)verts.size());

        // Top-left of quad (fontSize=1.0 so em fractions == pixels directly)
        float x0 = 100.0f, y0 = 200.0f, x1 = 140.0f, y1 = 248.0f;

        // All 6 vertices must have X in [x0, x1] and Y in [y0, y1]
        for (const auto& v : verts)
        {
            Assert::IsTrue(v.Position.X >= x0 && v.Position.X <= x1);
            Assert::IsTrue(v.Position.Y >= y0 && v.Position.Y <= y1);
            Assert::AreEqual(0.5f, v.Position.Z, 1e-6f);
        }
    }

    TEST_METHOD(TexCoordsMatchAtlasEntry)
    {
        GlyphAtlasEntry entry = MakeEntry();
        Math::FloatVector3 pos(0.0f, 0.0f, 0.0f);
        std::vector<TextVertex> verts;

        CTextLayout::LayoutGlyph(0x41, CTrueTypeFont{}, entry, pos, 0xFFFFFFFF, 1.0f, verts);

        // All UVs must lie within the atlas entry's UV bounds
        for (const auto& v : verts)
        {
            Assert::IsTrue(v.TexCoord.X >= entry.AtlasU0 - 1e-5f && v.TexCoord.X <= entry.AtlasU1 + 1e-5f);
            Assert::IsTrue(v.TexCoord.Y >= entry.AtlasV0 - 1e-5f && v.TexCoord.Y <= entry.AtlasV1 + 1e-5f);
        }
    }

    TEST_METHOD(ColorIsStoredInVertices)
    {
        GlyphAtlasEntry entry = MakeEntry();
        Math::FloatVector3 pos(0.0f, 0.0f, 0.0f);
        std::vector<TextVertex> verts;
        uint32_t color = 0xDEADBEEFu;

        CTextLayout::LayoutGlyph(0x41, CTrueTypeFont{}, entry, pos, color, 1.0f, verts);

        for (const auto& v : verts)
            Assert::AreEqual(color, v.Color);
    }
};

//================================================================================================
TEST_CLASS(TextRectanglePackerTest)
{
public:

    TEST_METHOD(SingleAllocation)
    {
        CRectanglePacker packer(512, 512);
        CRectanglePacker::Rect r;
        Assert::IsTrue(packer.Allocate(64, 64, r));
        Assert::AreEqual(0u, r.X);
        Assert::AreEqual(0u, r.Y);
        Assert::AreEqual(64u, r.Width);
        Assert::AreEqual(64u, r.Height);
    }

    TEST_METHOD(TwoNonOverlappingAllocations)
    {
        CRectanglePacker packer(512, 512);
        CRectanglePacker::Rect r0, r1;
        Assert::IsTrue(packer.Allocate(64, 64, r0));
        Assert::IsTrue(packer.Allocate(64, 64, r1));

        // Rectangles must not overlap
        bool xSep = r0.X + r0.Width <= r1.X || r1.X + r1.Width <= r0.X;
        bool ySep = r0.Y + r0.Height <= r1.Y || r1.Y + r1.Height <= r0.Y;
        Assert::IsTrue(xSep || ySep, L"Allocated rectangles must not overlap");
    }

    TEST_METHOD(AllRectsStayWithinBounds)
    {
        CRectanglePacker packer(128, 128);
        for (int i = 0; i < 4; ++i)
        {
            CRectanglePacker::Rect r;
            if (!packer.Allocate(64, 64, r))
                break;
            Assert::IsTrue(r.X + r.Width  <= 128u);
            Assert::IsTrue(r.Y + r.Height <= 128u);
        }
    }

    TEST_METHOD(OverflowReturnsFalse)
    {
        CRectanglePacker packer(64, 64);
        CRectanglePacker::Rect r;
        Assert::IsTrue(packer.Allocate(64, 64, r));
        // Atlas is full — next allocation must fail
        Assert::IsFalse(packer.Allocate(1, 1, r), L"Allocation must fail when atlas is full");
    }

    TEST_METHOD(OversizeAllocationReturnsFalse)
    {
        CRectanglePacker packer(64, 64);
        CRectanglePacker::Rect r;
        Assert::IsFalse(packer.Allocate(128, 128, r), L"Allocation larger than atlas must fail");
    }
};

//================================================================================================
TEST_CLASS(TextSDFGeneratorTest)
{
public:

    TEST_METHOD(EmptyOutlineReturnsFalse)
    {
        // This is the space-character case that caused the blank-screen bug:
        // CacheGlyph passed an empty outline to Generate, which must return false,
        // allowing CacheGlyph to fall back to a metrics-only entry.
        GlyphOutline outline; // IsEmpty() == true
        CTrueTypeFont font;
        CSDFGenerator gen;
        CSDFGenerator::Config cfg;
        SDFBitmap bmp;
        Assert::IsFalse(gen.Generate(outline, font, cfg, bmp),
            L"Generate must return false for an empty (outline-less) glyph");
    }

    TEST_METHOD(ValidOutlineReturnsTrue)
    {
        CTrueTypeFont font; // metrics not needed for SDF pixel loop
        GlyphOutline outline = MakeSquareOutline(500.0f);
        CSDFGenerator gen;
        CSDFGenerator::Config cfg;
        cfg.TextureSize = 16;
        SDFBitmap bmp;
        Assert::IsTrue(gen.Generate(outline, font, cfg, bmp),
            L"Generate must succeed for a valid outline");
    }

    TEST_METHOD(OutputBitmapDimensionsMatchConfig)
    {
        CTrueTypeFont font;
        GlyphOutline outline = MakeSquareOutline();
        CSDFGenerator gen;
        CSDFGenerator::Config cfg;
        cfg.TextureSize = 32;
        SDFBitmap bmp;
        gen.Generate(outline, font, cfg, bmp);
        Assert::AreEqual(32u, bmp.Width);
        Assert::AreEqual(32u, bmp.Height);
    }

    TEST_METHOD(OutputDataSizeMatchesBitmap)
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
        Assert::AreEqual(expected, bmp.Data.size());
    }

    TEST_METHOD(SingleChannelSDFBytesPerPixelIsOne)
    {
        CTrueTypeFont font;
        GlyphOutline outline = MakeSquareOutline();
        CSDFGenerator gen;
        CSDFGenerator::Config cfg;
        cfg.GenerateMSDF = false;
        SDFBitmap bmp;
        gen.Generate(outline, font, cfg, bmp);
        Assert::AreEqual(1u, bmp.BytesPerPixel);
    }

    TEST_METHOD(AdvanceWidthNormalizedByUnitsPerEm)
    {
        // AdvanceWidth must be normalised (outline.AdvanceWidth / UnitsPerEm).
        // With a synthetic outline, font.UnitsPerEm defaults to 0 which would
        // divide-by-zero, so we use a real font for this check.
        CTrueTypeFont font;
        if (!LoadSystemFont(font))
        {
            Logger::WriteMessage("Skipped: system font not available");
            return;
        }

        uint16_t idx = font.GetGlyphIndex('A');
        GlyphOutline outline;
        Assert::IsTrue(font.GetGlyphOutline(idx, outline));

        CSDFGenerator gen;
        CSDFGenerator::Config cfg;
        cfg.TextureSize = 16;
        SDFBitmap bmp;
        Assert::IsTrue(gen.Generate(outline, font, cfg, bmp));

        // AdvanceWidth from SDF is NormalizeX(outline.AdvanceWidth) = fontUnits / UnitsPerEm
        // It must be in (0, 3] for any real font (most advances are 0.3-0.7 em)
        Assert::IsTrue(bmp.AdvanceWidth > 0.0f && bmp.AdvanceWidth <= 3.0f,
            L"AdvanceWidth from SDF must be a normalised em fraction");
    }
};

//================================================================================================
TEST_CLASS(TextFontLoadTest)
{
public:

    TEST_METHOD(LoadFromBuffer_Success)
    {
        CTrueTypeFont font;
        if (!LoadSystemFont(font))
        {
            Logger::WriteMessage("Skipped: system font not available");
            return;
        }
        // Basic sanity: UnitsPerEm should be a common value (1000 or 2048)
        uint16_t upm = font.GetUnitsPerEm();
        Assert::IsTrue(upm >= 256 && upm <= 4096, L"UnitsPerEm out of expected range");
    }

    TEST_METHOD(SpaceGlyphHasEmptyOutline)
    {
        // This is the key regression test for the blank-screen bug.
        // The space character (U+0020) has no contours; its outline must be empty
        // so SDFGenerator::Generate returns false and CacheGlyph can create a
        // metrics-only (no-quad) entry rather than failing entirely.
        CTrueTypeFont font;
        if (!LoadSystemFont(font))
        {
            Logger::WriteMessage("Skipped: system font not available");
            return;
        }

        uint16_t spaceIdx = font.GetGlyphIndex(' ');
        GlyphOutline outline;
        // GetGlyphOutline returns true even for space (metrics-only glyph)
        Assert::IsTrue(font.GetGlyphOutline(spaceIdx, outline),
            L"GetGlyphOutline must succeed for space");
        Assert::IsTrue(outline.IsEmpty(),
            L"Space glyph must have an empty outline (no contours)");
        // But it must still carry a non-zero advance width
        Assert::IsTrue(outline.AdvanceWidth > 0.0f,
            L"Space glyph must have a positive advance width");
    }

    TEST_METHOD(RegularGlyphHasNonEmptyOutline)
    {
        CTrueTypeFont font;
        if (!LoadSystemFont(font))
        {
            Logger::WriteMessage("Skipped: system font not available");
            return;
        }

        uint16_t idx = font.GetGlyphIndex('A');
        GlyphOutline outline;
        Assert::IsTrue(font.GetGlyphOutline(idx, outline));
        Assert::IsFalse(outline.IsEmpty(), L"'A' glyph must have at least one contour");
        Assert::IsTrue(!outline.Contours.empty() && !outline.Contours[0].Points.empty());
    }

    TEST_METHOD(AdvanceWidthPositiveForPrintableGlyphs)
    {
        CTrueTypeFont font;
        if (!LoadSystemFont(font))
        {
            Logger::WriteMessage("Skipped: system font not available");
            return;
        }

        for (char ch : std::string("Hello Canvas"))
        {
            uint16_t idx = font.GetGlyphIndex((uint32_t)(uint8_t)ch);
            GlyphOutline outline;
            font.GetGlyphOutline(idx, outline);
            Assert::IsTrue(outline.AdvanceWidth > 0.0f,
                L"Every printable glyph must have a positive advance width");
        }
    }

    TEST_METHOD(NormalizeXDividesUnitsPerEm)
    {
        CTrueTypeFont font;
        if (!LoadSystemFont(font))
        {
            Logger::WriteMessage("Skipped: system font not available");
            return;
        }

        uint16_t upm = font.GetUnitsPerEm();
        float normalized = font.NormalizeX(static_cast<float>(upm));
        Assert::AreEqual(1.0f, normalized, 1e-5f,
            L"NormalizeX(UnitsPerEm) must equal 1.0");
    }
};

} // namespace CanvasUnitTest
