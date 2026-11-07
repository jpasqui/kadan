#pragma once

class IDesktopPlatform;

IDesktopPlatform* P4KadanPathPicker();

#if P4_KADAN_TEST_SEAMS || P4_MAPKIT_TESTS
KADAN_API void P4KadanUnavailablePathPickerForTest(bool Unavailable);
#endif
