/* Solve for the two dwords the 2001 dongle returns for one 4 KB buffer.
 *
 * For the first block of a buffer the dongle path (0x2D04C) is
 *
 *     (L1,R1) = A(ciphertext)
 *     L2 = R1 ^ acc ;  R2 = L1          acc = the first LFSR result
 *     (L3,R3) = B(L2,R2)
 *     plaintext = (R3 ^ D, L3)          D   = the second LFSR result
 *
 * and the IV is zero for that block.  So L3 must equal the plaintext's high dword:
 * a 32-bit test for each guess of acc, with D falling out as L4 ^ R3.  That turns a
 * 2^64 problem into 2^32 with an immediate reject.
 *
 * Then the rest of the buffer is a check: schedule[0] = D, schedule[i] =
 * schedule[i-1] + acc with schedule[1] ^= ROR32(D, ...), and every later block is
 * 0x2E682's twelve invertible iterations.  A wrong (acc,D) fails that at once.
 *
 *   cc -O2 -o solved solved.c
 *   solved <C0hex16> <P0hex16> <C1hex16> <P1hex16>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define CA 0x5B2C004AU
#define CB 0x803425C3U

static const int ROT_A[6] = { 25, 20, 15, 10, 5, 0 };
static const int ROT_B[6] = { 10, 8, 6, 4, 2, 0 };

static inline uint32_t rol(uint32_t v, int k)
{
    k &= 31;
    return k ? ((v << k) | (v >> (32 - k))) : v;
}

static inline uint32_t ror(uint32_t v, int k)
{
    k &= 31;
    return k ? ((v >> k) | (v << (32 - k))) : v;
}

static void feistel(uint32_t *L, uint32_t *R, uint32_t c, const int *rots)
{
    uint32_t l = *L, r = *R;

    for (int i = 0; i < 6; i++) {
        uint32_t nl = r ^ rol(l ^ c, rots[i]);

        r = l;
        l = nl;
    }
    *L = l;
    *R = r;
}

/* 0x2E682, forward: twelve iterations of two half-rounds */
static void soft(uint32_t *Lp, uint32_t *Rp, const uint32_t *k)
{
    uint32_t L = *Lp, R = *Rp;

    for (int i = 12; i >= 1; i--) {
        L = ror(L - k[2 * i + 1], (R >> 7) & 0x1F) ^ R;
        R = ror(R + k[2 * i], (L >> 4) & 0x1F) ^ L;
    }
    *Lp = L;
    *Rp = R;
}

static void schedule(uint32_t D, uint32_t acc, uint32_t *k)
{
    uint32_t d = D;

    k[0] = D;
    for (int i = 1; i < 26; i++) {
        k[i] = k[i - 1] + acc;
        d = ror(d, k[i - 1] & 0x1F);
        k[1] ^= d;
    }
}

static void hex2(const char *s, uint32_t *a, uint32_t *b)
{
    uint8_t v[8];

    for (int i = 0; i < 8; i++) {
        unsigned x;

        sscanf(s + i * 2, "%2x", &x);
        v[i] = (uint8_t) x;
    }
    *a = v[0] | (v[1] << 8) | ((uint32_t) v[2] << 16) | ((uint32_t) v[3] << 24);
    *b = v[4] | (v[5] << 8) | ((uint32_t) v[6] << 16) | ((uint32_t) v[7] << 24);
}

int main(int argc, char **argv)
{
    uint32_t c0l, c0r, p0l, p0r, c1l, c1r, p1l, p1r;

    if (argc < 5) {
        fprintf(stderr, "need C0 P0 C1 P1 as 16 hex digits each\n");
        return 2;
    }
    hex2(argv[1], &c0l, &c0r);
    hex2(argv[2], &p0l, &p0r);
    hex2(argv[3], &c1l, &c1r);
    hex2(argv[4], &p1l, &p1r);

    uint32_t L1 = c0l, R1 = c0r;

    feistel(&L1, &R1, CA, ROT_A);

    long hits = 0;

    for (uint64_t a = 0; a < 0x100000000ULL; a++) {
        uint32_t acc = (uint32_t) a;
        uint32_t L3 = R1 ^ acc, R3 = L1;

        feistel(&L3, &R3, CB, ROT_B);
        if (L3 != p0r)
            continue;

        uint32_t D = p0l ^ R3;
        uint32_t k[26];

        schedule(D, acc, k);

        /* block 1 of the buffer: T(C1) must equal P1 ^ C0 (CBC) */
        uint32_t tl = c1l, tr = c1r;

        soft(&tl, &tr, k);
        if ((tl == (p1l ^ c0l)) && (tr == (p1r ^ c0r))) {
            printf("SOLVED acc=%08X D=%08X  (block 1 verifies)\n", acc, D);
            return 0;
        }
        hits++;
        if (hits <= 4)
            printf("  acc=%08X D=%08X passes the 32-bit test, fails block 1\n", acc, D);
    }
    printf("no candidate verified; %ld passed the first test\n", hits);
    return 1;
}
