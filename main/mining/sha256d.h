/**
 * @file sha256d.h
 * @brief SHA-256d (double SHA-256) 구현
 *
 * Bitcoin 채굴에 사용되는 SHA256d:
 *   SHA256d(data) = SHA256(SHA256(data))
 *
 * Merkle Root 계산에도 사용됨
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

/* SHA-256 컨텍스트 */
typedef struct {
    uint32_t state[8];
    uint32_t count[2];
    uint8_t  buf[64];
} sha256_ctx_t;

/* ── API ── */
void sha256_init(sha256_ctx_t *ctx);
void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, size_t len);
void sha256_final(sha256_ctx_t *ctx, uint8_t *hash);
void sha256(const uint8_t *data, size_t len, uint8_t *hash);
void sha256d(const uint8_t *data, size_t len, uint8_t *hash);

/* Merkle Root 계산 */
void merkle_step(const uint8_t *left, const uint8_t *right, uint8_t *out);
void merkle_root(const uint8_t *coinbase_hash,
                 const uint8_t branches[][32], uint8_t count,
                 uint8_t *root_out);

/* Midstate 계산 (첫 64바이트 SHA-256 중간값) */
void calc_midstate(const uint8_t *header_first64, uint8_t *midstate_out);
