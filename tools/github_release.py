"""Prepare public release files from an exact, already qualified native archive."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import zipfile

if __package__:
    from . import native_quality_gate, native_release_install, native_release_publish
else:
    import native_quality_gate
    import native_release_install
    import native_release_publish

ROOT = Path(__file__).resolve().parents[1]
MAX_EVIDENCE = 32 * 1024 * 1024
MAX_NOTES = 256 * 1024


def digest(path):
    value = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def git(root, *arguments):
    result = subprocess.run(['git', '--no-optional-locks', '-C', str(root), *arguments],
                            capture_output=True, text=True, encoding='utf-8', check=False)
    if result.returncode:
        raise ValueError('The tagged source identity could not be read')
    return result.stdout.strip()


def release_identity(root, tag):
    if not isinstance(tag, str) or len(tag) > 128:
        raise ValueError('Use a version tag of at most 128 characters')
    match = re.fullmatch(r'v((?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*))(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?', tag)
    if not match or (match.group(2) and any(part.isdigit() and len(part) > 1 and part.startswith('0')
                                          for part in match.group(2).split('.'))):
        raise ValueError('Use an existing version tag such as v0.3.0 or v0.3.0-rc.1')
    descriptor = json.loads((Path(root) / 'Kadan/Kadan.uplugin').read_text(encoding='utf-8'))
    if descriptor.get('VersionName') != match.group(1) or type(descriptor.get('Version')) is not int:
        raise ValueError('The tag must match Kadan.uplugin VersionName')
    return dict(tag=tag, version=match.group(1), plugin_version=descriptor['Version'],
                prerelease=match.group(2) is not None)


def source_identity(root, tag):
    root = Path(root).resolve()
    if Path(git(root, 'rev-parse', '--show-toplevel')).resolve() != root:
        raise ValueError('Release source must be the Git checkout root')
    if git(root, 'status', '--porcelain=v1', '--untracked-files=all', '--ignore-submodules=none'):
        raise ValueError('Release preparation requires clean, committed source')
    commit, tree = git(root, 'rev-parse', 'HEAD'), git(root, 'rev-parse', 'HEAD^{tree}')
    if not all(re.fullmatch(r'[0-9a-f]{40}|[0-9a-f]{64}', value) for value in (commit, tree)):
        raise ValueError('Git returned an invalid source identity')
    if git(root, 'rev-parse', '--verify', 'refs/tags/' + tag + '^{commit}') != commit:
        raise ValueError('The release tag must still name the checked-out source commit')
    return dict(commit=commit, tree=tree)


def linked(path):
    return path.is_symlink() or bool(getattr(path.lstat(), 'st_file_attributes', 0) & 0x400)


def plain_path(path):
    original = Path(path).absolute()
    for parent in (original, *original.parents):
        if parent.is_symlink() or parent.exists() and linked(parent):
            raise ValueError('Release paths must not use filesystem links')
    return original.resolve()


def input_files(root, release_root, tag):
    folder = plain_path(release_root)
    if not folder.is_dir() or folder.is_relative_to(Path(root).resolve()):
        raise ValueError('KADAN_RELEASE_ROOT must be an existing directory outside the source checkout')
    candidate = plain_path(folder / tag)
    if candidate.parent != folder or not candidate.is_dir():
        raise ValueError('The release input folder must exist directly below KADAN_RELEASE_ROOT')
    result = {name: plain_path(candidate / name) for name in
              ('Kadan-UE4.zip', 'qualification.json', 'release-notes.md')}
    if any(path.parent != candidate or not path.is_file() for path in result.values()):
        raise ValueError('The release folder needs an archive, qualification.json, and release-notes.md')
    if not 0 < result['Kadan-UE4.zip'].stat().st_size < 2 * 1024 * 1024 * 1024:
        raise ValueError('The qualified archive is empty or exceeds the release asset limit')
    if not 0 < result['qualification.json'].stat().st_size <= MAX_EVIDENCE:
        raise ValueError('The private qualification file is empty or too large')
    if not 0 < result['release-notes.md'].stat().st_size <= MAX_NOTES:
        raise ValueError('The release notes are empty or too large')
    return result


def archive_inventory(path, release):
    with zipfile.ZipFile(path) as archive:
        for member in archive.infolist():
            kind = stat.S_IFMT(member.external_attr >> 16)
            if member.is_dir() or kind not in (0, stat.S_IFREG):
                raise ValueError('The native archive must contain ordinary files only')
        descriptor = json.loads(archive.read('Kadan/Kadan.uplugin'))
        if type(descriptor.get('Version')) is not int or \
                (descriptor.get('VersionName'), descriptor.get('Version')) != (release['version'], release['plugin_version']):
            raise ValueError('The archive version differs from the tagged source')
    return native_release_install.archive_members(path)


def prepare(root=ROOT, release_root=None, tag=None):
    root = Path(root).resolve()
    release = release_identity(root, tag)
    source = source_identity(root, tag)
    inputs = input_files(root, release_root, tag)
    artifact = inputs['Kadan-UE4.zip']
    artifact_hash = digest(artifact)
    members = archive_inventory(artifact, release)
    evidence_bytes = inputs['qualification.json'].read_bytes()
    if len(evidence_bytes) > MAX_EVIDENCE:
        raise ValueError('The qualification file grew beyond its size limit')
    evidence_hash = hashlib.sha256(evidence_bytes).hexdigest()
    evidence = json.loads(evidence_bytes.decode('utf-8'))
    policy_path = root / 'tools/native_quality_policy.json'
    policy = json.loads(policy_path.read_text(encoding='utf-8'))
    notes = inputs['release-notes.md'].read_text(encoding='utf-8')
    if not notes.strip() or '\0' in notes or len(notes) > 65500:
        raise ValueError('Release notes must contain text within GitHub release limits')
    out = plain_path(root / 'build/github-release')
    if not out.is_relative_to(root / 'build') or out == root / 'build':
        raise ValueError('Release outputs must remain inside build/')
    archive_name = 'Kadan-' + tag[1:] + '-UE4-Win64.zip'
    expected = {archive_name, 'LICENSE', 'release.json', 'SHA256SUMS', 'release-notes.md'}
    out.mkdir(parents=True, exist_ok=True)
    if any(path.name not in expected or not path.is_file() or linked(path) for path in out.iterdir()):
        raise ValueError('The release output folder contains unrelated files')
    for name in ('release.json', 'SHA256SUMS'):
        (out / name).unlink(missing_ok=True)
    report = native_release_publish.publish(artifact, out / archive_name, evidence, policy, root)
    if not report['ready']:
        raise ValueError('Native qualification refused the archive; inspect it with native_quality_gate.py')
    if digest(artifact) != artifact_hash or digest(out / archive_name) != artifact_hash:
        raise ValueError('Qualified archive bytes changed during release preparation')
    license_path = plain_path(root / 'LICENSE')
    if license_path.parent != root or not license_path.is_file():
        raise ValueError('The source license is missing or linked')
    (out / 'LICENSE').write_bytes(license_path.read_bytes())
    (out / 'release-notes.md').write_text(notes, encoding='utf-8')
    if source_identity(root, tag) != source:
        raise ValueError('Source or tag changed during release preparation')
    if digest(inputs['qualification.json']) != evidence_hash:
        raise ValueError('Private qualification changed during release preparation')
    files = {name: dict(bytes=(out / name).stat().st_size, sha256=digest(out / name))
             for name in (archive_name, 'LICENSE')}
    receipt = dict(format=1, **release, source=source, files=files, plugin_members=members,
                   qualification=dict(engine=policy['engine'], platform=policy['platform'],
                                      evidence_sha256=evidence_hash,
                                      policy_sha256=digest(policy_path), checks=policy['required_checks']))
    (out / 'release.json').write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    sums = {name: info['sha256'] for name, info in files.items()}
    sums['release.json'] = digest(out / 'release.json')
    (out / 'SHA256SUMS').write_text(''.join(value + '  ' + name + '\n'
                                           for name, value in sorted(sums.items())), encoding='utf-8')
    return dict(tag=tag, source_commit=source['commit'], archive=archive_name,
                prerelease=release['prerelease'], files=sorted(expected - {'release-notes.md'}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tag', required=True)
    parser.add_argument('--release-root', default=os.environ.get('KADAN_RELEASE_ROOT'))
    args = parser.parse_args()
    if not args.release_root:
        parser.error('Set KADAN_RELEASE_ROOT to the private qualified-artifact directory')
    try:
        print(json.dumps(prepare(release_root=args.release_root, tag=args.tag), sort_keys=True))
    except (ValueError, OSError, KeyError, zipfile.BadZipFile, subprocess.SubprocessError) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
