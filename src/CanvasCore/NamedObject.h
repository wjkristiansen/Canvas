//================================================================================================
// NamedObject
//================================================================================================

#pragma once

class CCanvas;

//------------------------------------------------------------------------------------------------
class CObjectName :
    public XObjectName,
    public CInnerGenericBase
{
public:
    std::wstring m_Name;
    CCanvas *m_pCanvas; // Weak pointer

    CObjectName(XGeneric *pOuterGeneric, PCWSTR szName, CCanvas *pCanvas);

    virtual ~CObjectName();

    GOMMETHOD_(PCWSTR, GetName)() final
    {
        return m_Name.c_str();
    }

    GOMMETHOD(SetName)(PCWSTR szName) final;

    GOMMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj) final
    {
        if (XObjectName::IId == iid)
        {
            *ppObj = this;
            AddRef();
            return Result::Success;
        }

        return __super::InternalQueryInterface(iid, ppObj);
    }
};

