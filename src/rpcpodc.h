// Copyright (c) 2014-2017 The Däsh Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RPCPODC_H
#define RPCPODC_H

#include "hash.h"
#include "net.h"
#include "utilstrencodings.h"

#include <univalue.h>

std::string strReplace(std::string& str, const std::string& oldStr, const std::string& newStr);
double GetCryptoPrice(std::string sURL);
bool SignStake(std::string sBitcoinAddress, std::string strMessage, std::string& sError, std::string& sSignature);
std::string GetGithubVersion();
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

#endif
