// The order previously supplied by import_editor.py, executed inside the editor.
#include "P4BlueprintRun.h"
#include "P4BlueprintImport.h"
#include "P4BlueprintPlan.h"
#include "P4BlueprintReimport.h"
#include "P4BlueprintWidgetMembers.h"
#include "P4OriginalImportSave.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Animation/AnimBlueprint.h"
#include "WidgetBlueprint.h"
#include "LevelSequence.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectGlobals.h"

void P4RestorePendingStandIns(const TArray<FString>& Packages);
void P4KeepAbsentAsTheGames(bool bKeep);
TMap<FString, FString> P4TakeKeptAsTheGames();

namespace P4BlueprintRun
{
	namespace
	{
		bool bRunning = false;
		struct FRow
		{
			FString Package, Kind, Parent, File;
			bool bShell = false, bFailed = false, bComplete = false, bPendingAnimationSeal = false;
			TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>();
		};

		FString Text(const TSharedRef<FJsonObject>& Object)
		{
			FString Out;
			FJsonSerializer::Serialize(Object, TJsonWriterFactory<TCHAR>::Create(&Out));
			return Out;
		}

		bool Ok(const TSharedRef<FJsonObject>& Object, const TCHAR* Field)
		{
			const TSharedPtr<FJsonObject>* Report = nullptr;
			bool bOk = false;
			return Object->TryGetObjectField(Field, Report) && (*Report)->TryGetBoolField(TEXT("ok"), bOk) && bOk;
		}

		bool ReadParent(const TSharedPtr<FJsonObject>& Object, FString& Out)
		{
			const TSharedPtr<FJsonValue>* Value = Object->Values.Find(TEXT("parent"));
			if (!Value) { return false; }
			if ((*Value)->Type == EJson::Null) { Out.Empty(); return true; }
			return Object->TryGetStringField(TEXT("parent"), Out);
		}

		bool Save(const FString& Name)
		{
			UPackage* Package = FindPackage(nullptr, *Name);
			if (!Package) { Package = LoadPackage(nullptr, *Name, LOAD_NoWarn | LOAD_Quiet); }
			if (!Package) { return false; }
			FString File;
			if (!FPackageName::TryConvertLongPackageNameToFilename(Name, File, FPackageName::GetAssetPackageExtension())) { return false; }
			if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true)) { return false; }
			P4ImportPublication::FSaveScope ImportSave(Package);
			// Match save_asset(..., only_if_is_dirty=false), with the kit's one retry retained.
			for (int32 Attempt = 0; Attempt != 2; ++Attempt)
			{
				if (UPackage::SavePackage(Package, nullptr, RF_Public | RF_Standalone, *File, GError, nullptr, false, true, SAVE_NoError)) { return true; }
			}
			return false;
		}

		bool IsTaggedPackage(const FString& Name)
		{
			UPackage* Package = FindPackage(nullptr, *Name);
			if (!Package) { return false; }
			TArray<UObject*> Objects;
			GetObjectsWithOuter(Package, Objects, false);
			for (UObject* Object : Objects) { if (UP4BlueprintImport::IsStandIn(Object)) { return true; } }
			return false;
		}

		class FRun
		{
		public:
			const FOptions& Options;
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			TSharedRef<FJsonObject> Records = MakeShared<FJsonObject>();
			TSharedRef<FJsonObject> Seconds = MakeShared<FJsonObject>();
			TArray<TSharedPtr<FJsonValue>> Errors, Problems;
			TArray<FRow> Rows;
			TSet<FString> Scripted;
			double Started = FPlatformTime::Seconds(), PhaseStarted = Started;
			FString Phase = TEXT("start");
			bool bCancelled = false, bCheckpointFailed = false, bInfrastructureFailure = false;
			FScopedSlowTask Progress;

			explicit FRun(const FOptions& InOptions) : Options(InOptions), Progress(1.f, FText::FromString(TEXT("Importing Blueprint plans")))
			{
				Result->SetNumberField(TEXT("started"), FDateTime::UtcNow().ToUnixTimestamp());
				Result->SetObjectField(TEXT("rows"), Records);
				Result->SetObjectField(TEXT("seconds"), Seconds);
			}

			void Error(const FString& Why)
			{
				Errors.Add(MakeShared<FJsonValueString>(Why));
				Result->SetStringField(TEXT("error"), Why);
			}

			FString Report()
			{
				Result->SetStringField(TEXT("phase"), Phase);
				Result->SetNumberField(TEXT("elapsed"), FPlatformTime::Seconds() - Started);
				Result->SetBoolField(TEXT("ok"), Errors.Num() == 0 && !bCancelled && Phase == TEXT("done"));
				Result->SetBoolField(TEXT("cancelled"), bCancelled);
				Result->SetArrayField(TEXT("errors"), Errors);
				Result->SetArrayField(TEXT("problems"), Problems);
				return Text(Result);
			}

			void NativeReport(FP4BlueprintImportRunResult* Out)
			{
				if (!Out) { return; }
				Out->bOk = Errors.Num() == 0 && !bCancelled && Phase == TEXT("done");
				Out->bCancelled = bCancelled;
				Out->bInfrastructureFailure = bInfrastructureFailure;
				Out->Count = Rows.Num();
				Out->Elapsed = FPlatformTime::Seconds() - Started;
				Out->Phase = Phase;
				for (const FRow& Row : Rows)
				{
					if (Row.bComplete) { Out->CompletedPackages.Add(FName(*Row.Package)); continue; }
					FString Reason;
					Row.Record->TryGetStringField(TEXT("preflight_error"), Reason);
					for (const auto& Pass : Row.Record->Values)
					{
						const auto Reply = Pass.Value->Type == EJson::Object ? Pass.Value->AsObject() : nullptr;
						bool Good = true; const TArray<TSharedPtr<FJsonValue>>* Lines = nullptr;
						if (Reason.IsEmpty() && Reply.IsValid() && Reply->TryGetBoolField(TEXT("ok"), Good) && !Good &&
							Reply->TryGetArrayField(TEXT("problems"), Lines))
						{
							TArray<FString> Messages; for (const auto& Line : *Lines) Messages.Add(Line->AsString());
							Reason = Pass.Key + TEXT(": ") + FString::Join(Messages, TEXT("; "));
						}
					}
					if (Reason.IsEmpty()) for (const auto& ErrorValue : Errors)
						if (ErrorValue->AsString().StartsWith(Row.Package + TEXT(":"))) { Reason = ErrorValue->AsString(); break; }
					if (Reason.IsEmpty()) Reason = TEXT("Blueprint batch stopped before this asset completed: ") +
						(Errors.Num() ? Errors[0]->AsString() : Phase);
					Out->PackageErrors.Add(Row.Package, Reason);
				}
				for (const TSharedPtr<FJsonValue>& ErrorValue : Errors) { Out->Errors.Add(ErrorValue->AsString()); }
				for (const TSharedPtr<FJsonValue>& Problem : Problems) { Out->Problems.Add(Problem->AsString()); }
				const TArray<TSharedPtr<FJsonValue>>* Preserved = nullptr;
				if (Result->TryGetArrayField(TEXT("preserved_dependencies"), Preserved))
				{
					for (const TSharedPtr<FJsonValue>& Package : *Preserved) { Out->PreservedDependencies.Add(Package->AsString()); }
				}
			}

			void Checkpoint()
			{
				if (Options.ResultFile.IsEmpty() || bCheckpointFailed) { return; }
				const FString File = FPaths::ConvertRelativePathToFull(Options.ResultFile);
				const FString Temporary = File + TEXT(".tmp");
				if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true)
					|| !FFileHelper::SaveStringToFile(Report(), *Temporary, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
					|| !IFileManager::Get().Move(*File, *Temporary, true, true))
				{
					bCheckpointFailed = true;
					bInfrastructureFailure = true;
					Error(TEXT("could not replace the import result file"));
				}
			}

			void Start(const TCHAR* Name)
			{
				Phase = Name;
				PhaseStarted = FPlatformTime::Seconds();
				Checkpoint();
			}
			void End() { Seconds->SetNumberField(Phase, FPlatformTime::Seconds() - PhaseStarted); Checkpoint(); }
			bool Boundary(const FRow& Row)
			{
				Progress.EnterProgressFrame(1.f, FText::FromString(Phase + TEXT(": ") + Row.Package));
				if (Progress.ShouldCancel()) { bCancelled = true; }
				return !bCancelled;
			}
			void Cleanup(int32 Number, int32 Interval)
			{
				if ((Number + 1) % Interval == 0) { CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS); Checkpoint(); }
			}

			bool Call(FRow& Row, const TCHAR* Name, FString (*Function)(const FString&))
			{
				if (Row.bFailed) return false;
				TSharedPtr<FJsonObject> Reply;
				const FString Json = Function(Row.File);
				if (!FJsonSerializer::Deserialize(TJsonReaderFactory<TCHAR>::Create(Json), Reply) || !Reply.IsValid())
				{
					Row.bFailed = true;
					Error(Row.Package + TEXT(" ") + Name + TEXT(": importer returned an invalid report"));
					return false;
				}
				Row.Record->SetObjectField(Name, Reply);
				const TArray<TSharedPtr<FJsonValue>>* Lines = nullptr;
				if (Reply->TryGetArrayField(TEXT("problems"), Lines))
				{
					for (const TSharedPtr<FJsonValue>& Line : *Lines)
					{
						Problems.Add(MakeShared<FJsonValueString>(Row.Package + TEXT(" ") + Name + TEXT(": ") + Line->AsString()));
					}
				}
				bool bOk = false;
				if (!Reply->TryGetBoolField(TEXT("ok"), bOk) || !bOk)
				{
					Row.bFailed = true;
					Error(Row.Package + TEXT(" ") + Name + TEXT(": import failed; see the per-pass report"));
					return false;
				}
				return true;
			}

			bool Read()
			{
				if (Options.Limit < 0) { Error(TEXT("the import limit must be nonnegative")); return false; }
				FString Json, Format;
				TSharedPtr<FJsonObject> Index;
				double Version = 0;
				const TArray<TSharedPtr<FJsonValue>>* Order = nullptr;
				if (!FFileHelper::LoadFileToString(Json, *FPaths::Combine(Options.PlansDirectory, TEXT("index.json")))
					|| !FJsonSerializer::Deserialize(TJsonReaderFactory<TCHAR>::Create(Json), Index) || !Index.IsValid()
					|| !Index->TryGetStringField(TEXT("format"), Format) || Format != TEXT("p4-blueprint-plans")
					|| !Index->TryGetNumberField(TEXT("version"), Version) || Version != 1
					|| !Index->TryGetArrayField(TEXT("order"), Order))
				{
					Error(TEXT("the plans folder must contain a p4-blueprint-plans version 1 index")); return false;
				}
				TArray<FRow> All;
				TMap<FString, int32> ByPackage;
				TArray<FString> Kinds, Matches, Pending;
				TArray<TSharedPtr<FJsonValue>> Excluded;
				Options.Kinds.ParseIntoArray(Kinds, TEXT(","), true);
				Options.Match.ParseIntoArray(Matches, TEXT(","), false);
				int32 Picked = 0;
				for (const TSharedPtr<FJsonValue>& Value : *Order)
				{
					const TSharedPtr<FJsonObject> Item = Value->Type == EJson::Object ? Value->AsObject() : nullptr;
					FRow Row;
					if (!Item.IsValid() || !Item->TryGetStringField(TEXT("package"), Row.Package)
							|| !Item->TryGetStringField(TEXT("kind"), Row.Kind) || !ReadParent(Item, Row.Parent)
						|| Row.Package.IsEmpty())
					{
						Error(FString::Printf(TEXT("index row %d (%s) must have package, kind and a string or null parent"), All.Num(), *Row.Package)); return false;
					}
					if (ByPackage.Contains(Row.Package)) { Error(Row.Package + TEXT(": duplicate package in the plan index")); return false; }
					Item->TryGetBoolField(TEXT("shell"), Row.bShell);
					Row.File = FPaths::Combine(Options.PlansDirectory, TEXT("import"), Row.Package.Mid(1) + TEXT(".json"));
					ByPackage.Add(Row.Package, All.Num());
					bool bMatch = Options.bExactSelection ? Options.Packages.Contains(Row.Package) : Options.Match.IsEmpty();
					if (!Options.bExactSelection)
					{
						for (const FString& Match : Matches) { bMatch |= Row.Package.Contains(Match, ESearchCase::CaseSensitive); }
					}
					if (!Row.Package.StartsWith(TEXT("/Game/")))
					{
						TSharedRef<FJsonObject> Refusal = MakeShared<FJsonObject>();
						Refusal->SetStringField(TEXT("package"), Row.Package);
						Refusal->SetStringField(TEXT("reason"), TEXT("engine and external-plugin packages are protected; only /Game plans are imported"));
						Excluded.Add(MakeShared<FJsonValueObject>(Refusal));
						if ((Options.bExactSelection || !Options.Match.IsEmpty()) && Kinds.Contains(Row.Kind) && bMatch)
						{
							Result->SetArrayField(TEXT("excluded"), Excluded);
							Error(Row.Package + TEXT(": explicit selection cannot import a protected mount")); return false;
						}
					}
					else if (Kinds.Contains(Row.Kind) && bMatch && (!Options.Limit || Picked < Options.Limit)) { Pending.Add(Row.Package); ++Picked; }
					All.Add(MoveTemp(Row));
				}
				Result->SetArrayField(TEXT("excluded"), Excluded);
				if (Options.bExactSelection)
				{
					for (const FString& Package : Options.Packages)
					{
						if (!ByPackage.Contains(Package)) { Error(Package + TEXT(": selected package is absent from the plan index")); return false; }
					}
				}
				const TSharedPtr<FJsonObject>* Needs = nullptr;
				Index->TryGetObjectField(TEXT("needs"), Needs);
				TSet<FString> Wanted, Preserved;
				while (Pending.Num())
				{
					const FString Package = Pending.Pop(false);
					const int32* Position = ByPackage.Find(Package);
					if (!Position || Wanted.Contains(Package) || Preserved.Contains(Package)) { continue; }
					if (!Package.StartsWith(TEXT("/Game/")))
					{
						if (!FPackageName::DoesPackageExist(Package)) { Error(Package + TEXT(": a required external package is unavailable")); return false; }
						continue;
					}
					// Unreal excludes its read-only roots from this validator by default. Validate only
					// the /Game rows we will touch, after protected-mount classification above.
					if (!FPackageName::IsValidLongPackageName(Package)) { Error(Package + TEXT(": selected import has an invalid Unreal package name")); return false; }
					// An editor selection may extend a project that already has its parents or graph
					// dependencies. Preserve their live objects (including edits); no save or provenance
					// update is authorized for these implicit roots. Explicit reimport still updates the closure.
					if (Options.bExactSelection && !Options.bReimport && !Options.Packages.Contains(Package)
						&& (FPackageName::DoesPackageExist(Package) || FindPackage(nullptr, *Package)))
					{
						const FRow& DependencyRow = All[*Position];
						FString Why, Name;
						const TSharedPtr<FJsonObject> Plan = P4Plan::Load(DependencyRow.File, Why);
						if (!Plan.IsValid()) { Error(Package + TEXT(": cannot validate existing dependency: ") + Why); return false; }
						if (DependencyRow.Kind == TEXT("LevelSequence"))
						{
							const TSharedPtr<FJsonObject>* Sequence = nullptr;
							if (Plan->TryGetObjectField(TEXT("sequence"), Sequence)) { (*Sequence)->TryGetStringField(TEXT("name"), Name); }
						}
						else { Plan->TryGetStringField(TEXT("name"), Name); }
						UObject* Asset = Name.IsEmpty() ? nullptr : LoadObject<UObject>(nullptr, *(Package + TEXT(".") + Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
						if (!Asset) { Error(Package + TEXT(": existing dependency package lacks its expected asset")); return false; }
						if (!UP4BlueprintImport::IsStandIn(Asset))
						{
							const bool bCompatible = DependencyRow.Kind == TEXT("LevelSequence") ? Asset->IsA<ULevelSequence>()
								: DependencyRow.Kind == TEXT("AnimBlueprint") ? Asset->IsA<UAnimBlueprint>()
								: DependencyRow.Kind == TEXT("WidgetBlueprint") ? Asset->IsA<UWidgetBlueprint>()
								: Asset->IsA<UBlueprint>() && !Asset->IsA<UAnimBlueprint>() && !Asset->IsA<UWidgetBlueprint>();
							if (!bCompatible) { Error(Package + TEXT(": existing dependency has an incompatible asset class")); return false; }
							Preserved.Add(Package);
							continue;
						}
					}
					Wanted.Add(Package);
					const FString& Parent = All[*Position].Parent;
					int32 Dot = INDEX_NONE;
					Parent.FindChar(TEXT('.'), Dot);
					Pending.Add(Dot == INDEX_NONE ? Parent : Parent.Left(Dot));
					const TArray<TSharedPtr<FJsonValue>>* Dependencies = nullptr;
					if (Needs && (*Needs)->TryGetArrayField(Package, Dependencies))
					{
						for (const TSharedPtr<FJsonValue>& Dependency : *Dependencies) { Pending.Add(Dependency->AsString()); }
					}
				}
				for (bool bShell : { true, false })
				{
					for (FRow& Row : All) { if (Wanted.Contains(Row.Package) && Row.bShell == bShell) { Rows.Add(MoveTemp(Row)); } }
				}
				TArray<TSharedPtr<FJsonValue>> PreservedPackages;
				for (const FRow& Row : All) { if (Preserved.Contains(Row.Package)) { PreservedPackages.Add(MakeShared<FJsonValueString>(Row.Package)); } }
				Result->SetArrayField(TEXT("preserved_dependencies"), PreservedPackages);
				const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
				if (Index->TryGetArrayField(TEXT("with_graphs"), Graphs)) { for (const TSharedPtr<FJsonValue>& Value : *Graphs) { Scripted.Add(Value->AsString()); } }
				Result->SetNumberField(TEXT("count"), Rows.Num());
				Progress.TotalAmountOfWork = FMath::Max(1, Rows.Num() * 9);
				if (!IsRunningCommandlet()) { Progress.MakeDialog(true); }
				// Read one plan at a time: channel-heavy plans can be large, and are never kept in a batch.
				Start(TEXT("preflight"));
				for (FRow& Row : Rows)
				{
					if (!Boundary(Row)) { return false; }
					FString Why, Package, Kind, Parent;
					const TSharedPtr<FJsonObject> Plan = P4Plan::Load(Row.File, Why);
					if (!Plan.IsValid() || !Plan->TryGetStringField(TEXT("package"), Package) || Package != Row.Package
						|| !Plan->TryGetStringField(TEXT("kind"), Kind) || Kind != Row.Kind
						|| !ReadParent(Plan, Parent) || Parent != Row.Parent)
					{
						Error(Why.IsEmpty() ? Row.Package + TEXT(": index and import plan disagree") : Why); return false;
					}
					if (!P4Reimport::PreflightPlan(Plan, Options.bReimport, Why))
					{
						Error(Row.Package + TEXT(": ") + Why);
						if (!Options.bKeepAbsentAsTheGames) return false;
						// A creator's dirty asset or an unsupported source member is a
						// per-asset refusal, not a broken index or result transport.
						Row.bFailed = true; Row.Record->SetStringField(TEXT("preflight_error"), Why);
					}
				}
				End();
				return Errors.Num() == 0 || Options.bKeepAbsentAsTheGames;
			}

			bool Declare(FRow& Row)
			{
				if (Row.Record->HasField(TEXT("declare"))) { return Ok(Row.Record, TEXT("declare")); }
				return Call(Row, TEXT("declare"), Row.Kind == TEXT("LevelSequence") ? UP4BlueprintImport::Sequence : UP4BlueprintImport::Declare);
			}

			bool Import()
			{
				Start(TEXT("prepare"));
				for (FRow& Row : Rows)
				{
					if (Row.bFailed) { Records->SetObjectField(Row.Package, Row.Record); continue; }
					FString Why;
					const auto Plan = P4Plan::Load(Row.File, Why);
					if (!Plan.IsValid() || !P4Reimport::BeginPlan(Plan, Options.bReimport, Why))
					{ Row.bFailed = true; Error(Row.Package + TEXT(": ") + Why); }
					Records->SetObjectField(Row.Package, Row.Record);
				}
				End();
				// Declare classes before values so cross-asset references can resolve.
				Start(TEXT("declare"));
				for (FRow& Row : Rows)
				{
					if (!Boundary(Row)) return false;
					if (!Row.bFailed) Declare(Row);
				}
				End();
				if (Options.AfterDeclaration)
				{
					Start(TEXT("declared class dependencies"));
					bCancelled |= Progress.ShouldCancel();
					if ((Errors.Num() && !Options.bKeepAbsentAsTheGames) || bCancelled)
					{ Error(TEXT("Class declarations did not complete before importing their dependencies.")); End(); return false; }
					TMap<FName, UClass*> Declared;
					for (const FRow& Row : Rows)
					{
						if (Row.bFailed) continue;
						if (Row.Kind == TEXT("LevelSequence")) continue;
						FString Why; const auto Plan = P4Plan::Load(Row.File, Why);
						const FString Name = Plan.IsValid() ? Plan->GetStringField(TEXT("name")) : FString();
						UBlueprint* Blueprint = Name.IsEmpty() ? nullptr : FindObject<UBlueprint>(nullptr, *(Row.Package + TEXT(".") + Name));
						if (!Ok(Row.Record, TEXT("declare")) || !IsValid(Blueprint) || !IsValid(Blueprint->GeneratedClass) ||
							Blueprint->GeneratedClass->ClassGeneratedBy != Blueprint ||
							Blueprint->GeneratedClass->GetOutermost()->GetFName() != FName(*Row.Package))
						{ Error(Row.Package + TEXT(": its successful generated class declaration is unavailable.")); End(); return false; }
						Declared.Add(FName(*Row.Package), Blueprint->GeneratedClass);
					}
					FString Why;
					const bool Ready = Options.AfterDeclaration(Declared, Why);
					bCancelled |= Progress.ShouldCancel();
					if (!Ready && !bCancelled) Error(Why.IsEmpty() ? TEXT("Declared class dependency import failed.") : Why);
					End();
					if (!Ready || bCancelled) return false;
				}
				Start(TEXT("signatures"));
				for (FRow& Row : Rows)
				{
					if (!Boundary(Row)) return false;
					if (Row.bFailed) continue;
					if (Row.Kind == TEXT("WidgetBlueprint") && !Call(Row, TEXT("widgets"), UP4BlueprintImport::Widgets)) continue;
					if (Scripted.Contains(Row.Package) && !Call(Row, TEXT("signatures"), UP4BlueprintImport::Signatures)) continue;
					if ((Row.Kind == TEXT("WidgetBlueprint") || (Row.Kind == TEXT("LevelSequence") && !Row.Parent.IsEmpty())))
						Call(Row, TEXT("events"), UP4BlueprintImport::WidgetEvents);
				}
				End();
				Start(TEXT("import assets"));
				for (FRow& Row : Rows)
				{
					if (!Boundary(Row)) return false;
					for (const FRow& Parent : Rows) if (Parent.bFailed &&
						Row.Parent.StartsWith(Parent.Package + TEXT(".")))
					{ Row.bFailed = true; Error(Row.Package + TEXT(": its parent import failed")); }
					if (Row.bFailed) continue;
					if (Row.Kind != TEXT("LevelSequence") || !Row.Parent.IsEmpty())
					{ if (!Call(Row, TEXT("values"), UP4BlueprintImport::Values)) continue; }
					if (Scripted.Contains(Row.Package) && !Call(Row, TEXT("graphs"), UP4BlueprintImport::Graphs)) continue;
					if (((Row.Kind == TEXT("WidgetBlueprint") && Scripted.Contains(Row.Package)) ||
						(Row.Kind == TEXT("LevelSequence") && !Row.Parent.IsEmpty())) &&
						!Call(Row, TEXT("signed"), UP4BlueprintImport::WidgetSignatures)) continue;
					if (Row.Kind == TEXT("AnimBlueprint") && !Call(Row, TEXT("anim"), UP4BlueprintImport::AnimGraphs)) continue;
					SaveRow(Row);
				}
				End();
				for (FRow& Row : Rows) if (!Row.bComplete && !Row.bPendingAnimationSeal)
				{ Row.Record->SetBoolField(TEXT("saved"), false); Row.Record->SetStringField(TEXT("status"), TEXT("unsaved")); }
				return Errors.Num() == 0 && !bCancelled;
			}

			void SaveRow(FRow& Row)
			{
				FString Why;
				const auto Plan = P4Plan::Load(Row.File, Why);
				const FString Name = Plan.IsValid() ? (Row.Kind == TEXT("LevelSequence")
					? Plan->GetObjectField(TEXT("sequence"))->GetStringField(TEXT("name")) : Plan->GetStringField(TEXT("name"))) : FString();
				UObject* Asset = Name.IsEmpty() ? nullptr : LoadObject<UObject>(nullptr, *(Row.Package + TEXT(".") + Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
				const bool Prepared = Asset && P4Reimport::PrepareImportedRecord(Asset, Row.File, Why);
				const bool WasIncomplete = Asset && P4BlueprintReconstructionIncomplete(Asset);
				const bool NeedsAnimationSeal = Row.Kind == TEXT("AnimBlueprint") && Plan.IsValid() && Plan->HasTypedField<EJson::Object>(TEXT("anim"));
				// Validate first, then serialize the completed state atomically with
				// its source record. Failed publication restores the live marker.
				if (Prepared && WasIncomplete && !NeedsAnimationSeal) P4Reimport::SetReconstructionIncomplete(Asset,false);
				const bool Saved = Prepared && Save(Row.Package);
				Row.Record->SetBoolField(TEXT("saved"), Saved);
				if (!Saved || !P4Reimport::FinishPackage(Row.Package, Row.File, Why))
				{ if (WasIncomplete) P4Reimport::SetReconstructionIncomplete(Asset,true); Row.bFailed = true; Error(Row.Package + TEXT(": asset was not saved: ") + Why); }
				else if (NeedsAnimationSeal)
				{ Row.bPendingAnimationSeal = true; Row.Record->SetStringField(TEXT("status"), TEXT("awaiting-animation-seal")); }
				else { Row.bComplete = true; Row.Record->SetStringField(TEXT("status"), TEXT("complete")); }
				Checkpoint();
			}

			void Persist()
			{
				// What failed to import before saving (a widget, say) does not stop the sealing of the
				// animation Blueprints that did import and save; what fails while saving does.
				const int32 ImportErrors = Errors.Num();
				for (FRow& Row : Rows) if (!Row.bComplete && !Row.bPendingAnimationSeal)
				{ Row.Record->SetBoolField(TEXT("saved"), false); Row.Record->SetStringField(TEXT("status"), TEXT("unsaved")); }
				// Completed assets have already saved individually. Save their reference placeholders.
				Start(TEXT("save"));
				const TArray<FString> StandIns = UP4BlueprintImport::TakeStandIns();
				TSet<FString> Selected;
				for (const FRow& Row : Rows) { Selected.Add(Row.Package); }
				TArray<TSharedPtr<FJsonValue>> Failed, TaggedPackages;
				Progress.TotalAmountOfWork += StandIns.Num();
				Result->SetNumberField(TEXT("stand_ins"), StandIns.Num());
				for (int32 Number = 0; Number < StandIns.Num(); ++Number)
				{
					const FString& Package = StandIns[Number];
					// A placeholder may have become a selected sequence during this run. Its real
					// asset was already saved with its completed row.
					if (Selected.Contains(Package)) { continue; }
					Progress.EnterProgressFrame(1.f, FText::FromString(TEXT("Saving stand-in (finishing persistence): ") + Package));
					FString Why;
					const bool bTagged = IsTaggedPackage(Package);
					if (bTagged && !P4Reimport::PrepareStandInSave(Package, Why))
					{
						P4RestorePendingStandIns({ Package });
						Error(Package + TEXT(": ") + Why);
					}
					else if (!Save(Package))
					{
						P4Reimport::CancelStandInSave(Package);
						P4RestorePendingStandIns({ Package });
						Failed.Add(MakeShared<FJsonValueString>(Package)); Error(Package + TEXT(": stand-in was not saved"));
					}
					else if (bTagged && !P4Reimport::FinishStandInPackage(Package, Why))
					{
						P4RestorePendingStandIns({ Package });
						Error(Package + TEXT(": ") + Why);
					}
					if ((Number + 1) % 250 == 0)
					{
						Result->SetArrayField(TEXT("stand_ins_not_saved"), Failed);
						Result->SetNumberField(TEXT("stand_ins_processed"), Number + 1);
						CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
						Checkpoint();
					}
				}
				Result->SetArrayField(TEXT("stand_ins_not_saved"), Failed);
				for (const FString& Package : UP4BlueprintImport::StandIns()) { TaggedPackages.Add(MakeShared<FJsonValueString>(Package)); }
				Result->SetArrayField(TEXT("tagged_stand_ins"), TaggedPackages);
				End();
				// Only a successfully rebuilt source animation may establish its initial content
				// guard. Save every selected dependency first, then require two cold states equal.
				Start(TEXT("seal source animations"));
				TArray<FString> Animations;
				for (const FRow& Row : Rows) if (Row.bPendingAnimationSeal) Animations.Add(Row.Package);
				FString SealError;
				bool Sealed = Errors.Num() == ImportErrors && !bCancelled && !bInfrastructureFailure;
				if (Animations.Num() && !Sealed) SealError = bCancelled ? TEXT("animation-source-seal-cancelled") : TEXT("animation-source-seal-skipped-after-import-failure");
				// Reload compiles redirected referencers too. Suppress compiler autosave for the
				// whole operation so only our explicit selected-package saves can publish assets.
				if (Animations.Num() && Sealed) P4Reimport::WithCompilerSavePolicy([&]() { Sealed = P4Reimport::SealAnimationPackages(Animations, SealError); });
				for (FRow& Row : Rows) if (Row.bPendingAnimationSeal)
				{
					Row.bPendingAnimationSeal = false;
					if (!Sealed)
					{
						Row.bComplete = false; Row.bFailed = true;
						Row.Record->SetBoolField(TEXT("saved"), false); Row.Record->SetStringField(TEXT("status"), TEXT("unsaved"));
					}
					else { Row.bComplete = true; Row.Record->SetBoolField(TEXT("saved"), true); Row.Record->SetStringField(TEXT("status"), TEXT("complete")); }
				}
				if (!SealError.IsEmpty()) Error(SealError);
				End();
				Phase = Errors.Num() ? TEXT("failed") : TEXT("done");
			}
		};
	}

	FString Run(const FOptions& Options, FP4BlueprintImportRunResult* NativeResult)
	{
		FRun Run(Options);
		if (!IsInGameThread() || !GEditor || bRunning)
		{
			Run.bInfrastructureFailure = true;
			Run.Error(TEXT("Blueprint import must run exclusively on the editor thread"));
			Run.Phase = TEXT("failed");
			Run.NativeReport(NativeResult);
			return Run.Report();
		}
		TGuardValue<bool> Guard(bRunning, true);
		P4WidgetMembers::FSourceScope WidgetSources(Options.ValidateWidgetSource);
		P4ImportPublication::FImportScope Import;
		P4KeepAbsentAsTheGames(Options.bKeepAbsentAsTheGames);
		if (Run.Read())
		{
			FString Why;
			if (!P4Reimport::BeginRun(Why)) { Run.bInfrastructureFailure = true; Run.Error(Why); Run.Phase = TEXT("failed"); }
			else
			{
				P4Reimport::WithCompilerSavePolicy([&]() { Run.Import(); });
				Run.Persist();
				if (Options.bKeepAbsentAsTheGames && !P4Reimport::DiscardIncompleteCreatedAssets(Why))
				{ Run.bInfrastructureFailure = true; Run.Error(Why); }
				P4Reimport::EndRun();
				if (Run.bCancelled) Run.Phase = TEXT("cancelled");
			}
		}
		else { Run.bInfrastructureFailure = !Run.bCancelled; Run.Phase = Run.bCancelled ? TEXT("cancelled") : TEXT("failed"); }
		const TMap<FString, FString> Kept = P4TakeKeptAsTheGames();
		P4KeepAbsentAsTheGames(false);
		TArray<TSharedPtr<FJsonValue>> KeptValues;
		for (const auto& Pair : Kept) KeptValues.Add(MakeShared<FJsonValueString>(Pair.Key));
		Run.Result->SetArrayField(TEXT("kept_as_the_games"), KeptValues);
		Run.Checkpoint();
		Run.NativeReport(NativeResult);
		if (NativeResult) NativeResult->KeptAsTheGames = Kept;
		return Run.Report();
	}
}

FString UP4BlueprintImport::Run(const FString& PlansDirectory, const FString& ResultFile, const FString& Kinds, const FString& Match, int32 Limit, bool Reimport)
{
	P4BlueprintRun::FOptions Options;
	Options.PlansDirectory = PlansDirectory;
	Options.ResultFile = ResultFile;
	Options.Kinds = Kinds;
	Options.Match = Match;
	Options.Limit = Limit;
	Options.bReimport = Reimport;
	return P4BlueprintRun::Run(Options);
}

FString UP4BlueprintImport::RunSelected(const FString& PlansDirectory, const FString& ResultFile, const TArray<FString>& Packages, bool Reimport)
{
	P4BlueprintRun::FOptions Options;
	Options.PlansDirectory = PlansDirectory;
	Options.ResultFile = ResultFile;
	Options.bExactSelection = true;
	for (const FString& Package : Packages) { Options.Packages.Add(Package); }
	Options.bReimport = Reimport;
	return P4BlueprintRun::Run(Options);
}

FP4BlueprintImportRunResult UP4BlueprintImport::RunSelectedNative(const FString& PlansDirectory, const FString& ResultFile, const TArray<FString>& Packages, bool Reimport, const TFunction<bool(const TMap<FName, UClass*>&, FString&)>& AfterDeclaration, bool KeepAbsentAsTheGames,const TFunction<bool(const FString&,const TSharedPtr<FJsonObject>&,FString&)>& ValidateWidgetSource)
{
	P4BlueprintRun::FOptions Options;
	Options.PlansDirectory = PlansDirectory;
	Options.ResultFile = ResultFile;
	Options.bExactSelection = true;
	for (const FString& Package : Packages) { Options.Packages.Add(Package); }
	Options.bReimport = Reimport;
	Options.AfterDeclaration = AfterDeclaration;
	Options.ValidateWidgetSource = ValidateWidgetSource;
	Options.bKeepAbsentAsTheGames = KeepAbsentAsTheGames;
	FP4BlueprintImportRunResult Result;
	P4BlueprintRun::Run(Options, &Result);
	return Result;
}
