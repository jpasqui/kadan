#pragma once
#include "CoreMinimal.h"

// Connect to the preview game through the Dev Server. Play restarts the current editor map;
// Stop discards unkept live copies and pauses the preview. Connection status is in Kadan's header.
// The console command Kadan.GameConnection [sync] [play|stop|keep] [selftest] opens the panel and can try it once,
// logging each result as "Kadan self-test".
void P4RegisterGameConnection();
void P4UnregisterGameConnection();
void P4OpenGameConnection();
TSharedRef<class SWidget> P4GameConnectionMenuHeader();

// The game's placement number of a level-placed actor (its GenerateInfo.DebugUniqueId), or 0.
uint64 P4GamePlacementOf(const AActor* Actor);
