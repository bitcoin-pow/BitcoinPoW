// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_POW_H
#define BITCOIN_POW_H

#include <consensus/params.h>

#include <stdint.h>

class arith_uint256;
class CBlockHeader;
class CBlockIndex;
class uint256;

const CBlockIndex* GetLastBlockIndex(const CBlockIndex* pindex, bool fProofOfStake);
unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params&);
unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params&);

/** Integer ASERT target calculation, with truncation and saturation defined by the ASERT specification. */
arith_uint256 CalculateASERTTarget(const arith_uint256& reference_target, int64_t target_spacing,
                                  int64_t time_diff, int64_t height_diff,
                                  const arith_uint256& pow_limit, int64_t half_life);

/** Check whether a block hash satisfies the proof-of-work requirement specified by nBits */
bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params&);

/**
 * Return false if the proof-of-work requirement specified by new_nbits at a
 * given height is not possible, given the proof-of-work on the prior block as
 * specified by old_nbits.
 *
 * Before the upgrade, retain the historical permissive presync behavior.
 * After the upgrade, reject invalid targets. ASERT's exact target needs the
 * anchor and timestamps, so full contextual header validation checks it.
 * No-retargeting networks require the previous bits to be retained.
 */
bool PermittedDifficultyTransition(const Consensus::Params& params, int64_t height, uint32_t old_nbits, uint32_t new_nbits);

#endif // BITCOIN_POW_H
