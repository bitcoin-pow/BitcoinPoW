# Bitcoin PoW (BTCW)

Bitcoin PoW is a cryptocurrency with its own chain and wallet. BTCW blocks after the early proof-of-work period use a coin owned by the miner and a signature-based work search. The miner creates a coinstake transaction using an eligible unspent transaction output (UTXO), then searches for a block signature whose hash meets the network target.

BTCW aims to make mining practical for people running their own wallets and to make conventional pool arrangements harder. Its rules tie mining to a coin owner, but they do **not** guarantee that pools cannot exist or that every mining attempt has the same computational cost.

## How mining works

1. **Choose an eligible coin.** The wallet selects a mature UTXO it can spend and creates a coinstake transaction. The current kernel check does not give larger UTXOs more mining weight; owning more eligible UTXOs can provide more choices of mining input.
2. **Search for work.** The miner signs the unsigned block header, varying the internal ECDSA signing nonce between attempts. It hashes the DER signature and repeats until that hash is at or below the target set by the block difficulty.
3. **Verify the block.** Nodes check the coinstake spend, the relationship between the stake coin and the mining key, the block signature, the work target, and the block's other consensus rules.

The early chain used ordinary proof of work. Current mining combines UTXO ownership with a signature-based proof-of-work search. “Proof of Transactions” appears in older descriptions of BTCW, but there is no separate transaction-count mining stage in the current rules.

## Rules from block 144444

The rules activated at height **144444** require canonical, low-S block signatures without an external mining nonce and switch difficulty adjustment to ASERT. They also require every positive coinstake output to pay the same public key used for block signing. This includes the returned stake and the claimed block reward; a miner cannot put a positive coinstake payout directly into another key's output.

These are consensus rules: nodes reject blocks that break them. They do not control payments a miner makes later, nor do they prove that a miner cannot arrange to share work with others. Reusing private ECDSA signing state can also reduce the work needed for repeated mining trials. See the [signature reuse audit](doc/signature-reuse-audit.md) for the details and limits of that finding.

## Getting started

- Download a release from the [Bitcoin PoW releases page](https://btcw.space/download), or [build from source](doc/build-unix.md). Build notes for [Windows](doc/build-windows.md), [macOS](doc/build-osx.md), and other systems are in [`doc/`](doc/).
- Run a BTCW wallet and let it synchronize with mainnet. Keep a secure backup of your wallet and keys.
- Mining requires an eligible, mature UTXO in a wallet that can sign for it. The wallet creates the coinstake and searches for a valid block. Splitting coins into many outputs is optional; more outputs also create more UTXOs to manage and spend.

Mainnet is the supported public network in this codebase. Regtest is available for development. See [historical replay and activation notes](doc/checkpoint-history.md) for checkpoint, sync, and difficulty details.

## Project resources

- [Source code](https://github.com/btcw-space/BitcoinPoW)
- [Release notes](doc/release-notes.md)
- [Issue tracker](https://github.com/btcw-space/BitcoinPoW/BitcoinPoW/issues)
- [Project website](https://btcw.space)
- [Telegram](https://t.me/BitcoinPoWPoT)
