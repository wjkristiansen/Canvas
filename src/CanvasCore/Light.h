//================================================================================================
// Light
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CLight :
    public XLight,
    public CGenericBase
{
public:
    CLight() :
        CGenericBase() {}
    GEMMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj) final
    {
        if (XLight::IId == iid)
        {
            *ppObj = this;
            AddRef(); // This will actually AddRef the outer generic
            return Result::Success;
        }

        return __super::InternalQueryInterface(iid, ppObj);
    }
};

