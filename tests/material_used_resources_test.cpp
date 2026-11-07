#include "../core/material_used_resources.h"
#include "../core/native_custom_backend_archive.h"
#include <fstream>
#include <iostream>
namespace M=P4MapKit::Material;
namespace C=P4MapKit::CustomMaterial;
static int Checks=0;
static void Check(bool Value,const char* Why){++Checks;if(!Value)throw std::runtime_error(Why);}
template<class F>static void Refuses(F Action,const char* Why){bool Held=false;try{Action();}catch(const M::Refused&){Held=true;}Check(Held,Why);}
static C::Bytes File(const std::string& Name,std::uint64_t Bound)
{
    std::ifstream In(Name,std::ios::binary);if(!In)throw std::runtime_error("missing explicit input: "+Name);
    In.seekg(0,std::ios::end);const auto Size=In.tellg();if(Size<=0||std::uint64_t(Size)>Bound)throw std::runtime_error("input bound");
    In.seekg(0);C::Bytes Result(std::size_t(Size),0);if(!In.read(reinterpret_cast<char*>(Result.data()),Size))throw std::runtime_error("input read");return Result;
}
static M::Json Facts(const M::FMaterialUsedResourceDiscovery& Discovery)
{
    auto Result=M::Object({{"owner",Discovery.Owner},{"owner_source_sha256",Discovery.OwnerSourceSha256},{"resource_hash",Discovery.ResourceHash},
        {"graph_sha256",Discovery.GraphSha256},{"graph_nodes",double(Discovery.GraphNodes)},{"reachable_nodes",double(Discovery.ReachableNodes)},
        {"requests",M::Json::MakeArray()},{"archive_reads",M::Json::MakeArray()},{"sources",M::Json::MakeArray()},
        {"cached_missing_not_requested",M::Json::MakeArray()},{"renderer_proven",Discovery.RendererProven},{"production_admitted",Discovery.ProductionAdmitted}});
    for(const auto& Request:Discovery.Requests)
    {
        auto Row=M::Object({{"package",Request.Package},{"object",Request.Object},{"native_class",Request.NativeClass},{"source_available",Request.SourceAvailable},
            {"kind",Request.Kind==M::EMaterialUsedResourceKind::Texture?"Texture":"Collection"},
            {"source_sha256",Request.SourceSha256},{"sampler_type",Request.SamplerType},{"references",M::Json::MakeArray()},{"uses",M::Json::MakeArray()}});
        for(const auto& Reference:Request.References)Row["references"].array.push_back(M::Object({{"source",Reference.SourcePackage},{"sha256",Reference.SourceSha256},
            {"property",Reference.Property},{"object",Reference.Object},{"native_class",Reference.NativeClass},{"import_index",Reference.ImportIndex}}));
        for(const auto& Use:Request.Uses)Row["uses"].array.push_back(M::Object({{"node",Use.Node},{"type",Use.NodeType},{"parameter",Use.Parameter}}));
        Result["requests"].array.push_back(Row);
    }
    for(const auto& Program:Discovery.ArchiveReads)Result["archive_reads"].array.push_back(M::Object({{"index",double(Program.ArchiveIndex)},
        {"frequency",int(Program.Frequency)},{"payload_sha256",Program.PayloadSha256}}));
    for(const auto& Source:Discovery.Sources)Result["sources"].array.push_back(M::Object({{"package",Source.Package},
        {"header_sha256",Source.HeaderSha256},{"body_sha256",Source.BodySha256},{"source_sha256",Source.SourceSha256}}));
    for(const auto& Missing:Discovery.CachedMissingNotRequested)Result["cached_missing_not_requested"].array.push_back(Missing);
    return Result;
}
static void GraphControls(const std::string& Package,const M::MaterialOriginalImportRawResolver& Raw,
    const M::MaterialOriginalArchiveAccess& Archive,const std::string& Normal)
{
    M::MaterialUsedResourceDetail::FReader Reader(Raw);const auto* Source=Reader.Read(Package);
    const auto Resolve=Reader.Resolver();const auto Chain=M::ReadOriginalMaterialParentChain(Source->Asset,Resolve);
    const auto References=M::MaterialUsedResourceDetail::ChainReferences(Reader,Chain);
    const auto Owner=M::ReadOriginalMaterialOwnerSource(Source->Asset,Resolve,{});
    const auto Recovered=M::RecoverOriginalMaterialSourceExpressionGraph(Owner,Archive,M::MaterialDomainSourceScope::ReadOnlyExpressions);
    const auto Graph=Recovered.Primary.Material.Graph;std::size_t Reachable=0;
    const auto Original=M::MaterialUsedResourceDetail::GraphRequests(Graph,Owner,References,Reader,Reachable);
    Check(!Original.empty(),"graph controls consume a real used-resource owner");
    auto Extra=Graph;Extra["nodes"]["UnusedNormal"]=M::Object({{"type","TextureObject"},{"props",M::Object({{"Texture",Normal}})},{"inputs",M::Json::MakeObject()}});
    Extra["nodes"]["PathTextOnly"]=M::Object({{"type","Custom"},{"props",M::Object({{"Code","// "+Normal+"\nreturn 0;"},{"OutputType","CMOT_Float1"}})},{"inputs",M::Json::MakeObject()}});
    Extra["outputs"]["Metallic"]=M::Array({"PathTextOnly",""});
    const auto Without=M::MaterialUsedResourceDetail::GraphRequests(Extra,Owner,References,Reader,Reachable);
    Check(Without.size()==Original.size(),"unreachable typed texture and reachable HLSL path text do not become requests");
    Extra["outputs"]["Roughness"]=M::Array({"UnusedNormal","R"});
    const auto With=M::MaterialUsedResourceDetail::GraphRequests(Extra,Owner,References,Reader,Reachable);
    bool Found=false;for(const auto& Request:With)if(Request.Package==Normal)Found=!Request.SourceAvailable&&Request.NativeClass=="/Script/Engine.Texture2D"&&!Request.References.empty();
    Check(Found&&With.size()==Original.size()+1,"a genuinely reachable typed missing normal has an exact request; no blanket normal exclusion");
    Extra["nodes"]["UnusedNormal"]["props"]["Texture"]="/Game/Unbound/Texture";
    Refuses([&]{M::MaterialUsedResourceDetail::GraphRequests(Extra,Owner,References,Reader,Reachable);},"unbound graph resource never gains guessed identity");
    Extra["nodes"]["UnusedNormal"]["props"]["Texture"]=Normal;
    auto Conflict=References;bool Copied=false;for(const auto& Reference:References)if(M::MaterialSourcePackage(Reference.Object)==Normal)
    {auto Other=Reference;Other.NativeClass="/Script/Engine.TextureCube";Conflict.push_back(Other);Copied=true;break;}
    Check(Copied,"negative control binds a real cached normal source import");
    Refuses([&]{M::MaterialUsedResourceDetail::GraphRequests(Extra,Owner,Conflict,Reader,Reachable);},"conflicting source import classes refuse");
    Extra["nodes"]["UnusedNormal"]["type"]="TextureSampleParameterCube";
    Extra["nodes"]["UnusedNormal"]["props"]["SamplerSource"]="SSM_FromTextureAsset";
    Refuses([&]{M::MaterialUsedResourceDetail::GraphRequests(Extra,Owner,References,Reader,Reachable);},"cube consumer cannot coerce actual Texture2D identity");
    auto WrongSampler=Graph;bool Changed=false;for(auto& Node:WrongSampler["nodes"].object)if(Node.second["props"].Has("Texture"))
    {Node.second["props"]["SamplerType"]="SAMPLERTYPE_Normal";Changed=true;}
    Check(Changed,"negative sampler control changed actual texture node");
    Refuses([&]{M::MaterialUsedResourceDetail::GraphRequests(WrongSampler,Owner,References,Reader,Reachable);},"available source sampler mismatch refuses");
}
// Synthetic graph-boundary controls also run without private game inputs.
static void EngineResourceControls()
{
    const auto Graph=[](const std::string& Type,const std::string& Texture)
    {
        return M::Object({{"format",2},{"domain","MD_Surface"},{"blend_mode","BLEND_Opaque"},{"shading_model","MSM_Unlit"},
            {"two_sided",false},{"tangent_space_normal",true},{"clip",0.333},
            {"nodes",M::Object({{"sample",M::Object({{"type",Type},{"props",M::Object({{"Texture",Texture},
                {"SamplerSource","SSM_FromTextureAsset"}})},{"inputs",M::Json::MakeObject()}})}})},
            {"outputs",M::Object({{"EmissiveColor",M::Array({"sample","RGB"})}})}});
    };
    const auto Reference=[](const std::string& Object,const std::string& Class)
    {M::FMaterialUsedResourceReference Value;Value.Object=Object;Value.NativeClass=Class;return Value;};
    std::size_t SourceReads=0;
    const auto Discover=[&](const std::string& Type,const std::string& Texture,const std::vector<M::FMaterialUsedResourceReference>& References)
    {
        M::MaterialOriginalOwnerSource Owner;
        M::MaterialUsedResourceDetail::FReader Reader([&](const std::string&,M::MaterialOriginalImportRawAsset&){++SourceReads;return false;});
        std::size_t Reachable=0;
        return M::MaterialUsedResourceDetail::GraphRequests(Graph(Type,Texture),Owner,References,Reader,Reachable);
    };
    const auto Reject=[&](const std::string& Type,const std::string& Texture,const std::vector<M::FMaterialUsedResourceReference>& References,
        const std::string& Category,const char* Why)
    {
        bool Rejected=false;
        try{Discover(Type,Texture,References);}
        catch(const M::Refused& Error){Rejected=Error.name==Category;}
        Check(Rejected,Why);
    };
    for(const std::string Prefix:{"/Engine/ResourceControls/","/Game/ResourceControls/"})
    {
        const bool Engine=Prefix=="/Engine/ResourceControls/";
        const std::string Cube=Prefix+"Cube.Cube",Flat=Prefix+"Flat.Flat";
        const auto CubeReference=Reference(Cube,"/Script/Engine.TextureCube"),FlatReference=Reference(Flat,"/Script/Engine.Texture2D");
        SourceReads=0;
        const auto Cubes=Discover("TextureSampleParameterCube",Cube,{CubeReference});
        Check(Cubes.size()==(Engine?0u:1u),"valid cube retains game request and exempts engine request");
        Check(SourceReads==(Engine?0u:1u),"engine cube does not resolve a game import source");
        SourceReads=0;
        const auto Flats=Discover("TextureSampleParameter2D",Flat,{FlatReference});
        Check(Flats.size()==(Engine?0u:1u),"valid 2D texture retains game request and exempts engine request");
        Check(SourceReads==(Engine?0u:1u),"engine 2D texture does not resolve a game import source");
        Reject("TextureSampleParameter2D",Cube,{CubeReference},"material used resource class","2D consumer cannot coerce an engine or game cube");
        Reject("TextureSampleParameterCube",Flat,{FlatReference},"material used resource class","cube consumer cannot coerce an engine or game 2D texture");
        Reject("ParticleSubUV",Cube,{CubeReference},"material used resource class","SubUV consumer cannot coerce an engine or game cube");
        Reject("TextureSampleParameterCube",Cube,{CubeReference,Reference(Cube,"/Script/Engine.Texture2D")},
            "material used resource ambiguous","engine and game imports both reject conflicting classes");
        Reject("TextureSampleParameterCube",M::MaterialSourcePackage(Cube),{CubeReference,Reference(Prefix+"Cube.Other","/Script/Engine.TextureCube")},
            "material used resource ambiguous","engine and game package selections both reject conflicting objects");
        Reject("TextureSampleParameterCube",Prefix+"Cube.Other",{CubeReference},"material used resource unbound","full object selection needs its exact typed reference");
        Reject("TextureSampleParameterCube",Cube,{},"material used resource unbound","engine and game textures both require a typed source reference");
        Reject("TextureSampleParameterCube",Cube,{Reference(Cube,"/Script/Engine.StaticMesh")},"material used resource unbound","a nontexture import cannot bind an engine or game texture");
        const std::string Invalid=Prefix+"Bad Name.Bad";
        Reject("TextureSampleParameterCube",Invalid,{Reference(Invalid,"/Script/Engine.TextureCube")},"material used resource identity","engine and game paths both retain identity syntax checks");
        Check(Discover("TextureSampleParameterCube",M::MaterialSourcePackage(Cube),{CubeReference}).size()==(Engine?0u:1u),
            "an unambiguous package selection still binds its typed object");
        Check(Discover("TextureSampleParameterCube",Cube,{CubeReference,CubeReference}).size()==(Engine?0u:1u),
            "repeated identical typed source references remain valid");
    }
}
int main(int Argc,char** Argv){try{
    if(Argc==2&&std::string(Argv[1])=="--engine-resources-only")
    {EngineResourceControls();std::cout<<"PASS "<<Checks<<" engine/game resource boundary checks\n";return 0;}
    if(Argc!=4)throw std::runtime_error("archive, explicit original corpus root, one output JSON required");
    EngineResourceControls();
    const auto ArchiveBytes=File(Argv[1],1ull<<31);const auto ArchiveSha=C::Hex(C::Sha256(ArchiveBytes));
    Check(ArchiveSha=="04a142f5025e51f8ac072d7fce3033706fc18de5ebc07072f51d658cf861792f","exact existing original archive bytes");
    C::NativeBackend::ShaderArchive Archive(ArchiveBytes);M::MaterialOriginalArchiveAccess Access;
    Access.MapIndices=[&](const std::array<std::uint8_t,20>& Hash){return Archive.Map(C::Bytes(Hash.begin(),Hash.end()));};
    Access.Payload=[&](std::uint32_t Index){return Archive.Payload(Index);};Access.Frequency=[&](std::uint32_t Index){return std::uint8_t(Archive.Frequency(Index));};
    const std::string Root=Argv[2],MTexture="/Game/Carrot4/MasterMaterial/Texture/T_Base_M",DTexture="/Game/Carrot4/MasterMaterial/Texture/T_Base_D",NTexture="/Game/Carrot4/MasterMaterial/Texture/T_Base_N";
    const std::vector<std::string> Owners={
        "/Game/Carrot4/MasterMaterial/chr/M_chr_Shadow_Master",
        "/Game/Carrot4/Characters/Teki/DiscoDamagumo/MI_chr_DiscoDamagumo_00_Shadow",
        "/Game/Carrot4/Characters/Teki/HageDamagumo/MI_chr_HageDamagumo_00_Shadow",
        "/Game/Carrot4/MasterMaterial/chr/MI_chr_Shadow_Dissolve",
        "/Game/Carrot4/MasterMaterial/Unique/Silhouette/M_Silhouette_00_Master",
        "/Game/Carrot4/Characters/Teki/Dodoro/MI_chr_Dodoro_00_Silhouette",
        "/Game/Carrot4/MasterMaterial/Unique/Silhouette/MI_Silhouette_Object_00"};
    std::set<std::string> Allowed(Owners.begin(),Owners.end());
    for(const auto& Package:{MTexture,DTexture,std::string("/Game/Carrot4/MasterMaterial/chr/MI_chr_Shadow"),
        std::string("/Game/Carrot4/MasterMaterial/Unique/Silhouette/MI_Silhouette_Player_00"),
        std::string("/Game/Carrot4/MasterMaterial/Unique/Silhouette/MI_Silhouette_Teki_00")})Allowed.insert(Package);
    Check(Allowed.size()==12,"exact twelve-source universe only");
    std::map<std::string,M::MaterialOriginalImportRawAsset> Cached;
    M::MaterialOriginalImportRawResolver Raw=[&](const std::string& Package,M::MaterialOriginalImportRawAsset& Out)
    {
        if(!Allowed.count(Package))return false;auto Prior=Cached.find(Package);
        if(Prior==Cached.end())
        {
            const auto Stem=Root+"/Carrot4/Content/"+Package.substr(6);
            Prior=Cached.emplace(Package,M::MaterialOriginalImportRawAsset{std::make_shared<const C::Bytes>(File(Stem+".uasset",64u*1024u*1024u)),
                std::make_shared<const C::Bytes>(File(Stem+".uexp",64u*1024u*1024u))}).first;
        }
        Out=Prior->second;return true;
    };
    auto Report=M::Object({{"archive_sha256",ArchiveSha},{"owners",M::Json::MakeArray()},{"missing_source_owners",M::Json::MakeArray()}});
    std::set<std::string> Union;bool HadUnrequestedNormal=false,HadMissingRequests=false,CheckedGraph=false;
    for(const auto& Owner:Owners)
    {
        const auto Complete=M::DiscoverOriginalMaterialUsedResources(Owner,Raw,Access);
        if(Complete.Status!=M::EMaterialUsedResourceStatus::Complete)throw std::runtime_error(Owner+": "+Complete.RefusalCategory+": "+Complete.Refusal);
        Check(Complete.OwnerSourceSha256.size()==64&&Complete.ResourceHash.size()==40&&Complete.GraphSha256.size()==64&&!Complete.ArchiveReads.empty(),"actual source/graph/program signature retained");
        Check(!Complete.RendererProven&&!Complete.ProductionAdmitted,"discovery never admits Nintendo renderer or stock SM5");
        for(const auto& Request:Complete.Requests)
        {
            Check(Request.Package==MTexture||Request.Package==DTexture,"only actual reachable M/D resources requested");
            Check(Request.Kind==M::EMaterialUsedResourceKind::Texture&&Request.NativeClass=="/Script/Engine.Texture2D"&&Request.SourceAvailable&&Request.SourceSha256.size()==64,"actual typed available texture identity");
            Check(Request.Owner==Owner&&Request.OwnerSourceSha256==Complete.OwnerSourceSha256&&Request.GraphSha256==Complete.GraphSha256&&Request.ResourceHash==Complete.ResourceHash,"request bound to actual owner/graph/resource map");
            Check(!Request.References.empty()&&!Request.Uses.empty(),"real source property/import and graph consumers retained");
            for(const auto& Reference:Request.References)Check(Reference.ImportIndex<0&&Reference.SourceSha256.size()==64&&!Reference.Property.empty()&&Reference.Object==Request.Object&&Reference.NativeClass==Request.NativeClass,"exact source import witness");
            Union.insert(Request.Package);
        }
        HadUnrequestedNormal|=std::find(Complete.CachedMissingNotRequested.begin(),Complete.CachedMissingNotRequested.end(),NTexture)!=Complete.CachedMissingNotRequested.end();
        Report["owners"].array.push_back(Facts(Complete));
        if(!CheckedGraph&&!Complete.Requests.empty()){GraphControls(Owner,Raw,Access,NTexture);CheckedGraph=true;}
        Allowed.erase(MTexture);Allowed.erase(DTexture);
        const auto Missing=M::DiscoverOriginalMaterialUsedResources(Owner,Raw,Access);
        Allowed.insert(MTexture);Allowed.insert(DTexture);
        if(Missing.Status!=M::EMaterialUsedResourceStatus::Complete)throw std::runtime_error("missing-source discovery: "+Owner+": "+Missing.RefusalCategory+": "+Missing.Refusal);
        Check(Missing.Requests.size()==Complete.Requests.size(),"missing sampler metadata does not hide named used inputs");
        for(std::size_t Index=0;Index<Missing.Requests.size();++Index)
        {
            const auto& A=Missing.Requests[Index];const auto& B=Complete.Requests[Index];
            Check(!A.SourceAvailable&&A.SourceSha256.empty()&&A.SamplerType.empty()&&A.Package==B.Package&&A.Object==B.Object&&A.NativeClass==B.NativeClass,"exact typed absent source request without guessed sampler");HadMissingRequests=true;
        }
        Report["missing_source_owners"].array.push_back(Facts(Missing));
    }
    Check(Union==std::set<std::string>{MTexture,DTexture}&&HadMissingRequests&&HadUnrequestedNormal,"complete two-resource union; optimized-out missing N never scheduled");
    Check(CheckedGraph,"actual graph controls executed");
    const auto MissingOwner=M::DiscoverOriginalMaterialUsedResources("/Game/NoOwner",Raw,Access);
    Check(MissingOwner.Status==M::EMaterialUsedResourceStatus::Incomplete&&MissingOwner.Requests.empty()&&!MissingOwner.RefusalCategory.empty(),"early unsupported/missing source has no fake completion or requests");
    const auto Owner=Owners.front();M::MaterialOriginalImportRawAsset OwnerBytes;Check(Raw(Owner,OwnerBytes),"tamper controls use retained actual owner");
    auto BrokenHeader=std::make_shared<C::Bytes>(*OwnerBytes.Header);BrokenHeader->at(0)^=255;
    const M::MaterialOriginalImportRawResolver Corrupt=[&](const std::string& Package,M::MaterialOriginalImportRawAsset& Out)
    {if(Package!=Owner)return Raw(Package,Out);Out={BrokenHeader,OwnerBytes.Body};return true;};
    const auto Invalid=M::DiscoverOriginalMaterialUsedResources(Owner,Corrupt,Access);
    Check(Invalid.Status==M::EMaterialUsedResourceStatus::Incomplete&&Invalid.Requests.empty(),"invalid existing source refuses rather than becoming an acquisition request");
    auto Changing=Access;unsigned Reads=0;Changing.MapIndices=[&](const std::array<std::uint8_t,20>& Hash)
    {auto Indices=Access.MapIndices(Hash);if(++Reads>1)Indices.push_back(0);return Indices;};
    const auto Changed=M::DiscoverOriginalMaterialUsedResources(Owner,Raw,Changing);
    Check(Reads>1&&Changed.Status==M::EMaterialUsedResourceStatus::Incomplete&&Changed.Requests.empty()&&Changed.RefusalCategory=="material used resource archive","changed archive map refuses without partial requests");
    M::MaterialOriginalImportRawAsset DifferentTexture;Check(Raw(DTexture,DifferentTexture),"texture conflict uses real other original source");
    const M::MaterialOriginalImportRawResolver WrongTexture=[&](const std::string& Package,M::MaterialOriginalImportRawAsset& Out)
    {if(Package!=MTexture)return Raw(Package,Out);Out=DifferentTexture;return true;};
    const auto ConflictingTexture=M::DiscoverOriginalMaterialUsedResources(Owners.at(3),WrongTexture,Access);
    Check(ConflictingTexture.Status==M::EMaterialUsedResourceStatus::Incomplete&&ConflictingTexture.Requests.empty(),"present wrong resource package refuses instead of falling through to missing acquisition");
    Report["checks"]=Checks;Report["passed"]=true;Report["scope"]="Named-resource source-only discovery; no native/render/fixed-missing/MPC-missing completion claim";
    std::ofstream Out(Argv[3],std::ios::binary);const auto Text=Report.Dump();if(!Out.write(Text.data(),std::streamsize(Text.size())))throw std::runtime_error("receipt write");
    std::cout<<"PASS "<<Checks<<" checks / 7 actual owners / exact 2 used textures; missing N excluded\n";return 0;
}catch(const std::exception& Error){std::cerr<<Error.what()<<"\n";return 1;}}
