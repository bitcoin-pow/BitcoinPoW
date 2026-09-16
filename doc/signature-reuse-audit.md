# Signature-work reuse audit

## Result

**High severity for the intended mining-cost model: the fork at 144444 does not
require a fresh expensive ECDSA signing computation for every mining trial.**
A miner can reuse the internal ECDSA signing nonce within a private search,
precompute its elliptic-curve point and inverse, and produce valid signatures
for different external 64-bit mining nonces using scalar arithmetic.

This is distinct from reusing identical signature bytes. It does not bypass the
proof-hash target, forge another party's signatures, or demonstrate a working
Bitcoin ASIC miner. No hardware speedup has been measured.

## Why the shortcut works

The signing implementation in `src/secp256k1/src/ecdsa_impl.h` computes:

```
R = k*G
r = x(R) mod n
s = inverse(k) * (z + r*d) mod n
```

Here `d` is the signing private key, `k` is the internal signing nonce, `n` is
the curve order, and `z` is the message interpreted as an integer. The separate
external mining nonce changes the message in `CheckBlockSignature`:

```
message_bytes = uint256(header_hash + external_nonce)
```

For fixed secret `d` and `k`, the miner can precompute `r`, `inverse(k)`, and
`r*d`. Each candidate then needs scalar arithmetic, low-S normalization, DER
encoding, and the proof hash. It does not need a new `k*G` multiplication or
modular inversion. The current message construction also permits incremental
optimizations, but the shortcut does not depend on that: hashing the external
nonce into the message would still leave the fixed-k optimization possible.

The verifier checks validity, not whether the miner used the provided
`SignMining`/RFC6979 implementation. RFC6979 signatures are deliberately
compatible with ordinary ECDSA verification; the verifier cannot infer that
nonce-generation procedure from validity alone.

Reference: [RFC 6979](https://www.rfc-editor.org/rfc/rfc6979.html), sections 2.4
and 3. Deterministic nonce generation in the supplied miner is not a consensus
enforcement mechanism.

Revealing two distinct signatures made with the same internal nonce can expose
the private key. That does not prevent this mining optimization: a miner can
keep losing candidates private and use a fresh secret internal nonce for each
new work template, publishing at most one signature from each such search.
The public `d=k=1` values in the test are only a convenient reproducer and must
never be used to protect coins. The algebra applies to arbitrary secret values.

## Reproducer

`validation_tests/mining_fixed_signing_nonce_audit` constructs 256 candidates
without calling a signing API or doing per-candidate elliptic-curve signing
work. It uses the production header-message construction, then checks:

- ECDSA verification for every generated signature;
- the fork's 78/79-byte, strict-DER and low-S rules;
- recovery of the same committed public key;
- distinct proof hashes for accepted external nonces;
- failure of literal signature reuse with the next external nonce;
- rejection of appended garbage even when the total field remains 79 bytes.

Observed result: **255 of 256 candidates passed the fork encoding rules**,
with distinct proof hashes and successful public-key recovery. All 256 ECDSA
signatures verified; one was too short for the fork size rule. The combined
`pow_tests,validation_tests` run passed all **31 tests**.

This exercises the relevant primitives, not a full mainnet block connection,
UTXO spend, or a search for a winning mainnet proof. The remaining target search
is still required. Production consensus and miner code were not changed by this
audit.

## Other paths reviewed

| Candidate shortcut | Result in reviewed code |
| --- | --- |
| Append arbitrary bytes after DER | Rejected from 144444 by strict DER, including padding that fits the size limit. Historical acceptance remains before activation. |
| Nonminimal DER integer encodings | Strict DER rejects them after activation. |
| Flip S to curve-order minus S | Low-S removes this alternate representation after activation. |
| Reuse identical signature while changing external nonce | The message changes; test rejects reuse. No general bypass found. |
| Change block transactions, parent, target, time, stake outpoint, or mining marker | These are committed directly or through the Merkle root in the signed header. No free proof-hash variation found. |
| Recover a different signing key to fit a chosen signature | Full validation binds the coinstake input/output key and checks the block signature. Recovery alone is not block acceptance. |
| Use the old nonce-free recovery fallback | It remains in `CheckRecoveredPubKeyFromBlockSignature`, but full block signature verification still requires the nonce-bearing message. No full-block bypass established. |
| Avoid signatures by using a PoW marker | Mainnet block connection rejects PoW blocks after height 10. |
| Cache or checkpoint bypass | Height-aware encoding is rechecked during connection; trusted-history exemption ends at 141410. No post-fork bypass found in these reviewed paths. |

The message uses addition modulo 2^256 before ECDSA scalar interpretation.
Cross-header compensation and ECDSA message equivalences were considered, but
no practical method to exploit them was demonstrated. This is not a proof of
security or an exhaustive cryptographic audit.

