#include "core/light_function_export.h"
#include <iostream>
using namespace P4MapKit::Export;
#define CHECK(X) do { if (!(X)) { std::cerr << "Failed at " << __LINE__ << ": " << #X << '\n'; return 1; } } while (false)
int main() {
    MapEdit Map; Map.Package = "/Game/Carrot4/Maps/Cave000_F00_P";
    Descriptor Mod{"test.lightfunction", "Light function", "1.0.0"};
    std::string Script, Error;
    CHECK(DirectionalLightFunctionRuntime(Mod, {Map}, Script, Error));
    CHECK(Script.find("[\"Cave000_F00_P\"]=true") != std::string::npos);
    const auto Before = Script;
    Map.Package = "/Game/Bad\"Map";
    CHECK(!DirectionalLightFunctionRuntime(Mod, {Map}, Script, Error));
    CHECK(Script == Before);
    const std::string File = "romfs/tsuchi/mods/test.lightfunction/main.lua";
    CHECK(!AllowedLightFunctionFile(Mod, File, false, Error));
    CHECK(AllowedLightFunctionFile(Mod, File, true, Error));
    CHECK(!AllowedLightFunctionFile(Mod, "romfs/tsuchi/mods/other/main.lua", true, Error));
    CHECK(!AllowedLightFunctionFile(Mod, "romfs/tsuchi/mods/test.lightfunction/main.js", true, Error));
    CHECK(!AllowedLightFunctionFile(Mod, "romfs/tsuchi/mods/test.lightfunction/extra.lua", true, Error));
    // Without named maps the runtime is scoped by the lights it finds, never by world names.
    std::string Anywhere;
    CHECK(DirectionalLightFunctionRuntime(Mod, {}, Anywhere, Error));
    CHECK(Anywhere.find("local any_map=true") != std::string::npos);
    CHECK(Anywhere.find("Title_P") == std::string::npos && Anywhere.find("'Entry'") == std::string::npos);
    CHECK(Anywhere.find("world_has_owned_light()") != std::string::npos);
    std::cout << Before;
}
