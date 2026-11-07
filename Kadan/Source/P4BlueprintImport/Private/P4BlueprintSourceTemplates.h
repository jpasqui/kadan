#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
class UBlueprint;
class UActorComponent;
namespace P4Graphs { struct FReport; }

// The original cooked class can name inherited component templates (and differently named
// children of them). The import keeps those original records with the editor asset and makes each
// missing template once, after its compile, through the engine's component handler. A template a
// creator later changes or removes stays as the creator left it. No distinguishing property is invented.
P4BLUEPRINTIMPORT_API void P4RememberBlueprintSourceTemplates(UBlueprint*, const TSharedPtr<FJsonObject>&);
P4BLUEPRINTIMPORT_API void P4RestoreBlueprintSourceTemplates(UBlueprint*, P4Graphs::FReport&);
void P4ApplyBlueprintSourceTemplateValues(UActorComponent*, const TSharedPtr<FJsonObject>&, P4Graphs::FReport&);
// A template of the game's that says nothing (no values, no objects of its own) has what the parent's
// template has. The editor removes such a template when it compiles or loads the class, so the import
// makes none: the class has the parent's template, as it does after its next load.
bool P4BlueprintSourceTemplateSaysNothing(const TSharedPtr<FJsonObject>& Record);
