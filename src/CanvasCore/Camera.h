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
    float m_NearClip = 0;
    float m_FarClip = 0;
    float m_FovAngle = 0;

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
};

}