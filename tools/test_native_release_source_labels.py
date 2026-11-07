"""Release-only source label and compiler admission guard checks."""
import subprocess
import json
import hashlib
import os
import shutil
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

if __package__:
    from . import native_release_source_labels as labels
else:
    import native_release_source_labels as labels


def original_defaults_fixture():
    """Authored parser input; this is not native release qualification evidence."""
    literal = 'C:/home/Project/carrrot4u/Saved/Carrot4/Saved'
    return dict(
        format='p4-fixed-declarations-original-defaults-proof', version=1,
        source_executable_sha256='fe616565ecfd35868fdd9a43525931a3bd46c383151e2a244cdf8e199a974190',
        original_property=dict(struct='/Script/Carrot4.SaveLoadConfig', name='HostMountPath',
                               kind='StrProperty', status='observed', array_dim=1, flags='0x18000000000205'),
        allowed_original_default_rows=[
            dict(zip(('class', 'subobject', 'property', 'value', 'kind'), row))
            for row in (
                ('/Script/Carrot4.GameConfig', '', 'SaveLoadConfig.HostMountPath', literal, 'Text'),
                ('/Script/Carrot4.GameConfigActor', 'GameConfig', 'SaveLoadConfig.HostMountPath', literal, 'Text'))])


class SourceLabels(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='kadan-label-tests-')
        self.root = Path(self.temp.name).resolve()
        (self.root / 'Kadan.uplugin').write_text('{}', encoding='utf-8')

    def tearDown(self):
        self.temp.cleanup()

    def source(self, name, content):
        path = self.root / 'Source/Example/Private' / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)
        return path

    def original_defaults_proof(self):
        path = self.root / 'original-defaults.json'
        path.write_text(json.dumps(original_defaults_fixture()), encoding='utf-8')
        return path

    def test_labels_preserve_source_lines_and_repeat_without_rewrites(self):
        original = b'const char* name=__FILE__;\nint line=__LINE__;\n'
        path = self.source('sample.cpp', original)
        first = labels.label_first_party_sources(self.root)
        expected = b'#line 1 "Kadan/Source/Example/Private/sample.cpp"\n' + original
        self.assertEqual(path.read_bytes(), expected)
        stamp = path.stat().st_mtime_ns
        self.assertEqual(labels.label_first_party_sources(self.root), first)
        self.assertEqual(path.stat().st_mtime_ns, stamp)

    def test_utf8_signature_stays_at_translation_unit_start(self):
        path = self.source('sample.h', b'\xef\xbb\xbf// creator UTF-8\n')
        labels.label_first_party_sources(self.root)
        once = path.read_bytes()
        self.assertEqual(once, b'\xef\xbb\xbf// creator UTF-8\n')
        labels.label_first_party_sources(self.root)
        self.assertEqual(path.read_bytes(), once)

    def test_headers_keep_physical_lines_with_compiler_filename_mapping(self):
        compiler = shutil.which('clang++')
        self.assertIsNotNone(compiler, 'Clang preprocessor is required for the host checks')
        for extension in ('.h', '.hpp', '.inl'):
            with self.subTest(extension=extension):
                original = b'const char* header_file = __FILE__;\nint header_line = __LINE__;\n'
                path = self.source('sample' + extension, original)
                labels.label_first_party_sources(self.root)
                translation = self.source('probe.cpp', ('#include "sample' + extension + '"\n').encode('utf-8'))
                completed = subprocess.run([str(compiler), '-E', '-P', '-x', 'c++',
                                          '-ffile-prefix-map=' + str(self.root) + '=Kadan', str(translation)],
                                          capture_output=True, text=True, timeout=30)
                self.assertEqual(completed.returncode, 0, completed.stderr)
                label = 'Kadan/Source/Example/Private/sample' + extension
                self.assertIn('header_file = "' + label + '";', completed.stdout.replace('\\\\', '/'))
                self.assertIn('header_line = 2;', completed.stdout)
                once = path.read_bytes()
                self.assertEqual(once, original)
                stamp = path.stat().st_mtime_ns
                labels.label_first_party_sources(self.root)
                self.assertEqual(path.read_bytes(), once)
                self.assertEqual(path.stat().st_mtime_ns, stamp)

    def test_legacy_header_stage_is_upgraded_without_stacking_directives(self):
        original = b'#pragma once\n// preserved original body\n'
        path = self.source('sample.h', b'#line 1 "Kadan/Source/Example/Private/sample.h"\n' + original)
        labels.label_first_party_sources(self.root)
        result = path.read_bytes()
        self.assertEqual(result, original)

    def test_guarded_legacy_header_restores_exact_body_and_physical_macro_lines(self):
        label = 'Kadan/Source/Example/Private/sample.h'
        original = b'#pragma once\nGENERATED_BODY()\n'
        prefix = ('#ifdef __cplusplus\n#line 0 "' + label + '"\n#endif\n').encode('utf-8')
        path = self.source('sample.h', b'\xef\xbb\xbf' + prefix + original)
        report = labels.label_first_party_sources(self.root)
        self.assertEqual(path.read_bytes(), b'\xef\xbb\xbf' + original)
        self.assertEqual(report['files']['Source/Example/Private/sample.h']['label_policy'], 'compiler-trim')
        self.assertIsNone(report['files']['Source/Example/Private/sample.h']['label'])
        self.assertEqual(path.read_bytes().splitlines()[1], b'GENERATED_BODY()')

    def test_foreign_header_directive_is_not_removed(self):
        original = b'#line 1 "Other/Source/sample.h"\n// untouched\n'
        path = self.source('sample.h', original)
        labels.label_first_party_sources(self.root)
        self.assertEqual(path.read_bytes(), original)

    def test_canonical_source_refused_before_writing(self):
        with self.assertRaises(ValueError):
            labels.label_first_party_sources(labels.CANONICAL)

    def test_isolated_stub_stage_uses_its_own_relative_label(self):
        (self.root / 'P4Stubs.uplugin').write_text('{}', encoding='utf-8')
        path = self.source('stub.cpp', b'const char* name=__FILE__;\n')
        labels.label_first_party_sources(self.root, plugin_name='P4Stubs')
        self.assertTrue(path.read_bytes().startswith(b'#line 1 "P4Stubs/Source/'))
        with self.assertRaises(ValueError):
            labels.label_first_party_sources(labels.STUB_CANONICAL, plugin_name='P4Stubs')
        with self.assertRaises(ValueError):
            labels.label_first_party_sources(self.root, plugin_name='../Other')

    def test_untrusted_filename_refused(self):
        path = self.source('unsafe name.cpp', b'// keep\n')
        with self.assertRaises(ValueError):
            labels.label_first_party_sources(self.root)
        self.assertEqual(path.read_bytes(), b'// keep\n')

    def test_hardlinked_source_refused_without_mutating_other_link(self):
        path = self.source('sample.cpp', b'// untouched origin\n')
        other = self.root / 'origin.cpp'
        try:
            os.link(path, other)
        except OSError:
            self.skipTest('Host does not allow creating a temporary hard link')
        with self.assertRaises(ValueError):
            labels.label_first_party_sources(self.root)
        self.assertEqual(other.read_bytes(), b'// untouched origin\n')

    def test_other_files_remain_exact(self):
        self.source('sample.cpp', b'// source\n')
        path = self.source('configuration.json', b'{"private":"fixture"}')
        labels.label_first_party_sources(self.root)
        self.assertEqual(path.read_bytes(), b'{"private":"fixture"}')

    def test_compiler_provenance_tracks_labeled_stage_preserving_origin(self):
        path = self.source('compiler.cpp', b'// own compiler\n')
        manifest = self.source('SOURCE_PROVENANCE.json', json.dumps(dict(files={
            'compiler.cpp': dict(source_sha256='0'*64, staged_sha256='1'*64)})).encode('utf-8'))
        labels.label_first_party_sources(self.root)
        record = json.loads(manifest.read_text(encoding='utf-8'))['files']['compiler.cpp']
        self.assertEqual(record['source_sha256'], '0'*64)
        self.assertEqual(record['staged_sha256'], hashlib.sha256(path.read_bytes()).hexdigest())
        self.assertEqual(record['diagnostic_label'], 'Kadan/Source/Example/Private/compiler.cpp')

    def test_compiler_must_emit_all_relative_labels(self):
        headers = ''.join('"source-label-header' + extension + '"\n2\n"physical-body-3"\n'
                          for extension in ('.h', '.hpp', '.inl'))
        result = subprocess.CompletedProcess([], 0, '"SourceLabelProbe0.h"\n"Kadan/SourceLabelProbe.cpp"\n' + headers, '')
        with patch.object(labels.subprocess, 'run', return_value=result):
            self.assertTrue(labels.probe_trim_arguments('cl.exe', self.root / 'probe', [self.root])['ok'])
        for output in ('"C:\\\\Users\\\\Private\\\\SourceLabelProbe0.h"\n"Kadan/SourceLabelProbe.cpp"\n',
                       '"SourceLabelProbe0.h"\n'):
            result.stdout = output
            with patch.object(labels.subprocess, 'run', return_value=result):
                self.assertFalse(labels.probe_trim_arguments('cl.exe', self.root / 'probe', [self.root])['ok'])

    def test_header_absolute_path_line_shift_or_wrong_generated_macro_refuses(self):
        good = ('"SourceLabelProbe0.h"\n"Kadan/SourceLabelProbe.cpp"\n' +
                ''.join('"source-label-header' + ext + '"\n2\n"physical-body-3"\n'
                        for ext in ('.h', '.hpp', '.inl')))
        for bad in (good.replace('source-label-header.h', 'C:/Private/source-label-header.h'),
                    good.replace('\n2\n', '\n5\n', 1),
                    good.replace('physical-body-3', 'P4_BODY_6', 1)):
            result = subprocess.CompletedProcess([], 0, bad, '')
            with patch.object(labels.subprocess, 'run', return_value=result):
                self.assertFalse(labels.probe_trim_arguments('cl.exe', self.root / 'probe', [self.root])['ok'])

    def test_canonical_compiler_environment_delivery_is_probed_without_changing_parent(self):
        good = ('"SourceLabelProbe0.h"\n"Kadan/SourceLabelProbe.cpp"\n' +
                ''.join('"source-label-header' + ext + '"\n2\n"physical-body-3"\n'
                        for ext in ('.h', '.hpp', '.inl')))
        result = subprocess.CompletedProcess([], 0, good, '')
        inherited = dict(PATH='toolchain path', _CL_='/inherited-option')
        with patch.object(labels.subprocess, 'run', return_value=result) as run:
            self.assertTrue(labels.probe_trim_arguments('cl.exe', self.root / 'probe', [self.root], inherited)['ok'])
        self.assertFalse(any(arg.startswith('/d1trimfile:') for arg in run.call_args.args[0]))
        self.assertIn('/d1trimfile:', run.call_args.kwargs['env']['_CL_'])
        self.assertTrue(run.call_args.kwargs['env']['_CL_'].startswith('/inherited-option '))
        self.assertEqual(run.call_args.kwargs['env']['PATH'], 'toolchain path')
        self.assertEqual(inherited, dict(PATH='toolchain path', _CL_='/inherited-option'))

    def test_ignored_option_never_qualifies(self):
        result = subprocess.CompletedProcess([], 0, '"SourceLabelProbe0.h"\n"Kadan/SourceLabelProbe.cpp"\n',
                                             'warning D9002: ignoring unknown option')
        with patch.object(labels.subprocess, 'run', return_value=result):
            self.assertFalse(labels.probe_trim_arguments('cl.exe', self.root / 'probe', [self.root])['ok'])

    def test_escaped_private_path_is_decoded_before_relative_source_proof(self):
        good = ('"SourceLabelProbe0.h"\n"Kadan/SourceLabelProbe.cpp"\n' +
                ''.join('"source-label-header' + ext + '"\n2\n"physical-body-3"\n'
                        for ext in ('.h', '.hpp', '.inl')))
        encoded_path = '"\\u0043:\\u005cUsers\\u005cPrivate\\u005cSourceLabelProbe0.h"'
        result = subprocess.CompletedProcess([], 0, good.replace('"SourceLabelProbe0.h"', encoded_path), '')
        with patch.object(labels.subprocess, 'run', return_value=result):
            report = labels.probe_trim_arguments('cl.exe', self.root / 'probe', [self.root])
        self.assertFalse(report['ok'])
        self.assertFalse(report['relative_labels'])
        self.assertFalse(report['malformed_literals'])
        result.stdout = good.replace('SourceLabelProbe0.h', 'bad\\q')
        with patch.object(labels.subprocess, 'run', return_value=result):
            report = labels.probe_trim_arguments('cl.exe', self.root / 'probe', [self.root])
        self.assertFalse(report['ok'])
        self.assertTrue(report['malformed_literals'])

    def test_final_binary_audit_detects_both_encodings_without_receipt_path_leaks(self):
        path = self.root / 'module.dll'
        private = 'C:\\Users\\PrivateUser\\Research\\Owned.cpp'
        engine = 'C:/UE_4.26/Engine/Source/Runtime/Core/Public/Array.h'
        path.write_bytes(b'MZ\0' + private.encode('utf-8') + b'\0' + engine.encode('utf-16-le') + b'\0\0')
        report = labels.audit_dll_source_labels(path)
        self.assertFalse(report['ok'])
        self.assertEqual(report['counts']['utf8'], 1)
        self.assertEqual(report['counts']['utf16'], 1)
        self.assertEqual(report['counts']['user_path_strings'], 1)
        self.assertNotIn('PrivateUser', json.dumps(report))
        self.assertNotIn('UE_4.26', json.dumps(report))

    def test_relative_labels_and_web_urls_do_not_fail_binary_audit(self):
        path = self.root / 'module.dll'
        path.write_bytes(b'MZ\0Kadan/Source/Example.cpp\0https://example.invalid/help\0')
        self.assertTrue(labels.audit_dll_source_labels(path)['ok'])

    def test_virtual_save_paths_and_code_bytes_do_not_fail_binary_audit(self):
        path = self.root / 'module.dll'
        path.write_bytes(b'MZ\0save:/tsuchi/new_maps.json\0Z:\\T^\0\\4b(d:\\4b(d:\\(b(d:V(X(Z\0P(\\Lv:\\xPL\\\0')
        self.assertTrue(labels.audit_dll_source_labels(path)['ok'])
        for bad in ('C:/Users/Someone/Owned.cpp', 'D:\\Program Files (x86)\\Kit\\a.h', '(C:\\Work\\b.cpp', '\\\\server\\share\\c.h'):
            path.write_bytes(b'MZ\0' + bad.encode('utf-8') + b'\0')
            self.assertFalse(labels.audit_dll_source_labels(path)['ok'], bad)

    def test_type_tags_and_code_bytes_mid_string_do_not_fail_binary_audit(self):
        path = self.root / 'module.dll'
        tag = 'array:struct:/Script/Engine.MaterialTextureInfo'
        path.write_bytes(b'MZ\0' + tag.encode('utf-16-le') + b'\0\0\\h`Vbbd,f:\\fh6\\lj\0')
        self.assertTrue(labels.audit_dll_source_labels(path)['ok'])
        for bad in ('Failed to open C:\\Users\\Someone\\a.cpp', 'file="D:/Work/b.h"', 'xC:\\Work\\c.cpp'):
            path.write_bytes(b'MZ\0' + bad.encode('utf-8') + b'\0\0\0' + bad.encode('utf-16-le') + b'\0\0')
            report = labels.audit_dll_source_labels(path)
            self.assertFalse(report['ok'], bad)
            self.assertEqual((report['counts']['utf8'], report['counts']['utf16']), (1, 1), bad)

    def test_missing_or_non_pe_module_cannot_qualify(self):
        self.assertFalse(labels.audit_dll_source_labels(self.root / 'missing.dll')['ok'])
        path = self.root / 'module.dll'
        path.write_bytes(b'not a DLL')
        self.assertFalse(labels.audit_dll_source_labels(path)['ok'])

    def test_only_exact_original_carrot4_default_with_both_rows_qualifies(self):
        proof = self.original_defaults_proof()
        value = 'C:/home/Project/carrrot4u/Saved/Carrot4/Saved'
        path = self.root / 'UE4Editor-Carrot4.dll'
        path.write_bytes(b'MZ\0' + value.encode('utf-8') + b'\0\0' + value.encode('utf-16-le') + b'\0\0')
        self.assertFalse(labels.audit_dll_source_labels(path)['ok'])
        report = labels.audit_dll_source_labels(path, original_defaults_proof=proof)
        self.assertTrue(report['ok'])
        self.assertEqual(report['counts']['qualified_original_default_strings'], 2)
        for bad in (value + '/Owned.cpp', 'C:/home/Private/Owned.cpp', value.lower()):
            path.write_bytes(b'MZ\0' + bad.encode('utf-8') + b'\0')
            self.assertFalse(labels.audit_dll_source_labels(path, original_defaults_proof=proof)['ok'])
        data = json.loads(proof.read_text(encoding='utf-8'))
        data['allowed_original_default_rows'].pop()
        proof.write_text(json.dumps(data), encoding='utf-8')
        self.assertFalse(labels.audit_dll_source_labels(path, original_defaults_proof=proof)['ok'])

    def test_original_default_exception_cannot_be_used_for_another_module(self):
        proof = self.original_defaults_proof()
        path = self.root / 'UE4Editor-AkAudio.dll'
        path.write_bytes(b'MZ\0C:/home/Project/carrrot4u/Saved/Carrot4/Saved\0')
        self.assertFalse(labels.audit_dll_source_labels(path, original_defaults_proof=proof)['ok'])

    def test_original_default_evidence_requires_integer_schema_fields(self):
        original = original_defaults_fixture()
        proof = self.root / 'original-defaults.json'
        path = self.root / 'UE4Editor-Carrot4.dll'
        path.write_bytes(b'MZ\0C:/home/Project/carrrot4u/Saved/Carrot4/Saved\0')
        for field in ('version', 'array_dim'):
            for invalid in (True, 1.0, '1'):
                with self.subTest(field=field, value=invalid):
                    data = json.loads(json.dumps(original))
                    target = data if field == 'version' else data['original_property']
                    target[field] = invalid
                    proof.write_text(json.dumps(data), encoding='utf-8')
                    self.assertFalse(labels.audit_dll_source_labels(path, original_defaults_proof=proof)['ok'])


if __name__ == '__main__':
    unittest.main()
