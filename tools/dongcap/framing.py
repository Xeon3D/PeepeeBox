#!/usr/bin/env python3
r"""Work out how a guest frames its dongle queries, from a PEEPEEBOX_LPT_TRACE log.

    python tools/dongcap/framing.py <86box-trace.log>

Background.  The keyed round the picture cipher needs is consulted as a repeated
three-write, one-read pattern:

    pay, pay|clk, pay, read_status

I.G.O. 2 puts the clock on DATA bit 4 and the five query bits in 1,2,3,5,6
(docs/research-v2/05.4).  I.G.O. 3's writes look like the same shape with the
clock somewhere else, which would mean the device never recognises a query --
it watches bit 4, which on that guest is part of the payload.  This tool decides
that from the wire instead of by eye.

It reports, in order:

  1. the raw event mix, and how far the boot got;
  2. the repeat factor, which the transport applies to every write;
  3. every V, V^bit, V triple found, and which bit is the clock;
  4. the distinct payloads -- 32 of them means a five-bit query;
  5. which payload bits are live, constant or free, and whether an affine
     five-bit index can be read out of them;
  6. the call sites doing it, so the routine can be found in the binary;
  7. the round structure: queries between resets (forty is I.G.O. 2's number).

Nothing here needs hardware, and nothing validates the *answers* -- the run that
produced the trace had no real part on the wire, so the status values in it are
this device's own guesses.  What the trace can settle is the framing, and that is
what this measures.

No third-party modules; Python 3.8+.
"""

import collections
import re
import sys

# I.G.O. 2's framing, measured -- docs/research/30 s9.1.  Used only as the
# reference to compare a new guest against.
IGO2_PAYLOAD = sorted({((q << 1) & 0x0E) | ((q << 2) & 0x60) | 0x80 for q in range(32)})
IGO2_CLOCK   = 0x10

LINE = re.compile(
    r'^PPRAW\s+(\d+)\s+((?:[0-9A-Fa-f]{4}:[0-9A-Fa-f]{4}\s+)*)'
    r'(write_data|read_data|write_ctrl|read_ctrl|read_status)\s+([0-9A-Fa-f]{2})\s*$'
)

Event = collections.namedtuple('Event', 'seq stack kind val')


def parse(path):
    """Every PPRAW line in order.  Lines the emulator interleaves (the capped
    'PP: raw ...' window, code dumps, other devices' logging) are ignored."""
    out = []
    with open(path, 'r', errors='replace') as f:
        for line in f:
            m = LINE.match(line.strip())
            if m:
                out.append(Event(int(m.group(1)), m.group(2).split(),
                                 m.group(3), int(m.group(4), 16)))
    return out


def collapse(events):
    """Drop the transport's repeats.

    Every logical write appears many times over, each followed by a control
    read.  Collapse a run of identical consecutive write_data values into one
    event, and drop read_ctrl entirely -- it carries no dongle information.
    Status reads are kept as they are: they are the part's turn to speak."""
    out = []
    for e in events:
        if e.kind == 'read_ctrl':
            continue
        if (e.kind == 'write_data' and out
                and out[-1].kind == 'write_data' and out[-1].val == e.val):
            continue
        out.append(e)
    return out


def repeat_factor(events):
    """How many times each logical write is repeated, as a histogram."""
    hist = collections.Counter()
    run = 0
    prev = None
    for e in events:
        if e.kind != 'write_data':
            continue
        if prev is not None and e.val == prev:
            run += 1
        else:
            if prev is not None:
                hist[run] += 1
            run = 1
        prev = e.val
    if prev is not None:
        hist[run] += 1
    return hist


def find_triples(logical):
    """Consultations: a write V, a write differing from V in exactly one bit,
    then V again.  Returns (clock_bit, payload, index_of_first_write) tuples.

    Keying on 'exactly one bit differs' rather than on a known clock is the
    whole point -- it finds the clock rather than assuming it."""
    hits = []
    w = [(i, e) for i, e in enumerate(logical) if e.kind == 'write_data']
    for k in range(len(w) - 2):
        (i0, a), (_, b), (_, c) = w[k], w[k + 1], w[k + 2]
        if a.val != c.val:
            continue
        d = a.val ^ b.val
        if d and (d & (d - 1)) == 0:      # exactly one bit
            hits.append((d, a.val, i0))
    return hits


def bit_report(payloads):
    """Which bits vary across the payload set, which are always set, which never."""
    always = 0xFF
    ever = 0x00
    for p in payloads:
        always &= p
        ever |= p
    live = ever & ~always & 0xFF
    return always, ever, live


def affine_index(payloads, live):
    """If the payloads are a five-bit query scattered over the live bits, then
    exactly 32 of them exist and gathering the live bits in order reproduces
    0..31 exactly once each.  Try that, and report which bit carries which
    query bit."""
    bits = [b for b in range(8) if live & (1 << b)]
    seen = {}
    for p in payloads:
        idx = 0
        for n, b in enumerate(bits):
            if p & (1 << b):
                idx |= 1 << n
        seen.setdefault(idx, []).append(p)
    ok = len(payloads) == (1 << len(bits)) and all(len(v) == 1 for v in seen.values())
    return bits, seen, ok


def microwire_decode(writes, cs_bit, sk_bit, di_bit, abits):
    """Run the device's own Microwire state machine over a write stream under a
    candidate pin assignment, and return the READ instructions it decodes.

    This is hd_write_data() from src/device/dongle_photoplay.c, with the three
    line masks as parameters instead of constants -- so a guest that drives the
    same part on different pins shows up as a different assignment decoding
    cleanly where the shipped one decodes nothing."""
    CS, SK, DI = 1 << cs_bit, 1 << sk_bit, 1 << di_bit
    IDLE, OP, ADDR, READ, WRITE, DONE = range(6)

    ph, n, op, addr, last_sk = IDLE, 0, 0, 0, 0
    reads = []
    for val in writes:
        sel, clk, dat = bool(val & CS), bool(val & SK), bool(val & DI)
        if not sel:
            ph, n = IDLE, 0
        elif clk and not last_sk:
            if ph == IDLE:
                if dat:
                    ph, n, op = OP, 0, 0
            elif ph == OP:
                op = (op << 1) | dat
                n += 1
                if n == 2:
                    ph, n, addr = ADDR, 0, 0
            elif ph == ADDR:
                addr = (addr << 1) | dat
                n += 1
                if n == abits:
                    n = 0
                    if op == 2:
                        reads.append(addr)
                        ph = READ
                    elif op == 1:
                        ph = WRITE
                    else:
                        ph = DONE
            elif ph == READ:
                n += 1
                if n >= 16:
                    ph, n = DONE, 0
            elif ph == WRITE:
                n += 1
                if n == 16:
                    ph, n = DONE, 0
        last_sk = clk
    return reads


def microwire_search(logical):
    """Try every pin assignment and report the ones that decode record reads.

    The shipped device uses CS = bit 1, SK = bit 5, DI = bit 6 with six address
    bits (docs/research-v2/05.2), measured off a real I.G.O. 2 part.  A guest
    that decodes nothing under that is either not doing Microwire at all or is
    doing it on other lines; this says which."""
    writes = [e.val for e in logical if e.kind == 'write_data']
    if not writes:
        print("   no writes to decode.")
        return

    results = []
    for cs in range(8):
        for sk in range(8):
            for di in range(8):
                if len({cs, sk, di}) != 3:
                    continue
                for abits in (6, 8):
                    reads = microwire_decode(writes, cs, sk, di, abits)
                    if reads:
                        results.append((len(reads), cs, sk, di, abits, reads))

    if not results:
        print("   No pin assignment decodes a single read instruction.")
        print("   So this guest is not clocking Microwire on any three of these")
        print("   eight lines -- the memory layer is reached some other way.")
        return

    results.sort(reverse=True)
    shipped = (1, 5, 6, 6)
    print("   %-28s %6s  %s" % ("pins (CS/SK/DI, addr bits)", "reads", "addresses seen"))
    for nread, cs, sk, di, abits, reads in results[:10]:
        uniq = sorted(set(reads))
        show = " ".join("%02X" % a for a in uniq[:14])
        if len(uniq) > 14:
            show += " ..."
        mark = "  <- shipped" if (cs, sk, di, abits) == shipped else ""
        print("   CS=%d SK=%d DI=%d, %d bits %6d  %s%s"
              % (cs, sk, di, abits, nread, show, mark))

    best = results[0]
    nread, cs, sk, di, abits, reads = best
    print()
    print("   Best: CS = DATA bit %d, SK = bit %d, DI = bit %d, %d address bits"
          % (cs, sk, di, abits))
    print("         %d read instructions, %d distinct addresses"
          % (nread, len(set(reads))))
    lo, hi = min(reads), max(reads)
    print("         address range %02X..%02X" % (lo, hi))
    if lo >= 8 and hi <= 63:
        print("         -- inside words 8..63, which is exactly where the record")
        print("            lives.  That is what a genuine record read looks like.")
    else:
        print("         -- NOT confined to words 8..63, so treat this as a")
        print("            coincidental decode rather than the record being read.")
    print("   First 24 in order: " + " ".join("%02X" % a for a in reads[:24]))


def main(path):
    events = parse(path)
    if not events:
        print("No PPRAW lines in %s." % path)
        print("Was the run made with PEEPEEBOX_LPT_TRACE=1?")
        return 1

    print("=" * 74)
    print("1. What is in the trace")
    print("=" * 74)
    kinds = collections.Counter(e.kind for e in events)
    for k, n in kinds.most_common():
        print("   %-12s %7d" % (k, n))
    print("   %-12s %7d" % ("TOTAL", len(events)))

    print()
    print("=" * 74)
    print("2. The transport's repeat factor")
    print("=" * 74)
    hist = repeat_factor(events)
    for run, n in sorted(hist.items()):
        print("   a write repeated %3d x : %5d times" % (run, n))

    logical = collapse(events)
    nw = sum(1 for e in logical if e.kind == 'write_data')
    nr = sum(1 for e in logical if e.kind == 'read_status')
    print("   -> %d logical writes, %d status reads" % (nw, nr))

    print()
    print("=" * 74)
    print("3. Consultation triples  (V, V^bit, V)")
    print("=" * 74)
    hits = find_triples(logical)
    if not hits:
        print("   none found -- this guest does not use the three-write shape.")
        return 0
    clocks = collections.Counter(d for d, _, _ in hits)
    for d, n in clocks.most_common():
        print("   clock bit %d (mask %02X) : %5d triples" % (d.bit_length() - 1, d, n))
    clock = clocks.most_common(1)[0][0]
    print("   -> clock is bit %d (mask %02X); I.G.O. 2 uses bit 4 (mask 10)"
          % (clock.bit_length() - 1, clock))

    payloads = sorted({p for d, p, _ in hits if d == clock})

    print()
    print("=" * 74)
    print("4. The payloads")
    print("=" * 74)
    print("   %d distinct values:" % len(payloads))
    for i in range(0, len(payloads), 16):
        print("     " + " ".join("%02X" % p for p in payloads[i:i + 16]))
    print("   32 would mean a five-bit query, which is what the 32-entry key wants.")
    same = set(payloads) == set(IGO2_PAYLOAD)
    print("   identical to I.G.O. 2's payload set? %s" % ("yes" if same else "no"))
    if not same:
        extra = set(payloads) - set(IGO2_PAYLOAD)
        if extra:
            print("     values I.G.O. 2 never sends: "
                  + " ".join("%02X" % p for p in sorted(extra)))

    print()
    print("=" * 74)
    print("5. Bit layout")
    print("=" * 74)
    always, ever, live = bit_report(payloads)
    print("   always set : %02X   ever set : %02X   live (varying) : %02X"
          % (always, ever, live))
    print("   live bits  : %s" % ", ".join(str(b) for b in range(8) if live & (1 << b)))
    bits, seen, ok = affine_index(payloads, live)
    if ok:
        print("   The live bits gather to 0..%d exactly once each." % ((1 << len(bits)) - 1))
        print("   So the query index is, low bit first:")
        for n, b in enumerate(bits):
            print("       q%d  <-  DATA bit %d" % (n, b))
        expr = " | ".join("((val >> %d) & 1) << %d" % (b, n) for n, b in enumerate(bits))
        print("   i%d = %s;" % (len(bits), expr))
    else:
        print("   The live bits do NOT gather to a clean index:")
        print("     %d payloads over %d live bits (%d slots)"
              % (len(payloads), len(bits), 1 << len(bits)))
        dup = {k: v for k, v in seen.items() if len(v) > 1}
        if dup:
            print("     collisions: " + ", ".join(
                "%d<-%s" % (k, "/".join("%02X" % x for x in v))
                for k, v in sorted(dup.items())[:8]))
        print("   Read that as: the payload is not a plain scatter of the index,")
        print("   or the boot did not exercise the whole query space.")

    print()
    print("=" * 74)
    print("6. Who is driving it")
    print("=" * 74)
    sites = collections.Counter()
    wl = [(i, e) for i, e in enumerate(logical) if e.kind == 'write_data']
    pos = {i: e for i, e in wl}
    for d, p, i0 in hits:
        if d == clock and i0 in pos:
            sites[" ".join(pos[i0].stack)] += 1
    for stack, n in sites.most_common(5):
        print("   %5d  %s" % (n, stack))
    print("   (innermost frame last; that is the routine to disassemble)")

    print()
    print("=" * 74)
    print("7. Round structure")
    print("=" * 74)
    # A round is delimited by whatever the guest does between bursts of
    # consultations.  Measure the gaps: consecutive triples that are adjacent in
    # the logical stream belong to one round.
    idxs = sorted(i0 for d, _, i0 in hits if d == clock)
    runs = []
    cur = 1
    for a, b in zip(idxs, idxs[1:]):
        if b - a <= 3:
            cur += 1
        else:
            runs.append(cur)
            cur = 1
    runs.append(cur)
    rhist = collections.Counter(runs)
    for length, n in sorted(rhist.items()):
        mark = "   <- I.G.O. 2's round length" if length == 40 else ""
        print("   runs of %3d consultations : %4d%s" % (length, n, mark))
    print("   total consultations: %d in %d runs" % (len(idxs), len(runs)))

    print()
    print("=" * 74)
    print("8. Microwire pin search")
    print("=" * 74)
    microwire_search(logical)

    print()
    print("=" * 74)
    print("What this does and does not settle")
    print("=" * 74)
    print("   Settled: the clock line, the payload set, the bit layout and the")
    print("   round length -- all properties of the guest, so no part is needed.")
    print("   Not settled: whether the ANSWERS are right.  The run had no real")
    print("   dongle on the wire, so every status value in this trace is this")
    print("   device's own guess.  That is a boot test, not a trace test.")
    return 0


if __name__ == '__main__':
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1]))
