//================================================================================================
// CanvasCore
//================================================================================================

#include "pch.h"

#include "Canvas.h"
#include "CanvasElement.h"
#include "Camera.h"
#include "Light.h"
#include "Mesh.h"
#include "Model.h"
#include "Scene.h"
#include "CanvasGfx.h"
#include "FontImpl.h"
#include "UIGraph.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
Gem::Result CCanvas::Initialize()
{
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
void CCanvas::Uninitialize()
{
    // Iterate still active XCanvasElement objects and report them as leaks
    for(XCanvasElement *pElement : m_ActiveCanvasElements)
    {
        PCSTR pszName = pElement->GetName();
        if(pszName)
        {
            Canvas::LogError(GetLogger(), "%s leaked, Name: %s", pElement->GetTypeName(), pszName);
        }
        else
        {
            Canvas::LogError(GetLogger(), "%s leaked", pElement->GetTypeName());
        }
    }
}

//------------------------------------------------------------------------------------------------
CCanvas::~CCanvas()
{
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::RegisterElement(XCanvasElement *pElement)
{
    if (!pElement)
        return Gem::Result::BadPointer;

    std::lock_guard<std::mutex> lock(m_Mutex);

    PCSTR pszName = pElement->GetName();

    if (m_ActiveCanvasElements.find(pElement) != m_ActiveCanvasElements.end())
    {
        if (GetLogger())
        {
            if (pszName)
            {
                Canvas::LogWarn(GetLogger(), "Element already registered: %s (Name: %s)",
                               pElement->GetTypeName(), pszName);
            }
            else
            {
                Canvas::LogWarn(GetLogger(), "Element already registered: %s", pElement->GetTypeName());
            }
        }
        return Gem::Result::InvalidArg;
    }

    m_ActiveCanvasElements.emplace(pElement);

    if(GetLogger())
    {
        if (pszName)
        {
            Canvas::LogInfo(GetLogger(), "Registered element: %s (Name: %s)",
                           pElement->GetTypeName(), pszName);
        }
        else
        {
            Canvas::LogError(GetLogger(), "Registered unnamed element: %s", pElement->GetTypeName());
        }
    }
    
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::UnregisterElement(XCanvasElement *pElement)
{
    if (!pElement)
        return Gem::Result::BadPointer;

    std::lock_guard<std::mutex> lock(m_Mutex);

    PCSTR pszName = pElement->GetName();

    auto it = m_ActiveCanvasElements.find(pElement);
    if (it == m_ActiveCanvasElements.end())
    {
        if (GetLogger())
        {
            if (pszName)
            {
                Canvas::LogError(GetLogger(), "Attempted to unregister element that was not registered: %s (Name: %s)",
                                pElement->GetTypeName(), pszName);
            }
            else
            {
                Canvas::LogError(GetLogger(), "Attempted to unregister element that was not registered: %s",
                                pElement->GetTypeName());
            }
        }
        return Gem::Result::NotFound;
    }

    if(GetLogger())
    {
        if (pszName)
        {
            Canvas::LogInfo(GetLogger(), "Unregistering element: %s (Name: %s)",
                           pElement->GetTypeName(), pszName);
        }
        else
        {
            Canvas::LogInfo(GetLogger(), "Unregistering element type: %s", pElement->GetTypeName());
        }
    }

    m_ActiveCanvasElements.erase(it);
    
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::CreateSceneGraph(XGfxDevice *pDevice, XSceneGraph **ppScene, PCSTR name)
{
    CFunctionSentinel sentinel("XCanvas::CreateSceneGraph", m_pLogger);

    return CreateElement<CSceneGraph>(ppScene, name, pDevice);
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::CreateSceneGraphNode(XSceneGraphNode **ppNode, PCSTR name)
{
    CFunctionSentinel sentinel("XCanvas::CreateSceneGraphNode", m_pLogger);

    return CreateElement<CSceneGraphNode>(ppNode, name);
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::CreateCamera(XCamera **ppCamera, PCSTR name)
{
    CFunctionSentinel sentinel("XCanvas::CreateCamera", m_pLogger);

    return CreateElement<CCamera>(ppCamera, name);
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::CreateLight(LightType type, XLight **ppLight, PCSTR name)
{
    CFunctionSentinel sentinel("XCanvas::CreateLight", m_pLogger);

    // Create using the standard CreateElement pattern to ensure proper registration
    Gem::Result result = CreateElement<CLight>(ppLight, name, type);
    
    return result;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::CreateMeshInstance(XMeshInstance **ppMeshInstance, PCSTR name)
{
    CFunctionSentinel sentinel("XCanvas::CreateMeshInstance", m_pLogger);

    return CreateElement<CMeshInstance>(ppMeshInstance, name);
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::CreateModel(XGfxDevice *pDevice, XModel **ppModel, PCSTR name)
{
    CFunctionSentinel sentinel("XCanvas::CreateModel", m_pLogger);

    if (!pDevice)
        return Gem::Result::BadPointer;

    return CreateElement<CModel>(ppModel, name, pDevice);
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::CreateFont(const uint8_t* pTTFData, size_t dataSize, PCSTR name, XFont** ppFont)
{
    CFunctionSentinel sentinel("XCanvas::CreateFont", m_pLogger);

    if (!pTTFData || !ppFont)
        return Gem::Result::BadPointer;

    Gem::TGemPtr<CFont> pFont;
    Gem::Result result = Gem::TGenericImpl<CFont>::Create(&pFont, name ? name : "");
    if (Gem::Failed(result))
        return result;

    result = pFont->LoadFromBuffer(pTTFData, dataSize);
    if (Gem::Failed(result))
        return result;

    *ppFont = pFont.Detach();
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::CreateUIGraph(XGfxDevice* pDevice, XUIGraph** ppGraph)
{
    CFunctionSentinel sentinel("XCanvas::CreateUIGraph", m_pLogger);

    if (!pDevice || !ppGraph)
        return Gem::Result::BadPointer;

    Gem::TGemPtr<CUIGraph> pGraph = new Gem::TGenericImpl<CUIGraph>();
    pGraph->SetName("UIGraph");
    pGraph->Register(this);
    pGraph->SetDevice(pDevice);
    *ppGraph = pGraph.Detach();
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
Gem::Result CANVAS_API CreateCanvas(XLogger *pLogger, XCanvas **ppCanvas)
{
    CFunctionSentinel sentinel("CreateCanvas", pLogger);
    
    *ppCanvas = nullptr;

    try
    {
        Canvas::LogInfo(pLogger, "CANVAS: CreateCanvas: Creating canvas object...");
        
        Gem::TGemPtr<CCanvas> pCanvas;
        Gem::ThrowGemError(Gem::TGenericImpl<CCanvas>::Create(&pCanvas, pLogger)); // throw(Gem::GemError)
        *ppCanvas = pCanvas.Detach();

        return Gem::Result::Success;
    }
    catch (const Gem::GemError &e)
    {
        Canvas::LogError(pLogger, "CANVAS: FAILURE in CreateCanvas");
        return e.Result();
    }
}

GEMMETHODIMP CCanvas::LoadPlugin(PCSTR path, XCanvasPlugin **ppPlugin)
{
    CFunctionSentinel sentinel("XCanvas::LoadPlugin", m_pLogger);

    try
    {
        auto &module = m_PluginModules.emplace_back(path);
        Gem::ThrowGemError(module.LoadPlugin(ppPlugin));
    }
    catch (const std::bad_alloc &)
    {
        return Gem::Result::OutOfMemory;
    }
    catch (const Gem::GemError &e)
    {
        return e.Result();
    }

    return Gem::Result::Success;
}

} // namespace Canvas