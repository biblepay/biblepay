// Copyright (c) 2014-2019 The Dash Core Developers, The DAC Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RPCPOG_H
#define RPCPOG_H

#include "wallet/wallet.h"
#include "chat.h"
#include "email.h"
#include "hash.h"
#include "net.h"
#include "utilstrencodings.h"
#include "validation.h"
#include <univalue.h>

class CWallet;


std::string RetrieveMd5(std::string s1);

struct UserVote
{
	int nTotalYesCount = 0;
	int nTotalNoCount = 0;
	int nTotalAbstainCount = 0;
	int nTotalYesWeight = 0;
	int nTotalNoWeight = 0;
	int nTotalAbstainWeight = 0;
};

struct CPK
{
  std::string sAddress;
  int64_t nLockTime = 0;
  std::string sCampaign;
  std::string sNickName;
  std::string sEmail;
  std::string sVendorType;
  std::string sError;
  std::string sChildId;
  std::string sOptData;
  std::string cpid;
  double nProminence = 0;
  double nPoints = 0;
  bool fValid = false;
};

struct PriceQuote
{
	double nForeignPrice = 0;
	double nBTCPrice = 0;
	double nBBPPrice = 0;
	double nUSDBBP = 0;
	double nUSDForeign = 0;
	double nBBPValueUSD = 0;
	double nForeignValueUSD = 0;
	CAmount nBBPQty = 0;
	CAmount nForeignQty = 0;
};

struct UserRecord
{
	std::string CPK;
	std::string NickName;
	std::string ExternalEmail;
	std::string InternalEmail;
	std::string Longitude;
	std::string Latitude;
	std::string URL;
	std::string RSAPublicKey;
	// Only available to the biblepaycore user:
	std::string RSAPrivateKey;
	bool AuthorizePayments = false;
	bool Found = false;
};

struct RSAKey
{
	std::string PrivateKey;
	std::string PublicKey;
	std::string Error;
	bool Valid;
};

struct IPFSTransaction
{
	std::string File;
	std::string TXID;
	CAmount nFee = 0;
	CAmount nSize = 0;
	std::string FileName;
	std::string URL;
	double nDuration = 0;
	int nDensity = 0;
	std::string ErrorCode;
	std::string Response;
	std::string BlockHash;
	std::string Network;
	std::string CPK;
	int nHeight = 0;
	std::map<std::string, std::string> mapRegions;
};

struct DashUTXO
{
	std::string TXID = std::string();
	CAmount Amount = 0;
	std::string Address = std::string();
	std::string Network = std::string();
	bool Spent = false;
	bool Found = false;
};

struct DACResult
{
	std::string Response;
	bool fError = false;
	CAmount nFee = 0;
	int nSize = 0;
	std::string TXID;
	std::string ErrorCode;
	std::string PrimaryKey;
	int64_t nTime = 0;
	std::map<std::string, IPFSTransaction> mapResponses;
	std::map<std::string, std::string> mapRegions;
};

struct QueuedProposal
{
	bool Submitted = false;
	std::string Hex;
	uint64_t StartTime = 0;
	int PrepareHeight = 0;
	std::string Error;
	uint256 TXID = uint256S("0x0");
	uint256 GovObj = uint256S("0x0");
	int SubmissionCount = 0;
};

struct Researcher
{
	std::string nickname;
	int teamid = 0;
	std::string country;
	int64_t creationtime = 0;
	double totalcredit = 0;
	double wcgpoints = 0;
	double rac = 0;
	int id = 0;
	std::string cpid;
	bool found = false;
	bool unbanked = false;
	double CoinAge = 0;
	std::string CPK;
};

struct CoinAgeVotingDataStruct
{
	std::map<int, std::map<std::string, int>> mapsVoteCount;
	std::map<int, std::map<std::string, double>> mapsVoteAge;
	std::map<int, int> mapTotalVotes;
	std::map<int, double> mapTotalCoinAge;
};

struct CoinVin
{
	COutPoint OutPoint;
	uint256 HashBlock = uint256S("0x0");
	int64_t BlockTime = 0;
	double CoinAge = 0;
	CAmount Amount = 0;
	std::string Destination = std::string();
	bool Found = false;
	CTransactionRef TxRef;
};

struct WhaleStake
{
	double Amount = 0;
	double RewardAmount = 0;
	double TotalOwed = 0;
	int64_t BurnTime = 0;
	int BurnHeight = 0;
	int Duration = 0;
	double DWU = 0;
	double ActualDWU = 0;
	int64_t MaturityTime = 0;
	int MaturityHeight = 0;
	std::string CPK = std::string();
	uint256 TXID = uint256S("0x0");
	std::string XML = std::string();
	std::string ReturnAddress = std::string();
	bool found = false;
	bool paid = false;
};

struct Orphan
{
	std::string OrphanID;
	std::string Charity;
	std::string Name;
	std::string URL;
	double MonthlyAmount;
};

struct Expense
{
	int64_t nTime = 0;
	std::string ExpenseID = std::string();
	std::string Added = std::string();
	std::string Charity = std::string();
	double nBBPAmount = 0;
	double nUSDAmount = 0;
};

struct SimpleUTXO
{
	std::string TXID;
	int nOrdinal = 0;
	CAmount nAmount = 0;
	double ValueUSD = 0;
};

struct UTXOStake
{
	std::string XML = std::string();
	CAmount nBBPAmount = 0;
	CAmount nForeignAmount = 0;
	int64_t Time = 0;
	int Height = 0;
	int nType = 0;
	std::string CPK = std::string();
	std::string SignatureNarr = std::string();
	bool found = false;
	std::string ForeignTicker = std::string();
	std::string ReportTicker = std::string();
	std::string BBPUTXO = std::string();
	std::string ForeignUTXO = std::string();
	std::string BBPAddress = std::string();
	std::string ForeignAddress = std::string();
	std::string BBPSignature = std::string();
	std::string ForeignSignature = std::string();
	double nBBPPrice = 0;
	double nForeignPrice = 0;
	double nBTCPrice = 0;
	double nBBPValueUSD = 0;
	double nForeignValueUSD = 0;
	double nValue = 0;
	bool BBPSignatureValid = false;
	bool ForeignSignatureValid = false;
	bool SignatureValid = false;
	uint256 TXID = uint256S("0x0");
};

struct DashStake
{
	std::string XML = std::string();
	CAmount nBBPAmount = 0;
	CAmount nDashAmount = 0;
	double MonthlyEarnings = 0;
	int64_t Time = 0;
	int64_t MaturityTime = 0;
	int MaturityHeight = 0;
	int Height = 0;
	int Duration = 0;
	std::string CPK = std::string();
	std::string ReturnAddress = std::string();
	bool found = false;
	bool expired = false;
	bool spent = false;
	double DWU = 0;
	double ActualDWU = 0;
	std::string BBPUTXO = std::string();
	std::string DashUTXO = std::string();
	std::string BBPAddress = std::string();
	std::string DashAddress = std::string();
	std::string BBPSignature = std::string();
	std::string DashSignature = std::string();
	double nBBPPrice = 0;
	double nDashPrice = 0;
	double nBTCPrice = 0;
	double nBBPValueUSD = 0;
	double nDashValueUSD = 0;
	double nBBPQty = 0;
	bool BBPSignatureValid = false;
	bool DashSignatureValid = false;
	bool SignatureValid = false;
	uint256 TXID = uint256S("0x0");
};

static double MAX_DAILY_WHALE_COMMITMENTS = 10000000;
static double MAX_WHALE_DWU = 2.0;
static double MAX_DASH_DWU = 1.0;
static double MAX_DAILY_DASH_STAKE_COMMITMENTS = 5000000;
static double MAX_DAILY_DAC_DONATIONS = 40000000;

struct WhaleMetric
{
	double nTotalFutureCommitments = 0;
	double nTotalGrossFutureCommitments = 0;
	
	double nTotalCommitmentsDueToday = 0;
	double nTotalGrossCommitmentsDueToday = 0;

	double nTotalBurnsToday = 0;
	double nTotalGrossBurnsToday = 0;

	double nTotalMonthlyCommitments = 0;
	double nTotalGrossMonthlyCommitments = 0;

	double nTotalAnnualReward = 0;

	double nSaturationPercentAnnual = 0;
	double nSaturationPercentMonthly = 0;
	double DWU = 0;
};

struct DACProposal
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

/** Comparison function for sorting the getchaintips heads.  */
struct CompareBlocksByHeight
{
    bool operator()(const CBlockIndex* a, const CBlockIndex* b) const
    {
        /* Make sure that unequal blocks with the same height do not compare
           equal. Use the pointers themselves to make a distinction. */

        if (a->nHeight != b->nHeight)
          return (a->nHeight > b->nHeight);

        return a < b;
    }
};

std::string RoundToString(double d, int place);
std::string QueryBibleHashVerses(uint256 hash, uint64_t nBlockTime, uint64_t nPrevBlockTime, int nPrevHeight, CBlockIndex* pindexPrev);
CAmount GetDailyMinerEmissions(int nHeight);
std::string CreateBankrollDenominations(double nQuantity, CAmount denominationAmount, std::string& sError);
std::string DefaultRecAddress(std::string sType);
std::string GenerateNewAddress(std::string& sError, std::string sName);
std::string GetActiveProposals();
bool VoteManyForGobject(std::string govobj, std::string strVoteSignal, std::string strVoteOutcome, 
	int iVotingLimit, int& nSuccessful, int& nFailed, std::string& sError);
std::string CreateGovernanceCollateral(uint256 GovObjHash, CAmount caFee, std::string& sError);
int GetNextSuperblock();
bool is_email_valid(const std::string& e);
double GetSporkDouble(std::string sName, double nDefault);
int64_t GETFILESIZE(std::string sPath);
std::string ReadCache(std::string sSection, std::string sKey);
std::string ReadCacheWithMaxAge(std::string sSection, std::string sKey, int64_t nSeconds);
void ClearCache(std::string sSection);
void WriteCache(std::string sSection, std::string sKey, std::string sValue, int64_t locktime, bool IgnoreCase=true);
std::string GetSporkValue(std::string sKey);
std::string TimestampToHRDate(double dtm);
std::string GetArrayElement(std::string s, std::string delim, int iPos);
void GetMiningParams(int nPrevHeight, bool& f7000, bool& f8000, bool& f9000, bool& fTitheBlocksActive);
bool findStringCaseInsensitive(const std::string & strHaystack, const std::string & strNeedle);
bool SubmitProposalToNetwork(uint256 txidFee, int64_t nStartTime, std::string sHex, std::string& sError, std::string& out_sGovObj);
UniValue GetDataList(std::string sType, int iMaxAgeInDays, int& iSpecificEntry, std::string sSearch, std::string& outEntry);
double GetDifficulty(const CBlockIndex* blockindex);
std::string PubKeyToAddress(const CScript& scriptPubKey);
int DeserializePrayersFromFile();
double Round(double d, int place);
void SerializePrayersToFile(int nHeight);
std::string AmountToString(const CAmount& amount);
CBlockIndex* FindBlockByHeight(int nHeight);
std::string rPad(std::string data, int minWidth);
double cdbl(std::string s, int place);
std::string AmountToString(const CAmount& amount);
std::string ExtractXML(std::string XMLdata, std::string key, std::string key_end);
bool Contains(std::string data, std::string instring);
bool CheckNonce(bool f9000, unsigned int nNonce, int nPrevHeight, int64_t nPrevBlockTime, int64_t nBlockTime, const Consensus::Params& params);
bool RPCSendMoney(std::string& sError, const CTxDestination &address, CAmount nValue, bool fSubtractFeeFromAmount, CWalletTx& wtxNew, bool fUseInstantSend=false, std::string sOptionalData = "", double nCoinAge = 0);
bool FundWithExternalPurse(std::string& sError, const CTxDestination &address, CAmount nValue, bool fSubtractFeeFromAmount, CWalletTx& wtxNew, bool fUseInstantSend, CAmount nExactAmount, std::string sOptionalData, double dMinCoinAge, std::string sPursePubKey);
std::vector<char> ReadBytesAll(char const* filename);
CAmount StringToAmount(std::string sValue);
bool CompareMask(CAmount nValue, CAmount nMask);
bool POOSOrphanTest(std::string sSanctuaryPubKey, int64_t nTimeout);
std::string GetElement(std::string sIn, std::string sDelimiter, int iPos);
bool CopyFile(std::string sSrc, std::string sDest);
std::string Caption(std::string sDefault, int iMaxLen);
std::vector<std::string> Split(std::string s, std::string delim);
void MemorizeBlockChainPrayers(bool fDuringConnectBlock, bool fSubThread, bool fColdBoot, bool fDuringSanctuaryQuorum);
double GetBlockVersion(std::string sXML);
bool CheckStakeSignature(std::string sBitcoinAddress, std::string sSignature, std::string strMessage, std::string& strError);
std::string Uplink(bool bPost, std::string sPayload, std::string sBaseURL, std::string sPage, int iPort, int iTimeoutSecs, int iBOE, 
	std::map<std::string, std::string> mapRequestHeaders = std::map<std::string, std::string>(), std::string TargetFileName = "", bool fJson = false);
std::string FormatHTML(std::string sInput, int iInsertCount, std::string sStringToInsert);
std::string GJE(std::string sKey, std::string sValue, bool bIncludeDelimiter, bool bQuoteValue);
bool InstantiateOneClickMiningEntries();
bool WriteKey(std::string sKey, std::string sValue);
std::string GetTransactionMessage(CTransactionRef tx);
std::map<std::string, CPK> GetChildMap(std::string sGSCObjType);
bool AdvertiseChristianPublicKeypair(std::string sProjectId, std::string sNickName, std::string sEmail, std::string sVendorType, bool fUnJoin, bool fForce, CAmount nFee, std::string sOptData, std::string &sError);
double GetAntiBotNetWeight(int64_t nBlockTime, CTransactionRef tx, bool fDebug, std::string sSolver);
std::map<std::string, std::string> GetSporkMap(std::string sPrimaryKey, std::string sSecondaryKey);
std::map<std::string, CPK> GetGSCMap(std::string sGSCObjType, std::string sSearch, bool fRequireSig);
void WriteCacheDouble(std::string sKey, double dValue);
double ReadCacheDouble(std::string sKey);
bool CheckAntiBotNetSignature(CTransactionRef tx, std::string sType, std::string sSolver);
double GetVINCoinAge(int64_t nBlockTime, CTransactionRef tx, bool fDebug);
CAmount GetTitheAmount(CTransactionRef ctx);
CPK GetCPK(std::string sData);
std::string GetCPKData(std::string sProjectId, std::string sPK);
CAmount GetRPCBalance();
void GetGovSuperblockHeights(int& nNextSuperblock, int& nLastSuperblock);
int GetHeightByEpochTime(int64_t nEpoch);
std::string GetPOGBusinessObjectList(std::string sType, std::string sFields);
std::string SignMessageEvo(std::string strAddress, std::string strMessage, std::string& sError);
const CBlockIndex* GetBlockIndexByTransactionHash(const uint256 &hash);
double AddVector(std::string sData, std::string sDelim);
int ReassessAllChains();
double GetFees(CTransactionRef tx);
int64_t GetCacheEntryAge(std::string sSection, std::string sKey);
std::vector<std::string> GetVectorOfFilesInDirectory(const std::string &dirPath, const std::vector<std::string> dirSkipList);
std::string GetAttachmentData(std::string sPath, bool fEncrypted);
std::string Path_Combine(std::string sPath, std::string sFileName);
void ProcessBLSCommand(CTransactionRef tx);
DACResult GetDecentralizedURL();
std::string BIPFS_Payment(CAmount nAmount, std::string sTXID, std::string sXML);
DACResult DSQL_ReadOnlyQuery(std::string sXMLSource);
DACResult DSQL_ReadOnlyQuery(std::string sEndpoint, std::string sXML);
int LoadResearchers();
std::string TeamToName(int iTeamID);
std::string GetResearcherCPID(std::string sSearch);
bool CreateExternalPurse(std::string& sError);
bool VerifyMemoryPoolCPID(CTransaction tx);
std::string GetEPArg(bool fPublic);
std::vector<WhaleStake> GetDWS(bool fIncludeMemoryPool);
WhaleMetric GetWhaleMetrics(int nHeight, bool fIncludeMemoryPool);
bool VerifyDynamicWhaleStake(CTransactionRef tx, std::string& sError);
double GetDWUBasedOnMaturity(double nDuration, double dDWU);
double GetOwedBasedOnMaturity(double nDuration, double dDWU, double dAmount);
std::vector<WhaleStake> GetPayableWhaleStakes(int nHeight, double& nOwed);
CoinVin GetCoinVIN(COutPoint o, int64_t nTxTime);
bool GetTxDAC(uint256 txid, CTransactionRef& tx1);
double GetWhaleStakesInMemoryPool(std::string sCPK);
std::string GetCPKByCPID(std::string sCPID);
int GetNextPODCTransmissionHeight(int height);
int GetWhaleStakeSuperblockHeight(int nHeight);
std::string SearchChain(int nBlocks, std::string sDest);
std::string GetResDataBySearch(std::string sSearch);
int GetWCGIdByCPID(std::string sSearch);
uint256 ComputeRandomXTarget(uint256 hash, int64_t nPrevBlockTime, int64_t nBlockTime);
std::string ReverseHex(std::string const & src);
uint256 GetRandomXHash(std::string sHeaderHex, uint256 key, uint256 hashPrevBlock, int iThreadID);
uint256 GetRandomXHash2(std::string sHeaderHex, uint256 key, uint256 hashPrevBlock, int iThreadID);
std::string GenerateFaucetCode();
void WriteBinaryToFile(char const* filename, std::vector<char> data);
std::tuple<std::string, std::string, std::string> GetOrphanPOOSURL(std::string sSanctuaryPubKey);
bool ApproveSanctuaryRevivalTransaction(CTransaction tx);
bool VoteWithCoinAge(std::string sGobjectID, std::string sOutcome, std::string& TXID_OUT, std::string& ERROR_OUT);
double GetCoinAge(std::string txid);
CoinAgeVotingDataStruct GetCoinAgeVotingData(std::string sGobjectID);
std::string GetAPMNarrative();
std::string SplitFile(std::string sPath);
DACResult SubmitIPFSPart(int iPort, std::string sWebPath, std::string sTXID, std::string sBaseURL, std::string sPage, std::string sOriginalName, std::string sFileName, int iPartNumber, int iTotalParts, int iDensity, int iDuration, bool fEncrypted, CAmount nFee);
DACResult DownloadFile(std::string sBaseURL, std::string sPage, int iPort, int iTimeoutSecs, std::string sTargetFileName, bool fEncrypted);
DACResult BIPFS_UploadFile(std::string sLocalPath, std::string sWebPath, std::string sTXID, int iTargetDensity, int nDurationDays, bool fDryRun, bool fEncrypted);
DACResult BIPFS_UploadFolder(std::string sDirPath, std::string sWebPath, std::string sTXID, int iTargetDensity, int nDurationDays, bool fDryRun, bool fEncrypted);
bool SendDWS(std::string& sTXID, std::string& sError, std::string sReturnAddress, std::string sCPK, double nAmt, double nDuration, bool fDryRun);
std::string GetHowey(bool fRPC, bool fBurn);
bool EncryptFile(std::string sPath, std::string sTargetPath);
bool DecryptFile(std::string sPath, std::string sTargetPath);
std::string FormatURL(std::string URL, int iPart);
void SyncSideChain(int nHeight);
std::string GetUTXO(std::string sHash, int nOrdinal, CAmount& nValue, std::string& sError);
WhaleMetric GetDashStakeMetrics(int nHeight, bool fIncludeMemoryPool);
std::vector<DashStake> GetDashStakes(bool fIncludeMemoryPool);
bool SendDashStake(std::string sReturnAddress, std::string& sTXID, std::string& sError, std::string sBBPUTXO, std::string sDashUTXO, std::string sBBPSig, std::string sDashSig, double nDuration, std::string sCPK, bool fDryRun, DashStake& out_ds);
bool VerifyDashStakeSignature(std::string sAddress, std::string sUTXO, std::string sSig, int nKeyType);
void ProcessInnerUTXOData(std::string sInnerData);
std::string SignBBPUTXO(std::string sUTXO, std::string& sError);
void ProcessDashUTXOData();
bool IsDuplicateUTXO(std::string UTXO);
bool IsDuplicateUTXO(std::vector<UTXOStake>& utxoStakes, std::string UTXO);
std::vector<DashStake> GetPayableDashStakes(int nHeight, double& nOwed);
void LockUTXOStakes();
DashStake GetDashStakeByUTXO(std::string sDashStake);
void SendChat(CChat chat);
UserRecord GetUserRecord(std::string sSourceCPK);
RSAKey GetMyRSAKey();
RSAKey GetTestRSAKey();
std::string Mid(std::string data, int nStart, int nLength);
std::string GetSANDirectory4();
void WriteUnsignedBytesToFile(char const* filename, std::vector<unsigned char> outchar);
bool PayEmailFees(CEmail email);
void SendEmail(CEmail email);
bool VerifyNonstandardSignature(std::string sAddress, std::string sTargMsg, std::string sSig, int nKeyType);
double GetDACDonationsByRange(int nStartHeight, int nRange);
UserRecord GetMyUserRecord();
bool VerifyDACDonation(CTransactionRef tx, std::string& sError);
bool WriteDataToFile(std::string sPath, std::string data);
std::vector<char> ReadAllBytesFromFile(char const* filename);
SimpleUTXO QueryUTXO(int64_t nTargetLockTime, double nTargetAmount, std::string sTicker, std::string sAddress, std::string sUTXO, int xnOut, std::string& sError, bool fReturnFirst = false);
bool SendUTXOStake(double nTargetAmount, std::string sForeignTicker, std::string& sTXID, std::string& sError, std::string sBBPAddress, std::string sBBPUTXO, std::string sForeignAddress, std::string sForeignUTXO, 
	std::string sBBPSig, std::string sForeignSig, std::string sCPK, bool fDryRun, UTXOStake& out_utxostake);
std::vector<UTXOStake> GetUTXOStakes(bool fIncludeMemoryPool);
int AssimilateUTXO(UTXOStake d);
UTXOStake GetUTXOStakeByUTXO(std::string sUTXOStake);
int GetUTXOStatus(uint256 txid);
std::string GetUTXOSummary(std::string sCPK);
std::string ScanBlockForNewUTXO(const CBlock& block);
double GetVINAge2(int64_t nBlockTime, CTransactionRef tx, CAmount nMinAmount, bool fDebug);
double CalculateUTXOReward(int nStakeCount);
std::string strReplace(std::string& str, const std::string& oldStr, const std::string& newStr);
double AddressToPin(std::string sAddress);
bool CompareMask2(CAmount nAmount, double nMask);
std::vector<DACResult> GetDataListVector(std::string sType, int nDaysLimit);
PriceQuote GetPriceQuote(std::string sForeignSymbol, CAmount nBBPQty, CAmount nForeignQty);
int64_t HRDateToTimestamp(std::string sDate);
void AppendStorageFile(std::string sDataStoreName, std::string sData);
bool HashExistsInDataFile(std::string sDataStoreName, std::string sHash);
std::string GetPopUpVerses(std::string sRange);
bool SignStake(std::string sBitcoinAddress, std::string strMessage, std::string& sError, std::string& sSignature);
double GetCryptoPrice(std::string sURL);
bool VerifySigner(std::string sXML);
double GetPBase(double& out_BTC, double& out_BBP);
std::string GetCPID();
bool GetTransactionTimeAndAmount(uint256 txhash, int nVout, int64_t& nTime, CAmount& nAmount);
std::string SendBlockchainMessage(std::string sType, std::string sPrimaryKey, std::string sValue, double dStorageFee, int nSign, std::string sExtraPayload, std::string& sError);
std::string ToYesNo(bool bValue);
bool VoteForGobject(uint256 govobj, std::string sVoteOutcome, std::string& sError);
int64_t GetDCCFileAge();
std::string GetSANDirectory2();
int GetWCGMemberID(std::string sMemberName, std::string sAuthCode, double& nPoints);
Researcher GetResearcherByID(int nID);
std::map<std::string, Researcher> GetPayableResearchers();
CAmount ARM64();
std::string AmtToString(CAmount nAmount);

#endif
