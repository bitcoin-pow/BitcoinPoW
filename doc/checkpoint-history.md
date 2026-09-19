# Mainnet historical replay

This version downloads blocks from genesis and reconstructs the UTXO set by
processing their transactions. It trusts the historical mining proofs committed
by this mandatory header checkpoint:

- Height: **141410**
- Hash: `05d553c0600bdeff22592f1331c8bc9fd534c35cd75a892c32055dea914cd00e`

## Rules

1. Genesis is hardcoded. Other historical blocks cannot connect until their
   headers are proven to be ancestors of the exact checkpoint.
2. Through **141410**, bodies are checked against their Merkle roots and witness
   commitments. Transaction processing, coin maturity, rewards and UTXO updates
   retain their historical rules. Retired kernel and block-signature proofs are
   not evaluated. Existing `assumevalid` script-check behavior is unchanged.
3. From **141411** through **144443**, enforce the current PurePoW mining rules
   and the `0xFEEDBEE2` marker.
4. From **144444**, additionally require strict DER and low-S block signatures
   with no external mining nonce. The complete signature field must be exactly
   70 or 71 bytes and its hash must meet the work target. Difficulty switches
   from historical LWMA3 to ASERT with a 12-hour half-life and 600-second target.

The old mining searches and old signing algorithm are removed. Historical wire
formats and transaction rules remain necessary to replay the original chain.

## Synchronization and recovery

Network sync obtains checkpoint-linked headers before requesting historical
bodies. Imports can stage bodies before the checkpoint header arrives, but
staged blocks cannot change balances. Receiving the checkpoint header makes
already-staged ancestors eligible for activation.

`-reindex` and `-reindex-chainstate` rebuild balances under the same anchor.
A partial import ending below the checkpoint waits for its headers from peers.
An existing non-genesis chainstate without checkpoint-linked headers cannot be
resumed directly; use `-reindex-chainstate`, or synchronize to the checkpoint
with the previous version before upgrading. Pruned nodes still need access to
all historical bodies when rebuilding chainstate.

`-checkpoints=0` does not disable this mandatory anchor. The checkpoint and its
ancestors cannot be invalidated through RPC. Competing history is rejected.

Mainnet is the supported public network. Testnet and signet startup is disabled
because their old proofs have been removed without replacement checkpoints.
Regtest remains available for development.

## Validation

The `validation_tests` suite covers the configured mainnet anchor, missing and
wrong anchors, conflicting ancestry, staging and activation eligibility with
optional checkpoints disabled, signature-check caching, and the signature
activation boundary. Before release, also exercise a complete mainnet sync and
both reindex modes against the checkpoint, and compare the UTXO set with the
previous version. A checkpoint authenticates history; this version does not
independently re-prove retired mining work.

## Difficulty upgrade at 144444

LWMA3 remains byte-for-byte compatible in its target calculation through block
144443, including its rounding and three-fast-block override. These rules are
needed to validate headers downloaded from genesis. Changing them retroactively
would reject existing history.

For blocks 144444 through 144587, ASERT calculates each target from the target at
block 144443 multiplied by 10000, capped at the network target limit, and the
timestamp of block 144443 on the same branch. This makes the first fork block
10000 times easier, subject to the limit and compact target rounding. It compares
elapsed time with the expected 600 seconds per block. Being 12 hours ahead of
schedule halves the target (doubles difficulty); being 12 hours behind doubles
the target. Targets are bounded between one and the network proof-of-work limit.
The first upgraded block uses the eased reference directly. Through block 144587, subsequent blocks
keep that same reference and adjust for elapsed time since block 144443; the
10000-fold easing is not reapplied per block. The final pre-fork solve time does
not affect the reset.

The integer polynomial follows the [ASERT specification](https://upgradespecs.bitcoincashnode.org/2020-11-15-asert/),
with a 43200-second half-life. Wide intermediate arithmetic prevents overflow,
and explicit signed rounding makes results portable. No floating-point math,
candidate timestamp, or active-chain cache participates in consensus. Branches
resolve their own anchor, including during reorganizations. Regtest's
no-retargeting setting is honored before and after the upgrade.

This removes LWMA's rolling-window and short-burst overrides from the new rules.
It does not guarantee constant block times: timestamp variation and abrupt
hash-rate changes still affect difficulty. A 12-hour half-life responds faster
than a longer half-life and also reacts more strongly to timestamp variation.

Difficulty tests cover steady cadence, doubling/halving, fractional exponents,
extreme inputs, target limits, historical LWMA rounding, activation, alternate
branches, candidate timestamp independence, and the no-retargeting setting.

### End of the easing transition

The eased historical reference applies to exactly 144 blocks: 144444 through
144587 inclusive. Starting at 144588, ASERT uses block 144587's actual target
and timestamp on the same branch, with no multiplier. The first target after
the handoff equals the target at 144587. Later targets adjust around this new
fixed reference using the same 12-hour half-life and 600-second spacing.
The original historical reference is no longer consulted. This is a one-time
handoff, not a recurring daily reset. 144 blocks is approximately one day at
the intended block rate, not a wall-clock deadline.

## Mining-cost audit

Strict signature encoding does not enforce fresh expensive signing work per
mining trial. The [signature-work reuse audit](signature-reuse-audit.md)
reproduces valid candidates using a reused internal ECDSA nonce and precomputed
curve work. This remains unresolved; the fork must not be described as proven
ASIC-resistant or as enforcing a full ECDSA signing operation per attempt.
