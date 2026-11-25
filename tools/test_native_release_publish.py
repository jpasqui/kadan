from pathlib import Path
import sys
import tempfile
import unittest
import zipfile
sys.path.insert(0,str(Path(__file__).resolve().parent))
import native_release_publish as release

class AuditTests(unittest.TestCase):
    def archive(self,path,*names):
        with zipfile.ZipFile(path,'w') as archive:
            for name in names:archive.writestr(name,b'candidate')

    def test_private_python_and_game_assets_are_refused(self):
        with tempfile.TemporaryDirectory() as folder:
            candidate=Path(folder)/'candidate.zip'
            for name in ('Content/Python/main.py','Saved/source-custody/source.uexp','Content/Carrot4/Mesh.uasset','../outside.dll',
                         'Kadan/Binaries/Win64/UE4Editor-Kadan.pdb','Kadan/Tools/game.pak','Kadan/Tools/global.utoc'):
                with self.subTest(name=name):
                    self.archive(candidate,name)
                    self.assertTrue(release.audit_archive(candidate))

    def test_compiled_private_automation_is_not_distributed(self):
        with tempfile.TemporaryDirectory() as folder:
            candidate=Path(folder)/'candidate.zip'
            for name in ('Kadan/Binaries/Win64/UE4Editor-Kadan.dll','P4Stubs/Binaries/Win64/UE4Editor-Carrot4.dll'):
                with self.subTest(name=name):
                    with zipfile.ZipFile(candidate,'w') as archive:
                        archive.writestr(name,'P4MapKit.Quality.TableAdmission'.encode('utf-16-le'))
                    self.assertTrue(any('automation' in error for error in release.audit_archive(candidate)))

    def test_p4stubs_descriptor_module_list_and_dlls_ship(self):
        with tempfile.TemporaryDirectory() as folder:
            candidate=Path(folder)/'candidate.zip'
            self.archive(candidate,'README.txt','Kadan/Kadan.uplugin','Kadan/BUILD.json',*sorted(release.STUB_MEMBERS))
            self.assertEqual(release.audit_archive(candidate),[])

    def test_p4stubs_sources_pdbs_and_other_files_are_refused(self):
        with tempfile.TemporaryDirectory() as folder:
            candidate=Path(folder)/'candidate.zip'
            for name in ('P4Stubs/Source/Carrot4/Public/Carrot4StubClasses.h','P4Stubs/Source/Carrot4/Private/Carrot4Module.cpp',
                         'P4Stubs/Source/Carrot4/Carrot4.Build.cs','P4Stubs/Binaries/Win64/UE4Editor-Carrot4.pdb',
                         'P4Stubs/unified-stubs.json','P4Stubs/default-rows.json','P4Stubs/Intermediate/Build/x.obj',
                         'Plugins/P4Stubs/Binaries/Win64/UE4Editor-Carrot4.dll','Kadan/Binaries/Win64/UE4Editor-AkAudio.dll',
                         'P4Stubs/Binaries/Win64/UE4Editor-P4StubsEditor.dll'):
                with self.subTest(name=name):
                    self.archive(candidate,name)
                    self.assertTrue(release.audit_archive(candidate))

if __name__=='__main__':unittest.main()
