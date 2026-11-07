// Existing importer regressions adjusted for independent per-asset reimport.
#include "P4BlueprintImport.h"
#include "P4BlueprintReimport.h"
#include "P4OriginalImportSave.h"
// These tests use Kadan headers, so only the Kadan test module builds them.
#if defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "P4OriginalRecord.h"
#include "P4KadanAssetPolicy.h"
#include "P4KadanReadOnly.h"
#endif
#include "AssetRegistryModule.h"
#include "BlueprintEditorSettings.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Editor/TransBuffer.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/StaticMeshActor.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"
#include "UObject/MetaData.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"
#include "UObject/StrongObjectPtr.h"
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
namespace P4ReimportTests
{
	struct FAlwaysSavePolicy
	{
		UBlueprintEditorSettings* Settings = GetMutableDefault<UBlueprintEditorSettings>();
		TEnumAsByte<ESaveOnCompile> Previous = Settings->SaveOnCompile;
		FAlwaysSavePolicy() { Settings->SaveOnCompile = SoC_Always; }
		~FAlwaysSavePolicy() { Settings->SaveOnCompile = Previous; }
	};
	bool PolicyIntact(FAutomationTestBase& Test, const FString& Label)
	{
		return Test.TestTrue(Label + TEXT(" preserves caller SaveOnCompile=Always"),
			GetMutableDefault<UBlueprintEditorSettings>()->SaveOnCompile == SoC_Always);
	}
	bool Replay(FAutomationTestBase& Test, bool bRedo, const TCHAR* Label)
	{
		const bool bResult = bRedo ? GEditor->RedoTransaction() : GEditor->UndoTransaction();
		PolicyIntact(Test, Label);
		return bResult;
	}
	const EObjectFlags PersistentTemplateFlags = EObjectFlags(RF_Public | RF_Standalone | RF_Transient | RF_Transactional);
	struct FTemplateIdentity
	{
		USceneComponent* Template = nullptr;
		FString Path;
		EObjectFlags Flags = RF_NoFlags;
		explicit FTemplateIdentity(UBlueprint* Blueprint)
		{
			if (Blueprint && Blueprint->SimpleConstructionScript)
				for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
					if (Node->GetVariableName() == TEXT("ProofChild")) Template = Cast<USceneComponent>(Node->ComponentTemplate);
			if (Template) { Path = Template->GetPathName(); Flags = Template->GetFlags() & PersistentTemplateFlags; }
		}
	};
	TSharedRef<FJsonObject> Value(const TCHAR* Name, const FString& Text)
	{
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("name"), Name);
		Out->SetNumberField(TEXT("index"), 0);
		Out->SetStringField(TEXT("text"), Text);
		return Out;
	}
	bool WriteJson(const FString& File, const TSharedRef<FJsonObject>& Object)
	{
		FString Text;
		return FJsonSerializer::Serialize(Object, TJsonWriterFactory<TCHAR>::Create(&Text)) &&
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true) &&
			FFileHelper::SaveStringToFile(Text, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
	TSharedPtr<FJsonObject> Reply(const FString& Text)
	{
		TSharedPtr<FJsonObject> Out;
		FJsonSerializer::Deserialize(TJsonReaderFactory<TCHAR>::Create(Text), Out);
		return Out;
	}
	bool Ok(const TSharedPtr<FJsonObject>& Object)
	{
		bool bOk = false;
		const TArray<TSharedPtr<FJsonValue>>* Problems = nullptr;
		return Object.IsValid() && Object->TryGetBoolField(TEXT("ok"), bOk) && bOk &&
			(!Object->TryGetArrayField(TEXT("problems"), Problems) || Problems->Num() == 0);
	}
	struct FFixture
	{
		FString Root, Directory, ActorPackage, ActorPath;
		TArray<FString> Packages;
		FFixture()
		{
			const FString Key = FGuid::NewGuid().ToString(EGuidFormats::Digits);
			Root = TEXT("/Game/P4BlueprintProof/Transactions_") + Key;
			Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("P4BlueprintProof"), TEXT("Transactions_") + Key);
			ActorPackage = Root + TEXT("/ActorProof");
			ActorPath = ActorPackage + TEXT(".ActorProof");
			Packages.Add(ActorPackage);
		}
		~FFixture()
		{
			// Only unique test-owned roots/files are retired. Never reset the editor transaction buffer.
			for (const FString& Package : Packages)
			{
				if (!Package.StartsWith(Root + TEXT("/"))) continue;
				if (UPackage* Loaded = FindPackage(nullptr, *Package))
				{
					TArray<UObject*> Objects; GetObjectsWithOuter(Loaded, Objects, false);
					for (UObject* Object : Objects)
					{
						if (Object->IsAsset()) FAssetRegistryModule::AssetDeleted(Object);
						const FName Retired = MakeUniqueObjectName(GetTransientPackage(), Object->GetClass(), TEXT("P4RetiredProof"));
						Object->Rename(*Retired.ToString(), GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
						Object->ClearFlags(RF_Public | RF_Standalone); Object->SetFlags(RF_Transient);
					}
					Loaded->SetDirtyFlag(false);
				}
				FString Base;
				if (FPackageName::TryConvertLongPackageNameToFilename(Package, Base))
					for (const TCHAR* Suffix : {TEXT(".uasset"), TEXT(".umap"), TEXT(".uexp"), TEXT(".ubulk"), TEXT(".uptnl")})
						IFileManager::Get().Delete(*(Base + Suffix), false, false, true);

			}
			IFileManager::Get().DeleteDirectory(*Directory, false, true);
		}
		TSharedRef<FJsonObject> Plan(const FString& Package, bool bChanged, bool bMissingParent = false)
		{
			TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
			Out->SetStringField(TEXT("format"), TEXT("p4-blueprint-plan")); Out->SetNumberField(TEXT("version"), 1);
			Out->SetStringField(TEXT("package"), Package); Out->SetStringField(TEXT("name"), FPackageName::GetShortName(Package));
			Out->SetStringField(TEXT("kind"), TEXT("Blueprint"));
			Out->SetStringField(TEXT("parent"), bMissingParent ? TEXT("/Script/Engine.P4ProofMissingParent") : TEXT("/Script/Engine.StaticMeshActor"));
			Out->SetStringField(TEXT("class_flags"), TEXT("0x0"));
			for (const TCHAR* Field : {TEXT("interfaces"), TEXT("assets"), TEXT("overrides"), TEXT("graphs")}) Out->SetArrayField(Field, {});
			TSharedRef<FJsonObject> Type = MakeShared<FJsonObject>(); Type->SetStringField(TEXT("c"), TEXT("int"));
			TSharedRef<FJsonObject> Variable = MakeShared<FJsonObject>();
			Variable->SetStringField(TEXT("name"), bChanged ? TEXT("ReplacementCount") : TEXT("OriginalCount"));
			Variable->SetStringField(TEXT("flags"), TEXT("0x5")); Variable->SetObjectField(TEXT("type"), Type);
			Out->SetArrayField(TEXT("variables"), {MakeShared<FJsonValueObject>(Variable)});
			const FString NativeName = GetDefault<AStaticMeshActor>()->GetStaticMeshComponent()->GetName();
			TSharedRef<FJsonObject> Component = MakeShared<FJsonObject>();
			Component->SetStringField(TEXT("variable"), TEXT("ProofChild")); Component->SetStringField(TEXT("cls"), TEXT("/Script/Engine.SceneComponent"));
			Component->SetStringField(TEXT("parent"), NativeName); Component->SetBoolField(TEXT("parent_native"), true);
			Component->SetArrayField(TEXT("values"), {MakeShared<FJsonValueObject>(Value(TEXT("RelativeLocation"), bChanged ? TEXT("(X=73,Y=-4,Z=2)") : TEXT("(X=23,Y=-4,Z=2)")))});
			Out->SetArrayField(TEXT("components"), {MakeShared<FJsonValueObject>(Component)});
			TSharedRef<FJsonObject> Defaults = MakeShared<FJsonObject>();
			Defaults->SetArrayField(TEXT("values"), {
				MakeShared<FJsonValueObject>(Value(bChanged ? TEXT("ReplacementCount") : TEXT("OriginalCount"), bChanged ? TEXT("29") : TEXT("7"))),
				MakeShared<FJsonValueObject>(Value(TEXT("InitialLifeSpan"), bChanged ? TEXT("11.25") : TEXT("3.5")))});
			TSharedRef<FJsonObject> Native = MakeShared<FJsonObject>();
			Native->SetArrayField(TEXT("path"), {MakeShared<FJsonValueString>(NativeName)});
			Native->SetStringField(TEXT("cls"), TEXT("/Script/Engine.StaticMeshComponent"));
			Native->SetArrayField(TEXT("values"), {MakeShared<FJsonValueObject>(Value(TEXT("RelativeLocation"), bChanged ? TEXT("(X=61,Y=-2,Z=5)") : TEXT("(X=17,Y=-2,Z=5)")))});
			Defaults->SetArrayField(TEXT("subobjects"), {MakeShared<FJsonValueObject>(Native)});
			Out->SetObjectField(TEXT("class_defaults"), Defaults);
			return Out;
		}
		bool Write(bool bChanged, bool bLateFailure = false)
		{
			TSharedRef<FJsonObject> Index = MakeShared<FJsonObject>();
			Index->SetStringField(TEXT("format"), TEXT("p4-blueprint-plans")); Index->SetNumberField(TEXT("version"), 1);
			TArray<TSharedPtr<FJsonValue>> Rows;
			TArray<TSharedRef<FJsonObject>> Plans = {Plan(ActorPackage, bChanged)};
			if (bLateFailure)
			{
				const FString Broken = Root + TEXT("/LateMissing"); Packages.AddUnique(Broken);
				Plans.Add(Plan(Broken, false, true));
			}
			for (const TSharedRef<FJsonObject>& Body : Plans)
			{
				TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
				for (const TCHAR* Field : {TEXT("package"), TEXT("parent"), TEXT("kind")}) Row->SetStringField(Field, Body->GetStringField(Field));
				Rows.Add(MakeShared<FJsonValueObject>(Row));
				if (!WriteJson(FPaths::Combine(Directory, TEXT("import"), Body->GetStringField(TEXT("package")).Mid(1) + TEXT(".json")), Body)) return false;
			}
			Index->SetArrayField(TEXT("order"), Rows);
			return WriteJson(FPaths::Combine(Directory, TEXT("index.json")), Index);
		}
		TSharedPtr<FJsonObject> Import(bool bReimport, FAutomationTestBase* Test)
		{
			TSharedPtr<FJsonObject> Result = Reply(UP4BlueprintImport::Run(Directory, FString(), TEXT("Blueprint"), FString(), 0, bReimport));
			PolicyIntact(*Test, bReimport ? TEXT("actual reimport") : TEXT("actual initial import"));
			return Result;
		}
	};

	bool CheckState(FAutomationTestBase& Test, const FString& Label, FFixture& Fixture, UBlueprint* Identity, bool bChanged, const FTemplateIdentity& ExpectedTemplate)
	{
		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *Fixture.ActorPath);
		if (!Test.TestTrue(Label + TEXT(" retains exact asset root identity"), Blueprint == Identity) || !Blueprint || !Blueprint->GeneratedClass) return false;
		UObject* CDO = Blueprint->GeneratedClass->GetDefaultObject();
		// Compiler-owned generated classes/CDOs may reinstance; the authored SCS template must retain its exact captured identity.
		FIntProperty* Own = FindFProperty<FIntProperty>(Blueprint->GeneratedClass, bChanged ? TEXT("ReplacementCount") : TEXT("OriginalCount"));
		FProperty* Removed = FindFProperty<FProperty>(Blueprint->GeneratedClass, bChanged ? TEXT("OriginalCount") : TEXT("ReplacementCount"));
		bool bOk = Test.TestNotNull(Label + TEXT(" has the restored own int field"), Own);
		bOk &= Test.TestNull(Label + TEXT(" excludes the other schema field"), Removed);
		if (Own) bOk &= Test.TestEqual(Label + TEXT(" restores nonzero own CDO default"), Own->GetPropertyValue_InContainer(CDO), bChanged ? 29 : 7);
		FFloatProperty* Life = FindFProperty<FFloatProperty>(Blueprint->GeneratedClass, TEXT("InitialLifeSpan"));
		bOk &= Test.TestNotNull(Label + TEXT(" retains inherited scalar declaration"), Life);
		if (Life) bOk &= Test.TestEqual(Label + TEXT(" restores inherited scalar default"), Life->GetPropertyValue_InContainer(CDO), bChanged ? 11.25f : 3.5f);
		AStaticMeshActor* Actor = Cast<AStaticMeshActor>(CDO);
		bOk &= Test.TestNotNull(Label + TEXT(" has native Actor CDO"), Actor);
		if (Actor && Actor->GetStaticMeshComponent())
			bOk &= Test.TestEqual(Label + TEXT(" restores inherited CDO subobject default"), Actor->GetStaticMeshComponent()->GetRelativeLocation(), FVector(bChanged ? 61 : 17, -2, 5));
		else bOk &= Test.TestTrue(Label + TEXT(" has native CDO component subobject"), false);
		USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
		bOk &= Test.TestNotNull(Label + TEXT(" retains an authored construction script"), SCS);
		if (SCS)
		{
			bOk &= Test.TestTrue(Label + TEXT(" SCS outer is the actual generated class"), SCS->GetOuter() == Blueprint->GeneratedClass);
			USCS_Node* Node = nullptr;
			for (USCS_Node* Candidate : SCS->GetAllNodes()) if (Candidate->GetVariableName() == TEXT("ProofChild")) Node = Candidate;
			bOk &= Test.TestNotNull(Label + TEXT(" retains authored SCS node"), Node);
			USceneComponent* Template = Node ? Cast<USceneComponent>(Node->ComponentTemplate) : nullptr;
			bOk &= Test.TestNotNull(Label + TEXT(" retains component template"), Template);
			if (Template)
			{
				bOk &= Test.TestNotNull(Label + TEXT(" has a captured expected component template"), ExpectedTemplate.Template);
				bOk &= Test.TestTrue(Label + TEXT(" retains exact SCS component-template pointer"), Template == ExpectedTemplate.Template);
				bOk &= Test.TestEqual(Label + TEXT(" restores exact SCS component-template path"), Template->GetPathName(), ExpectedTemplate.Path);
				bOk &= Test.TestEqual(Label + TEXT(" restores persistent SCS component-template flags"),
					static_cast<uint32>(Template->GetFlags() & PersistentTemplateFlags), static_cast<uint32>(ExpectedTemplate.Flags));
				bOk &= Test.TestEqual(Label + TEXT(" restores SCS template default"), Template->GetRelativeLocation(), FVector(bChanged ? 73 : 23, -4, 2));
			}
		}
		return bOk;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4BlueprintTransactionDefaults,
 "P4Blueprints.Reimport.SchemaDefaults", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4BlueprintTransactionDefaults::RunTest(const FString& Parameters)
{
 using namespace P4ReimportTests;
 FAlwaysSavePolicy SavePolicy;
 FFixture Fixture;
 if(!TestTrue(TEXT("original recipe written"),Fixture.Write(false))||!TestTrue(TEXT("initial import succeeds"),Ok(Fixture.Import(false,this))))return false;
 UBlueprint* Blueprint=LoadObject<UBlueprint>(nullptr,*Fixture.ActorPath);
 const FTemplateIdentity Original(Blueprint);
 if(!CheckState(*this,TEXT("original"),Fixture,Blueprint,false,Original))return false;
 const FString File=FPackageName::LongPackageNameToFilename(Fixture.ActorPackage,FPackageName::GetAssetPackageExtension());
 const FString Stem=File.LeftChop(FPaths::GetExtension(File,true).Len());
 const FString SourceRecordText=Blueprint->GetOutermost()->GetMetaData()->GetValue(Blueprint,TEXT("P4OriginalRecord"));
 const FP4OriginalStatus SourceStatus=P4QueryOriginalAsset(Blueprint);
 const P4Kadan::FAssetPolicy SourcePolicy=P4Kadan::GetAssetPolicy(Blueprint);
 UE_LOG(LogTemp,Display,TEXT("Synthetic source annotation fixture %s: original=%d state=%d record-readonly=%d reference-only=%d edit-allowed=%d package-locked=%d file-readonly=%d reason=%s record=%s"),
  *Blueprint->GetPathName(),SourceStatus.IsOriginal(),int32(SourceStatus.State),SourceStatus.Record.ReadOnly,SourceStatus.Record.ReferenceOnly,
  SourcePolicy.AllowEdit,P4Kadan::IsLockedPackage(Blueprint->GetOutermost()),IFileManager::Get().IsReadOnly(*File),*SourcePolicy.Reason,*SourceRecordText.Left(2048));
 if(!TestTrue(TEXT("synthetic source has actual native original provenance"),SourceStatus.IsOriginal()&&SourceStatus.Error.IsEmpty()&&!SourceRecordText.IsEmpty()))return false;
 TestTrue(TEXT("native policy classifies the imported Blueprint as a protected original"),SourcePolicy.Original&&!SourcePolicy.AllowEdit);
 TestTrue(TEXT("actual native save guard locks the imported Blueprint package"),P4Kadan::IsLockedPackage(Blueprint->GetOutermost()));
 TestTrue(TEXT("ordinary save starts outside native import publication"),!P4ImportPublication::IsImporting()&&!P4ImportPublication::IsSaving(Blueprint->GetOutermost()));
 TestTrue(TEXT("published imported-source file has its actual readonly save protection"),IFileManager::Get().IsReadOnly(*File));
 TMap<FString,TArray<uint8>> PublishedFiles;
 for(const TCHAR* Suffix : {TEXT(".uasset"),TEXT(".uexp"),TEXT(".ubulk"),TEXT(".uptnl")})
 {
  const FString One=Stem+Suffix;
  if(IFileManager::Get().FileExists(*One))
  {
   TArray<uint8> Bytes;
   if(!TestTrue(TEXT("reads unique synthetic-source package before ordinary save"),FFileHelper::LoadFileToArray(Bytes,*One)))return false;
   PublishedFiles.Add(One,MoveTemp(Bytes));
  }
 }
 if(!TestTrue(TEXT("synthetic source already has a published package"),PublishedFiles.Contains(File)))return false;
 TestFalse(TEXT("ordinary unscoped save of imported source is refused"),UPackage::SavePackage(Blueprint->GetOutermost(),Blueprint,RF_Public|RF_Standalone,*File,GError,nullptr,false,true,SAVE_NoError));
 for(const TCHAR* Suffix : {TEXT(".uasset"),TEXT(".uexp"),TEXT(".ubulk"),TEXT(".uptnl")})
 {
  const FString One=Stem+Suffix;
  if(const TArray<uint8>* Before=PublishedFiles.Find(One))
  {
   TArray<uint8> After;
   TestTrue(TEXT("refused ordinary save leaves source bundle bytes unchanged"),FFileHelper::LoadFileToArray(After,*One)&&After==*Before);
  }
  else TestFalse(TEXT("refused ordinary save creates no source bundle file"),IFileManager::Get().FileExists(*One));
 }
 // This is unique synthetic-source fixture setup, not a creator edit of an original.
 // Publish only an editor annotation through the same explicit native import/save scopes.
 if(!TestTrue(TEXT("source annotation setup owns the exact unique fixture"),Blueprint->GetPathName()==Fixture.ActorPath&&Fixture.ActorPackage.StartsWith(Fixture.Root+TEXT("/"))))return false;
 {
  P4ImportPublication::FImportScope Import;
  P4ImportPublication::FSaveScope Save(Blueprint->GetOutermost());
  Blueprint->BlueprintDescription=TEXT("Synthetic source description");
  if(!TestTrue(TEXT("synthetic source annotation publication succeeds"),UPackage::SavePackage(Blueprint->GetOutermost(),Blueprint,RF_Public|RF_Standalone,*File,GError,nullptr,false,true,SAVE_NoError)))return false;
 }
 TestTrue(TEXT("source annotation publication restores readonly file protection"),IFileManager::Get().IsReadOnly(*File));
 TestTrue(TEXT("source annotation publication restores native scope state"),!P4ImportPublication::IsImporting()&&!P4ImportPublication::IsSaving(Blueprint->GetOutermost()));
 const FP4OriginalStatus AfterAnnotationStatus=P4QueryOriginalAsset(Blueprint);
 const P4Kadan::FAssetPolicy AfterAnnotationPolicy=P4Kadan::GetAssetPolicy(Blueprint);
 TestTrue(TEXT("source annotation retains the exact native origin record and flags"),
  FString(Blueprint->GetOutermost()->GetMetaData()->GetValue(Blueprint,TEXT("P4OriginalRecord")))==SourceRecordText&&
  AfterAnnotationStatus.IsOriginal()&&AfterAnnotationStatus.Record.ReadOnly==SourceStatus.Record.ReadOnly&&AfterAnnotationStatus.Record.ReferenceOnly==SourceStatus.Record.ReferenceOnly);
 TestTrue(TEXT("source annotation restores actual native original protection"),AfterAnnotationPolicy.Original&&!AfterAnnotationPolicy.AllowEdit&&P4Kadan::IsLockedPackage(Blueprint->GetOutermost()));
 if(!TestTrue(TEXT("changed recipe written"),Fixture.Write(true))||!TestTrue(TEXT("in-place reimport succeeds"),Ok(Fixture.Import(true,this))))return false;
 CheckState(*this,TEXT("reimport"),Fixture,Blueprint,true,Original);
 TestEqual(TEXT("source annotation retained"),Blueprint->BlueprintDescription,FString(TEXT("Synthetic source description")));
 const FString Ids=UP4BlueprintImport::Ids(Fixture.ActorPath);
 TestTrue(TEXT("repeated import succeeds"),Ok(Fixture.Import(true,this)));
 CheckState(*this,TEXT("repeat"),Fixture,Blueprint,true,Original);
 TestEqual(TEXT("repeated IDs remain stable"),UP4BlueprintImport::Ids(Fixture.ActorPath),Ids);
 return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4BlueprintLateFailureRollback,
 "P4Blueprints.Reimport.LateFailurePreservesCompletedAssets", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4BlueprintLateFailureRollback::RunTest(const FString& Parameters)
{
 using namespace P4ReimportTests;
 FAlwaysSavePolicy SavePolicy;
 FFixture Fixture;
 if(!Fixture.Write(false)||!Ok(Fixture.Import(false,this)))return false;
 UBlueprint* Blueprint=LoadObject<UBlueprint>(nullptr,*Fixture.ActorPath);
 const FTemplateIdentity Original(Blueprint);
 USceneComponent* Prior=NewObject<USceneComponent>(GetTransientPackage(),NAME_None,RF_Transactional);
 Prior->AddToRoot();
 {
  FScopedTransaction Transaction(NSLOCTEXT("P4Blueprints","PriorEdit","Unrelated edit"));
  Prior->Modify();Prior->SetRelativeLocation(FVector(321,0,0));
 }
 if(!Fixture.Write(true,true)){Prior->RemoveFromRoot();return false;}
 const auto Failed=Fixture.Import(true,this);
 TestFalse(TEXT("later missing parent is reported"),Ok(Failed));
 CheckState(*this,TEXT("completed first asset"),Fixture,Blueprint,true,Original);
 TestFalse(TEXT("completed first asset saved"),Blueprint->GetOutermost()->IsDirty());
 TestEqual(TEXT("unrelated edit retained"),Prior->GetRelativeLocation(),FVector(321,0,0));
 TestTrue(TEXT("unrelated Undo remains available"),Replay(*this,false,TEXT("unrelated Undo")));
 TestEqual(TEXT("unrelated Undo affects only its object"),Prior->GetRelativeLocation(),FVector::ZeroVector);
 CheckState(*this,TEXT("after unrelated Undo"),Fixture,Blueprint,true,Original);
 Prior->RemoveFromRoot();
 TestTrue(TEXT("recovery recipe written"),Fixture.Write(false));
 TestTrue(TEXT("recovery reimport succeeds"),Ok(Fixture.Import(true,this)));
 CheckState(*this,TEXT("recovery"),Fixture,Blueprint,false,Original);
 return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4BlueprintNativePreflightIsolation,
 "P4Blueprints.Reimport.NativePreflightFailurePreservesOtherAssets", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4BlueprintNativePreflightIsolation::RunTest(const FString&)
{
 using namespace P4ReimportTests;
 FAlwaysSavePolicy SavePolicy;
 FFixture Fixture;
 if(!Fixture.Write(false)||!Ok(Fixture.Import(false,this)))return false;
 UBlueprint* Existing=LoadObject<UBlueprint>(nullptr,*Fixture.ActorPath);
 if(!TestNotNull(TEXT("existing imported fixture"),Existing))return false;
 const FTemplateIdentity Before(Existing);
 Existing->GetOutermost()->SetDirtyFlag(true);
 const FString Other=Fixture.Root+TEXT("/Independent");Fixture.Packages.Add(Other);
 auto Index=MakeShared<FJsonObject>();Index->SetStringField(TEXT("format"),TEXT("p4-blueprint-plans"));Index->SetNumberField(TEXT("version"),1);
 TArray<TSharedPtr<FJsonValue>> Rows;
 for(const FString& Package:{Fixture.ActorPackage,Other})
 {
  auto Body=Fixture.Plan(Package,true);auto Row=MakeShared<FJsonObject>();
  for(const TCHAR* Field:{TEXT("package"),TEXT("parent"),TEXT("kind")})Row->SetStringField(Field,Body->GetStringField(Field));
  Rows.Add(MakeShared<FJsonValueObject>(Row));
  if(!WriteJson(FPaths::Combine(Fixture.Directory,TEXT("import"),Package.Mid(1)+TEXT(".json")),Body))return false;
 }
 Index->SetArrayField(TEXT("order"),Rows);if(!WriteJson(FPaths::Combine(Fixture.Directory,TEXT("index.json")),Index))return false;
 const auto Result=UP4BlueprintImport::RunSelectedNative(Fixture.Directory,FString(),{Fixture.ActorPackage,Other},true,{},true);
 TestFalse(TEXT("dirty creator asset remains a refused row"),Result.CompletedPackages.Contains(FName(*Fixture.ActorPackage)));
 TestFalse(TEXT("asset refusal is not an infrastructure failure"),Result.bInfrastructureFailure);
 TestTrue(TEXT("independent source still imports and saves"),Result.CompletedPackages.Contains(FName(*Other)));
 TestTrue(TEXT("the dirty row retains its own precise refusal"),Result.PackageErrors.FindRef(Fixture.ActorPackage).Contains(TEXT("save your changes before reimporting")));
 TestFalse(TEXT("completed independent source has no refusal"),Result.PackageErrors.Contains(Other));
 TestTrue(TEXT("dirty flag is preserved"),Existing->GetOutermost()->IsDirty());
 CheckState(*this,TEXT("refused dirty source"),Fixture,Existing,false,Before);
 return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4BlueprintFailedDeclarationCleanup,
 "P4Blueprints.Reimport.RefusedDeclarationsDoNotPoisonNextMap", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4BlueprintFailedDeclarationCleanup::RunTest(const FString&)
{
 using namespace P4ReimportTests;
 FFixture Fixture;if(!Fixture.Write(false))return false;
 TStrongObjectPtr<UBlueprint> Child;UClass* ParentClass=nullptr;
 const FString ChildPackage=Fixture.Root+TEXT("/DependentChild");Fixture.Packages.Add(ChildPackage);
 const auto Failed=UP4BlueprintImport::RunSelectedNative(Fixture.Directory,FString(),{Fixture.ActorPackage},false,
  [&](const TMap<FName,UClass*>& Declared,FString& Why)
  {
   ParentClass=Declared.FindRef(FName(*Fixture.ActorPackage));
   if(ParentClass)Child.Reset(FKismetEditorUtilities::CreateBlueprint(ParentClass,CreatePackage(*ChildPackage),TEXT("DependentChild"),BPTYPE_Normal,UBlueprint::StaticClass(),UBlueprintGeneratedClass::StaticClass()));
   Why=TEXT("synthetic deferred dependency refused");return false;
  },true);
 TestEqual(TEXT("refused declaration has no completed package"),Failed.CompletedPackages.Num(),0);
 TestNull(TEXT("failed fresh shell vacates its original asset identity"),FindObject<UBlueprint>(nullptr,*Fixture.ActorPath));
 TestFalse(TEXT("failed fresh shell leaves no dirty source package"),FindPackage(nullptr,*Fixture.ActorPackage)&&FindPackage(nullptr,*Fixture.ActorPackage)->IsDirty());
 CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
 TestTrue(TEXT("a live child retains its refused parent's valid ancestry"),Child.IsValid()&&ParentClass&&!ParentClass->IsPendingKill()&&Child->GeneratedClass->GetSuperClass()==ParentClass);
 const auto Retried=UP4BlueprintImport::RunSelectedNative(Fixture.Directory,FString(),{Fixture.ActorPackage},false,{},true);
 TestTrue(TEXT("the next map can import the same original source"),Retried.bOk&&Retried.CompletedPackages.Contains(FName(*Fixture.ActorPackage)));
 return !HasAnyErrors();
}
#endif
