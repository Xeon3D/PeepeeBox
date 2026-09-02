# Phase 28 — the oracle's answers, recovered without a model

Branch: `picturedecryptingtest`.

## 1. io.hasp4's model does not fit

Phase 27 asked whether any key makes io.hasp4's silicon model reproduce a verified pair.
The answer is no for **both** releases, across the whole space: **711 table windows x 4
bit-index readings x 256 column_mask x 256 crypt_init_vect = 186 million candidates each,
zero hits**.

```
target f(000A610D) = FD29CDA4 over 719-byte dump    done: 0 hit(s)     I.G.O. 2
target f(000A610D) = C7E4A1CC over 719-byte dump    done: 0 hit(s)     I.G.O. 3
```

The password path missed too, at 14,220 candidates per release.

So the model is wrong, and guessing at key material inside it is finished.

## 2. The LFSR gives up the oracle's answers for free

`POLY = 0x80500062` has bit 31 set and `V >> 1` does not, so at every step

```
branch[k] = V[k] >> 31
```

— the branch is simply the top bit of that step's result. Running the recursion backwards
from a known output therefore fixes **every branch with no guessing at all**:

```
V[k-1] = ((V[k] ^ (POLY if branch[k])) << 1) | c[k]
```

and the only unknowns are the low bits `c[k]` shifted back in. Each `c[j]` contributes
exactly `2^(j-1)` to `V0`, so `c[1..32]` are read straight off the known input and only
`c[33..39]` are free — **2^7 = 128 candidate trajectories per pair**. The oracle's answer
at each step then falls out as `b[k-1] = branch[k] ^ c[k]`.

Validated: over 200 pairs per release, every reconstruction **replays forward to the exact
output, 200/200** (`scratchpad/unroll.py`).

That gives 39 observations of the oracle per verified pair, with no assumption whatever
about how the dongle computes them.

## 3. The oracle is stateful, not a table

Queries are five bits — `0x32F59` sends only bits 0..4 — so a stateless oracle would be a
32-entry table. Keeping only observations that are identical across all 128 free choices,
and so certain:

```
IGO2  2704 certain observations, 32 distinct queries, all 32 with conflicting answers
IGO3  2741 certain observations, 32 distinct queries, all 32 with conflicting answers
```

Every one of the 32 query values is answered both ways somewhere. **The oracle carries
state.**

## 4. The oracle carries state across rounds — the contradiction, resolved

There is a test with no ambiguity at all. The first observation is
`(V0 & 0x1F, branch[1] ^ (V0 & 1))`, and **both terms are fully determined** — the seven
free bits cannot touch it. If the oracle were reset at the start of each keyed round, the
same first query would have to give the same first answer.

It does not. Over all 936 pairs, **all 32 query values are answered both ways, at close to
50/50**:

```
IGO2   q=0 {1:28, 0:17}   q=1 {0:25, 1:15}   q=2 {0:18, 1:19}   q=3 {1:14, 0:13} …
IGO3   q=0 {1:22, 0:22}   q=1 {1:26, 0:16}   q=2 {1:18, 0:18}   q=3 {1:14, 0:14} …
```

Phase 27 appeared to say the opposite, and it was wrong: its 78 agreeing collisions are
two inputs seen forty times each, **always at buffer 0**, the first buffer every entry
shares. Same input, same position, same answer — a test that could not fail. Corrected in
place there.

So the keyed round is **not** a function of its input alone, and the dongle's state
survives from one round to the next. That also explains the trie conflicts: 141 over 300
sequences, and a greedy search for a globally consistent assignment placed only about half
the pairs (107/200 and 104/200). There is no consistent reset automaton to find, because
the premise is false.

`0x32FC2` fits this on re-reading: it does not simply reset. It sets one byte, then calls
`0x32F36` to **read a value back from the part** and feeds that value straight into
`0x32DE2` before sending `0x4E`. That is a continuation of whatever state the part already
holds, not an initialisation to a constant.

### What it costs, and what it does not

Nothing in Phase 26 or 27's decryption is affected: buffers are solved individually and
507 of 507 verified, and that stands. What is lost is the hope of computing the keyed
round from its input, which is what the emulator would need.

## 5. Next

1. Thread the observations in **session order** rather than treating rounds as
   independent. Each buffer contributes two rounds, and the state runs through both; the
   question is what resets it. All entries agree at buffer 0, so the state is the same at
   the start of every file, which is the anchor to work from.
2. Establish the reset boundary directly: whether it is per file, per `DecodeData` call
   (one per 4 KB buffer), or per keyed round with a carried seed. Buffer 0 agreeing across
   entries is consistent with the first two and rules out nothing yet.
3. Read `0x32F36` and `0x32DE2`, which are what `0x32FC2` uses to carry the part's state
   forward. Those two decide what the emulated device has to remember.
