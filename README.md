# Bitcoin PoW (BTCW)

Bitcoin PoW is a cryptocurrency with its own chain and wallet. BTCW blocks after the early proof-of-work period use a coin owned by the miner and a signature-based work search. The miner creates a coinstake transaction using an eligible unspent transaction output (UTXO), then searches for a block signature whose hash meets the network target.

BTCW aims to make mining practical for people running their own wallets and to make conventional pool arrangements harder. Its rules tie mining to a coin owner, but they do **not** guarantee that pools cannot exist or that every mining attempt has the same computational cost.

This repository is the BTCW node and wallet. It is based on Bitcoin Core 31.x,
but it operates the BTCW network and implements BTCW-specific block,
proof-of-stake, mining, difficulty, and historical-validation rules. Do not use
Bitcoin chain data or Bitcoin network parameters with it.

## Important wallet and mining notes

- New wallets are descriptor wallets stored in SQLite. Berkeley DB is not
  required for normal operation or mining; remaining BDB support is read-only
  and exists to migrate old wallets.
- Back up the wallet and recovery information before funding it. Losing the
  wallet's private keys also loses control of its BTCW.
- Mining currently requires an eligible P2PK or legacy P2PKH output. Default
  Bech32, wrapped SegWit, and Taproot outputs are not selected by Stage 1.
- A mining output must be spendable by the local wallet and have at least six
  confirmations. Watch-only and external-signer wallets cannot mine locally.
- Stage 2 requires the separate BTCW GPU worker. The node exchanges work with
  it through the `/shared_mem` POSIX shared-memory interface.
- The shared-memory mining interface contains sensitive private-key material
  while an attempt is active. Run the node and trusted GPU worker under a
  dedicated operating-system account, do not grant other users access, and do
  not run untrusted software as that account.
- Mining rewards return to the public key of the selected staking coin. The
  reward destination cannot currently be redirected to another address.

## Migrating a legacy BTCW wallet

Legacy Berkeley DB (`wallet.dat`) wallets must be migrated before they can be
loaded normally in this release. The `migratewallet` RPC converts them to
SQLite descriptor wallets, retaining their keys and addresses.

1. Shut down the old wallet application cleanly and keep an untouched backup.
2. To import a wallet from another installation, create a new directory such
   as `oldwallet` inside the node's wallet directory and place a copy of the
   legacy file at `<walletdir>/oldwallet/wallet.dat`. Do not overwrite an
   existing wallet. A wallet already in the wallet directory can be migrated
   under its existing name; `listwalletdir` lists the available names.
3. Start the new node and migrate the wallet without loading it first:

   ```bash
   bitcoin-cli -rpcclienttimeout=0 migratewallet "oldwallet"
   ```

   Encrypted wallets require their passphrase as the second RPC argument.
   To avoid putting it in shell history, use
   `bitcoin-cli -rpcclienttimeout=0 -stdin migratewallet "oldwallet"`, enter
   the passphrase on standard input, then end input (Ctrl-D on Unix).
4. Check the migrated wallet's balance, addresses, and transaction history,
   and make a new backup of each resulting wallet before using it.

Migration creates a `<wallet name>-<timestamp>.legacy.bak` backup and returns
its location as `backup_path`. Watch-only and other solvable scripts may be
placed in separate wallets named in the RPC result. Keep the original backup
and test migration with a copy: unusual legacy scripts may need additional
attention. See the [release notes](doc/release-notes.md#migrating-legacy-berkeley-db-wallets)
for details.

## How mining works

1. **Choose an eligible coin.** The wallet selects a mature UTXO it can spend and creates a coinstake transaction. The current kernel check does not give larger UTXOs more mining weight; owning more eligible UTXOs can provide more choices of mining input.
2. **Search for work.** The miner signs the unsigned block header, varying the internal ECDSA signing nonce between attempts. It hashes the DER signature and repeats until that hash is at or below the target set by the block difficulty.
3. **Verify the block.** Nodes check the coinstake spend, the relationship between the stake coin and the mining key, the block signature, the work target, and the block's other consensus rules.

The early chain used ordinary proof of work. Current mining combines UTXO ownership with a signature-based proof-of-work search. “Proof of Transactions” appears in older descriptions of BTCW, but there is no separate transaction-count mining stage in the current rules.

## Rules from block 144444

The rules activated at height **144444** require canonical, low-S block signatures without an external mining nonce and switch difficulty adjustment to ASERT. They also require every positive coinstake output to pay the same public key used for block signing. This includes the returned stake and the claimed block reward; a miner cannot put a positive coinstake payout directly into another key's output.

These are consensus rules: nodes reject blocks that break them. They do not control payments a miner makes later, nor do they prove that a miner cannot arrange to share work with others. Reusing private ECDSA signing state can also reduce the work needed for repeated mining trials. See the [signature reuse audit](doc/signature-reuse-audit.md) for the details and limits of that finding.

## Getting started

- Download a release from the [Bitcoin PoW releases page](https://btcw.space/download),
  or [build from source](doc/build-unix.md). Build notes for
  [Windows](doc/build-windows.md), [macOS](doc/build-osx.md), and other systems
  are in [`doc/`](doc/).
- Run BitcoinPoW Core and allow it to synchronize fully before sending funds or
  mining.
- Read [MINING.md](MINING.md) before preparing a mining wallet or starting the
  GPU worker.

### Build BitcoinPoW Core Qt on Ubuntu/Pop!_OS

Install the dependencies listed in [the Unix build guide](doc/build-unix.md),
then run these commands from the repository root—not from `src/`:

```bash
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBUILD_GUI=ON \
    -DENABLE_WALLET=ON \
    -DENABLE_IPC=OFF \
    -DBUILD_TESTS=OFF \
    -DBUILD_BENCH=OFF
cmake --build build --target bitcoin-qt -j"$(nproc)"
./build/bin/bitcoin-qt
```

`ENABLE_IPC=OFF` disables Bitcoin Core's optional Cap'n Proto multiprocess
interface. It does not disable BTCW wallet mining or the GPU shared-memory
interface.

#### Running Qt on Pop!_OS

Some Pop!_OS installations using the distribution's Qt 6.4.2 libraries can
crash in Qt's accessibility/DBus integration. If `bitcoin-qt` exits with a
segmentation fault in `libQt6Gui` or `libQt6DBus`, run it with those desktop
integrations disabled:

```bash
QT_IM_MODULE=compose \
QT_QPA_PLATFORMTHEME=none \
QT_QPA_PLATFORM=xcb \
QT_ACCESSIBILITY=0 \
DBUS_SESSION_BUS_ADDRESS=unix:path=/tmp/btcw-no-dbus \
./build/bin/bitcoin-qt
```

The nonexistent DBus socket is intentional. This workaround disables desktop
notifications, system-tray integration, accessibility services, and other
DBus desktop features for this process. It does not disable the BTCW node,
wallet, synchronization, or mining. Building against a newer Qt release is the
preferred long-term solution.

### Start continuous mining

After preparing an eligible wallet and starting the GPU worker, use the Qt
Debug Console:

```text
setstaking true 30
getstakinginfo
```

The timeout defaults to 30 seconds, so `setstaking true` and
`setstaking true 30` are equivalent. The recommended value is 30 seconds.
Avoid unnecessarily large values: the node may keep the same mining attempt
open after the chain tip or candidate block has changed, causing the GPU to
repeat work that is no longer useful before the next attempt is prepared.

Stop mining with:

```text
setstaking false
```

The same RPCs are available through `bitcoin-cli`. Continuous mining stops
when the wallet unloads or the application shuts down and must be enabled again
after a restart. See [MINING.md](MINING.md) for the complete procedure.

Mainnet is the supported public network in this codebase. Regtest is available for development. See [historical replay and activation notes](doc/checkpoint-history.md) for checkpoint, sync, and difficulty details.

## Project resources

- [Source code](https://github.com/btcw-space/BitcoinPoW)
- [Release notes](doc/release-notes.md)
- [Issue tracker](https://github.com/btcw-space/BitcoinPoW/issues)
- [Project website](https://btcw.space/)
- [Telegram](https://t.me/BitcoinPoWPoT)

## Safety and development status

Consensus and wallet software is security-sensitive. Test new builds with a
separate data directory and small amounts before relying on them. Keep offline
backups, verify downloaded releases, and never expose RPC credentials or wallet
passphrases. Source availability and successful local tests are not substitutes
for independent review of consensus-critical changes.

BitcoinPoW Core is distributed under the MIT License. See [COPYING](COPYING).
