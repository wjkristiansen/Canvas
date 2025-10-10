//================================================================================================
// Camera
//================================================================================================

#pragma once

#include "SceneGraph.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
class CCamera :
    public TSceneGraphElement<XCamera>
{
    float m_NearClip = 0.1f;
    float m_FarClip = 1000.f;
    float m_FovAngle = float(Math::Pi / 4);
    float m_AspectRatio = 16.f / 9.f;

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XCamera)
    END_GEM_INTERFACE_MAP()

    // Ctor
    CCamera(CCanvas *pCanvas) :
        TSceneGraphElement(pCanvas)
    {
    }

    // XCamera methods
    GEMMETHOD_(void, SetNearClip)(float nearClip) final { m_NearClip = nearClip; }
    GEMMETHOD_(void, SetFarClip)(float farClip) final { m_FarClip = farClip; }
    GEMMETHOD_(void, SetFovAngle)(float fovAngle) final { m_FovAngle = fovAngle; }
    GEMMETHOD_(void, SetAspectRatio)(float aspectRatio) final { m_AspectRatio = aspectRatio; }

    GEMMETHOD_(float, GetNearClip)() final { return m_NearClip; }
    GEMMETHOD_(float, GetFarClip)() final { return m_FarClip; }
    GEMMETHOD_(float, GetFovAngle)() final { return m_FovAngle; }
    GEMMETHOD_(float, GetAspectRatio)() final { return m_AspectRatio; }

    GEMMETHOD_(Math::FloatMatrix4x4, GetViewMatrix)() final { return Math::FloatMatrix4x4::Identity(); }
    GEMMETHOD_(Math::FloatMatrix4x4, GetProjectionMatrix)() final { return Math::FloatMatrix4x4::Identity(); }
};

}