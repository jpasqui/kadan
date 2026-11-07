"""Focused file list, dependency, ownership and release-label boundary checks."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch

try:
    from . import native_core_sources as core
    from . import native_release_source_labels as labels
except ImportError:
    import native_core_sources as core
    import native_release_source_labels as labels

OUTPUT = core.ROOT / 'build/native-core-source-tests'


class CoreSources(unittest.TestCase):
    def setUp(self):
        OUTPUT.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=OUTPUT)
        self.root = Path(self.temp.name)
        self.repo = self.root / 'repo'  # Deliberately no .git.
        self.plugin = self.root / 'plugin'
        self.module = 'Kadan/Source/Kadan'
        self.write(self.module + '/Private/entry.cpp', '#include "P4MapKitCore/a.h"\n')
        self.write(core.CORE + '/a.h', '#include "nested/b.h"\n')
        self.write(core.CORE + '/nested/b.h', '#pragma once\n')
        for name in core.BACKENDS:
            self.write(core.CORE + '/' + name, '// backend\n')
        self.write(core.CORE + '/native_custom_backend_bits.h', '// portable bits\n')
        for name in core.backend.SOURCES + core.backend.HEADERS:
            self.write('compiler/' + name, '// compiler\n')
        self.admit()

    def tearDown(self):
        self.temp.cleanup()

    def write(self, relative, content):
        path = self.repo / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding='utf-8', newline='\n')
        return path

    def admit(self):
        """Lists every portable file the synthetic plugin reaches, as native_core_files.txt does."""
        names = []
        for path in core.source_closure(self.repo, tests=True):
            name = path.relative_to(self.repo).as_posix()
            if name.startswith(core.CORE + '/') or name.startswith('compiler/'):
                names.append(name)
        path = self.repo / core.LIST
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('# synthetic list\n' + '\n'.join(sorted(names)) + '\n', encoding='utf-8')

    def staged(self, name):
        return self.plugin / core.TARGET / name

    def test_transitive_closure_excludes_unused_research_without_git(self):
        self.write(core.CORE + '/research.h', '#error research must stay out\n')
        core.stage(self.plugin, repo=self.repo)
        self.assertTrue(self.staged('nested/b.h').is_file())
        self.assertFalse(self.staged('research.h').exists())
        self.assertFalse((self.repo / '.git').exists())

    def test_reachable_header_has_to_be_listed(self):
        self.write(core.CORE + '/promotion.h', '// proposed source\n')
        self.write(self.module + '/Private/entry.cpp', '#include "P4MapKitCore/promotion.h"\n')
        with self.assertRaisesRegex(ValueError, 'not in'):
            core.stage(self.plugin, repo=self.repo)
        self.assertFalse(self.plugin.exists())
        self.admit()
        core.stage(self.plugin, repo=self.repo)
        self.assertTrue(self.staged('promotion.h').is_file())
        self.write(core.CORE + '/promotion.h', '// changed\n')
        core.stage(self.plugin, repo=self.repo)
        self.assertEqual(self.staged('promotion.h').read_bytes(), b'// changed\n')

    def test_conditionals_and_comments_do_not_hide_dependencies(self):
        self.write(core.CORE + '/a.h', '/* #include "unknown.h" */\n#if 0\n#include "nested/b.h"\n#endif\n')
        self.admit()
        self.assertIn('nested/b.h', core.source_plan(self.repo))

    def test_crlf_checkout_stages_and_owns_its_raw_bytes(self):
        path = self.repo / core.CORE / 'a.h'
        original = path.read_bytes()
        path.write_bytes(original.replace(b'\n', b'\r\n'))
        self.assertEqual(core.source_plan(self.repo)['a.h'], path.read_bytes())
        core.stage(self.plugin, repo=self.repo)
        data = json.loads((self.plugin / core.RECEIPT).read_text(encoding='utf-8'))
        self.assertEqual(data['files']['a.h'], core.digest(path.read_bytes()))
        self.assertNotEqual(data['files']['a.h'], core.digest(original))

    def test_unresolved_internal_and_macro_includes_refuse(self):
        for text in ('#include "P4MapKitCore/missing.h"\n', '#include SELECTED_HEADER\n'):
            self.write(self.module + '/Private/entry.cpp', text)
            with self.assertRaises(ValueError):
                core.stage(self.plugin, repo=self.repo)
        self.assertFalse(self.plugin.exists())

    def test_ambiguous_module_include_refuses(self):
        self.write(self.module + '/Public/shared.h', '// authoring\n')
        self.write('Kadan/Source/P4BlueprintImport/Public/shared.h', '// blueprint\n')
        self.write(self.module + '/Private/entry.cpp', '#include "shared.h"\n')
        with self.assertRaisesRegex(ValueError, 'Ambiguous'):
            core.source_closure(self.repo)

    def test_include_escape_refuses(self):
        self.write(self.module + '/Private/entry.cpp', '#include "P4MapKitCore/../outside.h"\n')
        with self.assertRaisesRegex(ValueError, 'relative'):
            core.source_closure(self.repo)

    def test_repeated_stage_does_not_rewrite_and_owned_stale_file_is_removed(self):
        core.stage(self.plugin, repo=self.repo)
        stamps = {p: p.stat().st_mtime_ns for p in self.plugin.rglob('*') if p.is_file()}
        self.assertEqual(core.stage(self.plugin, repo=self.repo), [])
        self.assertEqual(stamps, {p: p.stat().st_mtime_ns for p in stamps})
        self.write(self.module + '/Private/entry.cpp', '// dependency removed\n')
        core.stage(self.plugin, repo=self.repo)
        self.assertFalse(self.staged('a.h').exists())
        self.assertFalse(self.staged('nested/b.h').exists())
        self.assertTrue((self.repo / core.CORE / 'a.h').exists())

    def test_unknown_and_modified_files_survive_refusal_before_updates(self):
        core.stage(self.plugin, repo=self.repo)
        original = self.staged(core.BACKENDS[0]).read_bytes()
        self.write(core.CORE + '/' + core.BACKENDS[0], '// reviewed update\n')
        self.admit()
        foreign = self.staged('foreign.h')
        foreign.write_bytes(b'// another worker\n')
        with self.assertRaisesRegex(ValueError, 'Unowned'):
            core.stage(self.plugin, repo=self.repo)
        self.assertEqual(self.staged(core.BACKENDS[0]).read_bytes(), original)
        self.assertEqual(foreign.read_bytes(), b'// another worker\n')
        foreign.unlink()
        self.staged('a.h').write_bytes(b'// another worker edit\n')
        with self.assertRaisesRegex(ValueError, 'outside its owner'):
            core.stage(self.plugin, repo=self.repo)
        self.assertEqual(self.staged('a.h').read_bytes(), b'// another worker edit\n')

    def test_exact_legacy_bytes_adopt_but_excluded_legacy_file_survives(self):
        self.staged('a.h').parent.mkdir(parents=True)
        self.staged('a.h').write_bytes((self.repo / core.CORE / 'a.h').read_bytes())
        core.stage(self.plugin, repo=self.repo)
        (self.plugin / core.RECEIPT).unlink()
        self.staged('research.h').write_bytes(b'// held legacy research\n')
        with self.assertRaisesRegex(ValueError, 'Unowned'):
            core.stage(self.plugin, repo=self.repo)
        self.assertTrue(self.staged('research.h').exists())
        self.assertFalse((self.plugin / core.RECEIPT).exists())

    def test_invalid_receipt_and_linked_metadata_refuse(self):
        core.stage(self.plugin, repo=self.repo)
        receipt = self.plugin / core.RECEIPT
        data = json.loads(receipt.read_text(encoding='utf-8'))
        data['files']['../escape.h'] = 'a' * 64
        receipt.write_text(json.dumps(data), encoding='utf-8')
        with self.assertRaises(ValueError):
            core.stage(self.plugin, repo=self.repo)
        receipt.unlink()
        # This filesystem denies creating hard links. Exercise the actual stage
        # guard using link metadata, without requiring elevated file operations.
        original_stat = Path.stat
        linked = self.staged('a.h')
        def linked_stat(path, *args, **kwargs):
            result = original_stat(path, *args, **kwargs)
            if path == linked:
                fields = {name: getattr(result, name) for name in dir(result) if name.startswith('st_')}
                fields['st_nlink'] = 2
                return SimpleNamespace(**fields)
            return result
        with patch.object(Path, 'stat', linked_stat):
            with self.assertRaisesRegex(ValueError, 'escapes'):
                core.stage(self.plugin, repo=self.repo)
        self.assertTrue(linked.is_file())

    def test_test_only_dependency_stays_out_of_the_product(self):
        self.write(core.CORE + '/test_only.h', '// test dependency\n')
        self.write(self.module + '/Private/ExampleAutomation.cpp', '#include "P4MapKitCore/test_only.h"\n')
        self.admit()
        self.assertNotIn('test_only.h', core.source_plan(self.repo))
        self.assertIn('test_only.h', core.source_plan(self.repo, tests=True))

    def test_reflected_fixture_header_resolves_only_in_development_closure(self):
        fixture = self.write('Kadan/Source/P4MapKitTests/Private/P4Fixture.h', '#pragma once\n')
        self.write(self.module + '/Private/FixtureAutomation.cpp', '#include "P4Fixture.h"\n')
        self.assertIn(fixture, core.source_closure(self.repo, tests=True))
        self.assertNotIn(fixture, core.source_closure(self.repo, tests=False))
        self.write(self.module + '/Private/entry.cpp', '#include "P4Fixture.h"\n')
        with self.assertRaisesRegex(ValueError, 'Unresolved internal include'):
            core.source_closure(self.repo, tests=False)

    def test_fixed_backend_adaptations_and_provenance(self):
        self.write('compiler/nvasm_parse.cpp',
                   '#include <bit>\n// OUT\nauto x=std::popcount(1);\nauto y=std::countr_zero(2);\n')
        self.admit()
        plan = core.source_plan(self.repo)
        staged = plan['KadanMaxwell/nvasm_parse.cpp']
        self.assertIn(b'#include "native_custom_backend_bits.h"', staged)
        self.assertIn(b'NvOut', staged)
        self.assertIn(b'nvasm::portable::popcount', staged)
        manifest = json.loads(plan['KadanMaxwell/SOURCE_PROVENANCE.json'])
        self.assertEqual(manifest['files']['nvasm_parse.cpp']['staged_sha256'], core.digest(staged))

    def test_unlisted_compiler_dependency_cannot_escape_fixed_archive(self):
        self.write('compiler/extra.h', '// proposed compiler dependency\n')
        self.write('compiler/nvasm_parse.cpp', '#include "extra.h"\n')
        self.admit()
        with self.assertRaisesRegex(ValueError, 'Unlisted compiler dependency'):
            core.source_plan(self.repo)

    def test_release_labels_refresh_ownership_and_repeat_safely(self):
        core.stage(self.plugin, repo=self.repo)
        (self.plugin / 'Kadan.uplugin').write_text('{}', encoding='utf-8')
        labels.label_first_party_sources(self.plugin)
        receipt = json.loads((self.plugin / core.RECEIPT).read_text(encoding='utf-8'))
        for name, expected in receipt['files'].items():
            self.assertEqual(expected, core.digest(self.staged(name).read_bytes()))
        stamp = (self.plugin / core.RECEIPT).stat().st_mtime_ns
        labels.label_first_party_sources(self.plugin)
        self.assertEqual(stamp, (self.plugin / core.RECEIPT).stat().st_mtime_ns)
        core.stage(self.plugin, repo=self.repo)
        self.assertEqual(self.staged(core.BACKENDS[0]).read_bytes(), b'// backend\n')

    def test_modified_owned_source_refuses_before_labeling_other_files(self):
        core.stage(self.plugin, repo=self.repo)
        (self.plugin / 'Kadan.uplugin').write_text('{}', encoding='utf-8')
        original = self.staged(core.BACKENDS[0]).read_bytes()
        self.staged('a.h').write_bytes(b'// foreign\n')
        with self.assertRaisesRegex(ValueError, 'before diagnostic'):
            labels.label_first_party_sources(self.plugin)
        self.assertEqual(self.staged(core.BACKENDS[0]).read_bytes(), original)


def main():
    global OUTPUT
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=OUTPUT)
    parser.add_argument('--review-root', type=Path, help='Read-only original inventory snapshot')
    args = parser.parse_args()
    OUTPUT = args.out.resolve()
    OUTPUT.mkdir(parents=True, exist_ok=True)
    pin_paths = (Path(__file__).resolve(), core.ROOT / 'tools/native_core_sources.py',
                 core.ROOT / core.LIST,
                 core.ROOT / 'tools/native_custom_backend_sources.py',
                 core.ROOT / 'tools/native_release_source_labels.py',
                 core.ROOT / 'tools/dev_build.py', core.ROOT / 'tools/native_test_module.py',
                 core.ROOT / 'docs/K6_SOURCE_REVIEW.json', core.ROOT / 'docs/K6_SOURCE_REVIEW.md')
    before = {path.relative_to(core.ROOT).as_posix(): core.digest(path.read_bytes()) for path in pin_paths}
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(CoreSources))
    inventory = json.loads((core.ROOT / 'docs/K6_SOURCE_REVIEW.json').read_text(encoding='utf-8'))
    reviewed = inventory['files']
    inventory_match = None
    if args.review_root:
        inventory_match = all(core.digest((args.review_root / row['path']).read_bytes()) == row['sha256'] for row in reviewed)
    product = core.source_plan()
    tests = core.source_plan(tests=True)
    counts = {}
    for row in reviewed:
        counts[row['classification']] = counts.get(row['classification'], 0) + 1
    pins = {path.relative_to(core.ROOT).as_posix(): core.digest(path.read_bytes()) for path in pin_paths}
    report = dict(format=1, passed=result.wasSuccessful() and inventory_match is not False and pins == before,
                  sources_unchanged=pins == before,
                  tests=result.testsRun, failures=len(result.failures), errors=len(result.errors),
                  review_files=len(reviewed), classifications=counts, review_bytes_match=inventory_match,
                  product_staged_files=len(product), development_staged_files=len(tests),
                  excluded_pending_files=len([r for r in reviewed if r['path'].startswith(core.CORE + '/')
                                              and r['path'][len(core.CORE) + 1:] not in product]),
                  source_pins=pins, scope='Host file list and source ownership only. No Unreal, cook or game result.')
    (OUTPUT / 'source-staging-host.json').write_text(json.dumps(report, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    print(json.dumps({k: v for k, v in report.items() if k != 'source_pins'}))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
