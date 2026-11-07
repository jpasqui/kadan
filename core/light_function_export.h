#pragma once
#include "export_contract.h"

namespace P4MapKit { namespace Export {
// Only an export that produced the fixed runtime may admit this extra file.
// The generic export contract continues to refuse arbitrary script files.
inline bool AllowedLightFunctionFile(const Descriptor& Mod, const std::string& Relative,
    bool GeneratedRuntime, std::string& Error) {
    if (GeneratedRuntime && Relative == "romfs/tsuchi/mods/" + Mod.Id + "/main.lua") {
        Names Naming;
        if (!DerivedNames(Mod, Naming, Error)) return false;
        Error.clear(); return true;
    }
    return AllowedFile(Mod, Relative, Error);
}
// Forward projection requires an allocated light shadow channel even though the
// directional light function has its own separate sampling channel. Keep that
// sampling channel valid: -1 clears the proxy's light-function sampling mask.
inline bool DirectionalLightFunctionRuntime(const Descriptor& Mod, const std::vector<MapEdit>& Maps,
    std::string& Script, std::string& Error) {
    Names Naming;
    if (!DerivedNames(Mod, Naming, Error)) return false;
    std::string Selected;
    for (const auto& Map : Maps) {
        if (!PackageName(Map.Package)) { Error = "Invalid light-function map"; return false; }
        const auto Name = Map.Package.substr(Map.Package.rfind('/') + 1);
        Selected += "[" + Quote(Name) + "]=true,";
    }
    const std::string Prepared =
        "-- Kadan: allocate a directional shadow channel for creator light functions.\n"
        "local maps={" + Selected + "}\n"
        "local root=" + Quote(Naming.Root + "/") + "\n"
        "local any_map=" + (Maps.empty() ? "true" : "false") + "\n"
        R"lua(local dir='r.C4.DirectionalLightShadowMapChannel'
local lf='r.C4.LightFunctionShadowMapChannel'
local held,problem,frames,refreshed,last_lf={},nil,0,nil,nil
local prepared={}
local function release()
 for key in pairs(held) do reflection.release_console_variable(key) end
 held={}
end
local function owns(material)
 for depth=1,16 do
  if not material then return false end
  local path=reflection.path(material)
  if type(path)=='string' and path:sub(1,#root)==root then return true end
  material=reflection.get(material,'Parent')
 end
 return false
end
local function owned_light(c,kind)
 return reflection.is_a(c,kind) and owns(reflection.get(c,'LightFunctionMaterial')) and reflection.get(c,'bVisible')==true
end
-- Without named maps the mod is active wherever one of its light functions is on a directional light.
local function world_has_owned_light()
 local kind=reflection.find_class('DirectionalLightComponent')
 if not kind then return false end
 for _,a in ipairs(actor.list() or {}) do
  for _,c in ipairs(component.list(a) or {}) do
   if owned_light(c,kind) then return true end
  end
 end
 return false
end
local function refresh(force)
 local kind=reflection.find_class('DirectionalLightComponent')
 if not kind then return end
 for _,a in ipairs(actor.list() or {}) do
  for _,c in ipairs(component.list(a) or {}) do
   if owned_light(c,kind) then
    local key=reflection.path(c)
    local changed=force or not prepared[key]
    -- C4 view uniforms read the function mask from atmosphere sun 0, and
    -- its proxy returns that mask only when the game's C4 parameters are on.
    if reflection.get(c,'bUsedAsAtmosphereSunLight')~=true then
     reflection.call(c,'SetAtmosphereSunLight',{bNewValue=true});changed=true
    end
    if reflection.get(c,'AtmosphereSunLightIndex')~=0 then
     reflection.call(c,'SetAtmosphereSunLightIndex',{NewValue=0});changed=true
    end
    if reflection.get(c,'bC4Param')~=true then
     reflection.set(c,'bC4Param',true);changed=true
    end
    local strength=reflection.get(c,'ShadowAmount')
    if type(strength)=='number' and reflection.get(c,'C4Param7')~=strength then
     reflection.set(c,'C4Param7',strength);changed=true
    end
    if changed then
     reflection.call(c,'SetVisibility',{bNewVisibility=false,bPropagateToChildren=false})
     reflection.call(c,'SetVisibility',{bNewVisibility=true,bPropagateToChildren=false})
    end
    prepared[key]=true
   end
  end
 end
end
time.on_step(function()
 frames=frames+1
 if frames%30~=0 then return end
 local world=game.world()
 local active=maps[world] or (any_map and type(world)=='string' and world~='' and world_has_owned_light())
 if active then
  local changed,why=false,nil
  local value=reflection.console_variable(lf)
  if type(value)~='number' then why='Light-function channel is unavailable'
  elseif value<0 or value>=4 then
   local ok,reason=reflection.hold_console_variable(lf,1)
   if ok then held[lf],changed,value=true,true,1 else why=tostring(reason) end
  end
  -- The native allocator excludes channel 3 for directional lights.
  local channel=value==2 and 1 or 2
  if not why and reflection.console_variable(dir)~=channel then
   local ok,reason=reflection.hold_console_variable(dir,channel)
   if ok then held[dir],changed=true,true else why=tostring(reason) end
  end
  if why then
   release()
   if problem~=why then
    problem=why;mod.log('Directional light functions could not allocate their shadow channel: '..why)
   end
  else
   refresh(changed or refreshed~=world or last_lf~=value)
   refreshed,last_lf,problem=world,value,nil
  end
 else
  release();refreshed,last_lf,problem=nil,nil,nil;prepared={}
 end
end)
)lua";
    Script = Prepared; Error.clear(); return true;
}
}}
