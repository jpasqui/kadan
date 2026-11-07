// Complete latest UE4.26 cooked MapBuildDataRegistry native-tail reader.
// This is semantic readback, not creator ownership or export admission.
#pragma once
#include "package_reader.h"
#include <cmath>
#include <functional>
#include <map>
#include <set>

namespace P4MapKit { namespace LightingRegistry {
using Bytes = std::vector<std::uint8_t>;
using Guid = std::array<std::uint32_t,4>;
struct TextureRef { std::string Object, Class; };
struct LightMap {
    bool Present=false;
    std::vector<Guid> Lights;
    std::array<TextureRef,4> Textures; // HQ, LQ, sky occlusion, AO mask
    std::array<std::array<float,4>,4> Scale{}, Add{};
    std::array<float,2> CoordinateScale{}, CoordinateBias{};
    std::array<bool,4> Channels{};
    std::array<float,4> Penumbra{};
};
struct ShadowMap {
    bool Present=false; std::vector<Guid> Lights; TextureRef Texture;
    std::array<float,2> CoordinateScale{}, CoordinateBias{};
    std::array<bool,4> Channels{}; std::array<float,4> Penumbra{};
};
struct Mesh {
    LightMap Light; ShadowMap Shadow; std::vector<Guid> IrrelevantLights;
    std::vector<std::array<float,4>> PerInstance;
};
struct LightRecord {
    std::int32_t ShadowChannel=0, DepthWidth=0, DepthHeight=0;
    std::array<float,16> WorldToLight{};
    std::vector<std::uint16_t> Depth; // complete serialized half-float bit patterns
};
struct Registry {
    std::map<Guid,Mesh> Meshes;
    std::map<Guid,LightRecord> LightRecords;
    std::set<Guid> AllGuids;
    std::set<Guid> NonStationaryGuids;
    // Exact positions produced by this parser, for scoped stationary-light edits.
    // Static lightmap GUIDs and mesh/volume keys are deliberately not included.
    std::map<Guid,std::vector<std::size_t>> StationaryGuidOffsets;
    std::set<std::pair<std::string,std::string>> Textures; // exact object and class
    std::size_t LightVolumes=0, VolumetricLightmaps=0, Lights=0, Reflections=0, Skies=0;
    Bytes Canonical; // full native content; no source storage offsets/indices
};
// Must resolve a positive export to its exact canonical package/object and class.
using ResolveTexture=std::function<bool(std::int32_t,std::string&,std::string&)>;
namespace Detail {
inline void Word(Bytes& Out,std::uint32_t V) {
    for(unsigned I=0;I<4;++I) Out.push_back(std::uint8_t(V>>(8*I)));
}
inline void Text(Bytes& Out,const std::string& S) {
    Word(Out,std::uint32_t(S.size())); Out.insert(Out.end(),S.begin(),S.end());
}
class In {
    const Bytes& Data; FByteReader R; const ResolveTexture& Resolve; Registry& Result;
    std::size_t GuidReads=0;
public:
    Bytes Canonical;
    In(const Bytes& B,std::size_t Start,std::size_t Stop,const ResolveTexture& ResolveIn,Registry& Out)
        : Data(B),R(B,Start,Stop),Resolve(ResolveIn),Result(Out) {}
    FReadError Error() const {return R.Error;}
    bool Reject(const char* Why) {return R.Reject(Why);}
    bool Room(std::size_t Size) {
        return (Canonical.size()<=64*1024*1024 && Size<=64*1024*1024-Canonical.size()) || Reject("lighting-canonical-byte-bound");
    }
    bool U32(std::uint32_t& V) {if(!R.U32(V)||!Room(4)) return false; Word(Canonical,V);return true;}
    bool I32(std::int32_t& V) {if(!R.I32(V)||!Room(4)) return false; Word(Canonical,std::uint32_t(V));return true;}
    bool Count(std::size_t& V,std::size_t Max=100000) {
        if(!R.Count(V,Max)||!Room(4)) return false; Word(Canonical,std::uint32_t(V));return true;
    }
    bool Bool(bool& V) {
        std::uint32_t N=0;if(!U32(N)) return false;
        if(N>1) return Reject("lighting-invalid-bool");V=N!=0;return true;
    }
    bool Real(float& V) {
        std::uint32_t N=0;if(!U32(N)) return false;std::memcpy(&V,&N,4);
        return std::isfinite(V) || Reject("lighting-nonfinite-float");
    }
    template<std::size_t N> bool Reals(std::array<float,N>& V) {
        for(auto& F:V) if(!Real(F)) return false;return true;
    }
    bool Id(Guid& V,bool Stationary=false) {
        if(++GuidReads>1000000) return Reject("lighting-guid-census-bound");
        const auto At=R.Tell();for(auto& W:V) if(!U32(W)) return false;
        Result.AllGuids.insert(V);if(Stationary) Result.StationaryGuidOffsets[V].push_back(At);
        else Result.NonStationaryGuids.insert(V);return true;
    }
    bool Ids(std::vector<Guid>& V,bool Stationary=false) {
        std::size_t N=0;if(!Count(N)) return false;
        if(N>(R.Limit()-R.Tell())/16) return Reject("lighting-truncated-guid-array");
        V.resize(N);for(auto& G:V) if(!Id(G,Stationary)) return false;return true;
    }
    bool Raw(std::size_t Size) {
        const auto At=R.Tell();if(!R.Skip(Size)) return false;
        if(!Room(Size)) return false;
        Canonical.insert(Canonical.end(),Data.begin()+At,Data.begin()+At+Size);return true;
    }
    bool Blob(std::size_t Element=1,std::size_t* CountOut=nullptr) {
        std::size_t N=0;if(!Count(N,64*1024*1024)) return false;
        if(!Element || N>(R.Limit()-R.Tell())/Element) return Reject("lighting-truncated-blob");
        if(CountOut) *CountOut=N;return Raw(N*Element);
    }
    bool String() {
        std::string S;if(!R.String(S)||!Room(S.size()+4)) return false;Text(Canonical,S);return true;
    }
    bool Texture(TextureRef& V) {
        std::int32_t Index=0;if(!R.I32(Index)) return false;
        if(!Index) {if(!Room(8)) return false;Text(Canonical,"");Text(Canonical,"");return true;}
        if(Index<0 || !Resolve || !Resolve(Index,V.Object,V.Class))
            return Reject("lighting-texture-reference-not-owned-export");
        if(V.Object.empty() || V.Object.size()>4096 || V.Object.find('\0')!=std::string::npos ||
            (V.Class!="/Script/Engine.Texture2D" && V.Class!="/Script/Engine.LightMapTexture2D" &&
             V.Class!="/Script/Engine.ShadowMapTexture2D" && V.Class!="/Script/Engine.TextureCube"))
            return Reject("lighting-invalid-texture-identity");
        if(!Room(V.Object.size()+V.Class.size()+8)) return false;
        Text(Canonical,V.Object);Text(Canonical,V.Class);Result.Textures.emplace(V.Object,V.Class);return true;
    }
    bool Light(LightMap& V) {
        std::uint32_t Kind=0;if(!U32(Kind)) return false;if(!Kind) return true;
        if(Kind!=2) return Reject("lighting-lightmap-kind-unqualified"); V.Present=true;
        if(!Ids(V.Lights)) return false;
        for(auto& T:V.Textures) if(!Texture(T)) return false;
        for(std::size_t I=0;I<4;++I) if(!Reals(V.Scale[I]) || !Reals(V.Add[I])) return false;
        if(!Reals(V.CoordinateScale)||!Reals(V.CoordinateBias)) return false;
        for(auto& C:V.Channels) if(!Bool(C)) return false;
        std::int32_t Virtual=0;
        return Reals(V.Penumbra)&&I32(Virtual)&&(Virtual==0 || Reject("lighting-virtual-lightmap-unqualified"));
    }
    bool Shadow(ShadowMap& V) {
        std::uint32_t Kind=0;if(!U32(Kind)) return false;if(!Kind) return true;
        if(Kind!=2) return Reject("lighting-shadowmap-kind-unqualified"); V.Present=true;
        if(!Ids(V.Lights,true)||!Texture(V.Texture)||!Reals(V.CoordinateScale)||!Reals(V.CoordinateBias)) return false;
        for(auto& C:V.Channels) if(!Bool(C)) return false;return Reals(V.Penumbra);
    }
    bool MeshData(Mesh& V) {
        if(!Light(V.Light)||!Shadow(V.Shadow)||!Ids(V.IrrelevantLights,true)) return false;
        // Empty BulkSerialize permits zero element-size. Normalize that storage detail.
        std::int32_t Size=0;std::size_t N=0;
        if(!R.I32(Size)||!R.Count(N,1000000)) return false;
        if((Size!=16 && !(N==0 && Size==0)) || N>(R.Limit()-R.Tell())/16)
            return Reject("lighting-per-instance-layout-unqualified");
        if(!Room(4)) return false;Word(Canonical,std::uint32_t(N));V.PerInstance.resize(N);
        for(auto& Item:V.PerInstance) if(!Reals(Item)) return false;return true;
    }
    bool Box() {
        std::array<float,6> V{};std::uint64_t Valid=0;
        if(!Reals(V)||!R.Unsigned(1,Valid)) return false;
        if(Valid>1) return Reject("lighting-invalid-box");if(!Room(1)) return false;Canonical.push_back(std::uint8_t(Valid));return true;
    }
    bool Volume() {
        bool Present=false,Initialized=false;
        if(!Bool(Present)) return false;if(!Present) return true;
        if(!Bool(Initialized)) return false;if(!Initialized) return true;
        float Spacing=0;std::int32_t SH=0;
        return Box()&&Real(Spacing)&&I32(SH)&&(SH==9 || Reject("lighting-volume-sh-unqualified"))&&Blob(129)&&Blob(129);
    }
    bool Layer() {return Blob()&&String();}
    bool Vlm() {
        bool Present=false;if(!Bool(Present)) return false;if(!Present) return true;
        if(!Box()) return false;
        std::int32_t Dimension=0;
        for(unsigned I=0;I<3;++I) if(!I32(Dimension)||Dimension<0) return Reject("lighting-invalid-vlm-dimensions");
        if(!Layer()||!I32(Dimension)||Dimension<0) return Reject("lighting-invalid-vlm-brick-size");
        for(unsigned I=0;I<3;++I) if(!I32(Dimension)||Dimension<0) return Reject("lighting-invalid-vlm-dimensions");
        // Ambient, six SH layers, sky bent normal, directional shadow, LQ color/direction.
        for(unsigned I=0;I<11;++I) if(!Layer()) return false;
        return Blob(12)&&Blob(4);
    }
    bool LightData(LightRecord& V) {
        std::size_t N=0;
        if(!I32(V.ShadowChannel)||!Reals(V.WorldToLight)||!I32(V.DepthWidth)||!I32(V.DepthHeight)||!Count(N,32*1024*1024)) return false;
        if(N!=std::uint64_t(std::max(V.DepthWidth,0))*std::uint64_t(std::max(V.DepthHeight,0)))
            return Reject("lighting-depth-dimensions-mismatch");
        const auto At=R.Tell();if(!Raw(N*2)) return false;
        V.Depth.resize(N);for(std::size_t I=0;I<N;++I) V.Depth[I]=std::uint16_t(Data[At+2*I])|std::uint16_t(Data[At+2*I+1])<<8;
        return true;
    }
    bool Reflection() {
        std::int32_t Size=0;float Brightness=0;TextureRef T;
        return I32(Size)&&Real(Brightness)&&Real(Brightness)&&Blob()&&Texture(T);
    }
    template<class ReadValue> bool Map(std::size_t& Number,ReadValue Value,bool Stationary=false) {
        if(!R.Count(Number,100000)) return false;
        std::map<Guid,Bytes> Sorted; auto Prefix=std::move(Canonical);Canonical.clear();
        std::size_t BytesUsed=Prefix.size()+4;
        if(BytesUsed>64*1024*1024) return Reject("lighting-canonical-byte-bound");
        for(std::size_t I=0;I<Number;++I) {
            Guid G{};if(!Id(G,Stationary)) return false;Canonical.clear();
            if(!Value(G)) return false;
            if(Canonical.size()+16>64*1024*1024-BytesUsed) return Reject("lighting-canonical-byte-bound");
            BytesUsed+=Canonical.size()+16;
            if(!Sorted.emplace(G,std::move(Canonical)).second) return Reject("lighting-duplicate-guid");
            Canonical.clear();
        }
        Canonical=Prefix;Word(Canonical,std::uint32_t(Number));
        for(const auto& Item:Sorted) {for(const auto W:Item.first) Word(Canonical,W);
            Canonical.insert(Canonical.end(),Item.second.begin(),Item.second.end());}
        return true;
    }
    bool Run() {
        // Object GUIDs and editor/server-only/custom-version variants are not guessed.
        std::int32_t HasGuid=0;std::uint64_t Global=0,Class=0;
        if(!I32(HasGuid)||HasGuid!=0) return Reject("lighting-registry-object-guid-unqualified");
        if(!R.Unsigned(1,Global)||!R.Unsigned(1,Class)) return false;
        if(Global!=1 || Class!=0) return Reject("lighting-registry-strip-layout-unqualified");
        Canonical.push_back(1);Canonical.push_back(0);
        std::size_t Number=0;
        if(!Map(Number,[&](const Guid& G){Mesh M;if(!MeshData(M)) return false;
            return Result.Meshes.emplace(G,std::move(M)).second || Reject("lighting-duplicate-guid");}) ||
           !Map(Result.LightVolumes,[&](const Guid&){return Volume();}) ||
           !Map(Result.VolumetricLightmaps,[&](const Guid&){return Vlm();}) ||
           !Map(Result.Lights,[&](const Guid& G){LightRecord V;if(!LightData(V)) return false;
               return Result.LightRecords.emplace(G,std::move(V)).second || Reject("lighting-duplicate-guid");},true) ||
           !Map(Result.Reflections,[&](const Guid&){return Reflection();})) return false;
        if(!Count(Result.Skies)) return false;std::set<Guid> Skies;
        auto Prefix=std::move(Canonical);Canonical.clear();
        if(Result.Skies>(64*1024*1024-Prefix.size())/16) return Reject("lighting-canonical-byte-bound");
        for(std::size_t I=0;I<Result.Skies;++I) {Guid G{};if(!Id(G)) return false;
            if(!Skies.insert(G).second) return Reject("lighting-duplicate-guid");}
        Canonical=Prefix;for(const auto& G:Skies) for(auto W:G) Word(Canonical,W);
        return R.Tell()==R.Limit() || Reject("lighting-unconsumed-native-tail");
    }
};
}
inline bool Read(const Bytes& Body,std::size_t Start,std::size_t End,const ResolveTexture& Resolve,
    Registry& Out,FReadError& Error) {
    if(End<Start || End>Body.size() || End-Start>64*1024*1024) {
        Error={Start,"lighting-native-span-bound"};return false;
    }
    Registry Pending;Detail::In Reader(Body,Start,End,Resolve,Pending);
    if(!Reader.Run()) {Error=Reader.Error();return false;}
    Pending.Canonical=std::move(Reader.Canonical);Out=std::move(Pending);Error={};return true;
}
}}
