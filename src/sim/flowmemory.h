#pragma once
#include "flowfield.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace tak::sim::flow {
// Deterministic logical storage reservation. This covers simultaneous old/new
// profile generations and scratch, not just published payloads. It deliberately
// overcounts shared tiles; only active rebuilds reserve graph scratch. The
// node/growth allowances cover supported standard
// library implementations; this is not an operating-system RSS or OOM promise.
struct MemoryPlan {
    static constexpr uint64_t MiB=1024*1024;
    static constexpr uint64_t defaultLimit=512*MiB;
    static constexpr size_t maxProfiles=16,maxSnapshots=2,maxCachedTiles=1024,maxRequests=16384,maxStructures=32768;
    static constexpr size_t maxDestinations=32,maxFields=512,maxBuilders=16,maxLocalJobs=8;
    static constexpr Limits topologyLimits{1024,65536,262144};
    // Canonical byte charges keep admission identical across 32/64-bit ABIs.
    // Compile-time guards make layout growth require an explicit budget review.
    static constexpr uint64_t tileBytes=16512,fieldBytes=20736,builderBytes=49152,edgeBytes=24;
    // Per admitted unit: bounded request + firing seeds, subscriber and FIFO
    // nodes, traffic record/group/wait sets, service binding, and node padding.
    // Traffic's footprint reservations cap both links (65,536 at 48 bytes)
    // and buckets (16,384 at 112 bytes): at most 304 bytes per admitted unit.
    // Including those reservations, the individual worst-case buckets total
    // about 1.7KiB on 64-bit hosts, below the canonical 2KiB charge.
    static constexpr uint64_t requestBytes=2048;
    static constexpr uint64_t resolvedBytes=192;
    static_assert(sizeof(Tile)<=tileBytes && sizeof(Field)<=fieldBytes &&
                  sizeof(FieldBuilder)<=builderBytes && sizeof(Edge)<=edgeBytes);
    uint64_t sharedBytes=0,profileBytes=0,snapshotBytes=0,cacheBytes=0,reservedBytes=0,limitBytes=0;
    size_t profiles=0;
    bool supported=false;

    static MemoryPlan forMap(int width,int height,uint64_t limit=defaultLimit) {
        MemoryPlan out;out.limitBytes=limit;
        // Match maps support at most 64x64, or 2048x2048 path cells. Reject
        // unsupported dimensions before any multiplication/allocation.
        if(width<=0||height<=0||width>2048||height>2048)return out;
        const uint64_t tx=(uint64_t(width)+63)/64,tz=(uint64_t(height)+63)/64;
        const uint64_t tiles=tx*tz,cells=uint64_t(width)*uint64_t(height);
        const uint64_t components=std::min<uint64_t>(cells,topologyLimits.maxComponents);
        // At most two directed crossings per cell on each tile interface.
        const uint64_t edges=std::min<uint64_t>(128*((tx-1)*tz+(tz-1)*tx),topologyLimits.maxEdges);
        // Published graphs are lightweight: only immutable tiles and final CSR.
        // Reserve scratch separately for at most two simultaneous generations,
        // rather than charging every idle profile for a full active rebuild.
        out.profileBytes=tiles*(tileBytes+128)+components*8+edges*(2*edgeBytes)+128*1024;
        out.snapshotBytes=tiles*(tileBytes+128)+components*24+
            edges*(4*edgeBytes+96)+2*MiB;
        // Cache references count against this pool even when their payload also
        // belongs to a resident profile. Deliberate overcount bounds eviction
        // retention without requiring pointer-dependent admission decisions.
        out.cacheBytes=maxCachedTiles*(tileBytes+256ull)+tileBytes;
        // Dense obstacle counts + bounded same-owner gate tiles, structure
        // installation/removal tables, requests/subscribers/traffic/bindings,
        // destination BFS arrays and point-goal keys, detailed fields/builders,
        // local fallback jobs and transient publication/queue storage.
        out.sharedBytes=cells*2+8*MiB+maxStructures*640ull+
            maxRequests*requestBytes+maxDestinations*(components*12+4096*32+4096)+
            maxFields*(fieldBytes+256)+maxBuilders*(builderBytes+512)+
            maxLocalJobs*MiB+4*MiB+
            // Per destination and tile, the memoised field identity (exit
            // mask and bias box) that selects a private or shared field.
            maxDestinations*maxCachedTiles*resolvedBytes;
        const uint64_t fixed=out.sharedBytes+out.cacheBytes+maxSnapshots*out.snapshotBytes;
        if(fixed>limit||out.profileBytes>limit-fixed)return out;
        out.profiles=size_t(std::min<uint64_t>(maxProfiles,(limit-fixed)/out.profileBytes));
        out.reservedBytes=fixed+out.profiles*out.profileBytes;
        out.supported=out.profiles!=0;
        return out;
    }
};
} // namespace tak::sim::flow
