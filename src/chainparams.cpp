// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2020 The Bitcoin Core developers
// Copyright (c) 2014-2024 The Dash Core developers
// Copyright (c) 2014-2024 The BiblePay Core developers

// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>

#include <chainparamsseeds.h>
#include <consensus/merkle.h>
#include <deploymentinfo.h>
#include <llmq/params.h>
#include <util/ranges.h>
#include <util/system.h>
#include <util/underlying.h>
#include <versionbits.h>

#include <arith_uint256.h>

#include <assert.h>

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

static CBlock FindDevNetGenesisBlock(const CBlock &prevBlock, const CAmount& reward)
{
    std::string devNetName = gArgs.GetDevNetName();
    assert(!devNetName.empty());

    CBlock block = CreateDevNetGenesisBlock(prevBlock.GetHash(), devNetName, prevBlock.nTime + 1, 0, prevBlock.nBits, reward);

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

bool CChainParams::IsValidMNActivation(int nBit, int64_t timePast) const
{
    assert(nBit < VERSIONBITS_NUM_BITS);

    for (int index = 0; index < Consensus::MAX_VERSION_BITS_DEPLOYMENTS; ++index) {
        if (consensus.vDeployments[index].bit == nBit) {
            auto& deployment = consensus.vDeployments[index];
            if (timePast > deployment.nTimeout || timePast < deployment.nStartTime) {
                LogPrintf("%s: activation by bit=%d deployment='%s' is out of time range start=%lld timeout=%lld\n", __func__, nBit, VersionBitsDeploymentInfo[Consensus::DeploymentPos(index)].name, deployment.nStartTime, deployment.nTimeout);
                continue;
            }
            if (!deployment.useEHF) {
                LogPrintf("%s: trying to set MnEHF for non-masternode activation fork bit=%d\n", __func__, nBit);
                return false;
            }
            LogPrintf("%s: set MnEHF for bit=%d is valid\n", __func__, nBit);
            return true;
        }
    }
    LogPrintf("%s: WARNING: unknown MnEHF fork bit=%d\n", __func__, nBit);
    return true;
}

void CChainParams::AddLLMQ(Consensus::LLMQType llmqType)
{
    assert(!GetLLMQ(llmqType).has_value());
    for (const auto& llmq_param : Consensus::available_llmqs) {
        if (llmq_param.type == llmqType) {
            consensus.llmqs.push_back(llmq_param);
            return;
        }
    }
    error("CChainParams::%s: unknown LLMQ type %d", __func__, static_cast<uint8_t>(llmqType));
    assert(false);
}

std::optional<Consensus::LLMQParams> CChainParams::GetLLMQ(Consensus::LLMQType llmqType) const
{
    for (const auto& llmq_param : consensus.llmqs) {
        if (llmq_param.type == llmqType) {
            return std::make_optional(llmq_param);
        }
    }
    return std::nullopt;
}

/**
 * Main network on which people trade goods and services.
 */
class CMainParams : public CChainParams {
public:
    CMainParams() {
        strNetworkID = CBaseChainParams::MAIN;
        consensus.BABYLON_FALLING_HEIGHT = 600000;
        
        consensus.BABYLON_FALLING_TIME = 1744172648;

        consensus.BARLEY_HARVEST_HEIGHT  = 320000;
	    consensus.BARLEY_HARVEST_HEIGHT2 = 320000;
        consensus.EXODUS_HEIGHT = 346500;
        consensus.REDSEA_HEIGHT = 428000;


        consensus.LATTER_RAIN_HEIGHT = 449485;
        consensus.REDSEA_PARTING = 457510;
    	consensus.RANDOMX_HEIGHT = 184675;
        consensus.nSubsidyHalvingInterval = 210240; // Note: actual number of blocks per calendar year with DGW v3 is ~200700 (for example 449750 - 249050)
        consensus.BIP16Height = 0;
        consensus.nMasternodePaymentsStartBlock = 21600; // not true, but it's ok as long as it's less then nMasternodePaymentsIncreaseBlock
        consensus.nMasternodePaymentsIncreaseBlock = 21601; // actual historical value
        consensus.nMasternodePaymentsIncreasePeriod = 205*30; // 17280 - actual historical value
        consensus.nInstantSendConfirmationsRequired = 2;
        consensus.nInstantSendKeepLock = 24;
        consensus.nBudgetPaymentsStartBlock = 21551; // actual historical value
        consensus.nBudgetPaymentsCycleBlocks = 205*30; //Monthly
        consensus.nBudgetPaymentsWindowBlocks = 100;
        consensus.nSuperblockStartBlock = 21710; // The block at which 12.1 goes live (end of final 12.0 budget cycle)

        consensus.nSuperblockStartHash = uint256S("0x0");
        consensus.nSuperblockCycle = 205*30;
        consensus.nSuperblockMaturityWindow = 205*3; // ~3 days before actual Superblock is emitted
        consensus.nGovernanceMinQuorum = 10;
        consensus.nGovernanceFilterElements = 20000;
        consensus.nMasternodeMinimumConfirmations = 7;
        consensus.BIP34Height = 278014;
        consensus.BIP34Hash = uint256S("0x0");
        consensus.BIP65Height = 123200; // 00000000000076d8fcea02ec0963de4abfd01e771fec0863f960c2c64fe6f357
        consensus.BIP66Height = 123200; // 00000000000b1fa2dfa312863570e13fae9ca7b5566cb27e55422620b469aefa
        consensus.BIP147Height = 123200; // 00000000000000117befca4fab5622514772f608852e5edd8df9c55464b6fe37
        consensus.CSVHeight = 123200; // 00000000000002e3d3a6224cfce80bae367fd3283d1e5a8ba50e5e60b2d2905d
        consensus.DIP0001Height = 278014; // 000000000000000cbc9cb551e8ee1ac7aa223585cbdfb755d3683bafd93679e4
        consensus.DIP0003Height = 278014;

        consensus.DIP0003EnforcementHeight = 320000;
        consensus.DIP0003EnforcementHash = uint256S("0x0");
        consensus.DIP0008Height = 279407; // 00000000000000112e41e4b3afda8b233b8cc07c532d2eac5de097b68358c43e
        consensus.BRRHeight = 123200; // 000000000000000c5a124f3eccfbe6e17876dca79cec9e63dfa70d269113c926
        consensus.DIP0020Height = 278014; // 000000000000000f64ed3bd9af1078177ac026f6aa2677aa4d8beeae43be56cc
        consensus.DIP0024Height = 278014; // 0000000000000001342be9c0b75ad40c276beaad91616423c4d9cb101b3db438
        consensus.DIP0024QuorumsHeight = 278014; // 000000000000001aa25181e4c466e593992c98f9eb21c69ee757b8bb0af50244
        consensus.V19Height = 278014; // 0000000000000015e32e73052d663626327004c81c5c22cb8b42c361015c0eae
        consensus.MinBIP9WarningHeight = 300000 + 2016; // V19 activation height + miner confirmation window
        consensus.powLimit = uint256S("0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // 

		consensus.FoundationAddress = "BB2BwSbDCqCqNsfc7FgWFJn4sRgnUt4tsM";
		//consensus.FoundationPODSAddress = "BScSypUZVEEY4TMz1ehyyPcS5wrnMM7WPB";
		//consensus.FoundationQTAddress = "BDcNYc8tGXmwD7QmLXbR1rk1qteTDVEjUD";
		consensus.BurnAddress = "B4T5ciTCkWauSqVAcVKy88ofjcSasUkSYU";
		
        consensus.nPowTargetTimespan = 24 * 60 * 60; // Biblepay: 1 day
        consensus.nPowTargetSpacing = 7 * 60; // Biblepay: 7 minutes
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nPowKGWHeight = 0;
        consensus.nPowDGWHeight = 1;
        consensus.nRuleChangeActivationThreshold = 1916; // 95% of 2016
        consensus.nMinerConfirmationWindow = 205; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        consensus.vDeployments[Consensus::DEPLOYMENT_V20].bit = 9;
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nStartTime = 1700006400;      // November 15, 2023
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nTimeout = 1731628800;        // November 15, 2024
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nWindowSize = 4032;
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nThresholdStart = 3226;       // 80% of 4032
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nThresholdMin = 2420;         // 60% of 4032
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nFalloffCoeff = 5;            // this corresponds to 10 periods

        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].bit = 10;
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nStartTime = 1704067200;   // January 1, 2024
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nTimeout = 1767225600; // January 1, 2026
        // NOTE: nWindowSize for MN_RR __MUST__ be greater than or equal to nSuperblockMaturityWindow for CSuperblock::GetPaymentsLimit() to work correctly
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nWindowSize = 4032;
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nThresholdStart = 3226;     // 80% of 4032
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nThresholdMin = 2420;       // 60% of 4032
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nFalloffCoeff = 5;          // this corresponds to 10 periods
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].useEHF = true;

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
        nDefaultPort = 40000;
        nDefaultPlatformP2PPort = 36656;
        nDefaultPlatformHTTPPort = 443;
        nPruneAfterHeight = 0;
        m_assumed_blockchain_size = 45;
        m_assumed_chain_state_size = 1;

        genesis =  CreateGenesisBlock(1496347844, 12, 0x207fffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x3b4431310395638c0ed65b40ede4b110d8da70fcc0c2ed4a729fb8e4d78b4452"));
        assert(genesis.hashMerkleRoot == uint256S("0x02b05f3b8a7168bcf83b888e0092446b248b2641bd9844b5d12a45eaa2765725"));

        // Note that of those which support the service bits prefix, most only support a subset of
        // possible options.
        // This is fine at runtime as we'll fall back to using them as a oneshot if they don't support the
        // service bits we want, but we should get them updated to support all service bits wanted by any
        // release ASAP to avoid it where possible.
        vSeeds.emplace_back("dnsseed.biblepay.org");

        // BBP addresses start with 'B'
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1, 25);  // &h19
        // BBP script addresses start with '7'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1, 16);  // &h10
        // BBP private keys start with '7' or 'X'
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1, 182); // &hb6
        // BBP BIP32 pubkeys start with 'xpub' (Bitcoin defaults)
		base58Prefixes[EXT_PUBLIC_KEY] = {0x04,0x88,0xB2,0x1E};
	    // BBP BIP32 prvkeys start with 'xprv' (Bitcoin defaults)
		base58Prefixes[EXT_SECRET_KEY] = {0x04,0x88,0xAD,0xE4};

        // BBP BIP44 coin type
        nExtCoinType = 1777;
        vFixedSeeds = std::vector<uint8_t>(std::begin(chainparams_seed_main), std::end(chainparams_seed_main));

        // long living quorum params

        AddLLMQ(Consensus::LLMQType::LLMQ_400_60);
        consensus.llmqTypeChainLocks = Consensus::LLMQType::LLMQ_400_60;
        consensus.llmqTypeDIP0024InstantSend = Consensus::LLMQType::LLMQ_400_60;
        consensus.llmqTypePlatform = Consensus::LLMQType::LLMQ_400_60;
        consensus.llmqTypeMnhf = Consensus::LLMQType::LLMQ_400_60;


        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fRequireRoutableExternalIP = true;
        m_is_test_chain = false;
        fAllowMultipleAddressesFromGroup = true;
        fAllowMultiplePorts = true;
        nLLMQConnectionRetryTimeout = 60;
        m_is_mockable_chain = false;

        nPoolMinParticipants = 2;
        nPoolMaxParticipants = 20;
        nFulfilledRequestExpireTime = 60*60; // fulfilled requests expire in 1 hour

        vSporkAddresses = {"BB2BwSbDCqCqNsfc7FgWFJn4sRgnUt4tsM"};
        nMinSporkKeys = 1;

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
                    {270000, uint256S("0x4cb48793f323d2252cfc9f5b233931e8a727a091532841379a375390d9e31e20")},
					{315770, uint256S("0xe8fb51c409c9c2942e60e15d013efc475eacd0a4fa9df8d54eddfaa0c421146a")},
                    {344000, uint256S("0x7c3e13950bdb25ea8ff11bd091be5bede5241dfa87ca149a75a2103c1041468a")},
                    {440000, uint256S("0x70e38d78fcd8a5e21c4352a42c67b55907ce1f1831ed2edec9100cd657dd650f")},
                    {498700, uint256S("0xa261c9535b7f8d67e59ccad435183ee29a100b52a7496526b840a89a3305955e")},
				}
            }
        };

        m_assumeutxo_data = MapAssumeutxo{
         // TODO to be specified in a future patch.
        };

        // getchaintxstats 17280 0000000000000020d5e38b6aef5bc8e430029444d7977b46f710c7d281ef1281
        chainTxData = ChainTxData{
            1594482077, // * UNIX timestamp of last known number of transactions
            1292967,     // * total number of transactions between genesis and that timestamp
                        //   (the tx=... number in the SetBestChain debug.log lines)
            0.01        // * estimated number of transactions per second after that timestamp
        };
    }
};

/**
 * Testnet (v3): public test network which is reset from time to time.
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        strNetworkID = CBaseChainParams::TESTNET; 
        consensus.BARLEY_HARVEST_HEIGHT = 199010;
		consensus.BARLEY_HARVEST_HEIGHT2 = 199010;
        consensus.REDSEA_PARTING = 199010;
        consensus.BABYLON_FALLING_HEIGHT = 205465;

        consensus.BABYLON_FALLING_TIME = 1715396772; // May 10th 2024

        consensus.EXODUS_HEIGHT = 183000;
        consensus.REDSEA_HEIGHT = 183000;
        consensus.LATTER_RAIN_HEIGHT = 192200;

        
        consensus.nSubsidyHalvingInterval =  365 * 205;
        consensus.BIP16Height = 0;
        consensus.nMasternodePaymentsStartBlock = 2001; // not true, but it's ok as long as it's less then nMasternodePaymentsIncreaseBlock
        consensus.nMasternodePaymentsIncreaseBlock = 2001;
        consensus.nMasternodePaymentsIncreasePeriod = 205*30;
        consensus.nInstantSendConfirmationsRequired = 1;
        consensus.nInstantSendKeepLock = 6;
        consensus.nBudgetPaymentsStartBlock = 2002;
        consensus.nBudgetPaymentsCycleBlocks = 50;
        consensus.nBudgetPaymentsWindowBlocks = 10;
        consensus.nSuperblockStartBlock = 4007; // NOTE: Should satisfy nSuperblockStartBlock > nBudgetPeymentsStartBlock
        consensus.nSuperblockStartHash = uint256(); // do not check this on testnet
        consensus.nSuperblockCycle = 205*7; // Weekly
        consensus.nGovernanceMinQuorum = 1;
        consensus.nGovernanceFilterElements = 500;
        consensus.nMasternodeMinimumConfirmations = 1;
        consensus.BIP34Hash = uint256S("0x0");
        consensus.BIP65Height = 2431; // 
        consensus.BIP66Height = 2075; // 
        consensus.CSVHeight = 72860;      // 
		consensus.FoundationAddress = "yTrEKf8XQ7y7tychC2gWuGw1hsLqBybnEN";
		consensus.BurnAddress = "yLKSrCjLQFsfVgX8RjdctZ797d54atPjnV";
	
        consensus.DIP0001Height = 7000;
        consensus.BIP34Height = 72860;
        consensus.DIP0003Height = 72860;
        consensus.DIP0003EnforcementHeight = 72860;
        consensus.DIP0003EnforcementHash = uint256S("0x0");
		consensus.LLMQHeight = 72860;
        consensus.DIP0008Height = 74000; // 
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // 
        consensus.nPowTargetTimespan = 24 * 60 * 60; // BiblePay: 1 day
        consensus.nPowTargetSpacing = 3 * 60; // BiblePay: 3mins
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = false;
        consensus.nPowKGWHeight = 0; // nPowKGWHeight >= nPowDGWHeight means "no KGW"
        consensus.nPowDGWHeight = 1; // 
        consensus.nRuleChangeActivationThreshold = 1512; // 75% for testchains
        consensus.nMinerConfirmationWindow = 205; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        consensus.vDeployments[Consensus::DEPLOYMENT_V20].bit = 9;
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nStartTime = 1693526400;     // Friday, September 1, 2023 0:00:00
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nWindowSize = 100;
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nThresholdStart = 80;         // 80% of 100
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nThresholdMin = 60;           // 60% of 100
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nFalloffCoeff = 5;            // this corresponds to 10 periods

        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].bit = 10;
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nStartTime = 1693526400;   // Friday, September 1, 2023 0:00:00
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nWindowSize = 100;
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nThresholdStart = 80;       // 80% of 100
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nThresholdMin = 60;         // 60% of 100
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nFalloffCoeff = 5;          // this corresponds to 10 periods
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].useEHF = true;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x000000000000000000000000000000000000000000000000000000000000000"); // 4000

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x0000000005ae4db9746d6cad8e0ccebdef1e05afec9c40809f31457fdaf7d843"); // 95930

        pchMessageStart[0] = 0xce;
        pchMessageStart[1] = 0xe2;
        pchMessageStart[2] = 0xca;
        pchMessageStart[3] = 0xff;
        nDefaultPort = 40001;
        nPruneAfterHeight = 0;

        genesis = CreateGenesisBlock(1496347850, 15, 0x207fffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x122f423f0912850a871c58f1533dd80be62154bb0c56dfb8cb9ae2b957d1ac10"));
        assert(genesis.hashMerkleRoot == uint256S("0x02b05f3b8a7168bcf83b888e0092446b248b2641bd9844b5d12a45eaa2765725"));

        vFixedSeeds.clear();
        vFixedSeeds = std::vector<uint8_t>(std::begin(chainparams_seed_test), std::end(chainparams_seed_test));

        vSeeds.clear();
        // nodes with support for servicebits filtering should be at the top
        vSeeds.emplace_back("testnet-seed.biblepaydot.io"); // Just a static list of stable node(s), only supports x9

        // Testnet BiblePay addresses start with 'y'
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,140);
        // Testnet BiblePay script addresses start with '8' or '9'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,19);
        // Testnet private keys start with '9' or 'c' (Bitcoin defaults)
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        // Testnet BiblePay BIP32 pubkeys start with 'tpub' (Bitcoin defaults)
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        // Testnet BiblePay BIP32 prvkeys start with 'tprv' (Bitcoin defaults)
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        // Testnet BiblePay BIP44 coin type is '1' (All coin's testnet default)
        nExtCoinType = 1;

        // long living quorum params
        
        AddLLMQ(Consensus::LLMQType::LLMQ_400_85);
        consensus.llmqTypeChainLocks = Consensus::LLMQType::LLMQ_400_85;
        consensus.llmqTypeDIP0024InstantSend = Consensus::LLMQType::LLMQ_400_85;
        consensus.llmqTypePlatform = Consensus::LLMQType::LLMQ_400_85;
        consensus.llmqTypeMnhf = Consensus::LLMQType::LLMQ_400_85;

        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        fRequireRoutableExternalIP = true;
        m_is_test_chain = true;
        fAllowMultipleAddressesFromGroup = false;
        fAllowMultiplePorts = true;
        nLLMQConnectionRetryTimeout = 30;
        m_is_mockable_chain = false;

        nPoolMinParticipants = 2;
        nPoolMaxParticipants = 20;
        nFulfilledRequestExpireTime = 5*60; // fulfilled requests expire in 5 minutes

        vSporkAddresses = {"yTrEKf8XQ7y7tychC2gWuGw1hsLqBybnEN"};
		nMinSporkKeys = 1;
        //fBIP9CheckMasternodesUpgraded = true;

   		checkpointData = (CCheckpointData) 
		{
			{
				{1,     uint256S("0xde31388eddd9e0ea515353628a0ca1f167466f09c45b10354de10459c7c018f2")},
				{5000,  uint256S("0xc63bfaaddcb4714c594b701ae8bd6320cef3221df7b33d781d0400ca5a1348b9")},
				{18610, uint256S("0x778067c8239f02166519359a82d2e0b5bea19a94df7344442dad9fb65ff013f8")},
				{72000, uint256S("0xaa706f900264c8a459afb01ca296055c41b3173d8c02b8e6d388c4487ad6552f")},
			}
		};

        m_assumeutxo_data = MapAssumeutxo{
            // TODO to be specified in a future patch.
        };

        // getchaintxstats 17280 0000000386cf5061ea16404c66deb83eb67892fa4f79b9e58e5eaab097ec2bd6
        chainTxData = ChainTxData{
            1574877650, // * UNIX timestamp of last known number of transactions
            29477,       // * total number of transactions between genesis and that timestamp
                        //   (the tx=... number in the SetBestChain debug.log lines)
            0.01        // * estimated number of transactions per second after that timestamp
        };
    }
};

/**
 * Devnet: The Development network intended for developers use.
 */
class CDevNetParams : public CChainParams {
public:
    explicit CDevNetParams(const ArgsManager& args) {
        strNetworkID = CBaseChainParams::DEVNET;
        consensus.nSubsidyHalvingInterval = 210240;
        consensus.BIP16Height = 0;
        consensus.nMasternodePaymentsStartBlock = 4010; // not true, but it's ok as long as it's less then nMasternodePaymentsIncreaseBlock
        consensus.nMasternodePaymentsIncreaseBlock = 4030;
        consensus.nMasternodePaymentsIncreasePeriod = 10;
        consensus.nInstantSendConfirmationsRequired = 2;
        consensus.nInstantSendKeepLock = 6;
        consensus.nBudgetPaymentsStartBlock = 4100;
        consensus.nBudgetPaymentsCycleBlocks = 50;
        consensus.nBudgetPaymentsWindowBlocks = 10;
        consensus.nSuperblockStartBlock = 4200; // NOTE: Should satisfy nSuperblockStartBlock > nBudgetPeymentsStartBlock
        consensus.nSuperblockStartHash = uint256(); // do not check this on devnet
        consensus.nSuperblockCycle = 24; // Superblocks can be issued hourly on devnet
        consensus.nSuperblockMaturityWindow = 8;
        consensus.nGovernanceMinQuorum = 1;
        consensus.nGovernanceFilterElements = 500;
        consensus.nMasternodeMinimumConfirmations = 1;
        consensus.BABYLON_FALLING_HEIGHT = 1;
        
        consensus.BIP34Height = 1; // BIP34 activated immediately on devnet
        consensus.BIP65Height = 1; // BIP65 activated immediately on devnet
        consensus.BIP66Height = 1; // BIP66 activated immediately on devnet
        consensus.BIP147Height = 1; // BIP147 activated immediately on devnet
        consensus.CSVHeight = 1; // BIP68 activated immediately on devnet
        consensus.DIP0001Height = 2; // DIP0001 activated immediately on devnet
        consensus.DIP0003Height = 2; // DIP0003 activated immediately on devnet
        consensus.DIP0003EnforcementHeight = 2; // DIP0003 activated immediately on devnet
        consensus.DIP0003EnforcementHash = uint256();
        consensus.DIP0008Height = 2; // DIP0008 activated immediately on devnet
        consensus.BRRHeight = 300;
        consensus.DIP0020Height = 300;
        consensus.DIP0024Height = 300;
        consensus.DIP0024QuorumsHeight = 300;
        consensus.V19Height = 300;
        consensus.MinBIP9WarningHeight = 300 + 2016; // v19 activation height + miner confirmation window
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // ~uint256(0) >> 1
        consensus.nPowTargetTimespan = 24 * 60 * 60; // BiblePay: 1 day
        consensus.nPowTargetSpacing = 2.5 * 60; // BiblePay: 2.5 minutes
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = false;
        consensus.nPowKGWHeight = 1; // nPowKGWHeight >= nPowDGWHeight means "no KGW"
        consensus.nPowDGWHeight = 1;
        consensus.nRuleChangeActivationThreshold = 1512; // 75% for testchains
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        consensus.vDeployments[Consensus::DEPLOYMENT_V20].bit = 9;
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nStartTime = 1661990400; // Sep 1st, 2022
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nWindowSize = 120;
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nThresholdStart = 80; // 80% of 100
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nThresholdMin = 60;   // 60% of 100
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nFalloffCoeff = 5;     // this corresponds to 10 periods

        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].bit = 10;
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nStartTime = 1661990400; // Sep 1st, 2022
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nWindowSize = 120;
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nThresholdStart = 80; // 80% of 100
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nThresholdMin = 60;   // 60% of 100
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nFalloffCoeff = 5;     // this corresponds to 10 periods
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].useEHF = true;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x000000000000000000000000000000000000000000000000000000000000000");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x000000000000000000000000000000000000000000000000000000000000000");

        pchMessageStart[0] = 0xe2;
        pchMessageStart[1] = 0xca;
        pchMessageStart[2] = 0xff;
        pchMessageStart[3] = 0xce;
        nDefaultPort = 40002;
        nDefaultPlatformP2PPort = 22100;
        nDefaultPlatformHTTPPort = 22101;
        nPruneAfterHeight = 0;
        m_assumed_blockchain_size = 0;
        m_assumed_chain_state_size = 0;

        UpdateDevnetSubsidyAndDiffParametersFromArgs(args);
        genesis = CreateGenesisBlock(1417713337, 1096447, 0x207fffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x8b2dec091b22b56b47c7e857bb54a9fe0efd379f0d400b7c69260c7e7e93fdcc"));
        assert(genesis.hashMerkleRoot == uint256S("0x02b05f3b8a7168bcf83b888e0092446b248b2641bd9844b5d12a45eaa2765725"));

        devnetGenesis = FindDevNetGenesisBlock(genesis, 50 * COIN);
        consensus.hashDevnetGenesisBlock = devnetGenesis.GetHash();

        vFixedSeeds.clear();
        vSeeds.clear();
        //vSeeds.push_back(CDNSSeedData("biblepayevo.org",  "devnet-seed.biblepayevo.org"));

        // Testnet BiblePay addresses start with 'y'
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,140);
        // Testnet BiblePay script addresses start with '8' or '9'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,19);
        // Testnet private keys start with '9' or 'c' (Bitcoin defaults)
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        // Testnet BiblePay BIP32 pubkeys start with 'tpub' (Bitcoin defaults)
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        // Testnet BiblePay BIP32 prvkeys start with 'tprv' (Bitcoin defaults)
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        // Testnet BiblePay BIP44 coin type is '1' (All coin's testnet default)
        nExtCoinType = 1;

        // long living quorum params
        AddLLMQ(Consensus::LLMQType::LLMQ_TEST);
        consensus.llmqTypeChainLocks = Consensus::LLMQType::LLMQ_TEST;
        consensus.llmqTypeDIP0024InstantSend = Consensus::LLMQType::LLMQ_TEST;
        consensus.llmqTypePlatform = Consensus::LLMQType::LLMQ_TEST;


        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        fRequireRoutableExternalIP = true;
        m_is_test_chain = true;
        fAllowMultipleAddressesFromGroup = true;
        fAllowMultiplePorts = true;
        nLLMQConnectionRetryTimeout = 60;
        m_is_mockable_chain = false;

        nPoolMinParticipants = 2;
        nPoolMaxParticipants = 20;
        nFulfilledRequestExpireTime = 5*60; // fulfilled requests expire in 5 minutes

        vSporkAddresses = {"yjPtiKh2uwk3bDutTEA2q9mCtXyiZRWn55"};
        nMinSporkKeys = 1;

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

    /**
     * Allows modifying the subsidy and difficulty devnet parameters.
     */
    void UpdateDevnetSubsidyAndDiffParameters(int nMinimumDifficultyBlocks, int nHighSubsidyBlocks, int nHighSubsidyFactor)
    {
        consensus.nMinimumDifficultyBlocks = nMinimumDifficultyBlocks;
        consensus.nHighSubsidyBlocks = nHighSubsidyBlocks;
        consensus.nHighSubsidyFactor = nHighSubsidyFactor;
    }
    void UpdateDevnetSubsidyAndDiffParametersFromArgs(const ArgsManager& args);

    /**
     * Allows modifying the LLMQ type for ChainLocks.
     */
    void UpdateDevnetLLMQChainLocks(Consensus::LLMQType llmqType)
    {
        consensus.llmqTypeChainLocks = llmqType;
    }
    void UpdateDevnetLLMQChainLocksFromArgs(const ArgsManager& args);

    /**
     * Allows modifying the LLMQ type for InstantSend (DIP0024).
     */
    void UpdateDevnetLLMQDIP0024InstantSend(Consensus::LLMQType llmqType)
    {
        consensus.llmqTypeDIP0024InstantSend = llmqType;
    }

    /**
     * Allows modifying the LLMQ type for Platform.
     */
    void UpdateDevnetLLMQPlatform(Consensus::LLMQType llmqType)
    {
        consensus.llmqTypePlatform = llmqType;
    }

    /**
     * Allows modifying the LLMQ type for Mnhf.
     */
    void UpdateDevnetLLMQMnhf(Consensus::LLMQType llmqType)
    {
        consensus.llmqTypeMnhf = llmqType;
    }

    /**
     * Allows modifying PowTargetSpacing
     */
    void UpdateDevnetPowTargetSpacing(int64_t nPowTargetSpacing)
    {
        consensus.nPowTargetSpacing = nPowTargetSpacing;
    }

    /**
     * Allows modifying parameters of the devnet LLMQ
     */
    void UpdateLLMQDevnetParameters(int size, int threshold)
    {
        auto params = ranges::find_if(consensus.llmqs, [](const auto& llmq){ return llmq.type == Consensus::LLMQType::LLMQ_DEVNET;});
        assert(params != consensus.llmqs.end());
        params->size = size;
        params->minSize = threshold;
        params->threshold = threshold;
        params->dkgBadVotesThreshold = threshold;
    }
    void UpdateLLMQDevnetParametersFromArgs(const ArgsManager& args);
    void UpdateDevnetLLMQInstantSendFromArgs(const ArgsManager& args);
    void UpdateDevnetLLMQInstantSendDIP0024FromArgs(const ArgsManager& args);
    void UpdateDevnetLLMQPlatformFromArgs(const ArgsManager& args);
    void UpdateDevnetLLMQMnhfFromArgs(const ArgsManager& args);
    void UpdateDevnetPowTargetSpacingFromArgs(const ArgsManager& args);
};

/**
 * Regression test: intended for private networks only. Has minimal difficulty to ensure that
 * blocks can be found instantly.
 */
class CRegTestParams : public CChainParams {
public:
    explicit CRegTestParams(const ArgsManager& args) {
        strNetworkID =  CBaseChainParams::REGTEST;
        consensus.nSubsidyHalvingInterval = 150;
        consensus.BIP16Height = 0; // always enforce P2SH BIP16 on regtest
        consensus.nMasternodePaymentsStartBlock = 240;
        consensus.nMasternodePaymentsIncreaseBlock = 350;
        consensus.nMasternodePaymentsIncreasePeriod = 10;
        consensus.nInstantSendConfirmationsRequired = 2;
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
        consensus.BIP65Height = 1351; // BIP65 activated on regtest (Used in functional tests)
        consensus.BIP66Height = 1251; // BIP66 activated on regtest (Used in functional tests)
        consensus.BIP147Height = 432; // BIP147 activated on regtest (Used in functional tests)
        consensus.CSVHeight = 432; // CSV activated on regtest (Used in rpc activation tests)
        consensus.DIP0001Height = 2000;
        consensus.DIP0003Height = 432;
        consensus.DIP0003EnforcementHeight = 500;
        consensus.DIP0003EnforcementHash = uint256();
        consensus.DIP0008Height = 432;
        consensus.BRRHeight = 2500; // see block_reward_reallocation_tests
        consensus.DIP0020Height = 300;
        consensus.DIP0024Height = 900;
        consensus.DIP0024QuorumsHeight = 900;
        consensus.V19Height = 900;
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // ~uint256(0) >> 1
        consensus.nPowTargetTimespan = 24 * 60 * 60; // BiblePay: 1 day
        consensus.nPowTargetSpacing = 2.5 * 60; // BiblePay: 2.5 minutes
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = true;
        consensus.nPowKGWHeight = 15200; // same as mainnet
        consensus.nPowDGWHeight = 34140; // same as mainnet
        consensus.nRuleChangeActivationThreshold = 108; // 75% for testchains
        consensus.nMinerConfirmationWindow = 144; // Faster than normal for regtest (144 instead of 2016)
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;

        consensus.vDeployments[Consensus::DEPLOYMENT_V20].bit = 9;
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nWindowSize = 400;
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nThresholdStart = 384; // 80% of 480
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nThresholdMin = 288;   // 60% of 480
        consensus.vDeployments[Consensus::DEPLOYMENT_V20].nFalloffCoeff = 5;     // this corresponds to 10 periods

        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].bit = 10;
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nWindowSize = 12;
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nThresholdStart = 9; // 80% of 12
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nThresholdMin = 7;   // 60% of 7
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].nFalloffCoeff = 5;     // this corresponds to 10 periods
        consensus.vDeployments[Consensus::DEPLOYMENT_MN_RR].useEHF = true;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x00");

        pchMessageStart[0] = 0xfc;
        pchMessageStart[1] = 0xc1;
        pchMessageStart[2] = 0xb7;
        pchMessageStart[3] = 0xdc;
        nDefaultPort = 19899;
        nDefaultPlatformP2PPort = 22200;
        nDefaultPlatformHTTPPort = 22201;
        nPruneAfterHeight = gArgs.GetBoolArg("-fastprune", false) ? 100 : 1000;
        m_assumed_blockchain_size = 0;
        m_assumed_chain_state_size = 0;

		genesis = CreateGenesisBlock(1496347864, 18, 0x207fffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x2a2a5f2fd4648f7d72b41f076567d143ed67db051dfda746c9cfba616c6a49d6"));
        assert(genesis.hashMerkleRoot == uint256S("0x02b05f3b8a7168bcf83b888e0092446b248b2641bd9844b5d12a45eaa2765725"));

        vFixedSeeds.clear(); //!< Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();      //!< Regtest mode doesn't have any DNS seeds.

        fDefaultConsistencyChecks = true;
        fRequireStandard = false;
        fRequireRoutableExternalIP = false;
        m_is_test_chain = true;
        fAllowMultipleAddressesFromGroup = true;
        fAllowMultiplePorts = true;
        nLLMQConnectionRetryTimeout = 1; // must be lower then the LLMQ signing session timeout so that tests have control over failing behavior
        m_is_mockable_chain = true;

        nFulfilledRequestExpireTime = 5*60; // fulfilled requests expire in 5 minutes
        nPoolMinParticipants = 2;
        nPoolMaxParticipants = 20;

        // privKey: cP4EKFyJsHT39LDqgdcB43Y3YXjNyjb5Fuas1GQSeAtjnZWmZEQK
        vSporkAddresses = {"yj949n1UH6fDhw6HtVE5VMj2iSTaSWBMcW"};
        nMinSporkKeys = 1;

        checkpointData = {
            {
                {0, uint256S("0x000008ca1832a4baf228eb1553c03d3a2c8e02399550dd6ea8d65cec3ef23d2e")},
            }
        };

        m_assumeutxo_data = MapAssumeutxo{
            {
                110,
                {AssumeutxoHash{uint256S("0x9b2a277a3e3b979f1a539d57e949495d7f8247312dbc32bce6619128c192b44b")}, 110},
            },
            {
                210,
                {AssumeutxoHash{uint256S("0xd4c97d32882583b057efc3dce673e44204851435e6ffcef20346e69cddc7c91e")}, 210},
            },
        };

        chainTxData = ChainTxData{
            0,
            0,
            0
        };

        // Regtest BiblePay addresses start with 'y'
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,140);
        // Regtest BiblePay script addresses start with '8' or '9'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,19);
        // Regtest private keys start with '9' or 'c' (Bitcoin defaults)
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        // Regtest BiblePay BIP32 pubkeys start with 'tpub' (Bitcoin defaults)
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        // Regtest BiblePay BIP32 prvkeys start with 'tprv' (Bitcoin defaults)
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        // Regtest BiblePay BIP44 coin type is '1' (All coin's testnet default)
        nExtCoinType = 1;

        // long living quorum params
        AddLLMQ(Consensus::LLMQType::LLMQ_TEST);
       
        consensus.llmqTypeChainLocks = Consensus::LLMQType::LLMQ_TEST;           
        consensus.llmqTypeDIP0024InstantSend = Consensus::LLMQType::LLMQ_TEST;
        consensus.llmqTypePlatform = Consensus::LLMQType::LLMQ_TEST;
    }

    /**
     * Allows modifying the Version Bits regtest parameters.
     */
    void UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout, int64_t nWindowSize, int64_t nThresholdStart, int64_t nThresholdMin, int64_t nFalloffCoeff, int64_t nUseEHF)
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
        if (nUseEHF != -1) {
            consensus.vDeployments[d].useEHF = nUseEHF > 0;
        }
    }
    void UpdateActivationParametersFromArgs(const ArgsManager& args);

    /**
     * Allows modifying the DIP3 activation and enforcement height
     */
    void UpdateDIP3Parameters(int nActivationHeight, int nEnforcementHeight)
    {
        consensus.DIP0003Height = nActivationHeight;
        consensus.DIP0003EnforcementHeight = nEnforcementHeight;
    }
    void UpdateDIP3ParametersFromArgs(const ArgsManager& args);

    /**
     * Allows modifying the DIP8 activation height
     */
    void UpdateDIP8Parameters(int nActivationHeight)
    {
        consensus.DIP0008Height = nActivationHeight;
    }
    void UpdateDIP8ParametersFromArgs(const ArgsManager& args);

    /**
     * Allows modifying the budget regtest parameters.
     */
    void UpdateBudgetParameters(int nMasternodePaymentsStartBlock, int nBudgetPaymentsStartBlock, int nSuperblockStartBlock)
    {
        consensus.nMasternodePaymentsStartBlock = nMasternodePaymentsStartBlock;
        consensus.nBudgetPaymentsStartBlock = nBudgetPaymentsStartBlock;
        consensus.nSuperblockStartBlock = nSuperblockStartBlock;
    }
    void UpdateBudgetParametersFromArgs(const ArgsManager& args);

    /**
     * Allows modifying parameters of the test LLMQ
     */
    void UpdateLLMQTestParameters(int size, int threshold, const Consensus::LLMQType llmqType)
    {
        auto params = ranges::find_if(consensus.llmqs, [llmqType](const auto& llmq){ return llmq.type == llmqType;});
        assert(params != consensus.llmqs.end());
        params->size = size;
        params->minSize = threshold;
        params->threshold = threshold;
        params->dkgBadVotesThreshold = threshold;
    }

    /**
     * Allows modifying the LLMQ type for InstantSend (DIP0024).
     */
    void UpdateLLMQDIP0024InstantSend(Consensus::LLMQType llmqType)
    {
        consensus.llmqTypeDIP0024InstantSend = llmqType;
    }

    void UpdateLLMQTestParametersFromArgs(const ArgsManager& args, const Consensus::LLMQType llmqType);
    void UpdateLLMQInstantSendDIP0024FromArgs(const ArgsManager& args);
};

void CRegTestParams::UpdateActivationParametersFromArgs(const ArgsManager& args)
{
    if (!args.IsArgSet("-vbparams")) return;

    for (const std::string& strDeployment : args.GetArgs("-vbparams")) {
        std::vector<std::string> vDeploymentParams = SplitString(strDeployment, ':');
        if (vDeploymentParams.size() != 3 && vDeploymentParams.size() != 5 && vDeploymentParams.size() != 8) {
            throw std::runtime_error("Version bits parameters malformed, expecting "
                    "<deployment>:<start>:<end> or "
                    "<deployment>:<start>:<end>:<window>:<threshold> or "
                    "<deployment>:<start>:<end>:<window>:<thresholdstart>:<thresholdmin>:<falloffcoeff>:<useehf>");
        }
        int64_t nStartTime, nTimeout, nWindowSize = -1, nThresholdStart = -1, nThresholdMin = -1, nFalloffCoeff = -1, nUseEHF = -1;
        if (!ParseInt64(vDeploymentParams[1], &nStartTime)) {
            throw std::runtime_error(strprintf("Invalid nStartTime (%s)", vDeploymentParams[1]));
        }
        if (!ParseInt64(vDeploymentParams[2], &nTimeout)) {
            throw std::runtime_error(strprintf("Invalid nTimeout (%s)", vDeploymentParams[2]));
        }
        if (vDeploymentParams.size() >= 5) {
            if (!ParseInt64(vDeploymentParams[3], &nWindowSize)) {
                throw std::runtime_error(strprintf("Invalid nWindowSize (%s)", vDeploymentParams[3]));
            }
            if (!ParseInt64(vDeploymentParams[4], &nThresholdStart)) {
                throw std::runtime_error(strprintf("Invalid nThresholdStart (%s)", vDeploymentParams[4]));
            }
        }
        if (vDeploymentParams.size() == 8) {
            if (!ParseInt64(vDeploymentParams[5], &nThresholdMin)) {
                throw std::runtime_error(strprintf("Invalid nThresholdMin (%s)", vDeploymentParams[5]));
            }
            if (!ParseInt64(vDeploymentParams[6], &nFalloffCoeff)) {
                throw std::runtime_error(strprintf("Invalid nFalloffCoeff (%s)", vDeploymentParams[6]));
            }
            if (!ParseInt64(vDeploymentParams[7], &nUseEHF)) {
                throw std::runtime_error(strprintf("Invalid nUseEHF (%s)", vDeploymentParams[7]));
            }
        }
        bool found = false;
        for (int j=0; j < (int)Consensus::MAX_VERSION_BITS_DEPLOYMENTS; ++j) {
            if (vDeploymentParams[0] == VersionBitsDeploymentInfo[j].name) {
                UpdateVersionBitsParameters(Consensus::DeploymentPos(j), nStartTime, nTimeout, nWindowSize, nThresholdStart, nThresholdMin, nFalloffCoeff, nUseEHF);
                found = true;
                LogPrintf("Setting version bits activation parameters for %s to start=%ld, timeout=%ld, window=%ld, thresholdstart=%ld, thresholdmin=%ld, falloffcoeff=%ld, useehf=%ld\n",
                          vDeploymentParams[0], nStartTime, nTimeout, nWindowSize, nThresholdStart, nThresholdMin, nFalloffCoeff, nUseEHF);
                break;
            }
        }
        if (!found) {
            throw std::runtime_error(strprintf("Invalid deployment (%s)", vDeploymentParams[0]));
        }
    }
}

void CRegTestParams::UpdateDIP3ParametersFromArgs(const ArgsManager& args)
{
    if (!args.IsArgSet("-dip3params")) return;

    std::string strParams = args.GetArg("-dip3params", "");
    std::vector<std::string> vParams = SplitString(strParams, ':');
    if (vParams.size() != 2) {
        throw std::runtime_error("DIP3 parameters malformed, expecting <activation>:<enforcement>");
    }
    int nDIP3ActivationHeight, nDIP3EnforcementHeight;
    if (!ParseInt32(vParams[0], &nDIP3ActivationHeight)) {
        throw std::runtime_error(strprintf("Invalid activation height (%s)", vParams[0]));
    }
    if (!ParseInt32(vParams[1], &nDIP3EnforcementHeight)) {
        throw std::runtime_error(strprintf("Invalid enforcement height (%s)", vParams[1]));
    }
    LogPrintf("Setting DIP3 parameters to activation=%ld, enforcement=%ld\n", nDIP3ActivationHeight, nDIP3EnforcementHeight);
    UpdateDIP3Parameters(nDIP3ActivationHeight, nDIP3EnforcementHeight);
}

void CRegTestParams::UpdateDIP8ParametersFromArgs(const ArgsManager& args)
{
    if (!args.IsArgSet("-dip8params")) return;

    std::string strParams = args.GetArg("-dip8params", "");
    std::vector<std::string> vParams = SplitString(strParams, ':');
    if (vParams.size() != 1) {
        throw std::runtime_error("DIP8 parameters malformed, expecting <activation>");
    }
    int nDIP8ActivationHeight;
    if (!ParseInt32(vParams[0], &nDIP8ActivationHeight)) {
        throw std::runtime_error(strprintf("Invalid activation height (%s)", vParams[0]));
    }
    LogPrintf("Setting DIP8 parameters to activation=%ld\n", nDIP8ActivationHeight);
    UpdateDIP8Parameters(nDIP8ActivationHeight);
}

void CRegTestParams::UpdateBudgetParametersFromArgs(const ArgsManager& args)
{
    if (!args.IsArgSet("-budgetparams")) return;

    std::string strParams = args.GetArg("-budgetparams", "");
    std::vector<std::string> vParams = SplitString(strParams, ':');
    if (vParams.size() != 3) {
        throw std::runtime_error("Budget parameters malformed, expecting <masternode>:<budget>:<superblock>");
    }
    int nMasternodePaymentsStartBlock, nBudgetPaymentsStartBlock, nSuperblockStartBlock;
    if (!ParseInt32(vParams[0], &nMasternodePaymentsStartBlock)) {
        throw std::runtime_error(strprintf("Invalid masternode start height (%s)", vParams[0]));
    }
    if (!ParseInt32(vParams[1], &nBudgetPaymentsStartBlock)) {
        throw std::runtime_error(strprintf("Invalid budget start block (%s)", vParams[1]));
    }
    if (!ParseInt32(vParams[2], &nSuperblockStartBlock)) {
        throw std::runtime_error(strprintf("Invalid superblock start height (%s)", vParams[2]));
    }
    LogPrintf("Setting budget parameters to masternode=%ld, budget=%ld, superblock=%ld\n", nMasternodePaymentsStartBlock, nBudgetPaymentsStartBlock, nSuperblockStartBlock);
    UpdateBudgetParameters(nMasternodePaymentsStartBlock, nBudgetPaymentsStartBlock, nSuperblockStartBlock);
}

void CRegTestParams::UpdateLLMQTestParametersFromArgs(const ArgsManager& args, const Consensus::LLMQType llmqType)
{
    assert(llmqType == Consensus::LLMQType::LLMQ_TEST || llmqType == Consensus::LLMQType::LLMQ_TEST_INSTANTSEND);

    std::string cmd_param{"-llmqtestparams"}, llmq_name{"LLMQ_TEST"};
    if (llmqType == Consensus::LLMQType::LLMQ_TEST_INSTANTSEND) {
        cmd_param = "-llmqtestinstantsendparams";
        llmq_name = "LLMQ_TEST_INSTANTSEND";
    }

    if (!args.IsArgSet(cmd_param)) return;

    std::string strParams = args.GetArg(cmd_param, "");
    std::vector<std::string> vParams = SplitString(strParams, ':');
    if (vParams.size() != 2) {
        throw std::runtime_error(strprintf("%s parameters malformed, expecting <size>:<threshold>", llmq_name));
    }
    int size, threshold;
    if (!ParseInt32(vParams[0], &size)) {
        throw std::runtime_error(strprintf("Invalid %s size (%s)", llmq_name, vParams[0]));
    }
    if (!ParseInt32(vParams[1], &threshold)) {
        throw std::runtime_error(strprintf("Invalid %s threshold (%s)", llmq_name, vParams[1]));
    }
    LogPrintf("Setting %s parameters to size=%ld, threshold=%ld\n", llmq_name, size, threshold);
    UpdateLLMQTestParameters(size, threshold, llmqType);
}

void CRegTestParams::UpdateLLMQInstantSendDIP0024FromArgs(const ArgsManager& args)
{
    if (!args.IsArgSet("-llmqtestinstantsenddip0024")) return;

    const auto& llmq_params_opt = GetLLMQ(consensus.llmqTypeDIP0024InstantSend);
    assert(llmq_params_opt.has_value());

    std::string strLLMQType = gArgs.GetArg("-llmqtestinstantsenddip0024", std::string(llmq_params_opt->name));

    Consensus::LLMQType llmqType = Consensus::LLMQType::LLMQ_NONE;
    for (const auto& params : consensus.llmqs) {
        if (params.name == strLLMQType) {
            llmqType = params.type;
        }
    }
    if (llmqType == Consensus::LLMQType::LLMQ_NONE) {
        throw std::runtime_error("Invalid LLMQ type specified for -llmqtestinstantsenddip0024.");
    }
    LogPrintf("Setting llmqtestinstantsenddip0024 to %ld\n", ToUnderlying(llmqType));
    UpdateLLMQDIP0024InstantSend(llmqType);
}

void CDevNetParams::UpdateDevnetSubsidyAndDiffParametersFromArgs(const ArgsManager& args)
{
    if (!args.IsArgSet("-minimumdifficultyblocks") && !args.IsArgSet("-highsubsidyblocks") && !args.IsArgSet("-highsubsidyfactor")) return;

    int nMinimumDifficultyBlocks = gArgs.GetArg("-minimumdifficultyblocks", consensus.nMinimumDifficultyBlocks);
    int nHighSubsidyBlocks = gArgs.GetArg("-highsubsidyblocks", consensus.nHighSubsidyBlocks);
    int nHighSubsidyFactor = gArgs.GetArg("-highsubsidyfactor", consensus.nHighSubsidyFactor);
    LogPrintf("Setting minimumdifficultyblocks=%ld, highsubsidyblocks=%ld, highsubsidyfactor=%ld\n", nMinimumDifficultyBlocks, nHighSubsidyBlocks, nHighSubsidyFactor);
    UpdateDevnetSubsidyAndDiffParameters(nMinimumDifficultyBlocks, nHighSubsidyBlocks, nHighSubsidyFactor);
}

void CDevNetParams::UpdateDevnetLLMQChainLocksFromArgs(const ArgsManager& args)
{
    if (!args.IsArgSet("-llmqchainlocks")) return;

    const auto& llmq_params_opt = GetLLMQ(consensus.llmqTypeChainLocks);
    assert(llmq_params_opt.has_value());

    std::string strLLMQType = gArgs.GetArg("-llmqchainlocks", std::string(llmq_params_opt->name));

    Consensus::LLMQType llmqType = Consensus::LLMQType::LLMQ_NONE;
    for (const auto& params : consensus.llmqs) {
        if (params.name == strLLMQType) {
            if (params.useRotation) {
                throw std::runtime_error("LLMQ type specified for -llmqchainlocks must NOT use rotation");
            }
            llmqType = params.type;
        }
    }
    if (llmqType == Consensus::LLMQType::LLMQ_NONE) {
        throw std::runtime_error("Invalid LLMQ type specified for -llmqchainlocks.");
    }
    LogPrintf("Setting llmqchainlocks to size=%ld\n", static_cast<uint8_t>(llmqType));
    UpdateDevnetLLMQChainLocks(llmqType);
}

void CDevNetParams::UpdateDevnetLLMQInstantSendDIP0024FromArgs(const ArgsManager& args)
{
    if (!args.IsArgSet("-llmqinstantsenddip0024")) return;

    const auto& llmq_params_opt = GetLLMQ(consensus.llmqTypeDIP0024InstantSend);
    assert(llmq_params_opt.has_value());

    std::string strLLMQType = gArgs.GetArg("-llmqinstantsenddip0024", std::string(llmq_params_opt->name));

    Consensus::LLMQType llmqType = Consensus::LLMQType::LLMQ_NONE;
    for (const auto& params : consensus.llmqs) {
        if (params.name == strLLMQType) {
            if (!params.useRotation) {
                throw std::runtime_error("LLMQ type specified for -llmqinstantsenddip0024 must use rotation");
            }
            llmqType = params.type;
        }
    }
    if (llmqType == Consensus::LLMQType::LLMQ_NONE) {
        throw std::runtime_error("Invalid LLMQ type specified for -llmqinstantsenddip0024.");
    }
    LogPrintf("Setting llmqinstantsenddip0024 to size=%ld\n", static_cast<uint8_t>(llmqType));
    UpdateDevnetLLMQDIP0024InstantSend(llmqType);
}

void CDevNetParams::UpdateDevnetLLMQPlatformFromArgs(const ArgsManager& args)
{
    if (!args.IsArgSet("-llmqplatform")) return;

    const auto& llmq_params_opt = GetLLMQ(consensus.llmqTypePlatform);
    assert(llmq_params_opt.has_value());

    std::string strLLMQType = gArgs.GetArg("-llmqplatform", std::string(llmq_params_opt->name));

    Consensus::LLMQType llmqType = Consensus::LLMQType::LLMQ_NONE;
    for (const auto& params : consensus.llmqs) {
        if (params.name == strLLMQType) {
            llmqType = params.type;
        }
    }
    if (llmqType == Consensus::LLMQType::LLMQ_NONE) {
        throw std::runtime_error("Invalid LLMQ type specified for -llmqplatform.");
    }
    LogPrintf("Setting llmqplatform to size=%ld\n", static_cast<uint8_t>(llmqType));
    UpdateDevnetLLMQPlatform(llmqType);
}

void CDevNetParams::UpdateDevnetLLMQMnhfFromArgs(const ArgsManager& args)
{
    if (!args.IsArgSet("-llmqmnhf")) return;

    const auto& llmq_params_opt = GetLLMQ(consensus.llmqTypeMnhf);
    assert(llmq_params_opt.has_value());

    std::string strLLMQType = gArgs.GetArg("-llmqmnhf", std::string(llmq_params_opt->name));

    Consensus::LLMQType llmqType = Consensus::LLMQType::LLMQ_NONE;
    for (const auto& params : consensus.llmqs) {
        if (params.name == strLLMQType) {
            llmqType = params.type;
        }
    }
    if (llmqType == Consensus::LLMQType::LLMQ_NONE) {
        throw std::runtime_error("Invalid LLMQ type specified for -llmqmnhf.");
    }
    LogPrintf("Setting llmqmnhf to size=%ld\n", static_cast<uint8_t>(llmqType));
    UpdateDevnetLLMQMnhf(llmqType);
}

void CDevNetParams::UpdateDevnetPowTargetSpacingFromArgs(const ArgsManager& args)
{
    if (!args.IsArgSet("-powtargetspacing")) return;

    std::string strPowTargetSpacing = gArgs.GetArg("-powtargetspacing", "");

    int64_t powTargetSpacing;
    if (!ParseInt64(strPowTargetSpacing, &powTargetSpacing)) {
        throw std::runtime_error(strprintf("Invalid parsing of powTargetSpacing (%s)", strPowTargetSpacing));
    }

    if (powTargetSpacing < 1) {
        throw std::runtime_error(strprintf("Invalid value of powTargetSpacing (%s)", strPowTargetSpacing));
    }

    LogPrintf("Setting powTargetSpacing to %ld\n", powTargetSpacing);
    UpdateDevnetPowTargetSpacing(powTargetSpacing);
}

void CDevNetParams::UpdateLLMQDevnetParametersFromArgs(const ArgsManager& args)
{
    if (!args.IsArgSet("-llmqdevnetparams")) return;

    std::string strParams = args.GetArg("-llmqdevnetparams", "");
    std::vector<std::string> vParams = SplitString(strParams, ':');
    if (vParams.size() != 2) {
        throw std::runtime_error("LLMQ_DEVNET parameters malformed, expecting <size>:<threshold>");
    }
    int size, threshold;
    if (!ParseInt32(vParams[0], &size)) {
        throw std::runtime_error(strprintf("Invalid LLMQ_DEVNET size (%s)", vParams[0]));
    }
    if (!ParseInt32(vParams[1], &threshold)) {
        throw std::runtime_error(strprintf("Invalid LLMQ_DEVNET threshold (%s)", vParams[1]));
    }
    LogPrintf("Setting LLMQ_DEVNET parameters to size=%ld, threshold=%ld\n", size, threshold);
    UpdateLLMQDevnetParameters(size, threshold);
}

static std::unique_ptr<const CChainParams> globalChainParams;

const CChainParams &Params() {
    assert(globalChainParams);
    return *globalChainParams;
}

std::unique_ptr<const CChainParams> CreateChainParams(const ArgsManager& args, const std::string& chain)
{
    if (chain == CBaseChainParams::MAIN) {
        return std::unique_ptr<CChainParams>(new CMainParams());
    } else if (chain == CBaseChainParams::TESTNET) {
        return std::unique_ptr<CChainParams>(new CTestNetParams());
    } else if (chain == CBaseChainParams::DEVNET) {
        return std::unique_ptr<CChainParams>(new CDevNetParams(args));
    } else if (chain == CBaseChainParams::REGTEST) {
        return std::unique_ptr<CChainParams>(new CRegTestParams(args));
    }
    throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectParams(const std::string& network)
{
    SelectBaseParams(network);
    globalChainParams = CreateChainParams(gArgs, network);
}
