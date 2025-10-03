//================================================================================================
// Mesh
//================================================================================================

#pragma once

namespace Canvas
{

//------------------------------------------------------------------------------------------------
class CMeshInstance :
    public XMeshInstance,
    public Gem::CGenericBase
{
public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XMeshInstance)
    END_GEM_INTERFACE_MAP()

    CMeshInstance() :
        CGenericBase() {}
};

}
