/*
 * PeepeeBox - Aladdin HASP4 dongle core.
 *
 * A dump-driven HASP4 (MemoHASP) implementation: it loads a real h5dmp /
 * UniDump dongle dump and answers the HASP service layer from it.
 *
 * This is deliberately a *core*, not a device.  The HASP4 parallel-port wire
 * framing is not known (see the note in hasp4.c), and Photo Play's own part is
 * not a HASP at all -- but its library still speaks the HASP API downward
 * (dongle_photoplay.c observes MENU.EXE issuing services 1, 5 and 0x32).  So
 * the useful, reusable piece is the service layer, which any transport can sit
 * on top of.
 */
#ifndef EMU_HASP4_H
#define EMU_HASP4_H

#include <stdint.h>

/* Dongle types, as recorded in the dump container. */
#define HASP4_TYPE_HASP3     0
#define HASP4_TYPE_MEMOHASP  1
#define HASP4_TYPE_TIMEHASP  3
#define HASP4_TYPE_NETHASP   4
#define HASP4_TYPE_TIMEHASP3 5

/* HASP service numbers. */
#define HASP4_SVC_ISHASP     0x01
#define HASP4_SVC_GETCODE    0x02
#define HASP4_SVC_READMEMO   0x03
#define HASP4_SVC_WRITEMEMO  0x04
#define HASP4_SVC_STATUS     0x05
#define HASP4_SVC_HASPID     0x06
#define HASP4_SVC_READBLOCK  0x32
#define HASP4_SVC_WRITEBLOCK 0x33

/* Status codes returned in out[2]. */
#define HASP4_OK          0
#define HASP4_ERR_RANGE   1
#define HASP4_ERR_NOKEY   2
#define HASP4_ERR_BADPASS 3
#define HASP4_ERR_NOSVC   4

/* A MemoHASP carries 112 bytes; a MemoHASP4 carries 496.  The dump container
   reserves room for the larger one either way. */
#define HASP4_MEM_MAX      496
#define HASP4_MEMOHASP_MEM 112

typedef struct hasp4_key_t {
    uint16_t pwd1;
    uint16_t pwd2;
    uint32_t id;      /* serial number */
    uint8_t  type;
    uint8_t  sectable[8]; /* universal security table, derived from the password */
    int      mem_bytes;
    uint8_t  mem[HASP4_MEM_MAX];
    int      loaded;
    char     name[64]; /* for logging: the dump's basename */
} hasp4_key_t;

/* --- dump container ------------------------------------------------------ */

/* Parse a UniDump image.  Returns 1 on success.  Only the 719-byte variant is
   verified against real dumps; the other sizes are transcribed from
   haspnt64/haspdos/UCLHASPF.ASM and are marked in the source. */
extern int hasp4_key_parse(hasp4_key_t *key, const uint8_t *buf, uint32_t len);

/* Load a dump from disk (also accepts a .7z holding a single dump if the
   caller has already extracted it -- this takes the raw dump only). */
extern int hasp4_key_load(hasp4_key_t *key, const char *path);

/* --- algorithms ---------------------------------------------------------- */

/* Derive the 8-byte universal security table from the 32-bit password
   ((pwd1 << 16) | pwd2).  Called for you by hasp4_key_parse(). */
extern void hasp4_derive_sectable(uint16_t pwd1, uint16_t pwd2, uint8_t out[8]);

/* HASP4 GetCode: eight response bytes for a 16-bit seed. */
extern void hasp4_getcode(uint16_t seed, const uint8_t sectable[8], uint8_t out[8]);

/* --- service layer ------------------------------------------------------- */

/*
 * Dispatch one HASP service.  Register mapping follows the DOS HASP calling
 * convention:
 *
 *   in[0] = AX (seed)          out[0] = AX
 *   in[1] = CX (password 1)    out[1] = BX
 *   in[2] = DX (password 2)    out[2] = CX  (status)
 *   in[3] = DI (word address)  out[3] = DX
 *   in[4] = SI (data / length)
 *
 * For GETCODE the four response words are returned in out[0..3].
 * Passwords in in[1]/in[2] are checked only when check_password is non-zero.
 */
extern void hasp4_service(hasp4_key_t *key, uint8_t svc, const uint16_t in[5],
                          uint16_t out[4], int check_password);

/* Block read/write against the dump's memory, word-addressed like service
   0x32/0x33.  Returns a HASP4_ERR_* code. */
extern int hasp4_read_block(const hasp4_key_t *key, uint16_t start_word,
                            uint16_t words, uint8_t *dst);
extern int hasp4_write_block(hasp4_key_t *key, uint16_t start_word,
                             uint16_t words, const uint8_t *src);

/* --- built-in dumps ------------------------------------------------------ *
 *
 * Nine real Photo Play dongle dumps are embedded (hasp4_keys.h).  These
 * accessors let other devices -- dongle_photoplay.c in particular -- offer them
 * without pulling in the table itself.
 */

/* Number of embedded dumps. */
extern int hasp4_builtin_count(void);

/* Menu label for dump i, e.g. "Photo Play 2001 [ES]".  NULL if out of range.
   The version and territory come from the record's own banner, not the file
   name -- they disagree for one of the nine. */
extern const char *hasp4_builtin_label(int i);

/* Short id for dump i, e.g. "2001ES" (this is the source file's name). */
extern const char *hasp4_builtin_id(int i);

/* Parse embedded dump i into key.  Returns 1 on success. */
extern int hasp4_builtin_load(int i, hasp4_key_t *key);

/* The LPT shell device is declared in lpt.h with the other parallel devices. */

#endif /*EMU_HASP4_H*/
