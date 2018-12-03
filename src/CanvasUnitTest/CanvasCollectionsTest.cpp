#include "stdafx.h"
#include "CppUnitTest.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

using namespace Canvas;

namespace CanvasUnitTest
{
    TEST_CLASS(CanvasCollectionsTest)
    {
    public:

        TEST_METHOD(SimpleSortedListTest)
        {
            CComPtr<XCanvas> pCanvas;
            Canvas::Result result = CreateCanvas(XCanvas::IId, (void **) &pCanvas);
        }
    };
}
