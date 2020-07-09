//================================================================================================
// Mesh
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CMeshInstance :
    public XMeshInstance,
    public CGenericBase
{
public:
    BEGIN_GEM_INTERFACE_MAP(CGenericBase)
        GEM_INTERFACE_ENTRY(XMeshInstance)
    END_GEM_INTERFACE_MAP()

    CMeshInstance() :
        CGenericBase() {}
};