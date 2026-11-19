//================================================================================================
// CCpkgWriter
//================================================================================================

#include "CpkgFile.h"
#include "CpkgFormat.h"

#include <cstring>
#include <stdexcept>

namespace Canvas
{

CCpkgWriter::CCpkgWriter() = default;

CCpkgWriter::~CCpkgWriter()
{
    if (IsOpen())
        Fail(Gem::Result::Fail);
}

//------------------------------------------------------------------------------------------------
Gem::Result CCpkgWriter::Open(const char *fileName, std::string_view packageName)
{
    if (IsOpen() || !fileName || packageName.empty() || !CpkgIsValidName(packageName))
        return Gem::Result::InvalidArg;

    std::unique_ptr<CCpkgFile> pFile;
    Gem::Result result = CCpkgFile::CreateNew(fileName, &pFile);
    if (Gem::Failed(result))
        return result;

    try
    {
        m_FileName = fileName;
    }
    catch (const std::bad_alloc &)
    {
        pFile.reset();
        CCpkgFile::Delete(fileName);
        return Gem::Result::OutOfMemory;
    }

    m_pFile = std::move(pFile);
    m_PackageNameLength = uint32_t(packageName.size());
    m_EndOffset = 0;

    // The header's bundle table and bundle name blob fields stay zero until Close.
    CpkgFileHeader header = {};
    std::memcpy(header.Magic, kCpkgMagic, sizeof(header.Magic));
    header.Version = kMaxSupportedVersion;
    header.PackageNameOffset = sizeof(CpkgFileHeader);
    header.PackageNameLength = m_PackageNameLength;

    result = Write(&header, sizeof(header));
    if (Gem::Succeeded(result))
        result = Write(packageName.data(), packageName.size());
    if (Gem::Failed(result))
        return Fail(result);

    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
Gem::Result CCpkgWriter::Close()
{
    if (!IsOpen())
        return Gem::Result::NotOpen;
    if (m_BundleOpen)
        return Gem::Result::InvalidArg;

    CpkgFileHeader header = {};
    std::memcpy(header.Magic, kCpkgMagic, sizeof(header.Magic));
    header.Version = kMaxSupportedVersion;
    header.PackageNameOffset = sizeof(CpkgFileHeader);
    header.PackageNameLength = m_PackageNameLength;
    header.BundleCount = uint32_t(m_BundleTable.size());

    header.BundleTableOffset = m_EndOffset;
    Gem::Result result = Write(m_BundleTable.data(), m_BundleTable.size() * sizeof(CpkgBundleTableEntry));
    if (Gem::Failed(result))
        return Fail(result);

    header.BundleNameBlobOffset = m_EndOffset;
    header.BundleNameBlobSize = m_BundleNameBlob.size();
    result = Write(m_BundleNameBlob.data(), m_BundleNameBlob.size());
    if (Gem::Failed(result))
        return Fail(result);

    result = m_pFile->WriteAt(0, &header, sizeof(header));
    if (Gem::Failed(result))
        return Fail(result);

    Reset();
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
Gem::Result CCpkgWriter::BeginBundle(std::string_view bundleName, uint32_t *pBundleIndex)
{
    if (!IsOpen())
        return Gem::Result::NotOpen;
    if (m_BundleOpen
        || bundleName.empty()
        || !CpkgIsValidName(bundleName)
        || m_BundleNames.contains(bundleName)
        || m_BundleTable.size() >= UINT32_MAX)
    {
        return Gem::Result::InvalidArg;
    }

    try
    {
        CpkgBundleTableEntry entry = {};
        entry.NameOffset = m_BundleNameBlob.size();
        entry.NameLength = uint32_t(bundleName.size());

        m_BundleNames.emplace(bundleName);
        m_BundleNameBlob.append(bundleName);
        m_BundleTable.push_back(entry);
    }
    catch (const std::bad_alloc &)
    {
        return Fail(Gem::Result::OutOfMemory);
    }
    catch (const std::length_error &)
    {
        return Fail(Gem::Result::OutOfMemory);
    }

    m_BundleOpen = true;
    m_AssetTable.clear();
    m_AssetNameBlob.clear();
    m_AssetNames.clear();

    if (pBundleIndex)
        *pBundleIndex = uint32_t(m_BundleTable.size() - 1);
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
Gem::Result CCpkgWriter::EndBundle()
{
    if (!IsOpen())
        return Gem::Result::NotOpen;
    if (!m_BundleOpen)
        return Gem::Result::InvalidArg;

    CpkgBundleTableEntry &entry = m_BundleTable.back();
    entry.AssetCount = uint32_t(m_AssetTable.size());

    entry.AssetTableOffset = m_EndOffset;
    Gem::Result result = Write(m_AssetTable.data(), m_AssetTable.size() * sizeof(CpkgAssetTableEntry));
    if (Gem::Failed(result))
        return Fail(result);

    entry.AssetNameBlobOffset = m_EndOffset;
    entry.AssetNameBlobSize = m_AssetNameBlob.size();
    result = Write(m_AssetNameBlob.data(), m_AssetNameBlob.size());
    if (Gem::Failed(result))
        return Fail(result);

    m_BundleOpen = false;
    m_AssetTable.clear();
    m_AssetNameBlob.clear();
    m_AssetNames.clear();
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
Gem::Result CCpkgWriter::AppendAsset(AssetType assetType, std::string_view name, CpkgCodec codec, std::span<const std::byte> data, uint32_t *pAssetIndex)
{
    if (!IsOpen())
        return Gem::Result::NotOpen;
    if (!m_BundleOpen
        || !CpkgIsKnownAssetType(assetType)
        || !CpkgIsKnownCodec(codec)
        || data.empty()
        || !CpkgIsValidName(name)
        || (name.empty() && CpkgIsSharedAssetType(assetType))
        || (!name.empty() && m_AssetNames.contains(name))
        || m_AssetTable.size() >= UINT32_MAX)
    {
        return Gem::Result::InvalidArg;
    }

    CpkgAssetTableEntry entry = {};
    entry.PayloadOffset = m_EndOffset;
    entry.NameOffset = name.empty() ? 0 : m_AssetNameBlob.size();
    entry.Type = assetType;
    entry.NameLength = uint32_t(name.size());
    entry.Compression.Codec = codec;
    entry.Compression.StoredSize = data.size();
    entry.Compression.RawSize = data.size();

    try
    {
        if (!name.empty())
        {
            m_AssetNames.emplace(name);
            m_AssetNameBlob.append(name);
        }
        m_AssetTable.push_back(entry);
    }
    catch (const std::bad_alloc &)
    {
        return Fail(Gem::Result::OutOfMemory);
    }
    catch (const std::length_error &)
    {
        return Fail(Gem::Result::OutOfMemory);
    }

    Gem::Result result = Write(data.data(), data.size());
    if (Gem::Failed(result))
        return Fail(result);

    if (pAssetIndex)
        *pAssetIndex = uint32_t(m_AssetTable.size() - 1);
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
Gem::Result CCpkgWriter::Write(const void *pData, size_t size)
{
    Gem::Result result = m_pFile->WriteAt(m_EndOffset, pData, size);
    if (Gem::Succeeded(result))
        m_EndOffset += size;
    return result;
}

// Closes the writer after a failure: releases the file, deletes it, and returns result.
Gem::Result CCpkgWriter::Fail(Gem::Result result)
{
    m_pFile.reset();
    CCpkgFile::Delete(m_FileName.c_str());
    Reset();
    return result;
}

void CCpkgWriter::Reset()
{
    m_pFile.reset();
    m_FileName.clear();
    m_PackageNameLength = 0;
    m_EndOffset = 0;
    m_BundleTable.clear();
    m_BundleNameBlob.clear();
    m_BundleNames.clear();
    m_BundleOpen = false;
    m_AssetTable.clear();
    m_AssetNameBlob.clear();
    m_AssetNames.clear();
}

} // namespace Canvas
