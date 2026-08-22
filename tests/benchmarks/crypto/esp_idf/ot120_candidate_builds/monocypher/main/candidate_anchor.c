#include "monocypher_benchmark_api.h"

#define OT_LINK_ANCHOR(symbol) \
    __asm__ __volatile__("" : : "r"(&(symbol)) : "memory")

void app_main(void)
{
    OT_LINK_ANCHOR(ot_monocypher_ed25519_sign);
    OT_LINK_ANCHOR(ot_monocypher_ed25519_verify);
    OT_LINK_ANCHOR(ot_monocypher_x25519);
    OT_LINK_ANCHOR(ot_monocypher_chacha20poly1305_ietf_encrypt);
    OT_LINK_ANCHOR(ot_monocypher_chacha20poly1305_ietf_decrypt);
}
