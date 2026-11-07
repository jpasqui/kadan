"""Small source snapshots prove test isolation without touching a runtime host."""
import json
import os
from pathlib import Path
import tempfile
import types
import unittest
from unittest.mock import patch

import native_test_module as staging

# The first owned scaffold of the test module, byte for byte as native_test_module.LEGACY_PENDING_SCAFFOLD
# pins it.
LEGACY_SCAFFOLD = {
    'P4MapKitTests.Build.cs': (
        b'using UnrealBuildTool;\n'
        b'using System.IO;\n'
        b'\n'
        b'// Development-only module. The canonical release descriptor never declares it.\n'
        b'public class P4MapKitTests : ModuleRules\n'
        b'{\n'
        b'    public P4MapKitTests(ReadOnlyTargetRules Target) : base(Target)\n'
        b'    {\n'
        b'        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;\n'
        b'        PrivatePCHHeaderFile = "Private/P4MapKitTestsPCH.h";\n'
        b'        bUseUnity = false;\n'
        b'        CppStandard = CppStandardVersion.Cpp17;\n'
        b'        bEnableExceptions = true;\n'
        b'        bEnforceIWYU = false;\n'
        b'        PrivateDefinitions.Add("P4_MAPKIT_TESTS=1");\n'
        b'        PrivateDependencyModuleNames.AddRange(new string[] {\n'
        b'            "Core", "CoreUObject", "Engine", "UnrealEd", "P4MapKitAuthoring", "P4BlueprintImport",\n'
        b'            "Json", "PlacementMode", "AssetRegistry", "RHI", "MeshDescription", "StaticMeshDescription",\n'
        b'            "RenderCore", "RawMesh", "BlueprintGraph", "Foliage", "NavigationSystem", "Navmesh",\n'
        b'            "DerivedDataCache", "Slate", "SlateCore", "LevelEditor", "DesktopPlatform", "ContentBrowser",\n'
        b'            "EditorStyle", "MaterialEditor", "InputCore", "AssetTools", "PropertyEditor", "GraphEditor",\n'
        b'            "UMGEditor", "UMG", "Kismet", "KismetCompiler", "AnimGraph", "AnimGraphRuntime",\n'
        b'            "MovieScene", "MovieSceneTracks", "MovieSceneTools", "LevelSequence"\n'
        b'        });\n'
        b'        // Tests link the product DLLs. They never compile copies of product translation units.\n'
        b'        PrivateIncludePaths.Add(Path.GetFullPath(Path.Combine(ModuleDirectory,"../P4MapKitAuthoring/Private")));\n'
        b'        PrivateIncludePaths.Add(Path.GetFullPath(Path.Combine(ModuleDirectory,"../P4BlueprintImport/Private")));\n'
        b'        PrivateIncludePaths.Add(Path.Combine(ModuleDirectory,"Private/Authoring"));\n'
        b'        PrivateIncludePaths.Add(Path.Combine(ModuleDirectory,"Private/Blueprints"));\n'
        b'        if (Target.Platform == UnrealTargetPlatform.Win64) PublicSystemLibraries.Add("bcrypt.lib");\n'
        b'    }\n'
        b'}\n'
    ),
    'Private/P4MapKitTestsModule.cpp': (
        b'#include "Modules/ModuleManager.h"\n'
        b'\n'
        b'void P4StartPrivateNativeAutomationPacing();\n'
        b'void P4StopPrivateNativeAutomationPacing();\n'
        b'\n'
        b'class FP4MapKitTestsModule final : public IModuleInterface\n'
        b'{\n'
        b'public:\n'
        b'    void StartupModule() override { P4StartPrivateNativeAutomationPacing(); }\n'
        b'    void ShutdownModule() override { P4StopPrivateNativeAutomationPacing(); }\n'
        b'};\n'
        b'IMPLEMENT_MODULE(FP4MapKitTestsModule,P4MapKitTests);\n'
    ),
    'Private/P4MapKitTestsPCH.h': (
        b'#pragma once\n'
        b'#if defined(_MSC_VER)\n'
        b'#pragma float_control(precise, on)\n'
        b'#pragma fp_contract(off)\n'
        b'#endif\n'
        b'#include "UnrealEdSharedPCH.h"\n'
    ),
}


class TestModuleStaging(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.canonical = self.base / 'canonical'
        self.plugin = self.base / 'snapshot'
        self.plugin.mkdir()
        self.patch = patch.object(staging, 'CANONICAL', self.canonical)
        self.patch.start()
        self.addCleanup(self.patch.stop)
        self.write('Kadan.uplugin', json.dumps(dict(Modules=[dict(Name=name, Type='Editor', LoadingPhase='Default') for name in staging.PRODUCTS])))
        for name in staging.PRODUCTS:
            self.write('Source/' + name + '/Private/Product.cpp', '// real product source stays in its own module\n')
        self.write('Source/Kadan/Kadan.Build.cs', 'class Product {\n    PrivatePCHHeaderFile = "Private/P4MapKitPCH.h";\n    ' +
                   staging.PRODUCT_TEST_DEFINE + '\n}\n')
        self.authoring = 'Source/Kadan/Private/FirstAutomation.cpp'
        self.blueprint = 'Source/P4BlueprintImport/Private/ReimportTests.cpp'
        self.write(self.authoring, 'IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTest, "First", 0)\n')
        self.write(self.blueprint, 'IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTest, "Reimport", 0)\n')
        for name, body in [('P4MapKitTests.Build.cs', 'class P4MapKitTests {}\n'), ('Private/P4MapKitTestsModule.cpp', 'IMPLEMENT_MODULE(FModule, P4MapKitTests)\n'), ('Private/P4MapKitTestsPCH.h', '#pragma once\n')]:
            path = self.canonical / 'Source/P4MapKitTests' / (name + '.pending')
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(body.encode('utf-8'))
        self.write('Binaries/Win64/UE4Editor-Kadan.dll', b'installed-authoring-dll')
        self.write('Binaries/Win64/UE4Editor-P4BlueprintImport.dll', b'installed-blueprint-dll')
        self.write('Binaries/Win64/UE4Editor.modules', b'installed-module-map')
        self.runtime = self.files('Binaries')

    def write(self, name, content):
        path = self.plugin / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content if isinstance(content, bytes) else content.encode('utf-8'))

    def files(self, folder=''):
        return {p.relative_to(self.plugin).as_posix(): p.read_bytes() for p in (self.plugin / folder).rglob('*') if p.is_file()}

    def modules(self):
        return json.loads((self.plugin / 'Kadan.uplugin').read_text(encoding='utf-8'))['Modules']

    def refuse_atomically(self, **kwargs):
        before = self.files()
        with self.assertRaises(ValueError):
            staging.stage(self.plugin, enabled=True, **kwargs)
        self.assertEqual(before, self.files())

    def resolved_alias(self, alias, target):
        """Model 8.3 expansion without needing a volume that generates short names."""
        original = Path.resolve
        def resolve(path, *args, **kwargs):
            if path.is_relative_to(alias):
                path = target / path.relative_to(alias)
            return original(path, *args, **kwargs)
        return patch.object(Path, 'resolve', resolve)

    def test_short_and_long_snapshot_spellings_stage_the_same_physical_tree(self):
        alias = self.base / 'SHORT~1' / 'snapshot'
        with self.resolved_alias(alias, self.plugin):
            result = staging.stage(alias, enabled=True)
        self.assertEqual(result['tests'], 2)
        self.assertFalse((self.plugin / self.authoring).exists())
        self.assertTrue((self.plugin / 'Source/P4MapKitTests/Private/Authoring/FirstAutomation.cpp').is_file())
        self.assertEqual(self.runtime, self.files('Binaries'))

    def test_resolved_alias_outside_the_snapshot_still_refuses(self):
        candidate = self.plugin / 'Source/SHORT~1.cpp'
        outside = self.base / 'outside.cpp'
        outside.write_bytes(b'outside source stays unchanged')
        before = self.files()
        with self.resolved_alias(candidate, outside):
            with self.assertRaisesRegex(ValueError, 'escapes its snapshot'):
                staging._inside(candidate, self.plugin)
        self.assertEqual(before, self.files())
        self.assertEqual(outside.read_bytes(), b'outside source stays unchanged')

    def test_original_reparse_spelling_is_checked_before_normalized_containment(self):
        source = self.plugin / 'Source'
        candidate = source / 'Kadan/Private/Product.cpp'
        original = Path.lstat
        def reparse(path, *args, **kwargs):
            value = original(path, *args, **kwargs)
            if path == source:
                return types.SimpleNamespace(st_mode=value.st_mode, st_file_attributes=0x400)
            return value
        before = self.files()
        with patch.object(Path, 'lstat', reparse):
            with self.assertRaisesRegex(ValueError, 'escapes its snapshot'):
                staging._inside(candidate, self.plugin)
        self.assertEqual(before, self.files())

    def test_canonical_source_reached_through_a_short_alias_is_refused(self):
        alias = self.base / 'SHORT~1' / 'canonical'
        before = {path.relative_to(self.canonical).as_posix(): path.read_bytes()
                  for path in self.canonical.rglob('*') if path.is_file()}
        with self.resolved_alias(alias, self.canonical):
            with self.assertRaisesRegex(ValueError, 'isolated source snapshot'):
                staging.stage(alias, enabled=True)
        after = {path.relative_to(self.canonical).as_posix(): path.read_bytes()
                 for path in self.canonical.rglob('*') if path.is_file()}
        self.assertEqual(before, after)

    def test_reparse_parent_above_the_snapshot_refuses_before_redirected_writes(self):
        parent = self.base / 'redirected-parent'
        alias = parent / 'snapshot'
        original = Path.lstat
        def reparse(path, *args, **kwargs):
            if path == parent:
                value = original(self.base, *args, **kwargs)
                return types.SimpleNamespace(st_mode=value.st_mode, st_file_attributes=0x400)
            return original(path, *args, **kwargs)
        before = self.files()
        with self.resolved_alias(alias, self.plugin), patch.object(Path, 'lstat', reparse):
            with self.assertRaisesRegex(ValueError, 'escapes its snapshot'):
                staging.stage(alias, enabled=True)
        self.assertEqual(before, self.files())

    def test_development_links_products_and_moves_only_test_units(self):
        result = staging.stage(self.plugin, enabled=True)
        self.assertEqual(result['tests'], 2)
        self.assertFalse((self.plugin / self.authoring).exists())
        self.assertFalse((self.plugin / self.blueprint).exists())
        sources = self.files('Source/P4MapKitTests')
        self.assertEqual(set(sources), {'Source/P4MapKitTests/P4MapKitTests.Build.cs', 'Source/P4MapKitTests/Private/P4MapKitTestsModule.cpp', 'Source/P4MapKitTests/Private/P4MapKitTestsPCH.h', 'Source/P4MapKitTests/Private/Authoring/FirstAutomation.cpp', 'Source/P4MapKitTests/Private/Blueprints/ReimportTests.cpp'})
        self.assertEqual(self.modules()[-1], dict(Name='P4MapKitTests', Type='Editor', LoadingPhase='PostEngineInit'))
        self.assertNotIn('P4_MAPKIT_TESTS', (self.plugin / 'Source/Kadan/Kadan.Build.cs').read_text(encoding='utf-8'))
        self.assertEqual(self.runtime, self.files('Binaries'))
        self.assertTrue((self.plugin / 'Source/Kadan/Private/Product.cpp').is_file())

    def test_repeated_staging_does_not_rewrite_sources(self):
        staging.stage(self.plugin, enabled=True)
        before = {p: p.stat().st_mtime_ns for p in self.plugin.rglob('*') if p.is_file()}
        self.assertEqual(staging.stage(self.plugin, enabled=True)['touched'], [])
        self.assertEqual(before, {p: p.stat().st_mtime_ns for p in self.plugin.rglob('*') if p.is_file()})

    def test_release_omits_all_automation_and_tests_module(self):
        staging.stage(self.plugin, enabled=True)
        result = staging.stage(self.plugin, enabled=False)
        self.assertEqual(result['tests'], 0)
        self.assertEqual(self.files('Source/P4MapKitTests'), {})
        self.assertEqual([m['Name'] for m in self.modules()], list(staging.PRODUCTS))
        self.assertEqual(self.runtime, self.files('Binaries'))

    def test_plain_development_also_omits_tests(self):
        staging.stage(self.plugin)
        self.assertFalse((self.plugin / self.authoring).exists())
        self.assertFalse((self.plugin / self.blueprint).exists())
        self.assertEqual(self.files('Source/P4MapKitTests'), {})

    def test_unknown_staged_test_edit_refused(self):
        staging.stage(self.plugin, enabled=True)
        self.write('Source/P4MapKitTests/Private/Authoring/FirstAutomation.cpp', '// another worker edit\n')
        self.refuse_atomically()

    def test_unknown_staged_source_removal_refused(self):
        self.write('Source/P4MapKitTests/Private/Unknown.cpp', '// another worker source\n')
        self.refuse_atomically()

    def test_exact_pending_scaffold_migrates_without_runtime_changes(self):
        for path in (self.canonical / 'Source/P4MapKitTests').rglob('*.pending'):
            self.write('Source/P4MapKitTests/' + path.relative_to(self.canonical / 'Source/P4MapKitTests').as_posix(), path.read_bytes())
            path.rename(path.with_name(path.name[:-8]))
        staging.stage(self.plugin, enabled=True)
        self.assertFalse(list((self.plugin / 'Source/P4MapKitTests').rglob('*.pending')))
        self.assertEqual(self.runtime, self.files('Binaries'))
        self.assertEqual(len(self.files('Source/P4MapKitTests')), 5)

    def test_exact_pending_scaffold_release_exclusion(self):
        for path in (self.canonical / 'Source/P4MapKitTests').rglob('*.pending'):
            self.write('Source/P4MapKitTests/' + path.relative_to(self.canonical / 'Source/P4MapKitTests').as_posix(), path.read_bytes())
            path.rename(path.with_name(path.name[:-8]))
        staging.stage(self.plugin, enabled=False)
        self.assertEqual(self.files('Source/P4MapKitTests'), {})
        self.assertEqual(self.runtime, self.files('Binaries'))

    def test_changed_pending_scaffold_is_refused_atomically(self):
        self.write('Source/P4MapKitTests/P4MapKitTests.Build.cs.pending', '// unowned changes\n')
        self.refuse_atomically()

    def test_unknown_pending_file_is_refused_atomically(self):
        self.write('Source/P4MapKitTests/Private/Unknown.cpp.pending', '// other owner\n')
        self.refuse_atomically()

    def test_exact_original_owned_build_scaffold_migrates(self):
        for name, old in LEGACY_SCAFFOLD.items():
            self.write('Source/P4MapKitTests/' + name + '.pending', old)
        staging.stage(self.plugin, enabled=True)
        self.assertFalse(list((self.plugin / 'Source/P4MapKitTests').rglob('*.pending')))
        self.assertEqual(self.runtime, self.files('Binaries'))

    def test_renamed_embedded_test_refused(self):
        self.write('Source/Kadan/Private/Unknown.cpp', 'IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnknown, "Unknown", 0)\n')
        self.refuse_atomically()

    def test_custom_complex_embedded_test_refused(self):
        self.write('Source/P4BlueprintImport/Private/Unknown.cpp', 'IMPLEMENT_CUSTOM_COMPLEX_AUTOMATION_TEST (FUnknown, "Unknown", 0)\n')
        self.refuse_atomically()

    def test_both_product_modules_required(self):
        self.write('Kadan.uplugin', json.dumps(dict(Modules=[dict(Name=staging.PRODUCTS[0])])) )
        self.refuse_atomically()

    def test_unknown_or_duplicate_product_module_refused(self):
        for name in ('UnexpectedModule', staging.PRODUCTS[0]):
            self.write('Kadan.uplugin', json.dumps(dict(Modules=[dict(Name=product) for product in staging.PRODUCTS] + [dict(Name=name)])))
            self.refuse_atomically()

    def test_incomplete_scaffold_refused(self):
        (self.canonical / 'Source/P4MapKitTests/Private/P4MapKitTestsPCH.h.pending').unlink()
        self.refuse_atomically()

    def test_unrecognized_product_test_flag_refused(self):
        self.write('Source/Kadan/Kadan.Build.cs', 'Definitions.Add("P4_MAPKIT_TESTS=1");\n')
        self.refuse_atomically()

    def test_receipt_cannot_claim_runtime_files(self):
        self.write('test-module-sources.json', json.dumps(dict(owner=staging.OWNER, files={'Binaries/Win64/UE4Editor.modules': '0' * 64})))
        self.refuse_atomically()

    def test_receipt_digest_must_be_valid(self):
        self.write('test-module-sources.json', json.dumps(dict(owner=staging.OWNER, files={'Source/P4MapKitTests/Private/Bad.cpp': 'invalid'})))
        self.refuse_atomically()

    def test_foreign_receipt_refused(self):
        self.write('test-module-sources.json', json.dumps(dict(owner='another-agent', files={})))
        self.refuse_atomically()

    def test_hardlinked_product_unit_refused(self):
        original = self.base / 'outside.cpp'
        original.write_text('// source\n', encoding='utf-8')
        linked = self.plugin / 'Source/Kadan/Private/LinkedAutomation.cpp'
        os.link(original, linked)
        self.refuse_atomically()
        self.assertEqual(original.read_text(encoding='utf-8'), '// source\n')

    def test_hardlinked_descriptor_refused(self):
        descriptor = self.plugin / 'Kadan.uplugin'
        os.link(descriptor, self.base / 'outside.uplugin')
        self.refuse_atomically()

    def test_python311_reparse_directory_alias_refused(self):
        original = Path.lstat
        source = (self.plugin / 'Source').resolve()
        def lstat(path):
            value = original(path)
            if path == source:
                return types.SimpleNamespace(st_mode=value.st_mode, st_file_attributes=0x400)
            return value
        with patch.object(Path, 'lstat', lstat):
            self.refuse_atomically()

    def test_authoritative_refresh_removes_deleted_last_test(self):
        staging.stage(self.plugin, enabled=True)
        result = staging.stage(self.plugin, enabled=True, fresh_product_sources=True)
        self.assertEqual(result['tests'], 0)
        self.assertEqual(set(self.files('Source/P4MapKitTests')), {'Source/P4MapKitTests/P4MapKitTests.Build.cs', 'Source/P4MapKitTests/Private/P4MapKitTestsModule.cpp', 'Source/P4MapKitTests/Private/P4MapKitTestsPCH.h'})

    def test_disabled_removes_matching_unowned_scaffold(self):
        self.write('Source/P4MapKitTests/P4MapKitTests.Build.cs', 'class P4MapKitTests {}\n')
        staging.stage(self.plugin, enabled=False)
        self.assertEqual(self.files('Source/P4MapKitTests'), {})

    def test_canonical_source_rejected(self):
        with self.assertRaises(ValueError):
            staging.stage(self.canonical, enabled=True)

    def test_incoming_canonical_test_has_no_product_copy_and_updates_exact_unit(self):
        (self.plugin / self.authoring).unlink()
        content = b'IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNew, "New", 0)\n'
        staging.stage(self.plugin, enabled=True, fresh_product_sources=True, incoming_tests={self.authoring: content})
        target = self.plugin / 'Source/P4MapKitTests/Private/Authoring/FirstAutomation.cpp'
        self.assertEqual(target.read_bytes(), content)
        self.assertFalse((self.plugin / self.authoring).exists())

    def test_incoming_source_path_and_filename_collision_refused(self):
        for name in ('Source/Kadan/../Private/FirstAutomation.cpp', 'Binaries/Win64/BadAutomation.cpp'):
            self.refuse_atomically(incoming_tests={name: b'// no admission\n'})
        self.refuse_atomically(incoming_tests={'Source/Kadan/Another/FirstAutomation.cpp': b'// conflicting unit\n'})

    def test_production_source_gate(self):
        with self.assertRaises(ValueError):
            staging.assert_production_sources(self.plugin)
        staging.stage(self.plugin, enabled=False)
        staging.assert_production_sources(self.plugin)
        self.write('Source/P4MapKitTests/Private/Unexpected.cpp', '// stray test module\n')
        with self.assertRaises(ValueError):
            staging.assert_production_sources(self.plugin)


class SeamStaging(unittest.TestCase):
    """The seams' sources live with the tests and compile into the Kadan module of a test stage only."""

    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        base = Path(temp.name)
        self.canonical = base / 'canonical'
        self.plugin = base / 'snapshot'
        patcher = patch.object(staging, 'CANONICAL', self.canonical)
        patcher.start()
        self.addCleanup(patcher.stop)
        for name, body in (('P4MapKitTests.Build.cs', 'class P4MapKitTests {}\n'),
                           ('Private/P4MapKitTestsModule.cpp', 'IMPLEMENT_MODULE(FModule, P4MapKitTests)\n'),
                           ('Private/P4MapKitTestsPCH.h', '#pragma once\n'),
                           ('Private/Authoring/FirstAutomation.cpp', 'IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTest, "First", 0)\n'),
                           ('KadanSeams/P4ExampleTestSeams.cpp', '#if P4_KADAN_TEST_SEAMS\n#endif\n')):
            self.put(self.canonical / 'Source/P4MapKitTests' / name, body)
        modules = [dict(Name=name, Type='Editor', LoadingPhase='Default') for name in staging.PRODUCTS + staging.UNTESTED]
        self.put(self.plugin / 'Kadan.uplugin', json.dumps(dict(Modules=modules)))
        self.put(self.plugin / 'Source/Kadan/Kadan.Build.cs', 'class Kadan {\n        PrivatePCHHeaderFile = "Private/P4MapKitPCH.h";\n}\n')
        for name in staging.PRODUCTS:
            self.put(self.plugin / 'Source' / name / 'Private/Product.cpp', '// product\n')
        self.seam = self.plugin / 'Source/Kadan/Private/P4ExampleTestSeams.cpp'

    @staticmethod
    def put(path, text):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf-8')

    def test_a_test_stage_compiles_the_seams_into_kadan_and_a_release_stage_removes_them(self):
        self.assertEqual(staging.seam_targets(), {'Source/Kadan/Private/P4ExampleTestSeams.cpp'})
        result = staging.stage(self.plugin, enabled=True)
        self.assertEqual(result['tests'], 1)
        self.assertEqual(self.seam.read_text(encoding='utf-8'), '#if P4_KADAN_TEST_SEAMS\n#endif\n')
        self.assertFalse((self.plugin / 'Source/P4MapKitTests/KadanSeams').exists())
        self.assertIn('P4_KADAN_TEST_SEAMS=1', (self.plugin / 'Source/Kadan/Kadan.Build.cs').read_text(encoding='utf-8'))
        # Stagers from before the seams moved accept only test module paths in the receipt's files.
        receipt = json.loads((self.plugin / 'test-module-sources.json').read_text(encoding='utf-8'))
        self.assertTrue(all(name.startswith('Source/P4MapKitTests/') for name in receipt['files']))
        self.assertEqual(set(receipt['seams']), {'Source/Kadan/Private/P4ExampleTestSeams.cpp'})
        with self.assertRaises(ValueError):
            staging.assert_production_sources(self.plugin)
        staging.stage(self.plugin, enabled=False)
        self.assertFalse(self.seam.exists())
        staging.assert_production_sources(self.plugin)

    def test_a_release_source_with_a_seam_in_kadan_is_refused(self):
        staging.stage(self.plugin, enabled=False)
        self.put(self.seam, '// copied by hand\n')
        with self.assertRaises(ValueError):
            staging.assert_production_sources(self.plugin)


if __name__ == '__main__':
    unittest.main()
