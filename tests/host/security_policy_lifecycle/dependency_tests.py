"""Negative admission and selected-compiler checks; no network or retained cache."""
from pathlib import Path
import tempfile
import unittest
import zipfile
from types import SimpleNamespace
from unittest.mock import patch
from security_policy_lifecycle import dependencies as dependency

class Tests(unittest.TestCase):
    def archive(self):
        rows=[zipfile.ZipInfo('file-'+str(i)) for i in range(731)]
        return SimpleNamespace(infolist=lambda:rows),rows
    def test_archive_shape(self):
        archive,_=self.archive();self.assertEqual(len(dependency.members(archive)),731)
    def test_missing_extra(self):
        for count in (730,732):
            archive,rows=self.archive()
            rows[:]=rows[:count] if count==730 else rows+[zipfile.ZipInfo('extra')]
            with self.assertRaises(RuntimeError):dependency.members(archive)
    def test_unsafe_paths(self):
        for name in ('../escape','/absolute','C:drive','a\\b','a/./b','a//b'):
            archive,rows=self.archive();rows[0].filename=name
            with self.subTest(name=name),self.assertRaises(RuntimeError):dependency.members(archive)
    def test_duplicate_casefold(self):
        archive,rows=self.archive();rows[1].filename='FILE-0'
        with self.assertRaises(RuntimeError):dependency.members(archive)
    def test_nonregular_types(self):
        for mode in (0o120777,0o040755,0o020600):
            archive,rows=self.archive();rows[0].external_attr=mode<<16
            with self.assertRaises(RuntimeError):dependency.members(archive)
    def test_encrypted_oversize(self):
        for kind in ('encrypted','oversize'):
            archive,rows=self.archive()
            if kind=='encrypted':rows[0].flag_bits=1
            else:rows[0].file_size=4*1024*1024+1
            with self.assertRaises(RuntimeError):dependency.members(archive)
    def test_archive_hash_before_extraction(self):
        with tempfile.TemporaryDirectory() as d:
            output=Path(d).resolve()/'fresh'
            with self.assertRaises(RuntimeError):dependency.acquire(output,fetch=lambda url,limit:b'changed archive')
            self.assertFalse(output.exists())
    def test_existing_destination_preserved(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d).resolve();sentinel=root/'sentinel';sentinel.write_bytes(b'unchanged')
            with patch.object(dependency,'download') as fetch:
                with self.assertRaises(RuntimeError):dependency.acquire(root,fetch=fetch)
                fetch.assert_not_called()
            self.assertEqual(sentinel.read_bytes(),b'unchanged')
    def test_total_expansion_cap(self):
        archive,rows=self.archive()
        for row in rows[:9]:row.file_size=4*1024*1024
        with self.assertRaises(RuntimeError):dependency.members(archive)
    def test_big_endian_rejected_before_compile(self):
        with patch.object(dependency.sys,'byteorder','big'),patch.object(dependency.subprocess,'run') as run:
            with self.assertRaises(RuntimeError):dependency.native_probe(None,None,'unused')
            run.assert_not_called()
    def test_download_total_deadline(self):
        class Response:
            def __enter__(self):return self
            def __exit__(self,*args):pass
            def geturl(self):return dependency.ARCHIVE_URL
            def read1(self,size):return b'x'
        with patch.object(dependency.urllib.request,'urlopen',return_value=Response()),patch.object(dependency.time,'monotonic',side_effect=[0,1,91]):
            with self.assertRaises(RuntimeError):dependency.download(dependency.ARCHIVE_URL,1024)
    def test_selected_compiler_every_build_command(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d).resolve();source=root/'source';header=source/'src/libsodium/include/sodium/version.h.in'
            header.parent.mkdir(parents=True);header.write_text('@VERSION@',encoding='utf-8')
            module=SimpleNamespace(verify_inputs=lambda:[],SOURCE=source,SUCCESSOR=root/'adapter',SOURCES=['one.c'],header=lambda values:'')
            selected=str(root/'alternate-toolchain'/'gcc.exe')
            calls=[]
            def invoke(args,**kwargs):
                calls.append(args);return SimpleNamespace(stdout='PASS: 26 verified cases')
            with patch.object(dependency.subprocess,'run',side_effect=invoke):
                dependency.native_probe(module,root/'build',selected)
            self.assertEqual(len(calls),5)
            self.assertTrue(all(args[0]==selected for args in calls[:-1]))
            self.assertTrue(calls[-1][0].endswith('independent-probe.exe'))

def run():
    result=unittest.TextTestRunner(verbosity=0).run(unittest.defaultTestLoader.loadTestsFromTestCase(Tests))
    if not result.wasSuccessful():raise RuntimeError('dependency admission tests failed')

if __name__=='__main__':run()
