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
#include <wallet/coincontrol.h>
#include <wallet/rpcwallet.h>

#include <wallet/wallet.h>
#include <wallet/walletdb.h>
#include <wallet/walletutil.h>


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
double StringToDouble(std::string s, int place);
bool CheckStakeSignature(std::string sBitcoinAddress, std::string sSignature, std::string strMessage, std::string& strError);
std::string GetUniString(UniValue o, std::string sMemberName);
double GetUniReal(UniValue o, std::string sMemberName);
int GetUniInt(UniValue o, std::string sMemberName);
std::string ExtractXML(std::string XMLdata, std::string key, std::string key_end);
std::string DoubleToString(double d, int place);


struct BBPResult
{
	std::string Response;
	bool fError = false;
    CAmount nFee = 0;
	int nSize = 0;
	std::string TXID;
	std::string ErrorCode;
	std::string PublicKey;
    std::string Address;
	int64_t nTime = 0;
	CAmount nAmount = 0;
};


struct Sidechain
{
	std::string ObjectType;
	std::string URL;
	int64_t Time;
	int Height;
    std::string TXID;

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

struct BBPTradingMessage
{
        std::string id;
        std::string Signer; // BBP Address of Buyer or Seller
        std::string AltAddress; // DOGE Address of Buyer or Seller
        std::string Message;
        std::string Signature;
        int Version;
        std::string Action;
        std::string Serialize()
        {
                std::string sSer = "<id>" + id + "</id><altaddress>" + AltAddress + "</altaddress><signer>" + Signer + "</signer><message>" + Message + "</message><signature>"
                    + Signature + "</signature><version>" + DoubleToString(Version, 0) + "</version>" + "<action>" + Action + "</action>";
                return sSer;
        }
        void Deserialize(std::string sData)
        {
                id = ExtractXML(sData, "<id>", "</id>");
                Signer = ExtractXML(sData, "<signer>", "</signer>");
                Message = ExtractXML(sData, "<message>", "</message>");
                AltAddress = ExtractXML(sData, "<altaddress>", "</altaddress>");
                Signature = ExtractXML(sData, "<signature>", "</signature>");
                Version = (int)StringToDouble(ExtractXML(sData, "<version>", "</version>"), 0);
                Action = ExtractXML(sData, "<action>", "</action>");
        }
        bool IsValid()
        {
             if (id.length() < 2) return false;
             if (Signer.length() < 2 || Signature.length() < 2) return false;
             std::string sSigError;
             bool fPassed = CheckStakeSignature(Signer, Signature, Message, sSigError);
             return fPassed;
        }

};



struct AtomicSymbol
{
     std::string Symbol;
     std::string ShortCode;
     std::string AddressBookEntry;
     std::string LongAssetName;
     std::string BlockExplorerURL;
     std::string IconName;
     bool Found = false;
};


struct AtomicTrade
{
     std::string SymbolBuy;  // BBP
     std::string SymbolSell; // DOGE
     std::string Action;     // BUY,SELL
     std::string id;         // Trade GUID
     std::string Signer;     // BBP Address of person placing trade
     std::string Message;
     std::string Signature;
     std::string AltAddress; // Address of ALT coin (pubkey)
     std::string TXID;       // TXID of completed trade.
     std::string Error; 
     int Version;
     double Quantity;
     double Price;
     std::string Status;     // Open, Closed, Transferring
     std::string MatchedTo;  // Matching ID
     int FilledQuantity;
     int Time;
     int Height;
     std::string CollateralBBPAddress;
     std::string CollateralDOGEAddress;
     std::string CollateralALTAddress;
     std::string CollateralAssetAddress;

     std::string CollateralTXID;
     std::string ReturnTXID;
     std::string BlockExplorerURL;

     void New()
     {
             Version = 1;
             Quantity = 0;
             Price = 0;
             Status = "NA";
             MatchedTo = "";
             FilledQuantity = 0;
             Time = GetAdjustedTime();
             Height = 0;
             BlockExplorerURL = "";
     }

     void PopulateBX()
     {
             if (Status == "canceled" && ReturnTXID != "")
             {
                 if (Action == "buy") {
                     BlockExplorerURL = "https://live.blockcypher.com/doge/tx/" + ReturnTXID + "/";
                 } else if (Action == "sell") {
                     BlockExplorerURL = "https://chainz.cryptoid.info/bbp/tx.dws?" + ReturnTXID + "/";
                 }
             }

             if (Status == "open" && CollateralTXID != "")
             {
                 if (Action == "buy") {
                     BlockExplorerURL = "https://live.blockcypher.com/doge/tx/" + CollateralTXID + "/";
                 } else if (Action == "sell") {
                     BlockExplorerURL = "https://chainz.cryptoid.info/bbp/tx.dws?" + CollateralTXID + "/";
                 } else {
                     BlockExplorerURL = "NA";
                 }
             }
     }
     void ToJson(UniValue& obj)
     {
           obj.clear();
           obj.setObject();
           obj.pushKV("SymbolBuy", SymbolBuy);
           obj.pushKV("SymbolSell", SymbolSell);
           obj.pushKV("Action", Action);
           obj.pushKV("id", id);
           obj.pushKV("Version", Version);
           obj.pushKV("Quantity", Quantity);
           obj.pushKV("Price", Price);
           obj.pushKV("Status", Status);

           obj.pushKV("Signer", Signer);
           obj.pushKV("Signature", Signature);
           obj.pushKV("CollateralBBPAddress", CollateralBBPAddress);
           obj.pushKV("CollateralDOGEAddress", CollateralDOGEAddress);
           obj.pushKV("CollateralALTAddress", CollateralALTAddress);
           obj.pushKV("CollateralAssetAddress", CollateralAssetAddress);

           obj.pushKV("CollateralTXID", CollateralTXID);
           obj.pushKV("ReturnTXID", ReturnTXID);
           PopulateBX();
           obj.pushKV("BlockExplorerURL", BlockExplorerURL);

           obj.pushKV("Message", Message);

           obj.pushKV("AltAddress", AltAddress);
           obj.pushKV("TXID", TXID);
           obj.pushKV("MatchedTo", MatchedTo);
           obj.pushKV("FilledQuantity", FilledQuantity);
           obj.pushKV("Time", Time);
           obj.pushKV("Height", Height);
           obj.pushKV("Error", Error);

     }
     AtomicTrade FromJson(std::string sJson)
     {
           AtomicTrade a;
           a.New();
           UniValue obj;
           obj.clear();
           obj.setObject();
           if (!obj.read(sJson)) {
               a.id = "0";
               return a;
           }
           a.SymbolBuy = GetUniString(obj, "SymbolBuy");
           a.SymbolSell = GetUniString(obj, "SymbolSell");
           a.Action = GetUniString(obj, "Action");
           a.id = GetUniString(obj, "id");
           a.Quantity = GetUniReal(obj, "Quantity");
           a.Price = GetUniReal(obj, "Price");
           a.Status = GetUniString(obj, "Status");
           a.CollateralTXID = GetUniString(obj, "CollateralTXID");
           a.ReturnTXID = GetUniString(obj, "ReturnTXID");

           a.CollateralBBPAddress = GetUniString(obj, "CollateralBBPAddress");
           a.CollateralDOGEAddress = GetUniString(obj, "CollateralDOGEAddress");
           a.CollateralALTAddress = GetUniString(obj, "CollateralALTAddress");
           a.CollateralAssetAddress = GetUniString(obj, "CollateralAssetAddress");

           a.Signer = GetUniString(obj, "Signer");
           a.Message = GetUniString(obj, "Message");
           a.Signature = GetUniString(obj, "Signature");

           a.AltAddress = GetUniString(obj, "AltAddress");
           a.TXID = GetUniString(obj, "TXID");
           a.Version = GetUniInt(obj, "Version");
           a.MatchedTo = GetUniString(obj, "MatchedTo");
           a.FilledQuantity = GetUniInt(obj, "FilledQuantity");
           a.Time = GetUniInt(obj, "Time");
           a.Height = GetUniInt(obj, "Height");
           a.Error = GetUniString(obj, "Error");
           a.BlockExplorerURL = GetUniString(obj, "BlockExplorerURL");
           boost::to_lower(a.Action);
           boost::trim(a.Action);
           boost::to_lower(a.Status);
           boost::to_lower(a.SymbolBuy);
           boost::to_lower(a.SymbolSell);
           return a;
     }
     bool IsValid()
     {
           if (id.length() < 2) return false;
           if (Signer.length() < 2 || Signature.length() < 2) return false;
           
           std::string sSigError;
           bool fPassed = CheckStakeSignature(Signer, Signature, Message, sSigError);
           return fPassed;
     }
     std::string ToString()
     {
           UniValue obj;
           ToJson(obj);
           std::string strJSON = obj.write() + "\n";
           return strJSON;
     }
};


struct NFT {
        std::string Category; // ORPHAN,GENERAL,CHRISTIAN
        std::string Name;
        std::string Action;
        std::string Description;
        std::string AssetURL;
        std::string JsonURL;
        std::string id;
        std::string Signer;
        std::string Signature;
        std::string Message;

        double BuyItNowAmount;
        int SoulBound;
        int Marketable;
        int Deleted;
        int Version;
        void ToJson(UniValue& obj)
        {
                obj.clear();
                obj.setObject();
                obj.pushKV("Category", Category);
                obj.pushKV("Name", Name);
                obj.pushKV("Action", Action);
                obj.pushKV("Description", Description);
                obj.pushKV("AssetURL", AssetURL);
                obj.pushKV("JsonURL", JsonURL);
                obj.pushKV("id", id);
                obj.pushKV("BuyItNowAmount", BuyItNowAmount);
                obj.pushKV("SoulBound", SoulBound);
                obj.pushKV("Marketable", Marketable);
                obj.pushKV("Deleted", Deleted);
                obj.pushKV("Version", Version);
                obj.pushKV("Signer", Signer);
                obj.pushKV("Signature", Signature);
                obj.pushKV("Message", Message);
        }


    NFT FromJson(std::string sJson)
    {
           NFT n;
           UniValue obj;
           obj.clear();
           obj.setObject();
           if (!obj.read(sJson))
           {
               n.id = "0";
               return n;
           }
           n.Category = GetUniString(obj, "Category");
           n.Name = GetUniString(obj, "Name");
           n.Action = GetUniString(obj, "Action");
           n.Description = GetUniString(obj, "Description");
           n.AssetURL = GetUniString(obj, "AssetURL");
           n.JsonURL = GetUniString(obj, "JsonURL");
           n.id = GetUniString(obj, "id");
           n.BuyItNowAmount = GetUniReal(obj, "BuyItNowAmount");
           n.SoulBound = GetUniInt(obj, "SoulBound");
           n.Marketable = GetUniInt(obj, "Marketable");
           n.Deleted = GetUniInt(obj, "Deleted");
           n.Version = GetUniInt(obj,"Version");
           n.Signer = GetUniString(obj, "Signer");
           n.Signature = GetUniString(obj, "Signature");
           n.Message = GetUniString(obj,"Message");
           boost::to_lower(n.Action);
           boost::trim(n.Action);
           return n;
    }
    bool IsValid()
    {
           if (id.length() < 2) return false;
           if (Signer.length() < 2 || Signature.length() < 2) return false;
           std::string sSigError;
           bool fPassed = CheckStakeSignature(Signer, Signature, Message, sSigError);
           return fPassed;
    }
    std::string ToString()
    {
           UniValue obj;
           ToJson(obj);
           std::string strJSON = obj.write() + "\n";
           return strJSON;
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
std::string ReverseHex(std::string const& src);
std::string DefaultRecAddress(JSONRPCRequest r,std::string sType);
CBlockIndex* FindBlockByHeight(int nHeight);
std::string SignMessageEvo(JSONRPCRequest r,std::string strAddress, std::string strMessage, std::string& sError);
bool RPCSendMoney(JSONRPCRequest r,std::string& sError, std::string sAddress, CAmount nValue, std::string& sTXID, std::string sOptionalData, int& nVoutPosition);
std::vector<std::string> Split(std::string s, std::string delim);
bool SendManyXML(std::string XML, std::string& sTXID);
bool ValidateAddress2(std::string sAddress);
std::string PubKeyToAddress(const CScript& scriptPubKey);
boost::filesystem::path GetDeterministicConfigFile();
boost::filesystem::path GetMasternodeConfigFile(); 
boost::filesystem::path GetGenericFilePath(std::string sPath);
uint256 CRXT(uint256 hash, int64_t nPrevBlockTime, int64_t nBlockTime);
std::string PubKeyToAddress(const CScript& scriptPubKey);
std::string Uplink(bool bPost, std::string sPayload, std::string sBaseURL, std::string sPage, int iPort, int iTimeoutSecs, int iBOE, 
	std::map<std::string, std::string> mapRequestHeaders = std::map<std::string, std::string>());
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
bool IsSanctuaryCollateral(CAmount nAmount);
CAmount GetSancCollateralAmount(std::string sSearch);
CAmount ExtrapolateSubsidy(CDeterministicMNCPtr dmnPayee, CAmount nAmount, bool fBlockChecking);
std::string GetSidechainValue(std::string sType, std::string sKey, int nMinTimestamp);
void MilliSleep(int64_t n);
BBPResult UnchainedApiGet();
std::string ComputeMinedBlockVersion();
bool IsSanctuaryLegacyTempleOrAltar(CDeterministicMNCPtr dmn);
bool IsSanctuaryPoseBanned(CDeterministicMNCPtr dmn);
bool ContextualCheckBlockMinedBySanc(const CBlock& block);
std::string GetSanctuaryTypeName(CDeterministicMNCPtr dmnPayee);
std::string GetSanctuaryTypeName(CDeterministicMN dmnPayee);
std::string Test1000();
void CreateWalletIfNotExists(JSONRPCRequest r);
std::map<std::string, NFT> GetNFTs();
std::map<std::string, AtomicTrade> GetAtomicTrades();
AtomicTrade GetAtomicTradeById(std::string sID);
bool AuthorizeNFT(NFT n, std::string& sError);
bool CheckMemPoolTransactionBiblepay(const CTransaction& tx, const CBlockIndex* pindexPrev);
CAmount GetSanctuaryCollateralAmount();
std::string EncryptBlockCypherString(const std::string& sData, const std::string& sPassword);
std::string DecryptBlockCypherString(const std::string& sData, const std::string& sPassword);
std::string GetDogePrivateKey(std::string sBBPPubKey, JSONRPCRequest r);
std::string GetDogePubKey(std::string sBBPPubKey, JSONRPCRequest r);
double GetAltBalance(std::string sTicker, std::string sPubKey);
bool SendDOGEToAddress(std::string sDogePrivKey, std::string sToAddress, double nAmount, std::string& sError, std::string& sTXID);
void TradingLog(std::string sData);
std::map<std::string, AtomicTrade> GetOrderBookData(bool fForceRefresh, std::string sTicker);
std::string TransmitSidechainTx(JSONRPCRequest r, AtomicTrade a, std::string& sError);
std::string DoubleToStringWithLeadingZeroes(double n, int nPlaces, int nTotalPlaces);
void OneTimeShaDump();
bool CheckLegacyRandomXBlockHash(uint256 hash, int height);
bool ProcessTradingMessage(BBPTradingMessage bbptm);
bool BBPTradingMessageSeen(std::string s);
void SetBBPTradingMessageSeen(std::string s);
AtomicTrade GetAtomicTradeFromTransaction(const CTransaction& tx);
std::string AtomicCommunication(std::string Action, std::map<std::string, std::string> mapRequestHeaders);
AtomicTrade TransmitAtomicTrade(JSONRPCRequest r, AtomicTrade a, std::string sMethod, std::string sAddressBookName);
std::string YesNo(bool f); 
std::string GetTradingBBPPrivateKey(std::string sBBPPubKey, JSONRPCRequest r);
std::string CreateBankrollDenominations(JSONRPCRequest r, double nQuantity, CAmount denominationAmount, std::string& sError);
std::string GenerateAssetAddress(JSONRPCRequest r);
std::string SearchForAsset(JSONRPCRequest r, std::string sAssetSuffix, std::string sAddressLabel, std::string& sPrivKey, int nMaxIterations);
std::vector<std::pair<std::string, AtomicTrade>> GetSortedOrderBook(std::string sAction, std::string sStatus, std::string sTicker);
std::string GetDisplayAgeInDays(int nRefTime);
std::string GetColoredAssetShortCode(std::string sTicker);
bool IsColoredCoin0(std::string sDestination);
double GetAssetBalance(JSONRPCRequest r, std::string sShortCode);
std::string GetFlags(AtomicTrade a, std::string sMyAddress, std::map<std::string, AtomicTrade> mapAT);
std::string GetDefaultReceiveAddress(std::string sName);
std::string IsInAddressBook(JSONRPCRequest r, std::string sNamedEntry);
bool ValidateAssetTransaction(const CTransaction& tx, const CCoinsViewCache& view);
bool RPCSendAsset(JSONRPCRequest r, std::string& sError, std::string& sTXID, std::string sAddress, CAmount nValue, std::string sLongAssetCode);
CAmount GetWalletBalanceForSpecificAddress(std::string sAddress);
double AmountToDouble(const CAmount& amount);
std::string GetTCPContent(std::string sFQDN, std::string sAction, int nPort, int nTimeoutSecs);
BBPResult GetAddressFromTransaction(std::string sTXID, int nVOUT);
bool ExportMultiWalletKeys();
AtomicTrade WrapCoin(AtomicSymbol oSymbol, double nQuantity);
AtomicTrade UnwrapCoin(AtomicSymbol oSymbol, double nAmount);
CWallet* GetInternalWallet(JSONRPCRequest r);
double GetAssetBalanceNoWallet(std::string sShortCode);
std::string GetTradingRoomIcon(std::string sIcon);
AtomicSymbol GetAtomicSymbol(std::string sSymbol);
std::string GetAltPublicKey(std::string sSymbol);
std::string EncryptRSAData(std::string sText);
std::string stringToHexString(const std::string& input);
bool IsWalletAvailable();
std::string GetAltPubKey(std::string sTicker, std::string sAltPrivKey);



/** Used to store a reference to the global node */
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
