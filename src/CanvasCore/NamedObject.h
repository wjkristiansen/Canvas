//================================================================================================
// NamedObject
//================================================================================================

#pragma once

class CObjectName :
    public XObjectName,
    public CInnerGenericBase
{
public:
    std::string m_Name;

    CObjectName(XGeneric *pOuterGeneric);

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

class CNamedObjectList
{
public:
    std::map<std::string, CObjectName *> m_ObjectNames;

    CANVASMETHOD(Select)(PCSTR szName, InterfaceId iid, void **ppObj) final;

    CANVASMETHOD(Set)(CObjectName *pObjectName)
    {
        m_ObjectNames.emplace(pObjectName->m_Name, pObjectName);
        return Result::Success;
    }

    CANVASMETHOD(Erase)(CObjectName *pObjectName)
    {
        m_ObjectNames.erase(pObjectName->m_Name);
        return Result::Success;
    }
};