//================================================================================================
// WritePackage - the public PackageData::WritePackage member: streams a PackageData to a .cpkg file.
//
// Follows the Session 3 streaming write pattern: create the file, write the header and a placeholder
// chunk table, stream each present chunk (4-byte aligned) while recording its offset/size, then
// back-patch the table entries and close. Empty chunk types are skipped. The internal write path
// throws a CpkgError on the first failure; this member is the public boundary that catches it and
// returns the carried Gem::Result.
//================================================================================================
#include "pch.h"

#include "CpkgBlobTypes.h"
#include "CpkgChunks.h"
#include "CpkgIO.h"
#include "CpkgLog.h"
#include "CpkgSink.h"

#include <cstdint>

namespace Canvas
{

namespace
{
    using namespace Canvas::Cpkg;

    // One chunk type's identity and writer, plus whether this PackageData carries any of its data.
    struct ChunkPlan
    {
        uint32_t fourcc;
        uint16_t version;
        void (*write)(CCpkgSink&, const PackageData&, const PackageLogFn&);
        bool present;
    };
}

Gem::Result PackageData::WritePackage(const char* pOutputPath, const PackageLogFn& logFn) const
{
    if (!pOutputPath)
    {
        LogF(logFn, PackageLogLevel::Error, "WritePackage: null output path");
        return Gem::Result::BadPointer;
    }

    // Chunk order matches the on-disk convention: NODE, MESH, MATL, TXTR, LITE, CAMR, ANIM.
    const ChunkPlan plan[] = {
        { CPKG_FOURCC_NODE, CPKG_NODE_CHUNK_VERSION, &WriteNodeChunk, !Nodes.empty()     },
        { CPKG_FOURCC_MESH, CPKG_MESH_CHUNK_VERSION, &WriteMeshChunk, !Meshes.empty()    },
        { CPKG_FOURCC_MATL, CPKG_MATL_CHUNK_VERSION, &WriteMatlChunk, !Materials.empty() },
        { CPKG_FOURCC_TXTR, CPKG_TXTR_CHUNK_VERSION, &WriteTxtrChunk, !Textures.empty()  },
        { CPKG_FOURCC_LITE, CPKG_LITE_CHUNK_VERSION, &WriteLiteChunk, !Lights.empty()    },
        { CPKG_FOURCC_CAMR, CPKG_CAMR_CHUNK_VERSION, &WriteCamrChunk, !Cameras.empty()   },
        { CPKG_FOURCC_ANIM, CPKG_ANIM_CHUNK_VERSION, &WriteAnimChunk, !AnimClips.empty() },
    };

    uint32_t chunkCount = 0;
    for (const ChunkPlan& c : plan)
        if (c.present)
            ++chunkCount;

    CCpkgSink sink;
    if (Gem::Result r = CCpkgSink::CreateFile(pOutputPath, &sink,
                                              CCpkgSink::kDefaultFlushBufferSize, logFn);
        Gem::Failed(r))
        return r;

    try
    {
        WriteCpkgHeader(sink, chunkCount);

        const uint64_t tableOffset = sink.Tell();
        WriteChunkTable(sink, chunkCount);

        uint32_t entryIndex = 0;
        for (const ChunkPlan& c : plan)
        {
            if (!c.present)
                continue;

            sink.PadToAlignment(4);
            const uint64_t dataOffset = sink.Tell();
            c.write(sink, *this, logFn);
            const uint32_t sizeRaw = static_cast<uint32_t>(sink.Tell() - dataOffset);

            if (Gem::Result r = PatchChunkEntry(sink, tableOffset, entryIndex, c.fourcc, c.version,
                                                dataOffset, sizeRaw);
                Gem::Failed(r))
                return r;

            ++entryIndex;
        }
    }
    catch (const CpkgError& e)
    {
        return e.Result();
    }

    return sink.Close();
}

} // namespace Canvas
