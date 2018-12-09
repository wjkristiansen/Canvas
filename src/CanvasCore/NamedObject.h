//================================================================================================
// NamedObject
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CObjectName :
    public XObjectName,
    public CInnerGenericBase
{
public:
    std::string m_Name;

    CObjectName(XGeneric *pOuterGeneric, PCSTR szName);

    virtual ~CObjectName();

    CANVASMETHOD_(PCSTR, GetName)() final
    {
        return m_Name.c_str();
    }

    CANVASMETHOD(SetName)(PCSTR szName) final;

    CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj) final
    {
        if (InterfaceId::XObjectName == iid)
        {
            *ppObj = this;
            AddRef();
            return Result::Success;
        }

        return __super::InternalQueryInterface(iid, ppObj);
    }
};

