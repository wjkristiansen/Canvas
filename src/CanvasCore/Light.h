//================================================================================================
// Light
//================================================================================================

#pragma once

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
    // Base light data
    LightType m_Type;
    Math::FloatVector4 m_Color;
    
    // Extended rendering data
    UINT m_Flags;
    
    // Intensity
    float m_Intensity;
    
    // Attenuation parameters (for point/spot lights)
    float m_AttenuationConstant;
    float m_AttenuationLinear;
    float m_AttenuationQuadratic;
    float m_Range;
    
    // Spot light parameters
    float m_SpotInnerAngle;  // In radians
    float m_SpotOuterAngle;  // In radians

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XLight)
    END_GEM_INTERFACE_MAP()

    CLight(CCanvas *pCanvas) :
        TSceneGraphElement(pCanvas) {}

    // XSceneGraphElement methods
    GEMMETHOD(Update)(float) final { return Gem::Result::Success; }
};

}

#ifdef _MSC_VER
#pragma warning(pop)
#endif
