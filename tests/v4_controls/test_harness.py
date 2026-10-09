"""Safety/geometry helpers used by the v4 native checks, independent of a build."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
from PIL import Image, ImageDraw
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from check_home import snapshot_backend, assert_header_fit

class NativeEvidence(unittest.TestCase):
    def fixture(self,root):
        build=root/'build';build.mkdir();output=root/'evidence';output.mkdir()
        binary=build/'echoy_backend';binary.write_bytes(b'binary-v3');binary.chmod(0o700)
        bundle=build/'app.gspb';bundle.write_bytes(b'bundle-v3')
        api=build/'bundle_gsp.api.json';api.write_text('{"scenes":[]}')
        (build/'echoy_backend-Release.json').write_text(json.dumps({'component_root':'/unchanged/component','executable':str(binary),'bundle':str(bundle)}))
        return build,output

    def test_snapshot_is_independent_and_does_not_rebuild_or_mutate_source(self):
        with tempfile.TemporaryDirectory() as temp:
            build,out=self.fixture(Path(temp));before={p.name:p.read_bytes() for p in build.iterdir()}
            manifest=snapshot_backend(build,out)
            self.assertEqual({p.name:p.read_bytes() for p in build.iterdir()},before)
            (build/'echoy_backend').write_bytes(b'later-build')
            self.assertEqual(Path(manifest['executable']).read_bytes(),b'binary-v3')
            self.assertTrue((out/'runtime-snapshot.json').exists())

    def test_copy_rejects_concurrent_build_changes(self):
        import shutil
        original=shutil.copy2
        with tempfile.TemporaryDirectory() as temp:
            build,out=self.fixture(Path(temp))
            def changed(source,target):
                result=original(source,target);Path(source).write_bytes(b'changed during copy');return result
            with patch('check_home.shutil.copy2',changed),self.assertRaisesRegex(AssertionError,'Build changed'):
                snapshot_backend(build,out)

    def test_header_raster_detects_old_second_line_and_battery_intrusion(self):
        image=Image.new('RGB',(480,480),(241,239,232));draw=ImageDraw.Draw(image)
        draw.rectangle((36,30,120,50),fill=(90,100,80));assert_header_fit(image)
        draw.rectangle((36,65,90,72),fill=(90,100,80))
        with self.assertRaisesRegex(AssertionError,'wraps'):
            assert_header_fit(image)
        draw.rectangle((34,63,383,81),fill=(241,239,232));draw.rectangle((350,30,380,45),fill=(90,100,80))
        with self.assertRaisesRegex(AssertionError,'battery'):
            assert_header_fit(image)

if __name__=='__main__':unittest.main()
