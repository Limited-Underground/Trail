"""Host-only adversarial worker tests; synthetic files and a harmless native sentinel, no ports."""
import copy,hashlib,importlib.util,json,sys,tempfile,unittest,os,shutil,subprocess
from pathlib import Path
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[2]
SPEC=importlib.util.spec_from_file_location("ot196_stage_operator_tests_worker",ROOT/"tools/security_policy_stage_operator.py")
worker=importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(worker)

def encoded(value):return json.dumps(value,sort_keys=True,separators=(",",":")).encode()
class Tests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name).resolve()
    def write(self,name,raw):
        p=self.root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(raw);return p,hashlib.sha256(raw).hexdigest()
    def manifest_fixture(self):
        worktree=self.root/"worktree";capsule=worktree/".private/capsule";capsule.mkdir(parents=True)
        files={}
        names=["policy/Invoke-SecurityPolicyOperator.ps1","esptool.cfg","packages/esptool/__init__.py","packages/serial/__init__.py","python.exe","python314.dll","python314._pth","policy/security_policy_stage_operator.py","policy/Invoke-SecurityPolicyStageOperator.ps1"]+["policy/security_policy_"+x+".py" for x in worker.POLICY]
        for name in names:
            raw=b"[esptool]\n" if name=="esptool.cfg" else worker.PTH if name=="python314._pth" else (ROOT/"tools/Invoke-SecurityPolicyStageOperator.ps1").read_bytes() if name=="policy/Invoke-SecurityPolicyStageOperator.ps1" else b"synthetic pinned fixture"
            p=capsule/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(raw)
            files[name]={"bytes":len(raw),"sha256":worker.digest(raw)}
        host=self.root/"pwsh.exe";host.write_bytes(b"synthetic host")
        m={"schema":"OT196-STAGE-RUNTIME-1","root":str(capsule),"worktree":str(worktree),"files":files,"versions":{"python":"3.14.6","esptool":"5.3.1","pyserial":"3.5"},"powershell":{"path":str(host),"bytes":host.stat().st_size,"sha256":worker.digest(host.read_bytes())}}
        path,h=self.write("manifest.json",encoded(m))
        return m,path,h
    def test_valid_manifest(self):
        m,p,h=self.manifest_fixture();self.assertEqual(m,worker.verify_manifest(p,h))
    def test_manifest_missing_extra_changed_files(self):
        m,p,h=self.manifest_fixture();root=Path(m["root"])
        file=root/"python.exe";saved=file.read_bytes();file.write_bytes(b"changed")
        with self.assertRaises(Exception):worker.verify_manifest(p,h)
        file.write_bytes(saved);extra=root/"extra.py";extra.write_bytes(b"extra")
        with self.assertRaises(Exception):worker.verify_manifest(p,h)
        extra.unlink();file.unlink()
        with self.assertRaises(Exception):worker.verify_manifest(p,h)
    def test_manifest_path_and_startup_attacks(self):
        m,p,h=self.manifest_fixture();root=Path(m["root"])
        for name in ["../escape.py","Lib/../escape.py","Lib/a.pyc","packages/hook.pth","PYTHON.EXE"]:
            changed=copy.deepcopy(m);changed["files"][name]=m["files"]["python.exe"]
            p,h=self.write("manifest.json",encoded(changed))
            with self.assertRaises(Exception):worker.verify_manifest(p,h)
        (root/"python314._pth").write_bytes(worker.PTH+b"import site\n")
        changed=copy.deepcopy(m);raw=(root/"python314._pth").read_bytes();changed["files"]["python314._pth"]={"bytes":len(raw),"sha256":worker.digest(raw)}
        p,h=self.write("manifest.json",encoded(changed))
        with self.assertRaises(Exception):worker.verify_manifest(p,h)
    def test_probe_never_constructs_devices_and_uses_version_child(self):
        from types import SimpleNamespace
        m,p,h=self.manifest_fixture();ctx={"manifest":m,"path":p,"sha":h};imports=[]
        def imported(name):
            imports.append(name)
            if name=="esptool":return SimpleNamespace(__version__="5.3.1")
            if name=="serial":return SimpleNamespace(__version__="3.5")
            return SimpleNamespace()
        result=SimpleNamespace(returncode=0,stdout=encoded({"esptool":"5.3.1","pyserial":"3.5"}))
        with patch.object(worker.importlib,"import_module",side_effect=imported),patch.object(worker,"audit_loaded_modules",return_value=True),patch.object(worker.subprocess,"run",return_value=result) as run:
            value=worker.probe(ctx)
        self.assertIs(value["hardware_access"],False)
        args=run.call_args.args[0];self.assertEqual("Version",args[args.index("-Mode")+1])
        self.assertEqual(["esptool","serial"]+["security_policy_"+x for x in worker.POLICY],imports)
    def test_operator_request_bound_files_and_runtime(self):
        m,p,h=self.manifest_fixture();ctx={"manifest":m,"path":p,"sha":h};private=Path(m["worktree"])/".private"
        package=private/"package.json";package.write_bytes(b"{}")
        grant=private/"grant.json";grant.write_bytes(b"{}")
        req={"schema":"OT196-STAGE-OPERATOR-1","runtime_sha256":h,"operation":"recover","package_path":str(package),"package_sha256":worker.digest(b"{}"),"grant_path":str(grant),"grant_sha256":worker.digest(b"{}"),"origin_attempt":"a"*32}
        path=private/"request.json";path.write_bytes(encoded(req));sha=worker.digest(path.read_bytes())
        self.assertEqual(req,worker.operator_request(ctx,path,sha))
        for key,value in [("runtime_sha256","0"*64),("origin_attempt",None),("extra",True)]:
            altered={**req,key:value};path.write_bytes(encoded(altered))
            with self.assertRaises(Exception):worker.operator_request(ctx,path,worker.digest(path.read_bytes()))
        path.write_bytes(encoded(req));package.write_bytes(b'{"changed":true}')
        with self.assertRaises(Exception):worker.operator_request(ctx,path,sha)
    @unittest.skipUnless(sys.platform=="win32","PowerShell Windows prelaunch boundary")
    def test_powershell_rejects_poison_before_native_startup(self):
        compiler=shutil.which("g++");powershell=shutil.which("pwsh")
        self.assertIsNotNone(compiler,"native g++ required");self.assertIsNotNone(powershell,"pwsh required")
        # Bind the actual host, not a PATH alias; preserve strict production checks.
        discovered=subprocess.run([powershell,"-NoProfile","-NonInteractive","-Command", "[Console]::Write([IO.Path]::Combine($PSHOME,'pwsh.exe'))"],capture_output=True,check=True,timeout=30)
        powershell=str(Path(discovered.stdout.decode("utf-8-sig").strip()).resolve())
        self.assertTrue(Path(powershell).is_file())
        m,p,h=self.manifest_fixture();root=Path(m["root"]);marker=self.root/"executed.txt"
        source=self.root/"sentinel.cpp"
        source.write_text('#include <cstdio>\n#include <cstdlib>\n#include <cstring>\nint main(){const char*cfg=std::getenv("ESPTOOL_CFGFILE");const char*expected=std::getenv("OT189_EXPECTED_CFG");if(!cfg||!expected||std::strcmp(cfg,expected)!=0||std::getenv("ESPTOOL_OPEN_PORT_ATTEMPTS"))return 6;const char*p=std::getenv("OT189_SENTINEL_PATH");if(!p)return 4;FILE*f=std::fopen(p,"wb");if(!f)return 5;std::fputs("executed",f);std::fclose(f);return 0;}')
        subprocess.run([compiler,"-static",str(source),"-o",str(root/"python.exe")],check=True,capture_output=True,timeout=60)
        raw=(root/"python.exe").read_bytes();m["files"]["python.exe"]={"bytes":len(raw),"sha256":worker.digest(raw)}
        host=Path(powershell);m["powershell"]={"path":str(host),"bytes":host.stat().st_size,"sha256":worker.digest(host.read_bytes())}
        env=dict(os.environ);env["OT189_SENTINEL_PATH"]=str(marker);env["OT189_EXPECTED_CFG"]=str(root/"esptool.cfg");env["ESPTOOL_CFGFILE"]=str(self.root/"poison.cfg");env["ESPTOOL_OPEN_PORT_ATTEMPTS"]="999"
        script=ROOT/"tools/Invoke-SecurityPolicyStageOperator.ps1"
        def invoke(manifest):
            p.write_bytes(encoded(manifest))
            return subprocess.run([powershell,"-NoProfile","-NonInteractive","-File",str(script),"-Manifest",str(p),"-ManifestSha256",worker.digest(p.read_bytes()),"-Mode","Probe"],capture_output=True,env=env,timeout=30)
        control=invoke(m)
        diagnostics={}
        if control.returncode!=0:
            # Fixture-only booleans/counts: no paths or underlying exception text.
            diagnostics={"root_canonical":str(root)==str(root.resolve()),"marker_exists":marker.exists(),"native_exists":(root/"python.exe").is_file(),"root_reparse_ancestors":sum(part.is_symlink() or part.is_junction() for part in (root,*root.parents)),"host_bytes_match":host.stat().st_size==m["powershell"]["bytes"],"host_hash_matches":worker.digest(host.read_bytes())==m["powershell"]["sha256"],"wrapper_hash_matches":worker.digest(script.read_bytes())==m["files"]["policy/Invoke-SecurityPolicyStageOperator.ps1"]["sha256"],"file_hash_mismatches":sum(not(root/name).is_file() or worker.digest((root/name).read_bytes())!=pin["sha256"] for name,pin in m["files"].items())}
            env_diagnostic=dict(env);env_diagnostic["OT189_DIAG_ROOT"]=str(root);env_diagnostic["OT189_DIAG_HOST"]=str(host)
            try:
                result=subprocess.run([powershell,"-NoProfile","-NonInteractive","-Command", "@{host_path_matches=((Join-Path $PSHOME 'pwsh.exe') -ieq $env:OT189_DIAG_HOST);root_exact_normalization=([IO.Path]::GetFullPath($env:OT189_DIAG_ROOT) -ceq $env:OT189_DIAG_ROOT)} | ConvertTo-Json -Compress"],capture_output=True,env=env_diagnostic,timeout=30,check=True)
                diagnostics.update(json.loads(result.stdout.decode("utf-8-sig")))
            except Exception:diagnostics["diagnostic_probe_failed"]=True
        self.assertEqual(0,control.returncode,"valid control refused; fixture diagnostics="+json.dumps(diagnostics,sort_keys=True));self.assertTrue(marker.exists());marker.unlink()
        (root/"python314._pth").write_bytes(worker.PTH+b"import site\n")
        poison=copy.deepcopy(m);raw=(root/"python314._pth").read_bytes();poison["files"]["python314._pth"]={"bytes":len(raw),"sha256":worker.digest(raw)}
        rejected=invoke(poison);self.assertNotEqual(0,rejected.returncode);self.assertFalse(marker.exists())
        (root/"python314._pth").write_bytes(worker.PTH)
        (root/"startup.pth").write_bytes(b"import malicious\n")
        self.assertNotEqual(0,invoke(m).returncode);self.assertFalse(marker.exists());(root/"startup.pth").unlink()
        (root/"python314.dll").unlink()
        self.assertNotEqual(0,invoke(m).returncode);self.assertFalse(marker.exists())

    def test_rom_argv_bounds_and_recovery_payload(self):
        m,p,h=self.manifest_fixture();ctx={"manifest":m,"path":p,"sha":h};private=Path(m["worktree"])/".private"
        row={"private_route":"fixture-route","nvs":b"N"*12288,"application":b"A"*589824};data={"roles":[row],"candidate":b"candidate"}
        prefix=["--chip","esp32s3","--port","fixture-route","--baud","115200","--before","default-reset","--after","no-reset","--no-stub"]
        output=private/"ot188-read-fixture/region.bin";output.parent.mkdir()
        read=prefix+["read-flash","0xd000","12288",str(output)]
        self.assertEqual(read,worker.validate_rom_argv(ctx,{"operation":"recover","_phase":"preflight_intent"},data,read))
        for changed in [prefix+["erase-flash"],prefix+["read-mac","extra"],prefix+["read-flash","0x0","16",str(output)],prefix+["read-flash","0xd000","12288",str(self.root/"outside.bin")]]:
            with self.assertRaises(Exception):worker.validate_rom_argv(ctx,{"operation":"execute","_phase":"preflight_intent"},data,changed)
        file=private/"ot188-write-fixture/region.bin";file.parent.mkdir();file.write_bytes(row["application"])
        write=prefix+["write-flash","--flash-size","16MB","0x10000",str(file)]
        recovery={"roles":[row]}
        self.assertEqual(write,worker.validate_rom_argv(ctx,{"operation":"recover","_phase":"restore_intent"},recovery,write))
        file.write_bytes(data["candidate"].ljust(589824,b"\xff"))
        self.assertEqual(write,worker.validate_rom_argv(ctx,{"operation":"execute","_phase":"candidate_write_intent"},data,write))
        with self.assertRaises(Exception):worker.validate_rom_argv(ctx,{"operation":"recover","_phase":"restore_intent"},recovery,write)
        for phase in ["serial_open_intent","run_intent","serial_closed","original_booted",None]:
            with self.assertRaises(Exception):worker.validate_rom_argv(ctx,{"operation":"execute","_phase":phase},data,write)
        run=prefix.copy();run[9]="hard-reset";run+=["run"]
        self.assertEqual(run,worker.validate_rom_argv(ctx,{"operation":"execute","_phase":"candidate_boot_intent"},data,run))
        with self.assertRaises(Exception):worker.validate_rom_argv(ctx,{"operation":"execute","_phase":"restore_intent"},data,run)
        file.write_bytes(b"X"*589824)
        with self.assertRaises(Exception):worker.validate_rom_argv(ctx,{"operation":"execute","_phase":"candidate_write_intent"},data,write)

    def test_recover_dispatch_without_candidate_and_source_mismatch(self):
        from types import SimpleNamespace
        from unittest.mock import Mock
        m,p,h=self.manifest_fixture();ctx={"manifest":m,"path":p,"sha":h};private=Path(m["worktree"])/".private"
        package={"sources":{"tools/security_policy_"+x+".py":m["files"]["policy/security_policy_"+x+".py"] for x in worker.POLICY},"roles":[{"role":"A","private_route":"route","private_identity":"id"}]}
        package['runtime_sha256']=h
        for launcher in ('Invoke-SecurityPolicyStageOperator.ps1','Invoke-SecurityPolicyOperator.ps1'):
            m['files']['policy/'+launcher]=m['files']['policy/Invoke-SecurityPolicyStageOperator.ps1']
            package['sources']['tools/'+launcher]=m['files']['policy/'+launcher]
        package_path=private/"package.json";package_path.write_bytes(encoded(package));grant_path=private/"grant.json";grant_path.write_bytes(b"{}")
        req={"schema":"OT196-STAGE-OPERATOR-1","runtime_sha256":h,"operation":"recover","package_path":str(package_path),"package_sha256":worker.digest(package_path.read_bytes()),"grant_path":str(grant_path),"grant_sha256":worker.digest(b"{}"),"origin_attempt":"a"*32}
        request=private/"request.json";request.write_bytes(encoded(req))
        execution=SimpleNamespace(FileAuthority=Mock(return_value="authority"),recover=Mock(return_value={"status":"recovered"}),execute=Mock(side_effect=AssertionError("execute forbidden")))
        hardware=SimpleNamespace(RoleBinding=lambda *args:args,Backend=Mock(return_value="backend"))
        modules={"security_policy_stage_bundle":SimpleNamespace(),"security_policy_stage_execution":execution,"security_policy_hardware":hardware}
        with patch.object(worker.importlib,"import_module",side_effect=lambda n:modules[n]),patch.object(worker,"make_transport",return_value="transport"),patch.object(worker,"make_backend",side_effect=hardware.Backend),patch.object(worker,"with_diagnostics",side_effect=lambda c,b,r:r),patch.object(worker,"audit_loaded_modules",return_value=True):
            self.assertEqual({"status":"recovered"},worker.run_operator(ctx,request,worker.digest(request.read_bytes())))
            self.assertEqual("a"*32,execution.recover.call_args.kwargs["origin_attempt"])
            package["sources"]["tools/security_policy_capture.py"]={"bytes":1,"sha256":"0"*64};package_path.write_bytes(encoded(package));req["package_sha256"]=worker.digest(package_path.read_bytes());request.write_bytes(encoded(req))
            with self.assertRaises(Exception):worker.run_operator(ctx,request,worker.digest(request.read_bytes()))
            self.assertEqual(1,hardware.Backend.call_count)

    def test_origins_admitted_before_imports(self):
        from types import SimpleNamespace
        m,p,h=self.manifest_fixture();ctx={"manifest":m,"path":p,"sha":h};root=Path(m["root"])
        origins={"esptool":root/"packages/esptool/__init__.py","serial":root/"packages/serial/__init__.py"}
        origins.update({"security_policy_"+suffix:root/("policy/security_policy_"+suffix+".py") for suffix in worker.POLICY})
        specs={name:SimpleNamespace(origin=str(path)) for name,path in origins.items()}
        cached={name:SimpleNamespace(__file__=str(path)) for name,path in origins.items()}
        with patch.dict(worker.sys.modules,cached),patch.object(worker.PathFinder,"find_spec",side_effect=lambda name,paths:specs[name]),patch.object(worker.importlib,"import_module",side_effect=AssertionError("imports must not execute")):
            self.assertTrue(worker.admit_origins(ctx))
            for name in origins:
                original=specs[name]
                for replacement in [None,SimpleNamespace(origin=None),SimpleNamespace(origin=str(self.root/"shadow.py"))]:
                    specs[name]=replacement
                    with self.assertRaises(worker.OperatorError):worker.admit_origins(ctx)
                specs[name]=original
                cached[name].__file__=str(self.root/"cached-shadow.py")
                with self.assertRaises(worker.OperatorError):worker.admit_origins(ctx)
                cached[name].__file__=str(origins[name])

    def test_digest(self):
        self.assertEqual(hashlib.sha256(b"abc").hexdigest(),worker.digest(b"abc"))
    def test_request_exact_buffer(self):
        value={"schema":"test","x":1};p,h=self.write("request.json",encoded(value))
        self.assertEqual(value,worker.load_request(p,h))
        p.write_bytes(encoded({"schema":"test","x":2}))
        with self.assertRaises(Exception):worker.load_request(p,h)
    def test_request_duplicate_and_invalid_json(self):
        for raw in [b'{"a":1,"a":2}',b'{"a":NaN}',b'not json']:
            p,h=self.write("bad.json",raw)
            with self.assertRaises(Exception):worker.load_request(p,h)
    def test_child_command_uses_verified_powershell(self):
        capsule=self.root/"capsule";manifest={"root":str(capsule),"powershell":{"path":str(self.root/"pwsh.exe")}}
        path=self.root/"manifest.json";sha="a"*64;request=self.root/"request.json"
        args=worker.child_args(manifest,path,sha,"StageOperator",request,"b"*64)
        self.assertEqual(str(self.root/"pwsh.exe"),str(args[0]))
        self.assertIn("-NoProfile",args);self.assertIn("-NonInteractive",args)
        self.assertEqual(str(capsule/"policy/Invoke-SecurityPolicyStageOperator.ps1"),str(args[args.index("-File")+1]))
        self.assertEqual(str(path),str(args[args.index("-Manifest")+1]))
        self.assertEqual(sha,args[args.index("-ManifestSha256")+1])
        self.assertEqual("StageOperator",args[args.index("-Mode")+1])
        self.assertEqual(str(request),str(args[args.index("-Request")+1]))
        self.assertEqual("b"*64,args[args.index("-RequestSha256")+1])
if __name__=="__main__":unittest.main()
