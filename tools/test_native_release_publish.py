from pathlib import Path
import sys
import tempfile
import unittest
import zipfile
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parent))
import native_release_publish as release

class PublishTests(unittest.TestCase):
    def archive(self,path,name):
        with zipfile.ZipFile(path,'w') as archive:archive.writestr(name,b'candidate')
    def test_missing_qualification_never_replaces_previous_release(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);candidate=root/'candidate.zip';dest=root/'release.zip'
            self.archive(candidate,'Plugins/Kit.dll');dest.write_bytes(b'previous')
            with patch.object(release.native_quality_gate,'check',return_value=dict(ready=False,errors=['unmeasured'])):
                self.assertFalse(release.publish(candidate,dest,{}, {},root)['ready'])
            self.assertEqual(dest.read_bytes(),b'previous')
    def test_private_python_and_game_assets_cannot_be_published(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);candidate=root/'candidate.zip';dest=root/'release.zip'
            for name in ('Content/Python/main.py','Saved/source-custody/source.uexp','Content/Carrot4/Mesh.uasset','../outside.dll'):
                self.archive(candidate,name)
                with patch.object(release.native_quality_gate,'check',return_value=dict(ready=True,errors=[])):
                    self.assertFalse(release.publish(candidate,dest,{}, {},root)['ready'])
                self.assertFalse(dest.exists())
    def test_success_is_atomic_and_staged_recheck_failure_preserves_previous(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);candidate=root/'candidate.zip';dest=root/'release.zip'
            self.archive(candidate,'Plugins/Kit.dll');dest.write_bytes(b'previous')
            with patch.object(release.native_quality_gate,'check',side_effect=[dict(ready=True,errors=[]),dict(ready=False,errors=['changed during copy'])]):
                self.assertFalse(release.publish(candidate,dest,{}, {},root)['ready'])
            self.assertEqual(dest.read_bytes(),b'previous')
            self.assertFalse(list(root.glob('.native-kit-*')))
            with patch.object(release.native_quality_gate,'check',side_effect=lambda *args:dict(ready=True,errors=[])):
                self.assertTrue(release.publish(candidate,dest,{}, {},root)['ready'])
            self.assertEqual(dest.read_bytes(),candidate.read_bytes())

    def test_compiled_private_automation_is_not_distributed(self):
        with tempfile.TemporaryDirectory() as folder:
            candidate=Path(folder)/'candidate.zip'
            with zipfile.ZipFile(candidate,'w') as archive:
                archive.writestr('Plugins/Kadan/Binaries/Win64/UE4Editor-Kadan.dll',
                                 'P4MapKit.Quality.TableAdmission'.encode('utf-16-le'))
            self.assertTrue(any('automation' in error for error in release.audit_archive(candidate)))

    def test_game_derived_declarations_never_enter_shareable_distribution(self):
        with tempfile.TemporaryDirectory() as folder:
            candidate = Path(folder) / 'candidate.zip'
            for name in ('P4Stubs/P4Stubs.uplugin', 'Plugins/P4Stubs/Binaries/Win64/UE4Editor-Carrot4.dll',
                         'Elsewhere/UE4Editor-AkAudio.dll'):
                with self.subTest(name=name):
                    self.archive(candidate, name)
                    self.assertTrue(any('Game-derived type declarations' in error for error in release.audit_archive(candidate)))

if __name__=='__main__':unittest.main()
