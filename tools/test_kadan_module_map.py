"""Validate the actual UBT stale-module-map transition after a plugin rename."""
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import dev_build


class ModuleMap(unittest.TestCase):
    def plugin(self, root, extra=None):
        binary = root / 'Binaries/Win64'
        binary.mkdir(parents=True)
        names = dev_build.module_names(True)
        (root / 'Kadan.uplugin').write_text(json.dumps(dict(Modules=[dict(Name=n) for n in names])), encoding='utf-8')
        modules = {n: 'UE4Editor-' + n + '.dll' for n in names}
        modules['P4MapKitAuthoring'] = 'UE4Editor-P4MapKitAuthoring.dll'
        modules.update(extra or {})
        for filename in modules.values():
            (binary / filename).write_bytes(b'preserved module bytes')
        mapping = binary / 'UE4Editor.modules'
        mapping.write_text(json.dumps(dict(BuildId='preserved', Modules=modules)), encoding='utf-8')
        return mapping

    def test_retires_only_old_main_entry_preserving_legacy_dll_and_buildid(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            mapping = self.plugin(root)
            legacy = mapping.parent / 'UE4Editor-P4MapKitAuthoring.dll'
            before = legacy.read_bytes()
            self.assertTrue(dev_build.retire_legacy_module_map(root))
            result = json.loads(mapping.read_text(encoding='utf-8'))
            self.assertEqual(result['BuildId'], 'preserved')
            self.assertEqual(set(result['Modules']), set(dev_build.module_names(True)))
            self.assertEqual(legacy.read_bytes(), before)
            self.assertFalse(dev_build.retire_legacy_module_map(root))
            self.assertEqual(len(dev_build.install_artifacts(root)), len(dev_build.module_names(True)) + 2)

    def test_release_build_retires_only_stale_test_module_map_entry(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            mapping = self.plugin(root)
            self.assertTrue(dev_build.retire_legacy_module_map(root))
            tests_dll = mapping.parent / 'UE4Editor-P4MapKitTests.dll'
            before = tests_dll.read_bytes()
            (root / 'Kadan.uplugin').write_text(json.dumps(dict(Modules=[
                dict(Name=name) for name in dev_build.module_names()])), encoding='utf-8')
            self.assertTrue(dev_build.retire_legacy_module_map(root))
            result = json.loads(mapping.read_text(encoding='utf-8'))
            self.assertEqual(result['BuildId'], 'preserved')
            self.assertEqual(set(result['Modules']), set(dev_build.module_names()))
            self.assertEqual(tests_dll.read_bytes(), before)
            self.assertFalse(dev_build.retire_legacy_module_map(root))

    def test_unrelated_map_entry_or_missing_current_dll_refuses_without_rewrite(self):
        for extra in ({'Other': 'UE4Editor-Other.dll'}, None):
            with tempfile.TemporaryDirectory() as folder:
                root = Path(folder)
                mapping = self.plugin(root, extra)
                if extra is None:
                    (mapping.parent / 'UE4Editor-Kadan.dll').unlink()
                before = mapping.read_bytes()
                with self.assertRaises(ValueError):
                    dev_build.retire_legacy_module_map(root)
                self.assertEqual(mapping.read_bytes(), before)
                self.assertTrue((mapping.parent / 'UE4Editor-P4MapKitAuthoring.dll').is_file())

    def test_mismatched_old_basename_is_preserved(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            mapping = self.plugin(root)
            document = json.loads(mapping.read_text(encoding='utf-8'))
            document['Modules']['P4MapKitAuthoring'] = 'UE4Editor-Foreign.dll'
            mapping.write_text(json.dumps(document), encoding='utf-8')
            before = mapping.read_bytes()
            with self.assertRaises(ValueError):
                dev_build.retire_legacy_module_map(root)
            self.assertEqual(mapping.read_bytes(), before)

    def test_failed_build_does_not_retire_the_old_entry(self):
        with tempfile.TemporaryDirectory() as folder:
            host = Path(folder)
            mapping = self.plugin(host / 'Plugins/Kadan')
            (host / 'DevHost.uproject').write_text(json.dumps(dict(FileVersion=3, Plugins=[dict(Name='Kadan', Enabled=True)])), encoding='utf-8')
            before = mapping.read_bytes()
            with patch('sys.argv', ['dev_build', '--host', str(host), '--engine', str(host / 'engine'), '--tests']), \
                    patch.object(dev_build, 'sync', return_value=[]), \
                    patch.object(dev_build.subprocess, 'run') as run, \
                    patch.object(dev_build, 'retire_legacy_module_map') as retire:
                run.return_value.returncode = 7
                self.assertEqual(dev_build.main(), 1)
                retire.assert_not_called()
            self.assertEqual(mapping.read_bytes(), before)
            self.assertTrue((mapping.parent / 'UE4Editor-P4MapKitAuthoring.dll').is_file())

    def test_install_to_the_same_built_project_preserves_all_module_files(self):
        with tempfile.TemporaryDirectory() as folder:
            host = Path(folder)
            plugin = host / 'Plugins/Kadan'
            self.plugin(plugin)
            dev_build.retire_legacy_module_map(plugin)
            project = host / 'DevHost.uproject'
            project.write_text(json.dumps(dict(FileVersion=3, Plugins=[dict(Name='Kadan', Enabled=True)])), encoding='utf-8')
            files = dev_build.install_artifacts(plugin)
            before = [(file.read_bytes(), file.stat().st_mtime_ns) for file in files]
            dev_build.install(host, project)
            self.assertEqual([(file.read_bytes(), file.stat().st_mtime_ns) for file in files], before)

    def test_runtime_only_declarations_are_disabled_only_for_ubt(self):
        with tempfile.TemporaryDirectory() as folder:
            host = Path(folder)
            plugin = host / 'Plugins/Kadan'
            self.plugin(plugin)
            dev_build.retire_legacy_module_map(plugin)
            project = host / 'DevHost.uproject'
            project.write_text(json.dumps(dict(FileVersion=3, Plugins=[dict(Name='Kadan', Enabled=True), dict(Name='P4Stubs', Enabled=True)])), encoding='utf-8')
            stubs = host / 'Plugins/P4Stubs'
            stubs.mkdir()
            (stubs / 'P4Stubs.uplugin').write_text('{}', encoding='utf-8')
            before = project.read_bytes()
            with patch('sys.argv', ['dev_build', '--host', str(host), '--engine', str(host / 'engine'), '--tests']), \
                    patch.object(dev_build, 'sync', return_value=[]), patch.object(dev_build.subprocess, 'run') as run:
                run.return_value.returncode = 0
                self.assertEqual(dev_build.main(), 0)
                self.assertIn('-DisablePlugin=P4Stubs', run.call_args.args[0])
            self.assertEqual(project.read_bytes(), before)


if __name__ == '__main__':
    unittest.main()
