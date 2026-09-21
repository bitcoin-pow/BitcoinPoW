// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>

#include <arith_uint256.h>
#include <chain.h>
#include <consensus/consensus.h>
#include <primitives/block.h>
#include <uint256.h>
#include <util/check.h>

#include <cstring>
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

namespace {
// Signed 256-bit helper so ASERT intermediates stay exact without Boost.
struct ASERTSigned256 {
    bool neg{false};
    arith_uint256 mag{};

    static ASERTSigned256 FromInt64(int64_t v)
    {
        ASERTSigned256 out;
        if (v < 0) {
            out.neg = true;
            out.mag = arith_uint256{static_cast<uint64_t>(-(v + 1)) + 1};
        } else {
            out.mag = arith_uint256{static_cast<uint64_t>(v)};
        }
        return out;
    }

    static int Compare(const ASERTSigned256& a, const ASERTSigned256& b)
    {
        if (a.mag == 0 && b.mag == 0) return 0;
        if (a.neg != b.neg) return a.neg ? -1 : 1;
        const int mag_cmp = a.mag.CompareTo(b.mag);
        return a.neg ? -mag_cmp : mag_cmp;
    }

    static ASERTSigned256 Add(ASERTSigned256 a, ASERTSigned256 b)
    {
        if (a.neg == b.neg) {
            a.mag += b.mag;
            return a;
        }
        if (a.mag >= b.mag) {
            a.mag -= b.mag;
            if (a.mag == 0) a.neg = false;
            return a;
        }
        b.mag -= a.mag;
        if (b.mag == 0) b.neg = false;
        return b;
    }

    static ASERTSigned256 Sub(const ASERTSigned256& a, ASERTSigned256 b)
    {
        if (b.mag != 0) b.neg = !b.neg;
        return Add(a, b);
    }

    static ASERTSigned256 Mul(ASERTSigned256 a, const ASERTSigned256& b)
    {
        a.neg = (a.neg != b.neg) && a.mag != 0 && b.mag != 0;
        a.mag *= b.mag;
        return a;
    }

    static ASERTSigned256 DivTowardZero(ASERTSigned256 a, const ASERTSigned256& b)
    {
        a.neg = (a.neg != b.neg) && a.mag != 0;
        a.mag /= b.mag;
        if (a.mag == 0) a.neg = false;
        return a;
    }

    int64_t ToInt64() const
    {
        const uint64_t low = mag.GetLow64();
        return neg ? -static_cast<int64_t>(low) : static_cast<int64_t>(low);
    }
};

// 512-bit unsigned so reference_target * factor can shift without wrapping 256 bits.
struct ASERTUint512 {
    uint64_t d[8]{};

    static void Mul64(uint64_t a, uint64_t b, uint64_t& lo, uint64_t& hi)
    {
        const uint64_t a_lo = static_cast<uint32_t>(a);
        const uint64_t a_hi = a >> 32;
        const uint64_t b_lo = static_cast<uint32_t>(b);
        const uint64_t b_hi = b >> 32;
        const uint64_t p0 = a_lo * b_lo;
        const uint64_t p1 = a_lo * b_hi;
        const uint64_t p2 = a_hi * b_lo;
        const uint64_t p3 = a_hi * b_hi;
        const uint64_t mid = (p0 >> 32) + static_cast<uint32_t>(p1) + static_cast<uint32_t>(p2);
        lo = (p0 & 0xffffffffull) | (mid << 32);
        hi = p3 + (p1 >> 32) + (p2 >> 32) + (mid >> 32);
    }

    static ASERTUint512 From256(const arith_uint256& value)
    {
        ASERTUint512 out;
        arith_uint256 tmp = value;
        for (int i = 0; i < 4; ++i) {
            out.d[i] = tmp.GetLow64();
            tmp >>= 64;
        }
        return out;
    }

    ASERTUint512& operator*=(uint64_t k)
    {
        uint64_t carry = 0;
        for (auto& limb : d) {
            uint64_t lo = 0;
            uint64_t hi = 0;
            Mul64(limb, k, lo, hi);
            const uint64_t summed = lo + carry;
            const uint64_t wrap = summed < lo ? 1 : 0;
            limb = summed;
            carry = hi + wrap;
        }
        return *this;
    }

    void ShiftLeft(int n)
    {
        if (n <= 0) return;
        if (n >= 512) {
            std::memset(d, 0, sizeof(d));
            return;
        }
        const int limbs = n / 64;
        const int bits = n % 64;
        uint64_t out[8]{};
        for (int i = 7; i >= 0; --i) {
            const int src = i - limbs;
            if (src < 0) continue;
            out[i] = d[src] << bits;
            if (bits && src > 0) out[i] |= d[src - 1] >> (64 - bits);
        }
        std::memcpy(d, out, sizeof(d));
    }

    void ShiftRight(int n)
    {
        if (n <= 0) return;
        if (n >= 512) {
            std::memset(d, 0, sizeof(d));
            return;
        }
        const int limbs = n / 64;
        const int bits = n % 64;
        uint64_t out[8]{};
        for (int i = 0; i < 8; ++i) {
            const int src = i + limbs;
            if (src >= 8) continue;
            out[i] = d[src] >> bits;
            if (bits && src + 1 < 8) out[i] |= d[src + 1] << (64 - bits);
        }
        std::memcpy(d, out, sizeof(d));
    }

    bool IsZero() const
    {
        for (const auto limb : d) {
            if (limb != 0) return false;
        }
        return true;
    }

    int Compare(const ASERTUint512& other) const
    {
        for (int i = 7; i >= 0; --i) {
            if (d[i] < other.d[i]) return -1;
            if (d[i] > other.d[i]) return 1;
        }
        return 0;
    }

    arith_uint256 Low256() const
    {
        arith_uint256 out{0};
        for (int i = 0; i < 4; ++i) {
            out |= arith_uint256{d[i]} << (64 * i);
        }
        return out;
    }
};
} // namespace

arith_uint256 CalculateASERTTarget(const arith_uint256& reference_target, int64_t target_spacing,
                                  int64_t time_diff, int64_t height_diff,
                                  const arith_uint256& pow_limit, int64_t half_life)
{
    assert(reference_target > 0 && reference_target <= pow_limit);
    assert(target_spacing > 0 && half_life > 0 && height_diff >= 0);

    // ASERT integer formula and polynomial coefficients:
    // https://upgradespecs.bitcoincashnode.org/2020-11-15-asert/
    // Wide intermediates avoid overflow even for extreme timestamp/height inputs.
    const ASERTSigned256 schedule_error = ASERTSigned256::Sub(
        ASERTSigned256::FromInt64(time_diff),
        ASERTSigned256::Mul(ASERTSigned256::FromInt64(target_spacing),
                            ASERTSigned256::Add(ASERTSigned256::FromInt64(height_diff), ASERTSigned256::FromInt64(1))));
    const ASERTSigned256 exponent_wide = ASERTSigned256::DivTowardZero(
        ASERTSigned256::Mul(schedule_error, ASERTSigned256::FromInt64(65536)),
        ASERTSigned256::FromInt64(half_life));
    if (ASERTSigned256::Compare(exponent_wide, ASERTSigned256::FromInt64(-256 * 65536)) <= 0) return arith_uint256{1};
    if (ASERTSigned256::Compare(exponent_wide, ASERTSigned256::FromInt64(256 * 65536)) >= 0) return pow_limit;
    const int64_t exponent = exponent_wide.ToInt64();

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

    ASERTUint512 target = ASERTUint512::From256(reference_target);
    target *= factor;
    shifts -= 16; // factor includes sixteen fractional bits.
    if (shifts < 0) target.ShiftRight(static_cast<int>(-shifts));
    else target.ShiftLeft(static_cast<int>(shifts));
    if (target.IsZero()) return arith_uint256{1};
    if (target.Compare(ASERTUint512::From256(pow_limit)) > 0) return pow_limit;
    return target.Low256();
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
            // then it MUST be a min-difficulty block.
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

    // Special difficulty rule for Testnet4
    if (params.enforce_BIP94) {
        // Here we use the first block of the difficulty period. This way
        // the real difficulty is always preserved in the first block as
        // it is not allowed to use the min-difficulty exception.
        int nHeightFirst = pindexLast->nHeight - (params.DifficultyAdjustmentInterval()-1);
        const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
        bnNew.SetCompact(pindexFirst->nBits);
    } else {
        bnNew.SetCompact(pindexLast->nBits);
    }

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

// Bypasses the actual proof of work check during fuzz testing with a simplified validation checking whether
// the most significant bit of the last byte of the hash is set.
bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    if (EnableFuzzDeterminism()) return (hash.data()[31] & 0x80) == 0;
    return CheckProofOfWorkImpl(hash, nBits, params);
}

std::optional<arith_uint256> DeriveTarget(unsigned int nBits, const uint256 pow_limit)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(pow_limit))
        return {};

    return bnTarget;
}

bool CheckProofOfWorkImpl(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    auto bnTarget{DeriveTarget(nBits, params.powLimit)};
    if (!bnTarget) return false;

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return false;

    return true;
}
