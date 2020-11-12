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
public:
	std::mutex m_mutex;
	CGraphicsContextVk *m_pContext; // weak ptr
	UniqueVkSwapchainKHR m_VkSwapChain;
	UniqueVkFence m_VkFence;
	std::vector<VkImage> m_VkImages;
	Gem::TGemPtr<CSurfaceVk> m_pSurface;
	uint32_t m_ImageIndex = 0;
	VkFormat m_SwapchainFormat;
	VkFormat m_ViewFormat;
	VkColorSpaceKHR m_ColorSpace;

public:
	BEGIN_GEM_INTERFACE_MAP()
		GEM_INTERFACE_ENTRY(Canvas::XGfxSwapChain)
	END_GEM_INTERFACE_MAP()

	CSwapChainVk(CGraphicsContextVk *pContext);

	// XGgxSwapChain methods
	GEMMETHOD(GetSurface)(Canvas::XGfxSurface **ppSurface) final;
	GEMMETHOD(WaitForLastPresent)() final;

	// Internal methods
	Gem::Result Initialize(HWND hWnd, bool Windowed, VkFormat Format);
	Gem::Result Present();
};