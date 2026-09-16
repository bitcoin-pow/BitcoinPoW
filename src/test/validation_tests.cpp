// Copyright (c) 2014-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <consensus/amount.h>
#include <consensus/consensus.h>
#include <key.h>
#include <util/strencodings.h>
#include <util/signalinterrupt.h>
#include <consensus/merkle.h>
#include <core_io.h>
#include <hash.h>
#include <net.h>
#include <signet.h>
#include <uint256.h>
#include <util/chaintype.h>
#include <validation.h>

#include <array>
#include <string>

#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include <iterator>
#include <set>

BOOST_FIXTURE_TEST_SUITE(validation_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(historical_checkpoint_anchor)
{
    // A small synthetic header chain exercises the same ancestry logic as
    // mainnet, including the checkpoint height itself and its descendants.
    std::array<CBlockHeader, 5> headers;
    std::array<uint256, 5> hashes;
    std::array<CBlockIndex, 5> indices;
    for (size_t i = 0; i < headers.size(); ++i) {
        headers[i].nNonce = i;
        if (i) headers[i].hashPrevBlock = hashes[i - 1];
        hashes[i] = headers[i].GetHash();
        indices[i].phashBlock = &hashes[i];
        indices[i].nHeight = i;
        indices[i].pprev = i ? &indices[i - 1] : nullptr;
        indices[i].BuildSkip();
    }
    Consensus::Params params{};
    params.historical_checkpoint = Consensus::HistoricalCheckpoint{3, hashes[3]};
    for (size_t i = 0; i < headers.size(); ++i) {
        BlockValidationState state;
        BOOST_CHECK(CheckHistoricalCheckpoint(headers[i], indices[i].pprev, params, state));
        BOOST_CHECK(!IsCheckpointAnchored(indices[i], nullptr, params));
        BOOST_CHECK(IsCheckpointAnchored(indices[i], &indices[3], params));
    }

    CBlockHeader wrong_header = headers[3];
    ++wrong_header.nNonce;
    BlockValidationState wrong_state;
    BOOST_CHECK(!CheckHistoricalCheckpoint(wrong_header, &indices[2], params, wrong_state));
    BOOST_CHECK(wrong_state.GetResult() == BlockValidationResult::BLOCK_CHECKPOINT);
    const uint256 wrong_hash = wrong_header.GetHash();
    CBlockIndex wrong_index;
    wrong_index.nHeight = 3;
    wrong_index.pprev = &indices[2];
    wrong_index.phashBlock = &wrong_hash;
    BOOST_CHECK(!IsCheckpointAnchored(indices[2], &wrong_index, params));
    BOOST_CHECK(!IsCheckpointAnchored(wrong_index, &indices[3], params));
    BlockValidationState descendant_state;
    BOOST_CHECK(!CheckHistoricalCheckpoint(headers[4], &wrong_index, params, descendant_state));

    // Height alone is insufficient, including for blocks below the checkpoint.
    CBlockIndex side_branch;
    side_branch.nHeight = 2;
    side_branch.pprev = &indices[1];
    side_branch.phashBlock = &wrong_hash;
    BOOST_CHECK(!IsCheckpointAnchored(side_branch, &indices[3], params));
    wrong_index.pprev = &side_branch;
    wrong_index.nHeight = 3;
    wrong_index.phashBlock = &wrong_hash;
    wrong_index.pskip = nullptr;
    BOOST_CHECK(!IsCheckpointAnchored(wrong_index, &indices[3], params));

    indices[3].nStatus |= BLOCK_FAILED_VALID;
    BOOST_CHECK(!IsCheckpointAnchored(indices[2], &indices[3], params));
    params.historical_checkpoint.reset();
    BOOST_CHECK(IsCheckpointAnchored(indices[2], nullptr, params));
}

BOOST_AUTO_TEST_CASE(historical_checkpoint_staging)
{
    const auto base_params = CreateChainParams(*m_node.args, ChainType::REGTEST);
    CBlock first = base_params->GenesisBlock();
    first.hashPrevBlock = first.GetHash();
    first.nVersion = 4;
    first.nBits = UintToArith256(base_params->GetConsensus().powLimit).GetCompact();
    ++first.nTime;
    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].scriptSig = CScript() << 1 << OP_0;
    coinbase.vout.emplace_back(1, CScript() << OP_TRUE);
    first.vtx = {MakeTransactionRef(coinbase)};
    first.hashMerkleRoot = BlockMerkleRoot(first);
    while (!CheckProofOfWork(first.GetHash(), first.nBits, base_params->GetConsensus())) ++first.nNonce;
    CBlockHeader checkpoint = first;
    checkpoint.hashPrevBlock = first.GetHash();
    ++checkpoint.nTime;
    struct CheckpointParams : CChainParams {
        CheckpointParams(const CChainParams& base, const uint256& hash) : CChainParams(base)
        {
            consensus.historical_checkpoint = Consensus::HistoricalCheckpoint{2, hash};
            consensus.nLastPOWBlock = 10;
        }
    } params(*base_params, checkpoint.GetHash());
    util::SignalInterrupt interrupt;
    auto& notifications = m_node.chainman->GetNotifications();
    ChainstateManager manager(interrupt,
        {.chainparams = params, .datadir = m_path_root / "checkpoint-test",
         .adjusted_time_callback = [] { return NodeClock::now(); },
         .checkpoints_enabled = false, .notifications = notifications},
        {.chainparams = params, .blocks_dir = m_path_root / "checkpoint-test" / "blocks",
         .notifications = notifications});
    LOCK(cs_main);
    CBlockIndex* best = nullptr;
    auto* genesis = manager.m_blockman.AddToBlockIndex(params.GenesisBlock(), best);
    auto* staged = manager.m_blockman.AddToBlockIndex(first, best);
    BOOST_CHECK(manager.CanActivateCheckpointChain(*genesis));
    BOOST_CHECK(!manager.CanActivateCheckpointChain(*staged));
    BOOST_CHECK(!manager.IsTrustedHistory(*staged));

    auto& chainstate = manager.InitializeChainstate(nullptr);
    chainstate.m_chain.SetTip(*genesis);
    CCoinsView backing;
    CCoinsViewCache view(&backing);
    view.SetBestBlock(genesis->GetBlockHash());
    const COutPoint created(first.vtx[0]->GetHash(), 0);
    BlockValidationState unanchored;
    BOOST_CHECK(!chainstate.ConnectBlock(first, unanchored, staged, view, true));
    BOOST_CHECK_EQUAL(unanchored.GetRejectReason(), "unanchored-history");
    BOOST_CHECK(!view.HaveCoin(created));

    // This models headers-first sync and sequential -reindex imports. Once
    // the anchor header arrives, already-staged ancestors become eligible.
    auto* anchor = manager.m_blockman.AddToBlockIndex(checkpoint, best);
    BOOST_CHECK(manager.CanActivateCheckpointChain(*staged));
    BOOST_CHECK(manager.IsTrustedHistory(*staged));
    BOOST_CHECK(manager.IsTrustedHistory(*anchor));
    BlockValidationState connected;
    BOOST_REQUIRE_MESSAGE(chainstate.ConnectBlock(first, connected, staged, view, true), connected.ToString());
    BOOST_CHECK(view.HaveCoin(created));
    BOOST_CHECK_EQUAL(view.AccessCoin(created).out.nValue, 1);
    CBlockHeader next = checkpoint;
    next.hashPrevBlock = checkpoint.GetHash();
    ++next.nTime;
    auto* descendant = manager.m_blockman.AddToBlockIndex(next, best);
    BOOST_CHECK(manager.CanActivateCheckpointChain(*descendant));
    BOOST_CHECK(!manager.IsTrustedHistory(*descendant));

    CBlockHeader other = first;
    ++other.nNonce;
    auto* side = manager.m_blockman.AddToBlockIndex(other, best);
    BOOST_CHECK(!manager.CanActivateCheckpointChain(*side));
    BOOST_CHECK(!manager.IsTrustedHistory(*side));
}

BOOST_AUTO_TEST_CASE(mainnet_historical_checkpoint)
{
    const auto params = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& anchor = params->GetConsensus().historical_checkpoint;
    BOOST_REQUIRE(anchor);
    BOOST_CHECK_EQUAL(anchor->height, 141410);
    BOOST_CHECK_EQUAL(anchor->hash.GetHex(), "05d553c0600bdeff22592f1331c8bc9fd534c35cd75a892c32055dea914cd00e");
    BOOST_CHECK(params->Checkpoints().mapCheckpoints.at(anchor->height) == anchor->hash);
}

BOOST_AUTO_TEST_CASE(skipped_block_signature_is_not_cached)
{
    const auto params = CreateChainParams(*m_node.args, ChainType::MAIN);
    CBlock block = params->GenesisBlock();
    block.fChecked = false;
    // PoW header hashes do not include the block signature. All other checks
    // still pass, but a subsequent full check must reject a nonempty signature.
    block.vchBlockSig = {1};
    BlockValidationState staged_state;
    BOOST_REQUIRE(CheckBlock(block, staged_state, params->GetConsensus(), true, true, false));
    BOOST_CHECK(!block.fChecked);
    BlockValidationState full_state;
    BOOST_CHECK(!CheckBlock(block, full_state, params->GetConsensus()));
    BOOST_CHECK_EQUAL(full_state.GetRejectReason(), "bad-blk-signature");
}

// Audit reproducer, not a desired security property: fixed ECDSA signing
// nonces remain valid under the fork rules. Never use these public test secrets.
BOOST_AUTO_TEST_CASE(mining_fixed_signing_nonce_audit)
{
    using boost::multiprecision::cpp_int;
    const cpp_int order("0xfffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141");
    const cpp_int r("0x79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798");
    const auto pubbytes = ParseHex("0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798");
    const CPubKey pubkey(pubbytes);
    CBlock block;
    block.nNonce = CURRENT_MINING_NONCE;
    block.nTime = 1700000000;
    const arith_uint256 base = UintToArith256(block.GetHashWithoutSign());
    const auto der_integer = [](const cpp_int& value) {
        std::vector<unsigned char> bytes;
        export_bits(value, std::back_inserter(bytes), 8, true);
        if (bytes.front() & 0x80) bytes.insert(bytes.begin(), 0);
        std::vector<unsigned char> encoded{0x02, static_cast<unsigned char>(bytes.size())};
        encoded.insert(encoded.end(), bytes.begin(), bytes.end());
        return encoded;
    };
    const auto encoded_r = der_integer(r);
    unsigned accepted = 0;
    std::set<uint256> proof_hashes;
    for (uint64_t nonce = 0; nonce < 256; ++nonce) {
        const uint256 message = ArithToUint256(base + arith_uint256(nonce));
        // The verifier interprets the raw message bytes as a big-endian scalar.
        const cpp_int z("0x" + HexStr(message));
        // ECDSA s = k^-1 * (z + r*d) mod n. Here d=k=1, so no
        // signing call, EC multiplication, or inversion is needed per trial.
        cpp_int scalar_s = (z + r) % order;
        if (scalar_s == 0) continue;
        if (scalar_s > order / 2) scalar_s = order - scalar_s;
        const auto encoded_s = der_integer(scalar_s);
        std::vector<unsigned char> der{0x30, static_cast<unsigned char>(encoded_r.size() + encoded_s.size())};
        der.insert(der.end(), encoded_r.begin(), encoded_r.end());
        der.insert(der.end(), encoded_s.begin(), encoded_s.end());
        BOOST_REQUIRE(pubkey.Verify(message, der));
        block.vchBlockSig = der;
        for (int shift = 56; shift >= 0; shift -= 8) block.vchBlockSig.push_back(nonce >> shift);
        if (!CheckBlockSignatureEncoding(block, NO_EXT_WORK_ACTIVATION_HEIGHT)) continue;
        ++accepted;
        if (der.size() == 70) {
            // Trailing garbage must fail DER even when total length stays 79.
            block.vchBlockSig.insert(block.vchBlockSig.begin() + der.size(), 0);
            BOOST_CHECK_EQUAL(block.vchBlockSig.size(), 79U);
            BOOST_CHECK(!CheckBlockSignatureEncoding(block, NO_EXT_WORK_ACTIVATION_HEIGHT));
        }
        CDataStream stream(SER_GETHASH, 0);
        stream << nonce << der;
        proof_hashes.insert(Hash(stream));
        // The accepted signature also recovers the committed public key.
        bool recovered_match = false;
        for (int recid = 0; recid < 4; ++recid) {
            CPubKey recovered;
            if (recovered.RecoverLaxDER(message, der, recid, true) && recovered == pubkey) recovered_match = true;
        }
        BOOST_CHECK(recovered_match);
        // Literal signature reuse with a changed external mining nonce fails.
        BOOST_CHECK(!pubkey.Verify(ArithToUint256(base + arith_uint256(nonce + 1)), der));
    }
    BOOST_TEST_MESSAGE("Fixed-signing-nonce candidates accepted: " << accepted << "/256");
    BOOST_CHECK_GT(accepted, 240U);
    BOOST_CHECK_EQUAL(proof_hashes.size(), accepted);
}

BOOST_AUTO_TEST_CASE(block_signature_size_activation)
{
    // Structurally valid low-S DER values; actual cryptographic verification is
    // separate. Exercise both allowed lengths and their adjacent boundaries.
    for (size_t r_size : {size_t{31}, size_t{32}, size_t{33}}) {
        for (size_t s_size : {size_t{31}, size_t{32}}) {
            std::vector<unsigned char> der{0x30, static_cast<unsigned char>(4 + r_size + s_size),
                                          0x02, static_cast<unsigned char>(r_size)};
            std::vector<unsigned char> r(r_size, 1);
            if (r_size == 33) { r[0] = 0; r[1] = 0x80; }
            der.insert(der.end(), r.begin(), r.end());
            der.push_back(0x02);
            der.push_back(s_size);
            der.insert(der.end(), s_size, 1);
            CBlock block;
            block.nNonce = CURRENT_MINING_NONCE;
            block.vchBlockSig = der;
            block.vchBlockSig.resize(der.size() + 8, 0);
            BOOST_CHECK(CheckBlockSignatureEncoding(block, NO_EXT_WORK_ACTIVATION_HEIGHT - 1));
            const bool allowed = block.vchBlockSig.size() == 78 || block.vchBlockSig.size() == 79;
            BOOST_CHECK_EQUAL(CheckBlockSignatureEncoding(block, NO_EXT_WORK_ACTIVATION_HEIGHT), allowed);
            BOOST_CHECK_EQUAL(CheckBlockSignatureEncoding(block, NO_EXT_WORK_ACTIVATION_HEIGHT + 1), allowed);
            block.vchBlockSig.resize(80, 0);
            BOOST_CHECK(!CheckBlockSignatureEncoding(block, NO_EXT_WORK_ACTIVATION_HEIGHT));
        }
    }
}

BOOST_AUTO_TEST_CASE(miner_reward_destination_activation)
{
    CKey miner;
    miner.MakeNewKey(true);
    CKey other;
    other.MakeNewKey(true);
    const CScript miner_script = CScript() << miner.GetPubKey().getvch() << OP_CHECKSIG;
    const CScript other_script = CScript() << other.GetPubKey().getvch() << OP_CHECKSIG;

    CMutableTransaction coinstake;
    coinstake.vout.emplace_back(0, CScript());
    coinstake.vout.emplace_back(100, miner_script);
    CBlock block;
    block.nNonce = CURRENT_MINING_NONCE;
    auto update = [&] { block.vtx = {MakeTransactionRef(coinstake), MakeTransactionRef(coinstake)}; };
    BOOST_CHECK(!CheckBlockRewardDestination(block, NO_EXT_WORK_ACTIVATION_HEIGHT));
    update();
    BOOST_CHECK(CheckBlockRewardDestination(block, NO_EXT_WORK_ACTIVATION_HEIGHT - 1));
    BOOST_CHECK(CheckBlockRewardDestination(block, NO_EXT_WORK_ACTIVATION_HEIGHT));

    coinstake.vout.emplace_back(1, other_script);
    update();
    BOOST_CHECK(CheckBlockRewardDestination(block, NO_EXT_WORK_ACTIVATION_HEIGHT - 1));
    BOOST_CHECK(!CheckBlockRewardDestination(block, NO_EXT_WORK_ACTIVATION_HEIGHT));
    BOOST_CHECK(!CheckBlockRewardDestination(block, NO_EXT_WORK_ACTIVATION_HEIGHT + 1));

    coinstake.vout.back().scriptPubKey = miner_script;
    update();
    BOOST_CHECK(CheckBlockRewardDestination(block, NO_EXT_WORK_ACTIVATION_HEIGHT));
    coinstake.vout.back().nValue = 0;
    coinstake.vout.back().scriptPubKey = other_script;
    update();
    BOOST_CHECK(CheckBlockRewardDestination(block, NO_EXT_WORK_ACTIVATION_HEIGHT));

    coinstake.vout[1].scriptPubKey = CScript() << OP_TRUE;
    update();
    BOOST_CHECK(!CheckBlockRewardDestination(block, NO_EXT_WORK_ACTIVATION_HEIGHT));
    coinstake.vout[1].scriptPubKey = miner_script;
    coinstake.vout[1].nValue = 0;
    update();
    BOOST_CHECK(!CheckBlockRewardDestination(block, NO_EXT_WORK_ACTIVATION_HEIGHT));

    block.nNonce = 0;
    BOOST_CHECK(CheckBlockRewardDestination(block, NO_EXT_WORK_ACTIVATION_HEIGHT));
}

BOOST_AUTO_TEST_CASE(block_signature_encoding_activation)
{
    CKey key;
    key.MakeNewKey(true);
    const uint256 hash = uint256S("01");
    std::vector<unsigned char> signature;
    do {
        key.MakeNewKey(true);
        BOOST_REQUIRE(key.SignMining(hash, signature));
    } while (signature.size() < 70);

    for (uint32_t marker : {CURRENT_MINING_NONCE}) {
        CBlock block;
        block.nNonce = marker;
        auto set_signature = [&](const std::vector<unsigned char>& der) {
            block.vchBlockSig = der;
            if (marker != 0xFEEDBEEF) block.vchBlockSig.resize(der.size() + 8, 0);
        };
        set_signature(signature);
        BOOST_CHECK(CheckBlockSignatureEncoding(block, 144443));
        BOOST_CHECK(CheckBlockSignatureEncoding(block, 144444));
        BOOST_CHECK(CheckBlockSignatureEncoding(block, 144445));

        auto padded = signature;
        padded.insert(padded.end(), {0xde, 0xad, 0xbe, 0xef});
        // Reproduce the vulnerability: lax verification accepts appended bytes.
        BOOST_REQUIRE(key.GetPubKey().Verify(hash, padded));
        set_signature(padded);
        BOOST_CHECK(CheckBlockSignatureEncoding(block, 144443));
        BOOST_CHECK(!CheckBlockSignatureEncoding(block, 144444));
        BOOST_CHECK(!CheckBlockSignatureEncoding(block, 144445));

        // Allowed total length and strict DER, but S=curve_order-1 is high-S.
        set_signature(ParseHex("304502200101010101010101010101010101010101010101010101010101010101010101022100fffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364140"));
        BOOST_CHECK(CheckBlockSignatureEncoding(block, 144443));
        BOOST_CHECK(!CheckBlockSignatureEncoding(block, 144444));

        set_signature({});
        BOOST_CHECK(!CheckBlockSignatureEncoding(block, 144444));
        block.vchBlockSig.assign(7, 0);
        BOOST_CHECK(!CheckBlockSignatureEncoding(block, 144444));

        // A prior context-free validation cache must not skip the new rule.
        block.fChecked = true;
        BOOST_CHECK(!CheckBlockSignatureEncoding(block, 144444));
    }
    for (uint32_t marker : {0xFEEDBEEFU, 0xFEEDBEE1U}) {
        CBlock retired;
        retired.nNonce = marker;
        retired.vchBlockSig = signature;
        if (marker == 0xFEEDBEE1U) retired.vchBlockSig.resize(signature.size() + 8, 0);
        BOOST_CHECK(!CheckBlockSignatureEncoding(retired, 144444));
    }
    CBlock pow_block;
    BOOST_CHECK(CheckBlockSignatureEncoding(pow_block, NO_EXT_WORK_ACTIVATION_HEIGHT));
}

static void TestBlockSubsidyHalvings(const Consensus::Params& consensusParams)
{
    int maxHalvings = 64;
    CAmount nInitialSubsidy = 50 * COIN;

    CAmount nPreviousSubsidy = nInitialSubsidy * 2; // for height == 0
    BOOST_CHECK_EQUAL(nPreviousSubsidy, nInitialSubsidy * 2);
    for (int nHalvings = 0; nHalvings < maxHalvings; nHalvings++) {
        int nHeight = nHalvings * consensusParams.nSubsidyHalvingInterval;
        CAmount nSubsidy = GetBlockSubsidy(nHeight, consensusParams);
        BOOST_CHECK(nSubsidy <= nInitialSubsidy);
        BOOST_CHECK_EQUAL(nSubsidy, nPreviousSubsidy / 2);
        nPreviousSubsidy = nSubsidy;
    }
    BOOST_CHECK_EQUAL(GetBlockSubsidy(maxHalvings * consensusParams.nSubsidyHalvingInterval, consensusParams), 0);
}

static void TestBlockSubsidyHalvings(int nSubsidyHalvingInterval)
{
    Consensus::Params consensusParams;
    consensusParams.nSubsidyHalvingInterval = nSubsidyHalvingInterval;
    TestBlockSubsidyHalvings(consensusParams);
}

BOOST_AUTO_TEST_CASE(block_subsidy_test)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    TestBlockSubsidyHalvings(chainParams->GetConsensus()); // As in main
    TestBlockSubsidyHalvings(150); // As in regtest
    TestBlockSubsidyHalvings(1000); // Just another interval
}

BOOST_AUTO_TEST_CASE(subsidy_limit_test)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    CAmount nSum = 0;
    for (int nHeight = 0; nHeight < 14000000; nHeight += 1000) {
        CAmount nSubsidy = GetBlockSubsidy(nHeight, chainParams->GetConsensus());
        BOOST_CHECK(nSubsidy <= 50 * COIN);
        nSum += nSubsidy * 1000;
        BOOST_CHECK(MoneyRange(nSum));
    }
    BOOST_CHECK_EQUAL(nSum, CAmount{2099999997690000});
}

BOOST_AUTO_TEST_CASE(signet_parse_tests)
{
    ArgsManager signet_argsman;
    signet_argsman.ForceSetArg("-signetchallenge", "51"); // set challenge to OP_TRUE
    const auto signet_params = CreateChainParams(signet_argsman, ChainType::SIGNET);
    CBlock block;
    BOOST_CHECK(signet_params->GetConsensus().signet_challenge == std::vector<uint8_t>{OP_TRUE});
    CScript challenge{OP_TRUE};

    // empty block is invalid
    BOOST_CHECK(!SignetTxs::Create(block, challenge));
    BOOST_CHECK(!CheckSignetBlockSolution(block, signet_params->GetConsensus()));

    // no witness commitment
    CMutableTransaction cb;
    cb.vout.emplace_back(0, CScript{});
    block.vtx.push_back(MakeTransactionRef(cb));
    block.vtx.push_back(MakeTransactionRef(cb)); // Add dummy tx to exercise merkle root code
    BOOST_CHECK(!SignetTxs::Create(block, challenge));
    BOOST_CHECK(!CheckSignetBlockSolution(block, signet_params->GetConsensus()));

    // no header is treated valid
    std::vector<uint8_t> witness_commitment_section_141{0xaa, 0x21, 0xa9, 0xed};
    for (int i = 0; i < 32; ++i) {
        witness_commitment_section_141.push_back(0xff);
    }
    cb.vout.at(0).scriptPubKey = CScript{} << OP_RETURN << witness_commitment_section_141;
    block.vtx.at(0) = MakeTransactionRef(cb);
    BOOST_CHECK(SignetTxs::Create(block, challenge));
    BOOST_CHECK(CheckSignetBlockSolution(block, signet_params->GetConsensus()));

    // no data after header, valid
    std::vector<uint8_t> witness_commitment_section_325{0xec, 0xc7, 0xda, 0xa2};
    cb.vout.at(0).scriptPubKey = CScript{} << OP_RETURN << witness_commitment_section_141 << witness_commitment_section_325;
    block.vtx.at(0) = MakeTransactionRef(cb);
    BOOST_CHECK(SignetTxs::Create(block, challenge));
    BOOST_CHECK(CheckSignetBlockSolution(block, signet_params->GetConsensus()));

    // Premature end of data, invalid
    witness_commitment_section_325.push_back(0x01);
    witness_commitment_section_325.push_back(0x51);
    cb.vout.at(0).scriptPubKey = CScript{} << OP_RETURN << witness_commitment_section_141 << witness_commitment_section_325;
    block.vtx.at(0) = MakeTransactionRef(cb);
    BOOST_CHECK(!SignetTxs::Create(block, challenge));
    BOOST_CHECK(!CheckSignetBlockSolution(block, signet_params->GetConsensus()));

    // has data, valid
    witness_commitment_section_325.push_back(0x00);
    cb.vout.at(0).scriptPubKey = CScript{} << OP_RETURN << witness_commitment_section_141 << witness_commitment_section_325;
    block.vtx.at(0) = MakeTransactionRef(cb);
    BOOST_CHECK(SignetTxs::Create(block, challenge));
    BOOST_CHECK(CheckSignetBlockSolution(block, signet_params->GetConsensus()));

    // Extraneous data, invalid
    witness_commitment_section_325.push_back(0x00);
    cb.vout.at(0).scriptPubKey = CScript{} << OP_RETURN << witness_commitment_section_141 << witness_commitment_section_325;
    block.vtx.at(0) = MakeTransactionRef(cb);
    BOOST_CHECK(!SignetTxs::Create(block, challenge));
    BOOST_CHECK(!CheckSignetBlockSolution(block, signet_params->GetConsensus()));
}

//! Test retrieval of valid assumeutxo values.
BOOST_AUTO_TEST_CASE(test_assumeutxo)
{
    const auto params = CreateChainParams(*m_node.args, ChainType::REGTEST);

    // These heights don't have assumeutxo configurations associated, per the contents
    // of kernel/chainparams.cpp.
    std::vector<int> bad_heights{0, 100, 111, 115, 209, 211};

    for (auto empty : bad_heights) {
        const auto out = params->AssumeutxoForHeight(empty);
        BOOST_CHECK(!out);
    }

    const auto out110 = *params->AssumeutxoForHeight(110);
    BOOST_CHECK_EQUAL(out110.hash_serialized.ToString(), "6657b736d4fe4db0cbc796789e812d5dba7f5c143764b1b6905612f1830609d1");
    BOOST_CHECK_EQUAL(out110.nChainTx, 111U);

    const auto out110_2 = *params->AssumeutxoForBlockhash(uint256S("0x696e92821f65549c7ee134edceeeeaaa4105647a3c4fd9f298c0aec0ab50425c"));
    BOOST_CHECK_EQUAL(out110_2.hash_serialized.ToString(), "6657b736d4fe4db0cbc796789e812d5dba7f5c143764b1b6905612f1830609d1");
    BOOST_CHECK_EQUAL(out110_2.nChainTx, 111U);
}

BOOST_AUTO_TEST_CASE(block_malleation)
{
    // Test utilities that calls `IsBlockMutated` and then clears the validity
    // cache flags on `CBlock`.
    auto is_mutated = [](CBlock& block, bool check_witness_root) {
        bool mutated{IsBlockMutated(block, check_witness_root)};
        block.fChecked = false;
        block.m_checked_witness_commitment = false;
        block.m_checked_merkle_root = false;
        return mutated;
    };
    auto is_not_mutated = [&is_mutated](CBlock& block, bool check_witness_root) {
        return !is_mutated(block, check_witness_root);
    };

    // Test utilities to create coinbase transactions and insert witness
    // commitments.
    //
    // Note: this will not include the witness stack by default to avoid
    // triggering the "no witnesses allowed for blocks that don't commit to
    // witnesses" rule when testing other malleation vectors.
    auto create_coinbase_tx = [](bool include_witness = false) {
        CMutableTransaction coinbase;
        coinbase.vin.resize(1);
        if (include_witness) {
            coinbase.vin[0].scriptWitness.stack.resize(1);
            coinbase.vin[0].scriptWitness.stack[0] = std::vector<unsigned char>(32, 0x00);
        }

        coinbase.vout.resize(1);
        coinbase.vout[0].scriptPubKey.resize(MINIMUM_WITNESS_COMMITMENT);
        coinbase.vout[0].scriptPubKey[0] = OP_RETURN;
        coinbase.vout[0].scriptPubKey[1] = 0x24;
        coinbase.vout[0].scriptPubKey[2] = 0xaa;
        coinbase.vout[0].scriptPubKey[3] = 0x21;
        coinbase.vout[0].scriptPubKey[4] = 0xa9;
        coinbase.vout[0].scriptPubKey[5] = 0xed;

        auto tx = MakeTransactionRef(coinbase);
        assert(tx->IsCoinBase());
        return tx;
    };
    auto insert_witness_commitment = [](CBlock& block, uint256 commitment) {
        assert(!block.vtx.empty() && block.vtx[0]->IsCoinBase() && !block.vtx[0]->vout.empty());

        CMutableTransaction mtx{*block.vtx[0]};
        CHash256().Write(commitment).Write(std::vector<unsigned char>(32, 0x00)).Finalize(commitment);
        memcpy(&mtx.vout[0].scriptPubKey[6], commitment.begin(), 32);
        block.vtx[0] = MakeTransactionRef(mtx);
    };

    {
        CBlock block;

        // Empty block is expected to have merkle root of 0x0.
        BOOST_CHECK(block.vtx.empty());
        block.hashMerkleRoot = uint256{1};
        BOOST_CHECK(is_mutated(block, /*check_witness_root=*/false));
        block.hashMerkleRoot = uint256{};
        BOOST_CHECK(is_not_mutated(block, /*check_witness_root=*/false));

        // Block with a single coinbase tx is mutated if the merkle root is not
        // equal to the coinbase tx's hash.
        block.vtx.push_back(create_coinbase_tx());
        BOOST_CHECK(block.vtx[0]->GetHash() != block.hashMerkleRoot);
        BOOST_CHECK(is_mutated(block, /*check_witness_root=*/false));
        block.hashMerkleRoot = block.vtx[0]->GetHash();
        BOOST_CHECK(is_not_mutated(block, /*check_witness_root=*/false));

        // Block with two transactions is mutated if the merkle root does not
        // match the double sha256 of the concatenation of the two transaction
        // hashes.
        block.vtx.push_back(MakeTransactionRef(CMutableTransaction{}));
        BOOST_CHECK(is_mutated(block, /*check_witness_root=*/false));
        HashWriter hasher;
        hasher.write(Span(reinterpret_cast<const std::byte*>(block.vtx[0]->GetHash().data()), 32));
        hasher.write(Span(reinterpret_cast<const std::byte*>(block.vtx[1]->GetHash().data()), 32));
        block.hashMerkleRoot = hasher.GetHash();
        BOOST_CHECK(is_not_mutated(block, /*check_witness_root=*/false));

        // Block with two transactions is mutated if any node is duplicate.
        {
            block.vtx[1] = block.vtx[0];
            BOOST_CHECK(is_mutated(block, /*check_witness_root=*/false));
            HashWriter hasher;
            hasher.write(Span(reinterpret_cast<const std::byte*>(block.vtx[0]->GetHash().data()), 32));
            hasher.write(Span(reinterpret_cast<const std::byte*>(block.vtx[1]->GetHash().data()), 32));
            block.hashMerkleRoot = hasher.GetHash();
            BOOST_CHECK(is_mutated(block, /*check_witness_root=*/false));
        }

        // Blocks with 64-byte coinbase transactions are not considered mutated
        block.vtx.clear();
        {
            CMutableTransaction mtx;
            mtx.vin.resize(1);
            mtx.vout.resize(1);
            mtx.vout[0].scriptPubKey.resize(4);
            block.vtx.push_back(MakeTransactionRef(mtx));
            block.hashMerkleRoot = block.vtx.back()->GetHash();
            assert(block.vtx.back()->IsCoinBase());
            assert(GetSerializeSize(block.vtx.back(), PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS) == 64);
        }
        BOOST_CHECK(is_not_mutated(block, /*check_witness_root=*/false));
    }

    {
        // Test merkle root malleation

        // Pseudo code to mine transactions tx{1,2,3}:
        //
        // ```
        // loop {
        //   tx1 = random_tx()
        //   tx2 = random_tx()
        //   tx3 = deserialize_tx(txid(tx1) || txid(tx2));
        //   if serialized_size_without_witness(tx3) == 64 {
        //     print(hex(tx3))
        //     break
        //   }
        // }
        // ```
        //
        // The `random_tx` function used to mine the txs below simply created
        // empty transactions with a random version field.
        CMutableTransaction tx1;
        BOOST_CHECK(DecodeHexTx(tx1, "ff204bd0000000000000", /*try_no_witness=*/true, /*try_witness=*/false));
        CMutableTransaction tx2;
        BOOST_CHECK(DecodeHexTx(tx2, "8ae53c92000000000000", /*try_no_witness=*/true, /*try_witness=*/false));
        CMutableTransaction tx3;
        BOOST_CHECK(DecodeHexTx(tx3, "cdaf22d00002c6a7f848f8ae4d30054e61dcf3303d6fe01d282163341f06feecc10032b3160fcab87bdfe3ecfb769206ef2d991b92f8a268e423a6ef4d485f06", /*try_no_witness=*/true, /*try_witness=*/false));
        {
            // Verify that double_sha256(txid1||txid2) == txid3
            HashWriter hasher;
            hasher.write(Span(reinterpret_cast<const std::byte*>(tx1.GetHash().data()), 32));
            hasher.write(Span(reinterpret_cast<const std::byte*>(tx2.GetHash().data()), 32));
            assert(hasher.GetHash() == tx3.GetHash());
            // Verify that tx3 is 64 bytes in size (without witness).
            assert(GetSerializeSize(tx3, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS) == 64);
        }

        CBlock block;
        block.vtx.push_back(MakeTransactionRef(tx1));
        block.vtx.push_back(MakeTransactionRef(tx2));
        uint256 merkle_root = block.hashMerkleRoot = BlockMerkleRoot(block);
        BOOST_CHECK(is_not_mutated(block, /*check_witness_root=*/false));

        // Mutate the block by replacing the two transactions with one 64-byte
        // transaction that serializes into the concatenation of the txids of
        // the transactions in the unmutated block.
        block.vtx.clear();
        block.vtx.push_back(MakeTransactionRef(tx3));
        BOOST_CHECK(!block.vtx.back()->IsCoinBase());
        BOOST_CHECK(BlockMerkleRoot(block) == merkle_root);
        BOOST_CHECK(is_mutated(block, /*check_witness_root=*/false));
    }

    {
        CBlock block;
        block.vtx.push_back(create_coinbase_tx(/*include_witness=*/true));
        {
            CMutableTransaction mtx;
            mtx.vin.resize(1);
            mtx.vin[0].scriptWitness.stack.resize(1);
            mtx.vin[0].scriptWitness.stack[0] = {0};
            block.vtx.push_back(MakeTransactionRef(mtx));
        }
        block.hashMerkleRoot = BlockMerkleRoot(block);
        // Block with witnesses is considered mutated if the witness commitment
        // is not validated.
        BOOST_CHECK(is_mutated(block, /*check_witness_root=*/false));
        // Block with invalid witness commitment is considered mutated.
        BOOST_CHECK(is_mutated(block, /*check_witness_root=*/true));

        // Block with valid commitment is not mutated
        {
            auto commitment{BlockWitnessMerkleRoot(block)};
            insert_witness_commitment(block, commitment);
            block.hashMerkleRoot = BlockMerkleRoot(block);
        }
        BOOST_CHECK(is_not_mutated(block, /*check_witness_root=*/true));

        // Malleating witnesses should be caught by `IsBlockMutated`.
        {
            CMutableTransaction mtx{*block.vtx[1]};
            assert(!mtx.vin[0].scriptWitness.stack[0].empty());
            ++mtx.vin[0].scriptWitness.stack[0][0];
            block.vtx[1] = MakeTransactionRef(mtx);
        }
        // Without also updating the witness commitment, the merkle root should
        // not change when changing one of the witnesses.
        BOOST_CHECK(block.hashMerkleRoot == BlockMerkleRoot(block));
        BOOST_CHECK(is_mutated(block, /*check_witness_root=*/true));
        {
            auto commitment{BlockWitnessMerkleRoot(block)};
            insert_witness_commitment(block, commitment);
            block.hashMerkleRoot = BlockMerkleRoot(block);
        }
        BOOST_CHECK(is_not_mutated(block, /*check_witness_root=*/true));

        // Test malleating the coinbase witness reserved value
        {
            CMutableTransaction mtx{*block.vtx[0]};
            mtx.vin[0].scriptWitness.stack.resize(0);
            block.vtx[0] = MakeTransactionRef(mtx);
            block.hashMerkleRoot = BlockMerkleRoot(block);
        }
        BOOST_CHECK(is_mutated(block, /*check_witness_root=*/true));
    }
}

BOOST_AUTO_TEST_SUITE_END()
