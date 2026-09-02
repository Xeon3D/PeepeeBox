/*
 * DONGTEST -- probe a real Photo Play / I.G.O. parallel dongle from Windows XP.
 *
 * Built freestanding: no C runtime, only kernel32 plus NtSetInformationProcess out of
 * ntdll.  Modern MSVC's CRT needs Vista or later, and this has to run on XP, so the CRT
 * is left out entirely and the entry point is our own.
 *
 * Raw IN/OUT from user mode is privileged on NT.  Rather than ship a kernel driver, the
 * program asks for I/O privilege directly:
 *
 *     NtSetInformationProcess(GetCurrentProcess(), ProcessUserModeIOPL = 16, NULL, 0)
 *
 * which an Administrator process on 32-bit NT/2000/XP is allowed to do.  It must
 * therefore be run **as Administrator on 32-bit XP**; on 64-bit Windows the call fails
 * and the program says so instead of producing rubbish.
 *
 * The wire is the one read out of I.G.O. 2's FINDIT.EXE (docs/research/29):
 *
 *   command byte b : write (b & 0xFE) | 0x80, b | 0x81, (b & 0xFE) | 0x80
 *                    -- DATA bit 0 is the clock, bit 7 always set        (0x32DE2)
 *   query q (5 bit): payload = ((q << 1) & 0x0E) | ((q << 2) & 0x60) | 0x80
 *                    write payload, payload | 0x10, payload
 *                    -- DATA bit 4 is the clock                          (0x32F59)
 *                    then read STATUS and take bit 5                     (0x32D17)
 *   round preamble : command(seed), command(0x4E), write 0x84            (0x32FC2)
 *
 * The seed byte did not resolve from the binary, so it is swept rather than guessed.
 *
 * Writes DONGTEST.BIN:
 *   0x0000  256 B   phase 1  DATA = v, STATUS read back, for v = 0..255
 *   0x0100 2048 B   phase 2  per seed 0..255: preamble, then 64 answers to query 0
 *   0x0900  512 B   phase 3  for seed 0x61 and 0x8B, per query 0..31: 64 answers
 *   0x0B00   64 B   phase 4  two identical runs of 256 queries cycling 0..31,
 *                            to show whether the preamble really resets the part
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef LONG (WINAPI *PFN_NTSIP)(HANDLE, ULONG, PVOID, ULONG);

static unsigned short g_base = 0x378;

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

/* the port needs a moment to settle; the guest's own code is far slower than this */
static void settle(void)
{
    volatile int i;

    for (i = 0; i < 200; i++)
        (void) inp8(0x80);          /* the traditional I/O delay port */
}

static void raw(unsigned char b)
{
    outp8(g_base, b);
    settle();
}

/* one command byte, clocked on DATA bit 0 */
static void cmdbyte(unsigned char b)
{
    raw((unsigned char) ((b & 0xFE) | 0x80));
    raw((unsigned char) (b | 0x81));
    raw((unsigned char) ((b & 0xFE) | 0x80));
}

/* one 5-bit query, clocked on DATA bit 4; the answer is STATUS bit 5 */
static unsigned char query(unsigned char q)
{
    unsigned char pay = (unsigned char) (((q << 1) & 0x0E) | ((q << 2) & 0x60) | 0x80);

    raw(pay);
    raw((unsigned char) (pay | 0x10));
    raw(pay);
    return (unsigned char) ((inp8((unsigned short) (g_base + 1)) >> 5) & 1);
}

static void preamble(unsigned char seed)
{
    cmdbyte(seed);
    cmdbyte(0x4E);
    raw(0x84);
}

static void say(const char *s)
{
    DWORD n = 0;
    const char *p = s;

    while (*p)
        p++;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, (DWORD) (p - s), &n, NULL);
}

/* pack `count` answers to `q` into the buffer, eight to a byte */
static unsigned char *collect(unsigned char *out, unsigned char q, int count)
{
    int i;
    unsigned char acc = 0;

    for (i = 0; i < count; i++) {
        acc = (unsigned char) ((acc << 1) | query(q));
        if ((i & 7) == 7)
            *out++ = acc;
    }
    return out;
}

static unsigned char g_buf[0x0B40];

void __stdcall start(void)
{
    HMODULE nt;
    PFN_NTSIP set;
    LONG st;
    unsigned char *p;
    int seed, q, rep;
    HANDLE h;
    DWORD written = 0;
    char *cmd;

    /* an optional hex LPT base on the command line, e.g. DONGTEST 278 */
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

    nt = LoadLibraryA("ntdll.dll");
    if (!nt) {
        say("cannot load ntdll\r\n");
        ExitProcess(1);
    }
    set = (PFN_NTSIP) GetProcAddress(nt, "NtSetInformationProcess");
    if (!set) {
        say("no NtSetInformationProcess\r\n");
        ExitProcess(1);
    }
    /* ProcessUserModeIOPL = 16 */
    st = set(GetCurrentProcess(), 16, NULL, 0);
    if (st < 0) {
        say("Could not get I/O privilege.\r\n"
            "Run as Administrator, on 32-bit Windows XP.\r\n");
        ExitProcess(1);
    }

    p = g_buf;

    /* phase 1 -- raw DATA to STATUS */
    for (q = 0; q < 256; q++) {
        raw((unsigned char) q);
        *p++ = inp8((unsigned short) (g_base + 1));
    }

    /* phase 2 -- every seed, 64 answers to query 0 */
    for (seed = 0; seed < 256; seed++) {
        preamble((unsigned char) seed);
        p = collect(p, 0, 64);
    }

    /* phase 3 -- the two candidate seeds, every query */
    for (rep = 0; rep < 2; rep++) {
        unsigned char sd = rep ? 0x8B : 0x61;

        for (q = 0; q < 32; q++) {
            preamble(sd);
            p = collect(p, (unsigned char) q, 64);
        }
    }

    /* phase 4 -- the same 256-query run twice, to test the reset */
    for (rep = 0; rep < 2; rep++) {
        unsigned char acc = 0;
        int i;

        preamble(0x61);
        for (i = 0; i < 256; i++) {
            acc = (unsigned char) ((acc << 1) | query((unsigned char) (i & 31)));
            if ((i & 7) == 7)
                *p++ = acc;
        }
    }

    h = CreateFileA("DONGTEST.BIN", GENERIC_WRITE, 0, NULL,
                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        say("cannot create DONGTEST.BIN\r\n");
        ExitProcess(1);
    }
    WriteFile(h, g_buf, (DWORD) (p - g_buf), &written, NULL);
    CloseHandle(h);

    say("Done -- DONGTEST.BIN written. Send it back.\r\n");
    ExitProcess(0);
}
