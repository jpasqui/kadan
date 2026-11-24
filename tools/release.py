"""Build a release on the desktop from a clean checkout of its tag, and bring the archive back.

    py -3.11 tools/release.py build [--prerelease] [--mirror N]

Run on the laptop, in a clean checkout whose HEAD carries the tag v<VersionName of Kadan/Kadan.uplugin>.
It refuses a checkout with uncommitted or untracked files, a HEAD without that tag, and a tag that
disagrees with VersionName. It then writes the source record, private/release-source.json: the commit,
tag and version, and the SHA-256 of every tracked file under the folders the build reads. Through
tools/remote_unreal.py it sends every tracked file and the record to the desktop's mirror, runs
tools/build_native_release.py there (which refuses a mirror that differs from the record), and fetches
the archive and its SHA256SUMS into private/desktop-results. Last it prints the gh command that uploads
them as a draft release; this tool does not run gh.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import sys
import zipfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
import build_native_release as build  # noqa: E402

RECORD = 'private/release-source.json'
FETCHED = 'private/desktop-results'


class Refused(Exception):
    """A check refused the release; the message says what to do."""


def git(root, *arguments):
    done = subprocess.run(['git', '-C', str(root), *arguments], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if done.returncode:
        raise Refused('git {} failed in {}: {}'.format(' '.join(arguments), root,
                                                     done.stderr.decode('utf-8', 'replace').strip()))
    return done.stdout.decode('utf-8')


def digest(path):
    value = hashlib.sha256()
    with Path(path).open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def check_checkout(root):
    """The commit, tag and version of a clean checkout whose HEAD carries the tag v<VersionName>."""
    dirty = [line for line in git(root, 'status', '--porcelain', '--untracked-files=all').splitlines() if line]
    if dirty:
        raise Refused('The checkout {} has {} uncommitted or untracked files ({}{}): a release is built from a clean '
                      'checkout of its tag. Commit or remove them, or use a fresh worktree of the tag.'.format(
                          root, len(dirty), ', '.join(line[3:] for line in dirty[:5]), ', ...' if len(dirty) > 5 else ''))
    version = build.plugin_version(root)
    tag = 'v' + version
    commit = git(root, 'rev-parse', 'HEAD').strip()
    tags = git(root, 'tag', '--points-at', 'HEAD').split()
    if tag not in tags:
        others = [name for name in tags if name.startswith('v')]
        if others:
            raise Refused('HEAD carries the tag {}, but Kadan/Kadan.uplugin says VersionName {}, so the tag would be {}. '
                          'Make VersionName and the tag agree in a new commit and tag that.'.format(
                              ', '.join(others), version, tag))
        tagged = git(root, 'tag', '--list', tag).strip()
        if tagged:
            raise Refused('The tag {} names commit {}, not HEAD ({}). Check out the tag (git checkout {}) and run '
                          'again.'.format(tag, git(root, 'rev-parse', tag + '^{commit}').strip()[:12], commit[:12], tag))
        raise Refused('HEAD ({}) has no tag {} (Kadan/Kadan.uplugin says VersionName {}). Tag the commit to '
                      'release with git tag {} and run again.'.format(commit[:12], tag, version, tag))
    return dict(commit=commit, tag=tag, version=version)


def source_record(root, checkout, prerelease):
    """The record build_native_release.py checks the mirror against."""
    names = [name for name in git(root, 'ls-files', '-z', '--', *build.SOURCE_FOLDERS).split('\0') if name]
    files = {name: digest(Path(root) / name) for name in sorted(names) if (Path(root) / name).is_file()}
    return dict(format=build.RECORD_FORMAT, commit=checkout['commit'], tag=checkout['tag'],
                kadan_version=checkout['version'], prerelease=prerelease, files=files)


def runner(mirror):
    """Runs tools/remote_unreal.py with these arguments and returns its exit code."""
    def run(*arguments):
        command = [sys.executable, str(HERE / 'remote_unreal.py')]
        if mirror:
            command += ['--mirror', str(mirror)]
        print('>', ' '.join(command[1:] + list(arguments)), flush=True)
        return subprocess.run(command + list(arguments), cwd=str(ROOT)).returncode
    return run


def check_fetched(folder, archive_name, checkout):
    """The fetched archive matches its SHA256SUMS and names the commit and tag it was built from."""
    archive, sums = folder / archive_name, folder / 'SHA256SUMS'
    if not archive.is_file() or not sums.is_file():
        raise Refused('The archive or SHA256SUMS did not arrive in ' + str(folder) + ': fetch them again with '
                      'py -3.11 tools/remote_unreal.py fetch')
    if sums.read_text(encoding='ascii').split() != [digest(archive), archive_name]:
        raise Refused(str(sums) + ' does not match ' + str(archive) + ': fetch both again')
    with zipfile.ZipFile(archive) as opened:
        built = json.loads(opened.read('Kadan/BUILD.json'))
    if (built.get('commit'), built.get('tag')) != (checkout['commit'], checkout['tag']):
        raise Refused('{} was built from {} {}, not {} {}: an older archive is in the desktop\'s output folder. '
                      'Remove private/release-{} there and run again.'.format(
                          archive, built.get('tag'), built.get('commit'), checkout['tag'], checkout['commit'],
                          checkout['version']))
    return archive, sums


def gh_command(checkout, prerelease, archive, sums):
    words = ['gh', 'release', 'create', checkout['tag'], '--verify-tag', '--draft',
             '--title', 'Kadan ' + checkout['tag'], '--notes', '']
    if prerelease:
        words.append('--prerelease')
    words += [Path(archive).as_posix(), Path(sums).as_posix()]
    return ' '.join(shlex.quote(word) for word in words)


def release(root, prerelease, run):
    """Check the checkout, send it with its record, build on the desktop and fetch the result."""
    root = Path(root)
    checkout = check_checkout(root)
    record = source_record(root, checkout, prerelease)
    path = root / RECORD
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(record, indent=1) + '\n', encoding='utf-8')
    output = 'private/release-' + checkout['version']
    archive_name = build.archive_name(checkout['version'], prerelease)
    name = 'release-' + checkout['version']
    steps = [('sync', '--all'), ('push', RECORD),
             ('run', '--no-sync', '--background', name, '--', 'py', 'tools/build_native_release.py', '--out', output),
             ('wait', name),
             ('fetch', output + '/' + archive_name, output + '/SHA256SUMS')]
    for step in steps:
        code = run(*step)
        if code:
            raise Refused('tools/remote_unreal.py {} ended with exit code {}. Its output above says why; '
                          'py -3.11 tools/remote_unreal.py log {} shows the build\'s.'.format(step[0], code, name))
    archive, sums = check_fetched(root / FETCHED / output, archive_name, checkout)
    return checkout, archive.relative_to(root), sums.relative_to(root)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='action', required=True)
    one = sub.add_parser('build', help='build the tagged release on the desktop and fetch it')
    one.add_argument('--prerelease', action='store_true', help='mark the release a pre-release')
    one.add_argument('--mirror', type=int, help='the desktop mirror to use (tools/remote_unreal.py --mirror)')
    args = parser.parse_args(argv)
    try:
        checkout, archive, sums = release(ROOT, args.prerelease, runner(args.mirror))
    except Refused as error:
        print('Release refused: ' + str(error), file=sys.stderr)
        return 1
    print('\nBuilt {} from commit {}.'.format(archive, checkout['commit']))
    print('Upload it as a draft release, then write the notes and publish it on GitHub:\n')
    print(gh_command(checkout, args.prerelease, archive, sums))
    return 0


if __name__ == '__main__':
    sys.exit(main())
