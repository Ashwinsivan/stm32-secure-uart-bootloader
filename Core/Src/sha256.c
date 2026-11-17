#include "sha256.h"
#include <string.h>

static const uint32_t k[64] = {
    0x428A2F98U,0x71374491U,0xB5C0FBCFU,0xE9B5DBA5U,0x3956C25BU,0x59F111F1U,0x923F82A4U,0xAB1C5ED5U,
    0xD807AA98U,0x12835B01U,0x243185BEU,0x550C7DC3U,0x72BE5D74U,0x80DEB1FEU,0x9BDC06A7U,0xC19BF174U,
    0xE49B69C1U,0xEFBE4786U,0x0FC19DC6U,0x240CA1CCU,0x2DE92C6FU,0x4A7484AAU,0x5CB0A9DCU,0x76F988DAU,
    0x983E5152U,0xA831C66DU,0xB00327C8U,0xBF597FC7U,0xC6E00BF3U,0xD5A79147U,0x06CA6351U,0x14292967U,
    0x27B70A85U,0x2E1B2138U,0x4D2C6DFCU,0x53380D13U,0x650A7354U,0x766A0ABBU,0x81C2C92EU,0x92722C85U,
    0xA2BFE8A1U,0xA81A664BU,0xC24B8B70U,0xC76C51A3U,0xD192E819U,0xD6990624U,0xF40E3585U,0x106AA070U,
    0x19A4C116U,0x1E376C08U,0x2748774CU,0x34B0BCB5U,0x391C0CB3U,0x4ED8AA4AU,0x5B9CCA4FU,0x682E6FF3U,
    0x748F82EEU,0x78A5636FU,0x84C87814U,0x8CC70208U,0x90BEFFFAU,0xA4506CEBU,0xBEF9A3F7U,0xC67178F2U
};

static uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32U - n)); }
static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24U) | ((uint32_t)p[1] << 16U) | ((uint32_t)p[2] << 8U) | p[3]; }
static void putbe(uint8_t *p, uint32_t x) { p[0]=(uint8_t)(x>>24U); p[1]=(uint8_t)(x>>16U); p[2]=(uint8_t)(x>>8U); p[3]=(uint8_t)x; }

static void Transform(SHA256_Context *c, const uint8_t block[64])
{
    uint32_t w[64], a,b,d,e,f,g,h,t1,t2,cc;
    for (cc=0U; cc<16U; cc++) w[cc]=be32(&block[cc*4U]);
    for (cc=16U; cc<64U; cc++) {
        uint32_t s0=rotr(w[cc-15U],7U)^rotr(w[cc-15U],18U)^(w[cc-15U]>>3U);
        uint32_t s1=rotr(w[cc-2U],17U)^rotr(w[cc-2U],19U)^(w[cc-2U]>>10U);
        w[cc]=w[cc-16U]+s0+w[cc-7U]+s1;
    }
    a=c->state[0]; b=c->state[1]; cc=c->state[2]; d=c->state[3]; e=c->state[4]; f=c->state[5]; g=c->state[6]; h=c->state[7];
    for (uint32_t i=0U;i<64U;i++) {
        uint32_t s1=rotr(e,6U)^rotr(e,11U)^rotr(e,25U);
        uint32_t ch=(e&f)^((~e)&g);
        t1=h+s1+ch+k[i]+w[i];
        uint32_t s0=rotr(a,2U)^rotr(a,13U)^rotr(a,22U);
        uint32_t maj=(a&b)^(a&cc)^(b&cc);
        t2=s0+maj; h=g; g=f; f=e; e=d+t1; d=cc; cc=b; b=a; a=t1+t2;
    }
    c->state[0]+=a; c->state[1]+=b; c->state[2]+=cc; c->state[3]+=d; c->state[4]+=e; c->state[5]+=f; c->state[6]+=g; c->state[7]+=h;
}

void SHA256_Init(SHA256_Context *c)
{
    c->state[0]=0x6A09E667U; c->state[1]=0xBB67AE85U; c->state[2]=0x3C6EF372U; c->state[3]=0xA54FF53AU;
    c->state[4]=0x510E527FU; c->state[5]=0x9B05688CU; c->state[6]=0x1F83D9ABU; c->state[7]=0x5BE0CD19U;
    c->bit_count=0U; memset(c->buffer,0,sizeof(c->buffer));
}

void SHA256_Update(SHA256_Context *c, const uint8_t *data, uint32_t length)
{
    uint32_t used=(uint32_t)((c->bit_count>>3U)&63U); c->bit_count += (uint64_t)length*8U;
    while (length) { uint32_t n=64U-used; if(n>length)n=length; memcpy(&c->buffer[used],data,n); used+=n; data+=n; length-=n; if(used==64U){Transform(c,c->buffer);used=0U;} }
}

void SHA256_Final(SHA256_Context *c, uint8_t digest[32])
{
    uint8_t pad[64]={0x80U}; uint8_t length[8]; uint64_t bits=c->bit_count;
    for(int i=7;i>=0;i--){length[i]=(uint8_t)bits;bits>>=8U;}
    uint32_t used=(uint32_t)((c->bit_count>>3U)&63U); SHA256_Update(c,pad,used<56U?56U-used:120U-used); SHA256_Update(c,length,8U);
    for(uint32_t i=0U;i<8U;i++) putbe(&digest[i*4U],c->state[i]);
}

void HMAC_SHA256(const uint8_t *key, uint32_t key_length,const uint8_t *data,uint32_t data_length,uint8_t digest[32])
{
    uint8_t k0[64]={0}, ipad[64], opad[64], inner[32]; SHA256_Context c;
    if(key_length>64U){SHA256_Init(&c);SHA256_Update(&c,key,key_length);SHA256_Final(&c,k0);} else memcpy(k0,key,key_length);
    for(uint32_t i=0;i<64U;i++){ipad[i]=k0[i]^0x36U;opad[i]=k0[i]^0x5CU;}
    SHA256_Init(&c);SHA256_Update(&c,ipad,64U);SHA256_Update(&c,data,data_length);SHA256_Final(&c,inner);
    SHA256_Init(&c);SHA256_Update(&c,opad,64U);SHA256_Update(&c,inner,32U);SHA256_Final(&c,digest);
}
