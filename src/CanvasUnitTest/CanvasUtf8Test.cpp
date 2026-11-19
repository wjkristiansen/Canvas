//================================================================================================
// CanvasUtf8Test - Tests that Canvas executables pass UTF-8 strings to the OS unchanged
//================================================================================================

#include "pch.h"

//------------------------------------------------------------------------------------------------
TEST(CanvasUtf8, ProcessUsesUtf8CodePage)
{
    // Utf8.manifest, merged in by the CanvasUtf8Manifest target, selects the UTF-8 code page.
    EXPECT_EQ(GetACP(), UINT(CP_UTF8));
}

//------------------------------------------------------------------------------------------------
TEST(CanvasUtf8, NarrowLiteralsAreUtf8)
{
    // U+00E9 is two bytes in UTF-8, plus the terminator.
    static_assert(sizeof("é") == 3);
    EXPECT_STREQ("é", "\xc3\xa9");
}
