#include <sodium.h>

#include "noise_xk_libsodium.h"

#define OT_LINK_ANCHOR(symbol) \
    __asm__ __volatile__("" : : "r"(&(symbol)) : "memory")

void app_main(void)
{
    OT_LINK_ANCHOR(crypto_sign_detached);
    OT_LINK_ANCHOR(crypto_sign_verify_detached);
    OT_LINK_ANCHOR(crypto_scalarmult_curve25519);
    OT_LINK_ANCHOR(crypto_hash_sha256);
    OT_LINK_ANCHOR(crypto_kdf_hkdf_sha256_extract);
    OT_LINK_ANCHOR(crypto_kdf_hkdf_sha256_expand);
    OT_LINK_ANCHOR(crypto_aead_chacha20poly1305_ietf_encrypt);
    OT_LINK_ANCHOR(crypto_aead_chacha20poly1305_ietf_decrypt);
    OT_LINK_ANCHOR(ot_noise_xk_init_initiator);
    OT_LINK_ANCHOR(ot_noise_xk_init_responder);
    OT_LINK_ANCHOR(ot_noise_xk_write_message);
    OT_LINK_ANCHOR(ot_noise_xk_read_message);
    OT_LINK_ANCHOR(ot_noise_xk_split);
    OT_LINK_ANCHOR(ot_noise_xk_abort);
}
