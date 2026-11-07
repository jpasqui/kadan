// Editor helpers for the stub classes. Experimental (map kit step 2).

#include "P4StubTools.h"
#include "AssetRegistryModule.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/InheritableComponentHandler.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, P4StubsEditor);

namespace
{
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
				USCS_Node* Root = Generated->SimpleConstructionScript->GetDefaultSceneRootNode();
				if (Root && Root->GetVariableName() == VariableName)
				{
					return Root;
				}
			}
		}
		return nullptr;
	}
}

UActorComponent* UP4StubTools::AddInstanceComponent(AActor* Actor, TSubclassOf<UActorComponent> ComponentClass, FName Name, USceneComponent* Parent)
{
	if (!Actor || !*ComponentClass)
	{
		return nullptr;
	}
	Actor->Modify();
	UActorComponent* Component = NewObject<UActorComponent>(Actor, *ComponentClass, Name, RF_Transactional);
	if (USceneComponent* Scene = Cast<USceneComponent>(Component))
	{
		USceneComponent* To = Parent ? Parent : Actor->GetRootComponent();
		if (To)
		{
			Scene->SetupAttachment(To);
		}
		else
		{
			Actor->SetRootComponent(Scene);
		}
	}
	Actor->AddInstanceComponent(Component);
	Component->OnComponentCreated();
	Component->RegisterComponent();
	Actor->MarkPackageDirty();
	return Component;
}

UBlueprint* UP4StubTools::CreateActorBlueprint(const FString& PackageName, UClass* ParentClass)
{
	if (!ParentClass || !FKismetEditorUtilities::CanCreateBlueprintOfClass(ParentClass))
	{
		return nullptr;
	}
	UPackage* Package = CreatePackage(*PackageName);
	const FName Name(*FPackageName::GetLongPackageAssetName(PackageName));
	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(ParentClass, Package, Name, BPTYPE_Normal,
		UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), FName(TEXT("P4Stubs")));
	if (Blueprint)
	{
		FAssetRegistryModule::AssetCreated(Blueprint);
		Package->MarkPackageDirty();
	}
	return Blueprint;
}

UActorComponent* UP4StubTools::AddBlueprintComponent(UBlueprint* Blueprint, TSubclassOf<UActorComponent> ComponentClass, FName VariableName, FName ParentVariableName)
{
	if (!Blueprint || !Blueprint->SimpleConstructionScript || !*ComponentClass)
	{
		return nullptr;
	}
	USimpleConstructionScript* Script = Blueprint->SimpleConstructionScript;
	Script->ValidateSceneRootNodes();
	USCS_Node* Parent = nullptr;
	bool bInherited = false;
	if (!ParentVariableName.IsNone())
	{
		Parent = Script->FindSCSNode(ParentVariableName);
		USCS_Node* Root = Script->GetDefaultSceneRootNode();
		if (!Parent && Root && Root->GetVariableName() == ParentVariableName && Script->GetRootNodes().Contains(Root))
		{
			Parent = Root;
		}
		if (!Parent)
		{
			Parent = InheritedNode(Blueprint, ParentVariableName);
			bInherited = Parent != nullptr;
		}
		if (!Parent)
		{
			// A component the native parent class makes itself (the game's EnvController has its root that way).
			USceneComponent* Native = nullptr;
			if (AActor* Defaults = Blueprint->ParentClass ? Cast<AActor>(Blueprint->ParentClass->GetDefaultObject()) : nullptr)
			{
				TInlineComponentArray<USceneComponent*> Found;
				Defaults->GetComponents(Found);
				for (USceneComponent* Component : Found)
				{
					if (Component && Component->GetFName() == ParentVariableName)
					{
						Native = Component;
						break;
					}
				}
			}
			if (!Native)
			{
				return nullptr;
			}
			USCS_Node* Under = Script->CreateNode(*ComponentClass, VariableName);
			if (!Under)
			{
				return nullptr;
			}
			Script->AddNode(Under);
			Under->SetParent(Native);
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			return Under->ComponentTemplate;
		}
	}
	USCS_Node* Node = Script->CreateNode(*ComponentClass, VariableName);
	if (!Node)
	{
		return nullptr;
	}
	if (Parent && !bInherited)
	{
		Parent->AddChildNode(Node);
	}
	else
	{
		Script->AddNode(Node);
		if (Parent)
		{
			Node->SetParent(Parent);
		}
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return Node->ComponentTemplate;
}

UActorComponent* UP4StubTools::OverrideInheritedComponent(UBlueprint* Blueprint, FName VariableName)
{
	if (!Blueprint)
	{
		return nullptr;
	}
	USCS_Node* Node = InheritedNode(Blueprint, VariableName);
	if (!Node)
	{
		return nullptr;
	}
	UInheritableComponentHandler* Handler = Blueprint->GetInheritableComponentHandler(true);
	if (!Handler)
	{
		return nullptr;
	}
	const FComponentKey Key(Node);
	UActorComponent* Template = Handler->GetOverridenComponentTemplate(Key);
	if (!Template)
	{
		Template = Handler->CreateOverridenComponentTemplate(Key);
	}
	Blueprint->MarkPackageDirty();
	return Template;
}

bool UP4StubTools::CompileBlueprint(UBlueprint* Blueprint)
{
	if (!Blueprint)
	{
		return false;
	}
	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
	return Blueprint->Status != BS_Error && Blueprint->GeneratedClass != nullptr;
}

bool UP4StubTools::SetPropertyText(UObject* Object, FName Property, const FString& Text)
{
	if (!Object)
	{
		return false;
	}
	FProperty* Found = Object->GetClass()->FindPropertyByName(Property);
	if (!Found)
	{
		return false;
	}
	Object->Modify();
	const TCHAR* Rest = Found->ImportText(*Text, Found->ContainerPtrToValuePtr<void>(Object), PPF_None, Object);
	Object->MarkPackageDirty();
	return Rest != nullptr;
}

TArray<FString> UP4StubTools::BlueprintTemplates(UBlueprint* Blueprint)
{
	TArray<FString> Out;
	if (!Blueprint || !Blueprint->GeneratedClass)
	{
		return Out;
	}
	for (UClass* Class = Blueprint->GeneratedClass; Class; Class = Class->GetSuperClass())
	{
		UBlueprintGeneratedClass* Generated = Cast<UBlueprintGeneratedClass>(Class);
		if (!Generated)
		{
			break;
		}
		TArray<UObject*> Inner;
		GetObjectsWithOuter(Generated, Inner, false);
		for (UObject* Object : Inner)
		{
			if (Object->IsA<UActorComponent>())
			{
				Out.Add(Object->GetPathName());
			}
		}
	}
	return Out;
}
