// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2014-2020 The Däsh Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <consensus/merkle.h>

#include <tinyformat.h>
#include <util.h>
#include <utilstrencodings.h>

#include <arith_uint256.h>

#include <assert.h>
#include <memory>
#include <chainparamsseeds.h>

static CBlock CreateGenesisBlock(const char* pszTimestamp, const CScript& genesisOutputScript, uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

static CBlock CreateDevNetGenesisBlock(const uint256 &prevBlockHash, const std::string& devNetName, uint32_t nTime, uint32_t nNonce, uint32_t nBits, const CAmount& genesisReward)
{
    assert(!devNetName.empty());

    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    // put height (BIP34) and devnet name into coinbase
    txNew.vin[0].scriptSig = CScript() << 1 << std::vector<unsigned char>(devNetName.begin(), devNetName.end());
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = CScript() << OP_RETURN;

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = 4;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock = prevBlockHash;
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

/**
 * Build the genesis block. Note that the output of its generation
 * transaction cannot be spent since it did not originally exist in the
 * database.
 *
 * CBlock(hash=00000ffd590b14, ver=1, hashPrevBlock=00000000000000, hashMerkleRoot=e0028e, nTime=1390095618, nBits=1e0ffff0, nNonce=28917698, vtx=1)
 *   CTransaction(hash=e0028e, ver=1, vin.size=1, vout.size=1, nLockTime=0)
 *     CTxIn(COutPoint(000000, -1), coinbase 04ffff001d01044c5957697265642030392f4a616e2f3230313420546865204772616e64204578706572696d656e7420476f6573204c6976653a204f76657273746f636b2e636f6d204973204e6f7720416363657074696e6720426974636f696e73)
 *     CTxOut(nValue=50.00000000, scriptPubKey=0xA9037BAC7050C479B121CF)
 *   vMerkleTree: e0028e
 */
static CBlock CreateGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    const char* pszTimestamp = "Pray always that ye may be accounted worthy to stand before the Son of Man.";
    const CScript genesisOutputScript = CScript() << ParseHex("040184710fa689ad5023690c80f3a49c8f13f8d45b8c857fbcbc8bc4a8e4d3eb4b10f4d4604fa08dce601aaf0f470216fe1b51850b4acf21b179c45070ac7b03a9") << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nVersion, genesisReward);
}


void CChainParams::UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout, int64_t nWindowSize, int64_t nThresholdStart, int64_t nThresholdMin, int64_t nFalloffCoeff)
{
    consensus.vDeployments[d].nStartTime = nStartTime;
    consensus.vDeployments[d].nTimeout = nTimeout;
    if (nWindowSize != -1) {
            consensus.vDeployments[d].nWindowSize = nWindowSize;
    }
    if (nThresholdStart != -1) {
        consensus.vDeployments[d].nThresholdStart = nThresholdStart;
    }
    if (nThresholdMin != -1) {
        consensus.vDeployments[d].nThresholdMin = nThresholdMin;
    }
    if (nFalloffCoeff != -1) {
        consensus.vDeployments[d].nFalloffCoeff = nFalloffCoeff;
    }
}

void CChainParams::UpdateDIP3Parameters(int nActivationHeight, int nEnforcementHeight)
{
    consensus.DIP0003Height = nActivationHeight;
    consensus.DIP0003EnforcementHeight = nEnforcementHeight;
}

void CChainParams::UpdateBudgetParameters(int nMasternodePaymentsStartBlock, int nBudgetPaymentsStartBlock, int nSuperblockStartBlock)
{
    consensus.nMasternodePaymentsStartBlock = nMasternodePaymentsStartBlock;
    consensus.nBudgetPaymentsStartBlock = nBudgetPaymentsStartBlock;
    consensus.nSuperblockStartBlock = nSuperblockStartBlock;
}

void CChainParams::UpdateSubsidyAndDiffParams(int nMinimumDifficultyBlocks, int nHighSubsidyBlocks, int nHighSubsidyFactor)
{
    consensus.nMinimumDifficultyBlocks = nMinimumDifficultyBlocks;
    consensus.nHighSubsidyBlocks = nHighSubsidyBlocks;
    consensus.nHighSubsidyFactor = nHighSubsidyFactor;
}

void CChainParams::UpdateLLMQChainLocks(Consensus::LLMQType llmqType) {
    consensus.llmqTypeChainLocks = llmqType;
}

void CChainParams::UpdateLLMQTestParams(int size, int threshold) {
    auto& params = consensus.llmqs.at(Consensus::LLMQ_TEST);
    params.size = size;
    params.minSize = threshold;
    params.threshold = threshold;
    params.dkgBadVotesThreshold = threshold;
}

void CChainParams::UpdateLLMQDevnetParams(int size, int threshold)
{
    auto& params = consensus.llmqs.at(Consensus::LLMQ_DEVNET);
    params.size = size;
    params.minSize = threshold;
    params.threshold = threshold;
    params.dkgBadVotesThreshold = threshold;
}

static CBlock FindDevNetGenesisBlock(const CBlock &prevBlock, const CAmount& reward)
{
    std::string devNetName = gArgs.GetDevNetName();
    assert(!devNetName.empty());

    CBlock block = CreateDevNetGenesisBlock(prevBlock.GetHash(), devNetName.c_str(), prevBlock.nTime + 1, 0, prevBlock.nBits, reward);

    arith_uint256 bnTarget;
    bnTarget.SetCompact(block.nBits);

    for (uint32_t nNonce = 0; nNonce < UINT32_MAX; nNonce++) {
        block.nNonce = nNonce;

        uint256 hash = block.GetHash();
        if (UintToArith256(hash) <= bnTarget)
            return block;
    }

    // This is very unlikely to happen as we start the devnet with a very low difficulty. In many cases even the first
    // iteration of the above loop will give a result already
    error("FindDevNetGenesisBlock: could not find devnet genesis block for %s", devNetName);
    assert(false);
}


// this one is for devnets only
static Consensus::LLMQParams llmq_devnet = {
        .type = Consensus::LLMQ_DEVNET,
        .name = "llmq_devnet",
        .size = 10,
        .minSize = 7,
        .threshold = 6,

        .dkgInterval = 24, // one DKG per hour
        .dkgPhaseBlocks = 2,
        .dkgMiningWindowStart = 10, // dkgPhaseBlocks * 5 = after finalization
        .dkgMiningWindowEnd = 18,
        .dkgBadVotesThreshold = 7,

        .signingActiveQuorumCount = 3, // just a few ones to allow easier testing

        .keepOldConnections = 4,
        .recoveryMembers = 6,
};


// this one is for testing only
static Consensus::LLMQParams llmq_test = {
        .type = Consensus::LLMQ_TEST,
        .name = "llmq_test",
        .size = 3,
        .minSize = 2,
        .threshold = 2,

        .dkgInterval = 24, // one DKG per hour
        .dkgPhaseBlocks = 2,
        .dkgMiningWindowStart = 10, // dkgPhaseBlocks * 5 = after finalization
        .dkgMiningWindowEnd = 18,
        .dkgBadVotesThreshold = 3,

        .signingActiveQuorumCount = 2, // just a few ones to allow easier testing

        .keepOldConnections = 3,
        .recoveryMembers = 3,
};
static Consensus::LLMQParams llmq5_60 = {
        .type = Consensus::LLMQ_5_60,
        .name = "llmq_5_60",
        .size = 6,
        .minSize = 3,
        .threshold = 3,

        .dkgInterval = 24, // one DKG per hour
        .dkgPhaseBlocks = 2,
        .dkgMiningWindowStart = 10, // dkgPhaseBlocks * 5 = after finalization
        .dkgMiningWindowEnd = 18,
        .dkgBadVotesThreshold = 3,

        .signingActiveQuorumCount = 3, 

        .keepOldConnections = 3,
        .recoveryMembers = 3,
};

static Consensus::LLMQParams llmq400_60 = {
        .type = Consensus::LLMQ_400_60,
        .name = "llmq_400_60",
        .size = 9,
        .minSize = 4,
        .threshold = 3,

        .dkgInterval = 24 * 12, // one DKG every 12 hours
        .dkgPhaseBlocks = 4,
        .dkgMiningWindowStart = 20, // dkgPhaseBlocks * 5 = after finalization
        .dkgMiningWindowEnd = 28,
        .dkgBadVotesThreshold = 100,
        .signingActiveQuorumCount = 3, 

        .keepOldConnections = 9,
        .recoveryMembers = 9,
};

// Used for deployment and min-proto-version signalling, so it needs a higher threshold
static Consensus::LLMQParams llmq400_85 = {
        .type = Consensus::LLMQ_400_85,
        .name = "llmq_400_85",
        .size = 4,
        .minSize = 3,
        .threshold = 3,

        .dkgInterval = 24 * 24, // one DKG every 24 hours
        .dkgPhaseBlocks = 4,
        .dkgMiningWindowStart = 20, // dkgPhaseBlocks * 5 = after finalization
        .dkgMiningWindowEnd = 48, // give it a larger mining window to make sure it is mined
        .dkgBadVotesThreshold = 100,

        .signingActiveQuorumCount = 4, // four days worth of LLMQs

        .keepOldConnections = 5,
        .recoveryMembers = 10,
};


/**
 * Main network
 */
/**
 * What makes a good checkpoint block?
 * + Is surrounded by blocks with reasonable timestamps
 *   (no blocks before with a timestamp after, none after with
 *    timestamp before)
 * + Contains no strange transactions
 */


class CMainParams : public CChainParams {
public:
    CMainParams() {
        strNetworkID = "main";
		//BiblePay

		consensus.POOM_PHASEOUT_HEIGHT = 199000;  // June 2020
		consensus.FPOG_CUTOVER_HEIGHT = 100001;   // Feb 7th, 2019 (100,001)
		consensus.PODC2_CUTOVER_HEIGHT = 166075;  // Go live height for PODC 2.0, DWS (Christmas 2019) (166050=monthly superblock, 166070=daily GSC)
		consensus.PODC_LAST_BLOCK = 107000; // March 13th, 2019
		consensus.EVOLUTION_CUTOVER_HEIGHT = 123200; // June 2nd, 2019 
		consensus.F7000_CUTOVER_HEIGHT = 7000;
		consensus.F8000_CUTOVER_HEIGHT = 21350;
		consensus.F9000_CUTOVER_HEIGHT = 21550;
		consensus.F11000_CUTOVER_HEIGHT = 33440;
		consensus.F12000_CUTOVER_HEIGHT = 35110;
		consensus.F13000_CUTOVER_HEIGHT = 57700; 
		consensus.ANTI_GPU_HEIGHT = 170830;
		consensus.RANDOMX_HEIGHT = 184675;
		consensus.LAST_TITHE_BLOCK = 21565;
		consensus.ABNHeight = 127000;

		// End of BiblePay

        consensus.nSubsidyHalvingInterval = 205 * 365; 
        consensus.nMasternodePaymentsStartBlock = 21600; 
		consensus.nMasternodePaymentsStartBlock = 21600;
		consensus.nMasternodePaymentsIncreaseBlock = 21601;
	
    	consensus.nMasternodePaymentsIncreasePeriod = 205 * 30; 
		consensus.nBudgetPaymentsStartBlock = 21551; // This is when we change our block distribution to include PR, P2P, IT expenses
		
		consensus.nInstantSendConfirmationsRequired = 6;
        consensus.nInstantSendKeepLock = 12;
        consensus.nBudgetPaymentsStartBlock = 21551; // actual historical value
        
		consensus.nRuleChangeActivationThreshold = 21600; // Same as Masternode Payments Start block
		consensus.nBudgetPaymentsCycleBlocks = 205 * 30; // Monthly
		consensus.nBudgetPaymentsWindowBlocks = 100;
	

		consensus.nInstantSendConfirmationsRequired = 1;
		consensus.nSuperblockStartBlock = 21710; // The first superblock
		consensus.nSuperblockCycle = 205 * 30; // Monthly

		consensus.nDCCSuperblockStartBlock = 33451; //F11000 CUTOVER HEIGHT + 11

		consensus.nDCCSuperblockCycle = 205; // Daily

		consensus.nGovernanceMinQuorum = 4;
		consensus.nGovernanceFilterElements = 20000;

		consensus.nSuperblockStartHash = uint256(); 
		consensus.nMasternodeMinimumConfirmations = 7;
		// BIP34, 65, 66 should match Evo cutover height
		consensus.BIP65Height = 123200; // 00000000000076d8fcea02ec0963de4abfd01e771fec0863f960c2c64fe6f357
		consensus.BIP66Height = 123200; // 00000000000b1fa2dfa312863570e13fae9ca7b5566cb27e55422620b469aefa

		consensus.nSanctuaryPaymentsPhaseIIHeight = 166075; // Set this at the Go-Live height for .14 
		// September 2020 Go-Live (POOS)
		consensus.POOS_HEIGHT = 217000; // Sept 2020 Mandatory Upgrade height
		consensus.TRIBULATION_HEIGHT = 255000;  // Go Live for BiblePay Mandatory 1.5.4.4 - March 1st, 2021
	
		// Harvest - Go Live for BiblePay Mandatory Upgrade (1.6.n.n) - July 1st, 2021
		// consensus.BIP34Height = 123200;  // Unable to create duplicate coinbases (tx-malleability protection)
		// BiblePay had this activated since 123200, but the BIP34+DIP3 rule (bad-cb-height) stops harvest from syncing in prod @ height 134041
		consensus.DIP0001Height = 279700;
		consensus.BIP34Hash = uint256();
		consensus.BIP34Height = 279700;
		consensus.DIP0003Height = 279700;
		consensus.LLMQHeight = 279700; // The first attempt at llmq quorums that conform to voting out bad quorum members
		consensus.DIP0003EnforcementHeight = 279700;
		consensus.HARVEST_HEIGHT = 279700;  // Go live height for BiblePay-Harvest
		consensus.HARVEST_HEIGHT2 = 279700; // New payment %s for masternodes (20%), less for monthly budget (5%), more for UTXO (50%), RANDOMX (25%)
		// Chainlock enforcement:
		consensus.DIP0008Height = 280700;

		// ToDo : set this to the actual LLMQ go live height
        consensus.DIP0003EnforcementHash = uint256S("0x0");

		// Mission Critical to do: verify each switch in getblockchaininfo (dip activation):
		// End of Harvest

		consensus.nSanctuaryPaymentsPhaseIIHeight = 166075; // Set this at the Go-Live height for .14 
		consensus.QTHeight = 124000;  // Note to future forkers of DACs!  This height must be > (BLOCKS_PER_DAY * 32)!  Thank you for your support!
		consensus.powLimit = uint256S("0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); 
		consensus.FoundationAddress = "BB2BwSbDCqCqNsfc7FgWFJn4sRgnUt4tsM";
		consensus.FoundationPODSAddress = "BScSypUZVEEY4TMz1ehyyPcS5wrnMM7WPB";
		consensus.FoundationQTAddress = "BDcNYc8tGXmwD7QmLXbR1rk1qteTDVEjUD";
		consensus.BurnAddress = "B4T5ciTCkWauSqVAcVKy88ofjcSasUkSYU";
		consensus.BurnAddressOrphanDonations = "B4YNHuaXAgpbyP5QgM3H6S41AuKcsRCEaB";
		consensus.BurnAddressWhaleMatches = "B4dey6hqas4JVvfekCkb4jJLcCCevtXcfC";

		consensus.nPowTargetTimespan = 24 * 60 * 60; // 1 day
		consensus.nPowTargetSpacing = 7 * 60; // 7 minutes
		consensus.fPowAllowMinDifficultyBlocks = false;
		consensus.fPowNoRetargeting = false;
		consensus.nPowKGWHeight = 0;
		consensus.nPowDGWHeight = 1;

        consensus.nGovernanceFilterElements = 20000;
        consensus.nMasternodeMinimumConfirmations = 15;
       

        consensus.fPowAllowMinDifficultyBlocks = false;
        
        consensus.nRuleChangeActivationThreshold = 1916; // 95% of 2016
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // Deployment of BIP68, BIP112, and BIP113.
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 1486252800; // Feb 5th, 2017
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 1517788800; // Feb 5th, 2018

        // Deployment of DIP0001
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nStartTime = 1508025600; // Oct 15th, 2017
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nTimeout = 1539561600; // Oct 15th, 2018
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nWindowSize = 4032;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nThresholdStart = 3226; // 80% of 4032

        // Deployment of BIP147
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].nStartTime = 1524477600; // Apr 23th, 2018
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].nTimeout = 1556013600; // Apr 23th, 2019
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].nWindowSize = 4032;
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].nThresholdStart = 3226; // 80% of 4032

        // Deployment of DIP0003
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].bit = 3;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].nStartTime = 1546300800; // Jan 1st, 2019
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].nTimeout = 1577836800; // Jan 1st, 2020
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].nWindowSize = 4032;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].nThresholdStart = 3226; // 80% of 4032

        // Deployment of DIP0008
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].bit = 4;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].nStartTime = 1557878400; // May 15th, 2019
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].nTimeout = 1589500800; // May 15th, 2020
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].nWindowSize = 4032;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].nThresholdStart = 3226; // 80% of 4032

       

		 // Deployment of Block Reward Reallocation
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].bit = 5;
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nStartTime = 1601510400; // Oct 1st, 2020
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nTimeout = 1633046400; // Oct 1st, 2021
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nWindowSize = 4032;
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nThresholdStart = 3226; // 80% of 4032
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nThresholdMin = 2420; // 60% of 4032
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nFalloffCoeff = 5; // this corresponds to 10 periods


		// The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x0000000000000000000000000000000000000000000000000000806e93acd1bb"); 

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x3b4431310395638c0ed65b40ede4b110d8da70fcc0c2ed4a729fb8e4d78b4452"); 


        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0xbf;
        pchMessageStart[1] = 0x0c;
        pchMessageStart[2] = 0x6b;
        pchMessageStart[3] = 0xbd;
        vAlertPubKey = ParseHex("0x0432cb0fcd551eb206e80fa59cc7fd4038923ef3416f3e132637b8e9a4bcd2f8c9e6c4ee9f01c6f3cea7fb5d85b65b7c18ba30e6c133242bf0de488dd839c36001");

        nDefaultPort = 40000;
        nPruneAfterHeight = 450000;
		
        genesis = CreateGenesisBlock(1496347844, 12, 0x207fffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x3b4431310395638c0ed65b40ede4b110d8da70fcc0c2ed4a729fb8e4d78b4452"));
 
        assert(genesis.hashMerkleRoot == uint256S("0x02b05f3b8a7168bcf83b888e0092446b248b2641bd9844b5d12a45eaa2765725"));


        // Note that of those which support the service bits prefix, most only support a subset of
        // possible options.
        // This is fine at runtime as we'll fall back to using them as a oneshot if they dont support the
        // service bits we want, but we should get them updated to support all service bits wanted by any
        // release ASAP to avoid it where possible.
        vSeeds.emplace_back("dnsseed.biblepay.org");
        vSeeds.emplace_back("dnsseed.biblepaydot.io");

        // BBP addresses start with 'B'
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1, 25);
        // BBP script addresses start with '7'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1, 16);
        // BBP private keys start with '7' or 'X'
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1, 182);
        // BBP BIP32 pubkeys start with 'xpub' (Bitcoin defaults)
		base58Prefixes[EXT_PUBLIC_KEY] = {0x04,0x88,0xB2,0x1E};
	    // BBP BIP32 prvkeys start with 'xprv' (Bitcoin defaults)
		base58Prefixes[EXT_SECRET_KEY] = {0x04,0x88,0xAD,0xE4};

        // BBP BIP44 coin type
        nExtCoinType = 10;

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_main, pnSeed6_main + ARRAYLEN(pnSeed6_main));

        // long living quorum params
	    consensus.llmqs[Consensus::LLMQ_5_60] = llmq5_60;
        consensus.llmqs[Consensus::LLMQ_400_60] = llmq400_60;
        /* consensus.llmqs[Consensus::LLMQ_400_85] = llmq400_85; */
		
        consensus.llmqTypeChainLocks = Consensus::LLMQ_400_60;
        consensus.llmqTypeInstantSend = Consensus::LLMQ_5_60;

		//fMiningRequiresPeers = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fRequireRoutableExternalIP = true;
        fMineBlocksOnDemand = false;
        fAllowMultipleAddressesFromGroup = true;
        fAllowMultiplePorts = true;
        nLLMQConnectionRetryTimeout = 60;

        nPoolMinParticipants = 3;
        nPoolNewMinParticipants = 3;
        nPoolMaxParticipants = 5;
        nPoolNewMaxParticipants = 20;
        nFulfilledRequestExpireTime = 60*60; // fulfilled requests expire in 1 hour

        vSporkAddresses = {"BB2BwSbDCqCqNsfc7FgWFJn4sRgnUt4tsM"};
      
        nMinSporkKeys = 1;
        fBIP9CheckMasternodesUpgraded = true;

        checkpointData = {
            {
                {
					{7, uint256S("0x00022b1be28b1deb9a51d4d69f3fa393f4ea36621039b6313a6c0796546621de")},
					{120, uint256S("0x00002fc6c9e4889a8d1a9bd5919a6bd4a4b09091e55049480509da14571e5653")},
					{6999, uint256S("0x000000dfbcdec4e6b0ab899f04d7ce8e4d8bc8a725a47169b626acd207ccea8d")},
					{18900, uint256S("0x94a1ff5e84a31219d5472536215f5a77b00cfd61f3fb99d0e9d3ab392f2ed2a6")},
					{20900, uint256S("0x23d0b5887ca89fc2dddb2f34810675cb1826371172a91b1211be4677fd260490")},
					{21650, uint256S("0x756e18f6a20d02d7af0a32c5705960d58adc4daba24c6a7dd9a8b80776bcca73")},
					{21960, uint256S("0xdd7e0acd7b9569b6fbf84a8262bb5fe3ea28af259f12d060acbcd62d4241fb51")},
					{32500, uint256S("0xacb4534f70da9624fee2b9032d2fe47fe6d7d3e8cffdbfbca4d0a3a63394045a")},
					{33460, uint256S("0xe64ff92ae97c2978c14d97ae45c618c1f2140339ce9ccb770945d3efb7d5e0f5")},
					{63000, uint256S("0x1ef6f6e5d803cf04f84a4377365db6cefe82a775a9a596029db7d23fa3652f57")},
					{105028, uint256S("0x2d38a145444c5e880209fe6a1ee1b09542f5438f50ca8736b20bafc5c552d79b")},
					{119000, uint256S("0x527a086d639b315e4eb03cb52d394a809654d072387c5cb73401799cb5998f90")},
					{145000, uint256S("0xe830a524fc6a71aa0c3171db1ef592893f84de65fdcda57ddff8b378dcbda12f")},
					{206000, uint256S("0xc5c6dfcc2b84fb48873b1fd45d2a693a183fbaef8c79673d6defb2b073ef09d2")},
				}
            }
        };

        chainTxData = ChainTxData{
            1594482077, // * UNIX timestamp of last known number of transactions
            1292967,     // * total number of transactions between genesis and that timestamp
                        //   (the tx=... number in the SetBestChain debug.log lines)
            0.01        // * estimated number of transactions per second after that timestamp
        };
    }
};

/**
 * Testnet (v3)
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
		strNetworkID = "test";
        consensus.POOM_PHASEOUT_HEIGHT = 38000; 
		consensus.POOS_HEIGHT = 55720; // Set the height for the September 2020 Mandatory Upgrade

		consensus.PODC_LAST_BLOCK = 100; // March 13th, 2019
		consensus.PODC2_CUTOVER_HEIGHT = 21000;  // Go live height for PODC 2.0, DWS (Christmas 2019)
		consensus.F7000_CUTOVER_HEIGHT = 100;
		consensus.F12000_CUTOVER_HEIGHT = 100;
		consensus.F13000_CUTOVER_HEIGHT = 100; 
		consensus.ANTI_GPU_HEIGHT = 28010;
		consensus.RANDOMX_HEIGHT = 28003;
		int BLOCKS_PER_DAY = 205;
		consensus.F9000_CUTOVER_HEIGHT= 100;
		consensus.F11000_CUTOVER_HEIGHT= 1;
		consensus.F8000_CUTOVER_HEIGHT = 1;
		consensus.F9000_CUTOVER_HEIGHT = 10;
		consensus.F13000_CUTOVER_HEIGHT = 1;
		consensus.FPOG_CUTOVER_HEIGHT = 1;  // Dec. 23rd, 2018
		consensus.F14000_CUTOVER_HEIGHT = 1; 
		consensus.LAST_TITHE_BLOCK = 1;
		consensus.EVOLUTION_CUTOVER_HEIGHT = 1000;
		consensus.nSubsidyHalvingInterval = 365 * BLOCKS_PER_DAY;
		consensus.nMasternodePaymentsStartBlock = 2001;
		consensus.nMasternodePaymentsIncreaseBlock = 2001;
		consensus.nMasternodePaymentsIncreasePeriod = BLOCKS_PER_DAY * 30;
		consensus.nInstantSendConfirmationsRequired = 1;
		consensus.nInstantSendKeepLock = 6;
		consensus.nBudgetPaymentsStartBlock = 2002;
		consensus.QTHeight = 5000;
		consensus.TRIBULATION_HEIGHT = 58000;
		
		consensus.nBudgetPaymentsCycleBlocks = 50;
		consensus.nBudgetPaymentsWindowBlocks = 10;
		consensus.nSuperblockStartBlock = 4007; // NOTE: Should satisfy nSuperblockStartBlock > nBudgetPaymentsStartBlock
		consensus.nSuperblockStartHash = uint256(); // Do not check this on testnet
		consensus.nSuperblockCycle = BLOCKS_PER_DAY * 7; // Weekly in TestNet
		consensus.nGovernanceMinQuorum = 1;
		consensus.nGovernanceFilterElements = 500;
		consensus.nMasternodeMinimumConfirmations = 1;
		consensus.FoundationAddress = "yTrEKf8XQ7y7tychC2gWuGw1hsLqBybnEN";
		consensus.FoundationPODSAddress = "yPrVdc1kFTC19Hx4NMnwEeLFifToUcQGMs";
		consensus.FoundationQTAddress = "ygGKQR4bYwjGaEnX8KCyzEJhht4paYCiuo";
		consensus.BurnAddress = "yLKSrCjLQFsfVgX8RjdctZ797d54atPjnV";
		consensus.BurnAddressOrphanDonations = "yLQjXPrepS7N2E7NVbLvrrMUYux6d8kkD8";
		consensus.BurnAddressWhaleMatches = "yLW2CayyEcM4YmhcZT4Eq9bozCq8griKNa";

		consensus.nDCCSuperblockStartBlock = 4504; 
        consensus.nDCCSuperblockCycle = BLOCKS_PER_DAY; // Daily
        consensus.BIP65Height = 2431; 
        consensus.BIP66Height = 2075; 
        consensus.DIP0001Height = 7000;
		consensus.nSanctuaryPaymentsPhaseIIHeight = 8400;

		//LLMQ:
		consensus.DIP0003EnforcementHash = uint256();
        consensus.BIP34Hash = uint256S("0x0");
		consensus.BIP34Height = 72860;
		consensus.DIP0003Height = 72860; 
		consensus.LLMQHeight = 72860;
    	consensus.DIP0003EnforcementHeight = 72860;
		consensus.HARVEST_HEIGHT = 72860;  // Go live height for BiblePay-Harvest
		consensus.HARVEST_HEIGHT2 = 73500; // New payment %s for masternodes (20%), less for monthly budget (5%), more for UTXO (50%), RANDOMX (25%)
		// Chainlock Enforcement:
		consensus.DIP0008Height = 74000;

        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); 
        consensus.nPowTargetTimespan = 24 * 60 * 60; // DAC: 1 day
        consensus.nPowTargetSpacing = 3 * 60; // 3 minutes in testnet (after harvest height) - for LLMQ
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = false;
        consensus.nPowKGWHeight = 1; // nPowKGWHeight >= nPowDGWHeight means "no KGW"
        consensus.nPowDGWHeight = 1;
        consensus.nRuleChangeActivationThreshold = 1512; // 75% for testchains
        consensus.nMinerConfirmationWindow = 205; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // Deployment of BIP68, BIP112, and BIP113.
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 1544655600; // Dec 13th, 2018
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 1576191600; // Dec 13th, 2019

        // Deployment of DIP0001
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nStartTime = 1544655600; // Dec 13th, 2018
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nTimeout = 1576191600; // Dec 13th, 2019
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nWindowSize = 100;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nThresholdStart = 50; // 50% of 100

        // Deployment of BIP147
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].nStartTime = 1544655600; // Dec 13th, 2018
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].nTimeout = 1576191600; // Dec 13th, 2019
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].nWindowSize = 100;
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].nThresholdStart = 50; // 50% of 100

        // Deployment of DIP0003
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].bit = 3;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].nStartTime = 1544655600; // Dec 13th, 2018
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].nTimeout = 1576191600; // Dec 13th, 2019
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].nWindowSize = 100;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].nThresholdStart = 50; // 50% of 100

        // Deployment of DIP0008
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].bit = 4;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].nStartTime = 1553126400; // Mar 21st, 2019
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].nTimeout = 1584748800; // Mar 21st, 2020
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].nWindowSize = 100;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].nThresholdStart = 50; // 50% of 100

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x000000000000000000000000000000000000000000000000000000000000000"); // 4000

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x0000000005ae4db9746d6cad8e0ccebdef1e05afec9c40809f31457fdaf7d843"); // 95930

        pchMessageStart[0] = 0xce;
        pchMessageStart[1] = 0xe2;
        pchMessageStart[2] = 0xca;
        pchMessageStart[3] = 0xff;
        vAlertPubKey = ParseHex("0x03239f430d6025348e971896d6f2ab6226e04a56ab798ae8b5efe3c5cf056d0609");
        nDefaultPort = 40001;
        nPruneAfterHeight = 100000;

        genesis = CreateGenesisBlock(1496347850, 15, 0x207fffff, 1, 50 * COIN);
	
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x122f423f0912850a871c58f1533dd80be62154bb0c56dfb8cb9ae2b957d1ac10"));
        assert(genesis.hashMerkleRoot == uint256S("0x02b05f3b8a7168bcf83b888e0092446b248b2641bd9844b5d12a45eaa2765725"));

        vFixedSeeds.clear();
        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_test, pnSeed6_test + ARRAYLEN(pnSeed6_test));

        vSeeds.clear();
        // nodes with support for servicebits filtering should be at the top
    


        // Testnet DAC addresses start with 'y'
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,140);
        // Testnet DAC script addresses start with '8' or '9'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,19);
        // Testnet private keys start with '9' or 'c' (Bitcoin defaults)
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        // Testnet DAC BIP32 pubkeys start with 'tpub' (Bitcoin defaults)
		base58Prefixes[EXT_PUBLIC_KEY] = {0x04,0x35,0x87,0xCF};
	    // Testnet DAC BIP32 prvkeys start with 'tprv' (Bitcoin defaults)
		base58Prefixes[EXT_SECRET_KEY] = {0x04,0x35,0x83,0x94};

        // Testnet DAC BIP44 coin type is '1' (All coin's testnet default)
        nExtCoinType = 1;

        // long living quorum params
	    //consensus.llmqs[Consensus::LLMQ_5_60] = llmq5_60;
        consensus.llmqs[Consensus::LLMQ_TEST] = llmq_test;
		
		consensus.llmqTypeChainLocks = Consensus::LLMQ_TEST;
        consensus.llmqTypeInstantSend = Consensus::LLMQ_TEST;


        //fMiningRequiresPeers = false;
        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        fRequireRoutableExternalIP = true;
        fMineBlocksOnDemand = false;
        fAllowMultipleAddressesFromGroup = true;
        fAllowMultiplePorts = true;

        nPoolMinParticipants = 3;
        nPoolMaxParticipants = 5;
        nFulfilledRequestExpireTime = 5 * 60; // fulfilled requests expire in 5 minutes

        vSporkAddresses = {"yTrEKf8XQ7y7tychC2gWuGw1hsLqBybnEN"};
        nMinSporkKeys = 1;
        fBIP9CheckMasternodesUpgraded = true;

		checkpointData = (CCheckpointData) 
		{
			{
				{1,     uint256S("0xde31388eddd9e0ea515353628a0ca1f167466f09c45b10354de10459c7c018f2")},
				{5000,  uint256S("0xc63bfaaddcb4714c594b701ae8bd6320cef3221df7b33d781d0400ca5a1348b9")},
				{18610, uint256S("0x778067c8239f02166519359a82d2e0b5bea19a94df7344442dad9fb65ff013f8")},
				{72000, uint256S("0xaa706f900264c8a459afb01ca296055c41b3173d8c02b8e6d388c4487ad6552f")},
			}
		};

        chainTxData = ChainTxData{
            1574877650, // * UNIX timestamp of last known number of transactions
            29477,       // * total number of transactions between genesis and that timestamp
                        //   (the tx=... number in the SetBestChain debug.log lines)
            0.01        // * estimated number of transactions per second after that timestamp
        };

    }
};

/**
 * Devnet
 */
class CDevNetParams : public CChainParams {
public:
    CDevNetParams() {
        strNetworkID = "devnet";
        int BLOCKS_PER_DAY = 205;

		consensus.nSubsidyHalvingInterval = 210240;
		consensus.PODC2_CUTOVER_HEIGHT = 19000;  // Go live height for PODC 2.0, DWS (Christmas 2019)
	
		consensus.nMasternodePaymentsStartBlock = 4010; // not true, but it's ok as long as it's less then nMasternodePaymentsIncreaseBlock
		consensus.nMasternodePaymentsIncreaseBlock = 4030;
		consensus.nMasternodePaymentsIncreasePeriod = 10;
		consensus.nInstantSendConfirmationsRequired = 2;
		consensus.ABNHeight = 100;
		consensus.nInstantSendKeepLock = 6;
		consensus.nBudgetPaymentsStartBlock = 4100;
		consensus.nBudgetPaymentsCycleBlocks = 50;
		consensus.nBudgetPaymentsWindowBlocks = 10;
		consensus.nSuperblockStartBlock = 4200; // NOTE: Should satisfy nSuperblockStartBlock > nBudgetPaymentsStartBlock
		consensus.nSuperblockStartHash = uint256(); // do not check this on devnet
		consensus.nSuperblockCycle = 24; // Superblocks can be issued hourly on devnet
		consensus.nGovernanceMinQuorum = 1;
		consensus.nGovernanceFilterElements = 500;
		consensus.F9000_CUTOVER_HEIGHT = 10;
		consensus.nDCCSuperblockStartBlock = 104; //F11000 CUTOVER HEIGHT + 11
		consensus.nDCCSuperblockCycle = BLOCKS_PER_DAY; // Daily
		consensus.nMasternodeMinimumConfirmations = 1;
		consensus.BIP34Height = 1; // BIP34 activated immediately on devnet
		consensus.BIP65Height = 1; // BIP65 activated immediately on devnet
		consensus.BIP66Height = 1; // BIP66 activated immediately on devnet
		consensus.DIP0001Height = 2; // DIP0001 activated immediately on devnet
		consensus.DIP0003Height = 2;

		consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // ~uint256(0) >> 1
		consensus.nPowTargetTimespan = 24 * 60 * 60; // DAC: 1 day
		consensus.nPowTargetSpacing = 2.5 * 60; // DAC: 2.5 minutes
		consensus.fPowAllowMinDifficultyBlocks = true;
		consensus.fPowNoRetargeting = false;
		consensus.nPowKGWHeight = 1; // nPowKGWHeight >= nPowDGWHeight means "no KGW"
		consensus.nPowDGWHeight = 1;
		consensus.nRuleChangeActivationThreshold = 1512; // 75% for testchains
		consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
		consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
		consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
		consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // Deployment of BIP68, BIP112, and BIP113.
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 1506556800; // September 28th, 2017
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 1538092800; // September 28th, 2018

        // Deployment of DIP0001
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nStartTime = 1505692800; // Sep 18th, 2017
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nTimeout = 1537228800; // Sep 18th, 2018
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nWindowSize = 100;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nThresholdStart = 50; // 50% of 100

        // Deployment of BIP147
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].nStartTime = 1517792400; // Feb 5th, 2018
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].nTimeout = 1549328400; // Feb 5th, 2019
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].nWindowSize = 100;
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].nThresholdStart = 50; // 50% of 100

        // Deployment of DIP0003
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].bit = 3;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].nStartTime = 1535752800; // Sep 1st, 2018
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].nTimeout = 1567288800; // Sep 1st, 2019
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].nWindowSize = 100;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].nThresholdStart = 50; // 50% of 100

        // Deployment of DIP0008
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].bit = 4;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].nStartTime = 1553126400; // Mar 21st, 2019
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].nTimeout = 1584748800; // Mar 21st, 2020
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].nWindowSize = 100;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].nThresholdStart = 50; // 50% of 100

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x000000000000000000000000000000000000000000000000000000000000000");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x000000000000000000000000000000000000000000000000000000000000000");

        pchMessageStart[0] = 0xe2;
        pchMessageStart[1] = 0xca;
        pchMessageStart[2] = 0xff;
        pchMessageStart[3] = 0xce;
        vAlertPubKey = ParseHex("04517d8a699cb43d3938d7b24faaff7cda448ca4ea267723ba614784de661949bf632d6304316b244646dea079735b9a6fc4af804efb4752075b9fe2245e14e412");
        nDefaultPort = 40002;
        nPruneAfterHeight = 1000;

        genesis = CreateGenesisBlock(1417713337, 1096447, 0x207fffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x000008ca1832a4baf228eb1553c03d3a2c8e02399550dd6ea8d65cec3ef23d2e"));
        assert(genesis.hashMerkleRoot == uint256S("0xe0028eb9648db56b1ac77cf090b99048a8007e2bb64b68f092c03c7f56a662c7"));

        devnetGenesis = FindDevNetGenesisBlock(genesis, 50 * COIN);
        consensus.hashDevnetGenesisBlock = devnetGenesis.GetHash();

        vFixedSeeds.clear();
        vSeeds.clear();

        // Testnet DAC addresses start with 'y'
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,140);
        // Testnet DAC script addresses start with '8' or '9'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,19);
        // Testnet private keys start with '9' or 'c' (Bitcoin defaults)
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        // Testnet DAC BIP32 pubkeys start with 'tpub' (Bitcoin defaults)
		base58Prefixes[EXT_PUBLIC_KEY] = {0x04,0x35,0x87,0xCF};
		// Testnet DAC BIP32 prvkeys start with 'tprv' (Bitcoin defaults)
		base58Prefixes[EXT_SECRET_KEY] = {0x04,0x35,0x83,0x94};
		
        // Testnet DAC BIP44 coin type is '1' (All coin's testnet default)
        nExtCoinType = 1;

        // long living quorum params
        consensus.llmqs[Consensus::LLMQ_5_60] = llmq5_60;
        consensus.llmqs[Consensus::LLMQ_400_60] = llmq400_60;
        consensus.llmqs[Consensus::LLMQ_400_85] = llmq400_85;
        consensus.llmqTypeChainLocks = Consensus::LLMQ_5_60;
        consensus.llmqTypeInstantSend = Consensus::LLMQ_5_60;

        //fMiningRequiresPeers = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        fMineBlocksOnDemand = false;
        fAllowMultipleAddressesFromGroup = true;
        fAllowMultiplePorts = true;

        nPoolMinParticipants = 3;
        nPoolMaxParticipants = 5;
        nFulfilledRequestExpireTime = 5*60; // fulfilled requests expire in 5 minutes

        vSporkAddresses = {"yjPtiKh2uwk3bDutTEA2q9mCtXyiZRWn55"};
        nMinSporkKeys = 1;
        // devnets are started with no blocks and no MN, so we can't check for upgraded MN (as there are none)
        fBIP9CheckMasternodesUpgraded = false;

        checkpointData = (CCheckpointData) {
			{
				{      0, uint256S("0x000008ca1832a4baf228eb1553c03d3a2c8e02399550dd6ea8d65cec3ef23d2e")},
				{      1, devnetGenesis.GetHash()},
			}
        };

        chainTxData = ChainTxData{
            devnetGenesis.GetBlockTime(), // * UNIX timestamp of devnet genesis block
            2,                            // * we only have 2 coinbase transactions when a devnet is started up
            0.01                          // * estimated number of transactions per second
        };
    }

    void UpdateSubsidyAndDiffParams(int nMinimumDifficultyBlocks, int nHighSubsidyBlocks, int nHighSubsidyFactor)
    {
        consensus.nMinimumDifficultyBlocks = nMinimumDifficultyBlocks;
        consensus.nHighSubsidyBlocks = nHighSubsidyBlocks;
        consensus.nHighSubsidyFactor = nHighSubsidyFactor;
    }

    void UpdateLLMQChainLocks(Consensus::LLMQType llmqType) {
        consensus.llmqTypeChainLocks = llmqType;
    }
};

/**
 * Regression test
 */
class CRegTestParams : public CChainParams {
public:
    CRegTestParams() {
        strNetworkID = "regtest";
        consensus.nSubsidyHalvingInterval = 150;
        consensus.nMasternodePaymentsStartBlock = 240;
        consensus.nMasternodePaymentsIncreaseBlock = 350;
        consensus.nMasternodePaymentsIncreasePeriod = 10;
        consensus.nInstantSendConfirmationsRequired = 2;
		consensus.F9000_CUTOVER_HEIGHT = 10;
	    consensus.nInstantSendKeepLock = 6;
        consensus.nBudgetPaymentsStartBlock = 1000;
        consensus.nBudgetPaymentsCycleBlocks = 50;
        consensus.nBudgetPaymentsWindowBlocks = 10;
        consensus.nSuperblockStartBlock = 1500;
        consensus.nSuperblockStartHash = uint256(); // do not check this on regtest
        consensus.nSuperblockCycle = 10;
        consensus.nGovernanceMinQuorum = 1;
        consensus.nGovernanceFilterElements = 100;
        consensus.nMasternodeMinimumConfirmations = 1;
        consensus.BIP34Height = 100000000; // BIP34 has not activated on regtest (far in the future so block v1 are not rejected in tests)
        consensus.BIP34Hash = uint256();
        consensus.BIP65Height = 1351; // BIP65 activated on regtest (Used in rpc activation tests)
        consensus.BIP66Height = 1251; // BIP66 activated on regtest (Used in rpc activation tests)
        consensus.DIP0001Height = 2000;
        consensus.DIP0003Height = 432;
        consensus.DIP0003EnforcementHeight = 500;
        consensus.DIP0003EnforcementHash = uint256();
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // ~uint256(0) >> 1
        consensus.nPowTargetTimespan = 24 * 60 * 60; // DAC: 1 day
        consensus.nPowTargetSpacing = 2.5 * 60; // DAC: 2.5 minutes
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = true;
        consensus.nPowKGWHeight = 1; // same as mainnet
        consensus.nPowDGWHeight = 1; // same as mainnet
        consensus.nRuleChangeActivationThreshold = 108; // 75% for testchains
        consensus.nMinerConfirmationWindow = 144; // Faster than normal for regtest (144 instead of 2016)
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].bit = 3;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].bit = 4;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].nTimeout = 999999999999ULL;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x00");

        pchMessageStart[0] = 0xfc;
        pchMessageStart[1] = 0xc1;
        pchMessageStart[2] = 0xb7;
        pchMessageStart[3] = 0xdc;
        nDefaultPort = 19994;
        nPruneAfterHeight = 1000;

		genesis = CreateGenesisBlock(1496347864, 18, 0x207fffff, 1, 50 * COIN);
	
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x2a2a5f2fd4648f7d72b41f076567d143ed67db051dfda746c9cfba616c6a49d6"));
        assert(genesis.hashMerkleRoot == uint256S("0x02b05f3b8a7168bcf83b888e0092446b248b2641bd9844b5d12a45eaa2765725"));

        vFixedSeeds.clear(); //!< Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();      //!< Regtest mode doesn't have any DNS seeds.

        //fMiningRequiresPeers = false;
        fDefaultConsistencyChecks = true;
        fRequireStandard = false;
        fRequireRoutableExternalIP = false;
        fMineBlocksOnDemand = true;
        fAllowMultipleAddressesFromGroup = true;
        fAllowMultiplePorts = true;

        nFulfilledRequestExpireTime = 5*60; // fulfilled requests expire in 5 minutes

        vSporkAddresses = {"yj949n1UH6fDhw6HtVE5VMj2iSTaSWBMcW"};
        nMinSporkKeys = 1;
        // regtest usually has no masternodes in most tests, so don't check for upgraged MNs
        fBIP9CheckMasternodesUpgraded = false;

        checkpointData = (CCheckpointData)
		{
			{
				{0, uint256S("0x000008ca1832a4baf228eb1553c03d3a2c8e02399550dd6ea8d65cec3ef23d2e")},
			}
        };

        chainTxData = ChainTxData{
            0,
            0,
            0
        };

        // Regtest DAC addresses start with 'y'
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,140);
        // Regtest DAC script addresses start with '8' or '9'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,19);
        // Regtest DAC keys start with '9' or 'c' (Bitcoin defaults)
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        // Regtest DAC BIP32 pubkeys start with 'tpub' (Bitcoin defaults)
		base58Prefixes[EXT_PUBLIC_KEY] = {0x04,0x35,0x87,0xCF};
		// Regtest DAC BIP32 prvkeys start with 'tprv' (Bitcoin defaults)
		base58Prefixes[EXT_SECRET_KEY] = {0x04,0x35,0x83,0x94};
		
        // Regtest DAC BIP44 coin type is '1' (All coin's testnet default)
        nExtCoinType = 1;

        // long living quorum params
        consensus.llmqs[Consensus::LLMQ_5_60] = llmq5_60;
        consensus.llmqs[Consensus::LLMQ_5_60] = llmq5_60;
        consensus.llmqTypeChainLocks = Consensus::LLMQ_5_60;
        consensus.llmqTypeInstantSend = Consensus::LLMQ_5_60;
    }

    
};

static std::unique_ptr<CChainParams> globalChainParams;

const CChainParams &Params() {
    assert(globalChainParams);
    return *globalChainParams;
}

std::unique_ptr<CChainParams> CreateChainParams(const std::string& chain)
{
    if (chain == CBaseChainParams::MAIN)
        return std::unique_ptr<CChainParams>(new CMainParams());
    else if (chain == CBaseChainParams::TESTNET)
        return std::unique_ptr<CChainParams>(new CTestNetParams());
    else if (chain == CBaseChainParams::DEVNET) {
        return std::unique_ptr<CChainParams>(new CDevNetParams());
    } else if (chain == CBaseChainParams::REGTEST)
        return std::unique_ptr<CChainParams>(new CRegTestParams());
    throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectParams(const std::string& network)
{
    SelectBaseParams(network);
    globalChainParams = CreateChainParams(network);
}

void UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout, int64_t nWindowSize, int64_t nThresholdStart, int64_t nThresholdMin, int64_t nFalloffCoeff)
{
    globalChainParams->UpdateVersionBitsParameters(d, nStartTime, nTimeout, nWindowSize, nThresholdStart, nThresholdMin, nFalloffCoeff);
}

void UpdateDIP3Parameters(int nActivationHeight, int nEnforcementHeight)
{
    globalChainParams->UpdateDIP3Parameters(nActivationHeight, nEnforcementHeight);
}

void UpdateBudgetParameters(int nMasternodePaymentsStartBlock, int nBudgetPaymentsStartBlock, int nSuperblockStartBlock)
{
    globalChainParams->UpdateBudgetParameters(nMasternodePaymentsStartBlock, nBudgetPaymentsStartBlock, nSuperblockStartBlock);
}

void UpdateDevnetSubsidyAndDiffParams(int nMinimumDifficultyBlocks, int nHighSubsidyBlocks, int nHighSubsidyFactor)
{
    globalChainParams->UpdateSubsidyAndDiffParams(nMinimumDifficultyBlocks, nHighSubsidyBlocks, nHighSubsidyFactor);
}

void UpdateDevnetLLMQChainLocks(Consensus::LLMQType llmqType)
{
    globalChainParams->UpdateLLMQChainLocks(llmqType);
}

void UpdateLLMQTestParams(int size, int threshold)
{
    globalChainParams->UpdateLLMQTestParams(size, threshold);
}

void UpdateLLMQDevnetParams(int size, int threshold)
{
    globalChainParams->UpdateLLMQDevnetParams(size, threshold);
}
