// Complete bounded UE4.26 PhysX BodySetup tail and CPU triangle collision facts.
// Facts alone confer no creator, source, platform or gameplay admission.
#pragma once
#include "bulk_data.h"
#include <cmath>
#include <functional>
#include <set>

namespace P4MapKit { namespace PhysicsBody {
using Bytes=std::vector<std::uint8_t>;
using Vec3=std::array<float,3>;
struct RTreePage { std::array<std::array<float,4>,6> Bounds{}; std::array<std::uint32_t,4> Children{}; };
struct TriangleMesh {
    std::uint32_t Version=0,Midphase=0,Flags=0;
    std::vector<Vec3> Vertices;
    std::vector<std::array<std::uint32_t,3>> Triangles;
    std::vector<std::uint16_t> Materials;
    std::vector<std::uint32_t> FaceRemap;
    std::vector<std::array<std::uint32_t,3>> Adjacencies;
    std::array<std::array<float,4>,4> TreeBounds{};
    std::uint32_t RootPages=0,Levels=0,Nodes=0;
    std::vector<RTreePage> Pages;
    float Epsilon=0;Vec3 Minimum{},Maximum{};
    Bytes ExtraTriangleData,Serialized;
};
struct Cook {
    std::vector<TriangleMesh> TriMeshes;
    std::vector<std::uint32_t> UVIndices;
    std::vector<Vec3> UVPositions;
    std::vector<std::vector<std::array<float,2>>> UVChannels;
    Bytes Serialized; // every actual PhysX and UV byte, including acceleration data
};
struct Format { std::string Name;FByteBulkRange Bulk;Cook Data; };
struct Body {
    std::array<std::uint32_t,4> Guid{};
    bool HasCookedCollision=false;
    std::vector<Format> Formats;
};
using ReadBulk=std::function<bool(const FByteBulkRange&,Bytes&)>;
namespace Detail {
class In {
    const Bytes& B;
public:
    FByteReader R;
    explicit In(const Bytes& Data,std::size_t Start=0,std::size_t End=std::numeric_limits<std::size_t>::max()):B(Data),R(Data,Start,End) {}
    bool Reject(const char* Why) {return R.Reject(Why);}
    bool Word(std::uint32_t& V,bool Big=false) {
        if(!R.U32(V)) return false;
        if(Big) V=((V&255)<<24)|((V&0xff00)<<8)|((V>>8)&0xff00)|(V>>24);
        return true;
    }
    bool Real(float& V,bool Big=false) {
        std::uint32_t Bits=0;if(!Word(Bits,Big)) return false;std::memcpy(&V,&Bits,4);
        return std::isfinite(V)||Reject("physics-nonfinite-float");
    }
    template<std::size_t N> bool Reals(std::array<float,N>& V,bool Big=false) {for(auto& F:V) if(!Real(F,Big)) return false;return true;}
    bool Count(std::size_t& N,std::size_t Max,std::size_t Width=1,bool Big=false) {
        std::uint32_t V=0;if(!Word(V,Big)) return false;N=V;
        return (N<=Max && N<=(R.Limit()-R.Tell())/Width)||Reject("physics-array-bound");
    }
    bool Bool(bool& V) {std::uint32_t W=0;if(!Word(W)) return false;if(W>1) return Reject("physics-invalid-bool");V=W!=0;return true;}
    bool Byte(std::uint8_t& V) {std::uint64_t W=0;if(!R.Unsigned(1,W)) return false;V=std::uint8_t(W);return true;}
    bool Magic(const char* Text,std::size_t N) {
        const auto At=R.Tell();if(!R.Skip(N)) return false;
        return std::memcmp(B.data()+At,Text,N)==0||Reject("physics-stream-magic");
    }
    bool Indices(std::vector<std::uint32_t>& Out,std::size_t Count,std::size_t Width,std::uint32_t Max) {
        if(Width<1 || Width>4 || Count>(R.Limit()-R.Tell())/Width) return Reject("physics-index-array-bound");
        Out.resize(Count);for(auto& I:Out) {std::uint64_t W=0;if(!R.Unsigned(Width,W)) return false;
            if(W>Max) return Reject("physics-index-out-of-bounds");I=std::uint32_t(W);}
        return true;
    }
    Bytes Span(std::size_t Start) const {return {B.begin()+Start,B.begin()+R.Tell()};}
};
inline bool Tree(In& I,TriangleMesh& M) {
    std::uint32_t Version=0,PageSize=0,Pages=0,Unused=0;
    if(!I.Magic("RTRE",4)||!I.Word(Version,true)||Version!=1) return I.Reject("physics-rtree-version");
    for(auto& B:M.TreeBounds) if(!I.Reals(B,true)) return false;
    if(!I.Word(PageSize,true)||!I.Word(M.RootPages,true)||!I.Word(M.Levels,true)||!I.Word(M.Nodes,true)||!I.Word(Pages,true)||!I.Word(Unused,true)) return false;
    if(PageSize!=4 || !Pages || Pages>100000 || !M.RootPages || M.RootPages>Pages || !M.Levels || M.Levels>64 ||
       M.Nodes>std::uint64_t(Pages)*4 || Pages>(I.R.Limit()-I.R.Tell())/112 || Unused)
        return I.Reject("physics-rtree-census-invalid");
    M.Pages.resize(Pages);
    for(auto& Page:M.Pages) {
        for(auto& Axis:Page.Bounds) if(!I.Reals(Axis,true)) return false;
        for(auto& Ptr:Page.Children) if(!I.Word(Ptr,true)) return false;
    }
    // The static cooking image stores sizeof(RTreePage)==112 byte offsets.
    // Validate the actual reachable tree, its leaf ranges and complete coverage.
    std::vector<std::pair<std::size_t,std::size_t>> Work;
    for(std::size_t P=0;P<M.RootPages;++P) Work.push_back({P,1});
    std::vector<bool> Seen(Pages,false),Covered(M.Triangles.size(),false);
    std::size_t Nodes=0,MaxLevel=0;
    while(!Work.empty()) {
        const auto Entry=Work.back();Work.pop_back();const auto P=Entry.first,Level=Entry.second;
        if(P>=Pages || Seen[P] || Level>M.Levels) return I.Reject("physics-rtree-cycle-or-depth");
        Seen[P]=true;MaxLevel=std::max(MaxLevel,Level);const auto& Page=M.Pages[P];
        for(std::size_t N=0;N<4;++N) {
            const bool Empty=Page.Bounds[0][N]>Page.Bounds[3][N];
            if(Empty) {
                if(!(Page.Bounds[1][N]>Page.Bounds[4][N] && Page.Bounds[2][N]>Page.Bounds[5][N])) return I.Reject("physics-rtree-empty-bounds");
                continue;
            }
            if(Page.Bounds[1][N]>Page.Bounds[4][N] || Page.Bounds[2][N]>Page.Bounds[5][N]) return I.Reject("physics-rtree-inverted-bounds");
            ++Nodes;const auto Ptr=Page.Children[N];
            if(Ptr&1) {
                const auto Begin=std::size_t(Ptr>>5),Count=std::size_t(((Ptr>>1)&15)+1);
                if(Begin>M.Triangles.size() || Count>M.Triangles.size()-Begin) return I.Reject("physics-rtree-leaf-bound");
                for(std::size_t T=Begin;T<Begin+Count;++T) {if(Covered[T]) return I.Reject("physics-rtree-leaf-overlap");Covered[T]=true;}
            } else {
                if(Ptr%112 || Ptr/112>=Pages) return I.Reject("physics-rtree-child-bound");
                Work.push_back({Ptr/112,Level+1});
            }
        }
    }
    if(M.Nodes!=std::uint64_t(Pages)*4 || Nodes>M.Nodes || MaxLevel!=M.Levels || std::find(Seen.begin(),Seen.end(),false)!=Seen.end() ||
       std::find(Covered.begin(),Covered.end(),false)!=Covered.end()) return I.Reject("physics-rtree-incomplete-census");
    return true;
}
inline bool Triangle(In& I,TriangleMesh& M) {
    const auto Start=I.R.Tell();std::size_t NV=0,NT=0;
    if(!I.Magic("NXS\x01MESH",8)||!I.Word(M.Version)||!I.Word(M.Midphase)||!I.Word(M.Flags)) return false;
    if(M.Version!=14 || M.Midphase!=0 || (M.Flags&~31u) || ((M.Flags&12u)==12u)) return I.Reject("physics-triangle-profile-unqualified");
    if(!I.Count(NV,2000000,12)||!I.Count(NT,2000000,3)||!NV || !NT) return I.Reject("physics-empty-or-large-triangle-mesh");
    M.Vertices.resize(NV);for(auto& V:M.Vertices) if(!I.Reals(V)) return false;
    const auto Width=(M.Flags&4)?1:(M.Flags&8)?2:4;
    std::vector<std::uint32_t> Indices;if(!I.Indices(Indices,NT*3,Width,std::uint32_t(NV-1))) return false;
    M.Triangles.resize(NT);for(std::size_t T=0;T<NT;++T) for(std::size_t C=0;C<3;++C) M.Triangles[T][C]=Indices[T*3+C];
    if(M.Flags&1) {M.Materials.resize(NT);for(auto& V:M.Materials) {std::uint64_t W=0;if(!I.R.Unsigned(2,W)) return false;V=std::uint16_t(W);}}
    if(M.Flags&2) {
        std::uint32_t Max=0;if(!I.Word(Max)||Max>=2000000) return I.Reject("physics-face-remap-bound");
        if(!I.Indices(M.FaceRemap,NT,Max<=255?1:Max<=65535?2:4,Max)) return false;
        std::set<std::uint32_t> Unique(M.FaceRemap.begin(),M.FaceRemap.end());
        if(Unique.size()!=NT) return I.Reject("physics-face-remap-duplicates");
    }
    if(M.Flags&16) {
        M.Adjacencies.resize(NT);for(auto& A:M.Adjacencies) for(auto& V:A) {
            if(!I.Word(V)) return false;if(V!=UINT32_MAX && V>=NT) return I.Reject("physics-adjacency-bound");
        }
    }
    if(!Tree(I,M)||!I.Real(M.Epsilon)||!I.Reals(M.Minimum)||!I.Reals(M.Maximum)) return false;
    if(M.Epsilon<0) return I.Reject("physics-negative-epsilon");
    for(std::size_t C=0;C<3;++C) if(M.Minimum[C]>M.Maximum[C]) return I.Reject("physics-inverted-mesh-bounds");
    for(const auto& V:M.Vertices) for(std::size_t C=0;C<3;++C)
        if(V[C]<M.Minimum[C]-M.Epsilon || V[C]>M.Maximum[C]+M.Epsilon) return I.Reject("physics-vertex-outside-bounds");
    // A valid pointer census alone does not establish that the collision tree
    // encloses its triangles. Check each actual leaf against decoded vertices.
    for(const auto& Page:M.Pages) for(std::size_t N=0;N<4;++N) {
        if(Page.Bounds[0][N]>Page.Bounds[3][N] || !(Page.Children[N]&1)) continue;
        const auto Begin=std::size_t(Page.Children[N]>>5),Count=std::size_t(((Page.Children[N]>>1)&15)+1);
        for(std::size_t T=Begin;T<Begin+Count;++T) for(const auto Vertex:M.Triangles[T])
            for(std::size_t C=0;C<3;++C) if(M.Vertices[Vertex][C]<Page.Bounds[C][N]-M.Epsilon ||
                M.Vertices[Vertex][C]>Page.Bounds[C+3][N]+M.Epsilon) return I.Reject("physics-rtree-leaf-does-not-enclose-geometry");
    }
    std::size_t Extra=0;if(!I.Count(Extra,NT)|| (Extra && Extra!=NT)) return I.Reject("physics-extra-triangle-count");
    const auto At=I.R.Tell();if(!I.R.Skip(Extra)) return false;M.ExtraTriangleData=I.Span(At);M.Serialized=I.Span(Start);return true;
}
}
inline bool ReadCook(const Bytes& Input,Cook& Out,FReadError& Error) {
    Detail::In I(Input);Cook Pending;std::uint8_t Endian=0;std::size_t Convex=0,Mirrored=0,Triangles=0;
    if(Input.size()>64*1024*1024) {Error={0,"physics-cook-byte-bound"};return false;}
    if(!I.Byte(Endian)||Endian!=1 || !I.Count(Convex,4096)||!I.Count(Mirrored,4096)||!I.Count(Triangles,4096)) {
        if(I.R.Error.Code.empty()) I.Reject("physics-cook-endian");Error=I.R.Error;return false;
    }
    if(Convex || Mirrored) {Error={I.R.Tell(),"physics-convex-stream-unqualified"};return false;}
    Pending.TriMeshes.resize(Triangles);for(auto& M:Pending.TriMeshes) if(!Detail::Triangle(I,M)) {Error=I.R.Error;return false;}
    std::size_t N=0;
    if(!I.Count(N,6000000,4)||!I.Indices(Pending.UVIndices,N,4,UINT32_MAX)||!I.Count(N,2000000,12)) {Error=I.R.Error;return false;}
    Pending.UVPositions.resize(N);for(auto& V:Pending.UVPositions) if(!I.Reals(V)) {Error=I.R.Error;return false;}
    if(Pending.UVIndices.size()%3) {Error={I.R.Tell(),"physics-uv-index-count"};return false;}
    for(const auto V:Pending.UVIndices) if(V>=Pending.UVPositions.size()) {Error={I.R.Tell(),"physics-uv-index-bound"};return false;}
    if(!I.Count(N,8,4)) {Error=I.R.Error;return false;}
    Pending.UVChannels.resize(N);for(auto& Channel:Pending.UVChannels) {
        if(!I.Count(N,2000000,8)||N!=Pending.UVPositions.size()) {if(I.R.Error.Code.empty()) I.Reject("physics-uv-channel-census");Error=I.R.Error;return false;}
        Channel.resize(N);for(auto& V:Channel) if(!I.Reals(V)) {Error=I.R.Error;return false;}
    }
    if(I.R.Tell()!=I.R.Limit()) {Error={I.R.Tell(),"physics-cook-unread-tail"};return false;}
    Pending.Serialized=Input;Out=std::move(Pending);Error={};return true;
}
// Start includes UObject's serialized GUID-presence word; caller must separately
// qualify stock latest / retoc latest BodySetup version gates before calling.
inline bool ReadBody(const Bytes& Input,std::size_t Start,std::size_t End,const std::vector<std::string>& Names,
    const FByteBulkContext& Context,const ReadBulk& Bulk,Body& Out,FReadError& Error) {
    Detail::In I(Input,Start,End);Body Pending;std::uint32_t ObjectGuid=0;bool Cooked=false;std::size_t N=0;
    if(!I.Word(ObjectGuid)||ObjectGuid) {if(I.R.Error.Code.empty()) I.Reject("physics-object-guid-unqualified");Error=I.R.Error;return false;}
    for(auto& W:Pending.Guid) if(!I.Word(W)) {Error=I.R.Error;return false;}
    if(Pending.Guid==std::array<std::uint32_t,4>{}) {Error={I.R.Tell(),"physics-body-guid-empty"};return false;}
    if(!I.Bool(Cooked)||!Cooked||!I.Bool(Pending.HasCookedCollision)||!I.Count(N,4,8)) {
        if(I.R.Error.Code.empty()) I.Reject("physics-uncooked-body");Error=I.R.Error;return false;
    }
    std::set<std::string> Seen;
    for(std::size_t F=0;F<N;++F) {
        Format V;std::uint32_t Index=0,Number=0;
        if(!I.Word(Index)||!I.Word(Number)||Index>=Names.size()||Number) {if(I.R.Error.Code.empty()) I.Reject("physics-format-name");Error=I.R.Error;return false;}
        V.Name=Names[Index];if((V.Name!="PhysXPC" && V.Name!="PhysXGeneric") || !Seen.insert(V.Name).second || N!=1) {Error={I.R.Tell(),"physics-format-unqualified"};return false;}
        if(!ReadByteBulk(I.R,Context,V.Bulk)) {Error=I.R.Error;return false;}
        Bytes BytesIn;if(!Bulk || V.Bulk.Size>64*1024*1024 || !V.Bulk.Size || !Bulk(V.Bulk,BytesIn) || BytesIn.size()!=V.Bulk.Size) {Error={I.R.Tell(),"physics-bulk-read-failed"};return false;}
        if(!ReadCook(BytesIn,V.Data,Error)) return false;Pending.Formats.push_back(std::move(V));
    }
    if(Pending.HasCookedCollision!=!Pending.Formats.empty()) {Error={I.R.Tell(),"physics-cooked-format-census"};return false;}
    if(I.R.Tell()!=I.R.Limit()) {Error={I.R.Tell(),"physics-body-unread-tail"};return false;}
    Out=std::move(Pending);Error={};return true;
}
}}
