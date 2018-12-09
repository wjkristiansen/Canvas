//================================================================================================
// NamedObject
//================================================================================================

#pragma once

class CNamedObject :
    public XNamedObject,
    public CGenericBase
{
public:
    std::string m_Name;

    CNamedObject(PCSTR szName) :
        m_Name(szName) {}

    CANVASMETHOD_(PCSTR) GetName() final
    {
        return m_Name.c_str();
    }

    CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj) final
    {
        if (InterfaceId::XNamedObject == iid)
        {
            *ppObj = this;
            AddRef();
            return Result::Success;
        }

        return __super::InternalQueryInterface(iid, ppObj);
    }
};

class CNamedObjectList :
    public XNamedObjectList,
    public CGenericBase
{
public:
    std::map<std::string, CCanvasPtr<XNamedObject>> m_NamedObjectMap;

    CANVASMETHOD(Select)(PCSTR szName, InterfaceId iid, void **ppObj) final;

    CANVASMETHOD(Add)(CNamedObject *pNamedObject)
    {
        m_NamedObjectMap.emplace(pNamedObject->m_Name, pNamedObject);
    }

    CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj) final
    {
        if (InterfaceId::XNamedObjectList == iid)
        {
            *ppObj = this;
            AddRef();
            return Result::Success;
        }

        return __super::InternalQueryInterface(iid, ppObj);
    }
};