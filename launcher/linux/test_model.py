import io
import json
import tarfile
import tempfile
import unittest
from pathlib import Path

from model import Session, REPO, digest, extract_verified, apply_package, parse_releases

def release(value, draft=False):
    name=f'Yo-kai-Watch-2-Linux-x64-v{value}.tar.gz'
    return {'tag_name':'v'+value,'draft':draft,'prerelease':False,'assets':[{'name':name,'state':'uploaded','size':123,'digest':'sha256:'+'1'*64,'browser_download_url':f'https://github.com/{REPO}/releases/download/v{value}/{name}'}]}

class Updates(unittest.TestCase):
    def test_latest_release_skips_intermediate_and_drafts(self):
        self.assertEqual(parse_releases([release('1.0.1'),release('1.0.3'),release('9.0.0',True)])['version'],'1.0.3')
    def test_bad_digest_or_url(self):
        r=release('2.0.0');r['assets'][0]['digest']='sha256:bad';self.assertIsNone(parse_releases([r]))
        r=release('2.0.0');r['assets'][0]['browser_download_url']='https://example.org/build.tar.gz';self.assertIsNone(parse_releases([r]))
    def test_full_update_and_save_preservation(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)/'Old builder with spaces';root.mkdir();(root/'VERSION').write_text('1.0.0');(root/'source').mkdir();(root/'source/old.txt').write_text('old')
            save=root/'dist/data/games/save/head.yw';save.parent.mkdir(parents=True);save.write_bytes(b'local save')
            rom=root/'my cartridge.3ds';rom.write_bytes(b'local rom');session=Session(root);session.require('1.0.3');self.assertTrue(session.update_required)
            stage=root/'launcher-data/stage';stage.mkdir();arc=stage/'update.tar.gz';prefix='Yo-kai-Watch-2-Linux-x64-v1.0.3'
            with tarfile.open(arc,'w:gz') as tar:
                for name,data in [('VERSION',b'1.0.3'),('build.sh',b'#!/bin/bash'),('YoKaiWatch2Builder',b'#!/bin/bash'),('launcher/linux/launcher.py',b'# launcher'),('source/clockwork/CMakeLists.txt',b'# new source')]:
                    info=tarfile.TarInfo(prefix+'/'+name);info.size=len(data);tar.addfile(info,io.BytesIO(data))
            asset={'version':'1.0.3','size':arc.stat().st_size,'sha256':digest(arc)}
            payload=extract_verified(arc,stage,asset);apply_package(root,payload,stage/'backup')
            self.assertEqual((root/'VERSION').read_text(),'1.0.3');self.assertEqual(save.read_bytes(),b'local save');self.assertEqual(rom.read_bytes(),b'local rom');self.assertFalse(Session(root).update_required);self.assertTrue((stage/'backup/source/old.txt').is_file())
    def test_rebuild_receipt(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);(root/'VERSION').write_text('1.0.3');dist=root/'dist';dist.mkdir();exe=dist/'YoKaiWatch2';exe.write_bytes(b'test executable');rom=root/'test.3ds';rom.write_bytes(b'test rom')
            from model import CODE_HASH
            m={'version':'1.0.1','cartridge':str(rom),'code_sha256':CODE_HASH,'title_id':'00040000001B2900','game_exe_sha256':digest(exe)}
            (dist/'build-manifest.json').write_text(json.dumps(m));self.assertTrue(Session(root).rebuild_required)
            m['version']='1.0.3';(dist/'build-manifest.json').write_text(json.dumps(m));self.assertFalse(Session(root).rebuild_required)

if __name__=='__main__':unittest.main()
