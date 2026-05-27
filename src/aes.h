#ifndef _AES_H_
#define _AES_H_

// ============================================================
//  Tiny-AES-c — AES-128 ECB
//  Public domain (The Unlicense) — github.com/kokke/tiny-AES-c
//  Trimmed to AES-128 ECB only for this project.
//  No dynamic memory. No dependencies beyond stdint.h.
// ============================================================

#include <stdint.h>
#include <stddef.h>

#define AES_BLOCKLEN  16   // Block length in bytes — AES is 128b block fixed
#define AES_KEYLEN    16   // Key length  in bytes — 128-bit key
#define AES_keyExpSize 176 // Expanded key schedule size

struct AES_ctx {
  uint8_t RoundKey[AES_keyExpSize];
};

// Initialise context with a 16-byte key
void AES_init_ctx(struct AES_ctx* ctx, const uint8_t* key);

// In-place ECB encrypt/decrypt of exactly one 16-byte block
void AES_ECB_encrypt(const struct AES_ctx* ctx, uint8_t* buf);
void AES_ECB_decrypt(const struct AES_ctx* ctx, uint8_t* buf);

#endif // _AES_H_