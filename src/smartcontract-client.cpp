// Copyright (c) 2014-2019 The Dash Core Developers, The DAC Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "smartcontract-client.h"
#include "smartcontract-server.h"
#include "util.h"
#include "utilmoneystr.h"
#include "rpcpog.h"
#include "init.h"
#include "messagesigner.h"
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string/case_conv.hpp> // for to_lower()
#include <boost/algorithm/string.hpp> // for trim()
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


//////////////////////////////////////////////////////////////////// DAC - SMART CONTRACTS - CLIENT SIDE ///////////////////////////////////////////////////////////////////////////////////////////////

CPK GetCPKFromProject(std::string sProjName, std::string sCPKPtr)
{
	std::string sRec = GetCPKData(sProjName, sCPKPtr);
	CPK oCPK = GetCPK(sRec);
	return oCPK;
}

UniValue GetCampaigns()
{
	UniValue results(UniValue::VOBJ);
	std::map<std::string, std::string> mCampaigns = GetSporkMap("spork", "gsccampaigns");
	int i = 0;
	// List of Campaigns
	results.push_back(Pair("List Of", "DAC Campaigns"));
	for (auto s : mCampaigns)
	{
		results.push_back(Pair("campaign " + s.first, s.first));
	} 

	results.push_back(Pair("List Of", "DAC CPKs"));
	// List of Christian-Keypairs (Global members)
	std::map<std::string, CPK> cp = GetGSCMap("cpk", "", true);
	for (std::pair<std::string, CPK> a : cp)
	{
		CPK oPrimary = GetCPKFromProject("cpk", a.second.sAddress);
		results.push_back(Pair("member [" + Caption(oPrimary.sNickName, 10) + "]", a.second.sAddress));
	}

	results.push_back(Pair("List Of", "Campaign Participants"));
	// List of participating CPKs per Campaign
	for (auto s : mCampaigns)
	{
		std::string sCampaign = s.first;
		std::map<std::string, CPK> cp1 = GetGSCMap("cpk-" + sCampaign, "", true);
		for (std::pair<std::string, CPK> a : cp1)
		{
			CPK oPrimary = GetCPKFromProject("cpk", a.second.sAddress);
			results.push_back(Pair("campaign-" + sCampaign + "-member [" + Caption(oPrimary.sNickName, 10) + "]", oPrimary.sAddress));
		}
	}
	
	return results;
}

CPK GetMyCPK(std::string sProjectName)
{
	std::string sCPK = DefaultRecAddress("Christian-Public-Key");
	std::string sRec = GetCPKData(sProjectName, sCPK);
	CPK myCPK = GetCPK(sRec);
	return myCPK;
}

bool CheckCampaign(std::string sName)
{
	boost::to_upper(sName);
		
	std::map<std::string, std::string> mCampaigns = GetSporkMap("spork", "gsccampaigns");
	for (auto s : mCampaigns)
	{
		std::string sCampaignName = s.first;
		boost::to_upper(sCampaignName);
		if (sCampaignName == sName)
			return true;
	}
	if (sName == "BMS" || sName == "BMSUSER")
		return true;
	return false;
}

UniValue SentGSCCReport(int nHeight, std::string sMyCPK)
{
	UniValue results(UniValue::VOBJ);
	
	if (!chainActive.Tip()) 
		return NullUniValue;

	if (nHeight == 0) 
		nHeight = chainActive.Tip()->nHeight - 1;

	if (nHeight > chainActive.Tip()->nHeight - 1)
		nHeight = chainActive.Tip()->nHeight - 1;

	int nMaxDepth = nHeight;
	int nMinDepth = nMaxDepth - (BLOCKS_PER_DAY * 7);
	if (nMinDepth < 1) 
		return NullUniValue;

	CBlockIndex* pindex = FindBlockByHeight(nMinDepth);
	const Consensus::Params& consensusParams = Params().GetConsensus();

	double nTotalPoints = 0;
	if (sMyCPK.empty())
		return NullUniValue;

	while (pindex && pindex->nHeight < nMaxDepth)
	{
		if (pindex->nHeight < chainActive.Tip()->nHeight) 
			pindex = chainActive.Next(pindex);
		CBlock block;
		if (ReadBlockFromDisk(block, pindex, consensusParams)) 
		{
			for (unsigned int n = 0; n < block.vtx.size(); n++)
			{
				std::string sCampaignName;
				std::string sDate = TimestampToHRDate(pindex->GetBlockTime());

				if (block.vtx[n]->IsGSCTransmission() && CheckAntiBotNetSignature(block.vtx[n], "gsc", ""))
				{
					std::string sCPK = GetTxCPK(block.vtx[n], sCampaignName);
					double nCoinAge = 0;
					CAmount nDonation = 0;
					GetTransactionPoints(pindex, block.vtx[n], nCoinAge, nDonation);
					std::string sDiary = ExtractXML(block.vtx[n]->GetTxMessage(), "<diary>", "</diary>");
					if (CheckCampaign(sCampaignName) && !sCPK.empty() && sMyCPK == sCPK)
					{
						double nPoints = CalculatePoints(sCampaignName, sDiary, nCoinAge, nDonation, sCPK);
						std::string sReport = "Points: " + RoundToString(nPoints, 0) + ", Campaign: "+ sCampaignName 
							+ ", CoinAge: "+ RoundToString(nCoinAge, 0) + ", Donation: "+ RoundToString((double)nDonation/COIN, 2) 
							+ ", Height: "+ RoundToString(pindex->nHeight, 0) + ", Date: " + sDate;
						nTotalPoints += nPoints;
						results.push_back(Pair(block.vtx[n]->GetHash().GetHex(), sReport));
					}
				}
			}
		}
	}
	results.push_back(Pair("Total", nTotalPoints));
	return results;
}
	

	

//////////////////////////////////////////////////////////////////////////// CAMPAIGN #1 - Created March 23rd, 2019 - PROOF-OF-GIVING /////////////////////////////////////////////////////////////////////////////
// Loop through each campaign, create the applicable client-side transaction 

bool Enrolled(std::string sCampaignName, std::string& sError)
{
	// First, verify the user is in good standing
	CPK myCPK = GetMyCPK("cpk");
	if (myCPK.sAddress.empty()) 
	{
		LogPrintf("CPK Missing %f\n", 789);
		sError = "User has no CPK.";
		return false;
	}
	// If we got this far, it was signed.
	if (sCampaignName == "cpk")
		return true;
	// Now check the project signature.
	std::string scn = "cpk-" + sCampaignName;
	boost::to_upper(scn);

	CPK myProject = GetMyCPK(scn);
	if (myProject.sAddress.empty())
	{
		sError = "User is not enrolled in " + scn + ".";
		return false;
	}
	return true;
}

