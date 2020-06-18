//================================================================================================
// Camera
//================================================================================================

#pragma once


//------------------------------------------------------------------------------------------------
class CCamera :
    public TSceneGraphNode<XCamera>
{
public:
    CCamera(CCanvas *pCanvas, PCSTR szName) :
        TSceneGraphNode<XCamera>(pCanvas, szName) {}
    GEMMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_result_nullonfailure_ void **ppObj) final
    {
        if (XCamera::IId == iid)
        {
            *ppObj = this;
            AddRef(); // This will actually AddRef the outer generic
            return Result::Success;
        }

        return TSceneGraphNode<XCamera>::InternalQueryInterface(iid, ppObj);
    }
};
