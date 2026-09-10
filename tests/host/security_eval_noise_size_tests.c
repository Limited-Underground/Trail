// Include unchanged actual target source to test its otherwise-static size guards.
#include <stdio.h>
#include <stdlib.h>
#include "noise_xk_libsodium.c"
#define CHECK(x) do{if(!(x))exit(1);}while(0)
int main(void){
 ot_noise_xk_state state={0};state.has_cipher_key=1;
 unsigned char byte=0x55;size_t output_size=77;
 CHECK(encrypt_and_hash(&state,&byte,SIZE_MAX,&byte,SIZE_MAX,&output_size)!=0);
 CHECK(encrypt_and_hash(&state,&byte,SIZE_MAX-OT_NOISE_XK_TAG_BYTES+1,&byte,SIZE_MAX,&output_size)!=0);
 CHECK(decrypt_and_hash(&state,&byte,15,&byte,SIZE_MAX,SIZE_MAX)!=0);
 CHECK(decrypt_and_hash(&state,&byte,0,&byte,SIZE_MAX,SIZE_MAX-OT_NOISE_XK_TAG_BYTES+1)!=0);
 CHECK(encrypt_and_hash(&state,&byte,0,&byte,15,&output_size)!=0);
 CHECK(byte==0x55&&output_size==77&&state.nonce==0);
 puts("PASS 5 target adapter size-bound refusal groups");return 0;
}
