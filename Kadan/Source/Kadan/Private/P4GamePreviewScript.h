#pragma once
// Public script API controller, embedded so installed plugins need no extra runtime files.
namespace P4Kadan
{
static const TCHAR* GamePreviewScript = TEXT(R"KadanLua(
-- Kadan's preview controller, installed in the Dev Server's persistent console.
-- Uses only the public script API and the game's reflected functions.
local P = {version = 12, state = "editing", paths = {}}
local owner, manager, instance, statics, pause, baseline
-- The last of the game's own worlds seen loaded, where a preview area is left for.
local home
local target, previous_captain, request_number
local step, touched, started = 0, 0, 0
-- The step until which the owner holds its lease while it prepares (P.prepare).
local held = 0
-- A preparation's hold lasts at most this many steps, the editor's own preparation deadline at 30 steps a second.
local most_held = 36000
-- A map load that never finishes must not leave the controller "starting" for good.
local load_steps = 7200
local subscription
local ordinary = {}
local ordinary_tokens = {}
local save_fields = {"Story", "Quest", "Prepo", "FoW", "Gen", "Player"}

local function invoke(object, name, args)
    local value, outputs = reflection.call(object, name, args or {})
    if type(outputs) == "string" then error(name .. ": " .. outputs) end
    return value
end

local function connect()
    statics = statics or assert(reflection.find_class("GameplayStatics"), "GameplayStatics is unavailable")
    local context = player.captain(0)
    if not context then
        local actors = actor.list()
        context = actors and actors[1]
    end
    if not context then error("The preview game has no loaded world yet.") end
    instance = assert(invoke(statics, "GetGameInstance", {WorldContextObject = context}), "The game instance is unavailable.")
    manager = assert(reflection.get(instance, "GameSceneMgr"), "The game's scene manager is unavailable.")
end

local function resume()
    if pause then
        local done, why = game.release_pause(pause)
        if not done and why ~= "not_found" then error("Could not resume the preview: " .. tostring(why)) end
        pause = nil
    end
end

local function hold()
    if not pause then
        pause = assert(game.hold_pause({timer = true, actors = true, controller = true, player = true,
            pikmin = true, creatures = true, events = true, flow = true}), 'The game could not pause the preview.')
    end
end

-- Fixed names, so a crashed editor never leaves a growing set of slots in the game's save data.
local function slot_name(field)
    return "KadanPreview_" .. field
end

local function discard_baseline()
    if baseline then
        for _, row in ipairs(baseline) do
            if row.slot then pcall(invoke, statics, "DeleteGameInSlot", {SlotName = row.slot, UserIndex = 0}) end
        end
    end
    baseline = nil
end

local function capture_baseline()
    if baseline then return end
    -- The starting state goes back through the game's own copy and apply (save.restore); setting new save objects
    -- left the game's places in the old ones, which the engine then freed.
    if type(save) ~= "table" or type(save.restore) ~= "function" then
        error("Update Tsuchi to restore the preview's starting state.")
    end
    local rows = {}
    for _, field in ipairs(save_fields) do
        local object = reflection.get(instance, "CarrotSaveGames." .. field)
        local row = {field = field}
        rows[#rows + 1] = row
        if object then
            row.slot = slot_name(field)
            if not invoke(statics, "SaveGameToSlot", {SaveGameObject = object, SlotName = row.slot, UserIndex = 0}) then
                baseline = rows
                discard_baseline()
                error("Could not capture the preview's " .. field .. " starting state.")
            end
        end
    end
    baseline = rows
end

-- Each saved copy goes into the game's current save of its kind, in the order of save_fields: the story before the
-- quests, whose apply finds the current area's records in the story again.
local function restore_baseline()
    if not baseline then return end
    local restored = {}
    for _, row in ipairs(baseline) do
        if row.slot then
            restored[#restored + 1] = {field = row.field,
                copy = assert(invoke(statics, "LoadGameFromSlot", {SlotName = row.slot, UserIndex = 0}),
                    "Could not restore the preview's " .. row.field .. " starting state.")}
        end
    end
    for _, row in ipairs(restored) do
        local done, why = save.restore(row.copy)
        if not done then error("Could not restore the preview's " .. row.field .. " starting state: " .. tostring(why)) end
    end
end

local function claim(who)
    if type(who) ~= "string" or not who:match("^[a-fA-F0-9]+$") or #who ~= 32 then error("Invalid preview session.") end
    if owner and owner ~= who then error("Another editor owns this preview game.") end
    owner, touched = who, step
end

local function expire_start()
    if P.state == "starting" and step - started > load_steps then P.state = "editing" end
end

-- Whether a world is one of this editor's reserved preview areas.
local function preview_world(world)
    for _, reservation in pairs(kadan_preview_scenes or {}) do
        local ok, info = pcall(game.preview_scene, reservation)
        if ok and info and info.world_name == world then return true end
    end
    return false
end

function P.status(who)
    if owner == who then touched = step end
    local world = game.world()
    if world and world ~= "" and game.has_control() and not preview_world(world) then home = world end
    local captain = player.captain(0)
    if P.state == "starting" and game.world() == target and game.has_control()
        and captain and tostring(captain) ~= previous_captain then P.state = "playing" end
    expire_start()
    return {state = P.state, world = game.world(), target = target, home = home, control = game.has_control(), owned = owner == who,
        starting_state = baseline ~= nil}
end

)KadanLua") TEXT(R"KadanLua(
local function transform_args(name, pose)
    local out = {}
    for axis, value in pairs(pose.location) do out[name .. '.Translation.' .. axis:upper()] = value end
    for axis, value in pairs(pose.quaternion) do out[name .. '.Rotation.' .. axis:upper()] = value end
    for axis, value in pairs(pose.scale) do out[name .. '.Scale3D.' .. axis:upper()] = value end
    return out
end

function P.ordinary_remove(who, id)
    claim(who)
    local key = tostring(id)
    local a = ordinary[key]
    if not a then return false end
    ordinary[key] = nil
    for token, value in pairs(ordinary_tokens) do if value == a then ordinary_tokens[token] = nil end end
    if actor.alive(a) then invoke(a, 'K2_DestroyActor') end
    return true
end

local function clear_ordinary()
    local old = ordinary
    ordinary = {}
    ordinary_tokens = {}
    for _, a in pairs(old) do if actor.alive(a) then pcall(invoke, a, 'K2_DestroyActor') end end
end

function P.ordinary_track(who, a, token)
    claim(who)
    ordinary[tostring(a)] = a
    if token then ordinary_tokens[token] = a end
    return a
end

function P.ordinary_create(who, spec)
    claim(who)
    local existing = spec.token and ordinary_tokens[spec.token]
    if existing and actor.alive(existing) then return existing end
    connect()
    local held, made = {}, nil
    local function load(path)
        path = P.paths[path] or path
        local value, why = asset.load(path)
        if not value then error('Could not load preview asset ' .. path .. ': ' .. tostring(why)) end
        held[#held + 1] = value
        return value
    end
    local done, result = pcall(function()
        local class = spec.class_path:sub(1, 8) == '/Script/' and reflection.find_class(spec.class_name) or load(spec.class_path)
        assert(class, 'The preview actor class is unavailable.')
        local mesh = spec.mesh and load(spec.mesh)
        local materials = {}
        for slot, path in pairs(spec.materials or {}) do materials[slot] = load(path) end
        local args = transform_args('SpawnTransform', spec.pose)
        args.WorldContextObject, args.ActorClass, args.CollisionHandlingOverride = player.captain(0), class, 1
        made = assert(invoke(statics, 'BeginDeferredActorSpawnFromClass', args), 'The preview could not create this actor.')
        if mesh then
            local component = assert(reflection.get(made, 'StaticMeshComponent'), 'The preview actor has no static mesh component.')
            invoke(component, 'SetMobility', {NewMobility = 2})
            assert(invoke(component, 'SetStaticMesh', {NewMesh = mesh}), 'The preview refused this static mesh.')
            for slot, material in pairs(materials) do invoke(component, 'SetMaterial', {ElementIndex = slot - 1, Material = material}) end
        end
        args = transform_args('SpawnTransform', spec.pose)
        args.Actor = made
        assert(invoke(statics, 'FinishSpawningActor', args), 'The preview could not finish this actor.')
        -- Reflection returns an Object handle. The actor service requires its typed Actor handle.
        local path, typed = reflection.path(made), nil
        for _, candidate in ipairs(actor.list(class)) do
            if reflection.path(candidate) == path then typed = candidate break end
        end
        made = assert(typed, 'The preview could not find its newly spawned actor.')
        ordinary[tostring(made)] = made
        if spec.token then ordinary_tokens[spec.token] = made end
        return made
    end)
    for _, value in ipairs(held) do pcall(asset.release, value) end
    if not done then
        if made then pcall(invoke, made, 'K2_DestroyActor') end
        error(result)
    end
    return result
end

function P.ordinary_move(who, id, pose)
    claim(who)
    local a = assert(ordinary[tostring(id)], 'This actor does not belong to the preview.')
    local _, why = actor.set_transform(a, {location = pose.location, rotation = pose.rotation})
    if why then error(why) end
    invoke(a, 'SetActorScale3D', {['NewScale3D.X'] = pose.scale.x, ['NewScale3D.Y'] = pose.scale.y, ['NewScale3D.Z'] = pose.scale.z})
    return true
end

function P.ordinary_rows(who)
    claim(who)
    local out = {}
    for key, a in pairs(ordinary) do
        if actor.alive(a) then
            local pose = actor.transform(a)
            if pose then local l, r = pose.location, pose.rotation
                local token = ''
                for name, value in pairs(ordinary_tokens) do if value == a then token = name break end end
                out[#out + 1] = {a, 'MESH', '0', l.x, l.y, l.z, r.pitch, r.yaw, r.roll, token} end
        else ordinary[key] = nil end
    end
    return out
end

)KadanLua") TEXT(R"KadanLua(
function P.play(who, map, number, fingerprint)
    if type(map) ~= "string" or not map:match("^[%w_]+$") then error("The editor map has no valid game name.") end
    claim(who)
    if request_number == number and target == map then return P.status(who) end
    expire_start()
    if P.state == "starting" then error("The preview is already loading a map.") end
    connect()
    local scene = invoke(manager, "FindGameScene", {InSceneName = map, bOpenLevel = true, bRemove_P = true})
    if not scene then error("The game has no scene for " .. map .. ". Install the map in the preview game first.") end
    local pending = reflection.get(manager, "NextGameSceneReq")
    local loading = reflection.get(manager, "NewGameSceneList")
    if pending or (loading and #loading > 0) then error("The game is already changing scenes.") end
    if not player.captain(0) then error("Load a preview save before pressing Play.") end
    local content = fingerprint and kadan_preview_bundles and kadan_preview_bundles[fingerprint]
    if content and content.preview then
        local info = assert(game.preview_scene(content.preview), 'The new level reservation is unavailable.')
        if info.world_name ~= map or info.world_package ~= content.world_package or info.scene_object ~= content.scene_object then
            error('The new level changed after preparation. Press Play to prepare it again.')
        end
        local scene_class = assert(reflection.find_class('GameSceneData'), 'GameSceneData is unavailable.')
        local selected = reflection.path(scene)
        if not reflection.is_a(scene, scene_class) or selected ~= content.scene_object then
            error('The game selected a different preview scene: ' .. tostring(selected) ..
                '. Expected ' .. tostring(content.scene_object) .. '. Press Play to prepare it again.')
        end
    end
    -- Every check passed. Only now does Play touch the game's starting state.
    capture_baseline()
    restore_baseline()
    if content and content.preview then
        local ready, why = game.prepare_preview(content.preview)
        if not ready then error('The new level could not prepare its starting state: ' .. tostring(why)) end
    end
    resume()
    clear_ordinary()
    previous_captain = tostring(player.captain(0))
    if not invoke(manager, "SceneReloadAll") then error("The game refused to restart its scene.") end
    -- SceneJump is a reflected void function. Success is the absence of a call error;
    -- the requested map and fresh captain below establish that loading actually finished.
    invoke(manager, "SceneJump", {InNextScene = scene})
    target, request_number, P.state, started = map, number, "starting", step
    return P.status(who)
end

function P.stop(who)
    claim(who)
    expire_start()
    if P.state == "starting" then error("Wait for the map to finish loading before stopping the preview.") end
    clear_ordinary()
    hold()
    P.state = "stopped"
    -- The player's own save must never keep what happened in the preview.
    local restored, why = pcall(restore_baseline)
    if not restored then error("The preview stopped, but its starting state could not be put back: " .. tostring(why)) end
    return P.status(who)
end

function P.pause(who)
    claim(who)
    if P.state ~= "playing" and P.state ~= "paused" then error("The preview is not playing.") end
    hold()
    P.state = "paused"
    return P.status(who)
end

function P.resume(who)
    claim(who)
    if P.state ~= "paused" then error("The preview is not paused.") end
    resume()
    P.state = "playing"
    return P.status(who)
end

-- A cook holds the editor, which then sends nothing, and an upload can outlast the lease: the owner holds the lease
-- for a bounded number of steps while it prepares. 0 ends the hold.
function P.prepare(who, steps)
    if owner ~= who then return false end
    touched = step
    held = step + math.max(0, math.min(math.tointeger(steps) or 0, most_held))
    return true
end

function P.release(who)
    if owner ~= who then return false end
    held = 0
    -- Put the starting state back before letting go, so an autosave never keeps preview progress.
    local restored, why = pcall(restore_baseline)
    if not restored then mod.log("Kadan preview could not restore its starting state: " .. tostring(why)) end
    local resumed, failure = pcall(resume)
    if not resumed then mod.log("Kadan preview could not resume the game: " .. tostring(failure)) end
    clear_ordinary()
    discard_baseline()
    owner, target, previous_captain, request_number = nil, nil, nil, nil
    P.state = "editing"
    return true
end

function P.new_start(who)
    claim(who)
    expire_start()
    if P.state == "starting" then error("Wait for the map to finish loading first.") end
    discard_baseline()
    return P.status(who)
end

function P.shutdown()
    if owner then error("Another preview session is still active. Stop and disconnect it before updating Kadan.") end
    if subscription then subscription:cancel() subscription = nil end
end

-- Slots a crashed editor left behind go when the controller is installed.
do
    local found, gameplay = pcall(reflection.find_class, "GameplayStatics")
    if found and gameplay then
        statics = gameplay
        for _, field in ipairs(save_fields) do
            pcall(invoke, statics, "DeleteGameInSlot", {SlotName = slot_name(field), UserIndex = 0})
        end
    end
end

-- A closed/crashed editor must not leave the preview permanently paused or owned.
subscription = time.on_step(function()
    -- Count callbacks owned by this controller. The first engine event.number may already
    -- be thousands when injection and Play arrive in one frame; it is not a local epoch.
    step = step + 1
    if owner and step - touched > 1800 and step >= held then
        local ok, why = pcall(P.release, owner)
        if not ok then mod.log("Kadan preview cleanup: " .. tostring(why)) end
    end
end)
return P
)KadanLua");
}
