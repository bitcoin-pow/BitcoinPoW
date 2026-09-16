// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_CONSENSUS_H
#define BITCOIN_CONSENSUS_CONSENSUS_H

#include <cstdlib>
#include <stdint.h>

/** The maximum allowed size for a serialized block, in bytes (only for buffer size limits) */
static const unsigned int MAX_BLOCK_SERIALIZED_SIZE = 4000000;
/** The maximum allowed weight for a block, see BIP 141 (network rule) */
static const unsigned int MAX_BLOCK_WEIGHT = 4000000;
/** The maximum allowed number of signature check operations in a block (network rule) */
static const int64_t MAX_BLOCK_SIGOPS_COST = 80000;
/** Historical coin maturity boundary, required when replaying transactions. */
static constexpr int HISTORICAL_COIN_MATURITY_HEIGHT = 23333;
/** Marker used by the current mining algorithm. Older markers remain in the wire decoder only. */
static constexpr uint32_t CURRENT_MINING_NONCE = 0xFEEDBEE2;
/** Activate canonical block signatures, ASERT difficulty, and miner-only coinstake payouts. */
static constexpr int NO_EXT_WORK_ACTIVATION_HEIGHT = 144444;
/** One-time target increase at activation, retained in the ASERT reference. */
static constexpr uint64_t ASERT_ACTIVATION_TARGET_MULTIPLIER = 1000;
/** Number of fork blocks using the eased historical reference. */
static constexpr int ASERT_TRANSITION_BLOCKS = 144;
/** ASERT response time: twelve hours ahead/behind schedule doubles/halves difficulty. */
static constexpr int64_t ASERT_HALF_LIFE = 12 * 60 * 60;
/** Coinbase transaction outputs can only be spent after this number of new blocks (network rule) */
constexpr int COINBASE_MATURITY()
{
    return 6;
}
constexpr int COINBASE_MATURITY( int n )
{
    if ( n < HISTORICAL_COIN_MATURITY_HEIGHT )
    {
        return 2;
    }
    return COINBASE_MATURITY();
}

static const int WITNESS_SCALE_FACTOR = 4;

static const size_t MIN_TRANSACTION_WEIGHT = WITNESS_SCALE_FACTOR * 60; // 60 is the lower bound for the size of a valid serialized CTransaction
static const size_t MIN_SERIALIZABLE_TRANSACTION_WEIGHT = WITNESS_SCALE_FACTOR * 10; // 10 is the lower bound for the size of a serialized CTransaction

/** Flags for nSequence and nLockTime locks */
/** Interpret sequence numbers as relative lock-time constraints. */
static constexpr unsigned int LOCKTIME_VERIFY_SEQUENCE = (1 << 0);
/** Use GetMedianTimePast() instead of nTime for end point timestamp. */
static constexpr unsigned int LOCKTIME_MEDIAN_TIME_PAST = (1 << 1);

#endif // BITCOIN_CONSENSUS_CONSENSUS_H
