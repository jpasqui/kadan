"""Check selected-project name migration and preservation boundaries."""
import json
from pathlib import Path
import tempfile
import unittest

try:
    from . import kadan_plugin as plugin
except ImportError:
    import kadan_plugin as plugin


class PluginMigration(unittest.TestCase):
    def project(self, root, entries):
        project = root / 'Creator.uproject'
        project.write_text(json.dumps(dict(FileVersion=3, Plugins=entries, Description='Creator project')), encoding='utf-8')
        return project

    def legacy(self, root, modules=None):
        old = root / 'Plugins' / plugin.LEGACY_NAME
        old.mkdir(parents=True)
        (old / (plugin.LEGACY_NAME + '.uplugin')).write_text(json.dumps(dict(Modules=[dict(Name=name) for name in (modules or [plugin.LEGACY_NAME, 'P4BlueprintImport'])])), encoding='utf-8')
        source = old / 'Source' / plugin.LEGACY_NAME
        source.mkdir(parents=True)
        (source / (plugin.LEGACY_NAME + '.Build.cs')).write_text('old rules', encoding='utf-8')
        (old / 'custom-note.txt').write_bytes(b'keep plugin additions')
        return old

    def test_migrates_only_selected_plugin_and_preserves_creator_content(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            project = self.project(root, [dict(Name=plugin.LEGACY_NAME, Enabled=True, Custom='keep'), dict(Name='Other', Enabled=False)])
            old = self.legacy(root)
            content = root / 'Content/Mine/Asset.uasset'
            content.parent.mkdir(parents=True)
            content.write_bytes(b'creator asset containing the old script class reference')
            target = plugin.prepare_plugin(project)
            self.assertEqual(target, (root / 'Plugins/Kadan').resolve())
            self.assertFalse(old.exists())
            self.assertEqual(content.read_bytes(), b'creator asset containing the old script class reference')
            self.assertEqual((target / 'custom-note.txt').read_bytes(), b'keep plugin additions')
            self.assertTrue((target / 'Kadan.uplugin').is_file())
            self.assertTrue((target / 'Source/Kadan/Kadan.Build.cs').is_file())
            settings = json.loads(project.read_text(encoding='utf-8'))
            self.assertEqual(settings['Plugins'], [dict(Name='Kadan', Enabled=True, Custom='keep'), dict(Name='Other', Enabled=False)])
            self.assertEqual(plugin.prepare_plugin(project), target)

    def test_duplicate_slots_refuse_before_changing_anything(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            project = self.project(root, [dict(Name=plugin.LEGACY_NAME, Enabled=True)])
            old = self.legacy(root)
            (root / 'Plugins/Kadan').mkdir()
            before = project.read_bytes()
            with self.assertRaisesRegex(ValueError, 'Both plugin names'):
                plugin.prepare_plugin(project)
            self.assertTrue(old.is_dir())
            self.assertEqual(project.read_bytes(), before)

    def test_foreign_modules_and_duplicate_settings_are_preserved(self):
        for modules, entries in (([plugin.LEGACY_NAME, 'Foreign'], [dict(Name=plugin.LEGACY_NAME)]), ([plugin.LEGACY_NAME], [dict(Name=plugin.LEGACY_NAME), dict(Name=plugin.NAME)])):
            with tempfile.TemporaryDirectory() as folder:
                root = Path(folder)
                project = self.project(root, entries)
                old = self.legacy(root, modules)
                before = project.read_bytes()
                with self.assertRaises(ValueError):
                    plugin.prepare_plugin(project)
                self.assertTrue(old.is_dir())
                self.assertEqual(project.read_bytes(), before)

    def test_new_project_keeps_other_plugin_settings(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            project = self.project(root, [dict(Name='Other', Enabled=True)])
            self.assertEqual(plugin.prepare_plugin(project), (root / 'Plugins/Kadan').resolve())
            self.assertEqual(json.loads(project.read_text(encoding='utf-8'))['Plugins'], [dict(Name='Other', Enabled=True), dict(Name='Kadan', Enabled=True)])

    def test_normal_build_does_not_migrate_a_legacy_project(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            project = self.project(root, [dict(Name=plugin.LEGACY_NAME, Enabled=True)])
            old = self.legacy(root)
            before = project.read_bytes()
            with self.assertRaisesRegex(ValueError, '--migrate-plugin-name'):
                plugin.plugin_slot(project)
            self.assertTrue(old.is_dir())
            self.assertEqual(project.read_bytes(), before)
            self.assertEqual(plugin.plugin_slot(project, migrate=True), (root / 'Plugins/Kadan').resolve())

    def test_existing_enabled_setting_is_preserved(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            project = self.project(root, [dict(Name=plugin.LEGACY_NAME, Enabled=False)])
            self.legacy(root)
            plugin.prepare_plugin(project)
            self.assertEqual(json.loads(project.read_text(encoding='utf-8'))['Plugins'], [dict(Name='Kadan', Enabled=False)])


if __name__ == '__main__':
    unittest.main()
