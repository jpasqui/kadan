#pragma once
#include "export_contract.h"
#include <set>

// Producer facts retain private source custody. Only the field deltas enter mod.json.
namespace P4MapKit { namespace TableEdit {
enum class Kind { Boolean, Integer, Number, Text, Structure, Array, Unsupported };
struct Value {
    Kind Type = Kind::Unsupported;
    bool Boolean = false; std::int64_t Integer = 0; double Number = 0;
    std::string Text;
    std::map<std::string, Value> Fields;
    std::vector<Value> Elements;
};
struct Provenance {
    std::string Producer = "native-datatable-fields-v1";
    std::string SourceDigest, RecordSourceDigest;
    std::string ComparisonDigest, RecordComparisonDigest;
    std::string SourceSchema = "header-body-sha256-v1", ComparisonSchema = "object-lines-v1";
};
using Row = std::map<std::string, Value>;
struct Table {
    std::string Package, ObjectPath, RowStruct;
    Provenance Origin;
    std::map<std::string, Row> Before, After;
};
struct Refusal { std::string Code, Table, Row, Field; };
struct Result {
    std::string TablesJson;
    std::vector<Table> Sources;
    std::size_t ChangedFields = 0;
};
inline std::string Fold(std::string S) { for (char& C : S) if (C >= 'A' && C <= 'Z') C += 'a' - 'A'; return S; }
inline bool Name(const std::string& S) {
    if (S.empty() || S.size() > 127) return false;
    for (unsigned char C : S) if (C < 32 || C == 127 || C == '.' || C == '[' || C == ']' || C == '/' || C == '\\') return false;
    return true;
}
inline bool RowName(const std::string& S) {
    if (S.empty() || S.size()>1023) return false;
    for (unsigned char C : S) if (C<32 || C==127) return false;
    return true;
}
inline bool RowStruct(const std::string& S) {
    if (S.compare(0,8,"/Script/") != 0) return false;
    const auto Dot=S.find('.',8);
    return Dot!=std::string::npos && Export::Plain(S.substr(8,Dot-8)) && Export::Plain(S.substr(Dot+1));
}
inline bool Same(const Value& A, const Value& B) {
    if (A.Type != B.Type) return false;
    switch (A.Type) {
    case Kind::Boolean: return A.Boolean == B.Boolean;
    case Kind::Integer: return A.Integer == B.Integer;
    case Kind::Number: return A.Number == B.Number && std::isfinite(A.Number);
    case Kind::Text: return A.Text == B.Text;
    case Kind::Unsupported: return A.Text == B.Text;
    case Kind::Structure:
        if (A.Fields.size() != B.Fields.size()) return false;
        for (const auto& F : A.Fields) { auto I = B.Fields.find(F.first); if (I == B.Fields.end() || !Same(F.second, I->second)) return false; } return true;
    case Kind::Array:
        if (A.Elements.size() != B.Elements.size()) return false;
        for (std::size_t I = 0; I < A.Elements.size(); ++I) if (!Same(A.Elements[I], B.Elements[I])) return false; return true;
    }
    return false;
}
inline bool Fail(Refusal& E, const std::string& Code, const std::string& Path) { E.Code = Code; E.Field = Path; return false; }
inline bool Diff(const Value& A, const Value& B, const std::string& Path,
                 std::map<std::string, Value>& Edits, Refusal& E, unsigned Depth = 0) {
    if (Depth > 32) return Fail(E, "table-property-depth", Path);
    if (A.Type != B.Type) return Fail(E, "table-property-type-changed", Path);
    if (B.Type == Kind::Number && !std::isfinite(B.Number)) return Fail(E, "table-number-not-finite", Path);
    if (B.Type == Kind::Text && B.Text.find('\0') != std::string::npos) return Fail(E, "table-text-embedded-nul", Path);
    if (B.Type == Kind::Text && B.Text.size()>1024*1024) return Fail(E,"table-text-budget",Path);
    if (B.Type == Kind::Array) {
        if (A.Elements.size() != B.Elements.size()) return Fail(E, "table-array-resize-unsupported", Path);
        if (B.Elements.size() > 65536) return Fail(E, "table-array-budget", Path);
        for (std::size_t I = 0; I < B.Elements.size(); ++I)
            if (!Diff(A.Elements[I], B.Elements[I], Path + '[' + std::to_string(I) + ']', Edits, E, Depth + 1)) return false;
        return true;
    }
    if (B.Type == Kind::Structure) {
        if (A.Fields.size() != B.Fields.size()) return Fail(E, "table-schema-changed", Path);
        std::set<std::string> Names;
        for (const auto& F : B.Fields) {
            const auto I = A.Fields.find(F.first);
            if (!Name(F.first) || !Names.insert(Fold(F.first)).second || I == A.Fields.end()) return Fail(E, "table-schema-changed", Path);
            if (!Diff(I->second, F.second, Path.empty() ? F.first : Path + '.' + F.first, Edits, E, Depth + 1)) return false;
        }
        return true;
    }
    if (Same(A, B)) return true;
    if (B.Type == Kind::Unsupported) return Fail(E, "table-property-unsupported", Path);
    if (Path.empty() || Path.size() > 1023 || !Edits.emplace(Path, B).second) return Fail(E, "table-field-path-invalid", Path);
    return true;
}
inline std::string Scalar(const Value& V) {
    if (V.Type == Kind::Text) return Export::Quote(V.Text);
    if (V.Type == Kind::Boolean) return V.Boolean ? "true" : "false";
    if (V.Type == Kind::Integer) return std::to_string(V.Integer);
    std::ostringstream Out; Out.imbue(std::locale::classic()); Out << std::setprecision(17) << V.Number;
    auto S = Out.str(); if (S.find_first_of(".eE") == std::string::npos) S += ".0"; return S;
}
inline bool Build(const std::vector<Table>& Tables, Result& Out, Refusal& E) {
    E = {}; Result Prepared; Prepared.TablesJson = "{"; std::set<std::string> Seen; bool FirstTable = true;
    if (Tables.size() > 64) return Fail(E, "table-count-budget", "");
    for (const auto& T : Tables) {
        E.Table = T.ObjectPath; E.Row.clear(); E.Field.clear();
        const auto Leaf = T.Package.substr(T.Package.rfind('/') + 1);
        const auto& P = T.Origin;
        if (!Export::PackageName(T.Package) || T.ObjectPath != T.Package + '.' + Leaf ||
            !RowStruct(T.RowStruct) || !Seen.insert(Fold(T.ObjectPath)).second)
            return Fail(E, "table-identity-invalid", "");
        if (P.Producer != "native-datatable-fields-v1" || P.SourceSchema != "header-body-sha256-v1" ||
            P.ComparisonSchema != "object-lines-v1" || !Export::Digest(P.SourceDigest) ||
            !Export::Digest(P.ComparisonDigest) || P.SourceDigest != P.RecordSourceDigest ||
            P.ComparisonDigest != P.RecordComparisonDigest) return Fail(E, "table-source-provenance-mismatch", "");
        if (T.Before.size() != T.After.size()) return Fail(E, "table-row-add-remove-unsupported", "");
        if (T.After.size() > 65536) return Fail(E, "table-row-budget", "");
        std::string Rows = "{"; bool FirstRow = true; std::set<std::string> RowNames;
        for (const auto& R : T.After) {
            E.Row = R.first; auto Old = T.Before.find(R.first);
            if (!RowName(R.first) || !RowNames.insert(Fold(R.first)).second || Old == T.Before.end()) return Fail(E, "table-row-add-remove-unsupported", "");
            Value A, B; A.Type = B.Type = Kind::Structure; A.Fields = Old->second; B.Fields = R.second;
            std::map<std::string, Value> Edits; if (!Diff(A, B, "", Edits, E)) return false;
            if (Edits.empty()) continue;
            if (!FirstRow) Rows += ','; FirstRow = false; Rows += Export::Quote(R.first) + ":{"; bool FirstField = true;
            for (const auto& F : Edits) { if (!FirstField) Rows += ','; FirstField = false; Rows += Export::Quote(F.first) + ':' + Scalar(F.second); }
            Prepared.ChangedFields += Edits.size(); Rows += '}';
        }
        Rows += '}'; if (FirstRow) continue;
        if (!FirstTable) Prepared.TablesJson += ','; FirstTable = false;
        Prepared.TablesJson += Export::Quote(T.ObjectPath) + ':' + Rows; Prepared.Sources.push_back(T);
        if (Prepared.TablesJson.size() > 4 * 1024 * 1024) return Fail(E, "table-output-budget", "");
    }
    Prepared.TablesJson += '}'; Out = std::move(Prepared); E = {}; return true;
}
// Only this typed producer can add a tables section; arbitrary JSON strings are never admitted.
inline bool FrameworkFiles(const Export::Descriptor& Mod, const Export::MapEdit* Map,
                           const std::vector<Table>& Tables, std::map<std::string,std::string>& Files,
                           Refusal& E) {
    Result Changes; if (!Build(Tables, Changes, E)) return false;
    std::map<std::string,std::string> Prepared; std::string Error;
    if (Map) { if (!Export::FrameworkFiles(Mod, *Map, Prepared, Error)) return Fail(E, Error, ""); }
    else {
        if (!Export::Validate(Mod, Error)) return Fail(E, Error, "");
        if (!Changes.ChangedFields) return Fail(E, "table-nothing-changed", "");
        Prepared["mod.json"] = "{\"id\":" + Export::Quote(Mod.Id) + ",\"name\":" + Export::Quote(Mod.Name) + ",\"version\":" + Export::Quote(Mod.Version) + "}\n";
    }
    if (Changes.ChangedFields) { auto& Manifest = Prepared["mod.json"]; Manifest.resize(Manifest.size() - 2); Manifest += ",\"tables\":" + Changes.TablesJson + "}\n"; }
    Files.swap(Prepared); return true;
}
}}
