/**
 * @file sha256d.c
 * @brief SHA-256d 구현
 */
#include "sha256d.h"
#include <string.h>

/* ── SHA-256 상수 ── */
static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,
    0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
    0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,
    0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,
    0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
    0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,
    0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,
    0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
    0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2,
};

#define ROR32(x,n) (((x)>>(n))|((x)<<(32-(n))))
#define CH(x,y,z)  (((x)&(y))^(~(x)&(z)))
#define MAJ(x,y,z) (((x)&(y))^((x)&(z))^((y)&(z)))
#define S0(x) (ROR32(x,2)^ROR32(x,13)^ROR32(x,22))
#define S1(x) (ROR32(x,6)^ROR32(x,11)^ROR32(x,25))
#define G0(x) (ROR32(x,7)^ROR32(x,18)^((x)>>3))
#define G1(x) (ROR32(x,17)^ROR32(x,19)^((x)>>10))

static const uint32_t INIT[8] = {
    0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
    0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19,
};

static void sha256_transform(uint32_t state[8], const uint8_t block[64])
{
    uint32_t w[64], a,b,c,d,e,f,g,h,t1,t2;
    int i;

    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i*4+0] << 24) |
               ((uint32_t)block[i*4+1] << 16) |
               ((uint32_t)block[i*4+2] <<  8) |
               ((uint32_t)block[i*4+3]);
    }
    for (; i < 64; i++) {
        w[i] = G1(w[i-2]) + w[i-7] + G0(w[i-15]) + w[i-16];
    }

    a=state[0]; b=state[1]; c=state[2]; d=state[3];
    e=state[4]; f=state[5]; g=state[6]; h=state[7];

    for (i = 0; i < 64; i++) {
        t1 = h + S1(e) + CH(e,f,g) + K[i] + w[i];
        t2 = S0(a) + MAJ(a,b,c);
        h=g; g=f; f=e; e=d+t1;
        d=c; c=b; b=a; a=t1+t2;
    }

    state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d;
    state[4]+=e; state[5]+=f; state[6]+=g; state[7]+=h;
}

void sha256_init(sha256_ctx_t *ctx)
{
    memcpy(ctx->state, INIT, sizeof(INIT));
    ctx->count[0] = ctx->count[1] = 0;
}

void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, size_t len)
{
    uint32_t i, idx = (ctx->count[0] >> 3) & 0x3F;
    ctx->count[0] += (uint32_t)(len << 3);
    if (ctx->count[0] < (uint32_t)(len << 3)) ctx->count[1]++;
    ctx->count[1] += (uint32_t)(len >> 29);

    uint32_t part = 64 - idx;
    if (len >= part) {
        memcpy(ctx->buf + idx, data, part);
        sha256_transform(ctx->state, ctx->buf);
        for (i = part; i + 63 < len; i += 64)
            sha256_transform(ctx->state, data + i);
        idx = 0;
    } else {
        i = 0;
    }
    memcpy(ctx->buf + idx, data + i, len - i);
}

void sha256_final(sha256_ctx_t *ctx, uint8_t *hash)
{
    uint8_t bits[8];
    uint32_t idx = (ctx->count[0] >> 3) & 0x3F;
    uint8_t pad = 0x80;

    for (int i = 7; i >= 0; i--) {
        bits[i] = (i < 4) ? (ctx->count[0] >> ((3-i)*8)) & 0xFF
                           : (ctx->count[1] >> ((7-i)*8)) & 0xFF;
    }

    sha256_update(ctx, &pad, 1);
    pad = 0;
    while (((ctx->count[0] >> 3) & 0x3F) != 56)
        sha256_update(ctx, &pad, 1);
    sha256_update(ctx, bits, 8);

    for (int i = 0; i < 8; i++) {
        hash[i*4+0] = (ctx->state[i] >> 24) & 0xFF;
        hash[i*4+1] = (ctx->state[i] >> 16) & 0xFF;
        hash[i*4+2] = (ctx->state[i] >>  8) & 0xFF;
        hash[i*4+3] = (ctx->state[i]      ) & 0xFF;
    }
}

void sha256(const uint8_t *data, size_t len, uint8_t *hash)
{
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, hash);
}

void sha256d(const uint8_t *data, size_t len, uint8_t *hash)
{
    uint8_t tmp[32];
    sha256(data, len, tmp);
    sha256(tmp, 32, hash);
}

/* ── Merkle 계산 ── */
void merkle_step(const uint8_t *left, const uint8_t *right, uint8_t *out)
{
    uint8_t buf[64];
    memcpy(buf,      left,  32);
    memcpy(buf + 32, right, 32);
    sha256d(buf, 64, out);
}

void merkle_root(const uint8_t *coinbase_hash,
                 const uint8_t branches[][32], uint8_t count,
                 uint8_t *root_out)
{
    uint8_t cur[32];
    memcpy(cur, coinbase_hash, 32);
    for (int i = 0; i < count; i++) {
        merkle_step(cur, branches[i], cur);
    }
    memcpy(root_out, cur, 32);
}

/* ── Midstate 계산 ── */
void calc_midstate(const uint8_t *header_first64, uint8_t *midstate_out)
{
    uint32_t state[8];
    memcpy(state, INIT, sizeof(INIT));
    sha256_transform(state, header_first64);
    for (int i = 0; i < 8; i++) {
        midstate_out[i*4+0] = (state[i] >> 24) & 0xFF;
        midstate_out[i*4+1] = (state[i] >> 16) & 0xFF;
        midstate_out[i*4+2] = (state[i] >>  8) & 0xFF;
        midstate_out[i*4+3] = (state[i]      ) & 0xFF;
    }
}
