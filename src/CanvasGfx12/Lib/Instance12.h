//================================================================================================
// Instance12
//================================================================================================

#pragma once

namespace Canvas
{

class CInstance12 :
	public Gem::TGeneric<XGfxInstance>
{
	static CInstance12 *m_pThis;

public:
	CInstance12();

    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XGfxInstance)
    END_GEM_INTERFACE_MAP()

    static CInstance12 *GetSingleton() { return m_pThis; }
	GEMMETHOD(CreateGfxDevice)(XGfxDevice **ppDevice);
};

}