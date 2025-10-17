//================================================================================================
// Light
//================================================================================================

#pragma once

#include "SceneGraph.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)
#endif

namespace Canvas
{

//------------------------------------------------------------------------------------------------
class CLight :
    public TSceneGraphElement<XLight>
{
    // Immutable attributes (set at construction)
    const LightType m_Type;
    
    // Mutable attributes
    Math::FloatVector4 m_Color;
    float m_Intensity;
    UINT m_Flags;
    
    // Attenuation parameters (for Point and Spot lights)
    float m_AttenuationConstant;
    float m_AttenuationLinear;
    float m_AttenuationQuadratic;
    float m_Range;
    
    // Spot light parameters (only valid for Spot lights)
    float m_SpotInnerAngle;  // In radians
    float m_SpotOuterAngle;  // In radians

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XLight)
    END_GEM_INTERFACE_MAP()

    CLight(CCanvas *pCanvas, LightType type = LightType::Point) :
        TSceneGraphElement(pCanvas),
        m_Type(type),
        m_Color(1.0f, 1.0f, 1.0f, 1.0f),
        m_Intensity(1.0f),
        m_Flags(LightFlags::Enabled),
        m_AttenuationConstant(1.0f),
        m_AttenuationLinear(0.0f),
        m_AttenuationQuadratic(0.0f),
        m_Range(100.0f),
        m_SpotInnerAngle(0.785398f), // 45 degrees
        m_SpotOuterAngle(1.047198f)  // 60 degrees
    {}
    
    // Internal factory method for creating with specific type
    void InitializeType(LightType type) { const_cast<LightType&>(m_Type) = type; }

    // XSceneGraphElement methods
    GEMMETHOD(Update)(float) final { return Gem::Result::Success; }

    // XLight methods - Immutable attributes
    GEMMETHOD_(LightType, GetType)() const final { return m_Type; }

    // XLight methods - Mutable attributes
    GEMMETHOD_(void, SetColor)(const Math::FloatVector4& color) final { m_Color = color; }
    GEMMETHOD_(Math::FloatVector4, GetColor)() const final { return m_Color; }

    GEMMETHOD_(void, SetIntensity)(float intensity) final { m_Intensity = intensity; }
    GEMMETHOD_(float, GetIntensity)() const final { return m_Intensity; }

    GEMMETHOD_(void, SetFlags)(UINT flags) final { m_Flags = flags; }
    GEMMETHOD_(UINT, GetFlags)() const final { return m_Flags; }

    // Attenuation
    GEMMETHOD_(void, SetRange)(float range) final { m_Range = range; }
    GEMMETHOD_(float, GetRange)() const final { return m_Range; }

    GEMMETHOD_(void, SetAttenuation)(float constant, float linear, float quadratic) final
    {
        m_AttenuationConstant = constant;
        m_AttenuationLinear = linear;
        m_AttenuationQuadratic = quadratic;
    }

    GEMMETHOD_(void, GetAttenuation)(float* pConstant, float* pLinear, float* pQuadratic) const final
    {
        if (pConstant) *pConstant = m_AttenuationConstant;
        if (pLinear) *pLinear = m_AttenuationLinear;
        if (pQuadratic) *pQuadratic = m_AttenuationQuadratic;
    }

    // Spot light parameters
    GEMMETHOD_(void, SetSpotAngles)(float innerAngle, float outerAngle) final
    {
        m_SpotInnerAngle = innerAngle;
        m_SpotOuterAngle = outerAngle;
    }

    GEMMETHOD_(void, GetSpotAngles)(float* pInnerAngle, float* pOuterAngle) const final
    {
        if (pInnerAngle) *pInnerAngle = m_SpotInnerAngle;
        if (pOuterAngle) *pOuterAngle = m_SpotOuterAngle;
    }
};

}

#ifdef _MSC_VER
#pragma warning(pop)
#endif
