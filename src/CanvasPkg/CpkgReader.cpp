//================================================================================================
// CCpkgReader
//================================================================================================

#include "CpkgFile.h"
#include "CpkgFormat.h"

#include <cstring>
#include <stdexcept>

namespace Canvas
{

namespace
{
    // True when a byte count read from the file can be allocated on this host.
    constexpr bool FitsInMemory(uint64_t size)
    {
        return size <= std::numeric_limits<size_t>::max();
    }
}

CCpkgReader::CCpkgReader() = default;
CCpkgReader::~CCpkgReader() = default;

//------------------------------------------------------------------------------------------------
Gem::Result CCpkgReader::Open(const char *fileName)
{
    if (IsOpen() || !fileName)
        return Gem::Result::InvalidArg;

    Gem::Result result = CCpkgFile::OpenRead(fileName, &m_pFile);
    if (Gem::Failed(result))
        return result;

    uint64_t fileSize = 0;
    result = m_pFile->GetSize(&fileSize);
    if (Gem::Succeeded(result))
        result = Load(fileSize);

    if (Gem::Failed(result))
        Reset();
    return result;
}

// Reads and validates the package structure. Every extent is checked against fileSize before it
// is allocated or read.
Gem::Result CCpkgReader::Load(uint64_t fileSize)
{
    try
    {
        // File header
        if (fileSize < sizeof(CpkgFileHeader))
            return Gem::Result::DataNotRecognized;

        CpkgFileHeader header;
        Gem::Result result = m_pFile->ReadAt(0, &header, sizeof(header));
        if (Gem::Failed(result))
            return result;
        if (std::memcmp(header.Magic, kCpkgMagic, sizeof(header.Magic)) != 0)
            return Gem::Result::DataNotRecognized;
        if (header.Version < kMinSupportedVersion || header.Version > kMaxSupportedVersion)
            return Gem::Result::OutOfRange;
        m_Version = header.Version;

        // Package name, bundle table, and bundle name blob
        if (header.PackageNameLength == 0
            || !CpkgExtentFits(header.PackageNameOffset, header.PackageNameLength, 1, fileSize)
            || !CpkgExtentFits(header.BundleTableOffset, header.BundleCount, sizeof(CpkgBundleTableEntry), fileSize)
            || !CpkgExtentFits(header.BundleNameBlobOffset, header.BundleNameBlobSize, 1, fileSize))
        {
            return Gem::Result::CorruptedData;
        }
        if (!FitsInMemory(header.BundleNameBlobSize))
            return Gem::Result::OutOfMemory;

        m_PackageName.resize(header.PackageNameLength);
        result = m_pFile->ReadAt(header.PackageNameOffset, m_PackageName.data(), m_PackageName.size());
        if (Gem::Failed(result))
            return result;

        std::vector<CpkgBundleTableEntry> bundleTable(header.BundleCount);
        result = m_pFile->ReadAt(header.BundleTableOffset, bundleTable.data(), bundleTable.size() * sizeof(CpkgBundleTableEntry));
        if (Gem::Failed(result))
            return result;

        m_BundleNameBlob.resize(size_t(header.BundleNameBlobSize));
        result = m_pFile->ReadAt(header.BundleNameBlobOffset, m_BundleNameBlob.data(), m_BundleNameBlob.size());
        if (Gem::Failed(result))
            return result;

        // Each bundle's asset table and asset name blob
        m_Bundles.resize(bundleTable.size());
        for (size_t bundleIndex = 0; bundleIndex < bundleTable.size(); ++bundleIndex)
        {
            const CpkgBundleTableEntry &entry = bundleTable[bundleIndex];
            if (entry.NameLength == 0
                || !CpkgRangeFits(entry.NameOffset, entry.NameLength, header.BundleNameBlobSize)
                || !CpkgExtentFits(entry.AssetTableOffset, entry.AssetCount, sizeof(CpkgAssetTableEntry), fileSize)
                || !CpkgExtentFits(entry.AssetNameBlobOffset, entry.AssetNameBlobSize, 1, fileSize))
            {
                return Gem::Result::CorruptedData;
            }
            if (!FitsInMemory(entry.AssetNameBlobSize))
                return Gem::Result::OutOfMemory;

            Bundle &bundle = m_Bundles[bundleIndex];
            bundle.Entry = entry;

            bundle.Assets.resize(entry.AssetCount);
            result = m_pFile->ReadAt(entry.AssetTableOffset, bundle.Assets.data(), bundle.Assets.size() * sizeof(CpkgAssetTableEntry));
            if (Gem::Failed(result))
                return result;

            bundle.AssetNameBlob.resize(size_t(entry.AssetNameBlobSize));
            result = m_pFile->ReadAt(entry.AssetNameBlobOffset, bundle.AssetNameBlob.data(), bundle.AssetNameBlob.size());
            if (Gem::Failed(result))
                return result;

            for (const CpkgAssetTableEntry &asset : bundle.Assets)
            {
                if (!CpkgRangeFits(asset.NameOffset, asset.NameLength, entry.AssetNameBlobSize)
                    || (asset.NameLength == 0 && asset.NameOffset != 0)
                    || !CpkgIsKnownAssetType(asset.Type)
                    || !CpkgIsKnownCodec(asset.Compression.Codec)
                    || (asset.Compression.Codec == CpkgCodec::None && asset.Compression.StoredSize != asset.Compression.RawSize)
                    || !CpkgExtentFits(asset.PayloadOffset, asset.Compression.StoredSize, 1, fileSize))
                {
                    return Gem::Result::CorruptedData;
                }
            }
        }
    }
    catch (const std::bad_alloc &)
    {
        return Gem::Result::OutOfMemory;
    }
    catch (const std::length_error &)
    {
        return Gem::Result::OutOfMemory;
    }

    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
Gem::Result CCpkgReader::Close()
{
    if (!IsOpen())
        return Gem::Result::NotOpen;
    Reset();
    return Gem::Result::Success;
}

void CCpkgReader::Reset()
{
    m_pFile.reset();
    m_Version = 0;
    m_PackageName.clear();
    m_BundleNameBlob.clear();
    m_Bundles.clear();
}

//------------------------------------------------------------------------------------------------
uint32_t CCpkgReader::GetVersion() const
{
    return m_Version;
}

std::string_view CCpkgReader::GetPackageName() const
{
    return m_PackageName;
}

uint32_t CCpkgReader::GetBundleCount() const
{
    return uint32_t(m_Bundles.size());
}

std::string_view CCpkgReader::GetBundleName(uint32_t bundleIndex) const
{
    if (bundleIndex >= m_Bundles.size())
        return {};
    const CpkgBundleTableEntry &entry = m_Bundles[bundleIndex].Entry;
    return std::string_view(m_BundleNameBlob).substr(size_t(entry.NameOffset), entry.NameLength);
}

uint32_t CCpkgReader::GetAssetCount(uint32_t bundleIndex) const
{
    return bundleIndex < m_Bundles.size() ? uint32_t(m_Bundles[bundleIndex].Assets.size()) : 0;
}

const CpkgAssetTableEntry *CCpkgReader::FindAsset(uint32_t bundleIndex, uint32_t assetIndex) const
{
    if (bundleIndex >= m_Bundles.size() || assetIndex >= m_Bundles[bundleIndex].Assets.size())
        return nullptr;
    return &m_Bundles[bundleIndex].Assets[assetIndex];
}

AssetType CCpkgReader::GetAssetType(uint32_t bundleIndex, uint32_t assetIndex) const
{
    const CpkgAssetTableEntry *pAsset = FindAsset(bundleIndex, assetIndex);
    return pAsset ? pAsset->Type : AssetType::Unknown;
}

CpkgCompressionScheme CCpkgReader::GetAssetCompression(uint32_t bundleIndex, uint32_t assetIndex) const
{
    const CpkgAssetTableEntry *pAsset = FindAsset(bundleIndex, assetIndex);
    return pAsset ? pAsset->Compression : CpkgCompressionScheme{};
}

std::string_view CCpkgReader::GetAssetName(uint32_t bundleIndex, uint32_t assetIndex) const
{
    const CpkgAssetTableEntry *pAsset = FindAsset(bundleIndex, assetIndex);
    if (!pAsset)
        return {};
    return std::string_view(m_Bundles[bundleIndex].AssetNameBlob).substr(size_t(pAsset->NameOffset), pAsset->NameLength);
}

//------------------------------------------------------------------------------------------------
Gem::Result CCpkgReader::ReadAsset(uint32_t bundleIndex, uint32_t assetIndex, std::span<std::byte> data) const
{
    if (!IsOpen())
        return Gem::Result::NotOpen;

    const CpkgAssetTableEntry *pAsset = FindAsset(bundleIndex, assetIndex);
    if (!pAsset || data.size() < pAsset->Compression.RawSize)
        return Gem::Result::InvalidArg;

    // CpkgCodec::None is the only codec: the stored bytes are the raw payload.
    return m_pFile->ReadAt(pAsset->PayloadOffset, data.data(), size_t(pAsset->Compression.StoredSize));
}

//------------------------------------------------------------------------------------------------
Gem::Result CCpkgReader::BuildBundleDirectory(CCpkgNameDirectory *pDirectory) const
{
    if (!IsOpen())
        return Gem::Result::NotOpen;
    if (!pDirectory)
        return Gem::Result::InvalidArg;

    try
    {
        CCpkgNameDirectory directory;
        directory.m_Entries.reserve(m_Bundles.size());
        for (uint32_t bundleIndex = 0; bundleIndex < m_Bundles.size(); ++bundleIndex)
            directory.m_Entries.emplace(GetBundleName(bundleIndex), bundleIndex);
        *pDirectory = std::move(directory);
    }
    catch (const std::bad_alloc &)
    {
        return Gem::Result::OutOfMemory;
    }
    return Gem::Result::Success;
}

Gem::Result CCpkgReader::BuildAssetDirectory(uint32_t bundleIndex, CCpkgNameDirectory *pDirectory, AssetType typeFilter) const
{
    if (!IsOpen())
        return Gem::Result::NotOpen;
    if (!pDirectory || bundleIndex >= m_Bundles.size())
        return Gem::Result::InvalidArg;

    try
    {
        const Bundle &bundle = m_Bundles[bundleIndex];
        CCpkgNameDirectory directory;
        for (uint32_t assetIndex = 0; assetIndex < bundle.Assets.size(); ++assetIndex)
        {
            const CpkgAssetTableEntry &asset = bundle.Assets[assetIndex];
            if (asset.NameLength == 0 || (typeFilter != AssetType::Unknown && asset.Type != typeFilter))
                continue;
            directory.m_Entries.emplace(GetAssetName(bundleIndex, assetIndex), assetIndex);
        }
        *pDirectory = std::move(directory);
    }
    catch (const std::bad_alloc &)
    {
        return Gem::Result::OutOfMemory;
    }
    return Gem::Result::Success;
}

} // namespace Canvas
