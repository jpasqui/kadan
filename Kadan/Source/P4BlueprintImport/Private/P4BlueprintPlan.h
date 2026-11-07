// Reading an import plan (decomp/blueprints/plan.py, import/<package>.json): the file with its format
// and version checked, and the two forms a plan writes numbers in that JSON numbers cannot hold exactly.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class FProperty;

namespace P4Plan
{
	// The format and version of plan this module reads. A plan of another version is refused whole:
	// a field read with an older meaning would be imported wrongly without a word.
	extern const TCHAR* const Format;
	extern const int32 Version;

	// The plan of a file, or null with the reason.
	TSharedPtr<FJsonObject> Load(const FString& File, FString& Why);

	// A set of flags, which a plan writes as hexadecimal text ("0x1C"). 0 when the field is not there.
	uint64 Flags(const TSharedPtr<FJsonObject>& Record, const TCHAR* Field);

	// A decimal number, which a plan writes as text that reads back as the same number.
	float Number(const TSharedPtr<FJsonValue>& Text);

	// Gives a property the value of a text, as the editor imports text. What the editor will not take is
	// added to Refused, a line for each: "no member <name> in <struct>" for a member of a struct it
	// does not have, "member <name> of <struct> did not take the value <text>" for a value it does
	// not know (an entry of an enum only the game's engine has), and "member <name> of <struct> is
	// only read from old files" for a member it takes and never saves. The import still succeeds
	// without that part, and nothing else would say so. A transient member (the state of a running
	// animation node, which the game's file holds and no asset stores) is not taken either: those
	// are counted in Transient, not listed. False when no part was taken.
	bool ImportText(FProperty* Property, const FString& Text, void* Address, UObject* Owner, TArray<FString>& Refused, int32* Transient = nullptr);

	// Visits hard object references using the reflected class of each leaf. Only the import text
	// forms used by direct references, arrays and structs are read; malformed values yield no visits.
	P4BLUEPRINTIMPORT_API void VisitObjectReferences(const FProperty* Property, const FString& Text, TFunctionRef<void(const FString&, UClass*)> Visit);

	// Gives the key channels inside one stored value their keys, through the sequencer's own channel
	// interface. Channels: the value's "channels" in the plan, each with "at", the member names and
	// array places from the value down to the channel. Property and Address: the value. False with
	// the reason when a channel is not where the plan says or its record does not read.
	bool SetChannels(FProperty* Property, void* Address, const TArray<TSharedPtr<FJsonValue>>& Channels, FString& Why);
}
