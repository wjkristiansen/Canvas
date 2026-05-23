//================================================================================================
// FenceToken - lightweight {timeline, value} handle passed by value.
//
// TimelineId indexes into CResourceManager::m_Timelines. The ID3D12Fence* COM
// pointer lives once in the FenceTimeline, not duplicated per token. Defined
// in its own header so consumers that only need the token type (e.g.
// CSurface12) don't have to pull in all of ResourceManager.h.
//================================================================================================

#pragma once

#include <cstdint>

struct FenceToken
{
    uint32_t TimelineId = kInvalidTimelineId;
    uint64_t Value      = 0;

    static constexpr uint32_t kInvalidTimelineId = UINT32_MAX;

    bool IsValid() const { return TimelineId != kInvalidTimelineId; }
};
