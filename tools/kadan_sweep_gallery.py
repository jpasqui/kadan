"""Capture-gated editor sweep evidence. Images stay beside this bounded HTML gallery."""
from collections import Counter, defaultdict
from html import escape
import json
from pathlib import Path
import re

VIEWS = ('overview', 'player-start', 'terrain-1', 'terrain-2')
FIELDS = ('capture_status', 'capture_error', 'capture_scene', 'capture_loaded_levels',
          'visible_geometry_components', 'terrain_components', 'empty_scene',
          'capture_missing_material_slots', 'capture_materials_without_shaders')


def component_summary(candidates):
    classes = {}
    for candidate in candidates:
        name = candidate.get('class') or 'unknown class'
        group = classes.setdefault(name, dict(actors=0, visible_actors=0, visible_meshes=0, class_imports=set()))
        group['actors'] += 1
        visible = candidate.get('level_visible') and not candidate.get('editor_hidden')
        group['visible_actors'] += bool(visible)
        group['visible_meshes'] += sum(bool(visible and part.get('mesh') and part.get('visible') and part.get('registered'))
                                       for part in candidate.get('mesh_components', []))
        group['class_imports'].add(candidate.get('class_import') or 'unavailable')
    return [dict(group, **{'class': name, 'class_imports': sorted(group['class_imports'])})
            for name, group in sorted(classes.items())]


def refusal_reason(package, error):
    """Older importers repeated a whole Blueprint batch's errors on every row."""
    lines = error.splitlines()
    if sum(line.startswith('/Game/') for line in lines) > 1:
        own = [line for line in lines if line.startswith((package+':', package+' '))]
        if own:
            return '\n'.join(own)
        # This row never reached its own import pass after the named batch failure.
        return 'Blueprint batch stopped: ' + lines[0]
    return error


def explain_refusal(error):
    """Keep the native diagnostic as evidence beside a reader-facing explanation."""
    text = error.lower()
    for needle, explanation in (
        ('akassetdatawithmedia', 'The embedded Wwise bank/media data is not yet reconstructed; the original game bank remains.'),
        ('native-tail-relocation-unqualified', 'The native data contains offsets that the importer cannot yet move safely.'),
        ('engine-made-another-object-there', 'An engine-created object occupies an original object’s name with a different class or lifetime.'),
        ('loop of packages that need each other', 'A native reference cycle has no qualified declaration and fill path yet.'),
        ('class was not successfully declared with the native type', 'The required generated class was unavailable when typed values were imported.'),
        ('save your changes before reimporting', 'The editor considers this existing asset unsaved and preserves it.'),
        ('source widget member has no exact', 'A source widget field could not be matched to an owned widget of the same type.'),
        ('native-reflection-request-bound', 'Required Blueprint and native dependency descriptions could not be assembled.'),
        ('level-import-asset-not-in-the-project', 'A required asset or class is unavailable in the editor project.'),
        ('game-backed-layer-not-built', 'This source level could not be reconstructed; its unchanged original remains game-owned.'),
        ('exact source family differs', 'The editable actor did not reproduce its original saved properties.'),
        ('original layer projection starts qualified and unchanged', 'The saved level no longer compares unchanged after opening.'),
        ('snapshot-unavailable', 'The saved sublevel was not captured before its dependencies changed; it was preserved.'),
        ('nonfinite-property', 'The source contains a non-finite numeric property that the decoder cannot reconstruct.'),
        ('invalid-name', 'The decoder could not resolve a name in this asset’s source layout.'),
        ('cachedhash', 'A stored native hash differs from the reconstructed asset’s hash.'),
        ('cook-differs', 'The reconstructed asset still differs from the original game data.'),
        ('changes when opened', 'Opening the reconstructed level changes original actor or component data.'),
        ('which did not import', 'A required actor class was refused; its original game reference remains.'),
        ('not imported because', 'A required dependency was refused, so this asset could not be reconstructed.'),
        ('needs a package that did not import', 'A required dependency was refused, so this asset could not be reconstructed.'),
        ('native-data-not-brought-over', 'This native asset layout is not yet supported by the importer.'),
        ('properties-not-read', 'Some source properties are not yet decoded completely.'),
    ):
        if needle in text:
            return explanation
    return 'The importer refused this asset; the recorded diagnostic below gives its remaining limitation.'


def import_duration(row):
    # The sweep's historical import_seconds column is a timing breakdown.
    # Keep that evidence intact and extract its total for comparisons.
    value = row.get('import_total_seconds', row.get('import_seconds'))
    try:
        return float(value)
    except (TypeError, ValueError):
        match = re.search(r'(?:^|\s)total=([0-9]+(?:\.[0-9]+)?)', str(value or ''))
        return float(match.group(1)) if match else None


def captured(native, folder):
    images = {item.get('image') for item in native.get('captures', [])}
    return (native.get('capture_verified') is True and native.get('shader_probe_ready') is True
            and images == {name + '.jpg' for name in VIEWS}
            and all((folder / (name + '.jpg')).is_file() and
                    (folder / (name + '.jpg')).stat().st_size > 128 for name in VIEWS))


def resume_verified(row, gallery):
    folder = Path(gallery)/(row['map'].rsplit('/',1)[-1]+'_'+row.get('variant','day'))
    try:
        native = json.loads((folder/'capture.json').read_text(encoding='utf-8-sig'))
        return (native.get('map') == row['map'] and native.get('variant') == row.get('variant','day')
                and captured(native,folder))
    except (OSError, ValueError):
        return False


def record(row, native, gallery, import_report=None):
    """A successful controller result alone cannot verify a map's visible scene."""
    gallery = Path(gallery)
    identity = row['map'].rsplit('/', 1)[-1] + '_' + row.get('variant', 'day')
    folder = gallery / identity
    captures = native.get('captures', [])
    verified = captured(native,folder)
    for key in FIELDS:
        if key in native:
            row[key] = native[key]
    row['capture_status'] = 'verified' if verified else 'not verified'
    if not verified:
        row['imported'] = False
        row['exit'] = row.get('exit') or 1
        row['capture_error'] = native.get('capture_error') or 'Four shader-ready SceneCapture2D JPEGs are unavailable'
    counts, kinds, refusals, projections = Counter(), defaultdict(Counter), [], []
    if import_report and Path(import_report).is_file():
        data = json.loads(Path(import_report).read_text(encoding='utf-8-sig'))
        assets=dict(data.get('assets',{}))
        known={package.casefold() for package in assets}
        # Older native reports omitted a per-asset reply for declaration failures.
        # Their explicit game-reference diagnoses must still appear in the gallery.
        for package,reason in data.get('game_reference_reasons',{}).items():
            if package.casefold() not in known:
                assets[package]=dict(valid=False,kind='unknown',error=reason)
                known.add(package.casefold())
        for package, asset in assets.items():
            if asset.get('game_backed'):
                projections.append(dict(package=package, reason=asset.get('game_backed_reason'),
                    uneditable_original_actors=asset.get('uneditable_original_actors', []),
                    uneditable_original_actor_count=asset.get('uneditable_original_actor_count'),
                    class_problems=asset.get('class_problems'), classes_that_differ=asset.get('classes_that_differ'),
                    values_not_held=asset.get('values_not_held'), changes_when_opened_details=asset.get('changes_when_opened_details')))
            if asset.get('valid') is not False:
                continue
            error = refusal_reason(package, str(asset.get('error') or asset.get('state') or 'No diagnostic'))
            refusals.append(dict(package=package, kind=asset.get('kind') or 'unknown', reason=error,
                                 first_refusal=asset.get('first_refusal')))
            codes = set(re.findall(r'(?:asset|level|mesh|material|blueprint)-import-[a-z0-9-]+', error))
            if not codes:
                low = error.lower()
                if any(text in low for text in ('not imported because', 'needs a package that did not import',
                       'which did not import', 'required declared-class dependency did not import completely')):
                    codes = {'Dependency refused'}
                elif 'source widget member has no exact owned widget' in low:
                    codes = {'Widget source member has no owned widget'}
                elif 'save your changes before reimporting this asset' in low:
                    codes = {'Dirty asset blocks reimport'}
                else:
                    codes = {re.sub(r'/Game/[^\s\"\']+', '<package>', error).split('\n')[0][:160]}
            for code in codes:
                counts[code] += 1
                kinds[code][asset.get('kind') or 'unknown'] += 1
    item = {key: row.get(key) for key in ('map', 'variant', 'reached', 'imported', 'assets', 'new_assets', 'preserved_assets', 'missing_assets',
            'reference_only_assets', 'levels', 'loaded_levels', 'seconds', 'import_seconds', 'exit', 'error', 'crash_frames', 'qualification',
            'editable_original_actors', 'uneditable_original_actors', 'game_backed_levels') + FIELDS}
    item.update(identity=identity, captures=captures if verified else [],
                import_total_seconds=import_duration(row),
                top_refusals=[dict(reason=key, count=count, kinds=dict(kinds[key])) for key, count in counts.most_common(6)],
                refusals=refusals, projections=projections,
                camera_policy=native.get('camera_policy'), player_start=native.get('player_start'),
                preview_lighting=native.get('preview_lighting'), shader_probe_pixel=native.get('shader_probe_pixel'),
                component_audit_summary=component_summary(native.get('component_audit', [])),
                component_audit_candidate_count=native.get('component_audit_candidate_count'),
                component_audit_truncated=native.get('component_audit_truncated'))
    gallery.mkdir(parents=True, exist_ok=True)
    (gallery / (identity + '.row.json')).write_text(json.dumps(item, indent=2), encoding='utf-8')
    render(gallery)
    return verified


def render(gallery):
    gallery = Path(gallery)
    rows = [json.loads(path.read_text(encoding='utf-8')) for path in sorted(gallery.glob('*.row.json'))]
    blocks = []
    e = lambda value: escape(str(value if value is not None else 'unavailable'))
    for row in rows:
        imported_label = ('captured partial editor map; some original actors remain game-owned' if row.get('uneditable_original_actors') else
                          'captured supported editor projection; original game references retained' if row.get('qualification') else
                          'captured editor scene; importer reported complete')
        if row.get('exit') not in (0,'0'):
            imported_label += '; subsequent editor checks failed'
        outcome = (imported_label if row['imported'] in (True, 'True') else
                   'captured editor preview; incomplete import/refusals') if row['capture_status'] == 'verified' else 'not verified'
        if row.get('visual_verified') is False and not row.get('empty_scene'):
            outcome += '; images do not yet verify visible map content'
        pictures = ''.join('<figure><img loading="lazy" src="{}/{}" width="1280" height="720"><figcaption>{}</figcaption></figure>'.format(
            e(row['identity']), e(image['image']), e(image.get('label') or image['image'].removesuffix('.jpg')))
                           for image in row['captures'] + row.get('additional_captures', []))
        reasons = ''.join('<li>{}: {} ({})</li>'.format(e(reason['reason']), reason['count'],
            e(', '.join('{} {}'.format(kind, count) for kind, count in reason['kinds'].items()))) for reason in row['top_refusals'])
        blocks.append('<section id="{}"><h2>{}</h2><b>{}</b><p>Phase {}; {} s. Accepted assets {}; refused {}; reference only {}; levels {} (loaded {}, capture {}). Geometry {}; terrain {}.</p><p>{}</p><div class="images">{}</div><p>Player start: {}. {}</p><p>Missing material slots: {}; materials without shaders: {}.</p><details><summary>Camera policy</summary><p>{}</p></details><ul>{}</ul><details><summary>Crash, timeout or refusal diagnostics</summary><pre>{}</pre></details></section>'.format(
            e(row['identity']), e(row['map']), e(outcome), e(row['reached']), e(row['seconds']), e(row['assets']), e(row['missing_assets']),
            e(row['reference_only_assets']), e(row['levels']), e(row['loaded_levels']), e(row.get('capture_loaded_levels')),
            e(row.get('visible_geometry_components')), e(row.get('terrain_components')), e(row.get('capture_scene') or row.get('capture_error')),
            pictures, e(row.get('player_start')), e(row.get('preview_lighting')),
            e(row.get('capture_missing_material_slots')),
            e(len(row['capture_materials_without_shaders']) if isinstance(row.get('capture_materials_without_shaders'),list) else None),
            e(row.get('camera_policy')), reasons,
            e(str(row.get('crash_frames') or '') + '\n' + str(row.get('error') or '') + '\n' + str(row.get('capture_error') or ''))))
        refusal_rows = ''.join('<li><code>{}</code> ({}): {}<details><summary>Recorded diagnostic</summary><pre>{}</pre></details></li>'.format(
            e(item['package']), e(item['kind']), e(explain_refusal(item.get('first_refusal') or item['reason'])),
            e((item['first_refusal']+'\n' if item.get('first_refusal') else '')+item['reason']))
                               for item in row.get('refusals', []))
        duration = import_duration(row)
        timing = 'Import: {} s.'.format(e(duration))
        if '/Cave/' in row['map'] and duration is not None and duration > 600:
            timing += ' Cave import exceeded ten minutes; investigate.'
        if row.get('editable_original_actors') is not None:
            timing += ' Original actors: {} editable; {} retained in the game.'.format(e(row['editable_original_actors']), e(row.get('uneditable_original_actors')))
        if row.get('preserved_assets') is not None:
            timing += ' Accepted asset replies include {} existing assets reused in this project.'.format(e(row['preserved_assets']))
        blocks[-1] = blocks[-1].removesuffix('</section>') + '<p>'+timing+'</p><details><summary>Every refused asset ({})</summary><ul>{}</ul></details></section>'.format(len(row.get('refusals', [])), refusal_rows)
        if row.get('projections'):
            blocks[-1] = blocks[-1].removesuffix('</section>') + '<details><summary>Levels retaining game-owned content</summary><pre>'+e(json.dumps(row['projections'], indent=2))+'</pre></details></section>'
        if row.get('visual_review'):
            blocks[-1] = blocks[-1].removesuffix('</section>') + '<p><b>Image review:</b> ' + e(row['visual_review']) + '</p></section>'
        if row.get('component_audit_candidate_count') is not None:
            audit = '<details><summary>Construction and water component audit ({})</summary>'.format(
                e(row['component_audit_candidate_count']))
            audit += '<p>Class and material name candidates; hidden layers are recorded too. '
            audit += 'Registered, visible components still require screenshot inspection.</p>'
            summary = row.get('component_audit_summary')
            if summary is None:
                summary = component_summary(row.get('component_audit', []))
            audit += '<ul>'
            for group in summary:
                audit += '<li>{}: {} actors, {} in visible layers, {} registered visible meshes; {}.</li>'.format(
                    e(group['class'].rsplit('.', 1)[-1]), e(group['actors']), e(group['visible_actors']),
                    e(group['visible_meshes']), e(', '.join(group['class_imports'])))
            audit += '</ul><p><a href="{}/capture.json">Component paths, mesh assets, materials and visibility</a></p>'.format(e(row['identity']))
            if row.get('component_audit_truncated'):
                audit += '<p>The bounded candidate list is truncated.</p>'
            blocks[-1] = blocks[-1].removesuffix('</section>') + audit + '</details></section>'
    totals = '<p>{} map rows; {} verified four-image captures; {} passed editor checks; {} retain game-owned actors.</p>'.format(
        len(rows),sum(row.get('capture_status')=='verified' for row in rows),
        sum(row.get('imported') in (True,'True') and row.get('exit') in (0,'0') for row in rows),
        sum(bool(row.get('uneditable_original_actors')) for row in rows))
    reports = [('game-vs-editor.html', 'Matching game and editor views'),
               ('construction-actors.html', 'Construction actors, per map'),
               ('creator-checks.html', 'Reported visual defects and verification')]
    comparison = ''.join('<p><a href="{}">{}</a></p>'.format(e(path), e(label))
                         for path, label in reports if (gallery/path).is_file())
    page = '<!doctype html><meta charset="utf-8"><title>Kadan editor import gallery</title><style>body{font:16px system-ui;background:#171b22;color:#e7edf6;max-width:1500px;margin:32px auto;padding:0 20px}section{border-top:1px solid #536177;margin-top:30px;padding-top:12px}h2{overflow-wrap:anywhere;font-size:22px}.images{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px}figure{margin:0}img{width:100%;height:auto;background:#000}figcaption{color:#b7c7de}pre{white-space:pre-wrap;overflow-wrap:anywhere;font-size:13px}b{color:#ffd583}a{color:#a8caff}@media(max-width:700px){.images{grid-template-columns:1fr}}</style><h1>Kadan editor import gallery</h1>'+totals+comparison+'<p>Editor evidence only. Four SceneCapture2D render-target views per verified preview; all pending shaders finished and a red unlit probe rendered red. Lighting is raised for inspection; these are not game-appearance captures. Missing pictures mean not verified. A captured incomplete scene is not a complete import.</p><p>Camera policy: each map records its overview footprint, start or fallback view, terrain views and any measured game camera overrides below.</p>' + ''.join(blocks)
    page=page.replace('<p>Camera policy:', '<p>Counts describe this import run, including accepted existing packages. Reference-only status can overlap accepted or refused status; do not add these columns. One project is reused, so later imports benefit from earlier work. Measured game camera overrides are identified per map.</p><p>Camera policy:',1)
    (gallery / 'index.html').write_text(page, encoding='utf-8')
    (gallery / 'summary.json').write_text(json.dumps(rows, indent=2), encoding='utf-8')
    size = sum(path.stat().st_size for path in gallery.rglob('*') if path.is_file())
    if size > 58 * 1024 * 1024:
        raise RuntimeError('Gallery exceeds the 58 MiB fetch budget; reduce JPEG quality before fetching')
