#include "P4TestFixtureRelease.h"
#include "Misc/AutomationTest.h"

#if P4_MAPKIT_TESTS
// After every automation test the undo buffer is cleared. A test's cleanup kills the assets it made, and an undo step
// the test recorded keeps a killed material instance alive past its parent (P4TestFixtureRelease.h). The tests found
// doing so clear the buffer themselves before they kill. This covers the ones nobody has found: without it their
// instance waits for a garbage collection in some later test, and the editor stops in whichever test compiles a
// material after that. Which test that is changes with the order of the run.
void P4StartUndoResetAfterTests()
{
    FAutomationTestFramework::Get().PostTestingEvent.AddStatic(&P4ForgetUndoOfTestFixtures);
}
#else
void P4StartUndoResetAfterTests() {}
#endif
