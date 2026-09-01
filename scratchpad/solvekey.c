/* Recover the two dwords the dongle hands back for the first 4 KB buffer.
 *
 * Block 0 is the only block the dongle touches.  Its decode is
 *
 *     (L1,R1) = A_rounds(C0)            keyless, so computed once
 *     (L2,R2) = (f1 ^ R1, L1)
 *     (L3,R3) = B_rounds(L2,R2)         keyless
 *     P0      = (f2 ^ R3, L3)
 *
 * and P0 is known from I.G.O. 4.  Its second dword is L3, which does not involve f2 at
 * all -- so guessing f1 is a 2^32 search with a 32-bit filter, and f2 falls out as
 * P0.lo ^ R3.  Survivors are then checked against blocks 1 and 2 through the software
 * round at 0x3483D, which is 128 further bits and settles it.
 *
 * The walker takes D from the transform's output slot +0 and acc from +4; which of f1,f2
 * lands where is not read out of 0x32A81 yet, so both orders are tried.
 *
 *   cc -O2 -o solvekey solvekey.c && ./solvekey
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "solvedata.h"

#define CA 0x5B2C004AU
#define CB 0x803425C3U

static inline uint32_t rol(uint32_t v, int s)
{
    s &= 31;
    return s ? ((v << s) | (v >> (32 - s))) : v;
}

static inline uint32_t ror(uint32_t v, int s)
{
    s &= 31;
    return s ? ((v >> s) | (v << (32 - s))) : v;
}

static inline uint32_t rd(const unsigned char *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) |
           ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

/* 0x34DA3 */
static void schedule(uint32_t D, uint32_t acc, uint32_t *k)
{
    k[0] = D;
    for (int j = 1; j < 26; j++) {
        uint32_t prev = k[j - 1];

        k[j] = prev + acc;
        k[1] ^= ror(D, prev & 31);
    }
}

/* 0x3483D */
static void soft_decode(uint32_t *Lp, uint32_t *Rp, const uint32_t *k)
{
    uint32_t L = *Lp, R = *Rp;

    for (int i = 12; i >= 1; i--) {
        L = ror(L - k[2 * i + 1], (R >> 7) & 31) ^ R;
        R = ror(R + k[2 * i], (L >> 4) & 31) ^ L;
    }
    *Lp = L - k[1];
    *Rp = R - k[0];
}

static int check_rest(uint32_t D, uint32_t acc, const unsigned char *ct)
{
    uint32_t k[26];

    schedule(D, acc, k);
    for (int b = 1; b <= 2; b++) {
        uint32_t L = rd(ct + b * 8), R = rd(ct + b * 8 + 4);
        uint32_t pL = rd(ct + (b - 1) * 8), pR = rd(ct + (b - 1) * 8 + 4);

        soft_decode(&L, &R, k);
        L ^= pL;                       /* CBC, previous ciphertext */
        R ^= pR;
        if (L != rd(PT + b * 8) || R != rd(PT + b * 8 + 4))
            return 0;
    }
    return 1;
}

static void run(const char *tag, const unsigned char *ct)
{
    const uint32_t p0lo = rd(PT), p0hi = rd(PT + 4);
    uint32_t L1 = rd(ct), R1 = rd(ct + 4);
    long filtered = 0;
    int  solved = 0;

    for (int s = 25; s >= 0; s -= 5) {   /* A rounds, keyless -- once */
        uint32_t t = rol(L1 ^ CA, s) ^ R1;

        R1 = L1;
        L1 = t;
    }

    for (uint64_t g = 0; g < 0x100000000ULL; g++) {
        uint32_t f1 = (uint32_t) g;
        uint32_t b0 = f1 ^ R1, b1 = L1;

        for (int s = 10; s >= 0; s -= 2) {
            uint32_t t = rol(b0 ^ CB, s) ^ b1;

            b1 = b0;
            b0 = t;
        }
        if (b0 != p0hi)                  /* after the B rounds b0 is L3 */
            continue;
        filtered++;

        uint32_t f2 = p0lo ^ b1;         /* and b1 is R3 */

        if (check_rest(f1, f2, ct)) {
            printf("  %s SOLVED: D=%08X acc=%08X   (D = first keyed output)\n",
                   tag, f1, f2);
            solved = 1;
        }
        if (check_rest(f2, f1, ct)) {
            printf("  %s SOLVED: D=%08X acc=%08X   (D = second keyed output)\n",
                   tag, f2, f1);
            solved = 1;
        }
    }
    printf("  %s: %ld candidates passed the 32-bit filter, %s\n",
           tag, filtered, solved ? "and one verified" : "none verified on blocks 1-2");
}

int
main(void)
{
    printf("plaintext block0 %08X %08X\n", rd(PT), rd(PT + 4));
    run("IGO2", CT2);
    run("IGO3", CT3);
    return 0;
}
