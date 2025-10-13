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
    Math::FloatQuaternion m_Rotation;
    Math::FloatVector4 m_Scale; // W is ignored
    Math::FloatVector4 m_Translation;

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XTransform)
    END_GEM_INTERFACE_MAP()

    CTransform() {}

    GEMMETHOD_(const Math::FloatQuaternion &, GetRotation)() const final
    {
        return m_Rotation;
    }

    GEMMETHOD_(const Math::FloatVector4 &, GetTranslation)() const final
    {
        return m_Translation;
    }

    GEMMETHOD_(const Math::FloatVector4 &, GetScale)() const final
    {
        return m_Scale;
    }

    GEMMETHOD_(void, SetRotation)(_In_ const Math::FloatQuaternion &Rotation) final
    {
        m_Rotation = Rotation;
    }

    GEMMETHOD_(void, SetTranslation)(_In_ const Math::FloatVector4 &Translation) final
    {
        m_Translation = Translation;
    }

    GEMMETHOD_(void, SetScale)(_In_ const Math::FloatVector4 &Scale) final
    {
        m_Scale = Scale;
    }

    GEMMETHOD(LookAt)(_In_ const Math::FloatVector4 &localPoint, _In_ const Math::FloatVector4 &localUpVector) final
    {
        Math::FloatMatrix4x4 rotMatrix = Math::FloatMatrix4x4::Identity();
        Math::FloatVector4 basisForwardVector = localPoint - this->m_Translation;
        Math::FloatVector4 basisSideVector;
        Math::FloatVector4 basisUpVector;
        basisForwardVector = NormalizeVector(basisForwardVector);
        Canvas::Math::ComposeLookAtBasisVectors<Math::FloatVector4>(
            localUpVector,
            basisForwardVector,
            basisSideVector,
            basisUpVector);
        rotMatrix.M[0] = basisForwardVector;
        rotMatrix.M[1] = basisSideVector;
        rotMatrix.M[2] = basisUpVector;
    }
};

}

#ifdef _MSC_VER
#pragma warning(pop)
#endif
