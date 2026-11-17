#if P4_MAPKIT_TESTS
#include "Misc/AutomationTest.h"
#include "P4KadanPanelImportPage.h"

// The Import page's search finds text anywhere in a package path, and * and ? stand for any text and one character.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ImportSearchWildcardTest, "P4MapKit.Kadan.ImportSearchWildcard",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ImportSearchWildcardTest::RunTest(const FString&)
{
    const FString Soil = TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave000/Textures/T_Cave000_Soil_D");
    const FString Moss = TEXT("/Game/Carrot4/Material/stg/MI_stg_Brick_Moss01_Soft");
    TestTrue(TEXT("An empty search shows everything"), P4KadanSearchMatches(Soil, TEXT("")));
    TestTrue(TEXT("Plain text matches anywhere"), P4KadanSearchMatches(Soil, TEXT("cave000")));
    TestFalse(TEXT("Plain text that is not there does not match"), P4KadanSearchMatches(Soil, TEXT("Moss")));
    TestTrue(TEXT("* stands for any text"), P4KadanSearchMatches(Soil, TEXT("Cave0*Soil")));
    TestTrue(TEXT("* matches in the middle of a path, ignoring case"), P4KadanSearchMatches(Moss, TEXT("mi_*_moss")));
    TestFalse(TEXT("* keeps the order of the parts"), P4KadanSearchMatches(Soil, TEXT("Soil*Cave0")));
    TestTrue(TEXT("? stands for one character"), P4KadanSearchMatches(Moss, TEXT("Moss0?_Soft")));
    TestFalse(TEXT("? is exactly one character"), P4KadanSearchMatches(Moss, TEXT("Moss?_Soft")));
    TestTrue(TEXT("Spaces around the search are ignored"), P4KadanSearchMatches(Moss, TEXT(" Brick*Soft ")));
    TestFalse(TEXT("? does not stand for no character"), P4KadanSearchMatches(Moss, TEXT("Moss01?_Soft")));
    TestTrue(TEXT("? matches at the end of the path"), P4KadanSearchMatches(Moss, TEXT("_Sof?")));
    TestTrue(TEXT("Several * in a row are one"), P4KadanSearchMatches(Soil, TEXT("Cave000***Soil")));
    TestTrue(TEXT("* can stand for no text"), P4KadanSearchMatches(Soil, TEXT("Cave000*/Textures")));
    TestTrue(TEXT("* and ? together"), P4KadanSearchMatches(Moss, TEXT("stg/*Moss??_*t")));
    TestFalse(TEXT("A search longer than the path does not match"), P4KadanSearchMatches(TEXT("ab"), TEXT("a?b")));
    // A search with many * against a long path that almost matches answers at once.
    const FString Long = FString::ChrN(20000, TCHAR('a'));
    const double Started = FPlatformTime::Seconds();
    TestFalse(TEXT("Many * against a long run of the same letter does not match"),
        P4KadanSearchMatches(Long, TEXT("*a*a*a*a*a*a*a*b")));
    TestTrue(TEXT("Many * against a long run of the same letter matches when it is there"),
        P4KadanSearchMatches(Long + TEXT("b"), TEXT("*a*a*a*a*a*a*a*b")));
    const double Took = FPlatformTime::Seconds() - Started;
    TestTrue(FString::Printf(TEXT("The searches answer promptly (%.3f s)"), Took), Took < 1.0);
    return !HasAnyErrors();
}
#endif
