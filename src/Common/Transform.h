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
    FloatMatrix3x3 m_RotScaleMatrix;
    RotationType m_RotationType;
    inline static const FloatVector3 m_WorldUp = FloatVector3(0.f, 1.f, 0.f);

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

    GEMMETHOD(LookAt)(_In_ const FloatVector3 &Location) final
    {
        m_RotationType = RotationType::Matrix;

        // Get the normalized vector from the lookat point
        FloatVector3 Facing = Location - m_Translation;
        float magsq = DotProduct(Facing, Facing);
        if (magsq < FLT_MIN)
        {
            return Result::InvalidArg;
        }
        float magrecip = 1.f / sqrtf(magsq);
        Facing = Facing * magrecip;
        FloatVector3 LocalSide = CrossProduct(Facing, m_WorldUp);
        magsq = DotProduct(LocalSide, LocalSide);
        if (magsq < FLT_MIN)
        {
            return Result::InvalidArg;
        }
        magrecip = 1.f / sqrtf(magsq);
        LocalSide = LocalSide * magrecip;

        // Facing and side are both normalized and orthogonal, so the 
        // Local up vector is just the cross-product of the Facing and LocalSide vector
        FloatVector3 LocalUp = CrossProduct(Facing, LocalSide);

        m_RotScaleMatrix.M[0] = LocalSide;
        m_RotScaleMatrix.M[1] = LocalUp;
        m_RotScaleMatrix.M[2] = Facing;

        return Result::Success;
    }
};
