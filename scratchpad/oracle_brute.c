/* Brute-force the password-zero path of the HASP4 data transform.
 *
 * HaspTransformWord has two paths.  The password one takes only an 8-byte security
 * table and was swept already: 3740 combinations, no hit.  This is the other one,
 * seeded instead by column_mask and crypt_init_vect through HaspPrepareTransformKey
 * -- one byte each, and only six bits of the second are used, so the whole space is
 * 256 * 64 keys per candidate table.
 *
 * The oracle is one block.  Every 2001 FINDIT picture starts with the same ciphertext
 * d2 46 3a 28 cf f9 62 d6, and the plaintext is known from the 2000 image plus its
 * LCG header key to be 0a 05 01 08 00 00 00 00 -- a plain PCX header.  Eight bytes is
 * far past coincidence, so a hit is the answer and a miss rules the path out.
 *
 *   cc -O2 -o h4brute h4brute.c && ./h4brute <dump> [tableoffset]
 *
 * Transliterated from batteryshark/dongle-lab projects/io.hasp4/src/hasp4_core.c.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define POLY 0x80500062u
#define CA   0x5B2C004Au
#define CB   0x803425C3u

typedef struct {
    uint8_t  sec_table[8];
    uint8_t  is_inv_sec_tab;
    uint32_t prep_not_mask;
    uint32_t initial_lfsr_state;
    uint32_t cur_lfsr_state;
    uint32_t password;
    int      wide;
} KEY;

static const uint32_t FACT_LFSR[4] = { 0x480u, 0x4A0u, 0x580u, 0x5A0u };

static inline uint32_t rol(uint32_t v, int s)
{
    s &= 31;
    return s ? ((v << s) | (v >> (32 - s))) : v;
}

static inline uint32_t sec_get(uint32_t i, const uint8_t *t, int mode)
{
    switch (mode) {
        case 0:  return (t[(i >> 2) & 0x0eu] >> ((31u - i) & 7u)) & 1u;
        case 1:  return (t[(i >> 2) & 7u]    >> ((31u - i) & 7u)) & 1u;
        case 2:  return (t[i >> 3] >> (7u - (i & 7u))) & 1u;
        default: return (t[i >> 3] >> (i & 7u)) & 1u;
    }
}

static uint32_t transform2(uint32_t in5, KEY *k)
{
    uint32_t fact;
    uint32_t nst = 0;
    uint32_t str;

    in5 &= 0x1fu;
    fact = FACT_LFSR[(in5 >> 1) & 3u];
    for (uint32_t pos = 0; pos < 12u; pos++)
        if ((fact >> pos) & 1u)
            nst ^= (k->cur_lfsr_state >> pos);

    k->cur_lfsr_state ^= (in5 & 1u) << 2;
    str = sec_get(in5, k->sec_table, k->wide) ^ k->is_inv_sec_tab;
    k->cur_lfsr_state = (k->cur_lfsr_state << 1) | ((nst ^ str) & 1u);
    k->cur_lfsr_state ^= (k->prep_not_mask >> in5) & 1u;

    return ((k->cur_lfsr_state >> 11) ^ str) & 1u;
}

static void prepare(KEY *k, const uint8_t *tab, uint8_t column_mask, uint8_t civ, int wide)
{
    uint8_t  first_bit;
    uint8_t  prep_cm;
    uint8_t  civ_work;
    uint32_t emul = 0;
    uint32_t pnm  = 0;

    memset(k, 0, sizeof(*k));
    k->wide = wide;
    memcpy(k->sec_table, tab, 8);
    k->is_inv_sec_tab = (uint8_t) ((civ >> 5) & 1u);

    first_bit = (uint8_t) (sec_get(0, k->sec_table, wide) ^ 1u);
    prep_cm   = first_bit ? column_mask : (uint8_t) (~column_mask);

    civ_work = (uint8_t) (civ & 0x1fu);
    for (int b = 0; b < 4; b++) {
        uint8_t b0 = (uint8_t) (emul & 0xffu);

        b0 = (uint8_t) (b0 << 2);
        b0 = (uint8_t) (b0 | ((civ_work & 1u) | (((civ_work ^ 1u) & 1u) << 1)));
        emul = (emul & 0xffffff00u) | b0;
        civ_work = (uint8_t) (civ_work >> 1);
    }
    emul &= 0xff0000ffu;
    emul |= ((uint32_t) (uint8_t) (emul & 0xffu)) << 8;
    emul |= ((uint32_t) (uint8_t) ((emul & 0xffu) ^ 0xffu)) << 16;
    emul |= ((uint32_t) (uint8_t) ((emul >> 16) & 0xffu)) << 24;

    for (int b = 0; b < 8; b++) {
        uint8_t b1 = (uint8_t) ((emul >> 8) & 0xffu);
        uint8_t b3 = (uint8_t) ((emul >> 24) & 0xffu);

        b1 ^= (uint8_t) ((sec_get((uint32_t) (b + 8), k->sec_table, wide) ^ civ_work) << b);
        b3 ^= (uint8_t) ((sec_get((uint32_t) (b + 24), k->sec_table, wide) ^ civ_work) << b);
        emul = (emul & 0xffff00ffu) | ((uint32_t) b1 << 8);
        emul = (emul & 0x00ffffffu) | ((uint32_t) b3 << 24);
    }

    for (int i = 31; i >= 0; i--) {
        uint32_t last = 0;

        k->cur_lfsr_state = ((uint32_t) prep_cm) << 3;
        for (int step = 0; step < 12; step++)
            last = transform2((uint32_t) i, k);
        pnm <<= 1;
        pnm |= sec_get((uint32_t) i, k->sec_table, wide) ^ ((uint32_t) i & 1u)
             ^ ((emul >> (uint32_t) i) & 1u) ^ last;
    }

    k->prep_not_mask      = pnm;
    k->initial_lfsr_state = (((uint32_t) prep_cm) << 3) | (((uint32_t) first_bit) << 2)
                          | (((uint32_t) first_bit) << 1) | first_bit;
    k->cur_lfsr_state     = k->initial_lfsr_state;
}

static void transform_word(uint32_t *data, KEY *k)
{
    uint32_t index = 0;

    k->cur_lfsr_state = k->initial_lfsr_state;
    for (int i = 1; i <= 39; i++) {
        const uint32_t byte = (*data >> (8u * index)) & 0xffu;
        const uint32_t bit  = transform2(byte, k);

        index = ((*data & 1u) << 1) | bit;
        if ((*data & 1u) == bit)
            *data >>= 1;
        else
            *data = (*data >> 1) ^ POLY;
    }
}

static void decode_block(const uint8_t *in, uint8_t *out, KEY *k)
{
    uint32_t b0 = (uint32_t) in[0] | ((uint32_t) in[1] << 8) | ((uint32_t) in[2] << 16) | ((uint32_t) in[3] << 24);
    uint32_t b1 = (uint32_t) in[4] | ((uint32_t) in[5] << 8) | ((uint32_t) in[6] << 16) | ((uint32_t) in[7] << 24);
    uint32_t tmp;

    for (int s = 25; s >= 0; s -= 5) {
        tmp = rol(b0 ^ CA, s) ^ b1;
        b1  = b0;
        b0  = tmp;
    }
    tmp = b0;
    transform_word(&b0, k);
    b0 ^= b1;
    b1  = tmp;
    for (int s = 10; s >= 0; s -= 2) {
        tmp = rol(b0 ^ CB, s) ^ b1;
        b1  = b0;
        b0  = tmp;
    }
    tmp = b0;
    transform_word(&b0, k);
    b0 ^= b1;
    b1  = tmp;

    for (int i = 0; i < 4; i++) {
        out[i]     = (uint8_t) (b0 >> (8 * i));
        out[i + 4] = (uint8_t) (b1 >> (8 * i));
    }
}

int
main(int argc, char **argv)
{
    uint8_t dump[4096];
    size_t  n;
    FILE   *f;
    uint32_t in, out;
    int      hits = 0;

    if (argc < 4) {
        fprintf(stderr, "usage: %s <dump> <input hex> <output hex>\n", argv[0]);
        return 2;
    }
    f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    n = fread(dump, 1, sizeof(dump), f);
    fclose(f);
    in  = (uint32_t) strtoul(argv[2], NULL, 16);
    out = (uint32_t) strtoul(argv[3], NULL, 16);
    printf("target f(%08X) = %08X over %zu-byte dump\n", in, out, n);

    for (size_t off = 0; off + 8 <= n; off++)
        for (int mode = 0; mode < 4; mode++)
            for (int cm = 0; cm < 256; cm++)
                for (int civ = 0; civ < 256; civ++) {
                    KEY k;
                    uint32_t v = in;

                    prepare(&k, dump + off, (uint8_t) cm, (uint8_t) civ, mode);
                    transform_word(&v, &k);
                    if (v == out) {
                        printf("  *** HIT table@0x%03zX mode=%d column_mask=%02X civ=%02X\n",
                               off, mode, cm, civ);
                        hits++;
                    }
                }
    printf("done: %d hit(s)\n", hits);
    return hits ? 0 : 1;
}
