#include "pch.h"
#include "BakeManifest.h"
#include "CpkgLog.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>

namespace Canvas::Cpkg
{

Gem::Result LoadBakeManifest(const char* pManifestPath, BakeManifest* out,
                             const PackageLogFn& logFn)
{
    if (!pManifestPath || !out)
    {
        LogF(logFn, PackageLogLevel::Error, "LoadBakeManifest: null %s argument",
             pManifestPath ? "out" : "path");
        return Gem::Result::BadPointer;
    }

    *out = BakeManifest{};

    std::ifstream file(std::filesystem::u8path(pManifestPath), std::ios::binary);
    if (!file.is_open())
    {
        LogF(logFn, PackageLogLevel::Error, "LoadBakeManifest: cannot open manifest file");
        return Gem::Result::NotFound;
    }

    // nlohmann::json::parse throws on malformed input, caught below.
    nlohmann::json root;
    try
    {
        root = nlohmann::json::parse(file);
    }
    catch (const nlohmann::json::exception& e)
    {
        LogF(logFn, PackageLogLevel::Error, "LoadBakeManifest: JSON parse failed: %s", e.what());
        return Gem::Result::InvalidArg;
    }

    if (!root.is_object())
    {
        LogF(logFn, PackageLogLevel::Error, "LoadBakeManifest: root is not a JSON object");
        return Gem::Result::InvalidArg;
    }

    // "output" is the one required field; everything else is optional.
    auto outputIt = root.find("output");
    if (outputIt == root.end() || !outputIt->is_string())
    {
        LogF(logFn, PackageLogLevel::Error,
             "LoadBakeManifest: required field \"output\" is missing or not a string");
        return Gem::Result::InvalidArg;
    }

    try
    {
        BakeManifest manifest;
        manifest.name       = root.value("name", std::string{});
        manifest.outputPath = outputIt->get<std::string>();

        // options.defaultEmbed is read before textures so each texture's omitted embed can fall
        // back to it.
        if (auto optionsIt = root.find("options"); optionsIt != root.end() && optionsIt->is_object())
            manifest.defaultEmbed = optionsIt->value("defaultEmbed", false);

        if (auto sourcesIt = root.find("sources"); sourcesIt != root.end() && sourcesIt->is_array())
        {
            for (const auto& src : *sourcesIt)
            {
                ManifestSource source;
                source.type = src.value("type", std::string{});
                source.path = src.value("path", std::string{});
                if (auto searchIt = src.find("textureSearchPaths");
                    searchIt != src.end() && searchIt->is_array())
                {
                    for (const auto& dir : *searchIt)
                        source.textureSearchPaths.push_back(dir.get<std::string>());
                }
                manifest.sources.push_back(std::move(source));
            }
        }

        if (auto texturesIt = root.find("textures"); texturesIt != root.end() && texturesIt->is_array())
        {
            for (const auto& tex : *texturesIt)
            {
                ManifestTexture texture;
                texture.name  = tex.value("name", std::string{});
                texture.path  = tex.value("path", std::string{});
                texture.embed = tex.value("embed", manifest.defaultEmbed);
                manifest.textures.push_back(std::move(texture));
            }
        }

        *out = std::move(manifest);
    }
    catch (const nlohmann::json::exception& e)
    {
        LogF(logFn, PackageLogLevel::Error, "LoadBakeManifest: malformed manifest field: %s",
             e.what());
        return Gem::Result::InvalidArg;
    }

    return Gem::Result::Success;
}

} // namespace Canvas::Cpkg
