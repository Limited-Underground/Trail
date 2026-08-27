#ifndef OT149_CANDIDATE_API_H
#define OT149_CANDIDATE_API_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool ot149_candidate_initialize(void);
bool ot149_candidate_vectors_and_negative_cases(void);
int ot149_candidate_x25519(void);
int ot149_candidate_sha256(void);
int ot149_candidate_hkdf_sha256(void);
int ot149_candidate_chacha20poly1305_encrypt(void);
int ot149_candidate_chacha20poly1305_decrypt(void);
bool ot149_candidate_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif
