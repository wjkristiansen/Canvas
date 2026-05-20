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
            // Canvas world / scene-graph convention (right-handed):
            //   row 0 of the camera node's local rotation = forward (+X)
            //   row 1 = left    (+Y)
            //   row 2 = up      (+Z)
            //
            // Canvas view-space convention (left-handed, standard D3D):
            //   +X = right, +Y = up, +Z = forward
            //
            // The view matrix takes world coordinates and produces view-space
            // coordinates, so its 3x3 rotation is the transpose of a
            // (right, up, forward)-rowed basis expressed in world coords:
            //   right_world   = -left_world      (left and right are opposites)
            //   up_world      =  up_world
            //   forward_world =  forward_world
            //
            // For row vectors v' = v * View, the columns of View's 3x3 are
            // (right_world, up_world, forward_world). The translation row is
            // -cameraPos projected onto each view-space axis.
            auto worldMatrix = pNode->GetGlobalMatrix();

            Math::FloatVector4 cameraPos(worldMatrix[3][0], worldMatrix[3][1], worldMatrix[3][2], 0.0f);

            // Read the camera node's world-space basis rows.
            Math::FloatVector4 basisFwd (worldMatrix[0][0], worldMatrix[0][1], worldMatrix[0][2], 0.0f);
            Math::FloatVector4 basisLeft(worldMatrix[1][0], worldMatrix[1][1], worldMatrix[1][2], 0.0f);
            Math::FloatVector4 basisUp  (worldMatrix[2][0], worldMatrix[2][1], worldMatrix[2][2], 0.0f);

            // Camera view should be rigid even if imported hierarchy contains
            // scale. Orthonormalize the basis rows to remove scale/shear
            // before constructing the view matrix.
            auto SafeNormalize = [](Math::FloatVector4 v, const Math::FloatVector4& fallback)
            {
                const float lenSq = Math::DotProduct(v, v);
                if (lenSq <= 1e-12f)
                    return fallback;
                return v * (1.0f / std::sqrt(lenSq));
            };

            basisFwd  = SafeNormalize(basisFwd,  Math::FloatVector4(1.0f, 0.0f, 0.0f, 0.0f));
            basisLeft = basisLeft - basisFwd * Math::DotProduct(basisLeft, basisFwd);
            basisLeft = SafeNormalize(basisLeft, Math::FloatVector4(0.0f, 1.0f, 0.0f, 0.0f));
            basisUp   = Math::CrossProduct(basisFwd, basisLeft);
            basisUp   = SafeNormalize(basisUp,   Math::FloatVector4(0.0f, 0.0f, 1.0f, 0.0f));

            // Right is the world-space negation of the camera's local left.
            Math::FloatVector4 basisRight(-basisLeft[0], -basisLeft[1], -basisLeft[2], 0.0f);

            // 3x3 rotation: columns are (right_world, up_world, forward_world).
            m_ViewMatrix[0][0] = basisRight[0];
            m_ViewMatrix[0][1] = basisUp[0];
            m_ViewMatrix[0][2] = basisFwd[0];
            m_ViewMatrix[0][3] = 0.0f;

            m_ViewMatrix[1][0] = basisRight[1];
            m_ViewMatrix[1][1] = basisUp[1];
            m_ViewMatrix[1][2] = basisFwd[1];
            m_ViewMatrix[1][3] = 0.0f;

            m_ViewMatrix[2][0] = basisRight[2];
            m_ViewMatrix[2][1] = basisUp[2];
            m_ViewMatrix[2][2] = basisFwd[2];
            m_ViewMatrix[2][3] = 0.0f;

            // Translation row (row vectors): -cameraPos projected onto each view axis.
            m_ViewMatrix[3][0] = -(cameraPos.X * basisRight[0] + cameraPos.Y * basisRight[1] + cameraPos.Z * basisRight[2]);
            m_ViewMatrix[3][1] = -(cameraPos.X * basisUp[0]    + cameraPos.Y * basisUp[1]    + cameraPos.Z * basisUp[2]);
            m_ViewMatrix[3][2] = -(cameraPos.X * basisFwd[0]   + cameraPos.Y * basisFwd[1]   + cameraPos.Z * basisFwd[2]);
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
        // Projection consumes view space (X=right, Y=up, Z=forward, LHS).
        // The view matrix is responsible for the world(X-fwd,Y-left,Z-up)
        // -> view(X-right,Y-up,Z-forward) basis remap.
        m_ProjectionMatrix = Math::PerspectiveReverseZ(
            m_FovAngle,
            m_AspectRatio,
            m_NearClip,
            m_FarClip
        );
        
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
