/*
 * DONGDUMP -- read everything an emulator needs out of a funworld parallel dongle.
 *
 * What this is for
 * ----------------
 * The 2001..2007 and I.G.O. Italy cabinets carry a HASP4 on the parallel port.  Three
 * different protocols share that one pair of wires, and PeepeeBox needs all three
 * answered to emulate a cabinet without the part:
 *
 *   session   identity, the 64-step sweep, the liveness probe -- the gates the
 *             library walks before it will talk to the part at all
 *   memory    Microwire, 64 words; the record, scrambled under the first password
 *   transform the picture cipher's keyed round -- a 32-bit key and the register
 *             its preamble leaves behind
 *
 * `tools/dongcap` captures the transform layer's *answers* for one game's inputs, which
 * is a table.  This captures the *part*: the key itself, the memory, and the gates, so a
 * generation nobody here owns can be added to the emulator from one run.
 *
 * It never writes to the dongle.  Only READ instructions are issued on the memory layer;
 * WRITE, ERASE, EWEN and ERAL are not implemented, deliberately.
 *
 * How the key comes out
 * ---------------------
 * The keyed round is a twelve-bit shift register (docs/research-v2/05.4):
 *
 *     i5  = five bits of the query byte           (the framing drops the other three)
 *     st  = (key >> i5) & 1
 *     b0  = i5 ^ ((st ^ 1) & (i5 >> 3)) ^ (i5 >> 4)
 *         ^ (cur >> 10) ^ (cur >> 7)
 *         ^ ((i5 & 2) ? cur >> 5 : 0) ^ ((i5 & 4) ? cur >> 8 : 0)
 *     cur = ((cur ^ ((i5 & 1) << 2)) << 1) | (b0 & 1)
 *     answer = ((cur >> 11) ^ st) & 1
 *
 * Every operation there is XOR and every unknown -- the 32 key bits and the 11 bits of
 * the initial register -- enters linearly, `st` included: `(st ^ 1) & (i5 >> 3)` is
 * `st ^ 1` when bit 3 of the query is set and zero when it is not, and bit 3 of the query
 * is ours to choose.  So each observed answer is one GF(2) equation in 43 unknowns, and
 * a few thousand queries over random inputs solve the whole part by Gaussian elimination.
 * No plaintext, no disk image, no guessing.
 *
 * That also makes it self-checking.  A wrong reading of the wire does not produce a wrong
 * key, it produces an inconsistent system -- and the recovered key is then replayed
 * against every observation and against rounds held back from the solve, so the report
 * says "N of N" or it says the model did not fit.  If it did not fit, the raw
 * (query, answer) log is written anyway, because that failure is the finding.
 *
 * The passwords
 * -------------
 * `pass1` is not guessed either.  The record is stored as
 *
 *     word[i] = plain[i - 8] ^ (uint16)(i - 8) ^ pass1 ^ (i < 8 ? 0xFF00 : 0)
 *
 * and words 0..7 hold no record, so each of them is pass1 ^ a known constant.  Eight
 * independent copies of the same password: if they do not agree, the memory read is
 * wrong and the tool says so rather than reporting a number.  `pass2` is never on the
 * wire -- it is reported only as the value the known pairing implies, and labelled.
 *
 * Build: build.cmd  (MSVC x86).  Freestanding -- no C runtime, because the modern one
 * needs Vista and this has to run on XP, which is where the working parallel ports are.
 *
 * NOT YET RUN AGAINST HARDWARE.  Every sequence here is taken from the wire as it is
 * documented in docs/research-v2/05 and from the passthrough capture in
 * docs/research/evidence, but no physical dongle has answered this program.  Treat the
 * first run as an experiment: DETECT before DUMP, and if the identity signature does not
 * come back as 0xCEFF0AFFCECE0A0A on a 68BB part, stop and look at wire.log.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>       /* ShellExecuteA, for the Open folder button */
#include <stdarg.h>

/* ------------------------------------------------------------------ tiny runtime
 *
 * No CRT, so these are ours.  #pragma function stops the compiler turning a call to
 * memset into an intrinsic and then, at -O1, back into a call to a memset we would not
 * have linked.
 */
#pragma function(memset, memcpy)

void *
memset(void *d, int c, size_t n)
{
    unsigned char *p = (unsigned char *) d;

    while (n--)
        *p++ = (unsigned char) c;
    return d;
}

void *
memcpy(void *d, const void *s, size_t n)
{
    unsigned char       *p = (unsigned char *) d;
    const unsigned char *q = (const unsigned char *) s;

    while (n--)
        *p++ = *q++;
    return d;
}

static size_t
zlen(const char *s)
{
    const char *p = s;

    while (*p)
        p++;
    return (size_t) (p - s);
}

static int
zcmpn(const char *a, const char *b, size_t n)
{
    while (n--) {
        if (*a != *b)
            return 1;
        if (!*a)
            return 0;
        a++;
        b++;
    }
    return 0;
}

/* A very small vsnprintf.  %s %c %d %u %x %X, width and zero padding on the integer
   forms only -- which is all this program formats. */
static void
zvfmt(char *out, size_t cap, const char *f, va_list ap)
{
    size_t o = 0;

    if (!cap)
        return;
    while (*f && (o + 1 < cap)) {
        char c = *f++;
        char num[24];
        int  width = 0, zero = 0, neg = 0, base = 10, upper = 0, i;
        unsigned int uv;
        const char *s;

        if (c != '%') {
            out[o++] = c;
            continue;
        }
        if (*f == '%') {
            f++;
            out[o++] = '%';
            continue;
        }
        if (*f == '0') {
            zero = 1;
            f++;
        }
        while ((*f >= '0') && (*f <= '9'))
            width = width * 10 + (*f++ - '0');
        c = *f++;
        switch (c) {
            case 's':
                s = va_arg(ap, const char *);
                if (!s)
                    s = "(null)";
                while (*s && (o + 1 < cap))
                    out[o++] = *s++;
                continue;
            case 'c':
                out[o++] = (char) va_arg(ap, int);
                continue;
            case 'X':
                upper = 1;
                /* fall through */
            case 'x':
                base = 16;
                uv = va_arg(ap, unsigned int);
                break;
            case 'u':
                uv = va_arg(ap, unsigned int);
                break;
            case 'd': {
                int v = va_arg(ap, int);

                neg = (v < 0);
                /* no 64-bit maths: INT_MIN negated in 32 bits is still INT_MIN,
                   and its unsigned reading is the value we want. */
                uv  = neg ? (unsigned int) (0u - (unsigned int) v) : (unsigned int) v;
                break;
            }
            default:
                out[o++] = c;
                continue;
        }

        i = 0;
        if (!uv)
            num[i++] = '0';
        while (uv) {
            unsigned int d = uv % (unsigned int) base;

            num[i++] = (char) ((d < 10) ? ('0' + d)
                                        : ((upper ? 'A' : 'a') + d - 10));
            uv /= (unsigned int) base;
        }
        if (neg)
            num[i++] = '-';
        while ((i < width) && (i < (int) sizeof num))
            num[i++] = zero ? '0' : ' ';
        while (i-- && (o + 1 < cap))
            out[o++] = num[i];
    }
    out[o] = 0;
}

static void
zfmt(char *out, size_t cap, const char *f, ...)
{
    va_list ap;

    va_start(ap, f);
    zvfmt(out, cap, f, ap);
    va_end(ap);
}

/* ------------------------------------------------------------------------ port access
 *
 * Two backends, because the machines that have a parallel port and the machines that
 * will run a modern binary are not the same set.
 *
 *   IOPL     NtSetInformationProcess(ProcessUserModeIOPL) -- 32-bit NT/XP only, needs
 *            SeTcbPrivilege, installs nothing.  This is the fast one: `in`/`out` run in
 *            the process.
 *   INPOUT   inpout32.dll, which ships a signed kernel driver and therefore also works
 *            on 64-bit Windows 7/10/11 under WOW64.  Slower per access -- a few
 *            microseconds -- which this protocol does not care about.
 *
 * The DLL is loaded by name only; nothing is installed or extracted by this program.
 */
enum { BK_NONE = 0, BK_IOPL, BK_INPOUT };

typedef LONG (WINAPI *PFN_NTSIP)(HANDLE, ULONG, PVOID, ULONG);
typedef void (__stdcall *PFN_OUT32)(short, short);
typedef short (__stdcall *PFN_INP32)(short);
typedef BOOL (__stdcall *PFN_DRVOPEN)(void);
typedef BOOL (WINAPI *PFN_ISWOW64)(HANDLE, PBOOL);
typedef BOOL (WINAPI *PFN_OPT)(HANDLE, DWORD, PHANDLE);
typedef BOOL (WINAPI *PFN_LPV)(LPCSTR, LPCSTR, PLUID);
typedef BOOL (WINAPI *PFN_ATP)(HANDLE, BOOL, PTOKEN_PRIVILEGES, DWORD,
                               PTOKEN_PRIVILEGES, PDWORD);

static int            g_backend = BK_NONE;
static char           g_backend_why[512];
static unsigned short g_base = 0x378;
static PFN_OUT32      g_out32;
static PFN_INP32      g_inp32;

/* Self-test mode: a software part answers instead of the port.  Defined with the
   transform layer, below, because that is the only layer it models. */
static int  g_sim;
static int  g_sim_pending, g_sim_ans;
static void sim_data(unsigned char val);

static void
raw_out(unsigned short port, unsigned char val)
{
    if (g_sim) {
        if (port == g_base)
            sim_data(val);
        return;
    }
    if (g_backend == BK_INPOUT) {
        g_out32((short) port, (short) val);
        return;
    }
    __asm {
        mov dx, port
        mov al, val
        out dx, al
    }
}

static unsigned char
raw_in(unsigned short port)
{
    unsigned char v;

    if (g_sim) {
        if (port == (unsigned short) (g_base + 1)) {
            const int a = g_sim_pending ? g_sim_ans : 0;

            g_sim_pending = 0;
            return (unsigned char) (a ? 0x20 : 0x00);
        }
        return 0x00;
    }
    if (g_backend == BK_INPOUT)
        return (unsigned char) (g_inp32((short) port) & 0xFF);

    __asm {
        mov dx, port
        in  al, dx
        mov v, al
    }
    return v;
}

/* The traditional ISA settle.  Through the driver each access already costs far more
   than a bus cycle, so it is only worth doing on the in-process backend. */
static void
settle(void)
{
    if (g_backend == BK_IOPL) {
        (void) raw_in(0x80);
        (void) raw_in(0x80);
    }
}

/* ------------------------------------------------------------------------- wire log
 *
 * Every access this program makes, in order, so that a run which does not behave can be
 * read offline against docs/research/evidence.  Capped, because the transform phase
 * makes tens of thousands of accesses and only the first few rounds of those are ever
 * worth reading.
 */
static char  *g_wire;
static size_t g_wire_n;
static size_t g_wire_cap;
static int    g_wire_on = 1;

static void
wire(const char *op, unsigned char v)
{
    char line[64];
    size_t n;

    if (!g_wire_on || !g_wire)
        return;
    zfmt(line, sizeof line, "%s %02X\r\n", op, (unsigned int) v);
    n = zlen(line);
    if (g_wire_n + n >= g_wire_cap) {
        g_wire_on = 0;
        return;
    }
    memcpy(g_wire + g_wire_n, line, n);
    g_wire_n += n;
}

static void
pp_out(unsigned char v)
{
    raw_out(g_base, v);
    wire("write_data", v);
    settle();
}

static unsigned char
pp_status(void)
{
    unsigned char s = raw_in((unsigned short) (g_base + 1));

    wire("read_status", s);
    return s;
}

/* STATUS bit 5 is DO on every one of the three layers. */
static int
pp_do(void)
{
    return (pp_status() >> 5) & 1;
}

/* ------------------------------------------------------- acquiring the backend */

/* SeTcbPrivilege is not the same thing as being an Administrator: on XP an admin token
   does not carry "Act as part of the operating system" by default, and the IOPL call
   wants it.  Enable it rather than assume it. */
static int
enable_tcb(void)
{
    HMODULE adv = LoadLibraryA("advapi32.dll");
    PFN_OPT open_tok;
    PFN_LPV lookup;
    PFN_ATP adjust;
    HANDLE  tok = NULL;
    TOKEN_PRIVILEGES tp;
    DWORD   err;

    if (!adv)
        return 0;
    open_tok = (PFN_OPT) GetProcAddress(adv, "OpenProcessToken");
    lookup   = (PFN_LPV) GetProcAddress(adv, "LookupPrivilegeValueA");
    adjust   = (PFN_ATP) GetProcAddress(adv, "AdjustTokenPrivileges");
    if (!open_tok || !lookup || !adjust)
        return 0;
    if (!open_tok(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok))
        return 0;

    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (!lookup(NULL, "SeTcbPrivilege", &tp.Privileges[0].Luid)) {
        CloseHandle(tok);
        return 0;
    }
    adjust(tok, FALSE, &tp, sizeof tp, NULL, NULL);
    /* AdjustTokenPrivileges reports success even when it changed nothing, so the last
       error is what says whether the privilege was there to enable. */
    err = GetLastError();
    CloseHandle(tok);
    return err == ERROR_SUCCESS;
}

static int
try_inpout(void)
{
    static const char *names[] = { "inpoutx64.dll", "inpout32.dll" };
    int i;

    for (i = 0; i < 2; i++) {
        HMODULE h = LoadLibraryA(names[i]);
        PFN_DRVOPEN isopen;

        if (!h)
            continue;
        g_out32 = (PFN_OUT32) GetProcAddress(h, "Out32");
        g_inp32 = (PFN_INP32) GetProcAddress(h, "Inp32");
        if (!g_out32 || !g_inp32) {
            FreeLibrary(h);
            continue;
        }
        isopen = (PFN_DRVOPEN) GetProcAddress(h, "IsInpOutDriverOpen");
        if (isopen && !isopen()) {
            zfmt(g_backend_why, sizeof g_backend_why,
                 "%s loaded but its kernel driver did not open.  Run as Administrator, "
                 "and on 64-bit Windows install the InpOutx64 package first.", names[i]);
            FreeLibrary(h);
            continue;
        }
        g_backend = BK_INPOUT;
        zfmt(g_backend_why, sizeof g_backend_why, "%s", names[i]);
        return 1;
    }
    return 0;
}

static int
try_iopl(void)
{
    HMODULE     ntm = LoadLibraryA("ntdll.dll");
    PFN_NTSIP   set = ntm ? (PFN_NTSIP) GetProcAddress(ntm, "NtSetInformationProcess")
                          : NULL;
    HMODULE     k32 = GetModuleHandleA("kernel32.dll");
    PFN_ISWOW64 wow = k32 ? (PFN_ISWOW64) GetProcAddress(k32, "IsWow64Process") : NULL;
    BOOL        is64 = FALSE;
    ULONG       dummy = 0;
    LONG        st1, st2;
    int         tcb;

    if (!set)
        return 0;
    /* ProcessUserModeIOPL does not exist on x64.  There is no fix; that machine needs
       the driver backend. */
    if (wow && wow(GetCurrentProcess(), &is64) && is64)
        return 0;

    tcb = enable_tcb();
    st1 = set(GetCurrentProcess(), 16, NULL, 0);
    if (st1 >= 0) {
        g_backend = BK_IOPL;
        zfmt(g_backend_why, sizeof g_backend_why, "ProcessUserModeIOPL");
        return 1;
    }
    st2 = set(GetCurrentProcess(), 16, &dummy, sizeof dummy);
    if (st2 >= 0) {
        g_backend = BK_IOPL;
        zfmt(g_backend_why, sizeof g_backend_why, "ProcessUserModeIOPL");
        return 1;
    }
    zfmt(g_backend_why, sizeof g_backend_why,
         "ProcessUserModeIOPL returned %08X and %08X; SeTcbPrivilege %s",
         (unsigned int) st1, (unsigned int) st2, tcb ? "enabled" : "NOT held");
    return 0;
}

/* Driver first.  On a machine that has both, the driver is the one that keeps working
   after a reboot into a 64-bit kernel, and the speed difference does not matter here. */
static int
acquire_port(void)
{
    char why_drv[512];

    g_backend_why[0] = 0;
    if (try_inpout())
        return 1;
    zfmt(why_drv, sizeof why_drv, "%s", g_backend_why);

    if (try_iopl())
        return 1;

    {
        char iopl_why[512];

        zfmt(iopl_why, sizeof iopl_why, "%s", g_backend_why);
        zfmt(g_backend_why, sizeof g_backend_why,
             "No port access.\r\n"
             "  inpout32: %s\r\n"
             "  IOPL: %s\r\n"
             "PRESS HELP for how to grant \"Act as part of the operating system\" to "
             "this account, or how to set up the InpOut32 driver.",
             why_drv[0] ? why_drv : "not found",
             iopl_why[0] ? iopl_why : "not available");
    }
    g_backend = BK_NONE;
    return 0;
}

/* =========================================================== layer 1: the session
 *
 * Command bytes go out on DATA with bit 7 clear and the answer comes back on STATUS
 * bit 5, one read per write.  Three transactions live here and the library runs all
 * three before it will touch the memory (docs/research-v2/05.3).
 */

/* What the library writes for the 64-step sweep.  Measured off a real part, and it is
   also the output of the LCG x = x*0x1989 + 5 seeded with 100, masked 0x7E, which is
   what I.G.O. 3's MENU.EXE computes at 0x24FB. */
static const unsigned char sweep_w[64] = {
    0x78, 0x6A, 0x56, 0x26, 0x02, 0x18, 0x6E, 0x3C, 0x2E, 0x3A, 0x72, 0x52,
    0x0C, 0x64, 0x70, 0x74, 0x2E, 0x24, 0x78, 0x36, 0x22, 0x0C, 0x1C, 0x26,
    0x78, 0x28, 0x68, 0x54, 0x40, 0x0C, 0x70, 0x52, 0x0C, 0x46, 0x44, 0x2E,
    0x6A, 0x68, 0x70, 0x78, 0x6A, 0x7E, 0x08, 0x40, 0x1C, 0x1C, 0x1A, 0x16,
    0x12, 0x50, 0x36, 0x0C, 0x58, 0x2C, 0x6C, 0x30, 0x04, 0x3C, 0x4E, 0x12,
    0x20, 0x14, 0x6A, 0x44
};

/* The identity ramp: DATA walks 00, 02 ... 7E, one STATUS read per step, and the part
   answers one bit per address.  `top` drives the same sequence with bit 7 set, which is
   what I.G.O. 5's library does -- the address is DATA bits 1..6 either way, so a part
   that is behaving answers both runs identically.  Disagreement between them is worth
   knowing about, so both are taken. */
static void
session_ramp(int top, unsigned char bits[64])
{
    int a;

    for (a = 0; a < 64; a++) {
        pp_out((unsigned char) ((a << 1) | (top ? 0x80 : 0x00)));
        bits[a] = (unsigned char) pp_do();
    }
}

/* acc = 0x7E XOR (XOR of addr<<1 wherever DO came back set).  0x37E1D accumulates it and
   0x36B02 looks it up in a four-entry table -- 0008, 000C, 0018, 001C -- rejecting 0x7E.
   A real 68BB part gives 0x18. */
static unsigned int
ramp_acc(const unsigned char bits[64])
{
    unsigned int acc = 0x7E;
    int a;

    for (a = 0; a < 64; a++)
        if (bits[a])
            acc ^= (unsigned int) (a << 1);
    return acc & 0xFF;
}

static void
session_sweep(unsigned char bits[64])
{
    int i;

    for (i = 0; i < 64; i++) {
        pp_out(sweep_w[i]);
        bits[i] = (unsigned char) pp_do();
    }
}

/* The library writes 1E, then 1C, and a live part answers 1 then 0.  A line stuck at
   either level fails it, which is the whole point of the gate. */
static void
session_liveness(int *hi, int *lo)
{
    pp_out(0x1E);
    *hi = pp_do();
    pp_out(0x1C);
    *lo = pp_do();
}

/* =========================================================== layer 2: the memory
 *
 * Ordinary Microwire.  CS = DATA bit 1, SK = DATA bit 5, DI = DATA bit 6, DO = STATUS
 * bit 5.  A read is 25 clocks: start bit, two opcode bits, SIX address bits, sixteen
 * data bits -- six, not eight, and the part holds 64 words (docs/research-v2/05.2).
 *
 * Only opcode 10, READ, is issued.  Nothing here can modify the dongle.
 */
#define MW_CS 0x02
#define MW_SK 0x20
#define MW_DI 0x40

static unsigned char g_mw_idle;   /* bits held on every write; calibrated, see below */
static unsigned char g_mw_latch;

static void
mw_put(unsigned char v)
{
    g_mw_latch = (unsigned char) (v | g_mw_idle);
    pp_out(g_mw_latch);
}

static void
mw_bit(unsigned char mask, int on)
{
    unsigned char v = (unsigned char) (on ? (g_mw_latch | mask)
                                          : (g_mw_latch & (unsigned char) ~mask));

    mw_put((unsigned char) (v & (unsigned char) ~g_mw_idle));
}

/* One bit out: put it on DI, then clock high and low.  The host clocks low-high-low and
   only samples afterwards, so the bit has to be on the line from the rising edge on. */
static void
mw_send(int b)
{
    mw_bit(MW_DI, b);
    mw_bit(MW_SK, 1);
    mw_bit(MW_SK, 0);
}

static int
mw_recv(void)
{
    mw_bit(MW_SK, 1);
    mw_bit(MW_SK, 0);
    return pp_do();
}

static unsigned int
mw_read_word(unsigned int addr, int abits)
{
    unsigned int v = 0;
    int i;

    mw_put(0);
    mw_bit(MW_CS, 1);
    mw_send(1);                        /* start bit */
    mw_send(1);                        /* opcode 10 = READ */
    mw_send(0);
    for (i = abits - 1; i >= 0; i--)
        mw_send((int) ((addr >> i) & 1));
    for (i = 0; i < 16; i++)
        v = (v << 1) | (unsigned int) mw_recv();
    mw_bit(MW_CS, 0);
    mw_put(0);
    return v & 0xFFFF;
}

/* ---- the password, and why a read can be trusted -------------------------------
 *
 * The record occupies words 8..63 and is stored as
 *
 *     word[i] = plain[i - 8] ^ (uint16)(i - 8) ^ pass1 ^ (i < 8 ? 0xFF00 : 0)
 *
 * Words 0..7 carry no record, so plain is zero there and each of those eight words is
 * pass1 XORed with a constant this program can compute:
 *
 *     pass1 = word[i] ^ (uint16)(i - 8) ^ 0xFF00 = word[i] ^ (0x00F8 + i)
 *
 * Eight independent readings of one 16-bit value.  If they all agree the memory read is
 * almost certainly right AND the password is known; if they do not, the read is wrong and
 * no number should be reported.  This is the calibration criterion for the whole memory
 * layer -- it needs no prior knowledge of which dongle is plugged in.
 */
static int
mw_pass1(const unsigned int w[64], unsigned int *pass1)
{
    unsigned int p = w[0] ^ 0x00F8u;
    int i;

    for (i = 1; i < 8; i++)
        if ((w[i] ^ (0x00F8u + (unsigned int) i)) != p)
            return 0;
    /* All ones or all zeros is what an absent part and a dead line both look like. */
    if ((w[0] == 0xFFFF) && (w[1] == 0xFFFF) && (w[2] == 0xFFFF))
        return 0;
    *pass1 = p & 0xFFFF;
    return 1;
}

/* Section 16 of the old handoff left three things open -- the bits idled on the other
   DATA lines, the address width, and whether the part answers at all from cold.  Rather
   than pick, sweep them and keep the configuration whose words 0..7 agree on a password.
   That is a real test, not "something moved". */
static int
mw_calibrate(unsigned int w[64], unsigned int *pass1, unsigned char *idle_out,
             int *abits_out)
{
    static const unsigned char idles[] = { 0x00, 0x80, 0x81, 0x01 };
    int im, ab, a;

    for (im = 0; im < (int) sizeof idles; im++) {
        for (ab = 6; ab <= 8; ab += 2) {
            g_mw_idle = idles[im];
            g_mw_latch = 0;
            for (a = 0; a < 8; a++)
                w[a] = mw_read_word((unsigned int) a, ab);
            if (!mw_pass1(w, pass1))
                continue;
            for (a = 8; a < 64; a++)
                w[a] = mw_read_word((unsigned int) a, ab);
            *idle_out  = idles[im];
            *abits_out = ab;
            return 1;
        }
    }
    return 0;
}

/* ========================================================= layer 3: the transform
 *
 * The picture cipher's keyed round.  Two framings exist and they clock on different
 * lines, so both are tried and the one that fits is reported:
 *
 *   variant A  (2001, I.G.O. 2, I.G.O. 5)  query clocks on DATA bit 4, bit 7 set;
 *              a round preamble is any DATA bit-0 rise with bit 7 set.
 *   variant B  (I.G.O. 3)                  query clocks on DATA bit 0, and the preamble
 *              is one specific payload, 0x46 -- wire C6.
 *
 * The payload mapping is the same either way, and the part only ever sees five bits of
 * it, so `q` below is written as the five bits directly.
 */
enum { TV_A = 0, TV_B = 1 };

#define T_PAYLOAD(q) ((unsigned char) ((((q) << 1) & 0x0E) | (((q) << 2) & 0x60) | 0x80))

static void
t_cmdbyte_A(unsigned char b)
{
    pp_out((unsigned char) ((b & 0xFE) | 0x80));
    pp_out((unsigned char) (b | 0x81));
    pp_out((unsigned char) ((b & 0xFE) | 0x80));
}

static void
t_preamble(int variant, unsigned char seed)
{
    if (variant == TV_A) {
        t_cmdbyte_A(seed);
        t_cmdbyte_A(0x4E);
        pp_out(0x84);
    } else {
        pp_out(0xC6);
        pp_out(0xC7);
        pp_out(0xC6);
    }
}

/* `extra` is only ever 0x10, and only ever in variant B.  There it matters:
 *
 * i5 is built from DATA bits 1..3 and 5..6, so bit 4 does not reach the part at all --
 * in variant A it is the clock, and in variant B it is spare.  But variant B's preamble
 * marker is the whole payload masked 0x7E, so the one payload that would ask for i5 = 19
 * is byte-for-byte the preamble and would reset the round instead of answering it.  Key
 * bit 19 is then unobservable and the system comes out one short of full rank -- which
 * is exactly what the self-test reported the first time it ran.
 *
 * Setting the spare bit moves that payload off the marker without changing i5.  It is
 * an assumption about the part (that bit 4 really is ignored), so it is not taken on
 * trust: those queries go into the same solve as all the others, and if the part does
 * consult bit 4 the system contradicts itself and the fit is reported as failed rather
 * than quietly wrong.
 */
static int
t_query(int variant, unsigned char q, unsigned char extra)
{
    unsigned char pay = (unsigned char) (T_PAYLOAD(q) | extra);

    pp_out(pay);
    pp_out((unsigned char) (pay | ((variant == TV_A) ? 0x10 : 0x01)));
    pp_out(pay);
    return pp_do();
}

/* The software part, for checking a recovered key against what was observed. */
static unsigned int
t_model_step(unsigned int *cur, unsigned int key, unsigned int i5)
{
    unsigned int st = (key >> i5) & 1u;
    unsigned int b0 = i5 ^ ((st ^ 1u) & (i5 >> 3)) ^ (i5 >> 4);
    unsigned int pre;

    b0 ^= *cur >> 10;
    b0 ^= *cur >> 7;
    if (i5 & 2)
        b0 ^= *cur >> 5;
    if (i5 & 4)
        b0 ^= *cur >> 8;

    pre  = *cur ^ ((i5 & 1u) << 2);
    *cur = ((pre << 1) | (b0 & 1u)) & 0xFFFu;
    return ((*cur >> 11) ^ st) & 1u;
}

/* ------------------------------------------------------------------ the software part
 *
 * The dongle, modelled, and driven through the same pp_out/pp_status the real one is --
 * so the recovery can be exercised with no hardware in the room.
 *
 * This is not a convenience.  The solver is the part of this program most likely to be
 * quietly wrong, and it is the part a misbehaving dongle cannot be told apart from:
 * both come back as "no fit".  Self-test settles that before a cabinet is blamed.  It
 * plants a known key in a simulated part and asks whether the same key comes back out.
 */
static int           g_sim_variant;
static unsigned int  g_sim_key, g_sim_init, g_sim_cur;
static unsigned char g_sim_last;

static void
sim_data(unsigned char val)
{
    unsigned char rose = (unsigned char) (val & ~g_sim_last);
    int query = 0, preamble = 0;

    if (!(val & 0x80))
        goto out;

    if (g_sim_variant == TV_A) {
        preamble = (rose & 0x01) != 0;
        query    = !preamble && ((rose & 0x10) != 0);
    } else {
        const unsigned char pay = (unsigned char) (val & 0x7E);

        preamble = (rose & 0x01) && (pay == 0x46);
        query    = (rose & 0x01) && (pay != 0x46);
    }

    if (preamble) {
        g_sim_cur     = g_sim_init;
        g_sim_pending = 0;
    } else if (query) {
        const unsigned int i5 = (unsigned int) (((val >> 1) & 0x07)
                                                | ((val >> 2) & 0x18));

        g_sim_ans     = (int) t_model_step(&g_sim_cur, g_sim_key, i5);
        g_sim_pending = 1;
    }

out:
    g_sim_last = val;
}

/* ------------------------------------------------------------------ the solver
 *
 * 43 unknowns: key bits 0..31 in column 0..31, initial register bits 0..10 in column
 * 32..42.  Bit 11 of the initial register is never consulted before it is shifted out,
 * so it is not an unknown -- it is unobservable, and claiming a value for it would be
 * inventing one.
 */
#define NUNK 43

typedef struct {
    unsigned int a[2];   /* a[0] = columns 0..31, a[1] = columns 32..42 */
    unsigned char c;     /* the constant term */
} lin_t;

static void
lin_zero(lin_t *l)
{
    l->a[0] = 0;
    l->a[1] = 0;
    l->c    = 0;
}

static void
lin_xor(lin_t *d, const lin_t *s)
{
    d->a[0] ^= s->a[0];
    d->a[1] ^= s->a[1];
    d->c    ^= s->c;
}

static int
lin_bit(const lin_t *l, int col)
{
    return (int) ((l->a[col >> 5] >> (col & 31)) & 1u);
}

static void
lin_set(lin_t *l, int col)
{
    l->a[col >> 5] |= 1u << (col & 31);
}

static int
lin_empty(const lin_t *l)
{
    return (l->a[0] | l->a[1]) == 0;
}

/* Elimination state: one pivot row per column, plus the count of rows that reduced to
   0 = 1.  A contradiction means the model does not describe this part, and it is
   reported rather than averaged away. */
typedef struct {
    lin_t piv[NUNK];
    int   have[NUNK];
    int   rank;
    int   contra;
    int   rows;
} gauss_t;

static void
gauss_init(gauss_t *g)
{
    int i;

    for (i = 0; i < NUNK; i++) {
        lin_zero(&g->piv[i]);
        g->have[i] = 0;
    }
    g->rank = g->contra = g->rows = 0;
}

static void
gauss_add(gauss_t *g, lin_t row)
{
    int c;

    g->rows++;
    for (c = 0; c < NUNK; c++) {
        if (!lin_bit(&row, c))
            continue;
        if (g->have[c]) {
            lin_xor(&row, &g->piv[c]);
            continue;
        }
        g->piv[c]  = row;
        g->have[c] = 1;
        g->rank++;
        return;
    }
    if (row.c)
        g->contra++;
}

/* Back-substitute into reduced row echelon form, then read the unknowns straight off
   the constant terms.  Returns 1 only when every unknown is pinned. */
static int
gauss_solve(gauss_t *g, unsigned int *key, unsigned int *init)
{
    int c, d;

    if (g->rank != NUNK)
        return 0;
    for (c = NUNK - 1; c >= 0; c--)
        for (d = 0; d < c; d++)
            if (lin_bit(&g->piv[d], c))
                lin_xor(&g->piv[d], &g->piv[c]);

    *key = *init = 0;
    for (c = 0; c < 32; c++)
        if (g->piv[c].c)
            *key |= 1u << c;
    for (c = 32; c < NUNK; c++)
        if (g->piv[c].c)
            *init |= 1u << (c - 32);
    return 1;
}

/* ---- one observation, turned into one equation --------------------------------
 *
 * `cur[]` carries the twelve register bits symbolically: each is a linear form over the
 * 43 unknowns plus a constant.  Feeding a query updates them exactly as the hardware
 * does, and the answer names one more equation.
 */
static void
sym_reset(lin_t cur[12])
{
    int i;

    for (i = 0; i < 12; i++) {
        lin_zero(&cur[i]);
        if (i < 11)
            lin_set(&cur[i], 32 + i);   /* initial register bit i */
        /* bit 11 starts as a hard zero -- see NUNK above */
    }
}

static lin_t
sym_step(lin_t cur[12], unsigned int i5)
{
    lin_t st, b0, ans;
    int   j;

    lin_zero(&st);
    lin_set(&st, (int) i5);              /* st = key bit i5 */

    /* answer = (new cur >> 11) ^ st, and the new bit 11 is the old bit 10 -- the
       constant the preamble injects only touches bit 2. */
    ans = cur[10];
    lin_xor(&ans, &st);

    lin_zero(&b0);
    b0.c ^= (unsigned char) (i5 & 1u);
    if (i5 & 8u) {                        /* (st ^ 1) & (i5 >> 3) */
        lin_xor(&b0, &st);
        b0.c ^= 1;
    }
    b0.c ^= (unsigned char) ((i5 >> 4) & 1u);
    lin_xor(&b0, &cur[10]);
    lin_xor(&b0, &cur[7]);
    if (i5 & 2u)
        lin_xor(&b0, &cur[5]);
    if (i5 & 4u)
        lin_xor(&b0, &cur[8]);

    cur[2].c ^= (unsigned char) (i5 & 1u);   /* cur ^= (i5 & 1) << 2 */
    for (j = 11; j >= 1; j--)
        cur[j] = cur[j - 1];
    cur[0] = b0;

    return ans;
}

/* ======================================================== the record, and what it says
 *
 * Words 8..63 descrambled are the 112-byte record.  There are three shapes and two byte
 * orders, and the pairing is not free -- 2001 unpacks each word high byte first and the
 * I.G.O. builds low byte first -- so both orders are tried and the one that parses is
 * the one the guest uses.  See docs/research-v2/07.
 */
enum { SH_NONE = 0, SH_2001, SH_SION, SH_VERS };

#define REC_BYTES 112

static void
descramble(const unsigned int w[64], unsigned int pass1, int swap,
           unsigned char rec[REC_BYTES])
{
    int j;

    for (j = 0; j < 56; j++) {
        unsigned int p = (w[j + 8] ^ (unsigned int) j ^ pass1) & 0xFFFFu;

        if (swap) {
            rec[j * 2]     = (unsigned char) (p & 0xFF);
            rec[j * 2 + 1] = (unsigned char) (p >> 8);
        } else {
            rec[j * 2]     = (unsigned char) (p >> 8);
            rec[j * 2 + 1] = (unsigned char) (p & 0xFF);
        }
    }
}

static int
printable2(unsigned char a, unsigned char b)
{
    return (a >= 'A') && (a <= 'Z') && (b >= 'A') && (b <= 'Z');
}

/* Returns the shape, and fills `ver` (up to 8 chars) and `terr` (2 chars + NUL). */
static int
rec_identify(const unsigned char rec[REC_BYTES], unsigned int pass1,
             char *ver, char *terr)
{
    int i;

    ver[0] = 0;
    terr[0] = 0;

    /* I.G.O. 5, 6, 7 and Italy:  "PT" '-' "Version" <token> 00 ')' 00
       The token at bytes 10..14 is the version the menu prints -- 2005B, 2006A, 08IT. */
    if ((rec[2] == '-') && !zcmpn((const char *) rec + 3, "Version", 7)
        && printable2(rec[0], rec[1])) {
        int n = 0;

        for (i = 10; i < 15; i++) {
            if ((rec[i] < 0x20) || (rec[i] > 0x7E) || (rec[i] == ' '))
                break;
            ver[n++] = (char) rec[i];
        }
        ver[n]  = 0;
        terr[0] = (char) rec[0];
        terr[1] = (char) rec[1];
        terr[2] = 0;
        return n ? SH_VERS : SH_NONE;
    }

    /* I.G.O. 2 and 3:  "PT" 00 "sion 2000 (SP)" 00
       The record cannot tell 2002 from 2003 -- both hold the identical string, written
       over a 2000-era banner.  The password does: 68BB is I.G.O. 2, 6B91 is I.G.O. 3. */
    if ((rec[2] == 0x00) && !zcmpn((const char *) rec + 3, "sion 200", 8)
        && printable2(rec[0], rec[1])) {
        const char *v = (pass1 == 0x68BB) ? "2002"
                      : (pass1 == 0x6B91) ? "2003"
                                          : "200x";

        for (i = 0; i < 4; i++)
            ver[i] = v[i];
        ver[4]  = 0;
        terr[0] = (char) rec[0];
        terr[1] = (char) rec[1];
        terr[2] = 0;
        return SH_SION;
    }

    /* 2001: a 30-column field holding "Version 2001 (ES)" right-aligned. */
    for (i = 0; i + 12 < 30; i++) {
        if (zcmpn((const char *) rec + i, "Version ", 8))
            continue;
        {
            int n = 0;

            for (int j = i + 8; (j < 30) && (n < 5); j++) {
                if ((rec[j] < '0') || (rec[j] > '9'))
                    break;
                ver[n++] = (char) rec[j];
            }
            ver[n] = 0;
        }
        for (int j = i; j < 30 - 3; j++) {
            if (rec[j] != '(')
                continue;
            if (!printable2(rec[j + 1], rec[j + 2]))
                continue;
            terr[0] = (char) rec[j + 1];
            terr[1] = (char) rec[j + 2];
            terr[2] = 0;
            break;
        }
        return ver[0] ? SH_2001 : SH_NONE;
    }

    return SH_NONE;
}

/* The content keys.  2001 writes them as zero-padded decimal columns after the banner,
   the I.G.O. shapes as eight little-endian dwords at byte 30 -- the same eight slots
   either way, and the games read them at fixed offsets (docs/research-v2/07.2). */
static void
rec_values(const unsigned char rec[REC_BYTES], int shape, int val[8])
{
    static const int at[8]   = { 30, 36, 42, 48, 54, 60, 66, 72 };
    static const int cols[8] = {  6,  6,  6,  6,  6,  6,  6, 11 };
    int n;

    for (n = 0; n < 8; n++) {
        if (shape == SH_2001) {
            int v = 0, neg = 0, i;

            for (i = at[n]; i < at[n] + cols[n]; i++) {
                unsigned char c = rec[i];

                if (c == ' ')
                    continue;
                if (c == '-') {
                    neg = 1;
                    continue;
                }
                if ((c < '0') || (c > '9')) {
                    v = 0;
                    break;
                }
                v = v * 10 + (int) (c - '0');
            }
            val[n] = neg ? -v : v;
        } else {
            const int o = 30 + n * 4;

            val[n] = (int) ((unsigned int) rec[o]
                            | ((unsigned int) rec[o + 1] << 8)
                            | ((unsigned int) rec[o + 2] << 16)
                            | ((unsigned int) rec[o + 3] << 24));
        }
    }
}

/* pass2 is never on this wire.  It is an argument the library passes, not something the
   part hands back, so it is only ever reported as what the known pairing implies -- and
   labelled as such.  An unrecognised pass1 gets no second password at all rather than a
   plausible-looking guess. */
static const char *
pass2_for(unsigned int pass1, unsigned int *p2)
{
    switch (pass1) {
        case 0x7477: *p2 = 0x7D57; return "known pairing (Photo Play 2001)";
        case 0x68BB: *p2 = 0x1329; return "known pairing (I.G.O. 2 / 6 / 7)";
        case 0x6B91: *p2 = 0x24A3; return "known pairing (I.G.O. 3 / 5)";
        default:     *p2 = 0;      return "UNKNOWN -- this pass1 is not in the table";
    }
}

/* ====================================================================== text output */

typedef struct {
    char  *p;
    size_t n;
    size_t cap;
} buf_t;

static void
buf_puts(buf_t *b, const char *s)
{
    size_t n = zlen(s);

    if (!b->p || (b->n + n >= b->cap))
        return;
    memcpy(b->p + b->n, s, n);
    b->n += n;
}

static void
buf_fmt(buf_t *b, const char *f, ...)
{
    char    line[1024];
    va_list ap;

    va_start(ap, f);
    zvfmt(line, sizeof line, f, ap);
    va_end(ap);
    buf_puts(b, line);
}

static int
write_file(const char *dir, const char *name, const void *data, DWORD len)
{
    char   path[MAX_PATH];
    HANDLE h;
    DWORD  wrote = 0;

    zfmt(path, sizeof path, "%s\\%s", dir, name);
    h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    WriteFile(h, data, len, &wrote, NULL);
    CloseHandle(h);
    return wrote == len;
}

/* ================================================================ the dump itself */

#define T_ROUNDS  128           /* rounds driven into the solve */
#define T_HOLDOUT 16            /* ...and rounds held back to check the answer with */
#define T_QPR     44            /* queries per round */
#define T_OBS     ((T_ROUNDS + T_HOLDOUT) * T_QPR)

/* Everything one run measures.  Static rather than stacked: this is a no-CRT build with
   stack probes disabled, so a few hundred KB of locals would be a fault, not a warning. */
static struct {
    int           have_port;
    unsigned char idle_status;

    unsigned char ramp_lo[64];      /* the ramp driven as 00, 02 ... 7E */
    unsigned char ramp_hi[64];      /* ...and as 80, 82 ... FE */
    unsigned int  acc_lo, acc_hi;
    int           ramp_agree;
    unsigned int  sig_lo, sig_hi;   /* the signature, low and high halves */

    unsigned char sweep[64];
    unsigned int  sweep_lo, sweep_hi;

    int           live_hi, live_lo;

    int           mem_ok;
    unsigned int  words[64];
    unsigned int  pass1, pass2;
    const char   *pass2_note;
    unsigned char mw_idle;
    int           mw_abits;

    int           shape;
    int           swap;
    unsigned char rec[REC_BYTES];
    char          ver[16];
    char          terr[4];
    int           val[8];

    int           t_variant;        /* -1 if neither framing fitted */
    unsigned char t_seed;
    unsigned int  t_key;
    unsigned int  t_init;
    int           t_rank, t_contra;
    int           t_agree, t_total;
    int           t_hold_agree, t_hold_total;
    int           t_seed_stable;    /* the same t_init came back for every seed tried */
    unsigned int  t_seed_init[4];
    unsigned char t_seed_val[4];
    int           t_nseeds;

    unsigned char obs_q[T_OBS];
    unsigned char obs_a[T_OBS];
    int           obs_n;
} R;

static unsigned int g_rng = 0x1B7F0C3Du;

static unsigned int
rnd(void)
{
    g_rng = g_rng * 1664525u + 1013904223u;
    return g_rng >> 16;
}

/* ---- the transform, one framing and one preamble seed --------------------------
 *
 * Drives rounds of random queries, turns each answer into an equation, and solves.  The
 * held-back rounds never enter the solve, so agreeing with them is a real prediction and
 * not a restatement of the input.
 */
static int
transform_try(int variant, unsigned char seed, unsigned int *key, unsigned int *init,
              int *rank, int *contra, int *agree, int *total,
              int *hagree, int *htotal, int record_obs)
{
    gauss_t g;
    lin_t   cur[12];
    int     r, j;
    static unsigned char q[T_ROUNDS + T_HOLDOUT][T_QPR];
    static unsigned char a[T_ROUNDS + T_HOLDOUT][T_QPR];

    gauss_init(&g);
    *agree = *total = *hagree = *htotal = 0;

    for (r = 0; r < T_ROUNDS + T_HOLDOUT; r++) {
        /* The first few rounds are worth having in the wire log; the rest are not, and
           at three writes and a read per query they would swamp it. */
        g_wire_on = (r < 4) && g_wire;

        t_preamble(variant, seed);
        sym_reset(cur);
        for (j = 0; j < T_QPR; j++) {
            unsigned char qq = (unsigned char) (rnd() & 0x1F);
            unsigned char extra = 0;

            /* Variant B reserves one payload for the preamble; see t_query. */
            if ((variant == TV_B) && (T_PAYLOAD(qq) == 0xC6))
                extra = 0x10;

            q[r][j] = qq;
            a[r][j] = (unsigned char) t_query(variant, qq, extra);

            if (r < T_ROUNDS) {
                lin_t eq = sym_step(cur, (unsigned int) qq);

                eq.c ^= a[r][j];
                gauss_add(&g, eq);
            }
        }
    }
    g_wire_on = (g_wire != NULL);

    *rank   = g.rank;
    *contra = g.contra;
    *key    = 0;
    *init   = 0;
    if (!gauss_solve(&g, key, init)) {
        /* Leave the outputs at zero rather than at whatever the last call left there:
           a partial solve has no key, and a stale one printed as this run's answer is
           worse than no answer. */
        *key = *init = 0;
        return 0;
    }

    /* Replay the recovered part against every observation, solved and held back. */
    for (r = 0; r < T_ROUNDS + T_HOLDOUT; r++) {
        unsigned int c = *init;

        for (j = 0; j < T_QPR; j++) {
            unsigned int pred = t_model_step(&c, *key, (unsigned int) q[r][j]);
            int          hit  = (pred == (unsigned int) a[r][j]);

            if (r < T_ROUNDS) {
                (*total)++;
                *agree += hit;
            } else {
                (*htotal)++;
                *hagree += hit;
            }
        }
    }

    if (record_obs) {
        R.obs_n = 0;
        for (r = 0; r < T_ROUNDS + T_HOLDOUT; r++)
            for (j = 0; j < T_QPR; j++) {
                R.obs_q[R.obs_n] = q[r][j];
                R.obs_a[R.obs_n] = a[r][j];
                R.obs_n++;
            }
    }
    return (*contra == 0) && (*agree == *total) && (*hagree == *htotal);
}

/* ---- log sink, so the phases can narrate into the window ----------------------- */

static void ui_log(const char *s);

static void
lg(const char *f, ...)
{
    char    line[1024];
    va_list ap;

    va_start(ap, f);
    zvfmt(line, sizeof line, f, ap);
    va_end(ap);
    ui_log(line);
}

static unsigned int
bits_to_u32(const unsigned char *b)
{
    unsigned int v = 0;
    int i;

    /* The bit for address i lands in bit i, which is how HD_SIGNATURE is written in
       src/device/dongle_photoplay.c -- so the two can be compared by eye. */
    for (i = 0; i < 32; i++)
        v |= ((unsigned int) (b[i] & 1)) << i;
    return v;
}

static unsigned int
bits_msb_u32(const unsigned char *b)
{
    unsigned int v = 0;
    int i;

    /* The sweep answer is quoted MSB first -- F5 7A 37 E7 8F 8F BD DA. */
    for (i = 0; i < 32; i++)
        v = (v << 1) | (unsigned int) (b[i] & 1);
    return v;
}

/* --------------------------------------------------------------- session phase */

static void
phase_session(void)
{
    int i;

    R.idle_status = raw_in((unsigned short) (g_base + 1));
    lg("Idle STATUS at %03X: %02X\r\n", (unsigned int) g_base,
       (unsigned int) R.idle_status);

    session_ramp(0, R.ramp_lo);
    session_ramp(1, R.ramp_hi);
    R.acc_lo = ramp_acc(R.ramp_lo);
    R.acc_hi = ramp_acc(R.ramp_hi);
    R.ramp_agree = 1;
    for (i = 0; i < 64; i++)
        if (R.ramp_lo[i] != R.ramp_hi[i])
            R.ramp_agree = 0;

    R.sig_lo = bits_to_u32(R.ramp_lo);
    R.sig_hi = bits_to_u32(R.ramp_lo + 32);

    lg("Identity: signature %08X%08X, acc %02X\r\n", R.sig_hi, R.sig_lo, R.acc_lo);
    lg("  bit 7 set drives the same ramp: acc %02X, %s\r\n", R.acc_hi,
       R.ramp_agree ? "identical answer" : "*** DIFFERENT ANSWER ***");
    if ((R.sig_hi == 0xCEFF0AFFu) && (R.sig_lo == 0xCECE0A0Au))
        lg("  matches the measured 68BB/1329 part exactly.\r\n");
    if ((R.acc_lo != 0x08) && (R.acc_lo != 0x0C) && (R.acc_lo != 0x18)
        && (R.acc_lo != 0x1C))
        lg("  WARNING: acc %02X is not in the library's table "
           "(0008, 000C, 0018, 001C) -- a real cabinet would give up here.\r\n",
           R.acc_lo);

    session_liveness(&R.live_hi, &R.live_lo);
    lg("Liveness: 1E gives %d, 1C gives %d%s\r\n", R.live_hi, R.live_lo,
       ((R.live_hi == 1) && (R.live_lo == 0)) ? "  (as measured)"
                                              : "  *** expected 1 then 0 ***");

    session_sweep(R.sweep);
    R.sweep_hi = bits_msb_u32(R.sweep);
    R.sweep_lo = bits_msb_u32(R.sweep + 32);
    lg("Sweep: %08X%08X%s\r\n", R.sweep_hi, R.sweep_lo,
       ((R.sweep_hi == 0xF57A37E7u) && (R.sweep_lo == 0x8F8FBDDAu))
           ? "  (as measured)" : "  -- NEW, this part answers differently");
}

/* ---------------------------------------------------------------- memory phase */

static void
phase_memory(void)
{
    int try_swap;

    R.mem_ok = mw_calibrate(R.words, &R.pass1, &R.mw_idle, &R.mw_abits);
    if (!R.mem_ok) {
        lg("Memory: no configuration produced eight agreeing copies of the password.\r\n"
           "  The record was NOT read.  wire.log has every access.\r\n");
        return;
    }
    lg("Memory: 64 words read (idle %02X, %d address bits).\r\n",
       (unsigned int) R.mw_idle, R.mw_abits);
    R.pass2_note = pass2_for(R.pass1, &R.pass2);
    lg("  pass1 %04X -- measured, eight agreeing copies in words 0..7.\r\n", R.pass1);
    lg("  pass2 %04X -- %s.\r\n", R.pass2, R.pass2_note);

    /* Both byte orders, and whichever parses is the one the guest unpacks with. */
    R.shape = SH_NONE;
    for (try_swap = 0; try_swap <= 1; try_swap++) {
        unsigned char rec[REC_BYTES];
        char          ver[16], terr[4];
        int           sh;

        descramble(R.words, R.pass1, try_swap, rec);
        sh = rec_identify(rec, R.pass1, ver, terr);
        if (sh == SH_NONE)
            continue;
        R.shape = sh;
        R.swap  = try_swap;
        memcpy(R.rec, rec, REC_BYTES);
        memcpy(R.ver, ver, sizeof ver);
        memcpy(R.terr, terr, sizeof terr);
        break;
    }

    if (R.shape == SH_NONE) {
        /* Keep the low-first reading so record.bin is still written and can be looked
           at offline; just do not claim to have identified anything. */
        descramble(R.words, R.pass1, 1, R.rec);
        R.swap    = 1;
        R.ver[0]  = 0;
        R.terr[0] = 0;
        lg("  Record: neither byte order parses as a known shape.  "
           "record.bin is written raw.\r\n");
        return;
    }

    rec_values(R.rec, R.shape, R.val);
    lg("  Record: %s shape, %s byte order -- %s (%s)\r\n",
       (R.shape == SH_2001) ? "2001"
       : (R.shape == SH_SION) ? "I.G.O. sion" : "I.G.O. Version",
       R.swap ? "low-first" : "high-first", R.ver, R.terr);
    /* The block starts at record byte 30 and its first entry is what the 1999 layout
       called v[1], so the game that reads +1C is looking at dword 2, not dword 3.
       Checked against all nine h5dmp dumps: dword 2 is 0001D760 on every one. */
    lg("  FINDIT reads +1C = %08X, MOSAIC +20 = %08X, FMEMO +24 = %08X\r\n",
       (unsigned int) R.val[2], (unsigned int) R.val[3], (unsigned int) R.val[4]);
    if ((unsigned int) R.val[2] != 0x0001D760u)
        lg("  WARNING: FINDIT's key is not 0001D760.  On every dongle dumped so far it "
           "is.  Check the byte order before trusting this.\r\n");
}

/* ------------------------------------------------------------- transform phase */

static void
phase_transform(void)
{
    static const unsigned char seeds[4] = { 0x00, 0x4E, 0xA5, 0x5A };
    int variant;

    R.t_variant = -1;

    for (variant = TV_A; variant <= TV_B; variant++) {
        unsigned int key, init;
        int rank, contra, ag, tot, hag, htot;

        lg("Transform: trying variant %s...\r\n",
           (variant == TV_A) ? "A, query clocks on DATA bit 4"
                             : "B, query clocks on DATA bit 0");

        if (!transform_try(variant, 0x00, &key, &init, &rank, &contra,
                           &ag, &tot, &hag, &htot, 1)) {
            lg("  no fit: rank %d of %d, %d contradictions, %d of %d observations, "
               "%d of %d held back.\r\n", rank, NUNK, contra, ag, tot, hag, htot);
            continue;
        }

        R.t_variant    = variant;
        R.t_seed       = 0x00;
        R.t_key        = key;
        R.t_init       = init;
        R.t_rank       = rank;
        R.t_contra     = contra;
        R.t_agree      = ag;
        R.t_total      = tot;
        R.t_hold_agree = hag;
        R.t_hold_total = htot;
        lg("  key %08X, initial register %03X\r\n", key, init);
        lg("  %d of %d observations reproduced, %d of %d held back -- "
           "the held-back rounds never entered the solve.\r\n", ag, tot, hag, htot);
        break;
    }

    if (R.t_variant < 0) {
        lg("Transform: neither framing fitted the model.  "
           "transform-raw.bin holds every (query, answer) pair regardless -- "
           "that is the finding, not a failure to record it.\r\n");
        return;
    }

    /* Does the preamble's seed byte change what the round starts from?  The model says
       no -- any bit-0 rise resets the register to one fixed value -- and dongcap having
       had to calibrate a seed says it might.  Cheap to settle, so settle it. */
    if (R.t_variant == TV_A) {
        int s;

        R.t_nseeds      = 0;
        R.t_seed_stable = 1;
        for (s = 0; s < 4; s++) {
            unsigned int key, init;
            int rank, contra, ag, tot, hag, htot;

            if (!transform_try(TV_A, seeds[s], &key, &init, &rank, &contra,
                               &ag, &tot, &hag, &htot, 0))
                continue;
            R.t_seed_val[R.t_nseeds]  = seeds[s];
            R.t_seed_init[R.t_nseeds] = init;
            R.t_nseeds++;
            if ((key != R.t_key) || (init != R.t_init))
                R.t_seed_stable = 0;
        }
        lg("  preamble seed: %d of 4 seeds fitted, %s\r\n", R.t_nseeds,
           R.t_seed_stable ? "all giving the same key and register"
                           : "*** and they do NOT agree -- the seed matters ***");
    }
}

/* ============================================================ writing the results
 *
 * The folder is <version>-<language>-<date and time>, and everything a run learned goes
 * inside it.  A phase that failed still writes its raw data: a dump that only says "it
 * did not work" is worth nothing offline, and the raw wire is what says why.
 *
 * Deliberately NOT written: an h5dmp-shaped hasp.dmp.  Those carry a 166-byte crypto
 * table at offset 0x009 which no part of the game's protocol ever puts on the wire, so
 * this program cannot fill it.  Emitting the file with that region zeroed would produce
 * something indistinguishable from a real dump at a glance and wrong where it matters.
 * transform.txt carries the functional equivalent -- the key the table computes with.
 */

static char g_outdir[MAX_PATH];

static void
name_folder(char *out, size_t cap)
{
    SYSTEMTIME st;
    const char *ver  = R.ver[0] ? R.ver : "unknown";
    const char *terr = R.terr[0] ? R.terr : "XX";

    GetLocalTime(&st);
    zfmt(out, cap, "%s-%s-%04u%02u%02u-%02u%02u%02u",
         ver, terr,
         (unsigned int) st.wYear, (unsigned int) st.wMonth, (unsigned int) st.wDay,
         (unsigned int) st.wHour, (unsigned int) st.wMinute, (unsigned int) st.wSecond);
}

static void
hexdump(buf_t *b, const unsigned char *p, int n)
{
    int i;

    for (i = 0; i < n; i += 16) {
        int j;

        buf_fmt(b, "%04X  ", (unsigned int) i);
        for (j = 0; j < 16; j++) {
            if (i + j < n)
                buf_fmt(b, "%02X ", (unsigned int) p[i + j]);
            else
                buf_puts(b, "   ");
        }
        buf_puts(b, " |");
        for (j = 0; (j < 16) && (i + j < n); j++) {
            unsigned char c = p[i + j];

            buf_fmt(b, "%c", ((c >= 0x20) && (c < 0x7F)) ? (char) c : '.');
        }
        buf_puts(b, "|\r\n");
    }
}

static const char *
shape_name(int s)
{
    return (s == SH_2001) ? "2001 (30-column right-aligned banner, decimal columns)"
         : (s == SH_SION) ? "I.G.O. sion (territory over a 2000-era banner)"
         : (s == SH_VERS) ? "I.G.O. Version (territory, '-', Version, token)"
                          : "unrecognised";
}

/* One big report.  This is the file to read first and the one to send back. */
static void
write_summary(buf_t *b)
{
    int i;

    buf_puts(b, "DONGDUMP -- funworld parallel HASP4, everything the emulator needs\r\n");
    buf_puts(b, "=================================================================\r\n\r\n");
    buf_fmt(b, "Port          0x%03X\r\n", (unsigned int) g_base);
    buf_fmt(b, "Backend       %s\r\n", g_backend_why);
    buf_fmt(b, "Idle STATUS   %02X\r\n\r\n", (unsigned int) R.idle_status);

    buf_puts(b, "IDENTITY (session layer)\r\n");
    buf_fmt(b, "  signature   %08X%08X   (bit i = the answer at address i)\r\n",
            R.sig_hi, R.sig_lo);
    buf_fmt(b, "  acc         %02X          %s\r\n", R.acc_lo,
            ((R.acc_lo == 0x08) || (R.acc_lo == 0x0C) || (R.acc_lo == 0x18)
             || (R.acc_lo == 0x1C))
                ? "in the library's four-entry table"
                : "NOT in the table 0008/000C/0018/001C -- a cabinet would give up");
    buf_fmt(b, "  bit-7 ramp  acc %02X, %s\r\n", R.acc_hi,
            R.ramp_agree ? "identical bit for bit" : "DIFFERENT from the bit-7-clear ramp");
    buf_fmt(b, "  known part  %s\r\n",
            ((R.sig_hi == 0xCEFF0AFFu) && (R.sig_lo == 0xCECE0A0Au))
                ? "matches the measured 68BB/1329 signature CEFF0AFFCECE0A0A"
                : "does NOT match CEFF0AFFCECE0A0A -- new measurement");
    buf_fmt(b, "  liveness    1E gives %d, 1C gives %d   (a live part answers 1 then 0)\r\n",
            R.live_hi, R.live_lo);
    buf_fmt(b, "  sweep       %08X%08X   %s\r\n\r\n", R.sweep_hi, R.sweep_lo,
            ((R.sweep_hi == 0xF57A37E7u) && (R.sweep_lo == 0x8F8FBDDAu))
                ? "matches the measured F57A37E78F8FBDDA"
                : "NEW -- differs from the only sweep measured so far");

    buf_puts(b, "MEMORY (Microwire, 64 words, read-only)\r\n");
    if (!R.mem_ok) {
        buf_puts(b, "  FAILED -- no idle/address-width configuration produced eight\r\n"
                    "  agreeing copies of the password in words 0..7, so nothing here\r\n"
                    "  can be trusted and no record was decoded.  See wire.log.\r\n\r\n");
    } else {
        buf_fmt(b, "  read with   idle %02X on the spare DATA lines, %d address bits\r\n",
                (unsigned int) R.mw_idle, R.mw_abits);
        buf_fmt(b, "  pass1       %04X  MEASURED -- words 0..7 are pass1 XOR a known\r\n"
                   "                    constant, and all eight agreed\r\n", R.pass1);
        buf_fmt(b, "  pass2       %04X  %s\r\n", R.pass2, R.pass2_note);
        buf_fmt(b, "  byte order  %s (the guest unpacks each word this way)\r\n",
                R.swap ? "low byte first" : "high byte first");
        buf_fmt(b, "  shape       %s\r\n", shape_name(R.shape));
        buf_fmt(b, "  release     %s\r\n", R.ver[0] ? R.ver : "not identified");
        buf_fmt(b, "  territory   %s\r\n", R.terr[0] ? R.terr : "not identified");
        if (R.shape == SH_SION)
            buf_puts(b, "              (the record cannot separate 2002 from 2003 -- both\r\n"
                        "               hold the same string; the password is what does)\r\n");
        buf_puts(b, "\r\n  content keys -- eight dwords from record byte 30, and where\r\n"
                    "  each one lands once the library has filed the record:\r\n");
        for (i = 0; i < 8; i++) {
            const char *who = (i == 2) ? "  FINDIT's level database"
                            : (i == 3) ? "  MOSAIC's"
                            : (i == 4) ? "  FMEMO's"
                            : (i == 7) ? "  per-unit, no game reads it"
                                       : "";

            buf_fmt(b, "    dword %d  at +%02X  %08X  %11d%s\r\n", i,
                    (unsigned int) (0x1C + (i - 2) * 4), (unsigned int) R.val[i],
                    R.val[i], who);
        }
        buf_puts(b, "\r\n");
    }

    buf_puts(b, "TRANSFORM (the picture cipher's keyed round)\r\n");
    if (R.t_variant < 0) {
        buf_puts(b, "  NO FIT.  Neither framing produced a consistent system, so this\r\n"
                    "  part does not behave as docs/research-v2/05.4 describes -- or the\r\n"
                    "  wire sequence here is wrong.  transform-raw.bin has every\r\n"
                    "  (query, answer) pair; solve it offline.\r\n\r\n");
    } else {
        buf_fmt(b, "  framing     variant %s\r\n",
                (R.t_variant == TV_A) ? "A -- query clocks on DATA bit 4, preamble is "
                                        "any bit-0 rise (2001, I.G.O. 2, I.G.O. 5)"
                                      : "B -- query clocks on DATA bit 0, preamble is "
                                        "payload 46 (I.G.O. 3)");
        buf_fmt(b, "  key         %08X\r\n", R.t_key);
        buf_fmt(b, "  initial     %03X\r\n", R.t_init);
        buf_fmt(b, "  fit         rank %d of %d, %d contradictions\r\n",
                R.t_rank, NUNK, R.t_contra);
        buf_fmt(b, "  reproduces  %d of %d observations\r\n", R.t_agree, R.t_total);
        buf_fmt(b, "  predicts    %d of %d answers in rounds held back from the solve\r\n",
                R.t_hold_agree, R.t_hold_total);
        if (R.t_variant == TV_A) {
            buf_fmt(b, "  seed test   %d of 4 preamble seeds fitted; %s\r\n", R.t_nseeds,
                    R.t_seed_stable ? "all gave the same key and register, so the seed "
                                      "byte does not select the state"
                                    : "they DISAGREE -- the seed byte does select the "
                                      "state, and each is listed below");
            for (i = 0; i < R.t_nseeds; i++)
                buf_fmt(b, "                seed %02X -> initial %03X\r\n",
                        (unsigned int) R.t_seed_val[i], R.t_seed_init[i]);
        }
        buf_puts(b, "\r\n");
    }

    buf_puts(b, "FOR src/device/dongle_photoplay.c\r\n");
    if (R.mem_ok && (R.shape != SH_NONE)) {
        buf_fmt(b, "  hd_keys[] row:  pass1 %04X, swap %d, shape %s,\r\n",
                R.pass1, R.swap,
                (R.shape == SH_2001) ? "HD_R2001"
                : (R.shape == SH_SION) ? "HD_RSION" : "HD_RVERS");
        buf_fmt(b, "                  v6 %d, v7 %d,\r\n", R.val[6], R.val[7]);
        if (R.t_variant >= 0)
            buf_fmt(b, "                  tkey 0x%08X, tinit 0x%03X, tclk 0x%02X%s\r\n",
                    R.t_key, R.t_init, (R.t_variant == TV_A) ? 0x10 : 0x01,
                    (R.t_variant == TV_B) ? ", tpre 0x46" : "");
        else
            buf_puts(b, "                  tkey/tinit: not recovered, see above\r\n");
    } else {
        buf_puts(b, "  Not enough was read to propose a row.\r\n");
    }
    buf_puts(b, "\r\n"
                "A completed handshake is not a passing check.  Nothing here is proven\r\n"
                "until a cabinet boots on it with pictures on screen.\r\n");
}

static void
write_outputs(const char *dir)
{
    static char  txt[262144];
    static unsigned char raw[4096];
    buf_t b;
    int   i;

    b.p = txt; b.cap = sizeof txt; b.n = 0;
    write_summary(&b);
    write_file(dir, "SUMMARY.txt", txt, (DWORD) b.n);

    /* --- session --- */
    b.n = 0;
    buf_fmt(&b, "identity ramp, bit 7 clear (DATA 00, 02 ... 7E)\r\n");
    for (i = 0; i < 64; i++)
        buf_fmt(&b, "  addr %2d  data %02X  DO %d\r\n", i, (unsigned int) (i << 1),
                R.ramp_lo[i]);
    buf_fmt(&b, "\r\nidentity ramp, bit 7 set (DATA 80, 82 ... FE)\r\n");
    for (i = 0; i < 64; i++)
        buf_fmt(&b, "  addr %2d  data %02X  DO %d\r\n", i,
                (unsigned int) ((i << 1) | 0x80), R.ramp_hi[i]);
    buf_fmt(&b, "\r\nsignature %08X%08X   acc %02X (bit 7 set: %02X)\r\n",
            R.sig_hi, R.sig_lo, R.acc_lo, R.acc_hi);
    buf_fmt(&b, "\r\nliveness: 1E -> %d, 1C -> %d\r\n\r\n", R.live_hi, R.live_lo);
    buf_puts(&b, "64-step sweep\r\n");
    for (i = 0; i < 64; i++)
        buf_fmt(&b, "  step %2d  wrote %02X  DO %d\r\n", i,
                (unsigned int) sweep_w[i], R.sweep[i]);
    buf_fmt(&b, "\r\nsweep answer, MSB first: %08X%08X\r\n", R.sweep_hi, R.sweep_lo);
    write_file(dir, "session.txt", txt, (DWORD) b.n);

    /* --- memory --- */
    if (R.mem_ok) {
        for (i = 0; i < 64; i++) {
            raw[i * 2]     = (unsigned char) (R.words[i] & 0xFF);
            raw[i * 2 + 1] = (unsigned char) (R.words[i] >> 8);
        }
        write_file(dir, "memory.raw", raw, 128);

        b.n = 0;
        buf_fmt(&b, "64 words as read, and descrambled with pass1 %04X.\r\n", R.pass1);
        buf_puts(&b, "Words 0..7 hold no record: each is pass1 XOR a known constant,\r\n"
                     "which is where the password comes from.\r\n\r\n");
        for (i = 0; i < 64; i++) {
            unsigned int plain = (R.words[i] ^ (unsigned int) (i - 8) ^ R.pass1
                                  ^ ((i < 8) ? 0xFF00u : 0u)) & 0xFFFFu;

            buf_fmt(&b, "  word %2d  raw %04X  plain %04X%s\r\n", i, R.words[i], plain,
                    (i < 8) ? "   <- must be 0000" : "");
        }
        write_file(dir, "memory.txt", txt, (DWORD) b.n);

        write_file(dir, "record.bin", R.rec, REC_BYTES);

        b.n = 0;
        buf_fmt(&b, "record, %s byte order, %s\r\n\r\n",
                R.swap ? "low-first" : "high-first", shape_name(R.shape));
        hexdump(&b, R.rec, REC_BYTES);
        buf_puts(&b, "\r\ncontent keys -- eight dwords from record byte 30.  The games do\r\n"
                     "not walk a struct: each reads one absolute offset, hardcoded at\r\n"
                     "compile time, into the block the library files.\r\n");
        for (i = 0; i < 8; i++)
            buf_fmt(&b, "  dword %d  record byte %d  read at +%02X = %08X = %d\r\n", i,
                    30 + i * 4, (unsigned int) (0x1C + (i - 2) * 4),
                    (unsigned int) R.val[i], R.val[i]);
        write_file(dir, "record.txt", txt, (DWORD) b.n);
    }

    /* --- transform --- */
    b.n = 0;
    if (R.t_variant >= 0) {
        buf_fmt(&b, "key      %08X\r\n", R.t_key);
        buf_fmt(&b, "initial  %03X\r\n", R.t_init);
        buf_fmt(&b, "framing  variant %c (query clock on DATA bit %d)\r\n",
                (R.t_variant == TV_A) ? 'A' : 'B', (R.t_variant == TV_A) ? 4 : 0);
        buf_fmt(&b, "fit      rank %d of %d, %d contradictions, %d/%d solved, "
                    "%d/%d held back\r\n\r\n",
                R.t_rank, NUNK, R.t_contra, R.t_agree, R.t_total,
                R.t_hold_agree, R.t_hold_total);
    } else {
        buf_puts(&b, "NO FIT -- see SUMMARY.txt.  The observations below are still\r\n"
                     "complete and can be solved offline.\r\n\r\n");
    }
    buf_puts(&b, "observations: query byte (five bits reach the part), answer bit\r\n");
    for (i = 0; i < R.obs_n; i++) {
        if ((i % T_QPR) == 0)
            buf_fmt(&b, "\r\n round %d\r\n", i / T_QPR);
        buf_fmt(&b, "  %02X %d\r\n", (unsigned int) R.obs_q[i], R.obs_a[i]);
    }
    write_file(dir, "transform.txt", txt, (DWORD) b.n);

    /* Two bytes per observation, so an offline solver need not parse the text. */
    {
        static unsigned char tr[T_OBS * 2];

        for (i = 0; i < R.obs_n; i++) {
            tr[i * 2]     = R.obs_q[i];
            tr[i * 2 + 1] = R.obs_a[i];
        }
        write_file(dir, "transform-raw.bin", tr, (DWORD) (R.obs_n * 2));
    }

    if (g_wire && g_wire_n)
        write_file(dir, "wire.log", g_wire, (DWORD) g_wire_n);
}

/* ============================================================================ GUI
 *
 * Plain Win32, no common controls and no manifest, so it draws on XP exactly as it does
 * on 11.  The log is an edit control because that is the thing an operator can select,
 * copy and paste back.
 */
#define ID_PORT   1001
#define ID_DETECT 1002
#define ID_DUMP   1003
#define ID_OPEN   1004
#define ID_LOG    1005
#define ID_STATUS 1006
#define ID_SELF   1007
#define ID_HELP   1008

static HWND  g_wnd, g_port, g_detect, g_dump, g_open, g_self, g_help, g_log, g_status;
static HFONT g_font, g_mono;
static int   g_busy;
static int   g_dumped;

/* Before the window exists -- the /selftest command line runs headless -- the log goes
   to a buffer, and from there to stdout and SELFTEST.txt. */
static char   g_early[65536];
static size_t g_early_n;

static void
ui_log(const char *s)
{
    int len;

    if (!g_log) {
        size_t n = zlen(s);

        if (g_early_n + n < sizeof g_early) {
            memcpy(g_early + g_early_n, s, n);
            g_early_n += n;
        }
        return;
    }

    len = GetWindowTextLengthA(g_log);
    SendMessageA(g_log, EM_SETSEL, (WPARAM) len, (LPARAM) len);
    SendMessageA(g_log, EM_REPLACESEL, FALSE, (LPARAM) s);
    /* The window must repaint between phases or a run looks like a hang. */
    {
        MSG m;

        while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageA(&m);
        }
    }
}

static void
ui_status(const char *s)
{
    if (g_status)
        SetWindowTextA(g_status, s);
}

static unsigned short
ui_port(void)
{
    char           t[32];
    unsigned short v = 0;
    int            i, any = 0;

    GetWindowTextA(g_port, t, sizeof t);
    for (i = 0; t[i]; i++) {
        char c = t[i];
        int  d;

        if ((c == '0') && ((t[i + 1] == 'x') || (t[i + 1] == 'X'))) {
            i++;
            continue;
        }
        if ((c >= '0') && (c <= '9'))       d = c - '0';
        else if ((c >= 'a') && (c <= 'f'))  d = c - 'a' + 10;
        else if ((c >= 'A') && (c <= 'F'))  d = c - 'A' + 10;
        else continue;
        v = (unsigned short) (v * 16 + d);
        any = 1;
    }
    return any ? v : 0x378;
}

/* Bring the port up, once, and say plainly what happened if it will not come up. */
static int
ensure_port(void)
{
    if (g_backend != BK_NONE)
        return 1;
    if (acquire_port()) {
        lg("Port access: %s\r\n", g_backend_why);
        return 1;
    }
    lg("%s\r\n", g_backend_why);
    return 0;
}

/* DETECT is the cheap half: the gates only, no memory and no transform.  Run it first
   on an unfamiliar machine -- if the identity ramp comes back constant, the wiring or
   the port address is wrong and a full dump would only produce a large wrong file. */
static void
do_detect(void)
{
    int i, ones = 0;

    g_base = ui_port();
    if (!ensure_port())
        return;

    ui_status("Detecting...");
    lg("\r\n--- detect, port %03X ---\r\n", (unsigned int) g_base);
    phase_session();

    for (i = 0; i < 64; i++)
        ones += R.ramp_lo[i];
    if ((ones == 0) || (ones == 64))
        lg("The identity ramp answered the same bit 64 times.  That is a stuck line:\r\n"
           "check the port address, the cable, and that the dongle is a parallel HASP4\r\n"
           "and not the 1999 two-chip part.\r\n");
    else
        lg("%d of 64 addresses answered 1 -- the part is talking.\r\n", ones);
    ui_status("Detect done.");
}

/* HELP.  Written into the log rather than a message box, because the one thing an
   operator does with these instructions is read them while clicking through another
   window -- and because the log can be selected and copied. */
static void
do_help(void)
{
    ui_log(
"\r\n"
"=============================================================================\r\n"
" GETTING PORT ACCESS\r\n"
"=============================================================================\r\n"
"\r\n"
"Nothing here can reach a parallel port from an ordinary user-mode process.\r\n"
"There are two ways round that and this program tries both, driver first.\r\n"
"Whichever one succeeds is named on the \"Port access:\" line above.\r\n"
"\r\n"
"Which one you want depends on the machine:\r\n"
"\r\n"
"  32-bit Windows XP  ->  either.  IOPL installs nothing and is faster.\r\n"
"  64-bit Windows     ->  the driver.  ProcessUserModeIOPL does not exist on\r\n"
"                         x64 at all, so no user-mode program can use it there.\r\n"
"  Vista/7 32-bit     ->  try IOPL elevated; fall back to the driver.\r\n"
"\r\n"
"\r\n"
"-----------------------------------------------------------------------------\r\n"
" A. \"Act as part of the operating system\"  (the IOPL route, 32-bit only)\r\n"
"-----------------------------------------------------------------------------\r\n"
"\r\n"
"This is the privilege SeTcbPrivilege.  Being an Administrator is NOT enough:\r\n"
"an XP admin account does not hold it by default, and this is the single most\r\n"
"common reason the port will not open.\r\n"
"\r\n"
"On XP Professional, 2000, Server, or any edition that has secpol.msc:\r\n"
"\r\n"
"  1. Log on with an Administrator account.\r\n"
"  2. Start -> Run -> type   secpol.msc   -> OK\r\n"
"     (Control Panel -> Administrative Tools -> Local Security Policy is the\r\n"
"      same thing.)\r\n"
"  3. In the left pane open:  Local Policies -> User Rights Assignment\r\n"
"  4. In the right pane double-click:\r\n"
"        Act as part of the operating system\r\n"
"  5. Click \"Add User or Group...\" (on 2000 it is just \"Add...\").\r\n"
"  6. Type the account name you will run this program as, click Check Names,\r\n"
"     then OK.  Add the account itself, not a group it belongs to -- group\r\n"
"     membership works but is harder to see when it does not.\r\n"
"  7. OK out of both dialogs and close secpol.msc.\r\n"
"  8. LOG OFF AND LOG BACK ON.  A privilege is put into the token when the\r\n"
"     session is created, so it does not take effect until you do.  Rebooting\r\n"
"     also works.  Nothing you do without this step will help.\r\n"
"  9. Run this program again and press Detect.  The \"Port access:\" line\r\n"
"     should now say ProcessUserModeIOPL.\r\n"
"\r\n"
"On XP Home, which has neither secpol.msc nor gpedit.msc:\r\n"
"\r\n"
"  Use ntrights.exe from the Windows 2000 or 2003 Resource Kit Tools, in a\r\n"
"  command prompt as an Administrator:\r\n"
"\r\n"
"        ntrights -u MACHINE\\username +r SeTcbPrivilege\r\n"
"\r\n"
"  Then log off and back on, as above.  ntrights is a Microsoft tool and it\r\n"
"  runs fine on XP Home even though it shipped for Server.\r\n"
"\r\n"
"If it still will not open:\r\n"
"\r\n"
"  - The failure line above quotes the NTSTATUS.  C0000061 is\r\n"
"    STATUS_PRIVILEGE_NOT_HELD, which means the privilege is genuinely absent\r\n"
"    from the token -- go back to step 8, you almost certainly did not log off.\r\n"
"  - On a domain machine a Group Policy from the domain overrides the local\r\n"
"    setting, and it is reapplied periodically, so a local edit can be silently\r\n"
"    undone.  Take the machine off the domain for the session, or have the\r\n"
"    right granted in the domain policy.\r\n"
"  - On Vista and 7, run the program elevated (right-click -> Run as\r\n"
"    administrator) as well.  UAC hands a filtered token to a normal launch and\r\n"
"    the privilege is stripped out of it.\r\n"
"  - Some \"security\" software blocks the call outright.  If the status is not\r\n"
"    C0000061, that is worth suspecting.\r\n"
"\r\n"
"Undo it the same way when you are finished: this right lets a process act as\r\n"
"the operating system, so it is not one to leave granted on a machine that does\r\n"
"anything else.  In secpol.msc, remove the account from the same policy; with\r\n"
"ntrights, use -r in place of +r.\r\n"
"\r\n"
"\r\n"
"-----------------------------------------------------------------------------\r\n"
" B. The InpOut32 driver  (any Windows, and the only option on 64-bit)\r\n"
"-----------------------------------------------------------------------------\r\n"
"\r\n"
"  1. Get the InpOutx64 package from Highrez (it contains both the 32-bit\r\n"
"     inpout32.dll and the 64-bit inpoutx64.dll and their signed driver).\r\n"
"     This program installs and extracts nothing on its own -- it only loads\r\n"
"     the DLL by name if it is already there.\r\n"
"  2. Run the package's installer once, as an Administrator, so the kernel\r\n"
"     driver is registered.\r\n"
"  3. Put inpout32.dll in the same folder as DONGDUMP.EXE (or anywhere on the\r\n"
"     path).  This program is 32-bit, so on 64-bit Windows it is still\r\n"
"     inpout32.dll you want beside it -- that DLL loads the 64-bit driver\r\n"
"     itself.\r\n"
"  4. Run DONGDUMP as an Administrator.\r\n"
"\r\n"
"If the DLL loads but the driver does not open, the line above says so.  That\r\n"
"is almost always \"not elevated\" or \"the installer was never run\".\r\n"
"\r\n"
"\r\n"
"-----------------------------------------------------------------------------\r\n"
" WHICH PORT ADDRESS\r\n"
"-----------------------------------------------------------------------------\r\n"
"\r\n"
"378 is LPT1 on nearly every board, 278 and 3BC are the other two standard\r\n"
"addresses.  Device Manager -> Ports (COM & LPT) -> the port -> Resources tab\r\n"
"gives the real one; use the first address of the range.  A PCI or PCIe card\r\n"
"will be somewhere else entirely, often above 1000 -- that works here, but not\r\n"
"every such card is register-compatible enough for this protocol.\r\n"
"\r\n"
"In the BIOS, set the port to SPP or Normal mode.  EPP and ECP change how the\r\n"
"registers behave and this protocol drives them directly.\r\n"
"\r\n"
"\r\n"
"-----------------------------------------------------------------------------\r\n"
" ORDER OF WORK\r\n"
"-----------------------------------------------------------------------------\r\n"
"\r\n"
"  Self-test  needs no dongle, no port and no privilege.  Run it once on a new\r\n"
"             machine so a later failure can only be the hardware.\r\n"
"  Detect     the session gates only.  If the identity ramp answers the same\r\n"
"             bit 64 times, the address or the cable is wrong -- stop there, a\r\n"
"             full dump would only be a large wrong file.\r\n"
"  Dump       writes the folder.  Takes a few seconds.\r\n"
"\r\n"
"This program never writes to the dongle.  Only Microwire READ is implemented.\r\n"
"=============================================================================\r\n"
"\r\n");
    ui_status("Help shown above.");
}


/* ---- the record decode, against records that came off real dongles ---------------
 *
 * Three of the nine h5dmp dumps, one per record shape, with the per-unit dword blanked
 * so no individual dongle's serial is baked into a tool.  Each is scrambled here exactly
 * as the part holds it and then put through the same mw_pass1 / descramble /
 * rec_identify / rec_values the wire read uses -- including the password recovery, which
 * has to come back out of words 0..7 on its own.
 *
 * These caught a real off-by-one: the eight dwords start at record byte 30 and the first
 * of them is what the 1999 layout called v[1], so FINDIT's +1C key is dword 2, not
 * dword 3.  Every one of the nine dumps has 0001D760 in dword 2.
 */
static const struct {
    const char   *name;
    unsigned int  pass1;
    int           swap;
    int           shape;
    const char   *ver;
    const char   *terr;
    const unsigned char rec[REC_BYTES];
} rec_vectors[] = {
{ "Photo Play 2001 ES", 0x7477, 0, SH_2001, "2001", "ES", {
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x56, 0x65, 0x72, 0x73, 0x69, 0x6F, 0x6E, 0x20, 0x32, 0x30, 0x30,
    0x31, 0x20, 0x28, 0x45, 0x53, 0x29, 0x30, 0x30, 0x30, 0x39, 0x30, 0x37,
    0x30, 0x39, 0x38, 0x37, 0x36, 0x35, 0x31, 0x32, 0x30, 0x36, 0x37, 0x32,
    0x31, 0x37, 0x30, 0x38, 0x39, 0x38, 0x30, 0x37, 0x35, 0x39, 0x30, 0x32,
    0x30, 0x30, 0x32, 0x32, 0x30, 0x35, 0x31, 0x36, 0x30, 0x36, 0x37, 0x38,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0xFF } },

{ "I.G.O. 3 PT", 0x6B91, 1, SH_SION, "2003", "PT", {
    0x50, 0x54, 0x00, 0x73, 0x69, 0x6F, 0x6E, 0x20, 0x32, 0x30, 0x30, 0x30,
    0x20, 0x28, 0x53, 0x50, 0x29, 0x00, 0x39, 0x3F, 0x12, 0x3D, 0x39, 0x3F,
    0x14, 0x3D, 0x39, 0x3F, 0x16, 0x3D, 0x8B, 0x03, 0x00, 0x00, 0xCD, 0x81,
    0x01, 0x00, 0x60, 0xD7, 0x01, 0x00, 0x92, 0x9B, 0x02, 0x00, 0x7E, 0x28,
    0x01, 0x00, 0x9D, 0x08, 0x00, 0x00, 0xA6, 0x73, 0x02, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF } },

{ "I.G.O. 7 ES", 0x68BB, 1, SH_VERS, "2007", "ES", {
    0x45, 0x53, 0x2D, 0x56, 0x65, 0x72, 0x73, 0x69, 0x6F, 0x6E, 0x32, 0x30,
    0x30, 0x37, 0x00, 0x50, 0x29, 0x00, 0x6F, 0x40, 0x88, 0x3E, 0x6F, 0x40,
    0x8A, 0x3E, 0x6F, 0x40, 0x8C, 0x3E, 0x8B, 0x03, 0x00, 0x00, 0xCD, 0x81,
    0x01, 0x00, 0x60, 0xD7, 0x01, 0x00, 0x92, 0x9B, 0x02, 0x00, 0x7E, 0x28,
    0x01, 0x00, 0x9D, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF } }
};

/* Build what the part holds, so the decode can be run forwards over it. */
static void
scramble(const unsigned char rec[REC_BYTES], unsigned int pass1, int swap,
         unsigned int w[64])
{
    int i;

    for (i = 0; i < 64; i++) {
        const int j = i - 8;
        unsigned int plain = 0;

        if ((j >= 0) && ((j * 2 + 1) < REC_BYTES))
            plain = swap ? (unsigned int) ((rec[j * 2 + 1] << 8) | rec[j * 2])
                         : (unsigned int) ((rec[j * 2] << 8) | rec[j * 2 + 1]);
        w[i] = (plain ^ (unsigned int) j ^ pass1
                ^ ((i < 8) ? 0xFF00u : 0u)) & 0xFFFFu;
    }
}

static int
selftest_records(void)
{
    static unsigned int  w[64];
    static unsigned char got[REC_BYTES];
    int bad = 0;
    int n;

    for (n = 0; n < (int) (sizeof rec_vectors / sizeof rec_vectors[0]); n++) {
        unsigned int p1 = 0;
        char ver[16], terr[4];
        int  shape, hits = 0, sw, chosen = -1, val[8], i;

        scramble(rec_vectors[n].rec, rec_vectors[n].pass1, rec_vectors[n].swap, w);

        if (!mw_pass1(w, &p1) || (p1 != rec_vectors[n].pass1)) {
            lg("  FAILED  %s: password came back %04X, wanted %04X\r\n",
               rec_vectors[n].name, p1, rec_vectors[n].pass1);
            bad++;
            continue;
        }

        /* Both byte orders, as the real run does -- exactly one must parse, or the
           tool would be picking between two readings by luck. */
        for (sw = 0; sw <= 1; sw++) {
            descramble(w, p1, sw, got);
            if (rec_identify(got, p1, ver, terr) != SH_NONE) {
                hits++;
                chosen = sw;
            }
        }
        if (hits != 1) {
            lg("  FAILED  %s: %d byte orders parsed, wanted exactly one\r\n",
               rec_vectors[n].name, hits);
            bad++;
            continue;
        }

        descramble(w, p1, chosen, got);
        shape = rec_identify(got, p1, ver, terr);
        rec_values(got, shape, val);

        for (i = 0; i < REC_BYTES; i++)
            if (got[i] != rec_vectors[n].rec[i])
                break;

        if ((chosen != rec_vectors[n].swap) || (shape != rec_vectors[n].shape)
            || (i != REC_BYTES)
            || zcmpn(ver, rec_vectors[n].ver, zlen(rec_vectors[n].ver) + 1)
            || zcmpn(terr, rec_vectors[n].terr, 3)
            || ((unsigned int) val[2] != 0x0001D760u)
            || ((unsigned int) val[3] != 0x00029B92u)
            || ((unsigned int) val[4] != 0x0001287Eu)) {
            lg("  FAILED  %s: got %s (%s), %s-first, FINDIT %08X\r\n",
               rec_vectors[n].name, ver, terr, chosen ? "low" : "high",
               (unsigned int) val[2]);
            bad++;
            continue;
        }
        lg("  OK      %s -> pass1 %04X, %s (%s), %s-first, FINDIT %08X\r\n",
           rec_vectors[n].name, p1, ver, terr, chosen ? "low" : "high",
           (unsigned int) val[2]);
    }
    return bad;
}

/* SELF-TEST.  Four known parts, two framings, no port touched.  If any line here says
   FAILED then this program is wrong and nothing it reports off a real dongle should be
   believed -- which is exactly what one wants to know before a long session with a
   cabinet on the bench. */
static void
do_selftest(void)
{
    static const struct {
        const char   *name;
        unsigned int  key;
        unsigned int  init;
        int           variant;
    } cases[] = {
        { "Photo Play 2001 (7477/7D57)", 0xCF47CB42u, 0x7DFu, TV_A },
        { "I.G.O. 2 (68BB/1329)",        0x3B227944u, 0x7DFu, TV_A },
        { "I.G.O. 3 (6B91/24A3)",        0xAB32E970u, 0x5DFu, TV_B },
        { "an arbitrary part",           0x12345678u, 0x123u, TV_A }
    };
    int i, bad = 0;
    const int wire_was = g_wire_on;

    lg("\r\n--- self-test ---\r\n");
    g_wire_on = 0;

    lg("record decode, against records read off real dongles:\r\n");
    bad += selftest_records();

    lg("key recovery, against a simulated part:\r\n");
    g_sim = 1;

    for (i = 0; i < (int) (sizeof cases / sizeof cases[0]); i++) {
        unsigned int key, init;
        int rank, contra, ag, tot, hag, htot, ok;

        g_sim_variant = cases[i].variant;
        g_sim_key     = cases[i].key;
        g_sim_init    = cases[i].init;
        g_sim_cur     = cases[i].init;
        g_sim_last    = 0;
        g_sim_pending = 0;

        ok = transform_try(cases[i].variant, 0x00, &key, &init, &rank, &contra,
                           &ag, &tot, &hag, &htot, 0);
        if (ok && (key == cases[i].key) && (init == cases[i].init)) {
            lg("  OK      %s -> key %08X, init %03X (%d/%d, %d/%d held back)\r\n",
               cases[i].name, key, init, ag, tot, hag, htot);
        } else {
            bad++;
            lg("  FAILED  %s: got key %08X init %03X, wanted %08X %03X "
               "(rank %d, %d contradictions)\r\n", cases[i].name, key, init,
               cases[i].key, cases[i].init, rank, contra);
        }
    }

    g_sim     = 0;
    g_wire_on = wire_was;
    lg(bad ? "Self-test FAILED.  Do not trust a dump from this build.\r\n"
           : "Self-test passed.  Everything that can be checked without a dongle has\r\n"
             "been: a \"no fit\" or a bad record against real hardware is then about the\r\n"
             "hardware or the wire, not about this program's arithmetic.\r\n");
    ui_status(bad ? "Self-test FAILED." : "Self-test passed.");
}

static void
do_dump(void)
{
    char folder[64];
    char path[MAX_PATH];
    char cwd[MAX_PATH];

    g_base = ui_port();
    if (!ensure_port())
        return;

    g_busy = 1;
    EnableWindow(g_dump, FALSE);
    EnableWindow(g_detect, FALSE);

    g_wire_n  = 0;
    g_wire_on = (g_wire != NULL);
    R.obs_n   = 0;
    R.shape   = SH_NONE;
    R.mem_ok  = 0;

    lg("\r\n--- dump, port %03X ---\r\n", (unsigned int) g_base);
    ui_status("Session layer...");
    phase_session();
    ui_status("Memory...");
    phase_memory();
    ui_status("Transform -- this is the long one...");
    phase_transform();

    GetCurrentDirectoryA(sizeof cwd, cwd);
    name_folder(folder, sizeof folder);
    zfmt(path, sizeof path, "%s\\%s", cwd, folder);
    if (!CreateDirectoryA(path, NULL) && (GetLastError() != ERROR_ALREADY_EXISTS)) {
        lg("Cannot create %s -- writing into the current directory instead.\r\n", path);
        zfmt(path, sizeof path, "%s", cwd);
    }
    zfmt(g_outdir, sizeof g_outdir, "%s", path);
    write_outputs(path);

    lg("\r\nWritten to %s\r\n", path);
    lg("Send SUMMARY.txt back, and the whole folder if anything above said NEW or "
       "WARNING.\r\n");
    ui_status("Done.");
    g_dumped = 1;
    EnableWindow(g_open, TRUE);
    EnableWindow(g_dump, TRUE);
    EnableWindow(g_detect, TRUE);
    g_busy = 0;
    Beep(880, 200);
}

static void
do_open(void)
{
    if (g_dumped && g_outdir[0])
        ShellExecuteA(NULL, "open", g_outdir, NULL, NULL, SW_SHOWNORMAL);
}

static LRESULT CALLBACK
wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
        case WM_CREATE: {
            g_font = CreateFontA(-11, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                 0, 0, 0, 0, "Tahoma");
            g_mono = CreateFontA(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                 0, 0, 0, FIXED_PITCH | FF_MODERN, "Courier New");

            CreateWindowExA(0, "STATIC", "LPT base:", WS_CHILD | WS_VISIBLE,
                            12, 15, 60, 18, h, NULL, NULL, NULL);
            g_port = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "378",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                     76, 12, 60, 22, h, (HMENU) ID_PORT, NULL, NULL);
            g_detect = CreateWindowExA(0, "BUTTON", "Detect",
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                       152, 11, 84, 25, h, (HMENU) ID_DETECT, NULL, NULL);
            g_dump = CreateWindowExA(0, "BUTTON", "Dump everything",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP
                                         | BS_DEFPUSHBUTTON,
                                     244, 11, 140, 25, h, (HMENU) ID_DUMP, NULL, NULL);
            g_open = CreateWindowExA(0, "BUTTON", "Open folder",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_DISABLED,
                                     392, 11, 104, 25, h, (HMENU) ID_OPEN, NULL, NULL);
            g_self = CreateWindowExA(0, "BUTTON", "Self-test",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                     504, 11, 90, 25, h, (HMENU) ID_SELF, NULL, NULL);
            g_help = CreateWindowExA(0, "BUTTON", "Help",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                     602, 11, 70, 25, h, (HMENU) ID_HELP, NULL, NULL);
            g_log = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                                    WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL
                                        | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                                    12, 46, 700, 420, h, (HMENU) ID_LOG, NULL, NULL);
            g_status = CreateWindowExA(0, "STATIC", "Ready.", WS_CHILD | WS_VISIBLE,
                                       12, 474, 700, 18, h, (HMENU) ID_STATUS, NULL,
                                       NULL);

            SendMessageA(g_port,   WM_SETFONT, (WPARAM) g_font, TRUE);
            SendMessageA(g_detect, WM_SETFONT, (WPARAM) g_font, TRUE);
            SendMessageA(g_dump,   WM_SETFONT, (WPARAM) g_font, TRUE);
            SendMessageA(g_open,   WM_SETFONT, (WPARAM) g_font, TRUE);
            SendMessageA(g_self,   WM_SETFONT, (WPARAM) g_font, TRUE);
            SendMessageA(g_help,   WM_SETFONT, (WPARAM) g_font, TRUE);
            SendMessageA(g_status, WM_SETFONT, (WPARAM) g_font, TRUE);
            SendMessageA(g_log,    WM_SETFONT, (WPARAM) g_mono, TRUE);
            return 0;
        }

        case WM_SIZE: {
            int cw = LOWORD(l), ch = HIWORD(l);

            MoveWindow(g_log, 12, 46, cw - 24, ch - 74, TRUE);
            MoveWindow(g_status, 12, ch - 24, cw - 24, 18, TRUE);
            return 0;
        }

        case WM_GETMINMAXINFO:
            ((MINMAXINFO *) l)->ptMinTrackSize.x = 560;
            ((MINMAXINFO *) l)->ptMinTrackSize.y = 320;
            return 0;

        case WM_COMMAND:
            if (g_busy)
                return 0;
            switch (LOWORD(w)) {
                case ID_DETECT: do_detect(); return 0;
                case ID_DUMP:   do_dump();   return 0;
                case ID_OPEN:   do_open();   return 0;
                case ID_SELF:   do_selftest(); return 0;
                case ID_HELP:   do_help();   return 0;
            }
            return 0;

        case WM_CLOSE:
            if (g_busy)
                return 0;
            DestroyWindow(h);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

void __stdcall
start(void)
{
    WNDCLASSA wc;
    MSG       msg;
    char     *cmd = GetCommandLineA();

    /* `DONGDUMP /selftest` runs the checks against the software part and exits, so the
       maths can be verified from a script and from a build machine with no dongle;
       `DONGDUMP /help` prints the same text the Help button shows, for pasting into a
       mail to whoever has to grant the privilege.  Either writes a file beside the
       program and also to stdout, so both work redirected. */
    for (; *cmd; cmd++) {
        const int self = !zcmpn(cmd, "selftest", 8);
        const int help = !zcmpn(cmd, "help", 4);

        if (!self && !help)
            continue;
        if (self)
            do_selftest();
        else
            do_help();
        {
            DWORD  n = 0;
            HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);

            if (h && (h != INVALID_HANDLE_VALUE))
                WriteFile(h, g_early, (DWORD) g_early_n, &n, NULL);
            h = CreateFileA(self ? "SELFTEST.txt" : "HELP.txt", GENERIC_WRITE, 0, NULL,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h != INVALID_HANDLE_VALUE) {
                WriteFile(h, g_early, (DWORD) g_early_n, &n, NULL);
                CloseHandle(h);
            }
        }
        ExitProcess(0);
    }

    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc   = wndproc;
    wc.hInstance     = GetModuleHandleA(NULL);
    wc.hCursor       = LoadCursorA(NULL, IDC_ARROW);
    wc.hIcon         = LoadIconA(NULL, IDI_APPLICATION);
    wc.hbrBackground = (HBRUSH) (COLOR_BTNFACE + 1);
    wc.lpszClassName = "DongDumpWnd";
    RegisterClassA(&wc);

    g_wire_cap = 4u * 1024u * 1024u;
    g_wire = (char *) VirtualAlloc(NULL, g_wire_cap, MEM_COMMIT, PAGE_READWRITE);
    if (!g_wire)
        g_wire_cap = 0;

    g_wnd = CreateWindowExA(0, "DongDumpWnd",
                            "DONGDUMP -- funworld parallel dongle",
                            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                            800, 560, NULL, NULL, wc.hInstance, NULL);
    ShowWindow(g_wnd, SW_SHOW);
    UpdateWindow(g_wnd);

    ui_log("DONGDUMP -- reads a 2001..2007 / I.G.O. parallel HASP4 for emulation.\r\n"
           "\r\n"
           "It never writes to the dongle: only Microwire READ is implemented.\r\n"
           "\r\n"
           "Detect first.  If the identity ramp answers the same bit 64 times the\r\n"
           "port address or the cable is wrong and a dump would be worthless.\r\n"
           "Then Dump everything -- a folder <version>-<language>-<date-time> appears\r\n"
           "beside this program.\r\n"
           "\r\n"
           "Self-test needs no dongle and no privilege: it checks the record decode\r\n"
           "against records read off real hardware and the key recovery against a\r\n"
           "simulated part.  Run it once on a new machine, so that a later failure\r\n"
           "can only be the dongle or the wiring.\r\n"
           "\r\n"
           "Needs Administrator, and on 64-bit Windows the InpOutx64 driver package.\r\n"
           "Press Help for how to set either of those up, step by step.\r\n"
           "\r\n");

    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageA(g_wnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }
    ExitProcess(0);
}
