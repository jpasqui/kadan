#include "P4KadanSettings.h"
#include "CoreGlobals.h"
#include "Misc/ConfigCacheIni.h"

namespace P4KadanSettings
{
namespace
{
const TCHAR* const Section = TEXT("Kadan");
}

const TCHAR* const GameFolder = TEXT("GameFolder");
const TCHAR* const GamePaks = TEXT("GamePaks");
const TCHAR* const Retoc = TEXT("Retoc");
const TCHAR* const Repak = TEXT("Repak");
const TCHAR* const ExportFolder = TEXT("ExportFolder");
const TCHAR* const GameLocation = TEXT("GameLocation");
const TCHAR* const UseExtractedPackages = TEXT("UseExtractedPackages");
const TCHAR* const ShaderArchive = TEXT("ShaderArchive");
const TCHAR* const GamePort = TEXT("GamePort");
const TCHAR* const GameLog = TEXT("GameLog");

void Read(const TCHAR* Name, FString& Value)
{
    if (GConfig)
        GConfig->GetString(Section, Name, Value, GEditorPerProjectIni);
}

void Read(const TCHAR* Name, bool& Value)
{
    if (GConfig)
        GConfig->GetBool(Section, Name, Value, GEditorPerProjectIni);
}

FString Get(const TCHAR* Name)
{
    FString Value;
    Read(Name, Value);
    return Value;
}

void Write(const TCHAR* Name, const FString& Value)
{
    if (GConfig)
        GConfig->SetString(Section, Name, *Value, GEditorPerProjectIni);
}

void Write(const TCHAR* Name, bool Value)
{
    if (GConfig)
        GConfig->SetBool(Section, Name, Value, GEditorPerProjectIni);
}

void Flush()
{
    if (GConfig)
        GConfig->Flush(false, GEditorPerProjectIni);
}
} // namespace P4KadanSettings
