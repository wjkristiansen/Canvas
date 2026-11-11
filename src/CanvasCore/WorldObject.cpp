//================================================================================================
// WorldObject
//================================================================================================

#include "pch.h"

#include "WorldObject.h"
#include "World.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
CWorldObject::CWorldObject(XCanvas *pCanvas) :
    TCanvasElement(pCanvas)
{
}

//------------------------------------------------------------------------------------------------
void CWorldObject::Uninitialize()
{
    if (m_pRootNode && m_pRootNode->GetRootedObject() == this)
        m_pRootNode->SetRootedObject(nullptr);
    m_pRootNode = nullptr;
    m_pDevice = nullptr;
}

//------------------------------------------------------------------------------------------------
void CWorldObject::SetInstance(CSceneNode *pRootNode, XGfxDevice *pDevice)
{
    m_pRootNode = pRootNode;
    m_pRootNode->SetRootedObject(this);
    m_pDevice = pDevice;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(XWorld *) CWorldObject::GetWorld()
{
    return m_pWorld;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CWorldObject::SetRootNode(XSceneNode *pNode)
{
    CSceneNode *pNew = static_cast<CSceneNode *>(pNode);
    if (pNew == m_pRootNode.Get())
        return Gem::Result::Success;

    if (pNew)
    {
        const char *problem = nullptr;
        if (pNew->GetCanvas() != m_pCanvas)
            problem = "was created by another canvas";
        else if (pNew->GetParent())
            problem = "has a parent";
        else if (pNew->GetRootedObject())
            problem = "is another world object's root node";
        else if (pNew->IsWorldRoot())
            problem = "is a world's root node";

        if (problem)
        {
            Canvas::LogError(m_pCanvas->GetLogger(), "XWorldObject::SetRootNode: the node %s", problem);
            return Gem::Result::InvalidArg;
        }
    }

    // The new root node takes the old one's place in the scene graph
    Gem::TGemPtr<CSceneNode> pOld = m_pRootNode;
    if (pOld && pOld->GetParent())
    {
        XSceneNode *pParent = pOld->GetParent();
        if (pNew)
        {
            Gem::Result result = pParent->InsertChildBefore(pNew, pOld);
            if (Gem::Failed(result))
                return result;
        }
        Gem::Result result = pParent->RemoveChild(pOld);
        if (Gem::Failed(result))
        {
            if (pNew)
                pParent->RemoveChild(pNew);
            return result;
        }
    }

    if (pOld)
    {
        pOld->SetRootedObject(nullptr);
        Canvas::LogInfo(m_pCanvas->GetLogger(), "XWorldObject::SetRootNode: world object '%s' %s its root node",
            GetName() ? GetName() : "", pNew ? "replaced" : "cleared");
    }

    m_pRootNode = pNew;
    if (pNew)
        pNew->SetRootedObject(this);
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
Gem::Result CWorldObject::Update(float dtime)
{
    return m_pRootNode ? m_pRootNode->UpdateObjectNodes(dtime) : Gem::Result::Success;
}

}
