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
    CCamera(CCanvas *pCanvas, const ModelData::CAMERA_DATA *pCameraData, PCSTR szName) :
        TSceneGraphNode<XCamera>(pCanvas, szName),
        m_NearClip(pCameraData->NearClip),
        m_FarClip(pCameraData->FarClip),
        m_FovAngle(pCameraData->FovAngle)
    {
    }

    GEMMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_result_nullonfailure_ void **ppObj)
    {
        if (XCamera::IId == iid)
        {
            *ppObj = this;
            AddRef(); // This will actually AddRef the outer generic
            return Result::Success;
        }

        return TSceneGraphNode<XCamera>::InternalQueryInterface(iid, ppObj);
    }
};
