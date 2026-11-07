-- Real Lua exercises the generated map scope, channel holds and proxy refresh.
local file=assert(io.open(arg[1],'r'));local source=file:read('*a');file:close()
local root=assert(source:match('local root="([^"]+)"'))
local dir,lf='r.C4.DirectionalLightShadowMapChannel','r.C4.LightFunctionShadowMapChannel'
local values={[dir]=-1,[lf]=1}
local owned,blocked={},{}
local callback,world,holds,releases,logs,toggles=nil,nil,0,0,0,0
local material={path=root..'MI_Function.MI_Function'}
local light={path=root..'Level:PersistentLevel.Sun.Component',directional=true,visible=true,material=material}
local mid={path=root..'Level:PersistentLevel.OtherSun.Component',directional=true,visible=true,material={path='/Engine/Transient.MID',Parent=material}}
local hidden={directional=true,visible=false,material=material}
local foreign={directional=true,visible=true,material={path='/Game/Mods/other/M.M'}}
local spot={directional=false,visible=true,material=material}
local lights={light,mid,hidden,foreign,spot}
for _,o in ipairs(lights) do
 o.bUsedAsAtmosphereSunLight=false;o.AtmosphereSunLightIndex=1
 o.bC4Param=false;o.C4Param7=1;o.ShadowAmount=.75
end
time={on_step=function(fn) callback=fn end}
game={world=function() return world end}
actor={list=function() return {'actor'} end}
component={list=function() return lights end}
reflection={
 console_variable=function(key) return values[key] end,
 hold_console_variable=function(key,new)
  assert((key==dir and (new==2 or new==1)) or (key==lf and new==1))
  if blocked[key] then return false,'busy' end
  if owned[key]==nil then owned[key]=values[key] end
  holds=holds+1;values[key]=new;return true
 end,
 release_console_variable=function(key)
  if owned[key]==nil then return false,'not_found' end
  releases=releases+1;values[key]=owned[key];owned[key]=nil;return true
 end,
 find_class=function(name) assert(name=='DirectionalLightComponent');return name end,
 is_a=function(o) return o.directional end,
 path=function(o) return o.path end,
 get=function(o,name)
  if name=='LightFunctionMaterial' then return o.material end
  if name=='bVisible' then return o.visible end
  if name=='Parent' then return o.Parent end
  if o[name]~=nil then return o[name] end
  error(name)
 end,
 set=function(o,name,value)
  assert(o==light or o==mid or o==lights[6])
  assert(name=='bC4Param' or name=='C4Param7');o[name]=value;return true
 end,
 call=function(o,name,args)
  assert(o==light or o==mid or o==lights[6])
  if name=='SetAtmosphereSunLight' then o.bUsedAsAtmosphereSunLight=args.bNewValue;return end
  if name=='SetAtmosphereSunLightIndex' then o.AtmosphereSunLightIndex=args.NewValue;return end
  assert(name=='SetVisibility')
  assert(args.bNewVisibility~=o.visible and args.bPropagateToChildren==false)
  toggles=toggles+1;o.visible=args.bNewVisibility;return nil,{}
 end,
}
mod={log=function() logs=logs+1 end}
assert(load(source))()
local function step() for i=1,30 do callback() end end
local function exit() world='Area001_P';step() end
step();assert(holds==0)
world='Title_P';step();assert(holds==0)
exit();assert(holds==0)
world='Cave000_F00_P';step()
assert(holds==1 and values[dir]==2 and values[lf]==1 and toggles==4)
assert(light.visible and mid.visible and not hidden.visible and foreign.visible and spot.visible)
assert(light.bUsedAsAtmosphereSunLight and light.AtmosphereSunLightIndex==0 and light.bC4Param and light.C4Param7==.75)
assert(mid.bC4Param and not hidden.bC4Param and not foreign.bC4Param and not spot.bC4Param)
step();assert(holds==1 and toggles==4)
local replacement={};for key,value in pairs(light) do replacement[key]=value end
light=replacement;lights[1]=light
step();assert(holds==1 and toggles==4)
values[lf]=3;step();assert(toggles==8 and holds==1)
exit();assert(releases==1 and values[dir]==-1 and values[lf]==3)
values[dir]=2;blocked[dir]=true
world='Cave000_F00_P';step();assert(holds==1 and toggles==12)
exit();assert(releases==1 and values[dir]==2)
values[dir]=-1
world='Cave000_F00_P';step();step();assert(logs==1 and holds==1 and toggles==12)
blocked[dir]=false;step();assert(holds==2 and values[dir]==2 and toggles==16)
exit();assert(values[dir]==-1)
values[lf]=-1;blocked[dir]=true
world='Cave000_F00_P';step()
assert(values[lf]==-1 and values[dir]==-1 and next(owned)==nil)
local before=logs;step();assert(logs==before)
blocked[dir]=false;step();assert(values[lf]==1 and values[dir]==2 and toggles==20)
exit();assert(values[lf]==-1 and values[dir]==-1)
world='Cave000_F00_P';step();assert(values[dir]==2)
owned={};values[dir]=-1;values[lf]=1
step();assert(values[dir]==2 and toggles==28)
world=nil;step();assert(values[dir]==-1 and values[lf]==1)
world='Cave000_F00_P';step()
local late={path=root..'Level:PersistentLevel.LateSun.Component',directional=true,visible=true,material=material,bUsedAsAtmosphereSunLight=false,AtmosphereSunLightIndex=1,bC4Param=false,C4Param7=1,ShadowAmount=.5}
lights[6]=late
local before_toggles=toggles;step()
assert(late.bC4Param and late.bUsedAsAtmosphereSunLight and late.AtmosphereSunLightIndex==0 and late.C4Param7==.5)
assert(toggles==before_toggles+2)
step();assert(toggles==before_toggles+2)
values[lf]=2;step();assert(values[dir]==1)
world=nil;step();assert(values[dir]==-1 and values[lf]==2)
-- Without named maps the runtime is active wherever one of the mod's light functions is on a light.
local anywhere=source:gsub('local maps=%b{}','local maps={}',1):gsub('local any_map=false','local any_map=true',1)
assert(anywhere~=source)
values[dir],values[lf]=-1,1;owned={};blocked={}
lights={foreign,spot}
assert(load(anywhere))()
world='Title_P';step();assert(values[dir]==-1 and next(owned)==nil)
world='Area001_P';step();assert(values[dir]==-1 and next(owned)==nil)
lights[3]=light;step();assert(values[dir]==2)
lights[3]=nil;step();assert(values[dir]==-1 and next(owned)==nil)
print('Directional light-function runtime lifecycle and owned proxy refresh passed')
