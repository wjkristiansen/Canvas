//================================================================================================
// Transform
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CTransform :
    public XTransform,
    public CInnerGenericBase
{
    FloatVector3 m_Translation;
    FloatVector4 m_Rotation;
    RotationType m_RotationType;

public:
    CTransform(XGeneric *pOuterObj) :
        CInnerGenericBase(pOuterObj) {}
    GEMMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj) final
    {
        if (XTransform::IId == iid)
        {
            *ppObj = this;
            AddRef(); // This will actually AddRef the outer generic
            return Result::Success;
        }

        return __super::InternalQueryInterface(iid, ppObj);
    }

    GEMMETHOD_(RotationType, GetRotationType)() const final
    {
        return m_RotationType;
    }

    GEMMETHOD_(const FloatVector4 &, GetRotation)() const final
    {
        return m_Rotation;
    }

    GEMMETHOD_(const FloatVector3 &, GetTranslation)() const final
    {
        return m_Translation;
    }

    GEMMETHOD_(void, SetRotation)(RotationType Type, _In_ const FloatVector4 &Rotation) final
    {
        m_RotationType = Type;
        m_Rotation = Rotation;
    }

    GEMMETHOD_(void, SetTranslation)(_In_ const FloatVector3 &Translation) final
    {
        m_Translation = Translation;
    }
};
