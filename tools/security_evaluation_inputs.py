"""Verify frozen inputs used by the additive build-only evaluation target."""
from pathlib import Path
import hashlib,json
ROOT=Path(__file__).resolve().parents[1]
COMP=ROOT/'tests/benchmarks/crypto/esp_idf/espressif_libsodium_1_0_22/managed_components/espressif__libsodium'
def verify():
    raw=(COMP/'CHECKSUMS.json').read_bytes()
    if hashlib.sha256(raw).hexdigest()!='5e3983c5496a3cffba3d013c70991b18cda6c345655fff883fb0e14dfa09e582':raise ValueError('source inventory changed')
    entries=json.loads(raw)['files']
    for e in entries:
        b=(COMP/e['path']).read_bytes()
        if len(b)!=e['size'] or hashlib.sha256(b).hexdigest()!=e['hash']:raise ValueError('source bytes changed')
    pins={'tests/benchmarks/crypto/independent_noise_xk/nonnull_adapter/noise_xk_libsodium.c':'4f3852e9822eebfed1df8946f6d0ca8ae756a7790ee15b48f1748aa662ba66c0','tests/benchmarks/crypto/independent_noise_xk/nonnull_adapter/noise_xk_libsodium.h':'b7c649434cdffe648e467bb117849ae0296a73fa041d614d3d4ba32578e40c45'}
    pins['firmware/targets/heltec_v4_security_eval/main/noise_adapter/noise_xk_libsodium.c']='b0be8109d017a851cea3952c4713157847c3bc64fe0eba1367c2b3c27cbcdc8b'
    pins['firmware/targets/heltec_v4_security_eval/main/noise_adapter/noise_xk_libsodium.h']='b7c649434cdffe648e467bb117849ae0296a73fa041d614d3d4ba32578e40c45'
    for p,h in pins.items():
        if hashlib.sha256((ROOT/p).read_bytes()).hexdigest()!=h:raise ValueError('corrected adapter changed')
    return len(entries)
if __name__=='__main__':print('Verified evaluation inputs:',verify(),'library files and corrected adapter')
