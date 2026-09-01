"""Recover the dongle oracle's answers from a known (input, output) pair of the keyed round.

The keyed round is 39 steps of

    branch  = (b[k-1] ^ V) & 1
    V       = (V >> 1) ^ (POLY if branch)
    b[k]    = oracle(byte idx of V),  idx = b[k-1] | ((V_before & 1) << 1)

with b[0] = oracle(V0 & 0xFF).  POLY = 0x80500062 has bit 31 set and `V >> 1` does not,
so **branch is simply bit 31 of the result of that step**.  Running the recursion
backwards from the known output therefore fixes every branch with no guessing at all:

    branch[k] = V[k] >> 31
    V[k-1]    = ((V[k] ^ (POLY if branch[k])) << 1) | c[k]

The only unknowns are the reinstated low bits c[k], and they enter V0 linearly -- c[k]
contributes 2^(k-1) -- so c[1..32] are read straight off the known V0 and only c[33..39]
are free.  That is 2^7 = 128 candidate trajectories per pair, and each one yields the
oracle's answer at every step, since b[k-1] = branch[k] ^ c[k].

So each verified pair from `harvest.py` gives 39 observations of a five-bit-in,
one-bit-out function, model-free -- no assumption about how the dongle computes it.
"""
import sys

M32 = 0xFFFFFFFF
POLY = 0x80500062
STEPS = 39


def branches(vout):
    """the branch taken at every step, read off the output alone"""
    v = vout & M32
    out = [0] * (STEPS + 1)
    for k in range(STEPS, 0, -1):
        br = (v >> 31) & 1
        out[k] = br
        v = ((v ^ (POLY if br else 0)) << 1) & M32   # c added by the caller
    return out


def base_v0(vout):
    """V0 with every reinstated bit taken as zero"""
    v = vout & M32
    for k in range(STEPS, 0, -1):
        br = (v >> 31) & 1
        v = ((v ^ (POLY if br else 0)) << 1) & M32
    return v


def trajectory(v0, vout, free):
    """rebuild V[0..39] and the oracle answers; `free` supplies c[33..39]"""
    br = branches(vout)
    c = [0] * (STEPS + 1)
    delta = (v0 ^ base_v0(vout)) & M32
    for j in range(1, 33):
        c[j] = (delta >> (j - 1)) & 1
    for j in range(33, STEPS + 1):
        c[j] = (free >> (j - 33)) & 1

    v = [0] * (STEPS + 1)
    v[STEPS] = vout & M32
    for k in range(STEPS, 0, -1):
        v[k - 1] = (((v[k] ^ (POLY if br[k] else 0)) << 1) | c[k]) & M32
    if v[0] != (v0 & M32):
        return None

    b = [0] * STEPS
    for k in range(1, STEPS + 1):
        b[k - 1] = br[k] ^ c[k]

    obs = [((v0 & 0x1F), b[0])]
    for k in range(1, STEPS):
        idx = b[k - 1] | (c[k] << 1)
        obs.append((((v[k] >> (8 * idx)) & 0x1F), b[k]))
    return v, b, obs


def forward(v0, b):
    """run the round forward with a given answer sequence, to check the reconstruction"""
    v = v0 & M32
    prev = b[0]
    for k in range(1, STEPS + 1):
        cur = (prev & 1) | ((v & 1) << 1)
        v = ((v >> 1) ^ POLY) & M32 if ((cur ^ v) & 1) else (v >> 1)
        prev = b[k] if k < len(b) else 0
    return v


def main():
    tag = sys.argv[1] if len(sys.argv) > 1 else 'igo2'
    pairs = [tuple(int(x, 16) for x in l.split())
             for l in open('scratchpad/fpairs_%s.txt' % tag)]
    ok = 0
    checked = 0
    for v0, vout in pairs[:200]:
        r = trajectory(v0, vout, 0)
        if r is None:
            continue
        v, b, obs = r
        checked += 1
        if forward(v0, b) == vout:
            ok += 1
    print('%s: %d pairs reconstructed, %d replay to the exact output' % (tag, checked, ok))

    # do the 128 free choices ever disagree about the first answer?
    v0, vout = pairs[0]
    first = {trajectory(v0, vout, f)[2][0] for f in range(128)}
    print('   first observation across all 128 free choices: %s' % sorted(first))


if __name__ == '__main__':
    main()
