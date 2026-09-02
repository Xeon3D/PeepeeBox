/*
 * DONGCAP -- capture a Photo Play / I.G.O. dongle's keyed-round outputs.
 *
 * Purpose
 * -------
 * PeepeeBox can do everything in the picture cipher except one step: the keyed round,
 * which the dongle computes.  Give the emulator that round's answer for every input the
 * games will ask about and it decrypts on its own, with the disk image untouched.
 *
 * This program produces exactly that table, from the real part.
 *
 * How it can do it without any plaintext
 * --------------------------------------
 * A 4 KB buffer's decryption needs two dwords from the dongle.  The block function is
 *
 *     (L1,R1) = A_rounds(ciphertext block 0)      keyless
 *     f1      = keyed_round(L1)                   <- the dongle
 *     (L3,R3) = B_rounds(f1 ^ R1, L1)             keyless
 *     f2      = keyed_round(L3)                   <- the dongle
 *
 * so L1 comes from the ciphertext alone, and once the part has answered with f1, L3
 * follows from it -- again with no key and no plaintext.  The .LST file therefore only
 * has to carry (L1, R1) per buffer, computed offline from the encrypted archive, and one
 * pass over it captures both dwords.
 *
 * The preamble seed
 * -----------------
 * A round opens with command(seed), command(0x4E), write 0x84.  That seed byte never
 * resolved from the binaries, so it is not guessed: the .LST carries a few
 * (input, expected output) pairs recovered offline from known plaintext, and the program
 * tries all 256 seeds until one reproduces every pair.  If none does, it says so and
 * stops rather than writing a table that would be wrong -- and that failure is itself the
 * answer, because it would mean the round is not reproducible from its input alone.
 *
 * The wire, from I.G.O. 2's FINDIT.EXE (docs/research/29):
 *   command byte b : write (b & 0xFE)|0x80, b|0x81, (b & 0xFE)|0x80   -- DATA bit 0 clocks
 *   query q        : payload = ((q<<1)&0x0E) | ((q<<2)&0x60) | 0x80
 *                    write payload, payload|0x10, payload             -- DATA bit 4 clocks
 *                    answer = STATUS bit 5
 *
 * Freestanding: no C runtime (the modern one needs Vista, this must run on XP), imports
 * only KERNEL32, ntdll resolved at runtime.  Port access comes from
 * NtSetInformationProcess(ProcessUserModeIOPL), which an Administrator gets on 32-bit XP.
 *
 * Usage:  DONGCAP            reads DONGCAP.LST, writes DONGCAP.BIN
 *         DONGCAP 278        same, with the LPT base in hex
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef LONG (WINAPI *PFN_NTSIP)(HANDLE, ULONG, PVOID, ULONG);

#define POLY 0x80500062u
#define CA   0x5B2C004Au
#define CB   0x803425C3u

static unsigned short g_base = 0x378;
static unsigned char  g_seed = 0;

/* ------------------------------------------------------------------ port */

static void outp8(unsigned short port, unsigned char val)
{
    __asm {
        mov dx, port
        mov al, val
        out dx, al
    }
}

static unsigned char inp8(unsigned short port)
{
    unsigned char v;

    __asm {
        mov dx, port
        in  al, dx
        mov v, al
    }
    return v;
}

static void raw(unsigned char b)
{
    outp8(g_base, b);
    (void) inp8(0x80);          /* the traditional I/O delay */
    (void) inp8(0x80);
}

static void cmdbyte(unsigned char b)
{
    raw((unsigned char) ((b & 0xFE) | 0x80));
    raw((unsigned char) (b | 0x81));
    raw((unsigned char) ((b & 0xFE) | 0x80));
}

static unsigned char query(unsigned char q)
{
    unsigned char pay = (unsigned char) (((q << 1) & 0x0E) | ((q << 2) & 0x60) | 0x80);

    raw(pay);
    raw((unsigned char) (pay | 0x10));
    raw(pay);
    return (unsigned char) ((inp8((unsigned short) (g_base + 1)) >> 5) & 1);
}

static void preamble(void)
{
    cmdbyte(g_seed);
    cmdbyte(0x4E);
    raw(0x84);
}

/* ------------------------------------------------- the keyed round itself */

/* 39 shift steps, 40 consultations; the byte offered to the part is picked by the
   previous answer and the bit about to be shifted out (0x32749). */
static unsigned int keyed_round(unsigned int v)
{
    unsigned char prev;
    int k;

    preamble();
    prev = query((unsigned char) (v & 0xFF));
    for (k = 1; k <= 39; k++) {
        unsigned int idx = (unsigned int) ((prev & 1) | ((v & 1) << 1));

        v = ((idx ^ v) & 1) ? ((v >> 1) ^ POLY) : (v >> 1);
        prev = query((unsigned char) ((v >> (8 * idx)) & 0xFF));
    }
    return v;
}

/* ------------------------------------------------------- keyless helpers */

static unsigned int rol(unsigned int v, int s)
{
    s &= 31;
    return s ? ((v << s) | (v >> (32 - s))) : v;
}

/* B_rounds(b0,b1) -> b0, which is L3 */
static unsigned int b_rounds_first(unsigned int b0, unsigned int b1)
{
    int s;

    for (s = 10; s >= 0; s -= 2) {
        unsigned int t = rol(b0 ^ CB, s) ^ b1;

        b1 = b0;
        b0 = t;
    }
    return b0;
}

/* the encode direction runs the same stage the other way: six rounds ascending, and it
   is the second output that becomes the next keyed round's input (0x32837) */
static unsigned int b_rounds_fwd_second(unsigned int b0, unsigned int b1)
{
    int s;

    for (s = 0; s <= 10; s += 2) {
        unsigned int t = rol(b1 ^ CB, s) ^ b0;

        b0 = b1;
        b1 = t;
    }
    return b1;
}

/* ------------------------------------------------------------------ i/o */

static void say(const char *s)
{
    DWORD n = 0;
    const char *p = s;

    while (*p)
        p++;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, (DWORD) (p - s), &n, NULL);
}

static void sayhex(unsigned int v, int digits)
{
    char b[12];
    int i;

    for (i = 0; i < digits; i++)
        b[digits - 1 - i] = "0123456789ABCDEF"[(v >> (4 * i)) & 15];
    b[digits] = 0;
    say(b);
}

static void saynum(unsigned int v)
{
    char b[12];
    int i = 11;

    b[11] = 0;
    if (!v) {
        say("0");
        return;
    }
    while (v && i) {
        b[--i] = (char) ('0' + (v % 10));
        v /= 10;
    }
    say(b + i);
}

/* A run takes minutes and may be launched by double-clicking, which would close the
   window before anything could be read.  Beep, then hold the window open. */
static void finish(void)
{
    char c;
    DWORD n = 0;

    Beep(880, 250);
    say("Press Enter to close.\r\n");
    ReadFile(GetStdHandle(STD_INPUT_HANDLE), &c, 1, &n, NULL);
}

/* ----------------------------------------------------------------- main */

#define LST_MAGIC 0x50414344u      /* 'DCAP' */
#define OUT_MAGIC 0x54554F44u      /* 'DOUT' */

void __stdcall start(void)
{
    HMODULE ntm;
    PFN_NTSIP set;
    HANDLE h;
    DWORD got = 0, wrote = 0, size;
    unsigned char *lst;
    unsigned int *hdr, count, ncal, nenc, i;
    unsigned int *cal, *work, *enc, *out;
    int seed;
    DWORD t0 = 0;
    char *cmd;

    cmd = GetCommandLineA();
    while (*cmd && *cmd != ' ')
        cmd++;
    while (*cmd == ' ')
        cmd++;
    if (*cmd) {
        unsigned short v = 0;
        int any = 0;

        while (*cmd) {
            char c = *cmd++;
            int d;

            if (c >= '0' && c <= '9')       d = c - '0';
            else if (c >= 'a' && c <= 'f')  d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F')  d = c - 'A' + 10;
            else break;
            v = (unsigned short) (v * 16 + d);
            any = 1;
        }
        if (any)
            g_base = v;
    }

    ntm = LoadLibraryA("ntdll.dll");
    set = ntm ? (PFN_NTSIP) GetProcAddress(ntm, "NtSetInformationProcess") : NULL;
    if (!set || set(GetCurrentProcess(), 16, NULL, 0) < 0) {
        say("Could not get I/O privilege.\r\n"
            "Run as Administrator on 32-bit Windows XP.\r\n");
        finish();
        ExitProcess(1);
    }

    h = CreateFileA("DONGCAP.LST", GENERIC_READ, FILE_SHARE_READ, NULL,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        say("DONGCAP.LST not found -- keep it next to this program.\r\n");
        finish();
        ExitProcess(1);
    }
    size = GetFileSize(h, NULL);
    lst = (unsigned char *) VirtualAlloc(NULL, size + 16, MEM_COMMIT, PAGE_READWRITE);
    ReadFile(h, lst, size, &got, NULL);
    CloseHandle(h);

    hdr = (unsigned int *) lst;
    if (got < 16 || hdr[0] != LST_MAGIC) {
        say("DONGCAP.LST is not a capture list.\r\n");
        finish();
        ExitProcess(1);
    }
    ncal  = hdr[1];
    count = hdr[2];
    nenc  = hdr[3];                  /* EncodeData blocks, e.g. I.G.O. 3's boot check */
    cal   = hdr + 4;                 /* ncal pairs of (input, expected)  */
    work  = cal + ncal * 2;          /* count pairs of (L1, R1)          */
    enc   = work + count * 2;        /* nenc pairs of (P0, P1)           */

    say("LPT base 0x");
    sayhex(g_base, 3);
    say(", ");
    saynum(count);
    say(" buffers, ");
    saynum(ncal);
    say(" calibration pairs\r\n");

    /* --- find the preamble seed the part actually answers to --- */
    say("Calibrating");
    seed = -1;
    for (i = 0; i < 256; i++) {
        unsigned int j;
        int ok = 1;

        g_seed = (unsigned char) i;
        for (j = 0; j < ncal; j++) {
            if (keyed_round(cal[j * 2]) != cal[j * 2 + 1]) {
                ok = 0;
                break;
            }
        }
        if ((i & 15) == 0)
            say(".");
        if (ok) {
            seed = (int) i;
            break;
        }
    }
    if (seed < 0) {
        /* Calibration failing is itself information, so leave data behind rather than
           nothing: every seed against the first few inputs, for working out offline
           what the part is actually doing. */
        unsigned int *dg = (unsigned int *) VirtualAlloc(NULL, (256u * 4 + 8) * 4,
                                                         MEM_COMMIT, PAGE_READWRITE);
        unsigned int nd = ncal < 3 ? ncal : 3;
        unsigned int w = 0;

        say("\r\nNo seed reproduces the known answers.\r\n"
            "Writing DONGCAP.DIAG instead -- every seed against the first inputs.\r\n");
        dg[w++] = 0x47414944u;      /* DIAG */
        dg[w++] = nd;
        for (i = 0; i < 256; i++) {
            unsigned int j;

            g_seed = (unsigned char) i;
            for (j = 0; j < nd; j++)
                dg[w++] = keyed_round(cal[j * 2]);
        }
        h = CreateFileA("DONGCAP.DIAG", GENERIC_WRITE, 0, NULL,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            WriteFile(h, dg, w * 4, &wrote, NULL);
            CloseHandle(h);
            say("DONGCAP.DIAG written. Send it back -- it says what the part answers.\r\n");
        }
        finish();
        ExitProcess(2);
    }
    say("\r\nSeed 0x");
    sayhex((unsigned int) seed, 2);
    say(" reproduces every calibration pair.\r\n");
    g_seed = (unsigned char) seed;

    /* --- capture --- */
    out = (unsigned int *) VirtualAlloc(NULL, ((count + nenc) * 2 + 8) * 4,
                                        MEM_COMMIT, PAGE_READWRITE);
    out[0] = OUT_MAGIC;
    out[1] = count;
    out[2] = (unsigned int) seed;
    out[3] = nenc;

    t0 = GetTickCount();
    say("Capturing");
    for (i = 0; i < count; i++) {
        unsigned int L1 = work[i * 2];
        unsigned int R1 = work[i * 2 + 1];
        unsigned int f1 = keyed_round(L1);
        unsigned int L3 = b_rounds_first(f1 ^ R1, L1);
        unsigned int f2 = keyed_round(L3);

        out[4 + i * 2]     = f1;
        out[4 + i * 2 + 1] = f2;
        if ((i & 511) == 0)
            say(".");
    }
    say("\r\n");

    h = CreateFileA("DONGCAP.BIN", GENERIC_WRITE, 0, NULL,
                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        say("cannot create DONGCAP.BIN\r\n");
        finish();
        ExitProcess(1);
    }
    WriteFile(h, out, ((count + nenc) * 2 + 4) * 4, &wrote, NULL);
    CloseHandle(h);

    say("Done -- DONGCAP.BIN written, ");
    saynum((count + nenc) * 2);
    say(" rounds captured in ");
    saynum((GetTickCount() - t0) / 1000);
    say(" seconds. Send DONGCAP.BIN back.\r\n");
    finish();
    ExitProcess(0);
}
