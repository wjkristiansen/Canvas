//================================================================================================
// CpkgFormat - format rules shared by CCpkgWriter and CCpkgReader
//================================================================================================

#pragma once

#include "CanvasPkg.h"

#include <cstdint>

namespace Canvas
{

constexpr char kCpkgMagic[4] = { 'C', 'P', 'K', 'G' };

// True for a named AssetType enumerator other than Unknown.
constexpr bool CpkgIsKnownAssetType(AssetType type)
{
    return uint32_t(type) >= uint32_t(AssetType::TransformNode) && uint32_t(type) <= uint32_t(AssetType::MeshField);
}

// True for a named CpkgCodec enumerator.
constexpr bool CpkgIsKnownCodec(CpkgCodec codec)
{
    return codec == CpkgCodec::None;
}

// True when [offset, offset + count * elementSize) lies after the file header and within a
// file of fileSize bytes, evaluated without overflow.
constexpr bool CpkgExtentFits(uint64_t offset, uint64_t count, uint64_t elementSize, uint64_t fileSize)
{
    return offset >= sizeof(CpkgFileHeader)
        && offset <= fileSize
        && count <= (fileSize - offset) / elementSize;
}

// True when [offset, offset + length) lies within a blob of blobSize bytes, without overflow.
constexpr bool CpkgRangeFits(uint64_t offset, uint64_t length, uint64_t blobSize)
{
    return offset <= blobSize && length <= blobSize - offset;
}

} // namespace Canvas
