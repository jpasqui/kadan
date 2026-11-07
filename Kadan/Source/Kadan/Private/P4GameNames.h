#pragma once
#include "P4MapKitCore/mod_folders.h"

// The game's names that Kadan needs as a whole: its project name, the script package of its native
// classes, the folder of its imported assets, its content folder and container files in the Paks
// folder, and the file of its shader library. Kadan's own preview packages have their root here too.
// They are narrow literals, so they join other literals in TEXT() and in std::string comparisons.
#define P4_GAME_PROJECT "Carrot4"
#define P4_GAME_SCRIPT_PACKAGE "/Script/" P4_GAME_PROJECT
#define P4_GAME_ASSET_ROOT "/Game/" P4_GAME_PROJECT
#define P4_GAME_CONTENT_FOLDER P4_GAME_PROJECT "/Content"
#define P4_GAME_CONTAINER P4_GAME_PROJECT "-Switch"
#define P4_GAME_SHADER_ARCHIVE "ShaderArchive-" P4_GAME_PROJECT "-GLSL_SWITCH.ushaderbytecode"
// Any build's: "ShaderArchive-Carrot4-GLSL_SWITCH", "ShaderArchive-Carrot4_Chunk0-SF_GLSL_SAGE", ...
#define P4_GAME_SHADER_ARCHIVE_ANY_FORMAT "ShaderArchive-" P4_GAME_PROJECT "*.ushaderbytecode"
#define P4_PREVIEW_PACKAGE_ROOT P4_PREVIEW_FOLDER "/" P4_PREVIEW_MOD_ID_PREFIX
