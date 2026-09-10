/* Test-only: actual frozen adapter + real pinned libsodium, independent vectors. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sodium.h>
#include "noise_xk_libsodium.h"
#include "independent_vectors.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"check failed line %d\n",__LINE__); exit(1); } } while(0)
static unsigned groups;
static size_t unhex(const char *s,unsigned char *out) {
 size_t len=strlen(s);CHECK(len%2==0);
 for(size_t i=0;i<len/2;i++){unsigned a;CHECK(sscanf(s+2*i,"%2x",&a)==1);out[i]=(unsigned char)a;}return len/2;
}
static void equals(const unsigned char* actual,size_t size,const char* expected){
 unsigned char b[128];size_t n=unhex(expected,b);CHECK(n==size);CHECK(memcmp(actual,b,n)==0);
}
static void setup(const struct Vector *v,ot_noise_xk_state *a,ot_noise_xk_state *b,int wrong){
 ot_noise_xk_keypair k[4];unsigned char prologue[128];size_t n=unhex(v->prologue,prologue);
 for(unsigned j=0;j<4;j++){CHECK(unhex(v->private_keys[j],k[j].secret)==32);CHECK(crypto_scalarmult_curve25519_base(k[j].public_key,k[j].secret)==0);equals(k[j].public_key,32,v->public_keys[j]);}
 unsigned char pinned[32];memcpy(pinned,k[2].public_key,32);if(wrong)pinned[0]^=1;
 CHECK(ot_noise_xk_init_initiator(a,&k[0],&k[1],pinned,prologue,n)==0);
 CHECK(ot_noise_xk_init_responder(b,&k[2],&k[3],prologue,n)==0);sodium_memzero(k,sizeof k);
}
static ot_noise_xk_state* sender(unsigned step,ot_noise_xk_state* a,ot_noise_xk_state* b){return step==1?b:a;}
static ot_noise_xk_state* receiver(unsigned step,ot_noise_xk_state* a,ot_noise_xk_state* b){return step==1?a:b;}
static void advance(const struct Vector *v,ot_noise_xk_state* a,ot_noise_xk_state* b,unsigned count){
 for(unsigned j=0;j<count;j++){unsigned char m[128];size_t n=0;CHECK(ot_noise_xk_write_message(sender(j,a,b),m,sizeof m,&n)==0);equals(m,n,v->messages[j]);CHECK(ot_noise_xk_read_message(receiver(j,a,b),m,n)==0);}
}
static void failed(ot_noise_xk_state* s){
 unsigned char zero[32]={0};CHECK(s->stage==OT_NOISE_XK_STAGE_FAILED);
 CHECK(memcmp(s->local_static_secret,zero,32)==0);CHECK(memcmp(s->local_ephemeral_secret,zero,32)==0);
 CHECK(memcmp(s->cipher_key,zero,32)==0);CHECK(memcmp(s->chaining_key,zero,32)==0);
}
int main(void){/* Use unchanged upstream default scalar implementations, without CPU dispatch. */
 for(unsigned i=0;i<sizeof vectors/sizeof vectors[0];i++){
  const struct Vector* v=&vectors[i];ot_noise_xk_state a,b;unsigned char tx[32],rx[32];
  setup(v,&a,&b,0);advance(v,&a,&b,3);equals(a.handshake_hash,32,v->hash);equals(b.handshake_hash,32,v->hash);
  CHECK(ot_noise_xk_split(&a,tx,rx)==0);equals(tx,32,v->initiator_tx);equals(rx,32,v->responder_tx);
  CHECK(ot_noise_xk_split(&b,tx,rx)==0);equals(tx,32,v->responder_tx);equals(rx,32,v->initiator_tx);groups++;
  for(unsigned step=0;step<3;step++)for(unsigned variant=0;variant<3;variant++){
   setup(v,&a,&b,0);advance(v,&a,&b,step);unsigned char m[129];size_t n=unhex(v->messages[step],m);
   if(variant==0)m[n-1]^=1;else if(variant==1)n--;else m[n++]=0;
   ot_noise_xk_state* r=receiver(step,&a,&b);CHECK(ot_noise_xk_read_message(r,m,n)!=0);failed(r);groups++;
  }
  setup(v,&a,&b,1);unsigned char m[128];size_t n=0;CHECK(ot_noise_xk_write_message(&a,m,sizeof m,&n)==0);CHECK(ot_noise_xk_read_message(&b,m,n)!=0);failed(&b);groups++;
  setup(v,&a,&b,0);n=unhex(v->messages[1],m);CHECK(ot_noise_xk_read_message(&a,m,n)!=0);failed(&a);groups++;
  setup(v,&a,&b,0);CHECK(ot_noise_xk_write_message(&b,m,sizeof m,&n)!=0);failed(&b);groups++;
 }
 printf("PASS: %u real-primitive independent XK groups\n",groups);return 0;
}
