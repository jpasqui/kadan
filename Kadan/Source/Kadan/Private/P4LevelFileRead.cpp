// Reading a cooked level or asset file: its objects with their decoded properties, the rows of its
// tables, which objects are the level's own, and what the import does not bring over yet.

#include "P4LevelContentInternal.h"
#include "P4Common.h"

namespace P4LevelContent
{
// The id an object carries after its properties for the lazy references that name it, at At: a 1, then the
// four words of the id, which Out gets as 32 hexadecimal digits. False when what is there is not that.
bool ReadObjectGuid(const std::vector<uint8>& Body, std::size_t At, std::size_t End, FString& Out)
{
    if (At > End || End > Body.size() || End - At < 20)
        return false;
    uint32 Words[5];
    FMemory::Memcpy(Words, Body.data() + At, 20);
    if (Words[0] != 1 || !(Words[1] | Words[2] | Words[3] | Words[4]))
        return false;
    Out = FString::Printf(TEXT("%08x%08x%08x%08x"), Words[1], Words[2], Words[3], Words[4]);
    return true;
}

bool ReadClassComponentTail(FNode& Node, const UStruct* Schema, const std::vector<uint8>& Body)
{
    if (!Node.Undecoded.IsEmpty() || !Schema || Node.TailStart > Node.TailEnd || Node.TailEnd > Body.size())
        return false;
    // SkyAtmosphere serializes UObject's empty GUID marker followed by its lighting-build GUID.
    // Class templates have the same native payload as placed components. Its authored properties
    // remain in the comparison; the build ID uses the existing level lighting-data classification.
    if (Schema->IsChildOf(USkyAtmosphereComponent::StaticClass()))
    {
        if (Node.TailEnd - Node.TailStart != 20 || Body[Node.TailStart] || Body[Node.TailStart + 1] ||
            Body[Node.TailStart + 2] || Body[Node.TailStart + 3])
            return false;
        Node.EngineTail = TEXT("the id of a lighting build");
        return true;
    }
    if (!Schema->IsChildOf(UStaticMeshComponent::StaticClass()))
        return false;
    const auto Native = P4MapKit::ReadStaticMeshComponentNative(Body, Node.TailStart, Node.TailEnd);
    if (!Native.Error.Code.empty() || Native.End != Node.TailEnd)
        return false;
    Node.Lods = int32(Native.Lods);
    Node.LightingIds.Reset();
    for (const auto& Id : Native.LightingIds)
        Node.LightingIds.Add(FString::Printf(TEXT("%08x %08x %08x %08x"), Id[0], Id[1], Id[2], Id[3]));
    return true;
}

// Reads Header and Body. PackageName, when given, is the name the file's objects are known under (a
// copy saved for comparison lies under another file name than its package's).
bool ReadNodes(FFile& File, const FString& PackageName, FString& Error)
{
    File.Package = P4MapKit::ReadPackage(File.Header, File.Body);
    if (File.Package.Error.Code.empty() && !File.RecoveredImports.empty())
        File.Package.RecoveredImportReferences = File.RecoveredImports;
    if (!File.Package.Error.Code.empty())
    {
        Error = FString::Printf(TEXT("level-file-unreadable: %s at byte %d of the header"),
            *Text(File.Package.Error.Code), int32(File.Package.Error.Offset));
        return false;
    }
    File.PackageName = PackageName.IsEmpty() ? Text(File.Package.Package) : PackageName;
    const auto& Exports = File.Package.Exports;
    TMap<UStruct*, std::vector<P4MapKit::FMapStructCodec>> SchemaCodecs;
    TSet<UStruct*> BadSchemas;
    const std::vector<P4MapKit::FMapStructCodec> NoCodecs;
    const std::vector<P4MapKit::FArchivedFloatCodec> Floats{{"AnimNotifyEvent", "MiddleDuration", true, false, false},
        {"FoliageType_InstancedStaticMesh", "LowBoundOriginRadius", false, true, false},
        {"P4RecoveredFoliageType", "LowBoundOriginRadius", false, true, false},
        {"AkAudioEvent", "MaximumDuration", false, true, false},
        {"AkAudioEventData", "MaximumDuration", false, true, false}};
    for (std::size_t Slot = 0; Slot < Exports.size(); ++Slot)
    {
        const auto& X = Exports[Slot];
        FNode Node;
        std::string Path, Class;
        if (!File.Package.Reference(std::int32_t(Slot + 1), Path) || !File.Package.Reference(X.Class, Class) ||
            X.Offset > File.Body.size() || X.Size > File.Body.size() - X.Offset)
        {
            Error = TEXT("level-file-object-unreadable");
            return false;
        }
        Node.Name = Text(X.Name);
        Node.Path = Text(Path);
        Node.Outer = X.Outer;
        Node.ClassIndex = X.Class;
        Node.Template = X.Template;
        Node.Flags = X.Flags;
        // A class that is itself in the file is named below the package.
        Node.Class = X.Class > 0 ? File.PackageName + TEXT(".") + Text(Class) : Text(Class);
        Node.Properties = P4MapKit::ReadPropertyEnvelopes(File.Body, File.Package.Names, X.Offset, X.Offset + X.Size);
        // Map archives omit the value struct's name. Recover it from the loaded
        // owning UE schema, as table-row decoding already does. Do not infer a
        // native/tagged layout from the bytes or load a game package here.
        UStruct* Schema = FindObject<UStruct>(nullptr, *Node.Class);
        if (Schema && !SchemaCodecs.Contains(Schema) && !BadSchemas.Contains(Schema))
        {
            std::vector<P4MapKit::FMapStructCodec> Collected;
            if (P4MapStructCodecs(Schema, Collected))
                SchemaCodecs.Add(Schema, MoveTemp(Collected));
            else
                BadSchemas.Add(Schema);
        }
        const auto* Codecs = SchemaCodecs.Find(Schema);
        const bool KnownCodecs = !BadSchemas.Contains(Schema);
        const auto Dot = Class.rfind('.');
        const std::string Owner = Schema ? TCHAR_TO_UTF8(*Schema->GetName())
                                         : Class.substr(Dot == std::string::npos ? 0 : Dot + 1);
        // These captured fields have actual nonfinite IEEE words. MiddleDuration
        // is absent in stock UE and stays an explicitly reported value not held;
        // foliage's derived lower-bound radius can overflow to positive infinity.
        P4MapKit::FPropertyDecoder Decoder(
            File.Package, File.Body, 1000000, 64 * 1024 * 1024, Codecs ? *Codecs : NoCodecs, Floats);
        std::size_t Position = X.Offset;
        // An object whose properties are not read is told as that when the level is asked for.
        Node.TailEnd = X.Offset + X.Size;
        if (!Node.Properties.Error.Code.empty())
        {
            Node.Undecoded = Text(Node.Properties.Error.Code);
            Node.Properties.End = Node.TailEnd;
        }
        else if (!KnownCodecs)
            Node.Undecoded = TEXT("ambiguous-or-oversized-struct-codecs");
        else if (!Decoder.Properties(Position, Node.TailEnd, Node.Values, 0, Owner))
        {
            Node.Undecoded = Text(Decoder.Error.Code);
            Node.Values = FDecodedValue();
        }
        Node.TailStart = Node.Properties.End;
        const std::size_t Tail = Node.TailEnd - Node.TailStart;
        Node.PlainTail = !Node.Undecoded.IsEmpty() ? false
            : Tail == 0 ? Node.Name.StartsWith(TEXT("Default__"), ESearchCase::CaseSensitive)
                        : Tail == 4 && File.Body[Node.TailStart] == 0 && File.Body[Node.TailStart + 1] == 0 &&
                File.Body[Node.TailStart + 2] == 0 && File.Body[Node.TailStart + 3] == 0;
        // An object a lazy reference names carries its id there instead: a 1 where others have a 0, then the id.
        if (!Node.PlainTail && Node.Undecoded.IsEmpty() && Tail == 20 &&
            ReadObjectGuid(File.Body, Node.TailStart, Node.TailEnd, Node.ObjectGuid))
            Node.PlainTail = true;
        // A Blueprint component template has the same complete static-mesh native
        // serialization as its level instance: UObject identity plus the LOD list.
        // Only an actual registered mesh-component subtype and full-span decode
        // qualify; unknown extra or truncated payload is still unreadable.
        if (!Node.PlainTail)
            ReadClassComponentTail(Node, Schema, File.Body);
        const FP4NativeTailReader* Reader = P4FindNativeTailReader(Node.Class);
        if (Node.Undecoded.IsEmpty() && !Node.PlainTail && Reader)
        {
            FString Line, Why;
            const uint8* NativeBytes = File.Body.data() + Node.TailStart;
            Node.NativeTail = Reader->Describe(NativeBytes, int64(Node.TailEnd - Node.TailStart), Line, Why);
            if (Node.NativeTail)
                Node.Native.Add(Line);
        }
        if (Node.Undecoded.IsEmpty() && Node.Class == TEXT("/Script/Engine.ParticleModuleRequired"))
        {
            FString Why;
            TArray<uint8> TailBytes;
            TailBytes.Append(File.Body.data() + Node.TailStart, int32(Node.TailEnd - Node.TailStart));
            Node.ParticleRequired = P4DescribeOriginalParticleRequiredTail(TailBytes, Node.Native, Why);
        }
        File.Nodes.Add(MoveTemp(Node));
    }
    // The native data of skeletons, skinned meshes and animation sequences. An object index is said by
    // the object's path, so every object has to be known first.
    for (FNode& Node : File.Nodes)
    {
        if (Node.NativeTail)
            if (const FP4NativeTailReader* Reader = P4FindNativeTailReader(Node.Class))
                if (Reader->TailObject)
                    Node.Native.Last() += TEXT(" ") +
                        File.Ref(Reader->TailObject(File.Body.data() + Node.TailStart, int64(Node.TailEnd - Node.TailStart)));
        if (Node.Undecoded.IsEmpty() && P4SameText(Node.Class, TEXT("/Script/Engine.PhysicsAsset")))
        {
            Node.PhysicsData = P4MapKit::ReadPhysicsAssetNative(File.Body, Node.TailStart, Node.TailEnd);
            Node.Physics = Node.PhysicsData.Error.Code.empty();
            if (Node.Physics)
            {
                Node.Native.Add(FString::Printf(
                    TEXT("collision disable table of %d pairs"), int32(Node.PhysicsData.Disabled.size())));
                for (const auto& Pair : Node.PhysicsData.Disabled)
                    Node.Native.Add(FString::Printf(TEXT("collision disabled between %d and %d = %s"), Pair.First,
                        Pair.Second, Pair.Value ? TEXT("true") : TEXT("false")));
                Node.Native.Sort();
            }
        }
        if (Node.Undecoded.IsEmpty() && P4SameText(Node.Class, TEXT("/Script/Engine.SkeletalBodySetup")))
        {
            Node.PhysicsBodyData = P4MapKit::ReadPrimitivePhysicsBodyNative(File.Body, Node.TailStart, Node.TailEnd);
            Node.PhysicsBody = Node.PhysicsBodyData.Error.Code.empty();
            if (Node.PhysicsBody)
            {
                const auto& Guid = Node.PhysicsBodyData.Guid;
                Node.Native.Add(
                    FString::Printf(TEXT("physics body id %08x %08x %08x %08x"), Guid[0], Guid[1], Guid[2], Guid[3]));
                Node.Native.Add(TEXT("primitive physics body without cooked convex or triangle data"));
            }
        }
        if (!Node.Undecoded.IsEmpty() || Node.PlainTail || !P4SkinnedClass(Node.Class))
            continue;
        const FDecodedValue* Colors = Node.Values.Field("bHasVertexColors");
        Node.Skinned = P4SkinnedNativeLines(
            File.Package, File.Body, Node.TailStart, Node.TailEnd, Node.Class, Colors && Colors->Text == "true",
            [&](int32 Index) { return File.Ref(Index); }, Node.Native, Node.NativeMade, Node.SkinnedIssue);
    }
    return true;
}

// Reads the rows of every table in a file that ReadNodes read. A table's rows follow its properties: how
// many there are, then each row's name and every member of the row, written as an object's properties
// are. A map in a row does not say what kind of struct its keys and values are, so that is taken from
// the row's type, which has to be in the project. With Refuse, a table whose rows are not read fails
// the read, with why. Without it such a table stays what it was, an object with native data.
bool ReadTables(FFile& File, bool Refuse, FString& Error, bool TypedValues)
{
    for (int32 Index = 1; Index <= File.Nodes.Num(); ++Index)
    {
        const FNode& Node = File.Node(Index);
        if (!P4SameText(Node.Class, TEXT("/Script/Engine.DataTable")) || !Node.Undecoded.IsEmpty() ||
            Node.TailEnd - Node.TailStart < 8)
            continue;
        const FDecodedValue* Said = Node.Values.Field("RowStruct");
        const FString Type = Said && Said->Kind == FDecodedValue::EKind::String ? Text(Said->Text) : FString();
        UScriptStruct* Row = Type.StartsWith(TEXT("/"), ESearchCase::CaseSensitive)
            ? LoadObject<UScriptStruct>(nullptr, *Type, nullptr, LOAD_NoWarn | LOAD_Quiet)
            : nullptr;
        FString Why;
        std::vector<P4MapKit::FMapStructCodec> Codecs;
        if (!Row)
            Why = TEXT("asset-import-row-type-not-in-the-project: ") + (Type.IsEmpty() ? FString(TEXT("none")) : Type);
        else if (!P4MapStructCodecs(Row, Codecs))
            Why = TEXT("asset-import-row-type-unreadable: ") + Type;
        else
        {
            // The four bytes every object ends its properties with come first.
            bool Plain = true;
            for (std::size_t At = 0; At < 4; ++At)
                Plain = Plain && File.Body[Node.TailStart + At] == 0;
            P4MapKit::FPropertyDecoder Decoder(File.Package, File.Body, 1000000, 64 * 1024 * 1024, Codecs);
            std::size_t Position = Node.TailStart + 4;
            std::vector<std::pair<std::string, FDecodedValue>> Found;
            if (Plain &&
                (TypedValues ? Decoder.TypedTableRows(Position, Node.TailEnd, TCHAR_TO_UTF8(*Row->GetName()), Found)
                             : Decoder.TableRows(Position, Node.TailEnd, TCHAR_TO_UTF8(*Row->GetName()), Found)) &&
                Position == Node.TailEnd)
            {
                FTableRows Rows;
                if (!TypedValues)
                {
                    // Keep the legacy read above authoritative. Only an independent typed read of
                    // those same rows may identify a soft reference for folded comparison.
                    P4MapKit::FPropertyDecoder TypedDecoder(File.Package, File.Body, 1000000, 64 * 1024 * 1024, Codecs);
                    std::size_t TypedPosition = Node.TailStart + 4;
                    std::vector<std::pair<std::string, FDecodedValue>> Typed;
                    bool SameRows = TypedDecoder.TypedTableRows(
                                        TypedPosition, Node.TailEnd, TCHAR_TO_UTF8(*Row->GetName()), Typed) &&
                        TypedPosition == Node.TailEnd && Typed.size() == Found.size();
                    for (std::size_t At = 0; SameRows && At < Found.size(); ++At)
                        SameRows = Typed[At].first == Found[At].first &&
                            P4MapKit::DecodedJson(Typed[At].second) == P4MapKit::DecodedJson(Found[At].second);
                    if (SameRows)
                        for (auto& One : Typed)
                            Rows.TypedComparisonRows.Add(MoveTemp(One.second));
                }
                for (auto& One : Found)
                {
                    Rows.Names.Add(Text(One.first));
                    Rows.Rows.Add(MoveTemp(One.second));
                }
                File.Tables.Add(Index, MoveTemp(Rows));
                continue;
            }
            Why = TEXT("asset-import-table-rows-not-read: ") + Node.Path + TEXT(" (") + Type + TEXT("): ") +
                (!Plain ? FString(TEXT("unsupported-object-guid"))
                        : Decoder.Error.Code.empty() ? FString(TEXT("unread-table-rows"))
                                                     : FString::Printf(TEXT("%s at byte %d"), *Text(Decoder.Error.Code),
                                                           int32(Decoder.Error.Offset)));
        }
        if (Refuse)
        {
            Error = Why;
            return false;
        }
    }
    return true;
}

// Reads a level file: its objects, and which of them are the level's own.
bool Read(FFile& File, const FString& PackageName, FString& Error, bool AllowUnsupportedScript)
{
    if (!ReadNodes(File, PackageName, Error))
        return false;
    for (int32 Index = 1; Index <= File.Nodes.Num(); ++Index)
        if (P4SameText(File.Node(Index).Class, TEXT("/Script/Engine.World")) && File.Node(Index).Outer == 0)
        {
            if (File.World)
            {
                Error = TEXT("level-file-has-two-worlds");
                return false;
            }
            File.World = Index;
        }
    for (int32 Index = 1; File.World && Index <= File.Nodes.Num(); ++Index)
        if (P4SameText(File.Node(Index).Class, TEXT("/Script/Engine.Level")) && File.Node(Index).Outer == File.World)
        {
            if (File.Level)
            {
                Error = TEXT("level-file-has-two-levels");
                return false;
            }
            File.Level = Index;
        }
    if (!File.World || !File.Level)
    {
        Error = TEXT("level-file-has-no-world");
        return false;
    }
    const auto Native = [&](const TCHAR* What, const P4MapKit::FReadError& Problem)
    {
        if (Problem.Code.empty())
            return true;
        Error = FString(TEXT("level-file-")) + What + TEXT("-undecoded: ") + Text(Problem.Code);
        return false;
    };
    File.WorldNative = P4MapKit::ReadWorldNative(
        File.Body, File.Node(File.World).TailStart, File.Node(File.World).TailEnd);
    File.LevelNative = P4MapKit::ReadLevelNative(
        File.Body, File.Node(File.Level).TailStart, File.Node(File.Level).TailEnd);
    if (!Native(TEXT("world"), File.WorldNative.Error) || !Native(TEXT("level"), File.LevelNative.Error))
        return false;
    if (File.WorldNative.PersistentLevel != File.Level)
    {
        Error = TEXT("level-file-world-names-another-level");
        return false;
    }
    TSet<int32> StreamingIndices;
    for (const int32 Index : File.WorldNative.StreamingLevels)
    {
        if (!File.Export(Index) || File.Node(Index).Outer != File.World || StreamingIndices.Contains(Index))
        {
            Error = TEXT("level-file-streaming-list-unreadable");
            return false;
        }
        StreamingIndices.Add(Index);
    }
    File.Model = File.LevelNative.Model;
    if (!File.Export(File.Model) || !P4SameText(File.Node(File.Model).Class, TEXT("/Script/Engine.Model")) ||
        File.Node(File.Model).Outer != File.Level)
    {
        Error = TEXT("level-file-has-no-model");
        return false;
    }
    File.ModelNative = P4MapKit::ReadModelNative(
        File.Body, File.Node(File.Model).TailStart, File.Node(File.Model).TailEnd);
    if (!Native(TEXT("model"), File.ModelNative.Error))
        return false;
    // The first of a level's actors is its world settings.
    File.Settings = File.LevelNative.Actors.size() ? File.LevelNative.Actors[0] : 0;
    if (!File.Export(File.Settings) || File.Node(File.Settings).Outer != File.Level)
    {
        Error = TEXT("level-file-has-no-world-settings");
        return false;
    }
    File.ScriptActor = File.LevelNative.LevelScriptActor;
    if (File.ScriptActor)
    {
        if (!File.Export(File.ScriptActor) || File.Node(File.ScriptActor).Outer != File.Level)
        {
            Error = TEXT("level-file-level-script-unreadable");
            return false;
        }
        File.ScriptClass = File.Node(File.ScriptActor).ClassIndex;
        if (!File.Export(File.ScriptClass) ||
            !P4SameText(File.Node(File.ScriptClass).Class, TEXT("/Script/Engine.BlueprintGeneratedClass")) ||
            File.Node(File.ScriptClass).Outer != 0)
        {
            Error = TEXT("level-file-level-script-unreadable");
            return false;
        }
        File.ClassNative = P4MapKit::ReadGeneratedClassNative(
            File.Body, File.Package.Names, File.Node(File.ScriptClass).TailStart, File.Node(File.ScriptClass).TailEnd);
        std::string EmptyScriptWhy;
        if (!File.ClassNative.UndecodedType.empty() || !File.ClassNative.Error.Code.empty())
        {
            if (P4MapKit::Blueprint::IsProvenEmptyLevelClass(File.Package, File.Body, File.ScriptClass, EmptyScriptWhy))
            {
                File.ClassNative = P4MapKit::Blueprint::ReadProvenEmptyGeneratedClassNative(
                    File.Package, File.Body, File.ScriptClass);
                File.ProvenEmptyBeginPlay = File.ClassNative.Error.Code.empty();
                if (File.ProvenEmptyBeginPlay)
                    for (const auto& Function : File.ClassNative.Functions)
                        File.EmptyLevelFunctions.Add(Function.second);
            }
        }
        if (File.ClassNative.UnsupportedVariable)
        {
            File.UnsupportedScript = true;
            if (!AllowUnsupportedScript)
            {
                Error = TEXT("level-file-level-script-variable-type-not-read: ") +
                    Text(File.ClassNative.UndecodedType) + TEXT("; empty-wrapper proof: ") + Text(EmptyScriptWhy);
                return false;
            }
            // The reader stopped at an unsupported variable type, before the native CDO
            // reference. Recover only its independently validated export identity; no
            // script/default values from this class become editable projection content.
            for (int32 Index = 1; Index <= File.Nodes.Num(); ++Index)
                if (File.Node(Index).ClassIndex == File.ScriptClass &&
                    (File.Node(Index).Flags & uint32(RF_ClassDefaultObject)))
                {
                    if (File.ScriptDefault)
                    {
                        Error = TEXT("level-file-level-script-has-two-defaults");
                        return false;
                    }
                    File.ScriptDefault = Index;
                }
        }
        else
        {
            if (!Native(TEXT("level-script-class"), File.ClassNative.Error))
                return false;
            File.ScriptDefault = File.ClassNative.DefaultObject;
        }
        if (!File.Export(File.ScriptDefault) || File.Node(File.ScriptDefault).ClassIndex != File.ScriptClass)
        {
            Error = TEXT("level-file-level-script-unreadable");
            return false;
        }
        if (File.ProvenEmptyBeginPlay)
        {
            // The proof checks the CDO's sole compiler frame envelope and entire
            // native class/function bodies. A captured frame is never an authored
            // default or a pointer to transplant into the editor.
            FNode& Default = File.Nodes[File.ScriptDefault - 1];
            Default.Values = FDecodedValue();
            Default.Undecoded.Reset();
            Default.PlainTail = true;
        }
    }
    for (const std::int32_t Index : File.LevelNative.Actors)
    {
        if (Index == 0 || Index == File.Settings || Index == File.ScriptActor)
            continue;
        if (!File.Export(Index) || File.Node(Index).Outer != File.Level || File.Actors.Contains(Index))
        {
            Error = TEXT("level-file-actor-list-unreadable");
            return false;
        }
        File.Actors.Add(Index);
    }
    // What follows the properties of an object inside an actor may be a static mesh component's data.
    for (int32 Index = 1; Index <= File.Nodes.Num(); ++Index)
    {
        FNode& Node = File.Nodes[Index - 1];
        if (Node.PlainTail || !Node.Undecoded.IsEmpty() || !File.InActor(Index))
            continue;
        const std::size_t Tail = Node.TailEnd - Node.TailStart;
        const auto Zeros = [&](std::size_t Count)
        {
            for (std::size_t At = 0; At < Count; ++At)
                if (File.Body[Node.TailStart + At] != 0)
                    return false;
            return true;
        };
        // The foliage actor of a level without foliage holds an empty list.
        if (P4SameText(Node.Class, TEXT("/Script/Foliage.InstancedFoliageActor")))
        {
            if (Tail == 8 && Zeros(8))
            {
                Node.PlainTail = true;
                continue;
            }
            P4MapKit::FFoliageActorNative Foliage = P4MapKit::ReadFoliageActorNative(
                File.Body, Node.TailStart, Node.TailEnd);
            if (Foliage.Error.Code.empty() && File.Actors.Contains(Index))
            {
                File.Foliage.Add(Index, MoveTemp(Foliage));
                continue;
            }
        }
        // A landscape's parts carry, after the id lazy references name them by (or the 0 of none), what a cook
        // makes of them: a component its grass data (none) and the flags of a cook, a collision component the
        // collision cooked from its heights, the landscape's own material instance the shaders a cook compiles,
        // a height map its image, which the import makes its source image.
        if (Node.Class.StartsWith(TEXT("/Script/Landscape."), ESearchCase::CaseSensitive) ||
            P4SameText(Node.Class, TEXT("/Script/Engine.Texture2D")))
        {
            FString Guid;
            std::size_t At = Node.TailStart;
            if (ReadObjectGuid(File.Body, At, Node.TailEnd, Guid))
                At += 20;
            else if (Tail >= 4 && Zeros(4))
                At += 4;
            else
                At = Node.TailEnd + 1;
            const auto Word = [&](std::size_t Place)
            {
                uint32 Value = 0;
                FMemory::Memcpy(&Value, File.Body.data() + At + Place * 4, 4);
                return Value;
            };
            const std::size_t Rest = At <= Node.TailEnd ? Node.TailEnd - At : 0;
            FString Made;
            if (At > Node.TailEnd)
            {
            }
            else if (P4SameText(Node.Class, TEXT("/Script/Landscape.LandscapeComponent")) && Rest == 20 &&
                Word(0) == 2 && Word(1) == 0 && Word(2) == 0 && Word(3) == 1 && Word(4) == 0)
                Made = TEXT("a landscape component's grass data, none, and the flags of a cook");
            else if (P4SameText(Node.Class, TEXT("/Script/Landscape.LandscapeHeightfieldCollisionComponent")) &&
                Rest >= 12 && Word(0) == 1 && Word(1) == 1 && Word(2) == Rest - 12)
                Made = TEXT("the collision a cook makes from a landscape's heights");
            else if (P4SameText(Node.Class, TEXT("/Script/Landscape.LandscapeMaterialInstanceConstant")) && Rest > 0)
            {
                const FDecodedValue* Permutation = Node.Values.Field("bHasStaticPermutationResource");
                if (Permutation && Permutation->Text == "true")
                    Made = TEXT("the shaders a cook compiles for a material instance");
            }
            else if (P4SameText(Node.Class, TEXT("/Script/Engine.Texture2D")) && File.Actors.Contains(Node.Outer) &&
                File.Node(Node.Outer).Class.StartsWith(TEXT("/Script/Landscape."), ESearchCase::CaseSensitive))
            {
                FString Unread;
                if (P4ReadPackageTextureImage(File.Package, File.Body, TCHAR_TO_UTF8(*Node.Class), Node.TailStart,
                        Node.TailEnd, nullptr, Unread))
                {
                    Made = TEXT("the image a cook makes from a texture's source");
                    Node.CookedTexture = true;
                }
            }
            if (!Made.IsEmpty())
            {
                Node.EngineTail = Made;
                Node.ObjectGuid = Guid;
                continue;
            }
        }
        // A sky atmosphere carries the id of the lighting build it was in.
        if (P4SameText(Node.Class, TEXT("/Script/Engine.SkyAtmosphereComponent")) && Tail == 20 && Zeros(4))
        {
            Node.EngineTail = TEXT("the id of a lighting build");
            continue;
        }
        // A body setup carries its id and the collision the engine cooked from the shapes its
        // properties say.
        if (P4SameText(Node.Class, TEXT("/Script/Engine.BodySetup")) && Tail >= 24 && Zeros(4))
        {
            Node.EngineTail = TEXT("cooked collision");
            continue;
        }
        // A brush actor's model is its shape.
        if (P4SameText(Node.Class, TEXT("/Script/Engine.Model")) && File.Actors.Contains(Node.Outer))
        {
            P4MapKit::FModelNative Model = P4MapKit::ReadModelNative(File.Body, Node.TailStart, Node.TailEnd);
            if (Model.Error.Code.empty())
            {
                File.Brushes.Add(Index, MoveTemp(Model));
                continue;
            }
        }
        // A navigation mesh actor carries the mesh a navigation build made.
        if (File.Actors.Contains(Index) && Node.Class.Contains(TEXT(".RecastNavMesh"), ESearchCase::CaseSensitive) &&
            Tail >= 4 && Zeros(4))
        {
            Node.EngineTail = TEXT("the navigation mesh of a navigation build");
            continue;
        }
        P4MapKit::FStaticMeshComponentNative Component = P4MapKit::ReadStaticMeshComponentNative(
            File.Body, Node.TailStart, Node.TailEnd);
        if (!Component.Error.Code.empty())
        {
            // An instanced component, with the tree of a hierarchical one or without.
            bool Found = false;
            for (const bool Tree : {true, false})
            {
                P4MapKit::FInstancedMeshNative Instanced = P4MapKit::ReadInstancedMeshNative(
                    File.Body, Node.TailStart, Node.TailEnd, Tree);
                if (!Instanced.Error.Code.empty())
                    continue;
                Component = Instanced.Mesh;
                Found = true;
                File.Instanced.Add(Index, MoveTemp(Instanced));
                break;
            }
            if (!Found)
                continue;
        }
        Node.Lods = int32(Component.Lods);
        Node.LightingIds.Reset();
        for (const auto& Id : Component.LightingIds)
            Node.LightingIds.Add(FString::Printf(TEXT("%08x %08x %08x %08x"), Id[0], Id[1], Id[2], Id[3]));
    }
    return true;
}

// The kind of variable the editor makes for a type of class variable. None for a type not brought over.
FName VariableKind(const std::string& Type)
{
    if (Type == "Int64Property")
        return UEdGraphSchema_K2::PC_Int64;
    if (Type == "IntProperty")
        return UEdGraphSchema_K2::PC_Int;
    if (Type == "FloatProperty")
        return UEdGraphSchema_K2::PC_Float;
    if (Type == "NameProperty")
        return UEdGraphSchema_K2::PC_Name;
    if (Type == "StrProperty")
        return UEdGraphSchema_K2::PC_String;
    return NAME_None;
}

// What the file holds that the import does not bring over yet. Empty when it brings over everything.
FString Refusal(const FFile& File)
{
    for (const std::int32_t Index : File.WorldNative.StreamingLevels)
        if (!File.Export(Index) || File.Node(Index).Outer != File.World || !File.Node(Index).PlainTail ||
            !File.Node(Index).Undecoded.IsEmpty())
            return TEXT("level-import-streaming-level-unreadable");
    if (File.WorldNative.ExtraReferencedObjects.size())
        return TEXT("level-import-extra-referenced-objects-not-brought-over-yet");
    if (File.LevelNative.ModelComponents.size() || !File.ModelNative.Empty())
        return TEXT("level-import-bsp-geometry-not-brought-over-yet");
    if (File.LevelNative.NavListStart || File.LevelNative.NavListEnd)
        return TEXT("level-import-navigation-list-not-brought-over-yet");
    if (File.LevelNative.DistanceFieldValues)
        return TEXT("level-import-precomputed-distance-field-not-brought-over-yet");
    for (const auto& Tag : File.Node(File.Level).Properties.Properties)
        if (Tag.Name != "Model" && Tag.Name != "LevelScriptActor" && Tag.Name != "LevelBuildDataId" &&
            Tag.Name != "MapBuildData" && Tag.Name != "WorldSettings" && Tag.Name != "StreamingTextureGuids")
            return TEXT("level-import-level-setting-not-brought-over-yet: ") + Text(Tag.Name);
    if (File.ScriptClass)
    {
        const auto& Class = File.ClassNative;
        if (!P4SameText(File.Ref(Class.Super), TEXT("/Script/Engine.LevelScriptActor")))
            return TEXT("level-import-level-script-parent-not-brought-over-yet: ") + File.Ref(Class.Super);
        if (!File.ProvenEmptyBeginPlay &&
            (Class.Children.size() || Class.Functions.size() || Class.ScriptBytes || Class.Interfaces ||
                File.Node(File.ScriptClass).Properties.Properties.size()))
            return TEXT("level-import-level-script-code-not-brought-over-yet");
        for (const auto& Variable : Class.Variables)
            if (VariableKind(Variable.Type).IsNone() || Variable.ArrayDim != 1 || Variable.RepNotify != "None")
                return TEXT("level-import-level-script-variable-not-brought-over-yet: ") + Text(Variable.Type) +
                    TEXT(" ") + Text(Variable.Name);
        for (const auto& Default : File.Node(File.ScriptDefault).Values.Fields)
        {
            const bool Known = Class.Variables.end() !=
                std::find_if(Class.Variables.begin(), Class.Variables.end(),
                    [&](const P4MapKit::FClassVariable& Variable) { return Variable.Name == Default.first; });
            if (!Known ||
                (Default.second.Kind != FDecodedValue::EKind::Number &&
                    Default.second.Kind != FDecodedValue::EKind::String))
                return TEXT("level-import-level-script-default-not-brought-over-yet: ") + Text(Default.first);
        }
    }
    // Everything else has to be the level's own, inside its world settings, or one of its actors.
    int32 Others = 0, Unread = 0;
    TSet<FString> Classes;
    FString First, Mine;
    for (int32 Index = 1; Index <= File.Nodes.Num(); ++Index)
    {
        const FNode& Node = File.Node(Index);
        const bool Own = Index == File.World || Index == File.Level || Index == File.Model ||
            Index == File.ScriptClass || File.EmptyLevelFunctions.Contains(Index);
        if (File.ProjectionExcluded.Contains(Index))
            continue;
        const bool Streaming = std::find(File.WorldNative.StreamingLevels.begin(),
                                   File.WorldNative.StreamingLevels.end(),
                                   Index) != File.WorldNative.StreamingLevels.end();
        if (Own || Streaming || Index == File.Settings || Index == File.ScriptDefault || Index == File.ScriptActor ||
            File.Below(Index, File.Settings) || File.InActor(Index))
        {
            if (!Mine.IsEmpty())
                continue;
            if (!Node.Undecoded.IsEmpty())
                Mine = TEXT("level-import-properties-not-read: ") + Node.Path + TEXT(" (") + Node.Class + TEXT("): ") +
                    Node.Undecoded;
            else if (!Own && !Node.PlainTail && Node.Lods < 0 && Node.EngineTail.IsEmpty() &&
                !File.Brushes.Contains(Index) && !File.Foliage.Contains(Index))
                Mine = TEXT("level-import-native-data-not-brought-over-yet: ") + Node.Path + TEXT(" (") + Node.Class +
                    TEXT(")");
            continue;
        }
        if (!Others)
            First = Node.Path + TEXT(" (") + Node.Class + TEXT(")");
        ++Others;
        Classes.Add(Node.Class);
        if (!Node.Undecoded.IsEmpty() || !Node.PlainTail)
            ++Unread;
    }
    if (Others)
        return FString::Printf(
            TEXT(
                "level-import-objects-not-brought-over-yet: %d of %d classes, %d of them with data that is not read yet, the first %s"),
            Others, Classes.Num(), Unread, *First);
    if (!Mine.IsEmpty())
        return Mine;
    // Instances are brought over as foliage: each instanced component has to be the one component of one
    // kind of foliage of the foliage actor it is in. A kind with no component is one the foliage actor
    // lists and never placed: the editor's foliage mode adds a kind to the list first and makes its
    // component when the first instance is placed.
    TSet<int32> Placed;
    for (const auto& Entry : File.Foliage)
        for (const P4MapKit::FFoliagePlaced& One : Entry.Value.Placed)
        {
            if (!One.Component && One.Type < 0)
                continue;
            const P4MapKit::FInstancedMeshNative* Instanced = File.Instanced.Find(One.Component);
            if (One.Type >= 0 || !Instanced || !Instanced->Hierarchical ||
                File.Node(One.Component).Outer != Entry.Key || Placed.Contains(One.Component))
                return TEXT("level-import-foliage-not-brought-over-yet: ") + File.Node(Entry.Key).Path;
            Placed.Add(One.Component);
        }
    for (const auto& Entry : File.Instanced)
        if (!Placed.Contains(Entry.Key))
            return TEXT("level-import-instanced-component-not-brought-over-yet: ") + File.Node(Entry.Key).Path +
                TEXT(" (") + File.Node(Entry.Key).Class + TEXT(")");
    return Mine;
}
} // namespace P4LevelContent
