//================================================================================================
// Name
//================================================================================================

#pragma once

class CCanvas;

//------------------------------------------------------------------------------------------------
class CNamedObjectTable
{
    using MapType = std::map<std::wstring, XGeneric*>;

    MapType m_NamedObjectMap;

public:
    using IteratorType = MapType::iterator;

    CNamedObjectTable() = default;

    // Returns the iterator of first object.
    // Returns End() if there are no objects
    IteratorType Begin()
    {
        return m_NamedObjectMap.begin();
    }

    // Returns the logical iterator representing the end of the collection.
    IteratorType End()
    {
        return m_NamedObjectMap.end();
    }

    // Inserts a new element into the collection.
    // Returns the iterator of the inserted element.
    // Returns End() if an object of that name is already present.
    IteratorType Insert(const std::wstring& Name, XGeneric* pGeneric)
    {
        auto result = m_NamedObjectMap.insert(std::make_pair(Name, pGeneric));
        if (result.second)
        {
            return result.first;
        }

        return End();
    }

    // Removes the element at the given location.
    // Returns the iterator of the next element or End() if this is the last element.
    IteratorType RemoveAt(IteratorType it)
    {
        return m_NamedObjectMap.erase(it);
    }

    // Removes the element with the given name.
    // Returns 'true' if a matching element was found and removed.
    bool Remove(const std::wstring& Name)
    {
        return m_NamedObjectMap.erase(Name) == 1;
    }

    // Returns the iterator of the element with the given name.
    // Returns End() if not found. 
    IteratorType Find(const std::wstring& Name)
    {
        return m_NamedObjectMap.find(Name);
    }

    // Returns the name at the given iterator location.
    const std::wstring& GetName(const IteratorType it) const
    {
        return it->first;
    }

    // Returns the XGeneric pointer at the given iterator location
    XGeneric* GetGenericPtr(const IteratorType it) const
    {
        return it->second;
    }
};

//------------------------------------------------------------------------------------------------
class CNamedObjectElement
{
    CNamedObjectTable *m_pTable = nullptr;
    CNamedObjectTable::IteratorType m_Location;

public:
    CNamedObjectElement(CNamedObjectTable* pTable) :
        m_pTable(pTable) {}
    CNamedObjectElement(CNamedObjectTable* pTable, const std::wstring &Name, XGeneric *pGeneric) :
        m_pTable(pTable),
        m_Location(pTable->Insert(Name, pGeneric))
    {
    }

    const std::wstring& GetName() const
    {
        return m_pTable->GetName(m_Location);
    }

    XGeneric* GetGenericPtr() const
    {
        return m_pTable->GetGenericPtr(m_Location);
    }
};

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

