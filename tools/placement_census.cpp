// Reads every blob of every row of the game's placement lists with Kadan's layouts and writes it back, and says
// which slots are complete for which owner classes (core/placement_layouts.json "complete").
//
//     clang++ -std=c++17 -O1 -I . -I core tools/placement_census.cpp -o placement_census
//     placement_census <placement folder> <classes.txt> [complete.json]
//
// classes.txt is what tools/placement_chains.py writes, perhaps with a fifth column: the tags of the class's
// components, as name=tag,tag entries separated by semicolons (the game finds some components by tag; reflection does
// not hold them, so they come from the Blueprints). A blob is exact when it decodes with nothing left over and
// encodes to the same bytes; partial when a tail is not decoded; raw when the slot has no layout for the row's
// classes; bad when the bytes do not fit. The classes with a blob that is not exact are printed, then the count of
// classes with every blob exact. A slot is complete for an owner class (the most derived native class of the
// blob's owner: the actor, its AI component, or the component the slot's owner rules choose) when every blob of
// the slot of owners of that class is exact; complete.json gets that list. The game's files stay private.
#include "core/property_decoder.h"
#include "core/placement_params.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
using namespace P4MapKit;
using namespace P4MapKit::Placement;
namespace fs = std::filesystem;

namespace
{

std::vector<std::uint8_t> Load(const fs::path& P)
{
    std::ifstream In(P, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(In)), std::istreambuf_iterator<char>());
}

std::uint64_t Num(const FDecodedValue* V)
{
    return V && V->Kind == FDecodedValue::EKind::Number && !V->Text.empty() && V->Text[0] != '-'
        ? std::strtoull(V->Text.c_str(), nullptr, 10)
        : 0;
}

std::vector<std::string> Split(const std::string& T, char By)
{
    std::vector<std::string> Out;
    std::stringstream S(T);
    std::string One;
    while (std::getline(S, One, By))
        if (!One.empty())
            Out.push_back(One);
    return Out;
}

struct Classes
{
    std::vector<std::string> Actor, AI;
    std::vector<std::pair<std::string, std::vector<std::string>>> Components;
    std::vector<std::string> Tags;
};

bool Has(const std::vector<std::string>& Chain, const std::string& Class)
{
    return std::find(Chain.begin(), Chain.end(), Class) != Chain.end();
}

// The native classes of the object a slot's blob is read from, as Kadan's writer chooses it.
std::vector<std::string> OwnerChain(const SlotName& Slot, const Classes& C)
{
    if (Slot.OwnerClasses.empty())
        return C.Actor;
    if (Slot.Chain == 'c')
        return C.AI;
    const auto Fits = [&](const std::vector<std::string>& Chain)
    {
        for (const std::string& Class : Slot.OwnerClasses)
            if (Has(Chain, Class))
                return true;
        return false;
    };
    // A native class's component (named with a *) is passed over here: in the editor its property holds nothing
    // until the game's code sets it.
    for (const auto& Component : C.Components)
        if (!Slot.OwnerProperty.empty() && Component.first == Slot.OwnerProperty && Fits(Component.second))
            return Component.second;
    for (const std::string& Class : Slot.OwnerClasses)
        for (const auto& Component : C.Components)
            if (Has(Component.second, Class) && (Slot.OwnerExcept.empty() || !Has(Component.second, Slot.OwnerExcept)))
                return Component.second;
    return {};
}

} // namespace

int main(int Argc, char** Argv)
{
    if (Argc < 3)
    {
        std::cerr << "placement_census <placement folder> <classes.txt> [complete.json]\n";
        return 2;
    }
    std::map<std::string, Classes> ByPath;
    {
        std::ifstream In(Argv[2]);
        std::string Line;
        while (std::getline(In, Line))
        {
            std::vector<std::string> Columns;
            std::size_t Start = 0;
            for (int I = 0; I < 5; ++I)
            {
                const std::size_t End = std::min(Line.find('\t', Start), Line.size());
                Columns.push_back(Start <= Line.size() ? Line.substr(Start, End - Start) : std::string());
                Start = End + 1;
            }
            Classes& C = ByPath[Columns[0]];
            C.Actor = Split(Columns[1], ',');
            C.AI = Split(Columns[2], ',');
            for (const std::string& One : Split(Columns[3], ';'))
            {
                const std::size_t Equal = One.find('=');
                if (Equal != std::string::npos)
                    C.Components.push_back({One.substr(0, Equal), Split(One.substr(Equal + 1), ':')});
            }
            for (const std::string& One : Split(Columns[4], ';'))
            {
                const std::size_t Equal = One.find('=');
                if (Equal != std::string::npos)
                    for (const std::string& Tag : Split(One.substr(Equal + 1), ','))
                        C.Tags.push_back(Tag);
            }
        }
    }
    // class -> slot -> state -> count, and (slot, owner class) -> every blob exact
    std::map<std::string, std::map<std::string, std::map<std::string, int>>> Tally;
    std::map<std::string, int> Rows;
    std::map<std::pair<std::string, std::string>, bool> Owners;
    std::set<std::string> NoClasses;
    for (const auto& Entry : fs::recursive_directory_iterator(Argv[1]))
    {
        const fs::path P = Entry.path();
        if (P.extension() != ".uasset" || P.filename().string().rfind("AP_", 0) != 0)
            continue;
        const auto Head = Load(P), Body = Load(fs::path(P).replace_extension(".uexp"));
        const FPackageInventory Package = ReadPackage(Head, Body);
        if (!Package.Error.Code.empty())
        {
            std::cerr << P.string() << ": " << Package.Error.Code << '\n';
            return 1;
        }
        for (const auto& Export : Package.Exports)
        {
            std::string Class;
            if (Export.Outer != 0 || !Package.Reference(Export.Class, Class) ||
                Class.find("ActorPlacementInfoAsset") == std::string::npos)
                continue;
            FPropertyDecoder Decoder(Package, Body);
            std::size_t Position = Export.Offset;
            FDecodedValue Values;
            if (!Decoder.Properties(Position, Export.Offset + Export.Size, Values))
                continue;
            const FDecodedValue* List = Values.Field("ActorGeneratorList");
            for (std::size_t I = 0; List && I < List->Items.size(); ++I)
            {
                const FDecodedValue& Row = List->Items[I];
                const FDecodedValue* ClassPath = Row.Field("SoftRefActorClass");
                const std::string Path = ClassPath ? ClassPath->Text : "";
                if (Path.empty())
                    continue;
                const std::string Short = Path.substr(Path.rfind('.') + 1);
                ++Rows[Short];
                const auto Found = ByPath.find(Path);
                if (Found == ByPath.end())
                {
                    NoClasses.insert(Short);
                    continue;
                }
                Context C;
                C.ActorChain = Found->second.Actor;
                C.ComponentChain = Found->second.AI;
                // A native class's component (named with a *) may hold nothing: only the Blueprints' own count for
                // the classes the actor's components have.
                for (const auto& Component : Found->second.Components)
                {
                    C.Components.push_back(Component.first);
                    if (Component.first.rfind('*', 0) != 0)
                        for (const std::string& Class : Component.second)
                            if (!Has(C.ComponentClasses, Class))
                                C.ComponentClasses.push_back(Class);
                }
                C.ComponentTags = Found->second.Tags;
                for (const SlotName& Name : Slots())
                    if (Name.Chain == 'o')
                        C.OwnerChains[Name.S] = OwnerChain(Name, Found->second);
                const FDecodedValue* Serialize = Row.Field("ActorSerializeParameter");
                for (std::size_t F = 0; Serialize && F < Serialize->Fields.size(); ++F)
                {
                    const auto& Pair = Serialize->Fields[F];
                    for (const bool Static : {true, false})
                    {
                        const FDecodedValue* Array = Pair.second.Field(Static ? "Static" : "Dynamic");
                        if (!Array || Array->Items.empty())
                            continue;
                        const std::string Key = Pair.first + (Static ? ".Static" : ".Dynamic");
                        if (Key.rfind("Hash.", 0) == 0 || Key.rfind("CheckComp", 0) == 0)
                            continue;
                        std::vector<std::uint8_t> Bytes;
                        for (const auto& B : Array->Items)
                            Bytes.push_back(std::uint8_t(Num(&B)));
                        Slot S;
                        std::string State;
                        if (!SlotOf(Pair.first, Static, S) || Layouts(S, C).empty())
                            State = "raw";
                        else
                        {
                            const std::uint64_t Version =
                                Num(Row.Field(Static ? "AssetVersion" : "GeneratorVersion"));
                            Blob Read;
                            std::string Error;
                            std::vector<std::uint8_t> Again;
                            if (!Decode(S, C, Bytes, Version, Read, Error))
                                State = "bad";
                            else if (!Read.Exact())
                                State = "partial";
                            else
                                State = Encode(S, C, Read, Version, Again, Error) && Again == Bytes
                                    ? "exact"
                                    : "rewritten-differs";
                        }
                        ++Tally[Short][Key][State];
                        for (const SlotName& Name : Slots())
                        {
                            if (Name.S != S)
                                continue;
                            const std::vector<std::string> Owner = OwnerChain(Name, Found->second);
                            const auto Pair2 = std::make_pair(Key, Owner.empty() ? std::string() : Owner.front());
                            const auto Was = Owners.find(Pair2);
                            Owners[Pair2] = (Was == Owners.end() || Was->second) && State == "exact";
                        }
                    }
                }
            }
        }
    }
    int Clean = 0;
    for (const auto& Class : Tally)
    {
        std::string Problems;
        for (const auto& Slot : Class.second)
            for (const auto& State : Slot.second)
                if (State.first != "exact")
                    Problems += " " + Slot.first + "=" + State.first + "x" + std::to_string(State.second);
        if (!Problems.empty())
            std::cout << Class.first << " rows=" << Rows[Class.first] << ":" << Problems << '\n';
        else
            ++Clean;
    }
    std::cout << "classes: " << Rows.size() << ", all blobs exact: " << Clean << ", no classes: " << NoClasses.size()
              << '\n';
    for (const auto& N : NoClasses)
        std::cout << "  no classes: " << N << '\n';
    if (Argc > 3)
    {
        // The complete list, in the order of the layout data's slots.
        std::ofstream Out(Argv[3]);
        Out << "[\n";
        bool First = true;
        for (const SlotName& Name : Slots())
        {
            const std::string Key = std::string(Name.Row) + (Name.Static ? ".Static" : ".Dynamic");
            std::vector<std::string> Listed;
            for (const auto& One : Owners)
                if (One.first.first == Key && !One.first.second.empty() && One.second)
                    Listed.push_back(One.first.second);
            if (Listed.empty())
                continue;
            Out << (First ? "" : ",\n") << "    {\"slot\": \"" << Key << "\", \"classes\": [";
            for (std::size_t I = 0; I < Listed.size(); ++I)
                Out << (I ? ", " : "") << '"' << Listed[I] << '"';
            Out << "]}";
            First = false;
        }
        Out << "\n  ]\n";
    }
    return 0;
}
