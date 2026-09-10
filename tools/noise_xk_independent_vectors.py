"""Reproduce independently generated, deterministic test-only Noise XK vectors."""
from pathlib import Path
import hashlib,json,sys,warnings
ROOT=Path(__file__).resolve().parents[1]
FIXTURES=ROOT/'tests/benchmarks/crypto/independent_noise_xk'
WHEEL=FIXTURES/'noiseprotocol-0.3.1-py3-none-any.whl'
WHEEL_SHA='2e1a603a38439636cf0ffd8b3e8b12cee27d368a28b41be7dbe568b2abb23111'

def produce():
    if hashlib.sha256(WHEEL.read_bytes()).hexdigest()!=WHEEL_SHA:raise ValueError('producer_changed')
    if any(k=='noise' or k.startswith('noise.') for k in sys.modules):raise ValueError('producer_preloaded')
    sys.path.insert(0,str(WHEEL))
    from noise.connection import NoiseConnection,Keypair
    from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey
    from cryptography.hazmat.primitives.serialization import Encoding,PublicFormat
    import cryptography
    from cryptography.hazmat.backends.openssl.backend import backend
    if cryptography.__version__!='50.0.0' or backend.openssl_version_text()!='OpenSSL 4.0.1 9 Jun 2026':raise ValueError('producer_environment_changed')
    cases=[]
    for offset,prologue in [(0,b''),(91,b'OpenTrail independent XK test only')]:
        secrets=[bytes((offset+32*j+i)%256 for i in range(32)) for j in range(4)]
        public=[X25519PrivateKey.from_private_bytes(s).public_key().public_bytes(Encoding.Raw,PublicFormat.Raw) for s in secrets]
        a,b=[NoiseConnection.from_name(b'Noise_XK_25519_ChaChaPoly_SHA256') for _ in range(2)]
        a.set_as_initiator();b.set_as_responder()
        for peer,si,ei in [(a,0,1),(b,2,3)]:
            peer.set_prologue(prologue)
            peer.set_keypair_from_private_bytes(Keypair.STATIC,secrets[si])
            peer.set_keypair_from_private_bytes(Keypair.EPHEMERAL,secrets[ei])
        a.set_keypair_from_public_bytes(Keypair.REMOTE_STATIC,public[2])
        with warnings.catch_warnings():
            warnings.simplefilter('ignore',UserWarning);a.start_handshake();b.start_handshake()
        messages=[]
        for sender,receiver in [(a,b),(b,a),(a,b)]:
            message=bytes(sender.write_message(b''))
            if receiver.read_message(message)!=b'':raise ValueError('producer_payload_mismatch')
            messages.append(message.hex())
        if a.get_handshake_hash()!=b.get_handshake_hash():raise ValueError('producer_hash_mismatch')
        ak=a.noise_protocol.cipher_state_encrypt.k;bk=b.noise_protocol.cipher_state_encrypt.k
        if ak!=b.noise_protocol.cipher_state_decrypt.k or bk!=a.noise_protocol.cipher_state_decrypt.k:raise ValueError('producer_split_mismatch')
        cases.append({'test_only':True,'prologue':prologue.hex(),'private_keys':[s.hex() for s in secrets],
                      'public_keys':[s.hex() for s in public],'messages':messages,
                      'handshake_hash':a.get_handshake_hash().hex(),'initiator_tx':ak.hex(),'responder_tx':bk.hex()})
    return {'schema':'independent-noise-xk-vectors-1','protocol':'Noise_XK_25519_ChaChaPoly_SHA256','producer':'noiseprotocol 0.3.1','cases':cases}

if __name__=='__main__':
    actual=produce();expected=json.loads((FIXTURES/'vectors.json').read_text())
    if actual!=expected:raise SystemExit('independent_vectors_changed')
    print('Independent producer reproduces both complete XK vectors')
