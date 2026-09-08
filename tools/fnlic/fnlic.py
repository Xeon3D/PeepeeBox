r"""fun.net licence numbers: check one, or make one.

The operator setup's fun.net registration page asks for three things -- a licence
number, a password, and the Photo Play serial number.  Only the licence number is
checked on the machine; the other two are just posted to the server.  So the
licence number is the one that stops a cabinet with no telephone line from ever
getting past the registration page, and it is a self-checking string with no
secret in it at all.

    NL-893-VISS-461OFK          # a real one, off a registered 2001 cabinet

The format is `LLAAAAAAAZZZLLL`, which is FN_SYS.EXE's own mask string:

    positions  0..1    L   'A'..'Z'
    positions  2..8    A   '0'..'Z'   (the check is a range, so it also lets
                                       through  :  ;  <  =  >  ?  @ )
    positions  9..11   Z   '0'..'9'
    positions 12..14   L   'A'..'Z'   -- and these three are computed

and the last three characters are check letters over the first twelve:

    c1 = 'A' + (sum over i in  0..4  of  s[i] XOR i)       mod 26
    c2 = 'A' + (sum over i in  5..11 of  s[i] XOR (i - 5)) mod 26
    c3 = 'A' + (sum over i in  0..11 of  s[i] XOR i)       mod 26

That is the whole algorithm.  It uses nothing about the machine -- not the serial
number, not the dongle, not the date -- so any string of the right shape with the
right three letters on the end is accepted by every cabinet.

Where it comes from: `\FN_SYS\FN_SYS.EXE`, the 458-byte routine at image offset
0x626E in the 2001 build, reached from the registration page at 0x6739 and
answering "valid" in AX.  The same 458 bytes appear byte for byte in the I.G.O. 6
build, so one algorithm covers every release.  docs/research/34-funnet-licence.md
has the disassembly and the derivation.

    python fnlic.py check NL-893-VISS-461OFK
    python fnlic.py gen --country NL -n 5
    python fnlic.py gen --body NL893VISS461         # complete a given first 12
    python fnlic.py selftest

No third-party modules; Python 3.8+.
"""
import argparse
import random
import string
import sys

LETTERS = string.ascii_uppercase
DIGITS = string.digits
ALNUM = string.digits + string.ascii_uppercase

LENGTH = 15
GROUPS = (2, 3, 4, 6)           # NL-893-VISS-461OFK

# A licence that a real registered cabinet had in its SETTINGS.TAB.  The point of
# keeping it here is that it is the only known-good sample: if a change to this
# file stops reproducing its three check letters, the change is wrong.
KNOWN_GOOD = "NL893VISS461OFK"


def strip(s):
    """Accept the number with or without its dashes, and in any case."""
    return "".join(ch for ch in s.upper() if ch != "-" and not ch.isspace())


def group(s):
    """NL893VISS461OFK -> NL-893-VISS-461OFK"""
    out = []
    i = 0
    for n in GROUPS:
        out.append(s[i:i + n])
        i += n
    return "-".join(p for p in out if p)


def check_letters(body):
    """The three check letters for a 12-character body."""
    b = [ord(c) for c in body]
    s1 = sum(b[i] ^ i for i in range(0, 5))
    s2 = sum(b[i] ^ (i - 5) for i in range(5, 12))
    s3 = sum(b[i] ^ i for i in range(0, 12))
    return "".join(LETTERS[s % 26] for s in (s1, s2, s3))


def shape_ok(s, upto=LENGTH):
    """The character-class test FN_SYS does before it computes anything.

    Positions 2..8 are a range check against '0'..'Z', not an isalnum(), so the
    seven punctuation characters between '9' and 'A' pass on real hardware.  That
    is reproduced rather than tidied up, because the point of this function is to
    say what the cabinet accepts."""
    for i in range(min(upto, LENGTH)):
        c = s[i]
        if i < 2 or i >= 12:
            if not ("A" <= c <= "Z"):
                return False, i, "expected a letter A-Z"
        elif i < 9:
            if not ("0" <= c <= "Z"):
                return False, i, "expected '0'..'Z'"
        else:
            if not ("0" <= c <= "9"):
                return False, i, "expected a digit 0-9"
    return True, None, None


def valid(s):
    s = strip(s)
    if len(s) != LENGTH:
        return False
    ok, _, _ = shape_ok(s)
    if not ok:
        return False
    return s[12:] == check_letters(s[:12])


def complete(body):
    """Turn a 12-character body into a full licence number."""
    body = strip(body)
    if len(body) != 12:
        raise ValueError("the body is 12 characters, got %d" % len(body))
    ok, i, why = shape_ok(body, 12)
    if not ok:
        raise ValueError("position %d (%r): %s" % (i, body[i], why))
    return body + check_letters(body)


def generate(country=None, rng=random):
    body = country if country else "".join(rng.choice(LETTERS) for _ in range(2))
    body = strip(body)
    if len(body) != 2 or not all("A" <= c <= "Z" for c in body):
        raise ValueError("the country prefix is two letters A-Z")
    body += "".join(rng.choice(ALNUM) for _ in range(7))
    body += "".join(rng.choice(DIGITS) for _ in range(3))
    return complete(body)


def explain(s):
    s = strip(s)
    b = [ord(c) for c in s[:12]]
    rows = [
        ("c1", range(0, 5), lambda i: i, s[12] if len(s) > 12 else "?"),
        ("c2", range(5, 12), lambda i: i - 5, s[13] if len(s) > 13 else "?"),
        ("c3", range(0, 12), lambda i: i, s[14] if len(s) > 14 else "?"),
    ]
    for name, span, key, got in rows:
        terms = ["%c^%d=%d" % (s[i], key(i), b[i] ^ key(i)) for i in span]
        total = sum(b[i] ^ key(i) for i in span)
        want = LETTERS[total % 26]
        print("  %s  %s" % (name, " + ".join(terms)))
        print("      = %d, mod 26 = %d -> %r   (number has %r) %s"
              % (total, total % 26, want, got,
                 "ok" if want == got else "MISMATCH"))


def main():
    ap = argparse.ArgumentParser(
        description="check or generate a fun.net licence number",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__.split("Where it comes from")[0])
    sub = ap.add_subparsers(dest="cmd")

    p = sub.add_parser("check", help="is this licence number valid?")
    p.add_argument("number")
    p.add_argument("--explain", action="store_true", help="show the sums")

    p = sub.add_parser("gen", help="make valid licence numbers")
    p.add_argument("-n", type=int, default=1, help="how many (default 1)")
    p.add_argument("--country", help="two-letter prefix, e.g. NL")
    p.add_argument("--body", help="a 12-character body to complete")
    p.add_argument("--seed", type=int, help="make the output repeatable")
    p.add_argument("--plain", action="store_true", help="no dashes")

    sub.add_parser("selftest", help="reproduce the known-good sample")

    args = ap.parse_args()

    if args.cmd == "check":
        s = strip(args.number)
        if len(s) != LENGTH:
            print("%s: %d characters, expected %d" % (args.number, len(s), LENGTH))
            return 1
        ok, i, why = shape_ok(s)
        if not ok:
            print("%s: position %d (%r): %s" % (group(s), i, s[i], why))
            return 1
        want = check_letters(s[:12])
        good = want == s[12:]
        print("%s  %s" % (group(s), "valid" if good else
                          "INVALID -- check letters should be %s" % want))
        if args.explain:
            explain(s)
        return 0 if good else 1

    if args.cmd == "gen":
        rng = random.Random(args.seed) if args.seed is not None else random
        try:
            for _ in range(args.n):
                s = complete(args.body) if args.body else generate(args.country, rng)
                print(s if args.plain else group(s))
                if args.body:
                    break
        except ValueError as e:
            print("fnlic: %s" % e, file=sys.stderr)
            return 1
        return 0

    if args.cmd == "selftest":
        got = check_letters(KNOWN_GOOD[:12])
        print("known-good sample : %s" % group(KNOWN_GOOD))
        print("check letters     : computed %s, stored %s" % (got, KNOWN_GOOD[12:]))
        if got != KNOWN_GOOD[12:]:
            print("SELFTEST FAILED")
            return 1
        # and a round trip: everything generated must verify
        rng = random.Random(1)
        for _ in range(20000):
            if not valid(generate(rng=rng)):
                print("SELFTEST FAILED: generated a number that does not verify")
                return 1
        print("20000 generated numbers all verify")
        print("selftest passed")
        return 0

    ap.print_help()
    return 1


if __name__ == "__main__":
    sys.exit(main())
