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
            CComPtr<ICanvas> pCanvas;
            Canvas::Result result = CreateCanvas(ICanvas::IId, (void **) &pCanvas);
        }
    };
}
