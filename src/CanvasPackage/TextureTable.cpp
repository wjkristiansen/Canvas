#include "pch.h"
#include "TextureTable.h"

#include <filesystem>
#include <system_error>
#include <utility>

namespace Canvas::Cpkg
{

namespace fs = std::filesystem;

std::string TextureTable::KeyOf(const std::string& absPath)
{
    return PathToString(fs::u8path(absPath).lexically_normal());
}

int32_t TextureTable::AddManifestTexture(const ManifestTexture& texture, const ResolvedPaths& paths)
{
    const std::string absPath = ResolveManifestRelative(paths, texture.path);

    const std::string key = KeyOf(absPath);
    if (auto it = m_IndexByKey.find(key); it != m_IndexByKey.end())
        return it->second;

    const int32_t index = static_cast<int32_t>(m_Entries.size());
    m_Entries.push_back({ texture.name, MakePkgRelative(paths, absPath) });
    m_IndexByKey.emplace(key, index);
    return index;
}

int32_t TextureTable::AddFbxTexture(const std::string& absPath, const ResolvedPaths& paths)
{
    const std::string key = KeyOf(absPath);
    if (auto it = m_IndexByKey.find(key); it != m_IndexByKey.end())
        return it->second;

    const int32_t index = static_cast<int32_t>(m_Entries.size());
    m_Entries.push_back({ std::string{}, MakePkgRelative(paths, absPath) });
    m_IndexByKey.emplace(key, index);
    return index;
}

int32_t TextureTable::FindByAbsPath(const std::string& absPath) const
{
    auto it = m_IndexByKey.find(KeyOf(absPath));
    return it != m_IndexByKey.end() ? it->second : -1;
}

std::vector<PackageTexture> TextureTable::Finalize() const
{
    std::vector<PackageTexture> textures;
    textures.reserve(m_Entries.size());
    for (const Entry& entry : m_Entries)
    {
        PackageTexture texture;
        texture.Name = entry.name;
        texture.Path = entry.pkgRelPath;
        textures.push_back(std::move(texture));
    }
    return textures;
}

std::string TextureTable::ResolveFbxTexturePath(const std::string& absPathFromFbx,
                                                const std::vector<std::string>& textureSearchDirs)
{
    std::error_code ec;
    if (!absPathFromFbx.empty() && fs::exists(fs::u8path(absPathFromFbx), ec))
        return absPathFromFbx;

    const fs::path fileName = fs::u8path(absPathFromFbx).filename();
    if (!fileName.empty())
    {
        for (const std::string& dir : textureSearchDirs)
        {
            fs::path candidate = fs::u8path(dir) / fileName;
            if (fs::exists(candidate, ec))
                return PathToString(candidate.lexically_normal());
        }
    }
    return std::string{};
}

} // namespace Canvas::Cpkg
