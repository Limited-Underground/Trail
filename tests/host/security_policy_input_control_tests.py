from pathlib import Path
import shutil,subprocess,tempfile,json,binascii
ROOT=Path(__file__).resolve().parents[2]
def run():
    compiler=shutil.which("g++")
    if compiler is None: raise RuntimeError("native g++ is required on PATH")
    with tempfile.TemporaryDirectory(prefix="security-control-") as d:
        exe=Path(d)/"control.exe"
        subprocess.run([compiler,"-std=c++17","-O2","-Wall","-Wextra","-Werror","-I",str(ROOT/"firmware/components/security_evaluation/include"),"-I",str(ROOT),str(ROOT/"tests/host/security_policy_input_control_tests.cpp"),"-o",str(exe)],check=True,timeout=90)
        result=subprocess.run([str(exe)],check=True,capture_output=True,text=True,timeout=20)
        lines=result.stdout.splitlines()
        if lines[0]!="PASS 24 input diagnostic host groups":raise RuntimeError("unexpected test output")
        contract=json.loads((ROOT/"firmware/components/security_diagnostics/input_record_v1.json").read_bytes())
        expected={(int(s),e) for s,errors in contract["allowed_errors"].items() for e in errors}
        actual=set()
        for line in lines[1:]:
            marker,stage,error,value=line.split();pair=(int(stage),int(error));raw=int(value).to_bytes(8,"little")
            if marker!="RECORD" or pair in actual or list(raw[:4])!=contract["prefix"] or tuple(raw[4:6])!=pair or int.from_bytes(raw[6:],"little")!=binascii.crc_hqx(raw[:6],65535):raise RuntimeError("codec contract mismatch")
            actual.add(pair)
        if actual!=expected:raise RuntimeError("codec allowlist mismatch")
        print(lines[0]+"; all codec pairs match JSON/independent CRC")
if __name__=="__main__":run()
