// Host test of core/placement_params.h. Without arguments: checks that need no game files. With the path
// of a stock listing (tools/navigation/test_placement_params.py writes it): every listed blob whose slot has a
// layout is decoded and written back byte for byte, and what it decodes to is written for comparing.
#include "core/placement_params.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
using namespace P4MapKit::Placement;

static int Failures = 0;
#define CHECK(X)                                                         \
    do                                                                   \
    {                                                                    \
        if (!(X))                                                        \
        {                                                                \
            std::cerr << "Failed at " << __LINE__ << ": " << #X << '\n'; \
            ++Failures;                                                  \
        }                                                                \
    }                                                                    \
    while (false)

static std::vector<std::string> Lines(const Blob& B)
{
    std::vector<std::string> Out;
    Flatten(B.Root, "", Out);
    return Out;
}
static Value* Path(Blob& B, const std::string& Name)
{
    return B.Root.Find(Name);
}
static std::vector<std::string> Split(const std::string& Text, char By)
{
    std::vector<std::string> Out;
    std::string One;
    std::istringstream In(Text);
    while (std::getline(In, One, By))
        Out.push_back(One);
    return Out;
}

static void Own()
{
    Context Kochappy;
    Kochappy.ComponentChain = {"KochappyAIComponent", "TekiAIComponent", "ObjectAIComponent", "CarrotAIComponent"};
    Kochappy.ActorChain = {"GActor"};
    // A fresh Teki blob, filled by name, written and read back to the same values.
    Blob Teki;
    CHECK(Fresh(Slot::AIStatic, Kochappy, RowVersion, Teki));
    CHECK(Path(Teki, "KochappyAIParameter.bCloseSetting") != nullptr);
    CHECK(Path(Teki, "TekiAIParameter.DropParameter.bEnableFreezeBothDrop") != nullptr);
    const Type* Items = FieldType(Slot::AIStatic, Kochappy, "TekiAIParameter.DropParameter.DropItemParameter");
    CHECK(Items && Items->Is == Kind::Array && Items->Item);
    if (Items && Items->Item)
    {
        Value Drop = EmptyOf(*Items->Item, RowVersion);
        Drop.Find("UniqueId")->Bits = UINT64_C(0x6B12345600000001);
        Drop.Find("MinNum")->Bits = 2;
        Drop.Find("MaxNum")->Bits = 3;
        Drop.Find("DropRatio")->SetFloat(0.5f);
        Value* Spawn = Drop.Find("SpawnMiniInfo");
        *Spawn->Find("DropActor") = NameFromUtf8("/Game/Carrot4/Placeables/Items/GHoney.GHoney_C");
        Spawn->Find("bSetTerritory")->Bits = 0;
        CHECK(Spawn->Find("Territory") != nullptr); // present but not written while bSetTerritory is clear
        Path(Teki, "TekiAIParameter.DropParameter.DropItemParameter")->Items.push_back(Drop);
        Path(Teki, "TekiAIParameter.DropParameter.DebugUniqueIdList")->Items.push_back(Value());
        Value& Listed = Path(Teki, "TekiAIParameter.DropParameter.DebugUniqueIdList")->Items.back();
        Listed.Is = Kind::U64;
        Listed.Bits = UINT64_C(0x6B12345600000001);
    }
    Path(Teki, "TekiAIParameter.Territory")->Find("Radius")->SetFloat(450.f);
    Path(Teki, "KochappyAIParameter.bCloseSetting")->Bits = 1;
    std::vector<std::uint8_t> Bytes;
    std::string Error;
    CHECK(Encode(Slot::AIStatic, Kochappy, Teki, RowVersion, Bytes, Error));
    Blob Back;
    CHECK(Decode(Slot::AIStatic, Kochappy, Bytes, RowVersion, Back, Error));
    CHECK(Back.Exact());
    // The territory of a drop is not in the stream when it is not set, so it does not come back.
    std::vector<std::string> Want = Lines(Teki), Got = Lines(Back);
    Want.erase(std::remove_if(Want.begin(), Want.end(),
                   [](const std::string& L) { return L.find("SpawnMiniInfo.Territory") != std::string::npos; }),
        Want.end());
    CHECK(Want == Got);
    std::vector<std::uint8_t> Again;
    CHECK(Encode(Slot::AIStatic, Kochappy, Back, RowVersion, Again, Error));
    CHECK(Again == Bytes);
    // An older version leaves gated fields out.
    std::vector<std::uint8_t> Old;
    Blob OldBlob;
    CHECK(Fresh(Slot::AIStatic, Kochappy, UINT64_C(0x20211001a), OldBlob));
    CHECK(Path(OldBlob, "KochappyAIParameter.bCloseSetting") == nullptr);
    CHECK(Encode(Slot::AIStatic, Kochappy, OldBlob, UINT64_C(0x20211001a), Old, Error));
    CHECK(Old.size() < Bytes.size());
    // Truncated bytes are refused, and so is a member left out.
    Blob Cut;
    std::vector<std::uint8_t> Short(Bytes.begin(), Bytes.begin() + 40);
    CHECK(!Decode(Slot::AIStatic, Kochappy, Short, RowVersion, Cut, Error));
    Blob Missing = Teki;
    Missing.Root.Members.pop_back();
    CHECK(!Encode(Slot::AIStatic, Kochappy, Missing, RowVersion, Old, Error));
    // A pot: the escape points follow their count, and a count that differs is refused.
    Context Pot;
    Pot.ComponentChain = {"CrackPotAIComponent", "ObjectAIComponent", "CarrotAIComponent"};
    Blob Crack;
    CHECK(Fresh(Slot::AIStatic, Pot, RowVersion, Crack));
    CHECK(Path(Crack, "CrackPotAIParameter.bHiddenBRMesh") != nullptr &&
        Path(Crack, "GateAIComponent.RareDropParameter") == nullptr);
    Path(Crack, "ObjectAIParameter.EscapePointSerializeNum")->Bits = 1;
    CHECK(!Encode(Slot::AIStatic, Pot, Crack, RowVersion, Bytes, Error));
    Value Point;
    Point.Is = Kind::Vec;
    Point.Words[0] = Value::FloatBits(10.f);
    Path(Crack, "EscapePoints")->Items.push_back(Point);
    CHECK(Encode(Slot::AIStatic, Pot, Crack, RowVersion, Bytes, Error));
    CHECK(Decode(Slot::AIStatic, Pot, Bytes, RowVersion, Back, Error) && Back.Exact() && Lines(Back) == Lines(Crack));
    // A treasure's dynamic blob: the survivor layout is tried when the plain one does not fit exactly.
    Context Treasure;
    Treasure.ComponentChain = {"OtakaraAIComponent", "ObjectAIComponent"};
    Blob Plain, Survivor;
    CHECK(Fresh(Slot::AIDynamic, Treasure, RowVersion, Plain));
    CHECK(Fresh(Slot::AIDynamic, Treasure, RowVersion, Survivor, 1));
    *Path(Survivor, "OtakaraAIComponent.NpcInfoKey") = NameFromUtf8("SV01");
    CHECK(Encode(Slot::AIDynamic, Treasure, Survivor, RowVersion, Bytes, Error));
    CHECK(Decode(Slot::AIDynamic, Treasure, Bytes, RowVersion, Back, Error) && Back.Variant == 1 && Back.Exact());
    CHECK(Encode(Slot::AIDynamic, Treasure, Plain, RowVersion, Bytes, Error));
    CHECK(Decode(Slot::AIDynamic, Treasure, Bytes, RowVersion, Back, Error) && Back.Variant == 0 && Back.Exact());
    // A slot with no layout for the actor.
    Context Npc;
    Npc.ComponentChain = {"NpcAIComponent", "CarrotAIComponent"};
    for (const std::uint64_t V : {UINT64_C(0x202110079), UINT64_C(0x20211007a), UINT64_C(0x202203319),
             UINT64_C(0x20220331a), UINT64_C(0x202207139), UINT64_C(0x20220713a), RowVersion})
    {
        Blob Options;
        CHECK(Fresh(Slot::AIStatic, Npc, V, Options));
        *Path(Options, "NpcInfoKey") = NameFromUtf8("SV03");
        CHECK(bool(Path(Options, "AIParameter.NpcRoleType")) == (V >= UINT64_C(0x20211007a)));
        CHECK(bool(Path(Options, "AIParameter.DirectLookLocation")) == (V >= UINT64_C(0x20220331a)));
        CHECK(bool(Path(Options, "AIParameter.PlayerWarpPointOffset")) == (V >= UINT64_C(0x20220713a)));
        if (V >= UINT64_C(0x20211007a))
        {
            const Type* Infos = FieldType(Slot::AIStatic, Npc, "AIParameter.NpcAIInfos");
            Value Info = EmptyOf(*Infos->Item, V);
            Info.Find("AIType")->Bits = 2;
            Info.Find("ProgressMin")->Bits = 3;
            Info.Find("ProgressMax")->Bits = 9;
            Path(Options, "AIParameter.NpcAIInfos")->Items.push_back(Info);
            const Type* Quests = FieldType(Slot::AIStatic, Npc, "AIParameter.NpcPlaceCheckQuestStatusList");
            Value Quest = EmptyOf(*Quests->Item, V);
            *Quest.Find("CheckQuestName") = NameFromUtf8("QuestOne");
            Quest.Find("CheckQuestStatusType")->Bits = 4;
            *Quest.Find("PlacementTagName") = NameFromUtf8("Base");
            Path(Options, "AIParameter.NpcPlaceCheckQuestStatusList")->Items.push_back(Quest);
            *Path(Options, "TalkParameter.ContentsName") = NameFromUtf8("\xE3\x81\x82Talk");
            Path(Options, "TalkParameter.CheckDist")->SetFloat(250.0f);
            CHECK(!Path(Options, "AIParameter.DefaultNpcAIInfo") &&
                !Path(Options, "AIParameter.WanderParameter.bRandomWander"));
        }
        CHECK(Encode(Slot::AIStatic, Npc, Options, V, Bytes, Error));
        CHECK(Decode(Slot::AIStatic, Npc, Bytes, V, Back, Error) && Back.Exact() && Lines(Options) == Lines(Back));
    }
    Blob NpcState;
    CHECK(Fresh(Slot::AIDynamic, Npc, RowVersion, NpcState));
    Path(NpcState, "AIParameter.bLeafState")->Bits = 1;
    Path(NpcState, "TalkParameter.TodayTalkNum")->Bits = 7;
    CHECK(Encode(Slot::AIDynamic, Npc, NpcState, RowVersion, Bytes, Error) && Bytes.size() == 16);
    CHECK(
        Decode(Slot::AIDynamic, Npc, Bytes, RowVersion, Back, Error) && Back.Exact() && Lines(NpcState) == Lines(Back));
    Context None;
    Blob Nothing;
    CHECK(!Decode(Slot::AIStatic, None, Bytes, RowVersion, Nothing, Error) && Error == "not covered");
    Slot S;
    CHECK(SlotOf("AI", false, S) && S == Slot::AIDynamic);
    CHECK(!SlotOf("Hash", true, S));
    // Names: narrow, wide and empty ones keep their form.
    Value Wide = NameFromUtf8("\xE3\x81\x82x");
    CHECK(Wide.Form == 2 && NameUtf8(Wide) == "\xE3\x81\x82x");
    Value Latin = NameFromUtf8("caf\xC3\xA9");
    CHECK(Latin.Form == 1 && Latin.Text.size() == 4 && NameUtf8(Latin) == "caf\xC3\xA9");
    CHECK(NameFromUtf8("").Form == 0);
    // Soft paths preserve the asset and subobject strings independently, including wide strings.
    Blob Audio;
    CHECK(Fresh(Slot::CakAudioStatic, None, RowVersion, Audio));
    const Type* AudioTables = FieldType(Slot::CakAudioStatic, None, "CakAudioTableComponent.OverrideAudioTables");
    Value Soft = EmptyOf(*AudioTables->Item, RowVersion);
    *Soft.Find("AssetPathName") = NameFromUtf8("/Game/Audio/Table.Table");
    *Soft.Find("SubPathString") = NameFromUtf8("\xE3\x81\x82:Child");
    Path(Audio, "CakAudioTableComponent.OverrideAudioTables")->Items.push_back(Soft);
    CHECK(Encode(Slot::CakAudioStatic, None, Audio, RowVersion, Bytes, Error));
    CHECK(Decode(Slot::CakAudioStatic, None, Bytes, RowVersion, Back, Error) && Back.Exact());
    CHECK(Lines(Audio) == Lines(Back));
    CHECK(Encode(Slot::CakAudioStatic, None, Back, RowVersion, Again, Error) && Again == Bytes);
    for (std::size_t CutAt = 0; CutAt < Bytes.size(); ++CutAt)
    {
        std::vector<std::uint8_t> Truncated(Bytes.begin(), Bytes.begin() + CutAt);
        CHECK(!Decode(Slot::CakAudioStatic, None, Truncated, RowVersion, Back, Error));
    }
    Blob Aff;
    CHECK(Fresh(Slot::AffordanceStatic, None, RowVersion, Aff));
    const Type* Events = FieldType(Slot::AffordanceStatic, None, "AffordanceEvents");
    Value Event = EmptyOf(*Events->Item, RowVersion);
    Event.Find("bOnceEvent")->Bits = 1;
    Event.Find("UpdateRate")->SetFloat(0.25f);
    *Event.Find("DemoPlayParam") = Soft;
    *Event.Find("LookTriggerActorTag") = NameFromUtf8("LookAt");
    Path(Aff, "AffordanceEvents")->Items.push_back(Event);
    Path(Aff, "CarrotAffordanceComponent.bGlobalOnce")->Bits = 1;
    CHECK(Encode(Slot::AffordanceStatic, None, Aff, RowVersion, Bytes, Error));
    CHECK(Decode(Slot::AffordanceStatic, None, Bytes, RowVersion, Back, Error) && Back.Exact() &&
        Lines(Aff) == Lines(Back));
    Value OldEvent = EmptyOf(*Events->Item, UINT64_C(0x202206029));
    CHECK(!OldEvent.Find("DoEventName") && !OldEvent.Find("NonFollowPikminFlags") &&
        !OldEvent.Find("LookTriggerActorTag"));
    // A DemoTrigger prefix round trips unknown component bytes, so decoding options never destroys
    // the collision component transforms the actor reader has not yet imported.
    Blob Demo;
    CHECK(Fresh(Slot::DemoTriggerStatic, None, RowVersion, Demo));
    Demo.Tail = {1, 2, 3, 4};
    *Path(Demo, "DemoPlayParam") = Soft;
    *Path(Demo, "FlagType") = NameFromUtf8("DemoFlag");
    CHECK(Encode(Slot::DemoTriggerStatic, None, Demo, RowVersion, Bytes, Error));
    CHECK(Decode(Slot::DemoTriggerStatic, None, Bytes, RowVersion, Back, Error) && Back.Tail == Demo.Tail);
    CHECK(Encode(Slot::DemoTriggerStatic, None, Back, RowVersion, Again, Error) && Again == Bytes);
    Context Colliders;
    Colliders.Components = {"DemoBoxColl", "DemoSphereColl", "Other"};
    Blob Shapes;
    CHECK(Fresh(Slot::DemoTriggerStatic, Colliders, RowVersion, Shapes));
    Path(Shapes, "DemoBoxColl")->Find("Rotation")->Words[3] = Value::FloatBits(1.f);
    Path(Shapes, "DemoBoxColl")->Find("BoxExtent")->Words[0] = Value::FloatBits(100.f);
    Path(Shapes, "DemoSphereColl")->Find("SphereRadius")->SetFloat(50.f);
    {
        std::vector<std::string> Got = Lines(Shapes);
        CHECK(std::find(Got.begin(), Got.end(), "DemoBoxColl.Rotation[3]=f:3f800000") != Got.end());
        CHECK(std::find(Got.begin(), Got.end(), "DemoSphereColl.SphereRadius=f:42480000") != Got.end());
    }
    Context OneCollider;
    OneCollider.Components = {"DemoSphereColl"};
    CHECK(Fresh(Slot::DemoTriggerStatic, OneCollider, RowVersion, Back) && !Path(Back, "DemoBoxColl") &&
        Path(Back, "DemoSphereColl"));
    // What a writer fills without a property: placeholders, run-time defaults, counts and component values.
    {
        Context Pellet;
        Pellet.ComponentChain = {"PelplantAIComponent", "CarrotAIComponent"};
        Context Pot;
        Pot.ComponentChain = {"CrackPotAIComponent", "ObjectAIComponent", "CarrotAIComponent"};
        Field Got;
        CHECK(FieldOf(Slot::AIDynamic, Pellet, "runtime_0x84", Got) && Got.Derived &&
            Got.RuntimeDefault == UINT64_C(0xBF800000));
        CHECK(FieldOf(Slot::AIDynamic, Pellet, "runtime_u8_0x7c", Got) && Got.Derived && Got.RuntimeDefault == 1);
        CHECK(FieldOf(Slot::AIStatic, Pot, "EscapePoints", Got) && Got.Derived && Got.ComponentTag == "EscapePoint");
        CHECK(FieldOf(Slot::AIStatic, Pot, "ObjectAIParameter.EscapePointSerializeNum", Got) && !Got.Derived);
        CHECK(FieldOf(Slot::AIDynamic, Pot, "ObjectAIComponent.bWorkDone", Got) && Got.Derived);
        CHECK(FieldOf(Slot::CakMultiplePositionStatic, None, "CakMultiplePositionComponent.ShapesCount", Got) &&
            !Got.Derived && Got.CountOf == "Shapes");
        CHECK(FieldOf(Slot::AffordanceDynamic, None, "AffordanceEvents.bEnded", Got) && Got.Derived &&
            Got.CountOf == "AffordanceEvents");
        CHECK(FieldOf(Slot::ActorStatic, None, "deprecated_word_1", Got) && Got.Derived);
        CHECK(FieldOf(Slot::DemoTriggerStatic, OneCollider, "DemoSphereColl", Got) && Got.Derived &&
            Got.ComponentTransform);
        CHECK(FieldOf(Slot::ActorStatic, None, "GActor.DemoBindName", Got) && !Got.Derived && Got.CountOf.empty());
        Slot Found = Slot::ActorStatic;
        CHECK(Slots().size() == 13 && SlotOf("AI", false, Found) && Found == Slot::AIDynamic &&
            !SlotOf("Hash", true, Found));
        // The object each blob is read from, and how drops are numbered.
        for (const auto& One : Slots())
        {
            if (One.S == Slot::ActorStatic)
                CHECK(One.OwnerClasses.empty() && One.Chain == 'a');
            if (One.S == Slot::AIStatic)
                CHECK(One.Chain == 'c' && One.OwnerProperty == "CarrotAIComponent" && One.OwnerClasses.size() == 5 &&
                    One.OwnerClasses.back() == "CarrotAIComponent");
            if (One.S == Slot::AffordanceDynamic)
                CHECK(One.OwnerClasses == std::vector<std::string>{"CarrotAffordanceComponent"} &&
                    One.OwnerExcept == "AttackAffordanceComponent");
            if (One.S == Slot::LifeDynamic)
                CHECK(One.Chain == 0 && One.OwnerProperty == "LifeComponent");
        }
        Context Beetle;
        Beetle.ComponentChain = {"KoganeAIComponent", "KoganeBaseAIComponent", "TekiAIComponent", "CarrotAIComponent"};
        CHECK(FieldOf(Slot::AIStatic, Beetle, "TekiAIParameter.DropParameter.DropItemParameter", Got) && Got.Drops &&
            Got.DropKey == "drop" && Got.DropList == "TekiAIParameter.DropParameter.DebugUniqueIdList");
        CHECK(FieldOf(Slot::AIStatic, Beetle, "KoganeBaseAIParameter.DropParameter.ParameterList", Got) && !Got.Drops &&
            Got.Of->Item->Members[0].Drops && Got.Of->Item->Members[0].DropKey == "kogane" &&
            Got.Of->Item->Members[0].DropList == "DebugUniqueIdList");
        CHECK(FieldOf(Slot::AIStatic, Pot, "ObjectAIParameter.DropParameter.DropItemParameter", Got) && Got.Drops &&
            Got.DropKey.empty());
    }
    CHECK(Encode(Slot::DemoTriggerStatic, Colliders, Shapes, RowVersion, Bytes, Error));
    CHECK(Decode(Slot::DemoTriggerStatic, Colliders, Bytes, RowVersion, Back, Error) && Back.Exact() &&
        Lines(Shapes) == Lines(Back));
    CHECK(Encode(Slot::DemoTriggerStatic, Colliders, Back, RowVersion, Again, Error) && Again == Bytes);
    CHECK(Decode(Slot::DemoTriggerStatic, None, Bytes, RowVersion, Back, Error) && Back.Tail.size() == 96);
    // Enemy option tails use their native inheritance and preserve version-specific stream lengths.
    Context Big;
    Big.ComponentChain = {"BigUjinkoAIComponent", "UjinkoBaseAIComponent", "TekiAIComponent"};
    Blob BigOld, BigNew;
    CHECK(Fresh(Slot::AIStatic, Big, UINT64_C(0x20211018b), BigOld));
    CHECK(Path(BigOld, "legacy_caution_bool") && !Path(BigOld, "TekiAIParameter.SearchAreaCaution"));
    CHECK(Fresh(Slot::AIStatic, Big, RowVersion, BigNew));
    CHECK(!Path(BigNew, "legacy_caution_bool") && Path(BigNew, "BigUjinkoAIParameter.SearchTagName"));
    *Path(BigNew, "BigUjinkoAIParameter.SearchTagName") = NameFromUtf8("CautionTarget");
    Path(BigNew, "TekiAIParameter.SearchAreaCaution")->Find("SphereRadius")->SetFloat(250.f);
    CHECK(Encode(Slot::AIStatic, Big, BigNew, RowVersion, Bytes, Error));
    CHECK(Decode(Slot::AIStatic, Big, Bytes, RowVersion, Back, Error) && Back.Exact() && Lines(BigNew) == Lines(Back));
    Context Fut;
    Fut.ComponentChain = {"FutakuchiAIComponent", "TekiAIComponent"};
    Blob FutOld, FutNew;
    CHECK(Fresh(Slot::AIStatic, Fut, UINT64_C(0x202206039), FutOld));
    CHECK(!Path(FutOld, "FutakuchiAIParameter.bDissapearVisibleOff"));
    CHECK(Fresh(Slot::AIStatic, Fut, RowVersion, FutNew));
    Path(FutNew, "FutakuchiAIParameter.SearchAreaAttack")->Find("Angle")->SetFloat(45.f);
    Path(FutNew, "FutakuchiAIParameter.bDissapearVisibleOff")->Bits = 1;
    CHECK(Encode(Slot::AIStatic, Fut, FutNew, RowVersion, Bytes, Error));
    CHECK(Decode(Slot::AIStatic, Fut, Bytes, RowVersion, Back, Error) && Back.Exact() && Lines(FutNew) == Lines(Back));
    Context Hari;
    Hari.ComponentChain = {"HariAIComponent", "TekiAIComponent"};
    for (const std::uint64_t V :
        {UINT64_C(0x20220331a), UINT64_C(0x20220401a), UINT64_C(0x202204199), UINT64_C(0x202208319), RowVersion})
    {
        Blob Options;
        CHECK(Fresh(Slot::AIStatic, Hari, V, Options));
        CHECK(bool(Path(Options, "HariAIParameter.bNoRegenerateNeedle")) == (V > UINT64_C(0x20220331a)));
        CHECK(bool(Path(Options, "HariAIParameter.FixedArtilleryWaitTime")) == (V > UINT64_C(0x20220401a)));
        CHECK(bool(Path(Options, "HariAIParameter.FixedArtilleryShotWaitTime")) == (V > UINT64_C(0x202204199)));
        CHECK(bool(Path(Options, "HariAIParameter.CautionParam.bEnableNotice")) == (V > UINT64_C(0x202208319)));
        Path(Options, "HariAIParameter.EscapeArea")->Find("Angle")->SetFloat(120.f);
        Path(Options, "HariAIParameter.BulletParameter.PredictionCoef")->SetFloat(0.75f);
        CHECK(Encode(Slot::AIStatic, Hari, Options, V, Bytes, Error));
        CHECK(Decode(Slot::AIStatic, Hari, Bytes, V, Back, Error) && Back.Exact() && Lines(Options) == Lines(Back));
        CHECK(Encode(Slot::AIStatic, Hari, Back, V, Again, Error) && Again == Bytes);
    }
    Blob AudioState;
    CHECK(Fresh(Slot::CakSimpleStateStatic, None, RowVersion, AudioState));
    Path(AudioState, "CakSimpleStateComponent.bStartOnBeginPlay")->Bits = 1;
    *Path(AudioState, "CakSimpleStateComponent.AudioKeyOnStart") = NameFromUtf8("AmbientStart");
    *Path(AudioState, "CakSimpleStateComponent.AudioKeyOnStop") = Wide;
    Path(AudioState, "CakSimpleStateComponent.Condition")->Bits = 2;
    CHECK(!Path(AudioState, "CakSimpleStateComponent.bStopEntryAudioKeyOnExit"));
    CHECK(Encode(Slot::CakSimpleStateStatic, None, AudioState, RowVersion, Bytes, Error));
    CHECK(Decode(Slot::CakSimpleStateStatic, None, Bytes, RowVersion, Back, Error) && Back.Exact() &&
        Lines(AudioState) == Lines(Back));
    CHECK(Encode(Slot::CakSimpleStateStatic, None, Back, RowVersion, Again, Error) && Again == Bytes);
    Blob Positions;
    CHECK(Fresh(Slot::CakMultiplePositionStatic, None, RowVersion, Positions));
    const Type* PositionArray = FieldType(
        Slot::CakMultiplePositionStatic, None, "CakMultiplePositionComponent.MultiplePositions");
    Value Position = EmptyOf(*PositionArray->Item, RowVersion);
    Position.Find("Rotation")->Words[3] = Value::FloatBits(1.f);
    Position.Find("Translation")->Words[0] = Value::FloatBits(123.f);
    Position.Find("Scale3D")->Words[0] = Position.Find("Scale3D")->Words[1] = Position.Find("Scale3D")->Words[2] =
        Value::FloatBits(1.f);
    Path(Positions, "CakMultiplePositionComponent.MultiplePositions")->Items.push_back(Position);
    CHECK(Encode(Slot::CakMultiplePositionStatic, None, Positions, RowVersion, Bytes, Error));
    CHECK(Decode(Slot::CakMultiplePositionStatic, None, Bytes, RowVersion, Back, Error) && Back.Exact() &&
        Lines(Positions) == Lines(Back));
    Path(Positions, "CakMultiplePositionComponent.ShapesCount")->Bits = 1;
    Positions.Tail = {4, 0, 0, 0, 's', 'h', 'p', 0};
    CHECK(Encode(Slot::CakMultiplePositionStatic, None, Positions, RowVersion, Bytes, Error));
    CHECK(Decode(Slot::CakMultiplePositionStatic, None, Bytes, RowVersion, Back, Error) && !Back.Exact() &&
        Back.Tail == Positions.Tail);
    CHECK(Encode(Slot::CakMultiplePositionStatic, None, Back, RowVersion, Again, Error) && Again == Bytes);
    Context PodContext;
    PodContext.ComponentChain = {"PodAIComponent", "ObjectAIComponent", "CarrotAIComponent"};
    Blob Pod;
    CHECK(Fresh(Slot::AIStatic, PodContext, RowVersion, Pod));
    CHECK(Pod.Root.Members.size() == 1 && !Path(Pod, "ObjectAIParameter.DropParameter.DropItemParameter"));
    Path(Pod, "PodAIParameter.bHeyWakka")->Bits = 1;
    CHECK(Encode(Slot::AIStatic, PodContext, Pod, RowVersion, Bytes, Error) && Bytes.size() == 4);
    CHECK(Decode(Slot::AIStatic, PodContext, Bytes, RowVersion, Back, Error) && Back.Exact() &&
        Lines(Pod) == Lines(Back));
    Context PhotoContext;
    PhotoContext.ComponentChain = {"OjamaBlockPhotoAIComponent", "CarrotAIComponent"};
    Blob Photo;
    CHECK(Fresh(Slot::AIStatic, PhotoContext, RowVersion, Photo));
    Path(Photo, "OjamaBlockPhotoAIParam.CheckDistance")->Bits = 500;
    CHECK(Encode(Slot::AIStatic, PhotoContext, Photo, RowVersion, Bytes, Error) && Bytes.size() == 20);
    CHECK(Decode(Slot::AIStatic, PhotoContext, Bytes, RowVersion, Back, Error) && Back.Exact() &&
        Lines(Photo) == Lines(Back));
    Context DecoContext;
    DecoContext.ComponentChain = {"DecoSpawnerAIComponent", "CarrotAIComponent"};
    Blob Deco;
    CHECK(Fresh(Slot::AIStatic, DecoContext, RowVersion, Deco));
    Path(Deco, "DecoSpawnerAIParam.SpawnNum")->Bits = 3;
    *Path(Deco, "DecoSpawnerAIParam.DecoIdlingPresetId") = NameFromUtf8("DecoratedPikmin");
    CHECK(
        !Path(Deco, "DecoSpawnerAIParam.PikminDecoLeaves") && !Path(Deco, "DecoSpawnerAIParam.PikminHorizontalWidth"));
    CHECK(Encode(Slot::AIStatic, DecoContext, Deco, RowVersion, Bytes, Error));
    CHECK(Decode(Slot::AIStatic, DecoContext, Bytes, RowVersion, Back, Error) && Back.Exact() &&
        Lines(Deco) == Lines(Back));
    for (const std::string Class :
        {"BuildObjectAIComponent", "TateanaAIComponent", "ExcavationAIComponent", "ShijimiAIComponent"})
    {
        Context C;
        C.ComponentChain = {
            Class, Class == "ShijimiAIComponent" ? "TekiAIComponent" : "ObjectAIComponent", "CarrotAIComponent"};
        Blob Options;
        CHECK(Fresh(Slot::AIStatic, C, RowVersion, Options));
        CHECK(Encode(Slot::AIStatic, C, Options, RowVersion, Bytes, Error));
        CHECK(
            Decode(Slot::AIStatic, C, Bytes, RowVersion, Back, Error) && Back.Exact() && Lines(Options) == Lines(Back));
    }
    Context Dig;
    Dig.ComponentChain = {"ExcavationAIComponent", "ObjectAIComponent", "CarrotAIComponent"};
    for (const std::uint64_t V :
        {UINT64_C(0x202110049), UINT64_C(0x20211004a), UINT64_C(0x202210319), UINT64_C(0x20221031a)})
    {
        Blob Options;
        CHECK(Fresh(Slot::AIStatic, Dig, V, Options));
        CHECK(bool(Path(Options, "ExcavationParam.bWorkEndKill")) == (V < UINT64_C(0x20211004a)));
        CHECK(bool(Path(Options, "ExcavationParam.WorkedHeight")) == (V > UINT64_C(0x202210319)));
        Path(Options, "ExcavationParam.BuryEndItemRate")->SetFloat(0.5f);
        CHECK(Encode(Slot::AIStatic, Dig, Options, V, Bytes, Error));
        CHECK(Decode(Slot::AIStatic, Dig, Bytes, V, Back, Error) && Back.Exact() && Lines(Options) == Lines(Back));
    }
    Context Water;
    Water.ComponentChain = {"WaterBoxAIComponent", "ObjectAIComponent", "CarrotAIComponent"};
    for (const std::uint64_t V :
        {UINT64_C(0x202205069), UINT64_C(0x20220506a), UINT64_C(0x202206149), UINT64_C(0x20220614a), RowVersion})
    {
        Blob Options;
        CHECK(Fresh(Slot::AIStatic, Water, V, Options));
        CHECK(bool(Path(Options, "legacy_water_distance_word")) == (V < UINT64_C(0x20220614a)));
        CHECK(bool(Path(Options, "legacy_water_down_word")) == (V < UINT64_C(0x20220506a)));
        *Path(Options, "WaterBoxAIParameter.WaterLevel.WaterBoxSwitchID") = NameFromUtf8("WaterSwitch");
        Path(Options, "WaterBoxAIParameter.WaterLevel.WaterLevelChangeDist")->SetFloat(250.f);
        CHECK(Encode(Slot::AIStatic, Water, Options, V, Bytes, Error));
        CHECK(Decode(Slot::AIStatic, Water, Bytes, V, Back, Error) && Back.Exact() && Lines(Options) == Lines(Back));
    }
    Context Mar;
    Mar.ComponentChain = {"MarAIComponent", "TekiAIComponent", "CarrotAIComponent"};
    for (const std::uint64_t V : {UINT64_C(0x202110019), UINT64_C(0x20211001a), UINT64_C(0x202206289),
             UINT64_C(0x20220628a), UINT64_C(0x202208259), RowVersion})
    {
        Blob Options;
        CHECK(Fresh(Slot::AIStatic, Mar, V, Options));
        CHECK(bool(Path(Options, "MarAIParameter.bWayCheckToTarget")) == (V > UINT64_C(0x202110019)));
        CHECK(bool(Path(Options, "MarAIParameter.WayCheckStartHeightOffset")) == (V > UINT64_C(0x202206289)));
        CHECK(bool(Path(Options, "MarAIParameter.bClose")) == (V > UINT64_C(0x202208259)));
        CHECK(!Path(Options, "MarAIParameter.AppearFallSearchArea")->Find("Angle"));
        Path(Options, "PlacedActor.CarrotMoveComponent.CarrotMoveParameter.FlyHeight")->SetFloat(450.f);
        CHECK(Encode(Slot::AIStatic, Mar, Options, V, Bytes, Error));
        CHECK(Decode(Slot::AIStatic, Mar, Bytes, V, Back, Error) && Back.Exact() && Lines(Options) == Lines(Back));
    }
    Context Beetle;
    Beetle.ComponentChain = {"KoganeAIComponent", "KoganeBaseAIComponent", "TekiAIComponent", "CarrotAIComponent"};
    for (const std::uint64_t V : {UINT64_C(0x202110089), UINT64_C(0x20211012a), UINT64_C(0x202303179), RowVersion})
    {
        Blob Options;
        CHECK(Fresh(Slot::AIStatic, Beetle, V, Options));
        CHECK(bool(Path(Options, "KoganeBaseAIParameter.TurnAngleMin")) == (V > UINT64_C(0x202110089)));
        CHECK(bool(Path(Options, "TekiAIParameter.SearchAreaCaution")) == (V > UINT64_C(0x202110129)));
        const Type* Parameters = FieldType(Slot::AIStatic, Beetle, "KoganeBaseAIParameter.DropParameter.ParameterList");
        Value Parameter = EmptyOf(*Parameters->Item, V);
        CHECK(bool(Parameter.Find("bEnableFreezeBothDrop")) == (V > UINT64_C(0x202303179)));
        const Type* Items = nullptr;
        for (const Field& F : Parameters->Item->Members)
            if (F.Name == "DropItemParameter")
                Items = F.Of;
        CHECK(Items != nullptr);
        Parameter.Find("DropItemParameter")->Items.push_back(EmptyOf(*Items->Item, V));
        Parameter.Find("DropItemParameter")->Items.back().Find("SpawnMiniInfo")->Find("bSetTerritory")->Bits = 1;
        if (V > UINT64_C(0x20220803a))
        {
            SetDropGeneratorNumbers(*Parameter.Find("DropItemParameter"), Parameter.Find("DebugUniqueIdList"),
                {UINT64_C(0x6b123456abcdef01)});
            CHECK(Parameter.Find("DropItemParameter")->Items[0].Find("UniqueId")->Bits == UINT64_C(0x6b123456abcdef01));
            CHECK(Parameter.Find("DebugUniqueIdList")->Items.size() == 1 &&
                Parameter.Find("DebugUniqueIdList")->Items[0].Bits == UINT64_C(0x6b123456abcdef01));
        }
        Path(Options, "KoganeBaseAIParameter.DropParameter.ParameterList")->Items.push_back(Parameter);
        CHECK(Encode(Slot::AIStatic, Beetle, Options, V, Bytes, Error));
        const bool Read = Decode(Slot::AIStatic, Beetle, Bytes, V, Back, Error);
        if (!Read)
            std::cerr << "Kogane decode " << std::hex << V << std::dec << ": " << Error << '\n';
        else if (Lines(Options) != Lines(Back))
        {
            const auto A = Lines(Options), B = Lines(Back);
            for (std::size_t I = 0; I < std::max(A.size(), B.size()); ++I)
                if (I >= A.size() || I >= B.size() || A[I] != B[I])
                {
                    std::cerr << "Kogane field here " << (I < A.size() ? A[I] : "-") << " back "
                              << (I < B.size() ? B[I] : "-") << '\n';
                    break;
                }
        }
        CHECK(Read && Back.Exact() && Lines(Options) == Lines(Back));
    }
    // Placement numbers: Kadan's range, the same on every run, and different for every name.
    const std::uint32_t List = ListWord("example.cave", "/Game/Maps/Cave000_F00_P");
    CHECK(List == ListWord("example.cave", "/game/maps/cave000_f00_p") &&
        List != ListWord("example.other", "/Game/Maps/Cave000_F00_P"));
    CHECK((List >> 24) == 0x6B);
    std::vector<std::string> Names;
    for (int I = 0; I < 2000; ++I)
        Names.push_back("Actor_" + std::to_string(I));
    auto Ids = Numbers(List, Names);
    CHECK(Ids.size() == Names.size());
    auto Sorted = Ids;
    std::sort(Sorted.begin(), Sorted.end());
    CHECK(std::adjacent_find(Sorted.begin(), Sorted.end()) == Sorted.end());
    CHECK(Numbers(List, Names) == Ids);
    for (auto Id : Ids)
        CHECK(!GameRange(Id) && Id != 0 && Id != ~UINT64_C(0));
    auto Taken = Numbers(List, {"Actor_7"}, {Ids[7]});
    CHECK(Taken.size() == 1 && Taken[0] != Ids[7]);
}

static std::vector<std::uint8_t> FromHex(const std::string& Text)
{
    std::vector<std::uint8_t> Out;
    for (std::size_t I = 0; I + 1 < Text.size(); I += 2)
        Out.push_back(std::uint8_t(std::stoul(Text.substr(I, 2), nullptr, 16)));
    return Out;
}

static int Stock(const char* File, const char* DecodedFile)
{
    std::ifstream In(File);
    if (!In)
    {
        std::cerr << "Cannot read " << File << '\n';
        return 1;
    }
    // What this reader makes of every blob, for comparing two versions of the layouts.
    std::ofstream Decoded;
    if (DecodedFile)
        Decoded.open(DecodedFile, std::ios::binary);
    std::string Line, Head;
    std::vector<std::uint8_t> Bytes;
    std::map<std::string, int> Tally;
    int Blobs = 0;
    while (std::getline(In, Line))
    {
        if (!Line.empty() && Line.back() == '\r')
            Line.pop_back();
        if (Line.compare(0, 5, "blob ") == 0)
        {
            Head = Line;
            Bytes.clear();
            continue;
        }
        if (Line.compare(0, 4, "hex ") == 0)
        {
            Bytes = FromHex(Line.substr(4));
            continue;
        }
        if (Line != "end")
            continue;
        ++Blobs;
        const auto Parts = Split(Head, ' ');
        if (Parts.size() != 7)
        {
            std::cerr << "Bad head " << Head << '\n';
            return 1;
        }
        const std::string &Name = Parts[1], Where = Parts[6];
        const bool Static = Parts[2] == "1";
        const std::uint64_t Version = std::stoull(Parts[3], nullptr, 16);
        Context C;
        if (Parts[4] != "-")
            C.ActorChain = Split(Parts[4], ',');
        if (Parts[5] != "-")
            C.ComponentChain = Split(Parts[5], ',');
        Slot S;
        Blob B;
        std::string Error;
        const std::string Key = Name + (Static ? ".Static " : ".Dynamic ");
        // A blob of a slot with no layout for the actor is kept whole by a writer.
        if (!SlotOf(Name, Static, S) || Layouts(S, C).empty())
        {
            ++Tally[Key + "raw"];
            if (DecodedFile)
                Decoded << "blob " << Where << ' ' << Key << "raw\n";
            continue;
        }
        if (!Decode(S, C, Bytes, Version, B, Error))
        {
            std::cerr << Where << ' ' << Key << "not decoded: " << Error << '\n';
            ++Failures;
            continue;
        }
        const std::string Mine = B.Exact() ? "exact" : "partial";
        ++Tally[Key + Mine];
        if (DecodedFile)
        {
            Decoded << "blob " << Where << ' ' << Key << Mine << " variant=" << B.Variant << " tail=" << B.Tail.size()
                    << '\n';
            for (const auto& Got : Lines(B))
                Decoded << "f " << Got << '\n';
        }
        std::vector<std::uint8_t> Written;
        if (!Encode(S, C, B, Version, Written, Error) || Written != Bytes)
        {
            ++Failures;
            std::cerr << Where << ' ' << Key << "is not written back byte for byte " << Error << '\n';
        }
        ++Tally["round trips"];
    }
    for (const auto& T : Tally)
        std::cout << T.first << ' ' << T.second << '\n';
    std::cout << Blobs << " stock blobs, " << Failures << " failures\n";
    return Failures ? 1 : 0;
}

// Every field of a layout with all it says, its type spelled out, for comparing two versions of the layouts.
static void DescribeType(const Type& T, const std::string& Indent, std::ostream& Out);
static void DescribeField(const Field& F, const std::string& Indent, std::ostream& Out)
{
    Out << Indent << F.Name << " gate=" << (F.Gate ? F.Gate : '-') << std::hex << F.Version
        << " default=" << F.RuntimeDefault << std::dec;
    Out << " when=" << F.When << " stop=" << F.StopIfSet << " derived=" << F.Derived << " count_of=" << F.CountOf;
    Out << " tag=" << F.ComponentTag << " component=" << F.ComponentTransform << '\n';
    DescribeType(*F.Of, Indent + "  ", Out);
}
static void DescribeType(const Type& T, const std::string& Indent, std::ostream& Out)
{
    Out << Indent << "kind " << int(T.Is) << " count_from=" << T.CountFrom << '\n';
    for (const auto& Member : T.Members)
        DescribeField(Member, Indent + "  ", Out);
    if (T.Item)
        DescribeType(*T.Item, Indent + "  ", Out);
}

// Every slot's layouts for each listed chain ("actor" or "component", then the classes, most derived first).
static int DescribeLayouts(const char* ChainsFile, const char* OutFile)
{
    std::ifstream In(ChainsFile);
    std::ofstream Out(OutFile, std::ios::binary);
    if (!In || !Out)
    {
        std::cerr << "Cannot read " << ChainsFile << " or write " << OutFile << '\n';
        return 1;
    }
    std::string Line;
    int Chains = 0;
    while (std::getline(In, Line))
    {
        if (!Line.empty() && Line.back() == '\r')
            Line.pop_back();
        const auto Parts = Split(Line, ' ');
        if (Parts.size() != 2)
            continue;
        Context C;
        (Parts[0] == "actor" ? C.ActorChain : C.ComponentChain) = Split(Parts[1], ',');
        C.Components = {"DemoBoxColl", "DemoSphereColl"};
        ++Chains;
        for (const auto& One : Slots())
        {
            const auto Found = Layouts(One.S, C);
            Out << "chain " << Line << " slot " << One.Row << (One.Static ? ".Static" : ".Dynamic") << " layouts "
                << Found.size() << '\n';
            for (std::size_t V = 0; V < Found.size(); ++V)
            {
                Out << " variant " << V << '\n';
                for (const auto& F : Found[V].Fields)
                    DescribeField(F, "  ", Out);
            }
        }
    }
    std::cout << Chains << " chains described\n";
    return 0;
}

int main(int Count, char** Args)
{
    if (Count == 4 && std::string(Args[1]) == "--layouts")
        return DescribeLayouts(Args[2], Args[3]);
    Own();
    if (Failures)
    {
        std::cout << Failures << " failures\n";
        return 1;
    }
    std::cout << "own checks passed\n";
    return Count > 1 ? Stock(Args[1], Count > 2 ? Args[2] : nullptr) : 0;
}
