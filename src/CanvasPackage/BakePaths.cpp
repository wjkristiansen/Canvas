#include "pch.h"
#include "BakePaths.h"
#include "CpkgLog.h"

#include <filesystem>
#include <string>

namespace Canvas::Cpkg
{

namespace fs = std::filesystem;

std::string PathToString(const fs::path& path)
{
    // generic_u8string emits UTF-8 with forward slashes; the reinterpret handles both the C++17
    // (std::string) and C++20 (std::u8string) return types uniformly.
    const auto utf8 = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
}

Gem::Result ResolveBakePaths(const BakeManifest& manifest, const char* pManifestPath,
                             ResolvedPaths* out, const PackageLogFn& logFn)
{
    if (!pManifestPath || !out)
    {
        LogF(logFn, PackageLogLevel::Error, "ResolveBakePaths: null %s argument",
             pManifestPath ? "out" : "path");
        return Gem::Result::BadPointer;
    }
    if (manifest.outputPath.empty())
    {
        LogF(logFn, PackageLogLevel::Error, "ResolveBakePaths: manifest has no output path");
        return Gem::Result::InvalidArg;
    }

    // The manifest directory anchors every relative path. Resolve it to absolute (against the
    // current directory when the manifest path itself is relative) before normalizing.
    fs::path manifestDir = fs::u8path(pManifestPath).parent_path();
    if (!manifestDir.is_absolute())
        manifestDir = fs::absolute(manifestDir);
    manifestDir = manifestDir.lexically_normal();

    fs::path outputPath = fs::u8path(manifest.outputPath);
    if (!outputPath.is_absolute())
        outputPath = manifestDir / outputPath;
    outputPath = outputPath.lexically_normal();

    out->manifestDir   = manifestDir;
    out->outputAbsPath = outputPath;
    out->outputDir     = outputPath.parent_path();
    return Gem::Result::Success;
}

std::string ResolveManifestRelative(const ResolvedPaths& paths, const std::string& relPath)
{
    fs::path p = fs::u8path(relPath);
    if (!p.is_absolute())
        p = paths.manifestDir / p;
    return PathToString(p.lexically_normal());
}

std::string MakePkgRelative(const ResolvedPaths& paths, const std::string& absPath)
{
    // lexically_relative is purely lexical here (both paths are already normalized/absolute);
    // PathToString forces forward slashes and UTF-8 so the stored TXTR path is platform-neutral.
    return PathToString(fs::u8path(absPath).lexically_relative(paths.outputDir));
}

} // namespace Canvas::Cpkg
