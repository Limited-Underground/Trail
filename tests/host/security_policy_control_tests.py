from pathlib import Path
import shutil,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
def run():
    compiler=shutil.which("g++")
    if compiler is None: raise RuntimeError("native g++ is required on PATH")
    with tempfile.TemporaryDirectory(prefix="security-control-") as d:
        exe=Path(d)/"control.exe"
        subprocess.run([compiler,"-std=c++17","-O2","-Wall","-Wextra","-Werror","-I",str(ROOT/"firmware/components/security_evaluation/include"),str(ROOT/"tests/host/security_policy_control_tests.cpp"),"-o",str(exe)],check=True,timeout=90)
        result=subprocess.run([str(exe)],check=True,capture_output=True,text=True,timeout=20)
        if result.stdout.strip()!="PASS 12 security control host groups":raise RuntimeError("unexpected test output")
        print(result.stdout.strip())
if __name__=="__main__":run()
