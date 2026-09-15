/*
 * ppfix -- repair a Photo Play 2.0 disk image's Microcosm CopyControl layout.
 *
 * 2.0 carries no dongle.  Its games are wrapped in CopyControl v1.66, whose key is not in
 * any file: it is the *physical layout* of the disk.  Two things have to be true, and
 * copying an image file by file destroys both:
 *
 *   - PP2000.CCC's last cluster must end with a run of a particular byte, written into
 *     the slack past the end of the file;
 *   - CCONTROL.SYS and PP2000.CCC must each start at the exact cluster CCMOVE recorded.
 *
 * Every 2.0 image found so far has lost both, which is why they stop with "Run CCMOVE to
 * create a working copy" the moment a game starts.  The files themselves are untouched
 * originals; only where they sit had been lost.
 *
 * This repairs an image in place and stamps a marker in LBA 1 -- a sector inside the MBR
 * track that no filesystem uses -- so PeepeeBox can tell a repaired image from one that
 * will fail, and say so instead of letting the games mystify the user.
 *
 * Where the three values come from:
 *
 *   PP2000.CCC's cluster   decrypted out of the licence.  The cipher is broken: an LFSR
 *                          seeded from the PCODE XOR the key byte at +8 of the .CCC, an
 *                          8x8 bit transpose, and a rotating XOR (docs/research/24).  The
 *                          PCODE is the .CCC's own filename, so this works for any install.
 *   CCONTROL.SYS's cluster the word at +6 of CCONTROL.SYS, XOR 4343.  On the NL install
 *                          that is the value the stub was measured comparing against;
 *                          --ccontrol overrides.
 *   the slack fill byte    chosen from an 8-entry table by an index the stub passes.  The
 *                          stub is the same on every install seen; --slack overrides.
 *
 * The clusters CCMOVE recorded belong to the cabinet's own disk, which was 2,111,864,832
 * bytes.  Images that were copied onto a smaller disk may not reach that far, and are
 * grown back to the cabinet's size first: the FAT gets longer, and the root directory and
 * data move down behind it, with every cluster number left as it was.
 *
 * It is a small window: it opens HardDisk.img from its own folder, or asks for one,
 * says whether that image is fixed, unfixed, or not a 2.0 image at all, and repairs it
 * only when the user presses Fix.  The two overridable values are behind the Advanced
 * button, and can also be pinned on the command line; --fix repairs without the window:
 *
 *     ppfix [--ccontrol N] [--slack XX] [--fix] [<image>]
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "ppfix_res.h"

#define SECTOR       512
#define MARKER_LBA   1
#define MARKER_MAGIC "PPBOXCC1"

/* The disk a 2.0 cabinet shipped with: 4092/16/63.  PeepeeBox takes the geometry from
   the image's size, so this is also what the grown image boots as. */
#define CABINET_SECS 4124736u
#define CABINET_SIZE "2,111,864,832"

/* CCONTROL.SYS keeps the cluster it expects to sit at in its word at +6, under this. */
#define SYS_XOR      0x4343u

/* There is no console to print to, so what the repair has to say is collected here
   and shown in the window. */
static char pf_msg[2048];

static void
pf_clear(void)
{
    pf_msg[0] = 0;
}

static void
pf_logf(const char *fmt, ...)
{
    const size_t n = strlen(pf_msg);
    va_list      ap;

    if (n >= (sizeof(pf_msg) - 1))
        return;
    va_start(ap, fmt);
    vsnprintf(pf_msg + n, sizeof(pf_msg) - n, fmt, ap);
    va_end(ap);
}

/* ------------------------------------------------------------------ the licence cipher */

typedef struct {
    uint8_t reg[8];
} lfsr_t;

static uint8_t
lfsr_step(lfsr_t *s)
{
    for (int pass = 0; pass < 4; pass++) {
        uint8_t cf = 0;
        uint8_t al;
        uint8_t t;

        for (int i = 0; i < 8; i++) {
            const uint8_t b = s->reg[i];

            s->reg[i] = (uint8_t) ((cf << 7) | (b >> 1));
            cf        = b & 1;
        }
        al = s->reg[7];
        t  = (uint8_t) ((cf << 7) | (al >> 1));
        cf = al & 1;
        al = t;
        t  = (uint8_t) ((cf << 7) | (al >> 1));
        al = t;
        al ^= s->reg[7];
        al  = (uint8_t) ((al >> 1) | ((al & 1) << 7));
        al &= 0x80;
        s->reg[0] |= al;
    }
    return s->reg[0];
}

/* plain = transpose(cipher ^ LFSR) ^ bh, bh rotating once per 8-byte block.  The LFSR is
   seeded with the NUL-padded PCODE XOR the key byte kept in the clear at +8 of the .CCC.
   It is 01 on some installs -- which is how "PCODE plus one" once looked right -- and
   2C or 2E on others. */
static void
licence_decrypt(const uint8_t *ct, uint32_t len, const char *pcode, uint8_t key, uint8_t bh0,
                uint8_t *out)
{
    lfsr_t  s;
    uint8_t bh = bh0;
    size_t  n  = strlen(pcode);

    for (int i = 0; i < 8; i++)
        s.reg[i] = (uint8_t) ((((size_t) i < n) ? (uint8_t) pcode[i] : 0) ^ key);
    for (int i = 0; i < 256; i++)
        lfsr_step(&s);

    for (uint32_t blk = 0; blk < (len / 8); blk++) {
        uint8_t src[8];
        uint8_t cf;

        for (int i = 0; i < 8; i++)
            src[i] = (uint8_t) (ct[(blk * 8) + i] ^ lfsr_step(&s));
        for (int j = 0; j < 8; j++) {
            uint8_t v = 0;

            for (int i = 0; i < 8; i++)
                v |= (uint8_t) (((src[i] >> j) & 1) << i);
            out[(blk * 8) + j] = (uint8_t) (v ^ bh);
        }
        cf = bh & 1;
        bh = (uint8_t) (((bh >> 1) | (cf << 7)) + cf);
    }
}

/* ------------------------------------------------------------------------ FAT16 access */

typedef struct {
    FILE    *f;
    uint32_t part;
    uint32_t tot;       /* sectors in the partition */
    uint32_t rsvd;
    uint32_t spc;
    uint32_t nfat;
    uint32_t spf;
    uint32_t fat_lba;
    uint32_t root_lba;
    uint32_t root_secs;
    uint32_t data_lba;
    uint32_t clusters;  /* one past the highest cluster number */
    uint8_t *fat;
} vol_t;

static uint16_t rd16(const uint8_t *p) { return (uint16_t) (p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t) (p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24)); }
static void     wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t) v; p[1] = (uint8_t) (v >> 8); }
static void     wr32(uint8_t *p, uint32_t v) { wr16(p, (uint16_t) v); wr16(&p[2], (uint16_t) (v >> 16)); }

/* A cabinet's disk is past 2 GB's worth of sector offsets once it is grown, so every seek
   is 64-bit. */
static int
seek_to(FILE *f, int64_t off)
{
    return _fseeki64(f, off, SEEK_SET) == 0;
}

static int
sec_read(vol_t *v, uint32_t lba, uint32_t n, uint8_t *buf)
{
    return seek_to(v->f, (int64_t) lba * SECTOR) && (fread(buf, SECTOR, n, v->f) == n);
}

static int
sec_write(vol_t *v, uint32_t lba, uint32_t n, const uint8_t *buf)
{
    return seek_to(v->f, (int64_t) lba * SECTOR) && (fwrite(buf, SECTOR, n, v->f) == n);
}

static uint16_t fat_get(const vol_t *v, uint16_t c)          { return rd16(&v->fat[c * 2]); }
static void     fat_set(vol_t *v, uint16_t c, uint16_t val)  { v->fat[c * 2] = (uint8_t) val;
                                                               v->fat[(c * 2) + 1] = (uint8_t) (val >> 8); }
static uint32_t clus_lba(const vol_t *v, uint16_t c)         { return v->data_lba + ((uint32_t) (c - 2) * v->spc); }

static int
vol_open(vol_t *v, const char *fn, int rw)
{
    uint8_t sec[SECTOR];

    memset(v, 0, sizeof(*v));
    v->f = fopen(fn, rw ? "r+b" : "rb");
    if (v->f == NULL) {
        pf_logf("cannot open %s\n", fn);
        return 0;
    }
    if (!sec_read(v, 0, 1, sec) || (rd16(&sec[510]) != 0xAA55)) {
        pf_logf("%s has no MBR\n", fn);
        return 0;
    }
    v->part = rd32(&sec[0x1BE + 8]);
    if (!v->part || !sec_read(v, v->part, 1, sec)) {
        pf_logf("no partition\n");
        return 0;
    }
    if (rd16(&sec[0x0B]) != SECTOR) {
        pf_logf("not a 512-byte-sector FAT volume\n");
        return 0;
    }
    v->tot       = rd16(&sec[0x13]) ? rd16(&sec[0x13]) : rd32(&sec[0x20]);
    v->rsvd      = rd16(&sec[0x0E]);
    v->spc       = sec[0x0D];
    v->nfat      = sec[0x10];
    v->spf       = rd16(&sec[0x16]);
    v->fat_lba   = v->part + v->rsvd;
    v->root_lba  = v->fat_lba + (v->nfat * v->spf);
    v->root_secs = ((uint32_t) rd16(&sec[0x11]) * 32) / SECTOR;
    v->data_lba  = v->root_lba + v->root_secs;
    if (!v->spc || !v->nfat || !v->spf || (v->tot <= (v->data_lba - v->part))) {
        pf_logf("bad BPB\n");
        return 0;
    }
    v->clusters = ((v->tot - (v->data_lba - v->part)) / v->spc) + 2;
    v->fat = malloc(v->spf * SECTOR);
    return (v->fat != NULL) && sec_read(v, v->fat_lba, v->spf, v->fat);
}

static void
vol_close(vol_t *v)
{
    free(v->fat);
    if (v->f)
        fclose(v->f);
}

/* Find "NAME    EXT" in a directory; dir_cl 0 means the root. */
static int64_t
dir_find(vol_t *v, uint16_t dir_cl, const char *name11, uint16_t *cl, uint32_t *size)
{
    uint8_t *buf = malloc(v->spc * SECTOR);
    int64_t  hit = -1;

    if (buf == NULL)
        return -1;
    for (int guard = 0; (guard < 64) && (hit < 0); guard++) {
        uint32_t lba  = dir_cl ? clus_lba(v, dir_cl) : v->root_lba;
        uint32_t secs = dir_cl ? v->spc : v->root_secs;

        if (!sec_read(v, lba, secs, buf))
            break;
        for (uint32_t i = 0; i < ((secs * SECTOR) / 32); i++) {
            const uint8_t *e = &buf[i * 32];

            if ((e[0] == 0x00) || (e[0] == 0xE5))
                continue;
            if (!memcmp(e, name11, 11)) {
                hit = ((int64_t) lba * SECTOR) + (i * 32);
                if (cl)
                    *cl = rd16(&e[0x1A]);
                if (size)
                    *size = rd32(&e[0x1C]);
                break;
            }
        }
        if (!dir_cl)
            break;
        dir_cl = fat_get(v, dir_cl);
        if ((dir_cl < 2) || (dir_cl >= 0xFFF0))
            break;
    }
    free(buf);
    return hit;
}

/* The slack the check reads: the tail of the last cluster, at most 512 bytes. */
static uint32_t
slack_span(const vol_t *v, uint32_t size, uint32_t *len)
{
    const uint32_t cl = v->spc * SECTOR;
    uint32_t       l  = cl - (size % cl);

    if (l > 512)
        l = 512;
    *len = l;
    return cl - l;
}

static int
relocate(vol_t *v, int64_t ent, uint16_t from, uint16_t to, uint32_t size, uint8_t fill, int dry)
{
    const uint32_t clb = v->spc * SECTOR;
    uint8_t       *buf;
    uint32_t       soff;
    uint32_t       slen;
    uint8_t        e[32];

    if (from == to)
        return 0;
    if ((to < 2) || (to >= v->clusters)) {
        pf_logf("target cluster %u is outside the volume\n", to);
        return -1;
    }
    if (fat_get(v, from) < 0xFFF8) {
        pf_logf("cluster %u is not a single-cluster file\n", from);
        return -1;
    }
    if (fat_get(v, to) != 0) {
        pf_logf("cluster %u is already in use\n", to);
        return -1;
    }
    pf_logf("  move cluster %u -> %u\n", from, to);
    if (dry)
        return 1;

    buf = malloc(clb);
    if ((buf == NULL) || !sec_read(v, clus_lba(v, from), v->spc, buf)) {
        free(buf);
        return -1;
    }
    soff = slack_span(v, size, &slen);
    memset(&buf[soff], fill, slen);
    if (!sec_write(v, clus_lba(v, to), v->spc, buf)) {
        free(buf);
        return -1;
    }
    free(buf);

    fat_set(v, to, 0xFFFF);
    fat_set(v, from, 0x0000);
    for (uint32_t n = 0; n < v->nfat; n++)
        sec_write(v, v->fat_lba + (n * v->spf), v->spf, v->fat);

    if (!seek_to(v->f, ent) || (fread(e, 1, 32, v->f) != 32))
        return -1;
    wr16(&e[0x1A], to);
    if (!seek_to(v->f, ent) || (fwrite(e, 1, 32, v->f) != 32))
        return -1;
    return 1;
}

static int
fill_slack(vol_t *v, uint16_t cl, uint32_t size, uint8_t fill, int dry)
{
    uint32_t slen;
    uint32_t soff = slack_span(v, size, &slen);
    uint32_t lba  = clus_lba(v, cl) + (soff / SECTOR);
    uint8_t  sec[SECTOR];
    int      ok = 1;

    if (!sec_read(v, lba, 1, sec))
        return -1;
    for (uint32_t i = 0; i < slen; i++)
        if (sec[(soff % SECTOR) + i] != fill) {
            ok = 0;
            break;
        }
    if (ok)
        return 0;
    pf_logf("  write %u slack bytes of %02X at cluster %u\n", slen, fill, cl);
    if (dry)
        return 1;
    memset(&sec[soff % SECTOR], fill, slen);
    return sec_write(v, lba, 1, sec) ? 1 : -1;
}

/* ---------------------------------------------------------------- growing the volume */

/* The clusters CCMOVE recorded are the cabinet disk's.  An image restored onto a smaller
   disk has a volume that simply ends before them, so it is grown back to the cabinet's
   size.  Cluster numbers are what the protection checks, so they must not change: the
   cluster size stays, the FAT gets longer, and the root directory and the data move down
   behind it as one block. */
typedef struct {
    uint32_t tot;       /* partition sectors */
    uint32_t spf;
    uint32_t clusters;
} grow_t;

static int
grow_plan(const vol_t *v, grow_t *g)
{
    uint32_t spf = v->spf;
    uint32_t n;

    if ((v->part + v->tot) >= CABINET_SECS)
        return 0;
    g->tot = CABINET_SECS - v->part;
    for (;;) {
        n = (g->tot - v->rsvd - (v->nfat * spf) - v->root_secs) / v->spc;
        if (((n + 2) * 2) <= (spf * SECTOR))
            break;
        spf++;
    }
    if ((n + 2) > 0xFFF0)
        return 0;   /* it would stop being FAT16 */
    g->spf      = spf;
    g->clusters = n + 2;
    return 1;
}

/* For the dry run: make the in-memory FAT look like the grown one, so relocate() can
   check its targets against it.  Never written back. */
static int
vol_widen(vol_t *v, const grow_t *g)
{
    uint8_t *fat = realloc(v->fat, g->spf * SECTOR);

    if (fat == NULL)
        return 0;
    memset(&fat[v->clusters * 2], 0, (g->spf * SECTOR) - (v->clusters * 2));
    v->fat      = fat;
    v->spf      = g->spf;
    v->clusters = g->clusters;
    return 1;
}

/* Write the grown image beside the original and swap it in only once it is complete, so
   a failure part way leaves the original untouched. */
static int
grow_image(const char *path, const grow_t *g)
{
    const uint32_t chunk = 2048;
    char           tmp[MAX_PATH + 16] = "";
    vol_t          v;
    FILE          *out  = NULL;
    uint8_t       *buf  = NULL;
    uint8_t       *fat  = NULL;
    uint32_t       done = 0;
    uint32_t       lba;
    uint32_t       end;
    int            ok = 0;

    if (!vol_open(&v, path, 0))
        goto fail;
    snprintf(tmp, sizeof(tmp), "%s.ppfix-tmp", path);
    out = fopen(tmp, "wb");
    buf = malloc(chunk * SECTOR);
    fat = calloc(g->spf, SECTOR);
    if ((out == NULL) || (buf == NULL) || (fat == NULL) || (v.fat_lba > chunk)) {
        pf_logf("cannot create %s\n", tmp);
        goto fail;
    }

    /* The MBR track, the boot sector and any reserved sectors, with the partition and the
       BPB told about the new size. */
    if (!sec_read(&v, 0, v.fat_lba, buf))
        goto fail;
    wr32(&buf[0x1BE + 12], g->tot);
    wr16(&buf[(v.part * SECTOR) + 0x13], 0);
    wr16(&buf[(v.part * SECTOR) + 0x16], (uint16_t) g->spf);
    wr32(&buf[(v.part * SECTOR) + 0x20], g->tot);
    if (fwrite(buf, SECTOR, v.fat_lba, out) != v.fat_lba)
        goto fail;
    done += v.fat_lba;

    /* Every FAT copy: the old chain entries, then free space. */
    memcpy(fat, v.fat, v.clusters * 2);
    for (uint32_t n = 0; n < v.nfat; n++) {
        if (fwrite(fat, SECTOR, g->spf, out) != g->spf)
            goto fail;
        done += g->spf;
    }

    /* The root directory and the data, unchanged, now further in. */
    end = v.part + v.tot;
    for (lba = v.root_lba; lba < end; lba += chunk) {
        const uint32_t n = ((end - lba) < chunk) ? (end - lba) : chunk;

        if (!sec_read(&v, lba, n, buf) || (fwrite(buf, SECTOR, n, out) != n)) {
            pf_logf("copy failed at sector %u\n", lba);
            goto fail;
        }
        done += n;
    }

    /* And the new free clusters. */
    memset(buf, 0, chunk * SECTOR);
    while (done < CABINET_SECS) {
        const uint32_t n = ((CABINET_SECS - done) < chunk) ? (CABINET_SECS - done) : chunk;

        if (fwrite(buf, SECTOR, n, out) != n) {
            pf_logf("cannot write %s -- is the disk full?\n", tmp);
            goto fail;
        }
        done += n;
    }
    ok = (fflush(out) == 0);

fail:
    if (out != NULL)
        ok = (fclose(out) == 0) && ok;
    vol_close(&v);
    free(buf);
    free(fat);
    if (ok && !MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        pf_logf("cannot replace %s\n", path);
        ok = 0;
    }
    if (!ok && (tmp[0] != '\0'))
        DeleteFileA(tmp);
    return ok;
}

/* --------------------------------------------------------------------------- the marker */

static void
marker_write(vol_t *v, uint16_t sys_cl, uint16_t ccc_cl, uint8_t slack)
{
    uint8_t sec[SECTOR];

    memset(sec, 0, sizeof(sec));
    memcpy(sec, MARKER_MAGIC, 8);
    sec[8]  = (uint8_t) sys_cl;
    sec[9]  = (uint8_t) (sys_cl >> 8);
    sec[10] = (uint8_t) ccc_cl;
    sec[11] = (uint8_t) (ccc_cl >> 8);
    sec[12] = slack;
    snprintf((char *) &sec[16], 200,
             "Photo Play 2.0 CopyControl layout repaired by ppfix. "
             "CCONTROL.SYS=%u PP2000.CCC=%u slack=%02X", sys_cl, ccc_cl, slack);
    sec_write(v, MARKER_LBA, 1, sec);
}

/* ------------------------------------------------------------------------- scan and fix */

typedef enum {
    ST_ERROR = 0,
    ST_NOT20,
    ST_UNFIXED,
    ST_FIXED
} state_t;

typedef struct {
    state_t  state;
    char     path[MAX_PATH];
    uint16_t cl_sys;      /* where the two files are now */
    uint16_t cl_ccc;
    uint32_t sz_sys;
    uint32_t sz_ccc;
    int64_t  ent_sys;     /* their directory entries, as file offsets */
    int64_t  ent_ccc;
    uint16_t want_sys;    /* where the protection expects them */
    uint16_t want_ccc;
    uint8_t  slack;
    const char *sys_from; /* where want_sys came from */
    int      slk_default;
    int      marker;      /* PeepeeBox's marker is already in LBA 1 */
    int      changes;     /* what a repair would have to do */
    int      grow;        /* the volume has to reach the cabinet disk's size first */
    uint32_t clus_now;
    grow_t   gp;
    char     install[80];
} scan_t;

/* The two overridable values.  -1 means "read it from the image"; the Advanced dialog
   and the command line can pin either. */
static long g_opt_sys = -1;
static int  g_opt_slk = -1;

/* Open the volume, find CopyControl's two files and read out of the licence where
   PP2000.CCC belongs.  Shared by the scan and the repair so both see the same image. */
static int
locate(vol_t *v, scan_t *s, int rw)
{
    uint16_t dir_exe = 0;
    uint16_t dir_cc  = 0;
    uint8_t  raw[0x800];
    uint8_t  lic[0x5F0];

    if (!vol_open(v, s->path, rw)) {
        s->state = ST_ERROR;
        return 0;
    }
    if ((dir_find(v, 0, "EXE        ", &dir_exe, NULL) < 0) ||
        (dir_find(v, dir_exe, "PP2000  081", &dir_cc, NULL) < 0)) {
        s->state = ST_NOT20;
        return 0;
    }
    s->ent_sys = dir_find(v, dir_cc, "CCONTROLSYS", &s->cl_sys, &s->sz_sys);
    s->ent_ccc = dir_find(v, dir_cc, "PP2000  CCC", &s->cl_ccc, &s->sz_ccc);
    if ((s->ent_sys < 0) || (s->ent_ccc < 0)) {
        s->state = ST_NOT20;
        return 0;
    }

    /* The licence says outright which cluster PP2000.CCC has to start at; the key is the
       file's own name and a byte kept in the clear beside it. */
    if (!sec_read(v, clus_lba(v, s->cl_ccc), 4, raw)) {
        pf_logf("cannot read PP2000.CCC\n");
        s->state = ST_ERROR;
        return 0;
    }
    licence_decrypt(&raw[0x10], sizeof(lic), "PP2000", raw[8], 0x35, lic);
    s->want_ccc = rd16(&lic[0x12]);
    if ((memcmp(lic, "PP2000", 6) != 0) || (s->want_ccc < 2) || (s->want_ccc > 0xFFEF)) {
        pf_logf("the licence did not decrypt (unexpected PCODE or key)\n");
        s->state = ST_ERROR;
        return 0;
    }

    /* CCONTROL.SYS says where it has to be itself.  Should that word ever be nonsense,
       fall back on the layout CCMOVE wrote on every install seen: two clusters below. */
    if (!sec_read(v, clus_lba(v, s->cl_sys), 1, raw)) {
        pf_logf("cannot read CCONTROL.SYS\n");
        s->state = ST_ERROR;
        return 0;
    }
    s->want_sys = (uint16_t) (rd16(&raw[6]) ^ SYS_XOR);
    s->sys_from = "from CCONTROL.SYS";
    if (g_opt_sys >= 0) {
        s->want_sys = (uint16_t) g_opt_sys;
        s->sys_from = "set by hand";
    } else if ((s->want_sys < 2) || (s->want_sys > 0xFFEF) || (s->want_sys == s->want_ccc)) {
        s->want_sys = (uint16_t) (s->want_ccc - 2);
        s->sys_from = "assumed";
    }
    s->slack       = (g_opt_slk >= 0) ? (uint8_t) g_opt_slk : 0x5A;
    s->slk_default = (g_opt_slk < 0);

    /* Recorded on a bigger disk than this image is? */
    s->clus_now = v->clusters;
    s->grow     = (s->want_sys >= v->clusters) || (s->want_ccc >= v->clusters);
    if (s->grow && (!grow_plan(v, &s->gp) || (s->want_sys >= s->gp.clusters) ||
                    (s->want_ccc >= s->gp.clusters))) {
        pf_logf("the protection wants clusters %u and %u, but this volume ends at %u and "
                "cannot be grown far enough\n", s->want_sys, s->want_ccc, v->clusters - 1);
        s->state = ST_ERROR;
        return 0;
    }

    for (size_t i = 0; i < (sizeof(s->install) - 1); i++) {
        const uint8_t c = lic[0x17 + i];

        s->install[i] = (c >= 0x20) ? (char) c : '\0';
        if (s->install[i] == '\0')
            break;
    }
    return 1;
}

/* Look, and say what a repair would do.  Nothing is written: the dry run exists so the
   window can show the change before the user asks for it, and so a target cluster that
   is already occupied turns into a message rather than a half-repaired image. */
static void
scan_image(scan_t *s)
{
    char    keep[MAX_PATH];
    vol_t   v;
    uint8_t sec[SECTOR];
    int     r1;
    int     r2;

    strcpy(keep, s->path);
    memset(s, 0, sizeof(*s));
    strcpy(s->path, keep);
    pf_clear();

    if (!locate(&v, s, 0)) {
        vol_close(&v);
        return;
    }
    if (sec_read(&v, MARKER_LBA, 1, sec) && !memcmp(sec, MARKER_MAGIC, 8))
        s->marker = 1;

    if (s->grow) {
        pf_logf("  grow the volume from %u to %u clusters\n", s->clus_now - 2,
                s->gp.clusters - 2);
        if (!vol_widen(&v, &s->gp)) {
            vol_close(&v);
            s->state = ST_ERROR;
            return;
        }
    }
    r1 = relocate(&v, s->ent_sys, s->cl_sys, s->want_sys, s->sz_sys, s->slack, 1);
    r2 = relocate(&v, s->ent_ccc, s->cl_ccc, s->want_ccc, s->sz_ccc, s->slack, 1);
    if ((r1 < 0) || (r2 < 0)) {
        vol_close(&v);
        s->state = ST_ERROR;
        return;
    }
    s->changes = s->grow + (r1 > 0) + (r2 > 0);
    if (!s->grow) {
        s->changes += (fill_slack(&v, s->want_ccc, s->sz_ccc, s->slack, 1) > 0);
        s->changes += (fill_slack(&v, s->want_sys, s->sz_sys, s->slack, 1) > 0);
    }
    vol_close(&v);

    /* Repaired means both: the layout the protection wants, and the marker that tells
       PeepeeBox it does not need to warn about this image. */
    s->state = (!s->changes && s->marker) ? ST_FIXED : ST_UNFIXED;
}

static int
fix_image(scan_t *s)
{
    vol_t v;
    int   ok = 0;

    pf_clear();
    if (!locate(&v, s, 0)) {
        vol_close(&v);
        return 0;
    }
    vol_close(&v);
    if (s->grow) {
        if (!grow_image(s->path, &s->gp)) {
            pf_logf("the volume could not be grown; the image is unchanged\n");
            return 0;
        }
        pf_logf("  volume grown from %u to %u clusters; the image is now " CABINET_SIZE
                " bytes\n", s->clus_now - 2, s->gp.clusters - 2);
    }
    if (!locate(&v, s, 1)) {
        vol_close(&v);
        return 0;
    }
    if ((relocate(&v, s->ent_sys, s->cl_sys, s->want_sys, s->sz_sys, s->slack, 0) >= 0) &&
        (relocate(&v, s->ent_ccc, s->cl_ccc, s->want_ccc, s->sz_ccc, s->slack, 0) >= 0)) {
        fill_slack(&v, s->want_ccc, s->sz_ccc, s->slack, 0);
        fill_slack(&v, s->want_sys, s->sz_sys, s->slack, 0);
        marker_write(&v, s->want_sys, s->want_ccc, s->slack);
        pf_logf("  marker written to LBA %d\n", MARKER_LBA);
        ok = 1;
    } else
        pf_logf("repair aborted; the image may be partly changed\n");
    vol_close(&v);
    return ok;
}

/* ------------------------------------------------------------------------------ the GUI */

static HINSTANCE g_inst;
static HFONT     g_title_font;
static scan_t    g_scan;

/* The image to start on: one named on the command line, else HardDisk.img beside the
   executable -- which is how it arrives, sitting next to PeepeeBox.exe in a cabinet
   folder -- else the one the user picks. */
static int
default_image(char *out, size_t sz)
{
    char  dir[MAX_PATH];
    char *p;

    if (GetModuleFileNameA(NULL, dir, sizeof(dir)) == 0)
        return 0;
    p = strrchr(dir, '\\');
    if (p == NULL)
        return 0;
    p[1] = '\0';
    if ((strlen(dir) + sizeof("HardDisk.img")) > sz)
        return 0;
    snprintf(out, sz, "%sHardDisk.img", dir);
    if (GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES)
        return 1;
    out[0] = '\0';
    return 0;
}

static int
browse(HWND owner, char *path, size_t sz)
{
    OPENFILENAMEA ofn;
    char          buf[MAX_PATH] = "";

    if (path[0] != '\0')
        snprintf(buf, sizeof(buf), "%s", path);

    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = owner;
    ofn.lpstrFilter = "Disk images (*.img)\0*.img\0All files (*.*)\0*.*\0";
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = sizeof(buf);
    ofn.lpstrTitle  = "Select a Photo Play hard disk image";
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameA(&ofn))
        return 0;
    snprintf(path, sz, "%s", buf);
    return 1;
}

static void
detail_text(const scan_t *s, char *out, size_t sz)
{
    switch (s->state) {
        case ST_ERROR:
            snprintf(out, sz, "%s", pf_msg[0] ? pf_msg : "The image could not be read.");
            break;

        case ST_NOT20:
            snprintf(out, sz,
                     "There is no EXE\\PP2000.081 directory in this image, so it carries no "
                     "CopyControl install. Nothing here needs fixing and nothing will be "
                     "written.");
            break;

        case ST_FIXED:
            snprintf(out, sz,
                     "CCONTROL.SYS is at cluster %u and PP2000.CCC at %u, with %02X in the "
                     "slack past its end -- which is what the protection asks for.\r\n"
                     "PeepeeBox will run this image's games.",
                     s->cl_sys, s->cl_ccc, s->slack);
            break;

        default:
            if (s->changes == 0)
                snprintf(out, sz,
                         "The layout is already right, but PeepeeBox's marker is missing, so "
                         "it cannot tell and will warn about this image.\r\n"
                         "Fixing it writes the marker and changes nothing else.");
            else {
                char grow[160] = "";

                if (s->grow)
                    snprintf(grow, sizeof(grow),
                             "The volume ends at cluster %u, so the image is first grown "
                             "to the cabinet's " CABINET_SIZE "-byte disk.\r\n",
                             s->clus_now - 1);
                snprintf(out, sz,
                         "%sCCONTROL.SYS  cluster %u  ->  %u  (%s)\r\n"
                         "PP2000.CCC    cluster %u  ->  %u  (from the licence, install "
                         "path %s)\r\n"
                         "slack fill %02X%s",
                         grow, s->cl_sys, s->want_sys, s->sys_from,
                         s->cl_ccc, s->want_ccc, s->install,
                         s->slack, s->slk_default ? "   (default)" : "");
            }
            break;
    }
}

static void
refresh(HWND h)
{
    static const char *const status[] = {
        "The image could not be read",
        "Not a Photo Play 2.0 image -- nothing to do",
        "Unfixed -- the games will drop back to the menu until this is repaired",
        "Fixed"
    };
    char detail[sizeof(pf_msg) + 512];

    SetDlgItemTextA(h, IDC_FILE, g_scan.path[0] ? g_scan.path : "(no image selected)");
    SetDlgItemTextA(h, IDC_STATUS, g_scan.path[0] ? status[g_scan.state] : "");
    if (g_scan.path[0] != '\0') {
        detail_text(&g_scan, detail, sizeof(detail));
        SetDlgItemTextA(h, IDC_DETAIL, detail);
    } else
        SetDlgItemTextA(h, IDC_DETAIL, "Pick a hard disk image to look at.");

    /* The two overrides only mean anything once the licence has been read. */
    EnableWindow(GetDlgItem(h, IDC_FIX), g_scan.state == ST_UNFIXED);
    EnableWindow(GetDlgItem(h, IDC_ADV_BTN),
                 (g_scan.state == ST_UNFIXED) || (g_scan.state == ST_FIXED));
    InvalidateRect(h, NULL, TRUE);
}

static INT_PTR CALLBACK
adv_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    char t[16];
    char info[192];

    (void) lp;
    switch (msg) {
        case WM_INITDIALOG:
            snprintf(t, sizeof(t), "%u", (unsigned) g_scan.want_sys);
            SetDlgItemTextA(h, IDC_ADV_SYS, t);
            snprintf(t, sizeof(t), "%02X", g_scan.slack);
            SetDlgItemTextA(h, IDC_ADV_SLK, t);
            snprintf(info, sizeof(info),
                     "PP2000.CCC goes to cluster %u -- that one is read out of the "
                     "licence.\nKnown fill bytes: FF E5 1A F6 20 5A A5 FF.",
                     (unsigned) g_scan.want_ccc);
            SetDlgItemTextA(h, IDC_ADV_CCC, info);
            return TRUE;

        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_ADV_DEF:
                    /* Back to reading it from the image. */
                    g_opt_sys = -1;
                    g_opt_slk = -1;
                    EndDialog(h, 1);
                    return TRUE;

                case IDOK: {
                    long cl;
                    long fill;

                    GetDlgItemTextA(h, IDC_ADV_SYS, t, sizeof(t));
                    cl = strtol(t, NULL, 10);
                    GetDlgItemTextA(h, IDC_ADV_SLK, t, sizeof(t));
                    fill = strtol(t, NULL, 16);
                    if ((cl < 2) || (cl > 0xFFEF) || (fill < 0) || (fill > 0xFF)) {
                        MessageBoxA(h, "The cluster has to be between 2 and 65519, and the "
                                       "fill byte a hex value between 00 and FF.",
                                    "Advanced", MB_OK | MB_ICONWARNING);
                        return TRUE;
                    }
                    g_opt_sys = cl;
                    g_opt_slk = (int) fill;
                    EndDialog(h, 1);
                    return TRUE;
                }

                case IDCANCEL:
                    EndDialog(h, 0);
                    return TRUE;

                default:
                    break;
            }
            break;

        default:
            break;
    }
    return FALSE;
}

static INT_PTR CALLBACK
main_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
        case WM_INITDIALOG: {
            HFONT   base = (HFONT) SendMessageA(h, WM_GETFONT, 0, 0);
            LOGFONTA lf;

            if ((base != NULL) && GetObjectA(base, sizeof(lf), &lf)) {
                lf.lfWeight = FW_BOLD;
                lf.lfHeight = (lf.lfHeight * 7) / 5;
                g_title_font = CreateFontIndirectA(&lf);
                if (g_title_font != NULL)
                    SendDlgItemMessageA(h, IDC_TITLE, WM_SETFONT, (WPARAM) g_title_font, TRUE);
            }
            SendMessageA(h, WM_SETICON, ICON_BIG, (LPARAM) LoadImageA(g_inst,
                         MAKEINTRESOURCEA(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                         GetSystemMetrics(SM_CYICON), 0));
            SendMessageA(h, WM_SETICON, ICON_SMALL, (LPARAM) LoadImageA(g_inst,
                         MAKEINTRESOURCEA(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                         GetSystemMetrics(SM_CYSMICON), 0));

            /* Settle the window first, then go looking: if there is no image to work on
               the file dialog should come up over a window that is already drawn. */
            PostMessageA(h, WM_APP, 0, 0);
            return TRUE;
        }

        case WM_APP:
            if (g_scan.path[0] == '\0')
                browse(h, g_scan.path, sizeof(g_scan.path));
            if (g_scan.path[0] != '\0')
                scan_image(&g_scan);
            refresh(h);
            return TRUE;

        case WM_CTLCOLORSTATIC: {
            static const COLORREF col[] = {
                RGB(0xB0, 0x30, 0x20), /* error   */
                RGB(0x60, 0x60, 0x60), /* not 2.0 */
                RGB(0xC0, 0x60, 0x00), /* unfixed */
                RGB(0x10, 0x80, 0x30)  /* fixed   */
            };
            const HWND c  = (HWND) lp;
            COLORREF   fg = GetSysColor(COLOR_WINDOWTEXT);

            if (c == GetDlgItem(h, IDC_STATUS))
                fg = col[g_scan.state];
            else if ((c == GetDlgItem(h, IDC_DETAIL)) || (c == GetDlgItem(h, IDC_COPY)))
                fg = RGB(0x60, 0x60, 0x60);
            SetTextColor((HDC) wp, fg);
            SetBkMode((HDC) wp, TRANSPARENT);
            return (INT_PTR) GetSysColorBrush(COLOR_BTNFACE);
        }

        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_BROWSE:
                    if (browse(h, g_scan.path, sizeof(g_scan.path))) {
                        scan_image(&g_scan);
                        refresh(h);
                    }
                    return TRUE;

                case IDC_ADV_BTN:
                    if (DialogBoxA(g_inst, MAKEINTRESOURCEA(IDD_ADV), h, adv_proc) == 1) {
                        scan_image(&g_scan);
                        refresh(h);
                    }
                    return TRUE;

                case IDC_FIX: {
                    /* Growing an image copies all of it, which takes a while. */
                    const HCURSOR was = SetCursor(LoadCursor(NULL, IDC_WAIT));
                    const int     ok  = fix_image(&g_scan);
                    char          msg[2048];

                    SetCursor(was);

                    if (ok)
                        snprintf(msg, sizeof(msg),
                                 "The image is repaired.\n\n%s\nIts games will start now.",
                                 pf_msg);
                    else
                        snprintf(msg, sizeof(msg),
                                 "The image was not repaired.\n\n%s\nIf the image is in "
                                 "use, close PeepeeBox and try again.", pf_msg);
                    scan_image(&g_scan);
                    refresh(h);
                    MessageBoxA(h, msg, "Photo Play 2.0 HDD Image Fixer",
                                MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONWARNING));
                    return TRUE;
                }

                case IDCANCEL:
                    EndDialog(h, 0);
                    return TRUE;

                default:
                    break;
            }
            break;

        case WM_CLOSE:
            EndDialog(h, 0);
            return TRUE;

        default:
            break;
    }
    return FALSE;
}

/* An image can be named on the command line, and the two values the Advanced dialog holds
   can be pinned there too -- which is how a install nobody has seen yet gets repaired
   without rebuilding the tool. */
static int g_headless;

static void
parse_cmdline(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--ccontrol") && ((i + 1) < argc))
            g_opt_sys = strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--slack") && ((i + 1) < argc))
            g_opt_slk = (int) strtol(argv[++i], NULL, 16);
        else if (!strcmp(argv[i], "--fix"))
            g_headless = 1;
        else if (argv[i][0] != '-')
            snprintf(g_scan.path, sizeof(g_scan.path), "%s", argv[i]);
    }
}

/* --fix: repair without the window, for scripts and for a batch of images.  What it has
   to say goes to the console it was started from, if any; the exit code is 0 for an image
   that is repaired or already was, 1 for one that is not 2.0, 2 for a failure. */
static int
run_headless(void)
{
    static const char *const state[] = { "error", "not a Photo Play 2.0 image", "unfixed",
                                         "fixed" };
    char  detail[sizeof(pf_msg) + 512];
    FILE *con = NULL;
    int   rc;

    /* Output redirected to a file or pipe reaches a window program too; otherwise borrow
       the console it was started from. */
    if (GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) != FILE_TYPE_UNKNOWN)
        con = stdout;
    else if (AttachConsole(ATTACH_PARENT_PROCESS))
        con = fopen("CONOUT$", "w");

    scan_image(&g_scan);
    detail_text(&g_scan, detail, sizeof(detail));
    if (con != NULL)
        fprintf(con, "%s: %s\n%s\n", g_scan.path, state[g_scan.state], detail);

    if (g_scan.state == ST_UNFIXED) {
        const int ok = fix_image(&g_scan);

        if (con != NULL)
            fprintf(con, "%s%s\n", pf_msg, ok ? "repaired" : "NOT repaired");
        scan_image(&g_scan);
    }
    rc = (g_scan.state == ST_FIXED) ? 0 : (g_scan.state == ST_NOT20) ? 1 : 2;
    if (con != NULL) {
        fprintf(con, "%s: %s\n", g_scan.path, state[g_scan.state]);
        if (con == stdout)
            fflush(con);
        else
            fclose(con);
    }
    return rc;
}

int WINAPI
WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    (void) prev;
    (void) cmd;
    (void) show;

    g_inst = inst;
    parse_cmdline(__argc, __argv);
    if (g_scan.path[0] == '\0')
        default_image(g_scan.path, sizeof(g_scan.path));
    if (g_headless)
        return (g_scan.path[0] != '\0') ? run_headless() : 2;

    InitCommonControls();
    DialogBoxA(inst, MAKEINTRESOURCEA(IDD_MAIN), NULL, main_proc);

    if (g_title_font != NULL)
        DeleteObject(g_title_font);
    return 0;
}
