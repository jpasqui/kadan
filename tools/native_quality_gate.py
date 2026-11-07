"""The release gate of the native kit.

A release is decided by the acceptance chain (one original map imported, compared with the game's
files, edited, saved and reopened, exported, checked for game assets and played) and the other
required checks in native_quality_policy.json, all on the exact artifact and the current sources.
Test coverage is measured and reported here. It does not decide a release.
"""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
POLICY = Path(__file__).with_name('native_quality_policy.json')

def source_inventory(root, policy):
    excluded = {entry['path'] for entry in policy['coverage_exclusions']}
    return {scope:{path.relative_to(root).as_posix():hashlib.sha256(path.read_bytes()).hexdigest()
                   for pattern in patterns for path in root.glob(pattern)
                   if path.is_file() and path.suffix in ('.h','.hpp','.inl','.c','.cc','.cpp','.s','.S','.asm') and
                       path.relative_to(root).as_posix() not in excluded}
            for scope,patterns in policy['coverage_scopes'].items()}

def qualification_inventory(root,policy):
    result={key:value for sources in source_inventory(root,policy).values() for key,value in sources.items()}
    for pattern in policy.get('qualification_inputs',[]):
        for path in root.glob(pattern):
            if path.is_file():result[path.relative_to(root).as_posix()]=hashlib.sha256(path.read_bytes()).hexdigest()
    return result

def coverage_report(policy, evidence, inventory):
    """What the coverage evidence shows for each scope: counts, percentages against the targets, and
    why a scope does not count as measured. For the record only."""
    targets = policy.get('coverage_targets', {})
    result = {}
    for scope, sources in inventory.items():
        report = evidence.get('coverage', {}).get(scope, {})
        notes = []
        for name, value in policy.get('coverage_contracts', {}).get(scope, {}).items():
            if type(report.get(name)) is not type(value) or report.get(name) != value:
                notes.append(name + ' compiler target contract missing or mismatched')
        if not sources:
            notes.append('empty source inventory')
        if report.get('sources') != sources:
            notes.append('source hashes incomplete or stale')
        files = report.get('files')
        complete = isinstance(files, dict) and set(files) == set(sources) and report.get('uninstrumented') == []
        if not complete:
            notes.append('compiler coverage file census incomplete or unmeasured')
        row = dict(notes=notes)
        for kind in ('lines', 'branches'):
            total = report.get(kind, {})
            count = total.get('count')
            covered = total.get('covered')
            if type(count) is not int or type(covered) is not int or count <= 0 or not 0 <= covered <= count:
                notes.append(kind + ' coverage unmeasured or invalid')
                continue
            percent = 100 * covered / count
            target = targets.get(kind)
            row[kind] = dict(count=count, covered=covered, percent=round(percent, 6), target=target,
                             meets_target=target is None or percent >= target)
            if complete:
                records = [record.get(kind, {}) if isinstance(record, dict) else {} for record in files.values()]
                valid = all(type(item.get('count')) is int and type(item.get('covered')) is int and
                            0 <= item['covered'] <= item['count'] for item in records)
                if not valid or sum(item['count'] for item in records) != count or sum(item['covered'] for item in records) != covered:
                    notes.append(kind + ' totals differ from compiler file counts')
        row['measured'] = not notes
        result[scope] = row
    return result

def check(root, policy, evidence, artifact):
    errors=[]
    inventory=source_inventory(root,policy)
    if evidence.get('format') != 1:errors.append('Missing evidence format 1')
    if evidence.get('engine') != policy['engine'] or evidence.get('platform') != policy['platform']:
        errors.append('Engine/platform qualification mismatch')
    regressions=evidence.get('open_regressions')
    if not isinstance(regressions,list):
        errors.append('Open regression inventory missing or invalid')
    elif regressions:
        errors.append('Unresolved regressions block release')
    all_sources=qualification_inventory(root,policy)
    for name in policy['required_checks']:
        report=evidence.get('checks',{}).get(name,{})
        if report.get('passed') is not True:errors.append(name+': qualification missing or failed')
        if report.get('sources') != all_sources:errors.append(name+': source qualification stale or incomplete')
    # The chain is one piece of work on one map: its links cannot be passed on different maps.
    chain=[link['check'] for link in policy.get('acceptance_chain',[])]
    for name in chain:
        if name not in policy['required_checks']:errors.append(name+': acceptance chain link is not a required check')
    maps=[evidence.get('checks',{}).get(name,{}).get('map') for name in chain]
    if chain and (len(set(maps))!=1 or not isinstance(maps[0],str) or not maps[0]):
        errors.append('Acceptance chain: every link must name the same original map')
    if not artifact.is_file():errors.append('Exact release artifact missing')
    elif evidence.get('artifact_sha256') != hashlib.sha256(artifact.read_bytes()).hexdigest():
        errors.append('Exact release artifact hash does not match qualification')
    return dict(format=1, ready=not errors, errors=errors, sources=inventory,
                coverage=coverage_report(policy,evidence,inventory))

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence',type=Path,required=True)
    parser.add_argument('--artifact',type=Path,required=True)
    args=parser.parse_args()
    policy=json.loads(POLICY.read_text(encoding='utf-8'))
    evidence=json.loads(args.evidence.read_text(encoding='utf-8'))
    report=check(ROOT,policy,evidence,args.artifact)
    print(json.dumps(report,indent=2))
    return 0 if report['ready'] else 1

if __name__=='__main__':raise SystemExit(main())
