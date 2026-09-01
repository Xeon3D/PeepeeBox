# Phase 28 — the oracle's answers, recovered without a model

Branch: `picturedecryptingtest`.

## 1. io.hasp4's model does not fit

Phase 27 asked whether any key makes io.hasp4's silicon model reproduce a verified pair.
For I.G.O. 2 the answer is no, across the whole space: **711 table windows x 4 bit-index
readings x 256 column_mask x 256 crypt_init_vect = 186 million candidates, zero hits**
against `f(000A610D) = FD29CDA4`. The password path missed too (14,220 candidates). The
I.G.O. 3 run is still going but there is no reason to expect otherwise.

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

## 4. And an open contradiction, stated rather than smoothed over

If the oracle is an automaton reset at the start of each keyed round — which
`0x32FC2` appears to do, and which is implied by `f` being a function of its input alone
(936 distinct inputs, ~78 collisions, **zero** contradictions in Phase 27) — then two
rounds whose query sequences share a prefix must share the answer prefix.

They do not. Building a trie over the certain prefixes:

```
IGO2  300 sequences, certain prefix 4/6/14 (min/median/max), 859 nodes, 141 conflicts
IGO3  300 sequences, certain prefix 4/6/30,                   892 nodes, 153 conflicts
```

Both cannot be true. Either the reconstruction is subtly wrong, or the oracle's state is
not reset per round even though the round's result is reproducible, or the query carries
information beyond the five bits that reach the DATA lines.

The reconstruction's algebra is verified — every trajectory replays to the exact output —
but that check is self-consistent by construction and does **not** prove the recovered
answer sequence is the true one out of the 128. That is the most likely place for the
error, and it is where to look next.

## 5. Next

1. Pin the true trajectory of the 128 rather than intersecting them. The right lever is
   pairs that share an input: I.G.O. 2 and I.G.O. 3 query the same values from the same
   `V0`, so their query sequences coincide step for step while their answers differ,
   which constrains the free bits from both sides at once.
2. If the contradiction survives that, re-read `0x32FC2` and `0x32D17` for state that
   crosses rounds, and check whether `0x32D17` returns a masked bit or a wider value.
