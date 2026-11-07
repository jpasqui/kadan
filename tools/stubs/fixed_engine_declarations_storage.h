#pragma once
#include <cstdint>

// First-party value storage: this vtable belongs to the host compiler.
// No original executable pointer or native implementation is used.
class UScriptStruct;
#ifndef P4_FIXED_TERRAIN_API
#define P4_FIXED_TERRAIN_API
#define P4_FIXED_TERRAIN_LOCAL_API
#endif

struct P4_FIXED_TERRAIN_API FC4TerrainTag
{
    std::uint32_t TerrainTag;
    FC4TerrainTag() : TerrainTag(0) {}
    virtual ~FC4TerrainTag() {}
    FC4TerrainTag(const FC4TerrainTag& Other) : TerrainTag(Other.TerrainTag) {}
    FC4TerrainTag& operator=(const FC4TerrainTag& Other)
    {
        TerrainTag = Other.TerrainTag;
        return *this;
    }
    static UScriptStruct* StaticStruct();
};

#ifdef P4_FIXED_TERRAIN_LOCAL_API
#undef P4_FIXED_TERRAIN_LOCAL_API
#undef P4_FIXED_TERRAIN_API
#endif
