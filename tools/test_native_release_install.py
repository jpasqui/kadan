"""Exercise exact release installation and acceptance identity failures."""
import json
import os
import subprocess
import stat
import struct
from pathlib import Path
import tempfile
import unittest
import zipfile
from types import SimpleNamespace
from unittest import mock
from tools.build_native_release import archive_runtime
from tools import native_release_install as release


class ReleaseInstall(unittest.TestCase):
    def test_declaration_install_accepts_short_spelling_without_overwriting_installed_files(self):
        with tempfile.TemporaryDirectory() as folder:
            home = Path(folder).resolve()
            actual, alias = home / 'long fixture directory', home / 'FIXTUR~1'
            actual.mkdir()
            archive, project = self.fixture(actual)
            release.install(archive, project)
            declarations = release.declaration_archive
            info = declarations.inspect_declaration_archive(self.declaration_fixture(actual), 'fixture-build')
            alias_project = alias / project.relative_to(actual)
            original_resolve, original_lstat, original_is_file = Path.resolve, Path.lstat, Path.is_file

            def mapped(path):
                try:
                    return actual / path.relative_to(alias)
                except ValueError:
                    return path

            # Model the filesystem's 8.3 alias behavior even on a volume that
            # does not generate short names. All actual I/O uses authored data.
            def resolve(path, *args, **kwargs):
                return original_resolve(mapped(path), *args, **kwargs)

            def lstat(path, *args, **kwargs):
                return original_lstat(mapped(path), *args, **kwargs)

            def is_file(path):
                return original_is_file(mapped(path))

            with mock.patch.object(Path, 'resolve', resolve), mock.patch.object(Path, 'lstat', lstat), \
                    mock.patch.object(Path, 'is_file', is_file):
                installed = declarations.install_declaration_archive(info, alias_project)
                self.assertEqual(len(declarations.verify_installed_declarations(info, alias_project)['declarations']), 8)
                before = {name: (project.parent / 'Plugins' / name).read_bytes() for name in declarations.MEMBERS}
                with self.assertRaisesRegex(ValueError, 'destination already exists'):
                    declarations.install_declaration_archive(info, alias_project)
            self.assertEqual(installed['after']['declarations'], {name: declarations._hash(data) for name, data in before.items()})
            self.assertEqual(before, {name: (project.parent / 'Plugins' / name).read_bytes() for name in declarations.MEMBERS})

    def test_declaration_project_reparse_ancestry_is_rejected_before_installation(self):
        with tempfile.TemporaryDirectory() as folder:
            archive, project = self.fixture(Path(folder).resolve())
            release.install(archive, project)
            declarations = release.declaration_archive
            info = declarations.inspect_declaration_archive(self.declaration_fixture(folder), 'fixture-build')
            original = Path.lstat

            def reparse(path, *args, **kwargs):
                if path == project.parent:
                    return SimpleNamespace(st_mode=stat.S_IFDIR, st_file_attributes=0x400)
                return original(path, *args, **kwargs)

            before = project.read_bytes()
            with mock.patch.object(Path, 'lstat', reparse):
                with self.assertRaisesRegex(ValueError, 'project ancestry'):
                    declarations.install_declaration_archive(info, project)
            self.assertEqual(project.read_bytes(), before)
            self.assertFalse((project.parent / 'Plugins/P4Stubs').exists())

    def declaration_fixture(self, folder, build_id='fixture-build'):
        declarations = release.declaration_archive
        data = bytearray(512)
        data[:2] = b'MZ'
        struct.pack_into('<I', data, 0x3c, 64)
        data[64:68] = b'PE\0\0'
        struct.pack_into('<HH', data, 68, 0x8664, 1)
        struct.pack_into('<HH', data, 84, 240, 0x2000)
        struct.pack_into('<H', data, 88, 0x20b)
        path = Path(folder) / 'game-types.zip'
        with zipfile.ZipFile(path, 'w') as target:
            target.writestr(declarations.DESCRIPTOR, json.dumps(dict(CanContainContent=False, Modules=[
                dict(Name=name, Type='Runtime', LoadingPhase='PreDefault') for name in declarations.MODULES])))
            target.writestr(declarations.MANIFEST, json.dumps(dict(BuildId=build_id, Modules={
                name: 'UE4Editor-' + name + '.dll' for name in declarations.MODULES})))
            for name in declarations.DLLS.values():
                target.writestr(name, bytes(data))
        return path

    def test_shareable_distribution_never_bundles_or_installs_private_game_declarations(self):
        with tempfile.TemporaryDirectory() as folder:
            archive, project = self.fixture(folder)
            declarations = self.declaration_fixture(folder)
            candidate = Path(folder) / 'candidate'
            members = archive_runtime(candidate, archive, declarations)
            self.assertEqual(len(members), 6)
            with zipfile.ZipFile(archive) as output:
                self.assertTrue(all(name.startswith('Kadan/') for name in output.namelist()))
                self.assertEqual(len(output.namelist()), 6)
            receipt = release.install(archive, project)
            self.assertEqual(receipt, release.verify(archive, project))
            self.assertNotIn('declarations', receipt)
            descriptor = json.loads((project.parent / 'Plugins/Kadan/Kadan.uplugin').read_text(encoding='utf-8'))
            self.assertNotIn('Plugins', descriptor)
            self.assertEqual(json.loads(project.read_text(encoding='utf-8')), {})
            self.assertFalse((project.parent / 'Plugins/P4Stubs').exists())
            # Private acceptance may install its generated SDK separately.
            info = release.declaration_archive.inspect_declaration_archive(declarations, 'fixture-build')
            release.declaration_archive.install_declaration_archive(info, project)
            self.assertEqual(len(release.declaration_archive.verify_installed_declarations(info, project)['declarations']), 8)
            self.assertEqual(receipt, release.verify(archive, project))

    def test_game_sdk_added_to_shareable_archive_is_refused(self):
        with tempfile.TemporaryDirectory() as folder:
            archive, project = self.fixture(folder)
            declarations = self.declaration_fixture(folder)
            with zipfile.ZipFile(archive, 'a') as output, zipfile.ZipFile(declarations) as sdk:
                for name in sdk.namelist():
                    output.writestr(name, sdk.read(name))
            with self.assertRaisesRegex(ValueError, 'exactly the canonical runtime files'):
                release.install(archive, project)
            self.assertFalse((project.parent / 'Plugins/Kadan').exists())

    def test_declarations_wrong_build_and_foreign_plugin_refuse_before_editor_install(self):
        with tempfile.TemporaryDirectory() as folder:
            archive, project = self.fixture(folder)
            original = archive.read_bytes()
            declarations = self.declaration_fixture(folder, 'other-build')
            with self.assertRaisesRegex(ValueError, 'different build IDs'):
                archive_runtime(Path(folder) / 'candidate', archive, declarations)
            self.assertEqual(archive.read_bytes(), original)
            declarations = self.declaration_fixture(folder)
            archive_runtime(Path(folder) / 'candidate', archive, declarations)
            foreign = project.parent / 'Plugins/Foreign'
            foreign.mkdir(parents=True)
            keep = foreign / 'Foreign.uplugin'
            keep.write_text(json.dumps(dict(Modules=[dict(Name='Carrot4')])), encoding='utf-8')
            before = keep.read_bytes()
            with self.assertRaisesRegex(ValueError, 'module identity collision'):
                release.install(archive, project)
            self.assertFalse((project.parent / 'Plugins/Kadan').exists())
            self.assertEqual(keep.read_bytes(), before)

    def fixture(self, folder):
        base = Path(folder)
        plugin = base / 'candidate'
        binary = plugin / 'Binaries/Win64'
        binary.mkdir(parents=True)
        modules = {name: 'UE4Editor-' + name + '.dll' for name in release.MODULES}
        (plugin / 'Kadan.uplugin').write_text(json.dumps(dict(Modules=[
            dict(Name=name, Type=kind, LoadingPhase=phase) for name, (kind, phase) in release.MODULE_ROWS.items()])), encoding='utf-8')
        (binary / 'UE4Editor.modules').write_text(json.dumps(dict(BuildId='fixture-build', Modules=modules)), encoding='utf-8')
        for name, filename in modules.items():
            (binary / filename).write_bytes(name.encode('ascii'))
        (plugin / 'Config').mkdir()
        (plugin / 'Config/DefaultKadan.ini').write_text('[CoreRedirects]\n', encoding='utf-8')
        archive = base / 'release.zip'
        archive_runtime(plugin, archive)
        project = base / 'host/Host.uproject'
        project.parent.mkdir()
        project.write_text('{}', encoding='utf-8')
        return archive, project

    def test_clean_install_binds_all_members_and_archive(self):
        with tempfile.TemporaryDirectory() as folder:
            archive, project = self.fixture(folder)
            receipt = release.install(archive, project)
            self.assertEqual(receipt, release.verify(archive, project))
            self.assertEqual(set(receipt['plugin_members']), release.MEMBERS)
            self.assertEqual(receipt['archive_sha256'], release.digest(archive))
            before = (project.parent / 'Plugins/Kadan/Binaries/Win64/UE4Editor-Kadan.dll').read_bytes()
            with self.assertRaisesRegex(ValueError, 'must be absent'):
                release.install(archive, project)
            self.assertEqual((project.parent / 'Plugins/Kadan/Binaries/Win64/UE4Editor-Kadan.dll').read_bytes(), before)

    def test_mutated_installed_bytes_extra_files_and_duplicate_module_refuse(self):
        with tempfile.TemporaryDirectory() as folder:
            archive, project = self.fixture(folder)
            release.install(archive, project)
            binary = project.parent / 'Plugins/Kadan/Binaries/Win64/UE4Editor-P4BlueprintImport.dll'
            original = binary.read_bytes()
            binary.write_bytes(original + b'changed')
            with self.assertRaisesRegex(ValueError, 'does not equal'):
                release.verify(archive, project)
            binary.write_bytes(original)
            extra = binary.parent / 'private.py'
            extra.write_text('private', encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'does not equal'):
                release.verify(archive, project)
            extra.unlink()
            duplicate = project.parent / 'Plugins/Other/Binaries/Win64' / binary.name
            duplicate.parent.mkdir(parents=True)
            duplicate.write_bytes(original)
            with self.assertRaisesRegex(ValueError, 'Another project plugin'):
                release.verify(archive, project)

    def test_actual_loaded_runtime_paths_hashes_and_build_identity_are_required(self):
        with tempfile.TemporaryDirectory() as folder:
            archive, project = self.fixture(folder)
            binding = release.install(archive, project)
            binary = project.parent / 'Plugins/Kadan/Binaries/Win64'
            rows = []
            for name in release.MODULES:
                path = binary / ('UE4Editor-' + name + '.dll')
                rows.append(dict(module=name, loaded_file=str(path), image_path=str(path), manifest_file=str(binary / 'UE4Editor.modules'),
                                 sha256=release.digest(path), bytes=path.stat().st_size, build_id=binding['build_id'], image_verified=True))
            identity = dict(format=1, valid=True, modules=rows)
            receipt = release.require_loaded_runtime(identity, binding, project)
            self.assertEqual(release.require_loaded_receipt(receipt, binding), receipt)
            self.assertNotIn(str(project.parent), json.dumps(receipt))
            for key, value in [('image_verified', False), ('image_verified', 1), ('image_verified', None), ('image_path', None), ('image_path', str(project.parent / 'foreign.dll')), ('sha256', 'changed'), ('build_id', 'other'), ('bytes', True), ('bytes', 1),
                               ('loaded_file', str(project.parent / 'foreign.dll')), ('manifest_file', str(project))]:
                bad = json.loads(json.dumps(identity))
                bad['modules'][0][key] = value
                with self.assertRaises(ValueError):
                    release.require_loaded_runtime(bad, binding, project)
            with self.assertRaises(ValueError):
                release.require_loaded_runtime(dict(identity, modules=[rows[0], rows[0], rows[2]]), binding, project)
            without_shader_format = [row for row in rows if row['module'] != 'KadanShaderFormat']
            with self.assertRaises(ValueError):
                release.require_loaded_runtime(dict(identity, modules=without_shader_format), binding, project)
            bad = json.loads(json.dumps(receipt))
            bad['modules']['P4BlueprintImport']['sha256'] = 'changed'
            with self.assertRaises(ValueError):
                release.require_loaded_receipt(bad, binding)
            with self.assertRaises(ValueError):
                release.require_loaded_receipt(None, binding)
            malformed = json.loads(json.dumps(receipt))
            malformed['modules']['P4BlueprintImport'] = None
            with self.assertRaises(ValueError):
                release.require_loaded_receipt(malformed, binding)

    def test_redirected_plugins_parent_cannot_verify_or_bind_loaded_runtime(self):
        with tempfile.TemporaryDirectory() as folder:
            archive, project = self.fixture(folder)
            binding = release.install(archive, project)
            plugins = project.parent / 'Plugins'
            relocated = Path(folder) / 'relocated-plugins'
            plugins.rename(relocated)
            try:
                try:
                    plugins.symlink_to(relocated, target_is_directory=True)
                except OSError:
                    if os.name != 'nt':
                        raise
                    made = subprocess.run(['cmd', '/c', 'mklink', '/J', str(plugins), str(relocated)],
                                          capture_output=True)
                    if made.returncode:
                        self.skipTest('Directory links are unavailable on this filesystem')
                with self.assertRaisesRegex(ValueError, 'Plugins folder is redirected'):
                    release.verify(archive, project)
                binary = plugins / 'Kadan/Binaries/Win64'
                rows = [dict(module=name, loaded_file=str(binary / ('UE4Editor-' + name + '.dll')),
                             manifest_file=str(binary / 'UE4Editor.modules'),
                             sha256=binding['plugin_members']['Binaries/Win64/UE4Editor-' + name + '.dll'],
                             bytes=1, build_id=binding['build_id'], image_verified=True)
                        for name in release.MODULES]
                with self.assertRaisesRegex(ValueError, 'plugin folder is redirected'):
                    release.require_loaded_runtime(dict(format=1, valid=True, modules=rows), binding, project)
            finally:
                if plugins.is_symlink():
                    plugins.unlink()
                elif plugins.exists():
                    os.rmdir(plugins)  # Remove the directory junction, not its destination.
                relocated.rename(plugins)

    def test_malformed_metadata_and_unsaved_project_refuse(self):
        with tempfile.TemporaryDirectory() as folder:
            archive, project = self.fixture(folder)
            project.unlink()
            with self.assertRaisesRegex(ValueError, 'saved Unreal project'):
                release.install(archive, project)
            project.write_text('{}', encoding='utf-8')
            with zipfile.ZipFile(archive) as source:
                members = {name: source.read(name) for name in source.namelist()}
            members['Kadan/Kadan.uplugin'] = b'{broken'
            with zipfile.ZipFile(archive, 'w') as target:
                for name, data in members.items():
                    target.writestr(name, data)
            with self.assertRaisesRegex(ValueError, 'Malformed release'):
                release.install(archive, project)
            self.assertFalse((project.parent / 'Plugins/Kadan').exists())

    def test_shader_format_row_with_another_loading_phase_never_installs(self):
        with tempfile.TemporaryDirectory() as folder:
            archive, project = self.fixture(folder)
            with zipfile.ZipFile(archive) as source:
                members = {name: source.read(name) for name in source.namelist()}
            descriptor = json.loads(members['Kadan/Kadan.uplugin'])
            next(row for row in descriptor['Modules'] if row['Name'] == 'KadanShaderFormat')['LoadingPhase'] = 'Default'
            members['Kadan/Kadan.uplugin'] = json.dumps(descriptor).encode('utf-8')
            with zipfile.ZipFile(archive, 'w') as target:
                for name, data in members.items():
                    target.writestr(name, data)
            with self.assertRaisesRegex(ValueError, 'type or loading phase: KadanShaderFormat'):
                release.install(archive, project)
            self.assertFalse((project.parent / 'Plugins/Kadan').exists())

    def test_extra_duplicate_or_traversing_archive_member_never_installs(self):
        for name in ('Kadan/extra.txt', 'Kadan/Kadan.uplugin', '../foreign.txt'):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as folder:
                archive, project = self.fixture(folder)
                with zipfile.ZipFile(archive, 'a') as output:
                    output.writestr(name, b'foreign')
                with self.assertRaises(ValueError):
                    release.install(archive, project)
                self.assertFalse((project.parent / 'Plugins/Kadan').exists())


if __name__ == '__main__':
    unittest.main()
