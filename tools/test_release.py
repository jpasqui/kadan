"""Host tests of the release path: the laptop's checks and source record (tools/release.py) with a fake
desktop runner, and the desktop's checks in tools/build_native_release.py. No Unreal, no connection."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import build_native_release as build  # noqa: E402
import release  # noqa: E402

COMMIT_ENVIRONMENT = dict(GIT_AUTHOR_NAME='Fixture', GIT_AUTHOR_EMAIL='fixture@example.invalid',
                          GIT_COMMITTER_NAME='Fixture', GIT_COMMITTER_EMAIL='fixture@example.invalid')


def git(root, *arguments):
    subprocess.run(['git', '-C', str(root), '-c', 'commit.gpgsign=false', '-c', 'tag.gpgsign=false', *arguments],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=dict(os.environ, **COMMIT_ENVIRONMENT))


class Checkout(unittest.TestCase):
    """A small authored repository standing in for a checkout of the project."""

    def setUp(self):
        self.root = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.root, ignore_errors=True)
        files = {'Kadan/Kadan.uplugin': json.dumps(dict(VersionName='1.2.3')), 'core/a.h': '// a\n',
                 'tools/b.py': '# b\n', 'navigation/c.txt': 'c\n', 'tests/d.cpp': '// d\n', 'README.md': 'readme\n',
                 '.gitignore': '/private/\n'}
        for name, text in files.items():
            (self.root / name).parent.mkdir(parents=True, exist_ok=True)
            (self.root / name).write_text(text, encoding='utf-8')
        git(self.root, 'init', '-q')
        git(self.root, 'add', '-A')
        git(self.root, 'commit', '-q', '-m', 'fixture')
        git(self.root, 'tag', 'v1.2.3')
        self.commit = subprocess.run(['git', '-C', str(self.root), 'rev-parse', 'HEAD'], stdout=subprocess.PIPE,
                                     check=True).stdout.decode('ascii').strip()

    def commit_change(self):
        (self.root / 'core/a.h').write_text('// changed\n', encoding='utf-8')
        git(self.root, 'commit', '-q', '-am', 'change')


class LaptopChecks(Checkout):
    def test_clean_tagged_checkout_is_accepted(self):
        self.assertEqual(release.check_checkout(self.root), dict(commit=self.commit, tag='v1.2.3', version='1.2.3'))

    def test_uncommitted_and_untracked_files_are_refused(self):
        (self.root / 'core/a.h').write_text('// edited\n', encoding='utf-8')
        with self.assertRaisesRegex(release.Refused, 'uncommitted or untracked files .*core/a.h'):
            release.check_checkout(self.root)
        git(self.root, 'checkout', '--', 'core/a.h')
        (self.root / 'tools/new.py').write_text('# new\n', encoding='utf-8')
        with self.assertRaisesRegex(release.Refused, 'tools/new.py'):
            release.check_checkout(self.root)

    def test_ignored_files_do_not_count(self):
        (self.root / 'private').mkdir()
        (self.root / 'private/release-source.json').write_text('{}', encoding='utf-8')
        self.assertEqual(release.check_checkout(self.root)['commit'], self.commit)

    def test_head_without_the_tag_is_refused(self):
        self.commit_change()
        with self.assertRaisesRegex(release.Refused, r'tag v1.2.3 names commit .*git checkout v1.2.3'):
            release.check_checkout(self.root)
        git(self.root, 'tag', '-d', 'v1.2.3')
        with self.assertRaisesRegex(release.Refused, 'has no tag v1.2.3 .*git tag v1.2.3'):
            release.check_checkout(self.root)

    def test_tag_that_disagrees_with_version_name_is_refused(self):
        self.commit_change()
        git(self.root, 'tag', 'v1.2.4')
        with self.assertRaisesRegex(release.Refused, 'HEAD carries the tag v1.2.4, but Kadan/Kadan.uplugin says VersionName 1.2.3'):
            release.check_checkout(self.root)

    def test_source_record_lists_the_tracked_files_the_build_reads(self):
        record = release.source_record(self.root, release.check_checkout(self.root), True)
        self.assertEqual(set(record['files']), {'Kadan/Kadan.uplugin', 'core/a.h', 'tools/b.py', 'navigation/c.txt'})
        self.assertEqual(record['files']['core/a.h'], release.digest(self.root / 'core/a.h'))
        self.assertEqual((record['commit'], record['tag'], record['kadan_version'], record['prerelease']),
                         (self.commit, 'v1.2.3', '1.2.3', True))


class FakeDesktop:
    """Stands in for tools/remote_unreal.py: a mirror folder, the source check, and a built archive."""

    def __init__(self, root, mirror, change=None, built_commit=None):
        self.root, self.mirror, self.change, self.built_commit = root, mirror, change, built_commit
        self.calls = []

    def __call__(self, *arguments):
        self.calls.append(arguments)
        action = arguments[0]
        if action == 'sync':
            names = subprocess.run(['git', '-C', str(self.root), 'ls-files', '-z'], stdout=subprocess.PIPE,
                                   check=True).stdout.decode('utf-8').split('\0')
            for name in filter(None, names):
                (self.mirror / name).parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(self.root / name, self.mirror / name)
            if self.change:
                (self.mirror / self.change).write_text('stale\n', encoding='utf-8')
        elif action == 'push':
            (self.mirror / arguments[1]).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(self.root / arguments[1], self.mirror / arguments[1])
        elif action == 'run':
            try:
                record = build.check_source(build.read_source_record(self.mirror / release.RECORD), self.mirror)
            except ValueError as error:
                self.refusal = str(error)
                return 2
            output = self.mirror / arguments[arguments.index('--out') + 1]
            output.mkdir(parents=True)
            archive = output / build.archive_name(record['kadan_version'], record['prerelease'])
            with zipfile.ZipFile(archive, 'w') as target:
                target.writestr('Kadan/BUILD.json', build.build_record(
                    dict(record, commit=self.built_commit or record['commit']), 'fixture-build'))
            build.write_sums(archive)
        elif action == 'wait':
            return 0
        elif action == 'fetch':
            for name in arguments[1:]:
                target = self.root / release.FETCHED / name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(self.mirror / name, target)
        return 0


class ReleaseWithFakeDesktop(Checkout):
    def setUp(self):
        super().setUp()
        self.mirror = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.mirror, ignore_errors=True)

    def test_release_sends_builds_fetches_and_prints_the_draft_command(self):
        desktop = FakeDesktop(self.root, self.mirror)
        checkout, archive, sums = release.release(self.root, True, desktop)
        self.assertEqual([call[0] for call in desktop.calls], ['sync', 'push', 'run', 'wait', 'fetch'])
        self.assertEqual(desktop.calls[0], ('sync', '--all'))
        self.assertEqual(desktop.calls[1], ('push', 'private/release-source.json'))
        self.assertEqual(desktop.calls[2][:4], ('run', '--no-sync', '--background', 'release-1.2.3'))
        self.assertEqual(archive.as_posix(), 'private/desktop-results/private/release-1.2.3/Kadan-UE4-1.2.3-prerelease.zip')
        command = release.gh_command(checkout, True, archive, sums)
        self.assertTrue(command.startswith('gh release create v1.2.3 --verify-tag --draft '))
        self.assertIn('--prerelease', command)
        self.assertTrue(command.endswith('Kadan-UE4-1.2.3-prerelease.zip private/desktop-results/private/release-1.2.3/SHA256SUMS'))
        self.assertNotIn('--prerelease', release.gh_command(checkout, False, archive, sums))
        self.assertEqual(json.loads((self.root / release.RECORD).read_text(encoding='utf-8'))['commit'], self.commit)

    def test_mirror_that_differs_from_the_tag_stops_the_build(self):
        desktop = FakeDesktop(self.root, self.mirror, change='core/a.h')
        with self.assertRaisesRegex(release.Refused, 'run ended with exit code 2'):
            release.release(self.root, False, desktop)
        self.assertRegex(desktop.refusal, 'not the content of tag v1.2.3 .*changed core/a.h')
        self.assertEqual([call[0] for call in desktop.calls], ['sync', 'push', 'run'])

    def test_file_left_in_the_mirror_stops_the_build(self):
        (self.mirror / 'Kadan/Source').mkdir(parents=True)
        (self.mirror / 'Kadan/Source/Left.cpp').write_text('// not in the tag\n', encoding='utf-8')
        desktop = FakeDesktop(self.root, self.mirror)
        with self.assertRaisesRegex(release.Refused, 'run ended with exit code 2'):
            release.release(self.root, False, desktop)
        self.assertRegex(desktop.refusal, 'not the content of tag v1.2.3 .*extra Kadan/Source/Left.cpp')
        self.assertEqual([call[0] for call in desktop.calls], ['sync', 'push', 'run'])

    def test_archive_from_another_commit_is_refused(self):
        desktop = FakeDesktop(self.root, self.mirror, built_commit='0' * 40)
        with self.assertRaisesRegex(release.Refused, 'was built from v1.2.3 0{40}'):
            release.release(self.root, False, desktop)

    def test_archive_that_does_not_match_its_sums_is_refused(self):
        desktop = FakeDesktop(self.root, self.mirror)
        checkout, archive, sums = release.release(self.root, False, desktop)
        with zipfile.ZipFile(self.root / archive, 'a') as target:
            target.writestr('extra.txt', 'extra')
        with self.assertRaisesRegex(release.Refused, 'does not match'):
            release.check_fetched((self.root / archive).parent, archive.name, checkout)

    def test_dirty_checkout_sends_nothing(self):
        (self.root / 'core/a.h').write_text('// edited\n', encoding='utf-8')
        desktop = FakeDesktop(self.root, self.mirror)
        with self.assertRaises(release.Refused):
            release.release(self.root, False, desktop)
        self.assertEqual(desktop.calls, [])


class DesktopChecks(unittest.TestCase):
    def setUp(self):
        self.folder = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.folder, ignore_errors=True)

    def record(self, **changes):
        record = dict(format=build.RECORD_FORMAT, commit='a' * 40, tag='v1.2.3', kadan_version='1.2.3',
                      prerelease=False, files={'Kadan/Kadan.uplugin': 'b' * 64})
        record.update(changes)
        path = self.folder / 'release-source.json'
        path.write_text(json.dumps(record), encoding='utf-8')
        return path

    def test_missing_or_malformed_record_is_refused(self):
        with self.assertRaisesRegex(ValueError, 'No source record .*tools/release.py build'):
            build.read_source_record(self.folder / 'absent.json')
        for changes in (dict(tag='v1.2.4'), dict(commit='abc'), dict(prerelease='no'), dict(files={}), dict(format='other')):
            with self.subTest(changes=changes), self.assertRaisesRegex(ValueError, 'incomplete'):
                build.read_source_record(self.record(**changes))
        for name in ('tests/a.cpp', '../outside.py', 'Kadan/../../x', 'C:/x/Kadan.uplugin', 'Kadan\\\\x'):
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, 'outside the release folders'):
                build.read_source_record(self.record(files={name: 'b' * 64}))

    def test_missing_file_in_the_mirror_is_refused(self):
        record = build.read_source_record(self.record())
        with self.assertRaisesRegex(ValueError, 'missing Kadan/Kadan.uplugin'):
            build.check_source(record, self.folder)

    def mirror(self):
        """A mirror that holds exactly its record's one file."""
        plugin = self.folder / 'Kadan/Kadan.uplugin'
        plugin.parent.mkdir()
        plugin.write_text(json.dumps(dict(VersionName='1.2.3')), encoding='utf-8')
        return build.read_source_record(self.record(files={'Kadan/Kadan.uplugin': build.sha256(plugin)}))

    def leave(self, name):
        path = self.folder / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('left\n', encoding='utf-8')
        return path

    def test_file_the_tag_does_not_have_is_refused(self):
        record = self.mirror()
        self.assertIs(build.check_source(record, self.folder), record)
        for name in ('Kadan/Source/Kadan/Private/Left.cpp', 'core/left.h', 'tools/left.py', 'tools/left.pyc',
                     'Kadan/Intermediate/Build/left.obj'):
            with self.subTest(name=name):
                path = self.leave(name)
                with self.assertRaisesRegex(ValueError, '1 of its files differ, extra ' + name + r'\. Delete the extra files'):
                    build.check_source(record, self.folder)
                path.unlink()

    def test_bytecode_folders_and_folders_the_build_does_not_read_are_let_be(self):
        record = self.mirror()
        for name in ('tools/__pycache__/release.cpython-312.pyc', 'tools/stubs/__pycache__/regenerate.cpython-312.pyc',
                     'tests/left_test.cpp', 'private/release-1.2.3/package-build.log', 'left.txt'):
            self.leave(name)
        self.assertIs(build.check_source(record, self.folder), record)
        self.assertEqual(build.mirror_files(self.folder), ['Kadan/Kadan.uplugin'])

    def test_every_difference_is_counted_and_the_first_five_named(self):
        record = self.mirror()
        for number in range(6):
            self.leave('core/left{}.h'.format(number))
        (self.folder / 'Kadan/Kadan.uplugin').write_text(json.dumps(dict(VersionName='9.9.9')), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, r'7 of its files differ, changed Kadan/Kadan.uplugin, extra core/left0.h, '
                                                r'.*extra core/left3.h, \.\.\.\. Delete the extra files'):
            build.check_source(record, self.folder)

    def tools(self):
        folders, pins = {}, {}
        for name, (executable, licenses) in build.TOOLS.items():
            folders[name] = self.folder / name
            folders[name].mkdir()
            (folders[name] / executable).write_bytes(name.encode('ascii'))
            for one in licenses:
                (folders[name] / one).write_text('license', encoding='utf-8')
            pins[name] = dict(file=executable, sha256=build.sha256(folders[name] / executable), bytes=len(name))
        return folders, pins

    def test_tools_must_match_their_pins(self):
        folders, pins = self.tools()
        self.assertEqual(len(build.release_tools(folders, pins)), 5)
        (folders['repak'] / 'repak.exe').write_bytes(b'other')
        with self.assertRaisesRegex(ValueError, r'repak.exe has SHA-256 .*tools/release_tools.json'):
            build.release_tools(folders, pins)

    def test_pinned_tools_file_names_both_executables(self):
        pins = build.pinned_tools()
        self.assertEqual(set(pins), set(build.TOOLS))
        for name, (executable, licenses) in build.TOOLS.items():
            self.assertEqual(pins[name]['file'], executable)
            self.assertRegex(pins[name]['sha256'], '^[0-9a-f]{64}$')
            self.assertGreater(pins[name]['bytes'], 0)
            self.assertEqual(pins[name]['licenses'], list(licenses))
        self.assertIn('not recorded', pins['repak']['built_from'])
        self.assertIn('885a8da', pins['retoc']['built_from'])

    def stubs(self, **descriptor):
        stubs = self.folder / 'P4Stubs'
        binary = stubs / 'Binaries/Win64'
        binary.mkdir(parents=True, exist_ok=True)
        data = dict(CanContainContent=False, EnabledByDefault=True, Category='Pikmin 4', Modules=[
            dict(Name=name, Type='Runtime', LoadingPhase='PreDefault') for name in build.STUB_MODULES])
        data.update(descriptor)
        (stubs / 'P4Stubs.uplugin').write_text(json.dumps(data), encoding='utf-8')
        (binary / 'UE4Editor.modules').write_text(json.dumps(dict(BuildId='fixture-build', Modules={
            name: 'UE4Editor-' + name + '.dll' for name in build.STUB_MODULES})), encoding='utf-8')
        for name in build.STUB_MODULES:
            (binary / ('UE4Editor-' + name + '.dll')).write_bytes(b'MZ')
            (binary / ('UE4Editor-' + name + '.pdb')).write_bytes(b'symbols')
        (stubs / 'Source').mkdir(exist_ok=True)
        return stubs

    def test_built_stubs_ship_without_pdbs_or_sources(self):
        members = [member for _, member in build.stub_files(self.stubs(), 'fixture-build')]
        self.assertEqual(len(members), 8)
        self.assertTrue(all(member.startswith('P4Stubs/') and not member.endswith('.pdb') and '/Source/' not in member
                            for member in members))

    def test_unbuilt_disabled_or_foreign_stubs_are_refused(self):
        with self.assertRaisesRegex(ValueError, 'No built P4Stubs plugin .*tools/stubs/regenerate.py --build'):
            build.stub_files(self.folder / 'P4Stubs')
        with self.assertRaisesRegex(ValueError, 'not enabled by default.*Regenerate and rebuild'):
            build.stub_files(self.stubs(EnabledByDefault=False))
        stubs = self.stubs()
        before = json.loads((stubs / 'P4Stubs.uplugin').read_text(encoding='utf-8'))
        del before['EnabledByDefault']
        (stubs / 'P4Stubs.uplugin').write_text(json.dumps(before), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'written before the generator set EnabledByDefault'):
            build.stub_files(stubs)
        with self.assertRaisesRegex(ValueError, "category 'Other', Kadan/Kadan.uplugin 'Pikmin 4'"):
            build.stub_files(self.stubs(Category='Other'))
        with self.assertRaisesRegex(ValueError, 'six runtime modules'):
            build.stub_files(self.stubs(Modules=[dict(Name='P4StubsEditor', Type='Editor', LoadingPhase='Default')]))
        with self.assertRaisesRegex(ValueError, 'built for Unreal build fixture-build, Kadan for other'):
            build.stub_files(self.stubs(Modules=[dict(Name=name, Type='Runtime', LoadingPhase='PreDefault')
                                                 for name in build.STUB_MODULES]), 'other')
        (self.folder / 'P4Stubs/Binaries/Win64/UE4Editor-Carrot4.dll').unlink()
        with self.assertRaisesRegex(ValueError, 'Missing built P4Stubs module'):
            build.stub_files(self.folder / 'P4Stubs')

    def test_archive_name_and_sums(self):
        self.assertEqual(build.archive_name('0.3.1', True), 'Kadan-UE4-0.3.1-prerelease.zip')
        self.assertEqual(build.archive_name('0.4.0', False), 'Kadan-UE4-0.4.0.zip')
        archive = self.folder / 'Kadan-UE4-0.4.0.zip'
        archive.write_bytes(b'archive')
        sums = build.write_sums(archive)
        self.assertEqual(sums.read_bytes(), (build.sha256(archive) + '  Kadan-UE4-0.4.0.zip\n').encode('ascii'))


if __name__ == '__main__':
    unittest.main()
