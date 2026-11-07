"""A draft release admits only matching tagged sources and qualified archive bytes."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import github_release as release


class GitHubRelease(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve() / 'source'
        self.root.mkdir()
        self.private = self.root.parent / 'qualified'
        self.tag = 'v0.3.0-rc.1'
        self.inputs = self.private / self.tag
        self.inputs.mkdir(parents=True)
        self.out = self.root / 'build/github-release'
        self.source = dict(commit='a' * 40, tree='b' * 40)
        (self.root / 'Kadan').mkdir()
        self.descriptor = dict(VersionName='0.3.0', Version=3, CanContainContent=False,
                              Modules=[dict(Name=name, Type=row[0], LoadingPhase=row[1])
                                       for name, row in release.native_release_install.MODULE_ROWS.items()])
        (self.root / 'Kadan/Kadan.uplugin').write_text(json.dumps(self.descriptor), encoding='utf-8')
        (self.root / 'LICENSE').write_text('MIT License\n', encoding='utf-8')
        (self.root / '.gitignore').write_text('build/\n', encoding='utf-8')
        (self.root / 'core').mkdir()
        (self.root / 'core/example.h').write_text('#pragma once\n', encoding='utf-8')
        (self.root / 'tools').mkdir()
        self.policy = dict(engine='UE4.26.2', platform='Win64', coverage_scopes={'core': ['core/**/*']},
                           coverage_exclusions=[], qualification_inputs=['Kadan/Kadan.uplugin'],
                           required_checks=['native_unit', 'acceptance_import_map', 'acceptance_play'],
                           acceptance_chain=[{'check': 'acceptance_import_map'}, {'check': 'acceptance_play'}])
        (self.root / 'tools/native_quality_policy.json').write_text(json.dumps(self.policy), encoding='utf-8')
        self.archive = self.inputs / 'Kadan-UE4.zip'
        self.write_archive()
        self.evidence = dict(format=1, engine='UE4.26.2', platform='Win64', open_regressions=[],
                             artifact_sha256=release.digest(self.archive), checks={})
        inventory = release.native_quality_gate.qualification_inventory(self.root, self.policy)
        for name in self.policy['required_checks']:
            self.evidence['checks'][name] = dict(passed=True, sources=inventory, map='/Game/AuthoredFixture')
        self.write_evidence()
        (self.inputs / 'release-notes.md').write_text('## Changes\n\n- Authored fixture update.\n', encoding='utf-8')

    def write_archive(self, descriptor=None, linked_member=None):
        members = {
            'Kadan/Kadan.uplugin': json.dumps(descriptor or self.descriptor).encode('utf-8'),
            'Kadan/Config/DefaultKadan.ini': b'[Kadan]\n',
            'Kadan/Binaries/Win64/UE4Editor.modules': json.dumps(dict(BuildId='authored-fixture', Modules={
                name: 'UE4Editor-' + name + '.dll' for name in release.native_release_install.MODULES})).encode('utf-8'),
        }
        for name in release.native_release_install.MODULES:
            members['Kadan/Binaries/Win64/UE4Editor-' + name + '.dll'] = b'authored fixture module'
        with zipfile.ZipFile(self.archive, 'w') as archive:
            for name, data in members.items():
                if name == linked_member:
                    info = zipfile.ZipInfo(name)
                    info.create_system = 3
                    info.external_attr = (stat.S_IFLNK | 0o777) << 16
                    archive.writestr(info, data)
                else:
                    archive.writestr(name, data)

    def write_evidence(self):
        (self.inputs / 'qualification.json').write_text(json.dumps(self.evidence), encoding='utf-8')

    def prepare(self):
        with patch.object(release, 'source_identity', return_value=self.source):
            return release.prepare(self.root, self.private, self.tag)

    def test_tag_format_and_descriptor_version_reject_before_private_inputs(self):
        for tag in ('v0.2.0', 'v0.3.0-rc.01', '../other', 'refs/heads/main', 'v0.3.0-', None):
            with self.subTest(tag=tag), patch.object(release, 'input_files') as inputs:
                with self.assertRaises(ValueError):
                    release.prepare(self.root, self.private, tag)
                inputs.assert_not_called()

    def test_exact_qualification_creates_only_public_assets_and_bound_receipt(self):
        result = self.prepare()
        receipt = json.loads((self.out / 'release.json').read_text(encoding='utf-8'))
        self.assertEqual(receipt['source'], self.source)
        self.assertEqual(receipt['qualification']['checks'], self.policy['required_checks'])
        self.assertEqual((self.out / result['archive']).read_bytes(), self.archive.read_bytes())
        self.assertEqual(set(result['files']), {result['archive'], 'LICENSE', 'release.json', 'SHA256SUMS'})
        self.assertEqual({path.name for path in self.out.iterdir()}, set(result['files']) | {'release-notes.md'})
        self.assertNotIn(str(self.private), (self.out / 'release.json').read_text(encoding='utf-8'))
        self.assertNotIn('qualification.json', result['files'])
        for line in (self.out / 'SHA256SUMS').read_text(encoding='utf-8').splitlines():
            expected, name = line.split('  ', 1)
            self.assertEqual(expected, release.digest(self.out / name))

    def test_open_regressions_and_changed_qualified_bytes_are_refused(self):
        self.evidence['open_regressions'] = ['unresolved fixture issue']
        self.write_evidence()
        with self.assertRaisesRegex(ValueError, 'qualification refused'):
            self.prepare()
        self.assertFalse((self.out / 'release.json').exists())
        self.evidence['open_regressions'] = []
        self.write_evidence()
        self.archive.write_bytes(self.archive.read_bytes() + b'changed archive bytes')
        with self.assertRaisesRegex(ValueError, 'qualification refused'):
            self.prepare()
        self.assertFalse((self.out / 'SHA256SUMS').exists())

    def test_archive_version_must_match_source_even_with_a_matching_hash(self):
        descriptor = dict(self.descriptor, VersionName='0.2.0')
        self.write_archive(descriptor)
        self.evidence['artifact_sha256'] = release.digest(self.archive)
        self.write_evidence()
        with self.assertRaisesRegex(ValueError, 'archive version'):
            self.prepare()

    def test_archive_links_and_extra_output_files_are_refused(self):
        self.write_archive(linked_member='Kadan/Binaries/Win64/UE4Editor-Kadan.dll')
        with self.assertRaisesRegex(ValueError, 'ordinary files'):
            self.prepare()
        self.write_archive()
        self.out.mkdir(parents=True)
        (self.out / 'qualification.json').write_text('private', encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'unrelated files'):
            self.prepare()

    def test_qualified_inputs_cannot_live_in_the_checkout(self):
        private = self.root / 'private'
        private.mkdir()
        with self.assertRaisesRegex(ValueError, 'outside the source checkout'):
            release.input_files(self.root, private, self.tag)

    def test_source_change_and_evidence_change_prevent_completion_receipts(self):
        with patch.object(release, 'source_identity', side_effect=[self.source, dict(commit='c' * 40, tree='d' * 40)]):
            with self.assertRaisesRegex(ValueError, 'Source or tag changed'):
                release.prepare(self.root, self.private, self.tag)
        self.assertFalse((self.out / 'release.json').exists())
        original = release.native_release_publish.publish

        def changed_evidence(*args, **kwargs):
            result = original(*args, **kwargs)
            (self.inputs / 'qualification.json').write_text('{}', encoding='utf-8')
            return result

        with patch.object(release.native_release_publish, 'publish', side_effect=changed_evidence):
            with self.assertRaisesRegex(ValueError, 'Private qualification changed'):
                self.prepare()
        self.assertFalse((self.out / 'SHA256SUMS').exists())

    @unittest.skipUnless(shutil.which('git'), 'needs git')
    def test_git_identity_requires_clean_source_and_current_tag_commit(self):
        def git(*args):
            return subprocess.run(['git', '-C', str(self.root), *args], check=True, capture_output=True)

        subprocess.run(['git', 'init', '-q', str(self.root)], check=True, capture_output=True)
        git('add', '.')
        git('-c', 'user.name=Kadan fixture', '-c', 'user.email=ci@example.invalid', 'commit', '-qm', 'fixture')
        git('tag', self.tag)
        self.assertEqual(len(release.source_identity(self.root, self.tag)['commit']), 40)
        (self.root / 'unexpected.txt').write_text('changed source', encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'clean, committed'):
            release.source_identity(self.root, self.tag)
        git('add', 'unexpected.txt')
        git('-c', 'user.name=Kadan fixture', '-c', 'user.email=ci@example.invalid', 'commit', '-qm', 'next fixture')
        with self.assertRaisesRegex(ValueError, 'tag must still name'):
            release.source_identity(self.root, self.tag)


if __name__ == '__main__':
    unittest.main()
