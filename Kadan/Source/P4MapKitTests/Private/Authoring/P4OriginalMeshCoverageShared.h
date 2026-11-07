// The helpers that P4OriginalMeshBoundaryCoverageAutomation.cpp and its companion test files share.
#pragma once
#include "Policies/CondensedJsonPrintPolicy.h"
#include "P4AuthoringRows.h"
#include "P4OriginalMeshStaging.h"
#include "P4OriginalMeshNegativeTests.h"
#include "P4NativeHash.h"
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectIterator.h"
#include "Engine/PointLight.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/TextureCube.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Lightmass/LightmassImportanceVolume.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "UObject/MetaData.h"

namespace P4MeshCoverage89
{
const TCHAR* const M89Package = TEXT("/Game/Carrot4/Objects/Otakara/OtakaraCollision/SM_stg_Cylinder01");
const TCHAR* const M89Export = TEXT(
    "/Game/Carrot4/Objects/Otakara/OtakaraCollision/SM_stg_Cylinder01.SM_stg_Cylinder01");
const TCHAR* const M89Digest = TEXT("72b564e120afd4f0b2dd08cb942bb2275635087c1a76026363027d1d8797fb4a");
inline TSharedPtr<FJsonObject> M89Parse(const FString& Text)
{
    TSharedPtr<FJsonObject> Out;
    FJsonSerializer::Deserialize(
        TJsonReaderFactory<>::Create(Text), Out, FJsonSerializer::EFlags::StoreNumbersAsStrings);
    return Out;
}
inline FString M89Quote(const FString& Text)
{
    FString Out;
    auto W = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
    W->WriteArrayStart();
    W->WriteValue(Text);
    W->WriteArrayEnd();
    W->Close();
    return Out.Mid(1, Out.Len() - 2);
}
inline FString M89Exact(const TSharedPtr<FJsonValue>& V)
{
    if (!V.IsValid())
        return FString();
    if (V->Type == EJson::Null)
        return TEXT("null");
    if (V->Type == EJson::Boolean)
        return V->AsBool() ? TEXT("true") : TEXT("false");
    if (V->Type == EJson::String)
        return M89Quote(V->AsString());
    if (V->Type == EJson::Number)
    {
        FString Token;
        return V->TryGetString(Token) ? Token : FString();
    }
    TArray<FString> Parts;
    if (V->Type == EJson::Array)
    {
        for (const auto& Child : V->AsArray())
            Parts.Add(M89Exact(Child));
        return TEXT("[") + FString::Join(Parts, TEXT(",")) + TEXT("]");
    }
    if (V->Type == EJson::Object)
    {
        for (const auto& Pair : V->AsObject()->Values)
            Parts.Add(M89Quote(Pair.Key) + TEXT(":") + M89Exact(Pair.Value));
        return TEXT("{") + FString::Join(Parts, TEXT(",")) + TEXT("}");
    }
    return FString();
}
template <typename T>
bool M89VectorBits(const std::vector<T>& A, const std::vector<T>& B)
{
    return A.size() == B.size() && (A.empty() || FMemory::Memcmp(A.data(), B.data(), A.size() * sizeof(T)) == 0);
}
inline bool M89GeometryBits(const FP4OriginalMeshSource& A, const FP4OriginalMeshSource& B)
{
    if (A.Render.Start != B.Render.Start || A.Render.End != B.Render.End ||
        A.Render.LODs.size() != B.Render.LODs.size())
        return false;
    for (size_t L = 0; L < A.Render.LODs.size(); ++L)
    {
        const auto& X = A.Render.LODs[L].Buffers;
        const auto& Y = B.Render.LODs[L].Buffers;
        if (X.Vertices != Y.Vertices || X.TexCoords != Y.TexCoords || X.ColorVertices != Y.ColorVertices ||
            X.HighPrecisionTangents != Y.HighPrecisionTangents || X.UVValues.size() != Y.UVValues.size() ||
            A.Render.LODs[L].Sections.size() != B.Render.LODs[L].Sections.size() ||
            !M89VectorBits(X.PositionValues, Y.PositionValues) || !M89VectorBits(X.TangentValues, Y.TangentValues) ||
            !M89VectorBits(X.ColorRGBA, Y.ColorRGBA) || !M89VectorBits(X.Main.Indices, Y.Main.Indices))
            return false;
        for (size_t U = 0; U < X.UVValues.size(); ++U)
            if (!M89VectorBits(X.UVValues[U], Y.UVValues[U]))
                return false;
    }
    return true;
}
inline TMap<FString, UObject*> M89NativeMeshes()
{
    TMap<FString, UObject*> Names;
    for (TObjectIterator<UObject> It; It; ++It)
        if (It->GetClass()->GetPathName() == TEXT("/Script/Engine.StaticMesh"))
            Names.Add(It->GetPathName(), *It);
    return Names;
}
struct FM89Case
{
    // The case's own files: its header first, its body second, then the bulk files written beside them.
    FString Header, Digest;
    TMap<FString, TArray<uint8>> Bytes;
    TArray<FString> Paths;
    void Same(class FM89Suite& F) const;
};
class FM89Suite
{
public:
    FAutomationTestBase& Test;
    FString Scope, OriginalHeader, OriginalBody, InputHeader, InputBody;
    TArray<uint8> Header, Body;
    FM89Case ControlCase;
    FP4OriginalMeshSource Control, Sentinel;
    TMap<FString, UObject*> BeforeNativeMeshes;
    int32 Captures = 0, Traces = 0;
    bool Good = true;
    explicit FM89Suite(FAutomationTestBase& In, const FString& Name) : Test(In)
    {
        Scope = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("P4NativeQuality/MeshCoverage89") /
            (Name + TEXT("_") + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
        InputHeader = Scope / TEXT("input/cylinder.uasset");
        InputBody = FPaths::ChangeExtension(InputHeader, TEXT("uexp"));
        BeforeNativeMeshes = M89NativeMeshes();
    }
    bool Check(const FString& Label, bool Value)
    {
        Good = Test.TestTrue(Label, Value) && Good;
        return Value;
    }
    void Trace(const FString& Label, bool Accepted, const FString& Error)
    {
        const FString Text = TEXT("{\"case\":") + M89Quote(Label) + TEXT(",\"accepted\":") +
            FString(Accepted ? TEXT("true") : TEXT("false")) + TEXT(",\"error\":") + M89Quote(Error) + TEXT("}");
        Check(Label + TEXT(": actual result retained"),
            FFileHelper::SaveStringToFile(Text, *(Scope / FString::Printf(TEXT("case-%03d.json"), ++Traces)),
                FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    }
    // Whether a case's files still hold what was written to them.
    bool Verified(const FM89Case& C)
    {
        TArray<uint8> Now;
        for (const auto& Pair : C.Bytes)
            if (!FFileHelper::LoadFileToArray(Now, *Pair.Key) || Now != Pair.Value)
                return false;
        return C.Bytes.Num() >= 2;
    }
    // Writes a case's files into a folder of its own: the header, the body, and the bulk files asked for,
    // which no mesh here refers to and which hold the first bytes of the authentic header.
    bool Capture(const TArray<uint8>& H, const TArray<uint8>& B, FM89Case& C, int32 Sidecars = 0)
    {
        C = FM89Case();
        C.Header = Scope / FString::Printf(TEXT("source-%03d/cylinder.uasset"), ++Captures);
        const FString BodyFile = FPaths::ChangeExtension(C.Header, TEXT("uexp"));
        if (!Check(TEXT("Owned case directory exists"),
                IFileManager::Get().MakeDirectory(*FPaths::GetPath(C.Header), true)) ||
            !Check(TEXT("Owned bounded exact header/body inputs written"),
                FFileHelper::SaveArrayToFile(H, *C.Header) && FFileHelper::SaveArrayToFile(B, *BodyFile)))
            return false;
        C.Paths.Add(C.Header);
        C.Paths.Add(BodyFile);
        const TCHAR* Extensions[] = {TEXT("ubulk"), TEXT("uptnl"), TEXT("m.ubulk")};
        for (int32 I = 0; I < 3; ++I)
        {
            if (!(Sidecars & (1 << I)))
                continue;
            const FString Path = FPaths::ChangeExtension(C.Header, Extensions[I]);
            TArray<uint8> Attachment;
            Attachment.Append(Header.GetData(), 32);
            if (!Check(TEXT("Owned unused attachment has exact authentic prefix bytes"),
                    FFileHelper::SaveArrayToFile(Attachment, *Path)))
                return false;
            C.Paths.Add(Path);
        }
        if (!Check(TEXT("The digest of the exact header and body is taken"),
                P4HashSourceBuffers(H.GetData(), H.Num(), B.GetData(), B.Num(), C.Digest)))
            return false;
        for (const FString& Path : C.Paths)
            if (!Check(TEXT("Independent full file bytes retained"),
                    FFileHelper::LoadFileToArray(C.Bytes.Add(Path), *Path)))
                return false;
        return true;
    }
    bool Open()
    {
        OriginalHeader = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_CYLINDER_SOURCE"));
        OriginalBody = FPaths::ChangeExtension(OriginalHeader, TEXT("uexp"));
        FString Digest;
        if (!Check(TEXT("Root supplies bounded exact original Cylinder lengths"),
                !OriginalHeader.IsEmpty() && IFileManager::Get().FileSize(*OriginalHeader) == 2835 &&
                    IFileManager::Get().FileSize(*OriginalBody) == 7643) ||
            !Check(TEXT("Authentic original source pair read once"),
                FFileHelper::LoadFileToArray(Header, *OriginalHeader) &&
                    FFileHelper::LoadFileToArray(Body, *OriginalBody)) ||
            !Check(TEXT("Independent full original source digest matches"),
                P4HashSourceBuffers(Header.GetData(), Header.Num(), Body.GetData(), Body.Num(), Digest) &&
                    Digest == M89Digest) ||
            !Capture(Header, Body, ControlCase))
            return false;
        FString Error;
        const bool Accepted = P4DecodeOriginalMesh(ControlCase.Header, FString(), M89Export, Control, Error);
        Trace(TEXT("exact-original-source-control"), Accepted, Error);
        if (!Check(TEXT("Actual original source decodes all material/render data"),
                Accepted && Control.MaterialsValidated && Control.RenderDecoded && Control.Materials.Num() == 1 &&
                    Control.Render.LODs.size() == 1 && Control.Render.LODs[0].Buffers.ColorRGBA.empty()))
            return false;
        Sentinel = P4OriginalMeshNegativeTests::OriginalMeshTestSentinel(Control);
        return true;
    }
    bool Refuses(const FString& Label, const FM89Case& C, const FString& Expected)
    {
        FP4OriginalMeshSource Out = Sentinel;
        FString Error;
        const bool Accepted = P4DecodeOriginalMesh(C.Header, FString(), M89Export, Out, Error);
        Trace(Label, Accepted, Error);
        Check(Label + TEXT(": real native source admission refuses"), !Accepted && !Error.IsEmpty());
        if (!Expected.IsEmpty())
            Check(Label + TEXT(": intended mesh boundary reached"), Error == Expected);
        Check(Label + TEXT(": complete caller-owned output retained"),
            P4OriginalMeshNegativeTests::OriginalMeshTestUnchanged(Out, Sentinel));
        const auto J = M89Parse(P4StageOriginalMesh(C.Header, FString(), M89Export));
        bool Valid = true;
        if (!Check(Label + TEXT(": reflected staging report exists"), J.IsValid()))
            return false;
        Check(Label + TEXT(": actual bound refusal has no partial material or LOD publication"),
            J->TryGetBoolField(TEXT("valid"), Valid) && !Valid && J->GetArrayField(TEXT("materials")).Num() == 0 &&
                J->GetArrayField(TEXT("lods")).Num() == 0);
        C.Same(*this);
        return !Accepted;
    }
    void Final()
    {
        TArray<uint8> Bytes;
        Check(TEXT("Entire authentic original header/body remain unchanged"),
            FFileHelper::LoadFileToArray(Bytes, *OriginalHeader) && Bytes == Header &&
                FFileHelper::LoadFileToArray(Bytes, *OriginalBody) && Bytes == Body);
        ControlCase.Same(*this);
        Check(TEXT("The control's files remain as written"), Verified(ControlCase));
        const TMap<FString, UObject*> AfterNativeMeshes = M89NativeMeshes();
        bool SameMeshes = BeforeNativeMeshes.Num() == AfterNativeMeshes.Num();
        for (const auto& Pair : BeforeNativeMeshes)
        {
            const auto* Current = AfterNativeMeshes.Find(Pair.Key);
            SameMeshes = (Current && *Current == Pair.Value) && SameMeshes;
        }
        Check(TEXT("No partial native StaticMesh object publication or replacement"), SameMeshes);
    }
};
inline void FM89Case::Same(FM89Suite& F) const
{
    TArray<uint8> After;
    for (const auto& Pair : Bytes)
        F.Check(TEXT("Every file of the case remains exact"),
            FFileHelper::LoadFileToArray(After, *Pair.Key) && After == Pair.Value);
}
inline uint32 M89U32(const TArray<uint8>& Bytes, int32 At)
{
    return uint32(Bytes[At]) | (uint32(Bytes[At + 1]) << 8) | (uint32(Bytes[At + 2]) << 16) |
        (uint32(Bytes[At + 3]) << 24);
}
inline bool M89Write(FM89Suite& F, TArray<uint8>& Bytes, int32 At, uint32 Before, uint32 After)
{
    if (!F.Check(TEXT("Independent exact source byte preimage agrees before mutation"),
            At >= 0 && At <= Bytes.Num() - 4 && M89U32(Bytes, At) == Before))
        return false;
    for (int32 I = 0; I < 4; ++I)
        Bytes[At + I] = uint8(After >> (I * 8));
    return true;
}
} //namespace P4MeshCoverage89
#endif
