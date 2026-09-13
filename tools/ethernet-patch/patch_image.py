#!/usr/bin/env python3
r"""patch_image.py -- give a Photo Play disk image the Ethernet option (or take it away again).

    python patch_image.py Harddisk.img
    python patch_image.py Harddisk.img --config 86box.cfg --server funnet.example.org:8086 --secret funworld
    python patch_image.py Harddisk.img --wattcp 192.168.200.20/24,192.168.200.1
    python patch_image.py Harddisk.img --revert [--config 86box.cfg]

What it does to the image (a backup is made first):

  FN_SYS\FN_SYS.EXE      patched: setting CONNTYPE, switch /conntype, a 4th
                         "connection type" field (Modem / Ethernet) in the
                         touch-screen dial-in settings, /export no longer needs
                         a modem in Ethernet mode, and the transmission report
                         shows an Ethernet session in blue.  Original kept as
                         FN_SYS\FN_SYS.ORG.
  FN_SYS\TEXT\*.CSV      four text keys appended (all languages).
  FN_SYS\DFU\TRANSMIT.BAT  Ethernet branch: RTSPKT 0x60 / CLIENT / RTSPKT -u
                         instead of LSL / PPP / IPSTUB / PPPMENU.  Original
                         kept as TRANSMIT.ORG.
  FN_SYS\DFU\RTSPKT.COM  Realtek RTL8139 packet driver v3.40 (+ RTSPKT.TXT).
  FN_SYS\DFU\SETETH.BAT  SETETH ON / OFF / (show) from a DOS prompt.
  FN_SYS\DFU\WATTCP.ETH  WATTCP config for Ethernet mode: --wattcp dhcp
                         (default) or a static address.

--revert puts all of that back.  With --config it also writes the [Network]
section of a PeepeeBox/86Box config: the RTL8139 on the local switch of this
host, or on the remote switch named by --server (with --secret).

Only the FN_SYS.EXE build this patch was made for is supported; the tool
refuses anything else rather than guess (see README.md).
"""
import argparse
import hashlib
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fat16  # noqa: E402
import machlic_pool  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
PAYLOAD = os.path.join(HERE, "payload")

# FN_SYS.EXE builds this patch was verified on.  sha256 of the original -> description.
# (patch_fn_sys.py locates everything by structure; a build outside this list is refused only
#  because it has not been checked, not because the patcher could not try -- see README.md)
KNOWN_FN_SYS = {
    "3697e9fcf8ce53863381ddb80dc758dd3dc21d767d2c4a486c7a5725726dfdf1":
        "FN_SYS.EXE 177550 bytes, Photo Play 2001 AT (A3735)",
    "e7b8200cc18d01264ac55489b0836166dad31b19d523ee8e0dfa87ff34d1b396":
        "FN_SYS.EXE 179040 bytes, Photo Play 2001 DE/IT/NL/SP (B4821, A3735, F2311, EB711)",
    "67694bc40c00854fec70df2cf0d7967f1d655dcb9360eb273a6f2b771f5b4b5c":
        "FN_SYS.EXE 190336 bytes, I.G.O. 1/2 IT/NL/GR (75G75, 85A99)",
    "51b9f4544cdfacc84800b7798e824d54a5d19f41abef696915b72100173bda6b":
        "FN_SYS.EXE 190966 bytes, Photo Play 2001 NL MASTERS (H9751 SR1)",
    "2115461b4d5d6a06ddefd19242dd7eb365be7557661f36d95ffd61c1e3a7d8e2":
        "FN_SYS.EXE 191300 bytes, I.G.O. 1/2 BE (82C81, EA881)",
    "62bccc67a2c6c5610b1280b43fca52859125a58712e1735e70daf198aa542766":
        "FN_SYS.EXE 191300 bytes, I.G.O. 2 IT/NL/PT (EDA21, EDA22)",
    "e2c366ad32dcb04bafaadbe1d562004299be96ae30e72c5e9616ee59b239c052":
        "FN_SYS.EXE 191500 bytes, I.G.O. 1/2 DE (23B58)",
    "e4bd805b3321bcf3eaaedf80e4af7beeda216e8809203e7ba7e2a5fb834e940a":
        "FN_SYS.EXE 212766 bytes, I.G.O. 3..8 and IGO Italy (all)",
}
# sha256 of the current patcher's output for each original: "already patched, current version"
CURRENT_FN_SYS = {
    "3697e9fcf8ce53863381ddb80dc758dd3dc21d767d2c4a486c7a5725726dfdf1": "c946e50dd70bd9f06b68169ed218991bc3c0d22ceea2b8c40b4605e359d5e6b1",
    "e7b8200cc18d01264ac55489b0836166dad31b19d523ee8e0dfa87ff34d1b396": "e18b3fa188e73f2826d03ed2ddc3e422ea41d8eda86ad0602fb75004cb898521",
    "67694bc40c00854fec70df2cf0d7967f1d655dcb9360eb273a6f2b771f5b4b5c": "04d2572bdb9a4797d009bcc4f65d56722f3d6cb1dcaee183f23be93afa417854",
    "51b9f4544cdfacc84800b7798e824d54a5d19f41abef696915b72100173bda6b": "215263c2a13f16760b08e9711ac4c50e6210a9c6650237262bb825fddbeae6a4",
    "2115461b4d5d6a06ddefd19242dd7eb365be7557661f36d95ffd61c1e3a7d8e2": "22dcc85c9e6682488e52d3f4968c6b8eb02306a5d126761fbcf4b6cbf0c8bf82",
    "62bccc67a2c6c5610b1280b43fca52859125a58712e1735e70daf198aa542766": "7d7b3d5b9b02bfed003a7cfc36016ab67329b4007b79496b1bcd0c6a133bdae5",
    "e2c366ad32dcb04bafaadbe1d562004299be96ae30e72c5e9616ee59b239c052": "1bc1190f7db93793c39840880706d51425d1afe6f2b72761e99313f4987b1930",
    "e4bd805b3321bcf3eaaedf80e4af7beeda216e8809203e7ba7e2a5fb834e940a": "95355c4f54575a4b77a7c839f552534ab60a33e4916cae0d2d7fdecf4848ea59",
}
# older outputs, recognised so they are upgraded from the kept original rather than mistaken for one
PATCHED_FN_SYS = set(CURRENT_FN_SYS.values()) | {
    "8698c9b88c0bbac83cd5adac85df38142255852b38af8faad8750c1ece0bd9a2",   # v2 MASTERS: no blue report line
}

TEXT_KEYS = ("INP_conntype", "INP_conntype_exp", "conn_modem", "conn_ethernet")
TEXTS = {
    "ENG": ("connection type", "Modem: dial-up through the built-in modem. Ethernet: network card (RTL8139 packet driver), addresses in WATTCP.ETH.", "Modem", "Ethernet"),
    "GER": ("Verbindungsart", "Modem: Einwahl \xfcber das eingebaute Modem. Ethernet: Netzwerkkarte (RTL8139 Packet-Treiber), Adressen in WATTCP.ETH.", "Modem", "Ethernet"),
    "DUT": ("verbindingstype", "Modem: inbellen via het ingebouwde modem. Ethernet: netwerkkaart (RTL8139 packet driver), adressen in WATTCP.ETH.", "Modem", "Ethernet"),
    "FRE": ("type de connexion", "Modem : connexion par le modem int\xe9gr\xe9. Ethernet : carte r\xe9seau (pilote packet RTL8139), adresses dans WATTCP.ETH.", "Modem", "Ethernet"),
    "ITA": ("tipo di connessione", "Modem: connessione tramite il modem integrato. Ethernet: scheda di rete (packet driver RTL8139), indirizzi in WATTCP.ETH.", "Modem", "Ethernet"),
    "SPA": ("tipo de conexi\xf3n", "M\xf3dem: conexi\xf3n por el m\xf3dem integrado. Ethernet: tarjeta de red (packet driver RTL8139), direcciones en WATTCP.ETH.", "M\xf3dem", "Ethernet"),
}

ETH_SELECT = """rem -----------------------------------------------------------
rem Connection type: FN_SYS /conntype returns errorlevel 1 when
rem the setting CONNTYPE=ETHERNET is set  (see SETETH.BAT).
rem -----------------------------------------------------------

\\FN_SYS\\FN_SYS /conntype
if ERRORLEVEL 1 goto ETHERNET

rem ================= MODEM (PPP over serial) =================

"""

ETH_BLOCK = """rem ================= ETHERNET (packet driver) ================
rem In ethernet mode FN_SYS /export does not touch WATTCP.CFG.
rem If WATTCP.ETH exists it is used as the WATTCP.CFG for the
rem transfer (my_ip = dhcp, or a static address).
rem Packet driver: RTSPKT.COM (Realtek RTL8139) in \\FN_SYS\\DFU.
rem   NE2000 instead: NE2000 0x60 <irq> <io> / NE2000 -u 0x60
rem -----------------------------------------------------------

:ETHERNET

if exist \\fn_sys\\dfu\\WATTCP.ETH copy \\fn_sys\\dfu\\WATTCP.ETH \\fn_sys\\dfu\\WATTCP.CFG > NUL

RTSPKT 0x60
if ERRORLEVEL 1 goto ETH_NODRV

CLIENT
if ERRORLEVEL 1 goto ETH_FEHLER

rem -----------------------------------------------------------

SET MODRES=ETHERNET
\\FN_SYS\\FN_SYS /setting=MODRES;%MODRES%
SET MODRES=

\\FN_SYS\\FN_SYS /datatrans_success

RTSPKT -u

GOTO ENDE

:ETH_FEHLER
RTSPKT -u

:ETH_NODRV
SET MODRES=ETHERNET ERROR
\\FN_SYS\\FN_SYS /setting=MODRES;%MODRES%
SET MODRES=

\\FN_SYS\\FN_SYS /datatrans_error

GOTO ENDE

rem -----------------------------------------------------------

"""


def sha(b):
    return hashlib.sha256(b).hexdigest()


def crlf(text):
    return text.replace("\r\n", "\n").replace("\n", "\r\n")


def patch_fn_sys(orig):
    """Run patch_fn_sys.py on the original bytes; return the patched bytes."""
    with tempfile.TemporaryDirectory() as td:
        src, dst = os.path.join(td, "FN_SYS.ORG"), os.path.join(td, "FN_SYS.EXE")
        open(src, "wb").write(orig)
        r = subprocess.run([sys.executable, os.path.join(HERE, "patch_fn_sys.py"), src, dst],
                           capture_output=True, text=True)
        if r.returncode != 0:
            raise RuntimeError("patch_fn_sys.py failed:\n" + r.stdout + r.stderr)
        return open(dst, "rb").read()


def patch_transmit(text):
    """Insert the Ethernet branch into any TRANSMIT.BAT that has the modem stack."""
    if "/conntype" in text.lower():
        return None                                      # already done
    lines = text.replace("\r\n", "\n").split("\n")
    lsl = next((i for i, l in enumerate(lines) if l.strip().upper() == "LSL"), None)
    fehler = next((i for i, l in enumerate(lines) if l.strip().upper() == ":FEHLER"), None)
    if lsl is None or fehler is None or fehler < lsl:
        raise RuntimeError("TRANSMIT.BAT does not look like the fun.net modem script "
                           "(no 'LSL' line / ':FEHLER' label); not touching it")
    out = lines[:lsl] + ETH_SELECT.split("\n") + lines[lsl:fehler] + ETH_BLOCK.split("\n") + lines[fehler:]
    return crlf("\n".join(out))


def write_machlic(path, machlic):
    """[Photo Play] machlic = <12 hex> | random  (PeepeeBox draws one and writes it back)"""
    raw = open(path, "rb").read(); bom = raw.startswith(bytes([0xef, 0xbb, 0xbf])); text = raw.decode("utf-8-sig")
    nl = chr(13) + chr(10) if chr(13) + chr(10) in text else chr(10)
    text = re.sub(r"^machlic = .*?" + re.escape(nl), "", text, flags=re.M)
    if "[Photo Play]" + nl in text:
        text = text.replace("[Photo Play]" + nl, "[Photo Play]" + nl + "machlic = " + machlic + nl, 1)
    else:
        text = text.rstrip(chr(13) + chr(10)) + nl + nl + "[Photo Play]" + nl + "machlic = " + machlic + nl
    open(path, "wb").write(((chr(0xfeff) if bom else "") + text).encode("utf-8"))


def write_config(path, server, secret, revert=False):
    raw = open(path, "rb").read()
    bom = raw.startswith(b"\xef\xbb\xbf")
    text = raw.decode("utf-8-sig")
    nl = "\r\n" if "\r\n" in text else "\n"
    if revert:
        sec = "[Network]%snet_01_card = rtl8139c+%snet_01_net_type = none%s" % (nl, nl, nl)
    elif server:
        sec = "[Network]%snet_01_card = rtl8139c+%snet_01_net_type = nrswitch%snet_01_nrs_host = %s%s" % (nl, nl, nl, server, nl)
        if secret:
            sec += "net_01_secret = %s%s" % (secret, nl)
    else:
        sec = "[Network]%snet_01_card = rtl8139c+%snet_01_net_type = nlswitch%s" % (nl, nl, nl)
    pat = re.compile(r"\[Network\]" + re.escape(nl) + r"(?:net_\d\d_[a-z_]+ = .*?" + re.escape(nl) + r")*")
    if pat.search(text):
        text = pat.sub(lambda m: sec, text, count=1)
    else:
        if not text.endswith(nl):
            text += nl
        text += nl + sec
    open(path, "wb").write(((u"﻿" if bom else "") + text).encode("utf-8"))


def wattcp_eth(spec):
    """WATTCP.ETH from 'dhcp' or 'IP/PREFIX,GATEWAY[,DNS]' (e.g. 192.168.200.20/24,192.168.200.1)."""
    head = ("# WATTCP.ETH - WATTCP configuration used by TRANSMIT.BAT in Ethernet mode\r\n"
            "# (copied over WATTCP.CFG before CLIENT.EXE runs).\r\n#\r\n")
    if spec.lower() == "dhcp":
        return (head + "my_ip = dhcp\r\n#\r\n# static example:\r\n#my_ip = 192.168.200.20\r\n"
                "#netmask = 255.255.255.0\r\n#gateway = 192.168.200.1\r\n#nameserver = 192.168.200.1\r\n").encode()
    m = re.match(r"^(\d+\.\d+\.\d+\.\d+)/(\d+),(\d+\.\d+\.\d+\.\d+)(?:,(\d+\.\d+\.\d+\.\d+))?$", spec)
    if not m or not 0 <= int(m.group(2)) <= 32:
        raise SystemExit("--wattcp wants 'dhcp' or IP/PREFIX,GATEWAY[,DNS], e.g. 192.168.200.20/24,192.168.200.1")
    ip, prefix, gw, dns = m.group(1), int(m.group(2)), m.group(3), m.group(4) or m.group(3)
    maskbits = (0xFFFFFFFF << (32 - prefix)) & 0xFFFFFFFF
    mask = ".".join(str((maskbits >> (24 - 8 * i)) & 0xFF) for i in range(4))
    return (head + "my_ip = %s\r\nnetmask = %s\r\ngateway = %s\r\nnameserver = %s\r\n" % (ip, mask, gw, dns)).encode()


def set_setting(fs, key, value):
    """Write key=value into SETTINGS.TAB (128-byte records: flag, 26-byte key, 101-byte value);
    value None deletes the record (an all-zero record is a free slot)."""
    path = "FN_SYS\\DATABASE\\USER\\SETTINGS.TAB"
    tab = bytearray(fs.read(path))
    rec = free = None
    for i in range(0, len(tab) - 127, 128):
        if tab[i] == 1 and tab[i + 1:i + 27].split(b"\0")[0].upper() == key.upper().encode():
            rec = i
            break
        if tab[i] != 1 and free is None:
            free = i
    if value is None:                      # delete: an all-zero record is a free slot
        if rec is None:
            return False
        tab[rec:rec + 128] = b"\0" * 128
    else:
        if rec is None:
            rec = free
        if rec is None:
            return False
        tab[rec:rec + 128] = bytes([1]) + key.encode().ljust(26, b"\0") + value.encode().ljust(101, b"\0")
    fs.write(path, bytes(tab))
    return True


def set_conntype(fs, value):
    return set_setting(fs, "CONNTYPE", value)


def revert(fs):
    """Put the image back: originals restored, additions removed, text keys stripped."""
    if fs.exists("FN_SYS\\FN_SYS.ORG"):
        fs.write("FN_SYS\\FN_SYS.EXE", fs.read("FN_SYS\\FN_SYS.ORG"))
        fs.remove("FN_SYS\\FN_SYS.ORG")
        print("FN_SYS.EXE: original restored")
    else:
        print("FN_SYS.EXE: no FN_SYS.ORG in the image, left as is")
    if fs.exists("FN_SYS\\DFU\\TRANSMIT.ORG"):
        fs.write("FN_SYS\\DFU\\TRANSMIT.BAT", fs.read("FN_SYS\\DFU\\TRANSMIT.ORG"))
        fs.remove("FN_SYS\\DFU\\TRANSMIT.ORG")
        print("TRANSMIT.BAT: original restored")
    for name in ("RTSPKT.COM", "RTSPKT.TXT", "SETETH.BAT", "WATTCP.ETH"):
        if fs.remove("FN_SYS\\DFU\\" + name):
            print("%s: removed" % name)
    for name, isdir, size in fs.listdir("FN_SYS\\TEXT"):
        if isdir or not name.upper().endswith(".CSV"):
            continue
        data = fs.read("FN_SYS\\TEXT\\" + name)
        lines = data.split(b"\r\n")
        kept = [l for l in lines if l.split(b"\t")[0].decode("latin-1") not in TEXT_KEYS]
        if len(kept) != len(lines):
            fs.write("FN_SYS\\TEXT\\" + name, b"\r\n".join(kept))
            print("TEXT\\%s: %d key(s) removed" % (name, len(lines) - len(kept)))
    if set_conntype(fs, None):
        print("SETTINGS.TAB: CONNTYPE record removed")


def main(argv=None):
    ap = argparse.ArgumentParser(description="Give a Photo Play disk image the Ethernet option (or take it away again).")
    ap.add_argument("image", help="raw disk image (Harddisk.img)")
    ap.add_argument("--config", help="PeepeeBox/86Box config file to point at the switch")
    ap.add_argument("--server", metavar="HOST[:PORT]",
                    help="remote fun.net switch to use (default: local switch on this host)")
    ap.add_argument("--secret", help="shared secret of that server, if it has one")
    ap.add_argument("--machlic", metavar="random|pool:N|HEX12",
                    help="with --config: the cabinet's machine licence (the emulated dongle serial). "
                         "'random' / 'pool:N' take an entry of the pool the fun.net stand-in knows and "
                         "also register the cabinet in SETTINGS.TAB (licnumb = machlic, password = "
                         "machlic reversed) so it passes the operator-password check")
    ap.add_argument("--wattcp", default="dhcp", metavar="dhcp|IP/PREFIX,GW[,DNS]",
                    help="what WATTCP.ETH says: 'dhcp' (default) or a static address, "
                         "e.g. 192.168.200.20/24,192.168.200.1")
    ap.add_argument("--keep-wattcp", action="store_true", help="leave an existing WATTCP.ETH alone")
    ap.add_argument("--enable", action="store_true",
                    help="also switch the cabinet to Ethernet now (CONNTYPE=ETHERNET), "
                         "instead of leaving it to the service menu / SETETH ON")
    ap.add_argument("--revert", action="store_true",
                    help="undo everything: restore FN_SYS.EXE and TRANSMIT.BAT, remove the driver "
                         "and helpers, strip the text keys, CONNTYPE=MODEM (and the card's mode "
                         "to none in --config)")
    ap.add_argument("--no-backup", action="store_true", help="do not keep a copy of the image")
    cfg = ap.parse_args(argv)

    if not os.path.isfile(cfg.image):
        print("no such image:", cfg.image, file=sys.stderr)
        return 1
    wattcp = None if cfg.revert else wattcp_eth(cfg.wattcp)
    machlic_value = pooled = None
    if cfg.machlic and not cfg.revert:
        m = cfg.machlic.strip()
        if m.lower() == "random":
            pooled = random.randrange(machlic_pool.POOL_SIZE)
        elif m.lower().startswith("pool:") and m[5:].isdigit() and int(m[5:]) < machlic_pool.POOL_SIZE:
            pooled = int(m[5:])
        elif re.fullmatch(r"[0-9A-Fa-f]{12}", m):
            machlic_value = m.upper()
        else:
            raise SystemExit("--machlic wants 'random', 'pool:N' (N < %d) or 12 hex digits" % machlic_pool.POOL_SIZE)
        if pooled is not None:
            machlic_value = machlic_pool.pool(pooled)
    if not cfg.no_backup:
        tag = "revert" if cfg.revert else "ethernet"
        bak = "%s.pre-%s.bak" % (cfg.image, tag)
        if os.path.exists(bak):
            bak = "%s.pre-%s-%s.bak" % (cfg.image, tag, time.strftime("%Y%m%d-%H%M%S"))
        print("backup:", bak)
        shutil.copyfile(cfg.image, bak)

    fs = fat16.Fat16(cfg.image)
    print("image: FAT16, %d KB clusters" % (fs.clbytes // 1024))

    if cfg.revert:
        revert(fs)
        fs.close()
        if cfg.config:
            write_config(cfg.config, None, None, revert=True)
            print("%s: [Network] -> card mode none" % cfg.config)
        print("\nreverted.")
        return 0

    # ---- FN_SYS.EXE
    if fs.exists("FN_SYS\\FN_SYS.ORG") and sha(fs.read("FN_SYS\\FN_SYS.ORG")) in KNOWN_FN_SYS:
        orig = fs.read("FN_SYS\\FN_SYS.ORG")
        print("FN_SYS.ORG: known original already in the image")
    else:
        orig = fs.read("FN_SYS\\FN_SYS.EXE")
    h = sha(orig)
    patched = None
    if h in KNOWN_FN_SYS:
        cur = sha(fs.read("FN_SYS\\FN_SYS.EXE"))
        if cur == CURRENT_FN_SYS[h]:
            print("FN_SYS.EXE: already patched (current version)")
        else:
            print("FN_SYS.EXE: %s" % KNOWN_FN_SYS[h])
            patched = patch_fn_sys(orig)
            print("FN_SYS.EXE: patched (%d -> %d bytes)%s" % (len(orig), len(patched),
                  " -- older patch upgraded" if cur in PATCHED_FN_SYS else ""))
    elif h in PATCHED_FN_SYS:
        print("FN_SYS.EXE: patched by an older version and no FN_SYS.ORG to rebuild from -- leaving it")
    else:
        print("FN_SYS.EXE: unknown build (%d bytes, sha256 %s)" % (len(orig), h), file=sys.stderr)
        print("This patch only supports:", ", ".join(KNOWN_FN_SYS.values()), file=sys.stderr)
        fs.close()
        return 2
    if patched:
        fs.write("FN_SYS\\FN_SYS.ORG", orig)
        fs.write("FN_SYS\\FN_SYS.EXE", patched)

    # ---- text keys
    for name, isdir, size in fs.listdir("FN_SYS\\TEXT"):
        if isdir or not name.upper().endswith(".CSV"):
            continue
        data = fs.read("FN_SYS\\TEXT\\" + name)
        txt = data.decode("latin-1")
        have = {l.split("\t")[0] for l in txt.split("\r\n") if l}
        vals = TEXTS.get(name[:3].upper(), TEXTS["ENG"])
        add = "".join("%s\t%s\r\n" % (k, v) for k, v in zip(TEXT_KEYS, vals) if k not in have)
        if add:
            if not data.endswith(b"\r\n"):
                add = "\r\n" + add
            fs.write("FN_SYS\\TEXT\\" + name, data + add.encode("cp1252"))
            print("TEXT\\%s: %d key(s) added" % (name, add.count("\r\n")))

    # ---- TRANSMIT.BAT
    tb = fs.read("FN_SYS\\DFU\\TRANSMIT.BAT")
    new = patch_transmit(tb.decode("latin-1"))
    if new is None:
        print("TRANSMIT.BAT: already has the Ethernet branch")
    else:
        if not fs.exists("FN_SYS\\DFU\\TRANSMIT.ORG"):
            fs.write("FN_SYS\\DFU\\TRANSMIT.ORG", tb)
        fs.write("FN_SYS\\DFU\\TRANSMIT.BAT", new.encode("latin-1"))
        print("TRANSMIT.BAT: Ethernet branch added (original kept as TRANSMIT.ORG)")

    # ---- driver, helper, WATTCP config
    for name in ("RTSPKT.COM", "RTSPKT.TXT", "SETETH.BAT"):
        print("%s: %s" % (name, fs.write("FN_SYS\\DFU\\" + name, open(os.path.join(PAYLOAD, name), "rb").read())))
    if cfg.keep_wattcp and fs.exists("FN_SYS\\DFU\\WATTCP.ETH"):
        print("WATTCP.ETH: kept")
    else:
        print("WATTCP.ETH: %s (%s)" % (fs.write("FN_SYS\\DFU\\WATTCP.ETH", wattcp), cfg.wattcp))

    if cfg.enable:
        print("SETTINGS.TAB: CONNTYPE = ETHERNET" if set_conntype(fs, "ETHERNET")
              else "SETTINGS.TAB: full, CONNTYPE not set -- use SETETH ON on the cabinet")
    if pooled is not None:
        pw = machlic_pool.password_for(machlic_value)
        ok = (set_setting(fs, "licnumb", machlic_value) and set_setting(fs, "password", pw)
              and set_setting(fs, "pwd_checkstatus", "0"))
        print("SETTINGS.TAB: licnumb = %s, password = %s (pool entry %d)%s"
              % (machlic_value, pw, pooled, "" if ok else " -- table full, NOT written"))

    fs.close()

    if cfg.config:
        write_config(cfg.config, cfg.server, cfg.secret)
        print("%s: [Network] -> RTL8139 on %s" % (cfg.config, ("remote switch " + cfg.server) if cfg.server else "the local switch"))
        if machlic_value:
            write_machlic(cfg.config, machlic_value)
            print("%s: [Photo Play] machlic = %s" % (cfg.config, machlic_value))
    elif cfg.machlic:
        print("--machlic needs --config (it is an emulator setting)", file=sys.stderr)

    print()
    print("done.  On the cabinet: service menu -> basic settings -> dial-in settings,")
    print("4th field 'connection type' -> Ethernet  (or  \\FN_SYS\\DFU\\SETETH ON).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
