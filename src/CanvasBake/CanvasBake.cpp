//================================================================================================
// CanvasBake - command-line tool that bakes a scene into a .cpkg package.
//
// Two modes:
//   Manifest mode:  CanvasBake --manifest <scene.cpkg.json> [--log-level <level>]
//   Quick-bake:     CanvasBake --fbx <path> --out <path> [--embed-textures] [--log-level <level>]
//
// Both run the same pipeline: resolve paths, import each FBX source, deduplicate textures through a
// TextureTable, serialize into a PackageData, optionally embed decoded pixels via WIC, and write the
// .cpkg. Quick-bake synthesizes an in-memory BakeManifest so no JSON file is needed.
//================================================================================================
#include "pch.h"

#include "CanvasPackageData.h"   // Canvas::PackageData, PackageTexture, PackageLogFn
#include "BakeManifest.h"        // Canvas::Cpkg::BakeManifest, LoadBakeManifest
#include "BakePaths.h"           // Canvas::Cpkg::ResolveBakePaths, ResolveManifestRelative, PathToString
#include "TextureTable.h"        // Canvas::Cpkg::TextureTable
#include "SerializeScene.h"      // Canvas::Cpkg::SerializeScene

#include "CanvasFbx.h"           // Canvas::Fbx::ImportScene
#include "CanvasPlatformWin32.h" // Canvas::Platform::Win32::LoadImageData (WIC decode)

#include "InCommand.h"

#include <sstream>
#include <utility>

namespace
{

using Canvas::PackageLogLevel;
using namespace Canvas::Cpkg;

//------------------------------------------------------------------------------------------------
// Console logging. All tool and library messages route through ConsoleLog so a single --log-level
// threshold governs both. The PackageLogFn contract (format + va_list, composed by the sink) lets
// us pass ConsoleLog straight to the CanvasPackage entry points.
//------------------------------------------------------------------------------------------------
PackageLogLevel g_LogThreshold = PackageLogLevel::Info;

void ConsoleLog(PackageLogLevel level, const char* format, va_list args)
{
    if (static_cast<int>(level) < static_cast<int>(g_LogThreshold))
        return;

    const char* tag = level == PackageLogLevel::Error   ? "error"
                    : level == PackageLogLevel::Warning ? "warn"
                                                        : "info";
    std::fprintf(stdout, "[%s] ", tag);
    std::vfprintf(stdout, format, args);
    std::fputc('\n', stdout);
}

void LogMsg(PackageLogLevel level, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    ConsoleLog(level, format, args);
    va_end(args);
}

// UTF-8 -> wide at the OS boundary only (see the string convention: never build a path from a
// std::string directly).
std::wstring ToWide(const std::string& utf8)
{
    return std::filesystem::u8path(utf8).wstring();
}

//------------------------------------------------------------------------------------------------
// Decode a source image via WIC and populate tex as an embedded, single-subresource RGBA8 texture.
// v1 embeds decoded pixels only (no mip generation, no BCn compression). Returns false and leaves
// tex as an external path reference on any decode failure.
//------------------------------------------------------------------------------------------------
bool EmbedTexture(const std::string& absPath, Canvas::PackageTexture& tex)
{
    using namespace Canvas::Platform::Win32;

    const std::wstring wide = ToWide(absPath);
    Gem::TGemPtr<XImage> image;
    if (Gem::Failed(LoadImageData(wide.c_str(), Canvas::GfxFormat::R8G8B8A8_UNorm, &image, nullptr)))
    {
        LogMsg(PackageLogLevel::Warning,
               "Embed: WIC decode failed for '%s'; storing as external path reference",
               absPath.c_str());
        return false;
    }

    const uint32_t width  = image->GetWidth();
    const uint32_t height = image->GetHeight();
    const uint32_t bpp    = image->GetBytesPerPixel();
    const uint8_t* pixels = image->GetPixels();
    const size_t   bytes  = image->GetPixelByteCount();
    if (!pixels || bytes == 0)
    {
        LogMsg(PackageLogLevel::Warning,
               "Embed: decoded image '%s' is empty; storing as external path reference",
               absPath.c_str());
        return false;
    }

    tex.Format    = Canvas::GfxFormat::R8G8B8A8_UNorm;
    tex.Dimension = Canvas::GfxSurfaceDimension::Dimension2D;
    tex.Width     = width;
    tex.Height    = height;
    tex.Depth     = 1;
    tex.ArraySize = 1;
    tex.MipCount  = 1;
    tex.Bytes.assign(pixels, pixels + bytes);

    Canvas::PackageSubresource sub;
    sub.Offset   = 0;
    sub.Size     = static_cast<uint32_t>(bytes);
    sub.RowPitch = width * bpp;
    tex.Subresources.assign(1, sub);

    // Embedded payload owns the pixels; the external path is no longer meaningful.
    tex.Path.clear();
    return true;
}

//------------------------------------------------------------------------------------------------
// The shared bake pipeline, driven by an already-parsed manifest and the UTF-8 path used to anchor
// its relative paths. Returns 0 on success, non-zero on failure.
//------------------------------------------------------------------------------------------------
int RunBake(const BakeManifest& manifest, const std::string& manifestPathUtf8)
{
    ResolvedPaths paths;
    if (Gem::Failed(ResolveBakePaths(manifest, manifestPathUtf8.c_str(), &paths, &ConsoleLog)))
    {
        LogMsg(PackageLogLevel::Error, "Failed to resolve bake paths");
        return 1;
    }

    const std::string outputAbs = PathToString(paths.outputAbsPath);
    LogMsg(PackageLogLevel::Info, "Output package: %s", outputAbs.c_str());

    Canvas::PackageData pkg;
    TextureTable        table;

    // Per-TXTR-index bake info, parallel to the table's entries: the absolute source path (for
    // embedding) and whether to embed. Dedup keeps the first-added entry, so manifest textures
    // (added first) win over FBX-sourced duplicates.
    struct TexBakeInfo { std::string absPath; bool embed = false; };
    std::vector<TexBakeInfo> texInfo;

    auto recordTex = [&](int32_t index, const std::string& absPath, bool embed)
    {
        if (index < 0)
            return;
        if (static_cast<size_t>(index) == texInfo.size())
            texInfo.push_back({ absPath, embed });
        // A smaller index is an existing (deduplicated) entry; keep the first-added record.
    };

    // 1) Manifest-declared textures are added first so they own the dedup slot and Name.
    for (const ManifestTexture& mtex : manifest.textures)
    {
        const int32_t     idx     = table.AddManifestTexture(mtex, paths);
        const std::string absPath = ResolveManifestRelative(paths, mtex.path);
        recordTex(idx, absPath, mtex.embed);
    }

    // 2) Import each source scene and add its FBX-sourced textures.
    for (const ManifestSource& source : manifest.sources)
    {
        if (source.type != "fbx")
        {
            LogMsg(PackageLogLevel::Error, "Unsupported source type '%s' (only 'fbx' is supported)",
                   source.type.c_str());
            return 1;
        }

        const std::string fbxAbs = ResolveManifestRelative(paths, source.path);

        std::vector<std::string> searchDirs;
        searchDirs.reserve(source.textureSearchPaths.size());
        for (const std::string& dir : source.textureSearchPaths)
            searchDirs.push_back(ResolveManifestRelative(paths, dir));

        LogMsg(PackageLogLevel::Info, "Importing FBX source: %s", fbxAbs.c_str());

        Canvas::Fbx::ImportOptions options;
        Canvas::Fbx::ImportedScene imported;
        const std::wstring wide = ToWide(fbxAbs);
        const HRESULT      hr    = Canvas::Fbx::ImportScene(wide.c_str(), options, &imported);

        bool hasErrors = FAILED(hr);
        for (const Canvas::Fbx::ImportDiag& diag : imported.Diagnostics)
        {
            const PackageLogLevel lvl =
                diag.Level == Canvas::Fbx::DiagLevel::Error   ? PackageLogLevel::Error
              : diag.Level == Canvas::Fbx::DiagLevel::Warning ? PackageLogLevel::Warning
                                                              : PackageLogLevel::Info;
            LogMsg(lvl, "FBX: %s", diag.Message.c_str());
            if (diag.Level == Canvas::Fbx::DiagLevel::Error)
                hasErrors = true;
        }

        if (hasErrors)
        {
            LogMsg(PackageLogLevel::Error, "FBX import failed: %s (hr=0x%08X)", fbxAbs.c_str(),
                   static_cast<unsigned int>(hr));
            return 1;
        }

        for (const Canvas::Fbx::ImportedTextureRef& ref : imported.Textures)
        {
            const std::string resolved =
                TextureTable::ResolveFbxTexturePath(ref.AbsoluteFilePath, searchDirs);
            if (resolved.empty())
            {
                LogMsg(PackageLogLevel::Warning, "Texture not found, leaving material slot unbound: '%s'",
                       ref.AbsoluteFilePath.c_str());
                continue;
            }
            const int32_t idx = table.AddFbxTexture(resolved, paths);
            recordTex(idx, resolved, manifest.defaultEmbed);
        }

        SerializeScene(imported, table, &pkg);
    }

    // Finalize the deduplicated texture list. This also covers a manifest with only textures[] and
    // no FBX source, where SerializeScene is never called.
    pkg.Textures = table.Finalize();

    // Embed decoded pixels for textures flagged embed=true.
    size_t embeddedCount = 0;
    for (size_t i = 0; i < pkg.Textures.size(); ++i)
    {
        if (i < texInfo.size() && texInfo[i].embed)
        {
            if (EmbedTexture(texInfo[i].absPath, pkg.Textures[i]))
                ++embeddedCount;
        }
    }

    // Ensure the output directory exists before streaming to it.
    std::error_code ec;
    std::filesystem::create_directories(paths.outputDir, ec);

    if (Gem::Failed(pkg.WritePackage(outputAbs.c_str(), &ConsoleLog)))
    {
        LogMsg(PackageLogLevel::Error, "WritePackage failed for '%s'", outputAbs.c_str());
        return 1;
    }

    // A chunk is written for each non-empty category (matching WritePackage's skip-empty rule).
    const uint32_t chunkCount =
        (!pkg.Nodes.empty()     ? 1u : 0u) + (!pkg.Meshes.empty()    ? 1u : 0u) +
        (!pkg.Materials.empty() ? 1u : 0u) + (!pkg.Textures.empty()  ? 1u : 0u) +
        (!pkg.Lights.empty()    ? 1u : 0u) + (!pkg.Cameras.empty()   ? 1u : 0u) +
        (!pkg.AnimClips.empty() ? 1u : 0u);

    std::error_code sizeEc;
    const uintmax_t fileSize = std::filesystem::file_size(paths.outputAbsPath, sizeEc);

    LogMsg(PackageLogLevel::Info,
           "Baked '%s': %u chunks, %zu textures (%zu embedded), %ju bytes",
           outputAbs.c_str(), chunkCount, pkg.Textures.size(), embeddedCount,
           sizeEc ? static_cast<uintmax_t>(0) : fileSize);

    return 0;
}

// Map a --log-level string to the console threshold. Unknown values keep the default (Info).
void ApplyLogLevel(const std::string& level)
{
    if (level == "warn" || level == "warning")
        g_LogThreshold = PackageLogLevel::Warning;
    else if (level == "error")
        g_LogThreshold = PackageLogLevel::Error;
    else
        g_LogThreshold = PackageLogLevel::Info; // "info" or anything else
}

} // namespace

int main(int argc, char** argv)
{
    std::string manifestPath;
    std::string fbxPath;
    std::string outPath;
    std::string logLevel = "info";
    bool        embedTextures = false;

    try
    {
        InCommand::CommandParser cmdParser("CanvasBake");
        auto& rootCmd = cmdParser.GetAppCommandDecl();
        rootCmd.SetDescription("Bake a scene (FBX + textures) into a .cpkg package");

        rootCmd.AddOption(InCommand::OptionType::Variable, "manifest")
            .SetDescription("Path to a .cpkg.json bake manifest")
            .BindTo(manifestPath);

        rootCmd.AddOption(InCommand::OptionType::Variable, "fbx")
            .SetDescription("Quick-bake: path to an FBX file (requires --out)")
            .BindTo(fbxPath);

        rootCmd.AddOption(InCommand::OptionType::Variable, "out")
            .SetDescription("Quick-bake: output .cpkg path")
            .BindTo(outPath);

        rootCmd.AddOption(InCommand::OptionType::Switch, "embed-textures")
            .SetDescription("Quick-bake: embed decoded texture pixels into the package")
            .BindTo(embedTextures);

        rootCmd.AddOption(InCommand::OptionType::Variable, "log-level", 'l')
            .SetDescription("Console verbosity")
            .SetDomain({ "info", "warn", "error" })
            .BindTo(logLevel);

        std::ostringstream helpStream;
        cmdParser.EnableAutoHelp("help", 'h', helpStream);

        // InCommand::ParseArgs takes const char**; main provides char**.
        std::vector<const char*> args;
        args.reserve(static_cast<size_t>(argc));
        for (int i = 0; i < argc; ++i)
            args.push_back(argv[i]);
        cmdParser.ParseArgs(argc, args.data());

        if (cmdParser.WasAutoHelpRequested())
        {
            std::fputs(helpStream.str().c_str(), stdout);
            return 0;
        }
    }
    catch (const InCommand::SyntaxException& e)
    {
        std::fprintf(stderr, "Command line error: %s", e.GetMessage().c_str());
        if (!e.GetToken().empty())
            std::fprintf(stderr, " (token: '%s')", e.GetToken().c_str());
        std::fputc('\n', stderr);
        return -1;
    }
    catch (const InCommand::ApiException& e)
    {
        std::fprintf(stderr, "Internal command-line error: %s\n", e.GetMessage().c_str());
        return -1;
    }

    ApplyLogLevel(logLevel);

    // WIC (texture decode) requires an initialized COM apartment. A console tool has no UI thread,
    // so multithreaded is the natural choice.
    const HRESULT comHr    = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool    comOwned = SUCCEEDED(comHr);

    int rc = 0;

    if (!manifestPath.empty())
    {
        // Manifest mode.
        if (!fbxPath.empty() || !outPath.empty())
            LogMsg(PackageLogLevel::Warning, "--fbx / --out are ignored in manifest mode");

        std::error_code   ec;
        const std::string manifestAbs =
            PathToString(std::filesystem::absolute(std::filesystem::u8path(manifestPath), ec));

        BakeManifest manifest;
        if (Gem::Failed(LoadBakeManifest(manifestAbs.c_str(), &manifest, &ConsoleLog)))
        {
            LogMsg(PackageLogLevel::Error, "Failed to load manifest: %s", manifestAbs.c_str());
            rc = 1;
        }
        else
        {
            rc = RunBake(manifest, manifestAbs);
        }
    }
    else if (!fbxPath.empty())
    {
        // Quick-bake mode: synthesize a manifest in memory. Resolve --fbx / --out to absolute paths
        // against the current directory so the synthetic manifest anchor is unambiguous.
        if (outPath.empty())
        {
            LogMsg(PackageLogLevel::Error, "--fbx requires --out");
            rc = 1;
        }
        else
        {
            std::error_code   ec;
            const std::string fbxAbs =
                PathToString(std::filesystem::absolute(std::filesystem::u8path(fbxPath), ec));
            const std::string outAbs =
                PathToString(std::filesystem::absolute(std::filesystem::u8path(outPath), ec));

            BakeManifest manifest;
            manifest.name         = std::filesystem::u8path(fbxAbs).stem().u8string();
            manifest.outputPath   = outAbs;
            manifest.defaultEmbed = embedTextures;

            ManifestSource source;
            source.type = "fbx";
            source.path = fbxAbs;
            manifest.sources.push_back(std::move(source));

            // The FBX file's directory anchors relative paths (there are none in quick-bake, but
            // ResolveBakePaths still needs a manifest path to derive the anchor from).
            rc = RunBake(manifest, fbxAbs);
        }
    }
    else
    {
        LogMsg(PackageLogLevel::Error,
               "No input. Use --manifest <file> or --fbx <file> --out <file>. Try --help.");
        rc = 1;
    }

    if (comOwned)
        CoUninitialize();

    return rc;
}
