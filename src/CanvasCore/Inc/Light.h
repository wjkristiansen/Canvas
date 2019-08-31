//================================================================================================
// Light
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CLight :
    public TSceneGraphNode<XLight>
{
public:
    CLight(CCanvas *pCanvas, PCSTR szName) :
        TSceneGraphNode(pCanvas, szName) {}
    GEMMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_result_nullonfailure_ void **ppObj)
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
