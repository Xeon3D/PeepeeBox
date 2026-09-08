/* Drives char_modem.c the way FN_SYS.EXE drives the real port, then runs the
   cabinet's own identification algorithm over the answers.

   The tables below are `\FN_SYS\DATABASE\NETWORK\MD_NAME.CSV` and
   `MD_INFOS.CSV`, transcribed from two images: the fourteen-row 2001 one from
   PP2001NL-MASTERS-NSB H9751 SR1, and the thirty-row I.G.O. 6 one from
   IGO 6 DE ND003.  A model passes only when it resolves to exactly one row in
   both -- not when it happens to contain the right substring. */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/char.h>
#include <86box/plat_netsocket.h>

/* ------------------------------------------------------------ 86Box stubs */
static char_port_t test_port;

char_port_t *
char_attach(uint32_t flags,
            size_t (*read)(uint8_t *, size_t, void *),
            size_t (*write)(uint8_t *, size_t, void *),
            uint32_t (*status)(void *),
            void (*control)(uint32_t, void *),
            void (*port_config)(void *), void *priv)
{
    test_port.chardev.flags       = flags;
    test_port.chardev.read        = read;
    test_port.chardev.write       = write;
    test_port.chardev.status      = status;
    test_port.chardev.control     = control;
    test_port.chardev.port_config = port_config;
    test_port.chardev.priv        = priv;
    test_port.attached            = 1;
    return &test_port;
}

void  char_update_status(char_port_t *p) { (void) p; }
void *char_log_open(char_port_t *p, char *n) { (void) p; (void) n; return NULL; }
void  log_close(void *p) { (void) p; }
void  log_out(void *p, const char *f, va_list a) { (void) p; (void) f; (void) a; }

/* The config the device would have read out of the ini.  Blank identity strings,
   so the model table's own answers are what gets tested. */
int
device_get_config_int(const char *name)
{
    if (!strcmp(name, "line"))
        return 0; /* not connected */
    if (!strcmp(name, "host_port"))
        return 23;
    if (!strcmp(name, "connect_rate"))
        return 57600;
    return 0;
}

const char *
device_get_config_string(const char *name)
{
    (void) name;
    return "";
}

static uint32_t fake_ticks = 100000;

uint32_t plat_get_ticks(void) { return fake_ticks; }

SOCKET plat_netsocket_create(int t) { (void) t; return (SOCKET) -1; }
void   plat_netsocket_close(SOCKET s) { (void) s; }
int    plat_netsocket_connect(SOCKET s, const char *h, unsigned short p) { (void) s; (void) h; (void) p; return -1; }
int    plat_netsocket_connected(SOCKET s) { (void) s; return -1; }
int    plat_netsocket_send(SOCKET s, const unsigned char *d, unsigned int n, int *w) { (void) s; (void) d; (void) n; (void) w; return -1; }
int    plat_netsocket_receive(SOCKET s, unsigned char *d, unsigned int n, int *w) { (void) s; (void) d; (void) n; (void) w; return -1; }

/* ---------------------------------------------- the cabinet's own tables */

typedef struct {
    int         id;
    int         ati;  /* which ATIn to ask */
    const char *want; /* the substring to look for in the answer */
} info_token_t;

typedef struct {
    const char *name;
    int         tok[4];
} name_row_t;

/* MD_INFOS.CSV -- I.G.O. 6.  The 2001 table is ids 9..28 of this, unchanged. */
static const info_token_t infos[] = {
    {  9, 6, "MicroLink"    }, { 10, 6, "33.6"       }, { 11, 6, "56"        },
    { 12, 6, "TanGo"        }, { 13, 6, "1000"       }, { 14, 6, "ISDN"      },
    { 15, 3, "SupraExpress" }, { 16, 4, "Sportster"  }, { 17, 4, "ISDN"      },
    { 18, 6, "Internet"     }, { 19, 0, "WAVECOM"    }, { 20, 0, "1800"      },
    { 21, 0, "900"          }, { 22, 6, "SIEMENS"    }, { 23, 6, "M20"       },
    { 24, 3, "Sportster"    }, { 25, 3, "56000"      }, { 26, 3, "Voice"     },
    { 27, 3, "P2107-V90"    }, { 28, 3, "V.92"       }, { 29, 0, "Nokia"     },
    { 30, 3, "Nokia 30"     }, { 31, 3, "TP560"      }, { 32, 6, "-i"        },
    { 33, 1, "Riser"        }, { 34, 1, "Voice"      }, { 35, 6, "2a/b"      },
    { 36, 0, "1292"         }, { 37, 0, "1.2"        }, { 38, 6, "k i"       },
    { 39, 6, "pro"          }, { 40, 0, "TA+PP2"     }, { 41, 3, "Tornado"   },
    { 42, 5, "ROPER FLYING" }, { 43, 0, "TINTORETTO" }, { 44, 0, "251"       },
    { 45, 3, "ISDN adapter" }, { 46, 3, "ZyXEL"      }, { 47, 1, "SmartUSB56"},
    { 49, 0, "MULTIBAND"    }, { 50, 0, "900E"       }, { 51, 0, "56000"     },
    { 52, 1, "255"          }, { 0, 0, NULL }
};

/* MD_NAME.CSV -- I.G.O. 6 DE ND003, all thirty rows. */
static const name_row_t names_igo6[] = {
    { "ELSA MicroLink 33.6TQV",              {  9, 10, -1, -1 } },
    { "ELSA MicroLink 56k",                  {  9, 11, -1, -1 } },
    { "Diamond SupraExpress 56e PRO",        { 15, -1, -1, -1 } },
    { "ELSA TanGo 1000",                     { 12, 13, -1, -1 } },
    { "ELSA MicroLink ISDN",                 {  9, 14, -1, -1 } },
    { "U.S. Robotics Sportster ISDN TA",     { 16, 17, -1, -1 } },
    { "ELSA MicroLink 56k Internet",         {  9, 11, 18, -1 } },
    { "Wavecom 1800",                        { 19, 20, -1, -1 } },
    { "Wavecom 900",                         { 19, 21, -1, -1 } },
    { "Siemens M20",                         { 22, 23, -1, -1 } },
    { "U.S. Robotics Sportster 56000 Voice", { 24, 25, 26, -1 } },
    { "ELSA MicroLink ISDN Internet",        {  9, 14, 18, -1 } },
    { "BestMatic Conexant 56k",              { 27, 11, -1, -1 } },
    { "Diamond SupraExpress 56e PRO V.92",   { 15, 28, -1, -1 } },
    { "Nokia 30",                            { 29, 30, -1, -1 } },
    { "Topic 56k",                           { 31, -1, -1, -1 } },
    { "ELSA MicroLink 56k i",                {  9, 11, 32, 38 } },
    { "56K Voice Modem Riser",               { 33, 34, -1, -1 } },
    { "ELSA Microlink 2ab PnP",              {  9, 14, 35, -1 } },
    { "DrayTek ISDN PPP",                    { 36, 37, -1, -1 } },
    { "ELSA MicroLink 56k pro",              {  9, 11, 39, -1 } },
    { "Stollmann TA+PP2",                    { 40, -1, -1, -1 } },
    { "Tornado WebJet 128",                  { 41, -1, -1, -1 } },
    { "AsusCom ISDNLink TA",                 { 42, -1, -1, -1 } },
    { "Tintoretto ISDN",                     { 43, -1, -1, -1 } },
    { "WebJet Pocket USB",                   { 44, 45, -1, -1 } },
    { "ZyXEL Omni 56K USB",                  { 46, -1, -1, -1 } },
    { "SmartUSB56 Voice Modem",              { 47, 34, -1, -1 } },
    { "Wavecom GPRS",                        { 49, 19, 50, 20 } },
    { "INSYS Pocket 56k Modem",              { 51, 52, -1, -1 } },
    { NULL, { 0, 0, 0, 0 } }
};

/* MD_NAME.CSV -- PP2001NL-MASTERS H9751 SR1.  Fourteen rows, and row 13 is
   weaker here than in I.G.O. 6: one token rather than two. */
static const name_row_t names_2001[] = {
    { "ELSA MicroLink 33.6TQV",              {  9, 10, -1, -1 } },
    { "ELSA MicroLink 56k",                  {  9, 11, -1, -1 } },
    { "Diamond SupraExpress 56e PRO",        { 15, -1, -1, -1 } },
    { "ELSA TanGo 1000",                     { 12, 13, -1, -1 } },
    { "ELSA MicroLink ISDN",                 {  9, 14, -1, -1 } },
    { "U.S. Robotics Sportster ISDN TA",     { 16, 17, -1, -1 } },
    { "ELSA MicroLink 56k Internet",         {  9, 11, 18, -1 } },
    { "Wavecom 1800",                        { 19, 20, -1, -1 } },
    { "Wavecom 900",                         { 19, 21, -1, -1 } },
    { "Siemens M20",                         { 22, 23, -1, -1 } },
    { "U.S. Robotics Sportster 56000 Voice", { 24, 25, 26, -1 } },
    { "ELSA MicroLink ISDN Internet",        {  9, 14, 18, -1 } },
    { "BestMatic Conexant 56k",              { 27, -1, -1, -1 } },
    { "Diamond SupraExpress 56e PRO V.92",   { 15, 28, -1, -1 } },
    { NULL, { 0, 0, 0, 0 } }
};

/* ------------------------------------------------------------------ driver */
extern const device_t char_modem_supra_com_device;
extern const device_t char_modem_elsa_com_device;

static void *dev;

static void
send_str(const char *s)
{
    while (*s != '\0') {
        uint8_t c = (uint8_t) *s++;

        test_port.chardev.write(&c, 1, dev);
    }
}

/* Pull everything the modem has queued, the way the UART's receive timer does. */
static void
drain(char *out, size_t outsz)
{
    size_t n = 0;

    for (int i = 0; i < 8192; i++) {
        uint8_t b;

        if (test_port.chardev.read(&b, 1, dev) != 1)
            break;
        if (n < (outsz - 1))
            out[n++] = (char) b;
    }
    out[n] = '\0';
}

/* One AT exchange: send the line, return everything that came back. */
static const char *
at(const char *cmd)
{
    static char buf[4096];

    send_str(cmd);
    send_str("\r");
    drain(buf, sizeof(buf));
    return buf;
}

static int failures = 0;

static void
expect(const char *what, const char *got, const char *needle)
{
    const int ok = (strstr(got, needle) != NULL);

    if (!ok)
        failures++;
    printf("  %-38s %-5s wants \"%s\"\n", what, ok ? "ok" : "FAIL", needle);
    if (!ok)
        printf("      got: %s\n", got);
}

/* -------------------------------------------- the identification algorithm */

static char ati[10][256];

static void
collect_ati(void)
{
    char buf[256];

    /* Echo off first, exactly as FN_SYS does -- otherwise the command itself is
       in the answer and the matcher would be reading its own question. */
    send_str("\r\r");
    drain(buf, sizeof(buf));
    send_str("AT\r");
    drain(buf, sizeof(buf));
    send_str("ATE0\r");
    drain(buf, sizeof(buf));

    for (int n = 0; n < 10; n++) {
        char cmd[8];

        snprintf(cmd, sizeof(cmd), "ATI%d", n);
        snprintf(ati[n], sizeof(ati[n]), "%s", at(cmd));
    }
}

static int
token_matches(int id)
{
    for (int i = 0; infos[i].want != NULL; i++) {
        if (infos[i].id != id)
            continue;
        return (strstr(ati[infos[i].ati], infos[i].want) != NULL);
    }
    return 0; /* a token this table does not define cannot match */
}

/* Every row whose tokens all match, the way FN_SYS decides what it is. */
static void
identify(const char *table_name, const name_row_t *rows, const char *want)
{
    const char *hits[8];
    int         n = 0;

    for (int r = 0; rows[r].name != NULL; r++) {
        int all = 1;

        for (int t = 0; t < 4; t++) {
            if (rows[r].tok[t] < 0)
                continue;
            if (!token_matches(rows[r].tok[t])) {
                all = 0;
                break;
            }
        }
        if (all && (n < 8))
            hits[n++] = rows[r].name;
    }

    const int ok = ((n == 1) && !strcmp(hits[0], want));

    if (!ok)
        failures++;
    printf("  %-38s %-5s resolves to exactly \"%s\"\n", table_name,
           ok ? "ok" : "FAIL", want);
    if (!ok) {
        printf("      %d row(s) matched:", n);
        for (int i = 0; i < n; i++)
            printf(" [%s]", hits[i]);
        printf("\n");
    }
}

/* ------------------------------------------------------------------- suite */

static void
run_common(void)
{
    char buf[4096];

    /* The wake-up FN_SYS sends: "\r\r", "AT\r", "ATE0\r". */
    send_str("\r\r");
    drain(buf, sizeof(buf));
    if (strstr(buf, "ERROR") != NULL) {
        failures++;
        printf("  %-38s FAIL  a bare CR is not a command\n", "wake-up");
    } else
        printf("  %-38s ok    a bare CR is not a command\n", "wake-up");

    expect("AT", at("AT"), "OK");
    expect("ATE0 (echo off)", at("ATE0"), "OK");

    /* MD_INIT0.CSV: AT*NC<n>Z for the Supra's rows, AT+GCI=<hex> for the ELSA's,
       then AT&F2&W for both. */
    expect("AT*NC6Z    (MD_INIT0 DE/3)", at("AT*NC6Z"), "OK");
    expect("AT+GCI=04  (MD_INIT0 DE/2)", at("AT+GCI=04"), "OK");
    expect("AT&F2&W", at("AT&F2&W"), "OK");

    /* MD_INIT1.CSV, and the two strings real cabinets' NET.CFG files carry. */
    send_str("ATE0\r");
    drain(buf, sizeof(buf));
    expect("ate0m1l3S8=5S7=20S10=40 (H9751)",
           at("ATE0M1L3S8=5S7=20S10=40"), "OK");
    expect("...x3 (ND003)", at("ATE0M1L3S8=5S7=20S10=40X3"), "OK");

    /* MD_INIT2.CSV: every one of modem 3's 51 ISP rows.  Modem 2 has none. */
    expect("AT%C3&K3BN1W2 (MD_INIT2 */3)", at("AT%C3&K3BN1W2"), "OK");

    expect("ATS7?", at("ATS7?"), "020");
    expect("an unknown command is refused", at("ATJ9"), "ERROR");

    /* X3 is set above, so no dial tone detection: blind dial, then S7. */
    send_str("ATDT0676077111\r");
    drain(buf, sizeof(buf));
    if (strstr(buf, "NO CARRIER") != NULL) {
        failures++;
        printf("  %-38s FAIL  the dial answered too early\n", "dial");
    } else
        printf("  %-38s ok    nothing until S7 elapses\n", "dial");
    fake_ticks += 21000; /* S7 = 20 s */
    drain(buf, sizeof(buf));
    expect("dial times out at S7", buf, "NO CARRIER");

    /* X4 does listen for dial tone, and a dead line has none. */
    expect("ATX4", at("ATX4"), "OK");
    send_str("ATDT123\r");
    drain(buf, sizeof(buf));
    fake_ticks += 2000;
    drain(buf, sizeof(buf));
    expect("no dial tone under X4", buf, "NO DIALTONE");

    expect("ATV0 makes results numeric", at("ATV0\rAT"), "0\r");
    expect("ATV1", at("ATV1"), "OK");
}

static void
run_model(const device_t *device, const char *expect_name)
{
    printf("\n== %s ==\n", device->name);
    dev = device->init(device);

    collect_ati();
    for (int n = 0; n < 10; n++) {
        char trimmed[256];
        int  k = 0;

        for (const char *s = ati[n]; *s != '\0'; s++)
            if ((*s != '\r') && (*s != '\n') && (k < ((int) sizeof(trimmed) - 1)))
                trimmed[k++] = *s;
        trimmed[k] = '\0';
        if (k > 0)
            printf("  ATI%d -> %s\n", n, trimmed);
    }

    identify("the 2001 table (14 rows)", names_2001, expect_name);
    identify("the I.G.O. 6 table (30 rows)", names_igo6, expect_name);

    run_common();

    device->close(dev);
}

int
main(void)
{
    run_model(&char_modem_elsa_com_device, "ELSA MicroLink 56k");
    run_model(&char_modem_supra_com_device, "Diamond SupraExpress 56e PRO");

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "all checks passed",
           failures, (failures == 1) ? "" : "s");
    return failures ? 1 : 0;
}
