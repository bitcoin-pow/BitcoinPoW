// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>

#include <arith_uint256.h>
#include <chain.h>
#include <consensus/consensus.h>
#include <primitives/block.h>
#include <uint256.h>

#include <boost/multiprecision/cpp_int.hpp>

#include <limits>

// Historical consensus: retain the original rounding and burst override until
// the upgrade. Altering this calculation would change accepted pre-fork blocks.
static unsigned int LegacyLwma3CalculateNextWorkRequired(const CBlockIndex* pindexLast, const Consensus::Params& params)
{
    const int64_t T = params.nPowTargetSpacing;
    const int64_t N = 45; // For 600 nPoSTargetSpacing
    const int64_t k = N * (N + 1) * T / 2;
    const int64_t height = pindexLast->nHeight;

    const arith_uint256 powLimit = UintToArith256(params.powLimit);
    if (height < N) { return powLimit.GetCompact(); }

    arith_uint256 sumTarget, previousDiff, nextTarget;
    int64_t thisTimestamp, previousTimestamp;
    int64_t t = 0, j = 0, solvetimeSum = 0;

    const CBlockIndex* blockPreviousTimestamp = pindexLast->GetAncestor(height - N);
    previousTimestamp = blockPreviousTimestamp->GetBlockTime();

    // Loop through N most recent blocks.
    for (int64_t i = height - N + 1; i <= height; i++) {
        const CBlockIndex* block = pindexLast->GetAncestor(i);
        thisTimestamp = (block->GetBlockTime() > previousTimestamp) ? block->GetBlockTime() : previousTimestamp + 1;

        int64_t solvetime = std::min(6 * T, thisTimestamp - previousTimestamp);
        previousTimestamp = thisTimestamp;

        j++;
        t += solvetime * j; // Weighted solvetime sum.
        arith_uint256 target;
        target.SetCompact(block->nBits);
        sumTarget += target / (k * N);

        if (i > height - 3) { solvetimeSum += solvetime; }
        if (i == height) { previousDiff = target.SetCompact(block->nBits); }
    }

    nextTarget = t * sumTarget;

    if (nextTarget > (previousDiff * 150) / 100) { nextTarget = (previousDiff * 150) / 100; }
    if (nextTarget < (previousDiff * 67) / 100) { nextTarget = (previousDiff * 67) / 100; }
    if (solvetimeSum < (8 * T) / 10) { nextTarget = previousDiff * 100 / 106; }
    if (nextTarget > powLimit) { nextTarget = powLimit; }

    return nextTarget.GetCompact();
}

arith_uint256 CalculateASERTTarget(const arith_uint256& reference_target, int64_t target_spacing,
                                  int64_t time_diff, int64_t height_diff,
                                  const arith_uint256& pow_limit, int64_t half_life)
{
    assert(reference_target > 0 && reference_target <= pow_limit);
    assert(target_spacing > 0 && half_life > 0 && height_diff >= 0);
    using boost::multiprecision::int256_t;
    using boost::multiprecision::uint512_t;

    // ASERT integer formula and polynomial coefficients:
    // https://upgradespecs.bitcoincashnode.org/2020-11-15-asert/
    // Wide intermediates avoid overflow even for extreme timestamp/height inputs.
    const int256_t schedule_error = int256_t{time_diff} - int256_t{target_spacing} * (int256_t{height_diff} + 1);
    const int256_t exponent_wide = schedule_error * 65536 / half_life;
    if (exponent_wide <= -256 * 65536) return arith_uint256{1};
    if (exponent_wide >= 256 * 65536) return pow_limit;
    const int64_t exponent = static_cast<int64_t>(exponent_wide);

    // Explicit floor division: do not rely on right-shifting negative integers.
    int64_t shifts = exponent / 65536;
    int64_t remainder = exponent % 65536;
    if (remainder < 0) {
        --shifts;
        remainder += 65536;
    }
    const uint64_t fraction = remainder;
    const uint64_t factor = 65536 + ((195766423245049ULL * fraction +
        971821376ULL * fraction * fraction + 5127ULL * fraction * fraction * fraction +
        (1ULL << 47)) >> 48);

    const auto widen = [](const arith_uint256& value) {
        uint512_t wide{0};
        for (int i = 3; i >= 0; --i) {
            wide <<= 64;
            wide |= arith_uint256(value >> (64 * i)).GetLow64();
        }
        return wide;
    };
    uint512_t target = widen(reference_target) * factor;
    shifts -= 16; // factor includes sixteen fractional bits.
    if (shifts < 0) target >>= -shifts;
    else target <<= shifts;
    if (target == 0) return arith_uint256{1};
    if (target > widen(pow_limit)) return pow_limit;

    arith_uint256 result{0};
    for (int i = 0; i < 4; ++i) {
        const uint64_t word = static_cast<uint64_t>(target & std::numeric_limits<uint64_t>::max());
        result |= arith_uint256{word} << (64 * i);
        target >>= 64;
    }
    return result;
}

// find last block index up to pindex
const CBlockIndex* GetLastBlockIndex(const CBlockIndex* pindex, bool fProofOfStake)
{
    //CBlockIndex will be updated with information about the proof type later
    while (pindex && pindex->pprev && (pindex->IsProofOfStake() != fProofOfStake))
        pindex = pindex->pprev;
    return pindex;
}

inline arith_uint256 GetLimit(int nHeight, const Consensus::Params& params, bool fProofOfStake)
{
    if (fProofOfStake) {
        return UintToArith256(params.posLimit);
    } else {
        return UintToArith256(params.powLimit);
    }
}

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);
    if (params.fPowNoRetargeting) return pindexLast->nBits;

    if (pindexLast->nHeight >= NO_EXT_WORK_ACTIVATION_HEIGHT - 1) {
        // Resolve the anchor on this branch, never from the active chain or a
        // process-global cache. This also makes reorgs and reindex deterministic. 
        const int settled_anchor_height = NO_EXT_WORK_ACTIVATION_HEIGHT + ASERT_TRANSITION_BLOCKS - 1;
        const bool transition = pindexLast->nHeight < settled_anchor_height;
        const CBlockIndex* anchor = pindexLast->GetAncestor(
            transition ? NO_EXT_WORK_ACTIVATION_HEIGHT - 1 : settled_anchor_height);
        assert(anchor);
        arith_uint256 reference_target;
        reference_target.SetCompact(anchor->nBits);
        const arith_uint256 pow_limit = UintToArith256(params.powLimit);
        // Divide before comparing so the one-time easing cannot overflow.
        if (transition) {
            if (reference_target > pow_limit / ASERT_ACTIVATION_TARGET_MULTIPLIER) {
                reference_target = pow_limit;
            } else {
                reference_target *= ASERT_ACTIVATION_TARGET_MULTIPLIER;
            }
        }
        // Start the new schedule at the anchor itself. The activation target
        // is exactly the eased reference (subject to compact rounding/capping),
        // regardless of how long the final pre-fork block took to mine.
        // After 144 fork blocks, use the achieved target at the new anchor
        // without a multiplier. The handoff preserves that target exactly.
        return CalculateASERTTarget(reference_target, params.nPowTargetSpacing,
            pindexLast->GetBlockTime() - anchor->GetBlockTime() + params.nPowTargetSpacing,
            pindexLast->nHeight - anchor->nHeight, pow_limit, ASERT_HALF_LIFE).GetCompact();
    }

    if ((pindexLast->nHeight+1) > 10)
    {
        // LWMA3
        return LegacyLwma3CalculateNextWorkRequired(pindexLast, params);
    }
    
    unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();

    // Only change once per difficulty adjustment interval
    if ((pindexLast->nHeight+1) % params.DifficultyAdjustmentInterval() != 0)
    {
        if (params.fPowAllowMinDifficultyBlocks)
        {
            // Special difficulty rule for testnet:
            // If the new block's timestamp is more than 2* 10 minutes
            // then allow mining of a min-difficulty block.
            if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing*2)
                return nProofOfWorkLimit;
            else
            {
                // Return the last non-special-min-difficulty-rules-block
                const CBlockIndex* pindex = pindexLast;
                while (pindex->pprev && pindex->nHeight % params.DifficultyAdjustmentInterval() != 0 && pindex->nBits == nProofOfWorkLimit)
                    pindex = pindex->pprev;
                return pindex->nBits;
            }
        }
        return pindexLast->nBits;
    }

    // Go back by what we want to be 14 days worth of blocks
    int nHeightFirst = pindexLast->nHeight - (params.DifficultyAdjustmentInterval()-1);
    assert(nHeightFirst >= 0);
    const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
    assert(pindexFirst);

    return CalculateNextWorkRequired(pindexLast, pindexFirst->GetBlockTime(), params);
}

unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    if (nActualTimespan < params.nPowTargetTimespan/4)
        nActualTimespan = params.nPowTargetTimespan/4;
    if (nActualTimespan > params.nPowTargetTimespan*4)
        nActualTimespan = params.nPowTargetTimespan*4;

    // Retarget
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    arith_uint256 bnNew;
    bnNew.SetCompact(pindexLast->nBits);
    bnNew *= nActualTimespan;
    bnNew /= params.nPowTargetTimespan;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    return bnNew.GetCompact();
}

// Coarse target checks for header presync; exact difficulty needs branch history.
bool PermittedDifficultyTransition(const Consensus::Params& params, int64_t height, uint32_t old_nbits, uint32_t new_nbits)
{
    if (params.fPowNoRetargeting) return old_nbits == new_nbits;
    if (height < NO_EXT_WORK_ACTIVATION_HEIGHT) return true;

    const auto valid_target = [&](uint32_t bits) {
        bool negative{false}, overflow{false};
        arith_uint256 target;
        target.SetCompact(bits, &negative, &overflow);
        return !negative && !overflow && target != 0 && target <= UintToArith256(params.powLimit);
    };
    // Without timestamps and the branch's anchor, a pair of nBits values
    // cannot establish the exact ASERT transition. Contextual header validation does that.
    return valid_target(old_nbits) && valid_target(new_nbits);
}

bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit))
        return false;

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return false;

    return true;
}
