//================================================================================================
// Generic
//================================================================================================

#include "stdafx.h"

std::unordered_set<typename XGeneric *> g_OutstandingGenerics;

extern void ReportGenericLeaks()
{
    for (XGeneric *pGeneric : g_OutstandingGenerics)
    {
        std::cout << "Leaked object: ";
        XGeneric *pXGeneric;
        if (Succeeded(pGeneric->QueryInterface(CANVAS_PPV_ARGS(&pXGeneric))))
        {
            ULONG RefCount = pXGeneric->Release();
            std::cout << "RefCount=" << RefCount << " ";
        }
        CCanvasPtr<XObjectName> pObjectName;
        if (Succeeded(pGeneric->QueryInterface(CANVAS_PPV_ARGS(&pObjectName))))
        {
            std::cout << "Name=\"" << pObjectName->GetName() << "\" ";
        }
        std::cout << std::endl;
    }
}
