#include "material_instance_binding_comparison.h"
#include "material_json.h"
#include <fstream>
#include <iostream>
namespace P=P4MapKit;namespace M=P4MapKit::Material;namespace B=M::InstanceBindingComparison;namespace C=P4MapKit::CustomMaterial;
using V=P::FDecodedValue;
static unsigned Checks=0;
static void Check(bool x){++Checks;if(!x)throw std::runtime_error("MIC projection control "+std::to_string(Checks)+" failed");}
static C::Bytes File(const std::string& path){std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error("missing explicit fixture");f.seekg(0,std::ios::end);const auto n=f.tellg();if(n<=0||n>64*1024*1024)throw std::runtime_error("fixture size bound");f.seekg(0);C::Bytes bytes(std::size_t(n),0);if(!f.read(reinterpret_cast<char*>(bytes.data()),n))throw std::runtime_error("fixture read");return bytes;}
static M::Json Json(const std::string& path){const auto bytes=File(path);return M::Json::Parse(std::string(bytes.begin(),bytes.end()));}
static std::string Digest(const std::vector<std::string>& lines){auto sorted=lines;std::sort(sorted.begin(),sorted.end());std::string text;for(const auto& l:sorted){if(!text.empty())text+='\n';text+=l;}return C::Hex(C::Sha256(C::ByteText(text)));}
static V CapturedGuid(const M::Json& json){V v;for(const char* key:{"a","b","c","d"}){const auto number=json.At(key).Numeric();if(number<0||number>UINT32_MAX||number!=std::floor(number))throw std::runtime_error("captured parent GUID malformed");v.Fields.emplace_back(std::string(1,char(key[0]-'a'+'A')),V::Atom(V::EKind::Number,std::to_string(std::int32_t(std::uint32_t(number)))));}return v;}
static std::size_t Line(const std::vector<std::string>& lines,const std::string& property){for(std::size_t i=0;i<lines.size();++i)if(lines[i].find(" | "+property+" = ")!=std::string::npos)return i;throw std::runtime_error("fixture line absent");}
template<class F>static void Edit(std::vector<std::string>& lines,const std::string& property,F f){auto& line=lines[Line(lines,property)];auto at=line.find(" = ")+3;auto value=B::Read(line.substr(at));f(value);line=line.substr(0,at)+P::DecodedJson(value);}
int main(int argc,char** argv){try{
    if(argc!=4)throw std::runtime_error("raw MIC uasset, exact semantic hypothesis JSON, parent-expression audit JSON required");
    auto header=File(argv[1]),body=File(std::string(argv[1]).substr(0,std::string(argv[1]).size()-7)+".uexp");const auto proof=Json(argv[2]),audit=Json(argv[3]);
    const auto package=audit.At("package").Text(),sha=proof.At("source_sha256").Text();std::vector<std::string> lines;for(const auto& l:proof.At("current_lines").array)lines.push_back(l.Text());
    Check(Digest(lines)==proof.At("current_native_sha256").Text());
    auto inventory=P::ReadPackage(header,body);Check(inventory.Error.Code.empty()&&inventory.Exports.size()==1);V source;P::FPropertyDecoder decoder(inventory,body);auto at=inventory.Exports[0].Offset;
    Check(decoder.TypedProperties(at,at+inventory.Exports[0].Size,source,0,"MaterialInstanceConstant"));const auto parent=B::Need(source,"Parent").Text;
    std::vector<M::MaterialInstanceParentBinding> frame;
    for(const auto& row:audit.At("parameter_binding_checks").array){
        if(!row.At("matches_parent").boolean)continue;
        std::string group;for(const char* candidate:{"ScalarParameterValues","VectorParameterValues","TextureParameterValues"})if(P::PropertyIdentity(candidate)==row.At("group").Text())group=candidate;
        const auto& original=B::Need(source,group.c_str()).Items.at(std::size_t(row.At("index").Numeric()));const auto& info=B::Need(original,"ParameterInfo");const auto& name=B::Need(info,"Name");
        Check(name.Text==row.At("parameter").Text()&&name.Kind==V::EKind::Name);
        const auto guid=CapturedGuid(row.At("current_guid"));unsigned matching=0;for(const auto& e:row.At("parent_expressions").array)if(P::DecodedJson(CapturedGuid(e.At("guid")))==P::DecodedJson(guid))++matching;
        Check(matching==1);frame.push_back({group,name.NameBase,name.NameNumber,P::LegacyDecodedValue(info),guid});
    }
    Check(frame.size()==12);std::vector<std::string> projected;std::size_t changed=0;std::string error;
    auto Project=[&](const std::vector<std::string>& input,const auto& frames,const std::string& owner,const std::string& digest,const auto& h,const auto& b){return M::ProjectOriginalMaterialInstanceBindingLines(package,digest,h,b,owner,frames,input,projected,changed,error);};
    Check(Project(lines,frame,parent,sha,header,body));Check(changed==12&&Digest(projected)==proof.At("recorded_sha256").Text());
    const auto originalProjection=projected;
    auto Refuses=[&](const auto& input,const auto& frames,const std::string& owner,const std::string& digest,const auto& h,const auto& b){projected={"unchanged sentinel"};Check(!Project(input,frames,owner,digest,h,b)&&projected==std::vector<std::string>{"unchanged sentinel"}&&!error.empty()&&changed==0);};
    Refuses(lines,frame,parent,std::string(64,'0'),header,body);
    Refuses(lines,frame,parent,sha,C::Bytes{},body);
    auto badHeader=header;badHeader[0]^=1;Refuses(lines,frame,parent,sha,badHeader,body);
    projected={"unchanged sentinel"};Check(!M::ProjectOriginalMaterialInstanceBindingLines(package+"_Other",sha,header,body,parent,frame,lines,projected,changed,error)&&projected==std::vector<std::string>{"unchanged sentinel"}&&!error.empty());
    Refuses(lines,frame,parent+"_Other",sha,header,body);
    auto bad=lines;Edit(bad,"parent",[](V& v){v.Text+="_Other";});Refuses(bad,frame,parent,sha,header,body);
    bad=lines;Edit(bad,"vectorparametervalues",[](V& v){B::Need(B::Need(v.Items.at(0),"ExpressionGUID"),"A").Text="123";});Refuses(bad,frame,parent,sha,header,body);
    bad=lines;Edit(bad,"scalarparametervalues",[](V& v){B::Need(v.Items.at(0),"ParameterValue").Text="2.0000000000000004";});Refuses(bad,frame,parent,sha,header,body);
    for(const char* field:{"Name","Association","Index"}){bad=lines;Edit(bad,"scalarparametervalues",[&](V& v){B::Need(B::Need(v.Items.at(0),"ParameterInfo"),field).Text=std::string(field)=="Index"?"0":"Changed";});Refuses(bad,frame,parent,sha,header,body);}
    bad=lines;Edit(bad,"scalarparametervalues",[](V& v){B::Need(v.Items.at(1),"ParameterInfo")=B::Need(v.Items.at(0),"ParameterInfo");});Refuses(bad,frame,parent,sha,header,body);
    bad=lines;Edit(bad,"textureparametervalues",[](V& v){B::Need(v.Items.at(0),"ParameterValue")=V::Atom(V::EKind::Null,"");});Refuses(bad,frame,parent,sha,header,body);
    auto badFrame=frame;badFrame.push_back(frame.at(0));Refuses(lines,badFrame,parent,sha,header,body);
    badFrame=frame;badFrame.at(0).Guid.Fields.at(0).second.Text="123";Refuses(lines,badFrame,parent,sha,header,body);
    badFrame=frame;badFrame.at(0).NameBase+="_Other";Refuses(lines,badFrame,parent,sha,header,body);
    badFrame=frame;badFrame.at(0).NameNumber+=1;Refuses(lines,badFrame,parent,sha,header,body);
    badFrame=frame;badFrame.erase(badFrame.begin());Refuses(lines,badFrame,parent,sha,header,body);
    badFrame=frame;badFrame.at(0).Group="TextureParameterValues";Refuses(lines,badFrame,parent,sha,header,body);
    for(const char* property:{"staticparameters","basepropertyoverrides"}){
        bad=lines;Edit(bad,property,[](V& v){v.Fields.emplace_back("CreatorChange",V::Atom(V::EKind::Boolean,"true"));});
        const auto edited=bad.at(Line(bad,property));Check(Project(bad,frame,parent,sha,header,body));Check(projected.at(Line(projected,property))==edited&&Digest(projected)!=proof.At("recorded_sha256").Text());
    }
    bad=lines;bad.push_back("MI_eft_Cmn_Awa00_alcfr | creatorfield = 7");Check(Project(bad,frame,parent,sha,header,body));Check(projected.back()==bad.back()&&Digest(projected)!=proof.At("recorded_sha256").Text());
    Check(Project(originalProjection,frame,parent,sha,header,body)&&changed==0&&projected==originalProjection);
    std::cout<<Checks<<" MIC comparison controls passed; 12 GUID-only projections reproduce exact original baseline; no renderer/baseline mutation\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
