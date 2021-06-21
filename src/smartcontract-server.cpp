// Copyright (c) 2014-2019 The Dash Core Developers, The DAC Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "smartcontract-server.h"
#include "util.h"
#include "utilmoneystr.h"
#include "smartcontract-client.h"
#include "init.h"
#include "messagesigner.h"
#include "spork.h"
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string/case_conv.hpp> // for to_lower()
#include <boost/algorithm/string.hpp> // for trim(), and case insensitive compare
#include <boost/date_time/posix_time/posix_time.hpp> // for StringToUnixTime()
#include <math.h>       /* round, floor, ceil, trunc */
#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <openssl/crypto.h>
#include <stdint.h>
#include <univalue.h>
#include <fstream>
#include <masternode/masternode-sync.h>
#include <masternode/activemasternode.h>
#include "rpcutxo.h"
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <cmath>


//////////////////////////////////////////////////////////////////////////////// Cameroon-One & Kairos  /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

double GetBBPUSDPrice()
{
	static int64_t nLastPriceCheck = 0;
	int64_t nElapsed = GetAdjustedTime() - nLastPriceCheck;
	static double nLastPrice = 0;
	if (nElapsed < (60 * 60) && nLastPrice > 0)
	{
		return nLastPrice;
	}
	nLastPriceCheck = GetAdjustedTime();
	double dPriorPrice = 0;
	double dPriorPhase = 0;
	double out_BTC = 0;
	double out_BBP = 0;
	nLastPrice = GetPBase(out_BTC, out_BBP);
	return nLastPrice;
}


//////////////////////////////////////////////////////////////////////////////// Watchman-On-The-Wall /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//								                          			DAC's version of The Sentinel, March 31st, 2019                                                                                                  //
//                                                                                                                                                                                                                   //

DACProposal GetProposalByHash(uint256 govObj, int nLastSuperblock)
{
	int nSancCount = deterministicMNManager->GetListAtChainTip().GetValidMNsCount();
	int nMinPassing = nSancCount * .10;
	if (nMinPassing < 1) nMinPassing = 1;
	CGovernanceObject* myGov = governance.FindGovernanceObject(govObj);
	UniValue obj = myGov->GetJSONObject();
	DACProposal dacProposal;
	// 8-6-2020 - R ANDREWS - Make resilient to prevent crashes
	dacProposal.sName = obj["name"].getValStr();
	dacProposal.nStartEpoch = cdbl(obj["start_epoch"].getValStr(), 0);
	dacProposal.nEndEpoch = cdbl(obj["end_epoch"].getValStr(), 0);
	dacProposal.sURL = obj["url"].getValStr();
	dacProposal.sExpenseType = obj["expensetype"].getValStr();
	dacProposal.nAmount = cdbl(obj["payment_amount"].getValStr(), 2);
	dacProposal.sAddress = obj["payment_address"].getValStr();
	dacProposal.uHash = myGov->GetHash();
	dacProposal.nHeight = GetHeightByEpochTime(dacProposal.nStartEpoch);
	dacProposal.nMinPassing = nMinPassing;
	dacProposal.nYesVotes = myGov->GetYesCount(VOTE_SIGNAL_FUNDING);
	dacProposal.nNoVotes = myGov->GetNoCount(VOTE_SIGNAL_FUNDING);
	dacProposal.nAbstainVotes = myGov->GetAbstainCount(VOTE_SIGNAL_FUNDING);
	dacProposal.nNetYesVotes = myGov->GetAbsoluteYesCount(VOTE_SIGNAL_FUNDING);
	dacProposal.nLastSuperblock = nLastSuperblock;
	dacProposal.sProposalHRTime = TimestampToHRDate(dacProposal.nStartEpoch);
	dacProposal.fPassing = dacProposal.nNetYesVotes >= nMinPassing;
	dacProposal.fIsPaid = dacProposal.nHeight < nLastSuperblock;
	return dacProposal;
}

std::string DescribeProposal(DACProposal dacProposal)
{
	std::string sReport = "Proposal StartDate: " + dacProposal.sProposalHRTime + ", Hash: " + dacProposal.uHash.GetHex() + " for Amount: " + RoundToString(dacProposal.nAmount, 2) + CURRENCY_NAME + ", Name: " 
				+ dacProposal.sName + ", ExpType: " + dacProposal.sExpenseType + ", PAD: " + dacProposal.sAddress 
				+ ", Height: " + RoundToString(dacProposal.nHeight, 0) 
				+ ", Votes: " + RoundToString(dacProposal.nNetYesVotes, 0) + ", LastSB: " 
				+ RoundToString(dacProposal.nLastSuperblock, 0);
	return sReport;
}

std::vector<DACProposal> GetWinningSanctuarySporkProposals()
{
	int nStartTime = GetAdjustedTime() - (86400 * 7);
	// NOTE: Sanctuary sporks occur every week, and expire 7 days after creation.  They should be voted on regularly.
	int nLastSuperblock = 0;
	int nNextSuperblock = 0;
	GetGovSuperblockHeights(nNextSuperblock, nLastSuperblock);
    LOCK2(cs_main, governance.cs);
    std::vector<const CGovernanceObject*> objs = governance.GetAllNewerThan(nStartTime);
	std::vector<DACProposal> vSporks;
	for (const CGovernanceObject* pGovObj : objs) 
    {
		if (pGovObj->GetObjectType() != GOVERNANCE_OBJECT_PROPOSAL) continue;
		DACProposal dacProposal = GetProposalByHash(pGovObj->GetHash(), nLastSuperblock);
		// We need proposals that are sporks, that are older than 48 hours that are not expired
		int64_t nAge = GetAdjustedTime() - dacProposal.nStartEpoch;
		if (dacProposal.sExpenseType == "XSPORK-ORPHAN" || dacProposal.sExpenseType == "XSPORK-CHARITY" || dacProposal.sExpenseType == "XSPORK-EXPENSE" || dacProposal.sExpenseType == "SPORK")
		{
			if (nAge > (60*60*24*1) && dacProposal.fPassing)
			{
				// spork elements are contained in dacProposal.sName, and URL in .sURL
				vSporks.push_back(dacProposal);
				LogPrintf("\nSporkProposal Detected %s ", dacProposal.sName);
			}
		}
	}
	return vSporks;
}

std::string WatchmanOnTheWall(bool fForce, std::string& sContract)
{
	if (!fMasternodeMode && !fForce)   
		return "NOT_A_WATCHMAN_SANCTUARY";
	if (!chainActive.Tip()) 
		return "WATCHMAN_INVALID_CHAIN";
	if (!ChainSynced(chainActive.Tip()))
		return "WATCHMAN_CHAIN_NOT_SYNCED";

	const Consensus::Params& consensusParams = Params().GetConsensus();
	int MIN_EPOCH_BLOCKS = consensusParams.nSuperblockCycle * .07; // TestNet Weekly superblocks (1435), Prod Monthly superblocks (6150), this means a 75 block warning in TestNet, and a 210 block warning in Prod

	int nLastSuperblock = 0;
	int nNextSuperblock = 0;
	GetGovSuperblockHeights(nNextSuperblock, nLastSuperblock);

	int nSancCount = deterministicMNManager->GetListAtChainTip().GetValidMNsCount();

	std::string sReport;

	int nBlocksUntilEpoch = nNextSuperblock - chainActive.Tip()->nHeight;
	if (nBlocksUntilEpoch < 0)
		return "WATCHMAN_LOW_HEIGHT";

	if (nBlocksUntilEpoch < MIN_EPOCH_BLOCKS && !fForce)
		return "WATCHMAN_TOO_EARLY_FOR_COMING";

	int nStartTime = GetAdjustedTime() - (86400 * 32);
    LOCK2(cs_main, governance.cs);
    std::vector<const CGovernanceObject*> objs = governance.GetAllNewerThan(nStartTime);
	std::vector<std::pair<int, uint256> > vProposalsSortedByVote;
	vProposalsSortedByVote.reserve(objs.size() + 1);
    
	for (const CGovernanceObject* pGovObj : objs) 
    {
		if (pGovObj->GetObjectType() != GOVERNANCE_OBJECT_PROPOSAL) continue;
		DACProposal dacProposal = GetProposalByHash(pGovObj->GetHash(), nLastSuperblock);
		// We need unpaid, passing that fit within the budget
		sReport = DescribeProposal(dacProposal);
		if (!dacProposal.fIsPaid)
		{
			if (dacProposal.fPassing)
			{
				LogPrintf("\n Watchman::Inserting %s for NextSB: %f", sReport, (double)nNextSuperblock);
				vProposalsSortedByVote.push_back(std::make_pair(dacProposal.nNetYesVotes, dacProposal.uHash));
			}
			else
			{
				LogPrintf("\n Watchman (not inserting) %s because we have Votes %f (req votes %f)", sReport, dacProposal.nNetYesVotes, dacProposal.nMinPassing);
			}
		}
		else
		{
			LogPrintf("\n Watchman (Found Paid) %s ", sReport);
		}
	}
	// Now we need to sort the vector of proposals by Vote descending
	std::sort(vProposalsSortedByVote.begin(), vProposalsSortedByVote.end());
	std::reverse(vProposalsSortedByVote.begin(), vProposalsSortedByVote.end());
	// Now lets only move proposals that fit in the budget
	std::vector<std::pair<double, uint256> > vProposalsInBudget;
	vProposalsInBudget.reserve(objs.size() + 1);
    
	CAmount nPaymentsLimit = CSuperblock::GetPaymentsLimit(nNextSuperblock, false);
	CAmount nSpent = 0;
	for (auto item : vProposalsSortedByVote)
    {
		DACProposal p = GetProposalByHash(item.second, nLastSuperblock);
		if (((p.nAmount * COIN) + nSpent) < nPaymentsLimit)
		{
			nSpent += (p.nAmount * COIN);
			vProposalsInBudget.push_back(std::make_pair(p.nAmount, p.uHash));
			sReport = DescribeProposal(p);
			LogPrintf("\n Watchman::Adding Budget Proposal %s -- Running Total %f ", sReport, (double)nSpent/COIN);
		}
    }
	// Create the contract
	std::string sAddresses;
	std::string sPayments;
	std::string sHashes;
	std::string sVotes;
	for (auto item : vProposalsInBudget)
    {
		DACProposal p = GetProposalByHash(item.second, nLastSuperblock);
		if (ValidateAddress2(p.sAddress) && p.nAmount > .01)
		{
			sAddresses += p.sAddress + "|";
			sPayments += RoundToString(p.nAmount, 2) + "|";
			sHashes += p.uHash.GetHex() + "|";
			sVotes += RoundToString(p.nNetYesVotes, 0) + "|";
		}
	}
	if (sPayments.length() > 1) 
		sPayments = sPayments.substr(0, sPayments.length() - 1);
	if (sAddresses.length() > 1)
		sAddresses = sAddresses.substr(0, sAddresses.length() - 1);
	if (sHashes.length() > 1)
		sHashes = sHashes.substr(0, sHashes.length() - 1);
	if (sVotes.length() > 1)
		sVotes = sVotes.substr(0, sVotes.length() -1);

	sContract = "<ADDRESSES>" + sAddresses + "</ADDRESSES><PAYMENTS>" + sPayments + "</PAYMENTS><PROPOSALS>" + sHashes + "</PROPOSALS>";

	uint256 uGovObjHash = uint256S("0x0");
	uint256 uPamHash = GetPAMHashByContract(sContract);
	int iTriggerVotes = 0;
	std::string sQTData;
	GetGSCGovObjByHeight(nNextSuperblock, uPamHash, iTriggerVotes, uGovObjHash, sAddresses, sPayments, sQTData);
	std::string sError;

	if (sPayments.empty())
	{
		return "EMPTY_CONTRACT";
	}
	sContract += "<VOTES>" + RoundToString(iTriggerVotes, 0) + "</VOTES><METRICS><HASH>" + uGovObjHash.GetHex() + "</HASH><PAMHASH>" 
		+ uPamHash.GetHex() + "</PAMHASH><SANCTUARYCOUNT>" + RoundToString(nSancCount, 0) + "</SANCTUARYCOUNT></METRICS><VOTEDATA>" + sVotes + "</VOTEDATA>";

	if (uGovObjHash == uint256S("0x0"))
	{
		std::string sWatchmanTrigger = SerializeSanctuaryQuorumTrigger(nNextSuperblock, nNextSuperblock, GetAdjustedTime(), sContract);
		std::string sGobjectHash;
		SubmitGSCTrigger(sWatchmanTrigger, sGobjectHash, sError);
		LogPrintf("**WatchmanOnTheWall::SubmitWatchmanTrigger::CreatingWatchmanContract hash %s , gobject %s, results %s **\n", sWatchmanTrigger, sGobjectHash, sError);
		sContract += "<ACTION>CREATING_CONTRACT</ACTION>";
		return "WATCHMAN_CREATING_CONTRACT";
	}
	else if (iTriggerVotes < (nSancCount / 2))
	{
		bool bResult = VoteForGSCContract(nNextSuperblock, sContract, sError);
		LogPrintf("**WatchmanOnTheWall::VotingForWatchmanTrigger PAM Hash %s, Trigger Votes %f  (%s)", uPamHash.GetHex(), (double)iTriggerVotes, sError);
		sContract += "<ACTION>VOTING</ACTION>";
		return "WATCHMAN_VOTING";
	}

	return "WATCHMAN_SUCCESS";
}


//////////////////////////////////////////////////////////////////////////////// GSC Server side Abstraction Interface ////////////////////////////////////////////////////////////////////////////////////////////////


// Decentralized Autonomous Charity - Allocation Engine 1.0
// R Andrews - BiblePay - 12-30-2020
// This engine groups our charity expenses by Charity Name, then Sums the monthly commitments per orphan by Charity Group.
// Then we arrive at a set of percentages due per charity for this given month.
// For example, lets say we have $1000 due to Cameroon-One, and $500 due to Kairos, and $500 due to SAI in this given month. 
// This engine would return .50, .25, .25, Cameroon-One, Kairos, SAI.
// The DAC donation given on the day prior would be allocated by these percentages and these would be forwarded to the charity addresses (for BBP) automatically - with no user intervention.
// Note that our monthly governance vote does not and is not related to this process - this process is designed to make a fully autonomous and automatic charity.

// Our sanctuaries have the right to vote out corrupt charities and vote in new charities.  They may also vote out orphan commitments and vote in new orphans, etc.
// Since all of this data is stored on the blockchain, the history will be permanent, therefore we have a very high propensity to make positive vetted charity relationships to maintain 100% integrity.

// To our knowledge, every single expense from September 2017 (inception) until today (Dec 2020) has reached real orphan-charity children (at over 75% efficiency) meaning we have met our goals, but our size is still small.  May God increase our market share globally so we can help more children.
std::map<std::string, double> DACEngine(std::map<std::string, Orphan>& mapOrphans, std::map<std::string, Expense>& mapExpenses)
{
	std::map<std::string, double> mapDAC;
	std::map<std::string, double> mapCharityCommitments;
	std::map<std::string, std::string> mapCharities;
	int iOrphanCount = 0;
	double nTotalMonthlyCommitments = 0;
	mapOrphans.clear();
	for (auto ii : mvApplicationCache) 
	{
		if (Contains(ii.first, "XSPORK-ORPHAN"))
		{
			std::pair<std::string, int64_t> v = mvApplicationCache[ii.first];
			int64_t nTimestamp = v.second;
			/*
				// Orphan record format:
				XSPORK-ORPHAN (TYPE) | (VALUE1) orphan-id | (RECORD) (charity, name, URL, monthly Amount)
			*/

			Orphan o;
			o.OrphanID = GetElement(ii.first, "[-]", 1);
			o.Charity = GetElement(v.first, "[-]", 0);
			boost::to_lower(o.Charity);
			o.Name = GetElement(v.first, "[-]", 1);
			o.URL = GetElement(v.first, "[-]", 2);
			o.MonthlyAmount = cdbl(GetElement(v.first, "[-]", 3), 2);
			if (false)
				LogPrintf("\nORPHAN childid %s, Charity %s, Name %s, URL %s ", 
				o.OrphanID, o.Charity, o.Name, o.URL);

			if (o.MonthlyAmount > 0 && !o.Charity.empty())
			{
				mapCharityCommitments[o.Charity] += o.MonthlyAmount;
				nTotalMonthlyCommitments += o.MonthlyAmount;
				iOrphanCount++;
				mapOrphans[o.OrphanID] = o;
			}
		}
		else if (Contains(ii.first, "XSPORK-CHARITY"))
		{
			std::pair<std::string, int64_t> v = mvApplicationCache[ii.first];
			// XSPORK-CHARITY (TYPE) | (VALUE) Charity-Name | (RECORD) (BBPAddress, URL)
			std::string sCharity = GetElement(ii.first, "[-]", 1);
			boost::to_lower(sCharity);
			std::string sAddress = GetElement(v.first, "[-]", 0);
			std::string sCharityURL = GetElement(v.first, "[-]", 1);
			LogPrintf("\nCHARITY charity %s, address %s, URL %s ", sCharity, sAddress, sCharityURL);
			mapCharities[sCharity] = sAddress;
		}
		else if (Contains(ii.first, "XSPORK-EXPENSE"))
		{
			// XSPORK-EXPENSE (TYPE) | (VALUE1) expense-id | (RECORD) (added, charity, bbpamount, usdamount)
			std::pair<std::string, int64_t> v = mvApplicationCache[ii.first];
			Expense e;
			e.ExpenseID = GetElement(ii.first, "[-]", 1);
			e.Added = GetElement(v.first, "[-]", 0);
			e.Charity = GetElement(v.first, "[-]", 1);
			boost::to_lower(e.Charity);
			e.nBBPAmount = cdbl(GetElement(v.first, "[-]", 2), 2);
			e.nUSDAmount = cdbl(GetElement(v.first, "[-]", 3), 2);
			mapExpenses[e.ExpenseID] = e;
			
		}
	}
	for (auto d : mapCharityCommitments)
	{
		double npct = d.second / (nTotalMonthlyCommitments+.01);
		std::string sCharity = d.first;
		std::string sAddress = mapCharities[sCharity];
		LogPrint(BCLog::NET, "\nSmartContractServer::DAC  Found charity %s with allocated amount %f to address %s and orphan count %f and TMC of %f ", sCharity, npct, sAddress, iOrphanCount, nTotalMonthlyCommitments);
		// If address is valid, and amount is > 0:
		if (!sAddress.empty() && npct > 0)
		{
			CTxDestination dest = DecodeDestination(sAddress);
		    bool isValid = IsValidDestination(dest);
			if (isValid)
			{
				mapDAC[sAddress] = npct;
			}
		}
	}
	return mapDAC;
}

std::string GetGSCContract(int nHeight, bool fCreating)
{
	int nNextSuperblock = 0;
	int nLast = GetLastGSCSuperblockHeight(chainActive.Tip()->nHeight, nNextSuperblock);
	if (nHeight != 0) 
		nLast = nHeight;
	std::string sContract = AssessBlocks(nLast, fCreating);
	return sContract;
}

bool VoteForGobject(uint256 govobj, std::string sVoteSignal, std::string sVoteOutcome, std::string& sError)
{

	if (sVoteSignal != "funding" && sVoteSignal != "delete")
	{
		LogPrintf("Sanctuary tried to vote in a way that is prohibited.  Vote failed. %s", sVoteSignal);
		return false;
	}

	vote_signal_enum_t eVoteSignal = CGovernanceVoting::ConvertVoteSignal(sVoteSignal);
	vote_outcome_enum_t eVoteOutcome = CGovernanceVoting::ConvertVoteOutcome(sVoteOutcome);
	int nSuccessful = 0;
	int nFailed = 0;
	int govObjType;
	{
        LOCK(governance.cs);
        CGovernanceObject *pGovObj = governance.FindGovernanceObject(govobj);
        if (!pGovObj) 
		{
			sError = "Governance object not found";
			return false;
        }
        govObjType = pGovObj->GetObjectType();
    }
	
    auto dmn = deterministicMNManager->GetListAtChainTip().GetValidMNByCollateral(activeMasternodeInfo.outpoint);

    if (!dmn) 
	{
        sError = "Can't find masternode by collateral output";
		return false;
    }

    CGovernanceVote vote(dmn->collateralOutpoint, govobj, eVoteSignal, eVoteOutcome);

    bool signSuccess = false;
    if (govObjType == GOVERNANCE_OBJECT_PROPOSAL && eVoteSignal == VOTE_SIGNAL_FUNDING)
    {
        sError = "Can't use vote-conf for proposals when deterministic masternodes are active";
        return false;
    }
    if (activeMasternodeInfo.blsKeyOperator)
    {
        signSuccess = vote.Sign(*activeMasternodeInfo.blsKeyOperator);
    }

    if (!signSuccess)
	{
        sError = "Failure to sign.";
		return false;
	}

    CGovernanceException exception;
    if (governance.ProcessVoteAndRelay(vote, exception, *g_connman)) 
	{
        nSuccessful++;
    } else {
        nFailed++;
    }

    return (nSuccessful > 0) ? true : false;
   
}

bool NickNameExists(std::string sProjectName, std::string sNickName, bool& fIsMine)
{
	std::string sUserCPK = DefaultRecAddress("Christian-Public-Key");

	std::map<std::string, CPK> mAllCPKs = GetGSCMap(sProjectName, "", true);
	boost::to_upper(sNickName);
	for (std::pair<std::string, CPK> a : mAllCPKs)
	{
		if (boost::iequals(a.second.sNickName, sNickName))
		{
			if (sUserCPK == a.second.sAddress)
				fIsMine = true;
			return true;
		}
	}
	return false;
}

std::string GetStringElement(std::string sData, std::string sDelimiter, int iElement)
{
	std::vector<std::string> vP = Split(sData.c_str(), sDelimiter);
	if ((iElement+1) > sData.size())
		return std::string();
	return vP[iElement];
}
		
std::string ExtractBlockMessage(int nHeight)
{
	CBlockIndex* pindex = FindBlockByHeight(nHeight);
	const Consensus::Params& consensusParams = Params().GetConsensus();
	std::string sMessage;
	if (pindex != NULL)
	{
		CBlock block;
		if (ReadBlockFromDisk(block, pindex, consensusParams)) 
		{
			for (unsigned int i = 0; i < block.vtx[0]->vout.size(); i++)
			{
				sMessage += block.vtx[0]->vout[i].sTxOutMessage;
			}
			return sMessage;
		}
	}
	return std::string();
}

double ExtractAPM(int nHeight)
{
	double nAPMHeight = GetSporkDouble("APM", 0);
	if (nHeight < nAPMHeight || nAPMHeight == 0)
		return 0;
	
    const CBlockIndex* pindex;
    {
        LOCK(cs_main);
        pindex = chainActive[nHeight];
    }

	if (pindex != NULL)
	{
		int64_t nAge = GetAdjustedTime() - pindex->GetBlockTime();
		if (nAge > (60 * 60 * 24 * 30))
			return 0;
	}

	int nNextSuperblock = 0;
	int nLastSuperblock = GetLastGSCSuperblockHeight(nHeight, nNextSuperblock);
	double nAPM = cdbl(ExtractXML(ExtractBlockMessage(nLastSuperblock), "<qtphase>", "</qtphase>"), 0);
	return nAPM;
}

double CalculateAPM(int nHeight)
{
	// Automatic Price Mooning - July 21, 2020
	double nAPMHeight = GetSporkDouble("APM", 0);
	if (nHeight < nAPMHeight || nHeight < 1 || nAPMHeight == 0)
		return 0;
	double out_BTC = 0;
	double out_BBP = 0;
	double dPrice = GetPBase(out_BTC, out_BBP);
	double dLastPrice = cdbl(ExtractXML(ExtractBlockMessage(nHeight), "<bbpprice>", "</bbpprice>"), 12);
	if (dLastPrice == 0 && nHeight > BLOCKS_PER_DAY * 2)
	{
		// In case BBP missed a day (somehow), one more try using the previous day as the prior price:
		nHeight -= BLOCKS_PER_DAY;
		dLastPrice = cdbl(ExtractXML(ExtractBlockMessage(nHeight), "<bbpprice>", "</bbpprice>"), 12);
	}
	double nResult = 0;
	if (dLastPrice == 0 || out_BBP == 0)
	{
		// Price is missing for one of the two days
		nResult = -1;
	}
	else if (dLastPrice == out_BBP)
	{
		// Price has not changed
		nResult = 1;
	}
	else if (dLastPrice < out_BBP)
	{
		// Price has INCREASED!  YES!
		nResult = 2;
	}
	else if (dLastPrice > out_BBP)
	{
		// Price has DECREASED -- BOO.
		nResult = 3;
	}

	/*
	LogPrintf("CalculateAPM::Result==%f::LastHeight %f Price %s, Current Price %s", 
		nResult, nHeight, RoundToString(dLastPrice, 12), RoundToString(out_BBP, 12));
		*/
	return nResult;
}


std::string AssessBlocks(int nHeight, bool fCreatingContract)
{
	LogPrint(BCLog::NET, "\nAssessBlocks Height %f time=%f", nHeight, GetAdjustedTime());

	CAmount nPaymentsLimit = CSuperblock::GetPaymentsLimit(nHeight, false);

	nPaymentsLimit -= MAX_BLOCK_SUBSIDY * COIN;
	std::map<std::string, CPK> mPoints;
	std::map<std::string, double> mCampaignPoints;
	std::map<std::string, CPK> mCPKCampaignPoints;
	std::map<std::string, double> mCampaigns;
	std::map<std::string, int> mUsedUTXO;

	double dDebugLevel = cdbl(gArgs.GetArg("-debuglevel", "0"), 0);
	std::string sAnalyzeUser = ReadCache("analysis", "user");
	std::string sAnalysisData1;
	std::string sDiaries;
	
	// UTXO STAKING
	std::vector<UTXOStake> uStakes = GetUTXOStakes(true);
	std::vector<ReferralCode> uRC = GetClaimedReferralCodes();
	std::vector<ReferralCode> vGRC = GetGeneratedReferralCodes();
	double dEnabled = GetSporkDouble("UTXOStakingEnabled", 0);
	std::map<std::string, std::string> mapSubCampaigns;

	if (dEnabled == 1)
	{
		for (auto d: uStakes)
		{
			bool fUsed = false;
			if (!d.Address.empty() && mUsedUTXO[d.Address] == 1)
				fUsed = true;
			// End of BBP (Extra Anti Duplication)
			if (!fUsed && d.nValueUSD > 0)
			{
				if (!d.Address.empty())
						mUsedUTXO[d.Address] = 1;
				std::string sThisCampaign = "UTXO";
				std::string sSubCampaign = d.Ticker;

				mapSubCampaigns[sSubCampaign] = sSubCampaign;
				// Entry into the UTXO campaign
				UserRecord u = GetUserRecord(d.CPK);
				{
					// CPK high level campaign reward
					CPK c = mPoints[d.CPK];
					c.sCampaign = sThisCampaign;
					c.sAddress = d.CPK;
					c.sNickName = u.NickName;
					// Referral codes
					UniValue details;
					ReferralCode rc1 = GetTotalPortfolioImpactFromReferralCodes(vGRC, uRC, uStakes, d.CPK, details);
					CAmount nCurGiftAmount = 0;
					double nUSDCurGiftAmount = 0;
					if (d.Ticker == "BBP")
					{
						nCurGiftAmount = rc1.dGiftAmount * COIN;
						nUSDCurGiftAmount = GetUSDValueBBP(nCurGiftAmount);
					}
					double nUSDValue = d.nValueUSD + nUSDCurGiftAmount;
					double nPoints = nUSDValue * d.nCoverage * rc1.ReferralRewards;
					c.nPoints += nPoints;
					c.nCurrencyAmount += d.nForeignTotal + d.nNativeTotal + nCurGiftAmount;

					c.sCurrencyAddress = d.Address;
					mCampaignPoints[sThisCampaign] += nPoints;
					mPoints[d.CPK] = c;
					/*
					LogPrintf("\nAssessBlocks::Cpk %s addr %s amount %f AdjAmount %f, Points %f, GiftAmt %f ", d.CPK, d.Address, d.nValueUSD, nAdjAmt, nPoints, rc1.dGiftAmount);
					*/

					// Details for CPK-Campaign-Address
					CPK cCPKCampaignPoints = mCPKCampaignPoints[d.CPK + sSubCampaign];
					cCPKCampaignPoints.sAddress = d.CPK;
					cCPKCampaignPoints.sNickName = c.sNickName;
					cCPKCampaignPoints.nPoints += nPoints;
					cCPKCampaignPoints.sCurrencyAddress = d.Address;
					
					cCPKCampaignPoints.nCurrencyAmount = d.nForeignTotal + d.nNativeTotal + nCurGiftAmount;
					mCPKCampaignPoints[d.CPK + sSubCampaign] = cCPKCampaignPoints;
					/*
					LogPrintf("\nAssessBlocks-lowlevel cpk %s addr %s amount %f  subcampaign %s", d.CPK, d.Address, d.nValueUSD, sSubCampaign);
					*/

				}
			}
		}
	}
	// End of UTXO Staking
		
	
	std::string sData;
	std::string sGenData;
	std::string sDetails;
	double nTotalPoints = 0;
	// Convert To Campaign-CPK-Prominence levels
	std::string sAnalysisData2;
	for (auto myCampaign : mCampaignPoints)
	{
		std::string sCampaignName = myCampaign.first;
		double nCampaignPercentage = GetSporkDouble(sCampaignName + "campaignpercentage", 0);
		double nCampaignPoints = mCampaignPoints[sCampaignName];
		nCampaignPoints += 1;
		nTotalPoints += nCampaignPoints;
		for (auto Members : mPoints)
		{
			for (auto subCampaign1 : mapSubCampaigns)
			{
				std::string sKey = Members.second.sAddress + subCampaign1.second;
				if (mCPKCampaignPoints[sKey].nPoints > 0)
				{
					double nP = (mCPKCampaignPoints[sKey].nPoints / nCampaignPoints) * nCampaignPercentage;
					mCPKCampaignPoints[sKey].nProminence = nP;
					std::string sRow = subCampaign1.second + "|" + Members.second.sAddress + "|" + RoundToString(mCPKCampaignPoints[sKey].nPoints, 0) + "|" 
							+ RoundToString(mCPKCampaignPoints[sKey].nProminence, 8) + "|" + Members.second.sNickName + "|" + 
							RoundToString(nCampaignPoints, 0) + "|" + Mid(mCPKCampaignPoints[sKey].sCurrencyAddress, 0, 12)
							+ "|" + RoundToString((double)mCPKCampaignPoints[sKey].nCurrencyAmount/COIN, 8) + "\n";
					if (!sAnalyzeUser.empty() && sAnalyzeUser == Members.second.sNickName)
					{
						sAnalysisData2 += sRow;
					}
					if (mCPKCampaignPoints[sKey].nProminence > 0)
						sDetails += sRow;
				}
			}
		}
	}
	WriteCache("analysis", "data_1", sAnalysisData1, GetAdjustedTime());
	WriteCache("analysis", "data_2", sAnalysisData2, GetAdjustedTime());

	// Grand Total for Smart Contract
	for (auto Members : mCPKCampaignPoints)
	{
		mPoints[Members.second.sAddress].nProminence += Members.second.nProminence;
	}
	
	// Create the Daily Contract
	// Allow room for a QT change between contract creation time and superblock generation time
	double nMaxContractPercentage = .999;
	std::string sAddresses;
	std::string sPayments;
	std::string sProminenceExport = "<PROMINENCE>";
	double nGSCContractType = GetSporkDouble("GSC_CONTRACT_TYPE", 0);
	double GSC_MIN_PAYMENT = 1;
	double nTotalProminence = 0;

	if (nGSCContractType == 0)
		GSC_MIN_PAYMENT = .25;
	for (auto Members : mPoints)
	{
		CAmount nPayment = Members.second.nProminence * nPaymentsLimit * nMaxContractPercentage;
		if (ValidateAddress2(Members.second.sAddress) && nPayment > (GSC_MIN_PAYMENT * COIN))
		{
			sAddresses += Members.second.sAddress + "|";
			if (nGSCContractType == 0)
			{
				sPayments += RoundToString(nPayment / COIN, 2) + "|";
			}
			else if (nGSCContractType == 1)
			{
				sPayments += RoundToString((double)nPayment / COIN, 2) + "|";
			}
			CPK localCPK = GetCPKFromProject("cpk", Members.second.sAddress);
			std::string sRow =  "ALL|" + Members.second.sAddress + "|" + RoundToString(Members.second.nPoints, 0) + "|" 
				+ RoundToString(Members.second.nProminence, 4) + "|" 
				+ localCPK.sNickName + "|" 
				+ RoundToString((double)nPayment / COIN, 2) + "|"
				+ Mid(Members.second.sCurrencyAddress, 0, 12) + "|"
				+ RoundToString((double)Members.second.nCurrencyAmount/COIN, 8)
				+ "\n";
			sGenData += sRow;
			nTotalProminence += Members.second.nProminence;
			sProminenceExport += "<CPK>" + Members.second.sAddress + "|" + RoundToString(Members.second.nPoints, 0) + "|" + RoundToString(Members.second.nProminence, 4) + "|" + localCPK.sNickName + "</CPK>";
		}
	}
	sProminenceExport += "</PROMINENCE>";
	
	std::string QTData;
	std::string sSporks;
	if (fCreatingContract)
	{
		// Add the QT Phase
		double out_BTC = 0;
		double out_BBP = 0;
		double dPrice = GetPBase(out_BTC, out_BBP);
		QTData = "<QTDATA><QTPHASE>" + RoundToString(CalculateAPM(nHeight), 0) + "</QTPHASE><BBPPRICE>" + RoundToString(out_BBP, 12) + "</BBPPRICE><PRICE>" 
			+ RoundToString(dPrice, 12) + "</PRICE><BTCPRICE>" + RoundToString(out_BTC, 2) + "</BTCPRICE></QTDATA>";


		// Sanctuary Spork Voting
		// For each winning Sanctuary Spork proposal
		sSporks = "<SPORKS>";
		std::string sCPK = DefaultRecAddress("Christian-Public-Key");
		std::vector<DACProposal> vSporks = GetWinningSanctuarySporkProposals();
		if (vSporks.size() > 0)
		{
			sSporks += "<MT>SPORK2</MT><MK></MK><MV></MV><MS>" + sCPK + "</MS>";

			for (int i = 0; i < vSporks.size(); i++)
			{
				// Mutate the winning Spork Proposal into superblock transaction data so the spork can be loaded globally in a synchronized way as the GSC contract block passes 
				// spork elements are contained in dacProposal.sName, and URL in .sURL, the startDate is in .nStartEpoch, and the .sExpenseType == "SPORK", and this spork is already winning
				// Spork Format:  proposal.name = SporkElement [Key] | SporkElement [Value]
				std::string sKey = GetStringElement(vSporks[i].sName, "|", 0);
				std::string sValue = GetStringElement(vSporks[i].sName, "|", 1);
				if (!sKey.empty() && !sValue.empty())
				{
					std::string sSpork = "<SPORK><SPORKTYPE>" + vSporks[i].sExpenseType + "</SPORKTYPE><SPORKKEY>" + sKey + "</SPORKKEY><SPORKVAL>" + sValue + "</SPORKVAL><NONCE>" 
						+ RoundToString(i, 0) + "</NONCE></SPORK>";
					sSporks += sSpork;
				}
			}
		}
		sSporks += "</SPORKS>";
		// End of Sanctuary Spork Voting
	}
	
	if (sPayments.length() > 1) 
		sPayments = sPayments.substr(0, sPayments.length() - 1);
	if (sAddresses.length() > 1)
		sAddresses = sAddresses.substr(0, sAddresses.length() - 1);

	double nTotalPayments = nTotalProminence * (double)nPaymentsLimit / COIN;

	sData = "<PAYMENTS>" + sPayments + "</PAYMENTS><ADDRESSES>" + sAddresses + "</ADDRESSES><DATA>" + sGenData + "</DATA><LIMIT>" 
		+ RoundToString(nPaymentsLimit/COIN, 4) + "</LIMIT><TOTALPROMINENCE>" + RoundToString(nTotalProminence, 2) + "</TOTALPROMINENCE><TOTALPAYOUT>" + RoundToString(nTotalPayments, 2) 
		+ "</TOTALPAYOUT><TOTALPOINTS>" + RoundToString(nTotalPoints, 2) + "</TOTALPOINTS><DIARIES>" 
		+ sDiaries + "</DIARIES><DETAILS>" + sDetails + "</DETAILS>" + sSporks + QTData + sProminenceExport;
	LogPrint(BCLog::NET, "XML %s, time %f", sData, GetAdjustedTime());
	return sData;
}

int GetRequiredQuorumLevel(int nHeight)
{
	int nMinimumQuorum = 2;
	int nCount = deterministicMNManager->GetListAtChainTip().GetValidMNsCount();
	int nReq = nCount * .35;
	if (nReq < nMinimumQuorum)
		nReq = nMinimumQuorum;
	return nReq;
}

uint256 GetPAMHash(std::string sAddresses, std::string sAmounts, std::string sQTPhase)
{
	std::string sConcat = sAddresses + sAmounts + sQTPhase;
	if (sConcat.empty()) return uint256S("0x0");
	std::string sHash = RetrieveMd5(sConcat);
	return uint256S("0x" + sHash);
}

std::vector<std::pair<int64_t, uint256>> GetGSCSortedByGov(int nHeight, uint256 inPamHash, bool fIncludeNonMatching)
{
	int nStartTime = 0; 
	LOCK2(cs_main, governance.cs);
	std::vector<const CGovernanceObject*> objs = governance.GetAllNewerThan(nStartTime);
	std::string sPAM;
	std::string sPAD;
	std::vector<std::pair<int64_t, uint256> > vPropByGov;
	vPropByGov.reserve(objs.size() + 1);
	int iOffset = 0;
	for (const auto& pGovObj : objs) 
	{
		CGovernanceObject* myGov = governance.FindGovernanceObject(pGovObj->GetHash());
		if (myGov->GetObjectType() != GOVERNANCE_OBJECT_TRIGGER) continue;
		UniValue obj = myGov->GetJSONObject();
		int nLocalHeight = obj["event_block_height"].get_int();
		if (nLocalHeight == nHeight)
		{
			iOffset++;
			// 8-6-2020 - Resilience
			std::string sPAD = obj["payment_addresses"].getValStr();
			std::string sPAM = obj["payment_amounts"].getValStr();
			std::string sQTPhase = obj["qtphase"].getValStr();
			
			int iVotes = myGov->GetAbsoluteYesCount(VOTE_SIGNAL_FUNDING);
			uint256 uPamHash = GetPAMHash(sPAD, sPAM, sQTPhase);
			if (fIncludeNonMatching && inPamHash != uPamHash)
			{
				// This is a Gov Obj that matches the height, but does not match the contract, we need to vote it down
				vPropByGov.push_back(std::make_pair(myGov->GetCreationTime() + iOffset, myGov->GetHash()));
			}
			if (!fIncludeNonMatching && inPamHash == uPamHash)
			{
				// Note:  the pair is used in case we want to store an object later (the PamHash is not distinct, but the govHash is).
				vPropByGov.push_back(std::make_pair(myGov->GetCreationTime() + iOffset, myGov->GetHash()));
			}
		}
	}
	return vPropByGov;
}

bool IsOverBudget(int nHeight, int64_t nTime, std::string sAmounts)
{
	CAmount nPaymentsLimit = CSuperblock::GetPaymentsLimit(nHeight, true);
	if (sAmounts.empty()) return false;
	std::vector<std::string> vPayments = Split(sAmounts.c_str(), "|");
	double dTotalPaid = 0;
	for (int i = 0; i < vPayments.size(); i++)
	{
		dTotalPaid += cdbl(vPayments[i], 2);
	}
	if ((dTotalPaid * COIN) > nPaymentsLimit)
		return true;
	return false;
}

bool VoteForGSCContract(int nHeight, std::string sMyContract, std::string& sError)
{
	int iPendingVotes = 0;
	uint256 uGovObjHash;
	std::string sPaymentAddresses;
	std::string sAmounts;
	std::string sQTData;
	uint256 uPamHash = GetPAMHashByContract(sMyContract);
	
	GetGSCGovObjByHeight(nHeight, uPamHash, iPendingVotes, uGovObjHash, sPaymentAddresses, sAmounts, sQTData);
	
	bool fOverBudget = IsOverBudget(nHeight, GetAdjustedTime(), sAmounts);

	// Verify Payment data matches our payment data, otherwise dont vote for it
	if (sPaymentAddresses.empty() || sAmounts.empty())
	{
		sError = "Unable to vote for GSC Contract::Foreign addresses or amounts empty.";
		return false;
	}
	// Sort by GSC gobject hash (creation time does not work as multiple nodes may be called during the same second to create a GSC)
	std::vector<std::pair<int64_t, uint256>> vPropByGov = GetGSCSortedByGov(nHeight, uPamHash, false);
	// Sort the vector by Gov hash to eliminate ties
	std::sort(vPropByGov.begin(), vPropByGov.end());
	std::string sAction;
	int iVotes = 0;
	// Step 1:  Vote for contracts that agree with the local chain
	for (int i = 0; i < vPropByGov.size(); i++)
	{
		CGovernanceObject* myGov = governance.FindGovernanceObject(vPropByGov[i].second);
		sAction = (i==0) ? "yes" : "no";
		if (fOverBudget) 
			sAction = "no";
		iVotes = myGov->GetAbsoluteYesCount(VOTE_SIGNAL_FUNDING);
		LogPrintf("\nSmartContract-Server::VoteForGSCContractOrderedByHash::Voting %s for govHash %s, with pre-existing-votes %f (created %f) Overbudget %f ",
			sAction, myGov->GetHash().GetHex(), iVotes, myGov->GetCreationTime(), (double)fOverBudget);
		VoteForGobject(myGov->GetHash(), "funding", sAction, sError);
		// Additionally, clear the delete flag, just in case another node saw this contract as a negative earlier in the cycle
		VoteForGobject(myGov->GetHash(), "delete", "no", sError);
		break;
	}
	// Phase 2: Vote against contracts at this height that do not match our hash
	int iVotedNo = 0;
	if (uPamHash != uint256S("0x0"))
	{
		vPropByGov = GetGSCSortedByGov(nHeight, uPamHash, true);
		for (int i = 0; i < vPropByGov.size(); i++)
		{
			CGovernanceObject* myGovForRemoval = governance.FindGovernanceObject(vPropByGov[i].second);
			sAction = "no";
			int iVotes = myGovForRemoval->GetAbsoluteYesCount(VOTE_SIGNAL_FUNDING);
			LogPrintf("\nSmartContract-Server::VoteDownBadGCCContracts::Voting %s for govHash %s, with pre-existing-votes %f (created %f)",
				sAction, myGovForRemoval->GetHash().GetHex(), iVotes, myGovForRemoval->GetCreationTime());
			VoteForGobject(myGovForRemoval->GetHash(), "funding", sAction, sError);
			iVotedNo++;
			if (iVotedNo > 2)
				break;
		}
	}

	//Phase 3:  Vote to delete very old contracts
	int iNextSuperblock = 0;
	int iLastSuperblock = GetLastGSCSuperblockHeight(chainActive.Tip()->nHeight, iNextSuperblock);
	vPropByGov = GetGSCSortedByGov(iLastSuperblock, uPamHash, true);
	for (int i = 0; i < vPropByGov.size(); i++)
	{
		CGovernanceObject* myGovYesterday = governance.FindGovernanceObject(vPropByGov[i].second);
		int iVotes = myGovYesterday->GetAbsoluteYesCount(VOTE_SIGNAL_FUNDING);
		LogPrintf("\nSmartContract-Server::DeleteYesterdaysEmptyGSCContracts::Voting %s for govHash %s, with pre-existing-votes %f (created %f)",
				sAction, myGovYesterday->GetHash().GetHex(), iVotes, myGovYesterday->GetCreationTime());
		int64_t nAge = GetAdjustedTime() - myGovYesterday->GetCreationTime();
		if (iVotes == 0 && nAge > (60 * 60 * 24 * 7))
		{
			VoteForGobject(myGovYesterday->GetHash(), "delete", "yes", sError);
		}
	}

	return sError.empty() ? true : false;
}

bool SubmitGSCTrigger(std::string sHex, std::string& gobjecthash, std::string& sError)
{
	if(!masternodeSync.IsBlockchainSynced()) 
	{
		sError = "Must wait for client to sync with Sanctuary network. Try again in a minute or so.";
		return false;
	}

	if (!fMasternodeMode)
	{
		sError = "You must be a sanctuary to submit a GSC trigger.";
		return false;
	}

	uint256 txidFee;
	uint256 hashParent = uint256();
	int nRevision = 1;
	int nTime = GetAdjustedTime();
	std::string strData = sHex;
	int64_t nLastGSCSubmitted = 0;
	CGovernanceObject govobj(hashParent, nRevision, nTime, txidFee, strData);


    DBG( std::cout << "gobject: submit "
         << " GetDataAsPlainString = " << govobj.GetDataAsPlainString()
         << ", hash = " << govobj.GetHash().GetHex()
         << ", txidFee = " << txidFee.GetHex()
         << std::endl; );

	auto mnList = deterministicMNManager->GetListAtChainTip();
    bool fMnFound = mnList.HasValidMNByCollateral(activeMasternodeInfo.outpoint);
	if (!fMnFound)
	{
		sError = "Unable to find deterministic sanctuary in latest sanctuary list.";
		return false;
	}

	if (govobj.GetObjectType() == GOVERNANCE_OBJECT_TRIGGER) 
	{
		govobj.SetMasternodeOutpoint(activeMasternodeInfo.outpoint);
        govobj.Sign(*activeMasternodeInfo.blsKeyOperator);
    }
    else 
	{
        sError = "Object submission rejected because Sanctuary is not running in deterministic mode\n";
		return false;
    }
    
	std::string strHash = govobj.GetHash().ToString();
	std::string strError;
	bool fMissingMasternode;
	bool fMissingConfirmations;
    {
        LOCK(cs_main);
        if (!govobj.IsValidLocally(strError, true)) 
		{
            sError = "gobject(submit) -- Object submission rejected because object is not valid - hash = " + strHash + ", strError = " + strError;
		    return false;
	    }
    }

	int64_t nAge = GetAdjustedTime() - nLastGSCSubmitted;
	if (nAge < (60 * 15))
	{
		sError = "Local Creation rate limit exceeded (0208)";
		return false;
	}

	if (fMissingConfirmations) 
	{
        governance.AddPostponedObject(govobj);
        govobj.Relay(*g_connman);
    } 
	else 
	{
        governance.AddGovernanceObject(govobj, *g_connman);
    }

	gobjecthash = govobj.GetHash().ToString();
	nLastGSCSubmitted = GetAdjustedTime();

	return true;
}

int GetLastGSCSuperblockHeight(int nCurrentHeight, int& nNextSuperblock)
{
    int nLastSuperblock = 0;
    int nSuperblockStartBlock = Params().GetConsensus().nDCCSuperblockStartBlock;
	int nHeight = nCurrentHeight;
	for (; nHeight > nSuperblockStartBlock; nHeight--)
	{
		if (CSuperblock::IsSmartContract(nHeight))
		{
			nLastSuperblock = nHeight;
			break;
		}
	}
	nHeight = nLastSuperblock + 1;

	for (; nHeight > nLastSuperblock; nHeight++)
	{
		if (CSuperblock::IsSmartContract(nHeight))
		{
			nNextSuperblock = nHeight;
			break;
		}
	}

	return nLastSuperblock;
}

uint256 GetPAMHashByContract(std::string sContract)
{
	std::string sAddresses = ExtractXML(sContract, "<ADDRESSES>","</ADDRESSES>");
	std::string sAmounts = ExtractXML(sContract, "<PAYMENTS>","</PAYMENTS>");
	std::string sQTPhase = ExtractXML(sContract, "<QTPHASE>", "</QTPHASE>");
	// 7-25-2020; R ANDREWS; ADD APM (Automatic Price Mooning) to PAM HASH

	uint256 u = GetPAMHash(sAddresses, sAmounts, sQTPhase);
	/* LogPrintf("GetPAMByContract addr %s, amounts %s, uint %s",sAddresses, sAmounts, u.GetHex()); */
	return u;
}

bool DoesContractExist(int nHeight, uint256 uGovID)
{
	std::string out_pa;
	std::string out_paa;
	std::string out_qt;
	uint256 out_govobjhash = uint256S("0x0");
	int out_votes = 0;
	GetGSCGovObjByHeight(nHeight, uGovID, out_votes, out_govobjhash, out_pa, out_paa, out_qt);
	return uGovID == out_govobjhash;
}

void GetGSCGovObjByHeight(int nHeight, uint256 uOptFilter, int& out_nVotes, uint256& out_uGovObjHash, std::string& out_PaymentAddresses, std::string& out_PaymentAmounts, std::string& out_qtdata)
{
	int nStartTime = 0; 
	LOCK2(cs_main, governance.cs);
	std::vector<const CGovernanceObject*> objs = governance.GetAllNewerThan(nStartTime);
	std::string sPAM;
	std::string sPAD;
	int iHighVotes = -1;
	for (const auto& pGovObj : objs) 
	{
		CGovernanceObject* myGov = governance.FindGovernanceObject(pGovObj->GetHash());
		if (myGov->GetObjectType() != GOVERNANCE_OBJECT_TRIGGER) continue;
	    UniValue obj = myGov->GetJSONObject();
		int nLocalHeight = obj["event_block_height"].get_int();
		if (nLocalHeight == nHeight)
		{
			std::string sPAD = obj["payment_addresses"].getValStr();
			std::string sPAM = obj["payment_amounts"].getValStr();
			std::string sQT = obj["qtphase"].getValStr();
			int iVotes = myGov->GetAbsoluteYesCount(VOTE_SIGNAL_FUNDING);
			uint256 uHash = GetPAMHash(sPAD, sPAM, sQT);
			/* LogPrintf("\n Found gscgovobj2 %s with votes %f with pad %s and pam %s , pam hash %s ", myGov->GetHash().GetHex(), (double)iVotes, sPAD, sPAM, uHash.GetHex()); */
			if (uOptFilter != uint256S("0x0") && uHash != uOptFilter) continue;
			// This governance-object matches the trigger height and the optional filter
			if (iVotes > iHighVotes) 
			{
				iHighVotes = iVotes;
				out_PaymentAddresses = sPAD;
				out_PaymentAmounts = sPAM;
				out_nVotes = iHighVotes;
				out_uGovObjHash = myGov->GetHash();
				out_qtdata = sQT;
			}
		}
	}
}

void GetGovObjDataByPamHash(int nHeight, uint256 hPamHash, std::string& out_Data)
{
	int nStartTime = 0; 
	LOCK2(cs_main, governance.cs);
	std::vector<const CGovernanceObject*> objs = governance.GetAllNewerThan(nStartTime);
	std::string sPAM;
	std::string sPAD;
	int iHighVotes = -1;
	std::string sData;
	for (const auto& pGovObj : objs) 
	{
		CGovernanceObject* myGov = governance.FindGovernanceObject(pGovObj->GetHash());
		if (myGov->GetObjectType() != GOVERNANCE_OBJECT_TRIGGER) continue;
	    UniValue obj = myGov->GetJSONObject();
		int nLocalHeight = obj["event_block_height"].get_int();
		if (nLocalHeight == nHeight)
		{
			std::string sPAD = obj["payment_addresses"].getValStr();
			std::string sPAM = obj["payment_amounts"].getValStr();
			std::string sQT = obj["qtphase"].getValStr();
			int iVotes = myGov->GetAbsoluteYesCount(VOTE_SIGNAL_FUNDING);
			uint256 uHash = GetPAMHash(sPAD, sPAM, sQT);
			if (hPamHash == uHash) 
			{	
				std::string sRow = "gov=" + myGov->GetHash().GetHex() + ",pam=" + hPamHash.GetHex() + ",votes=" + RoundToString(iVotes, 0) + ",qt=" + sQT + ";     ";
				sData += sRow;
			}
		}
	}
	out_Data = sData;
}

bool GetContractPaymentData(std::string sContract, int nBlockHeight, int nTime, std::string& sPaymentAddresses, std::string& sAmounts)
{
	CAmount nPaymentsLimit = CSuperblock::GetPaymentsLimit(nBlockHeight, true);
	sPaymentAddresses = ExtractXML(sContract, "<ADDRESSES>", "</ADDRESSES>");
	sAmounts = ExtractXML(sContract, "<PAYMENTS>", "</PAYMENTS>");
	std::vector<std::string> vPayments = Split(sAmounts.c_str(), "|");
	double dTotalPaid = 0;
	for (int i = 0; i < vPayments.size(); i++)
	{
		dTotalPaid += cdbl(vPayments[i], 2);
	}
	if (dTotalPaid < 1 || (dTotalPaid * COIN) > nPaymentsLimit)
	{
		LogPrintf("\n**GetContractPaymentData::Error::Superblock Payment Budget is out of bounds:  Limit %f,  Actual %f  ** \n", (double)nPaymentsLimit/COIN, (double)dTotalPaid);
		return false;
	}
	return true;
}

uint256 GetGSCHash(std::string sContract)
{
	std::string sHash = RetrieveMd5(sContract);
	return uint256S("0x" + sHash);
}

std::string SerializeSanctuaryQuorumTrigger(int iContractAssessmentHeight, int nEventBlockHeight, int64_t nTime, std::string sContract)
{
	std::string sEventBlockHeight = RoundToString(nEventBlockHeight, 0);
	std::string sPaymentAddresses;
	std::string sPaymentAmounts;
	// For Evo compatibility and security purposes, we move the QT Phase into the GSC contract so all sancs must agree on the phase
	std::string sQTData = ExtractXML(sContract, "<QTDATA>", "</QTDATA>");
	std::string sHashes = ExtractXML(sContract, "<PROPOSALS>", "</PROPOSALS>");
	bool bStatus = GetContractPaymentData(sContract, iContractAssessmentHeight, nTime, sPaymentAddresses, sPaymentAmounts);
	if (!bStatus) 
	{
		LogPrintf("\nERROR::SerializeSanctuaryQuorumTrigger::Unable to Serialize %f", 1);
		return std::string();
	}
	std::string sVoteData = ExtractXML(sContract, "<VOTEDATA>", "</VOTEDATA>");
	std::string sSporkData = ExtractXML(sContract, "<SPORKS>", "</SPORKS>");
	
	std::string sProposalHashes = GetPAMHashByContract(sContract).GetHex();
	if (!sHashes.empty())
		sProposalHashes = sHashes;
	std::string sType = "2"; // GSC Trigger is always 2
	std::string sQ = "\"";
	std::string sJson = "[[" + sQ + "trigger" + sQ + ",{";
	sJson += GJE("event_block_height", sEventBlockHeight, true, false); // Must be an int
	sJson += GJE("start_epoch", RoundToString(GetAdjustedTime(), 0), true, false);
	sJson += GJE("payment_addresses", sPaymentAddresses,  true, true);
	sJson += GJE("payment_amounts",   sPaymentAmounts,    true, true);
	sJson += GJE("proposal_hashes",   sProposalHashes,    true, true);
	if (!sVoteData.empty())
		sJson += GJE("vote_data", sVoteData, true, true);

	if (!sSporkData.empty())
		sJson += GJE("spork_data", sSporkData, true, true);
	
	if (!sQTData.empty())
	{
		sJson += GJE("price", ExtractXML(sQTData, "<PRICE>", "</PRICE>"), true, true);
		sJson += GJE("qtphase", ExtractXML(sQTData, "<QTPHASE>", "</QTPHASE>"), true, true);
		sJson += GJE("btcprice", ExtractXML(sQTData,"<BTCPRICE>", "</BTCPRICE>"), true, true);
		sJson += GJE("bbpprice", ExtractXML(sQTData,"<BBPPRICE>", "</BBPPRICE>"), true, true);
	}
	sJson += GJE("type", sType, false, false); 
	sJson += "}]]";
	LogPrintf("\nSerializeSanctuaryQuorumTrigger:Creating New Object %s ", sJson);
	std::vector<unsigned char> vchJson = std::vector<unsigned char>(sJson.begin(), sJson.end());
	std::string sHex = HexStr(vchJson.begin(), vchJson.end());
	return sHex;
}

bool ChainSynced(CBlockIndex* pindex)
{
	int64_t nAge = GetAdjustedTime() - pindex->GetBlockTime();
	return (nAge > (60 * 60)) ? false : true;
}

bool Included(std::string sFilterNickName, std::string sCPK)
{
	CPK oPrimary = GetCPKFromProject("cpk", sCPK);
	std::string sNickName = Caption(oPrimary.sNickName, 10);
	bool fIncluded = false;
	if (((sNickName == sFilterNickName || oPrimary.sNickName == sFilterNickName) && !sFilterNickName.empty()) || (sFilterNickName.empty()))
		fIncluded = true;
	return fIncluded;
}

UniValue GetProminenceLevels(int nHeight, std::string sFilterNickName)
{
	UniValue results(UniValue::VOBJ);
	if (nHeight == 0) 
		return NullUniValue;
      
	CAmount nPaymentsLimit = CSuperblock::GetPaymentsLimit(nHeight, false);
	nPaymentsLimit -= MAX_BLOCK_SUBSIDY * COIN;

	std::string sContract = GetGSCContract(nHeight, false);
	std::string sData = ExtractXML(sContract, "<DATA>", "</DATA>");
	std::string sDetails = ExtractXML(sContract, "<DETAILS>", "</DETAILS>");
	std::string sDiaries = ExtractXML(sContract, "<DIARIES>", "</DIARIES>");
	std::vector<std::string> vData = Split(sData.c_str(), "\n");
	std::vector<std::string> vDetails = Split(sDetails.c_str(), "\n");
	std::vector<std::string> vDiaries = Split(sDiaries.c_str(), "\n");
	results.push_back(Pair("Prominence v1.2", "Details"));
	// DETAIL ROW FORMAT: sCampaignName + "|" + Members.Address + "|" + nPoints + "|" + nProminence + "|" + NickName + "|\n";
	std::string sMyCPK = DefaultRecAddress("Christian-Public-Key");

	for (int i = 0; i < vDetails.size(); i++)
	{
		std::vector<std::string> vRow = Split(vDetails[i].c_str(), "|");
		if (vRow.size() >= 4)
		{
			std::string sCampaignName = vRow[0];
			std::string sCPK = vRow[1];
			double nPoints = cdbl(vRow[2], 2);
			double nProminence = cdbl(vRow[3], 8) * 100;
			CPK oPrimary = GetCPKFromProject("cpk", sCPK);
			std::string sNickName = Caption(oPrimary.sNickName, 10);
			if (sNickName.empty())
				sNickName = "N/A";
			std::string sNarr = sCampaignName + ": " + sCPK + " [" + sNickName + "], Pts: " + RoundToString(nPoints, 2);
			if (Included(sFilterNickName, sCPK))
				results.push_back(Pair(sNarr, RoundToString(nProminence, 2) + "%"));
		}
	}

	// ToDo : Move healing diaries to a widget

	/*
	if (vDiaries.size() > 0)
		results.push_back(Pair("Healing", "Diary Entries"));
	for (int i = 0; i < vDiaries.size(); i++)
	{
		std::vector<std::string> vRow = Split(vDiaries[i].c_str(), "|");
		if (vRow.size() >= 2)
		{
			std::string sCPK = vRow[0];
			if (Included(sFilterNickName, sCPK))
				results.push_back(Pair(Caption(vRow[1], 10), vRow[2]));
		}
	}
	*/
	
	double dTotalPaid = 0;

	double nMaxContractPercentage = .99;
	results.push_back(Pair("Prominence", "Totals"));
	for (int i = 0; i < vData.size(); i++)
	{
		std::vector<std::string> vRow = Split(vData[i].c_str(), "|");
		if (vRow.size() >= 6)
		{
			std::string sCampaign = vRow[0];
			std::string sCPK = vRow[1];
			double nPoints = cdbl(vRow[2], 2);
			double nProminence = cdbl(vRow[3], 4) * 100;
			double nPayment = cdbl(vRow[5], 4);
			std::string sNickName = vRow[4];
			if (sNickName.empty())
				sNickName = "N/A";
			CAmount nOwed = nPaymentsLimit * (nProminence / 100) * nMaxContractPercentage;
			std::string sNarr = sCampaign + ": " + sCPK + " [" + Caption(sNickName, 10) + "]" + ", Pts: " + RoundToString(nPoints, 2) 
				+ ", CurAddr: " + vRow[6] + ", CurAmt: " + vRow[7] + ", Reward: " + RoundToString(nPayment, 3);
			if (Included(sFilterNickName, sCPK))
				results.push_back(Pair(sNarr, RoundToString(nProminence, 3) + "%"));
		}
	}

	return results;
}

std::string ExecuteGenericSmartContractQuorumProcess()
{
	if (!chainActive.Tip()) 
		return "INVALID_CHAIN";

	if (!ChainSynced(chainActive.Tip()))
		return "CHAIN_NOT_SYNCED";
	
	int nFreq = (int)cdbl(gArgs.GetArg("-dailygscfrequency", RoundToString(BLOCKS_PER_DAY, 0)), 0);
	if (nFreq < 50)
		nFreq = 50; 

	if (!fMasternodeMode)   
		return "NOT_A_SANCTUARY";

	double nMinGSCProtocolVersion = GetSporkDouble("MIN_GSC_PROTO_VERSION", 0);
	if (PROTOCOL_VERSION < nMinGSCProtocolVersion)
		return "GSC_PROTOCOL_REQUIRES_UPGRADE";

	bool fWatchmanQuorum = (chainActive.Tip()->nHeight % 10 == 0) && fMasternodeMode;
	if (fWatchmanQuorum)
	{
		std::string sContr;
		std::string sWatchman = WatchmanOnTheWall(false, sContr);
	}

	// Goal 1: Be synchronized as a team after the warming period, but be cascading during the warming period
	int iNextSuperblock = 0;
	int iLastSuperblock = GetLastGSCSuperblockHeight(chainActive.Tip()->nHeight, iNextSuperblock);
	int nBlocksSinceLastEpoch = chainActive.Tip()->nHeight - iLastSuperblock;
	const Consensus::Params& consensusParams = Params().GetConsensus();
	int WARMING_DURATION = consensusParams.nSuperblockCycle * .10;
	int nCascadeHeight = GetRandInt(chainActive.Tip()->nHeight);
	bool fWarmingPeriod = nBlocksSinceLastEpoch < WARMING_DURATION;
	int nQuorumAssessmentHeight = fWarmingPeriod ? nCascadeHeight : chainActive.Tip()->nHeight;
	int nCreateWindow = chainActive.Tip()->nHeight * .25;
	bool fPrivilegeToCreate = nCascadeHeight < nCreateWindow;

 	if (!fProd)
		fPrivilegeToCreate = true;

	bool fQuorum = (nQuorumAssessmentHeight % 5 == 0);
	if (!fQuorum)
		return "NTFQ_";
	
	//  Check for Pending Contract
	int iVotes = 0;
	std::string sAddresses;
	std::string sAmounts;
	std::string sError;
	std::string out_qtdata;
	std::string sContract = GetGSCContract(0, true);
	uint256 out_uGovObjHash = uint256S("0x0");
	uint256 uPamHash = GetPAMHashByContract(sContract);
	
	GetGSCGovObjByHeight(iNextSuperblock, uPamHash, iVotes, out_uGovObjHash, sAddresses, sAmounts, out_qtdata);
	
	int iRequiredVotes = GetRequiredQuorumLevel(iNextSuperblock);
	bool bPending = iVotes > iRequiredVotes;

	if (bPending) 
	{
		return "PENDING_SUPERBLOCK";
	}
	// If we are > halfway into daily GSC deadline, and have not received the gobject, emit a distress signal
	int nBlocksLeft = iNextSuperblock - chainActive.Tip()->nHeight;
	if (nBlocksLeft < BLOCKS_PER_DAY / 2)
	{
		if (iVotes < iRequiredVotes || out_uGovObjHash == uint256S("0x0") || sAddresses.empty())
		{
			LogPrintf("\n ExecuteGenericSmartContractQuorum::DistressAlert!  Not enough votes %f for GSC %s!", 
				(double)iVotes, out_uGovObjHash.GetHex());
		}
	}

	if (fPrivilegeToCreate)
	{
		// In this case, we have the privilege to create, and the contract does not exist (and, no clone has been created either - with 0 votes)
		if (out_uGovObjHash == uint256S("0x0"))
		{
			std::string sQuorumTrigger = SerializeSanctuaryQuorumTrigger(iLastSuperblock, iNextSuperblock, GetAdjustedTime(), sContract);
			std::string sGobjectHash;
			SubmitGSCTrigger(sQuorumTrigger, sGobjectHash, sError);
			LogPrintf("**ExecuteGenericSmartContractQuorumProcess::CreatingGSCContract Hex %s , Gobject %s, results %s **\n", sQuorumTrigger.c_str(), sGobjectHash.c_str(), sError.c_str());
			return "CREATING_CONTRACT";
		}
	}
	if (iVotes <= iRequiredVotes)
	{
		bool bResult = VoteForGSCContract(iNextSuperblock, sContract, sError);
		if (!bResult)
		{
			LogPrintf("\n**ExecuteGenericSmartContractQuorum::Unable to vote for GSC contract: Reason [%s] ", sError.c_str());
			return "UNABLE_TO_VOTE_FOR_GSC_CONTRACT";
		}
		else
		{
			LogPrintf("\n**ExecuteGenericSmartContractQuorum::Voted Successfully %f.", 1);
			return "VOTED_FOR_GSC_CONTRACT";
		}
	}
	else if (iVotes > iRequiredVotes)
	{
		LogPrintf(" ExecuteGenericSmartContractQuorum::GSC Contract %s has won.  Waiting for superblock. ", out_uGovObjHash.GetHex());
		return "PENDING_SUPERBLOCK";
	}

	return "NOT_A_CHOSEN_SANCTUARY";
}

