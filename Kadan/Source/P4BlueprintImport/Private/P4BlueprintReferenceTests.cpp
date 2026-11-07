// Values naming objects that retoc could not name in the converted Blueprint package.
#include "P4BlueprintImport.h"
#include "P4BlueprintPlan.h"
#include "AssetRegistryModule.h"
#include "Components/PostProcessComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/ComponentDelegateBinding.h"
#include "Dom/JsonObject.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/StaticMesh.h"
#include "HAL/FileManager.h"
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"
#include "UObject/MetaData.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS
P4BLUEPRINTIMPORT_API void P4RestorePendingStandIns(const TArray<FString>& Packages);
namespace P4ReferenceTests
{
	TSharedRef<FJsonObject> Value(const TCHAR* Name, const FString& Text)
	{
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("name"), Name); Out->SetNumberField(TEXT("index"), 0); Out->SetStringField(TEXT("text"), Text);
		return Out;
	}
	TSharedRef<FJsonObject> Component(const TCHAR* Name, const TCHAR* Class, const TArray<TSharedPtr<FJsonValue>>& Values)
	{
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("variable"), Name); Out->SetStringField(TEXT("cls"), Class);
		Out->SetArrayField(TEXT("values"), Values); Out->SetArrayField(TEXT("subobjects"), {});
		return Out;
	}
	TSharedPtr<FJsonObject> Reply(const FString& Text)
	{
		TSharedPtr<FJsonObject> Out; FJsonSerializer::Deserialize(TJsonReaderFactory<TCHAR>::Create(Text), Out); return Out;
	}
	bool Ok(const TSharedPtr<FJsonObject>& Out)
	{
		bool bOk = false; return Out && Out->TryGetBoolField(TEXT("ok"), bOk) && bOk;
	}
	struct FFixture
	{
		const FString Root = TEXT("/Game/P4BlueprintProof/References_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
		const FString File = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("P4BlueprintProof"), FPackageName::GetShortName(Root), TEXT("plan.json"));
		TArray<FString> Packages, PriorStandIns = UP4BlueprintImport::TakeStandIns();
		FString Path(const TCHAR* Name)
		{
			const FString Package = Root + TEXT("/") + Name; Packages.AddUnique(Package); return Package + TEXT(".") + Name;
		}
		bool Write(const TSharedRef<FJsonObject>& Plan)
		{
			FString Text; return FJsonSerializer::Serialize(Plan, TJsonWriterFactory<TCHAR>::Create(&Text))
				&& IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true)
				&& FFileHelper::SaveStringToFile(Text, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		}
		~FFixture()
		{
			for (const FString& Pending : UP4BlueprintImport::TakeStandIns())
				if (!Pending.StartsWith(Root + TEXT("/"))) PriorStandIns.AddUnique(Pending);
			P4RestorePendingStandIns(PriorStandIns);
			for (const FString& Package : Packages)
			{
				if (UPackage* Loaded = FindPackage(nullptr, *Package))
				{
					TArray<UObject*> Objects; GetObjectsWithOuter(Loaded, Objects, false);
					for (UObject* Object : Objects)
					{
						if (Object->IsAsset()) FAssetRegistryModule::AssetDeleted(Object);
						const FName Retired = MakeUniqueObjectName(GetTransientPackage(), Object->GetClass(), TEXT("P4RetiredReferenceProof"));
						Object->Rename(*Retired.ToString(), GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
						Object->ClearFlags(RF_Public | RF_Standalone); Object->SetFlags(RF_Transient);
					}
					Loaded->SetDirtyFlag(false);
				}
			}
			IFileManager::Get().DeleteDirectory(*FPaths::GetPath(File), false, true);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4BlueprintReferenceBinding,
	"P4Blueprints.ReferenceOnly.TypedValues", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4BlueprintReferenceBinding::RunTest(const FString& Parameters)
{
	using namespace P4ReferenceTests;
	FFixture Fixture;
	const FString BlueprintPath = Fixture.Path(TEXT("ActorProof"));
	const FString MeshPath = Fixture.Path(TEXT("AbsentMesh")), MaterialPath = Fixture.Path(TEXT("AbsentMaterial"));
	const FString BlendablePath = Fixture.Path(TEXT("AbsentBlendable"));
	TSharedRef<FJsonObject> Plan = MakeShared<FJsonObject>();
	Plan->SetStringField(TEXT("format"), TEXT("p4-blueprint-plan")); Plan->SetNumberField(TEXT("version"), 1);
	Plan->SetStringField(TEXT("package"), FPackageName::ObjectPathToPackageName(BlueprintPath)); Plan->SetStringField(TEXT("name"), TEXT("ActorProof"));
	Plan->SetStringField(TEXT("kind"), TEXT("Blueprint")); Plan->SetStringField(TEXT("parent"), TEXT("/Script/Engine.Actor"));
	Plan->SetStringField(TEXT("class_flags"), TEXT("0x0"));
	for (const TCHAR* Field : {TEXT("interfaces"), TEXT("assets"), TEXT("variables"), TEXT("overrides"), TEXT("graphs")}) Plan->SetArrayField(Field, {});
	TSharedRef<FJsonObject> Defaults = MakeShared<FJsonObject>(); Defaults->SetArrayField(TEXT("values"), {}); Defaults->SetArrayField(TEXT("subobjects"), {});
	Plan->SetObjectField(TEXT("class_defaults"), Defaults);
	TSharedRef<FJsonObject> Mesh = Component(TEXT("MeshProof"), TEXT("/Script/Engine.StaticMeshComponent"), {
		MakeShared<FJsonValueObject>(Value(TEXT("StaticMesh"), MeshPath)),
		MakeShared<FJsonValueObject>(Value(TEXT("OverrideMaterials"), TEXT("(\"") + MaterialPath + TEXT("\")")))});
	TSharedRef<FJsonObject> Post = Component(TEXT("PostProof"), TEXT("/Script/Engine.PostProcessComponent"), {
		MakeShared<FJsonValueObject>(Value(TEXT("Settings"), TEXT("(WeightedBlendables=(Array=((Weight=1.0,Object=\"") + BlendablePath + TEXT("\"))))")))});
	Plan->SetArrayField(TEXT("components"), {MakeShared<FJsonValueObject>(Mesh), MakeShared<FJsonValueObject>(Post)});
	if (!TestTrue(TEXT("fixture declaration plan written"), Fixture.Write(Plan))
		|| !TestTrue(TEXT("fixture declares through public importer"), Ok(Reply(UP4BlueprintImport::Declare(Fixture.File))))) return false;
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
	if (!TestNotNull(TEXT("fixture Blueprint"), Blueprint) || !Blueprint->SimpleConstructionScript) return false;
	auto MeshTemplate = [&]() { USCS_Node* Node = Blueprint->SimpleConstructionScript->FindSCSNode(TEXT("MeshProof")); return Node ? Cast<UStaticMeshComponent>(Node->ComponentTemplate) : nullptr; };
	auto PostTemplate = [&]() { USCS_Node* Node = Blueprint->SimpleConstructionScript->FindSCSNode(TEXT("PostProof")); return Node ? Cast<UPostProcessComponent>(Node->ComponentTemplate) : nullptr; };

	// Objects the game does not ship: the references stay empty and nothing is made in their place.
	Plan->SetArrayField(TEXT("reference_only"), {MakeShared<FJsonValueString>(MeshPath), MakeShared<FJsonValueString>(MaterialPath), MakeShared<FJsonValueString>(BlendablePath)});
	if (!Fixture.Write(Plan)) return false;
	const TSharedPtr<FJsonObject> Empty = Reply(UP4BlueprintImport::Values(Fixture.File));
	if (!TestTrue(TEXT("Values accepts references to objects the game does not ship"), Ok(Empty))) return false;
	for (const FString& Path : {MeshPath, MaterialPath, BlendablePath})
	{
		TestNull(TEXT("nothing is made in place of a missing object"), FindObject<UObject>(nullptr, *Path));
		TestFalse(TEXT("no package is saved for a missing object"), FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(Path)));
	}
	UStaticMeshComponent* MeshValues = MeshTemplate(); UPostProcessComponent* PostValues = PostTemplate();
	if (!TestNotNull(TEXT("mesh template"), MeshValues) || !TestNotNull(TEXT("postprocess template"), PostValues)) return false;
	TestNull(TEXT("direct reference is empty"), MeshValues->GetStaticMesh());
	TestTrue(TEXT("array reference is empty"), MeshValues->OverrideMaterials.Num() == 1 && MeshValues->OverrideMaterials[0] == nullptr);
	TestTrue(TEXT("nested reference is empty"), PostValues->Settings.WeightedBlendables.Array.Num() == 1 && PostValues->Settings.WeightedBlendables.Array[0].Object == nullptr);
	TestFalse(TEXT("the reply holds no stand-in roles"), Empty->HasField(TEXT("reference_only_proxies")));
	const TSharedPtr<FJsonObject>* Made = nullptr; double EmptyCount = 0;
	TestTrue(TEXT("the reply counts the three empty references"), Empty->TryGetObjectField(TEXT("made"), Made) && Made
		&& (*Made)->TryGetNumberField(TEXT("references to objects the game does not ship, left empty"), EmptyCount) && EmptyCount == 3);

	// An object that is there binds as itself.
	const FString PresentPath = Fixture.Path(TEXT("PresentMaterial"));
	UPackage* PresentPackage = CreatePackage(*FPackageName::ObjectPathToPackageName(PresentPath));
	UMaterial* PresentMaterial = NewObject<UMaterial>(PresentPackage, TEXT("PresentMaterial"), RF_Public | RF_Standalone);
	PresentPackage->SetDirtyFlag(false);
	Plan->SetArrayField(TEXT("reference_only"), {MakeShared<FJsonValueString>(PresentPath)});
	Mesh->SetArrayField(TEXT("values"), {MakeShared<FJsonValueObject>(Value(TEXT("OverrideMaterials"), TEXT("(\"") + PresentPath + TEXT("\")")))});
	Plan->SetArrayField(TEXT("components"), {MakeShared<FJsonValueObject>(Mesh)});
	if (!Fixture.Write(Plan)) return false;
	TestTrue(TEXT("a present object binds"), Ok(Reply(UP4BlueprintImport::Values(Fixture.File))));
	TestTrue(TEXT("the reference is the present object"), MeshTemplate() && MeshTemplate()->OverrideMaterials.Num() == 1 && MeshTemplate()->OverrideMaterials[0] == PresentMaterial);
	TestFalse(TEXT("the present object is not marked as a stand-in"), UP4BlueprintImport::IsStandIn(PresentMaterial));
	TestFalse(TEXT("the present object's package stays clean"), PresentPackage->IsDirty());
	FProperty* Materials = FindFProperty<FProperty>(UStaticMeshComponent::StaticClass(), TEXT("OverrideMaterials"));
	int32 Visits = 0;
	P4Plan::VisitObjectReferences(Materials, TEXT("(\"") + MaterialPath + TEXT("\",\"") + PresentPath + TEXT("\""), [&](const FString&, UClass*) { ++Visits; });
	TestEqual(TEXT("malformed array creates no partial visits"), Visits, 0);
	return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4BlueprintStoredComponents,
	"P4Blueprints.StoredComponents.TemplatesAndBindings", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4BlueprintStoredComponents::RunTest(const FString&)
{
	using namespace P4ReferenceTests;
	FFixture Fixture;
	const FString BlueprintPath = Fixture.Path(TEXT("StoredComponents"));
	TSharedRef<FJsonObject> Plan = MakeShared<FJsonObject>();
	Plan->SetStringField(TEXT("format"), TEXT("p4-blueprint-plan")); Plan->SetNumberField(TEXT("version"), 1);
	Plan->SetStringField(TEXT("package"), FPackageName::ObjectPathToPackageName(BlueprintPath));
	Plan->SetStringField(TEXT("name"), TEXT("StoredComponents")); Plan->SetStringField(TEXT("kind"), TEXT("Blueprint"));
	Plan->SetStringField(TEXT("parent"), TEXT("/Script/Engine.Actor")); Plan->SetStringField(TEXT("class_flags"), TEXT("0x0"));
	for (const TCHAR* Field : {TEXT("interfaces"), TEXT("assets"), TEXT("variables"), TEXT("overrides"), TEXT("graphs")}) Plan->SetArrayField(Field, {});
	TSharedRef<FJsonObject> Defaults = MakeShared<FJsonObject>();
	Defaults->SetArrayField(TEXT("values"), {}); Defaults->SetArrayField(TEXT("subobjects"), {});
	Plan->SetObjectField(TEXT("class_defaults"), Defaults);
	Plan->SetArrayField(TEXT("components"), {MakeShared<FJsonValueObject>(Component(TEXT("CurrentCollision"), TEXT("/Script/Engine.StaticMeshComponent"), {}))});
	TSharedRef<FJsonObject> Template = MakeShared<FJsonObject>();
	Template->SetStringField(TEXT("name"), TEXT("NODE_AddStaticMeshComponent-5"));
	Template->SetStringField(TEXT("flags"), TEXT("0x29"));
	Template->SetStringField(TEXT("cls"), TEXT("/Script/Engine.StaticMeshComponent"));
	Template->SetStringField(TEXT("archetype"), TEXT("/Script/Engine.Default__StaticMeshComponent"));
	Template->SetArrayField(TEXT("values"), {MakeShared<FJsonValueObject>(Value(TEXT("bGenerateOverlapEvents"), TEXT("False")))});
	Template->SetArrayField(TEXT("subobjects"), {});
	Plan->SetArrayField(TEXT("component_templates"), {MakeShared<FJsonValueObject>(Template)});
	if (!Fixture.Write(Plan) || !TestTrue(TEXT("fixture declared"), Ok(Reply(UP4BlueprintImport::Declare(Fixture.File))))) return false;
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
	if (!TestNotNull(TEXT("fixture loaded"), Blueprint)) return false;
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		if (!TestTrue(TEXT("named template values survive compile and repeated import"), Ok(Reply(UP4BlueprintImport::Values(Fixture.File))))) return false;
		UBlueprintGeneratedClass* Class = Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass);
		if (!TestNotNull(TEXT("generated class"), Class)) return false;
		TestEqual(TEXT("authored template array has one entry"), Blueprint->ComponentTemplates.Num(), 1);
		TestEqual(TEXT("generated template array has one entry"), Class->ComponentTemplates.Num(), 1);
		UStaticMeshComponent* Stored = Class->ComponentTemplates.Num() ? Cast<UStaticMeshComponent>(Class->ComponentTemplates[0]) : nullptr;
		if (!TestNotNull(TEXT("add-component template retained"), Stored)) return false;
		TestEqual(TEXT("exact source template name retained"), Stored->GetName(), FString(TEXT("NODE_AddStaticMeshComponent-5")));
		TestTrue(TEXT("template belongs to generated class"), Stored->GetOuter() == Class);
		TestTrue(TEXT("template is an archetype"), Stored->HasAnyFlags(RF_ArchetypeObject));
		TestFalse(TEXT("template sparse source setting retained"), Stored->GetGenerateOverlapEvents());
		TestNull(TEXT("add-component template is not introduced as an SCS node"), Blueprint->SimpleConstructionScript->FindSCSNode(TEXT("NODE_AddStaticMeshComponent-5")));
	}
	const FString Function = TEXT("BndEvt__OldCollision_K2Node_ComponentBoundEvent_0_ComponentEndOverlapSignature__DelegateSignature");
	TSharedRef<FJsonObject> Event = MakeShared<FJsonObject>();
	Event->SetStringField(TEXT("t"), TEXT("bound_event")); Event->SetStringField(TEXT("id"), TEXT("n0"));
	Event->SetStringField(TEXT("fn"), Function); Event->SetStringField(TEXT("component"), TEXT("CurrentCollision"));
	Event->SetStringField(TEXT("delegate"), TEXT("ComponentEndOverlapSignature")); Event->SetStringField(TEXT("delegate_property"), TEXT("OnComponentEndOverlap"));
	TSharedRef<FJsonObject> Graph = MakeShared<FJsonObject>();
	Graph->SetStringField(TEXT("name"), TEXT("EventGraph")); Graph->SetStringField(TEXT("kind"), TEXT("event"));
	Graph->SetArrayField(TEXT("nodes"), {MakeShared<FJsonValueObject>(Event)}); Graph->SetArrayField(TEXT("links"), {}); Graph->SetArrayField(TEXT("locals"), {});
	Plan->SetArrayField(TEXT("graphs"), {MakeShared<FJsonValueObject>(Graph)});
	if (!Fixture.Write(Plan) || !TestTrue(TEXT("actual stored component binding declaration accepted"), Ok(Reply(UP4BlueprintImport::Signatures(Fixture.File))))
		|| !TestTrue(TEXT("actual stored binding compiles"), Ok(Reply(UP4BlueprintImport::Graphs(Fixture.File))))) return false;
	UBlueprintGeneratedClass* Class = Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass);
	UComponentDelegateBinding* Binding = nullptr;
	for (UDynamicBlueprintBinding* Held : Class->DynamicBindingObjects)
		if (UComponentDelegateBinding* Found = Cast<UComponentDelegateBinding>(Held)) Binding = Found;
	if (!TestNotNull(TEXT("compiler emitted component binding"), Binding)) return false;
	if (!TestEqual(TEXT("one exact binding emitted"), Binding->ComponentDelegateBindings.Num(), 1)) return false;
	const FBlueprintComponentDelegateBinding& Row = Binding->ComponentDelegateBindings[0];
	TestEqual(TEXT("binding uses current stored component rather than stale event name"), Row.ComponentPropertyName, FName(TEXT("CurrentCollision")));
	TestEqual(TEXT("binding uses exact stored delegate property"), Row.DelegatePropertyName, FName(TEXT("OnComponentEndOverlap")));
	TestEqual(TEXT("binding retains original generated function name"), Row.FunctionNameToBind, FName(*Function));
	Event->SetStringField(TEXT("delegate_property"), TEXT("OnComponentBeginOverlap"));
	if (!Fixture.Write(Plan)) return false;
	const TSharedPtr<FJsonObject> Refused = Reply(UP4BlueprintImport::Signatures(Fixture.File));
	TestFalse(TEXT("an explicit binding to a property with another signature stops before graph compilation"), Ok(Refused));
	const TArray<TSharedPtr<FJsonValue>>* Problems = nullptr;
	TestTrue(TEXT("mismatched source delegate property is named in the refusal"), Refused.IsValid()
		&& Refused->TryGetArrayField(TEXT("problems"), Problems) && Problems && Problems->ContainsByPredicate(
			[](const TSharedPtr<FJsonValue>& Line) { return Line->AsString().Contains(TEXT("source delegate property OnComponentBeginOverlap")); }));
	// The declaration removed the old event. Even an independently requested
	// graph pass cannot synthesize the mismatched binding from its function name.
	UP4BlueprintImport::Graphs(Fixture.File);
	Class = Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass);
	if (!TestNotNull(TEXT("refused fixture still has its generated class"), Class)) return false;
	int32 IncorrectBindings = 0;
	for (UDynamicBlueprintBinding* Held : Class->DynamicBindingObjects)
		if (UComponentDelegateBinding* Remaining = Cast<UComponentDelegateBinding>(Held))
			for (const FBlueprintComponentDelegateBinding& Candidate : Remaining->ComponentDelegateBindings)
				if (Candidate.FunctionNameToBind == FName(*Function)) ++IncorrectBindings;
	TestEqual(TEXT("graph compiler emitted no binding for the refused source event"), IncorrectBindings, 0);
	return !HasAnyErrors();
}
#endif
