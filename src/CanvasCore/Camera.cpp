//================================================================================================
// Camera
//================================================================================================

#include "pch.h"
#include "Camera.h"
#include <cmath>

namespace Canvas
{

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(Math::FloatMatrix4x4) CCamera::GetViewMatrix()
{
    if (m_ViewMatrixDirty)
    {
        // Get the camera's world transform from the attached scene graph node
        auto pNode = GetAttachedNode();
        if (pNode)
        {
            // Get the camera's world matrix
            auto worldMatrix = pNode->GetGlobalMatrix();
            
            // CRITICAL: Canvas uses ROW VECTORS (v' = v * M), so world * view = identity
            // Translation is in BOTTOM ROW [3][col], NOT right column [row][3]
            // Inverse formula for row vectors: T^-1 = -T * R^T (different from column vectors!)
            
            // Extract translation from bottom row
            Math::FloatVector4 translation(worldMatrix[3][0], worldMatrix[3][1], worldMatrix[3][2], 0.0f);
            
            // For row-vector rigid transform, the inverse is:
            // R^-1 = R^T (transpose of rotation)
            // T^-1 = -T * R^T (different from column vectors!)
            
            // Build the view matrix with transposed rotation
            m_ViewMatrix[0][0] = worldMatrix[0][0];
            m_ViewMatrix[0][1] = worldMatrix[1][0];
            m_ViewMatrix[0][2] = worldMatrix[2][0];
            m_ViewMatrix[0][3] = 0.0f;
            
            m_ViewMatrix[1][0] = worldMatrix[0][1];
            m_ViewMatrix[1][1] = worldMatrix[1][1];
            m_ViewMatrix[1][2] = worldMatrix[2][1];
            m_ViewMatrix[1][3] = 0.0f;
            
            m_ViewMatrix[2][0] = worldMatrix[0][2];
            m_ViewMatrix[2][1] = worldMatrix[1][2];
            m_ViewMatrix[2][2] = worldMatrix[2][2];
            m_ViewMatrix[2][3] = 0.0f;
            
            // Compute -T * R^T for the translation part (goes in bottom row for row vectors)
            float tx = -(translation.X * worldMatrix[0][0] + translation.Y * worldMatrix[0][1] + translation.Z * worldMatrix[0][2]);
            float ty = -(translation.X * worldMatrix[1][0] + translation.Y * worldMatrix[1][1] + translation.Z * worldMatrix[1][2]);
            float tz = -(translation.X * worldMatrix[2][0] + translation.Y * worldMatrix[2][1] + translation.Z * worldMatrix[2][2]);
            
            m_ViewMatrix[3][0] = tx;
            m_ViewMatrix[3][1] = ty;
            m_ViewMatrix[3][2] = tz;
            m_ViewMatrix[3][3] = 1.0f;
        }
        else
        {
            // No node attached, use identity
            m_ViewMatrix = Math::FloatMatrix4x4::Identity();
        }
        
        m_ViewMatrixDirty = false;
        m_ViewProjectionMatrixDirty = true; // View-projection needs recalculation
    }
    return m_ViewMatrix;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(Math::FloatMatrix4x4) CCamera::GetProjectionMatrix()
{
    if (m_ProjectionMatrixDirty)
    {
        // Perspective projection matrix for row vectors
        // Row-vector form: v' = v * P where P is the projection matrix
        //
        // For row vectors, we use the transpose:
        // [ f/aspect   0        0             0    ]
        // [   0        f        0             0    ]
        // [   0        0    (f+n)/(n-f)      -1    ]
        // [   0        0    2fn/(n-f)         0    ]
        
        float f = 1.0f / std::tan(m_FovAngle * 0.5f); // cotangent of half fov angle
        float n = m_NearClip;
        float farClip = m_FarClip;
        float rangeInv = 1.0f / (n - farClip);
        
        m_ProjectionMatrix[0][0] = f / m_AspectRatio;
        m_ProjectionMatrix[0][1] = 0.0f;
        m_ProjectionMatrix[0][2] = 0.0f;
        m_ProjectionMatrix[0][3] = 0.0f;
        
        m_ProjectionMatrix[1][0] = 0.0f;
        m_ProjectionMatrix[1][1] = f;
        m_ProjectionMatrix[1][2] = 0.0f;
        m_ProjectionMatrix[1][3] = 0.0f;
        
        m_ProjectionMatrix[2][0] = 0.0f;
        m_ProjectionMatrix[2][1] = 0.0f;
        m_ProjectionMatrix[2][2] = (farClip + n) * rangeInv;
        m_ProjectionMatrix[2][3] = -1.0f;
        
        m_ProjectionMatrix[3][0] = 0.0f;
        m_ProjectionMatrix[3][1] = 0.0f;
        m_ProjectionMatrix[3][2] = 2.0f * farClip * n * rangeInv;
        m_ProjectionMatrix[3][3] = 0.0f;
        
        m_ProjectionMatrixDirty = false;
        m_ViewProjectionMatrixDirty = true; // View-projection needs recalculation
    }
    return m_ProjectionMatrix;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(Math::FloatMatrix4x4) CCamera::GetViewProjectionMatrix()
{
    if (m_ViewProjectionMatrixDirty)
    {
        // Ensure view and projection matrices are up to date
        // Note: These calls may mark m_ViewProjectionMatrixDirty = true, but we'll
        // immediately clear it after computing the combined matrix
        GetViewMatrix();
        GetProjectionMatrix();
        
        // Compute view-projection matrix: ViewProjection = View * Projection
        // For row vectors: v' = v * View * Projection
        m_ViewProjectionMatrix = m_ViewMatrix * m_ProjectionMatrix;
        
        // Clear all dirty bits since all matrices are now computed
        m_ViewMatrixDirty = false;
        m_ProjectionMatrixDirty = false;
        m_ViewProjectionMatrixDirty = false;
    }
    return m_ViewProjectionMatrix;
}

}
