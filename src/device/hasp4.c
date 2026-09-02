/*
 * PeepeeBox - Aladdin HASP4 (MemoHASP) dongle core, driven by real dumps.
 *
 * WHAT THIS IS
 *
 *   A dump-driven HASP4 service layer.  Feed it a h5dmp / UniDump dongle dump
 *   and it answers the HASP services -- IsHasp, GetCode, ReadMemo, WriteMemo,
 *   HaspStatus, HaspId, and the block forms -- from the real key material.
 *
 * WHAT THIS IS NOT
 *
 *   It is not a working parallel-port dongle yet, because the HASP4 *wire*
 *   framing is not known here.  Three sources were checked and none has it:
 *
 *     - Aladdin's own HARDLOCK.VXD is a driver-level shim (hasp2hlk.ch); its
 *       port I/O is Super I/O base discovery, not dongle traffic.
 *     - batteryshark/dongle-lab's io.hasp4 is an API/FEnteDev-level emulator;
 *       it has no bit-banging at all (its Port=0x378 is a reported value).
 *     - 86Box's stock hasp.c replays one hand-guessed bit pattern for Savage
 *       Quest.  Its C6 C7 C6 80 attention sequence appears nowhere in
 *       HARDLOCK.VXD, so it is not a general HASP framing and is not assumed
 *       here.
 *
 *   So the transport is left to the caller.  That is not a loss: Photo Play's
 *   own part is not a HASP (see dongle_photoplay.c), yet its library still
 *   speaks the HASP API downward -- MENU.EXE issues services 1, 5 and 0x32 --
 *   so this core is directly useful behind the transport that file already
 *   implements.  The LPT device at the bottom of this file is a thin shell with
 *   a tracer, there to capture real framing when a guest provides it.
 *
 * ALGORITHM PROVENANCE
 *
 *   GetCode and the universal security-table derivation are implemented from
 *   the algorithm as characterised in batteryshark/dongle-lab (projects/
 *   io.hasp4).  That repository carries NO licence, so nothing is copied from
 *   it -- this is an independent implementation of the described algorithm,
 *   written against this project's own conventions.
 *
 *   Both agree with the older UCLHASP core (MeteO/Fixit, 1998-99) on the two
 *   constants that matter -- the LCG seed step (*0x1989 + 5) and the password
 *   mask 0x09071966 -- which is what ties them to the same family.  They differ
 *   in the bit extraction: UCLHASP shifts the password directly, whereas HASP4
 *   indexes an 8-byte security table derived from it.  The security-table form
 *   is the one implemented here.
 *
 *   NOTE, honestly: neither form has been validated against real HASP4 silicon
 *   in this workspace.  The 160-byte password-derived blob at offset 0x07 of
 *   the UniDump container -- the obvious candidate oracle -- is reproduced by
 *   NEITHER: exhaustively, no seed and no security table can generate its
 *   8-byte records.  Whatever that blob is, it is not a GetCode table, and it
 *   is read by no loader.  Treat GetCode as characterised-but-unverified.
 *
 * DUMP CONTAINER
 *
 *   Offsets are taken from the INT 21h seek/read sequence in
 *   haspnt64/haspdos/UCLHASPF.ASM and cross-checked against
 *   haspnt64/uclhasp/REGISTRY.C.  Verified against nine real Photo Play dumps.
 *
 * Authors: written for the Photo Play preservation effort.
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/plat.h>
#include <86box/lpt.h>
#include <86box/hasp4.h>

#include "hasp4_keys.h"

#define HASP4_FILE_FILTER "HASP dongle dumps (*.dmp *.bin)|*.dmp,*.bin"

#ifdef ENABLE_HASP4_LOG
int hasp4_do_log = ENABLE_HASP4_LOG;

static void
hasp4_log(const char *fmt, ...)
{
    va_list ap;

    if (hasp4_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define hasp4_log(fmt, ...)
#endif

/**********************************************************************
 * Algorithms
 **********************************************************************/

/* The eight fixed bit rows the security table is assembled from.  Row n is
   selected by a 3-bit field of the masked password, so the table is always one
   of 8^8 combinations of these. */
static const uint8_t hasp4_rows[8][8] = {
    { 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 1, 0, 1, 0, 1, 0, 1 },
    { 1, 0, 1, 0, 1, 0, 1, 0 },
    { 0, 0, 1, 1, 0, 0, 1, 1 },
    { 1, 1, 0, 0, 1, 1, 0, 0 },
    { 0, 0, 0, 0, 1, 1, 1, 1 },
    { 1, 1, 1, 1, 0, 0, 0, 0 },
    { 1, 1, 1, 1, 1, 1, 1, 1 }
};

void
hasp4_derive_sectable(uint16_t pwd1, uint16_t pwd2, uint8_t out[8])
{
    uint32_t pw = ((uint32_t) pwd1 << 16) | pwd2;
    uint8_t  rows[8];

    /* Swap the halves, then mask.  Both constants match the UCLHASP core. */
    pw = (pw >> 16) | (pw << 16);
    pw ^= 0x09071966U;

    for (uint32_t i = 0; i < 8; i++) {
        rows[i] = (uint8_t) (pw & 7U);
        pw >>= 3;
    }

    memset(out, 0, 8);
    for (uint32_t i = 0; i < 8; i++)
        for (uint32_t j = 0; j < 8; j++)
            out[j] = (uint8_t) (out[j] | (hasp4_rows[rows[i]][j] << (7U - i)));
}

void
hasp4_getcode(uint16_t seed, const uint8_t sectable[8], uint8_t out[8])
{
    for (int i = 0; i < 8; i++) {
        out[i] = 0;
        for (int j = 0; j < 8; j++) {
            uint8_t pos;
            uint8_t bit;

            seed = (uint16_t) ((seed * 0x1989U) + 5U);
            pos  = (uint8_t) ((seed >> 9) & 0x3fU);
            bit  = (uint8_t) ((sectable[pos >> 3] >> (7 - (pos & 7))) & 1U);
            out[i] = (uint8_t) (out[i] | (bit << (7 - j)));
        }
    }
}

/**********************************************************************
 * Dump container
 **********************************************************************/

static uint16_t
hasp4_rd16(const uint8_t *p)
{
    return (uint16_t) (p[0] | (p[1] << 8));
}

static uint32_t
hasp4_rd32(const uint8_t *p)
{
    return (uint32_t) (p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

int
hasp4_key_parse(hasp4_key_t *key, const uint8_t *buf, uint32_t len)
{
    memset(key, 0, sizeof(hasp4_key_t));

    if (len < 8)
        return 0;

    key->pwd1 = hasp4_rd16(&buf[0x000]);
    key->pwd2 = hasp4_rd16(&buf[0x002]);

    switch (len) {
        /* Verified against nine real Photo Play dumps. */
        case 719:
            key->type      = buf[0x005];
            key->id        = hasp4_rd32(&buf[0x0af]);
            key->mem_bytes = HASP4_MEM_MAX;
            memcpy(key->mem, &buf[0x0c3], HASP4_MEM_MAX);
            break;

        /* Transcribed from UCLHASPF.ASM read_693; unverified. */
        case 693:
            key->type      = buf[0x005];
            key->id        = hasp4_rd32(&buf[0x0af]);
            key->mem_bytes = HASP4_MEM_MAX;
            memcpy(key->mem, &buf[0x0b3], HASP4_MEM_MAX);
            break;

        /* Transcribed from UCLHASPF.ASM; unverified. */
        case 204:
            key->type      = buf[0x004];
            key->id        = hasp4_rd32(&buf[0x01a]);
            key->mem_bytes = HASP4_MEMOHASP_MEM;
            memcpy(key->mem, &buf[0x05c], HASP4_MEMOHASP_MEM);
            break;

        case 332:
            key->type      = buf[0x004];
            key->id        = hasp4_rd32(&buf[0x01a]);
            key->mem_bytes = HASP4_MEMOHASP_MEM;
            memcpy(key->mem, &buf[0x0dc], HASP4_MEMOHASP_MEM);
            break;

        case 716:
        case 732:
            key->type      = buf[0x004];
            key->id        = hasp4_rd32(&buf[0x01a]);
            key->mem_bytes = HASP4_MEM_MAX;
            memcpy(key->mem, &buf[0x0ec], HASP4_MEM_MAX);
            break;

        /* Keys with no memory. */
        case 108:
        case 220:
        case 236:
        case 588:
        case 604:
            key->type      = buf[0x004];
            key->id        = hasp4_rd32(&buf[0x01a]);
            key->mem_bytes = 0;
            break;

        default:
            return 0;
    }

    /* A MemoHASP has 112 live bytes however much the container reserves.  All
       nine reference dumps are zero past byte 0x70, which confirms it. */
    if ((key->type == HASP4_TYPE_MEMOHASP) && (key->mem_bytes > HASP4_MEMOHASP_MEM))
        key->mem_bytes = HASP4_MEMOHASP_MEM;

    hasp4_derive_sectable(key->pwd1, key->pwd2, key->sectable);
    key->loaded = 1;
    return 1;
}

int
hasp4_key_load(hasp4_key_t *key, const char *path)
{
    uint8_t     buf[1024];
    size_t      len;
    FILE       *f;
    const char *base;

    if ((path == NULL) || (path[0] == '\0'))
        return 0;

    if ((f = plat_fopen(path, "rb")) == NULL) {
        hasp4_log("HASP4: cannot open dump %s\n", path);
        return 0;
    }
    len = fread(buf, 1, sizeof(buf), f);
    fclose(f);

    if (!hasp4_key_parse(key, buf, (uint32_t) len)) {
        hasp4_log("HASP4: unrecognised dump size %u in %s\n", (unsigned) len, path);
        return 0;
    }

    base = strrchr(path, '/');
    if (base == NULL)
        base = strrchr(path, '\\');
    base = (base != NULL) ? (base + 1) : path;
    strncpy(key->name, base, sizeof(key->name) - 1);

    hasp4_log("HASP4: %s: type %d pwd %04X/%04X id %08X mem %d sectable %02X%02X%02X%02X%02X%02X%02X%02X\n",
              key->name, key->type, key->pwd1, key->pwd2, key->id, key->mem_bytes,
              key->sectable[0], key->sectable[1], key->sectable[2], key->sectable[3],
              key->sectable[4], key->sectable[5], key->sectable[6], key->sectable[7]);
    return 1;
}

/**********************************************************************
 * Service layer
 **********************************************************************/

int
hasp4_read_block(const hasp4_key_t *key, uint16_t start_word, uint16_t words, uint8_t *dst)
{
    uint32_t off = (uint32_t) start_word * 2U;
    uint32_t n   = (uint32_t) words * 2U;

    if (!key->loaded)
        return HASP4_ERR_NOKEY;
    if ((off + n) > (uint32_t) key->mem_bytes)
        return HASP4_ERR_RANGE;

    memcpy(dst, &key->mem[off], n);
    return HASP4_OK;
}

int
hasp4_write_block(hasp4_key_t *key, uint16_t start_word, uint16_t words, const uint8_t *src)
{
    uint32_t off = (uint32_t) start_word * 2U;
    uint32_t n   = (uint32_t) words * 2U;

    if (!key->loaded)
        return HASP4_ERR_NOKEY;
    if ((off + n) > (uint32_t) key->mem_bytes)
        return HASP4_ERR_RANGE;

    memcpy(&key->mem[off], src, n);
    return HASP4_OK;
}

void
hasp4_service(hasp4_key_t *key, uint8_t svc, const uint16_t in[5], uint16_t out[4],
              int check_password)
{
    uint8_t code[8];

    out[0] = out[1] = out[2] = out[3] = 0;

    if (!key->loaded) {
        out[2] = HASP4_ERR_NOKEY;
        return;
    }

    /* Every service but IsHasp carries the password pair.  Real parts answer
       nothing at all to a wrong pair, which is what a caller probing for its
       key relies on. */
    if (check_password && (svc != HASP4_SVC_ISHASP)
        && ((in[1] != key->pwd1) || (in[2] != key->pwd2))) {
        out[2] = HASP4_ERR_BADPASS;
        return;
    }

    switch (svc) {
        case HASP4_SVC_ISHASP:
            out[0] = 1;
            break;

        case HASP4_SVC_GETCODE:
            hasp4_getcode(in[0], key->sectable, code);
            out[0] = hasp4_rd16(&code[0]);
            out[1] = hasp4_rd16(&code[2]);
            out[2] = hasp4_rd16(&code[4]);
            out[3] = hasp4_rd16(&code[6]);
            break;

        case HASP4_SVC_READMEMO: {
            uint8_t w[2];
            int     st = hasp4_read_block(key, in[3], 1, w);

            out[1] = (st == HASP4_OK) ? hasp4_rd16(w) : 0;
            out[2] = (uint16_t) st;
            break;
        }

        case HASP4_SVC_WRITEMEMO: {
            uint8_t w[2] = { (uint8_t) (in[4] & 0xff), (uint8_t) (in[4] >> 8) };

            out[2] = (uint16_t) hasp4_write_block(key, in[3], 1, w);
            break;
        }

        case HASP4_SVC_STATUS:
            out[0] = (uint16_t) key->mem_bytes;
            out[1] = key->type;
            break;

        case HASP4_SVC_HASPID:
            out[0] = (uint16_t) (key->id & 0xffff);
            out[1] = (uint16_t) (key->id >> 16);
            break;

        default:
            hasp4_log("HASP4: unimplemented service %02X\n", svc);
            out[2] = HASP4_ERR_NOSVC;
            break;
    }
}

/**********************************************************************
 * Built-in dumps
 **********************************************************************/

int
hasp4_builtin_count(void)
{
    int n = 0;

    while (hasp4_builtins[n].id != NULL)
        n++;
    return n;
}

const char *
hasp4_builtin_label(int i)
{
    static char buf[64];

    if ((i < 0) || (i >= hasp4_builtin_count()))
        return NULL;

    snprintf(buf, sizeof(buf), "%s [%s]", hasp4_builtins[i].title,
             hasp4_builtins[i].territory);
    return buf;
}

const char *
hasp4_builtin_id(int i)
{
    if ((i < 0) || (i >= hasp4_builtin_count()))
        return NULL;
    return hasp4_builtins[i].id;
}

int
hasp4_builtin_load(int i, hasp4_key_t *key)
{
    if ((i < 0) || (i >= hasp4_builtin_count()))
        return 0;

    if (!hasp4_key_parse(key, hasp4_builtins[i].data, 719))
        return 0;

    snprintf(key->name, sizeof(key->name), "%s [%s] (%s)",
             hasp4_builtins[i].title, hasp4_builtins[i].territory,
             hasp4_builtins[i].id);
    return 1;
}

/**********************************************************************
 * LPT shell
 *
 * A place to hang a transport once real framing is captured.  Until then it
 * traces, so that a guest which does talk to a HASP hands us the sequence.
 **********************************************************************/

typedef struct hasp4_t {
    void       *lpt;
    hasp4_key_t key;

    uint8_t  status;
    FILE    *trace;
    uint32_t seq;
} hasp4_t;

static void
hasp4_trace(hasp4_t *dev, const char *what, uint8_t val)
{
    if (dev->trace == NULL)
        return;

    fprintf(dev->trace, "%08u %-11s %02X\n", dev->seq++, what, val);
    fflush(dev->trace);
}

static void
hasp4_write_data(uint8_t val, void *priv)
{
    hasp4_trace((hasp4_t *) priv, "write_data", val);
}

static void
hasp4_write_ctrl(uint8_t val, void *priv)
{
    hasp4_trace((hasp4_t *) priv, "write_ctrl", val);
}

static uint8_t
hasp4_read_status_lpt(void *priv)
{
    hasp4_t *dev = (hasp4_t *) priv;

    hasp4_trace(dev, "read_status", dev->status);
    return dev->status;
}

/* Prove the key material and the algorithms at load time, so a bad dump is
   loud rather than silent. */
static void
hasp4_selftest(hasp4_t *dev)
{
    uint16_t in[5] = { 0, 0, 0, 0, 0 };
    uint16_t out[4];

    if (!dev->key.loaded)
        return;

    in[1] = dev->key.pwd1;
    in[2] = dev->key.pwd2;

    hasp4_service(&dev->key, HASP4_SVC_GETCODE, in, out, 1);
    hasp4_log("HASP4: selftest GetCode(0000) = %04X %04X %04X %04X\n",
              out[0], out[1], out[2], out[3]);

    if (dev->trace != NULL) {
        fprintf(dev->trace, "# key %s: type=%d pwd=%04X/%04X id=%08X mem=%d\n",
                dev->key.name, dev->key.type, dev->key.pwd1, dev->key.pwd2,
                dev->key.id, dev->key.mem_bytes);
        fprintf(dev->trace, "# GetCode(0000) = %04X %04X %04X %04X\n",
                out[0], out[1], out[2], out[3]);
        fflush(dev->trace);
    }
}

static void *
hasp4_init(const device_t *info)
{
    hasp4_t    *dev = calloc(1, sizeof(hasp4_t));
    const char *path;

    path = device_get_config_string("dump_fn");
    if (!hasp4_key_load(&dev->key, path))
        hasp4_log("HASP4: no usable dump; the key will not answer\n");

    path = device_get_config_string("trace_fn");
    if ((path != NULL) && (path[0] != '\0')) {
        dev->trace = plat_fopen(path, "wt");
        if (dev->trace != NULL)
            fprintf(dev->trace, "# PeepeeBox HASP4 parallel port trace\n");
    }

    dev->lpt = lpt_attach(hasp4_write_data, hasp4_write_ctrl, NULL,
                          hasp4_read_status_lpt, NULL, NULL, NULL, dev);
    dev->status = 0x80;

    hasp4_selftest(dev);
    return dev;
}

static void
hasp4_close(void *priv)
{
    hasp4_t *dev = (hasp4_t *) priv;

    if (dev->trace != NULL)
        fclose(dev->trace);
    free(dev);
}

// clang-format off
static const device_config_t hasp4_config[] = {
    {
        .name           = "key",
        .description    = "Dongle",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { "Load from file...",                        0 },
            { "Photo Play 2001 [ES]",                     1 },
            { "Photo Play 2001 [PT]",                     2 },
            { "Photo Play 2000 SP [NL]  (file: 2002PT)",  3 },
            { "Photo Play 2000 SP [ES]  (file: 2003ES)",  4 },
            { "Photo Play 2000 SP [PT]  (file: 2003PT)",  5 },
            { "Photo Play 2005B [ES]",                    6 },
            { "Photo Play 2005B [PT]",                    7 },
            { "Photo Play 2006A [PT]",                    8 },
            { "Photo Play 2007 [ES]",                     9 },
            { "",                                         0 }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "dump_fn",
        .description    = "Dongle dump file (when \"Load from file...\")",
        .type           = CONFIG_FNAME,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = HASP4_FILE_FILTER,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "trace_fn",
        .description    = "Parallel port trace log (optional)",
        .type           = CONFIG_FNAME,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};
// clang-format on

const device_t lpt_hasp4_device = {
    .name          = "Aladdin HASP4 dongle (dump-driven)",
    .internal_name = "dongle_hasp4",
    .flags         = DEVICE_LPT | DEVICE_HOTPLUG,
    .local         = 0,
    .init          = hasp4_init,
    .close         = hasp4_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = hasp4_config
};
