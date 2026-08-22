#include <psa/crypto.h>

#define OT_LINK_ANCHOR(symbol) \
    __asm__ __volatile__("" : : "r"(&(symbol)) : "memory")

void app_main(void)
{
    OT_LINK_ANCHOR(psa_raw_key_agreement);
    OT_LINK_ANCHOR(psa_hash_compute);
    OT_LINK_ANCHOR(psa_key_derivation_setup);
    OT_LINK_ANCHOR(psa_key_derivation_input_bytes);
    OT_LINK_ANCHOR(psa_key_derivation_input_key);
    OT_LINK_ANCHOR(psa_key_derivation_output_bytes);
    OT_LINK_ANCHOR(psa_aead_encrypt);
    OT_LINK_ANCHOR(psa_aead_decrypt);
}
