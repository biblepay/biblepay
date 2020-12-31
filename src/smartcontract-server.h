// Copyright (c) 2014-2019 The Dash Core Developers, The DAC Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SMARTCONTRACTSERVER_H
#define SMARTCONTRACTSERVER_H

#include "wallet/wallet.h"
#include "hash.h"
#include "net.h"
#include "utilstrencodings.h"
#include "rpcpog.h"
#include <univalue.h>

class CWallet;

std::string AssessBlocks(int nHeight, bool fCreating);
int GetLastGSCSuperblockHeight(int nCurrentHeight, int& nNextSuperblock);
std::string GetGSCContract(int nHeight, bool fCreating);
bool SubmitGSCTrigger(std::string sHex, std::string& gobjecthash, std::string& sError);
void GetGSCGovObjByHeight(int nHeight, uint256 uOptFilter, int& out_nVotes, uint256& out_uGovObjHash, std::string& out_PaymentAddresses, std::string& out_PaymentAmounts, std::string& out_QT);
uint256 GetPAMHashByContract(std::string sContract);
uint256 GetPAMHash(std::string sAddresses, std::string sAmounts, std::string sQTPhase);
bool VoteForGSCContract(int nHeight, std::string sMyContract, std::string& sError);
std::string ExecuteGenericSmartContractQuorumProcess();
UniValue GetProminenceLevels(int nHeight, std::string sFilterName);
bool NickNameExists(std::string sProjectName, std::string sNickName, bool& fIsMine);
int GetRequiredQuorumLevel(int nHeight);
void GetTransactionPoints(CBlockIndex* pindex, CTransactionRef tx, double& nCoinAge, CAmount& nDonation);
bool ChainSynced(CBlockIndex* pindex);
std::string WatchmanOnTheWall(bool fForce, std::string& sContract);
void GetGovObjDataByPamHash(int nHeight, uint256 hPamHash, std::string& out_Data);
DACProposal GetProposalByHash(uint256 govObj, int nLastSuperblock);
std::string DescribeProposal(DACProposal dacProposal);
std::string GetTxCPK(CTransactionRef tx, std::string& sCampaignName);
double CalculatePoints(std::string sCampaign, std::string sDiary, double nCoinAge, CAmount nDonation, std::string sCPK);
double GetProminenceCap(std::string sCampaignName, double nPoints, double nProminence);
std::string GetCPIDByCPK(std::string sCPK);
std::string GetCPIDElementByData(std::string sData, int iElement);
double GetRequiredCoinAgeForPODC(double nRAC, double nTeamID);
double GetCoinPrice();
bool VerifyChild(std::string childID, std::string sCharity);
bool IsOverBudget(int nHeight, int64_t nTime, std::string sAmounts);
double CalculateAPM(int nHeight);
double ExtractAPM(int nHeight);
std::string CheckGSCHealth();
std::string ExtractBlockMessage(int nHeight);
bool DoesContractExist(int nHeight, uint256 uGovID);
double GetDACDonationTotals(int nHeight, int nTime);
std::string SerializeSanctuaryQuorumTrigger(int iContractAssessmentHeight, int nEventBlockHeight, int64_t nTime, std::string sContract);
std::map<std::string, double> DACEngine();

#endif
