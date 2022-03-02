// Copyright (c) 2014-2019 The Dash Core Developers, The DAC Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RPCPOG_H
#define RPCPOG_H

#include "hash.h"
#include "net.h"
#include "utilstrencodings.h"
#include "validation.h"
#include <univalue.h>
#include "base58.h"
#include <boost/algorithm/string/case_conv.hpp> // for to_lower()
#include <boost/algorithm/string/trim.hpp>
#include "governance/governance-classes.h"

struct BBPResult
{
	std::string Response;
	bool fError = false;
	CAmount nFee = 0;
	int nSize = 0;
	std::string TXID;
	std::string ErrorCode;
	std::string PublicKey;
	int64_t nTime = 0;
	CAmount nAmount = 0;
};

struct Sidechain
{
	std::string ObjectType;
	std::string URL;
	int64_t Time;
	int Height;
	void ToJson(UniValue& obj)
	{
		obj.clear();
		obj.setObject();
		obj.push_back(Pair("ObjectType", ObjectType));
		obj.push_back(Pair("URL", URL));
		obj.push_back(Pair("Time", Time));
		obj.push_back(Pair("Height", Height));
	}

};

struct Portfolio
{
	std::string OwnerAddress;
	std::string NickName;
	double AmountBBP;
	double AmountForeign;
	double AmountUSDBBP;
	double AmountUSDForeign;
	double AmountUSD;
	double Coverage;
	double Strength;
	double Owed;
	int64_t Time;
	void ToJson(UniValue& obj)
	{
		obj.clear();
		obj.setObject();
		obj.push_back(Pair("OwnerAddress", OwnerAddress));
		obj.push_back(Pair("AmountBBP", AmountBBP));
		obj.push_back(Pair("AmountForeign", AmountForeign));
	}
};

struct BBPProposal
{
	std::string sName;
	int64_t nStartEpoch = 0;
	int64_t nEndEpoch = 0;
	std::string sURL;
	std::string sExpenseType;
	double nAmount = 0;
	std::string sAddress;
	uint256 uHash = uint256S("0x0");
	int nHeight = 0;
	bool fPassing = false;
	int nNetYesVotes = 0;
	int nYesVotes = 0;
	int nNoVotes = 0;
	int nAbstainVotes = 0;
	int nMinPassing = 0;
	int nLastSuperblock = 0;
	bool fIsPaid = false;
	std::string sProposalHRTime;
};

uint256 CoordToUint256(int row, int col);
double StringToDouble(std::string s, int place);
std::string DoubleToString(double d, int place);
std::string ReverseHex(std::string const& src);
std::string DefaultRecAddress(std::string sType);
CBlockIndex* FindBlockByHeight(int nHeight);
std::string SignMessageEvo(std::string strAddress, std::string strMessage, std::string& sError);
bool RPCSendMoney(std::string& sError, std::string sAddress, CAmount nValue, std::string& sTXID, std::string sOptionalData = "");
std::vector<std::string> Split(std::string s, std::string delim);
bool SendManyXML(std::string XML, std::string& sTXID);
std::string ExtractXML(std::string XMLdata, std::string key, std::string key_end);
bool ValidateAddress2(std::string sAddress);
std::string PubKeyToAddress(const CScript& scriptPubKey);
boost::filesystem::path GetDeterministicConfigFile();
boost::filesystem::path GetMasternodeConfigFile();
boost::filesystem::path GetGenericFilePath(std::string sPath);
uint256 CRXT(uint256 hash, int64_t nPrevBlockTime, int64_t nBlockTime);
std::string PubKeyToAddress(const CScript& scriptPubKey);
std::string Uplink(bool bPost, std::string sPayload, std::string sBaseURL, std::string sPage, int iPort, int iTimeoutSecs, int iBOE, 
	std::map<std::string, std::string> mapRequestHeaders = std::map<std::string, std::string>(), std::string TargetFileName = "", bool fJson = false);
BBPResult UnchainedQuery(std::string sXMLSource, std::string sAPI);
BBPResult SidechainQuery(std::string sXMLSource, std::string sAPI);
std::shared_ptr<CReserveScript> GetScriptForMining();
std::string TimestampToHRDate(double dtm);
std::vector<Portfolio> GetDailySuperblock(int nHeight);
std::string GJE(std::string sKey, std::string sValue, bool bIncludeDelimiter, bool bQuoteValue);
bool ValidateDailySuperblock(const CTransaction& txNew, int nBlockHeight, int64_t nBlockTime);
bool IsDailySuperblock(int nHeight);
CAmount GetDailyPaymentsLimit(int nHeight);
std::string WatchmanOnTheWall(bool fForce, std::string& sContract);
bool ChainSynced(CBlockIndex* pindex);
bool Contains(std::string data, std::string instring);
std::string ScanChainForData(int nHeight);
std::string strReplace(std::string str_input, std::string str_to_find, std::string str_to_replace_with);
double AddressToPinV2(std::string sUnchainedAddress, std::string sCryptoAddress);
void LockStakes();
bool CompareMask2(CAmount nAmount, double nMask);
const CBlockIndex* GetBlockIndexByTransactionHash(const uint256& hash);
std::tuple<std::string, std::string, std::string> GetOrphanPOOSURL(std::string sSanctuaryPubKey);
bool POOSOrphanTest(std::string sSanctuaryPubKey, int64_t nTimeout);
int GetNextDailySuperblock(int nHeight);
std::string AmountToString(const CAmount& amount);
void MemorizeSidechain(bool fDuringConnectBlock, bool fColdBoot);
int DeserializeSidechainFromFile();
void SerializeSidechainToFile(int nHeight);
std::string Mid(std::string data, int nStart, int nLength);
CAmount ARM64();

#endif
