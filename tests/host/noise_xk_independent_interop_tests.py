"""Actual-source independent XK proof and fail-closed admission checks."""
from pathlib import Path
import importlib.util, json, subprocess, sys, tempfile, unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('independent_interop',ROOT/'tools/noise_xk_independent_interop.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class IndependentProof(unittest.TestCase):
 @classmethod
 def setUpClass(cls):(ROOT/"build").mkdir(parents=True,exist_ok=True)
 def test_01_independent_producer(self):
  subprocess.run([sys.executable,str(ROOT/'tools/noise_xk_independent_vectors.py')],check=True,timeout=30)
 def test_02_baseline_real_primitives(self):
  with tempfile.TemporaryDirectory(prefix='independent-baseline-',dir=ROOT/'build') as d:
   r=m.run(Path(d));self.assertEqual(r['groups'],26)
   self.assertTrue(any('nonnull' in w for w in r['warnings']))
 def test_03_nonnull_successor_real_primitives(self):
  with tempfile.TemporaryDirectory(prefix='independent-successor-',dir=ROOT/'build') as d:
   r=m.run(Path(d),True);self.assertEqual(r['groups'],26)
   self.assertFalse(any('nonnull' in w for w in r['warnings']))
 def test_04_inventory_tamper(self):
  with patch.object(m,'CHECKSUM_SHA','0'*64),self.assertRaisesRegex(ValueError,'source_inventory_changed'):m.verify_inputs()
 def test_05_vectors_tamper(self):
  with patch.object(m,'VECTOR_SHA','0'*64),self.assertRaisesRegex(ValueError,'vectors_changed'):m.verify_inputs()
 def test_06_successor_tamper(self):
  with patch.object(m,'SUCCESSOR_SHA','0'*64),self.assertRaisesRegex(ValueError,'successor_changed'):m.verify_inputs()
 def test_07_adapter_tamper(self):
  with patch.dict(m.ADAPTER_SHA,{'noise_xk_libsodium.c':'0'*64}),self.assertRaisesRegex(ValueError,'adapter_changed'):m.verify_inputs()
 def test_08_shadow_header_rejected_before_compile(self):
  with tempfile.TemporaryDirectory(dir=ROOT/'build') as d:
   (Path(d)/'sodium.h').write_text('untrusted')
   with patch.object(m.subprocess,'run') as run,self.assertRaisesRegex(ValueError,'build_directory_must_be_empty'):m.run(Path(d))
   run.assert_not_called()
 def test_09_exact_single_successor_change(self):
  old=(m.ADAPTER/'noise_xk_libsodium.c').read_bytes();new=(m.SUCCESSOR/'noise_xk_libsodium.c').read_bytes()
  self.assertEqual(old.count(b'NULL, 0U) == 0 &&'),1)
  self.assertEqual(new,old.replace(b'NULL, 0U) == 0 &&',b'(const unsigned char *) "", 0U) == 0 &&'))
 def test_10_big_endian_rejected_before_compile(self):
  with patch.object(m.sys,'byteorder','big'),patch.object(m.subprocess,'run') as run,self.assertRaisesRegex(ValueError,'little_endian_host_required'):m.run(ROOT/'build/unused')
  run.assert_not_called()
if __name__=='__main__':unittest.main()
