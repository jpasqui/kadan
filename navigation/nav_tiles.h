// nav.bin: a mod's own navigation for one of the game's maps (modding/NAVIGATION.md).
//
//   <Mod>/romfs/p4m/mods/<mod-id>/nav.bin
//
//   "P4MNAVM1", u32 maps, then for each map:
//     u16 + text   the game's map (package path)
//     u32          navmeshes, then for each:
//       u16 + text the navmesh actor's name in that map (RecastNavMeshPikmin-Pikmin, ...)
//       u32 size, then the navigation payload: what Unreal 4.26 serializes for a RecastNavMesh
//                  (int32 0, int32 version 13, uint32 size, int32 tiles, the navmesh parameters,
//                  then every tile as SerializeRecastMeshTile writes it, each with an empty cache)
//
// A map mod made without game files cannot ship the game's map package, which is where the game keeps
// its navigation. The mod's rebuilt navigation goes here, and a loader swaps it in when the map loads:
// it removes the tiles the game loaded and adds these (native/map_loader/nav_swap.h).
//
// This header reads the file and builds the tile block Detour takes (dtNavMesh::addTile) from a
// serialized tile, as the engine's own loader does. The layouts are those of the game's Detour
// (64-bit polygon references, cluster and segment links compiled in).
//
// Compatibility. A reader that does not know the magic takes nothing from the file, and a file with a
// field out of range is refused whole.
//
// Freestanding: no library calls, the caller owns every buffer.
#pragma once
#include <stdint.h>

namespace p4m::nav {
constexpr uint32_t kPathLimit=255;
constexpr uint32_t kNameLimit=127;
constexpr uint32_t kTileLimit=4096;         // tiles in one navmesh
constexpr int32_t kCountLimit=1000000;      // records of one kind in one tile
constexpr uint32_t kVersion=13;             // NAVMESHVER
constexpr uint32_t kDetourMagic=0x444E4156,kDetourVersion=7;

// In-memory record sizes of the game's Detour.
constexpr uint32_t kHeader=116,kPoly=32,kLink=16,kDetailMesh=12,kBvNode=16,kOffMesh=40,kSegment=64,kCluster=20;
// Serialized sizes of the records whose stored form differs from memory.
constexpr uint32_t kDetailMeshStored=10,kOffMeshStored=36,kSegmentStored=60,kClusterStored=12;

struct Navmesh { uint32_t name,name_length,payload,payload_size,tile_count,max_tiles; };
struct Map { uint32_t path,path_length,first_navmesh,navmesh_count; };
struct Set {
    const uint8_t* data; uint32_t size;
    Map* maps; uint32_t map_capacity,map_count;
    Navmesh* navmeshes; uint32_t navmesh_capacity,navmesh_count;
};
enum Result : uint32_t { kOk=0,kNotNavigation=1,kTruncated=2,kBadValue=3,kFull=4 };

// One serialized tile: where its parts are, and how large its block is in memory.
struct Tile {
    const uint8_t* at;          // the ten counts
    int32_t verts,polys,links,detail_meshes,detail_verts,detail_tris,bv_nodes,off_mesh,segments,clusters;
    int32_t x,y,layer;
    uint32_t stored,memory;     // bytes of the tile in the payload from `at` (with its cache size word), bytes of its block
};

namespace detail {
inline uint32_t u32_at(const uint8_t* p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
inline int32_t i32_at(const uint8_t* p){return int32_t(u32_at(p));}
inline uint32_t u16_at(const uint8_t* p){return uint32_t(p[0])|uint32_t(p[1])<<8;}
inline uint32_t align4(uint32_t n){return (n+3)&~3u;}
inline void copy(uint8_t* to,const uint8_t* from,uint32_t n){for(uint32_t i=0;i<n;++i)to[i]=from[i];}
inline bool text(const uint8_t* s,uint32_t length,uint32_t limit,bool path){
    if(!length||length>limit||(path&&s[0]!='/'))return false;
    for(uint32_t i=0;i<length;++i){
        const uint8_t c=s[i];
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||(path&&c=='/')))return false;
    }
    return true;
}
}

// Reads the tile at `at` (its ten counts; the caller has skipped the tile's reference and declared size).
// False when it does not fit in `available` bytes or a value is out of range.
inline bool read_tile(const uint8_t* at,uint32_t available,Tile* out){
    using namespace detail;
    if(available<40+kHeader)return false;
    Tile t{};t.at=at;
    int32_t* counts[10]={&t.verts,&t.polys,&t.links,&t.detail_meshes,&t.detail_verts,&t.detail_tris,&t.bv_nodes,&t.off_mesh,&t.segments,&t.clusters};
    for(uint32_t i=0;i<10;++i){*counts[i]=i32_at(at+4*i);if(*counts[i]<0||*counts[i]>kCountLimit)return false;}
    const uint8_t* h=at+40;
    if(u32_at(h)!=kDetourMagic||u32_at(h+4)!=kDetourVersion)return false;
    t.x=i32_at(h+8);t.y=i32_at(h+12);t.layer=i32_at(h+16);
    // The header repeats the counts: polys, verts, links, detail meshes, detail verts, detail tris, bv nodes, off-mesh.
    if(i32_at(h+24)!=t.polys||i32_at(h+28)!=t.verts||i32_at(h+32)!=t.links||i32_at(h+36)!=t.detail_meshes||
       i32_at(h+40)!=t.detail_verts||i32_at(h+44)!=t.detail_tris||i32_at(h+48)!=t.bv_nodes||i32_at(h+52)!=t.off_mesh||
       i32_at(h+100)!=t.clusters||i32_at(h+104)!=t.segments)return false;
    const uint64_t stored=40ull+kHeader+12ull*t.verts+uint64_t(kPoly)*t.polys+uint64_t(kDetailMeshStored)*t.detail_meshes+
        12ull*t.detail_verts+4ull*t.detail_tris+uint64_t(kBvNode)*t.bv_nodes+uint64_t(kOffMeshStored)*t.off_mesh+4ull*t.off_mesh+
        uint64_t(kSegmentStored)*t.segments+uint64_t(kClusterStored)*t.clusters+2ull*t.detail_meshes+4;
    const uint64_t memory=uint64_t(align4(kHeader))+align4(12u*t.verts)+align4(kPoly*t.polys)+align4(kLink*t.links)+
        align4(kDetailMesh*t.detail_meshes)+align4(12u*t.detail_verts)+align4(4u*t.detail_tris)+align4(kBvNode*t.bv_nodes)+
        align4(kOffMesh*t.off_mesh)+align4(kSegment*t.segments)+align4(kCluster*t.clusters)+align4(2u*t.detail_meshes);
    if(stored>available||memory>(64u<<20))return false;
    if(u32_at(at+stored-4)!=0)return false;   // a compressed tile cache: the game's maps have none
    t.stored=uint32_t(stored);t.memory=uint32_t(memory);
    *out=t;return true;
}

// The tile's block as dtNavMesh::addTile takes it: t.memory bytes at `out`.
inline void build_tile(const Tile& t,uint8_t* out){
    using namespace detail;
    for(uint32_t i=0;i<t.memory;++i)out[i]=0;
    const uint8_t* s=t.at+40;
    uint8_t* d=out;
    // The header, stored: 15 ints, 10 floats, cluster count, segment count, segment polygon base, segment vertex
    // base. In memory the three segment words and the cluster count come before the floats.
    copy(d,s,60);copy(d+60,s+104,12);copy(d+72,s+100,4);copy(d+76,s+60,40);
    s+=kHeader;d+=align4(kHeader);
    copy(d,s,12u*t.verts);s+=12u*t.verts;d+=align4(12u*t.verts);
    copy(d,s,kPoly*t.polys);s+=kPoly*t.polys;d+=align4(kPoly*t.polys);
    d+=align4(kLink*t.links);                                   // links: made by addTile
    for(int32_t i=0;i<t.detail_meshes;++i)copy(d+kDetailMesh*i,s+kDetailMeshStored*i,kDetailMeshStored);
    s+=kDetailMeshStored*t.detail_meshes;d+=align4(kDetailMesh*t.detail_meshes);
    copy(d,s,12u*t.detail_verts);s+=12u*t.detail_verts;d+=align4(12u*t.detail_verts);
    copy(d,s,4u*t.detail_tris);s+=4u*t.detail_tris;d+=align4(4u*t.detail_tris);
    copy(d,s,kBvNode*t.bv_nodes);s+=kBvNode*t.bv_nodes;d+=align4(kBvNode*t.bv_nodes);
    // Off-mesh connection, stored: pos[6], rad, poly u16, flags u8, side u8, user id u32, and the heights after
    // all of them. In memory: pos[6], rad, height, user id, poly, side, flags.
    const uint8_t* heights=s+kOffMeshStored*t.off_mesh;
    for(int32_t i=0;i<t.off_mesh;++i){
        const uint8_t* from=s+kOffMeshStored*i;uint8_t* to=d+kOffMesh*i;
        copy(to,from,28);copy(to+28,heights+4*i,4);copy(to+32,from+32,4);copy(to+36,from+28,2);
        to[38]=from[31];to[39]=from[30];
    }
    s=heights+4u*t.off_mesh;d+=align4(kOffMesh*t.off_mesh);
    // Segment, stored: start A, start B, end A, end B, rad, first poly u16, polys u8, flags u8, user id u32.
    // In memory: start A, end A, start B, end B, rad, height, user id, first poly, polys, flags.
    for(int32_t i=0;i<t.segments;++i){
        const uint8_t* from=s+kSegmentStored*i;uint8_t* to=d+kSegment*i;
        copy(to,from,12);copy(to+12,from+24,12);copy(to+24,from+12,12);copy(to+36,from+36,12);copy(to+48,from+48,4);
        copy(to+56,from+56,4);copy(to+60,from+52,4);
    }
    s+=kSegmentStored*t.segments;d+=align4(kSegment*t.segments);
    for(int32_t i=0;i<t.clusters;++i)copy(d+kCluster*i,s+kClusterStored*i,kClusterStored);   // its links: made by addTile
    s+=kClusterStored*t.clusters;d+=align4(kCluster*t.clusters);
    copy(d,s,2u*t.detail_meshes);
}

// visit(const Tile&) for every tile of a parsed navmesh's payload. Stops when visit returns false.
template<class F> inline void for_each_tile(const uint8_t* file,const Navmesh& mesh,F&& visit){
    using namespace detail;
    const uint8_t* payload=file+mesh.payload;
    uint32_t at=16+28;   // the payload's own header, then the navmesh parameters
    for(uint32_t i=0;i<mesh.tile_count;++i){
        Tile t{};
        if(!read_tile(payload+at+12,mesh.payload_size-at-12,&t))return;
        if(!visit(t))return;
        at+=12+t.stored;
    }
}

// Reads set->data into the tables, after what they already hold. On any result but kOk the counts are as before.
inline Result parse(Set* set){
    using namespace detail;
    const uint8_t* d=set->data;const uint32_t size=set->size;
    static const char magic[]="P4MNAVM1";
    if(size<12)return kNotNavigation;
    for(uint32_t i=0;i<8;++i)if(d[i]!=uint8_t(magic[i]))return kNotNavigation;
    const uint32_t maps=u32_at(d+8);
    const uint32_t first_map=set->map_count,first_mesh=set->navmesh_count;
    auto fail=[&](Result r){set->map_count=first_map;set->navmesh_count=first_mesh;return r;};
    uint32_t at=12;
    for(uint32_t m=0;m<maps;++m){
        if(set->map_count==set->map_capacity)return fail(kFull);
        if(size-at<2)return fail(kTruncated);
        Map map{};
        map.path_length=u16_at(d+at);map.path=at+2;
        at+=2;
        if(size-at<map.path_length||size-at-map.path_length<4)return fail(kTruncated);
        if(!text(d+at,map.path_length,kPathLimit,true))return fail(kBadValue);
        at+=map.path_length;
        const uint32_t meshes=u32_at(d+at);
        at+=4;
        map.first_navmesh=set->navmesh_count;
        for(uint32_t n=0;n<meshes;++n){
            if(set->navmesh_count==set->navmesh_capacity)return fail(kFull);
            if(size-at<2)return fail(kTruncated);
            Navmesh mesh{};
            mesh.name_length=u16_at(d+at);mesh.name=at+2;
            at+=2;
            if(size-at<mesh.name_length||size-at-mesh.name_length<4)return fail(kTruncated);
            if(!text(d+at,mesh.name_length,kNameLimit,false))return fail(kBadValue);
            at+=mesh.name_length;
            mesh.payload_size=u32_at(d+at);mesh.payload=at+4;
            at+=4;
            if(size-at<mesh.payload_size)return fail(kTruncated);
            const uint8_t* p=d+at;
            if(mesh.payload_size<44||u32_at(p)!=0||u32_at(p+4)!=kVersion||u32_at(p+8)!=mesh.payload_size-8)return fail(kBadValue);
            mesh.tile_count=u32_at(p+12);mesh.max_tiles=u32_at(p+36);
            if(!mesh.tile_count||mesh.tile_count>kTileLimit||mesh.max_tiles<mesh.tile_count)return fail(kBadValue);
            uint32_t tile_at=44;
            for(uint32_t i=0;i<mesh.tile_count;++i){
                Tile t{};
                if(mesh.payload_size-tile_at<12||!read_tile(p+tile_at+12,mesh.payload_size-tile_at-12,&t))return fail(kBadValue);
                tile_at+=12+t.stored;
            }
            if(tile_at!=mesh.payload_size)return fail(kBadValue);
            at+=mesh.payload_size;
            set->navmeshes[set->navmesh_count++]=mesh;
        }
        map.navmesh_count=meshes;
        set->maps[set->map_count++]=map;
    }
    if(at!=size)return fail(kBadValue);
    return kOk;
}
}
