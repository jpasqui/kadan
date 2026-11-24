"""Lua that shows the material coverage gallery in the game.

The editor test P4MapKit.Materials.Coverage (KADAN_COVERAGE_EXPORT=1) exports a mod with a cube and every
material that compiled for the game, and writes Saved/P4MapKit/MaterialCoverage/gallery.json. With that mod
loaded in the game (Tsuchi), this script's Lua puts one cube per material in rows in front of the captain, where
it stands and the way it faces, so the floor under the rows is floor the captain can reach.

    py -3.11 tools/material_gallery.py gallery.json [--only ID ...] [--row 6] > gallery.lua

Run the Lua with the game's developer console (the Dev Server's run_lua). It prints one line per cube: its index,
material and where it stands, so a picture can be read back against gallery.json. Cubes go from left to right as
the captain looks, rows from near to far; a last line gives each row's centre and the captain's yaw for the camera.
Running it again removes the cubes it placed before.
"""
import argparse
import json
import sys

LUA = r'''
local cube_path = %(cube)s
local entries = %(entries)s
local row_size, spacing, near, lift = %(row)d, 140, %(near)f, %(lift)f
local out = {}
for _, old in ipairs(kadan_gallery_parts or {}) do pcall(component.remove, old) end
kadan_gallery_parts = {}
local captain = player.captain(0)
if not captain then return "no captain" end
local pose = entity.pose(captain)
local yaw = math.rad(pose.rotation.yaw)
local forward, right = {math.cos(yaw), math.sin(yaw)}, {-math.sin(yaw), math.cos(yaw)}
local base = pose.location
local function call(object, name, args)
    local value, problem = reflection.call(object, name, args or {})
    if type(problem) == "string" then error(name .. ": " .. problem) end
    return value
end
local mesh, why = asset.load(cube_path)
if not mesh then return "cube load: " .. tostring(why) end
local owner = player.captain(0)
local holder_class = reflection.find_class("StaticMeshActor")
for _, candidate in ipairs(holder_class and actor.list(holder_class) or {}) do owner = candidate break end
local mesh_class = reflection.find_class("StaticMeshComponent")
for i, entry in ipairs(entries) do
    local column, row = (i - 1) %% row_size, math.floor((i - 1) / row_size)
    local across, ahead = (column - (row_size - 1) / 2) * spacing, near + row * spacing
    local at = {X=base.x + forward[1] * ahead + right[1] * across, Y=base.y + forward[2] * ahead + right[2] * across,
                Z=base.z + lift}
    local material, problem = asset.load(entry[2])
    if not material then
        out[#out + 1] = string.format("%%d %%s load: %%s", i, entry[1], tostring(problem))
    else
        local part; part, problem = component.add(owner, mesh_class, reflection.get(owner, "RootComponent"))
        local ok, failure = pcall(function()
            if not part then error(problem) end
            call(part, "SetMobility", {NewMobility=2})
            call(part, "SetAbsolute", {bNewAbsoluteLocation=true, bNewAbsoluteRotation=true, bNewAbsoluteScale=true})
            call(part, "SetCollisionEnabled", {NewType=0})
            if call(part, "SetStaticMesh", {NewMesh=mesh}) ~= true then error("SetStaticMesh refused") end
            call(part, "SetMaterial", {ElementIndex=0, Material=material})
            call(part, "SetWorldScale3D", {NewScale={X=1, Y=1, Z=1}})
            call(part, "K2_SetWorldRotation", {NewRotation={Pitch=0, Yaw=0, Roll=0}, bSweep=false, bTeleport=true})
            call(part, "K2_SetWorldLocation", {NewLocation=at, bSweep=false, bTeleport=true})
            call(part, "SetHiddenInGame", {NewHidden=false, bPropagateToChildren=false})
            call(part, "SetVisibility", {bNewVisibility=true, bPropagateToChildren=false})
        end)
        if ok then
            kadan_gallery_parts[#kadan_gallery_parts + 1] = part
            out[#out + 1] = string.format("%%d %%s row %%d column %%d at %%.0f,%%.0f,%%.0f", i, entry[1], row, column, at.X, at.Y, at.Z)
        else
            if part then component.remove(part) end
            out[#out + 1] = string.format("%%d %%s refused: %%s", i, entry[1], tostring(failure))
        end
    end
end
local rows = {}
for row = 0, math.floor((#entries - 1) / row_size) do
    local ahead = near + row * spacing
    rows[#rows + 1] = string.format("%%.0f,%%.0f,%%.0f", base.x + forward[1] * ahead, base.y + forward[2] * ahead, base.z + lift)
end
out[#out + 1] = string.format("rows %%s yaw %%.1f", table.concat(rows, " "), pose.rotation.yaw)
return table.concat(out, "\n")
'''


def lua_string(text):
    return json.dumps(text)


def build(gallery, only=(), row=6, near=350.0, lift=60.0):
    entries = [(m['material'], m['runtime']) for m in gallery['materials']
               if not only or any(word in m['material'] for word in only)]
    object_path = lambda path: path if '.' in path.rsplit('/', 1)[-1] else path + '.' + path.rsplit('/', 1)[-1]
    listed = '{' + ', '.join('{%s, %s}' % (lua_string(name), lua_string(object_path(path))) for name, path in entries) + '}'
    return LUA % {'cube': lua_string(object_path(gallery['cube'])), 'entries': listed, 'row': row,
                  'near': near, 'lift': lift}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('gallery')
    parser.add_argument('--only', nargs='*', default=[], help='materials whose name holds one of these words')
    parser.add_argument('--row', type=int, default=6)
    parser.add_argument('--near', type=float, default=350.0, help='cm from the captain to the first row')
    parser.add_argument('--lift', type=float, default=60.0, help="cm from the captain's position up to the cubes' centres")
    args = parser.parse_args(argv)
    with open(args.gallery, encoding='utf-8') as handle:
        gallery = json.load(handle)
    sys.stdout.write(build(gallery, args.only, args.row, args.near, args.lift))
    return 0


if __name__ == '__main__':
    sys.exit(main())
