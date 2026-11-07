// Complete first creator-mesh storage profile, with exact typed property/name
// identities, render buffers, PhysX CPU triangles and navigation collision.
// A readback comparison is not a creator/dependency/source or game admission.
#pragma once
#include "creator_mesh_package.h"
#include "static_mesh_facts.h"
#include "mesh_nav_collision.h"
namespace P4MapKit { namespace MeshCookedReadback {
using Bytes=std::vector<std::uint8_t>;
struct Facts {
    CreatorMeshPackage::Header Header;
    FCookedStaticMeshFacts Mesh;
    PhysicsBody::Body Physics;
    FDecodedValue NavigationProperties;
    FPropertyInventory NavigationTags;
    MeshNavCollision::Facts Navigation;
    Bytes Canonical;
};
namespace Detail {
struct Writer {
    Bytes B;FReadError& Error;std::size_t Values=0;
    explicit Writer(FReadError& E):Error(E) {}
    bool Fail(const char* Why) {if(Error.Code.empty()) Error={0,Why};return false;}
    bool Raw(const std::uint8_t* P,std::size_t N) {if(B.size()>64*1024*1024||N>64*1024*1024-B.size()) return Fail("mesh-readback-canonical-bound");if(N) B.insert(B.end(),P,P+N);return true;}
    bool Word(std::uint32_t V) {std::array<std::uint8_t,4> W{};for(unsigned I=0;I<4;++I) W[I]=std::uint8_t(V>>(I*8));return Raw(W.data(),W.size());}
    bool Count(std::size_t N) {return N<=UINT32_MAX?Word(std::uint32_t(N)):Fail("mesh-readback-count-bound");}
    bool Bool(bool V) {return Word(V?1:0);}
    bool Float(float V) {std::uint32_t Bits=0;std::memcpy(&Bits,&V,4);return std::isfinite(V)?Word(Bits):Fail("mesh-readback-nonfinite");}
    bool Text(const std::string& S) {return Count(S.size())&&Raw(reinterpret_cast<const std::uint8_t*>(S.data()),S.size());}
    bool Data(const Bytes& D) {return Count(D.size())&&Raw(D.data(),D.size());}
    template<class T,std::size_t N> bool Words(const std::array<T,N>& A) {for(const auto V:A) if(!Word(std::uint32_t(V))) return false;return true;}
    template<std::size_t N> bool Floats(const std::array<float,N>& A) {for(const auto V:A) if(!Float(V)) return false;return true;}
    bool Value(const FDecodedValue& V,std::size_t Depth=0) {
        if(Depth>64||++Values>1000000) return Fail("mesh-readback-value-bound");
        if(!Word(std::uint32_t(V.Kind))||!Text(V.Text)||!Text(V.Type)||!Text(V.EnumType)||!Text(V.NameBase)||!Word(std::uint32_t(V.NameNumber))||!Text(V.AssetPath)||!Text(V.SubPath)||!Bool(V.EnumNumeric)) return false;
        std::map<std::string,const FDecodedValue*> Fields;for(const auto& F:V.Fields) if(!Fields.emplace(F.first,&F.second).second) return Fail("mesh-readback-duplicate-property");
        if(!Count(Fields.size())) return false;for(const auto& F:Fields) if(!Text(F.first)||!Value(*F.second,Depth+1)) return false;
        if(!Count(V.Items.size())) return false;for(const auto& Item:V.Items) if(!Value(Item,Depth+1)) return false;return true;
    }
    bool Tags(const FPropertyInventory& Inventory) {
        std::map<std::pair<std::string,std::size_t>,const FPropertyEnvelope*> Tags;
        for(const auto& T:Inventory.Properties) if(!Tags.emplace(std::make_pair(T.Name,T.ArrayIndex),&T).second) return Fail("mesh-readback-duplicate-tag");
        if(!Count(Tags.size())) return false;
        for(const auto& Pair:Tags) {
            const auto& T=*Pair.second;if(!Text(T.Name)||!Text(T.Type)||!Count(T.ArrayIndex)||!Bool(T.BoolValue)||!Bool(T.HasGuid)||!Words(T.StructGuid)||!Words(T.PropertyGuid)||!Count(T.Detail.size())) return false;
            for(const auto& D:T.Detail) if(!Text(D)) return false;
        }
        return true;
    }
    bool Array(const FStaticMeshArray& A) {return Word(A.ElementSize)&&Count(A.Count)&&Data(A.Bytes);}
    bool Index(const FStaticMeshIndexBuffer& I) {return Bool(I.Wide)&&Bool(I.ExpandOnLoad)&&Array(I.Data);}
    bool Buffers(const FStaticMeshBuffers& A) {
        if(!Words(A.Strip)||!Words(A.VertexStrip)||!Words(A.ColorStrip)||!Word(A.Vertices)||!Word(A.TexCoords)||!Word(A.PositionStride)||!Word(A.ColorStride)||!Word(A.ColorVertices)||!Bool(A.FullPrecisionUV)||!Bool(A.HighPrecisionTangents)||
           !Array(A.Positions)||!Array(A.Tangents)||!Array(A.UV)||!Array(A.Colors)||!Array(A.RayTracing)||!Index(A.Main)||!Index(A.Reversed)||!Index(A.DepthOnly)||!Index(A.ReversedDepthOnly)||!Index(A.Wireframe)||!Index(A.Adjacency)||!Count(A.Samplers.size())) return false;
        for(const auto& S:A.Samplers) {
            if(!Count(S.Probabilities.size())) return false;for(auto P:S.Probabilities) if(!Float(P)) return false;
            if(!Count(S.Aliases.size())) return false;for(auto P:S.Aliases) if(!Word(std::uint32_t(P))) return false;if(!Float(S.TotalWeight)) return false;
        }
        return true;
    }
    bool Ref(const FPackageInventory& P,std::int32_t Index,const std::string& Owner) {
        std::string Name;if(!P.Reference(Index,Name)) return Fail("mesh-readback-reference-invalid");
        if(Index>0) Name=Owner+"."+Name;return Text(Name);
    }
    bool Export(const FPackageInventory& P,const FPackageExport& E,const std::string& Owner) {
        return Text(E.Name)&&Ref(P,E.Class,Owner)&&Ref(P,E.Super,Owner)&&Ref(P,E.Template,Owner)&&Ref(P,E.Outer,Owner)&&Word(E.Flags)&&Word(E.PackageFlags)&&Words(E.Guid)&&Words(E.Filters)&&Word(std::uint32_t(E.NotAlwaysLoaded))&&Word(std::uint32_t(E.IsAsset));
    }
    bool Render(const FStaticMeshRenderPayload& A,const FPackageInventory& P,const std::string& Owner) {
        if(!Bool(A.HasObjectGuid)||!Words(A.ObjectGuid)||!Words(A.LightingGuid)||!Bool(A.ShareStaticLighting)||!Words(A.Strip)||!Words(A.DistanceFieldStrip)||!Ref(P,A.BodySetupIndex,Owner)||!Ref(P,A.NavCollisionIndex,Owner)||!Count(A.SocketIndices.size())) return false;
        for(const auto S:A.SocketIndices) if(!Ref(P,S,Owner)) return false;
        if(!Count(A.LODs.size())||!Word(A.InlinedLODs)) return false;
        for(const auto& L:A.LODs) {
            if(!Words(L.Strip)||!Float(L.MaxDeviation)||!Bool(L.CookedOut)||!Bool(L.Inlined)||!Bool(L.HasGeometry)||!Count(L.Sections.size())) return false;
            for(const auto& S:L.Sections) if(!Word(std::uint32_t(S.MaterialIndex))||!Word(S.FirstIndex)||!Word(S.Triangles)||!Word(S.MinVertex)||!Word(S.MaxVertex)||!Bool(S.Collision)||!Bool(S.Shadow)||!Bool(S.ForceOpaque)||!Bool(S.RayTracing)) return false;
            const auto& V=L.Availability;
            if(!Word(V.DepthTriangles)||!Word(V.Flags)||!Word(V.TexCoords)||!Word(V.Vertices)||!Word(V.PositionStride)||!Word(V.PositionVertices)||!Word(V.ColorStride)||!Word(V.ColorVertices)||!Bool(V.FullPrecisionUV)||!Bool(V.HighPrecisionTangents)||!Words(V.IndexCounts)||!Words(V.IndexWide)||!Words(L.BufferSizes)||!Buffers(L.Buffers)) return false;
        }
        if(!Count(A.DistanceFields.size())) return false;
        for(const auto& D:A.DistanceFields) if(!Bool(D.Present)||!Bool(D.BoxValid)||!Bool(D.Closed)||!Bool(D.TwoSided)||!Bool(D.Plane)||!Array(D.CompressedVolume)||!Words(D.Size)||!Floats(D.Bounds)||!Floats(D.MinMax)) return false;
        if(!Floats(A.Bounds)||!Floats(A.ScreenSizes)||!Bool(A.HasOccluder)||!Array(A.OccluderVertices)||!Array(A.OccluderIndices)) return false;
        return true;
    }
};
}
inline bool Read(const Bytes& H,const Bytes& B,const std::string& Owner,const std::string& Object,
    const std::array<std::int64_t,3>& SidecarSizes,const FStaticMeshBulkRead& ReadAt,Facts& Out,FReadError& Error,const std::function<bool()>& Cancel={}) {
    Facts Pending;FByteBulkContext Context;
    if(!CreatorMeshPackage::Read(H,B.size(),Pending.Header,Error)||!CreatorMeshPackage::BulkContext(H,B.size(),Context,Error)) return false;
    std::vector<std::pair<std::size_t,std::size_t>> Spans;
    for(const auto& E:Pending.Header.Inventory.Exports) Spans.push_back({E.Offset,E.Size});
    std::sort(Spans.begin(),Spans.end());std::size_t End=0;
    for(const auto& Span:Spans) {
        if(Span.first!=End || Span.first>B.size() || Span.second>B.size()-Span.first) {Error={End,"mesh-readback-sparse-or-overlapping-exports"};return false;}
        End+=Span.second;
    }
    if(End>B.size() || B.size()-End!=4 || B[End]!=0xc1 || B[End+1]!=0x83 || B[End+2]!=0x2a || B[End+3]!=0x9e ||
        Context.BulkStart!=std::int64_t(H.size()+End)) {Error={End,"mesh-readback-body-trailer-or-bulk-boundary"};return false;}
    for(std::size_t I=0;I<3;++I) Context.FileSizes[I+1]=SidecarSizes[I];
    if(!ReadCookedStaticMeshFacts(Pending.Header.Inventory,B,Owner,Object,Context,ReadAt,Pending.Mesh,Error,{},Cancel)) return false;
    const auto& P=Pending.Header.Inventory;const auto& M=Pending.Mesh;
    if(!M.BodyIndex || !M.Render.SocketIndices.empty()) {Error={0,"mesh-readback-body-or-sockets-unqualified"};return false;}
    const PhysicsBody::ReadBulk Bulk=[&](const FByteBulkRange& Range,Bytes& BytesOut) {
        if(Range.Size>64*1024*1024) return false;BytesOut.resize(std::size_t(Range.Size));
        if(Range.Source==EBulkSource::Body) {if(Range.Offset>B.size()||Range.Size>B.size()-Range.Offset) return false;std::copy(B.begin()+std::size_t(Range.Offset),B.begin()+std::size_t(Range.Offset+Range.Size),BytesOut.begin());return true;}
        return ReadAt && ReadAt(Range.Source,Range.Offset,BytesOut.data(),BytesOut.size());
    };
    if(!PhysicsBody::ReadBody(B,M.BodyNativeOffset,M.BodyExport.Offset+M.BodyExport.Size,P.Names,Context,Bulk,Pending.Physics,Error)) return false;
    const auto NavIndex=M.Render.NavCollisionIndex;
    if(NavIndex) {
        if(NavIndex<1||std::size_t(NavIndex)>P.Exports.size()||NavIndex==M.ExportIndex||NavIndex==M.BodyIndex) {Error={0,"mesh-readback-navigation-index-invalid"};return false;}
        const auto& E=P.Exports[std::size_t(NavIndex)-1];std::string Class;
        if(E.Outer!=M.ExportIndex||!P.Reference(E.Class,Class)||Class!="/Script/NavigationSystem.NavCollision") {Error={E.Offset,"mesh-readback-navigation-identity"};return false;}
        FPropertyDecoder Decoder(P,B);std::size_t At=E.Offset;
        Pending.NavigationTags=ReadPropertyEnvelopes(B,P.Names,E.Offset,E.Offset+E.Size);
        if(!Pending.NavigationTags.Error.Code.empty()) {Error=Pending.NavigationTags.Error;return false;}
        if(!Decoder.TypedProperties(At,E.Offset+E.Size,Pending.NavigationProperties,0,"NavCollision")) {Error=Decoder.Error;return false;}
        if(!MeshNavCollision::Read(B,At,E.Offset+E.Size,P,Context,Bulk,Pending.Navigation,Error)) return false;
    }
    if(P.Exports.size()!=std::size_t(2+(NavIndex!=0))) {Error={0,"mesh-readback-extra-package-export"};return false;}
    Detail::Writer W(Error);
    if(!W.Text("p4-creator-mesh-readback/v1")||!W.Text(Owner)||!W.Text(Object)||!W.Tags(M.MeshPropertyTags)||!W.Tags(M.BodyPropertyTags)||!W.Tags(Pending.NavigationTags)||!W.Value(M.MeshProperties)||!W.Value(M.BodyProperties)||!W.Value(Pending.NavigationProperties)||!W.Render(M.Render,P,Owner)||!W.Words(Pending.Physics.Guid)||!W.Bool(Pending.Physics.HasCookedCollision)||!W.Count(Pending.Physics.Formats.size())) return false;
    for(const auto& F:Pending.Physics.Formats) if(!W.Text("PhysX3.4")||!W.Data(F.Data.Serialized)) return false; // explicit PC/Generic storage label equivalence only
    if(!W.Words(Pending.Navigation.Guid)||!W.Text(Pending.Navigation.AreaClass)||!W.Data(Pending.Navigation.Serialized)||!W.Count(M.Materials.size())) return false;
    for(const auto& S:M.Materials) if(!W.Value(S.MaterialInterface)||!W.Value(S.MaterialSlotName)||!W.Value(S.ImportedMaterialSlotName)||!W.Value(S.NativeMaterialSlotName)||!W.Bool(S.UVInitialized)||!W.Bool(S.UVOverride)||!W.Floats(S.UVDensities)) return false;
    std::map<std::string,const FPackageExport*> Exports;for(const auto& E:P.Exports) {std::string Name;if(!P.Reference(std::int32_t(&E-P.Exports.data()+1),Name)||!Exports.emplace(Name,&E).second) {Error={0,"mesh-readback-export-census"};return false;}}
    if(!W.Count(Exports.size())) return false;for(const auto& E:Exports) if(!W.Text(E.first)||!W.Export(P,*E.second,Owner)) return false;
    if(Cancel&&Cancel()) {Error={0,"mesh-readback-cancelled"};return false;}
    Pending.Canonical=std::move(W.B);Out=std::move(Pending);Error={};return true;
}
}}
