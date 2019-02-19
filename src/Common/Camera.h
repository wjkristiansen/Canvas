//================================================================================================
// Camera
//================================================================================================

#pragma once


//------------------------------------------------------------------------------------------------
class CCamera :
    public XCamera,
    public CGenericBase
{
public:
    CCamera() :
        CGenericBase() {}
    GEMMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj) final
    {
        if (XCamera::IId == iid)
        {
            *ppObj = this;
            AddRef(); // This will actually AddRef the outer generic
            return Result::Success;
        }

        return __super::InternalQueryInterface(iid, ppObj);
    }
};
