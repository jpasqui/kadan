// Reading an import plan. See the header.

#include "P4BlueprintPlan.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Misc/FileHelper.h"
#include "Misc/FrameRate.h"
#include "Misc/OutputDevice.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"

namespace P4Plan
{
	const TCHAR* const Format = TEXT("p4-blueprint-plan");
	const int32 Version = 1;

	TSharedPtr<FJsonObject> Load(const FString& File, FString& Why)
	{
		FString Text;
		TSharedPtr<FJsonObject> Plan;
		if (!FFileHelper::LoadFileToString(Text, *File))
		{
			Why = FString::Printf(TEXT("cannot read %s"), *File);
			return nullptr;
		}
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<TCHAR>::Create(Text), Plan) || !Plan.IsValid())
		{
			Why = FString::Printf(TEXT("%s is not JSON"), *File);
			return nullptr;
		}
		FString Held;
		double Number = 0;
		if (!Plan->TryGetStringField(TEXT("format"), Held) || Held != Format || !Plan->TryGetNumberField(TEXT("version"), Number) || (int32)Number != Version)
		{
			Why = FString::Printf(TEXT("%s is not an import plan of version %d (it says \"%s\", version %d)"), *File, Version, *Held, (int32)Number);
			return nullptr;
		}
		return Plan;
	}

	uint64 Flags(const TSharedPtr<FJsonObject>& Record, const TCHAR* Field)
	{
		FString Text;
		return Record->TryGetStringField(Field, Text) ? FCString::Strtoui64(*Text, nullptr, 16) : 0;
	}

	float Number(const TSharedPtr<FJsonValue>& Text)
	{
		return FCString::Atof(*Text->AsString());
	}

	namespace
	{
		// The members of one name anywhere inside the value of a property.
		void MembersNamed(const FProperty* Property, FName Name, TArray<const FProperty*>& Out, TSet<const UStruct*>& Seen)
		{
			if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
			{
				if (Seen.Contains(Struct->Struct))
				{
					return;
				}
				Seen.Add(Struct->Struct);
				for (TFieldIterator<FProperty> It(Struct->Struct); It; ++It)
				{
					if (It->GetFName() == Name)
					{
						Out.Add(*It);
					}
					MembersNamed(*It, Name, Out, Seen);
				}
			}
			else if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
			{
				MembersNamed(Array->Inner, Name, Out, Seen);
			}
			else if (const FSetProperty* Set = CastField<FSetProperty>(Property))
			{
				MembersNamed(Set->ElementProp, Name, Out, Seen);
			}
			else if (const FMapProperty* Map = CastField<FMapProperty>(Property))
			{
				MembersNamed(Map->KeyProp, Name, Out, Seen);
				MembersNamed(Map->ValueProp, Name, Out, Seen);
			}
		}

		// The name a text of members starts with ("Name=value,...").
		FString FirstName(const FString& Text)
		{
			int32 End = 0;
			while (End < Text.Len() && (FChar::IsAlnum(Text[End]) || Text[End] == TEXT('_')))
			{
				++End;
			}
			return Text.Left(End);
		}

		// What the editor's text import says while it imports: each line is a member of a struct it did
		// not take. It says two things (FProperty::ImportSingleProperty): "Unknown property in <struct>:
		// <the text from the member on>" for a member the struct does not have, and "Cannot perform text
		// import on property '<member>' here: ..." for a member it has and does not import as text: a
		// transient one (the state of a running node, which no asset stores), which is counted.
		struct FRefusals : public FOutputDevice
		{
			const FProperty* Root;
			TArray<FString>& Lines;
			int32 Transient = 0;
			FRefusals(const FProperty* InRoot, TArray<FString>& InLines) : Root(InRoot), Lines(InLines) {}
			virtual void Serialize(const TCHAR* Text, ELogVerbosity::Type Verbosity, const FName& Category) override
			{
				static const FString Unknown(TEXT("Unknown property in "));
				static const FString NotAsText(TEXT("Cannot perform text import on property '"));
				const FString Line = FString(Text).TrimStartAndEnd();
				FString Struct, Rest;
				if (Line.StartsWith(Unknown) && Line.RightChop(Unknown.Len()).Split(TEXT(": "), &Struct, &Rest) && !FirstName(Rest).IsEmpty())
				{
					// The same words are said of a member the struct has whose value did not read (an entry
					// of an enum that only the game's engine has), and of an array given the text of an
					// empty array, which is nothing: the editor has emptied the array as asked.
					const FString Name = FirstName(Rest);
					const UStruct* Named = FindObject<UStruct>(ANY_PACKAGE, *Struct);
					if (!Named || !Named->FindPropertyByName(FName(*Name)))
					{
						Lines.Add(FString::Printf(TEXT("no member %s in %s"), *Name, *Struct));
						return;
					}
					FString Given = Rest.RightChop(Name.Len()).TrimStart();
					Given.RemoveFromStart(TEXT("="));
					int32 End = 0;
					while (End < Given.Len() && Given[End] != TEXT(',') && Given[End] != TEXT(')'))
					{
						++End;
					}
					Given = Given.Left(End).TrimStartAndEnd();
					if (!Given.IsEmpty())
					{
						Lines.Add(FString::Printf(TEXT("member %s of %s did not take the value %s"), *Name, *Struct, *Given.Left(80)));
					}
					return;
				}
				if (Line.StartsWith(NotAsText))
				{
					FString Name = Line.RightChop(NotAsText.Len());
					int32 End = 0;
					Name = Name.FindChar(TEXT('\''), End) ? Name.Left(End) : FString();
					TArray<const FProperty*> Found;
					TSet<const UStruct*> Seen;
					MembersNamed(Root, FName(*Name), Found, Seen);
					bool bTransient = Found.Num() > 0;
					for (const FProperty* Member : Found)
					{
						bTransient = bTransient && Member->HasAnyPropertyFlags(CPF_Transient | CPF_NonPIEDuplicateTransient);
					}
					if (bTransient)
					{
						++Transient;
						return;
					}
				}
				Lines.Add(Line.Left(160));
			}
		};

		// Moves over one value of a struct's text, to the comma or bracket that ends it.
		void SkipValue(const TCHAR*& At)
		{
			int32 Depth = 0;
			bool bQuoted = false;
			for (; *At; ++At)
			{
				if (bQuoted)
				{
					if (*At == TEXT('\\') && At[1])
					{
						++At;
					}
					else if (*At == TEXT('"'))
					{
						bQuoted = false;
					}
				}
				else if (*At == TEXT('"'))
				{
					bQuoted = true;
				}
				else if (*At == TEXT('('))
				{
					++Depth;
				}
				else if (*At == TEXT(')'))
				{
					if (!Depth)
					{
						return;
					}
					--Depth;
				}
				else if (*At == TEXT(',') && !Depth)
				{
					return;
				}
			}
		}

		// Collect references first, so a malformed later element cannot cause partial asset creation.
		bool ObjectReferences(const FProperty* Property, const FString& Given, int32 Depth, TArray<TPair<FString, UClass*>>& Out)
		{
			if (!Property || Depth > 32) return false;
			if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_NonPIEDuplicateTransient | CPF_Deprecated)) return true;
			const FString Text = Given.TrimStartAndEnd();
			if (const FObjectPropertyBase* Object = CastField<FObjectPropertyBase>(Property))
			{
				// Class and soft references have different loading semantics; these are hard asset leaves.
				if (Property->IsA<FClassProperty>() || Property->IsA<FSoftObjectProperty>()) return true;
				FString Path = Text;
				if (Path.StartsWith(TEXT("\"")))
				{
					if (Path.Len() < 2 || !Path.EndsWith(TEXT("\""))) return false;
					Path = Path.Mid(1, Path.Len() - 2);
				}
				// Do not reinterpret escaped or class-qualified text as an asset identity.
				if (Path.Contains(TEXT("\"")) || Path.Contains(TEXT("\\")) || Path.Contains(TEXT("'"))) return false;
				if (Path.StartsWith(TEXT("/Game/"))) Out.Emplace(Path, Object->PropertyClass);
				return true;
			}
			const FStructProperty* Struct = CastField<FStructProperty>(Property);
			const FArrayProperty* Array = CastField<FArrayProperty>(Property);
			if (!Struct && !Array) return true;
			if (!Text.StartsWith(TEXT("(")) || !Text.EndsWith(TEXT(")"))) return Text.IsEmpty() && Array;
			const TCHAR* At = *Text + 1;
			const TCHAR* End = *Text + Text.Len() - 1;
			while (At < End)
			{
				while (At < End && FChar::IsWhitespace(*At)) ++At;
				if (At == End) break;
				const FProperty* Leaf = Array ? Array->Inner : nullptr;
				if (Struct)
				{
					const TCHAR* Name = At;
					while (At < End && (FChar::IsAlnum(*At) || *At == TEXT('_'))) ++At;
					if (At == Name) return false;
					const FName MemberName((int32)(At - Name), Name);
					while (At < End && FChar::IsWhitespace(*At)) ++At;
					if (At == End || *At++ != TEXT('=')) return false;
					Leaf = Struct->Struct->FindPropertyByName(MemberName);
				}
				const TCHAR* Start = At;
				SkipValue(At);
				if (At <= Start || At > End) return false;
				if (Leaf && !ObjectReferences(Leaf, FString((int32)(At - Start), Start), Depth + 1, Out)) return false;
				if (At < End)
				{
					if (*At++ != TEXT(',') || At == End) return false;
				}
			}
			return At == End;
		}

		// The members a text gives a value that the editor has only to read old files (deprecated ones).
		// The text import takes such a value without a word, and no save writes it: the value is lost.
		// At: the start of the value of Property; it is moved to the value's end.
		void OldMembers(const FProperty* Property, const TCHAR*& At, TArray<FString>& Out)
		{
			const TCHAR* End = At;
			SkipValue(End);
			const FStructProperty* Struct = CastField<FStructProperty>(Property);
			const FArrayProperty* Array = CastField<FArrayProperty>(Property);
			if (*At == TEXT('(') && Struct)
			{
				++At;
				while (*At && *At != TEXT(')') && At < End)
				{
					const TCHAR* Name = At;
					while (FChar::IsAlnum(*At) || *At == TEXT('_'))
					{
						++At;
					}
					if (*At != TEXT('=') || At == Name)
					{
						break;   // not "name=value": a form this does not read
					}
					const FProperty* Member = Struct->Struct->FindPropertyByName(FName((int32)(At - Name), Name));
					const TCHAR* Given = ++At;
					if (Member && Member->HasAnyPropertyFlags(CPF_Deprecated))
					{
						SkipValue(At);
						if (At > Given)
						{
							Out.AddUnique(FString::Printf(TEXT("member %s of %s is only read from old files, its value is not kept: %s"),
								*Member->GetName(), *Struct->Struct->GetName(), *FString((int32)(At - Given), Given).Left(80)));
						}
					}
					else if (Member)
					{
						OldMembers(Member, At, Out);
					}
					else
					{
						SkipValue(At);
					}
					if (*At == TEXT(','))
					{
						++At;
					}
				}
			}
			else if (*At == TEXT('(') && Array)
			{
				++At;
				while (*At && *At != TEXT(')') && At < End)
				{
					OldMembers(Array->Inner, At, Out);
					if (*At == TEXT(','))
					{
						++At;
					}
				}
			}
			At = End;
		}

		// The channel a plan's "at" leads to from a value: a name is a member of the struct here (with the
		// place in a fixed array after it), a number is an element of the array here.
		FMovieSceneFloatChannel* ChannelAt(FProperty* Property, void* Address, const TArray<TSharedPtr<FJsonValue>>& At)
		{
			for (int32 Step = 0; Step < At.Num(); ++Step)
			{
				if (At[Step]->Type == EJson::String)
				{
					FStructProperty* Struct = CastField<FStructProperty>(Property);
					FProperty* Member = Struct ? Struct->Struct->FindPropertyByName(FName(*At[Step]->AsString())) : nullptr;
					if (!Member)
					{
						return nullptr;
					}
					int32 Index = 0;
					if (Member->ArrayDim > 1 && At.IsValidIndex(Step + 1) && At[Step + 1]->Type == EJson::Number)
					{
						Index = (int32)At[++Step]->AsNumber();
						if (Index < 0 || Index >= Member->ArrayDim)
						{
							return nullptr;
						}
					}
					Address = Member->ContainerPtrToValuePtr<void>(Address, Index);
					Property = Member;
				}
				else
				{
					FArrayProperty* Array = CastField<FArrayProperty>(Property);
					if (!Array)
					{
						return nullptr;
					}
					FScriptArrayHelper Elements(Array, Address);
					const int32 Index = (int32)At[Step]->AsNumber();
					if (!Elements.IsValidIndex(Index))
					{
						return nullptr;
					}
					Address = Elements.GetRawPtr(Index);
					Property = Array->Inner;
				}
			}
			FStructProperty* Struct = CastField<FStructProperty>(Property);
			return Struct && Struct->Struct == FMovieSceneFloatChannel::StaticStruct() ? static_cast<FMovieSceneFloatChannel*>(Address) : nullptr;
		}
	}

	bool ImportText(FProperty* Property, const FString& Text, void* Address, UObject* Owner, TArray<FString>& Refused, int32* Transient)
	{
		FRefusals Said(Property, Refused);
		// The editor names a member it does not have only when this log category is at "verbose".
		const ELogVerbosity::Type Before = LogExec.GetVerbosity();
		LogExec.SetVerbosity(ELogVerbosity::Verbose);
		const bool bTaken = Property->ImportText(*Text, Address, PPF_None, Owner, &Said) != nullptr;
		LogExec.SetVerbosity(Before);
		const TCHAR* At = *Text;
		OldMembers(Property, At, Refused);
		if (Transient)
		{
			*Transient += Said.Transient;
		}
		return bTaken;
	}

	void VisitObjectReferences(const FProperty* Property, const FString& Text, TFunctionRef<void(const FString&, UClass*)> Visit)
	{
		TArray<TPair<FString, UClass*>> References;
		if (ObjectReferences(Property, Text, 0, References))
			for (const TPair<FString, UClass*>& Reference : References) Visit(Reference.Key, Reference.Value);
	}

	bool SetChannels(FProperty* Property, void* Address, const TArray<TSharedPtr<FJsonValue>>& Channels, FString& Why)
	{
		for (const TSharedPtr<FJsonValue>& Item : Channels)
		{
			const TSharedPtr<FJsonObject> Record = Item->AsObject();
			const TArray<TSharedPtr<FJsonValue>>* At = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Keys = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Tick = nullptr;
			FString Default;
			bool bHasDefault = false;
			if (!Record.IsValid() || !Record->TryGetArrayField(TEXT("at"), At) || !Record->TryGetArrayField(TEXT("keys"), Keys) ||
				!Record->TryGetArrayField(TEXT("tick"), Tick) || Tick->Num() != 2 || !Record->TryGetStringField(TEXT("default"), Default) ||
				!Record->TryGetBoolField(TEXT("has_default"), bHasDefault))
			{
				Why = TEXT("a key channel's record does not read");
				return false;
			}
			FMovieSceneFloatChannel* Channel = ChannelAt(Property, Address, *At);
			if (!Channel)
			{
				TArray<FString> Steps;
				for (const TSharedPtr<FJsonValue>& Step : *At)
				{
					Steps.Add(Step->Type == EJson::String ? Step->AsString() : FString::FromInt((int32)Step->AsNumber()));
				}
				Why = FString::Printf(TEXT("no key channel at %s"), *FString::Join(Steps, TEXT(".")));
				return false;
			}
			// [frame, value, interpolation, tangent mode, arrive tangent, leave tangent, tangent weight mode,
			//  arrive weight, leave weight, padding byte]
			TArray<FFrameNumber> Times;
			TArray<FMovieSceneFloatValue> Values;
			Times.Reserve(Keys->Num());
			Values.Reserve(Keys->Num());
			for (const TSharedPtr<FJsonValue>& Each : *Keys)
			{
				const TArray<TSharedPtr<FJsonValue>>& Key = Each->AsArray();
				if (Key.Num() != 10)
				{
					Why = TEXT("a key that does not have its ten parts");
					return false;
				}
				FMovieSceneFloatValue Value(Number(Key[1]));
				Value.InterpMode = (ERichCurveInterpMode)(uint8)Key[2]->AsNumber();
				Value.TangentMode = (ERichCurveTangentMode)(uint8)Key[3]->AsNumber();
				Value.Tangent.ArriveTangent = Number(Key[4]);
				Value.Tangent.LeaveTangent = Number(Key[5]);
				Value.Tangent.TangentWeightMode = (ERichCurveTangentWeightMode)(uint8)Key[6]->AsNumber();
				Value.Tangent.ArriveTangentWeight = Number(Key[7]);
				Value.Tangent.LeaveTangentWeight = Number(Key[8]);
				Value.PaddingByte = (uint8)Key[9]->AsNumber();
				Times.Add(FFrameNumber((int32)Key[0]->AsNumber()));
				Values.Add(Value);
			}
			Channel->Set(MoveTemp(Times), MoveTemp(Values));
			Channel->PreInfinityExtrap = (ERichCurveExtrapolation)(uint8)Record->GetNumberField(TEXT("pre"));
			Channel->PostInfinityExtrap = (ERichCurveExtrapolation)(uint8)Record->GetNumberField(TEXT("post"));
			// The file holds the default's number also when the channel is marked as having none.
			Channel->SetDefault(FCString::Atof(*Default));
			if (!bHasDefault)
			{
				Channel->RemoveDefault();
			}
			Channel->SetTickResolution(FFrameRate((int32)(*Tick)[0]->AsNumber(), (int32)(*Tick)[1]->AsNumber()));
		}
		return true;
	}
}
