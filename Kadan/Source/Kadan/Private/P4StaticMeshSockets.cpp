#include "P4StaticMeshSockets.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

bool P4ReadOriginalMeshSockets(const P4MapKit::FPackageInventory& Package, const std::vector<uint8>& Body,
                               int32 MeshIndex, const std::vector<int32>& Indices, TArray<FP4OriginalMeshSocket>& Out,
                               FString& Error)
{
    const auto Fail = [&](const TCHAR* Why)
    {
        Error = FString(TEXT("mesh-socket-source-unqualified: ")) + Why;
        return false;
    };
    if (!Package.Error.Code.empty() || Indices.size() > 1024 || MeshIndex <= 0 ||
        size_t(MeshIndex) > Package.Exports.size())
        return Fail(TEXT("source bounds"));
    const auto& MeshExport = Package.Exports[size_t(MeshIndex - 1)];
    if (MeshExport.Offset > Body.size() || MeshExport.Size > Body.size() - MeshExport.Offset)
        return Fail(TEXT("mesh export is outside the source body"));
    const auto MeshProperties =
        P4MapKit::ReadPropertyEnvelopes(Body, Package.Names, MeshExport.Offset, MeshExport.Offset + MeshExport.Size);
    if (!MeshProperties.Error.Code.empty())
        return Fail(TEXT("mesh property envelopes are invalid"));
    bool TaggedSockets = false;
    for (const auto& Tag : MeshProperties.Properties)
    {
        if (Tag.Name != "Sockets")
            continue;
        if (TaggedSockets || Tag.Type != "ArrayProperty" || Tag.Detail != std::vector<std::string>{"ObjectProperty"} ||
            Tag.ArrayIndex || Tag.HasGuid)
            return Fail(TEXT("tagged socket array schema differs"));
        TaggedSockets = true;
        P4MapKit::FByteReader Reader(Body, Tag.PayloadOffset, Tag.PayloadOffset + Tag.Size);
        int32 Count = 0;
        if (!Reader.I32(Count) || Count < 0 || size_t(Count) != Indices.size())
            return Fail(TEXT("tagged and native socket counts differ"));
        for (int32 Expected : Indices)
        {
            int32 Actual = 0;
            if (!Reader.I32(Actual) || Actual != Expected)
                return Fail(TEXT("tagged and native socket identities differ"));
        }
        if (Reader.Tell() != Tag.PayloadOffset + Tag.Size)
            return Fail(TEXT("tagged socket array has an unread tail"));
    }
    TArray<FP4OriginalMeshSocket> Pending;
    TSet<int32> Seen;
    TSet<FName> Names;
    for (int32 Index : Indices)
    {
        if (Index <= 0 || size_t(Index) > Package.Exports.size() || Seen.Contains(Index))
            return Fail(TEXT("socket is not a unique owned export"));
        Seen.Add(Index);
        const auto& Export = Package.Exports[size_t(Index - 1)];
        if (Export.Offset > Body.size() || Export.Size > Body.size() - Export.Offset)
            return Fail(TEXT("socket export is outside the source body"));
        std::string Class, Template;
        if (Export.Outer != MeshIndex || !Package.Reference(Export.Class, Class) ||
            Class != "/Script/Engine.StaticMeshSocket")
            return Fail(TEXT("socket owner or class differs"));
        if (Export.Template && (!Package.Reference(Export.Template, Template) ||
                                FString(UTF8_TO_TCHAR(Template.c_str())) !=
                                    UStaticMeshSocket::StaticClass()->GetDefaultObject()->GetPathName()))
            return Fail(TEXT("socket has an unqualified template"));
        FP4OriginalMeshSocket Socket;
        Socket.ObjectName = UTF8_TO_TCHAR(Export.Name.c_str());
        const FName Name(*Socket.ObjectName);
        if (Name.IsNone() || Names.Contains(Name) ||
            !FPackageName::IsValidObjectPath(FString(UTF8_TO_TCHAR(Package.Package.c_str())) + TEXT(".") +
                                             Socket.ObjectName))
            return Fail(TEXT("socket object name is invalid or duplicated"));
        Names.Add(Name);
        const auto Properties =
            P4MapKit::ReadPropertyEnvelopes(Body, Package.Names, Export.Offset, Export.Offset + Export.Size);
        const size_t End = Export.Offset + Export.Size;
        if (!Properties.Error.Code.empty() || Properties.End > End || End - Properties.End != 4 ||
            Body[Properties.End] || Body[Properties.End + 1] || Body[Properties.End + 2] || Body[Properties.End + 3])
            return Fail(TEXT("socket native tail is not the plain object identity"));
        if (!P4InspectObjectProperties(Package, Export, Properties, Body, UStaticMeshSocket::StaticClass(),
                                       TEXT("StaticMeshSocket"), {}, Socket.Properties, Error) ||
            !Socket.Properties.NativePropertiesRepresentable)
        {
            if (Error.IsEmpty())
                Error = TEXT("mesh-socket-source-properties-unrepresentable");
            return false;
        }
        Pending.Add(MoveTemp(Socket));
    }
    Out = MoveTemp(Pending);
    Error.Reset();
    return true;
}

bool P4BuildOriginalMeshSockets(UStaticMesh* Mesh, const TArray<FP4OriginalMeshSocket>& Source, FString& Error)
{
    if (!IsInGameThread() || !Mesh || Mesh->Sockets.Num() || Source.Num() > 1024)
    {
        Error = TEXT("mesh-socket-build-requires-an-empty-owned-mesh");
        return false;
    }
    TArray<UStaticMeshSocket*> Built;
    bool Published = false;
    ON_SCOPE_EXIT
    {
        if (!Published)
            for (UStaticMeshSocket* Socket : Built)
            {
                Socket->SetFlags(RF_Transient);
                Socket->Rename(nullptr, GetTransientPackage(),
                               REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
                Socket->MarkPendingKill();
            }
    };
    TSet<FName> SocketNames;
    for (const auto& Original : Source)
    {
        if (FindObject<UObject>(Mesh, *Original.ObjectName))
        {
            Error = TEXT("mesh-socket-build-owned-name-is-occupied");
            return false;
        }
        TStrongObjectPtr<UStaticMeshSocket> Scratch(
            NewObject<UStaticMeshSocket>(GetTransientPackage(), NAME_None, RF_Transient));
        FString Canonical;
        if (!P4PrepareObjectProperties(Scratch.Get(), Original.Properties, Canonical, Error))
            return false;
        if (Scratch->SocketName.IsNone() || SocketNames.Contains(Scratch->SocketName))
        {
            Error = TEXT("mesh-socket-build-source-name-is-empty-or-ambiguous");
            return false;
        }
        SocketNames.Add(Scratch->SocketName);
        auto* Socket = DuplicateObject<UStaticMeshSocket>(Scratch.Get(), Mesh, FName(*Original.ObjectName));
        if (!Socket || Socket->GetOuter() != Mesh)
        {
            Error = TEXT("mesh-socket-build-duplicate-failed");
            return false;
        }
        Socket->ClearFlags(RF_Transient | RF_Public | RF_Standalone);
        Socket->SetFlags(RF_Transactional);
        Built.Add(Socket);
    }
    Mesh->Sockets = MoveTemp(Built);
    Published = true;
    Error.Reset();
    return true;
}
