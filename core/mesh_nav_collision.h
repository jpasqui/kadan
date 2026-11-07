// Complete v4 NavCollision_X facts. These geometry facts are not a navmesh,
// creator custody, collision policy or a walkability/export admission proof.
#pragma once
#include "physics_body.h"
namespace P4MapKit { namespace MeshNavCollision {
using Bytes=PhysicsBody::Bytes;using Vec3=PhysicsBody::Vec3;
struct Geometry {std::vector<Vec3> Vertices;std::vector<std::array<std::uint32_t,3>> Triangles;};
struct Facts {std::array<std::uint32_t,4> Guid{};Geometry Tri,Convex;std::vector<std::uint32_t> ConvexStarts;std::string AreaClass;FByteBulkRange Bulk;Bytes Serialized;};
inline bool Read(const Bytes& Body,std::size_t Start,std::size_t End,const FPackageInventory& Inventory,
    const FByteBulkContext& Context,const PhysicsBody::ReadBulk& Bulk,Facts& Out,FReadError& Error) {
    PhysicsBody::Detail::In I(Body,Start,End);Facts Pending;std::uint32_t ObjectGuid=0,Magic=0,Version=0,Index=0,Number=0;std::size_t N=0;bool Cooked=false;
    const auto Fail=[&](const char* Why) {if(I.R.Error.Code.empty()) I.Reject(Why);Error=I.R.Error;return false;};
    if(!I.Word(ObjectGuid)||ObjectGuid||!I.Word(Magic)||Magic!=0xa237f237 ||!I.Word(Version)||Version!=4) return Fail("mesh-nav-native-profile-unqualified");
    for(auto& W:Pending.Guid) if(!I.Word(W)) return Fail("mesh-nav-guid-truncated");
    if(!I.Bool(Cooked)||!Cooked||!I.Count(N,1)||N!=1||!I.Word(Index)||!I.Word(Number)||Number || Index>=Inventory.Names.size() || Inventory.Names[Index]!="NavCollision_X") return Fail("mesh-nav-format-unqualified");
    if(!ReadByteBulk(I.R,Context,Pending.Bulk)) return Fail("mesh-nav-bulk-invalid");
    Bytes Payload;if(!Bulk || !Pending.Bulk.Size || Pending.Bulk.Size>64*1024*1024 || !Bulk(Pending.Bulk,Payload) || Payload.size()!=Pending.Bulk.Size) return Fail("mesh-nav-bulk-read-failed");
    PhysicsBody::Detail::In B(Payload);std::uint8_t Endian=0;if(!B.Byte(Endian)||Endian!=1) {Error={0,"mesh-nav-payload-endian"};return false;}
    const auto GeometryIn=[&](Geometry& G) {
        std::size_t Vertices=0,Indices=0;if(!B.Count(Vertices,2000000,12)) return false;
        G.Vertices.resize(Vertices);for(auto& V:G.Vertices) if(!B.Reals(V)) return false;
        if(!B.Count(Indices,6000000,4)||Indices%3) return B.Reject("mesh-nav-triangle-count");
        std::vector<std::uint32_t> Flat;if(Indices && (!Vertices || !B.Indices(Flat,Indices,4,std::uint32_t(Vertices-1)))) return B.Reject("mesh-nav-triangle-index-bound");
        G.Triangles.resize(Indices/3);for(std::size_t T=0;T<G.Triangles.size();++T) for(std::size_t C=0;C<3;++C) G.Triangles[T][C]=Flat[T*3+C];return true;
    };
    if(!GeometryIn(Pending.Tri)||!GeometryIn(Pending.Convex)||!B.Count(N,100000,4)||!B.Indices(Pending.ConvexStarts,N,4,std::uint32_t(Pending.Convex.Vertices.size()))) {Error=B.R.Error;return false;}
    if(!std::is_sorted(Pending.ConvexStarts.begin(),Pending.ConvexStarts.end()) || B.R.Tell()!=B.R.Limit()) {Error={B.R.Tell(),"mesh-nav-unread-tail-or-shape-order"};return false;}
    std::int32_t Area=0;if(!I.R.I32(Area) || !Inventory.Reference(Area,Pending.AreaClass) || I.R.Tell()!=I.R.Limit()) return Fail("mesh-nav-area-or-unread-tail");
    Pending.Serialized=std::move(Payload);Out=std::move(Pending);Error={};return true;
}
}}
