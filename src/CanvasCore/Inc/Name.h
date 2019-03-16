//================================================================================================
// Name
//================================================================================================

#pragma once

class CCanvas;

//------------------------------------------------------------------------------------------------
class CName :
    public XName,
    public CInnerGenericBase
{
public:
    std::wstring m_Name;
    CCanvas *m_pCanvas; // Weak pointer

    CName(XGeneric *pOuterGeneric, PCWSTR szName, CCanvas *pCanvas);

    virtual ~CName();

    GEMMETHOD_(PCWSTR, GetName)() final
    {
        return m_Name.c_str();
    }

    GEMMETHOD(SetName)(PCWSTR szName) final;

    GEMMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj) final
    {
        if (XName::IId == iid)
        {
            *ppObj = this;
            AddRef();
            return Result::Success;
        }

        return __super::InternalQueryInterface(iid, ppObj);
    }
};

