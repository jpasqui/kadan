"""Host parity of the C++ navigation export core with the Python tools.

    py -3.11 modding/mapkit/tests/test_navigation_export.py

Compiles navigation_export_test.cpp with clang++ (or g++) and checks, byte for byte or field
for field against the Python tools:
  - Convert + EncodeBuilt against swap_navmesh.remap/carry_over + nav_encode + mapkit.nav_bin;
  - ReadStockMap (the stock map's own RecastNavMesh actors and SupportedAreas tables) against
    swap_navmesh.navmeshes;
  - ExportMap (actor routing, Hero bakes, kept actors and the errors) against swap_navmesh.swap
    followed by mapkit.nav_bin.
The synthetic cases always run. The map cases read private extracts of Cave000_F00_P, Area001_P
and its Area001_GameArea sublevel, by default from private/custom-navigation-research of this
checkout or of the main checkout, and are skipped with a message when those are absent.
No bake, cook, Unreal, desktop connection or game process is started.
"""
import argparse
import copy
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[3]
sys.path[:0] = [str(ROOT / 'tools/navigation'), str(ROOT / 'tools')]
import map_nav
import mapkit
import nav_encode as ne
import swap_navmesh as sw
from test_nav_encode import tiny_payload

CAVE_PACKAGE = '/Game/Carrot4/Maps/Madori/Cave/Cave000/Cave000_F00/Cave000_F00_P'
AREA_PACKAGE = '/Game/Carrot4/Maps/Main/Area/Area001/Area001_P'
RESEARCH = 'private/custom-navigation-research'
CAVE = RESEARCH + '/assets/Carrot4/Content/Carrot4/Maps/Madori/Cave/Cave000/Cave000_F00/Cave000_F00_P.umap'
AREA = RESEARCH + '/levels-extract/Area001_P/Carrot4/Content/Carrot4/Maps/Main/Area/Area001/Area001_P.umap'
SUBLEVEL = RESEARCH + '/levels-extract/Area001_P/Carrot4/Content/Carrot4/Maps/Main/Area/Area001/Sublevels/Area001_GameArea.umap'


def fixture(relative):
    """The private extract in this checkout, else in the main checkout (worktrees share no private/)."""
    roots = [ROOT]
    try:
        common = subprocess.run(['git', 'rev-parse', '--path-format=absolute', '--git-common-dir'], cwd=ROOT,
                                capture_output=True, text=True, check=True).stdout.strip()
        roots.append(Path(common).parent)
    except (OSError, subprocess.CalledProcessError):
        pass
    for root in roots:
        if (root / relative).is_file():
            return root / relative
    return ROOT / relative


class Bakes:
    """swap_navmesh.swap reads every map through map_nav. Here the bakes are plain tiles, as the editor
    hands them over, and the stock map is the real extract. With paint=False the helper volumes are
    never used, and the transplanted map is not part of the .nav file, so both are stubbed."""

    def __init__(self, maps):
        self.maps = maps   # {fake path: [dict(name, nav, areas)]}

    def layout(self, key):
        body, meshes = bytearray(), []
        for bake in self.maps[key]:
            payload = ne.encode(bake['nav'])
            meshes.append(dict(name=bake['name'], class_name='/Script/NavigationSystem.RecastNavMesh',
                               native_payload_offset=len(body), export_end=len(body) + len(payload),
                               properties=[dict(name='SupportedAreas', decoded=[
                                   [dict(name='AreaClassName', value=n), dict(name='AreaID', value=v)]
                                   for n, v in bake['areas'].items()])]))
            body += payload
        return bytes(body), meshes

    def __enter__(self):
        self.saved = (map_nav.Asset, map_nav.inspect, map_nav.transplant, sw.map_sources, sw.helper_volumes)
        asset, inspect = map_nav.Asset, map_nav.inspect

        def fake_asset(path):
            return SimpleNamespace(body=self.layout(str(path))[0]) if str(path) in self.maps else asset(path)

        def fake_inspect(path):
            return dict(meshes=self.layout(str(path))[1]) if str(path) in self.maps else inspect(path)

        def fake_transplant(path, actor, native, output):
            for suffix in ('.umap', '.uexp'):
                output.with_suffix(suffix).write_bytes(b'')
            return dict(unchanged_export_count=0)

        map_nav.Asset, map_nav.inspect, map_nav.transplant = fake_asset, fake_inspect, fake_transplant
        sw.map_sources = lambda *args, **kwargs: ([], [])
        sw.helper_volumes = lambda *args, **kwargs: []
        return self

    def __exit__(self, *error):
        map_nav.Asset, map_nav.inspect, map_nav.transplant, sw.map_sources, sw.helper_volumes = self.saved


def as_bake(game, name, radius=None):
    """A stock actor's tiles as an editor bake: Blueprint area names and another area order
    (Default 63 and Null 0 stay, as UE4.26 fixes them), flags left for the conversion to set."""
    ids = {old: old if old in (0, 63) else 63 - old for old in game['areas'].values()}
    nav = copy.deepcopy(game['nav'])
    for tile in nav['tiles']:
        if radius is not None:
            tile['header']['walkable_radius'] = radius
        for poly in tile['polys']:
            poly['area'], poly['flags'] = ids[poly['area']], 0
    areas = {(n if n in sw.PLAIN else n + '__L2_C'): ids[v] for n, v in game['areas'].items()}
    return dict(name=name, nav=nav, areas=areas)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--cave-map', type=Path, default=fixture(CAVE))
    parser.add_argument('--area-map', type=Path, default=fixture(AREA))
    parser.add_argument('--area-sublevel', type=Path, default=fixture(SUBLEVEL))
    parser.add_argument('--cave-bake', type=Path)
    parser.add_argument('--area-bake', type=Path, help='Existing cooked/rebuilt Area001 parent map')
    parser.add_argument('--out', type=Path, default=ROOT / 'private/nav-export-tests')
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    compiler = shutil.which('clang++') or shutil.which('g++')
    if not compiler:
        parser.error('A C++17 host compiler is required (the consumer header uses C++17)')
    args.out.mkdir(parents=True, exist_ok=True)
    subprocess.run([compiler, '-std=c++14', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
                    '-I', str(ROOT / 'modding/mapkit'), '-x', 'c++', '-fsyntax-only', '-'],
                   input='#include "core/navigation_map_export.h"\n', text=True, check=True)
    executable = args.out / ('navigation_export_test.exe' if os.name == 'nt' else 'navigation_export_test')
    flags = ['-std=c++17', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-ffp-contract=off']
    if args.sanitize:
        flags += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all']
    subprocess.run([compiler, *flags, '-I', str(ROOT / 'modding/mapkit'), '-I', str(ROOT / 'modding'),
                    str(Path(__file__).with_name('navigation_export_test.cpp')), '-o', str(executable)], check=True)
    env = dict(os.environ)
    if args.sanitize and os.name == 'nt':
        resource = subprocess.check_output([compiler, '-print-resource-dir'], text=True).strip()
        env['PATH'] = str(Path(resource) / 'lib/windows') + os.pathsep + env['PATH']
    cases = 0
    # Reuse one small output folder. Expected bytes never feed the C++ path.
    input_file, expected_file, actual_file, result_file = [args.out / name for name in
                                                           ('input.json', 'python.nav', 'cpp.nav', 'cpp.json')]

    def run(*arguments):
        result = subprocess.run([str(executable), *map(str, arguments)], env=env, capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(' '.join(map(str, arguments[:1])) + ': ' + result.stderr.strip())
        return result.stdout.strip()

    def same_bytes(label, python, actual):
        if actual != python:
            first = next((i for i, (a, b) in enumerate(zip(python, actual)) if a != b), min(len(python), len(actual)))
            difference = '{}: Python {} bytes, C++ {} bytes; first difference at 0x{:x}; Python {}, C++ {}'.format(
                label, len(python), len(actual), first, python[first:first+32].hex(), actual[first:first+32].hex())
            (args.out / 'difference.txt').write_text(difference + '\n', encoding='utf-8')
            raise AssertionError(difference)

    def compare(label, package, actors):
        nonlocal cases
        input_file.write_text(json.dumps(dict(package=package, actors=actors)), encoding='utf-8')
        expected, added, skipped = {}, 0, 0
        for actor in actors:
            if 'target' in actor:
                target = dict(nav=actor['target'], areas=actor['target_areas'],
                              agent=tuple(actor['target']['tiles'][0]['header'][name] for name in
                                          ('walkable_radius', 'walkable_height', 'walkable_climb')),
                              mesh=dict(properties=[dict(name='SupportedAreas', decoded=[
                                  [dict(name='AreaClassName', value=name), dict(name='AreaID', value=value)]
                                  for name, value in actor['target_areas'].items()])]))
                bake = dict(nav=actor['bake'], areas=actor['bake_areas'])
                converted, _ = sw.remap(target, bake)
                if actor['carry']:
                    result = sw.carry_over(target, converted, paint=False)
                    added += result['links_added']
                    skipped += len(result['links_skipped'])
            else:
                converted = actor['bake']
            expected[actor['name']] = ne.encode(converted)
        python = mapkit.nav_bin(package, expected)
        expected_file.write_bytes(python)
        actual_file.unlink(missing_ok=True)
        stdout = run('convert', input_file, actual_file)
        same_bytes(label, python, actual_file.read_bytes())
        if actors[0].get('carry'):
            assert 'links_added={} links_skipped={}'.format(added, skipped) in stdout, stdout
        cases += 1
        print('{}: byte-equal, {} bytes, {}'.format(label, len(python), stdout))

    def stock(label, path):
        """ReadStockMap against swap_navmesh.navmeshes: every decoded field, the class, the agent and
        the SupportedAreas table, compared as JSON text so -0.0 and 0.0 stay apart."""
        nonlocal cases
        run('stock', path, path.with_suffix('.uexp'), result_file)
        cpp = json.loads(result_file.read_text(encoding='utf-8'))
        games = sw.navmeshes(path)
        python = [dict(name=g['name'], cls=g['cls'], agent=list(g['agent']), areas=g['areas'], nav=g['nav'])
                  for g in games]
        if json.dumps(cpp, sort_keys=True) != json.dumps(python, sort_keys=True):
            raise AssertionError(label + ': ReadStockMap differs from swap_navmesh.navmeshes')
        cases += 1
        print('{}: {} actors, {} tiles read field-equal'.format(label, len(python),
                                                                sum(len(g['nav']['tiles']) for g in games)))
        return games

    def route(label, package, path, bakes, hero_bakes=None, carry=True):
        """ExportMap against swap_navmesh.swap(keep, paint=False) and mapkit.nav_bin."""
        nonlocal cases
        maps = {'fake-bake-normal': bakes}
        if hero_bakes is not None:
            maps['fake-bake-hero'] = hero_bakes
        payloads = {}
        try:
            with Bakes(maps), tempfile.TemporaryDirectory() as work:
                report = sw.swap(path, Path('fake-bake-normal'), Path(work), keep=carry, paint=False, payloads=payloads,
                                 hero_bake_map=Path('fake-bake-hero') if hero_bakes is not None else None)
            python, error = mapkit.nav_bin(package, payloads), ''
        except sw.Stop as stop:
            python, report, error = None, None, str(stop)
        document = dict(package=package, stock=dict(umap=str(path), uexp=str(path.with_suffix('.uexp'))),
                        bakes=bakes, carry=carry)
        if hero_bakes is not None:
            document['hero_bakes'] = hero_bakes
        input_file.write_text(json.dumps(document), encoding='utf-8')
        actual_file.unlink(missing_ok=True)
        run('route', input_file, result_file, actual_file)
        cpp = json.loads(result_file.read_text(encoding='utf-8'))
        if error:
            assert cpp['error'] and cpp['message'] == error, '{}: Python "{}", C++ {} "{}"'.format(
                label, error, cpp['error'], cpp['message'])
            assert not actual_file.exists(), label + ': C++ wrote a file after an error'
            cases += 1
            print('{}: same error, {}'.format(label, cpp['error']))
            return None
        assert not cpp['error'], '{}: C++ refused with {} {}'.format(label, cpp['error'], cpp.get('message'))
        expected_file.write_bytes(python)
        same_bytes(label, python, actual_file.read_bytes())
        rows = [(r['actor'], r['replaced'], r.get('source', ''), r.get('reason', ''), r.get('tiles', 0),
                 r.get('polygons', 0), (r.get('carried') or {}).get('links_added', 0),
                 len((r.get('carried') or {}).get('links_skipped', []))) for r in report]
        mine = [(r['actor'], r['replaced'], r['source'], r['reason'], r['tiles'], r['polygons'], r['links_added'],
                 r['links_skipped']) for r in cpp['actors']]
        assert rows == mine, '{}: reports differ\n{}\n{}'.format(label, rows, mine)
        cases += 1
        print('{}: byte-equal, {} bytes, {} of {} actors replaced'.format(
            label, len(python), sum(r[1] for r in rows), len(rows)))
        return python

    tiny = tiny_payload()
    # Exact references beyond double's 53-bit mantissa, user IDs, negative grid/BV values,
    # firstLink and unused polygon slots are retained, not normalized.
    tiny['tiles'][0]['reference'] = (1 << 63) + 123
    tiny['tiles'][0]['header']['user_id'] = 0xffffffff
    tiny['tiles'][0]['header']['x'] = -2
    tiny['tiles'][0]['verts'][0][0] = -0.0
    tiny['tiles'][0]['bv_tree'][0][-1] = -1
    compare('synthetic exact fields', '/Game/Tests/Navigation', [dict(name='Mesh', bake=tiny)])
    all_sections = copy.deepcopy(tiny)
    tile = all_sections['tiles'][0]
    tile['detail_verts'] = [[12.5, -4.25, 30.0]]
    tile['detail_meshes'][0][2] = 1
    tile['segments'] = [dict(start_a=[0.0, 0.0, 0.0], start_b=[10.0, 0.0, 0.0],
                            end_a=[0.0, 0.0, 10.0], end_b=[10.0, 0.0, 10.0], rad=2.0,
                            first_poly=0, npolys=0, flags=0, user_id=0xffffffff)]
    tile['memory_size'] = ne.layout_size(tile)
    compare('synthetic all wire sections', '/Game/Tests/Navigation', [dict(name='Mesh', bake=all_sections)])

    areas = {'NavArea_Default': 63, 'NavArea_Null': 0, 'NavArea_Water': 7}
    target, bake = tiny_payload(), tiny_payload()
    bake['tiles'][0]['polys'][0]['area'] = 2
    bake['tiles'][0]['polys'][1]['area'] = 3
    bake['tiles'][0]['polys'][2]['area'] = 3
    compare('remap Blueprint and ordered area, fallback flags', '/Game/Tests/Navigation', [dict(
        name='Mesh', bake=bake, target=target, target_areas=areas,
        bake_areas={'NavArea_Default_C': 3, 'NavArea_Water__L2_C': 2}, carry=True)])

    outside = tiny_payload()
    outside['tiles'][0]['off_mesh'][0]['pos'][0] = 300.0
    compare('inert stock link outside rebuilt tiles is skipped', '/Game/Tests/Navigation', [dict(
        name='Mesh', bake=tiny_payload(), target=outside, target_areas=areas, bake_areas=areas, carry=True)])
    layered = tiny_payload()
    second = copy.deepcopy(layered['tiles'][0])
    second['reference'] += 1
    second['header']['layer'] = 1
    layered['tiles'].append(second)
    compare('layer duplicate link and nearest-ground tie order', '/Game/Tests/Navigation', [dict(
        name='Mesh', bake=layered, target=layered, target_areas=areas, bake_areas=areas, carry=True)])
    # carry_over keys links by JSON text: a link at -0.0 is a different link from one at 0.0.
    signed = copy.deepcopy(layered)
    signed['tiles'][1]['off_mesh'][0]['pos'][1] = -0.0
    compare('links differing only in the sign of zero are both carried', '/Game/Tests/Navigation', [dict(
        name='Mesh', bake=layered, target=signed, target_areas=areas, bake_areas=areas, carry=True)])

    missing = [p for p in (args.cave_map, args.area_map, args.area_sublevel)
               if not (p.is_file() and p.with_suffix('.uexp').is_file())]
    if missing:
        print('{} synthetic cases passed. Skipped the map cases: the private extract {} is not on this '
              'machine (pass --cave-map, --area-map and --area-sublevel).'.format(cases, missing[0]))
        return

    def real_cases(label, path, games, bake_path=None):
        bakes = sw.navmeshes(bake_path) if bake_path else None
        package = CAVE_PACKAGE if label == 'Cave000' else AREA_PACKAGE
        assert games, label + ' must have real tiles'
        for game in games:
            if bakes:
                matches = [b for b in bakes if b['name'] == game['name']]
                if not matches:
                    matches = [b for b in bakes if b['agent'][0] == game['agent'][0]]
                assert len(matches) == 1
                baked = matches[0]
                assert not sw.settings_problems(game, baked)
                nav, baked_areas = baked['nav'], baked['areas']
            else:
                # Genuine tile inputs, with a deliberately different editor area ordering.
                nav = copy.deepcopy(game['nav'])
                ids = {old: 63-old for old in game['areas'].values()}
                for tile in nav['tiles']:
                    for poly in tile['polys']:
                        poly['area'], poly['flags'] = ids[poly['area']], 0
                baked_areas = {(name if name in ('NavArea_Default', 'NavArea_Null') else name+'__L2_C'): ids[value]
                               for name, value in game['areas'].items()}
            actor = dict(name=game['name'], bake=nav, target=game['nav'], target_areas=game['areas'],
                         bake_areas=baked_areas, carry=True)
            compare(label + ' ' + game['name'], package, [actor])
        compare(label + ' complete actor file', package, [dict(name=g['name'], bake=g['nav']) for g in reversed(games)])

    cave, area = args.cave_map, args.area_map
    cave_games = stock('Cave000 stock actors', cave)
    area_games = stock('Area001 stock actors', area)
    real_cases('Cave000', cave, cave_games, args.cave_bake)
    real_cases('Area001', area, area_games, args.area_bake)

    def agent_name(game):
        return 'RecastNavMesh-' + game['name'].split('-')[-1].replace('Hero', '')

    big, small, pikmin = cave_games
    normal = [as_bake(g, agent_name(g)) for g in reversed(cave_games)]
    route('Cave000 routed by radius', CAVE_PACKAGE, cave, normal)
    route('Cave000 radius not baked keeps the game actor', CAVE_PACKAGE, cave, normal[:2])
    route('Cave000 without carried links', CAVE_PACKAGE, cave, normal, carry=False)
    route('Cave000 several navmeshes', CAVE_PACKAGE, cave, normal + [as_bake(pikmin, 'RecastNavMesh-Again')])
    route('Cave000 nothing matched', CAVE_PACKAGE, cave, [as_bake(pikmin, 'RecastNavMesh-Odd', radius=99.0)])
    wide, pooled = as_bake(pikmin, agent_name(pikmin)), as_bake(small, agent_name(small))
    wide['nav']['tile_width'] *= 2
    pooled['nav']['max_tiles'] += 1
    route('Cave000 settings differ', CAVE_PACKAGE, cave, [as_bake(big, agent_name(big)), pooled, wide])
    route('Cave000 no bake', CAVE_PACKAGE, cave, [])
    empty = as_bake(pikmin, agent_name(pikmin))
    empty['nav']['tiles'] = []
    route('Cave000 empty baked navmesh', CAVE_PACKAGE, cave, [empty])

    plain = [g for g in area_games if not sw.is_hero(g['name'])]
    heroes = [g for g in area_games if sw.is_hero(g['name'])]
    assert plain and heroes, 'Area001 has Hero actors'
    with_hero = route('Area001 Hero actors take the Hero bake', AREA_PACKAGE, area,
                      [as_bake(g, agent_name(g)) for g in plain], [as_bake(g, agent_name(g)) for g in heroes])
    without = route('Area001 without a Hero bake', AREA_PACKAGE, area, [as_bake(g, agent_name(g)) for g in plain])
    assert with_hero != without, 'The Hero bake must change the Hero actors'
    route('Area001 Hero bake without a radius', AREA_PACKAGE, area, [as_bake(g, agent_name(g)) for g in plain],
          [as_bake(g, agent_name(g)) for g in heroes if g['agent'][0] != 70.0])

    # A sublevel owns no Recast actors: the parent map's actors are baked with the sublevels'
    # collision loaded, so a sublevel edit is exported through its parent map, as above.
    assert stock('Area001 sublevel ' + args.area_sublevel.stem, args.area_sublevel) == []
    route('Area001 sublevel is not a navigation map', AREA_PACKAGE, args.area_sublevel,
          [as_bake(g, agent_name(g)) for g in plain])
    print('{} cases passed; host validation and transactional rejection checks passed'.format(cases))


if __name__ == '__main__':
    main()
