#pragma once
// Neutral author requests, not evidence of a successful cook or runtime registration.
// Native adapters must independently resolve every private source/asset requirement.
#include "table_edit_export.h"

namespace P4MapKit
{
namespace Definitions
{
template <class T>
struct Optional
{
    bool Present = false;
    T Value{};
};
enum class AssetOrigin
{
    Creator,
    Game,
    OtherMod
};
struct AssetRef
{
    std::string Path, Class, Owner, SourceDigest, CustodyKey;
    AssetOrigin Origin = AssetOrigin::Creator;
};
struct RowSource
{
    std::string Table, Row, RowStruct, SourceDigest, CustodyKey;
    std::string SourceDigestKind, ComparisonSchema, ComparisonDigest;
    std::map<std::string, TableEdit::Kind> FieldTypes;
};
struct Donor
{
    std::string Id, NativeRow, ClassPath;
    std::vector<RowSource> Rows;
};
using Values = std::map<std::string, TableEdit::Value>;
struct Carriers
{
    std::int32_t Least = 0, Most = 0;
};
struct Numbers
{
    Optional<double> Life;
    Optional<std::int32_t> Sparklium, Pikmin;
    Optional<Carriers> Carry, BattleCarry;
    Values Fields;
};
struct TreasureText
{
    std::string Name;
    Optional<std::string> CatalogName, ResultName, Description, OlimarNotes, LouieNotes;
};
struct CreatureText
{
    std::string Name;
    Optional<std::string> ScientificName, Family, Description, OlimarNotes, LouieNotes;
};
struct Series
{
    std::string Id, Name;
};
struct TreasureCatalog
{
    std::string Series;
    Values Fields;
    RowSource Source;
};
struct CreatureCatalog
{
    Values Fields;
    RowSource Source;
};
struct Treasure
{
    std::string Id;
    AssetRef Class;
    Donor From;
    TreasureText Text;
    Optional<std::int32_t> Sparklium;
    Optional<Carriers> Carry, BattleCarry;
    Optional<AssetRef> Icon;
    Values Fields;
    Optional<TreasureCatalog> Catalog;
};
struct Creature
{
    std::string Id;
    AssetRef Class;
    Donor From;
    CreatureText Text;
    Optional<AssetRef> NightClass, Icon;
    Numbers Day;
    Optional<Numbers> Night;
    Optional<CreatureCatalog> Catalog;
};
struct MapBuild
{
    unsigned Area = 0;
    AssetRef World, Scene, RootScene, Counts;
};
struct Radar
{
    AssetRef Texture;
    Optional<std::array<double, 2>> Offset;
    Optional<double> Size, Angle, Scale;
    Optional<std::array<double, 4>> Limits;
};
struct Totals
{
    Optional<std::int32_t> Treasures, Castaways, Flarlic;
};
struct NewMap
{
    std::string Id, Name;
    std::vector<MapBuild> Builds;
    Optional<AssetRef> Preview;
    Optional<Radar> MapRadar;
    Optional<std::vector<std::string>> Pikmin;
    Optional<Totals> Counts;
};
struct Set
{
    std::vector<Series> SeriesList;
    std::vector<Treasure> Treasures;
    std::vector<Creature> Creatures;
    std::vector<NewMap> Maps;
};
struct SlotClaim
{
    unsigned Area = 0;
    std::string Id;
};
struct Context
{
    // Populated by the native target inventory, not by an author-supplied manifest.
    std::string TargetBuild, GameBuildId;
    std::vector<unsigned> FreeAreas;
    std::vector<SlotClaim> Occupied;
};
struct Refusal
{
    std::string Code, Definition, Field;
};
struct Result
{
    std::string ContentJson, NewMapsJson;
    std::vector<AssetRef> Assets;
    std::vector<Donor> Donors;
    std::vector<RowSource> Rows;
    std::vector<std::string> Reservations;
    std::vector<SlotClaim> Slots;
};
inline bool Fail(Refusal& E, const std::string& C, const std::string& F)
{
    E.Code = C;
    E.Field = F;
    return false;
}
inline std::string Fold(const std::string& S)
{
    return TableEdit::Fold(S);
}
inline bool Text(const std::string& S, std::size_t Max = 1024 * 1024)
{
    if (S.size() > Max || S.find('\0') != std::string::npos)
        return false;
    // Strict Unicode scalar validation, also used to count UTF-16 units for map names.
    for (std::size_t I = 0; I < S.size();)
    {
        unsigned C = static_cast<unsigned char>(S[I++]), N = 0, Min = 0;
        if (C < 0x80)
            continue;
        if (C >= 0xc2 && C <= 0xdf)
        {
            C &= 31;
            N = 1;
            Min = 0x80;
        }
        else if (C >= 0xe0 && C <= 0xef)
        {
            C &= 15;
            N = 2;
            Min = 0x800;
        }
        else if (C >= 0xf0 && C <= 0xf4)
        {
            C &= 7;
            N = 3;
            Min = 0x10000;
        }
        else
            return false;
        if (N > S.size() - I)
            return false;
        while (N--)
        {
            const unsigned B = static_cast<unsigned char>(S[I++]);
            if ((B & 0xc0) != 0x80)
                return false;
            C = (C << 6) | (B & 63);
        }
        if (C < Min || C > 0x10ffff || (C >= 0xd800 && C <= 0xdfff))
            return false;
    }
    return true;
}
inline bool LocalId(const std::string& S)
{
    if (S.empty() || S.size() > 47 || !Export::Alnum(S.front()) || !Export::Alnum(S.back()))
        return false;
    for (char C : S)
        if (!Export::Alnum(C) && C != '.' && C != '_' && C != '-')
            return false;
    return true;
}
inline bool LogicalId(const std::string& S)
{
    if (S.empty() || S.size() > 95)
        return false;
    const auto C = S.find(':');
    if (C != std::string::npos)
        return LocalId(S.substr(0, C)) && LocalId(S.substr(C + 1));
    for (char V : S)
        if (!Export::Alnum(V) && !(V >= 'A' && V <= 'Z') && V != '_' && V != '.' && V != '-')
            return false;
    return true;
}
inline bool Path(const std::string& S, bool Object = false)
{
    const auto D = S.find('.');
    const auto P = S.substr(0, D);
    return Export::PackageName(P) && (D == std::string::npos ? !Object : Export::Plain(S.substr(D + 1)));
}
inline std::string Package(const std::string& S)
{
    return S.substr(0, S.find('.'));
}
inline std::string Leaf(const std::string& S)
{
    return Package(S).substr(Package(S).rfind('/') + 1);
}
inline bool Asset(const AssetRef& A, const Export::Descriptor& Mod, const std::string& Expected, Result& R, Refusal& E,
                  const std::string& Field, bool Class = false)
{
    if (!Path(A.Path, Class) || A.Class != Expected)
        return Fail(E, "definition-asset-type-or-path", Field);
    if (Class && (A.Path.size() < 3 || A.Path.substr(A.Path.size() - 2) != "_C"))
        return Fail(E, "definition-blueprint-class", Field);
    if (!Export::Digest(A.SourceDigest) || A.CustodyKey.empty() || !Text(A.CustodyKey, 1023))
        return Fail(E, "definition-source-custody", Field);
    Export::Names N;
    std::string Error;
    if (!Export::DerivedNames(Mod, N, Error))
        return Fail(E, "definition-mod", Field);
    if (A.Origin == AssetOrigin::Creator)
    {
        if (A.Owner != Mod.Id || Fold(Package(A.Path)).compare(0, N.Root.size() + 1, Fold(N.Root + '/')) != 0)
            return Fail(E, "definition-creator-root", Field);
    }
    else if (A.Origin == AssetOrigin::Game)
    {
        if (!A.Owner.empty() || Fold(Package(A.Path)).compare(0, 11, "/game/mods/") == 0)
            return Fail(E, "definition-game-origin", Field);
    }
    else
    {
        if (!Export::ValidId(A.Owner) || A.Owner == Mod.Id)
            return Fail(E, "definition-other-mod-origin", Field);
        Export::Descriptor Other{A.Owner, "Reference", "0.0.0"};
        Export::Names ON;
        if (!Export::DerivedNames(Other, ON, Error) ||
            Fold(Package(A.Path)).compare(0, ON.Root.size() + 1, Fold(ON.Root + '/')) != 0)
            return Fail(E, "definition-other-mod-root", Field);
    }
    // Same reference must never carry contradictory private provenance.
    for (const auto& B : R.Assets)
        if (Fold(A.Path) == Fold(B.Path))
        {
            if (A.Class != B.Class || A.Owner != B.Owner || A.Origin != B.Origin || A.SourceDigest != B.SourceDigest ||
                A.CustodyKey != B.CustodyKey)
                return Fail(E, "definition-asset-conflict", Field);
            return true;
        }
    R.Assets.push_back(A);
    return true;
}
inline bool Row(const RowSource& S, Result& R, Refusal& E, const std::string& F)
{
    if (!Path(S.Table, true) || !TableEdit::RowName(S.Row) || !TableEdit::RowStruct(S.RowStruct) ||
        !Export::Digest(S.SourceDigest) || S.CustodyKey.empty() || !Text(S.CustodyKey, 1023) ||
        S.SourceDigestKind != "header-body-sha256-v1" || S.ComparisonSchema != "object-lines-v1" ||
        !Export::Digest(S.ComparisonDigest))
        return Fail(E, "definition-row-source", F);
    R.Rows.push_back(S);
    return true;
}
inline bool DonorSource(const Donor& D, const std::string& Parameter, Result& R, Refusal& E)
{
    if (!LogicalId(D.Id) || !TableEdit::RowName(D.NativeRow) || !Path(D.ClassPath, true) ||
        D.ClassPath.substr(D.ClassPath.size() - 2) != "_C" || D.Rows.empty())
        return Fail(E, "definition-donor", "from");
    if (D.Id.find(':') == std::string::npos && Fold(D.Id) != Fold(D.NativeRow))
        return Fail(E, "definition-donor-row", "from");
    bool Bound = false, ClassRow = false, IconRow = false;
    std::set<std::string> Seen;
    for (const auto& S : D.Rows)
    {
        if (!Seen.insert(Fold(S.Table) + ':' + Fold(S.Row)).second || !Row(S, R, E, "from"))
            return Fail(E, "definition-donor-row", "from");
        if (Fold(S.Table) == Fold(Parameter) && Fold(S.Row) == Fold(D.NativeRow))
            Bound = true;
        if (Fold(S.Table) == Fold("/Game/Carrot4/Placeables/Zukan/DT_ZukanBP.DT_ZukanBP") &&
            Fold(S.Row) == Fold(D.NativeRow))
            ClassRow = true;
        if (Fold(S.Table) == Fold("/Game/Carrot4/Core/GActor/DT_ActorTexture.DT_ActorTexture") &&
            Fold(S.Row) == Fold(D.NativeRow))
            IconRow = true;
    }
    if (!Bound)
        return Fail(E, "definition-donor-parameter-row", "from");
    if (!ClassRow || !IconRow)
        return Fail(E, "definition-donor-auxiliary-row", "from");
    R.Donors.push_back(D);
    return true;
}
inline void Add(std::string& J, const std::string& K, const std::string& V)
{
    if (J.size() > 1)
        J += ',';
    J += Export::Quote(K) + ':' + V;
}
inline bool ValuesJson(const Values& V, const std::vector<RowSource>& Sources, std::string& J, Refusal& E,
                       const std::string& F)
{
    J = "{";
    if (V.size() > 4096)
        return Fail(E, "definition-values-budget", F);
    std::set<std::string> Seen;
    for (const auto& P : V)
    {
        const auto& X = P.second;
        if (P.first.empty() || P.first.size() > 1023 || !Text(P.first) || !Seen.insert(Fold(P.first)).second)
            return Fail(E, "definition-field-path", F + '.' + P.first);
        if (X.Type != TableEdit::Kind::Boolean && X.Type != TableEdit::Kind::Integer &&
            X.Type != TableEdit::Kind::Number && X.Type != TableEdit::Kind::Text)
            return Fail(E, "definition-field-unsupported", F + '.' + P.first);
        if ((X.Type == TableEdit::Kind::Number && !std::isfinite(X.Number)) ||
            (X.Type == TableEdit::Kind::Text && !Text(X.Text)))
            return Fail(E, "definition-field-value", F + '.' + P.first);
        bool Found = false;
        for (const auto& S : Sources)
        {
            const auto I = S.FieldTypes.find(P.first);
            if (I != S.FieldTypes.end() && I->second == X.Type)
                Found = true;
        }
        if (!Found)
            return Fail(E, "definition-field-schema", F + '.' + P.first);
        Add(J, P.first, TableEdit::Scalar(X));
        if (J.size() > 4 * 1024 * 1024)
            return Fail(E, "definition-output-budget", F);
    }
    J += '}';
    return true;
}
inline bool Carry(const Optional<Carriers>& C, std::string& J, const std::string& F, Refusal& E)
{
    if (!C.Present)
        return true;
    if (C.Value.Least < 1 || C.Value.Most < C.Value.Least)
        return Fail(E, "definition-carriers", F);
    Add(J, F, "[" + std::to_string(C.Value.Least) + ',' + std::to_string(C.Value.Most) + ']');
    return true;
}
inline bool Integer(const Optional<std::int32_t>& V, std::string& J, const std::string& F, Refusal& E,
                    int Max = INT32_MAX)
{
    if (!V.Present)
        return true;
    if (V.Value < 0 || V.Value > Max)
        return Fail(E, "definition-integer", F);
    Add(J, F, std::to_string(V.Value));
    return true;
}
inline bool OptionalText(const Optional<std::string>& V, std::string& J, const std::string& F, Refusal& E)
{
    if (!V.Present)
        return true;
    if (!Text(V.Value))
        return Fail(E, "definition-text", F);
    Add(J, F, Export::Quote(V.Value));
    return true;
}
inline bool NumberFields(const Numbers& N, const std::vector<RowSource>& Sources, std::string& J, Refusal& E)
{
    if (N.Life.Present)
    {
        if (!std::isfinite(N.Life.Value) || N.Life.Value <= 0 || N.Life.Value > INT32_MAX)
            return Fail(E, "definition-life", "life");
        TableEdit::Value V;
        V.Type = TableEdit::Kind::Number;
        V.Number = N.Life.Value;
        Add(J, "life", TableEdit::Scalar(V));
    }
    if (!Integer(N.Sparklium, J, "sparklium", E) || !Integer(N.Pikmin, J, "pikmin", E) ||
        !Carry(N.Carry, J, "carriers", E) || !Carry(N.BattleCarry, J, "battle_carriers", E))
        return false;
    if (!N.Fields.empty())
    {
        std::string V;
        if (!ValuesJson(N.Fields, Sources, V, E, "values"))
            return false;
        Add(J, "values", V);
    }
    return true;
}
inline bool Identity(const std::string& Id, const std::string& Group, const Export::Descriptor& Mod, Result& R,
                     Refusal& E, std::set<std::string>& Seen)
{
    E.Definition = Group + ':' + Mod.Id + ':' + Id;
    E.Field.clear();
    if (!LocalId(Id) || Mod.Id.size() + Id.size() + 1 > 95 || !Seen.insert(Fold(Id)).second)
        return Fail(E, "definition-id", "id");
    R.Reservations.push_back(E.Definition);
    return true;
}
inline bool Build(const Export::Descriptor& Mod, const Set& S, const Context& C, Result& Out, Refusal& E)
{
    E = {};
    Result R;
    std::string Error;
    if (!Export::Validate(Mod, Error))
        return Fail(E, "definition-mod", "");
    // This first release targets only the proved 1.1.1 area inventory.
    if (C.TargetBuild != "1.1.1" || C.GameBuildId != "060F827BD2B38703E9EA11E3210D89BC" ||
        C.FreeAreas != std::vector<unsigned>{5, 7, 8, 9})
        return Fail(E, "definition-target-build", "");
    if (S.Treasures.size() + S.Creatures.size() + S.SeriesList.size() + S.Maps.size() > 256)
        return Fail(E, "definition-count-budget", "");
    std::string SeriesJ = "{", TreasureJ = "{", CreatureJ = "{", MapJ = "{";
    std::set<std::string> Seen, SeriesIds, Classes;
    for (const auto& V : S.SeriesList)
    {
        if (!Identity(V.Id, "treasure_series", Mod, R, E, Seen) || V.Name.empty() || !Text(V.Name))
            return Fail(E, "definition-series-name", "name");
        SeriesIds.insert(V.Id);
        Add(SeriesJ, V.Id, "{\"name\":" + Export::Quote(V.Name) + '}');
    }
    Seen.clear();
    for (const auto& V : S.Treasures)
    {
        if (!Identity(V.Id, "treasures", Mod, R, E, Seen) ||
            !DonorSource(V.From, "/Game/Carrot4/Core/GActor/DT_OtakaraParameter.DT_OtakaraParameter", R, E) ||
            !Asset(V.Class, Mod, "BlueprintGeneratedClass", R, E, "class", true))
            return false;
        if (!Classes.insert(Fold(V.Class.Path)).second)
            return Fail(E, "definition-class-shared", "class");
        if (V.Class.Origin != AssetOrigin::Creator)
            return Fail(E, "definition-authored-class", "class");
        if (V.Text.Name.empty() || !Text(V.Text.Name))
            return Fail(E, "definition-text", "text.name");
        std::string T = "{";
        Add(T, "name", Export::Quote(V.Text.Name));
        if (!OptionalText(V.Text.CatalogName, T, "catalog_name", E) ||
            !OptionalText(V.Text.ResultName, T, "result_name", E) ||
            !OptionalText(V.Text.Description, T, "description", E) ||
            !OptionalText(V.Text.OlimarNotes, T, "olimar_notes", E) ||
            !OptionalText(V.Text.LouieNotes, T, "louie_notes", E))
            return false;
        std::string J = "{";
        Add(J, "class", Export::Quote(V.Class.Path));
        Add(J, "from", Export::Quote(V.From.Id));
        Add(J, "text", T + '}');
        if (!Integer(V.Sparklium, J, "sparklium", E) || !Carry(V.Carry, J, "carriers", E) ||
            !Carry(V.BattleCarry, J, "battle_carriers", E))
            return false;
        if (V.Icon.Present)
        {
            if (!Asset(V.Icon.Value, Mod, "Texture2D", R, E, "icon"))
                return false;
            Add(J, "icon", Export::Quote(V.Icon.Value.Path));
        }
        if (!V.Fields.empty())
        {
            std::string F;
            std::vector<RowSource> Parameters;
            for (const auto& P : V.From.Rows)
                if (Fold(P.Table) == Fold("/Game/Carrot4/Core/GActor/DT_OtakaraParameter.DT_OtakaraParameter"))
                    Parameters.push_back(P);
            if (!ValuesJson(V.Fields, Parameters, F, E, "values"))
                return false;
            Add(J, "values", F);
        }
        if (V.Catalog.Present)
        {
            const auto& K = V.Catalog.Value;
            if (!SeriesIds.count(K.Series))
                return Fail(E, "definition-catalog-series", "catalog.series");
            if (Fold(K.Source.Table) !=
                    Fold("/Game/Carrot4/Placeables/Zukan/DT_ZukanOtakaraParameter.DT_ZukanOtakaraParameter") ||
                Fold(K.Source.Row) != Fold(V.From.NativeRow) || !Row(K.Source, R, E, "catalog"))
                return Fail(E, "definition-catalog-source", "catalog");
            std::string F, KJ = "{";
            Add(KJ, "series", Export::Quote(K.Series));
            if (!K.Fields.empty())
            {
                if (!ValuesJson(K.Fields, {K.Source}, F, E, "catalog.values"))
                    return false;
                Add(KJ, "values", F);
            }
            Add(J, "catalog", KJ + '}');
        }
        Add(TreasureJ, V.Id, J + '}');
        if (TreasureJ.size() > 4 * 1024 * 1024)
            return Fail(E, "definition-output-budget", "");
    }
    Seen.clear();
    for (const auto& V : S.Creatures)
    {
        if (!Identity(V.Id, "creatures", Mod, R, E, Seen) ||
            !DonorSource(V.From, "/Game/Carrot4/Core/GActor/DT_TekiParameter.DT_TekiParameter", R, E) ||
            !Asset(V.Class, Mod, "BlueprintGeneratedClass", R, E, "class", true))
            return false;
        if (!Classes.insert(Fold(V.Class.Path)).second)
            return Fail(E, "definition-class-shared", "class");
        if (V.Class.Origin != AssetOrigin::Creator)
            return Fail(E, "definition-authored-class", "class");
        if (V.Text.Name.empty() || !Text(V.Text.Name))
            return Fail(E, "definition-text", "text.name");
        std::string T = "{";
        Add(T, "name", Export::Quote(V.Text.Name));
        if (!OptionalText(V.Text.ScientificName, T, "scientific_name", E) ||
            !OptionalText(V.Text.Family, T, "family", E) || !OptionalText(V.Text.Description, T, "description", E) ||
            !OptionalText(V.Text.OlimarNotes, T, "olimar_notes", E) ||
            !OptionalText(V.Text.LouieNotes, T, "louie_notes", E))
            return false;
        std::string J = "{";
        Add(J, "class", Export::Quote(V.Class.Path));
        Add(J, "from", Export::Quote(V.From.Id));
        Add(J, "text", T + '}');
        std::vector<RowSource> Parameters;
        for (const auto& P : V.From.Rows)
            if (Fold(P.Table) == Fold("/Game/Carrot4/Core/GActor/DT_TekiParameter.DT_TekiParameter"))
                Parameters.push_back(P);
        if (!NumberFields(V.Day, Parameters, J, E))
            return false;
        if (V.NightClass.Present)
        {
            if (!Classes.insert(Fold(V.NightClass.Value.Path)).second ||
                !Asset(V.NightClass.Value, Mod, "BlueprintGeneratedClass", R, E, "night_class", true) ||
                V.NightClass.Value.Origin != AssetOrigin::Creator)
                return Fail(E, "definition-night-class", "night_class");
            Add(J, "night_class", Export::Quote(V.NightClass.Value.Path));
        }
        if (V.Icon.Present)
        {
            if (!Asset(V.Icon.Value, Mod, "Texture2D", R, E, "icon"))
                return false;
            Add(J, "icon", Export::Quote(V.Icon.Value.Path));
        }
        if (V.Night.Present)
        {
            std::string N = "{";
            if (!NumberFields(V.Night.Value, Parameters, N, E))
                return false;
            Add(J, "night", N + '}');
        }
        if (V.Catalog.Present)
        {
            std::string K = "{";
            const auto& CS = V.Catalog.Value.Source;
            if (Fold(CS.Table) != Fold("/Game/Carrot4/Placeables/Zukan/DT_ZukanTekiParameter.DT_ZukanTekiParameter") ||
                Fold(CS.Row) != Fold(V.From.NativeRow) || !Row(CS, R, E, "catalog"))
                return Fail(E, "definition-catalog-source", "catalog");
            if (!V.Catalog.Value.Fields.empty())
            {
                std::string F;
                if (!ValuesJson(V.Catalog.Value.Fields, {CS}, F, E, "catalog.values"))
                    return false;
                Add(K, "values", F);
            }
            Add(J, "catalog", K + '}');
        }
        Add(CreatureJ, V.Id, J + '}');
        if (CreatureJ.size() > 4 * 1024 * 1024)
            return Fail(E, "definition-output-budget", "");
    }
    Seen.clear();
    std::map<unsigned, std::string> Claimed;
    for (const auto& V : C.Occupied)
    {
        if (std::find(C.FreeAreas.begin(), C.FreeAreas.end(), V.Area) == C.FreeAreas.end() || !LogicalId(V.Id) ||
            V.Id.find(':') == std::string::npos || !Claimed.emplace(V.Area, V.Id).second)
            return Fail(E, "definition-slot-inventory", "");
    }
    for (const auto& V : S.Maps)
    {
        if (!Identity(V.Id, "new_maps", Mod, R, E, Seen))
            return false;
        if (V.Name.empty() || !Text(V.Name))
            return Fail(E, "definition-map-name", "name");
        unsigned Units = 0;
        for (unsigned char B : V.Name)
            if ((B & 0xc0) != 0x80)
                Units += B >= 0xf0 ? 2 : 1;
        if (Units > 160 || V.Builds.empty() || V.Builds.size() > C.FreeAreas.size())
            return Fail(E, "definition-map-builds", "builds");
        std::string J = "{";
        Add(J, "name", Export::Quote(V.Name));
        std::string B = "[";
        std::set<unsigned> Areas;
        for (const auto& X : V.Builds)
        {
            const std::string Id = Mod.Id + ':' + V.Id;
            if (std::find(C.FreeAreas.begin(), C.FreeAreas.end(), X.Area) == C.FreeAreas.end() ||
                !Areas.insert(X.Area).second)
                return Fail(E, "definition-map-slot", "builds");
            const auto Old = Claimed.find(X.Area);
            if (Old != Claimed.end() && Old->second != Id)
                return Fail(E, "definition-map-slot-occupied", "builds");
            Claimed[X.Area] = Id;
            std::ostringstream N;
            N << std::setw(3) << std::setfill('0') << X.Area;
            const auto Num = N.str();
            if (Leaf(X.World.Path) != "Area" + Num + "_P" || X.World.Path != Package(X.World.Path) ||
                Leaf(X.Scene.Path) != "SD_Area" + Num)
                return Fail(E, "definition-map-build-name", "builds");
            const auto Dot = X.Scene.Path.find('.');
            if (Dot != std::string::npos && X.Scene.Path.substr(Dot + 1) != "SD_Area" + Num)
                return Fail(E, "definition-map-scene-name", "builds.scene");
            if (!Asset(X.World, Mod, "World", R, E, "builds.world") ||
                !Asset(X.Scene, Mod, "/Script/Carrot4.GameSceneData", R, E, "builds.scene") ||
                !Asset(X.RootScene, Mod, "/Script/Carrot4.GameSceneData", R, E, "builds.root_scene") ||
                !Asset(X.Counts, Mod, "/Script/Carrot4.ActorPlacedNumAsset", R, E, "builds.counts"))
                return false;
            if (X.World.Origin != AssetOrigin::Creator || X.Scene.Origin != AssetOrigin::Creator ||
                X.RootScene.Origin != AssetOrigin::Creator || X.Counts.Origin != AssetOrigin::Creator)
                return Fail(E, "definition-map-authored-assets", "builds");
            if (B.size() > 1)
                B += ',';
            B += "{\"world\":" + Export::Quote(X.World.Path) + ",\"scene\":" + Export::Quote(X.Scene.Path) + '}';
            R.Slots.push_back({X.Area, Id});
        }
        Add(J, "builds", B + ']');
        if (V.Preview.Present)
        {
            if (!Asset(V.Preview.Value, Mod, "Texture2D", R, E, "preview"))
                return false;
            Add(J, "preview", Export::Quote(V.Preview.Value.Path));
        }
        if (V.MapRadar.Present)
        {
            const auto& X = V.MapRadar.Value;
            if (!Asset(X.Texture, Mod, "Texture2D", R, E, "radar.texture"))
                return false;
            std::string RJ = "{";
            Add(RJ, "texture", Export::Quote(X.Texture.Path));
            auto Num = [&](const Optional<double>& O, const char* F, double Min, double Max) -> bool
            {
                if (!O.Present)
                    return true;
                if (!std::isfinite(O.Value) || O.Value < Min || O.Value > Max)
                    return Fail(E, "definition-radar-number", F);
                TableEdit::Value T;
                T.Type = TableEdit::Kind::Number;
                T.Number = O.Value;
                Add(RJ, F, TableEdit::Scalar(T));
                return true;
            };
            if (!Num(X.Size, "size", 0, 1e7) || !Num(X.Angle, "angle", -360, 360) || !Num(X.Scale, "scale", 0, 100) ||
                (X.Size.Present && X.Size.Value <= 0) || (X.Scale.Present && X.Scale.Value <= 0))
                return Fail(E, "definition-radar-number", "radar");
            auto Arr = [&](const auto& A, const char* F) -> bool
            {
                std::string Z = "[";
                for (double N : A)
                {
                    if (!std::isfinite(N))
                        return Fail(E, "definition-radar-number", F);
                    TableEdit::Value T;
                    T.Type = TableEdit::Kind::Number;
                    T.Number = N;
                    if (Z.size() > 1)
                        Z += ',';
                    Z += TableEdit::Scalar(T);
                }
                Add(RJ, F, Z + ']');
                return true;
            };
            if (X.Offset.Present && !Arr(X.Offset.Value, "offset"))
                return false;
            if (X.Limits.Present)
            {
                const auto& L = X.Limits.Value;
                if (!(L[0] < L[2] && L[3] < L[1]) || !Arr(L, "limits"))
                    return Fail(E, "definition-radar-limits", "radar.limits");
            }
            Add(J, "radar", RJ + '}');
        }
        if (V.Pikmin.Present)
        {
            std::set<std::string> P;
            for (const auto& Id : V.Pikmin.Value)
                if (!LogicalId(Id) || !P.insert(Fold(Id)).second)
                    return Fail(E, "definition-map-pikmin", "pikmin");
            Add(J, "pikmin", Export::StringList(V.Pikmin.Value));
        }
        if (V.Counts.Present)
        {
            std::string TJ = "{";
            if (!Integer(V.Counts.Value.Treasures, TJ, "treasures", E, 10000) ||
                !Integer(V.Counts.Value.Castaways, TJ, "castaways", E, 10000) ||
                !Integer(V.Counts.Value.Flarlic, TJ, "flarlic", E, 10000))
                return false;
            Add(J, "totals", TJ + '}');
        }
        Add(MapJ, V.Id, J + '}');
        if (MapJ.size() > 4 * 1024 * 1024)
            return Fail(E, "definition-output-budget", "");
    }
    R.ContentJson = "{";
    if (!S.SeriesList.empty())
        Add(R.ContentJson, "treasure_series", SeriesJ + '}');
    if (!S.Treasures.empty())
        Add(R.ContentJson, "treasures", TreasureJ + '}');
    if (!S.Creatures.empty())
        Add(R.ContentJson, "creatures", CreatureJ + '}');
    R.ContentJson += '}';
    R.NewMapsJson = MapJ + '}';
    if (R.ContentJson.size() + R.NewMapsJson.size() > 4 * 1024 * 1024)
        return Fail(E, "definition-output-budget", "");
    Out = std::move(R);
    E = {};
    return true;
}
inline bool FrameworkFiles(const Export::Descriptor& Mod, const Export::MapEdit* Map,
                           const std::vector<TableEdit::Table>& Tables, const Set& S, const Context& C,
                           std::map<std::string, std::string>& Files, Result& Out, Refusal& E)
{
    Result R;
    if (!Build(Mod, S, C, R, E))
        return false;
    std::map<std::string, std::string> Prepared;
    TableEdit::Refusal T;
    const bool HasDefinitions =
        !S.SeriesList.empty() || !S.Treasures.empty() || !S.Creatures.empty() || !S.Maps.empty();
    TableEdit::Result Changes;
    if (!TableEdit::Build(Tables, Changes, T))
        return Fail(E, T.Code, T.Field);
    if (Map || Changes.ChangedFields)
    {
        if (!TableEdit::FrameworkFiles(Mod, Map, Tables, Prepared, T))
            return Fail(E, T.Code, T.Field);
    }
    else if (HasDefinitions)
        Prepared["mod.json"] = "{\"id\":" + Export::Quote(Mod.Id) + ",\"name\":" + Export::Quote(Mod.Name) +
                               ",\"version\":" + Export::Quote(Mod.Version) + "}\n";
    else
        return Fail(E, "definition-nothing-changed", "");
    auto& J = Prepared["mod.json"];
    J.resize(J.size() - 2);
    if (R.ContentJson != "{}")
        J += ",\"content\":" + R.ContentJson;
    if (R.NewMapsJson != "{}")
        J += ",\"new_maps\":" + R.NewMapsJson;
    J += "}\n";
    Files.swap(Prepared);
    Out = std::move(R);
    return true;
}
}
}
