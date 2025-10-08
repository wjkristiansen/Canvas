//================================================================================================
// Transform
//================================================================================================

#pragma once

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)
#endif

namespace Canvas
{

//------------------------------------------------------------------------------------------------
class CTransform :
    public Gem::TGeneric<XTransform>
{
    Math::FloatVector4 m_Translation;
    Math::FloatVector4 m_Rotation;
    RotationType m_RotationType = RotationType::EulerXYZ;

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XTransform)
    END_GEM_INTERFACE_MAP()

    CTransform() {}

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

    GEMMETHOD(LookAt)(_In_ const Math::FloatVector4 &/*Location*/, _In_ const Math::FloatVector4 &/*WorldUp*/) final
    {
        return Gem::Result::NotImplemented;
    }
};

}

#ifdef _MSC_VER
#pragma warning(pop)
#endif
