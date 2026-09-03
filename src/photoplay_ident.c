/*
 * PeepeeBox   A fork of 86Box that emulates the funworld Photo Play / I.G.O.
 *             arcade kiosk hardware, including its two protection tokens.
 *
 *             Work out which release and territory a disk image is, so the
 *             window can say "IGO 5 PT" instead of whatever the working
 *             directory happens to be called.
 *
 *             The cabinets record this in \FOTO\SETTINGS\MAIN.SET, which is
 *             the authoritative source -- filenames and folder names lie (one
 *             image in circulation is filed as a 1998 Spanish build and its
 *             MAIN.SET says "Version 2003 (ES)").  The file is encrypted, but
 *             with a fixed key rather than a dongle-derived one, so it can be
 *             read with nothing but the image:
 *
 *                 three 16-bit words   count, size of the key block, size of
 *                                      the string pool
 *                 size1 bytes          entries: NUL-terminated key, then a
 *                                      16-bit offset into the pool
 *                 size2 bytes          the pool
 *
 *             Every one of those five reads restarts the keystream from the
 *             same seed, because the game seeds the PRNG per read and restores
 *             it afterwards.  The cipher is the Borland/Turbo Pascal LCG the
 *             rest of this software leans on:
 *
 *                 s = s * 0x08088405 + 1;   keystream byte = s >> 24
 *
 *             Reaching the file means walking the image by hand: MBR, FAT16
 *             BPB, then the root directory.  All read-only, all bounds-checked
 *             -- a malformed or foreign image must fail quietly, not crash the
 *             emulator before it has drawn a window.
 *
 * Authors:    The HUEG PP team.
 *
 *             Released under the GNU General Public License version 2 or
 *             later.  See COPYING for more information.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/plat.h>
#include <86box/photoplay.h>

#define SET_KEY  0x00016295
#define SECSZ    512

typedef struct {
    FILE    *f;
    uint64_t part_off;   /* byte offset of the partition */
    uint32_t bps;
    uint32_t spc;
    uint64_t root_off;   /* byte offset of the root directory */
    uint32_t root_ents;
    uint64_t data_off;   /* byte offset of cluster 2 */
    uint64_t fat_off;
    uint32_t clusters;
} pp_fat_t;

static int
pp_read_at(FILE *f, uint64_t off, void *buf, size_t len)
{
    if (fseeko64(f, (off64_t) off, SEEK_SET))
        return 0;
    return fread(buf, 1, len, f) == len;
}

static uint16_t
rd16(const uint8_t *p)
{
    return (uint16_t) (p[0] | (p[1] << 8));
}

static uint32_t
rd32(const uint8_t *p)
{
    return (uint32_t) (p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t) p[3] << 24));
}

/* Open the image and work out where the FAT16 volume's pieces live. */
static int
pp_fat_open(pp_fat_t *fs, const char *path)
{
    uint8_t sec[SECSZ];

    memset(fs, 0, sizeof(pp_fat_t));
    fs->f = plat_fopen64(path, "rb");
    if (fs->f == NULL)
        return 0;

    if (!pp_read_at(fs->f, 0, sec, SECSZ))
        goto fail;

    /* An MBR is not guaranteed -- accept a bare volume too. */
    fs->part_off = 0;
    if ((sec[510] == 0x55) && (sec[511] == 0xAA)) {
        for (int i = 0; i < 4; i++) {
            const uint8_t *e    = &sec[0x1BE + (i * 16)];
            const uint8_t  type = e[4];

            if ((type == 0x04) || (type == 0x06) || (type == 0x0E) || (type == 0x01)) {
                fs->part_off = (uint64_t) rd32(&e[8]) * SECSZ;
                break;
            }
        }
    }

    if (!pp_read_at(fs->f, fs->part_off, sec, SECSZ))
        goto fail;

    fs->bps = rd16(&sec[0x0B]);
    fs->spc = sec[0x0D];
    const uint32_t rsvd    = rd16(&sec[0x0E]);
    const uint32_t nfats   = sec[0x10];
    fs->root_ents          = rd16(&sec[0x11]);
    const uint32_t fatsz   = rd16(&sec[0x16]);
    uint32_t       totsec  = rd16(&sec[0x13]);

    if (totsec == 0)
        totsec = rd32(&sec[0x20]);

    /* Only the shape these cabinets actually use. */
    if ((fs->bps != SECSZ) || (fs->spc == 0) || (rsvd == 0) || (nfats == 0) ||
        (fs->root_ents == 0) || (fatsz == 0) || (totsec == 0))
        goto fail;

    const uint32_t root_secs = ((fs->root_ents * 32) + (fs->bps - 1)) / fs->bps;

    fs->fat_off  = fs->part_off + ((uint64_t) rsvd * fs->bps);
    fs->root_off = fs->fat_off + ((uint64_t) nfats * fatsz * fs->bps);
    fs->data_off = fs->root_off + ((uint64_t) root_secs * fs->bps);
    fs->clusters = (totsec - rsvd - (nfats * fatsz) - root_secs) / fs->spc;
    return 1;

fail:
    fclose(fs->f);
    fs->f = NULL;
    return 0;
}

static uint16_t
pp_fat_next(pp_fat_t *fs, uint16_t clus)
{
    uint8_t e[2];

    if (!pp_read_at(fs->f, fs->fat_off + ((uint64_t) clus * 2), e, 2))
        return 0xFFFF;
    return rd16(e);
}

/* Read a whole cluster chain, capped so a corrupt FAT cannot make us allocate
   without bound. */
static uint8_t *
pp_read_chain(pp_fat_t *fs, uint16_t clus, uint32_t size, uint32_t cap)
{
    if ((size == 0) || (size > cap))
        return NULL;

    const uint32_t csz = fs->spc * fs->bps;
    uint8_t       *buf = malloc(size);
    uint32_t       got = 0;
    int            guard = 0;

    if (buf == NULL)
        return NULL;

    while ((got < size) && (clus >= 2) && (clus < 0xFFF0) && (guard++ < 65536)) {
        const uint32_t want = ((size - got) < csz) ? (size - got) : csz;

        if (!pp_read_at(fs->f, fs->data_off + ((uint64_t) (clus - 2) * csz), buf + got, want)) {
            free(buf);
            return NULL;
        }
        got += want;
        clus = pp_fat_next(fs, clus);
    }

    if (got < size) {
        free(buf);
        return NULL;
    }
    return buf;
}

/* Find `name83` (11 bytes, space padded, upper case) in a directory.  `clus` of
   zero means the fixed root directory. */
static int
pp_dir_find(pp_fat_t *fs, uint16_t clus, const char *name83,
            uint16_t *out_clus, uint32_t *out_size, int *out_isdir)
{
    const uint32_t csz  = fs->spc * fs->bps;
    uint8_t        ent[32];
    int            guard = 0;

    if (clus == 0) {
        for (uint32_t i = 0; i < fs->root_ents; i++) {
            if (!pp_read_at(fs->f, fs->root_off + ((uint64_t) i * 32), ent, 32))
                return 0;
            if (ent[0] == 0x00)
                return 0;
            if ((ent[0] == 0xE5) || (ent[11] == 0x0F))
                continue;
            if (!memcmp(ent, name83, 11)) {
                *out_clus  = rd16(&ent[26]);
                *out_size  = rd32(&ent[28]);
                *out_isdir = !!(ent[11] & 0x10);
                return 1;
            }
        }
        return 0;
    }

    while ((clus >= 2) && (clus < 0xFFF0) && (guard++ < 65536)) {
        for (uint32_t i = 0; i < (csz / 32); i++) {
            const uint64_t off = fs->data_off + ((uint64_t) (clus - 2) * csz) + (i * 32);

            if (!pp_read_at(fs->f, off, ent, 32))
                return 0;
            if (ent[0] == 0x00)
                return 0;
            if ((ent[0] == 0xE5) || (ent[11] == 0x0F))
                continue;
            if (!memcmp(ent, name83, 11)) {
                *out_clus  = rd16(&ent[26]);
                *out_size  = rd32(&ent[28]);
                *out_isdir = !!(ent[11] & 0x10);
                return 1;
            }
        }
        clus = pp_fat_next(fs, clus);
    }
    return 0;
}

/* dst[i] ^= keystream(seed)[i], the keystream restarting at `seed` every call */
static void
pp_set_decrypt(uint8_t *dst, uint32_t len, uint32_t seed)
{
    uint32_t s = seed;

    for (uint32_t i = 0; i < len; i++) {
        s = (uint32_t) ((s * 0x08088405u) + 1u);
        dst[i] ^= (uint8_t) (s >> 24);
    }
}

/* Pull one key's value out of a decrypted MAIN.SET. */
static int
pp_set_lookup(const uint8_t *raw, uint32_t len, const char *key, char *out, size_t outsz)
{
    uint8_t hdr[6];

    if (len < 6)
        return 0;
    memcpy(hdr, raw, 6);
    pp_set_decrypt(&hdr[0], 2, SET_KEY);
    pp_set_decrypt(&hdr[2], 2, SET_KEY);
    pp_set_decrypt(&hdr[4], 2, SET_KEY);

    const uint32_t count = rd16(&hdr[0]);
    const uint32_t size1 = rd16(&hdr[2]);
    const uint32_t size2 = rd16(&hdr[4]);

    if ((size1 == 0) || (size2 == 0) || ((uint64_t) 6 + size1 + size2 > len))
        return 0;

    uint8_t *keys = malloc(size1);
    uint8_t *pool = malloc(size2);
    int      found = 0;

    if ((keys == NULL) || (pool == NULL))
        goto done;

    memcpy(keys, raw + 6, size1);
    memcpy(pool, raw + 6 + size1, size2);
    pp_set_decrypt(keys, size1, SET_KEY);
    pp_set_decrypt(pool, size2, SET_KEY);

    uint32_t p = 0;
    for (uint32_t n = 0; (n < count) && (p < size1); n++) {
        const uint32_t start = p;

        while ((p < size1) && keys[p])
            p++;
        if ((p + 3) > size1)
            break;

        const uint32_t off = rd16(&keys[p + 1]);

        if (!strcmp((const char *) &keys[start], key) && (off < size2)) {
            size_t i = 0;

            while (((off + i) < size2) && pool[off + i] && (i < (outsz - 1))) {
                out[i] = (char) pool[off + i];
                i++;
            }
            out[i] = '\0';
            found  = 1;
            break;
        }
        p += 3;
    }

done:
    free(keys);
    free(pool);
    return found;
}

/* "Version 2005B" -> "IGO 5".  The mapping is from images whose MAIN.SET was
   read directly; where no image was available the year pattern is followed. */
static const struct {
    const char *banner;
    const char *display;
} pp_release_names[] = {
    { "Version 99",    "Photo Play 99"   },
    { "Version 2000",  "Photo Play 2000" },
    { "Version 2001",  "IGO 1"           },
    { "Version 2002",  "IGO 2"           },
    { "Version 2003",  "IGO 3"           },
    { "Version 2004",  "IGO 4"           },
    { "Version 2005B", "IGO 5"           },
    { "Version 2005",  "IGO 5"           },
    { "Version 2006",  "IGO 6"           },
    { "Version 2007",  "IGO 7"           },
    { "Version 2008",  "IGO 8"           },
};

int
photoplay_identify_ex(const char *img_path, char *out, size_t outsz,
                      char *banner_out, size_t bsz, char *terr_out, size_t tsz)
{
    pp_fat_t fs;
    uint16_t clus;
    uint32_t size;
    int      isdir;

    if ((out == NULL) || (outsz < 8))
        return 0;
    out[0] = '\0';
    if (banner_out != NULL)
        banner_out[0] = '\0';
    if (terr_out != NULL)
        terr_out[0] = '\0';

    if (!pp_fat_open(&fs, img_path))
        return 0;

    int ok = 0;

    /* Funny's Interactive Playworld -- a different cabinet from the same corner of the
       arcade world, and the reason this function has two answers.  It carries no
       FOTO\SETTINGS\MAIN.SET to name itself with: AUTOEXEC.BAT runs GAME\FSYSTEM.EXE,
       which probes the touchscreen, checks its dongle and then execs GAME\FUNNY.DLL --
       a WDOSX executable despite the extension, and the whole game.  Those two files
       together are the signature; no Photo Play release has either.  The banner is what
       picks the token in photoplay.c, so it has to be recognisable there. */
    if (pp_dir_find(&fs, 0, "GAME       ", &clus, &size, &isdir) && isdir) {
        uint16_t sub;
        uint32_t ssize;
        int      sdir;

        if (pp_dir_find(&fs, clus, "FSYSTEM EXE", &sub, &ssize, &sdir) && !sdir &&
            pp_dir_find(&fs, clus, "FUNNY   DLL", &sub, &ssize, &sdir) && !sdir) {
            snprintf(out, outsz, "%s", PHOTOPLAY_FUNNY_DISPLAY);
            if (banner_out != NULL)
                snprintf(banner_out, bsz, "%s", PHOTOPLAY_FUNNY_BANNER);
            /* No territory: this cabinet records one nowhere the image can be asked for
               it, and its token does not carry one either.  The build to hand is German
               -- KEYB GR+, COUNTRY=49, LASTGAME.LOG says GER -- but that is a fact about
               this image, not something read out of it. */
            if (terr_out != NULL)
                terr_out[0] = '\0';
            fclose(fs.f);
            return 1;
        }
    }

    if (pp_dir_find(&fs, 0, "FOTO       ", &clus, &size, &isdir) && isdir &&
        pp_dir_find(&fs, clus, "SETTINGS   ", &clus, &size, &isdir) && isdir &&
        pp_dir_find(&fs, clus, "MAIN    SET", &clus, &size, &isdir) && !isdir) {
        uint8_t *raw = pp_read_chain(&fs, clus, size, 256 * 1024);

        if (raw != NULL) {
            char version[128] = { 0 };
            char land[16]     = { 0 };

            if (pp_set_lookup(raw, size, "Version", version, sizeof(version))) {
                /* Territory: prefer the explicit field, else the parenthesis. */
                if (!pp_set_lookup(raw, size, "Land", land, sizeof(land))) {
                    const char *o = strchr(version, '(');
                    const char *c = o ? strchr(o, ')') : NULL;

                    if (o && c && ((c - o) > 1) && ((size_t) (c - o) < sizeof(land))) {
                        memcpy(land, o + 1, (size_t) (c - o - 1));
                        land[c - o - 1] = '\0';
                    }
                }

                const char *name = NULL;
                for (size_t i = 0; i < (sizeof(pp_release_names) / sizeof(pp_release_names[0])); i++) {
                    if (!strncmp(version, pp_release_names[i].banner,
                                 strlen(pp_release_names[i].banner))) {
                        name = pp_release_names[i].display;
                        break;
                    }
                }

                /* The dongle has to report this string rather than the display
                   name: the guest compares its record against the MAIN.SET
                   "Version" field verbatim (Docs/08). */
                if (banner_out != NULL)
                    snprintf(banner_out, bsz, "%s", version);
                if (terr_out != NULL)
                    snprintf(terr_out, tsz, "%s", land);

                if (name != NULL) {
                    if (land[0])
                        snprintf(out, outsz, "%s %s", name, land);
                    else
                        snprintf(out, outsz, "%s", name);
                } else {
                    /* Unrecognised release: show what the image actually says
                       rather than inventing a name for it. */
                    snprintf(out, outsz, "%s", version);
                }
                ok = 1;
            }
            free(raw);
        }
    }

    fclose(fs.f);
    return ok;
}


int
photoplay_identify(const char *img_path, char *out, size_t outsz)
{
    return photoplay_identify_ex(img_path, out, outsz, NULL, 0, NULL, 0);
}