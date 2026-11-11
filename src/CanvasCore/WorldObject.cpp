//================================================================================================
// WorldObject
//================================================================================================

#include "pch.h"

#include "WorldObject.h"
#include "World.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
CWorldObject::CWorldObject(XCanvas *pCanvas, CWorld *pWorld, CSceneNode *pAnchorNode) :
    TCanvasElement(pCanvas),
    m_pWorld(pWorld),
    m_pAnchorNode(pAnchorNode)
{
}

//------------------------------------------------------------------------------------------------
void CWorldObject::Initialize()
{
    if (m_pAnchorNode)
        m_pAnchorNode->SetAnchoredObject(this);
}

//------------------------------------------------------------------------------------------------
void CWorldObject::Uninitialize()
{
    if (m_pAnchorNode && m_pAnchorNode->GetAnchoredObject() == this)
        m_pAnchorNode->SetAnchoredObject(nullptr);
    m_pAnchorNode = nullptr;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(XWorld *) CWorldObject::GetWorld()
{
    return m_pWorld;
}

}
