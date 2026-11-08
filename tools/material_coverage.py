"""The material coverage table: which UE4.26 material features export and draw in the game.

docs/material-coverage.json holds one row per feature (a domain, blend mode, shading model, Details setting,
material input, texture type or expression node). docs/MATERIAL_COVERAGE.md is made from it. Each row has one
status:

    game       seen drawing in the game (Eden) through Kadan's export, the feature's own effect included; `seen`
               says what was seen and when
    draws      a material using it draws in the game, but the feature's own effect was not checked there
    wrong      it compiles and exports, but draws wrong in the game (`seen` says how)
    compiles   compiles for the game through Kadan's export, not yet seen in the game
    refused    Kadan's game compile or export refuses it (`error` has the message) while UE4 compiles it for the editor
    ue4        UE4 itself refuses this graph for the editor too: the survey's setup, not a Kadan gap

The rows come from the editor test P4MapKit.Materials.Coverage, which builds one creator material per feature and
compiles it for the editor and for the game. Its report (Saved/P4MapKit/MaterialCoverage/report.json) is merged in;
what was seen in the game is kept across merges.

    py -3.11 tools/material_coverage.py merge report.json      take the test's compile results
    py -3.11 tools/material_coverage.py seen ID... --note TEXT  record what was seen in the game
    py -3.11 tools/material_coverage.py seen ID... --draws --note TEXT  it draws; its effect not checked
    py -3.11 tools/material_coverage.py seen ID... --wrong --note TEXT  it draws wrong in the game
    py -3.11 tools/material_coverage.py render                  write docs/MATERIAL_COVERAGE.md
    py -3.11 tools/material_coverage.py check                   the page is up to date (tools/check.py)
"""
import argparse
import datetime
import json
from pathlib import Path
import sys

REPO = Path(__file__).resolve().parent.parent
TABLE = REPO / 'docs/material-coverage.json'
PAGE = REPO / 'docs/MATERIAL_COVERAGE.md'
FAMILIES = [
    ('translucency', 'Translucency settings'),
    ('blend', 'Blend modes'),
    ('shading', 'Shading models'),
    ('inputs', 'Material inputs and surface settings'),
    ('textures', 'Texture types and parameter collections'),
    ('domain', 'Material domains and decal blend modes'),
    ('nodes', 'Expression nodes'),
]
STATUS = {'game': 'seen in the game', 'draws': 'draws in the game, its effect not checked yet',
          'wrong': 'draws wrong in the game', 'compiles': 'compiles, not seen in the game yet',
          'refused': 'refused', 'ue4': 'UE4 refuses this setup too'}


def load():
    if not TABLE.exists():
        return {'rows': []}
    return json.loads(TABLE.read_text(encoding='utf-8'))


def save(table):
    table['rows'].sort(key=lambda r: ([f for f, _ in FAMILIES].index(r['family'])
                                      if r['family'] in [f for f, _ in FAMILIES] else 99, r['id']))
    TABLE.write_text(json.dumps(table, indent=1, ensure_ascii=False) + '\n', encoding='utf-8', newline='\n')


def merge(table, report):
    rows = {(r['family'], r['id']): r for r in table['rows']}
    # A node is tried with several input wirings; the last attempt UE4 compiled is the one that counts.
    latest = {}
    for entry in report:
        key = (entry['family'], entry['id'])
        if entry.get('result') == 'not-built':
            continue
        if key in latest and latest[key].get('result') != 'ue4-refuses' and entry.get('result') == 'ue4-refuses':
            continue
        latest[key] = entry
    for key, entry in latest.items():
        row = rows.setdefault(key, {'family': key[0], 'id': key[1]})
        row['feature'] = entry['feature'].split(' (inputs:')[0] if key[0] == 'nodes' else entry['feature']
        result = entry.get('result')
        if result == 'compiles' and entry.get('export_refused'):
            row['status'] = 'refused'
            row['error'] = 'Compiles; export refuses ' + entry['export_refused']
            row.pop('seen', None)
        elif result == 'compiles':
            if row.get('status') not in ('game', 'draws', 'wrong'):
                row['status'] = 'compiles'
            row.pop('error', None)
        elif result == 'game-refuses':
            row['status'] = 'refused'
            row['error'] = entry.get('game_error', '')
            row.pop('seen', None)
        else:
            row['status'] = 'ue4'
            row['error'] = entry.get('editor_errors', '')
            row.pop('seen', None)
    table['rows'] = list(rows.values())
    return table


def render(table):
    rows = table['rows']
    lines = ['# Material coverage', '',
             'Which Unreal Engine 4.26 material features Kadan exports to Pikmin 4, and which have been seen drawing',
             'in the game. Made by `tools/material_coverage.py` from `docs/material-coverage.json`; do not edit by hand.',
             '', 'Kadan compiles a creator material with UE4\'s own material translator and shader source, then its own',
             'SPIR-V and Maxwell compiler (docs/OWN_SHADER_MAPS.md). A row is **seen in the game** only after a material',
             'using the feature was exported and looked at in Eden next to the UE4 editor\'s rendering, and the feature\'s own',
             'effect was seen. **Draws** means such a material draws in the game but its effect was not checked there.',
             '**Draws wrong** means it compiles and exports but the game draws it wrong; the note says how.',
             '**Compiles** means',
             'the game compile accepts it and nobody has looked at it in the game yet: it is not a promise that it draws',
             'right. **Refused** rows name the message the export gives.', '']
    counts = {s: sum(1 for r in rows if r.get('status') == s) for s in STATUS}
    lines += ['| Status | Rows |', '| --- | ---: |']
    lines += ['| %s | %d |' % (STATUS[s], counts[s]) for s in STATUS]
    lines.append('')
    for family, title in FAMILIES:
        members = [r for r in rows if r['family'] == family]
        if not members:
            continue
        lines += ['## ' + title, '', '| Feature | Status | Notes |', '| --- | --- | --- |']
        for r in members:
            note = r.get('seen') or r.get('error') or ''
            note = ' '.join(note.split()).replace('|', '\\|')
            lines.append('| %s | %s | %s |' % (r.get('feature', r['id']), STATUS.get(r.get('status'), '?'), note))
        lines.append('')
    return '\n'.join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='action', required=True)
    m = sub.add_parser('merge')
    m.add_argument('report')
    s = sub.add_parser('seen')
    s.add_argument('ids', nargs='+', help='family/id or id')
    s.add_argument('--note', required=True)
    s.add_argument('--date', default=datetime.date.today().isoformat())
    s.add_argument('--draws', action='store_true', help='it draws, but its own effect was not checked')
    s.add_argument('--wrong', action='store_true', help='it draws wrong in the game')
    sub.add_parser('render')
    sub.add_parser('check')
    args = parser.parse_args(argv)
    table = load()
    if args.action == 'merge':
        save(merge(table, json.loads(Path(args.report).read_text(encoding='utf-8-sig'))))
    elif args.action == 'seen':
        for wanted in args.ids:
            found = [r for r in table['rows'] if wanted in (r['id'], r['family'] + '/' + r['id'])]
            if not found:
                print('no row', wanted, file=sys.stderr)
                return 1
            for r in found:
                if r.get('status') not in ('compiles', 'game', 'draws', 'wrong'):
                    print('%s is %s, not compiled for the game' % (wanted, r.get('status')), file=sys.stderr)
                    return 1
                r['status'] = 'wrong' if args.wrong else 'draws' if args.draws else 'game'
                r['seen'] = '%s (%s)' % (args.note, args.date)
        save(table)
    if args.action in ('merge', 'seen', 'render'):
        PAGE.write_text(render(load()) + '\n', encoding='utf-8', newline='\n')
        return 0
    if PAGE.read_text(encoding='utf-8') != render(table) + '\n':
        print('docs/MATERIAL_COVERAGE.md is out of date: run tools/material_coverage.py render', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
