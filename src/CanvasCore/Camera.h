//================================================================================================
// Camera
//
// MATRIX CONVENTION: Canvas uses ROW VECTORS (v' = v * M)
//   - View matrix is the inverse of the camera's world transform
//   - Verification: world * view = identity (not view * world)
//   - Translation in BOTTOM ROW of all matrices
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

    // Dirty bits and cached matrices
    bool m_ViewMatrixDirty = true;
    bool m_ProjectionMatrixDirty = true;
    bool m_ViewProjectionMatrixDirty = true;
    Math::FloatMatrix4x4 m_ViewMatrix;
    Math::FloatMatrix4x4 m_ProjectionMatrix;
    Math::FloatMatrix4x4 m_ViewProjectionMatrix;

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XCamera)
    END_GEM_INTERFACE_MAP()

    // Ctor
    CCamera(CCanvas *pCanvas) :
        TSceneGraphElement(pCanvas)
    {
    }

    // XSceneGraphElement methods
    GEMMETHOD(Update)(float /*dtime*/) final 
    { 
        // Frame update logic can go here if needed in the future
        return Gem::Result::Success; 
    }

    // Internal method called by scene graph when ancestor transforms change
    void MarkViewDirty()
    {
        m_ViewMatrixDirty = true;
        m_ViewProjectionMatrixDirty = true;
    }

    // XCamera methods
    GEMMETHOD_(void, SetNearClip)(float nearClip) final 
    { 
        m_NearClip = nearClip; 
        m_ProjectionMatrixDirty = true;
    }
    
    GEMMETHOD_(void, SetFarClip)(float farClip) final 
    { 
        m_FarClip = farClip; 
        m_ProjectionMatrixDirty = true;
    }
    
    GEMMETHOD_(void, SetFovAngle)(float fovAngle) final 
    { 
        m_FovAngle = fovAngle; 
        m_ProjectionMatrixDirty = true;
    }
    
    GEMMETHOD_(void, SetAspectRatio)(float aspectRatio) final 
    { 
        m_AspectRatio = aspectRatio; 
        m_ProjectionMatrixDirty = true;
    }

    GEMMETHOD_(float, GetNearClip)() final { return m_NearClip; }
    GEMMETHOD_(float, GetFarClip)() final { return m_FarClip; }
    GEMMETHOD_(float, GetFovAngle)() final { return m_FovAngle; }
    GEMMETHOD_(float, GetAspectRatio)() final { return m_AspectRatio; }

    GEMMETHOD_(Math::FloatMatrix4x4, GetViewMatrix)() final;
    GEMMETHOD_(Math::FloatMatrix4x4, GetProjectionMatrix)() final;
    GEMMETHOD_(Math::FloatMatrix4x4, GetViewProjectionMatrix)() final;
};

}