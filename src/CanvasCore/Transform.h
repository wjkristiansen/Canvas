//================================================================================================
// Transform
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
template<class _Base>
class TTransform : public _Base
{
    CTranslationDataSource *m_pTranslationDataSource = nullptr;
    CRotationDataSource *m_pRotationDataSource = nullptr;
    Math::FloatVector4 m_Translation;
    Math::FloatVector4 m_Rotation;
    RotationType m_RotationType = RotationType::EulerXYZ;
    inline static const Math::FloatVector4 m_WorldUp = Math::FloatVector4( 0.f, 1.f, 0.f, 0.f );

public:
    TTransform() {}

    GEMMETHOD_(RotationType, GetRotationType)() const final
    {
        return m_RotationType;
    }

    GEMMETHOD_(const Math::FloatVector4 &, GetRotation)() const final
    {
        return m_Rotation;
    }

    GEMMETHOD_(const Math::FloatVector4 &, GetTranslation)() const final
    {
        return m_Translation;
    }

    GEMMETHOD_(void, SetRotation)(RotationType Type, _In_ const Math::FloatVector4 &Rotation) final
    {
        m_RotationType = Type;
        m_Rotation = Rotation;
    }

    GEMMETHOD_(void, SetTranslation)(_In_ const Math::FloatVector4 &Translation) final
    {
        m_Translation = Translation;
    }

    GEMMETHOD(LookAt)(_In_ const Math::FloatVector4 &Location) final
    {
        return Result::NotImplemented;
    }

    GEMMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_result_nullonfailure_ void **ppObj)
    {
        *ppObj = nullptr;

        if (XTransform::IId == iid)
        {
            *ppObj = reinterpret_cast<XTransform *>(this);
            this->AddRef();
            return Result::Success;
        }

        return Result::NoInterface;
    }
};
