// Ten bounded retained source pairs, not a historical registry re-read or UE job.
#include "../core/material_family_source_validation.h"
#include <fstream>
#include <iostream>
#include <limits>
namespace M=P4MapKit::Material;
namespace C=P4MapKit::CustomMaterial;
static int Checks=0;
static void Check(bool Value,const char* Why){++Checks;if(!Value)throw std::runtime_error(Why);}
static std::shared_ptr<const C::Bytes> File(const std::string& Name)
{
    std::ifstream In(Name,std::ios::binary);if(!In)throw std::runtime_error("missing source: "+Name);
    In.seekg(0,std::ios::end);const auto Size=In.tellg();if(Size<=0||Size>64*1024*1024)throw std::runtime_error("source bound");
    auto Data=std::make_shared<C::Bytes>(std::size_t(Size),0);In.seekg(0);
    if(!In.read(reinterpret_cast<char*>(Data->data()),Size))throw std::runtime_error("source read");return Data;
}
static M::FMaterialParentRow Row(const std::string& Package,const std::string& Parent={},const std::string& ParentClass={})
{
    const auto Object=[](const std::string& P){return P+"."+P.substr(P.find_last_of('/')+1);};
    return {Package,Object(Package),Parent.empty()?"Material":"MaterialInstanceConstant",Parent,Parent.empty()?"":Object(Parent),ParentClass};
}
int main(int Argc,char** Argv){try{
    if(Argc!=2)throw std::runtime_error("one explicit original source corpus root required");
    const std::string Root=Argv[1];
    const std::string Shadow="/Game/Carrot4/MasterMaterial/chr/M_chr_Shadow_Master";
    const std::string Dissolve="/Game/Carrot4/MasterMaterial/chr/MI_chr_Shadow_Dissolve";
    const std::string Silhouette="/Game/Carrot4/MasterMaterial/Unique/Silhouette/M_Silhouette_00_Master";
    const std::string Teki="/Game/Carrot4/MasterMaterial/Unique/Silhouette/MI_Silhouette_Teki_00";
    // Exact independent retained-registry Parent rows supplied by its parser
    // proof. They are test expectations, never selected-game product authority.
    const std::vector<M::FMaterialRegistryFamily> Families={
        {Row(Shadow),{Row(Shadow),Row("/Game/Carrot4/MasterMaterial/chr/MI_chr_Shadow",Shadow,"Material"),Row(Dissolve,Shadow,"Material"),
            Row("/Game/Carrot4/Characters/Teki/DiscoDamagumo/MI_chr_DiscoDamagumo_00_Shadow",Dissolve,"MaterialInstanceConstant"),
            Row("/Game/Carrot4/Characters/Teki/HageDamagumo/MI_chr_HageDamagumo_00_Shadow",Dissolve,"MaterialInstanceConstant")}},
        {Row(Silhouette),{Row(Silhouette),Row("/Game/Carrot4/Characters/Teki/Dodoro/MI_chr_Dodoro_00_Silhouette",Teki,"MaterialInstanceConstant"),
            Row("/Game/Carrot4/MasterMaterial/Unique/Silhouette/MI_Silhouette_Object_00",Silhouette,"Material"),
            Row("/Game/Carrot4/MasterMaterial/Unique/Silhouette/MI_Silhouette_Player_00",Silhouette,"Material"),Row(Teki,Silhouette,"Material")}}};
    std::map<std::string,M::FMaterialFamilyRawSource> Sources;
    for(const auto& Family:Families)for(const auto& Member:Family.Members)
    {
        const auto Stem=Root+"/Carrot4/Content/"+Member.Package.substr(6);
        Sources.emplace(Member.Package,M::FMaterialFamilyRawSource{File(Stem+".uasset"),File(Stem+".uexp")});
    }
    auto Resolve=[&](const std::string& P,M::FMaterialFamilyRawSource& Out){const auto I=Sources.find(P);if(I==Sources.end())return false;Out=I->second;return true;};
    for(std::size_t F=0;F<Families.size();++F)
    {
        const auto& Family=Families[F];M::FMaterialFamilySourceWitness Witness;P4MapKit::FReadError Error;
        const auto Valid=[&](const M::FMaterialRegistryFamily& Value){return M::ValidateMaterialFamilySources(Value,Resolve,Witness,Error);};
        if(!Valid(Family))throw std::runtime_error(Error.Code);
        Check(Witness.Members.size()==5&&Witness.Owners.size()==(F?3u:4u),"actual five-member / four-three owner coverage");
        Check(Witness.SourceSetSha256.size()==64&&Witness.SourceBytes>0,"complete source-set binding");
        for(const auto& Member:Witness.Members)
        {
            Check(Member.second.Row.Package==Member.first&&!Member.second.Owner.empty(),"every member has actual source owner");
            if(Member.second.OwnsShaders)Check(Member.second.ShaderReferences>0&&Member.second.ResourceHash.size()==40,"every owner has real structurally parsed resource");
        }
        const auto Digest=Witness.SourceSetSha256;
        // The staging operation validates only each newly converted member.
        // It calls this exact primitive too when finally proving the full set.
        std::size_t Available=0,LocalCalls=0;
        for(const auto& Row:Family.Members)
        {
            M::FMaterialFamilySourceMember Local;const auto& Raw=Sources.at(Row.Package);
            Check(M::ValidateMaterialFamilySourceMember(Row,Raw,Available,Local,Error),"each exact registry member is locally valid");++LocalCalls;
            const auto& Full=Witness.Members.at(Row.Package);
            Check(M::MaterialFamilySourceDetail::SameRow(Local.Row,Full.Row)&&Local.HeaderSha256==Full.HeaderSha256&&
                Local.BodySha256==Full.BodySha256&&Local.SourceSha256==Full.SourceSha256&&Local.OwnsShaders==Full.OwnsShaders&&
                Local.ResourceHash==Full.ResourceHash&&Local.ShaderReferences==Full.ShaderReferences&&Local.ExportIndex==Full.ExportIndex,
                "local package/Parent/owner proof is identical to the complete validator's member proof");
        }
        Check(LocalCalls==Family.Members.size()&&Available==Witness.SourceBytes,"one local call per member and same aggregate byte budget");
        std::size_t Seeded=0;
        for(std::size_t I=0;I<2;++I){const auto& Raw=Sources.at(Family.Members[I].Package);Seeded+=Raw.Header->size()+Raw.Body->size();}
        for(std::size_t I=2;I<Family.Members.size();++I){M::FMaterialFamilySourceMember Local;
            Check(M::ValidateMaterialFamilySourceMember(Family.Members[I],Sources.at(Family.Members[I].Package),Seeded,Local,Error),"new members charge an existing validated-source budget");}
        Check(Seeded==Witness.SourceBytes,"registered/cache source bytes are included before conversion");
        const auto& Raw=Sources.at(Family.Members.back().Package);const auto PairBytes=Raw.Header->size()+Raw.Body->size();
        constexpr std::size_t Limit=128u*1024u*1024u;M::FMaterialFamilySourceMember Local;
        std::size_t Budget=Limit-PairBytes;
        Check(M::ValidateMaterialFamilySourceMember(Family.Members.back(),Raw,Budget,Local,Error)&&Budget==Limit,"exact aggregate boundary accepted");
        Check(!M::ValidateMaterialFamilySourceMember(Family.Members.back(),Raw,Budget,Local,Error)&&Budget==Limit&&Local.Row.Package.empty(),"next member refuses with no budget charge/partial output");
        Budget=std::numeric_limits<std::size_t>::max();
        Check(!M::ValidateMaterialFamilySourceMember(Family.Members.back(),Raw,Budget,Local,Error)&&Budget==std::numeric_limits<std::size_t>::max()&&Local.Row.Package.empty(),"aggregate arithmetic cannot wrap");
        Budget=0;auto WrongRow=Family.Members.back();WrongRow.ParentObject+="_Wrong";
        Check(!M::ValidateMaterialFamilySourceMember(WrongRow,Raw,Budget,Local,Error)&&Budget==0&&Local.Row.Package.empty(),"bad newly converted Parent refuses immediately without partial admission");
        auto PartialRaw=Raw;PartialRaw.Body.reset();
        Check(!M::ValidateMaterialFamilySourceMember(Family.Members.back(),PartialRaw,Budget,Local,Error)&&Budget==0&&Local.Row.Package.empty(),"partial newly converted bundle refuses immediately");
        Check(!M::ValidateMaterialFamilySourceMember(Family.Members.front(),Raw,Budget,Local,Error)&&Budget==0&&Local.Row.Package.empty(),"wrong canonical source package refuses local binding");
        if(F==0)
        {
            // Every surviving row is locally correct, yet ancestry is incomplete.
            // Only the final unchanged closure proof may publish a witness.
            auto Incomplete=Family;Incomplete.Members.erase(Incomplete.Members.begin()+2);Budget=0;
            for(const auto& Row:Incomplete.Members)Check(M::ValidateMaterialFamilySourceMember(Row,Sources.at(Row.Package),Budget,Local,Error),"locally valid orphan does not imply complete family");
            Check(!Valid(Incomplete)&&Witness.Members.empty()&&Error.Code=="material-family-source-parent-outside-closure","final full validation rejects missing intermediate Parent with no witness");
            Check(Valid(Family)&&Witness.SourceSetSha256==Digest,"full exact final proof is preserved after local validation");
        }
        auto Changed=Family;std::reverse(Changed.Members.begin(),Changed.Members.end());
        Check(Valid(Changed)&&Witness.SourceSetSha256==Digest,"source-set identity independent of caller order");
        Changed=Family;Changed.Members.push_back(Family.Members[1]);
        Check(!Valid(Changed)&&Witness.Members.empty(),"duplicate source member refuses without partial publication");
        Changed=Family;Changed.Members[1].Package[1]='g';
        Check(!Valid(Changed),"case-alias package refuses");
        Changed=Family;Changed.Members[1].Object+="_Wrong";
        Check(!Valid(Changed),"wrong actual export object refuses");
        Changed=Family;Changed.Members[1].Class="Material";Changed.Members[1].ParentPackage.clear();Changed.Members[1].ParentObject.clear();Changed.Members[1].ParentClass.clear();
        Check(!Valid(Changed),"wrong material root/class refuses");
        Changed=Family;Changed.Members.back().ParentPackage=Family.Master.Package;Changed.Members.back().ParentObject=Family.Master.Object;Changed.Members.back().ParentClass="Material";
        if(Changed.Members.back().ParentObject==Family.Members.back().ParentObject)Changed.Members.back().ParentObject+="_Wrong";
        Check(!Valid(Changed),"registry Parent must match actual H+B exactly");
        Changed=Family;Changed.Members.back().ParentClass="Texture2D";
        Check(!Valid(Changed),"unsupported parent class refuses");
        const auto Missing=Sources.at(Family.Members[2].Package);Sources.erase(Family.Members[2].Package);
        Check(!Valid(Family)&&Error.Code=="material-family-source-missing"&&Witness.Members.empty(),"missing source refuses complete family publication");
        Sources.emplace(Family.Members[2].Package,Missing);
        const auto Original=Sources.at(Family.Master.Package);
        auto Header=std::make_shared<C::Bytes>(*Original.Header);(*Header)[0]^=1;Sources[Family.Master.Package].Header=Header;
        Check(!Valid(Family),"corrupt package header refuses");Sources[Family.Master.Package]=Original;
        const auto Package=P4MapKit::ReadPackage(*Original.Header,*Original.Body);
        P4MapKit::FMaterialResourceBoundary Boundary;
        Check(P4MapKit::DiscoverMaterialResourceBoundary(Package,*Original.Body,0,Boundary,Error),"actual resource boundary exists");
        auto Body=std::make_shared<C::Bytes>(*Original.Body);(*Body)[Boundary.ResourceStart]=0;Sources[Family.Master.Package].Body=Body;
        Check(!Valid(Family)&&Witness.Members.empty(),"missing real shader owner envelope refuses");
        Budget=0;Check(!M::ValidateMaterialFamilySourceMember(Family.Master,Sources.at(Family.Master.Package),Budget,Local,Error)&&Budget==0&&Local.Row.Package.empty(),
            "local new-member proof rejects the same missing real shader owner without charging/publishing");Sources[Family.Master.Package]=Original;
        Changed=Family;Changed.Members.erase(Changed.Members.begin());
        Check(!Valid(Changed),"missing declared root refuses");
        Check(Valid(Family)&&Witness.SourceSetSha256==Digest,"all exact sources restored without writes");
        std::cout<<Family.Master.Package<<" members="<<Witness.Members.size()<<" owners="<<Witness.Owners.size()
            <<" bytes="<<Witness.SourceBytes<<" source_set_sha256="<<Witness.SourceSetSha256<<"\n";
    }
    std::cout<<"PASS "<<Checks<<" actual-source identity/Parent/owner/source-set controls; no current-game or renderer claim\n";
    return 0;
}catch(const std::exception& Error){std::cerr<<Error.what()<<"\n";return 1;}}
