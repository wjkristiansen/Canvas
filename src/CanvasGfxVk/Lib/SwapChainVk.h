//================================================================================================
// SwapChainVk
//================================================================================================

#pragma once

#include "SurfaceVk.h"

//------------------------------------------------------------------------------------------------
class CSwapChainVk :
	public Canvas::XGfxSwapChain,
	public Gem::CGenericBase
{
	std::mutex m_mutex;
	CGraphicsContextVk *m_pContext; // weak ptr
	UniqueVkSwapchainKHR m_VkSwapChain;
	VkFence m_VkFence;
	std::vector<VkImage> m_VkImages;
	Gem::TGemPtr<CSurfaceVk> m_pSurface;
	uint32_t m_ImageIndex = 0;

public:
	BEGIN_GEM_INTERFACE_MAP()
		GEM_INTERFACE_ENTRY(Canvas::XGfxSwapChain)
	END_GEM_INTERFACE_MAP()

	CSwapChainVk(CGraphicsContextVk *pContext);

	// XGgxSwapChain methods
	GEMMETHOD(GetSurface)(Canvas::XGfxSurface **ppSurface) final;
	GEMMETHOD(WaitForLastPresent)() final;

	// Internal methods
	Gem::Result Initialize(HWND hWnd, bool Windowed);
	Gem::Result Present();
};