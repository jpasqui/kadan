#pragma once
#include "CoreMinimal.h"

// Kadan's settings, in the [Kadan] section of the editor's per-project settings file. A read leaves
// the value as it was when the file does not have the setting. Writes are kept until Flush.
namespace P4KadanSettings
{
// The names of the settings.
extern const TCHAR* const GameFolder;
extern const TCHAR* const GamePaks;
extern const TCHAR* const Retoc;
extern const TCHAR* const Repak;
extern const TCHAR* const ExportFolder;
extern const TCHAR* const GameLocation;
extern const TCHAR* const UseExtractedPackages;
extern const TCHAR* const ShaderArchive;
extern const TCHAR* const GamePort;
extern const TCHAR* const GameLog;

void Read(const TCHAR* Name, FString& Value);
void Read(const TCHAR* Name, bool& Value);

// The setting's text, empty when the file does not have it.
FString Get(const TCHAR* Name);

void Write(const TCHAR* Name, const FString& Value);
void Write(const TCHAR* Name, bool Value);
void Flush();
} // namespace P4KadanSettings
