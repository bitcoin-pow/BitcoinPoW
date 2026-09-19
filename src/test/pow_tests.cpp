// Copyright (c) 2015-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <consensus/consensus.h>
#include <limits>
#include <cmath>
#include <chainparams.h>
#include <pow.h>
#include <test/util/random.h>
#include <test/util/setup_common.h>
#include <util/chaintype.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(pow_tests, BasicTestingSetup)

/* Test calculation of next difficulty target with no constraints applying */
BOOST_AUTO_TEST_CASE(get_next_work)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    int64_t nLastRetargetTime = 1261130161; // Block #30240
    CBlockIndex pindexLast;
    pindexLast.nHeight = 32255;
    pindexLast.nTime = 1262152739;  // Block #32255
    pindexLast.nBits = 0x1d00ffff;

    // Here (and below): expected_nbits is calculated in
    // CalculateNextWorkRequired(); redoing the calculation here would be just
    // reimplementing the same code that is written in pow.cpp. Rather than
    // copy that code, we just hardcode the expected result.
    unsigned int expected_nbits = 0x1d00d86aU;
    BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, chainParams->GetConsensus()), expected_nbits);
}

/* Test the constraint on the upper bound for next work */
BOOST_AUTO_TEST_CASE(get_next_work_pow_limit)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    int64_t nLastRetargetTime = 1231006505; // Block #0
    CBlockIndex pindexLast;
    pindexLast.nHeight = 2015;
    pindexLast.nTime = 1233061996;  // Block #2015
    pindexLast.nBits = 0x1d00ffff;
    unsigned int expected_nbits = 0x1d00ffffU;
    BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, chainParams->GetConsensus()), expected_nbits);
}

/* Test the constraint on the lower bound for actual time taken */
BOOST_AUTO_TEST_CASE(get_next_work_lower_limit_actual)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    int64_t nLastRetargetTime = 1279008237; // Block #66528
    CBlockIndex pindexLast;
    pindexLast.nHeight = 68543;
    pindexLast.nTime = 1279297671;  // Block #68543
    pindexLast.nBits = 0x1c05a3f4;
    unsigned int expected_nbits = 0x1c0168fdU;
    BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, chainParams->GetConsensus()), expected_nbits);
}

/* Test the constraint on the upper bound for actual time taken */
BOOST_AUTO_TEST_CASE(get_next_work_upper_limit_actual)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    int64_t nLastRetargetTime = 1263163443; // NOTE: Not an actual block time
    CBlockIndex pindexLast;
    pindexLast.nHeight = 46367;
    pindexLast.nTime = 1269211443;  // Block #46367
    pindexLast.nBits = 0x1c387f6f;
    unsigned int expected_nbits = 0x1d00e1fdU;
    BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, chainParams->GetConsensus()), expected_nbits);
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_negative_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits;
    nBits = UintToArith256(consensus.powLimit).GetCompact(true);
    hash.SetHex("0x1");
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_overflow_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits{~0x00800000U};
    hash.SetHex("0x1");
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_too_easy_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits;
    arith_uint256 nBits_arith = UintToArith256(consensus.powLimit);
    nBits_arith *= 2;
    nBits = nBits_arith.GetCompact();
    hash.SetHex("0x1");
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_biger_hash_than_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits;
    arith_uint256 hash_arith = UintToArith256(consensus.powLimit);
    nBits = hash_arith.GetCompact();
    hash_arith *= 2; // hash > nBits
    hash = ArithToUint256(hash_arith);
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_zero_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits;
    arith_uint256 hash_arith{0};
    nBits = hash_arith.GetCompact();
    hash = ArithToUint256(hash_arith);
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(GetBlockProofEquivalentTime_test)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    std::vector<CBlockIndex> blocks(10000);
    for (int i = 0; i < 10000; i++) {
        blocks[i].pprev = i ? &blocks[i - 1] : nullptr;
        blocks[i].nHeight = i;
        blocks[i].nTime = 1269211443 + i * chainParams->GetConsensus().nPowTargetSpacing;
        blocks[i].nBits = 0x207fffff; /* target 0x7fffff000... */
        blocks[i].nChainWork = i ? blocks[i - 1].nChainWork + GetBlockProof(blocks[i - 1]) : arith_uint256(0);
    }

    for (int j = 0; j < 1000; j++) {
        CBlockIndex *p1 = &blocks[InsecureRandRange(10000)];
        CBlockIndex *p2 = &blocks[InsecureRandRange(10000)];
        CBlockIndex *p3 = &blocks[InsecureRandRange(10000)];

        int64_t tdiff = GetBlockProofEquivalentTime(*p1, *p2, *p3, chainParams->GetConsensus());
        BOOST_CHECK_EQUAL(tdiff, p1->GetBlockTime() - p2->GetBlockTime());
    }
}

void sanity_check_chainparams(const ArgsManager& args, ChainType chain_type)
{
    const auto chainParams = CreateChainParams(args, chain_type);
    const auto consensus = chainParams->GetConsensus();

    // hash genesis is correct
    BOOST_CHECK_EQUAL(consensus.hashGenesisBlock, chainParams->GenesisBlock().GetHash());

    // target timespan is an even multiple of spacing
    BOOST_CHECK_EQUAL(consensus.nPowTargetTimespan % consensus.nPowTargetSpacing, 0);

    // genesis nBits is positive, doesn't overflow and is lower than powLimit
    arith_uint256 pow_compact;
    bool neg, over;
    pow_compact.SetCompact(chainParams->GenesisBlock().nBits, &neg, &over);
    BOOST_CHECK(!neg && pow_compact != 0);
    BOOST_CHECK(!over);
    BOOST_CHECK(UintToArith256(consensus.powLimit) >= pow_compact);

    // check max target * 4*nPowTargetTimespan doesn't overflow -- see pow.cpp:CalculateNextWorkRequired()
    if (!consensus.fPowNoRetargeting) {
        arith_uint256 targ_max("0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
        targ_max /= consensus.nPowTargetTimespan*4;
        BOOST_CHECK(UintToArith256(consensus.powLimit) < targ_max);
    }
}

BOOST_AUTO_TEST_CASE(ChainParams_MAIN_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::MAIN);
}

BOOST_AUTO_TEST_CASE(ChainParams_REGTEST_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::REGTEST);
}

BOOST_AUTO_TEST_CASE(ChainParams_TESTNET_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::TESTNET);
}

BOOST_AUTO_TEST_CASE(ChainParams_SIGNET_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::SIGNET);
}


BOOST_AUTO_TEST_CASE(asert_target_arithmetic)
{
    const arith_uint256 limit = ~arith_uint256{0};
    const arith_uint256 reference = arith_uint256{1} << 200;
    const auto target = [&](int64_t time, int64_t height = 0) {
        return CalculateASERTTarget(reference, 600, time, height, limit, ASERT_HALF_LIFE);
    };
    BOOST_CHECK(target(600) == reference);
    BOOST_CHECK(target(600 * 1001, 1000) == reference);
    BOOST_CHECK(target(600 + ASERT_HALF_LIFE) == reference * 2);
    BOOST_CHECK(target(600 - ASERT_HALF_LIFE) == reference / 2);
    BOOST_CHECK(target(std::numeric_limits<int64_t>::max()) == limit);
    BOOST_CHECK(target(std::numeric_limits<int64_t>::min()) == 1);
    BOOST_CHECK(target(0, std::numeric_limits<int64_t>::max()) == 1);
    BOOST_CHECK(CalculateASERTTarget(limit, 600, 600, 0, limit, ASERT_HALF_LIFE) == limit);
    BOOST_CHECK(CalculateASERTTarget(limit, 600, 601, 0, limit, ASERT_HALF_LIFE) == limit);
    BOOST_CHECK(CalculateASERTTarget(arith_uint256{1}, 600, -100000, 0, limit, ASERT_HALF_LIFE) == 1);
    // Fractional exponents, including negative floor division; compare against
    // the exponential with the published polynomial's 0.013% error bound.
    for (int error = -86400; error <= 86400; error += 137) {
        const double actual = target(600 + error).getdouble() / reference.getdouble();
        const double expected = std::exp2(double(error) / ASERT_HALF_LIFE);
        BOOST_CHECK_SMALL(actual / expected - 1.0, 0.00015);
        BOOST_CHECK(target(601 + error) >= target(600 + error));
    }
}

BOOST_AUTO_TEST_CASE(asert_activation_and_branches)
{
    auto params = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    std::vector<CBlockIndex> blocks(48);
    for (size_t i = 0; i < blocks.size(); ++i) {
        blocks[i].nHeight = NO_EXT_WORK_ACTIVATION_HEIGHT - 47 + i;
        blocks[i].nTime = 1700000000 + 600 * i;
        blocks[i].nBits = 0x1b010000;
        blocks[i].pprev = i ? &blocks[i-1] : nullptr;
    }
    CBlockHeader candidate;
    // Last pre-fork target preserves LWMA's historical truncation.
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&blocks[45], &candidate, params), 0x1b00ffffU);
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&blocks[46], &candidate, params), 0x1c271000U);
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&blocks[47], &candidate, params), 0x1c271000U);
    blocks[47].nTime += ASERT_HALF_LIFE;
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&blocks[47], &candidate, params), 0x1c4e2000U);
    candidate.nTime = std::numeric_limits<uint32_t>::max();
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&blocks[47], &candidate, params), 0x1c4e2000U);
    CBlockIndex alternative_anchor;
    alternative_anchor.nHeight = blocks[46].nHeight;
    alternative_anchor.nTime = blocks[46].nTime;
    alternative_anchor.pprev = blocks[46].pprev;
    alternative_anchor.nBits = 0x1b020000;
    CBlockIndex alternative_tip;
    alternative_tip.nHeight = blocks[47].nHeight;
    alternative_tip.nTime = blocks[47].nTime;
    alternative_tip.pprev = &alternative_anchor;
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&alternative_tip, &candidate, params), 0x1d009c40U);
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&blocks[47], &candidate, params), 0x1c4e2000U);
    // The reset must not inherit the solve time of the last legacy block.
    blocks[45].nTime -= ASERT_HALF_LIFE;
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&blocks[46], &candidate, params), 0x1c271000U);
    // An already-easy reference saturates at the network limit.
    blocks[46].nBits = UintToArith256(params.powLimit).GetCompact();
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&blocks[46], &candidate, params), blocks[46].nBits);
    // Also cover a near-256-bit limit, where naive multiplication wraps.
    params.powLimit = ArithToUint256(~arith_uint256{0});
    blocks[46].nBits = 0x207fffff;
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&blocks[46], &candidate, params), UintToArith256(params.powLimit).GetCompact());
    params.fPowNoRetargeting = true;
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&blocks[45], &candidate, params), blocks[45].nBits);
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&blocks[47], &candidate, params), blocks[47].nBits);
}

BOOST_AUTO_TEST_CASE(asert_transition_reference_expires)
{
    const auto params = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    CBlockHeader candidate;
    std::vector<CBlockIndex> blocks(ASERT_TRANSITION_BLOCKS + 3);
    blocks[0].nHeight = NO_EXT_WORK_ACTIVATION_HEIGHT - 1;
    blocks[0].nTime = 1700000000;
    blocks[0].nBits = 0x1b010000;
    for (size_t i = 1; i < blocks.size(); ++i) {
        blocks[i].pprev = &blocks[i - 1];
        blocks[i].nHeight = blocks[0].nHeight + i;
        // Faster blocks ensure the achieved target differs from the reset.
        blocks[i].nTime = blocks[i - 1].nTime + 300;
        blocks[i].nBits = GetNextWorkRequired(&blocks[i - 1], &candidate, params);
    }
    const int anchor = ASERT_TRANSITION_BLOCKS;
    BOOST_CHECK_EQUAL(blocks[1].nBits, 0x1c271000U);
    BOOST_CHECK(blocks[anchor].nBits != blocks[1].nBits);
    BOOST_CHECK_EQUAL(blocks[anchor + 1].nHeight, 144588);
    BOOST_CHECK_EQUAL(blocks[anchor + 1].nBits, blocks[anchor].nBits);
    const auto settled = GetNextWorkRequired(&blocks[anchor + 1], &candidate, params);
    BOOST_CHECK(settled < blocks[anchor].nBits);
    // Once settled, even changing the old reference has no effect.
    blocks[0].nBits = 0x1b020000;
    blocks[0].nTime -= ASERT_HALF_LIFE;
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&blocks[anchor + 1], &candidate, params), settled);
    // Resolve the new anchor on each branch, without stale cached state.
    CBlockIndex alternative;
    alternative.pprev = blocks[anchor].pprev;
    alternative.nHeight = blocks[anchor].nHeight;
    alternative.nTime = blocks[anchor].nTime;
    alternative.nBits = 0x1c010000;
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&alternative, &candidate, params), alternative.nBits);
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&blocks[anchor], &candidate, params), blocks[anchor].nBits);
    blocks[anchor + 1].nTime = blocks[anchor].nTime + 600 + ASERT_HALF_LIFE;
    arith_uint256 achieved;
    achieved.SetCompact(blocks[anchor].nBits);
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&blocks[anchor + 1], &candidate, params), arith_uint256(achieved * 2).GetCompact());
}

// Deterministic expected solve times model hash-rate steps without mining.
BOOST_AUTO_TEST_CASE(asert_hashrate_response)
{
    const arith_uint256 reference = arith_uint256{1} << 200;
    const arith_uint256 limit = ~arith_uint256{0};
    for (double hashrate : {0.25, 4.0}) {
        arith_uint256 target = reference;
        int64_t elapsed = 0;
        for (int height = 0; height < 2000; ++height) {
            const int64_t solve_time = std::llround(600.0 * reference.getdouble() / target.getdouble() / hashrate);
            elapsed += solve_time;
            const arith_uint256 next = CalculateASERTTarget(reference, 600, elapsed, height, limit, ASERT_HALF_LIFE);
            if (hashrate > 1) BOOST_CHECK(next <= target);
            else BOOST_CHECK(next >= target);
            target = next;
        }
        BOOST_CHECK_SMALL(target.getdouble() / reference.getdouble() * hashrate - 1.0, 0.002);
    }
}

BOOST_AUTO_TEST_CASE(difficulty_transition_policy)
{
    auto params = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    // Historical header presync defers exact LWMA checks to contextual validation.
    BOOST_CHECK(PermittedDifficultyTransition(params, NO_EXT_WORK_ACTIVATION_HEIGHT - 1, 0x1c010000, 0));
    BOOST_CHECK(PermittedDifficultyTransition(params, NO_EXT_WORK_ACTIVATION_HEIGHT, 0x1c010000, 0x1c020000));
    for (uint32_t invalid : {0U, 0x1c810000U, 0x23010000U, 0x1e010000U}) {
        BOOST_CHECK(!PermittedDifficultyTransition(params, NO_EXT_WORK_ACTIVATION_HEIGHT, 0x1c010000, invalid));
        BOOST_CHECK(!PermittedDifficultyTransition(params, NO_EXT_WORK_ACTIVATION_HEIGHT, invalid, 0x1c010000));
    }
    params.fPowNoRetargeting = true;
    BOOST_CHECK(PermittedDifficultyTransition(params, 100, 0x1c010000, 0x1c010000));
    BOOST_CHECK(!PermittedDifficultyTransition(params, 100, 0x1c010000, 0x1c020000));
}

BOOST_AUTO_TEST_SUITE_END()
