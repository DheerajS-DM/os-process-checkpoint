#include "workload.h"

#define HASH_RING_SIZE_BYTES (64 * 1024 * 1024) /* 64 MB */
#define HASH_CHUNK_SIZE      64

typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t buffer[64];
} sha256_mini_ctx_t;

static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define EP1(x) (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define SIG0(x) (ROTR(x, 7) ^ ROTR(x, 18) ^ ((x) >> 3))
#define SIG1(x) (ROTR(x, 17) ^ ROTR(x, 19) ^ ((x) >> 10))

static void sha256_transform(sha256_mini_ctx_t *ctx, const uint8_t data[64]) {
    uint32_t a, b, c, d, e, f, g, h, t1, t2, m[64];
    for (int i = 0, j = 0; i < 16; ++i, j += 4)
        m[i] = ((uint32_t)data[j] << 24) | ((uint32_t)data[j + 1] << 16) | ((uint32_t)data[j + 2] << 8) | ((uint32_t)data[j + 3]);
    for (int i = 16; i < 64; ++i)
        m[i] = SIG1(m[i - 2]) + m[i - 7] + SIG0(m[i - 15]) + m[i - 16];

    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];

    for (int i = 0; i < 64; ++i) {
        t1 = h + EP1(e) + CH(e, f, g) + K[i] + m[i];
        t2 = EP0(a) + MAJ(a, b, c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

static void sha256_mini_init(sha256_mini_ctx_t *ctx) {
    ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
    ctx->count = 0;
}

typedef struct {
    size_t ring_size;
    uint32_t lcg_state;
    uint8_t *ring_buffer;
} hash_workload_state_t;

static void rebind_hash_pointers(workload_ctx_t *ctx) {
    if (!ctx || !ctx->heap_base) return;
    hash_workload_state_t *st = (hash_workload_state_t *)ctx->heap_base;
    size_t header_size = (sizeof(hash_workload_state_t) + 63) & ~63;
    
    if (st->ring_size == 0 || st->ring_size > ctx->heap_size) {
        st->ring_size = ctx->heap_size > (HASH_RING_SIZE_BYTES + header_size) 
                        ? HASH_RING_SIZE_BYTES 
                        : (ctx->heap_size - header_size);
    }
    st->ring_buffer = (uint8_t *)ctx->heap_base + header_size;
}

void workload_hash_init(workload_ctx_t *ctx) {
    if (!ctx || !ctx->heap_base) return;
    
    hash_workload_state_t *st = (hash_workload_state_t *)ctx->heap_base;
    size_t header_size = (sizeof(hash_workload_state_t) + 63) & ~63;
    
    st->ring_size = ctx->heap_size > (HASH_RING_SIZE_BYTES + header_size) 
                    ? HASH_RING_SIZE_BYTES 
                    : (ctx->heap_size - header_size);
    st->lcg_state = 0x1337BEEF;
    st->ring_buffer = (uint8_t *)ctx->heap_base + header_size;
    
    for (size_t i = 0; i < st->ring_size; i++) {
        st->ring_buffer[i] = (uint8_t)(i ^ (i >> 8));
    }
    
    LOG_I("WORKLOAD", "Chaotic Hash Ring initialized (ring_size=%zu MB)", st->ring_size / (1024 * 1024));
}

void workload_hash_step(workload_ctx_t *ctx) {
    if (!ctx || !ctx->heap_base) return;
    
    rebind_hash_pointers(ctx);
    hash_workload_state_t *st = (hash_workload_state_t *)ctx->heap_base;
    size_t updates_per_step = 64;
    
    if (st->ring_size <= HASH_CHUNK_SIZE + 32) return;
    
    for (size_t u = 0; u < updates_per_step; u++) {
        st->lcg_state = st->lcg_state * 1664525u + 1013904223u;
        size_t offset_a = (st->lcg_state % (st->ring_size - HASH_CHUNK_SIZE));
        
        st->lcg_state = st->lcg_state * 1664525u + 1013904223u;
        size_t offset_b = (st->lcg_state % (st->ring_size - 32));
        
        sha256_mini_ctx_t s_ctx;
        sha256_mini_init(&s_ctx);
        sha256_transform(&s_ctx, &st->ring_buffer[offset_a]);
        
        uint8_t hash_out[32];
        for (int i = 0; i < 8; i++) {
            hash_out[i * 4 + 0] = (uint8_t)(s_ctx.state[i] >> 24);
            hash_out[i * 4 + 1] = (uint8_t)(s_ctx.state[i] >> 16);
            hash_out[i * 4 + 2] = (uint8_t)(s_ctx.state[i] >> 8);
            hash_out[i * 4 + 3] = (uint8_t)(s_ctx.state[i]);
        }
        
        memcpy(&st->ring_buffer[offset_b], hash_out, 32);
    }
    
    ctx->iterations++;
}
