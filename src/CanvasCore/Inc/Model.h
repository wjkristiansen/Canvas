//================================================================================================
// Light
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CMeshInstance :
    public XMeshInstance,
    public CGenericBase
{
public:
    CMeshInstance() :
        CGenericBase() {}
    GEMMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_result_nullonfailure_ void **ppObj) final
    {
        if (XMeshInstance::IId == iid)
        {
            *ppObj = this;
            AddRef(); // This will actually AddRef the outer generic
            return Result::Success;
        }

        return CGenericBase::InternalQueryInterface(iid, ppObj);
    }
};