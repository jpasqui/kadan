// Ordinary Blueprint assets from import plans. See the header.

#include "P4BlueprintImport.h"
#include "P4StockEngineFields.h"
#include "P4BlueprintTestSeams.h"
#include "P4BlueprintSubobjectStorage.h"
#include "P4RecoveredPostProcessStorage.h"
#include "P4BlueprintSourceTemplates.h"
#include "P4BlueprintWidgetMembers.h"
#include "P4OriginalImportSave.h"
#include "P4BlueprintGraphs.h"
#include "P4BlueprintIds.h"
#include "P4BlueprintReimport.h"
#include "Editor.h"
#include "P4BlueprintPlan.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/Skeleton.h"
#include "AssetRegistryModule.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "K2Node_FunctionEntry.h"
#include "WidgetBlueprint.h"
#include "Animation/WidgetAnimation.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Engine/Level.h"
#include "Engine/LevelScriptActor.h"
#include "Engine/LevelScriptBlueprint.h"
#include "Engine/World.h"
#include "K2Node_Event.h"
#include "LevelSequence.h"
#include "LevelSequenceDirector.h"
#include "MovieScene.h"
#include "MovieSceneSequence.h"
#include "Channels/MovieSceneEvent.h"
#include "K2Node_CustomEvent.h"
#include "MovieSceneEventUtils.h"
#include "Sections/MovieSceneEventSectionBase.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Dom/JsonObject.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/InheritableComponentHandler.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/DataTable.h"
#include "MeshDescription.h"
#include "Materials/Material.h"
#include "StaticMeshAttributes.h"
#include "GameFramework/Actor.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/Package.h"
#include "Modules/ModuleManager.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"
#include "UObject/MetaData.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"

namespace
{
	const FName StandInTag(TEXT("P4BlueprintStandIn"));
	bool TaggedStandIn(const UObject* Object)
	{
	return Object && FString(Object->GetOutermost()->GetMetaData()->GetValue(Object, StandInTag)) == TEXT("1");
	}
}

#if P4_BLUEPRINT_TEST_SEAMS
FString P4BlueprintApplySourceValuesForTest(UObject* Target, const TSharedPtr<FJsonObject>& Record);
namespace { FP4BlueprintTestSeams BlueprintTestSeams; }
#endif

class FP4BlueprintImportModule : public FDefaultModuleImpl
{
public:
	virtual void StartupModule() override
	{
		UObject::GetMetaDataTagsForAssetRegistry().Add(StandInTag);
#if P4_BLUEPRINT_TEST_SEAMS
		BlueprintTestSeams.ApplySourceValues = &P4BlueprintApplySourceValuesForTest;
		IModularFeatures::Get().RegisterModularFeature(FP4BlueprintTestSeams::FeatureName(), &BlueprintTestSeams);
#endif
	}
	virtual void ShutdownModule() override
	{
#if P4_BLUEPRINT_TEST_SEAMS
		IModularFeatures::Get().UnregisterModularFeature(FP4BlueprintTestSeams::FeatureName(), &BlueprintTestSeams);
#endif
	}
};
IMPLEMENT_MODULE(FP4BlueprintImportModule, P4BlueprintImport);

namespace
{
	TArray<FString> StandInsMade;
	// The sub-objects made for the Blueprint being given its values, by the path the plan's values name
	// them with. A name with a dot in it (the game has some) cannot be found again from text.
	TMap<FString, TWeakObjectPtr<UObject>> SubobjectsMade;
	TSet<FString> SubobjectsRefused;
	void ResetSubobjects() { SubobjectsMade.Reset(); SubobjectsRefused.Reset(); }
	// The objects the plan's Blueprint names that retoc could not name in its converted package. The map
	// import brings in each one the game ships; one the game does not ship stays an empty reference.
	const TSet<FString>* ReferenceOnlyPaths = nullptr;
	// A map import makes nothing in place of a game asset the project does not hold. The asset stays the
	// game's: a value that names it keeps the game's value in the Blueprint's record with a note, the
	// editor shows the reference empty, and the reference is told, with where it is named.
	bool bKeepAbsentAsTheGames = false;
	TMap<FString, FString> KeptAsTheGames;

	// True when the game asset at Path stays the game's in this run: a /Game asset the project does not
	// hold, outside the packages this run imports. Where names the value or pin that refers to it.
	bool KeepAsTheGames(const FString& Path, const FString& Where)
	{
		if (!bKeepAbsentAsTheGames || !Path.StartsWith(TEXT("/Game/"))) return false;
		const FString PackageName = FPackageName::ObjectPathToPackageName(Path);
		if (P4Reimport::IsRunPackage(PackageName) || FindObject<UObject>(nullptr, *Path) || FPackageName::DoesPackageExist(PackageName)) return false;
		if (!KeptAsTheGames.Contains(Path) || KeptAsTheGames[Path].IsEmpty()) KeptAsTheGames.Add(Path, Where);
		return true;
	}

	using P4Graphs::FReport;
	using P4Graphs::LoadPlan;

	// The pin type of a plan's type record: {"c": category, "o": object path, "k": container, "v": map value}.
	bool PinType(const TSharedPtr<FJsonObject>& Record, FEdGraphPinType& Out, FString& Why)
	{
		Out.PinCategory = FName(*Record->GetStringField(TEXT("c")));
		if (Out.PinCategory == UEdGraphSchema_K2::PC_Delegate || Out.PinCategory == UEdGraphSchema_K2::PC_MCDelegate)
		{
			Why = TEXT("delegate variables are not made yet");
			return false;
		}
		FString Path;
		if (Record->TryGetStringField(TEXT("o"), Path))
		{
			UObject* Object = StaticLoadObject(UObject::StaticClass(), nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (!Object)
			{
				Why = FString::Printf(TEXT("%s is not in the project"), *Path);
				return false;
			}
			Out.PinSubCategoryObject = Object;
		}
		bool bWeak = false;
		if (Record->TryGetBoolField(TEXT("weak"), bWeak))
		{
			Out.bIsWeakPointer = bWeak;
		}
		FString Container;
		if (Record->TryGetStringField(TEXT("k"), Container))
		{
			Out.ContainerType = Container == TEXT("array") ? EPinContainerType::Array
				: Container == TEXT("set") ? EPinContainerType::Set : EPinContainerType::Map;
			const TSharedPtr<FJsonObject>* Value = nullptr;
			if (Out.ContainerType == EPinContainerType::Map)
			{
				FEdGraphPinType ValueType;
				if (!Record->TryGetObjectField(TEXT("v"), Value) || !PinType(*Value, ValueType, Why))
				{
					return false;
				}
				Out.PinValueType = FEdGraphTerminalType::FromPinType(ValueType);
			}
		}
		return true;
	}

	// The node of a component variable in the Blueprint parents of a Blueprint.
	USCS_Node* InheritedNode(UBlueprint* Blueprint, FName VariableName)
	{
		for (UClass* Class = Blueprint->ParentClass; Class; Class = Class->GetSuperClass())
		{
			UBlueprintGeneratedClass* Generated = Cast<UBlueprintGeneratedClass>(Class);
			if (Generated && Generated->SimpleConstructionScript)
			{
				if (USCS_Node* Node = Generated->SimpleConstructionScript->FindSCSNode(VariableName))
				{
					return Node;
				}
				// Every Blueprint keeps a scene root node of its own for the day it has no other root. Only
				// the one in use is a component of the class: a child names the root of the Blueprint that
				// has it, not the spare one of a parent in between.
				USCS_Node* Root = Generated->SimpleConstructionScript->GetDefaultSceneRootNode();
				if (Root && Root->GetVariableName() == VariableName && Generated->SimpleConstructionScript->GetRootNodes().Contains(Root))
				{
					return Root;
				}
			}
		}
		return nullptr;
	}

	// A component the native part of the parent class makes itself.
	USceneComponent* NativeComponent(UBlueprint* Blueprint, FName Name)
	{
		AActor* Defaults = Blueprint->ParentClass ? Cast<AActor>(Blueprint->ParentClass->GetDefaultObject()) : nullptr;
		if (!Defaults)
		{
			return nullptr;
		}
		TInlineComponentArray<USceneComponent*> Found;
		Defaults->GetComponents(Found);
		for (USceneComponent* Component : Found)
		{
			if (Component && Component->GetFName() == Name)
			{
				return Component;
			}
		}
		return nullptr;
	}

	void DeclareVariables(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Plan, FReport& Report)
	{
		for (const TSharedPtr<FJsonValue>& Item : Plan->GetArrayField(TEXT("variables")))
		{
			const TSharedPtr<FJsonObject> Record = Item->AsObject();
			const FString Name = Record->GetStringField(TEXT("name"));
			FBPVariableDescription Variable;
			FString Why;
			if (Record->GetObjectField(TEXT("type"))->GetStringField(TEXT("c")) == TEXT("mcdelegate"))
			{
				// An event dispatcher: the variable and the graph that holds its signature, as the editor's
				// "add event dispatcher" makes them.
				const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
				FEdGraphPinType Type;
				Type.PinCategory = UEdGraphSchema_K2::PC_MCDelegate;
				if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, FName(*Name), Type))
				{
					Report.Problem(FString::Printf(TEXT("dispatcher %s was not made"), *Name));
					continue;
				}
				UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, FName(*Name), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
				Graph->bEditable = false;
				Schema->CreateDefaultNodesForGraph(*Graph);
				Schema->CreateFunctionGraphTerminators(*Graph, (UClass*)nullptr);
				Schema->AddExtraFunctionFlags(Graph, (FUNC_BlueprintCallable | FUNC_BlueprintEvent | FUNC_Public));
				Schema->MarkFunctionEntryAsEditable(Graph, true);
				Blueprint->DelegateSignatureGraphs.Add(Graph);
				const TArray<TSharedPtr<FJsonValue>>* Inputs = nullptr;
				TArray<UK2Node_FunctionEntry*> Entries;
				Graph->GetNodesOfClass(Entries);
				if (Record->TryGetArrayField(TEXT("inputs"), Inputs) && Entries.Num())
				{
					for (const TSharedPtr<FJsonValue>& Input : *Inputs)
					{
						FEdGraphPinType InputType;
						if (PinType(Input->AsObject()->GetObjectField(TEXT("type")), InputType, Why))
						{
							Entries[0]->CreateUserDefinedPin(FName(*Input->AsObject()->GetStringField(TEXT("name"))), InputType, EGPD_Output, false);
						}
						else
						{
							Report.Problem(FString::Printf(TEXT("dispatcher %s: parameter %s"), *Name, *Why));
						}
					}
				}
				const int32 Index = FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, FName(*Name));
				if (Index != INDEX_NONE)
				{
					Blueprint->NewVariables[Index].PropertyFlags |= P4Plan::Flags(Record, TEXT("flags"));
					Blueprint->NewVariables[Index].VarGuid = P4Ids::OfVariable(Blueprint, FName(*Name));
				}
				Report.Count(TEXT("dispatchers"));
				continue;
			}
			if (!PinType(Record->GetObjectField(TEXT("type")), Variable.VarType, Why))
			{
				Report.Problem(FString::Printf(TEXT("variable %s: %s"), *Name, *Why));
				continue;
			}
			Variable.VarName = FName(*Name);
			Variable.VarGuid = P4Ids::OfVariable(Blueprint, Variable.VarName);   // the same in every project
			Variable.FriendlyName = FName::NameToDisplayString(Name, Variable.VarType.PinCategory == UEdGraphSchema_K2::PC_Boolean);
			Variable.Category = UEdGraphSchema_K2::VR_DefaultCategory;
			Variable.PropertyFlags = P4Plan::Flags(Record, TEXT("flags"));
			FString Notify;
			if (Record->TryGetStringField(TEXT("rep_notify"), Notify))
			{
				Variable.RepNotifyFunc = FName(*Notify);
			}
			Blueprint->NewVariables.Add(Variable);
			Report.Count(TEXT("variables"));
		}
	}

	void ApplyNodeMetadata(USCS_Node* Node,const TSharedPtr<FJsonObject>& Record,FReport& Report)
	{
		if(!Record->HasField(TEXT("node_metadata")))return;
		const TArray<TSharedPtr<FJsonValue>>* Metadata=nullptr;
		if(!Record->TryGetArrayField(TEXT("node_metadata"),Metadata))
		{Report.Problem(TEXT("source component-node metadata is not an array"));return;}
		for(const auto& Value:*Metadata)
		{
			const auto Entry=Value.IsValid()&&Value->Type==EJson::Object?Value->AsObject():nullptr;FString Key,Text;
			if(!Node||!Entry.IsValid()||Entry->Values.Num()!=2||!Entry->TryGetStringField(TEXT("DataKey"),Key)||
				!Entry->TryGetStringField(TEXT("DataValue"),Text)||Key.IsEmpty()||Key.Len()>=NAME_SIZE)
			{Report.Problem(TEXT("source component-node metadata has no exact name/string entry"));continue;}
			Node->Modify();Node->SetMetaData(FName(*Key),Text);
			Report.Count(TEXT("component-node metadata entries"));
		}
	}

	void DeclareComponents(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Plan, FReport& Report)
	{
		const TArray<TSharedPtr<FJsonValue>>& List = Plan->GetArrayField(TEXT("components"));
		USimpleConstructionScript* Script = Blueprint->SimpleConstructionScript;
		if (!Script)
		{
			if (List.Num())
			{
				Report.Problem(TEXT("the plan has components and this kind of Blueprint has no component tree"));
			}
			return;
		}
		Script->ValidateSceneRootNodes();
		TMap<FName, USCS_Node*> Made;
		for (const TSharedPtr<FJsonValue>& Item : List)
		{
			const TSharedPtr<FJsonObject> Record = Item->AsObject();
			const FName Name(*Record->GetStringField(TEXT("variable")));
			const FString ClassPath = Record->GetStringField(TEXT("cls"));
			UClass* Class = LoadObject<UClass>(nullptr, *ClassPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (!Class || !Class->IsChildOf(UActorComponent::StaticClass()))
			{
				Report.Problem(FString::Printf(TEXT("component %s: class %s is not in the project"), *Name.ToString(), *ClassPath));
				continue;
			}
			USCS_Node* Node = Script->FindSCSNode(Name);
			if (Node && Node->ComponentClass != Class) { Report.Stop(TEXT("reimport cannot change an existing component class")); return; }
			bool bDefaultRoot = false;
			Record->TryGetBoolField(TEXT("default_scene_root"), bDefaultRoot);
			if (bDefaultRoot)
			{
				Node = Script->GetDefaultSceneRootNode();
			}
			const bool bNew = Node == nullptr;
			if (bNew)
			{
				Node = Script->CreateNode(Class, Name);
			}
			if (!Node)
			{
				Report.Problem(FString::Printf(TEXT("component %s: the editor made no node"), *Name.ToString()));
				continue;
			}
			FString Guid;
			if (!Record->TryGetStringField(TEXT("guid"), Guid) || !FGuid::Parse(Guid, Node->VariableGuid))
			{
				Node->VariableGuid = P4Ids::OfComponent(Blueprint, Name);   // the game's file holds none
			}
			FString ParentName;
			bool bNative = false;
			Record->TryGetStringField(TEXT("parent"), ParentName);
			Record->TryGetBoolField(TEXT("parent_native"), bNative);
			USCS_Node** Own = ParentName.IsEmpty() ? nullptr : Made.Find(FName(*ParentName));
			if (Own)
			{
				if (!(*Own)->GetChildNodes().Contains(Node)) (*Own)->AddChildNode(Node);
			}
			else
			{
				if (bNew || !Script->GetRootNodes().Contains(Node))
				{
					Script->AddNode(Node);
				}
				if (!ParentName.IsEmpty())
				{
					USceneComponent* Native = bNative ? NativeComponent(Blueprint, FName(*ParentName)) : nullptr;
					USCS_Node* Inherited = bNative ? nullptr : InheritedNode(Blueprint, FName(*ParentName));
					if (Native)
					{
						Node->SetParent(Native);
					}
					else if (Inherited)
					{
						Node->SetParent(Inherited);
					}
					else
					{
						Report.Problem(FString::Printf(TEXT("component %s: parent component %s was not found"), *Name.ToString(), *ParentName));
					}
				}
			}
			FString Socket;
			if (Record->TryGetStringField(TEXT("socket"), Socket))
			{
				Node->AttachToName = FName(*Socket);
			}
			Made.Add(Name, Node);
			ApplyNodeMetadata(Node,Record,Report);
			Report.Count(TEXT("components"));
		}
		// Children in the game's order (the plan lists them so): a node kept from an earlier import keeps its
		// place in its parent's list, and the class's file says the order.
		TMap<USCS_Node*, TArray<USCS_Node*>> Order;
		for (const TSharedPtr<FJsonValue>& Item : List)
		{
			const TSharedPtr<FJsonObject> Record = Item->AsObject();
			FString ParentName;
			USCS_Node** Child = Made.Find(FName(*Record->GetStringField(TEXT("variable"))));
			USCS_Node** Own = Record->TryGetStringField(TEXT("parent"), ParentName) && !ParentName.IsEmpty()
				? Made.Find(FName(*ParentName)) : nullptr;
			if (Child && Own && (*Own)->GetChildNodes().Contains(*Child))
				Order.FindOrAdd(*Own).AddUnique(*Child);
		}
		for (const auto& Pair : Order)
		{
			TArray<USCS_Node*> Wanted;
			for (USCS_Node* Child : Pair.Key->GetChildNodes())
				if (!Pair.Value.Contains(Child))
					Wanted.Add(Child);   // a child the plan does not name keeps its place before the others
			Wanted.Append(Pair.Value);
			if (Wanted == Pair.Key->GetChildNodes())
				continue;
			for (USCS_Node* Child : Wanted)
				Pair.Key->RemoveChildNode(Child, false);
			for (USCS_Node* Child : Wanted)
				Pair.Key->AddChildNode(Child, false);
		}
		// The scene root every Blueprint keeps for the day it has no other root: the plan does not name
		// one that is not in use, and the editor gave it an ID at random.
		if (USCS_Node* Spare = Script->GetDefaultSceneRootNode())
		{
			if (!Made.Contains(Spare->GetVariableName()))
			{
				Spare->VariableGuid = P4Ids::OfComponent(Blueprint, Spare->GetVariableName());
				// The editor renames the template of a root a creator removed, and the game's file keeps
				// that name. The template takes the game's name, so the class says what the game's says.
				FString Source;
				UActorComponent* Template = Spare->ComponentTemplate;
				if (Template && Plan->TryGetStringField(TEXT("default_scene_root_template"), Source) && !Source.IsEmpty() &&
					Template->GetFName() != FName(*Source))
				{
					FText Why;
					UObject* Taken = StaticFindObjectFast(nullptr, Template->GetOuter(), FName(*Source));
					if (!FName(*Source).IsValidObjectName(Why) || (Taken && Taken != Template))
						Report.Problem(TEXT("the unused scene root's template cannot take the game's name ") + Source);
					else
						Template->Rename(*Source, nullptr, REN_DontCreateRedirectors | REN_ForceNoResetLoaders | REN_DoNotDirty);
				}
			}
		}
	}

	bool Compile(UBlueprint* Blueprint, FReport& Report)
	{
#if P4_BLUEPRINT_TEST_SEAMS
		if(FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_BLUEPRINT_COMPILE_TRACE"))==TEXT("1"))
			UE_LOG(LogTemp,Display,TEXT("Native Blueprint compile: %s"),*Blueprint->GetPathName());
#endif
		const auto CompileBlueprint = [&]() { P4Reimport::WithCompilerSavePolicy([&]() {
			FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection | EBlueprintCompileOptions::SkipSave); }); };
		UAnimBlueprint* Animation = Cast<UAnimBlueprint>(Blueprint);
		if (Animation && P4Reimport::IsFreshStandIn(Animation->TargetSkeleton))
		{
			FString Why;
			// The compiler registers default slots on its target skeleton. Only our unchanged,
			// newly created placeholder can receive this synchronous engine initialization.
			if (!P4Reimport::ConfigureStandIn(Animation->TargetSkeleton, CompileBlueprint, Why))
			{
				Report.Problem(Why); return false;
			}
		}
		else { CompileBlueprint(); }
		if (Blueprint->Status == BS_Error || !Blueprint->GeneratedClass)
		{
			Report.Problem(TEXT("the Blueprint did not compile"));
			return false;
		}
		if (Blueprint->Status != BS_UpToDate)
		{
			Report.Count(TEXT("compiled with warnings"));
		}
		P4Ids::NameBindings(Blueprint);
		return true;
	}

	// An empty object of the named class at the named path when nothing is there.
	void StandIn(const FString& Path, const FString& ClassPath, FReport& Report)
	{
		FString PackageName, ObjectName;
		if (!Path.Split(TEXT("."), &PackageName, &ObjectName) || ObjectName.Contains(TEXT(".")) || ObjectName.Contains(TEXT(":")))
		{
			Report.Problem(FString::Printf(TEXT("no stand-in for %s: not an asset path"), *Path));
			return;
		}
		// A failed StaticLoadObject can itself leave an empty package in memory. Capture existence
		// before attempting that load; its loader-created shell is not an existing user package.
		const bool bPackageExisted = FPackageName::DoesPackageExist(PackageName) || FindPackage(nullptr, *PackageName);
		if (StaticLoadObject(UObject::StaticClass(), nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			return;
		}
		if (!bPackageExisted && KeepAsTheGames(Path, FString()))
		{
			Report.Count(TEXT("game assets the project does not hold, kept as the game's"));
			return;
		}
		UClass* Class = LoadObject<UClass>(nullptr, *ClassPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Class || Class->HasAnyClassFlags(CLASS_Abstract))
		{
			Report.Problem(FString::Printf(TEXT("no stand-in for %s: class %s cannot be made"), *Path, *ClassPath));
			return;
		}
		// A missing object in an existing package is not permission to alter that package. Never
		// manufacture assets on an engine/plugin mount, or over an unreadable/user-created file.
		if (!PackageName.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(PackageName)
			|| bPackageExisted)
		{
			Report.Problem(FString::Printf(TEXT("no stand-in for %s: only an absent /Game package can be made"), *Path));
			return;
		}
		UPackage* Package = CreatePackage(*PackageName);
		UObject* Made = NewObject<UObject>(Package, Class, FName(*ObjectName), RF_Public | RF_Standalone | RF_Transactional);
		if (ULevelSequence* Sequence = Cast<ULevelSequence>(Made))
		{
			Sequence->Initialize();   // an empty movie scene: the editor stops on a sequence that has none
		}
		if (UDataTable* Table = Cast<UDataTable>(Made))
		{
			// A null row type emits load/cook errors even for a reference-only empty table.
			// This engine base carries no game rows or claimed recovered row declaration.
			Table->RowStruct = FTableRowBase::StaticStruct();
		}
		if (UStaticMesh* Mesh = Cast<UStaticMesh>(Made))
		{
			// A static mesh with no geometry stops the editor when it is loaded again ("Bad
			// MeshDescription"), so the stand-in holds one small triangle.
			Mesh->AddSourceModel();
			FMeshDescription* Description = Mesh->CreateMeshDescription(0);
			FStaticMeshAttributes Attributes(*Description);
			TVertexAttributesRef<FVector> Positions = Attributes.GetVertexPositions();
			const FPolygonGroupID Group = Description->CreatePolygonGroup();
			const FVector Corners[3] = { FVector(0, 0, 0), FVector(0, 10, 0), FVector(10, 0, 0) };
			TArray<FVertexInstanceID> Instances;
			for (const FVector& Corner : Corners)
			{
				const FVertexID Vertex = Description->CreateVertex();
				Positions[Vertex] = Corner;
				Instances.Add(Description->CreateVertexInstance(Vertex));
			}
			Description->CreatePolygon(Group, Instances);
			Mesh->CommitMeshDescription(0);
			Mesh->StaticMaterials.Add(FStaticMaterial());
			// Finish native render/source initialization before recording the creation baseline.
			// A later Blueprint compile can otherwise build this same placeholder lazily.
			Mesh->Build(false);
		}
		if (USkeletalMesh* Skinned = Cast<USkeletalMesh>(Made))
		{
			// A skeletal mesh loaded from a file always has render data, empty when it has no model. A
			// skeletal mesh component registered with one that has none stops the editor, as a level's
			// import does with an actor of a Blueprint that names this stand-in.
			Skinned->AllocateResourceForRendering();
		}
		if (!Made)
		{
			Report.Problem(FString::Printf(TEXT("no stand-in for %s"), *Path));
			return;
		}
		Package->GetMetaData()->SetValue(Made, StandInTag, TEXT("1"));
		P4Reimport::RecordStandInCreated(Made);
		FAssetRegistryModule::AssetCreated(Made);
		Package->MarkPackageDirty();
		StandInsMade.Add(PackageName);
		Report.Count(TEXT("stand-ins"));
	}

	// True for a map, a set, or a struct that holds one (not through an array: an array is stored whole).
	bool HoldsMapOrSet(const FProperty* Property)
	{
		if (Property->IsA<FMapProperty>() || Property->IsA<FSetProperty>())
		{
			return true;
		}
		if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
		{
			for (TFieldIterator<FProperty> It(Struct->Struct); It; ++It)
			{
				if (HoldsMapOrSet(*It))
				{
					return true;
				}
			}
		}
		return false;
	}

	// After a text import into Now: every map and set in it gets the entries it had Before, under the
	// ones the import put there.
	void KeepEntries(const FProperty* Property, void* Before, void* Now)
	{
		if (const FMapProperty* Map = CastField<FMapProperty>(Property))
		{
			FScriptMapHelper Imported(Map, Now), Kept(Map, Before);
			for (int32 At = 0; At < Imported.GetMaxIndex(); ++At)
			{
				if (Imported.IsValidIndex(At))
				{
					Kept.AddPair(Imported.GetKeyPtr(At), Imported.GetValuePtr(At));
				}
			}
			Kept.Rehash();
			Map->CopySingleValue(Now, Before);
		}
		else if (const FSetProperty* Set = CastField<FSetProperty>(Property))
		{
			FScriptSetHelper Imported(Set, Now), Kept(Set, Before);
			for (int32 At = 0; At < Imported.GetMaxIndex(); ++At)
			{
				if (Imported.IsValidIndex(At))
				{
					Kept.AddElement(Imported.GetElementPtr(At));
				}
			}
			Kept.Rehash();
			Set->CopySingleValue(Now, Before);
		}
		else if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
		{
			for (TFieldIterator<FProperty> It(Struct->Struct); It; ++It)
			{
				if (HoldsMapOrSet(*It))
				{
					for (int32 Index = 0; Index < It->ArrayDim; ++Index)
					{
						KeepEntries(*It, It->ContainerPtrToValuePtr<void>(Before, Index), It->ContainerPtrToValuePtr<void>(Now, Index));
					}
				}
			}
		}
	}

	bool OriginalSettingsMemberType(const TSharedPtr<FJsonObject>& Record, const FP4RecoveredPostProcessMember& Member)
	{
		const FProperty* Property = Member.Property;
		FString Type;
		if (!Property || !Record->TryGetStringField(TEXT("type"), Type) || Property->GetClass()->GetName() != Type) return false;
		const TArray<TSharedPtr<FJsonValue>>* Detail = nullptr;
		if (!Record->TryGetArrayField(TEXT("detail"), Detail)) return false;
		TArray<FString> Expected;
		if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
		{
			FString SourceStruct;
			if (!Record->TryGetStringField(TEXT("struct"), SourceStruct) || SourceStruct != Struct->Struct->GetName()) return false;
			Expected.Add(Struct->Struct->GetName());
		}
		else if (const FByteProperty* Byte = CastField<FByteProperty>(Property)) Expected.Add(Member.OriginalDepthOfFieldMethod ? TEXT("EDepthOfFieldMethod") : Byte->Enum ? Byte->Enum->GetName() : TEXT("None"));
		else if (const FEnumProperty* Enum = CastField<FEnumProperty>(Property)) Expected.Add(Enum->GetEnum()->GetName());
		else if (const FArrayProperty* Array = CastField<FArrayProperty>(Property)) Expected.Add(Array->Inner->GetClass()->GetName());
		else if (const FSetProperty* Set = CastField<FSetProperty>(Property)) Expected.Add(Set->ElementProp->GetClass()->GetName());
		else if (const FMapProperty* Map = CastField<FMapProperty>(Property)) { Expected.Add(Map->KeyProp->GetClass()->GetName()); Expected.Add(Map->ValueProp->GetClass()->GetName()); }
		if (Expected.Num() != Detail->Num()) return false;
		for (int32 N = 0; N < Expected.Num(); ++N) { FString Given; if (!(*Detail)[N].IsValid() || !(*Detail)[N]->TryGetString(Given) || Given != Expected[N]) return false; }
		return true;
	}
	struct FApplyValue
	{
		TSharedPtr<FJsonObject> Record, Original;
		FProperty* Property = nullptr;
		void* Address = nullptr;
		bool bSettingsMember = false;
	};
	void Apply(UObject* Target, const TArray<TSharedPtr<FJsonValue>>& Values, const FString& Where, FReport& Report)
	{
		FP4RecoveredPostProcessStorage Storage;
		FString StorageError;
		const bool bRecovered = P4GetRecoveredPostProcessStorage(Target->GetClass(), Storage, StorageError);
		TArray<FApplyValue> Work;
		for (const TSharedPtr<FJsonValue>& Item : Values)
		{
			const TSharedPtr<FJsonObject> Record = Item->AsObject();
			const FString Name = Record->GetStringField(TEXT("name"));
			if ((bRecovered || !StorageError.IsEmpty()) && (FName(*Name) == FName(TEXT("Settings")) || FName(*Name) == FName(TEXT("ForkSettings"))))
			{
				const TArray<TSharedPtr<FJsonValue>>* Members = nullptr;
				FString Type, Struct;
				double Index = -1;
				FString Why = StorageError;
				if (Why.IsEmpty() && (Name != TEXT("Settings") || !Record->TryGetStringField(TEXT("type"), Type) || Type != TEXT("StructProperty")
					|| !Record->TryGetStringField(TEXT("struct"), Struct) || Struct != TEXT("PostProcessSettings")
					|| !Record->TryGetNumberField(TEXT("index"), Index) || Index != 0 || !Record->TryGetArrayField(TEXT("members"), Members) || Members->Num() > 100000))
					Why = TEXT("missing original typed Settings member contract, or direct extension source path");
				TArray<FApplyValue> Pending;
				TSet<FString> Seen;
				if (Why.IsEmpty()) for (const TSharedPtr<FJsonValue>& Value : *Members)
				{
					const TSharedPtr<FJsonObject>* OriginalPtr = nullptr;
					if (!Value.IsValid() || !Value->TryGetObject(OriginalPtr) || !OriginalPtr || !OriginalPtr->IsValid())
					{ Why = TEXT("malformed original Settings member record"); break; }
					const TSharedPtr<FJsonObject>& Original = *OriginalPtr;
					FString MemberName, Text;
					double MemberIndex = -1;
					FP4RecoveredPostProcessMember Member;
					if (!Original.IsValid() || !Original->TryGetStringField(TEXT("name"), MemberName)
						|| !Original->TryGetNumberField(TEXT("index"), MemberIndex) || !FMath::IsFinite(MemberIndex) || MemberIndex < 0 || MemberIndex > MAX_int32 || MemberIndex != double(int32(MemberIndex))
						|| !P4ResolveRecoveredPostProcessMember(Storage, FName(*MemberName), Member) || Member.Property->GetName() != MemberName || !OriginalSettingsMemberType(Original, Member)
						|| MemberIndex >= Member.Property->ArrayDim || !Original->TryGetStringField(TEXT("text"), Text)
						|| Original->HasField(TEXT("channels")) || Member.Property->HasAnyPropertyFlags(CPF_Transient))
					{ Why = TEXT("unknown or incompatible original Settings member: ") + MemberName; break; }
					const FString Identity = MemberName.ToLower() + TEXT("[") + FString::FromInt(int32(MemberIndex)) + TEXT("]");
					if (Seen.Contains(Identity)) { Why = TEXT("duplicate original Settings member: ") + MemberName; break; }
					Seen.Add(Identity);
					TSharedPtr<FJsonObject> NativeRecord = MakeShared<FJsonObject>(*Original);
					if (Member.OriginalDepthOfFieldMethod)
					{
						FString NativeName;
						if (!P4RecoveredPostProcessSourceEnum(Member, Text, NativeName)) { Why = TEXT("unknown original depth-of-field enum choice: ") + Text; break; }
						NativeRecord->SetStringField(TEXT("text"), NativeName);
					}
					void* Address = P4RecoveredPostProcessMemberData(Target, Storage, Member);
					if (!Address) { Why = TEXT("unavailable native Settings storage: ") + MemberName; break; }
					// MemberData returns the single property's first element, never a source offset.
					Address = static_cast<uint8*>(Address) + int32(MemberIndex) * Member.Property->ElementSize;
					FApplyValue Task; Task.Record = NativeRecord; Task.Original = Record;
					Task.Property = const_cast<FProperty*>(Member.Property); Task.Address = Address; Task.bSettingsMember = true; Pending.Add(Task);
				}
				if (!Why.IsEmpty())
				{
					P4Reimport::RetainOriginalValue(Target, Record);
					Report.Problem(FString::Printf(TEXT("%s: Settings: %s"), *Where, *Why));
				}
				else Work.Append(Pending);
				continue;
			}
			FApplyValue Task; Task.Record = Record; Task.Original = Record; Work.Add(Task);
		}
		for (const FApplyValue& Task : Work)
		{
			const TSharedPtr<FJsonObject>& Record = Task.Record;
			const FString Name = (Task.bSettingsMember ? TEXT("Settings.") : TEXT("")) + Record->GetStringField(TEXT("name"));
			const int32 Index = (int32)Record->GetNumberField(TEXT("index"));
			// A value is text the editor imports, and the key channels in it apart: their keys go to the
			// sequencer's own channel interface. A value that is a key channel has no text.
			FString Text;
			const bool bText = Record->TryGetStringField(TEXT("text"), Text);
			const TArray<TSharedPtr<FJsonValue>>* Channels = nullptr;
			Record->TryGetArrayField(TEXT("channels"), Channels);
			FProperty* Property = Task.Property ? Task.Property : Target->GetClass()->FindPropertyByName(FName(*Name));
			if (!Property)
			{
				FString Why;
				const auto Stored = P4ApplyStockEngineFieldPlan(Target, Record, Why);
				if (Stored == EP4StockEngineField::Stored) { Report.Count(TEXT("values")); continue; }
				if (Stored == EP4StockEngineField::Refused) { Report.Problem(Where + TEXT(": ") + Why); continue; }
			}
			if (!Property || Index < 0 || Index >= Property->ArrayDim)
			{
				P4Reimport::RetainOriginalValue(Target, Task.Original);
				Report.Problem(FString::Printf(TEXT("%s: no property %s"), *Where, *Name));
				continue;
			}
			if (Property->HasAnyPropertyFlags(CPF_Transient))
			{
				Report.Problem(FString::Printf(TEXT("%s: transient property %s was not imported"), *Where, *Name));
				continue;
			}
			void* Address = Task.Address ? Task.Address : Property->ContainerPtrToValuePtr<void>(Target, Index);
			if (!bText)
			{
				FString Why;
				if (!Channels || !P4Plan::SetChannels(Property, Address, *Channels, Why))
				{
					P4Reimport::RetainOriginalValue(Target, Task.Original);
					Report.Problem(FString::Printf(TEXT("%s: %s did not take its keys: %s"), *Where, *Name, Channels ? *Why : TEXT("the plan gives the value neither text nor keys")));
					continue;
				}
				Report.Count(TEXT("values"));
				Report.Count(TEXT("key channels"), Channels->Num());
				continue;
			}
			if (Text.IsEmpty() && (Property->IsA<FSetProperty>() || Property->IsA<FMapProperty>()))
			{
				Report.Count(TEXT("values"));   // nothing added or changed
				continue;
			}
			if (Text.IsEmpty() && Property->IsA<FArrayProperty>())
			{
				Property->ClearValue(Address);
				Report.Count(TEXT("values"));
				continue;
			}
			if (FObjectPropertyBase* Reference = CastField<FObjectPropertyBase>(Property))
			{
				if (SubobjectsRefused.Contains(Text))
				{
					Report.Problem(FString::Printf(TEXT("%s: %s did not take refused source sub-object %s"), *Where, *Name, *Text));
					continue;
				}
				if (const TWeakObjectPtr<UObject>* Cached = SubobjectsMade.Find(Text))
				{
					UObject* Known = Cached->Get(); // Weak identity does not keep the object alive.
					if (!Known || !Reference->PropertyClass || !Known->IsA(Reference->PropertyClass))
					{
						Report.Problem(FString::Printf(TEXT("%s: %s did not take source object %s: unavailable or incompatible property class"), *Where, *Name, *Text));
						continue;
					}
					Reference->SetObjectPropertyValue(Address, Known);
					Report.Count(TEXT("values"));
					continue;
				}
			}
			// A stored map or set holds what was added or changed since the object's template (the
			// parent Blueprint's value), not everything, also inside a struct. The text import puts
			// only those entries there, so what was there before is put back under them.
			void* Before = nullptr;
			if (HoldsMapOrSet(Property))
			{
				Before = FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment());
				Property->InitializeValue(Before);
				Property->CopySingleValue(Before, Address);
			}
			TArray<FString> Refused;
			int32 Transient = 0;
			// The game's assets the value names that stay the game's in this run. The text import leaves
			// those references empty; the game's value is kept with a note.
			TArray<FString> Kept;
			P4Plan::VisitObjectReferences(Property, Text, [&](const FString& Path, UClass*)
			{
				if (ReferenceOnlyPaths && ReferenceOnlyPaths->Contains(Path) && !FindObject<UObject>(nullptr, *Path)
					&& !FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(Path)))
				{
					// The text import leaves a reference to an object that is not there empty.
					Report.Count(TEXT("references to objects the game does not ship, left empty"));
					return;
				}
				if (KeepAsTheGames(Path, Target->GetOutermost()->GetName() + TEXT(" ") + Where + TEXT(": ") + Name)) Kept.AddUnique(Path);
			});
			bool bTaken = P4Plan::ImportText(Property, Text, Address, Target, Refused, &Transient);
			if (Kept.Num())
			{
				// What the editor says of a missing asset that stays the game's is not a refusal of the value.
				Refused.RemoveAll([&](const FString& Line)
				{
					for (const FString& Path : Kept)
						if (Line.Contains(Path) || Line.Contains(FPackageName::ObjectPathToPackageName(Path))) return true;
					return false;
				});
				if (!bTaken && CastField<FObjectPropertyBase>(Property))
				{
					// A lone reference the editor did not take keeps no other asset in its place.
					CastField<FObjectPropertyBase>(Property)->SetObjectPropertyValue(Address, nullptr);
					bTaken = true;
				}
				P4Reimport::RetainOriginalValue(Target, Task.Original, FString::Join(Kept, TEXT(", ")));
				Report.Count(TEXT("references to game assets kept as the game's values"), Kept.Num());
			}
			for (const FString& Line : Refused)
			{
				Report.Problem(FString::Printf(TEXT("%s: %s: the editor refused part of the value: %s"), *Where, *Name, *Line));
			}
			if (Refused.Num()) P4Reimport::RetainOriginalValue(Target, Task.Original);
			if (Transient)
			{
				Report.Count(TEXT("transient members not imported (the state of a running node)"), Transient);
			}
			if (Before)
			{
				if (bTaken)
				{
					KeepEntries(Property, Before, Address);
				}
				Property->DestroyValue(Before);
				FMemory::Free(Before);
			}
			if (!bTaken)
			{
				if (!Refused.Num()) P4Reimport::RetainOriginalValue(Target, Task.Original);
				Report.Problem(FString::Printf(TEXT("%s: %s did not take %s"), *Where, *Name, *Text.Left(120)));
				continue;
			}
			if (Channels)
			{
				FString Why;
				if (!P4Plan::SetChannels(Property, Address, *Channels, Why))
				{
					P4Reimport::RetainOriginalValue(Target, Task.Original);
					Report.Problem(FString::Printf(TEXT("%s: %s did not take its keys: %s"), *Where, *Name, *Why));
					continue;
				}
				Report.Count(TEXT("key channels"), Channels->Num());
			}
			Report.Count(TEXT("values"));
		}
	}

	// The template the editor makes for an inherited component starts as a copy of the parent's, with
	// copies of the objects the parent's template holds. The game's file names every object its template
	// has. A copy it does not name, that nothing in the template refers to, is not the game's, and the
	// editor would give it back to the template's references when the class is next loaded (it counts
	// as the template's instance of the parent's object). Such copies are removed.
	void DropUnnamedCopies(UObject* Template, const TSharedPtr<FJsonObject>& Record, FReport& Report)
	{
		UObject* Archetype = Template ? Template->GetArchetype() : nullptr;
		if (!Archetype || Archetype->HasAnyFlags(RF_ClassDefaultObject)) return;
		TSet<FName> Named;
		const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
		if (Record->TryGetArrayField(TEXT("subobjects"), List))
			for (const TSharedPtr<FJsonValue>& Item : *List)
			{
				const TSharedPtr<FJsonObject> Sub = Item.IsValid() ? Item->AsObject() : nullptr;
				const TArray<TSharedPtr<FJsonValue>>* Path = nullptr;
				if (Sub.IsValid() && Sub->TryGetArrayField(TEXT("path"), Path) && Path->Num()) Named.Add(FName(*(*Path)[0]->AsString()));
			}
		TArray<UObject*> Children, Copies;
		GetObjectsWithOuter(Template, Children, false);
		for (UObject* Child : Children)
		{
			UObject* Made = Child->GetArchetype();
			if (!Named.Contains(Child->GetFName()) && Made && Made != Archetype && Made->IsIn(Archetype)) Copies.Add(Child);
		}
		if (!Copies.Num()) return;
		TArray<UObject*> Referenced;
		FReferenceFinder Finder(Referenced, Template, false, true, true, false);
		Finder.FindReferences(Template);
		for (UObject* Copy : Copies)
		{
			if (Referenced.Contains(Copy)) continue;
			Copy->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_ForceNoResetLoaders | REN_DoNotDirty | REN_NonTransactional);
			Copy->MarkPendingKill();
			Report.Count(TEXT("copies of a parent's template objects the game's template does not have, removed"));
		}
	}

	// Objects that live inside a template or the class defaults and are named by its values (the
	// units of a sound handler, the actor of a child actor component). Made under the same outer with
	// the same name, so the value that names one by path finds it.
	void Subobjects(UObject* Owner, const TSharedPtr<FJsonObject>& Record, const FString& Where, FReport& Report)
	{
		const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
		if (!Record->TryGetArrayField(TEXT("subobjects"), List))
		{
			return;
		}
		TArray<TPair<UObject*, TSharedPtr<FJsonObject>>> Made;
		for (const TSharedPtr<FJsonValue>& Item : *List)
		{
			const TSharedPtr<FJsonObject> Sub = Item->AsObject();
			const TArray<TSharedPtr<FJsonValue>>& Path = Sub->GetArrayField(TEXT("path"));
			UObject* Outer = Owner;
			for (int32 Level = 0; Outer && Level + 1 < Path.Num(); ++Level)
			{
				Outer = StaticFindObjectFast(nullptr, Outer, FName(*Path[Level]->AsString()));
			}
			const FString Name = Path.Last()->AsString();
			auto Refuse = [&](const FString& Why)
			{
				Report.Problem(FString::Printf(TEXT("%s: sub-object %s: %s"), *Where, *Name, *Why));
				FString Reference;
				if (Sub->TryGetStringField(TEXT("ref"), Reference)) SubobjectsRefused.Add(Reference);
			};
			if (!Outer)
			{
				Refuse(TEXT("has no outer"));
				continue;
			}
			const FString ClassPath = Sub->GetStringField(TEXT("cls"));
			UClass* Class = LoadObject<UClass>(nullptr, *ClassPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (!Class) { Refuse(TEXT("declared class ") + ClassPath + TEXT(" is unavailable")); continue; }
			UObject* Object = StaticFindObjectFast(nullptr, Outer, FName(*Name));
			if (Object && !P4BlueprintSourceSubobjectClassMatches(Object->GetClass(), Class))
			{ Refuse(TEXT("existing child is incompatible with declared class ") + ClassPath); continue; }
			// Template/default-subobject identity is serialized state. In particular an instanced
			// unit inside a Blueprint component template must remain a template after compilation.
			const EObjectFlags IdentityFlags = RF_Public | RF_ArchetypeObject | RF_DefaultSubObject | RF_InheritableComponentTemplate;
			const EObjectFlags Wanted = Sub->HasField(TEXT("flags"))
				? EObjectFlags(uint32(P4Plan::Flags(Sub, TEXT("flags"))) & uint32(IdentityFlags | RF_Transactional))
				: Outer->GetMaskedFlags(RF_PropagateToSubObjects) | RF_Transactional;
			if (!Object)
			{
				if (Class->HasAnyClassFlags(CLASS_Abstract))
				{
					Refuse(TEXT("class ") + ClassPath + TEXT(" cannot be made"));
					continue;
				}
				// With the flags the game's object has, when the plan gives them (a widget of a tree is
				// transactional and nothing else).
				// The game's file holds what differs from the object's template: the object of the same
				// name inside the parent Blueprint's copy of the outer, when there is one (the actor of a
				// child actor component a child Blueprint changes). It starts as a copy of that.
				UObject* Template = UObject::GetArchetypeFromRequiredInfo(Class, Outer, FName(*Name), Wanted | RF_ArchetypeObject);
				Object = NewObject<UObject>(Outer, Class, FName(*Name), Wanted,
					Template && Template->GetClass() == Class && !Template->HasAnyFlags(RF_ClassDefaultObject) ? Template : nullptr);
				Report.Count(TEXT("sub-objects"));
			}
			else if (Sub->HasField(TEXT("flags")))
			{
				Object->ClearFlags(IdentityFlags);
				Object->SetFlags(Wanted);
			}
			FString Reference;
			if (Sub->TryGetStringField(TEXT("ref"), Reference))
			{
				if (const TWeakObjectPtr<UObject>* Prior = SubobjectsMade.Find(Reference))
					if (Prior->Get() != Object)
					{ Refuse(TEXT("source reference ") + Reference + TEXT(" is ambiguous")); continue; }
				SubobjectsMade.Add(Reference, TWeakObjectPtr<UObject>(Object));
			}
			Made.Emplace(Object, Sub);
		}
		for (const TPair<UObject*, TSharedPtr<FJsonObject>>& Pair : Made)
		{
			const TArray<TSharedPtr<FJsonValue>>* Stored = nullptr;
			if (Pair.Value->TryGetArrayField(TEXT("values"), Stored))
			{
				Apply(Pair.Key, *Stored, FString::Printf(TEXT("%s sub-object %s"), *Where, *Pair.Key->GetName()), Report);
			}
		}
	}

	UBlueprint* Find(const TSharedPtr<FJsonObject>& Plan)
	{
		const FString Path = Plan->GetStringField(TEXT("package")) + TEXT(".") + Plan->GetStringField(TEXT("name"));
		return LoadObject<UBlueprint>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
}

void P4ApplyBlueprintSourceTemplateValues(UActorComponent* Template, const TSharedPtr<FJsonObject>& Record, P4Graphs::FReport& Report)
{
	TGuardValue<TMap<FString, TWeakObjectPtr<UObject>>> MadeScope(SubobjectsMade, {});
	TGuardValue<TSet<FString>> RefusedScope(SubobjectsRefused, {});
	Subobjects(Template, Record, TEXT("original inherited template"), Report);
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (Record->TryGetArrayField(TEXT("values"), Values)) Apply(Template, *Values, TEXT("original inherited template"), Report);
	DropUnnamedCopies(Template, Record, Report);
}

#if P4_BLUEPRINT_TEST_SEAMS
// Development seam: exercises the actual source-subobject and Apply
// branches without a subsequent compiler reset obscuring transient readback.
FString P4BlueprintApplySourceValuesForTest(UObject* Target, const TSharedPtr<FJsonObject>& Record)
{
	FReport Report;
	if (!IsValid(Target) || !Record.IsValid() || P4Reimport::IsRunActive())
	{ Report.Stop(TEXT("private transient fixture requires an idle valid target")); return Report.Text(); }
	ResetSubobjects();
	Subobjects(Target, Record, TEXT("private transient fixture"), Report);
	Apply(Target, Record->GetArrayField(TEXT("values")), TEXT("private transient fixture"), Report);
	ResetSubobjects(); return Report.Text();
}
#endif

void P4MakeStandIn(const FString& Path, const FString& ClassPath, TArray<FString>& Problems)
{
	FReport Report;
	StandIn(Path, ClassPath, Report);
	Problems.Append(Report.Problems);
}

P4BLUEPRINTIMPORT_API void P4RestorePendingStandIns(const TArray<FString>& Packages)
{
	for (const FString& Package : Packages) { StandInsMade.AddUnique(Package); }
}

// For one run: whether a game asset the project does not hold stays the game's instead of getting a
// stand-in. Starting or ending a run forgets the references kept before.
void P4KeepAbsentAsTheGames(bool bKeep)
{
	bKeepAbsentAsTheGames = bKeep;
	KeptAsTheGames.Reset();
}

// The game assets kept as the game's since the run started, each with the first place that names it.
TMap<FString, FString> P4TakeKeptAsTheGames()
{
	TMap<FString, FString> Out = MoveTemp(KeptAsTheGames);
	KeptAsTheGames.Reset();
	return Out;
}

bool P4KeepAsTheGames(const FString& Path, const FString& Where)
{
	return KeepAsTheGames(Path, Where);
}

FString UP4BlueprintImport::Declare(const FString& PlanFile)
{
	FReport Report;
	const TSharedPtr<FJsonObject> Plan = LoadPlan(PlanFile, Report);
	if (!Plan.IsValid())
	{
		return Report.Text();
	}
	const FString Kind = Plan->GetStringField(TEXT("kind"));
	const FString PackageName = Plan->GetStringField(TEXT("package"));
	const FString ParentPath = Plan->GetStringField(TEXT("parent"));
	UClass* Parent = LoadObject<UClass>(nullptr, *ParentPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Parent)
	{
		Report.Stop(FString::Printf(TEXT("parent class %s is not in the project"), *ParentPath));
		return Report.Text();
	}
	UBlueprint* Existing = Find(Plan);
	FString ReimportWhy;
	if (Existing && !P4Reimport::ReuseBlueprint(Existing, Parent, ReimportWhy))
	{
		Report.Stop(ReimportWhy);
		return Report.Text();
	}
	UPackage* Package = CreatePackage(*PackageName);
	// A macro library's class is the only one the compiler marks "cannot be placed" by itself (with
	// "abstract"): the game's file holds none of the macros, only that class.
	const int64 ClassFlags = (int64)P4Plan::Flags(Plan, TEXT("class_flags"));
	const bool bMacros = (ClassFlags & CLASS_Abstract) && (ClassFlags & CLASS_NotPlaceable) && !Parent->HasAnyClassFlags(CLASS_NotPlaceable);
	const EBlueprintType Type = Parent->IsChildOf(UBlueprintFunctionLibrary::StaticClass()) ? BPTYPE_FunctionLibrary
		: bMacros ? BPTYPE_MacroLibrary : BPTYPE_Normal;
	UClass* AssetClass = UBlueprint::StaticClass();
	UClass* GeneratedClass = UBlueprintGeneratedClass::StaticClass();
	if (Kind == TEXT("AnimBlueprint"))
	{
		AssetClass = UAnimBlueprint::StaticClass();
		GeneratedClass = UAnimBlueprintGeneratedClass::StaticClass();
	}
	else if (Kind == TEXT("WidgetBlueprint"))
	{
		AssetClass = UWidgetBlueprint::StaticClass();
		GeneratedClass = UWidgetBlueprintGeneratedClass::StaticClass();
	}
	UBlueprint* Blueprint = Existing;
	if (!Blueprint) P4Reimport::WithCompilerSavePolicy([&]() {
		Blueprint = FKismetEditorUtilities::CreateBlueprint(Parent, Package, FName(*Plan->GetStringField(TEXT("name"))), Type,
			AssetClass, GeneratedClass, FName(TEXT("P4BlueprintImport")));
	});
	if (!Blueprint)
	{
		Report.Stop(TEXT("the editor made no Blueprint"));
		return Report.Text();
	}
	P4Reimport::MarkImported(Blueprint);
	if (UAnimBlueprint* Animation = Cast<UAnimBlueprint>(Blueprint))
	{
		FString SkeletonPath;
		if (Plan->TryGetStringField(TEXT("skeleton"), SkeletonPath) && !SkeletonPath.IsEmpty())
		{
			StandIn(SkeletonPath, TEXT("/Script/Engine.Skeleton"), Report);
			Animation->TargetSkeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		}
		if (!Animation->TargetSkeleton)
		{
			Report.Problem(TEXT("the animation Blueprint has no skeleton"));
		}
	}
	if (!Existing) { FAssetRegistryModule::AssetCreated(Blueprint); }
	// The interfaces the Blueprint itself implements (not the ones its parents do).
	for (const TSharedPtr<FJsonValue>& Item : Plan->GetArrayField(TEXT("interfaces")))
	{
		const FString InterfacePath = Item->AsObject()->GetStringField(TEXT("cls"));
		UClass* Interface = LoadObject<UClass>(nullptr, *InterfacePath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		bool AlreadyPresent = false;
		for (const FBPInterfaceDescription& ExistingInterface : Blueprint->ImplementedInterfaces)
			if (ExistingInterface.Interface == Interface && Interface) AlreadyPresent = true;
		if (AlreadyPresent) { Report.Count(TEXT("interfaces")); continue; }
		if (!Interface || !FBlueprintEditorUtils::ImplementNewInterface(Blueprint, Interface->GetFName()))
		{
			Report.Problem(FString::Printf(TEXT("interface %s was not added"), *InterfacePath));
			continue;
		}
		Report.Count(TEXT("interfaces"));
	}
	DeclareVariables(Blueprint, Plan, Report);
	DeclareComponents(Blueprint, Plan, Report);
	P4Ids::Name(Blueprint);
	P4Reimport::WithCompilerSavePolicy([&]() { FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint); });
	if (!Compile(Blueprint, Report))
	{
		Report.bStopped = true;
	}
	Package->MarkPackageDirty();
	return Report.Text();
}

FString UP4BlueprintImport::LevelScript(UWorld* World, const FString& Record)
{
	FReport Report;
	TSharedPtr<FJsonObject> Held;
	if (!World || !World->PersistentLevel || !FJsonSerializer::Deserialize(TJsonReaderFactory<TCHAR>::Create(Record), Held) || !Held.IsValid())
	{
		Report.Stop(TEXT("no map, or a record that does not read"));
		return Report.Text();
	}
	ULevelScriptBlueprint* Blueprint = World->PersistentLevel->GetLevelScriptBlueprint(false);
	if (!Blueprint)
	{
		Report.Stop(TEXT("the map gave no level Blueprint"));
		return Report.Text();
	}
	const TArray<TSharedPtr<FJsonValue>>* Variables = nullptr;
	if (Held->TryGetArrayField(TEXT("variables"), Variables))
	{
		for (const TSharedPtr<FJsonValue>& Item : *Variables)
		{
			const TSharedPtr<FJsonObject> Variable = Item->AsObject();
			const FString Name = Variable->GetStringField(TEXT("name"));
			// The game's level Blueprints hold whole numbers only.
			if (Variable->GetStringField(TEXT("type")) != TEXT("Int64Property"))
			{
				Report.Problem(FString::Printf(TEXT("variable %s: a %s is not handled"), *Name, *Variable->GetStringField(TEXT("type"))));
				continue;
			}
			FEdGraphPinType Type;
			Type.PinCategory = UEdGraphSchema_K2::PC_Int64;
			if (FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, FName(*Name)) == INDEX_NONE &&
				!FBlueprintEditorUtils::AddMemberVariable(Blueprint, FName(*Name), Type))
			{
				Report.Problem(FString::Printf(TEXT("variable %s was not made"), *Name));
				continue;
			}
			const int32 At = FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, FName(*Name));
			const uint64 Flags = FCString::Strtoui64(*Variable->GetStringField(TEXT("flags")), nullptr, 16);
			Blueprint->NewVariables[At].PropertyFlags = Flags & (CPF_Edit | CPF_BlueprintVisible | CPF_BlueprintReadOnly | CPF_DisableEditOnInstance |
				CPF_DisableEditOnTemplate | CPF_Transient | CPF_SaveGame | CPF_Interp | CPF_ExposeOnSpawn);
			Report.Count(TEXT("variables"));
		}
	}
	const TArray<TSharedPtr<FJsonValue>>* Events = nullptr;
	if (Held->TryGetArrayField(TEXT("events"), Events))
	{
		UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
		for (const TSharedPtr<FJsonValue>& Item : *Events)
		{
			UFunction* Signature = ALevelScriptActor::StaticClass()->FindFunctionByName(FName(*Item->AsString()));
			if (!Graph || !Signature)
			{
				Report.Problem(FString::Printf(TEXT("event %s is not one of a level Blueprint"), *Item->AsString()));
				continue;
			}
			// A new level Blueprint has greyed-out BeginPlay and Tick nodes: the event's node is made real.
			UK2Node_Event* Node = nullptr;
			for (UEdGraphNode* Each : Graph->Nodes)
			{
				UK2Node_Event* Event = Cast<UK2Node_Event>(Each);
				if (Event && Event->EventReference.GetMemberName() == Signature->GetFName())
				{
					Node = Event;
				}
			}
			if (!Node)
			{
				Node = NewObject<UK2Node_Event>(Graph, NAME_None, RF_Transactional);
				Node->EventReference.SetFromField<UFunction>(Signature, false);
				Node->bOverrideFunction = true;
				Graph->AddNode(Node, false, false);
				Node->AllocateDefaultPins();
			}
			Node->MakeAutomaticallyPlacedGhostNode();
			Node->bCommentBubbleVisible = false;
			Node->SetEnabledState(ENodeEnabledState::Enabled, true);
			Node->NodeComment.Reset();
			Report.Count(TEXT("events"));
		}
	}
	P4Ids::Name(Blueprint);
	P4Reimport::WithCompilerSavePolicy([&]() { FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint); });
	if (!Compile(Blueprint, Report))
	{
		Report.bStopped = true;
		return Report.Text();
	}
	// The values, on the compiled class's defaults.
	UObject* Defaults = Blueprint->GeneratedClass->GetDefaultObject();
	for (const TSharedPtr<FJsonValue>& Item : Variables ? *Variables : TArray<TSharedPtr<FJsonValue>>())
	{
		const TSharedPtr<FJsonObject> Variable = Item->AsObject();
		double Number = 0;
		FInt64Property* Property = FindFProperty<FInt64Property>(Blueprint->GeneratedClass, FName(*Variable->GetStringField(TEXT("name"))));
		if (Property && Variable->TryGetNumberField(TEXT("value"), Number))
		{
			Property->SetPropertyValue_InContainer(Defaults, (int64)Number);
			Report.Count(TEXT("values"));
		}
	}
	Blueprint->MarkPackageDirty();
	return Report.Text();
}

FString UP4BlueprintImport::Sequence(const FString& PlanFile)
{
	FReport Report;
	const TSharedPtr<FJsonObject> Plan = LoadPlan(PlanFile, Report);
	if (!Plan.IsValid())
	{
		return Report.Text();
	}
	const TSharedPtr<FJsonObject>* Record = nullptr;
	if (!Plan->TryGetObjectField(TEXT("sequence"), Record))
	{
		Report.Stop(TEXT("the plan holds no sequence"));
		return Report.Text();
	}
	const FString PackageName = Plan->GetStringField(TEXT("package"));
	const FString AssetName = (*Record)->GetStringField(TEXT("name"));
	ULevelSequence* Made = LoadObject<ULevelSequence>(nullptr, *(PackageName + TEXT(".") + AssetName), nullptr, LOAD_NoWarn | LOAD_Quiet);
	FString ReimportWhy;
	if (Made && TaggedStandIn(Made) && !P4Reimport::IsUntouchedStandIn(Made, ReimportWhy))
	{
		Report.Stop(ReimportWhy);
		return Report.Text();
	}
	if (Made && !TaggedStandIn(Made) && !P4Reimport::ReuseSequence(Made, ReimportWhy))
	{
		Report.Stop(ReimportWhy);
		return Report.Text();
	}
	if (!Made)
	{
		// A sequence another one named before this one was made is there as a stand-in: it is filled.
		Made = NewObject<ULevelSequence>(CreatePackage(*PackageName), FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(Made);
	}
	P4Reimport::MarkImported(Made);
	ResetSubobjects();
	for (const TSharedPtr<FJsonValue>& Item : Plan->GetArrayField(TEXT("assets")))
	{
		StandIn(Item->AsObject()->GetStringField(TEXT("path")), Item->AsObject()->GetStringField(TEXT("cls")), Report);
	}
	// Every object of the sequence under the same outer with the same name as in the plan, so a
	// value that names one by path finds it. Then the sequence's own values.
	Subobjects(Made, *Record, TEXT("sequence"), Report);
	Apply(Made, (*Record)->GetArrayField(TEXT("values")), TEXT("sequence"), Report);
	if (!Made->GetMovieScene())
	{
		Report.Problem(TEXT("the sequence has no movie scene"));
	}
	Report.Count(TEXT("sequences"));
	if (Plan->GetBoolField(TEXT("director")))
	{
		UClass* Parent = LoadObject<UClass>(nullptr, *Plan->GetStringField(TEXT("parent")), nullptr, LOAD_NoWarn | LOAD_Quiet);
		UBlueprint* Blueprint = Cast<UBlueprint>(Made->GetDirectorBlueprint());
		if (Blueprint && !P4Reimport::ReuseBlueprint(Blueprint, Parent, ReimportWhy))
		{
			Report.Stop(ReimportWhy);
			return Report.Text();
		}
		if (!Blueprint && Parent)
		{
			P4Reimport::WithCompilerSavePolicy([&]() {
				Blueprint = FKismetEditorUtilities::CreateBlueprint(Parent, Made, FName(TEXT("SequenceDirector")), BPTYPE_Normal,
					UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), FName(TEXT("P4BlueprintImport")));
			});
		}
		if (!Blueprint)
		{
			Report.Stop(TEXT("the editor made no Blueprint for the sequence"));
			return Report.Text();
		}
		Blueprint->ClearFlags(RF_Standalone);
		Made->SetDirectorBlueprint(Blueprint);
		DeclareVariables(Blueprint, Plan, Report);
		P4Ids::Name(Blueprint);
		P4Reimport::WithCompilerSavePolicy([&]() { FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint); });
		if (!Compile(Blueprint, Report))
		{
			Report.bStopped = true;
		}
		Report.Count(TEXT("sequence Blueprints"));
	}
	if (!Report.bStopped)
	{
		Made->GetOutermost()->GetMetaData()->RemoveValue(Made, StandInTag);
	}
	Made->MarkPackageDirty();
	return Report.Text();
}

FString UP4BlueprintImport::Widgets(const FString& PlanFile)
{
	FReport Report;
	const TSharedPtr<FJsonObject> Plan = LoadPlan(PlanFile, Report);
	if (!Plan.IsValid())
	{
		return Report.Text();
	}
	UWidgetBlueprint* Blueprint = Cast<UWidgetBlueprint>(Find(Plan));
	const TSharedPtr<FJsonObject>* Widget = nullptr;
	if (!Blueprint || !Plan->TryGetObjectField(TEXT("widget"), Widget))
	{
		Report.Stop(TEXT("the widget Blueprint or its plan is not there"));
		return Report.Text();
	}
	FString MemberError;
	if(!P4WidgetMembers::Preflight(Plan,nullptr,MemberError))
	{Report.Stop(MemberError);return Report.Text();}
	ResetSubobjects();
	for (const TSharedPtr<FJsonValue>& Item : Plan->GetArrayField(TEXT("assets")))
	{
		StandIn(Item->AsObject()->GetStringField(TEXT("path")), Item->AsObject()->GetStringField(TEXT("cls")), Report);
	}
	if (!Blueprint->WidgetTree)
	{
		Blueprint->WidgetTree = NewObject<UWidgetTree>(Blueprint, TEXT("WidgetTree"), RF_Transactional);
	}
	// In the editor the tree and the animations are objects of the Blueprint. Each is made under the
	// same outer with the same name as in the plan, so a value that names one by path finds it.
	Subobjects(Blueprint, *Widget, TEXT("widgets"), Report);
	Blueprint->Animations.Reset();
	for (const TSharedPtr<FJsonValue>& Item : (*Widget)->GetArrayField(TEXT("animations")))
	{
		UWidgetAnimation* Animation = Cast<UWidgetAnimation>(StaticFindObjectFast(UWidgetAnimation::StaticClass(), Blueprint, FName(*Item->AsString())));
		if (!Animation)
		{
			Report.Problem(FString::Printf(TEXT("animation %s was not made"), *Item->AsString()));
			continue;
		}
		Blueprint->Animations.Add(Animation);
		Report.Count(TEXT("widget animations"));
	}
	TArray<UWidget*> All;
	Blueprint->WidgetTree->GetAllWidgets(All);
	if(!P4WidgetMembers::ApplyVariableIntent(Blueprint,Plan,MemberError))
	{Report.Stop(MemberError);return Report.Text();}
	Report.Count(TEXT("widgets in trees"), All.Num());
	if (!Blueprint->WidgetTree->RootWidget && (*Widget)->GetArrayField(TEXT("subobjects")).Num() > 1)
	{
		Report.Problem(TEXT("the widget tree has no root"));
	}
	P4Reimport::WithCompilerSavePolicy([&]() { FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint); });
	FCompilerResultsLog Log;
	Log.bSilentMode = true;
	Log.bAnnotateMentionedNodes = false;
	P4Reimport::WithCompilerSavePolicy([&]() {
		FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection | EBlueprintCompileOptions::SkipSave, &Log);
	});
	P4Ids::NameBindings(Blueprint);
	int32 Shown = 0;
	for (const TSharedRef<FTokenizedMessage>& Message : Log.Messages)
	{
		if (Message->GetSeverity() == EMessageSeverity::Error && Shown++ < 12)
		{
			Report.Problem(FString::Printf(TEXT("compiler: %s"), *Message->ToText().ToString()));
		}
	}
	Report.Count(TEXT("compiler errors"), Log.NumErrors);
	Report.Count(TEXT("compiler warnings"), Log.NumWarnings);
	if (Blueprint->Status == BS_Error)
	{
		Report.bStopped = true;
	}
	if(!Report.bStopped&&!P4WidgetMembers::VerifyCompiled(Blueprint,Plan,false,MemberError))Report.Stop(MemberError);
	Blueprint->MarkPackageDirty();
	return Report.Text();
}

FString UP4BlueprintImport::WidgetSignatures(const FString& PlanFile)
{
	FReport Report;
	const TSharedPtr<FJsonObject> Plan = LoadPlan(PlanFile, Report);
	if (!Plan.IsValid())
	{
		return Report.Text();
	}
	UBlueprint* Blueprint = Find(Plan);
	const TSharedPtr<FJsonObject>* Widget = nullptr;
	if (!Blueprint || !(Plan->TryGetObjectField(TEXT("widget"), Widget) || Plan->TryGetObjectField(TEXT("sequence"), Widget)))
	{
		return Report.Text();
	}
	UObject* Holder = Blueprint->IsA<UWidgetBlueprint>() ? static_cast<UObject*>(Blueprint) : Blueprint->GetOuter();
	ResetSubobjects();
	const TArray<TSharedPtr<FJsonValue>>* Own = nullptr;
	if (!Blueprint->IsA<UWidgetBlueprint>() && (*Widget)->TryGetArrayField(TEXT("values"), Own))
	{
		TArray<TSharedPtr<FJsonValue>> Signed;
		for (const TSharedPtr<FJsonValue>& Value : *Own)
		{
			if (Value->AsObject()->GetStringField(TEXT("name")) == TEXT("Signature"))
			{
				Signed.Add(Value);
			}
		}
		Apply(Holder, Signed, TEXT("signature"), Report);
	}
	for (const TSharedPtr<FJsonValue>& Item : (*Widget)->GetArrayField(TEXT("subobjects")))
	{
		const TSharedPtr<FJsonObject> Sub = Item->AsObject();
		TArray<TSharedPtr<FJsonValue>> Signed;
		for (const TSharedPtr<FJsonValue>& Value : Sub->GetArrayField(TEXT("values")))
		{
			const FString Name = Value->AsObject()->GetStringField(TEXT("name"));
			if (Name == TEXT("Signature") || Name == TEXT("EvaluationFieldGuid"))
			{
				Signed.Add(Value);
			}
		}
		UObject* Found = Signed.Num() ? Holder : nullptr;
		for (const TSharedPtr<FJsonValue>& Name : Sub->GetArrayField(TEXT("path")))
		{
			Found = Found ? StaticFindObjectFast(nullptr, Found, FName(*Name->AsString())) : nullptr;
		}
		if (Found)
		{
			Apply(Found, Signed, TEXT("signature"), Report);
		}
	}
	Blueprint->MarkPackageDirty();
	return Report.Text();
}

FString UP4BlueprintImport::WidgetEvents(const FString& PlanFile)
{
	FReport Report;
	const TSharedPtr<FJsonObject> Plan = LoadPlan(PlanFile, Report);
	if (!Plan.IsValid())
	{
		return Report.Text();
	}
	UBlueprint* Blueprint = Find(Plan);
	const TSharedPtr<FJsonObject>* Widget = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Events = nullptr;
	if (!Blueprint || !(Plan->TryGetObjectField(TEXT("widget"), Widget) || Plan->TryGetObjectField(TEXT("sequence"), Widget)) ||
		!(*Widget)->TryGetArrayField(TEXT("events"), Events))
	{
		return Report.Text();
	}
	// The sections hang under the Blueprint (a widget's animations) or under the sequence the Blueprint is in.
	UObject* Holder = Blueprint->IsA<UWidgetBlueprint>() ? static_cast<UObject*>(Blueprint) : Blueprint->GetOuter();
	for (const TSharedPtr<FJsonValue>& Item : *Events)
	{
		const TSharedPtr<FJsonObject> Record = Item->AsObject();
		UObject* Found = Holder;
		for (const TSharedPtr<FJsonValue>& Name : Record->GetArrayField(TEXT("path")))
		{
			Found = Found ? StaticFindObjectFast(nullptr, Found, FName(*Name->AsString())) : nullptr;
		}
		UMovieSceneEventSectionBase* Section = Cast<UMovieSceneEventSectionBase>(Found);
		const int32 Key = (int32)Record->GetNumberField(TEXT("key"));
		FString Endpoint;
		if (!Section || !Section->GetAllEntryPoints().IsValidIndex(Key) || !Record->TryGetStringField(TEXT("endpoint"), Endpoint))
		{
			Report.Problem(TEXT("an animation event names a section, a key or an endpoint that is not there"));
			continue;
		}
		// The endpoint: a custom event of that name, or the entry of a function of that name.
		UK2Node* Node = nullptr;
		TArray<UK2Node_CustomEvent*> Custom;
		FBlueprintEditorUtils::GetAllNodesOfClass(Blueprint, Custom);
		for (UK2Node_CustomEvent* Each : Custom)
		{
			if (Each->CustomFunctionName == FName(*Endpoint))
			{
				Node = Each;
			}
		}
		for (UEdGraph* Graph : Blueprint->FunctionGraphs)
		{
			TArray<UK2Node_FunctionEntry*> Entries;
			if (!Node && Graph && Graph->GetFName() == FName(*Endpoint))
			{
				Graph->GetNodesOfClass(Entries);
				Node = Entries.Num() ? Entries[0] : nullptr;
			}
		}
		if (!Node)
		{
			Report.Problem(FString::Printf(TEXT("animation event endpoint %s was not found"), *Endpoint));
			continue;
		}
		FMovieSceneEvent* Event = &Section->GetAllEntryPoints()[Key];
		FString Bound;
		UEdGraphPin* Pin = Record->TryGetStringField(TEXT("bound"), Bound) ? Node->FindPin(FName(*Bound), EGPD_Output) : nullptr;
		FMovieSceneEventUtils::SetEndpoint(Event, Section, Node, Pin);
		Event->PayloadVariables.Reset();
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Record->GetObjectField(TEXT("payload"))->Values)
		{
			FMovieSceneEventPayloadVariable Value;
			Value.Value = Pair.Value->AsString();
			Event->PayloadVariables.Add(FName(*Pair.Key), Value);
		}
		FMovieSceneEventUtils::BindEventSectionToBlueprint(Section, Blueprint);
		Report.Count(TEXT("animation events tied to endpoints"));
	}
	// Every event section of a sequence is known to the sequence's Blueprint, also one whose events
	// call nothing and one of a track the movie scene no longer lists: the Blueprint keeps them.
	const TArray<TSharedPtr<FJsonValue>>* Sections = nullptr;
	if ((*Widget)->TryGetArrayField(TEXT("event_sections"), Sections))
	{
		for (const TSharedPtr<FJsonValue>& Item : *Sections)
		{
			UObject* Found = Holder;
			for (const TSharedPtr<FJsonValue>& Name : Item->AsArray())
			{
				Found = Found ? StaticFindObjectFast(nullptr, Found, FName(*Name->AsString())) : nullptr;
			}
			if (UMovieSceneEventSectionBase* Section = Cast<UMovieSceneEventSectionBase>(Found))
			{
				FMovieSceneEventUtils::BindEventSectionToBlueprint(Section, Blueprint);
				Report.Count(TEXT("event sections known to the Blueprint"));
			}
		}
	}
	Blueprint->MarkPackageDirty();
	return Report.Text();
}

FString UP4BlueprintImport::Values(const FString& PlanFile)
{
	FReport Report;
	const TSharedPtr<FJsonObject> Plan = LoadPlan(PlanFile, Report);
	if (!Plan.IsValid())
	{
		return Report.Text();
	}
	UBlueprint* Blueprint = Find(Plan);
	if (!Blueprint || !Blueprint->GeneratedClass)
	{
		Report.Stop(TEXT("the Blueprint is not in the project"));
		return Report.Text();
	}
	TSet<FString> AllowedReferences;
	const TArray<TSharedPtr<FJsonValue>>* ReferenceOnly = nullptr;
	if (Plan->HasField(TEXT("reference_only")) && !Plan->TryGetArrayField(TEXT("reference_only"), ReferenceOnly))
	{
		Report.Stop(TEXT("reference_only is not an array"));
		return Report.Text();
	}
	if (Plan->TryGetArrayField(TEXT("reference_only"), ReferenceOnly))
	{
		for (const TSharedPtr<FJsonValue>& Item : *ReferenceOnly)
		{
			FString Path, Package, Object;
			if (!Item.IsValid() || Item->Type != EJson::String || !Item->TryGetString(Path) || !Path.Split(TEXT("."), &Package, &Object)
				|| !Package.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(Package)
				|| Object != FPackageName::GetShortName(Package))
			{
				Report.Stop(TEXT("reference_only contains an invalid asset identity"));
				return Report.Text();
			}
			AllowedReferences.Add(Path);
		}
	}
	TGuardValue<const TSet<FString>*> ReferenceScope(ReferenceOnlyPaths, &AllowedReferences);
	// Declaration compiles children before their parents receive source defaults.
	// Refresh only a Blueprint created by this run, from the completed parent
	// lineage, before applying its own sparse component/default deltas. The engine
	// handles object/subobject references; saved/reimported creator defaults remain.
	P4Reimport::ResetReimportSourceDefaults(Blueprint);
	if (P4Reimport::IsFreshBlueprint(Blueprint))
	{
		FBlueprintEditorUtils::PropagateParentBlueprintDefaults(Blueprint->GeneratedClass);
	}
	ResetSubobjects();
	for (const TSharedPtr<FJsonValue>& Item : Plan->GetArrayField(TEXT("assets")))
	{
		StandIn(Item->AsObject()->GetStringField(TEXT("path")), Item->AsObject()->GetStringField(TEXT("cls")), Report);
	}
	USimpleConstructionScript* Script = Blueprint->SimpleConstructionScript;
	const TArray<TSharedPtr<FJsonValue>>* ComponentTemplates = nullptr;
	if (Plan->TryGetArrayField(TEXT("component_templates"), ComponentTemplates))
	{
		for (const TSharedPtr<FJsonValue>& Item : *ComponentTemplates)
		{
			const TSharedPtr<FJsonObject> Record = Item->AsObject();
			const FString Name = Record->GetStringField(TEXT("name"));
			UClass* Class = LoadObject<UClass>(nullptr, *Record->GetStringField(TEXT("cls")), nullptr, LOAD_NoWarn | LOAD_Quiet);
			const EObjectFlags AllowedFlags = RF_Public | RF_Transactional | RF_ArchetypeObject | RF_DefaultSubObject | RF_InheritableComponentTemplate;
			const uint64 SourceFlags = P4Plan::Flags(Record, TEXT("flags"));
			if ((SourceFlags & ~uint64(AllowedFlags)) != 0)
			{
				Report.Problem(TEXT("add-component template has unsupported source object flags: ") + Name);
				continue;
			}
			FString ArchetypePath;
			UActorComponent* Archetype = Record->TryGetStringField(TEXT("archetype"), ArchetypePath)
				? LoadObject<UActorComponent>(nullptr, *ArchetypePath, nullptr, LOAD_NoWarn | LOAD_Quiet) : nullptr;
			if (!Class || !Class->IsChildOf(UActorComponent::StaticClass()) || !Archetype || Archetype->GetClass() != Class)
			{
				Report.Problem(TEXT("add-component template has no matching class/archetype: ") + Name);
				continue;
			}
			UObject* Existing = FindObjectFast<UObject>(Blueprint->GeneratedClass, FName(*Name));
			UActorComponent* Template = Cast<UActorComponent>(Existing);
			if (Existing && (!Template || Template->GetClass() != Class))
			{
				Report.Problem(TEXT("add-component template identity already has another class: ") + Name);
				continue;
			}
			if (!Template) Template = NewObject<UActorComponent>(Blueprint->GeneratedClass, Class, FName(*Name),
				EObjectFlags(SourceFlags), Archetype);
			if (!Template)
			{
				Report.Problem(TEXT("add-component template was not created: ") + Name);
				continue;
			}
			// KismetCompiler preserves this array's subobjects during class cleanup
			// and copies it to the generated class. They are not SCS components.
			Blueprint->ComponentTemplates.AddUnique(Template);
			Template->SetFlags(EObjectFlags(SourceFlags));
			Subobjects(Template, Record, TEXT("add-component template ") + Name, Report);
			Apply(Template, Record->GetArrayField(TEXT("values")), TEXT("add-component template ") + Name, Report);
			Report.Count(TEXT("add-component templates"));
		}
	}
	for (const TSharedPtr<FJsonValue>& Item : Plan->GetArrayField(TEXT("components")))
	{
		const TSharedPtr<FJsonObject> Record = Item->AsObject();
		const FString Name = Record->GetStringField(TEXT("variable"));
		const TArray<TSharedPtr<FJsonValue>>* Stored = nullptr;
		if (!Record->TryGetArrayField(TEXT("values"), Stored))
		{
			continue;
		}
		USCS_Node* Node = Script ? Script->FindSCSNode(FName(*Name)) : nullptr;
		if (!Node && Script && Script->GetDefaultSceneRootNode() && Script->GetDefaultSceneRootNode()->GetVariableName() == FName(*Name))
		{
			Node = Script->GetDefaultSceneRootNode();
		}
		if (!Node || !Node->ComponentTemplate)
		{
			if (Stored->Num())
			{
				Report.Problem(FString::Printf(TEXT("component %s: no template to hold its %d values"), *Name, Stored->Num()));
			}
			continue;
		}
		Node->ComponentTemplate->Modify();
		Subobjects(Node->ComponentTemplate, Record, FString::Printf(TEXT("component %s"), *Name), Report);
		Apply(Node->ComponentTemplate, *Stored, FString::Printf(TEXT("component %s"), *Name), Report);
	}
	const TSharedPtr<FJsonObject>* SpareRecord=nullptr;
	if(Plan->TryGetObjectField(TEXT("unused_default_scene_root"),SpareRecord))
	{
		USCS_Node* Node=Script?Script->GetDefaultSceneRootNode():nullptr;FString Variable,Class;
		const TArray<TSharedPtr<FJsonValue>>* Stored=nullptr;
		if(!Node||!Node->ComponentTemplate||!(*SpareRecord)->TryGetStringField(TEXT("variable"),Variable)||
			!(*SpareRecord)->TryGetStringField(TEXT("cls"),Class)||Node->GetVariableName()!=FName(*Variable)||
			Node->ComponentTemplate->GetClass()->GetPathName()!=Class||!(*SpareRecord)->TryGetArrayField(TEXT("values"),Stored))
			Report.Problem(TEXT("the unused source scene root has no matching native template"));
		else
		{
			Node->ComponentTemplate->Modify();
			Subobjects(Node->ComponentTemplate,*SpareRecord,TEXT("unused source scene root"),Report);
			Apply(Node->ComponentTemplate,*Stored,TEXT("unused source scene root"),Report);
			ApplyNodeMetadata(Node,*SpareRecord,Report);
			Report.Count(TEXT("unused source scene-root templates"));
		}
	}
	for (const TSharedPtr<FJsonValue>& Item : Plan->GetArrayField(TEXT("overrides")))
	{
		const TSharedPtr<FJsonObject> Record = Item->AsObject();
		const FString Name = Record->GetStringField(TEXT("variable"));
		const TArray<TSharedPtr<FJsonValue>>* Stored = nullptr;
		if (!Record->TryGetArrayField(TEXT("values"), Stored))
		{
			continue;
		}
		USCS_Node* Node = InheritedNode(Blueprint, FName(*Name));
		UInheritableComponentHandler* Handler = Node ? Blueprint->GetInheritableComponentHandler(true) : nullptr;
		if (!Handler)
		{
			Report.Problem(FString::Printf(TEXT("inherited component %s was not found in the parent Blueprints"), *Name));
			continue;
		}
		const FComponentKey Key(Node);
		if (P4BlueprintSourceTemplateSaysNothing(Record))
		{
			Report.Count(TEXT("inherited component templates that say nothing, left to the parent's"));
			continue;
		}
		UActorComponent* Template = Handler->GetOverridenComponentTemplate(Key);
		if (!Template)
		{
			Template = Handler->CreateOverridenComponentTemplate(Key);
		}
		if (!Template)
		{
			Report.Problem(FString::Printf(TEXT("inherited component %s: the editor made no template"), *Name));
			continue;
		}
		Subobjects(Template, Record, FString::Printf(TEXT("inherited component %s"), *Name), Report);
		Apply(Template, *Stored, FString::Printf(TEXT("inherited component %s"), *Name), Report);
		DropUnnamedCopies(Template, Record, Report);
		Report.Count(TEXT("inherited component templates"));
	}
	const TSharedPtr<FJsonObject> Defaults = Plan->GetObjectField(TEXT("class_defaults"));
	UObject* DefaultObject = Blueprint->GeneratedClass->GetDefaultObject();
	DefaultObject->Modify();
	Subobjects(DefaultObject, Defaults, TEXT("class defaults"), Report);
	Apply(DefaultObject, Defaults->GetArrayField(TEXT("values")), TEXT("class defaults"), Report);
	P4RememberBlueprintSourceTemplates(Blueprint, Plan);
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	if (!Compile(Blueprint, Report))
	{
		Report.bStopped = true;
	}
	else
	{
		// Original inherited templates the compile did not keep are made once, here, as saved data.
		P4RestoreBlueprintSourceTemplates(Blueprint, Report);
	}
	FString StockOwnerError;
	if(UWidgetBlueprint* WidgetBlueprint=Cast<UWidgetBlueprint>(Blueprint))
		if(!P4WidgetMembers::VerifyCompiled(WidgetBlueprint,Plan,true,StockOwnerError))Report.Stop(StockOwnerError);
	if (!P4OwnStockEngineFields(Blueprint->GetOutermost(), StockOwnerError)) Report.Problem(StockOwnerError);
	Blueprint->MarkPackageDirty();
	return Report.Text();
}

FString UP4BlueprintImport::Ids(const FString& BlueprintPath)
{
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	FString Out;
	if (Blueprint)
	{
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(P4Ids::Dump(Blueprint), Writer);
	}
	return Out;
}

FString UP4BlueprintImport::UndoCheck(const FString& BlueprintPath)
{
	FReport Report;
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Blueprint || !GEditor || !GEditor->Trans)
	{
		Report.Stop(TEXT("no Blueprint, or an editor without an undo record"));
		return Report.Text();
	}
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	for (UEdGraph* Graph : Graphs)
	{
		for (UEdGraphNode* Node : Graph ? TArray<UEdGraphNode*>(Graph->Nodes) : TArray<UEdGraphNode*>())
		{
			if (!Node)
			{
				continue;
			}
			// What a user's edit does: the node is moved and the wires of one of its pins are cut, as one
			// step. Then the step is undone.
			UEdGraphPin* Wired = nullptr;
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin && Pin->LinkedTo.Num() && !Wired)
				{
					Wired = Pin;
				}
			}
			const int32 Before = Node->NodePosX;
			const FGuid PinId = Wired ? Wired->PinId : FGuid();
			const int32 Wires = Wired ? Wired->LinkedTo.Num() : 0;
			GEditor->BeginTransaction(FText::FromString(TEXT("P4 undo check")));
			Node->Modify();
			Node->NodePosX = Before + 1000;
			if (Wired)
			{
				Wired->BreakAllPinLinks();
			}
			GEditor->EndTransaction();
			const bool bUndone = GEditor->UndoTransaction();
			UEdGraphPin* Again = Wired ? Node->FindPinById(PinId) : nullptr;
			Report.Count(TEXT("nodes moved and cut, then undone"));
			if (bUndone && Node->NodePosX == Before && (!Wired || (Again && Again->LinkedTo.Num() == Wires)))
			{
				Report.Count(TEXT("nodes the undo put back"));
			}
			else if (Report.Problems.Num() < 12)
			{
				Report.Problem(FString::Printf(TEXT("undo did not put back %s (moved back: %s, wires %d of %d)"), *Node->GetPathName(Blueprint),
					Node->NodePosX == Before ? TEXT("yes") : TEXT("no"), Again ? Again->LinkedTo.Num() : 0, Wires));
			}
		}
	}
	GEditor->Trans->Reset(FText::FromString(TEXT("P4 undo check")));
	return Report.Text();
}

bool UP4BlueprintImport::DumpDeclarations(const FString& File)
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	for (TObjectIterator<UStruct> It; It; ++It)
	{
		UStruct* Struct = *It;
		if (!Struct->IsA<UClass>() && !Struct->IsA<UScriptStruct>())
		{
			continue;
		}
		TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Properties, Functions, Deprecated;
		for (TFieldIterator<FProperty> Property(Struct, EFieldIteratorFlags::ExcludeSuper); Property; ++Property)
		{
			Properties.Add(MakeShared<FJsonValueString>(Property->GetName()));
			if (Property->HasAnyPropertyFlags(CPF_Deprecated))
			{
				Deprecated.Add(MakeShared<FJsonValueString>(Property->GetName()));   // read from old files, never saved
			}
		}
		for (TFieldIterator<UFunction> Function(Struct, EFieldIteratorFlags::ExcludeSuper); Function; ++Function)
		{
			Functions.Add(MakeShared<FJsonValueString>(Function->GetName()));
		}
		Record->SetArrayField(TEXT("p"), Properties);
		Record->SetArrayField(TEXT("f"), Functions);
		if (Deprecated.Num())
		{
			Record->SetArrayField(TEXT("d"), Deprecated);
		}
		Root->SetObjectField(Struct->GetPathName(), Record);
	}
	FString Out;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
	return FJsonSerializer::Serialize(Root, Writer) && FFileHelper::SaveStringToFile(Out, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

TArray<FString> UP4BlueprintImport::TakeStandIns()
{
	TArray<FString> Out = MoveTemp(StandInsMade);
	StandInsMade.Reset();
	return Out;
}

bool UP4BlueprintImport::IsStandIn(UObject* Asset)
{
	return TaggedStandIn(Asset);
}

FString UP4BlueprintImport::StandInAsset(const FString& AssetPath, const FString& ClassPath)
{
	FReport Report;
	StandIn(AssetPath, ClassPath, Report);
	return Report.Text();
}

FString UP4BlueprintImport::CaptureStandIn(const FString& PackageName)
{
	FReport Report;
	FString Why;
	if (!P4Reimport::FinishStandInPackage(PackageName, Why)) { Report.Stop(Why); }
	return Report.Text();
}

FString UP4BlueprintImport::SaveStandIn(const FString& PackageName)
{
	FReport Report;
	FString Why, File;
	UPackage* Package = FindPackage(nullptr, *PackageName);
	if (!IsInGameThread() || !GEditor || P4Reimport::IsRunActive() || !PackageName.StartsWith(TEXT("/Game/")) || !Package
		|| !FPackageName::TryConvertLongPackageNameToFilename(PackageName, File, FPackageName::GetAssetPackageExtension()))
	{
		Report.Stop(TEXT("stand-in persistence requires its loaded /Game package on the editor thread"));
		return Report.Text();
	}
	if (!P4Reimport::PrepareStandInSave(PackageName, Why)) { Report.Stop(Why); return Report.Text(); }
	P4ImportPublication::FSaveScope ImportSave(Package);
	if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true)
		|| !UPackage::SavePackage(Package, nullptr, RF_Public | RF_Standalone, *File, GError, nullptr, false, true, SAVE_NoError))
	{
		P4Reimport::CancelStandInSave(PackageName);
		Report.Stop(TEXT("the guarded stand-in save failed; its queued save and creation guard are retained"));
		return Report.Text();
	}
	if (!P4Reimport::FinishStandInPackage(PackageName, Why)) { Report.Stop(Why); return Report.Text(); }
	StandInsMade.Remove(PackageName);
	return Report.Text();
}

FString UP4BlueprintImport::StandInStatus(UObject* Asset)
{
	FReport Report;
	FString Why;
	if (!P4Reimport::IsUntouchedStandIn(Asset, Why)) { Report.Stop(Why); }
	return Report.Text();
}

TArray<FString> UP4BlueprintImport::StandIns()
{
	TArray<FAssetData> Assets;
	FAssetRegistryModule::GetRegistry().GetAllAssets(Assets);
	TArray<FString> Out;
	for (const FAssetData& Asset : Assets)
	{
		FString Value;
		if (Asset.GetTagValue(StandInTag, Value) && Value == TEXT("1"))
		{
			Out.AddUnique(Asset.PackageName.ToString());
		}
	}
	Out.Sort();
	return Out;
}
