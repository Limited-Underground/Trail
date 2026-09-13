"""Pure captured-byte claims; unchanged restoration/receipt barriers tested separately."""
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import security_policy_input_readback as v1
import security_policy_sync_observation as observation
import security_policy_sync_readback as readback
import security_policy_sync_readback_tests as fixtures


class Tests(unittest.TestCase):
    def setUp(self):
        self.original = fixtures.fixtures.fixture()
        self.image = fixtures.SYNC_IMAGE
        self.observer = observation.Observation(self.original, "A", expected_image=self.image)

    def project(self, captured=None, *, original=None, image=None):
        return self.observer.project(fixtures.diagnostic() if captured is None else captured,
                                     original_nvs=self.original if original is None else original,
                                     observed_image=self.image if image is None else image)

    def test_projection_is_one_role_and_one_claim(self):
        result = self.project()
        self.assertEqual("A", result["role"])
        self.assertEqual("captured_bytes_projection", result["evidence"])
        self.assertEqual("input_result", result["stage"])
        with self.assertRaises(observation.ObservationError):
            self.project()

    def test_fresh_original_authority_reused_before_capture(self):
        with patch.object(readback, "assert_fresh", wraps=v1.assert_fresh) as fresh:
            observation.Observation(self.original, "B", expected_image=self.image)
            fresh.assert_called_once_with(self.original)
        for raw in (fixtures.diagnostic(), b"", bytearray(self.original)):
            with self.assertRaises(v1.ReadbackError):
                observation.Observation(raw, "A", expected_image=self.image)

    def test_invalid_or_old_image_not_admitted_as_sync_observation(self):
        for image in (None, True, fixtures.V1_IMAGE):
            with self.assertRaises((observation.ObservationError, readback.ReadbackError)):
                observation.Observation(self.original, "A", expected_image=image)
        with self.assertRaises(TypeError):
            observation.Observation(self.original, "A")

    def test_roles_are_exact(self):
        for role in (None, True, "C", "A ", "a"):
            with self.assertRaises(observation.ObservationError):
                observation.Observation(self.original, role, expected_image=self.image)

    def test_original_change_refuses_before_decode_and_consumes(self):
        with patch.object(readback, "decode", side_effect=AssertionError("must not decode")):
            with self.assertRaises(observation.ObservationError):
                self.project(original=b"?" * len(self.original))
        with self.assertRaises(observation.ObservationError):
            self.project()

    def test_observed_image_change_consumes_without_retry(self):
        wrong = readback.ImageBinding(readback.SYNC_CONTRACT, self.image.target, "c" * 64)
        with self.assertRaises(readback.ReadbackError):
            self.project(image=wrong)
        with self.assertRaises(observation.ObservationError):
            self.project()

    def test_failed_decode_consumes_and_has_no_cached_success(self):
        with self.assertRaises(readback.ReadbackError):
            self.project(b"private")
        with self.assertRaises(observation.ObservationError):
            self.project()
        self.assertFalse(hasattr(self.observer, "result"))

    def test_waiting_is_partial_and_send_return_is_not_a_receipt(self):
        result = self.project(fixtures.diagnostic(3))
        self.assertEqual("committed_before_stage_result", result["input_status"])
        later = observation.Observation(self.original, "B", expected_image=self.image)
        result = later.project(fixtures.diagnostic(8), original_nvs=self.original, observed_image=self.image)
        for key in ("receipt", "pass", "complete", "restored", "custody_verified", "deadline_met"):
            self.assertNotIn(key, result)

    def test_no_physical_or_restoration_adapter_is_exposed(self):
        for name in ("restore_with_observation", "execution", "backend", "os", "subprocess", "serial"):
            self.assertFalse(hasattr(observation, name))
        self.assertFalse(hasattr(self.observer, "capture"))
        with self.assertRaises(TypeError):
            self.observer.project(fixtures.diagnostic(), original_nvs=self.original,
                                  observed_image=self.image, custody_verified=True)


if __name__ == "__main__":
    unittest.main(verbosity=2)
