#include "P4RecoveredLightCook.h"
#include "P4MapKitCore/export_package.h"

bool P4TranslateRecoveredLightCook(std::vector<uint8>& Header, std::vector<uint8>& Body, FString& Error)
{
    const auto Before = P4MapKit::ReadPackage(Header, Body);
    if (!Before.Error.Code.empty())
    {
        Error = UTF8_TO_TCHAR(Before.Error.Code.c_str());
        return false;
    }
    const std::string Storage = "P4RecoveredDirectionalLightComponent";
    const std::string Native = "DirectionalLightComponent";
    int32 EnginePackage = 0;
    bool Found = false;
    for (std::size_t Index = 0; Index < Before.Imports.size(); ++Index)
    {
        const auto& Import = Before.Imports[Index];
        if (!Import.Outer && Import.Name == "/Script/Engine" && Import.Class == "Package")
            EnginePackage = -int32(Index) - 1;
        if (Import.Name == Storage || Import.Name == "Default__" + Storage)
        {
            std::string Outer;
            if (Import.Outer >= 0 || !Before.Reference(Import.Outer, Outer) || Outer != "/Script/P4StubsCore")
            {
                Error = TEXT("light-cook-recovered-class-namespace-differs");
                return false;
            }
            Found = true;
        }
        if (Import.Class == Storage)
        {
            if (Import.ClassPackage != "/Script/P4StubsCore")
            {
                Error = TEXT("light-cook-recovered-class-package-differs");
                return false;
            }
            Found = true;
        }
    }
    if (!Found)
    {
        Error.Reset();
        return true;
    }
    if (!EnginePackage)
    {
        Error = TEXT("light-cook-native-engine-package-missing");
        return false;
    }
    std::map<std::string, std::string> Rename;
    std::map<uint32, uint32> NameSlots;
    for (const auto& Pair :
        std::map<std::string, std::string>{{Storage, Native}, {"Default__" + Storage, "Default__" + Native}})
    {
        const auto Old = std::find(Before.Names.begin(), Before.Names.end(), Pair.first);
        if (Old == Before.Names.end())
            continue;
        const auto New = std::find(Before.Names.begin(), Before.Names.end(), Pair.second);
        if (std::count(Before.Names.begin(), Before.Names.end(), Pair.first) != 1 ||
            (New != Before.Names.end() && std::count(Before.Names.begin(), Before.Names.end(), Pair.second) != 1))
        {
            Error = TEXT("light-cook-name-identity-collision");
            return false;
        }
        if (New == Before.Names.end())
            Rename.emplace(Pair);
        else
            NameSlots.emplace(uint32(Old - Before.Names.begin()), uint32(New - Before.Names.begin()));
    }
    const auto EngineName = std::find(Before.Names.begin(), Before.Names.end(), "/Script/Engine");
    if (EngineName == Before.Names.end())
    {
        Error = TEXT("light-cook-native-engine-name-missing");
        return false;
    }
    auto Candidate = Header;
    bool Table = false;
    for (const auto& Span : Before.HeaderSpans)
        if (Span.Kind == "imports")
        {
            Table = true;
            for (std::size_t Index = 0; Index < Before.Imports.size(); ++Index)
            {
                const auto& Import = Before.Imports[Index];
                const auto At = Span.Offset + Index * 28;
                if (Import.Name == Storage || Import.Name == "Default__" + Storage)
                    P4MapKit::Export::PackageRewriteDetail::Put(Candidate, At + 16, EnginePackage);
                if (Import.Class == Storage)
                    P4MapKit::Export::PackageRewriteDetail::Put(
                        Candidate, At, uint32(EngineName - Before.Names.begin()));
                for (const std::size_t Field : {std::size_t(8), std::size_t(20)})
                {
                    uint64 Name = 0, Number = 0;
                    if (!P4MapKit::Export::PackageRewriteDetail::Scalar(Candidate, At + Field, Name) ||
                        !P4MapKit::Export::PackageRewriteDetail::Scalar(Candidate, At + Field + 4, Number))
                    {
                        Error = TEXT("light-cook-import-name-unreadable");
                        return false;
                    }
                    const auto To = NameSlots.find(uint32(Name));
                    if (To == NameSlots.end())
                        continue;
                    if (Number)
                    {
                        Error = TEXT("light-cook-numbered-class-identity");
                        return false;
                    }
                    P4MapKit::Export::PackageRewriteDetail::Put(Candidate, At + Field, To->second);
                }
            }
        }
    if (!Table)
    {
        Error = TEXT("light-cook-import-table-missing");
        return false;
    }
    P4MapKit::Export::RewrittenPackage Rewritten;
    std::string Why;
    if (!P4MapKit::Export::RewriteCookedNames(Candidate, Body, Rename, Rewritten, Why))
    {
        Error = UTF8_TO_TCHAR(Why.c_str());
        return false;
    }
    if (Rewritten.Body != Body)
    {
        Error = TEXT("light-cook-translation-changed-export-bytes");
        return false;
    }
    const auto After = P4MapKit::ReadPackage(Rewritten.Header, Rewritten.Body);
    if (!After.Error.Code.empty())
    {
        Error = UTF8_TO_TCHAR(After.Error.Code.c_str());
        return false;
    }
    for (const auto& Import : After.Imports)
        if (Import.Name == Storage || Import.Name == "Default__" + Storage || Import.Class == Storage)
        {
            Error = TEXT("light-cook-class-identity-not-restored");
            return false;
        }
    Header = std::move(Rewritten.Header);
    Error.Reset();
    return true;
}
