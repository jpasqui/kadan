// Installed stock-engine C++ declaration index, not original-game extraction.
// The caller supplies its own headers/module/include identities; no filesystem IO.
#pragma once
#include "original_declarations_manifest.h"

namespace P4MapKit { namespace OriginalDeclarationsEngine {
using Blueprint::J;
using Blueprint::Error;
struct Header { std::string Module, Include, Text; };
inline bool Ident(const std::string& Text)
{
    const auto Alpha = [](char C) { return (C >= 'A' && C <= 'Z') || (C >= 'a' && C <= 'z') || C == '_'; };
    if (Text.empty() || !Alpha(Text.front())) return false;
    return std::all_of(Text.begin(), Text.end(), [&](char C) { return Alpha(C) || (C >= '0' && C <= '9'); });
}
inline std::vector<std::string> Tokens(const std::string& Text)
{
    if (Text.size() > 4u * 1024u * 1024u) throw Error("engine header byte budget");
    std::vector<std::string> Out;
    for (std::size_t At = 0; At < Text.size();)
    {
        const char C = Text[At];
        if (C == ' ' || C == '\r' || C == '\n' || C == '\t') { ++At; continue; }
        if (C == '#')
        {
            do { const auto End = Text.find('\n', At); if (End == std::string::npos) { At = Text.size(); break; }
                const bool Continued = End && (Text[End - 1] == '\\' || (End > 1 && Text[End - 1] == '\r' && Text[End - 2] == '\\')); At = End + 1; if (!Continued) break;
            } while (At < Text.size());
            continue;
        }
        if (C == '/' && At + 1 < Text.size() && Text[At + 1] == '/')
        { const auto End = Text.find('\n', At + 2); At = End == std::string::npos ? Text.size() : End + 1; continue; }
        if (C == '/' && At + 1 < Text.size() && Text[At + 1] == '*')
        { const auto End = Text.find("*/", At + 2); if (End == std::string::npos) throw Error("unterminated engine header comment"); At = End + 2; continue; }
        std::size_t RawPrefix = 0;
        for (const auto& Prefix : {std::string("R\""), std::string("u8R\""), std::string("uR\""), std::string("UR\""), std::string("LR\"")})
            if (Text.compare(At, Prefix.size(), Prefix) == 0) { RawPrefix = Prefix.size(); break; }
        if (RawPrefix)
        {
            const auto Begin = At + RawPrefix, Open = Text.find('(', Begin);
            if (Open == std::string::npos || Open - Begin > 16) throw Error("invalid engine header raw string");
            const auto End = Text.find(")" + Text.substr(Begin, Open - Begin) + "\"", Open + 1);
            if (End == std::string::npos) throw Error("unterminated engine header raw string");
            At = End + Open - Begin + 2;
            continue;
        }
        if (C == '\'' && At && At + 1 < Text.size() && Text[At - 1] != ' ' &&
            ((Text[At + 1] >= '0' && Text[At + 1] <= '9') || (Text[At + 1] >= 'a' && Text[At + 1] <= 'f') || (Text[At + 1] >= 'A' && Text[At + 1] <= 'F')) &&
            ((Text[At - 1] >= '0' && Text[At - 1] <= '9') || (Text[At - 1] >= 'a' && Text[At - 1] <= 'f') || (Text[At - 1] >= 'A' && Text[At - 1] <= 'F')))
        { ++At; continue; } // C++14 integer digit separator.
        if (C == '"' || C == '\'')
        {
            const char Quote = C; bool Closed = false; ++At;
            while (At < Text.size()) { const char Next = Text[At++]; if (Next == '\\') { if (At == Text.size()) break; ++At; }
                else if (Next == Quote) { Closed = true; break; } }
            if (!Closed) throw Error("unterminated engine header literal"); continue;
        }
        const auto Start = At;
        if ((C >= 'A' && C <= 'Z') || (C >= 'a' && C <= 'z') || C == '_')
        { do { ++At; } while (At < Text.size() && ((Text[At] >= 'A' && Text[At] <= 'Z') || (Text[At] >= 'a' && Text[At] <= 'z') || (Text[At] >= '0' && Text[At] <= '9') || Text[At] == '_')); }
        else ++At;
        Out.push_back(Text.substr(Start, At - Start));
        if (Out.size() > 500000) throw Error("engine header token budget");
    }
    return Out;
}
inline std::size_t MacroEnd(const std::vector<std::string>& Words, std::size_t Open)
{
    if (Open >= Words.size() || Words[Open] != "(") throw Error("reflected engine macro arguments required");
    unsigned Depth = 0;
    for (std::size_t At = Open; At < Words.size(); ++At)
    {
        if (Words[At] == "(" && ++Depth > 64) throw Error("engine macro nesting budget");
        if (Words[At] == ")" && --Depth == 0) return At;
    }
    throw Error("unterminated reflected engine macro");
}
inline bool Api(const std::string& Text) { return Text.size() > 4 && Text.compare(Text.size() - 4, 4, "_API") == 0; }
inline J Index(const std::vector<Header>& Headers)
{
    if (Headers.size() > 30000) throw Error("engine header count budget");
    J Out = J::Object({{"classes", J::Object()}, {"structs", J::Object()}, {"enums", J::Object()}, {"delegates", J::Object()}});
    std::map<std::string, std::string> Seen;
    std::size_t Total = 0;
    for (const auto& Source : Headers)
    {
        if (!Ident(Source.Module) || !OriginalDeclarations::RelativePath(Source.Include)) throw Error("engine header module or include identity invalid");
        if (Source.Text.size() > 128u * 1024u * 1024u - Total) throw Error("engine header input byte budget");
        Total += Source.Text.size(); const auto Words = Tokens(Source.Text);
        const auto Add = [&](const std::string& Kind, const std::string& Name, J Record)
        {
            if (!Ident(Name)) throw Error("invalid reflected engine name"); Record["include"] = Source.Include;
            const auto Key = Source.Module + '.' + Name, Normal = OriginalDeclarations::Fold(Key);
            const auto Meaning = Kind + ':' + Key + ':' + Record.Dump();
            const auto Found = Seen.find(Normal);
            if (Found != Seen.end() && Found->second != Meaning) throw Error("conflicting reflected engine identity: " + Key);
            Seen[Normal] = Meaning; Out[Kind][Key] = std::move(Record);
        };
        for (std::size_t At = 0; At < Words.size(); ++At)
        {
            const auto Macro = Words[At];
            const bool Class = Macro == "UCLASS" || Macro == "UINTERFACE", Struct = Macro == "USTRUCT", Enum = Macro == "UENUM";
            const bool Delegate = Macro.find("DECLARE_DYNAMIC") == 0 && Macro.find("_DELEGATE") != std::string::npos;
            if (!Class && !Struct && !Enum && !Delegate) continue;
            const auto End = MacroEnd(Words, At + 1);
            if (Delegate)
            {
                std::vector<std::vector<std::string>> Args(1); unsigned Paren = 0, Angle = 0;
                for (std::size_t N = At + 2; N < End; ++N)
                {
                    if (Words[N] == "(") ++Paren; else if (Words[N] == ")" && Paren) --Paren;
                    else if (Words[N] == "<") ++Angle; else if (Words[N] == ">" && Angle) --Angle;
                    if (Words[N] == "," && !Paren && !Angle) Args.emplace_back(); else Args.back().push_back(Words[N]);
                }
                const auto N = Macro.find("RetVal") != std::string::npos ? 1u : 0u;
                if (N >= Args.size() || Args[N].size() != 1 || Args[N][0].size() < 2 || Args[N][0][0] != 'F') throw Error("invalid engine delegate declaration");
                const auto Cpp = Args[N][0]; Add("delegates", Cpp.substr(1), J::Object({{"cpp", Cpp}})); At = End; continue;
            }
            std::size_t Next = End + 1;
            const auto Take = [&]() -> std::string { if (Next == Words.size()) throw Error("incomplete reflected engine declaration"); return Words[Next++]; };
            if (Class || Struct)
            {
                if (Take() != (Class ? "class" : "struct")) throw Error("reflected engine declaration kind mismatch");
                auto Cpp = Take(); const bool Exported = Api(Cpp); if (Exported) Cpp = Take();
                if (!Ident(Cpp) || Cpp.size() < 2) throw Error("reflected engine C++ name required");
                if (Cpp.find("UDEPRECATED_") == 0 || Cpp.find("ADEPRECATED_") == 0) { At = End; continue; }
                J Record = J::Object({{"cpp", Cpp}, {"exported", Exported}});
                if (Class) { Record["minimal"] = std::find(Words.begin() + At + 2, Words.begin() + End, "MinimalAPI") != Words.begin() + End; Record["interface"] = Macro == "UINTERFACE"; }
                Add(Class ? "classes" : "structs", Cpp.substr(1), Record);
            }
            else
            {
                const auto Kind = Take();
                if (Kind == "enum")
                {
                    auto Cpp = Take(); const bool Scoped = Cpp == "class"; if (Scoped) Cpp = Take();
                    Add("enums", Cpp, J::Object({{"cpp", Cpp}, {"form", Scoped ? "class" : "plain"}}));
                }
                else if (Kind == "namespace")
                {
                    const auto Space = Take(); if (Take() != "{" || Take() != "enum") throw Error("invalid reflected engine enum namespace");
                    const auto Inner = Take(); Add("enums", Space, J::Object({{"cpp", Space + "::" + Inner}, {"form", "namespace"}}));
                }
                else throw Error("invalid reflected engine enum declaration");
            }
            At = End;
        }
    }
    return Out;
}
}} // namespace P4MapKit::OriginalDeclarationsEngine
