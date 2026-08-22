#!/usr/bin/env python3
"""Adversarial host tests for OT-118 Monocypher partial API/config admission."""
from __future__ import annotations

import copy
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
CRYPTO = ROOT / "tests/benchmarks/crypto"
sys.path.insert(0, str(TOOLS))
import crypto_api_config_acceptance_contract as acceptance  # noqa: E402
import crypto_monocypher_api_config_admission as admission_validator  # noqa: E402
import crypto_monocypher_api_config_evidence as evidence_validator  # noqa: E402

BUNDLE = CRYPTO / "OT-118-OT005-MONOCYPHER-API-CONFIG-OPERATION-EVIDENCE-V0.json"
API = CRYPTO / "OT-118-OT005-MONOCYPHER-API-CONFIG-EVIDENCE-V2.json"
ADMISSION = CRYPTO / "OT-118-OT005-MONOCYPHER-API-CONFIG-ADMISSION-DELTA-V0.json"
CONTRACT = CRYPTO / "OT-108-OT005-PER-CANDIDATE-API-CONFIG-ACCEPTANCE-CONTRACT-V1.json"
EXPECTED = {BUNDLE:(evidence_validator.EXPECTED_BUNDLE_RAW_SHA256,evidence_validator.EXPECTED_BUNDLE_SHA256),API:(evidence_validator.EXPECTED_API_EVIDENCE_RAW_SHA256,evidence_validator.EXPECTED_API_EVIDENCE_SHA256),ADMISSION:(admission_validator.EXPECTED_ADMISSION_RAW_SHA256,admission_validator.EXPECTED_ADMISSION_SHA256)}

HISTORICAL_RAW = {
    CRYPTO / "OT-108-OT005-PER-CANDIDATE-API-CONFIG-ACCEPTANCE-CONTRACT-V1.json": "575e8747cdba643f2abb6f3252a62adbf3f12a9faaa46ca1d4bf4ce8bd9d23f3",
    CRYPTO / "OT-109-OT005-MBEDTLS-PSA-API-CONFIG-OPERATION-EVIDENCE-V0.json": "ea85f548deee36ca34241747cdf567036febfb9eecd88d9e134d3383edf2379e",
    CRYPTO / "OT-109-OT005-MBEDTLS-PSA-API-CONFIG-EVIDENCE-V2.json": "67532e10704d02489b72a72ef55607743c00a5bd8504276750931b5d986f6155",
    CRYPTO / "OT-109-OT005-MBEDTLS-PSA-API-CONFIG-ADMISSION-DELTA-V0.json": "0311b8b264d264d4a005ac9be8531c06175521362ed54ac9f40bdef1fd7a5df0",
    CRYPTO / "OT-117-OT005-LIBSODIUM-API-CONFIG-OPERATION-EVIDENCE-V0.json": "b1daf950473ea9e86ddff16d8f28efbc0972582902d5b262686cf73d0907bf58",
    CRYPTO / "OT-117-OT005-LIBSODIUM-API-CONFIG-EVIDENCE-V2.json": "34888d71da2c9042856ea48c7b1225f21c1345582c144239cab0096ff03e69b5",
    CRYPTO / "OT-117-OT005-LIBSODIUM-API-CONFIG-ADMISSION-DELTA-V0.json": "527c5e713a6d96d38b7dfbfb9d3e0ceb891f4652f8428e2ca65e0b8ea8f316d2",
}


def artifacts(): return evidence_validator.load(BUNDLE), evidence_validator.load(API), admission_validator.load(ADMISSION), acceptance.load(CONTRACT)


def reject(action, text):
    try: action()
    except (evidence_validator.ValidationError, admission_validator.ValidationError, acceptance.ValidationError) as exc:
        assert text in str(exc), (text, str(exc)); return
    raise AssertionError("expected rejection")


def test_canonical_chain():
    bundle, api, admission, contract = artifacts()
    assert evidence_validator.validate_operation_bundle(bundle)["eligible_operation_count"] == 5
    assert evidence_validator.validate_api_evidence(api,bundle,contract)["selection_eligible"] is False
    result=admission_validator.validate(admission,bundle,api,contract)
    assert result["accepted_api_config_count"] == 3 and result["phase_zero_complete"] is False
    for path,(raw,canonical) in EXPECTED.items():
        value=admission_validator.load(path) if path==ADMISSION else evidence_validator.load(path)
        assert hashlib.sha256(path.read_bytes()).hexdigest()==raw
        assert evidence_validator.canonical_sha256(value)==canonical


def test_exact_five_of_eight_partial():
    bundle,api,_,contract=artifacts(); eligible=[r["operation_id"] for r in api["operation_results"] if r["state"]=="eligible"]
    assert eligible==["ed25519_sign","ed25519_verify","x25519","chacha20poly1305_encrypt","chacha20poly1305_decrypt"]
    changed=copy.deepcopy(api); changed["operation_results"][3]={"operation_id":"sha256","state":"eligible","evidence_sha256":"00"*32}
    reject(lambda:evidence_validator.validate_api_evidence(changed,bundle,contract),"")


def test_no_hash_or_noise_substitution():
    bundle,_,_,_=artifacts()
    for index,symbol in ((3,"crypto_sha512"),(4,"crypto_hkdf_sha512"),(7,"Noise_XK")):
        changed=copy.deepcopy(bundle); changed["operation_records"][index]["api_symbols"]=[symbol]
        reject(lambda changed=changed:evidence_validator.validate_operation_bundle(changed),"unavailable operation")


def test_standard_api_and_ietf_adapter_are_exact():
    bundle,_,_,_=artifacts(); direct=bundle["adapter"]["direct_api_symbols"]
    assert direct==evidence_validator.DIRECT
    assert "crypto_eddsa_sign" not in direct and "crypto_aead_lock" not in direct and "crypto_aead_unlock" not in direct
    assert "crypto_ed25519_sign" in direct and "crypto_ed25519_check" in direct and "crypto_aead_init_ietf" in direct
    changed=copy.deepcopy(bundle); changed["adapter"]["direct_api_symbols"].append("crypto_aead_lock")
    reject(lambda:evidence_validator.validate_operation_bundle(changed),"adapter binding")


def test_source_config_and_nonexecution_are_exact():
    bundle,_,_,_=artifacts(); reproduction=bundle["configuration_reproduction"]
    assert reproduction["generated_sdkconfig_bytes"]==106913
    changed=copy.deepcopy(bundle); changed["configuration_reproduction"]["runs"][0]["candidate_compiled"]=True
    reject(lambda:evidence_validator.validate_operation_bundle(changed),"must be false")
    changed=copy.deepcopy(bundle); changed["configuration_reproduction"]["source_requirements"][0]["raw_sha256"]="00"*32
    reject(lambda:evidence_validator.validate_operation_bundle(changed),"source requirements")


def test_registry_phase_and_authority():
    bundle,api,admission,contract=artifacts(); registry=admission["accepted_api_config_evidence_sha256"]
    assert all(len(registry[name])==1 for name in ("espressif_libsodium","esp_idf_mbedtls_psa","monocypher"))
    assert admission["acceptance_counts"]=={"source":3,"api_config":3,"candidate_import":0}
    assert admission["phase_zero"]["remaining"]==["independent_second_node_exact_profile_admission"]
    assert not any(admission["authority"].values()) and admission["claims"]["complete_api_config_eligibility_proven"] is False
    changed=copy.deepcopy(admission); changed["phase_zero"]["complete"]=True
    reject(lambda:admission_validator.validate(changed,bundle,api,contract),"phase-zero")
    changed=copy.deepcopy(admission); changed["authority"]["benchmark_execution_authorized"]=True
    reject(lambda:admission_validator.validate(changed,bundle,api,contract),"authority")


def test_adapter_and_retained_sources_are_byte_exact():
    for path,digest in ((evidence_validator.ADAPTER_HEADER,evidence_validator.EXPECTED_ADAPTER_HEADER_RAW_SHA256),(evidence_validator.ADAPTER_SOURCE,evidence_validator.EXPECTED_ADAPTER_SOURCE_RAW_SHA256),(evidence_validator.CORE_SOURCE,evidence_validator.CORE_SHA),(evidence_validator.ED_SOURCE,evidence_validator.ED_SHA)):
        assert hashlib.sha256(path.read_bytes()).hexdigest()==digest


def test_historical_artifacts_are_byte_exact():
    for path, digest in HISTORICAL_RAW.items():
        assert hashlib.sha256(path.read_bytes()).hexdigest() == digest, path.name


def test_clis_are_sanitized_and_raw_exact():
    commands=([sys.executable,str(evidence_validator.__file__),"--bundle",str(BUNDLE),"--api-evidence",str(API),"--contract",str(CONTRACT)],[sys.executable,str(admission_validator.__file__),"--admission",str(ADMISSION),"--bundle",str(BUNDLE),"--api-evidence",str(API),"--contract",str(CONTRACT)])
    for command in commands:
        done=subprocess.run(command,cwd=ROOT,capture_output=True,text=True); assert done.returncode==0,done.stderr
        hostile=subprocess.run(command[:2]+["--private=C:\\Users\\operator\\secret.json"],cwd=ROOT,capture_output=True,text=True)
        assert hostile.returncode==2 and hostile.stdout=="" and hostile.stderr.strip()=="ERROR: invalid arguments"
    with tempfile.TemporaryDirectory() as directory:
        target=Path(directory)/BUNDLE.name; target.write_bytes(BUNDLE.read_bytes().replace(b"\n",b"\r\n"))
        changed=list(commands[0]); changed[3]=str(target); done=subprocess.run(changed,cwd=ROOT,capture_output=True,text=True)
        assert done.returncode==1 and done.stdout=="" and done.stderr.strip()=="ERROR: validation failed"


def main():
    tests=(test_canonical_chain,test_exact_five_of_eight_partial,test_no_hash_or_noise_substitution,test_standard_api_and_ietf_adapter_are_exact,test_source_config_and_nonexecution_are_exact,test_registry_phase_and_authority,test_adapter_and_retained_sources_are_byte_exact,test_historical_artifacts_are_byte_exact,test_clis_are_sanitized_and_raw_exact)
    for test in tests: test()
    print(f"PASS: {len(tests)} OT-118 Monocypher API/config admission scenario groups"); return 0


if __name__=="__main__": raise SystemExit(main())
