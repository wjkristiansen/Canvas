//================================================================================================
// Transform
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CTransform :
    public XTransform,
    public CInnerGenericBase
{
    FloatVector4 m_Translation;
    FloatVector4 m_Rotation;
    FloatMatrix4x4 m_RotScaleMatrix;
    RotationType m_RotationType;
    inline static const FloatVector4 m_WorldUp = FloatVector4( 0.f, 1.f, 0.f, 0.f );

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

    GEMMETHOD_(const FloatVector4 &, GetTranslation)() const final
    {
        return m_Translation;
    }

    GEMMETHOD_(void, SetRotation)(RotationType Type, _In_ const FloatVector4 &Rotation) final
    {
        m_RotationType = Type;
        m_Rotation = Rotation;
    }

    GEMMETHOD_(void, SetTranslation)(_In_ const FloatVector4 &Translation) final
    {
        m_Translation = Translation;
    }

    GEMMETHOD(LookAt)(_In_ const FloatVector4 &Location) final
    {
        return Result::NotImplemented;
    }
};
