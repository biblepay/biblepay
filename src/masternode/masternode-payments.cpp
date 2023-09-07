// Copyright (c) 2014-2021 The DÃSH Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <masternode/activemasternode.h>
#include <governance/governance-classes.h>
#include <masternode/masternode-payments.h>
#include <masternode/masternode-sync.h>
#include <netfulfilledman.h>
#include <netmessagemaker.h>
#include <validation.h>
#include <string>
#include "rpcpog.h"


CMasternodePayments mnpayments;

bool IsOldBudgetBlockValueValid(const CBlock& block, int nBlockHeight, CAmount blockReward, std::string& strErrorRet) {
    const Consensus::Params& consensusParams = Params().GetConsensus();
    bool isBlockRewardValueMet = (block.vtx[0]->GetValueOut() <= (blockReward + ARM64()));

    if (nBlockHeight < consensusParams.nBudgetPaymentsStartBlock) {
        strErrorRet = strprintf("Incorrect block %d, old budgets are not activated yet", nBlockHeight);
        return false;
    }

    if (nBlockHeight >= consensusParams.nSuperblockStartBlock) {
        strErrorRet = strprintf("Incorrect block %d, old budgets are no longer active", nBlockHeight);
        return false;
    }

    // we are still using budgets, but we have no data about them anymore,
    // all we know is predefined budget cycle and window

    int nOffset = nBlockHeight % consensusParams.nBudgetPaymentsCycleBlocks;
    if(nBlockHeight >= consensusParams.nBudgetPaymentsStartBlock &&
       nOffset < consensusParams.nBudgetPaymentsWindowBlocks) {
        // NOTE: old budget system is disabled since 12.1
        if(masternodeSync.IsSynced()) {
            // no old budget blocks should be accepted here on mainnet,
            // testnet/devnet/regtest should produce regular blocks only
            LogPrint(BCLog::GOBJECT, "%s -- WARNING: Client synced but old budget system is disabled, checking block value against block reward\n", __func__);
            if(!isBlockRewardValueMet) {
                strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, old budgets are disabled",
                                        nBlockHeight, block.vtx[0]->GetValueOut(), blockReward);
            }
            return isBlockRewardValueMet;
        }
        // when not synced, rely on online nodes (all networks)
        LogPrint(BCLog::GOBJECT, "%s -- WARNING: Skipping old budget block value checks, accepting block\n", __func__);
        return true;
    }
    if(!isBlockRewardValueMet) {
        strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, block is not in old budget cycle window",
                                nBlockHeight, block.vtx[0]->GetValueOut(), blockReward);
    }
    return isBlockRewardValueMet;
}

/**
* IsBlockValueValid
*
*   Determine if coinbase outgoing created money is the correct value
*
*   Why is this needed?
*   - In BiblePay some blocks are superblocks, which output much higher amounts of coins
*   - Other blocks are 10% lower in outgoing value, so in total, no extra coins are created
*   - When non-superblocks are detected, the normal schedule should be maintained
*/

bool IsBlockValueValid(const CBlock& block, int nBlockHeight, CAmount blockReward, std::string& strErrorRet)
{
	const Consensus::Params& consensusParams = Params().GetConsensus();
    
	if (nBlockHeight < consensusParams.BARLEY_HARVEST_HEIGHT)
		return true;

	// BiblePay
	bool fDailySuperblock = IsDailySuperblock(nBlockHeight);
	if (fDailySuperblock)
	{
		blockReward = GetDailyPaymentsLimit(nBlockHeight);
	}
	// End of Biblepay

    bool isBlockRewardValueMet = (block.vtx[0]->GetValueOut() <= (blockReward + ARM64()));

    strErrorRet = "";

    if (nBlockHeight < consensusParams.nBudgetPaymentsStartBlock) {
        // old budget system is not activated yet, just make sure we do not exceed the regular block reward
        if(!isBlockRewardValueMet) {
            strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, old budgets are not activated yet",
                                    nBlockHeight, block.vtx[0]->GetValueOut(), blockReward);
        }
        return isBlockRewardValueMet;
    } else if (nBlockHeight < consensusParams.nSuperblockStartBlock) {
        // superblocks are not enabled yet, check if we can pass old budget rules
        return IsOldBudgetBlockValueValid(block, nBlockHeight, blockReward, strErrorRet);
    }

    LogPrint(BCLog::MNPAYMENTS, "block.vtx[0]->GetValueOut() %lld <= blockReward %lld\n", block.vtx[0]->GetValueOut(), blockReward);

    CAmount nSuperblockMaxValue =  blockReward + CSuperblock::GetPaymentsLimit(nBlockHeight);
    bool isSuperblockMaxValueMet = (block.vtx[0]->GetValueOut() <= nSuperblockMaxValue);

    LogPrint(BCLog::GOBJECT, "block.vtx[0]->GetValueOut() %lld <= nSuperblockMaxValue %lld\n", block.vtx[0]->GetValueOut(), nSuperblockMaxValue);

    if (!CSuperblock::IsValidBlockHeight(nBlockHeight)) {
        // can't possibly be a superblock, so lets just check for block reward limits
        if (!isBlockRewardValueMet) {
			// 3-2-2022 ARM:
            strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, only regular blocks are allowed at this height",
                                    nBlockHeight, block.vtx[0]->GetValueOut(), blockReward);
        }
        return isBlockRewardValueMet;
    }

    // bail out in case superblock limits were exceeded
    if (!isSuperblockMaxValueMet) {
        strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded superblock max value",
                                nBlockHeight, block.vtx[0]->GetValueOut(), nSuperblockMaxValue);
        return false;
    }

    if(!masternodeSync.IsSynced() || fDisableGovernance) {
        LogPrint(BCLog::MNPAYMENTS, "%s -- WARNING: Not enough data, checked superblock max bounds only\n", __func__);
        // not enough data for full checks but at least we know that the superblock limits were honored.
        // We rely on the network to have followed the correct chain in this case
        return true;
    }

    // we are synced and possibly on a superblock now

    if (!AreSuperblocksEnabled()) {
        // should NOT allow superblocks at all, when superblocks are disabled
        // revert to block reward limits in this case
        LogPrint(BCLog::GOBJECT, "%s -- Superblocks are disabled, no superblocks allowed\n", __func__);
        if(!isBlockRewardValueMet) {
            strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, superblocks are disabled",
                                    nBlockHeight, block.vtx[0]->GetValueOut(), blockReward);
        }
        return isBlockRewardValueMet;
    }

    if (!CSuperblockManager::IsSuperblockTriggered(nBlockHeight)) {
        // we are on a valid superblock height but a superblock was not triggered
        // revert to block reward limits in this case
        if(!isBlockRewardValueMet) {
            strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, no triggered superblock detected",
                                    nBlockHeight, block.vtx[0]->GetValueOut(), blockReward);
        }
        return isBlockRewardValueMet;
    }

    // this actually also checks for correct payees and not only amount
    if (!CSuperblockManager::IsValid(*block.vtx[0], nBlockHeight, blockReward)) {
        // triggered but invalid? that's weird
        LogPrintf("%s -- ERROR: Invalid superblock detected at height %d: %s", __func__, nBlockHeight, block.vtx[0]->ToString()); /* Continued */
        // should NOT allow invalid superblocks, when superblocks are enabled
        strErrorRet = strprintf("invalid superblock detected at height %d", nBlockHeight);
        return false;
    }

    // we got a valid superblock
    return true;
}

bool IsBlockPayeeValid(const CTransaction& txNew, int nBlockHeight, CAmount blockReward, int64_t nTime)
{
    if(fDisableGovernance) {
        //there is no budget data to use to check anything, let's just accept the longest chain
        LogPrint(BCLog::MNPAYMENTS, "%s -- WARNING: Not enough data, skipping block payee checks\n", __func__);
        return true;
    }

    // we are still using budgets, but we have no data about them anymore,
    // we can only check masternode payments

    const Consensus::Params& consensusParams = Params().GetConsensus();

    if(nBlockHeight < consensusParams.nSuperblockStartBlock) {
        // NOTE: old budget system is disabled since 12.1 and we should never enter this branch
        // anymore when sync is finished (on mainnet). We have no old budget data but these blocks
        // have tons of confirmations and can be safely accepted without payee verification
        LogPrint(BCLog::GOBJECT, "%s -- WARNING: Client synced but old budget system is disabled, accepting any payee\n", __func__);
        return true;
    }

    // superblocks started
    // SEE IF THIS IS A VALID SUPERBLOCK

    if(AreSuperblocksEnabled()) {
        if(CSuperblockManager::IsSuperblockTriggered(nBlockHeight)) {
            if(CSuperblockManager::IsValid(txNew, nBlockHeight, blockReward)) {
                LogPrint(BCLog::GOBJECT, "%s -- Valid superblock at height %d: %s", __func__, nBlockHeight, txNew.ToString()); /* Continued */
                // continue validation, should also pay MN
            } else {
                LogPrintf("%s -- ERROR: Invalid superblock detected at height %d: %s", __func__, nBlockHeight, txNew.ToString()); /* Continued */
                // should NOT allow such superblocks, when superblocks are enabled
                return false;
            }
        } else {
            LogPrint(BCLog::GOBJECT, "%s -- No triggered superblock detected at height %d\n", __func__, nBlockHeight);
        }
    } else {
        // should NOT allow superblocks at all, when superblocks are disabled
        LogPrint(BCLog::GOBJECT, "%s -- Superblocks are disabled, no superblocks allowed\n", __func__);
    }

	// BiblePay
	bool fDailySuperblock = IsDailySuperblock(nBlockHeight);
	if (fDailySuperblock)
	{
		bool fValid = ValidateDailySuperblock(txNew, nBlockHeight, nTime);
		if (!fValid)
		{
			LogPrintf("\nIsBlockPayeeValid::ERROR Daily Superblock Validation failed! %f ", nBlockHeight);
			return false;
		}
		else
		{
			return true;
		}
	}

    // Check for correct masternode payment 
    if(CMasternodePayments::IsTransactionValid(txNew, nBlockHeight, blockReward)) {
        LogPrint(BCLog::MNPAYMENTS, "%s -- Valid masternode payment at height %d: %s", __func__, nBlockHeight, txNew.ToString()); /* Continued */
        return true;
    }

    LogPrintf("%s -- ERROR: Invalid masternode payment detected at height %d: %s", __func__, nBlockHeight, txNew.ToString()); /* Continued */
    return false;
}

void FillBlockPayments(CMutableTransaction& txNew, int nBlockHeight, CAmount blockReward, std::vector<CTxOut>& voutMasternodePaymentsRet, std::vector<CTxOut>& voutSuperblockPaymentsRet)
{
    // only create superblocks if spork is enabled AND if superblock is actually triggered
    // (height should be validated inside)
    if(AreSuperblocksEnabled() && CSuperblockManager::IsSuperblockTriggered(nBlockHeight)) {
        LogPrint(BCLog::GOBJECT, "%s -- triggered superblock creation at height %d\n", __func__, nBlockHeight);
        CSuperblockManager::GetSuperblockPayments(nBlockHeight, voutSuperblockPaymentsRet);
    }

    bool fOutInvestorBlock = false;
    if (!CMasternodePayments::GetMasternodeTxOuts(nBlockHeight, blockReward, voutMasternodePaymentsRet, fOutInvestorBlock)) {
        LogPrint(BCLog::MNPAYMENTS, "%s -- no masternode to pay (MN list probably empty)\n", __func__);
    }

    txNew.vout.insert(txNew.vout.end(), voutMasternodePaymentsRet.begin(), voutMasternodePaymentsRet.end());
    txNew.vout.insert(txNew.vout.end(), voutSuperblockPaymentsRet.begin(), voutSuperblockPaymentsRet.end());

    std::string voutMasternodeStr;
    for (const auto& txout : voutMasternodePaymentsRet) {
        // subtract MN payment from miner reward
        txNew.vout[0].nValue -= txout.nValue;
        if (!voutMasternodeStr.empty())
            voutMasternodeStr += ",";
        voutMasternodeStr += txout.ToString();
    }

    if (fOutInvestorBlock)
    {
        // Investors get 50%, sancs get 100%
        txNew.vout[0].nValue = (txNew.vout[0].nValue * 5000) / 10000;
    }

    // BiblePay
    
    const CChainParams& chainparams = Params();
    if (nBlockHeight >= chainparams.GetConsensus().REDSEA_HEIGHT)
    {
        txNew.vout[0].scriptPubKey = txNew.vout[1].scriptPubKey;
    }

	// BiblePay Daily Superblock
	bool fSuperblock = IsDailySuperblock(nBlockHeight);
	if (fSuperblock)
	{
		std::vector<Portfolio> p = GetDailySuperblock(nBlockHeight);
		for (int i = 0; i < p.size(); i++)
		{
			CScript spkPayee = GetScriptForDestination(DecodeDestination(p[i].OwnerAddress));
			txNew.vout.insert(txNew.vout.end(), CTxOut(p[i].Owed * COIN, spkPayee));
		}
	}

    LogPrint(BCLog::MNPAYMENTS, "%s -- nBlockHeight %d blockReward %lld voutMasternodePaymentsRet \"%s\" txNew %s", __func__, /* Continued */
                            nBlockHeight, blockReward, voutMasternodeStr, txNew.ToString());
}

/**
*   GetMasternodeTxOuts
*
*   Get masternode payment tx outputs
*/

bool CMasternodePayments::GetMasternodeTxOuts(int nBlockHeight, CAmount blockReward, 
    std::vector<CTxOut>& voutMasternodePaymentsRet, bool& fOutInvestorBlock)
{
    // make sure it's not filled yet
    voutMasternodePaymentsRet.clear();

    if(!GetBlockTxOuts(nBlockHeight, blockReward, voutMasternodePaymentsRet, fOutInvestorBlock)) {
        LogPrintf("CMasternodePayments::%s -- no payee (deterministic masternode list empty)\n", __func__);
        return false;
    }

    for (const auto& txout : voutMasternodePaymentsRet) {
        CTxDestination dest;
        ExtractDestination(txout.scriptPubKey, dest);

        LogPrintf("CMasternodePayments::%s -- Masternode payment %lld to %s\n", __func__, txout.nValue, EncodeDestination(dest));
    }

    return true;
}

bool CMasternodePayments::GetBlockTxOuts(int nBlockHeight, CAmount blockReward,
    std::vector<CTxOut>& voutMasternodePaymentsRet, bool& fOutInvestorBlock)
{
    voutMasternodePaymentsRet.clear();

    const CBlockIndex* pindex;
    int nReallocActivationHeight{std::numeric_limits<int>::max()};

    {
        LOCK(cs_main);
        pindex = chainActive[nBlockHeight - 1];

        const Consensus::Params& consensusParams = Params().GetConsensus();
        if (VersionBitsState(pindex, consensusParams, Consensus::DEPLOYMENT_REALLOC, versionbitscache) == ThresholdState::ACTIVE) {
            nReallocActivationHeight = VersionBitsStateSinceHeight(pindex, consensusParams, Consensus::DEPLOYMENT_REALLOC, versionbitscache);
        }
    }

    CAmount masternodeReward = GetMasternodePayment(nBlockHeight, blockReward, nReallocActivationHeight);

    auto dmnPayee = deterministicMNManager->GetListForBlock(pindex).GetMNPayee();
    if (!dmnPayee) {
        return false;
    }

	// POVS (Proof-of-video-streaming) - R ANDREWS - 3-29-2022
	if (pindex != NULL)
	{
		double nBanning = 1;
		int64_t nElapsed = GetAdjustedTime() - pindex->GetBlockTime();
		if (nElapsed < (60 * 60 * 24) && nBanning == 1)
		{
            std::string sKey = dmnPayee->pdmnState->pubKeyOperator.Get().ToString();
        	int nStatus = mapPOVSStatus[sKey];
            int nPoseScore = dmnPayee->pdmnState->nPoSePenalty;
            // Note, the nStatus value will be 255 when the BMS POSE = 800 (that means their BMS endpoint is down)
			if (nPoseScore > 0 || nStatus == 255)
			{
                // Investors get 50%, sancs get 100%
                masternodeReward = (masternodeReward * 5000) / 10000;
                fOutInvestorBlock = true;
			}
		}
	}
	// End of POVS

    CAmount operatorReward = 0;
    if (dmnPayee->nOperatorReward != 0 && dmnPayee->pdmnState->scriptOperatorPayout != CScript()) {
        // This calculation might eventually turn out to result in 0 even if an operator reward percentage is given.
        // This will however only happen in a few years when the block rewards drops very low.
        operatorReward = (masternodeReward * dmnPayee->nOperatorReward) / 10000;
        masternodeReward -= operatorReward;
    }

    if (masternodeReward > 0) {
        voutMasternodePaymentsRet.emplace_back(masternodeReward, dmnPayee->pdmnState->scriptPayout);
    }
    if (operatorReward > 0) {
        voutMasternodePaymentsRet.emplace_back(operatorReward, dmnPayee->pdmnState->scriptOperatorPayout);
    }

    return true;
}

bool VectorContainsAddress(std::vector<CTxOut> v, std::string sMyAddress)
{
    for (const auto& txout : v) 
    {
    	std::string sRecipient1 = PubKeyToAddress(txout.scriptPubKey);
		if (sRecipient1 == sMyAddress)
            return true;
    }
    return false;
}

bool CMasternodePayments::IsTransactionValid(const CTransaction& txNew, int nBlockHeight, CAmount blockReward)
{
    if (!deterministicMNManager->IsDIP3Enforced(nBlockHeight)) {
        // can't verify historical blocks here
        return true;
    }

    std::vector<CTxOut> voutMasternodePayments;
    bool fOutInvestorBlock = false;
    if (!GetBlockTxOuts(nBlockHeight, blockReward, voutMasternodePayments, fOutInvestorBlock)) {
        LogPrintf("CMasternodePayments::%s -- ERROR failed to get payees for block at height %s\n", __func__, nBlockHeight);
        return true;
    }

    for (const auto& txout : voutMasternodePayments) {
        bool found = false;
        for (const auto& txout2 : txNew.vout) {
            if (txout == txout2) {
                found = true;
                break;
            }
			else
			{
				// POVS (Proof-of-video-streaming)
				std::string sRecipient1 = PubKeyToAddress(txout.scriptPubKey);
				std::string sRecipient2 = PubKeyToAddress(txout2.scriptPubKey);
				// txout2 contains the 'purported' values (from the network); txout contains the sanctuaries calculated values
				CAmount nAmount1 = txout.nValue;
				CAmount nAmount2 = txout2.nValue;
                if (sRecipient1 == sRecipient2) 
                {
                    found = true;
                    break;
                }
			}
        }
        if (!found) {
            CTxDestination dest;
            if (!ExtractDestination(txout.scriptPubKey, dest))
                assert(false);
            LogPrintf("CMasternodePayments::%s -- ERROR/WARNING failed to find expected payee %s in block at height %s\n", __func__, EncodeDestination(dest), nBlockHeight);
			return false;
        }
    }
    // (POVS) Verify the Sanc mined the block too:
    const CChainParams& chainparams = Params();
    if (nBlockHeight >= chainparams.GetConsensus().REDSEA_HEIGHT)
    {
            if (txNew.vout.size() >= 2)
            {
                std::string sRecip1 = PubKeyToAddress(txNew.vout[0].scriptPubKey);
	    	    std::string sRecip2 = PubKeyToAddress(txNew.vout[1].scriptPubKey);
                bool fOK1 = VectorContainsAddress(voutMasternodePayments, sRecip1);
                bool fOK2 = VectorContainsAddress(voutMasternodePayments, sRecip2);
                if (sRecip1 != sRecip2 || !fOK1 || !fOK2)
                {
                    LogPrintf("IsTransactionValid::ERROR::Sanc %s and %s did not mine the block at height %f\n", sRecip1, sRecip2, nBlockHeight);
			        return false;
                }
            }
            else
            {
                LogPrintf("IsTransactionValid::ERROR::Error at height %f due to bad coinbase length\n", nBlockHeight);
                return false;
            }
    }
    
    return true;
}
