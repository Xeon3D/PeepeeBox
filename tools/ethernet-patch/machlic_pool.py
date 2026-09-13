"""The machine-licence pool shared by PeepeeBox and the fun.net stand-in.

A Photo Play's machlic is the 6-byte iButton serial of its dongle, written as
12 hex digits in reverse byte order.  PeepeeBox's emulated dongle can pick its
serial from a pool of POOL_SIZE values instead of the fixed "PPBOX", and the
server side knows the same pool -- so every possible cabinet can be registered
(password file) and served (machine script) before it ever connects.

    pool(i) = SHA3-256("PeepeeBox machlic <i>")[:6] as upper-case hex

photoplay.c computes the same thing with shathree's SHA3.  The operator
registration a pooled cabinet carries is  licnumb = machlic,
password = machlic reversed  (character-wise), which is what mkpool.py writes
into /master/outgoing/password/<cc> for every country.

    python machlic_pool.py            print the pool
    python machlic_pool.py 123        one entry
"""
import hashlib
import sys

POOL_SIZE = 2000


def pool(i):
    if not 0 <= i < POOL_SIZE:
        raise ValueError("pool index out of range")
    return hashlib.sha3_256(("PeepeeBox machlic %d" % i).encode()).hexdigest()[:12].upper()


def password_for(machlic):
    return machlic[::-1]


def index_of(machlic):
    m = machlic.upper()
    for i in range(POOL_SIZE):
        if pool(i) == m:
            return i
    return None


if __name__ == "__main__":
    if len(sys.argv) > 1:
        i = int(sys.argv[1]); print("%4d %s %s" % (i, pool(i), password_for(pool(i))))
    else:
        for i in range(POOL_SIZE):
            print("%4d %s %s" % (i, pool(i), password_for(pool(i))))
