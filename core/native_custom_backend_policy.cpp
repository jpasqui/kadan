#include "native_custom_backend_policy.h"
#include "material_engine_hashes.h"
#include "native_custom_backend_template.h"
#include "KadanMaxwell/nvasm_program.h"
#include "custom_material_policy_bindings.h"
namespace P4MapKit { namespace CustomMaterial { namespace NativeBackend {
namespace {
using Operand=nvasm::detail::Operand;
bool PlainRegister(const Operand& Value){return Value.kind==Operand::Var&&Value.temp>=0&&!Value.indexed&&!Value.abs;}
bool SameScalar(const Operand& Read,const Operand& Written,bool Negative=false){
    return PlainRegister(Read)&&PlainRegister(Written)&&Read.name==Written.name&&Read.neg==Negative&&
        !Written.neg&&Written.suffix.size()==1&&(Read.suffix==Written.suffix||
        (Read.suffix.empty()&&Written.suffix=="x"));
}
bool SameRead(const Operand& Left,const Operand& Right){
    return PlainRegister(Left)&&PlainRegister(Right)&&Left.name==Right.name&&Left.suffix==Right.suffix&&
        Left.neg==Right.neg;
}
bool FloatInstruction(const nvasm::detail::Instr& Instruction,const char* Op){
    return Instruction.op==Op&&(Instruction.has("F32")||Instruction.has("F"));
}
bool IsControlGuard(const std::vector<nvasm::detail::Instr>& Code,std::size_t At){
    if(At+3>=Code.size())return false;
    const auto& Load=Code[At];const auto& Compare=Code[At+1];
    const auto& Convert=Code[At+2];const auto& Branch=Code[At+3];
    return FloatInstruction(Compare,"SGT")&&Compare.ops.size()==3&&
        SameScalar(Compare.ops[1],Load.ops[0])&&Compare.ops[2].kind==Operand::Const&&
        !Compare.ops[2].consts.empty()&&Compare.ops[2].consts[0]=="0"&&
        Convert.op=="TRUNC"&&Convert.has("U")&&Convert.has("CC")&&Convert.ops.size()==2&&
        SameScalar(Convert.ops[1],Compare.ops[0])&&Branch.op=="IF"&&Branch.ops.size()==1&&
        Branch.ops[0].kind==Operand::CCTest&&Branch.ops[0].name=="NE";
}
bool IsControlBlend(const std::vector<nvasm::detail::Instr>& Code,std::size_t At){
    const auto& Load=Code[At];const auto Limit=std::min(Code.size(),At+13);
    for(std::size_t MadAt=At+1;MadAt<Limit;++MadAt){
        const auto& Mad=Code[MadAt];
        if(Mad.op=="IF"||Mad.op=="ELSE"||Mad.op=="ENDIF")break;
        if(!FloatInstruction(Mad,"MAD")||Mad.ops.size()!=4)continue;
        const bool First=SameScalar(Mad.ops[1],Load.ops[0],true)&&SameRead(Mad.ops[2],Mad.ops[3]);
        const bool Second=SameScalar(Mad.ops[2],Load.ops[0],true)&&SameRead(Mad.ops[1],Mad.ops[3]);
        if(!First&&!Second)continue;
        for(std::size_t AddAt=MadAt+1;AddAt<Limit;++AddAt){
            const auto& Add=Code[AddAt];
            if(Add.op=="IF"||Add.op=="ELSE"||Add.op=="ENDIF")break;
            if(!FloatInstruction(Add,"ADD")||Add.ops.size()!=3)continue;
            if((SameScalar(Add.ops[1],Load.ops[0])&&SameScalar(Add.ops[2],Mad.ops[0]))||
                (SameScalar(Add.ops[2],Load.ops[0])&&SameScalar(Add.ops[1],Mad.ops[0])))return true;
        }
    }
    return false;
}
}
OpaqueDirectionalSourceProof ReadOpaqueDirectionalSourceProof(const Bytes& Payload,const FMaterialShaderReference& Ref){
    if(Ref.Frequency!=3||!KnownBasePass(Ref.TypeHash)||
        (Ref.VertexFactoryHash!=EngineHash::LocalVertexFactory&&Ref.VertexFactoryHash!=EngineHash::InstancedStaticMeshVertexFactory))
        throw std::runtime_error("opaque-directional-source: selected base-pass/factory reference unqualified");
    const auto View=Ref.UniformBufferHashes.find(0),Primitive=Ref.UniformBufferHashes.find(1);
    if(View==Ref.UniformBufferHashes.end()||View->second!=EngineHash::ViewUniformBuffer||
        Primitive==Ref.UniformBufferHashes.end()||Primitive->second!=0x60929d69929b2b59ull)
        throw std::runtime_error("opaque-directional-source: actual View/Primitive identities absent");
    const auto Bindings=ReadPixelBindings(Payload);(void)Lighting::ReadForwardPolicyBindings(Bindings);
    const auto Assembly=OriginalAssemblyMetadata(Payload,Bindings);
    const auto Parsed=nvasm::detail::parse(Assembly);
    if(Parsed.stage!=nvasm::Stage::Fragment||Parsed.code.size()>16384)
        throw std::runtime_error("opaque-directional-source: bounded original PS required");
    unsigned Loads=0,Blends=0,Guards=0;
    for(std::size_t At=0;At<Parsed.code.size();++At){
        const auto& Instruction=Parsed.code[At];
        for(std::size_t Arg=0;Arg<Instruction.ops.size();++Arg){
            const auto& Value=Instruction.ops[Arg];const auto Buffer=Parsed.cbuffers.find(Value.name);
            if(Value.kind!=Operand::Var||Buffer==Parsed.cbuffers.end()||Buffer->second!=0||!Value.indexed)continue;
            if(!Value.index_reg.empty())throw std::runtime_error("opaque-directional-source: relative View reads unqualified");
            if(Value.index!=2592)continue;
            if(Arg!=1||Instruction.op!="LDC"||Instruction.mods!=std::vector<std::string>{"F32"}||
                Instruction.ops.size()!=2||Value.neg||Value.abs||!PlainRegister(Instruction.ops[0])||Instruction.ops[0].suffix.size()!=1)
                throw std::runtime_error("opaque-directional-source: control load pattern unqualified");
            ++Loads;Blends+=IsControlBlend(Parsed.code,At);Guards+=IsControlGuard(Parsed.code,At);
        }
    }
    if(Loads&&(Loads!=3||Blends!=2||Guards!=1))
        throw std::runtime_error("opaque-directional-source: specular control blend/guard pattern unqualified");
    const Bytes Text(Assembly.begin(),Assembly.end());
    return OpaqueDirectionalSourceProof(Loads?OriginalOpaqueDirectionalResponse::SeparateSpecularShadow:
        OriginalOpaqueDirectionalResponse::SurfaceShadow,Hex(Sha256(Text)),Loads);
}
} } }
