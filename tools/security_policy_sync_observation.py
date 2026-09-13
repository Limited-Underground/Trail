"""One-use pure projection of already captured, image-bound OT-200 evidence.

This is not an executable observation/restoration adapter. It performs no device,
file, journal, network, process or clock operations and imports no physical
runtime. A future separately admitted executable must preserve confirmed serial
closure, durable restore intent, capture custody, and unconditional independently
verified restoration. Existing OT-198 grants and runtime do not admit this image.
"""
import hashlib

import security_policy_sync_readback as readback


class ObservationError(ValueError):
    """Fixed refusal without disclosing raw evidence or image identities."""


def _need(ok):
    if not ok:
        raise ObservationError("sync_observation_refused")


class Observation:
    """Claim one captured-byte projection, bound to one original and image.

    Fresh-original admission proves only absence of recognizable namespace
    history in those bytes. Neither a matching hash nor this one-use object
    establishes physical freshness, capture timing, an accepted receipt, or
    successful restoration. No caller boolean is accepted as such evidence.
    """
    def __init__(self, original_nvs, role, *, expected_image):
        _need(type(role) is str and role in ("A", "B"))
        readback.assert_image_binding(expected_image, expected_image)
        _need(expected_image.contract == readback.SYNC_CONTRACT)
        readback.assert_fresh(original_nvs)
        self._original_sha = hashlib.sha256(original_nvs).digest()
        self._role = role
        self._expected_image = expected_image
        self._consumed = False

    def project(self, captured_nvs, *, original_nvs, observed_image):
        """Consume the single claim even on decode/binding failure; never cache success."""
        _need(not self._consumed)
        self._consumed = True
        _need(type(original_nvs) is bytes and
              hashlib.sha256(original_nvs).digest() == self._original_sha)
        decoded = readback.decode(captured_nvs, expected_image=self._expected_image,
                                  observed_image=observed_image)
        return {"role": self._role, "evidence": "captured_bytes_projection", **decoded}
