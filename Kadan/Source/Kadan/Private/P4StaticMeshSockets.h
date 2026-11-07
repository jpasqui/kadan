#pragma once
#include "P4OriginalMeshStaging.h"

class UStaticMesh;

KADAN_API bool P4ReadOriginalMeshSockets(const P4MapKit::FPackageInventory& Package, const std::vector<uint8>& Body,
                                         int32 MeshIndex, const std::vector<int32>& Indices,
                                         TArray<FP4OriginalMeshSocket>& Out, FString& Error);
bool P4BuildOriginalMeshSockets(UStaticMesh* Mesh, const TArray<FP4OriginalMeshSocket>& Source, FString& Error);
