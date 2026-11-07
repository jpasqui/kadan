#include "../core/material_scene.h"
#include <iostream>
using namespace P4MapKit;
using namespace P4MapKit::Material;
static Value Numeric(const Value&v,const Json&sample) {
    if(!symbolic(v))return v;const auto&node=*v.expression;const auto&a=node.args;
    if(node.op=="COMP") {
        const auto&base=*a.at(0).expression;int c=int(a.at(1).number());
        if(base.op=="SRC") {auto detail=base.args.at(1).kind==Value::Null?"":base.args.at(1).text;auto key=base.args.at(0).text+"|"+detail;return sample.At("sources").At(key).array.at(std::size_t(c)).Numeric();}
        if(base.op=="SCENETEX"||base.op=="SCENE") {
            double u=Numeric(base.args.at(1),sample).number(),w=Numeric(base.args.at(2),sample).number();
            const auto&kind=base.args.at(0);bool raw=base.op=="SCENETEX";
            bool depth=raw?kind.kind!=Value::Null&&kind.number()==sample.At("depth_slot").Numeric():kind.text=="Depth"||kind.text=="DeviceDepth";
            if(depth) {double z=sample.At("depth").Numeric()+17*u+29*w;return raw||kind.text=="DeviceDepth"?1/z:z;}
            double color=1.25+u*.7+w*.4+c*.13;
            if(raw)return std::pow(color*.1,.25);return color;
        }
        throw EvalError("numeric fixture component is unbound");
    }
    std::vector<Value> args;for(const auto&operand:a)args.push_back(Numeric(operand,sample));
    if(node.op=="SEL")return args.at(0).truth()?args.at(1):args.at(2);
    return numeric_operation(node.op,args);
}
static Value Atom(const Json&j,const std::map<std::string,Value>&nodes) {
    if(j.kind==Json::Null)return Value::null();if(j.kind==Json::String)return j.string;if(j.kind==Json::Boolean)return j.boolean;if(j.kind==Json::Number)return j.number;
    if(j.kind==Json::Array) { std::vector<Value> items;for(const auto&item:j.array)items.push_back(Atom(item,nodes));return Value::tuple_value(items); }
    if(j.Has("ref"))return nodes.at(j.At("ref").Text());
    if(j.Has("float_bits")) { auto bits=std::stoull(j.At("float_bits").Text(),nullptr,16);double value;std::memcpy(&value,&bits,8);return value; }
    if(j.Has("integer"))return Value(std::int64_t(std::stoll(j.At("integer").Text())));
    throw EvalError("bad fixture atom");
}
static Json Encode(const Value&v,std::map<const Expression*,std::string>&seen,Json&formulas) {
    if(v.kind==Value::Expr) {
        auto at=seen.find(v.expression.get());if(at==seen.end()) { Json args=Json::MakeArray();for(const auto&a:v.expression->args)args.array.push_back(Encode(a,seen,formulas));std::string id="X"+std::to_string(seen.size());seen[v.expression.get()]=id;Json item=Json::MakeObject();item["id"]=id;item["op"]=v.expression->op;item["args"]=args;formulas.array.push_back(item);at=seen.find(v.expression.get()); }
        Json ref=Json::MakeObject();ref["ref"]=at->second;return ref;
    }
    if(v.kind==Value::Null)return Json();if(v.kind==Value::Text)return Json(v.text);if(v.kind==Value::Boolean)return Json(v.boolean);
    if(v.kind==Value::Tuple) { Json items=Json::MakeArray();for(const auto&i:v.tuple)items.array.push_back(Encode(i,seen,formulas));return items; }
    Json value=Json::MakeObject();if(v.kind==Value::Int)value["integer"]=v.integer.decimal();else if(v.kind==Value::Float) { std::uint64_t bits;std::memcpy(&bits,&v.real,8);std::ostringstream text;text<<std::hex<<std::setw(16)<<std::setfill('0')<<bits;value["float_bits"]=text.str(); }else throw EvalError("bad output atom");return value;
}

int main() {
    std::string line;while(std::getline(std::cin,line)) {Json answer=Json::MakeObject();try {
        const auto request=Json::Parse(line);SymbolicArena arena;std::map<std::string,Value> nodes;
        for(const auto&f:request.At("formulas").array) {std::vector<Value> args;for(const auto&a:f.At("args").array)args.push_back(Atom(a,nodes));nodes[f.At("id").Text()]=arena.mk(f.At("op").Text(),args);}
        std::vector<Value> values;for(const auto&v:request.At("values").array)values.push_back(Atom(v,nodes));
        const auto input_values=values;SceneReadFactory factory;SceneRoles roles;SceneRewriteFacts facts;
        if(request.Has("roles"))for(const auto&role:request.At("roles").object)roles[role.first]=Atom(role.second,nodes);
        if(request.Has("factory")) {const auto&f=request.At("factory");factory.RolesProved=f.At("proved").boolean;factory.CopySampler=f.At("sampler").Text();for(const auto&role:f.At("roles").object)factory.TextureRoles[role.first]=Atom(role.second,nodes);}
        auto stage=request.At("stage").Text();
        if(stage=="own")values=PixelOwn(arena,values,&facts);
        else if(stage=="pixel_depth")values=PixelDepth(arena,values,&facts);
        else if(stage=="ordinary")values=SceneReadsOrdinary(arena,values,roles,&facts);
        else if(stage=="legacy")values=PixelDepth(arena,SceneReadsOrdinary(arena,PixelOwn(arena,values,&facts),roles,&facts),&facts);
        else if(stage=="copy")values=SceneCopyValues(arena,values,factory,roles,&facts);
        else if(stage=="device")values=DeviceDepthValues(arena,values,factory,&facts);
        else if(stage=="sun")values=SunDisk(arena,values,roles,&facts);
        else if(stage=="production")values=SceneReads(arena,values,factory,roles,&facts);
        else if(stage=="combined")values=SceneReads(arena,SunDisk(arena,values,roles,&facts),factory,roles,&facts);
        else throw EvalError("unknown scene fixture stage");
        Json formulas=Json::MakeArray(),outputs=Json::MakeArray(),role_values=Json::MakeObject(),counts=Json::MakeObject();std::map<const Expression*,std::string> seen;
        for(const auto&v:values)outputs.array.push_back(Encode(v,seen,formulas));for(const auto&role:roles)role_values[role.first]=Encode(role.second,seen,formulas);
        for(const auto&count:facts.replacements)counts[count.first]=double(count.second);
        answer["formulas"]=formulas;answer["values"]=outputs;answer["roles"]=role_values;answer["counts"]=counts;answer["inferred_color"]=facts.inferred_color;answer["requires_independent_sky_proof"]=facts.requires_independent_sky_proof;
        if(request.Has("sample")) {Json before=Json::MakeArray(),after=Json::MakeArray();for(const auto&v:input_values)before.array.push_back(Numeric(v,request.At("sample")).number());for(const auto&v:values)after.array.push_back(Numeric(v,request.At("sample")).number());answer["numeric_before"]=before;answer["numeric_after"]=after;}
    }catch(const Refused&e) {answer["refused"]=e.what();answer["refusal"]=e.name;}catch(const std::exception&e) {answer["refused"]=e.what();}std::cout<<answer.Dump()<<'\n';}return 0;
}
