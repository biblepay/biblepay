// Copyright (c) 2014-2019 The Dash Core Developers, The DAC Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RPCPOG_H
#define RPCPOG_H

#include "hash.h"
#include "net.h"
#include "validation.h"
#include <univalue.h>
#include "base58.h"
#include <boost/algorithm/string/case_conv.hpp> // for to_lower()
#include <boost/algorithm/string/trim.hpp>
#include <txmempool.h>
#include <memory>
#include <optional>
#include <stdint.h>
#include <context.h>
#include <shutdown.h>
#include <evo/deterministicmns.h>


class JSONRPCRequest;
class CGlobalNode;
class CChainState;
struct NodeContext;


namespace interfaces {
class Handler;
class Node;
class Wallet;
};

const std::string MESSAGE_MAGIC_BBP = "DarkCoin Signed Message:\n";


//CGovernanceManager& governance_manager;
//const llmq::CQuorumBlockProcessor& quorum_block_processor;
//llmq::CInstantSendManager& m_isman;



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
        /*
        obj.push_back(std::make_pair("ObjectType", ObjectType));
		obj.push_back(std::make_pair("URL", URL));
		obj.push_back(std::make_pair("Time", Time));
		obj.push_back(std::make_pair("Height", Height));
        */
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
        /*obj.push_back(std::make_pair("OwnerAddress", OwnerAddress));
		obj.push_back(std::make_pair("AmountBBP", AmountBBP));
		obj.push_back(std::make_pair("AmountForeign", AmountForeign));
        */
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
std::string DefaultRecAddress(JSONRPCRequest r,std::string sType);
CBlockIndex* FindBlockByHeight(int nHeight);
std::string SignMessageEvo(JSONRPCRequest r,std::string strAddress, std::string strMessage, std::string& sError);
bool RPCSendMoney(JSONRPCRequest r,std::string& sError, std::string sAddress, CAmount nValue, std::string& sTXID, std::string sOptionalData, int& nVoutPosition);
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
CScript GetScriptForMining(JSONRPCRequest r);
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
//const CBlockIndex* GetBlockIndexByTransactionHash(const uint256& hash);
std::tuple<std::string, std::string, std::string> GetPOVSURL(std::string sSanctuaryPubKey, std::string sIP, int iType);
bool POVSTest(std::string sSanctuaryPubKey, std::string sIP, int64_t nTimeout, int nType);
int GetNextDailySuperblock(int nHeight);
std::string AmountToString(const CAmount& amount);
void MemorizeSidechain(bool fDuringConnectBlock, bool fColdBoot);
int DeserializeSidechainFromFile();
void SerializeSidechainToFile(int nHeight);
std::string Mid(std::string data, int nStart, int nLength);
CAmount ARM64();
uint64_t IsHODLAddress(std::string sAddress);
bool CheckTLTTx(const CTransaction& tx, const CCoinsViewCache& view);
std::string GetElement(std::string sData, std::string sDelimiter, int iPos);
CAmount GetWalletBalance(JSONRPCRequest r);
std::string GetSanctuaryMiningAddress();
void WriteUnchainedConfigFile(std::string sPub, std::string sPriv);
void ReadUnchainedConfigFile(std::string& sPub, std::string& sPriv);
std::string url_encode(std::string value);
bool IsMyAddress(JSONRPCRequest r,std::string sAddress);
bool ReviveSanctuaryEnhanced(JSONRPCRequest rold, std::string sSancSearch, std::string& sError, UniValue& uSuccess);
std::string ScanDeterministicConfigFile(std::string sSearch);
std::string ProvisionUnchained2(JSONRPCRequest r, std::string& sError);
std::string GetPrivKey2(JSONRPCRequest r, std::string sPubKey, std::string& sError);
std::string ReceiveIPC();
void WriteIPC(std::string sData);
std::string ReviveSanctuariesJob(JSONRPCRequest r);
bool TcpTest(std::string sIP, int nPort, int nTimeout);
bool IsMySanc(JSONRPCRequest r,std::string sSearchProRegTxHash);
BBPResult UnchainedGet(std::string sAPIPath);
bool IsSanctuaryCollateral(CAmount nAmount);
CAmount GetSancCollateralAmount(std::string sSearch);
std::string GetSidechainValue(std::string sType, std::string sKey, int nMinTimestamp);
void MilliSleep(int64_t n);
BBPResult UnchainedApiGet();
std::string ComputeMinedBlockVersion();
bool IsSanctuaryLegacyTempleOrAltar(CDeterministicMNCPtr dmn);
bool IsSanctuaryPoseBanned(CDeterministicMNCPtr dmn);

bool ContextualCheckBlockMinedBySanc(const CBlock& block);
std::string Test1000();

/** Used to store a reference to the global node */
//static const CoreContext* g_bbp_core_context_ptr10;
//static const NodeContext* ode_context_ptr10;
//void setNodePog(interfaces::Node& node);
// const CoreContext& GetGlobalCoreContext();
// const NodeContext& ();



class CGlobalNode
{
    
    private:
        inline static const CoreContext* z_bbp_core_context_ptr;
        inline static const NodeContext* z_bbp_node_context_ptr;
    public:
        
         static const CoreContext& GetGlobalCoreContext()
         {
                const CoreContext& cc(*z_bbp_core_context_ptr);
                return cc;
         }

         static void SetGlobalCoreContext(const CoreContext& core)
         {
                z_bbp_core_context_ptr = &core;
         }

         static const NodeContext& GetGlobalNodeContext()
         {
                const NodeContext& cc(*z_bbp_node_context_ptr);
                return cc;
         }

         static void SetGlobalNodeContext(const NodeContext& node)
         {
                z_bbp_node_context_ptr = &node;
         }

};





#endif
