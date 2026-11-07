// What a level file says, one line for each thing, in an order that does not depend on the file's.

#include "P4LevelContentInternal.h"
#include "P4Common.h"
#include "P4GameNames.h"

namespace P4LevelContent
{
// Takes out of a value every struct that says none of its members: loading such a struct changes nothing.
// A list keeps its places. Returns whether the value itself is such a struct.
bool Prune(FDecodedValue& Value)
{
    if (Value.Kind == FDecodedValue::EKind::Array)
    {
        for (FDecodedValue& Item : Value.Items)
            Prune(Item);
        return false;
    }
    if (Value.Kind != FDecodedValue::EKind::Object)
        return false;
    for (std::size_t Index = Value.Fields.size(); Index-- > 0;)
        if (Prune(Value.Fields[Index].second))
            Value.Fields.erase(Value.Fields.begin() + Index);
    return Value.Fields.empty();
}

// Whether a value is a delegate that calls nothing but the object it is on, the object at Path.
bool CallsItself(const FDecodedValue& Value, const FString& Path)
{
    if (Value.Kind != FDecodedValue::EKind::Array || Value.Items.empty())
        return false;
    for (const FDecodedValue& Item : Value.Items)
    {
        const FDecodedValue* Target = Item.Kind == FDecodedValue::EKind::Object && Item.Fields.size() == 2 &&
                Item.Field("function")
            ? Item.Field("object")
            : nullptr;
        if (!Target || Target->Kind != FDecodedValue::EKind::String || !P4SameText(Text(Target->Text), Path))
            return false;
    }
    return true;
}

// Puts the names of a value's members in small letters, all the way down. The engine tells the names of
// properties apart without regard to case, and a type made from the game's own descriptions can spell
// a member with other capitals than the game's files do.
void SmallNames(FDecodedValue& Value)
{
    for (auto& Field : Value.Fields)
    {
        for (char& Letter : Field.first)
            if (Letter >= 'A' && Letter <= 'Z')
                Letter += 'a' - 'A';
        SmallNames(Field.second);
    }
    for (FDecodedValue& Item : Value.Items)
        SmallNames(Item);
}

// A reference to what the game does not ship is, in the game, a reference to nothing, and a file made
// here does not say a reference to nothing where that is what the object starts with. Such a reference
// is taken out: a struct loses the member, a list keeps the place as nothing. A struct that is in a
// list is another matter: a file says every member of such a struct, a reference to nothing too,
// because there is nothing it could start with. There the member stays, as nothing. Returns whether
// the value itself is one.
bool Unship(FDecodedValue& Value, bool InList = false)
{
    if (Value.Kind == FDecodedValue::EKind::String && Value.Text == Unshipped)
        return true;
    for (std::size_t Index = Value.Fields.size(); Index-- > 0;)
        if (Unship(Value.Fields[Index].second, InList))
        {
            if (InList)
                Value.Fields[Index].second = FDecodedValue::Atom(FDecodedValue::EKind::Null, std::string());
            else
                Value.Fields.erase(Value.Fields.begin() + Index);
        }
    for (FDecodedValue& Item : Value.Items)
        if (Unship(Item, true))
            Item = FDecodedValue::Atom(FDecodedValue::EKind::Null, std::string());
    return false;
}

bool NativeDerivedTerrainTracking(const FString& ComponentClass, const FDecodedValue& Member)
{
    // DetermineUCSModifiedProperties rebuilds this list against the current archetype.
    // The game's terrain field is already classified as game-derived below; its native
    // member-reference tracking is the same bookkeeping, not another authored value.
    // Bind this classification to the qualified SDK storage and the complete native
    // member identity. Other members (including Blueprint GUIDs) remain compared.
    const FString NativeClass = TEXT(P4_GAME_SCRIPT_PACKAGE ".StaticCollisionComponent");
    if (ComponentClass != NativeClass || Member.Kind != FDecodedValue::EKind::Object || Member.Fields.size() != 3)
        return false;
    const FDecodedValue *Parent = Member.Field("MemberParent"), *Name = Member.Field("MemberName"),
                        *Guid = Member.Field("MemberGuid");
    if (!Parent ||
        (Parent->Kind != FDecodedValue::EKind::String && Parent->Kind != FDecodedValue::EKind::HardReference) ||
        Parent->Text != P4_GAME_SCRIPT_PACKAGE ".StaticCollisionComponent" || !Name ||
        (Name->Kind != FDecodedValue::EKind::String && Name->Kind != FDecodedValue::EKind::Name) ||
        Name->Text != "C4TerrainTagContainer" || Name->NameNumber != 0 || !Guid ||
        Guid->Kind != FDecodedValue::EKind::Object || Guid->Fields.size() != 4)
        return false;
    for (const char* Part : {"A", "B", "C", "D"})
    {
        const FDecodedValue* Value = Guid->Field(Part);
        if (!Value || Value->Kind != FDecodedValue::EKind::Number || Value->Text != "0")
            return false;
    }
    const UClass* Class = FindObject<UClass>(nullptr, *NativeClass);
    const FStructProperty* Property = Class ? FindFProperty<FStructProperty>(Class, TEXT("C4TerrainTagContainer"))
                                            : nullptr;
    if (!Class || !Class->HasAnyClassFlags(CLASS_Native) || !Class->IsChildOf(UStaticMeshComponent::StaticClass()) ||
        !Property || Property->GetOwnerStruct() != Class || Property->ArrayDim != 1 ||
        !Property->HasAnyPropertyFlags(CPF_Edit) ||
        Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated | CPF_EditorOnly) || !Property->Struct ||
        Property->Struct->GetPathName() != TEXT("/Script/Engine.C4TerrainTagContainer") ||
        Property->Struct->GetStructureSize() != 16)
        return false;
    const FArrayProperty* Tags = FindFProperty<FArrayProperty>(Property->Struct, TEXT("TerrainTags"));
    const FStructProperty* Tag = Tags ? CastField<FStructProperty>(Tags->Inner) : nullptr;
    const FUInt32Property* Number = Tag && Tag->Struct ? FindFProperty<FUInt32Property>(Tag->Struct, TEXT("TerrainTag"))
                                                       : nullptr;
    return Tags && Tags->ArrayDim == 1 && Tag && Tag->ArrayDim == 1 &&
        Tag->Struct->GetPathName() == TEXT("/Script/Engine.C4TerrainTag") && Number && Number->ArrayDim == 1 &&
        Number->ElementSize == 4;
}

// What a file says, one line for each thing, in an order that does not depend on the file's. What the
// engine makes for itself goes to EngineMade and is not part of what the level is.
void Lines(
    const FFile& File, TArray<FString>& Out, TArray<FString>& EngineMade, const TSet<int32>* Included, bool FoldNames)
{
    const auto Ref = [&](int32 Index)
    {
        const FString Path = File.Ref(Index);
        return FoldNames ? Path.ToLower() : Path;
    };
    // The game ships packages whose object differs from the package name in case only, and
    // Unreal names ignore case, so a folded comparison folds the object paths a value names too.
    const auto FoldPaths = [&](const FString& Said)
    {
        if (!FoldNames)
            return Said;
        FString Folded = Said;
        for (int32 From = Folded.Find(TEXT("/Game/"), ESearchCase::CaseSensitive); From != INDEX_NONE;
            From = Folded.Find(TEXT("/Game/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, From + 1))
        {
            int32 End = From;
            while (End < Folded.Len() && Folded[End] != TEXT(' ') && Folded[End] != TEXT(',') &&
                Folded[End] != TEXT('"') && Folded[End] != TEXT('\''))
                ++End;
            Folded = Folded.Left(From) + Folded.Mid(From, End - From).ToLower() + Folded.Mid(End);
        }
        return Folded;
    };
    const auto Refs = [&](const std::vector<std::int32_t>& Indices)
    {
        FString Joined;
        for (const std::int32_t Index : Indices)
            Joined += (Joined.IsEmpty() ? TEXT("") : TEXT(", ")) + Ref(Index);
        return Joined;
    };
    for (int32 Index = 1; Index <= File.Nodes.Num(); ++Index)
    {
        if (Included && !Included->Contains(Index))
            continue;
        const FNode& Node = File.Node(Index);
        const FString At = (FoldNames ? Node.Path.ToLower() : Node.Path) + TEXT(" | ");
        const UClass* PropertyClass = FoldNames ? FindObject<UClass>(nullptr, *Node.Class) : nullptr;
        // What kind of object it is to its package: public, a template, a default part of its owner. And
        // what it is made from. A file says only what differs from that, so two files say the same
        // only when their objects are made from the same.
        Out.Add(At + TEXT("class ") + (FoldNames ? Node.Class.ToLower() : Node.Class) +
            FString::Printf(TEXT(", kind %08x"),
                Node.Flags &
                    uint32(RF_Public | RF_Standalone | RF_ClassDefaultObject | RF_ArchetypeObject |
                        RF_DefaultSubObject | RF_InheritableComponentTemplate)));
        Out.Add(At + MadeFrom + Ref(Node.Template));
        if (!Node.ObjectGuid.IsEmpty())
            Out.Add(At + TEXT("id for lazy references ") + Node.ObjectGuid);
        if (File.ProvenEmptyBeginPlay && Index == File.ScriptDefault)
            EngineMade.Add(At + TEXT("compiler UberGraphFrame: captured bookkeeping; rebuilt by the engine"));
        // What a material or material instance caches is worked out by the editor when it saves, from its
        // graph and its parent, and so is what it writes after its properties.
        const bool Material = P4SameText(Node.Class, TEXT("/Script/Engine.Material")) ||
            P4SameText(Node.Class, TEXT("/Script/Engine.MaterialInstanceConstant")) ||
            P4SameText(Node.Class, TEXT("/Script/Landscape.LandscapeMaterialInstanceConstant"));
        for (const auto& Field : Node.Values.Fields)
        {
            const bool Engine = (Index == File.Level &&
                                    (Field.first == "LevelBuildDataId" || Field.first == "MapBuildData" ||
                                        Field.first == "StreamingTextureGuids")) ||
                (Material &&
                    (Field.first == "CachedExpressionData" || Field.first == "CachedReferencedTextures" ||
                        Field.first == "TextureStreamingData"));
            if (Field.first == "BodyInstance" && Field.second.Kind == FDecodedValue::EKind::Object)
            {
                // When the engine makes a body it turns the project's largest angular velocity into
                // radians and back and keeps what comes out, which is not quite what went in.
                FDecodedValue Body = Field.second;
                const float Setting = UPhysicsSettings::Get()->MaxAngularVelocity,
                            Turned = FMath::RadiansToDegrees(FMath::DegreesToRadians(Setting));
                const int32 Found = Body.Fields.empty()
                    ? INDEX_NONE
                    : int32(std::find_if(Body.Fields.begin(), Body.Fields.end(),
                                [](const std::pair<std::string, FDecodedValue>& Member)
                                { return Member.first == "MaxAngularVelocity"; }) -
                          Body.Fields.begin());
                if (Found != INDEX_NONE && Found < int32(Body.Fields.size()) && Turned != Setting &&
                    FCString::Atof(*Text(Body.Fields[Found].second.Text)) == Turned)
                {
                    EngineMade.Add(
                        At + TEXT("BodyInstance.MaxAngularVelocity = ") + Text(Body.Fields[Found].second.Text));
                    Body.Fields.erase(Body.Fields.begin() + Found);
                    if (!Prune(Body))
                        Out.Add(At + Text(Field.first).ToLower() + TEXT(" = ") + Text(P4MapKit::DecodedJson(Body)));
                    continue;
                }
            }
            // The terrain tags of a collision component are made by the game's own code from the
            // component's mesh. A construction script run does not keep them, in the game's editor either.
            // The largest distance a component is drawn at is worked out by the engine from the
            // level's cull distance volumes.
            // A component keeps which of its properties a construction script run changed. The largest
            // distance it is drawn at is among them when the map's cull distance volumes were at work
            // during a run, which depends on what else was loaded: the engine's doing, as the distance is.
            if (Field.first == "UCSModifiedProperties" && Field.second.Kind == FDecodedValue::EKind::Array)
            {
                FDecodedValue List = Field.second;
                int32 Distance = 0, Terrain = 0;
                for (std::size_t Slot = List.Items.size(); Slot-- > 0;)
                {
                    const FDecodedValue* Member = List.Items[Slot].Field("MemberName");
                    if (Member && Member->Text == "CachedMaxDrawDistance")
                    {
                        List.Items.erase(List.Items.begin() + Slot);
                        ++Distance;
                    }
                    else if (NativeDerivedTerrainTracking(Node.Class, List.Items[Slot]))
                    {
                        List.Items.erase(List.Items.begin() + Slot);
                        ++Terrain;
                    }
                }
                if (Distance || Terrain)
                {
                    if (Distance)
                        EngineMade.Add(At + TEXT("a construction script run changed cachedmaxdrawdistance"));
                    if (Terrain)
                        EngineMade.Add(At + TEXT("native construction tracking of game-derived C4TerrainTagContainer"));
                    P4CanonicalOriginalEngineTracking(FindObject<UClass>(nullptr, *Node.Class), List);
                    if (!List.Items.empty())
                        Out.Add(At + TEXT("ucsmodifiedproperties = ") + Text(P4MapKit::DecodedJson(List)));
                    continue;
                }
            }
            bool GameMade = (Field.first == "C4TerrainTagContainer" &&
                                !Node.Class.StartsWith(TEXT("/Script/Landscape."), ESearchCase::CaseSensitive)) ||
                Field.first == "CachedMaxDrawDistance";
            // The game's cook keeps an empty reference whose default is not empty in the game's engine (a physics
            // asset's thumbnail info). Here the default is empty, so this engine's cook leaves it out: the same
            // in effect. An editor-only property is left out by this engine's cook too.
            if (Field.second.Kind == FDecodedValue::EKind::Null)
                if (const UClass* Owner = FindObject<UClass>(nullptr, *Node.Class))
                    if (const FProperty* Declared = Owner->FindPropertyByName(FName(*Text(Field.first))))
                    {
                        const FObjectPropertyBase* Reference = CastField<FObjectPropertyBase>(Declared);
                        GameMade = GameMade || Declared->HasAnyPropertyFlags(CPF_EditorOnly) ||
                            (Reference && Declared->ArrayDim == 1 &&
                                !Reference->GetObjectPropertyValue_InContainer(Owner->GetDefaultObject()));
                    }
            // A delegate of a component that calls the component itself is bound by the class's own code.
            GameMade = GameMade || CallsItself(Field.second, Node.Path);
            // The engine tells property names apart without regard to case, and a class made from the
            // game's own descriptions can spell one with other capitals than the game's files do.
            FDecodedValue Said = Field.second;
            if (Unship(Said) || Prune(Said))
                continue;
            if (const UClass* DistributionOwner = FindObject<UClass>(nullptr, *Node.Class))
                if (P4CanonicalOriginalParticleDistribution(
                        Said, DistributionOwner->FindPropertyByName(FName(*Text(Field.first)))))
                    EngineMade.Add(At + Text(Field.first) +
                        TEXT(".Distribution = null (native initialized default omitted by cook)"));
            if (Field.first == "UCSModifiedProperties")
                P4CanonicalOriginalEngineTracking(FindObject<UClass>(nullptr, *Node.Class), Said);
            // Object references and FNames ignore case in Unreal. Strings keep
            // their spelling; the reflected property's type determines this.
            if (PropertyClass)
                P4ComparisonNames(Said, PropertyClass->FindPropertyByName(FName(*Text(Field.first))));
            (Engine || GameMade ? EngineMade : Out)
                .Add(At + Text(Field.first).ToLower() + TEXT(" = ") + Text(P4MapKit::DecodedJson(Said)));
        }
        if (Index == File.World)
        {
            Out.Add(At + TEXT("level ") + Ref(File.WorldNative.PersistentLevel));
            Out.Add(At + TEXT("streaming levels ") + Refs(File.WorldNative.StreamingLevels));
            Out.Add(At + TEXT("extra referenced objects ") + Refs(File.WorldNative.ExtraReferencedObjects));
        }
        else if (Index == File.Level)
        {
            const auto& Level = File.LevelNative;
            // A cook leaves the place of an actor it does not keep empty. What was there is not in the file.
            // The engine puts the level script's actor and every child actor at the end of the list each
            // time it makes them, so where they stand is the engine's doing.
            std::vector<std::int32_t> Kept, Placed;
            int32 Gaps = 0;
            for (const std::int32_t Actor : Level.Actors)
            {
                if (!Actor)
                    ++Gaps;
                else if (Actor == File.ScriptActor || File.InChildActor(Actor))
                    Placed.push_back(Actor);
                else
                    Kept.push_back(Actor);
            }
            Out.Add(At + TEXT("actors ") + Refs(Kept));
            EngineMade.Add(At +
                FString::Printf(TEXT("%d places of actors the cook did not keep; placed by the engine: "), Gaps) +
                Refs(Placed));
            Out.Add(At + TEXT("model ") + Ref(Level.Model));
            Out.Add(At + TEXT("model components ") + Refs(Level.ModelComponents));
            Out.Add(At + TEXT("level script actor ") + Ref(Level.LevelScriptActor));
            Out.Add(At +
                FString::Printf(TEXT("navigation list %d %d, precomputed distance field values %d"), Level.NavListStart,
                    Level.NavListEnd, int32(Level.DistanceFieldValues)));
            EngineMade.Add(At + TEXT("saved under ") + Text(Level.Protocol) + TEXT(" ") + Text(Level.Host) + TEXT(" ") +
                Text(Level.Map) + TEXT(" ") + Text(Level.Portal) +
                FString::Printf(TEXT(" %d %d"), Level.Port, Level.Valid));
        }
        else if (Index == File.Model)
        {
            const auto& Model = File.ModelNative;
            Out.Add(At +
                FString::Printf(
                    TEXT("geometry %d %d %d %d %d %d, shared sides %d, root outside %d, linked %d, unique vertices %d"),
                    int32(Model.Vectors.size()), int32(Model.Points.size()), int32(Model.Nodes.size()),
                    int32(Model.Surfaces.size()), int32(Model.Vertices.size()), int32(Model.BufferVertices),
                    Model.SharedSides, int32(Model.RootOutside), int32(Model.Linked), Model.UniqueVertices));
            // A model that has had a lighting build keeps a first set of that build's settings, also when
            // it has no surface to use them.
            EngineMade.Add(At +
                FString::Printf(TEXT("%d sets of lighting build settings"), int32(Model.LightmassSettings.size())));
            EngineMade.Add(At +
                FString::Printf(TEXT("lighting id %08x %08x %08x %08x"), Model.LightingGuid[0], Model.LightingGuid[1],
                    Model.LightingGuid[2], Model.LightingGuid[3]));
        }
        else if (Index == File.ScriptClass)
        {
            const auto& Class = File.ClassNative;
            Out.Add(At + TEXT("parent ") + Ref(Class.Super) + TEXT(", within ") + Ref(Class.Within) +
                TEXT(", configuration ") + Text(Class.ConfigName) + TEXT(", default object ") +
                Ref(Class.DefaultObject));
            Out.Add(At +
                FString::Printf(
                    TEXT(
                        "class flags %08x, cooked %d, %d functions, %d bytes of code, %d interfaces, %d other members"),
                    Class.ClassFlags, int32(Class.Cooked), int32(Class.Functions.size()), int32(Class.ScriptBytes),
                    int32(Class.Interfaces), int32(Class.Children.size())));
            for (const auto& Variable : Class.Variables)
                Out.Add(At + TEXT("variable ") + Text(Variable.Type) + TEXT(" ") + Text(Variable.Name) +
                    FString::Printf(TEXT(" x%d of %d bytes, flags %016llx"), Variable.ArrayDim, Variable.ElementSize,
                        Variable.PropertyFlags));
        }
        else if (const P4MapKit::FFoliageActorNative* Foliage = File.Foliage.Find(Index))
        {
            for (const P4MapKit::FFoliagePlaced& One : Foliage->Placed)
                Out.Add(At + TEXT("foliage of ") + Ref(One.Type) + TEXT(" drawn by ") + Ref(One.Component));
        }
        else if (const P4MapKit::FInstancedMeshNative* Instanced = File.Instanced.Find(Index))
        {
            const auto Numbers = [](const float* Values, std::size_t Count)
            {
                FString Joined;
                for (std::size_t Place = 0; Place < Count; ++Place)
                    Joined += FString::Printf(TEXT(" %.9g"), Values[Place]);
                return Joined;
            };
            EngineMade.Add(At + FString::Printf(TEXT("%d levels of detail in the lighting build data: "), Node.Lods) +
                FString::Join(Node.LightingIds, TEXT(", ")));
            Out.Add(At +
                FString::Printf(TEXT("instances: %d, cooked %d, with a tree %d, %d numbers of their own"),
                    int32(Instanced->Instances.size()), int32(Instanced->Cooked), int32(Instanced->Hierarchical),
                    int32(Instanced->Custom.size())));
            for (int32 Row = 0; Row < int32(Instanced->Instances.size()); ++Row)
                Out.Add(
                    At + FString::Printf(TEXT("instance %d ="), Row) + Numbers(Instanced->Instances[Row].data(), 16));
            if (Instanced->Custom.size())
                Out.Add(At + TEXT("instance numbers =") + Numbers(Instanced->Custom.data(), Instanced->Custom.size()));
            // What is drawn is built by the engine from the instances. Where each is in the light map is
            // a lighting build's.
            Out.Add(At +
                FString::Printf(TEXT("drawn: %d, in 16-bit reals %d, %d numbers each, %llu bytes"), Instanced->Drawn,
                    int32(Instanced->HalfReals), Instanced->CustomPerInstance,
                    static_cast<unsigned long long>(Instanced->DrawnBytes)));
            for (int32 Row = 0; Row < int32(Instanced->Origins.size()); ++Row)
            {
                Out.Add(At + FString::Printf(TEXT("drawn %d = at"), Row) + Numbers(Instanced->Origins[Row].data(), 4) +
                    TEXT(", rows") + Numbers(Instanced->Rows[Row].data(), 12));
                EngineMade.Add(At +
                    FString::Printf(TEXT("drawn %d is in the light map of a lighting build at %d %d %d %d"), Row,
                        int32(Instanced->LightMaps[Row][0]), int32(Instanced->LightMaps[Row][1]),
                        int32(Instanced->LightMaps[Row][2]), int32(Instanced->LightMaps[Row][3])));
            }
            if (Instanced->DrawnCustom.size())
                Out.Add(At + TEXT("drawn numbers =") +
                    Numbers(Instanced->DrawnCustom.data(), Instanced->DrawnCustom.size()));
            for (int32 Row = 0; Row < int32(Instanced->Clusters.size()); ++Row)
            {
                const P4MapKit::FInstanceCluster& Cluster = Instanced->Clusters[Row];
                Out.Add(At +
                    FString::Printf(TEXT("cluster %d = children %d to %d, instances %d to %d, box"), Row,
                        Cluster.FirstChild, Cluster.LastChild, Cluster.FirstInstance, Cluster.LastInstance) +
                    Numbers(Cluster.Min.data(), 3) + Numbers(Cluster.Max.data(), 3) + TEXT(", scale") +
                    Numbers(Cluster.MinScale.data(), 3) + Numbers(Cluster.MaxScale.data(), 3));
            }
        }
        else if (Node.Lods >= 0)
        {
            // A lighting build gives a component its levels of detail and their ids.
            EngineMade.Add(At + FString::Printf(TEXT("%d levels of detail in the lighting build data: "), Node.Lods) +
                FString::Join(Node.LightingIds, TEXT(", ")));
        }
        else if (const P4MapKit::FModelNative* Shape = File.Brushes.Find(Index))
        {
            Out.Add(At +
                FString::Printf(
                    TEXT(
                        "bounds %.9g %.9g %.9g, %.9g %.9g %.9g, %.9g; shared sides %d, root outside %d, linked %d, unique vertices %d, buffer vertices %d"),
                    Shape->Bounds[0], Shape->Bounds[1], Shape->Bounds[2], Shape->Bounds[3], Shape->Bounds[4],
                    Shape->Bounds[5], Shape->Bounds[6], Shape->SharedSides, int32(Shape->RootOutside),
                    int32(Shape->Linked), Shape->UniqueVertices, int32(Shape->BufferVertices)));
            for (int32 Row = 0; Row < int32(Shape->Vectors.size()); ++Row)
                Out.Add(At +
                    FString::Printf(TEXT("vector %d = %.9g %.9g %.9g"), Row, Shape->Vectors[Row][0],
                        Shape->Vectors[Row][1], Shape->Vectors[Row][2]));
            for (int32 Row = 0; Row < int32(Shape->Points.size()); ++Row)
                Out.Add(At +
                    FString::Printf(TEXT("point %d = %.9g %.9g %.9g"), Row, Shape->Points[Row][0],
                        Shape->Points[Row][1], Shape->Points[Row][2]));
            for (int32 Row = 0; Row < int32(Shape->Nodes.size()); ++Row)
            {
                const P4MapKit::FModelNode& Polygon = Shape->Nodes[Row];
                Out.Add(At +
                    FString::Printf(
                        TEXT("polygon %d = plane %.9g %.9g %.9g %.9g, %d vertices from %d, surface %d, flags %d"), Row,
                        Polygon.Plane[0], Polygon.Plane[1], Polygon.Plane[2], Polygon.Plane[3], int32(Polygon.Vertices),
                        Polygon.VertexPool, Polygon.Surface, int32(Polygon.Flags)));
            }
            for (int32 Row = 0; Row < int32(Shape->Surfaces.size()); ++Row)
            {
                const P4MapKit::FModelSurface& Surface = Shape->Surfaces[Row];
                Out.Add(At +
                    FString::Printf(
                        TEXT(
                            "surface %d = material %s, flags %08x, base %d, normal %d, texture %d %d, brush polygon %d, plane %.9g %.9g %.9g %.9g, light map scale %.9g, of "),
                        Row, *Ref(Surface.Material), Surface.PolyFlags, Surface.Base, Surface.Normal, Surface.TextureU,
                        Surface.TextureV, Surface.BrushPolygon, Surface.Plane[0], Surface.Plane[1], Surface.Plane[2],
                        Surface.Plane[3], Surface.LightMapScale) +
                    Ref(Surface.Actor));
                EngineMade.Add(At +
                    FString::Printf(TEXT("surface %d uses lighting build settings %d"), Row, Surface.LightmassIndex));
            }
            FString Pool;
            for (const P4MapKit::FModelVertex& Vertex : Shape->Vertices)
                Pool += FString::Printf(TEXT(" %d/%d"), Vertex.Point, Vertex.Side);
            Out.Add(At + TEXT("vertex pool") + Pool);
            EngineMade.Add(At +
                FString::Printf(TEXT("%d sets of lighting build settings, lighting id %08x %08x %08x %08x"),
                    int32(Shape->LightmassSettings.size()), Shape->LightingGuid[0], Shape->LightingGuid[1],
                    Shape->LightingGuid[2], Shape->LightingGuid[3]));
        }
        else if (const FTableRows* Table = File.Tables.Find(Index))
        {
            // A table's rows are in an order of their own, and a row says every one of its members:
            // there is nothing a row is made from. So a reference to what the game does not ship is
            // said, as a reference to nothing, where an object would leave it out.
            Out.Add(At + FString::Printf(TEXT("table of %d rows"), Table->Names.Num()));
            for (int32 Row = 0; Row < Table->Names.Num(); ++Row)
            {
                Out.Add(At + FString::Printf(TEXT("row %d is "), Row) + Table->Names[Row]);
                for (const auto& Field : Table->Rows[Row].Fields)
                {
                    const FDecodedValue* TypedRow = Table->TypedComparisonRows.IsValidIndex(Row)
                        ? &Table->TypedComparisonRows[Row]
                        : nullptr;
                    const FDecodedValue* TypedMember = TypedRow ? TypedRow->Field(Field.first) : nullptr;
                    FDecodedValue Member = P4MapKit::TableRowComparison::Value(Field.second, TypedMember, FoldNames);
                    if (Unship(Member, true))
                        Member = FDecodedValue::Atom(FDecodedValue::EKind::Null, std::string());
                    SmallNames(Member);
                    // A row's soft reference is a name too: the game's DT_UIWidgetStory names
                    // WBP_ui_Caveinfo_Entrance.WBP_ui_CaveInfo_Entrance_C, and once the editor knows that
                    // name in another spelling the table made says it that way.
                    Out.Add(At + TEXT("row ") + Table->Names[Row] + TEXT(" > ") + Text(Field.first).ToLower() +
                        TEXT(" = ") + Text(P4MapKit::DecodedJson(Member)));
                }
            }
        }
        else if (File.EmptyLevelFunctions.Contains(Index))
            Out.Add(At + TEXT("proven empty BeginPlay wrapper function"));
        else if (!Node.EngineTail.IsEmpty())
            EngineMade.Add(
                At + Node.EngineTail + FString::Printf(TEXT(", %d bytes"), int32(Node.TailEnd - Node.TailStart)));
        else if (Node.Skinned || Node.Physics || Node.PhysicsBody || Node.ParticleRequired || Node.NativeTail)
        {
            for (const FString& Said : Node.Native)
                Out.Add(At + FoldPaths(Said));
            for (const FString& Said : Node.NativeMade)
                EngineMade.Add(At + FoldPaths(Said));
        }
        else if (!Node.PlainTail)
        {
            // This engine writes a physics asset's collision disable table after its properties, and an
            // empty one is only zeros: the game's file of a physics asset without one says nothing there.
            bool EmptyTable = P4SameText(Node.Class, TEXT("/Script/Engine.PhysicsAsset")) && Node.Undecoded.IsEmpty() &&
                Node.TailEnd - Node.TailStart <= 8;
            for (std::size_t Byte = Node.TailStart; EmptyTable && Byte < Node.TailEnd; ++Byte)
                EmptyTable = File.Body[Byte] == 0;
            (Material || EmptyTable ? EngineMade : Out)
                .Add(At + FString::Printf(TEXT("native data of %d bytes"), int32(Node.TailEnd - Node.TailStart)));
        }
    }
    // The engine names a child actor anew each time its component makes it, so a child actor is told
    // apart by the component that makes it, here and wherever something refers to it.
    TArray<TPair<FString, FString>> Renames;
    for (const int32 Actor : File.Actors)
    {
        const FDecodedValue* Parent = File.Node(Actor).Values.Field("ParentComponent");
        if (Parent && Parent->Kind == FDecodedValue::EKind::String)
            Renames.Emplace(File.Node(Actor).Path, Text(Parent->Text) + TEXT(">child"));
    }
    Renames.Sort(
        [](const TPair<FString, FString>& A, const TPair<FString, FString>& B) { return A.Key.Len() > B.Key.Len(); });
    const auto Rename = [&](FString& Line)
    {
        // More than once, for a child actor of a child actor.
        for (int32 Pass = 0; Pass < 4; ++Pass)
        {
            bool Changed = false;
            for (const TPair<FString, FString>& Pair : Renames)
            {
                int32 From = 0;
                while (
                    (From = Line.Find(Pair.Key, ESearchCase::CaseSensitive, ESearchDir::FromStart, From)) != INDEX_NONE)
                {
                    const int32 After = From + Pair.Key.Len();
                    const TCHAR Next = After < Line.Len() ? Line[After] : TEXT(' '),
                                Last = From > 0 ? Line[From - 1] : TEXT(' ');
                    // Only the whole path, not one that goes on or one that is the end of another.
                    if (FChar::IsAlnum(Next) || Next == TEXT('_') || FChar::IsAlnum(Last) || Last == TEXT('_') ||
                        Last == TEXT('.') || Last == TEXT('/'))
                    {
                        From = After;
                        continue;
                    }
                    Line = Line.Left(From) + Pair.Value + Line.Mid(After);
                    From += Pair.Value.Len();
                    Changed = true;
                }
            }
            if (!Changed)
                break;
        }
    };
    if (Renames.Num())
    {
        for (FString& Line : Out)
            Rename(Line);
        for (FString& Line : EngineMade)
            Rename(Line);
    }
    // An order and a comparison that tell letters of different case apart, as the engine's own do not.
    const auto Before = [](const FString& A, const FString& B) { return A.Compare(B, ESearchCase::CaseSensitive) < 0; };
    Out.Sort(Before);
    EngineMade.Sort(Before);
}
} // namespace P4LevelContent
