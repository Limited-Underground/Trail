"""Compile the real formatter/writer/adapter and parse its wire outputs; no devices."""
from pathlib import Path
import datetime as dt
import hashlib,json,os,subprocess,sys

ROOT=Path(__file__).resolve().parents[1]

def pin(path):
    raw=path.read_bytes();return dict(bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest())

def main():
    stamp=dt.datetime.now(dt.timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    out=ROOT/'.private/ot203-receipt/boundary-matrix'/stamp;out.mkdir(parents=True,exist_ok=False)
    compiler=Path('C:/msys64/ucrt64/bin/g++.exe')
    if not compiler.is_file():raise RuntimeError('Validated local g++ missing')
    env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1',TEMP=str(out),TMP=str(out),TMPDIR=str(out))
    env.pop('PYTHONOPTIMIZE',None);env['PATH']=str(compiler.parent)+os.pathsep+env.get('PATH','')
    sources=['tools/Test-SecurityReceiptBoundary.py','tools/security_policy_receipt_boundary.py',
        'tools/security_policy_capture.py','tools/security_policy_endpoint.py','tools/security_policy_diagnostics.py',
        'tools/security_policy_hardware.py','tests/host/security_policy_receipt_boundary_tests.cpp',
        'tests/host/security_policy_receipt_boundary_tests.py',
        *('firmware/components/security_evaluation/include/opentrail/'+x for x in
          ('evaluation_receipt_boundary.hpp','evaluation_control_synchronizing.hpp','evaluation_control.hpp')),
        *('firmware/components/diagnostics/include/opentrail/'+x for x in
          ('usb_fifo_console_transport.hpp','bounded_console_writer.hpp'))]
    result=dict(status='running',started_utc=dt.datetime.now(dt.timezone.utc).isoformat(),hardware=False,
        compiler=dict(path=str(compiler),**pin(compiler)),source_pins={x:pin(ROOT/x) for x in sources},steps=[])
    executable=out/'boundary.exe';fixtures=out/'wire-fixtures.tsv'
    commands=[('compile',[str(compiler),'-std=c++17','-O2','-Wall','-Wextra','-Werror',
        '-I'+str(ROOT/'firmware/components/security_evaluation/include'),
        '-I'+str(ROOT/'firmware/components/diagnostics/include'),
        str(ROOT/'tests/host/security_policy_receipt_boundary_tests.cpp'),'-o',str(executable)]),
        ('cpp',[str(executable),str(fixtures)]),
        ('python',[sys.executable,'-B',str(ROOT/'tests/host/security_policy_receipt_boundary_tests.py'),
            '--wire-fixtures',str(fixtures)])]
    try:
        print('Output: '+str(out),flush=True)
        for name,argv in commands:
            log=out/(name+'.log')
            with log.open('wb') as handle:
                process=subprocess.run(argv,cwd=ROOT,env=env,stdout=handle,stderr=subprocess.STDOUT,timeout=120,check=False)
            result['steps'].append(dict(name=name,command=argv,cwd=str(ROOT),exit_code=process.returncode,log=dict(path=str(log),**pin(log))))
            if process.returncode:raise RuntimeError(name+' failed; see '+str(log))
            print('PASS '+name,flush=True)
        if any(pin(ROOT/x)!=expected for x,expected in result['source_pins'].items()):raise RuntimeError('Source changed during tests')
        result['fixtures']=dict(path=str(fixtures),**pin(fixtures));result['status']='passed'
    except Exception as error:
        result['status']='failed';result['failure']=str(error);raise
    finally:
        result['finished_utc']=dt.datetime.now(dt.timezone.utc).isoformat()
        (out/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8',newline='\n')

if __name__=='__main__':main()
