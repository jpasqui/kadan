#pragma once
#include "Editor.h"

// Called before a test kills the assets it made. Assigning a material to a mesh is an undo step, so the editor's undo
// buffer references the material instance the test made, and it keeps a killed object alive. Garbage collection then
// destroys the killed parent material while the killed instance lives on, with the destroyed parent still set on the
// instance's proxy on the rendering thread. The engine follows that pointer for every proxy when a shader compile
// finishes, and the editor stopped in whichever later test compiled a material first. The editor's own delete clears
// the undo buffer for the same reason (ObjectTools). P4_NATIVE_PARENT_PROBE=1 names such an instance after each test
// (P4MaterialParentProbeAutomation.cpp).
inline void P4ForgetUndoOfTestFixtures()
{
    if (GEditor)
        GEditor->ResetTransaction(FText::FromString(TEXT("Kadan test fixtures released")));
}
