//================================================================================================
// Camera
//================================================================================================

#pragma once


//------------------------------------------------------------------------------------------------
class CCamera :
    public CSceneGraphNode<XCamera>
{
public:
    CCamera(CCanvas *pCanvas) :
        CSceneGraphNode<XCamera>(pCanvas) {}
    GEMMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj) final
    {
        if (XCamera::IId == iid)
        {
            *ppObj = this;
            AddRef(); // This will actually AddRef the outer generic
            return Result::Success;
        }

        return CSceneGraphNode<XCamera>::InternalQueryInterface(iid, ppObj);
    }
};
