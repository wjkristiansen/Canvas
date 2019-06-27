//================================================================================================
// Light
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CLight :
    public CSceneGraphNode,
    public XLight
{
public:
    CLight(CCanvas* pCanvas, PCWSTR szName) :
        CSceneGraphNode(pCanvas) {}
    GEMMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj)
    {
        if (XLight::IId == iid)
        {
            *ppObj = this;
            AddRef();
            return Result::Success;
        }

        return CSceneGraphNode::InternalQueryInterface(iid, ppObj);
    }
};

