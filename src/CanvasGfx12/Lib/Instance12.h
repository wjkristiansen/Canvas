//================================================================================================
// Instance12
//================================================================================================

#pragma once

namespace Canvas
{

class CInstance12 :
	public XGfxInstance,
	public Gem::CGenericBase
{
	std::shared_ptr<QLog::Logger> m_Logger;

	static CInstance12 *m_pThis;

public:
	CInstance12(std::shared_ptr<QLog::Logger> pLogger);

	std::shared_ptr<QLog::Logger> Logger() { return m_Logger; }

    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XGfxInstance)
    END_GEM_INTERFACE_MAP()

    static CInstance12 *GetSingleton() { return m_pThis; }
	GEMMETHOD(CreateGfxDevice)(XGfxDevice **ppDevice);
};

}