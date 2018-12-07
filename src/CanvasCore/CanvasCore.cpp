// CanvasCore.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"

using namespace Canvas;

//------------------------------------------------------------------------------------------------
class CCanvas :
    public XCanvas,
    public CGenericBase
{
public:
    CCanvas() = default;

    CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj)
    {
        *ppObj = nullptr;
        switch (iid)
        {
        case InterfaceId::XCanvas:
            *ppObj = this;
            AddRef();
            break;

        default:
            return __super::InternalQueryInterface(iid, ppObj);
        }

        return Result::Success;
    }

    CANVASMETHOD(CreateScene)(InterfaceId iid, void **ppScene)
    {
        *ppScene = nullptr;
        try
        {
            // Create a root scene graph node object
            CCanvasPtr<CSceneGraphNode> pSceneGraphNode;
            InterfaceId iids[] =
            {
                InterfaceId::XSceneGraphNode,
            };
            CreateCustomObject(iids, 1, reinterpret_cast<void **>(&pSceneGraphNode));
            CCanvasPtr<XScene> pScene = new CGeneric<CScene>(pSceneGraphNode); // throw(std::bad_alloc)
            return pScene->QueryInterface(iid, ppScene);
        }
        catch (std::bad_alloc&)
        {
            return Result::OutOfMemory;
        }
        return Result::Success;
    }

    CANVASMETHOD(CreateTransformObject)(InterfaceId iid, _Outptr_ void **ppObj)
    {
        class CTransformObject :
            public XGeneric,
            public CGenericBase
        {
        public:
            //CInnerGeneric<CTransform, InterfaceId::XTransform> m_Transform;
            //CInnerGeneric<CSceneGraphNode, InterfaceId::XSceneGraphNode> m_SceneGraphNode;

            //CTransformObject() :
            //    m_Transform(this),
            //    m_SceneGraphNode(this) 
            //{}
            virtual ~CTransformObject() {}

            //CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj)
            //{
            //    if (InterfaceId::XTransform == iid)
            //    {
            //        return m_Transform.InternalQueryInterface(iid, ppObj);
            //    }
            //    if (InterfaceId::XSceneGraphNode == iid)
            //    {
            //        return m_SceneGraphNode.InternalQueryInterface(iid, ppObj);
            //    }

            //    return __super::InternalQueryInterface(iid, ppObj);
            //}
        };

        try
        {
            CCanvasPtr<CTransformObject> pObj = new CGeneric<CTransformObject>(); // throw(std::bad_alloc)
            *ppObj = pObj;
            pObj.Detach();
            return Result::Success;
        }
        catch(std::bad_alloc &)
        {
            return Result::OutOfMemory;
        }
        return Result::NotImplemented;
    }

    CANVASMETHOD(CreateCameraObject)(InterfaceId iid, _Outptr_ void **ppObj)
    {
        return Result::NotImplemented;
    }

    CANVASMETHOD(CreateLightObject)(InterfaceId iid, _Outptr_ void **ppObj)
    {
        return Result::NotImplemented;
    }

    CANVASMETHOD(CreateModelInstanceObject)(InterfaceId iid, _Outptr_ void **ppObj)
    {
        return Result::NotImplemented;
    }

    //------------------------------------------------------------------------------------------------
    CANVASMETHOD(CreateCustomObject)(InterfaceId *pInnerInterfaces, UINT numInnerInterfaces, _Outptr_ void **ppObj)
    {
        Result res = Result::Success;

        if (numInnerInterfaces == 0)
        {
            return Result::InvalidArg;
        }

        if (pInnerInterfaces == nullptr)
        {
            return Result::BadPointer;
        }

        try
        {
            CCanvasPtr<CCustomObject> pObject = new CGeneric<CCustomObject>(); // throw(std::bad_alloc)

            for (UINT i = 0; i < numInnerInterfaces; ++i)
            {
                switch (pInnerInterfaces[i])
                {
                case InterfaceId::XSceneGraphNode:
                    pObject->m_InnerElements.emplace_back(std::make_unique<CInnerGeneric<CSceneGraphNode, InterfaceId::XSceneGraphNode>>(pObject)); // throw(std::bad_alloc)
                    break;
                case InterfaceId::XTransform:
                    pObject->m_InnerElements.emplace_back(std::make_unique<CInnerGeneric<CTransform, InterfaceId::XTransform>>(pObject)); // throw(std::bad+alloc)
                    break;
                case InterfaceId::XModelInstance:
                    pObject->m_InnerElements.emplace_back(std::make_unique<CInnerGeneric<CModelInstance, InterfaceId::XModelInstance>>(pObject)); // throw(std::bad+alloc)
                    break;
                case InterfaceId::XCamera:
                    pObject->m_InnerElements.emplace_back(std::make_unique<CInnerGeneric<CCamera, InterfaceId::XCamera>>(pObject)); // throw(std::bad+alloc)
                    break;
                case InterfaceId::XLight:
                    pObject->m_InnerElements.emplace_back(std::make_unique<CInnerGeneric<CLight, InterfaceId::XLight>>(pObject)); // throw(std::bad+alloc)
                    break;
                }
            }

            return pObject->QueryInterface(pInnerInterfaces[0], ppObj);
        }
        catch (std::bad_alloc&)
        {
            return Result::OutOfMemory;
        }
    }
};

//------------------------------------------------------------------------------------------------
Result CANVASAPI CreateCanvas(InterfaceId iid, void **ppCanvas)
{
    *ppCanvas = nullptr;

    try
    {
        if (iid == XCanvas::IId)
        {
            CCanvas *pCanvas = new CGeneric<CCanvas>; // throw(bad_alloc)
            *ppCanvas = pCanvas;
            pCanvas->AddRef();
        }
    }
    catch (std::bad_alloc &)
    {
        return Result::OutOfMemory;
    }

    return Result::Success;
}
