//================================================================================================
// Camera
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CCamera :
    public TSceneGraphNode<XCamera>
{
    float m_NearClip;
    float m_FarClip;
    float m_FovAngle;

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XCamera)
        GEM_INTERFACE_ENTRY(XTransform)
        GEM_INTERFACE_ENTRY(XSceneGraphNode)
        GEM_CONTAINED_INTERFACE_ENTRY(XNameTag, m_NameTag)
    END_GEM_INTERFACE_MAP()

    CCamera(CCanvas *pCanvas, const ModelData::CAMERA_DATA *pCameraData, PCSTR szName) :
        TSceneGraphNode<XCamera>(pCanvas, szName),
        m_NearClip(pCameraData->NearClip),
        m_FarClip(pCameraData->FarClip),
        m_FovAngle(pCameraData->FovAngle)
    {
    }
};
