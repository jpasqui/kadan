// The default values of the stub classes. See P4StubDefaults.h.

#include "P4StubDefaults.h"
#include "Misc/OutputDeviceNull.h"
#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

namespace
{
	FCriticalSection GLock;
	TSet<const FP4StubDefault*> GReported;
	TArray<FString> GProblems;
	int32 GAppliedToDefaults = 0;

	// "A.B" under an object: its default subobject A, then A's default subobject B.
	UObject* FindSub(UObject* Object, const TCHAR* Path)
	{
		FString Rest(Path);
		while (Object && !Rest.IsEmpty())
		{
			FString Head;
			if (!Rest.Split(TEXT("."), &Head, &Rest))
			{
				Head = Rest;
				Rest.Empty();
			}
			Object = Object->GetDefaultSubobjectByName(FName(*Head));
		}
		return Object;
	}

	// An object in memory by its path. A class default object ("/Script/M.Default__Name") that does not
	// exist yet is made by its class, as the engine does on first use.
	UObject* FindByPath(const TCHAR* Path)
	{
		if (UObject* Found = FindObject<UObject>(nullptr, Path))
		{
			return Found;
		}
		FString Package, Name;
		if (FString(Path).Split(TEXT(".Default__"), &Package, &Name))
		{
			if (UClass* Class = FindObject<UClass>(nullptr, *(Package + TEXT(".") + Name)))
			{
				return Class->GetDefaultObject();
			}
		}
		return nullptr;
	}

	// The property a path names and where its value is. Struct members by ".", fixed arrays by "[n]".
	bool Resolve(UStruct* Struct, void* Container, const TCHAR* Path, FProperty*& OutProperty, void*& OutValue, bool* Transient = nullptr)
	{
		if (Transient) *Transient = false;
		FString Rest(Path);
		for (;;)
		{
			FString Head;
			if (!Rest.Split(TEXT("."), &Head, &Rest))
			{
				Head = Rest;
				Rest.Empty();
			}
			int32 Index = 0;
			int32 Bracket = INDEX_NONE;
			if (Head.FindChar(TEXT('['), Bracket))
			{
				Index = FCString::Atoi(*Head + Bracket + 1);
				Head.LeftInline(Bracket);
			}
			FProperty* Property = Struct->FindPropertyByName(FName(*Head));
			if (!Property || Index < 0 || Index >= Property->ArrayDim)
			{
				return false;
			}
			if (Transient && Property->HasAnyPropertyFlags(CPF_Transient)) *Transient = true;
			void* Value = Property->ContainerPtrToValuePtr<void>(Container, Index);
			if (Rest.IsEmpty())
			{
				OutProperty = Property;
				OutValue = Value;
				return true;
			}
			FStructProperty* Inner = CastField<FStructProperty>(Property);
			if (!Inner)
			{
				return false;
			}
			Struct = Inner->Struct;
			Container = Value;
		}
	}

	const TCHAR* ApplyRow(UObject* Object, const FP4StubDefault& Row)
	{
		UObject* Target = *Row.Sub ? FindSub(Object, Row.Sub) : Object;
		if (!Target)
		{
			return TEXT("no such subobject");
		}
		FProperty* Property = nullptr;
		void* Value = nullptr;
		bool Transient = false;
		if (!Resolve(Target->GetClass(), Target, Row.Property, Property, Value, &Transient))
		{
			return TEXT("no such property");
		}
		if (Transient) return TEXT("transient captured default was not applied");
		if (Row.Kind == FP4StubDefault::Text)
		{
			static FOutputDeviceNull Quiet;
			return Property->ImportText(Row.Value, Value, PPF_None, Target, &Quiet) ? nullptr : TEXT("value not accepted");
		}
		FObjectPropertyBase* Reference = CastField<FObjectPropertyBase>(Property);
		UObject* To = Row.Kind == FP4StubDefault::Subobject ? FindSub(Object, Row.Value) : FindByPath(Row.Value);
		if (!Reference)
		{
			return TEXT("not an object property");
		}
		if (!To)
		{
			return TEXT("no such object");
		}
		if (!To->IsA(Reference->PropertyClass))
		{
			return TEXT("object of another class");
		}
		Reference->SetObjectPropertyValue(Value, To);
		return nullptr;
	}
}

void FP4StubDefaults::Apply(UObject* Object, const TCHAR* Class, const FP4StubDefault* Rows, int32 Count)
{
	const bool bDefaults = Object->HasAnyFlags(RF_ClassDefaultObject);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const TCHAR* Problem = ApplyRow(Object, Rows[Index]);
		if (Problem || bDefaults)
		{
			FScopeLock Scope(&GLock);
			if (!Problem)
			{
				++GAppliedToDefaults;
			}
			else if (!GReported.Contains(&Rows[Index]))
			{
				GReported.Add(&Rows[Index]);
				GProblems.Add(FString::Printf(TEXT("%s|%s|%s|%s|%s"), Class, Rows[Index].Sub, Rows[Index].Property, Rows[Index].Value, Problem));
			}
		}
	}
}

namespace
{
	// Native class -> the constructor function it had, and class -> the rows given for it.
	TMap<UClass*, UClass::ClassConstructorType> GConstructors;
	TMap<UClass*, TArray<const FP4EngineDefault*>> GEngineRows;
	TSet<const FP4EngineDefault*> GEngineRowsPlaced;
	TSet<const FP4EngineDefault*> GEngineRowsReported;

	// The rows of the object's class and of the classes above it, a parent's first: a class below
	// that has a row of its own for the property keeps its own value.
	void ApplyEngineRows(UObject* Object)
	{
		TArray<UClass*, TInlineAllocator<16>> Chain;
		for (UClass* Class = Object->GetClass(); Class; Class = Class->GetSuperClass())
		{
			if (GEngineRows.Contains(Class))
			{
				Chain.Add(Class);
			}
		}
		for (int32 Index = Chain.Num() - 1; Index >= 0; --Index)
		{
			for (const FP4EngineDefault* Row : GEngineRows[Chain[Index]])
			{
				const FP4StubDefault Text = { TEXT(""), Row->Property, Row->Value, FP4StubDefault::Text };
				if (const TCHAR* Problem = ApplyRow(Object, Text))
				{
					FScopeLock Scope(&GLock);
					if (!GEngineRowsReported.Contains(Row))
					{
						GEngineRowsReported.Add(Row);
						GProblems.Add(FString::Printf(TEXT("%s||%s|%s|%s"), Row->Class, Row->Property, Row->Value, Problem));
					}
				}
			}
		}
	}

	void ConstructWithEngineRows(const FObjectInitializer& Initializer)
	{
		UObject* Object = Initializer.GetObj();
		UClass::ClassConstructorType Original = nullptr;
		{
			// A Blueprint class has its parent's function: the nearest native class says which one it was.
			FScopeLock Scope(&GLock);
			for (UClass* Class = Object->GetClass(); Class && !Original; Class = Class->GetSuperClass())
			{
				if (UClass::ClassConstructorType* Found = GConstructors.Find(Class))
				{
					Original = *Found;
				}
			}
		}
		check(Original);
		(*Original)(Initializer);
		ApplyEngineRows(Object);
	}

	// Called at start and after each module is loaded (its classes exist by then, with their default objects).
	void PlaceEngineRows()
	{
		bool bNew = false;
		{
			FScopeLock Scope(&GLock);
			for (const FP4EngineDefault* Row = GP4EngineDefaults; Row->Class; ++Row)
			{
				UClass* Class = GEngineRowsPlaced.Contains(Row) ? nullptr : FindObject<UClass>(nullptr, Row->Class);
				if (Class)
				{
					GEngineRowsPlaced.Add(Row);
					GEngineRows.FindOrAdd(Class).Add(Row);
					bNew = true;
				}
			}
		}
		if (!GEngineRows.Num())
		{
			return;
		}
		TArray<UClass*> Wrapped;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Class = *It;
			if (!Class->HasAnyClassFlags(CLASS_Native) || !Class->ClassConstructor || Class->ClassConstructor == &ConstructWithEngineRows)
			{
				continue;
			}
			bool bHasRows = false;
			for (UClass* Above = Class; Above && !bHasRows; Above = Above->GetSuperClass())
			{
				bHasRows = GEngineRows.Contains(Above);
			}
			if (bHasRows)
			{
				FScopeLock Scope(&GLock);
				GConstructors.Add(Class, Class->ClassConstructor);
				Class->ClassConstructor = &ConstructWithEngineRows;
				Wrapped.Add(Class);
			}
		}
		// What was made before: the default objects, and the objects other default objects hold.
		for (UClass* Class : Wrapped)
		{
			ForEachObjectOfClass(Class, [](UObject* Object) { ApplyEngineRows(Object); }, false, RF_NoFlags);
		}
		if (bNew)
		{
			for (const TPair<UClass*, UClass::ClassConstructorType>& Pair : GConstructors)
			{
				if (!Wrapped.Contains(Pair.Key))
				{
					ForEachObjectOfClass(Pair.Key, [](UObject* Object) { ApplyEngineRows(Object); }, false, RF_NoFlags);
				}
			}
		}
	}

	class FP4StubsCoreModule : public IModuleInterface
	{
	public:
		virtual void StartupModule() override
		{
			if (!GP4EngineDefaults[0].Class)
			{
				return;
			}
			PlaceEngineRows();
			Handle = FModuleManager::Get().OnModulesChanged().AddLambda([](FName, EModuleChangeReason Reason)
			{
				if (Reason == EModuleChangeReason::ModuleLoaded)
				{
					PlaceEngineRows();
				}
			});
		}

		virtual void ShutdownModule() override
		{
			if (Handle.IsValid())
			{
				FModuleManager::Get().OnModulesChanged().Remove(Handle);
			}
		}

	private:
		FDelegateHandle Handle;
	};
}

IMPLEMENT_MODULE(FP4StubsCoreModule, P4StubsCore);

TArray<FString> UP4StubDefaultsLibrary::Problems()
{
	FScopeLock Scope(&GLock);
	return GProblems;
}

int32 UP4StubDefaultsLibrary::AppliedToDefaults()
{
	FScopeLock Scope(&GLock);
	return GAppliedToDefaults;
}

FString UP4StubDefaultsLibrary::Read(UObject* Object, const FString& Sub, const FString& Property)
{
	UObject* Target = Object && !Sub.IsEmpty() ? FindSub(Object, *Sub) : Object;
	if (!Target)
	{
		return TEXT("?no such subobject");
	}
	FProperty* Found = nullptr;
	void* Value = nullptr;
	if (!Resolve(Target->GetClass(), Target, *Property, Found, Value))
	{
		return TEXT("?no such property");
	}
	FString Out;
	Found->ExportText_Direct(Out, Value, Value, Target, PPF_None);
	return Out;
}

bool UP4StubDefaultsLibrary::Matches(UObject* Object, const FString& Sub, const FString& Property, const FString& Value)
{
	UObject* Target = Object && !Sub.IsEmpty() ? FindSub(Object, *Sub) : Object;
	FProperty* Found = nullptr;
	void* Held = nullptr;
	if (!Target || !Resolve(Target->GetClass(), Target, *Property, Found, Held))
	{
		return false;
	}
	FOutputDeviceNull Quiet;
	void* Scratch = FMemory::Malloc(Found->GetSize(), Found->GetMinAlignment());
	Found->InitializeValue(Scratch);
	const bool bSame = Found->ImportText(*Value, Scratch, PPF_None, Target, &Quiet) && Found->Identical(Scratch, Held, PPF_None);
	Found->DestroyValue(Scratch);
	FMemory::Free(Scratch);
	return bSame;
}
