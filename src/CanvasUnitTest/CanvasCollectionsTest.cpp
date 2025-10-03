#include "pch.h"
#include "CppUnitTest.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace CanvasUnitTest
{
    TEST_CLASS(CanvasCollectionsTest)
    {
    public:

        TEST_METHOD(SimpleSortedListTest)
        {
            Gem::TGemPtr<XCanvas> pCanvas;
            Result result = CreateCanvas(&pCanvas);
        }
    };
}
