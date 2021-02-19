// Copyright (c) 2014-2019 The Dash Core Developers, The DAC Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "smartcontract-server.h"
#include "util.h"
#include "utilmoneystr.h"
#include "rpcpog.h"
#include "rpcpodc.h"
#include "smartcontract-client.h"
#include "init.h"
#include "activemasternode.h"
#include "governance-classes.h"
#include "governance.h"
#include "masternode-sync.h"
#include "masternode-payments.h"
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
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <cmath>
#ifdef ENABLE_WALLET
extern CWallet* pwalletMain;
#endif // ENABLE_WALLET

void GetTransactionPoints(CBlockIndex* pindex, CTransactionRef tx, double& nCoinAge, CAmount& nDonation)
{
	nCoinAge = GetVINCoinAge(pindex->GetBlockTime(), tx, false);
	bool fSigned = CheckAntiBotNetSignature(tx, "gsc", "");
	nDonation = GetTitheAmount(tx);
	if (!fSigned) 
	{
		nCoinAge = 0;
		nDonation = 0;
		LogPrintf("antibotnetsignature failed on tx %s with purported coin-age of %f \n", tx->GetHash().GetHex(), nCoinAge);
		return;
	}
	return;
}

std::string GetTxCPK(CTransactionRef tx, std::string& sCampaignName)
{
	std::string sMsg = GetTransactionMessage(tx);
	std::string sCPK = ExtractXML(sMsg, "<abncpk>", "</abncpk>");
	sCampaignName = ExtractXML(sMsg, "<gsccampaign>", "</gsccampaign>");
	return sCPK;
}

static double N_MAX = 9999999999;
double GetRequiredCoinAgeForPODC(double nRAC, double nTeamID)
{
	// Todo for Prod Release:  Make sporks here
	// We currently require RAC ^ 1.3 in coin-age
	// Any CPIDs with RAC <= 250 are unbanked (they require 0 coin age).

	// This poll: https://forum.b i b l e pay.org/index.php?topic=476.0
	// sets our model to require ^1.6 for GRC and ^1.3 for Bible Pay
	double nExponent = 0;
	double nConfiguration = GetSporkDouble("PODCTeamConfiguration", 0);
	// Config 0 = Exp=1.3 for Bible-Pay, All non bible-pay teams are 1.6
	// Config 1 = Exp=1.3 for Bible-Pay, GRC = 1.6, All other teams not welcome
	// Config 2 = Exp=1.3 for Bible-Pay, All other teams not welcome
	if (nConfiguration == 0)
	{
		if (nTeamID == 35006)
		{
			nExponent = 1.3;
		}
		else 
		{
			nExponent = 1.6;
		}
	}
	else if (nConfiguration == 1)
	{
		if (nTeamID == 35006)
		{
			nExponent = 1.3;
		}
		else if (nTeamID == 30513)
		{
			// GRC
			nExponent = 1.6;
		}
		else
		{
			return N_MAX;
		}
	}
	else if (nConfiguration == 2)
	{
		if (nTeamID == 35006)
		{
			nExponent = 1.3;
		}
		else
		{
			return N_MAX;
		}
	}
	else
	{
		return N_MAX;
	}

	double nUnbankedThreshhold = GetSporkDouble("PODCUNBANKEDTHRESHHOLD", 250);
	double nAgeRequired = pow(nRAC, nExponent);
	if (nRAC <= nUnbankedThreshhold && nTeamID == 35006)
	{
		// Mark the researcher as Unbanked here:
		nAgeRequired = 0;
	}
	return nAgeRequired;
}

//////////////////////////////////////////////////////////////////////////////// Cameroon-One & Kairos  /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

double GetCoinPrice()
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

bool VerifyChild(std::string childID, std::string sCharity)
{
	std::map<std::string, CPK> cp1 = GetChildMap("cpk|" + sCharity);
	std::string sMyCPK = DefaultRecAddress("Christian-Public-Key");
	for (std::pair<std::string, CPK> a : cp1)
	{
		std::string sChildID = a.second.sOptData;
		if (childID == sChildID)
			return true;
	}
	return false;
}

double GetProminenceCap(std::string sCampaignName, double nPoints, double nProminence)
{
	boost::to_upper(sCampaignName);

	if (sCampaignName != "CAMEROON-ONE" && sCampaignName != "KAIROS")
		return nProminence;

	double nMonthlyRate = GetSporkDouble(sCampaignName + "monthlyrate", 40);

	double nDailyCharges = nMonthlyRate / 30;
	if (nDailyCharges <= .01)
		return 0;
	double nUSDSpent = nPoints / 1000;  // Amount user spent in one day on children
	double nChildrenSponsored = nUSDSpent / nDailyCharges;
	// Cap @ Coin Exchange Rate * Child Count
	double nPrice = GetCoinPrice();
	if (nPrice <= 0)
	{
		nPrice = .0004; // Guess
	}
	int nNextSuperblock = 0;
	int nLastSuperblock = GetLastGSCSuperblockHeight(chainActive.Tip()->nHeight, nNextSuperblock);
	CAmount nBudget = CSuperblock::GetPaymentsLimit(nNextSuperblock, GetAdjustedTime(), false);
	if (nBudget < 1)
		return 0;
	double nPaymentsLimit = (nBudget / COIN);
	double nUserReward = nPaymentsLimit * nProminence;
	double nRewardUSD = nUserReward * nPrice;
	if (nRewardUSD > nUSDSpent)
	{
		// Cap is in effect, so reverse engineer the payment to the actual market value
		double nProjectedAmount = nUSDSpent / nPrice;
		double nProjectedProminence = nProjectedAmount / nPaymentsLimit;
		if (fDebugSpam)
			LogPrintf(" GetProminenceCap Exceeded - new prominence %f ", nProjectedProminence);
		nProminence = nProjectedProminence;
	}
	if (fDebugSpam)
		LogPrintf("\n GetProminenceCap Points %f, Prominence %f, USD Price %f, UserReward %f  ", nPoints, nProminence, nPrice, nRewardUSD);
	return nProminence;
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
    
	CAmount nPaymentsLimit = CSuperblock::GetPaymentsLimit(nNextSuperblock, 0, false);
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
		CBitcoinAddress cbaAddress(p.sAddress);
		if (cbaAddress.IsValid() && p.nAmount > .01)
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
		LogPrintf("\nSmartContractServer::DAC  Found charity %s with allocated amount %f to address %s and orphan count %f and TMC of %f ", sCharity, npct, sAddress, iOrphanCount, nTotalMonthlyCommitments);
		// If address is valid, and amount is > 0:
		if (!sAddress.empty() && npct > 0)
		{
			CBitcoinAddress cbaAddress(sAddress);
			if (cbaAddress.IsValid())
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

static int LEGACY_WHALE_STAKE_AMOUNT = 10000000;
static int NEW_WHALE_STAKE_AMOUNT = 2000000;

double GetDACDonationTotals(int nHeight, int nTime)
{
	const Consensus::Params& consensusParams = Params().GetConsensus();

	// The plan is to use this function until all the legacy whale stakes (RPC: dws, dwsquote) are paid back to the stakers (IE until around September 2021).
	// Then we can remove this and go back to standard exact block totals as is done in bitcoin and dash.
	// This is possible because as of Feb 2021, we moved away from the Burn->CoinbaseReward model, over to the Stake->DailyGSCReward model.

	double n1 = 0;
	double n2 = 0;
	if (nHeight < consensusParams.TRIBULATION_HEIGHT || nTime < (GetAdjustedTime() - (86400*2)))
	{
		return LEGACY_WHALE_STAKE_AMOUNT;
	}
	else
	{
		// Go by strict rules until function is removed
		n1 += NEW_WHALE_STAKE_AMOUNT;
	}

	std::string sContract = AssessBlocks(nHeight, true);
	
	n1 += cdbl(ExtractXML(sContract, "<DWSTOTAL>", "</DWSTOTAL>"), 2);
	n2 += cdbl(ExtractXML(sContract, "<DSTOTAL>", "</DSTOTAL>"), 2);

	if (n1 > MAX_DAILY_WHALE_COMMITMENTS)
	{
		LogPrintf("MAX_DAILY_WHALE_COMMITMENTS EXCEEDED; LIMITING %f", n1);
		n1 = MAX_DAILY_WHALE_COMMITMENTS;
	}

	if (n2 > MAX_DAILY_DASH_STAKE_COMMITMENTS)
	{
		LogPrintf("MAX_DAILY_DASH_STAKE_COMMITMENTS EXCEEDED; LIMITING %f", n2);
		n2 = MAX_DAILY_DASH_STAKE_COMMITMENTS;
	}

	double n3 = cdbl(ExtractXML(sContract, "<DACTOTAL>", "</DACTOTAL>"), 2);

	if (n3 > MAX_DAILY_DAC_DONATIONS)
	{
		LogPrintf("MAX_DAILY_DAC_DONATIONS_EXCEEDED; LIMITING %f", n3);
		n3 = MAX_DAILY_DAC_DONATIONS;
	}
	LogPrintf(" DWS %f, Dash %f, Donations %f ", n1, n2, n3);
	return n1 + n2 + n3;
}


double CalculatePoints(std::string sCampaign, std::string sDiary, double nCoinAge, CAmount nDonation, std::string sCPK)
{
	boost::to_upper(sCampaign);
	double nPoints = 0;

	if (sCampaign == "WCG")
	{
		return nCoinAge;
	}
	else if (sCampaign == "POG")
	{
		// This project is being retired
		double nComponent1 = nCoinAge;
		double nTithed = (double)nDonation / COIN;
		if (nTithed < .25) nTithed = 0;
		double nTitheFactor = GetSporkDouble("pogtithefactor", 1);
		double nComponent2 = cbrt(nTithed) * nTitheFactor;
		nPoints = (nComponent1 * nComponent2) / 1000;
		return nPoints;
	}
	else if (sCampaign == "HEALING")
	{
		double nMultiplier = sDiary.empty() ? 0 : 1;
		nPoints = (nCoinAge * nMultiplier) / 1000;
		return nPoints;
	}
	else if (sCampaign == "CAMEROON-ONE" || sCampaign == "KAIROS")
	{
		return 0;
	}
	return 0;
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

    CGovernanceVote vote(dmn->collateralOutpoint, govobj, eVoteSignal, eVoteOutcome, "10");

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

std::string GetCPIDByCPK(std::string sCPK)
{
	std::string sData = ReadCache("CPK-WCG", sCPK);
	std::vector<std::string> vP = Split(sData.c_str(), "|");
	if (vP.size() < 10)
		return std::string();
	std::string cpid = vP[8];
	return cpid;
}

std::string GetCPIDElementByData(std::string sData, int iElement)
{
	std::vector<std::string> vP = Split(sData.c_str(), "|");
	if (vP.size() < 10)
		return std::string();
	return vP[iElement];
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
	LogPrintf("\nExtractAPM Height %f=%f ", nHeight, nAPM);
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

	LogPrintf("CalculateAPM::Result==%f::LastHeight %f Price %s, Current Price %s", 
		nResult, nHeight, RoundToString(dLastPrice, 12), RoundToString(out_BBP, 12));
	return nResult;
}

std::string AssessBlocks(int nHeight, bool fCreatingContract)
{

	LogPrintf("\nAssessBlocks Height %f ", nHeight);

	CAmount nPaymentsLimit = CSuperblock::GetPaymentsLimit(nHeight, 0, false);

	nPaymentsLimit -= MAX_BLOCK_SUBSIDY * COIN;

	std::map<std::string, Researcher> Researchers = GetPayableResearchers();

	if (!chainActive.Tip()) 
		return std::string();
	if (nHeight > chainActive.Tip()->nHeight)
		nHeight = chainActive.Tip()->nHeight - 1;

	int nMaxDepth = nHeight;
	int nMinDepth = nMaxDepth - BLOCKS_PER_DAY;
	if (nMinDepth < 1) 
		return std::string();
	CBlockIndex* pindex = FindBlockByHeight(nMinDepth);
	const Consensus::Params& consensusParams = Params().GetConsensus();
	std::map<std::string, CPK> mPoints;
	std::map<std::string, double> mCampaignPoints;
	std::map<std::string, CPK> mCPKCampaignPoints;
	std::map<std::string, double> mCampaigns;
	double dDebugLevel = cdbl(GetArg("-debuglevel", "0"), 0);
	std::string sDiaries;
	std::string sAnalyzeUser = ReadCache("analysis", "user");
	std::string sAnalysisData1;
	while (pindex && pindex->nHeight < nMaxDepth)
	{
		if (pindex->nHeight < chainActive.Tip()->nHeight) 
			pindex = chainActive.Next(pindex);
		CBlock block;
		if (ReadBlockFromDisk(block, pindex, consensusParams)) 
		{
			for (unsigned int n = 0; n < block.vtx.size(); n++)
			{
				if (block.vtx[n]->IsGSCTransmission() && CheckAntiBotNetSignature(block.vtx[n], "gsc", ""))
				{
					std::string sCampaignName;
					std::string sCPK = GetTxCPK(block.vtx[n], sCampaignName);
					CPK localCPK = GetCPKFromProject("cpk", sCPK);
					double nCoinAge = 0;
					CAmount nDonation = 0;
					GetTransactionPoints(pindex, block.vtx[n], nCoinAge, nDonation);
					if (CheckCampaign(sCampaignName) && !sCPK.empty())
					{
						std::string sDiary = ExtractXML(block.vtx[n]->GetTxMessage(), "<diary>", "</diary>");
						double nPoints = CalculatePoints(sCampaignName, sDiary, nCoinAge, nDonation, sCPK);

						if (sCampaignName == "WCG" && nPoints > 0)
						{
							std::string sCPID = GetCPIDByCPK(sCPK);

							Researcher r = Researchers[sCPID];
							if (r.found)
							{
								r.CoinAge += nPoints;
								r.CPK = sCPK;
								Researchers[sCPID] = r;
							}
							else
							{
								LogPrintf("\nAssessBlocks::Unable to find researcher for CPK %s with CPID %s", sCPK, sCPID);
							}
							nPoints = 0;
						}

						
						if (nPoints > 0)
						{
							// CPK 
							CPK c = mPoints[sCPK];
							c.sCampaign = sCampaignName;
							c.sAddress = sCPK;
							c.sNickName = localCPK.sNickName;
							c.nPoints += nPoints;
							mCampaignPoints[sCampaignName] += nPoints;
							mPoints[sCPK] = c;
							
							// CPK-Campaign
							CPK cCPKCampaignPoints = mCPKCampaignPoints[sCPK + sCampaignName];
							cCPKCampaignPoints.sAddress = sCPK;
							cCPKCampaignPoints.sNickName = c.sNickName;
							cCPKCampaignPoints.nPoints += nPoints;
							mCPKCampaignPoints[sCPK + sCampaignName] = cCPKCampaignPoints;
							if (dDebugLevel == 1)
								LogPrintf("\nUser %s , NN %s, Diary %s, height %f, TXID %s, nn %s, Points %f, Campaign %s, coinage %f, donation %f, usertotal %f ",
								c.sAddress, localCPK.sNickName, sDiary, pindex->nHeight, block.vtx[n]->GetHash().GetHex(), localCPK.sNickName, 
								(double)nPoints, c.sCampaign, (double)nCoinAge, 
								(double)nDonation/COIN, (double)c.nPoints);
							if (!sAnalyzeUser.empty() && sAnalyzeUser == c.sNickName)
							{
								std::string sInfo = "User: " + c.sAddress + ", Diary: " + sDiary + ", Height: " + RoundToString(pindex->nHeight, 2)
									+ ", TXID: " + block.vtx[n]->GetHash().GetHex() + ", NickName: " 
									+ localCPK.sNickName + ", Points: " + RoundToString(nPoints, 2) 
									+ ", Campaign: " + c.sCampaign + ", CoinAge: " + RoundToString(nCoinAge, 4) 
									+ ", Donation: " + RoundToString(nDonation/COIN, 4) + ", UserTotal: " + RoundToString(c.nPoints, 2) + "\n";
									sAnalysisData1 += sInfo;
							}
							if (c.sCampaign == "HEALING" && !sDiary.empty())
							{
								sDiaries += "\n" + sCPK + "|" + localCPK.sNickName + "|" + sDiary;
							}
						}
					}
				}
			}
		}
	}


	// UTXO STAKING
	double dEnabled = GetSporkDouble("UTXOStakingEnabled", 0);
	if (dEnabled == 1)
	{
		std::vector<UTXOStake> uStakes = GetUTXOStakes(false);
		std::string sThisCampaign = "UTXO";
		for (int i = 0; i < uStakes.size(); i++)
		{
			UTXOStake d = uStakes[i];
			if (d.found && d.nValue > 0)
			{
				int nStatus = GetUTXOStatus(d.TXID);
				LogPrintf("\nLeaderboard::UTXO1 nStatus %f", nStatus);

				if (nStatus > 0)
				{
					// Entry into the UTXO campaign
					UserRecord u = GetUserRecord(d.CPK);
					{
						LogPrintf("\nLeaderboard::UTXO1 cpk %s", d.CPK);

						// Legacy code that adds a CPK reward, then a CPK-Campgin reward
						CPK c = mPoints[d.CPK];
						c.sCampaign = sThisCampaign;
						c.sAddress = d.CPK;
						c.sNickName = u.NickName;
						double nPoints = d.nValue;
						c.nPoints += nPoints;
						mCampaignPoints[sThisCampaign] += nPoints;
						mPoints[d.CPK] = c;
							
						// CPK-Campaign
						CPK cCPKCampaignPoints = mCPKCampaignPoints[d.CPK + sThisCampaign];
						cCPKCampaignPoints.sAddress = d.CPK;
						cCPKCampaignPoints.sNickName = c.sNickName;
						cCPKCampaignPoints.nPoints += nPoints;
						mCPKCampaignPoints[d.CPK + sThisCampaign] = cCPKCampaignPoints;
					}
				}
			}
		}
	}
	// End of UTXO Staking

	
	
	// PODC 2.0
	// This dedicated area allows us to pay the unbanked each day *or* the researchers with collateral staked.
	// (In contrast to paying the list of collateralized CPIDs).
	std::string sCampaignName = "WCG";
	BOOST_FOREACH(PAIRTYPE(std::string, Researcher) r, Researchers)
	{
		if (r.second.found && r.second.cpid.length() == 32)
		{
			double nCoinAgeRequired = GetRequiredCoinAgeForPODC(r.second.rac, r.second.teamid);
			if (nCoinAgeRequired > r.second.CoinAge && nCoinAgeRequired != N_MAX)
			{
				// Reduce the researchers RAC to the applicable coinAge staked:
				double nPODCConfig = GetSporkDouble("mandatory1485", 0);
				if (nPODCConfig == 1)
				{
					// Maintain PODC consensus compatibility until 1.4.8.5 cutover height for sanctuaries is announced (TBD)
					double nExponent = (r.second.teamid == 35006) ? 1.3 : 1.6;
					r.second.rac = pow(r.second.CoinAge - 1, 1/nExponent);
				}
				else
				{
					r.second.rac = pow(r.second.rac - 1, 1/1.3);
				}

				nCoinAgeRequired = GetRequiredCoinAgeForPODC(r.second.rac, r.second.teamid);
			}

			bool fApplicable = r.second.CoinAge >= nCoinAgeRequired;
			if (!fApplicable)
			{
				if (fDebug && nCoinAgeRequired != N_MAX)
					LogPrintf("\nAssessBlocks::Researcher not applicable because CoinAge req %f is less than %f for CPID %s", nCoinAgeRequired, r.second.CoinAge, r.second.cpid);
			}
			else 
			{
				std::string sCPK = GetCPKByCPID(r.second.cpid);
				if (!sCPK.empty())
				{
					double nPoints = r.second.rac * 1;
					// CPK 
					CPK c = mPoints[sCPK];
					c.sCampaign = sCampaignName;
					c.sAddress = sCPK;
					CPK localCPK = GetCPKFromProject("cpk", sCPK);
					c.sNickName = localCPK.sNickName;
					c.nPoints += nPoints;
					c.cpid = r.second.cpid;
					mCampaignPoints[sCampaignName] += nPoints;
					mPoints[sCPK] = c;
					// CPK-Campaign
					CPK cCPKCampaignPoints = mCPKCampaignPoints[sCPK + sCampaignName];
					cCPKCampaignPoints.sAddress = sCPK;
					cCPKCampaignPoints.sNickName = c.sNickName;
					cCPKCampaignPoints.nPoints += nPoints;
					cCPKCampaignPoints.cpid = r.second.cpid;
					mCPKCampaignPoints[sCPK + sCampaignName] = cCPKCampaignPoints;
					if (dDebugLevel == 1)
					{
						LogPrintf("\nCPK %s , NN %s, Points %f, Campaign %s, coinage %f, usertotal %f ",
								sCPK, localCPK.sNickName, (double)nPoints, c.sCampaign, (double)r.second.CoinAge, (double)c.nPoints);
					}
					if (!sAnalyzeUser.empty() && sAnalyzeUser == c.sNickName)
					{
						std::string sInfo = "User: " + sCPK + ", NickName: " 
										+ localCPK.sNickName + ", Points: " + RoundToString(nPoints, 2) 
										+ ", Campaign: " + c.sCampaign + ", CoinAge: " + RoundToString(r.second.CoinAge, 4) 
										+ ", CPID: " + r.second.cpid + ", RAC: " + RoundToString(r.second.rac, 4) 
										+ ", UserTotal: " + RoundToString(c.nPoints, 2) + "\n";
						sAnalysisData1 += sInfo;
					}
				}
			}
		}
	}
	
	// End of PODC 2.0

	
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
		if (nCampaignPercentage < 0) nCampaignPercentage = 0;
		double nCampaignPoints = mCampaignPoints[sCampaignName];
		if (fDebugSpam)
			LogPrintf("\n SCS-AssessBlocks::Processing Campaign %s (%f), Payment Pctg [%f], TotalPoints %f ", 
			myCampaign.first, myCampaign.second, nCampaignPercentage, nCampaignPoints);
		nCampaignPoints += 1;
		nTotalPoints += nCampaignPoints;
		for (auto Members : mPoints)
		{
			std::string sKey = Members.second.sAddress + sCampaignName;
			
			double nPreProminence = (mCPKCampaignPoints[sKey].nPoints / nCampaignPoints) * nCampaignPercentage;
			// Verify we did not exceed the cap
			double nCap = GetProminenceCap(sCampaignName, mCPKCampaignPoints[sKey].nPoints, nPreProminence);
			mCPKCampaignPoints[sKey].nProminence = nCap;

			if (fDebugSpam)
				LogPrintf("\nUser %s, Campaign %s, Points %f, Prominence %f ", mCPKCampaignPoints[sKey].sAddress, sCampaignName, 
				mCPKCampaignPoints[sKey].nPoints, mCPKCampaignPoints[sKey].nProminence);
			std::string sLCN = sCampaignName == "WCG" && !Members.second.cpid.empty() ? sCampaignName + "-" + Members.second.cpid : sCampaignName;
			std::string sRow = sLCN + "|" + Members.second.sAddress + "|" + RoundToString(mCPKCampaignPoints[sKey].nPoints, 0) + "|" 
				+ RoundToString(mCPKCampaignPoints[sKey].nProminence, 8) + "|" + Members.second.sNickName + "|" + 
				RoundToString(nCampaignPoints, 0) + "\n";
			if (!sAnalyzeUser.empty() && sAnalyzeUser == Members.second.sNickName)
			{
				sAnalysisData2 += sRow;
			}
			if (mCPKCampaignPoints[sKey].nProminence > 0)
				sDetails += sRow;
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
	double nMaxContractPercentage = .98;
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
		CBitcoinAddress cbaAddress(Members.second.sAddress);
		if (cbaAddress.IsValid() && nPayment > (GSC_MIN_PAYMENT * COIN))
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
				+ RoundToString((double)nPayment / COIN, 2) + "\n";
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

		std::string DWSData;
		// Dynamic Whale Staking - R Andrews - 11/11/2019
		double dTotalWhalePayments = 0;
		std::vector<WhaleStake> dws = GetPayableWhaleStakes(nHeight, dTotalWhalePayments);
		for (int iWhale = 0; iWhale < dws.size(); iWhale++)
		{
			WhaleStake ws = dws[iWhale];
			// We already verified:  Burn was made successfully to the burn address, DWU has been checked for accuracy, DWU bounds and owed bounds has been checked, and the daily limit has been enforced
			// Note:  This vector only contains mature (owed) burns for this day.
			if (ws.found && ws.TotalOwed > 0 && !ws.ReturnAddress.empty())
			{
				sAddresses += ws.ReturnAddress + "|";
				sPayments += RoundToString(ws.TotalOwed, 4) + "|";
				DWSData += "<DWSROW><DWSADDR>" + ws.ReturnAddress + "</DWSADDR><DWSAMT>" + RoundToString(ws.TotalOwed, 4) + "</DWSAMT></DWSROW>";
			}
		}
		DWSData += "<DWSTOTAL>" + RoundToString(dTotalWhalePayments, 4) + "</DWSTOTAL>";
		QTData += DWSData;
		LogPrintf("\nCreating GSC Contract with Whale Payments=%f over %f recs.", dTotalWhalePayments, dws.size());
		// End of Dynamic Whale Staking 
		// Note that we need to leave Dynamic Whale Staking in until the last stake is paid 




		// Dash Staking - R Andrews - 8/16/2020
		std::string DSData;

		if (nHeight < consensusParams.TRIBULATION_HEIGHT)
		{
			double dTotalDashPayments = 0;
			std::vector<DashStake> dash = GetPayableDashStakes(nHeight, dTotalDashPayments);
			for (int iDash = 0; iDash < dash.size(); iDash++)
			{
				DashStake ws1 = dash[iDash];
				if (ws1.found && ws1.MonthlyEarnings > 0 && !ws1.ReturnAddress.empty())
				{
					sAddresses += ws1.ReturnAddress + "|";
					sPayments += RoundToString(ws1.MonthlyEarnings, 4) + "|";
					DSData += "<DSADDR>" + ws1.ReturnAddress + "</DSADDR><DSAMT>" + RoundToString(ws1.MonthlyEarnings, 4) + "</DSAMT>";
				}
			}
			DSData += "<DSTOTAL>" + RoundToString(dTotalDashPayments, 4) + "</DSTOTAL>";
			QTData += DSData;
			LogPrintf("\nCreating GSC With Dash Payments=%f over %f recs", dTotalDashPayments, dash.size());
			// End of Dash Staking
		}




		/*
		// R Andrews - 11-7-2020 - DAC (Decentralized Automonous Charity - Pay our Charities in a decentralized way):
		std::vector<std::string> vPayments = Split(Data.c_str(), "<BURN>");
		std::map<std::string, double> mapDAC = ();
		if (mapDAC.size() > 0)
		{

			for (int i = 0; i < vPayments.size(); i++)
			{
				std::string sDacPayment = vPayments[i];
				std::string sRecipient = ExtractXML(sDacPayment, "<RECIPIENT>", "</RECIPIENT>");
				double nAmount = cdbl(ExtractXML(sDacPayment, "<AMOUNT>", "</AMOUNT>"), 2);
				if (!sRecipient.empty() && nAmount > 0)
				{
					if (sRecipient == consensusParams.BurnAddressOrphanDonations || sRecipient == consensusParams.BurnAddressWhaleMatches)
					{
						for (auto dacAllocation : mapDAC)
						{
							std::string sAllocatedCharity = dacAllocation.first;
							double nPct = dacAllocation.second;
							double nAllocationAmount = nAmount * nPct;
							if (nPct <= 1 && nAllocationAmount > 0)
							{
									sAddresses += sAllocatedCharity + "|";
									sPayments += RoundToString(nAllocationAmount, 4) + "|";
							}

						}
					}
				}
			}
		}
		// End of DAC
		*/

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
		// Orphan Voting, and Charity Voting (11-7-2020 R ANDREWS):

		// End of Orphan Voting

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
	if (dDebugLevel == 1)
		LogPrintf("XML %s", sData);
	return sData;
}

int GetRequiredQuorumLevel(int nHeight)
{
	static int MINIMUM_QUORUM_PROD = 5;
	static int MINIMUM_QUORUM_TESTNET = 2;
	int nCount = deterministicMNManager->GetListAtChainTip().GetValidMNsCount();
	int nReq = nCount * .15;
	int nMinimumQuorum = fProd ? MINIMUM_QUORUM_PROD : MINIMUM_QUORUM_TESTNET;
	if (nReq < nMinimumQuorum) nReq = nMinimumQuorum;
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
	CAmount nPaymentsLimit = CSuperblock::GetPaymentsLimit(nHeight, nTime, true);
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

	if (fDebug)
		LogPrintf("\nSubmitting GSC Trigger %s ", govobj.GetDataAsPlainString());

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
        if (!govobj.IsValidLocally(strError, fMissingMasternode, fMissingConfirmations, true) && !fMissingConfirmations) 
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
	CAmount nPaymentsLimit = CSuperblock::GetPaymentsLimit(nBlockHeight, nTime, true);
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
		LogPrintf(" \n ** GetContractPaymentData::Superblock Payment Budget is out of bounds:  Limit %f,  Actual %f  ** \n", (double)nPaymentsLimit/COIN, (double)dTotalPaid);
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
		return std::string();
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
      
	CAmount nPaymentsLimit = CSuperblock::GetPaymentsLimit(nHeight, GetAdjustedTime(), false);
	nPaymentsLimit -= MAX_BLOCK_SUBSIDY * COIN;

	std::string sContract = GetGSCContract(nHeight, false);
	std::string sData = ExtractXML(sContract, "<DATA>", "</DATA>");
	std::string sDetails = ExtractXML(sContract, "<DETAILS>", "</DETAILS>");
	std::string sDiaries = ExtractXML(sContract, "<DIARIES>", "</DIARIES>");
	std::vector<std::string> vData = Split(sData.c_str(), "\n");
	std::vector<std::string> vDetails = Split(sDetails.c_str(), "\n");
	std::vector<std::string> vDiaries = Split(sDiaries.c_str(), "\n");
	results.push_back(Pair("Prominence v1.1", "Details"));
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

	double dTotalPaid = 0;
	// Allow room for a change in QT between first contract creation time and next superblock
	double nMaxContractPercentage = .98;
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
				+ ", Reward: " + RoundToString(nPayment, 3);
			if (Included(sFilterNickName, sCPK))
				results.push_back(Pair(sNarr, RoundToString(nProminence, 3) + "%"));
		}
	}

	return results;
}

void SendDistressSignal()
{
	static int64_t nLastReset = 0;
	if (GetAdjustedTime() - nLastReset > (60 * 60 * 1))
	{
		// Node will try to pull the gobjects again
		LogPrintf("\nSmartContract-Server::SendDistressSignal: Node is missing a gobject, pulling...%f\n", GetAdjustedTime());
		masternodeSync.Reset();
		masternodeSync.SwitchToNextAsset(*g_connman);
		nLastReset = GetAdjustedTime();
		ReassessAllChains();
	}
}

static int64_t nLastQuorumHashCheckup = 0;
std::string CheckGSCHealth()
{
	/*
	double nCheckGSCOptionDisabled = GetSporkDouble("disablegschealthcheck", 0);
	if (nCheckGSCOptionDisabled == 1)
		return "DISABLED";
	if (nLastQuorumHashCheckup == 0)
		nLastQuorumHashCheckup = GetAdjustedTime();

	int64_t nQHA = GetAdjustedTime() - nLastQuorumHashCheckup;
	if (nQHA < (60 * 60 * 1))
			return "WAITING";
	nLastQuorumHashCheckup = GetAdjustedTime();

	int iNextSuperblock = 0;
	int iLastSuperblock = GetLastGSCSuperblockHeight(chainActive.Tip()->nHeight, iNextSuperblock);
	std::string sAddresses;
	std::string sAmounts;
	int iVotes = 0;
	uint256 uGovObjHash = uint256S("0x0");
	uint256 uPAMHash = uint256S("0x0");
	ByHeight(iNextSuperblock, uPAMHash, iVotes, uGovObjHash, sAddresses, sAmounts);
	uint256 hPam = GetPAMHash(sAddresses, sAmounts);
	std::string sContract = GetGSCContract(iLastSuperblock, true);
	uint256 hPAMHash2 = GetPAMHashByContract(sContract);
	if (uGovObjHash == uint256S("0x0") || (hPAMHash2 != hPam))
	{
		SendDistressSignal();
		return "DISTRESS";
	}
	*/
	return "HEALTHY";
}

void SendOutGSCs()
{
	// In PODC 2.0, we send all of the GSCs out at the height shown in 'exec rac' from the main wallet thread.  (In contrast to sending them at a given time from the miner thread).
	// This is generally done once per day; but can be overridden with the key: dailygscfrequency=block_frequency
	// Note that the GSCs are now funded with coin-age from the external purse address (Christian-Public-Key).
	// As of November 2nd, 2019:  We have the campaigns:  WCG (PODC), HEALING, CAMEROON-ONE, and KAIROS.
	std::string sError;
	LogPrintf("\nSending out GSC Transmissions...%f\n", GetAdjustedTime());
	bool fCreated = CreateAllGSCTransmissions(sError);
	if (!fCreated)
		LogPrintf("\nEGSCQP::SendOutGSCs::Unable to create client side GSC transaction. (See Log [%s]). ", sError);
}

std::string ExecuteGenericSmartContractQuorumProcess()
{
	if (!chainActive.Tip()) 
		return "INVALID_CHAIN";

	if (!ChainSynced(chainActive.Tip()))
		return "CHAIN_NOT_SYNCED";
	
	int nFreq = (int)cdbl(GetArg("-dailygscfrequency", RoundToString(BLOCKS_PER_DAY, 0)), 0);
	if (nFreq < 50)
		nFreq = 50; 
	// Send out GSCs at midpoint of each day:
	bool fGSCTime = (chainActive.Tip()->nHeight % nFreq == (BLOCKS_PER_DAY/2));

	// UI Glitch in 1.4.8.5 fix (we normally have about 21,000 researchers in prod). 
	bool fReload = false;
	if (mvResearchers.size() < 500 && fProd && chainActive.Tip()->nHeight % 10 == 0)
		fReload = true;

	if (chainActive.Tip()->nHeight % 128 == 0 || fReload)
	{
		LoadResearchers();
	}

	if (fGSCTime)
		SendOutGSCs();

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
		if (fDebugSpam)
			LogPrintf("WatchmanOnTheWall::Status %s Contract %s", sWatchman, sContr);
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
		if (fDebug)
			LogPrintf("\n ExecuteGenericSmartContractQuorum::We have a pending superblock at height %f \n", (double)iNextSuperblock);
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

