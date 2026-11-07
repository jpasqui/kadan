#include "P4GameNativeTails.h"
#include "Misc/AutomationTest.h"
#include "UObject/Class.h"
#include "UObject/UObjectIterator.h"

#if WITH_DEV_AUTOMATION_TESTS
// The reader registry picks the reader each class is meant to have, for every class of this editor: the engine's
// and every native class the game declares.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKadanNativeTailReaders, "P4MapKit.Kadan.NativeTailReaders",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FKadanNativeTailReaders::RunTest(const FString&)
{
    const auto Old = [](const FString& Class)
    {
        if (Class == TEXT("/Script/Carrot4.FoliageGridDataAsset"))
            return 2;
        static const TSet<FString> Engine{TEXT("/Script/Engine.VectorFieldStatic"), TEXT("/Script/Engine.Font"),
            TEXT("/Script/Engine.FontFace"), TEXT("/Script/AkAudio.AkAssetDataWithMedia"),
            TEXT("/Script/AkAudio.AkAudioEventData"), TEXT("/Script/AkAudio.AkAssetPlatformData")};
        return Engine.Contains(Class) ? 1 : 0;
    };
    const auto New = [](const FString& Class)
    {
        const FP4NativeTailReader* Reader = P4FindNativeTailReader(Class);
        if (!Reader)
            return 0;
        // The vector field's engine class holds its data; the foliage grid's is kept for the cook.
        return Reader->Kept ? 2 : 1;
    };
    int32 Classes = 0, OldRead = 0, NewRead = 0, Game = 0;
    for (TObjectIterator<UClass> Class; Class; ++Class)
    {
        const FString Path = Class->GetPathName();
        ++Classes;
        Game += Path.StartsWith(TEXT("/Script/Carrot4.")) ? 1 : 0;
        OldRead += Old(Path) ? 1 : 0;
        NewRead += New(Path) ? 1 : 0;
        if (Old(Path) != New(Path))
            AddError(TEXT("The rules pick different readers for ") + Path);
    }
    AddInfo(FString::Printf(
        TEXT("%d classes (%d of the game's): old rule reads %d, new rule reads %d"), Classes, Game, OldRead, NewRead));
    TestTrue(TEXT("The game's classes are declared"), Game > 1000);
    TestEqual(TEXT("Both rules read the same classes"), NewRead, OldRead);
    TestEqual(
        TEXT("Every registered reader is for a class this editor has"), NewRead, int32(P4NativeTailReaders().Num()));
    return !HasAnyErrors();
}
#endif
