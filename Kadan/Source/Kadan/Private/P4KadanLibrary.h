#pragma once
#include "CoreMinimal.h"

struct FP4KadanImportSettings;

// The game assets Kadan has imported on this computer, kept for every project. The game's files do not change, so
// a texture, material or Blueprint built in one project is the same in the next: a resumed import takes it from
// here instead of building it again. Only game assets go in, never a creator's, and never a level (an import
// always builds its levels). A library belongs to one set of game containers and one Kadan build, so an update
// starts a new one. On by default; P4_KADAN_LIBRARY=0 turns it off, and automation tests use it only with
// P4_KADAN_LIBRARY=1, so a test of an import still imports. P4_KADAN_LIBRARY_FOLDER names the folder instead of the
// one for this build, for work on Kadan itself.
class FP4KadanLibrary
{
public:
    explicit FP4KadanLibrary(const FP4KadanImportSettings& Settings);
    bool Enabled() const { return !Root.IsEmpty(); }
    const FString& Folder() const { return Root; }

    // Copies the package into the project, with every game package it needs by hard reference that the project
    // does not have. True when the project now has the package. Nothing is copied unless the whole closure is
    // in the project or the library; a package that would need one that is not imports the usual way.
    bool Seed(const FString& Package);
    // Files copied so far, for the asset registry to scan once.
    TArray<FString> TakeSeededFiles();
    int32 SeededPackages() const { return Seeded.Num(); }

    // Keeps the run's newly built game packages (package and kind), unless the library has them already.
    int32 Publish(const TMap<FString, FString>& Imported, TArray<FString>& Warnings);

private:
    FString Entry(const FString& Package) const;
    bool InProject(const FString& Package) const;
    bool Closure(const FString& Package, TArray<FString>& Missing, TSet<FString>& Visiting);
    bool ReadDependencies(const FString& Package, TArray<FString>& Out) const;

    FString Root;
    TSet<FString> Seeded, Refused;
    TArray<FString> SeededFiles;
};
