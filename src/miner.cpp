// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2020 The Bitcoin Core developers
// Copyright (c) 2014-2024 The Dash Core developers
// Copyright (c) 2014-2024 The BiblePay Core developers

// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <miner.h>

#include <rpc/blockchain.h>
#include <amount.h>
#include <chain.h>
#include <chainparams.h>
#include <consensus/consensus.h>
#include <consensus/merkle.h>
#include <consensus/tx_verify.h>
#include <consensus/validation.h>
#include <deploymentstatus.h>
#include <policy/feerate.h>
#include <policy/policy.h>
#include <pow.h>
#include <primitives/transaction.h>
#include <timedata.h> 
#include <util/moneystr.h>
#include <util/system.h>
#include <shutdown.h>
#include <masternode/sync.h>
#include <wallet/rpcwallet.h>

#include <wallet/wallet.h>

#include <evo/specialtx.h>
#include <evo/cbtx.h>
#include <evo/creditpool.h>
#include <evo/mnhftx.h>
#include <evo/simplifiedmns.h>
#include <governance/governance.h>
#include <llmq/blockprocessor.h>
#include <llmq/chainlocks.h>
#include <llmq/context.h>
#include <llmq/instantsend.h>
#include <llmq/options.h>
#include <masternode/payments.h>
#include <spork.h>
#include <validation.h>
#include <node/context.h>
#include <algorithm>
#include <utility>
#include <rpcpog.h>
#include <util/threadnames.h>
#include <interfaces/node.h>
#include <util/system.h>
#include <util/thread.h>
#include <util/threadnames.h>

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

BlockAssembler::BlockAssembler(const CSporkManager& sporkManager, CGovernanceManager& governanceManager,
                               LLMQContext& llmq_ctx, CEvoDB& evoDb, CChainState& chainstate, const CTxMemPool& mempool, const CChainParams& params, const Options& options) :
      chainparams(params),
      m_mempool(mempool),
      m_chainstate(chainstate),
      spork_manager(sporkManager),
      governance_manager(governanceManager),
      quorum_block_processor(*llmq_ctx.quorum_block_processor),
      m_clhandler(*llmq_ctx.clhandler),
      m_isman(*llmq_ctx.isman),
      m_evoDb(evoDb)
{
    blockMinFeeRate = options.blockMinFeeRate;
    nBlockMaxSize = options.nBlockMaxSize;
}

static BlockAssembler::Options DefaultOptions()
{
    // Block resource limits
    BlockAssembler::Options options;
    options.nBlockMaxSize = DEFAULT_BLOCK_MAX_SIZE;
    if (gArgs.IsArgSet("-blockmaxsize")) {
        options.nBlockMaxSize = gArgs.GetArg("-blockmaxsize", DEFAULT_BLOCK_MAX_SIZE);
    }
    if (gArgs.IsArgSet("-blockmintxfee")) {
        std::optional<CAmount> parsed = ParseMoney(gArgs.GetArg("-blockmintxfee", ""));
        options.blockMinFeeRate = CFeeRate{parsed.value_or(DEFAULT_BLOCK_MIN_TX_FEE)};
    } else {
        options.blockMinFeeRate = CFeeRate{DEFAULT_BLOCK_MIN_TX_FEE};
    }
    return options;
}

BlockAssembler::BlockAssembler(const CSporkManager& sporkManager, CGovernanceManager& governanceManager,
                               LLMQContext& llmq_ctx, CEvoDB& evoDb, CChainState& chainstate, const CTxMemPool& mempool, const CChainParams& params)
    : BlockAssembler(sporkManager, governanceManager, llmq_ctx, evoDb, chainstate, mempool, params, DefaultOptions()) {}

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

std::unique_ptr<CBlockTemplate> BlockAssembler::CreateNewBlock(const CScript& scriptPubKeyIn)
{
    int64_t nTimeStart = GetTimeMicros();
    
    resetBlock();

    pblocktemplate.reset(new CBlockTemplate());

    if(!pblocktemplate.get())
        return nullptr;
    CBlock* const pblock = &pblocktemplate->block; // pointer for convenience

    // Add dummy coinbase tx as first transaction
    pblock->vtx.emplace_back();
    pblocktemplate->vTxFees.push_back(-1); // updated at end
    pblocktemplate->vTxSigOps.push_back(-1); // updated at end

    LOCK2(cs_main, m_mempool.cs);
    assert(std::addressof(*::ChainActive().Tip()) == std::addressof(*m_chainstate.m_chain.Tip()));
    CBlockIndex* pindexPrev = m_chainstate.m_chain.Tip();
    assert(pindexPrev != nullptr);
    nHeight = pindexPrev->nHeight + 1;

    const bool fDIP0001Active_context{DeploymentActiveAfter(pindexPrev, chainparams.GetConsensus(), Consensus::DEPLOYMENT_DIP0001)};
    const bool fDIP0003Active_context{DeploymentActiveAfter(pindexPrev, chainparams.GetConsensus(), Consensus::DEPLOYMENT_DIP0003)};
    const bool fDIP0008Active_context{DeploymentActiveAfter(pindexPrev, chainparams.GetConsensus(), Consensus::DEPLOYMENT_DIP0008)};
    const bool fV20Active_context{DeploymentActiveAfter(pindexPrev, chainparams.GetConsensus(), Consensus::DEPLOYMENT_V20)};

    // Limit size to between 1K and MaxBlockSize()-1K for sanity:
    nBlockMaxSize = std::max<unsigned int>(1000, std::min<unsigned int>(MaxBlockSize(fDIP0001Active_context) - 1000, nBlockMaxSize));
    nBlockMaxSigOps = MaxBlockSigOps(fDIP0001Active_context);
    pblock->nVersion = g_versionbitscache.ComputeBlockVersion(pindexPrev, chainparams.GetConsensus());

    if (false)    LogPrintf("\nCreateNewBlock - Block Version %f", pblock->nVersion);


    // Non-mainnet only: allow overriding block.nVersion with
    // -blockversion=N to test forking scenarios
    if (Params().NetworkIDString() != CBaseChainParams::MAIN)
        pblock->nVersion = gArgs.GetArg("-blockversion", pblock->nVersion);

    pblock->nTime = GetAdjustedTime();
    const int64_t nMedianTimePast = pindexPrev->GetMedianTimePast();

    nLockTimeCutoff = (STANDARD_LOCKTIME_VERIFY_FLAGS & LOCKTIME_MEDIAN_TIME_PAST)
                       ? nMedianTimePast
                       : pblock->GetBlockTime();

    LogPrintf("\r\nBBPMINER::CREATENEWBLOCK::VERSION %f TIME %f", pblock->nVersion, pblock->nTime);


    if (fDIP0003Active_context) {
        for (const Consensus::LLMQParams& params : llmq::GetEnabledQuorumParams(pindexPrev)) {
            std::vector<CTransactionRef> vqcTx;
            if (quorum_block_processor.GetMineableCommitmentsTx(params,
                                                                nHeight,
                                                                vqcTx)) {
                for (const auto& qcTx : vqcTx) {
                    pblock->vtx.emplace_back(qcTx);
                    pblocktemplate->vTxFees.emplace_back(0);
                    pblocktemplate->vTxSigOps.emplace_back(0);
                    nBlockSize += qcTx->GetTotalSize();
                    ++nBlockTx;
                }
            }
        }
    }

    int nPackagesSelected = 0;
    int nDescendantsUpdated = 0;

    addPackageTxs(nPackagesSelected, nDescendantsUpdated, pindexPrev);

    int64_t nTime1 = GetTimeMicros();

    m_last_block_num_txs = nBlockTx;
    m_last_block_size = nBlockSize;
    LogPrintf("CreateNewBlock(): total size %u txs: %u fees: %ld sigops %d\n", nBlockSize, nBlockTx, nFees, nBlockSigOps);

    // Create coinbase transaction.
    CMutableTransaction coinbaseTx;
    coinbaseTx.vin.resize(1);
    coinbaseTx.vin[0].prevout.SetNull();
    coinbaseTx.vout.resize(1);
    coinbaseTx.vout[0].scriptPubKey = scriptPubKeyIn;

    // NOTE: unlike in bitcoin, we need to pass PREVIOUS block height here
    CAmount blockSubsidy = GetBlockSubsidyInner(pindexPrev->nBits, pindexPrev->nHeight, Params().GetConsensus(), fV20Active_context);
    CAmount blockReward = blockSubsidy + nFees;

    // Compute regular coinbase transaction.
    coinbaseTx.vout[0].nValue = blockReward;

    if (!fDIP0003Active_context) {
        coinbaseTx.vin[0].scriptSig = CScript() << nHeight << OP_0;
    } else {
        coinbaseTx.vin[0].scriptSig = CScript() << OP_RETURN;

        coinbaseTx.nVersion = 3;
        coinbaseTx.nType = TRANSACTION_COINBASE;

        CCbTx cbTx;

        if (fV20Active_context) {
            cbTx.nVersion = CCbTx::Version::CLSIG_AND_BALANCE;
        } else if (fDIP0008Active_context) {
            cbTx.nVersion = CCbTx::Version::MERKLE_ROOT_QUORUMS;
        } else {
            cbTx.nVersion = CCbTx::Version::MERKLE_ROOT_MNLIST;
        }

        cbTx.nHeight = nHeight;

        BlockValidationState state;
        if (!CalcCbTxMerkleRootMNList(*pblock, pindexPrev, cbTx.merkleRootMNList, state, ::ChainstateActive().CoinsTip())) {
            throw std::runtime_error(strprintf("%s: CalcCbTxMerkleRootMNList failed: %s", __func__, state.ToString()));
        }
        if (fDIP0008Active_context) {
            if (!CalcCbTxMerkleRootQuorums(*pblock, pindexPrev, quorum_block_processor, cbTx.merkleRootQuorums, state)) {
                throw std::runtime_error(strprintf("%s: CalcCbTxMerkleRootQuorums failed: %s", __func__, state.ToString()));
            }
            if (fV20Active_context) {
                if (CalcCbTxBestChainlock(m_clhandler, pindexPrev, cbTx.bestCLHeightDiff, cbTx.bestCLSignature)) {
                    LogPrintf("CreateNewBlock() h[%d] CbTx bestCLHeightDiff[%d] CLSig[%s]\n", nHeight, cbTx.bestCLHeightDiff, cbTx.bestCLSignature.ToString());
                } else {
                    // not an error
                    LogPrintf("CreateNewBlock() h[%d] CbTx failed to find best CL. Inserting null CL\n", nHeight);
                }
                BlockValidationState state;
                const auto creditPoolDiff = GetCreditPoolDiffForBlock(*pblock, pindexPrev, chainparams.GetConsensus(), blockSubsidy, state);
                if (creditPoolDiff == std::nullopt) {
                    throw std::runtime_error(strprintf("%s: GetCreditPoolDiffForBlock failed: %s", __func__, state.ToString()));
                }

                cbTx.creditPoolBalance = creditPoolDiff->GetTotalLocked();
            }
        }

        SetTxPayload(coinbaseTx, cbTx);
    }

    
    // BIBLEPAY - Add core version to the subsidy tx message
    
    std::string sVersion = FormatFullVersion();
    coinbaseTx.vout[0].sTxOutMessage += ComputeMinedBlockVersion();

    // Update coinbase transaction with additional info about masternode and governance payments,
    // get some info back to pass to getblocktemplate
    MasternodePayments::FillBlockPayments(spork_manager, governance_manager, coinbaseTx, pindexPrev, blockSubsidy, nFees, pblocktemplate->voutMasternodePayments, pblocktemplate->voutSuperblockPayments);

    pblock->vtx[0] = MakeTransactionRef(std::move(coinbaseTx));
    pblocktemplate->vTxFees[0] = -nFees;

    // Fill in header
    pblock->hashPrevBlock  = pindexPrev->GetBlockHash();
    UpdateTime(pblock, chainparams.GetConsensus(), pindexPrev);
    pblock->nBits          = GetNextWorkRequired(pindexPrev, pblock, chainparams.GetConsensus());
    pblock->nNonce         = 0;
    pblocktemplate->nPrevBits = pindexPrev->nBits;
    pblocktemplate->vTxSigOps[0] = GetLegacySigOpCount(*pblock->vtx[0]);

    BlockValidationState state;
    assert(std::addressof(::ChainstateActive()) == std::addressof(m_chainstate));
    if (!TestBlockValidity(state, m_clhandler, m_evoDb, chainparams, m_chainstate, *pblock, pindexPrev, false, false)) {
        throw std::runtime_error(strprintf("%s: TestBlockValidity failed: %s", __func__, state.ToString()));
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
    if (nBlockSigOps + packageSigOps >= nBlockMaxSigOps)
        return false;
    return true;
}

// Perform transaction-level checks before adding to block:
// - transaction finality (locktime)
// - safe TXs in regard to ChainLocks
bool BlockAssembler::TestPackageTransactions(const CTxMemPool::setEntries& package) const
{
    // BIBLEPAY
    // CHECK BLOCK FOR UNCONFIRMED ATOMIC TRANSACTIONS -- NOT YET IN USE
    
    for (CTxMemPool::txiter it : package) {
        if (!IsFinalTx(it->GetTx(), nHeight, nLockTimeCutoff))
            return false;

        const auto& txid = it->GetTx().GetHash();
        if (!m_isman.RejectConflictingBlocks() || !m_isman.IsInstantSendEnabled() || m_isman.IsLocked(txid)) continue;

        if (!it->GetTx().vin.empty() && !m_clhandler.IsTxSafeForMining(txid)) {
            return false;
        }
    }
    return true;
}

void BlockAssembler::AddToBlock(CTxMemPool::txiter iter)
{
    CTransaction ctx = iter->GetTx();
    /*
    * In the future, if we want to selectively reject atomic transactions from a block, we can expand on this.  IE 'Unconfirmed' Wrapped DOGE.
    AtomicTrade a = GetAtomicTradeFromTransaction(ctx);
    if (a.IsValid())
    {
        LogPrintf("\nAddToBlock::AtomicTrade %s", a.id);
    }
    */

    pblocktemplate->block.vtx.emplace_back(iter->GetSharedTx());
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
    AssertLockHeld(m_mempool.cs);

    int nDescendantsUpdated = 0;
    for (CTxMemPool::txiter it : alreadyAdded) {
        CTxMemPool::setEntries descendants;
        m_mempool.CalculateDescendants(it, descendants);
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
    AssertLockHeld(m_mempool.cs);

    assert(it != m_mempool.mapTx.end());
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
void BlockAssembler::addPackageTxs(int &nPackagesSelected, int &nDescendantsUpdated, const CBlockIndex* const pindexPrev)
{
    AssertLockHeld(m_mempool.cs);

    // This credit pool is used only to check withdrawal limits and to find
    // duplicates of indexes. There's used `BlockSubsidy` equaled to 0
    std::optional<CCreditPoolDiff> creditPoolDiff;
    if (DeploymentActiveAfter(pindexPrev, chainparams.GetConsensus(), Consensus::DEPLOYMENT_V20)) {
        CCreditPool creditPool = creditPoolManager->GetCreditPool(pindexPrev, chainparams.GetConsensus());
        creditPoolDiff.emplace(std::move(creditPool), pindexPrev, chainparams.GetConsensus(), 0);
    }

    // This map with signals is used only to find duplicates
    std::unordered_map<uint8_t, int> signals = m_chainstate.GetMNHFSignalsStage(pindexPrev);

    // mapModifiedTx will store sorted packages after they are modified
    // because some of their txs are already in the block
    indexed_modified_transaction_set mapModifiedTx;
    // Keep track of entries that failed inclusion, to avoid duplicate work
    CTxMemPool::setEntries failedTx;

    CTxMemPool::indexed_transaction_set::index<ancestor_score>::type::iterator mi = m_mempool.mapTx.get<ancestor_score>().begin();
    CTxMemPool::txiter iter;

    // Limit the number of attempts to add transactions to the block when it is
    // close to full; this is just a simple heuristic to finish quickly if the
    // mempool has a lot of entries.
    const int64_t MAX_CONSECUTIVE_FAILURES = 1000;
    int64_t nConsecutiveFailed = 0;

    while (mi != m_mempool.mapTx.get<ancestor_score>().end() || !mapModifiedTx.empty()) {
        // First try to find a new transaction in mapTx to evaluate.
        if (mi != m_mempool.mapTx.get<ancestor_score>().end() &&
            SkipMapTxEntry(m_mempool.mapTx.project<0>(mi), mapModifiedTx, failedTx)) {
            ++mi;
            continue;
        }

        // Now that mi is not stale, determine which transaction to evaluate:
        // the next entry from mapTx, or the best from mapModifiedTx?
        bool fUsingModified = false;

        modtxscoreiter modit = mapModifiedTx.get<ancestor_score>().begin();
        if (mi == m_mempool.mapTx.get<ancestor_score>().end()) {
            // We're out of entries in mapTx; use the entry from mapModifiedTx
            iter = modit->iter;
            fUsingModified = true;
        } else {
            // Try to compare the mapTx entry to the mapModifiedTx entry
            iter = m_mempool.mapTx.project<0>(mi);
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

        if (creditPoolDiff != std::nullopt) {
            // If one transaction is skipped due to limits, it is not a reason to interrupt
            // whole process of adding transactions.
            // `state` is local here because used only to log info about this specific tx
            TxValidationState state;

            if (!creditPoolDiff->ProcessLockUnlockTransaction(iter->GetTx(), state)) {
                if (fUsingModified) {
                    mapModifiedTx.get<ancestor_score>().erase(modit);
                    failedTx.insert(iter);
                }
                LogPrintf("%s: asset-locks tx %s skipped due %s\n",
                          __func__, iter->GetTx().GetHash().ToString(), state.ToString());
                continue;
            }
        }
        if (std::optional<uint8_t> signal = extractEHFSignal(iter->GetTx()); signal != std::nullopt) {
            if (signals.find(*signal) != signals.end()) {
                if (fUsingModified) {
                    mapModifiedTx.get<ancestor_score>().erase(modit);
                    failedTx.insert(iter);
                }
                LogPrintf("%s: ehf signal tx %s skipped due to duplicate %d\n",
                          __func__, iter->GetTx().GetHash().ToString(), *signal);
                continue;
            }
            signals.insert({*signal, 0});
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
        m_mempool.CalculateMemPoolAncestors(*iter, ancestors, nNoLimit, nNoLimit, nNoLimit, nNoLimit, dummy, false);

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

        for (size_t i=0; i<sortedEntries.size(); ++i)
        {
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
    txCoinbase.vin[0].scriptSig = (CScript() << nHeight << CScriptNum(nExtraNonce));
    assert(txCoinbase.vin[0].scriptSig.size() <= 100);

    pblock->vtx[0] = MakeTransactionRef(std::move(txCoinbase));
    pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
}


//////////////////////////////////////////////////////////////////////////////
//                                                                          //
//        BiblePay - Sanctuary Miner - March 12th, 2019 mod April 2024      //
//                                                                          //
//                                                                          //
//////////////////////////////////////////////////////////////////////////////

void UpdateHashesPerSec(double& nHashesDone)
{
    nHashCounter += nHashesDone;
    nHashesDone = 0;
    dHashesPerSec = 1000.0 * nHashCounter / (GetTimeMillis() - nHPSTimerStart);
}

bool PeersExist(const NodeContext& node)
{
    int iConCount = (int)node.connman->GetNodeCount(CConnman::CONNECTIONS_ALL);
    return (iConCount > 0);
}


/*
* For Pools:
bool CreateBlockForStratum(std::string sAddress, std::string& sError, CBlock& blockX)
{
    int iThreadID = 0;
    // BIBLEPAY
    CScript cbScript = GetScriptForMining();

    std::unique_ptr<CBlockTemplate> pblocktemplate(BlockAssembler(Params()).CreateNewBlock(cbScript));

    if (!pblocktemplate.get()) {
        LogPrint(BCLog::NET, "CreateBlockForStratum::No block to mine %f", iThreadID);
        sError = "Unable to retrieve block mining template...";
        return false;
    }

    CBlock* pblock = &pblocktemplate->block;
    int iStart = rand() % 65536;
    unsigned int nExtraNonce = GetAdjustedTime() + iStart; // This is the Extra Nonce (not the nonce);
    
    CBlockIndex* pindexTip = WITH_LOCK(cs_main, return g_chainman.ActiveChain().Tip());

    
    IncrementExtraNonce(pblock, pindexTip, nExtraNonce);
    blockX = const_cast<CBlock&>(*pblock);
    return true;
}
*/

static bool fThreadInterrupt = false;
static CFeeRate blockMinFeeRate = CFeeRate(DEFAULT_BLOCK_MIN_TX_FEE);

BlockAssembler GetBlockAssembler(const CChainParams& params, const NodeContext& m_node)
{
    BlockAssembler::Options options;
    options.nBlockMaxSize = DEFAULT_BLOCK_MAX_SIZE;
    options.blockMinFeeRate = blockMinFeeRate;
    return BlockAssembler(*m_node.sporkman, *m_node.govman, *m_node.llmq_ctx, *m_node.evodb, ::ChainstateActive(), *m_node.mempool, params, options);
}


static void MinerSleep(int iMilliSecs)
{
    for (int i = 0; i < iMilliSecs / 10; i++)
    {
        // Allows the wallet to close during shutdown
        if (fThreadInterrupt || ShutdownRequested()) {
            return;
        }
        MilliSleep(i / 10); 
    }
}

static void BiblePayMiner(const CChainParams& chainparams, int iThreadID, int iFeatureSet, const JSONRPCRequest& jRequest)
{

    int iFailures = -1;

recover:
    
    const NodeContext& node = EnsureAnyNodeContext(jRequest.context);
    const CTxMemPool& mempool = EnsureMemPool(node);
    auto& mn_sync = *node.mn_sync;
    LogPrintf("BiblePayMiner -- started thread %f \n", (double)iThreadID);

    int64_t nThreadStart = GetTimeMillis();
    int64_t nLastGUI = GetAdjustedTime() - 30;
    int64_t nLastMiningBreak = 0;
    unsigned int nExtraNonce = 0;
    double nHashesDone = 0;

    CScript cbScript = GetScriptForMining(jRequest);

    int iStart = rand() % 1000;
    MinerSleep(iStart);


    try {
        // Throw an error if no script was provided.  This can happen
        // due to some internal error but also if the keypool is empty.
        // In the latter case, already the pointer is NULL.
        if (cbScript.empty())
            throw std::runtime_error("No coinbase script available (mining requires a wallet)");

        CBlockIndex* pindexTip = WITH_LOCK(cs_main, return g_chainman.ActiveChain().Tip());
        CChainState& active_chainstate = g_chainman.ActiveChainstate();

        while (true)
        {
            while (true) {
                bool fChainEmpty = (pindexTip == NULL || pindexTip->nHeight < 100);

                if (fReindex || fChainEmpty) {
                    // Busy-wait for the network to come online so we don't waste time mining
                    // on an obsolete chain. In regtest mode we expect to fly solo.
                    while (true) {
                        //    if(!m_node.masternodeSync().isBlockchainSynced())
                        if (PeersExist(node) && !active_chainstate.IsInitialBlockDownload() && !fReindex && !fChainEmpty)
                            break;

                        MinerSleep(1000);
                    }
                }

                //
                // Create new block
                //
                unsigned int nTransactionsUpdatedLast = mempool.GetTransactionsUpdated();

                CBlockIndex* pindexPrev = WITH_LOCK(cs_main, return g_chainman.ActiveChain().Tip());

                if (!pindexPrev) break;

                // Create block
                BlockAssembler ba = GetBlockAssembler(Params(), node);

                std::unique_ptr<CBlockTemplate> pblocktemplate(ba.CreateNewBlock(cbScript));

                if (!pblocktemplate.get())
                {
                    MinerSleep(15000);
                    LogPrint(BCLog::NET, "No block to mine %f", iThreadID);
                    goto recover;
                }

                CBlock* pblock = &pblocktemplate->block;

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

                while (true)
                {
                    pblock->nNonce += 1;
                    uint256 sanchash = pblock->GetHash();
                    nHashesDone += 1;

                    if (UintToArith256(sanchash) <= hashTarget) {
                        // Found a solution
                        LogPrintf("\r\nMiner::Found a sanc block solo mining! hashes=%f, hash=%s, thread=%f", nHashesDone, sanchash.GetHex(), iThreadID);
                        bool fOK = pindexTip->nHeight >= chainparams.GetConsensus().BABYLON_FALLING_HEIGHT-1;

                        if (fOK) {
                            std::shared_ptr<const CBlock> shared_pblock = std::make_shared<const CBlock>(*pblock);
                            bool bAccepted = g_chainman.ProcessNewBlock(Params(), shared_pblock, true, NULL);
                            if (!bAccepted) {
                                LogPrintf("\r\nblock rejected.%f", 2);
                                MinerSleep(1000);
                            } else {
                                // To prevent Chainlocks conflicts, allow the miner to sleep a while here
                                // If we solve multiple blocks in a row, this node can end up with a chainlocks conflict
                                if (chainparams.NetworkIDString() == CBaseChainParams::MAIN) {
                                    LogPrintf("\r\nMiner::Sleeping...%f", 1);
                                    MinerSleep(1000);
                                }
                            }
                        }
                        // coinbaseScript->KeepScript();
                        // In regression test mode, stop mining after a block is found. This
                        // allows developers to controllably generate a block on demand.
                        if (chainparams.MineBlocksOnDemand()) {
                            return;
                        }
                        break;
                    }

                    if ((pblock->nNonce & 0xF) == 0)
                    {
                        // This is the boost interruption point which detects node shutdowns; this occurs once every few seconds
                        if (fThreadInterrupt || ShutdownRequested()) {
                            return;
                        }
                        MilliSleep(1);

                        int64_t nElapsed = GetAdjustedTime() - nLastGUI;
                        if (nElapsed > 7) {
                            nLastGUI = GetAdjustedTime();
                            UpdateHashesPerSec(nHashesDone);
                            // Make a new block
                            pblock->nNonce = 0x9FFF;
                        }
                        int64_t nElapsedLastMiningBreak = GetAdjustedTime() - nLastMiningBreak;
                        if (nElapsedLastMiningBreak > 60) {
                            LogPrintf("\r\nMiner::Mining...hashtarg %s , %f", hashTarget.GetHex(), nElapsedLastMiningBreak);
                            nLastMiningBreak = GetAdjustedTime();
                            break;
                        }
                    }
                }

                UpdateHashesPerSec(nHashesDone);
                // Check for stop or if block needs to be rebuilt
                if (fThreadInterrupt || ShutdownRequested()) {
                    return;
                }

                if (!PeersExist(node)) {
                    break;
                }

                if (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLast && GetTime() - nStart > 60)
                    break;

                CBlockIndex* pindexActiveTip = WITH_LOCK(cs_main, return g_chainman.ActiveChain().Tip());
                if (pindexPrev != pindexActiveTip || pindexPrev == NULL || pindexActiveTip == NULL) {
                    LogPrintf("\r\nMining stale tip %f", 1);
                    break;
                }

                if (pblock->nNonce >= 0x9FFF)
                    break;

                // Update nTime every few seconds
                if (pindexPrev) {
                    if (UpdateTime(pblock, chainparams.GetConsensus(), pindexPrev) < 0)
                        break;
                    // Recreate the block if the clock has run backwards, so that we can use the correct time.
                }
                if (!fMasternodeMode)
                {
                    MilliSleep(10);
                }

                if (chainparams.GetConsensus().fPowAllowMinDifficultyBlocks) {
                    // Changing pblock->nTime can change work required on testnet:
                    hashTarget.SetCompact(pblock->nBits);
                }
            }
        }
        
    }
    catch (const std::runtime_error& e)
    {
        // This can happen if we create a block that fails to meet business logic rules.
        // IE: Block does not pay the right deterministic sanc.
        iFailures++;
        LogPrintf("\r\nSoloMiner -- failure #%f -- runtime error: %s\n", iFailures, e.what());
        if (fThreadInterrupt || iFailures >= 100)
        {
            return;
        }
        dHashesPerSec = 0;
        nThreadStart = GetTimeMillis();
        MinerSleep(10000);
        goto recover;
    }
}

static std::vector<std::thread> minerThreads;
static std::vector<std::thread> traderThreads;

void KillMinerThreads()
{
    fThreadInterrupt = true;
    for (auto& thread : minerThreads) {
        thread.join();
    }
    minerThreads.clear();
    fThreadInterrupt = false;
}

void KillTraderThreads()
{
    fThreadInterrupt = true;
    for (auto& thread : traderThreads) {
        thread.join();
    }
    traderThreads.clear();
    fThreadInterrupt = false;
}



void GenerateCoins(bool fGenerate, int nThreads, const CChainParams& chainparams, const JSONRPCRequest& jRequest)
{
    //nThreads = 1; // GetNumCores() - Reserved for a change to heat mining
    CreateWalletIfNotExists(jRequest);
    LogPrintf("Destroying all miner threads %f", GetAdjustedTime());
    KillMinerThreads();
    LogPrintf("Destroyed all miner threads %f", GetAdjustedTime());
    
    if (nThreads == 0 || !fGenerate)
        return;

    fThreadInterrupt = false;
    int iBibleHashNumber = 0;
    for (int i = 0; i < nThreads; i++)
    {
        minerThreads.emplace_back(std::thread(BiblePayMiner, chainparams, i, iBibleHashNumber, jRequest));
    }
    iMinerThreadCount = nThreads;
    // Maintain the HashPS
    nHPSTimerStart = GetTimeMillis();
    nHashCounter = 0;
    LogPrintf(" ** Started %f BibleMiner threads. ** \r\n", (double)nThreads);
}

static bool IsAtomicTradeMatched(std::string sID)
{
    std::map<std::string, AtomicTrade> mapAT = GetAtomicTrades();

    if (sID == "") {
        return false;
    }
    for (auto& it : mapAT) {
        AtomicTrade a = it.second;
        if (a.MatchedTo == sID && a.id != sID && a.Status=="open") return true;
    }
    return false;
}


static bool CreateAtomicTrade(const JSONRPCRequest& jRequest, std::string sAction, std::string symbolBuy, std::string symbolSell, int Qty, double Price)
{
    AtomicTrade a;
    a.Action = sAction;
    a.SymbolBuy = symbolBuy;
    a.SymbolSell = symbolSell;
    a.Quantity = Qty;
    a.Price = Price;
    a.Status = "open";

    if (a.Price <= 0)
    {
        std::string sErr = "The price of doge per BBP must be greater than zero.";
        LogPrintf("\nCreateAtomicTrade %s", sErr);
        return false;
    }
    a.id = DoubleToString(GetAdjustedTime(), 0);
    a.Signer = DefaultRecAddress(jRequest, "Trading-Public-Key");
    a.AltAddress = GetDogePubKey(a.Signer, jRequest);
    a.Time = GetAdjustedTime();
    std::string sError;
    TradingLog("Create Atomic Trade for leftover quantity " + DoubleToString(Qty, 0));

    std::string sTXID = TransmitSidechainTx(jRequest, a, sError);
    return true;
}


/*
static void ScanForAdjustmentTrades(const JSONRPCRequest& jRequest)
{
    std::map<std::string, AtomicTrade> mapAT = GetAtomicTrades();
    std::string sTPK = DefaultRecAddress(jRequest, "Trading-Public-Key");
 
    for (auto& it : mapAT)
    {
        // If its MINE and Im the buyer and I find a Seller that is selling LESS QTY than Im buying at the same or cheaper price?
        // Then we edit our transaction and re-push the leftover amount as a second one.
        AtomicTrade buyer = it.second;
        if (buyer.Status == "open" && buyer.Signer == sTPK && buyer.Action == "buy" && buyer.MatchedTo == "" && !IsAtomicTradeMatched(buyer.id))
        {
            for (auto& it2 : mapAT)
            {
                AtomicTrade seller = it2.second;
                if (seller.Status == "open" && seller.Action == "sell" && seller.MatchedTo == "" && !IsAtomicTradeMatched(seller.id))
                {
                    if (seller.Quantity < buyer.Quantity && seller.Price <= buyer.Price)
                    {
                        // we can adjust this one
                        int nLeftoverQuantity = buyer.Quantity - seller.Quantity;
                        if (nLeftoverQuantity > 99)
                        {
                            // We were the buyer of BBP, and seller of DOGE.
                            // Add the leftover trade ***HERE ***
                            buyer.FilledQuantity = nLeftoverQuantity;
                            bool f14 =  CreateAtomicTrade(jRequest, "buy", "bbp", "doge", nLeftoverQuantity, buyer.Price);
                        }
                        // BBP ATOMIC TRADE - PLACE EDIT
                        if (seller.Price < buyer.Price)
                        {
                            buyer.Price = seller.Price;
                        }
                        buyer.Quantity = seller.Quantity;
                        buyer.MatchedTo = seller.id;
                        TradingLog("BBPTrader::Adjusting BUY TX::" + buyer.id + " to qty " + DoubleToString(buyer.Quantity, 0) + " and price " + DoubleToString(buyer.Price, 6) + " matched to " + buyer.MatchedTo);
                        std::string sError;
                        std::string sTXID = TransmitSidechainTx(jRequest, buyer, sError);
                        MinerSleep(1000);
                    }
                }
            }
        }
    }




    for (auto& it : mapAT)
    {
        AtomicTrade seller = it.second;
        if (seller.Status == "open" && seller.Signer == sTPK && seller.Action == "sell" && seller.MatchedTo == "" && !IsAtomicTradeMatched(seller.id))
        {
            for (auto& it2 : mapAT) {
                AtomicTrade buyer = it2.second;
                if (buyer.Status == "open" && buyer.Action == "buy" && buyer.MatchedTo == "" && !IsAtomicTradeMatched(buyer.id))
                {
                    if (seller.Quantity > buyer.Quantity && seller.Price <= buyer.Price)
                    {
                        // we can adjust this one
                        int nLeftoverQuantity = seller.Quantity - buyer.Quantity;
                        if (nLeftoverQuantity > 99) {
                            seller.FilledQuantity = nLeftoverQuantity;
                            bool f14 = CreateAtomicTrade(jRequest, "sell", "doge", "bbp", nLeftoverQuantity, seller.Price);
                        }
                        // BBP ATOMIC TRADE - PLACE EDIT
                        if (buyer.Price > seller.Price)
                        {
                            seller.Price = buyer.Price;
                        }
                        seller.Quantity = buyer.Quantity;
                        seller.MatchedTo = buyer.id;
                        TradingLog("BBPTrader::Adjusting SELL TX::" + buyer.id + " to qty "
                            + DoubleToString(buyer.Quantity, 0) + " and price " + DoubleToString(buyer.Price, 6) + " matched to " + buyer.MatchedTo);
                        std::string sError;
                        std::string sTXID = TransmitSidechainTx(jRequest, seller, sError);
                        MinerSleep(1000);
                    }
                }
            }
        }
    }
}
*/


static void BiblePayTrader(const CChainParams& chainparams, const JSONRPCRequest& jRequest)
{
    int iFailures = -1;

recover:

    const NodeContext& node = EnsureAnyNodeContext(jRequest.context);
    const CTxMemPool& mempool = EnsureMemPool(node);
    auto& mn_sync = *node.mn_sync;
    LogPrintf("BiblePayTrader -- started %f \n", (double)GetAdjustedTime());

    CScript cbScript = GetScriptForMining(jRequest);

    int nInc = 0;
    try
    {
        while (true)
        {
            MinerSleep(1000);
            nInc++;
            if (fThreadInterrupt || ShutdownRequested())
            {
                return;
            }

            // Remove this section on next release (XLM is no longer supported)
            if (false) {
                if (nInc % 10 == 0 && msAssetXLMPublicKey.empty()) {
                    bool f = IsWalletAvailable();
                    if (f) {
                        std::string sXLM = DefaultRecAddress(jRequest, "ASSET-XLM");
                        if (!sXLM.empty()) {
                            std::string sXLMPrivKey = GetTradingBBPPrivateKey(sXLM, jRequest);
                            if (!sXLMPrivKey.empty()) {
                                std::string sSymbol = "XLM";
                                msAssetXLMPublicKey = GetAltPubKey(sSymbol, sXLMPrivKey);
                                LogPrintf("\nBiblePayTrader::XLMPUBKEY %s", msAssetXLMPublicKey);
                            }
                        }
                    }
                }
            }

            // DOGE
            std::string sPrivKey;
            std::string sAssetName = "TRADING-ASSET-DOGE";
            std::string sPub = SearchForAsset(jRequest, "DGZZ", sAssetName, sPrivKey, 25000);

        }
    }
    catch (const std::exception& e)
    {
        LogPrintf("BiblePayTrader_ERROR[0] -- %s", e.what());
    }
    catch (...)
    {
        LogPrintf("BiblePayTrader_ERROR[UNKNOWN] -- %f", (double)GetAdjustedTime());
    }
}


void StartTradingThread(const CChainParams& chainparams, const JSONRPCRequest& jRequest)
{
    CreateWalletIfNotExists(jRequest);
    traderThreads.emplace_back(std::thread(BiblePayTrader, chainparams, jRequest));
}

