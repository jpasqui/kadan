"""Separate module build/install boundaries, with every native command mocked."""
import json
import os
from pathlib import Path
import tempfile
import types
import unittest
from unittest.mock import patch

import dev_build
import native_core_sources
import native_test_module


class PipelineBoundary(unittest.TestCase):
    def fixture(self, root, tests=True):
        plugin = root / 'built/Plugins/Kadan'
        binary = plugin / 'Binaries/Win64'
        binary.mkdir(parents=True)
        names = dev_build.module_names(tests)
        descriptor = plugin / 'Kadan.uplugin'
        descriptor.write_text(json.dumps(dict(Modules=[dict(Name=name) for name in names])), encoding='utf-8')
        mapping = {name: 'UE4Editor-' + name + '.dll' for name in names}
        (binary / 'UE4Editor.modules').write_text(json.dumps(dict(Modules=mapping)), encoding='utf-8')
        for name, filename in mapping.items():
            (binary / filename).write_bytes(name.encode())
        project = root / 'installed/Project.uproject'
        project.parent.mkdir()
        project.write_text('{}', encoding='utf-8')
        return plugin, project

    def test_install_validates_and_installs_exact_declared_four_dll_runtime(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            plugin, project = self.fixture(root)
            dev_build.install(root / 'built', project)
            target = project.parent / 'Plugins/Kadan'
            files = {p.relative_to(target).as_posix(): p.read_bytes() for p in target.rglob('*') if p.is_file()}
            self.assertEqual(len(files), 6)
            self.assertEqual(sum(name.endswith('.dll') for name in files), 4)
            self.assertEqual(files['Binaries/Win64/UE4Editor-P4MapKitTests.dll'], b'P4MapKitTests')
            self.assertEqual((target / 'Kadan.uplugin').read_bytes(), (plugin / 'Kadan.uplugin').read_bytes())

    def test_missing_test_dll_refuses_before_install_target_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            plugin, project = self.fixture(root)
            (plugin / 'Binaries/Win64/UE4Editor-P4MapKitTests.dll').unlink()
            sentinel = project.parent / 'keep.txt'
            sentinel.write_bytes(b'old installed runtime')
            with self.assertRaises(ValueError):
                dev_build.install(root / 'built', project)
            self.assertEqual([path for path in project.parent.iterdir() if path != project], [sentinel])
            self.assertEqual(sentinel.read_bytes(), b'old installed runtime')

    def test_wrong_map_and_duplicate_descriptor_refuse(self):
        with tempfile.TemporaryDirectory() as directory:
            plugin, _ = self.fixture(Path(directory))
            mapping = plugin / 'Binaries/Win64/UE4Editor.modules'
            document = json.loads(mapping.read_text(encoding='utf-8'))
            document['Modules']['P4MapKitTests'] = '../outside.dll'
            mapping.write_text(json.dumps(document), encoding='utf-8')
            with self.assertRaises(ValueError):
                dev_build.install_artifacts(plugin)
            descriptor = plugin / 'Kadan.uplugin'
            document = json.loads(descriptor.read_text(encoding='utf-8'))
            document['Modules'].append(dict(Name='P4MapKitTests'))
            descriptor.write_text(json.dumps(document), encoding='utf-8')
            with self.assertRaises(ValueError):
                dev_build.install_artifacts(plugin)

    def test_plain_install_has_only_three_declared_dlls(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            plugin, project = self.fixture(root, tests=False)
            (plugin / 'Binaries/Win64/UE4Editor-P4MapKitTests.dll').write_bytes(b'unreferenced stale file')
            dev_build.install(root / 'built', project)
            target = project.parent / 'Plugins/Kadan/Binaries/Win64'
            self.assertEqual({p.name for p in target.iterdir()}, {'UE4Editor.modules', 'UE4Editor-Kadan.dll', 'UE4Editor-KadanShaderFormat.dll', 'UE4Editor-P4BlueprintImport.dll'})

    def test_install_replaces_hardlinked_destination_without_changing_peer(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            _, project = self.fixture(root)
            target = project.parent / 'Plugins/Kadan/Binaries/Win64'
            target.mkdir(parents=True)
            peer = root / 'peer.dll'
            peer.write_bytes(b'old linked bytes')
            os.link(peer, target / 'UE4Editor-P4MapKitTests.dll')
            dev_build.install(root / 'built', project)
            self.assertEqual(peer.read_bytes(), b'old linked bytes')
            self.assertEqual((target / 'UE4Editor-P4MapKitTests.dll').read_bytes(), b'P4MapKitTests')

    def test_failed_compile_can_restore_four_module_descriptor_only_with_unchanged_dlls(self):
        with tempfile.TemporaryDirectory() as directory:
            plugin, _ = self.fixture(Path(directory))
            snapshot = dev_build.runtime_snapshot(plugin)
            self.assertEqual(len(snapshot['dlls']), 4)
            descriptor = plugin / 'Kadan.uplugin'
            staged = json.dumps(dict(Modules=[dict(Name=name) for name in dev_build.module_names()])).encode()
            descriptor.write_bytes(staged)
            self.assertTrue(dev_build.restore_runtime_descriptor(plugin, snapshot, staged))
            self.assertEqual(descriptor.read_bytes(), snapshot['descriptor'])
            descriptor.write_bytes(staged)
            (plugin / 'Binaries/Win64/UE4Editor-P4MapKitTests.dll').write_bytes(b'new DLL')
            self.assertFalse(dev_build.restore_runtime_descriptor(plugin, snapshot, staged))
            self.assertEqual(descriptor.read_bytes(), staged)

    def test_sync_routes_canonical_tests_directly_and_preserves_incremental_test_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            canonical, snapshot = root / 'canonical', root / 'snapshot'
            canonical.mkdir()
            (canonical / 'Kadan.uplugin').write_text(json.dumps(dict(Modules=[dict(Name='Kadan'), dict(Name='KadanShaderFormat')])),
                                                       encoding='utf-8')
            product = canonical / 'Source/Kadan/Private'
            product.mkdir(parents=True)
            (product / 'Product.cpp').write_bytes(b'// product\n')
            (product / 'CurrentAutomation.cpp').write_bytes(b'IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCurrent, "Current", 0)\n')
            (product.parent / 'Kadan.Build.cs').write_bytes(b'// no product test flag\nPrivatePCHHeaderFile = "Private/P4MapKitPCH.h";\n')
            for path in (dev_build.SOURCE / 'Source/P4MapKitTests').rglob('*'):
                if path.is_file():
                    target = canonical / path.relative_to(dev_build.SOURCE)
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(path.read_bytes())
            def stage_blueprint(plugin):
                descriptor = plugin / 'Kadan.uplugin'
                data = json.loads(descriptor.read_text(encoding='utf-8'))
                data['Modules'].append(dict(Name='P4BlueprintImport'))
                descriptor.write_text(json.dumps(data), encoding='utf-8')
                return []
            with patch.object(dev_build, 'SOURCE', canonical), patch.object(native_test_module, 'CANONICAL', canonical), \
                    patch.object(native_core_sources, 'stage'), patch.dict('sys.modules', install_module=types.SimpleNamespace(stage=stage_blueprint)):
                dev_build.sync(snapshot, tests=True)
                unit = snapshot / 'Source/P4MapKitTests/Private/Authoring/CurrentAutomation.cpp'
                stamp = unit.stat().st_mtime_ns
                dev_build.sync(snapshot, tests=True)
                self.assertEqual(unit.stat().st_mtime_ns, stamp)
                self.assertFalse((snapshot / 'Source/Kadan/Private/CurrentAutomation.cpp').exists())
                dev_build.sync(snapshot, tests=False)
                native_test_module.assert_production_sources(snapshot)
                names = {row['Name'] for row in json.loads((snapshot / 'Kadan.uplugin').read_text(encoding='utf-8'))['Modules']}
                self.assertEqual(names, set(dev_build.module_names()))


if __name__ == '__main__':
    unittest.main()
