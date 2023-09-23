// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2014-2021 The DÃSH Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <miner.h>

#include <amount.h>
#include <chain.h>
#include <chainparams.h>
#include <coins.h>
#include <consensus/consensus.h>
#include <consensus/tx_verify.h>
#include <consensus/merkle.h>
#include <consensus/validation.h>
#include <hash.h>
#include <init.h>
#include <net.h>
#include <policy/feerate.h>
#include <policy/policy.h>
#include <pow.h>
#include <primitives/transaction.h>
#include <script/standard.h>
#include <timedata.h>
#include <util.h>
#include <utilmoneystr.h>
#include <masternode/masternode-payments.h>
#include <masternode/masternode-sync.h>
#include <validationinterface.h>

#include <evo/specialtx.h>
#include <evo/cbtx.h>
#include <evo/simplifiedmns.h>
#include <evo/deterministicmns.h>

#include <llmq/quorums_blockprocessor.h>
#include <llmq/quorums_chainlocks.h>

#include <algorithm>
#include <queue>
#include <utility>
#include <randomx_bbp.h>
#include <rpcpog.h>


// Unconfirmed transactions in the memory pool often depend on other
// transactions in the memory pool. When we select transactions from the
// pool, we select by highest fee rate of a transaction combined with all
// its ancestors.

uint64_t nLastBlockTx = 0;
uint64_t nLastBlockSize = 0;

int64_t UpdateTime(CBlockHeader* pblock, const Consensus::Params& consensusParams, const CBlockIndex* pindexPrev)
{
    int64_t nOldTime = pblock->nTime;
    int64_t nNewTime = std::max(pindexPrev->GetMedianTimePast()+1, GetAdjustedTime());

    if (nOldTime < nNewTime)
        pblock->nTime = nNewTime;

    // Updating time can change work required on testnet:
    if (consensusParams.fPowAllowMinDifficultyBlocks)
        pblock->nBits = GetNextWorkRequired(pindexPrev, pblock, consensusParams);

    return nNewTime - nOldTime;
}

BlockAssembler::Options::Options() {
    blockMinFeeRate = CFeeRate(DEFAULT_BLOCK_MIN_TX_FEE);
    nBlockMaxSize = DEFAULT_BLOCK_MAX_SIZE;
}

BlockAssembler::BlockAssembler(const CChainParams& params, const Options& options) : chainparams(params)
{
    blockMinFeeRate = options.blockMinFeeRate;
    // Limit size to between 1K and MaxBlockSize()-1K for sanity:
    nBlockMaxSize = std::max((unsigned int)1000, std::min((unsigned int)(MaxBlockSize(fDIP0001ActiveAtTip) - 1000), (unsigned int)options.nBlockMaxSize));
}

static BlockAssembler::Options DefaultOptions(const CChainParams& params)
{
    // Block resource limits
    BlockAssembler::Options options;
    options.nBlockMaxSize = DEFAULT_BLOCK_MAX_SIZE;
    if (gArgs.IsArgSet("-blockmaxsize")) {
        options.nBlockMaxSize = gArgs.GetArg("-blockmaxsize", DEFAULT_BLOCK_MAX_SIZE);
    }
    CAmount n = 0;
    if (gArgs.IsArgSet("-blockmintxfee") && ParseMoney(gArgs.GetArg("-blockmintxfee", ""), n)) {
        options.blockMinFeeRate = CFeeRate(n);
    } else {
        options.blockMinFeeRate = CFeeRate(DEFAULT_BLOCK_MIN_TX_FEE);
    }
    return options;
}

BlockAssembler::BlockAssembler(const CChainParams& params) : BlockAssembler(params, DefaultOptions(params)) {}

void BlockAssembler::resetBlock()
{
    inBlock.clear();

    // Reserve space for coinbase tx
    nBlockSize = 1000;
    nBlockSigOps = 100;

    // These counters do not include coinbase tx
    nBlockTx = 0;
    nFees = 0;
}

std::unique_ptr<CBlockTemplate> BlockAssembler::CreateNewBlock(const CScript& scriptPubKeyIn, std::string sPoolMiningPublicKey, uint256 uRandomXKey, std::vector<unsigned char> vRandomXHeader)
{
    int64_t nTimeStart = GetTimeMicros();

    resetBlock();

    pblocktemplate.reset(new CBlockTemplate());

    if(!pblocktemplate.get())
        return nullptr;
    pblock = &pblocktemplate->block; // pointer for convenience

    // Add dummy coinbase tx as first transaction
    pblock->vtx.emplace_back();
    pblocktemplate->vTxFees.push_back(-1); // updated at end
    pblocktemplate->vTxSigOps.push_back(-1); // updated at end

    LOCK2(cs_main, mempool.cs);

    CBlockIndex* pindexPrev = chainActive.Tip();
    assert(pindexPrev != nullptr);
    nHeight = pindexPrev->nHeight + 1;

    bool fDIP0003Active_context = nHeight >= chainparams.GetConsensus().DIP0003Height;
    bool fDIP0008Active_context = nHeight >= chainparams.GetConsensus().DIP0008Height;

    pblock->nVersion = ComputeBlockVersion(pindexPrev, chainparams.GetConsensus(), chainparams.BIP9CheckMasternodesUpgraded());
	LogPrintf("\nCreating block with version %f ", pblock->nVersion);

    // -regtest only: allow overriding block.nVersion with
    // -blockversion=N to test forking scenarios
    if (chainparams.MineBlocksOnDemand())
        pblock->nVersion = gArgs.GetArg("-blockversion", pblock->nVersion);

    pblock->nTime = GetAdjustedTime();
	// RandomX Support
	if (nHeight >= chainparams.GetConsensus().RANDOMX_HEIGHT)
	{
		pblock->RandomXKey  = uRandomXKey;
		pblock->RandomXData = "<rxheader>" + HexStr(vRandomXHeader.begin(), vRandomXHeader.end()) + "</rxheader>";
	}

    const int64_t nMedianTimePast = pindexPrev->GetMedianTimePast();

    nLockTimeCutoff = (STANDARD_LOCKTIME_VERIFY_FLAGS & LOCKTIME_MEDIAN_TIME_PAST)
                       ? nMedianTimePast
                       : pblock->GetBlockTime();

    if (fDIP0003Active_context) {
        for (const Consensus::LLMQType& type : llmq::CLLMQUtils::GetEnabledQuorumTypes(pindexPrev)) {
            CTransactionRef qcTx;
            if (llmq::quorumBlockProcessor->GetMinableCommitmentTx(type, nHeight, qcTx)) {
                pblock->vtx.emplace_back(qcTx);
                pblocktemplate->vTxFees.emplace_back(0);
                pblocktemplate->vTxSigOps.emplace_back(0);
                nBlockSize += qcTx->GetTotalSize();
                ++nBlockTx;
            }
        }
    }

    int nPackagesSelected = 0;
    int nDescendantsUpdated = 0;
    addPackageTxs(nPackagesSelected, nDescendantsUpdated);

    int64_t nTime1 = GetTimeMicros();

    nLastBlockTx = nBlockTx;
    nLastBlockSize = nBlockSize;
    LogPrintf("CreateNewBlock(): total size %u txs: %u fees: %ld sigops %d\n", nBlockSize, nBlockTx, nFees, nBlockSigOps);

    // Create coinbase transaction.
    CMutableTransaction coinbaseTx;
    coinbaseTx.vin.resize(1);
    coinbaseTx.vin[0].prevout.SetNull();
    coinbaseTx.vout.resize(1);
    coinbaseTx.vout[0].scriptPubKey = scriptPubKeyIn;

    // POVS Support
    if (!sPoolMiningPublicKey.empty())
	{
		CScript spkPoolScript = GetScriptForDestination(DecodeDestination(sPoolMiningPublicKey));
		coinbaseTx.vout[0].scriptPubKey = spkPoolScript;
	}
	
    // NOTE: unlike in bitcoin, we need to pass PREVIOUS block height here
    CAmount blockReward = nFees + GetBlockSubsidy(pindexPrev->nBits, pindexPrev->nHeight, Params().GetConsensus());

    // RANDREWS - BIBLEPAY - 9-5-2023 - IF this is an investor block, start at half here:

    // Compute regular coinbase transaction.
    coinbaseTx.vout[0].nValue = blockReward;

    if (!fDIP0003Active_context) {
        coinbaseTx.vin[0].scriptSig = CScript() << nHeight << OP_0;
    } else {
        coinbaseTx.vin[0].scriptSig = CScript() << OP_RETURN;

        coinbaseTx.nVersion = 3;
        coinbaseTx.nType = TRANSACTION_COINBASE;

        CCbTx cbTx;

        if (fDIP0008Active_context) {
            cbTx.nVersion = 2;
        } else {
            cbTx.nVersion = 1;
        }

        cbTx.nHeight = nHeight;

        CValidationState state;
        if (!CalcCbTxMerkleRootMNList(*pblock, pindexPrev, cbTx.merkleRootMNList, state, *pcoinsTip.get())) {
            throw std::runtime_error(strprintf("%s: CalcCbTxMerkleRootMNList failed: %s", __func__, FormatStateMessage(state)));
        }
        if (fDIP0008Active_context) {
            if (!CalcCbTxMerkleRootQuorums(*pblock, pindexPrev, cbTx.merkleRootQuorums, state)) {
                throw std::runtime_error(strprintf("%s: CalcCbTxMerkleRootQuorums failed: %s", __func__, FormatStateMessage(state)));
            }
        }

        SetTxPayload(coinbaseTx, cbTx);
    }
	// Add core version to the subsidy tx message
	std::string sVersion = FormatFullVersion();
	coinbaseTx.vout[0].sTxOutMessage += "<VER>" + sVersion + "</VER>";

    // Update coinbase transaction with additional info about masternode and governance payments,
    // get some info back to pass to getblocktemplate
    FillBlockPayments(coinbaseTx, nHeight, blockReward, pblocktemplate->voutMasternodePayments, pblocktemplate->voutSuperblockPayments);
    // Mission Critical Todo:  POVS:  Copy  vout[1] scriptPubKey out to vout[0] scriptPubKey here.

    pblock->vtx[0] = MakeTransactionRef(std::move(coinbaseTx));
    pblocktemplate->vTxFees[0] = -nFees;

    // Fill in header
    pblock->hashPrevBlock  = pindexPrev->GetBlockHash();
    UpdateTime(pblock, chainparams.GetConsensus(), pindexPrev);
    pblock->nBits          = GetNextWorkRequired(pindexPrev, pblock, chainparams.GetConsensus());
    pblock->nNonce         = 0;
    pblocktemplate->nPrevBits = pindexPrev->nBits;
    pblocktemplate->vTxSigOps[0] = GetLegacySigOpCount(*pblock->vtx[0]);

	unsigned int nExtraNonce = 1;

	IncrementExtraNonce(pblock, chainActive.Tip(), nExtraNonce);
        

    CValidationState state;
    if (!TestBlockValidity(state, chainparams, *pblock, pindexPrev, false, false)) {
        throw std::runtime_error(strprintf("%s: TestBlockValidity failed: %s", __func__, FormatStateMessage(state)));
    }
    int64_t nTime2 = GetTimeMicros();

    LogPrint(BCLog::BENCHMARK, "CreateNewBlock() packages: %.2fms (%d packages, %d updated descendants), validity: %.2fms (total %.2fms)\n", 0.001 * (nTime1 - nTimeStart), nPackagesSelected, nDescendantsUpdated, 0.001 * (nTime2 - nTime1), 0.001 * (nTime2 - nTimeStart));

    return std::move(pblocktemplate);
}

void BlockAssembler::onlyUnconfirmed(CTxMemPool::setEntries& testSet)
{
    for (CTxMemPool::setEntries::iterator iit = testSet.begin(); iit != testSet.end(); ) {
        // Only test txs not already in the block
        if (inBlock.count(*iit)) {
            testSet.erase(iit++);
        }
        else {
            iit++;
        }
    }
}

bool BlockAssembler::TestPackage(uint64_t packageSize, unsigned int packageSigOps) const
{
    if (nBlockSize + packageSize >= nBlockMaxSize)
        return false;
    if (nBlockSigOps + packageSigOps >= MaxBlockSigOps(fDIP0001ActiveAtTip))
        return false;
    return true;
}

// Perform transaction-level checks before adding to block:
// - transaction finality (locktime)
// - safe TXs in regard to ChainLocks
bool BlockAssembler::TestPackageTransactions(const CTxMemPool::setEntries& package)
{
    for (CTxMemPool::txiter it : package) {
        if (!IsFinalTx(it->GetTx(), nHeight, nLockTimeCutoff))
            return false;
        if (!llmq::chainLocksHandler->IsTxSafeForMining(it->GetTx().GetHash())) {
            return false;
        }
    }
    return true;
}

void BlockAssembler::AddToBlock(CTxMemPool::txiter iter)
{
    pblock->vtx.emplace_back(iter->GetSharedTx());
    pblocktemplate->vTxFees.push_back(iter->GetFee());
    pblocktemplate->vTxSigOps.push_back(iter->GetSigOpCount());
    nBlockSize += iter->GetTxSize();
    ++nBlockTx;
    nBlockSigOps += iter->GetSigOpCount();
    nFees += iter->GetFee();
    inBlock.insert(iter);

    bool fPrintPriority = gArgs.GetBoolArg("-printpriority", DEFAULT_PRINTPRIORITY);
    if (fPrintPriority) {
        LogPrintf("fee %s txid %s\n",
                  CFeeRate(iter->GetModifiedFee(), iter->GetTxSize()).ToString(),
                  iter->GetTx().GetHash().ToString());
    }
}

int BlockAssembler::UpdatePackagesForAdded(const CTxMemPool::setEntries& alreadyAdded,
        indexed_modified_transaction_set &mapModifiedTx)
{
    int nDescendantsUpdated = 0;
    for (CTxMemPool::txiter it : alreadyAdded) {
        CTxMemPool::setEntries descendants;
        mempool.CalculateDescendants(it, descendants);
        // Insert all descendants (not yet in block) into the modified set
        for (CTxMemPool::txiter desc : descendants) {
            if (alreadyAdded.count(desc))
                continue;
            ++nDescendantsUpdated;
            modtxiter mit = mapModifiedTx.find(desc);
            if (mit == mapModifiedTx.end()) {
                CTxMemPoolModifiedEntry modEntry(desc);
                modEntry.nSizeWithAncestors -= it->GetTxSize();
                modEntry.nModFeesWithAncestors -= it->GetModifiedFee();
                modEntry.nSigOpCountWithAncestors -= it->GetSigOpCount();
                mapModifiedTx.insert(modEntry);
            } else {
                mapModifiedTx.modify(mit, update_for_parent_inclusion(it));
            }
        }
    }
    return nDescendantsUpdated;
}

// Skip entries in mapTx that are already in a block or are present
// in mapModifiedTx (which implies that the mapTx ancestor state is
// stale due to ancestor inclusion in the block)
// Also skip transactions that we've already failed to add. This can happen if
// we consider a transaction in mapModifiedTx and it fails: we can then
// potentially consider it again while walking mapTx.  It's currently
// guaranteed to fail again, but as a belt-and-suspenders check we put it in
// failedTx and avoid re-evaluation, since the re-evaluation would be using
// cached size/sigops/fee values that are not actually correct.
bool BlockAssembler::SkipMapTxEntry(CTxMemPool::txiter it, indexed_modified_transaction_set &mapModifiedTx, CTxMemPool::setEntries &failedTx)
{
    assert (it != mempool.mapTx.end());
    return mapModifiedTx.count(it) || inBlock.count(it) || failedTx.count(it);
}

void BlockAssembler::SortForBlock(const CTxMemPool::setEntries& package, std::vector<CTxMemPool::txiter>& sortedEntries)
{
    // Sort package by ancestor count
    // If a transaction A depends on transaction B, then A's ancestor count
    // must be greater than B's.  So this is sufficient to validly order the
    // transactions for block inclusion.
    sortedEntries.clear();
    sortedEntries.insert(sortedEntries.begin(), package.begin(), package.end());
    std::sort(sortedEntries.begin(), sortedEntries.end(), CompareTxIterByAncestorCount());
}

// This transaction selection algorithm orders the mempool based
// on feerate of a transaction including all unconfirmed ancestors.
// Since we don't remove transactions from the mempool as we select them
// for block inclusion, we need an alternate method of updating the feerate
// of a transaction with its not-yet-selected ancestors as we go.
// This is accomplished by walking the in-mempool descendants of selected
// transactions and storing a temporary modified state in mapModifiedTxs.
// Each time through the loop, we compare the best transaction in
// mapModifiedTxs with the next transaction in the mempool to decide what
// transaction package to work on next.
void BlockAssembler::addPackageTxs(int &nPackagesSelected, int &nDescendantsUpdated)
{
    // mapModifiedTx will store sorted packages after they are modified
    // because some of their txs are already in the block
    indexed_modified_transaction_set mapModifiedTx;
    // Keep track of entries that failed inclusion, to avoid duplicate work
    CTxMemPool::setEntries failedTx;

    // Start by adding all descendants of previously added txs to mapModifiedTx
    // and modifying them for their already included ancestors
    UpdatePackagesForAdded(inBlock, mapModifiedTx);

    CTxMemPool::indexed_transaction_set::index<ancestor_score>::type::iterator mi = mempool.mapTx.get<ancestor_score>().begin();
    CTxMemPool::txiter iter;

    // Limit the number of attempts to add transactions to the block when it is
    // close to full; this is just a simple heuristic to finish quickly if the
    // mempool has a lot of entries.
    const int64_t MAX_CONSECUTIVE_FAILURES = 1000;
    int64_t nConsecutiveFailed = 0;

    while (mi != mempool.mapTx.get<ancestor_score>().end() || !mapModifiedTx.empty())
    {
        // First try to find a new transaction in mapTx to evaluate.
        if (mi != mempool.mapTx.get<ancestor_score>().end() &&
                SkipMapTxEntry(mempool.mapTx.project<0>(mi), mapModifiedTx, failedTx)) {
            ++mi;
            continue;
        }

        // Now that mi is not stale, determine which transaction to evaluate:
        // the next entry from mapTx, or the best from mapModifiedTx?
        bool fUsingModified = false;

        modtxscoreiter modit = mapModifiedTx.get<ancestor_score>().begin();
        if (mi == mempool.mapTx.get<ancestor_score>().end()) {
            // We're out of entries in mapTx; use the entry from mapModifiedTx
            iter = modit->iter;
            fUsingModified = true;
        } else {
            // Try to compare the mapTx entry to the mapModifiedTx entry
            iter = mempool.mapTx.project<0>(mi);
            if (modit != mapModifiedTx.get<ancestor_score>().end() &&
                    CompareTxMemPoolEntryByAncestorFee()(*modit, CTxMemPoolModifiedEntry(iter))) {
                // The best entry in mapModifiedTx has higher score
                // than the one from mapTx.
                // Switch which transaction (package) to consider
                iter = modit->iter;
                fUsingModified = true;
            } else {
                // Either no entry in mapModifiedTx, or it's worse than mapTx.
                // Increment mi for the next loop iteration.
                ++mi;
            }
        }

        // We skip mapTx entries that are inBlock, and mapModifiedTx shouldn't
        // contain anything that is inBlock.
        assert(!inBlock.count(iter));

        uint64_t packageSize = iter->GetSizeWithAncestors();
        CAmount packageFees = iter->GetModFeesWithAncestors();
        unsigned int packageSigOps = iter->GetSigOpCountWithAncestors();
        if (fUsingModified) {
            packageSize = modit->nSizeWithAncestors;
            packageFees = modit->nModFeesWithAncestors;
            packageSigOps = modit->nSigOpCountWithAncestors;
        }

        if (packageFees < blockMinFeeRate.GetFee(packageSize)) {
            // Everything else we might consider has a lower fee rate
            return;
        }

        if (!TestPackage(packageSize, packageSigOps)) {
            if (fUsingModified) {
                // Since we always look at the best entry in mapModifiedTx,
                // we must erase failed entries so that we can consider the
                // next best entry on the next loop iteration
                mapModifiedTx.get<ancestor_score>().erase(modit);
                failedTx.insert(iter);
            }

            ++nConsecutiveFailed;

            if (nConsecutiveFailed > MAX_CONSECUTIVE_FAILURES && nBlockSize > nBlockMaxSize - 1000) {
                // Give up if we're close to full and haven't succeeded in a while
                break;
            }
            continue;
        }

        CTxMemPool::setEntries ancestors;
        uint64_t nNoLimit = std::numeric_limits<uint64_t>::max();
        std::string dummy;
        mempool.CalculateMemPoolAncestors(*iter, ancestors, nNoLimit, nNoLimit, nNoLimit, nNoLimit, dummy, false);

        onlyUnconfirmed(ancestors);
        ancestors.insert(iter);

        // Test if all tx's are Final and safe
        if (!TestPackageTransactions(ancestors)) {
            if (fUsingModified) {
                mapModifiedTx.get<ancestor_score>().erase(modit);
                failedTx.insert(iter);
            }
            continue;
        }

        // This transaction will make it in; reset the failed counter.
        nConsecutiveFailed = 0;

        // Package can be added. Sort the entries in a valid order.
        std::vector<CTxMemPool::txiter> sortedEntries;
        SortForBlock(ancestors, sortedEntries);

        for (size_t i=0; i<sortedEntries.size(); ++i) {
            AddToBlock(sortedEntries[i]);
            // Erase from the modified set, if present
            mapModifiedTx.erase(sortedEntries[i]);
        }

        ++nPackagesSelected;

        // Update transactions that depend on each of these
        nDescendantsUpdated += UpdatePackagesForAdded(ancestors, mapModifiedTx);
    }
}

void IncrementExtraNonce(CBlock* pblock, const CBlockIndex* pindexPrev, unsigned int& nExtraNonce)
{
    // Update nExtraNonce
    static uint256 hashPrevBlock;
    if (hashPrevBlock != pblock->hashPrevBlock)
    {
        nExtraNonce = 0;
        hashPrevBlock = pblock->hashPrevBlock;
    }
    ++nExtraNonce;
    unsigned int nHeight = pindexPrev->nHeight+1; // Height first in coinbase required for block.version=2
    CMutableTransaction txCoinbase(*pblock->vtx[0]);
    txCoinbase.vin[0].scriptSig = (CScript() << nHeight << CScriptNum(nExtraNonce)) + COINBASE_FLAGS;
    assert(txCoinbase.vin[0].scriptSig.size() <= 100);

    pblock->vtx[0] = MakeTransactionRef(std::move(txCoinbase));
    pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
}



//////////////////////////////////////////////////////////////////////////////
//                                                                          //
//        BiblePay - RandomX Internal miner - March 12th, 2019              //
//                                                                          //
//                                                                          //
//////////////////////////////////////////////////////////////////////////////

void UpdateHashesPerSec(double& nHashesDone)
{
	nHashCounter += nHashesDone;
	nHashesDone = 0;
	dHashesPerSec = 1000.0 * nHashCounter / (GetTimeMillis() - nHPSTimerStart);
}

bool PeersExist()
{
	 int iConCount = (int)g_connman->GetNodeCount(CConnman::CONNECTIONS_ALL);
	 return (iConCount > 0);
}

bool LateBlock(CBlock block, CBlockIndex* pindexPrev, int iMinutes)
{
	// Mark this for retiring...
	int64_t nAgeTip = block.GetBlockTime() - pindexPrev->nTime;
	return (nAgeTip > (60 * iMinutes)) ? true : false;
}

bool CreateBlockForStratum(std::string sAddress, uint256 uRandomXKey, std::vector<unsigned char> vRandomXHeader, std::string& sError, CBlock& blockX)
{
	int iThreadID = 0;
	// BIBLEPAY
	std::shared_ptr<CReserveScript> coinbaseScript = GetScriptForMining();

	std::unique_ptr<CBlockTemplate> pblocktemplate(BlockAssembler(Params()).CreateNewBlock(coinbaseScript->reserveScript, sAddress, uRandomXKey, vRandomXHeader));
	if (!pblocktemplate.get())
    {
		LogPrint(BCLog::NET, "CreateBlockForStratum::No block to mine %f", iThreadID);
		sError = "Unable to retrieve block mining template...";
		return false;
    }
	CBlock *pblock = &pblocktemplate->block;
	int iStart = rand() % 65536;
	unsigned int nExtraNonce = GetAdjustedTime() + iStart; // This is the Extra Nonce (not the nonce); 
	CBlockIndex* pindexPrev = chainActive.Tip();
	IncrementExtraNonce(pblock, pindexPrev, nExtraNonce);
	blockX = const_cast<CBlock&>(*pblock);
	return true;
}	

void static BiblePayMiner(const CChainParams& chainparams, int iThreadID, int iFeatureSet)
{
	LogPrintf("BiblePayMiner -- started thread %f \n", (double)iThreadID);
    int64_t nThreadStart = GetTimeMillis();
	int64_t nLastGUI = GetAdjustedTime() - 30;
	int64_t nLastMiningBreak = 0;
	unsigned int nExtraNonce = 0;
	double nHashesDone = 0;
	
	// The jackrabbit start option forces the miner to start regardless of rules (like not having peers, not being synced etc).
	double dJackrabbitStart = StringToDouble(gArgs.GetArg("-jackrabbitstart", "0"), 0);
    RenameThread("biblepay-miner");
				
	std::shared_ptr<CReserveScript> coinbaseScript = GetScriptForMining();

	int iStart = rand() % 1000;
	MilliSleep(iStart);

recover:
	
    try {
        // Throw an error if no script was provided.  This can happen
        // due to some internal error but also if the keypool is empty.
        // In the latter case, already the pointer is NULL.
        if (!coinbaseScript || coinbaseScript->reserveScript.empty())
            throw std::runtime_error("No coinbase script available (mining requires a wallet)");

		
        while (true) 
		{
			bool fChainEmpty = (chainActive.Tip() == NULL || chainActive.Tip()->nHeight < 100);

            if (fReindex || fChainEmpty)
			{
                // Busy-wait for the network to come online so we don't waste time mining
                // on an obsolete chain. In regtest mode we expect to fly solo.
                while(true)
				{
		            if (PeersExist() && !IsInitialBlockDownload() && masternodeSync.IsSynced() && !fReindex && !fChainEmpty)
						break;
					if (dJackrabbitStart == 1) 
						break;
                    
                    MilliSleep(1000);
                } 
            }
			

            //
            // Create new block
            //
            unsigned int nTransactionsUpdatedLast = mempool.GetTransactionsUpdated();
            CBlockIndex* pindexPrev = chainActive.Tip();
            if(!pindexPrev) break;
			bool fRandomX = (pindexPrev->nHeight >= chainparams.GetConsensus().RANDOMX_HEIGHT);

			// Create block
			uint256 uRXKey = uint256S("0x01");
			std::vector<unsigned char> vchRXHeader = ParseHex("00");
			std::unique_ptr<CBlockTemplate> pblocktemplate(BlockAssembler(Params()).CreateNewBlock(coinbaseScript->reserveScript, "", uRXKey, vchRXHeader));
	
			if (!pblocktemplate.get())
            {
				MilliSleep(15000);
				LogPrint(BCLog::NET, "No block to mine %f", iThreadID);
				goto recover;
            }

			CBlock *pblock = &pblocktemplate->block;
			
			int iStart = rand() % 65536;
			unsigned int nExtraNonce = GetAdjustedTime() + iStart + iThreadID;
			
            IncrementExtraNonce(pblock, pindexPrev, nExtraNonce);
			nHashesDone++;
			UpdateHashesPerSec(nHashesDone);
		    //
            // Search
            //
            int64_t nStart = GetTime();
            arith_uint256 hashTarget = arith_uint256().SetCompact(pblock->nBits);
			const Consensus::Params& consensusParams = Params().GetConsensus();

bool fAllowedToMine = true;
#if defined(MAC_OSX)
	fAllowedToMine = false;
#endif

			while (true)
			{
				while (fAllowedToMine)
				{
					pblock->nNonce += 1;
					uint256 rxHeader = uint256S("0x" + DoubleToString(GetAdjustedTime(), 0) + DoubleToString(iThreadID, 0) + DoubleToString(pblock->nNonce, 0));
					pblock->RandomXData = "<rxheader>" + rxHeader.GetHex() + "</rxheader>";
					uint256 rxhash = GetRandomXHash3(pblock->RandomXData, uRXKey, iThreadID);
					nHashesDone += 1;
				
					if (UintToArith256(rxhash) <= hashTarget)
					{
						// Found a solution
						LogPrintf("\r\nMiner::Found a randomx block solo mining! hashes=%f, hash=%s, thread=%f", nHashesDone, rxhash.GetHex(), iThreadID);

						std::shared_ptr<const CBlock> shared_pblock = std::make_shared<const CBlock>(*pblock);
						bool bAccepted = !ProcessNewBlock(Params(), shared_pblock, true, NULL);
						if (!bAccepted)
						{
							LogPrintf("\nblock rejected.");
							MilliSleep(15000);
                        }
                        else
                        {
                            // To prevent Chainlocks conflicts, allow the miner to sleep a while here
                            // If we solve multiple blocks in a row, this node can end up with a chainlocks conflict
                           if (chainparams.NetworkIDString() == CBaseChainParams::MAIN) 
                           {
                                MilliSleep(120000);
                           }
                        }
						coinbaseScript->KeepScript();
						// In regression test mode, stop mining after a block is found. This
						// allows developers to controllably generate a block on demand.
						if (chainparams.MineBlocksOnDemand())
								throw boost::thread_interrupted();
						break;
					}
						
			
					if ((pblock->nNonce & 0xFF) == 0)
					{
						boost::this_thread::interruption_point();
					    if (ShutdownRequested()) 
							break;
		
            			int64_t nElapsed = GetAdjustedTime() - nLastGUI;
						if (nElapsed > 7)
						{
							nLastGUI = GetAdjustedTime();
							UpdateHashesPerSec(nHashesDone);
							// Make a new block
								pblock->nNonce = 0x9FFF;
						}
						int64_t nElapsedLastMiningBreak = GetAdjustedTime() - nLastMiningBreak;
						if (nElapsedLastMiningBreak > 60)
						{
							nLastMiningBreak = GetAdjustedTime();
							break;
						}
					}
                    else if (chainActive.Tip()->nHeight > chainparams.GetConsensus().LATTER_RAIN_HEIGHT)
                    {
                        MilliSleep(1);
                    }
    			}

				UpdateHashesPerSec(nHashesDone);
				// Check for stop or if block needs to be rebuilt
				boost::this_thread::interruption_point();
			    if (ShutdownRequested()) 
					break;

				if (!PeersExist())
					 break;

				if (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLast && GetTime() - nStart > 60)
					break;
		
				if (pindexPrev != chainActive.Tip() || pindexPrev==NULL || chainActive.Tip()==NULL)
					break;
		
				if (pblock->nNonce >= 0x9FFF)
					break;
	                        
				// Update nTime every few seconds
				if (pindexPrev)
				{
					if (UpdateTime(pblock, chainparams.GetConsensus(), pindexPrev) < 0)
						break;
					// Recreate the block if the clock has run backwards, so that we can use the correct time.
				}

				if (chainparams.GetConsensus().fPowAllowMinDifficultyBlocks)
				{
					// Changing pblock->nTime can change work required on testnet:
					hashTarget.SetCompact(pblock->nBits);
				}

			}
        }
    }
    catch (const boost::thread_interrupted&)
    {
        LogPrint(BCLog::NET, "\r\nSoloMiner -- terminated\n %f", iThreadID);
		dHashesPerSec = 0;
        throw;
    }
    catch (const std::runtime_error &e)
    {
        LogPrint(BCLog::NET, "\r\nSoloMiner -- runtime error: %s\n", e.what());
		dHashesPerSec = 0;
		nThreadStart = GetTimeMillis();
		MilliSleep(1000);
		goto recover;
		// throw;
    }
}

void GenerateCoins(bool fGenerate, int nThreads, const CChainParams& chainparams)
{
    static boost::thread_group* minerThreads = NULL;

    if (nThreads < 1) 
    {
        nThreads = 1;  // GetNumCores() - Reserved for a change to heat mining
    }

    if (minerThreads != NULL)
    {
        minerThreads->interrupt_all();
		LogPrintf("Destroying all miner threads %f", GetAdjustedTime());
		minerThreads->join_all();
        delete minerThreads;
        minerThreads = NULL;
		LogPrintf("Destroyed all miner threads %f", GetAdjustedTime());
		// We must be very careful here with RandomX, as we have one VM running per mining thread, so we need to let these threads exit
    }

    if (nThreads == 0 || !fGenerate)
        return;

    minerThreads = new boost::thread_group();
	int iBibleHashNumber = 0;			
    for (int i = 0; i < nThreads; i++)
	{
	    minerThreads->create_thread(boost::bind(&BiblePayMiner, boost::cref(chainparams), boost::cref(i), boost::cref(iBibleHashNumber)));
	    MilliSleep(100); 
	}
	iMinerThreadCount = nThreads;
	// Maintain the HashPS
	nHPSTimerStart = GetTimeMillis();
	nHashCounter = 0;
	LogPrintf(" ** Started %f BibleMiner threads. ** \r\n",(double)nThreads);
}



