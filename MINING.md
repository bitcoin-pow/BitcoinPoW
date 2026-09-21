# Mining BTCW

BTCW mining uses a descriptor wallet for Stage 1 stake selection and an
external GPU worker for Stage 2 block-signature work. Berkeley DB is not
required; new wallets use SQLite.

## Requirements

- A fully synchronized BTCW node.
- A loaded descriptor wallet containing private keys.
- An eligible P2PK or legacy P2PKH output with at least six confirmations.
- An unlocked wallet if it is encrypted.
- The BTCW GPU worker running with access to the `/shared_mem` POSIX
  shared-memory interface. On Linux this normally appears as
  `/dev/shm/shared_mem`.

Watch-only wallets and external-signer wallets cannot mine locally. Stage 1
currently skips P2WPKH, P2SH-P2WPKH, and Taproot outputs.

## Prepare a mining address

Generate a legacy P2PKH address in the descriptor wallet:

```bash
./build/bin/bitcoin-cli -rpcwallet=WALLET_NAME \
    getnewaddress "mining" legacy
```

Send BTCW to the returned address and wait for at least six confirmations. If
the wallet already contains BTCW on another address type, send those coins to
this legacy address.

Check the eligible output:

```bash
./build/bin/bitcoin-cli -rpcwallet=WALLET_NAME \
    listunspent 6 9999999 '["LEGACY_ADDRESS"]'
```

## Unlock an encrypted wallet

Unlock the wallet for a chosen number of seconds:

```bash
./build/bin/bitcoin-cli -rpcwallet=WALLET_NAME \
    walletpassphrase "PASSPHRASE" 3600
```

Do not place a real passphrase in a shared script or shell-history file.

## Mine continuously

Start the external GPU worker first. Then start the built-in continuous loop:

```bash
./build/bin/bitcoin-cli -rpcwallet=WALLET_NAME setstaking true 30
```

The timeout defaults to 30 seconds, so `setstaking true` and
`setstaking true 30` are equivalent. It is the maximum time the node waits for
a GPU result before preparing the next attempt. Keep the recommended 30-second
value unless you have a specific reason to change it. A much larger value can
leave the GPU working on an older attempt after the chain tip or candidate
block changes, resulting in duplicate or otherwise wasted work.

Check mining status and eligible stake weight:

```bash
./build/bin/bitcoin-cli -rpcwallet=WALLET_NAME getstakinginfo
```

Stop continuous mining:

```bash
./build/bin/bitcoin-cli -rpcwallet=WALLET_NAME setstaking false
```

The loop also stops when the wallet is unloaded or the application shuts down.
Mining does not automatically resume after an application restart.

## Mine from BitcoinPoW Qt

Start Qt with its RPC server enabled if command-line RPC access is needed:

```bash
./build/bin/bitcoin-qt -server
```

The same commands can be entered directly in **Window > Console**:

```text
setstaking true 30
getstakinginfo
setstaking false
```

An encrypted wallet must be unlocked before `setstaking true` is accepted.

## Attempt one block only

To perform one Stage 1/Stage 2 attempt without enabling the continuous loop:

```bash
./build/bin/bitcoin-cli -rpcwallet=WALLET_NAME generatestake 30
```

On success, `generatestake` returns the accepted block hash. Failure to find a
kernel or GPU result during one attempt is normal and does not indicate that
the node rejected a valid block.

## Troubleshooting

- **Wallet not found:** supply the correct wallet name with `-rpcwallet` and
  confirm it appears in `listwallets`.
- **Wallet is locked:** run `walletpassphrase` again.
- **Stake weight is zero:** confirm that the output is P2PK/P2PKH, spendable,
  unlocked, and has at least six confirmations.
- **No GPU result:** confirm the external worker is running as the same user and
  is using `/shared_mem`.
- **Node is still synchronizing:** wait until synchronization completes before
  mining.
