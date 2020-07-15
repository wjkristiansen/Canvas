//================================================================================================
// Instance12
//================================================================================================

#pragma once

class CInstance12 :
	public XGfxInstance,
	public Gem::CGenericBase
{
	QLog::CBasicLogger m_Logger;

	static CInstance12 *m_pThis;

public:
	CInstance12(QLog::CLogClient *pLogClient);

	QLog::CBasicLogger &Logger() { return m_Logger; }

    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XGfxInstance)
    END_GEM_INTERFACE_MAP()

    static CInstance12 *GetSingleton() { return m_pThis; }
	GEMMETHOD(CreateGfxDevice)(XGfxDevice **ppDevice);
};