//================================================================================================
// Light
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CLight :
    public TSceneGraphNode<XLight>
{
public:
    CLight(CCanvas* pCanvas, PCWSTR szName) :
        TSceneGraphNode(pCanvas, szName) {}
    GEMMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj)
    {
        if (XLight::IId == iid)
        {
            *ppObj = this;
            AddRef();
            return Result::Success;
        }

        return TSceneGraphNode<XLight>::InternalQueryInterface(iid, ppObj);
    }
};

