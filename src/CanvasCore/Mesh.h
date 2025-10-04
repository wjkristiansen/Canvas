//================================================================================================
// Mesh
//================================================================================================

#pragma once

namespace Canvas
{

//------------------------------------------------------------------------------------------------
class CMeshInstance :
    public Gem::TGeneric<XMeshInstance>
{
public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XMeshInstance)
    END_GEM_INTERFACE_MAP()

    CMeshInstance() {}
};

}
