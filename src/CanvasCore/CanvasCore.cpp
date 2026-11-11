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
#include "World.h"
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
        const char * pszName = pElement->GetName();
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

    const char * pszName = pElement->GetName();

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
            // A world object's name is an optional label
            Gem::TGemPtr<XWorldObject> pWorldObject;
            if (Gem::Succeeded(pElement->QueryInterface(&pWorldObject)))
                Canvas::LogInfo(GetLogger(), "Registered element: %s", pElement->GetTypeName());
            else
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

    const char * pszName = pElement->GetName();

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
GEMMETHODIMP CCanvas::CreateWorld(XGfxDevice *pDevice, XWorld **ppWorld, const char * name)
{
    CFunctionSentinel sentinel("XCanvas::CreateWorld", m_pLogger);

    return CreateElement<CWorld>(ppWorld, name, pDevice);
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::CreateSceneNode(XSceneNode **ppNode, const char * name)
{
    CFunctionSentinel sentinel("XCanvas::CreateSceneNode", m_pLogger);

    return CreateElement<CSceneNode>(ppNode, name);
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::CreateWorldObject(XModel *pModel, XWorldObject **ppObject, const char * name)
{
    CFunctionSentinel sentinel("XCanvas::CreateWorldObject", m_pLogger);

    if (!ppObject)
        return Gem::Result::BadPointer;
    *ppObject = nullptr;

    if (pModel && pModel->GetCanvas() != this)
    {
        Canvas::LogError(m_pLogger, "XCanvas::CreateWorldObject: the model was created by another canvas");
        return Gem::Result::InvalidArg;
    }

    // A world object is named after the model it instantiates unless a name is given
    if (!name && pModel)
        name = pModel->GetName();

    Gem::TGemPtr<CWorldObject> pObject;
    Gem::Result result = TCanvasElement<XWorldObject>::CreateAndRegister(&pObject, this, name);
    if (Gem::Failed(result))
        return result;

    if (pModel)
    {
        Gem::TGemPtr<XSceneNode> pInstanceRoot;
        result = static_cast<CModel *>(pModel)->CreateInstance(&pInstanceRoot);
        if (Gem::Failed(result))
            return result;
        pObject->SetInstance(static_cast<CSceneNode *>(pInstanceRoot.Get()), pModel->GetDevice());
    }

    *ppObject = pObject.Detach();
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::CreateCamera(XCamera **ppCamera, const char * name)
{
    CFunctionSentinel sentinel("XCanvas::CreateCamera", m_pLogger);

    return CreateElement<CCamera>(ppCamera, name);
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::CreateLight(LightType type, XLight **ppLight, const char * name)
{
    CFunctionSentinel sentinel("XCanvas::CreateLight", m_pLogger);

    // Create using the standard CreateElement pattern to ensure proper registration
    Gem::Result result = CreateElement<CLight>(ppLight, name, type);
    
    return result;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::CreateMeshInstance(XMeshInstance **ppMeshInstance, const char * name)
{
    CFunctionSentinel sentinel("XCanvas::CreateMeshInstance", m_pLogger);

    return CreateElement<CMeshInstance>(ppMeshInstance, name);
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::CreateModelBuilder(XGfxDevice *pDevice, XModelBuilder **ppBuilder, const char * name)
{
    CFunctionSentinel sentinel("XCanvas::CreateModelBuilder", m_pLogger);

    if (!pDevice)
        return Gem::Result::BadPointer;

    return CreateElement<CModelBuilder>(ppBuilder, name, pDevice);
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::CreateFont(const uint8_t* pTTFData, size_t dataSize, const char * name, XFont** ppFont)
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

    return CreateElement<CUIGraph>(ppGraph, "UIGraph", pDevice);
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

GEMMETHODIMP CCanvas::LoadPlugin(const char * path, XCanvasPlugin **ppPlugin)
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