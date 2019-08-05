//================================================================================================
// Name
//================================================================================================

#pragma once

class CCanvas;

//------------------------------------------------------------------------------------------------
// Represents a key/value pair that automatically removes itself from
// a key/value map.
template<class _KeyType, class _ValueType, class _MapType = std::map<_KeyType, _ValueType>>
class TScopedKeyValuePair
{
    using MapType = _MapType;
    using MapIteratorType = typename MapType::iterator;
    MapType *m_pMap = nullptr;
    MapIteratorType m_Location;

public:
    TScopedKeyValuePair() = default;
    TScopedKeyValuePair(const _KeyType &Key, const _ValueType &Value, MapType *pMap)
    {
        Assign(Key, Value, pMap);
    }
    ~TScopedKeyValuePair()
    {
        Unassign();
    }

    void Unassign()
    {
        if (m_pMap)
        {
            m_pMap->erase(m_Location);
            m_pMap = nullptr;
        }
    }

    void Assign(const _KeyType &Key, const _ValueType &Value, MapType *pMap)
    {
        Unassign();

        auto result = pMap->insert(std::make_pair(Key, Value)); // throw(std::bad_alloc)
        if (!result.second)
        {
            throw std::exception(); // insert failed (e.g. name collision)
        }
        m_pMap = pMap;
        m_Location = result.first;
    }

    bool IsAssigned() const
    {
        return m_pMap != nullptr;
    }

    const _KeyType &GetKey() const
    {
        if (!m_pMap)
        {
            throw(std::exception()); // Unassigned
        }

        return m_Location->first;
    }

    const _ValueType &GetValue() const
    {
        if (!m_pMap)
        {
            throw(std::exception()); // Unassigned
        }

        return m_Location->second;
    }
};

//------------------------------------------------------------------------------------------------
class CNameTag :
    public XNameTag,
    public CInnerGenericBase
{
    CCanvas *m_pCanvas = nullptr; // Weak pointer
    TScopedKeyValuePair<std::wstring, XGeneric *> m_Tag;

public:
    CNameTag(XGeneric *pOuterGeneric, CCanvas *pCanvas, PCWSTR szName = nullptr);

    GEMMETHOD_(PCWSTR, GetName)() final;
    GEMMETHOD(SetName)(PCWSTR szName) final;
    GEMMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj) final;
};