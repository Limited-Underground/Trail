"""Fail-closed corrected Monocypher accounting admission tests."""
from pathlib import Path
import copy
import hashlib
import json
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import crypto_monocypher_corrected_resource_accounting as successor
import crypto_matched_resource_accounting as predecessor


class CorrectedResourceTests(unittest.TestCase):
    def setUp(self):
        self.contract = json.loads((ROOT / successor.CONTRACT_REPO_PATH).read_bytes())
        self.result = json.loads((ROOT / 'tests/benchmarks/crypto/OT-163-MONOCYPHER-CORRECTED-MATCHED-RESOURCE-2026-09-10.json').read_bytes())

    def test_actual_corrected_result_and_recomputed_metrics(self):
        actual = successor.validate_result(self.contract, self.result)
        self.assertEqual(actual['verdict'], 'pass')
        self.assertEqual(actual['measurements']['linked_flash_delta_bytes'], 19660)
        self.assertEqual(actual['measurements']['static_ram_delta_bytes'], 0)

    def test_old_admission_remains_unchanged_and_rejects_new_schema(self):
        base_contract = json.loads((ROOT / predecessor.CONTRACT_REPO_PATH).read_bytes())
        old_result = json.loads((ROOT / 'tests/benchmarks/crypto/OT-150-OT005-MATCHED-RESOURCE-RESULT-V1.json').read_bytes())
        self.assertEqual(predecessor.validate_result(base_contract, old_result)['verdict'], 'pass')
        with self.assertRaises(ValueError):
            predecessor.validate_result(base_contract, self.result)

    def test_wrong_candidate_and_historical_config_rejected(self):
        bad = copy.deepcopy(self.result)
        bad['candidate_id'] = 'espressif_libsodium'
        with self.assertRaises(ValueError):
            successor.validate_result(self.contract, bad)
        bad = copy.deepcopy(self.result)
        for side in ('candidate', 'control'):
            bad['builds'][side]['matched_metadata']['generated_sdkconfig'] = predecessor.CANDIDATES[2]['generated_sdkconfig_sha256']
        with self.assertRaises(ValueError):
            successor.validate_result(self.contract, bad)

    def test_changed_predecessor_bytes_rejected_before_compilation(self):
        with patch.object(Path, 'read_bytes', return_value=b'print("unexpected")'):
            with self.assertRaisesRegex(ValueError, 'predecessor source changed'):
                successor.validate_contract(self.contract)

    def test_corrected_provenance_and_contract_binding_tamper(self):
        bad = copy.deepcopy(self.contract)
        bad['bindings']['corrected_target_build']['raw_sha256'] = '0' * 64
        with self.assertRaises(ValueError):
            successor.validate_contract(bad)
        bad = copy.deepcopy(self.result)
        bad['contract']['raw_sha256'] = '0' * 64
        with self.assertRaises(ValueError):
            successor.validate_result(self.contract, bad)

    def test_delta_projection_and_report_tamper(self):
        for edit in ('delta', 'projection', 'report'):
            bad = copy.deepcopy(self.result)
            if edit == 'delta':
                bad['measurements']['linked_flash_delta_bytes'] += 1
            elif edit == 'projection':
                bad['phase_two_projection']['static_ram_bytes'] = 0
            else:
                bad['reports']['candidate']['raw_sha256'] = '0' * 64
            with self.assertRaises(ValueError):
                successor.validate_result(self.contract, bad)

    def test_unmatched_build_and_false_execution_claim_rejected(self):
        bad = copy.deepcopy(self.result)
        bad['builds']['control']['matched_metadata']['project_version'] = 'different'
        with self.assertRaises(ValueError):
            successor.validate_result(self.contract, bad)
        bad = copy.deepcopy(self.result)
        bad['claims']['benchmark_executed'] = True
        with self.assertRaises(ValueError):
            successor.validate_result(self.contract, bad)

    def test_successor_does_not_mutate_predecessor_namespace(self):
        before = copy.deepcopy(predecessor.CANDIDATES)
        for _ in range(2):
            successor.validate_result(self.contract, self.result)
        self.assertEqual(predecessor.CANDIDATES, before)
        self.assertEqual(hashlib.sha256(successor.BASE_PATH.read_bytes()).hexdigest(), successor.BASE_SHA256)


if __name__ == '__main__':
    unittest.main(verbosity=2)
