BitcoinPoW Core 31.x Release Notes
==================================

BitcoinPoW Core 31.x updates the BTCW node and wallet to the Bitcoin Core 31.x
codebase while preserving BTCW's network, chain history, and consensus rules.
This software is for the BitcoinPoW (BTCW) network. It must not be used with a
Bitcoin data directory or Bitcoin chain data.

Report problems at:

  <https://github.com/bitcoin-pow/BitcoinPoW/issues>

Downloads and project information are available at:

  <https://btcw.space>

Important consensus change
==========================

The new BTCW rules activate at block height **144444**.

From that height, nodes enforce:

- Canonical low-S DER block signatures.
- Block signatures without the legacy external nonce suffix.
- The current BTCW mining marker in the block header.
- ASERT difficulty adjustment using BTCW's activation anchor.
- Positive coinstake outputs must pay the same public key that signs the block.

Every miner and validating node must upgrade before activation. Nodes running
incompatible consensus code may follow or produce an invalid chain after the
activation height.

Historical BTCW blocks retain their original validation and serialization
rules. The new rules do not reinterpret pre-activation blocks.

Mining
======

BTCW mining has two stages:

1. The wallet selects a mature, locally spendable staking UTXO and constructs
   the coinstake transaction.
2. The external GPU worker searches ECDSA signing nonces for a DER signature
   whose hash satisfies the network target.

This release adds wallet RPCs for mining:

- `setstaking true 30` starts continuous staking.
- `setstaking false` stops continuous staking.
- `getstakinginfo` reports mining state and eligible stake value.
- `generatestake 30` performs one mining attempt.

Stage 1 currently accepts P2PK and legacy P2PKH outputs with at least six
confirmations. SegWit, wrapped SegWit, and Taproot outputs are not selected for
staking. Encrypted wallets must be unlocked. Watch-only and external-signer
wallets cannot mine locally.

Stage 2 communicates through the `/shared_mem` POSIX shared-memory object. On
Linux this normally appears as `/dev/shm/shared_mem`. The node and GPU worker
must run as the same operating-system user.

The node logs every distinct GPU result, including its nonce, signature-work
hash, target, signature size, and whether it met the target. An accepted block
is logged separately with its block hash.

See [`MINING.md`](../MINING.md) for setup and troubleshooting instructions.

Wallet changes
==============

- New wallets use descriptors and SQLite.
- Descriptor-derived private keys are supported by BTCW staking.
- Berkeley DB is not required for normal wallet use or mining. Remaining BDB
  support is intended for migration of old wallets.
- Mining rewards return to the public key belonging to the selected staking
  output.

Back up the wallet and its recovery information before transferring funds or
upgrading. Test new releases with small amounts first.

### Migrating legacy Berkeley DB wallets

Legacy BTCW Berkeley DB (`wallet.dat`) wallets cannot be loaded for normal use
until they are migrated. Use `migratewallet` to convert them to SQLite
descriptor wallets. A Berkeley DB installation is not required: the node has
read-only support for reading legacy wallets during migration.

1. Shut down the old application cleanly and preserve an untouched wallet
   backup. Test migration using a copy.
2. If importing from another installation, create a new directory in the
   node's wallet directory and copy the legacy file into it, for example
   `<walletdir>/oldwallet/wallet.dat`. Do not overwrite another wallet.
   Wallets already present can be migrated under their existing names.
   Use `listwalletdir` to find those names.
3. Start the new node. Do not try to load the legacy wallet first. Run:

   ```bash
   bitcoin-cli -rpcclienttimeout=0 migratewallet "oldwallet"
   ```

   Migration can take a long time; `-rpcclienttimeout=0` disables the CLI
   timeout. For encrypted wallets, the second RPC argument is the wallet
   passphrase. Use standard input to avoid putting it in shell history:

   ```bash
   bitcoin-cli -rpcclienttimeout=0 -stdin migratewallet "oldwallet"
   ```

   Enter the passphrase, followed by end-of-input (Ctrl-D on Unix). The RPC
   syntax is `migratewallet "wallet_name" "passphrase"`; omit the passphrase
   for an unencrypted wallet. If using a wallet-specific RPC endpoint, its
   wallet name must match the name being migrated.
4. Verify balances, addresses, and transaction history, then create a new
   backup of every resulting wallet. Retain the original legacy backup.

The migration creates a `<wallet name>-<timestamp>.legacy.bak` backup in the
wallet directory and returns its path in `backup_path`. A wallet containing
private keys retains its name. Watch-only scripts and other solvable scripts
may be separated into `_watchonly` and `_solvables` wallets; check the returned
`wallet_name`, `watchonly_name`, and `solvables_name` fields. A purely watch-only
wallet may produce only the watch-only wallet.

Migration preserves existing keys and addresses, but newly generated addresses
use descriptor derivation rules. The migration code supports legacy wallets;
compatibility with every historical BTCW wallet and unusual script combination
has not been verified. If anything is missing or migration fails, keep the
original backup and report the problem before using the converted wallet.
See [wallet management](managing-wallets.md) for more migration and recovery
information.

Chain and network changes
=========================

- BTCW mainnet and testnet network identifiers, ports, address prefixes, and
  DNS seeds are retained.
- BTCW genesis blocks and historical checkpoints are retained.
- Mainnet history is replayed from genesis and checked against the mandatory
  historical anchor at height 141410.
- AssumeUTXO snapshots are not provided in this release.
- Mainnet is the supported public network; regtest remains available for
  development.

See [`checkpoint-history.md`](checkpoint-history.md) for historical replay,
checkpoint, and activation details.

Upgrading
=========

1. Stop mining and shut down the old node cleanly.
2. Wait for the process to exit completely.
3. Back up the wallet and data directory.
4. Install the new `bitcoind`, `bitcoin-qt`, and `bitcoin-cli` binaries.
5. Start the node with the existing BTCW data directory and allow verification
   and synchronization to finish.
6. If using a legacy Berkeley DB wallet, follow the migration instructions
   above and verify the converted wallet before resuming use.
7. Start the external GPU worker, unlock the wallet if necessary, and enable
   staking again. Staking does not resume automatically after restart.

Do not run two node versions against the same data directory at the same time.
Do not copy Bitcoin Core chainstate or wallet files into the BTCW data
directory.

Security notes
==============

The shared-memory mining interface contains sensitive private-key material
while a mining attempt is active. Run the node and trusted GPU worker under a
dedicated operating-system account. Do not give untrusted processes access to
that account or `/dev/shm/shared_mem`.

Consensus and wallet code is security-sensitive. Successful builds and local
tests are not substitutes for independent review. The limits of the current
signature-work design are described in
[`signature-reuse-audit.md`](signature-reuse-audit.md).

Compatibility
=============

BitcoinPoW Core 31.x is based on Bitcoin Core 31.x and uses its supported build
systems and platform libraries. Consult the platform-specific instructions in
`doc/build-unix.md`, `doc/build-windows.md`, and `doc/build-osx.md` before
building from source.

Credits
=======

BitcoinPoW Core includes work from the BitcoinPoW contributors and the Bitcoin
Core project. It is distributed under the MIT License; see `COPYING`.
