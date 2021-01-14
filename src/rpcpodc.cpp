// Copyright (c) 2014-2019 The Dash-Core Developers, The DAC Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "spork.h"
#include "utilmoneystr.h"
#include "masternode-payments.h"
#include "masternodeconfig.h"
#include "activemasternode.h"
#include "governance-classes.h"
#include "masternode-sync.h"
#include "smartcontract-server.h"
#include "rpcpog.h"
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string/case_conv.hpp> // for to_lower()
#include <boost/algorithm/string.hpp> // for trim()
#include <boost/date_time/posix_time/posix_time.hpp> // for StringToUnixTime()
#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <openssl/crypto.h>
#include <stdint.h>
#include <univalue.h>
#include <fstream>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

extern CWallet* pwalletMain;

std::string GetSANDirectory2()
{
	 std::string prefix = CURRENCY_NAME;
	 boost::to_lower(prefix);
	 boost::filesystem::path pathConfigFile(GetArg("-conf", prefix + ".conf"));
     if (!pathConfigFile.is_complete()) 
		 pathConfigFile = GetDataDir(false) / pathConfigFile;
	 boost::filesystem::path dir = pathConfigFile.parent_path();
	 std::string sDir = dir.string() + "/SAN/";
	 boost::filesystem::path pathSAN(sDir);
	 if (!boost::filesystem::exists(pathSAN))
	 {
		 boost::filesystem::create_directory(pathSAN);
	 }
	 return sDir;
}

std::string ToYesNo(bool bValue)
{
	std::string sYesNo = bValue ? "Yes" : "No";
	return sYesNo;
}


bool SignStake(std::string sBitcoinAddress, std::string strMessage, std::string& sError, std::string& sSignature)
{
	 LOCK(cs_main);
	 {
		CBitcoinAddress addr(sBitcoinAddress);
		CKeyID keyID;
		if (!addr.GetKeyID(keyID))
		{
			sError = "Address does not refer to key";
			return false;
		}
		CKey key;
		if (!pwalletMain->GetKey(keyID, key))
		{
			sError = "Private key not available for " + sBitcoinAddress + ".";
			LogPrintf("Unable to sign message %s with key %s.\n", strMessage, sBitcoinAddress);
			return false;
		}
		CHashWriter ss(SER_GETHASH, 0);
		ss << strMessageMagic;
		ss << strMessage;
		std::vector<unsigned char> vchSig;
		if (!key.SignCompact(ss.GetHash(), vchSig))
		{
			sError = "Sign failed";
			return false;
		}
		sSignature = EncodeBase64(&vchSig[0], vchSig.size());
		LogPrintf("Signed message %s successfully with address %s \n", strMessage, sBitcoinAddress);
		return true;
	 }
}


std::string SendBlockchainMessage(std::string sType, std::string sPrimaryKey, std::string sValue, double dStorageFee, int nSign, std::string sExtraPayload, std::string& sError)
{
	const Consensus::Params& consensusParams = Params().GetConsensus();
    std::string sAddress = consensusParams.FoundationAddress;
    CBitcoinAddress address(sAddress);
    if (!address.IsValid())
	{
		sError = "Invalid Destination Address";
		return sError;
	}
    CAmount nAmount = dStorageFee * COIN;
	CAmount nMinimumBalance = dStorageFee * COIN;
    CWalletTx wtx;
	boost::to_upper(sPrimaryKey); // DC Message can't be found if not uppercase
	boost::to_upper(sType);
	std::string sNonceValue = RoundToString(GetAdjustedTime(), 0);
 	std::string sMessageType      = "<MT>" + sType  + "</MT>";  
    std::string sMessageKey       = "<MK>" + sPrimaryKey   + "</MK>";
	std::string sMessageValue     = "<MV>" + sValue + "</MV>";
	std::string sNonce            = "<NONCE>" + sNonceValue + "</NONCE>";
	std::string sMessageSig = "";
	std::string sSignature = "";

	if (nSign==1)
	{
		// Sign as if this is a spork
		bool bSigned = SignStake(consensusParams.FoundationAddress, sValue + sNonceValue, sError, sSignature);
		if (bSigned) 
		{
			sMessageSig = "<SPORKSIG>" + sSignature + "</SPORKSIG>";
			sMessageSig += "<BOSIG>" + sSignature + "</BOSIG>";
			sMessageSig += "<BOSIGNER>" + consensusParams.FoundationAddress + "</BOSIGNER>";
		}
		if (!bSigned)
			LogPrintf("Unable to sign spork %s ", sError);
		LogPrintf(" Signing Nonce%f , With spork Sig %s on message %s  \n", (double)GetAdjustedTime(), 
			 sMessageSig.c_str(), sValue.c_str());
	}
	else if (nSign == 2)
	{
		// Sign as if this is a business object
		std::string sSignCPK = DefaultRecAddress("Christian-Public-Key");

		bool bSigned = SignStake(sSignCPK, sValue + sNonceValue, sError, sSignature);
		if (bSigned) 
		{
			sMessageSig = "<SPORKSIG>" + sSignature + "</SPORKSIG>";
			sMessageSig += "<BOSIG>" + sSignature + "</BOSIG>";
			sMessageSig += "<BOSIGNER>" + sSignCPK + "</BOSIGNER>";
		}
		if (!bSigned)
			LogPrintf("Unable to sign business object %s ", sError);
		LogPrintf(" Signing Nonce%f , With business-object Sig %s on message %s  \n", (double)GetAdjustedTime(), 
			 sMessageSig.c_str(), sValue.c_str());
	}
	std::string s1 = sMessageType + sMessageKey + sMessageValue + sNonce + sMessageSig + sExtraPayload;
	LogPrintf("SendBlockchainMessage %s", s1);
	bool fSubtractFee = false;
	bool fInstantSend = false;
	bool fSent = RPCSendMoney(sError, address.Get(), nAmount, fSubtractFee, wtx, fInstantSend, s1);

	if (!sError.empty())
		return std::string();
	if (sMessageType == "cpk")
	{
		// For the user record - The below did not work - Research
		// WriteCache(sMessageType, sMessageKey, sMessageValue, GetAdjustedTime(), true);
	}
    return wtx.GetHash().GetHex().c_str();
}


std::string GetGithubVersion()
{
	std::string sURL = "https://" + GetSporkValue("bms");
	std::string sRestfulURL = "BMS/LAST_MANDATORY_VERSION";
	std::string sV = ExtractXML(Uplink(false, "", sURL, sRestfulURL, SSL_PORT, 25, 1), "<VERSION>", "</VERSION>");
	return sV;
}

double GetCryptoPrice(std::string sSymbol)
{
	if (sSymbol.empty())
		return 0;

	boost::to_lower(sSymbol);

	double nLast = cdbl(ReadCacheWithMaxAge("price", sSymbol, (60 * 30)), 12);
	if (nLast > 0)
		return nLast;
	
	std::string sC1 = Uplink(false, "", GetSporkValue("bms"), GetSporkValue("getbmscryptoprice" + sSymbol), SSL_PORT, 15, 1);
	std::string sPrice = ExtractXML(sC1, "<MIDPOINT>", "</MIDPOINT>");
	double dMid = cdbl(sPrice, 12);
	WriteCache("price", sSymbol, RoundToString(dMid, 12), GetAdjustedTime());
	return dMid;
}

double GetPBase(double& out_BTC, double& out_BBP)
{
	// Get the DAC market price based on midpoint of bid-ask in Satoshi * BTC price in USD
	double dBBPPrice = GetCryptoPrice("bbp");  // ToDo:  Revisit this after the community votes, etc.
	double dBTC = GetCryptoPrice("btc");
	out_BTC = dBTC;
	out_BBP = dBBPPrice;

	double nBBPOverride = cdbl(GetSporkValue("BBPPRICE"), 12);
	if (!fProd && nBBPOverride > 0)
	{
		// In Testnet, allow us to override the BBP price with a spork price so we can test APM
		out_BBP = nBBPOverride;
	}

	double dPriceUSD = dBTC * dBBPPrice;
	return dPriceUSD;
}

bool VerifySigner(std::string sXML)
{
	std::string sSignature = ExtractXML(sXML, "<sig>", "</sig>");
	std::string sSigner = ExtractXML(sXML, "<signer>", "</signer>");
	std::string sMessage = ExtractXML(sXML, "<message>", "</message>");
	std::string sError;
	bool fValid = CheckStakeSignature(sSigner, sSignature, sMessage, sError);
	return fValid;
}

bool GetTransactionTimeAndAmount(uint256 txhash, int nVout, int64_t& nTime, CAmount& nAmount)
{
	uint256 hashBlock = uint256();
	CTransactionRef tx2;
	if (GetTransaction(txhash, tx2, Params().GetConsensus(), hashBlock, true))
	{
		   BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
           if (mi != mapBlockIndex.end() && (*mi).second) 
		   {
              CBlockIndex* pMNIndex = (*mi).second; 
			  nTime = pMNIndex->GetBlockTime();
		      nAmount = tx2->vout[nVout].nValue;
			  return true;
		   }
	}
	return false;
}

std::string rPad(std::string data, int minWidth)
{
	if ((int)data.length() >= minWidth) return data;
	int iPadding = minWidth - data.length();
	std::string sPadding = std::string(iPadding,' ');
	std::string sOut = data + sPadding;
	return sOut;
}

int64_t GetDCCFileAge()
{
	std::string sRACFile = GetSANDirectory2() + "wcg.rac";
	boost::filesystem::path pathFiltered(sRACFile);
	if (!boost::filesystem::exists(pathFiltered)) 
		return GetAdjustedTime() - 0;
	int64_t nTime = last_write_time(pathFiltered);
	int64_t nAge = GetAdjustedTime() - nTime;
	return nAge;
}

int GetWCGMemberID(std::string sMemberName, std::string sAuthCode, double& nPoints)
{
	std::string sDomain = "https://www.worldcommunitygrid.org";
	std::string sRestfulURL = "verifyMember.do?name=" + sMemberName + "&code=" + sAuthCode;
	std::string sResponse = Uplink(true, "", sDomain, sRestfulURL, SSL_PORT, 12, 1);
	int iID = (int)cdbl(ExtractXML(sResponse, "<MemberId>","</MemberId>"), 0);
	nPoints = cdbl(ExtractXML(sResponse, "<Points>", "</Points>"), 2);
	return iID;
}

Researcher GetResearcherByID(int nID)
{
    BOOST_FOREACH(const PAIRTYPE(const std::string, Researcher)& myResearcher, mvResearchers)
    {
		if (myResearcher.second.found && myResearcher.second.id == nID)
		{
			return mvResearchers[myResearcher.second.cpid];
		}
	}
	Researcher r;
	r.found = false;
	r.teamid = 0;
	return r;
}

std::map<std::string, Researcher> GetPayableResearchers()
{
	// Rules:

	// Researcher is in the team
	// RAC > 1 
	// Researchers reverse CPID lookup matches the signed association record (this ensures the LIFO rule is honored allowing re-associations)
	// Each CPID is only included ONCE
	// The CPID is Unbanked if the RAC < 250
	// Unbanked researchers do not need to post daily stake collateral
	// Banked researchers do need to post daily stake collateral:  RAC^1.30 in COIN-AGE per day
	std::vector<std::tuple<int64_t, std::string, std::string> > vFIFO;
	vFIFO.reserve(mvResearchers.size() * 2);
	std::map<std::string, Researcher> r;
	std::map<std::string, std::string> cpid_reverse_lookup;
	for (auto ii : mvApplicationCache)
	{
		if (Contains(ii.first, "CPK-WCG"))
		{
			std::string sData = ii.second.first;
			int64_t nLockTime = ii.second.second;
			std::string cpid = GetCPIDElementByData(sData, 8);
			std::string sCPK = GetCPIDElementByData(sData, 0);
			vFIFO.push_back(std::make_tuple(nLockTime, cpid, sCPK));
			if (fDebugSpam)
				LogPrintf("cpid %s cpk %s locktime %f", cpid, sCPK, nLockTime);
		}
	}
		

    // LIFO Sort
	std::sort(vFIFO.begin(), vFIFO.end());

	for (auto item : vFIFO)
    {
		std::string cpid = std::get<1>(item); //item.second;
		std::string sCPK = std::get<2>(item); //item.third;
		cpid_reverse_lookup[cpid] = sCPK;
		if (fDebugSpam)
			LogPrintf("Adding cpid %s with %s ", cpid, sCPK);
	}
	
	// Payable Researchers
	BOOST_FOREACH(const PAIRTYPE(const std::string, Researcher)& myResearcher, mvResearchers)
    {
		if (myResearcher.second.found)
		{
			if (myResearcher.second.rac > 1)
			{
				std::string sSourceCPK = cpid_reverse_lookup[myResearcher.second.cpid];
				std::string sSignedCPID = GetCPIDByCPK(sSourceCPK);
				
				if (sSignedCPID == myResearcher.second.cpid && !myResearcher.second.cpid.empty())
				{
					if (myResearcher.second.rac > 1)
					{
						r[sSignedCPID] = myResearcher.second;
						if (fDebugSpam)
							LogPrintf("\nGetPayableResearchers::Adding %s for %s", sSignedCPID, sSourceCPK);
					}
				}
				else
				{
					if (fDebugSpam)
						LogPrintf("\nGPR::Not Adding %s because %s", sSignedCPID, sSourceCPK);
				}
			}
		}
	}
	return r;
}

