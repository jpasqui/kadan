// JSON-lines differential test boundary; not an editor/runtime launcher.
#include "../core/material_symbolic.h"
#include "../core/material_json.h"
#include <iostream>
using namespace P4MapKit::Material;
static Value Decode(const Json&j) {
    std::string type=j.At("kind").Text(),s=j.At("value").Text();
    if(type=="float") { std::uint64_t bits=std::stoull(s,nullptr,16);double n;std::memcpy(&n,&bits,8);return n; }
    if(type=="bool")return s=="true";
    if(type=="int") { Integer n;bool negative=!s.empty()&&s[0]=='-';for(std::size_t i=negative?1:0;i<s.size();++i) { if(s[i]<'0'||s[i]>'9')throw EvalError("bad integer");n=n*Integer(10)+Integer(s[i]-'0'); }return negative?n.negated():n; }
    throw EvalError("bad test value");
}
static Json Encode(const Value&v) {
    Json j=Json::MakeObject();if(v.kind==Value::Float) { j["kind"]="float";std::uint64_t bits;std::memcpy(&bits,&v.real,8);std::ostringstream s;s<<std::hex<<std::setw(16)<<std::setfill('0')<<bits;j["value"]=s.str(); }
    else if(v.kind==Value::Int) { j["kind"]="int";j["value"]=v.integer.decimal(); }
    else if(v.kind==Value::Boolean) { j["kind"]="bool";j["value"]=v.boolean?"true":"false"; }
    else if(v.kind==Value::Handle) { j["kind"]="handle";Json items=Json::MakeArray();for(auto w:v.handle) { Json p=Json::MakeArray();p.array={Json(w.first),Json(w.second)};items.array.push_back(p); }j["value"]=items; }
    else throw EvalError("bad output value");return j;
}
static Json EncodeVector(const Vec&v) { Json j=Json::MakeArray();for(const auto&a:v)j.array.push_back(Encode(a));return j; }
static NumericEnv Environment(const Json&j) {
    NumericEnv e;if(j.Has("position"))for(int c=0;c<4;++c)e.position[c]=Decode(j.At("position").array.at(c));
    if(j.Has("facing"))e.facing=Decode(j.At("facing"));
    if(j.Has("attribs"))for(const auto&a:j.At("attribs").object) { Vec v=splat(0.0);for(std::size_t c=0;c<a.second.array.size()&&c<4;++c)v[c]=Decode(a.second.array[c]);e.attribs[std::stoi(a.first)]=v; }
    if(j.Has("buffers"))for(const auto&b:j.At("buffers").object)for(const auto&w:b.second.object)e.buffers[std::stoi(b.first)][std::stoi(w.first)]=Decode(w.second);
    e.texture=[](int t,int s,const std::array<double,2>&uv)->Vec { return {{uv[0],uv[1],double(t)/1024.0,double(s)/2048.0}}; };
    e.other=[](const std::string&op,const std::string&target,int t,int s,const Vec&v)->Vec { return {{v[2],v[3],double(t+int(op.size()))/1024.0,double(s+int(target.size()))/2048.0}}; };
    return e;
}
int main() {
    std::string line;while(std::getline(std::cin,line)) { Json answer=Json::MakeObject();try { Json query=Json::Parse(line);auto command=query.At("command").Text();
        if(command=="operation") { std::vector<Value> args;for(const auto&j:query.At("args").array)args.push_back(Decode(j));answer["result"]=Encode(numeric_operation(query.At("op").Text(),args)); }
        else if(command=="program") { auto lines=instructions(query.At("program").Text());auto env=Environment(query);std::size_t stop=query.Has("stop")?std::size_t(query.At("stop").Numeric()):std::numeric_limits<std::size_t>::max();auto numeric=trace(lines,env,stop);auto arena=std::make_shared<SymbolicArena>();SymbolicMachine symbolic(lines,arena);if(query.Has("bounds"))for(const auto&b:query.At("bounds").array) { auto v=arena->mk("IN",{"attrib",int(b.array.at(0).Numeric()),int(b.array.at(1).Numeric())});symbolic.input_bounds[v.expression->id]=Bound(b.array.at(2).Numeric(),b.array.at(3).Numeric()); }symbolic.run(stop);Evaluation eval(env);Json nr=Json::MakeObject(),sr=Json::MakeObject();for(const auto&r:numeric.reg)nr[r.first]=EncodeVector(r.second);for(const auto&r:symbolic.reg)sr[r.first]=EncodeVector(eval.vector(r.second));answer["numeric"]=nr;answer["symbolic"]=sr;answer["killed"]=numeric.killed;answer["symbolic_killed"]=eval.killed(symbolic);answer["cc"]=EncodeVector(numeric.cc);answer["symbolic_cc"]=EncodeVector(eval.vector(symbolic.cc));answer["nodes"]=int(arena->table.size()); }
        else if(command=="deep_dag") { auto arena=std::make_shared<SymbolicArena>();auto v=arena->mk("IN",{"position",0,0});for(int i=0;i<int(query.At("count").Numeric());++i)v=arena->op("ADD",{v,1.0});arena.reset();answer["result"]=Encode(evaluate(v,NumericEnv())); }
        else throw EvalError("test-command");
        answer["ok"]=true;
    }catch(const std::exception&e) { answer["ok"]=false;answer["error"]=e.what(); }std::cout<<answer.Dump()<<'\n'; }
}
