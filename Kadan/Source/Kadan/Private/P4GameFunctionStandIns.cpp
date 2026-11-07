#include "P4GameFunctionStandIns.h"
#include "P4GameNames.h"
#include "UObject/Class.h"
#include "UObject/Script.h"
#include "UObject/Stack.h"
#include "UObject/UnrealType.h"
#include "UObject/WeakObjectPtr.h"

// P4Stubs declares the game's native functions with "return {}" bodies, because the editor has none of the
// game's runtime state: the type's zero value, or for FVector (whose default constructor sets nothing)
// whatever the return slot held. That is harmless for nearly every function. The engine also
// evaluates animation Blueprints in the editor, once whenever a skeletal mesh registers (a map opening, an
// actor being placed or imported). A scale input needs a finite, nondegenerate preview value even though
// the editor has no game's weak-point state. The stand-in supplies an identity scale for that preview;
// it does not implement the game's damage or morph behavior. Only the declared native function is changed
// in this editor, and it is put back when the
// module stops.
namespace
{
// NiseBaseAnimInstance::GetWeakPointBoneScale (game code 0x189B6F0) returns the scale slot of a weak
// point (anim+0x5E8[index]) or a zero vector. The game fills the slots when the weak points start
// (NiseBaseWeakPoints_Init) and changes them only as a weak point is damaged (NiseBaseMorph_Tick).
// ABP_NiseZako and ABP_NiseBoss multiply the weak point bone's scale by it (ModifyBone, additive scale).
// With zero (or the stub's uninitialized vector) the bone's scale is degenerate and turning the pose back
// into local space divides by it, so editor evaluations made NaN bone transforms and an engine ensure.
// Identity scale keeps the authored bone size without inventing editor-side gameplay state.
DEFINE_FUNCTION(KadanWeakPointBoneScale)
{
    P_GET_PROPERTY(FIntProperty, WeakPointId);
    P_FINISH;
    (void)WeakPointId;
    *(FVector*)RESULT_PARAM = FVector::OneVector;
}

struct FStandIn
{
    const TCHAR* Class;
    const TCHAR* Function;
    FNativeFuncPtr Pointer;
    // The declaration the stand-in reads: parameters in order, then the return value.
    TArray<FFieldClass*> (*Shape)();
    UScriptStruct* (*Returned)();
};

TArray<FFieldClass*> IntToStruct()
{
    return {FIntProperty::StaticClass(), FStructProperty::StaticClass()};
}
UScriptStruct* VectorStruct()
{
    return TBaseStructure<FVector>::Get();
}

const FStandIn StandIns[] = {
    {TEXT(P4_GAME_SCRIPT_PACKAGE ".NiseBaseAnimInstance"), TEXT("GetWeakPointBoneScale"), &KadanWeakPointBoneScale,
        &IntToStruct, &VectorStruct},
};

struct FReplaced
{
    TWeakObjectPtr<UFunction> Function;
    FNativeFuncPtr Original = nullptr;
};
TArray<FReplaced> Replaced;

// The function as P4Stubs declares it, or null when this editor has another declaration.
UFunction* Declared(const FStandIn& StandIn)
{
    UClass* Class = FindObject<UClass>(nullptr, StandIn.Class);
    UFunction* Function = Class ? Class->FindFunctionByName(StandIn.Function, EIncludeSuperFlag::ExcludeSuper)
                                : nullptr;
    if (!Function || !Function->HasAnyFunctionFlags(FUNC_Native) || !Function->GetNativeFunc())
        return nullptr;
    const TArray<FFieldClass*> Shape = StandIn.Shape();
    int32 At = 0;
    for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It, ++At)
    {
        if (At >= Shape.Num() || It->GetClass() != Shape[At])
            return nullptr;
        const bool Return = It->HasAnyPropertyFlags(CPF_ReturnParm);
        if (Return != (At == Shape.Num() - 1))
            return nullptr;
        if (Return && CastFieldChecked<FStructProperty>(*It)->Struct != StandIn.Returned())
            return nullptr;
    }
    return At == Shape.Num() ? Function : nullptr;
}

void Point(UFunction* Function, FNativeFuncPtr Pointer)
{
    Function->SetNativeFunc(Pointer);
    // A later bind of the class reads its table, so the table names the same code.
    for (FNativeFunctionLookup& Entry : Function->GetOwnerClass()->NativeFunctionLookupTable)
        if (Entry.Name == Function->GetFName())
            Entry.Pointer = Pointer;
}
} //namespace

void P4StartGameFunctionStandIns()
{
    for (const FStandIn& StandIn : StandIns)
    {
        UFunction* Function = Declared(StandIn);
        if (!Function ||
            Replaced.ContainsByPredicate([&](const FReplaced& Done) { return Done.Function.Get() == Function; }))
            continue;
        Replaced.Add({Function, Function->GetNativeFunc()});
        Point(Function, StandIn.Pointer);
    }
}

void P4StopGameFunctionStandIns()
{
    for (const FReplaced& Done : Replaced)
        if (UFunction* Function = Done.Function.Get())
            Point(Function, Done.Original);
    Replaced.Reset();
}

TArray<FString> P4GameFunctionStandIns()
{
    TArray<FString> Out;
    for (const FReplaced& Done : Replaced)
        if (UFunction* Function = Done.Function.Get())
            Out.Add(Function->GetPathName());
    return Out;
}
