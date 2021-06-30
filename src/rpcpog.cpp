// Copyright (c) 2014-2019 The Dash Core Developers, The DAC Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include "bbpsocket.h"
#include "chat.h"
#include "messagesigner.h"

#include "evo/deterministicmns.h"
#include "evo/specialtx.h"
#include "kjv.h"

#include "net.h" // for CService
#include "netaddress.h"
#include "netbase.h" // for LookupHost
#include "randomx_bbp.h"

#include <policy/policy.h>

#include "rpc/server.h"
#include "rpcpog.h"
#include "spork.h"

#include "smartcontract-client.h"
#include "smartcontract-server.h"
#include "util.h"
#include "utilmoneystr.h"

#include "wallet/wallet.h"
#include <fstream>
#include <stdint.h>
#include <univalue.h>

#include "txmempool.h"
#include <governance/governance-vote.h>
#include <masternode/masternode-sync.h>
#include <openssl/bio.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/md5.h>
#include <openssl/ssl.h>
// For HTTPS (for the pool communication)

#include <math.h> /* round, floor, ceil, trunc */
#include <sstream>

#include <boost/algorithm/string.hpp>           // for trim()
#include <boost/algorithm/string/case_conv.hpp> // for to_lower()
#include <boost/algorithm/string/replace.hpp>
#include <boost/asio.hpp>
#include <boost/date_time/posix_time/posix_time.hpp> // for StringToUnixTime()
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/thread.hpp>

UniValue VoteWithMasternodes(const std::map<uint256, CKey>& keys,
    const uint256& hash,
    vote_signal_enum_t eVoteSignal,
    vote_outcome_enum_t eVoteOutcome);


std::string GJE(std::string sKey, std::string sValue, bool bIncludeDelimiter, bool bQuoteValue)
{
    // This is a helper for the Governance gobject create method
    std::string sQ = "\"";
    std::string sOut = sQ + sKey + sQ + ":";
    if (bQuoteValue) {
        sOut += sQ + sValue + sQ;
    } else {
        sOut += sValue;
    }
    if (bIncludeDelimiter) sOut += ",";
    return sOut;
}

std::string RoundToString(double d, int place)
{
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(place) << d;
    return ss.str();
}

double Round(double d, int place)
{
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(place) << d;
    double r = 0;
    try {
        r = boost::lexical_cast<double>(ss.str());
        return r;
    } catch (boost::bad_lexical_cast const& e) {
        LogPrintf("caught bad lexical cast %f", 1);
        return 0;
    } catch (...) {
        LogPrintf("caught bad lexical cast %f", 2);
    }
    return r;
}

std::vector<std::string> Split(std::string s, std::string delim)
{
    size_t pos = 0;
    std::string token;
    std::vector<std::string> elems;
    while ((pos = s.find(delim)) != std::string::npos) {
        token = s.substr(0, pos);
        elems.push_back(token);
        s.erase(0, pos + delim.length());
    }
    elems.push_back(s);
    return elems;
}

double cdbl(std::string s, int place)
{
    if (s == "") s = "0";
    if (s.length() > 255) return 0;
    s = strReplace(s, "\r", "");
    s = strReplace(s, "\n", "");
    std::string t = "";
    for (int i = 0; i < (int)s.length(); i++) {
        std::string u = s.substr(i, 1);
        if (u == "0" || u == "1" || u == "2" || u == "3" || u == "4" || u == "5" || u == "6" || u == "7" || u == "8" || u == "9" || u == "." || u == "-") {
            t += u;
        }
    }
    double r = 0;
    try {
        r = boost::lexical_cast<double>(t);
    } catch (boost::bad_lexical_cast const& e) {
        LogPrintf("caught cdbl bad lexical cast %f from %s with %f", 1, s, (double)place);
        return 0;
    } catch (...) {
        LogPrintf("caught cdbl bad lexical cast %f", 2);
    }
    double d = Round(r, place);
    return d;
}

bool Contains(std::string data, std::string instring)
{
    std::size_t found = 0;
    found = data.find(instring);
    if (found != std::string::npos) return true;
    return false;
}

std::string GetElement(std::string sIn, std::string sDelimiter, int iPos)
{
    if (sIn.empty())
        return std::string();
    std::vector<std::string> vInput = Split(sIn.c_str(), sDelimiter);
    if (iPos < (int)vInput.size()) {
        return vInput[iPos];
    }
    return std::string();
}

std::string GetSporkValue(std::string sKey)
{
    boost::to_upper(sKey);
    std::string expandedKey = "SPORK[-]" + sKey;
    std::pair<std::string, int64_t> v = mvApplicationCache[expandedKey];
    return v.first;
}

double GetSporkDouble(std::string sName, double nDefault)
{
    double dSetting = cdbl(GetSporkValue(sName), 2);
    if (dSetting == 0)
        return nDefault;
    return dSetting;
}

std::map<std::string, std::string> GetSporkMap(std::string sPrimaryKey, std::string sSecondaryKey)
{
    boost::to_upper(sPrimaryKey);
    boost::to_upper(sSecondaryKey);
    std::string sDelimiter = "|";
    std::pair<std::string, int64_t> v = mvApplicationCache[sPrimaryKey + "[-]" + sSecondaryKey];
    std::vector<std::string> vSporks = Split(v.first, sDelimiter);
    std::map<std::string, std::string> mSporkMap;
    for (int i = 0; i < vSporks.size(); i++) {
        std::string sMySpork = vSporks[i];
        if (!sMySpork.empty())
            mSporkMap.insert(std::make_pair(sMySpork, RoundToString(i, 0)));
    }
    return mSporkMap;
}

std::string Left(std::string sSource, int bytes)
{
    // I learned this in 1978 when I learned BASIC... LOL
    if (sSource.length() >= bytes) {
        return sSource.substr(0, bytes);
    }
    return std::string();
}


CPK GetCPK(std::string sData)
{
    // CPK DATA FORMAT: sCPK + "|" + Sanitized NickName + "|" + LockTime + "|" + SecurityHash + "|" + CPK Signature + "|" + Email + "|" + VendorType + "|" + OptData
    CPK k;
    std::vector<std::string> vDec = Split(sData.c_str(), "|");
    if (vDec.size() < 5) return k;
    std::string sSecurityHash = vDec[3];
    std::string sSig = vDec[4];
    std::string sCPK = vDec[0];
    if (sCPK.empty()) return k;
    if (vDec.size() >= 6)
        k.sEmail = vDec[5];
    if (vDec.size() >= 7)
        k.sVendorType = vDec[6];
    if (vDec.size() >= 8)
        k.sOptData = vDec[7];
    CWallet* const pwallet = GetWalletForGenericRequest();

    k.fValid = pwallet->CheckStakeSignature(sCPK, sSig, sSecurityHash, k.sError);
    if (!k.fValid) {
        LogPrintf("GetCPK::Error Sig %s, SH %s, Err %s, CPK %s, NickName %s ", sSig, sSecurityHash, k.sError, sCPK, vDec[1]);
        return k;
    }

    k.sAddress = sCPK;
    k.sNickName = vDec[1];
    k.nLockTime = (int64_t)cdbl(vDec[2], 0);

    return k;
}

std::map<std::string, CPK> GetGSCMap(std::string sGSCObjType, std::string sSearch, bool fRequireSig)
{
    std::map<std::string, CPK> mCPKMap;
    boost::to_upper(sGSCObjType);
    for (auto ii : mvApplicationCache) {
        if (Contains(ii.first, sGSCObjType)) {
            CPK k = GetCPK(ii.second.first);
            if (!k.sAddress.empty() && k.fValid) {
                if ((!sSearch.empty() && (sSearch == k.sAddress || sSearch == k.sNickName)) || sSearch.empty()) {
                    mCPKMap.insert(std::make_pair(k.sAddress, k));
                }
            }
        }
    }
    return mCPKMap;
}

std::string ReadCache(std::string sSection, std::string sKey)
{
    std::string sLookupSection = sSection;
    std::string sLookupKey = sKey;
    boost::to_upper(sLookupSection);
    boost::to_upper(sLookupKey);
    // NON-CRITICAL TODO : Find a way to eliminate this to_upper while we transition to non-financial transactions
    if (sLookupSection.empty() || sLookupKey.empty())
        return std::string();
    std::string sFullKey = sLookupSection + "[-]" + sLookupKey;
    int nCt = mvApplicationCache.count(sFullKey);
    if (nCt == 0)
        return std::string();
    std::pair<std::string, int64_t> t = mvApplicationCache[sFullKey];
    return t.first;
}

std::string ReadCacheWithMaxAge(std::string sSection, std::string sKey, int64_t nSeconds)
{
    std::string sLookupSection = sSection;
    std::string sLookupKey = sKey;
    boost::to_upper(sLookupSection);
    boost::to_upper(sLookupKey);
    int64_t nAge = GetCacheEntryAge(sLookupSection, sLookupKey);

    if (nAge > nSeconds) {
        // Invalidate the cache
        return std::string();
    }
    if (sLookupSection.empty() || sLookupKey.empty())
        return std::string();
    std::pair<std::string, int64_t> t = mvApplicationCache[sLookupSection + "[-]" + sLookupKey];
    return t.first;
}

std::string TimestampToHRDate(double dtm)
{
    if (dtm == 0) return "1-1-1970 00:00:00";
    if (dtm > 9888888888) return "1-1-2199 00:00:00";
    std::string sDt = DateTimeStrFormat("%m-%d-%Y %H:%M:%S", dtm);
    return sDt;
}

int64_t HRDateToTimestamp(std::string sDate)
{
    time_t tStart;
    int yy, month, dd, hh, mm, ss;
    struct tm whenStart;
    const char* zStart = sDate.c_str();
    sscanf(zStart, "%d-%d-%dT%d:%d:%dZ", &yy, &month, &dd, &hh, &mm, &ss);
    whenStart.tm_year = yy - 1900;
    whenStart.tm_mon = month - 1;
    whenStart.tm_mday = dd;
    whenStart.tm_hour = hh;
    whenStart.tm_min = mm;
    whenStart.tm_sec = ss;
    whenStart.tm_isdst = -1;

    tStart = mktime(&whenStart);
    return tStart;
}

std::string ExtractXML(std::string XMLdata, std::string key, std::string key_end)
{
    std::string extraction = "";
    std::string::size_type loc = XMLdata.find(key, 0);
    if (loc != std::string::npos) {
        std::string::size_type loc_end = XMLdata.find(key_end, loc + 3);
        if (loc_end != std::string::npos) {
            extraction = XMLdata.substr(loc + (key.length()), loc_end - loc - (key.length()));
        }
    }
    return extraction;
}

std::string AmountToString(const CAmount& amount)
{
    bool sign = amount < 0;
    int64_t n_abs = (sign ? -amount : amount);
    int64_t quotient = n_abs / COIN;
    int64_t remainder = n_abs % COIN;
    std::string sAmount = strprintf("%s%d.%08d", sign ? "-" : "", quotient, remainder);
    return sAmount;
}

double AmountToDouble(const CAmount& amount)
{
    double nAmount = (double)amount / COIN;
    return nAmount;
}

static CBlockIndex* pblockindexFBBHLast;
CBlockIndex* FindBlockByHeight(int nHeight)
{
    CBlockIndex* pblockindex;
    if (nHeight < 0 || nHeight > chainActive.Tip()->nHeight) return NULL;

    if (nHeight < chainActive.Tip()->nHeight / 2)
        pblockindex = mapBlockIndex[chainActive.Genesis()->GetBlockHash()];
    else
        pblockindex = chainActive.Tip();
    if (pblockindexFBBHLast && abs(nHeight - pblockindex->nHeight) > abs(nHeight - pblockindexFBBHLast->nHeight))
        pblockindex = pblockindexFBBHLast;
    while (pblockindex->nHeight > nHeight)
        pblockindex = pblockindex->pprev;
    while (pblockindex->nHeight < nHeight)
        pblockindex = chainActive.Next(pblockindex);
    pblockindexFBBHLast = pblockindex;
    return pblockindex;
}

std::string DefaultRecAddress(std::string sType)
{
    CWallet* const pwallet = GetWalletForGenericRequest();
    if (!pwallet) {
        LogPrintf("\nDefaultRecAddress::ERROR %f No pwallet", 03142021);
        return "";
    }
    std::string sDefaultRecAddress;
    for (auto item : pwallet->mapAddressBook) {
        CTxDestination txd1 = item.first;

        std::string sAddr = EncodeDestination(txd1);

        std::string strName = item.second.name;
        bool fMine = IsMine(*pwallet, item.first);
        if (fMine) {
            boost::to_upper(strName);
            boost::to_upper(sType);
            if (strName == sType) {
                sDefaultRecAddress = sAddr;
                return sDefaultRecAddress;
            }
            sDefaultRecAddress = sAddr;
        }
    }

    if (!sType.empty()) {
        std::string sError;
        sDefaultRecAddress = pwallet->GenerateNewAddress(sError, sType);
        if (sError.empty()) return sDefaultRecAddress;
    }
    LogPrintf("nDefRecAddress %s", sDefaultRecAddress);
    return sDefaultRecAddress;
}

std::string CreateBankrollDenominations(double nQuantity, CAmount denominationAmount, std::string& sError)
{
    // First mark the denominations with the 1milli TitheMarker
    denominationAmount += (.001 * COIN);
    CAmount nBankrollMask = .001 * COIN;

    CAmount nTotal = denominationAmount * nQuantity;
    CWallet* const pwallet = GetWalletForGenericRequest();
    CAmount curBalance = pwallet->GetBalance();
    if (curBalance < nTotal) {
        sError = "Insufficient funds (Unlock Wallet).";
        return std::string();
    }
    std::string sTitheAddress = DefaultRecAddress("Christian-Public-Key");
    CWalletTx wtx;

    CScript scriptPubKey = GetScriptForDestination(DecodeDestination(sTitheAddress));
    CReserveKey reservekey(pwallet);
    CAmount nFeeRequired;
    std::vector<CRecipient> vecSend;
    int nChangePosRet = -1;
    bool fSubtractFeeFromAmount = false;

    for (int i = 0; i < nQuantity; i++) {
        CRecipient recipient = {scriptPubKey, denominationAmount, false, fSubtractFeeFromAmount};
        vecSend.push_back(recipient);
    }
    // Add 10 small amounts to keep payments available for utxo stakes
    for (int i = 0; i < 10; i++) {
        CRecipient recipient = {scriptPubKey, 10 * COIN, false, fSubtractFeeFromAmount};
        vecSend.push_back(recipient);
    }

    bool fUseInstantSend = false;
    double minCoinAge = 0;
    std::string sOptData;
    CCoinControl coinControl;

    if (!pwallet->CreateTransaction(vecSend, wtx, reservekey, nFeeRequired, nChangePosRet, sError, coinControl, true, 0, sOptData)) {
        if (!sError.empty()) {
            return std::string();
        }

        if (nTotal + nFeeRequired > pwallet->GetBalance()) {
            sError = strprintf("Error: This transaction requires a transaction fee of at least %s because of its amount, complexity, or use of recently received funds!", FormatMoney(nFeeRequired));
            return std::string();
        }
    }
    CValidationState state;
    if (!pwallet->CommitTransaction(wtx, reservekey, g_connman.get(), state)) {
        sError = "Error: The transaction was rejected! This might happen if some of the coins in your wallet were already spent, such as if you used a copy of wallet.dat and coins were spent in the copy but not marked as spent here.";
        return std::string();
    }

    std::string sTxId = wtx.GetHash().GetHex();
    return sTxId;
}

std::string RetrieveMd5(std::string s1)
{
    try {
        const char* chIn = s1.c_str();
        unsigned char digest2[16];
        MD5((unsigned char*)chIn, strlen(chIn), (unsigned char*)&digest2);
        char mdString2[33];
        for (int i = 0; i < 16; i++)
            sprintf(&mdString2[i * 2], "%02x", (unsigned int)digest2[i]);
        std::string xmd5(mdString2);
        return xmd5;
    } catch (std::exception& e) {
        return std::string();
    }
}

std::string PubKeyToAddress(const CScript& scriptPubKey)
{
    CTxDestination address1;
    if (!ExtractDestination(scriptPubKey, address1)) {
        return "";
    }
    std::string sOut = EncodeDestination(address1);
    return sOut;
}

CAmount GetRPCBalance()
{
    CWallet* const pwallet = GetWalletForGenericRequest();
    return pwallet->GetBalance();
}


bool RPCSendMoney(std::string& sError, const CTxDestination& address, CAmount nValue, bool fSubtractFeeFromAmount, CWalletTx& wtxNew, bool fUseInstantSend, std::string sOptionalData, double nCoinAge)
{
    CWallet* const pwallet = GetWalletForGenericRequest();
    CAmount curBalance = pwallet->GetBalance();

    // Check amount
    if (nValue <= 0) {
        sError = "Invalid amount";
        return false;
    }

    if (pwallet->IsLocked()) {
        sError = "Wallet unlock required";
        return false;
    }

    if (nValue > curBalance) {
        sError = "Insufficient funds";
        return false;
    }
    // Parse address
    CScript scriptPubKey = GetScriptForDestination(address);

    // Create and send the transaction
    CReserveKey reservekey(pwallet);
    CAmount nFeeRequired;
    std::string strError;
    std::vector<CRecipient> vecSend;
    int nChangePosRet = -1;
    bool fForce = false;
    // BiblePay - Handle extremely large data transactions:
    if (sOptionalData.length() > 10000 && nValue > 0) {
        double nReq = ceil(sOptionalData.length() / 10000);
        double n1 = (double)nValue / COIN;
        double n2 = n1 / nReq;
        for (int n3 = 0; n3 < nReq; n3++) {
            CAmount indAmt = n2 * COIN;
            CRecipient recipient = {scriptPubKey, indAmt, fForce, fSubtractFeeFromAmount};
            vecSend.push_back(recipient);
        }
    } else {
        CRecipient recipient = {scriptPubKey, nValue, fForce, fSubtractFeeFromAmount};
        vecSend.push_back(recipient);
    }

    int nMinConfirms = 0;
    CCoinControl coinControl;
    int nChangePos = -1;
    if (!pwallet->CreateTransaction(vecSend, wtxNew, reservekey, nFeeRequired, nChangePos, strError, coinControl, true, 0, sOptionalData)) {
        if (!fSubtractFeeFromAmount && nValue + nFeeRequired > pwallet->GetBalance()) {
            sError = strprintf("Error: This transaction requires a transaction fee of at least %s because of its amount, complexity, or use of recently received funds!", FormatMoney(nFeeRequired));
            return false;
        }
        sError = "Unable to Create Transaction: " + strError;
        return false;
    }
    CValidationState state;

    if (!pwallet->CommitTransaction(wtxNew, reservekey, g_connman.get(), state)) {
        sError = "Error: The transaction was rejected! This might happen if some of the coins in your wallet were already spent, such as if you used a copy of wallet.dat and coins were spent in the copy but not marked as spent here.";
        return false;
    }
    return true;
}

int GetHeightByEpochTime(int64_t nEpoch)
{
    if (!chainActive.Tip()) return 0;
    int nLast = chainActive.Tip()->nHeight;
    if (nLast < 1) return 0;
    for (int nHeight = nLast; nHeight > 0; nHeight--) {
        CBlockIndex* pindex = FindBlockByHeight(nHeight);
        if (pindex) {
            int64_t nTime = pindex->GetBlockTime();
            if (nEpoch > nTime) return nHeight;
        }
    }
    return -1;
}

void GetGovSuperblockHeights(int& nNextSuperblock, int& nLastSuperblock)
{
    int nBlockHeight = 0;
    {
        LOCK(cs_main);
        nBlockHeight = (int)chainActive.Height();
    }
    int nSuperblockStartBlock = Params().GetConsensus().nSuperblockStartBlock;
    int nSuperblockCycle = Params().GetConsensus().nSuperblockCycle;
    int nFirstSuperblockOffset = (nSuperblockCycle - nSuperblockStartBlock % nSuperblockCycle) % nSuperblockCycle;
    int nFirstSuperblock = nSuperblockStartBlock + nFirstSuperblockOffset;
    if (nBlockHeight < nFirstSuperblock) {
        nLastSuperblock = 0;
        nNextSuperblock = nFirstSuperblock;
    } else {
        nLastSuperblock = nBlockHeight - nBlockHeight % nSuperblockCycle;
        nNextSuperblock = nLastSuperblock + nSuperblockCycle;
    }
}

std::string GetActiveProposals()
{
    int nStartTime = GetAdjustedTime() - (86400 * 32);
    LOCK2(cs_main, governance.cs);
    std::vector<const CGovernanceObject*> objs = governance.GetAllNewerThan(nStartTime);
    std::string sXML;
    int id = 0;
    std::string sDelim = "~";
    std::string sZero = "\0";
    int nLastSuperblock = 0;
    int nNextSuperblock = 0;
    GetGovSuperblockHeights(nNextSuperblock, nLastSuperblock);
    for (const CGovernanceObject* pGovObj : objs) {
        if (pGovObj->GetObjectType() != GOVERNANCE_OBJECT_PROPOSAL) continue;
        int64_t nEpoch = 0;
        int64_t nStartEpoch = 0;
        CGovernanceObject* myGov = governance.FindGovernanceObject(pGovObj->GetHash());
        UniValue obj = myGov->GetJSONObject();
        std::string sURL;
        std::string sCharityType;
        nStartEpoch = cdbl(obj["start_epoch"].getValStr(), 0);
        nEpoch = cdbl(obj["end_epoch"].getValStr(), 0);
        sURL = obj["url"].getValStr();
        sCharityType = obj["expensetype"].getValStr();
        if (sCharityType.empty()) sCharityType = "N/A";
        DACProposal dProposal = GetProposalByHash(pGovObj->GetHash(), nLastSuperblock);
        std::string sHash = pGovObj->GetHash().GetHex();
        int nEpochHeight = GetHeightByEpochTime(nStartEpoch);
        // First ensure the proposals gov height has not passed yet
        bool bIsPaid = nEpochHeight < nLastSuperblock;
        std::string sReport = DescribeProposal(dProposal);
        if (!bIsPaid) {
            int iYes = pGovObj->GetYesCount(VOTE_SIGNAL_FUNDING);
            int iNo = pGovObj->GetNoCount(VOTE_SIGNAL_FUNDING);
            int iAbstain = pGovObj->GetAbstainCount(VOTE_SIGNAL_FUNDING);
            id++;
            if (sCharityType.empty()) sCharityType = "N/A";
            std::string sProposalTime = TimestampToHRDate(nStartEpoch);
            if (id == 1) sURL += "&t=" + RoundToString(GetAdjustedTime(), 0);
            std::string sName;
            sName = obj["name"].getValStr();
            double dCharityAmount = 0;
            dCharityAmount = cdbl(obj["payment_amount"].getValStr(), 2);
            std::string sRow = "<proposal>" + sHash + sDelim + sName + sDelim + RoundToString(dCharityAmount, 2) + sDelim + sCharityType + sDelim + sProposalTime + sDelim + RoundToString(iYes, 0) + sDelim + RoundToString(iNo, 0) + sDelim + RoundToString(iAbstain, 0);

            // Add the coin-age voting data
            CoinAgeVotingDataStruct c = GetCoinAgeVotingData(pGovObj->GetHash().ToString());
            sRow += sDelim + RoundToString(c.mapTotalVotes[0], 0) + sDelim + RoundToString(c.mapTotalVotes[1], 0) + sDelim + RoundToString(c.mapTotalVotes[2], 0);
            // Add the coin-age voting data totals
            sRow += sDelim + RoundToString(c.mapTotalCoinAge[0], 0) + sDelim + RoundToString(c.mapTotalCoinAge[1], 0) + sDelim + RoundToString(c.mapTotalCoinAge[2], 0);
            sRow += sDelim + sURL;
            sXML += sRow;
        }
    }
    return sXML;
}

bool VoteManyForGobject(std::string govobj, std::string strVoteSignal, std::string strVoteOutcome, int iVotingLimit, int& nSuccessful, int& nFailed, std::string& sError)
{
    uint256 hash(uint256S(govobj));
    vote_signal_enum_t eVoteSignal = CGovernanceVoting::ConvertVoteSignal(strVoteSignal);
    if (eVoteSignal == VOTE_SIGNAL_NONE) {
        sError = "Invalid vote signal (funding).";
        return false;
    }
    vote_outcome_enum_t eVoteOutcome = CGovernanceVoting::ConvertVoteOutcome(strVoteOutcome);
    if (eVoteOutcome == VOTE_OUTCOME_NONE) {
        sError = "Invalid vote outcome (yes/no/abstain)";
        return false;
    }

    CWallet* const pwallet = GetWalletForGenericRequest();
    if (!pwallet) {
        sError = "Voting is not supported when wallet is disabled.";
        return false;
    }

    std::map<uint256, CKey> votingKeys;

    auto mnList = deterministicMNManager->GetListAtChainTip();
    mnList.ForEachMN(true, [&](const CDeterministicMNCPtr& dmn) {
        CKey votingKey;
        if (pwallet->GetKey(dmn->pdmnState->keyIDVoting, votingKey)) {
            votingKeys.emplace(dmn->proTxHash, votingKey);
        }
    });
    UniValue vOutcome;

    try {
        vOutcome = VoteWithMasternodes(votingKeys, hash, eVoteSignal, eVoteOutcome);
    } catch (std::runtime_error& e) {
        sError = e.what();
        return false;
    } catch (...) {
        sError = "Voting failed.";
        return false;
    }

    nSuccessful = cdbl(vOutcome["success_count"].getValStr(), 0);
    bool fResult = nSuccessful > 0 ? true : false;
    return fResult;
}

std::string CreateGovernanceCollateral(uint256 GovObjHash, CAmount caFee, std::string& sError)
{
    CWalletTx wtx;
    CWallet* const pwallet = GetWalletForGenericRequest();

    if (!pwallet->GetBudgetSystemCollateralTX(wtx, GovObjHash, caFee)) {
        sError = "Error creating collateral transaction for governance object.  Please check your wallet balance and make sure your wallet is unlocked.";
        return std::string();
    }
    if (sError.empty()) {
        // -- make our change address
        CReserveKey reservekey(pwallet);
        CValidationState state;
        pwallet->CommitTransaction(wtx, reservekey, g_connman.get(), state);
        DBG(cout << "gobject: prepare "
                 << " strData = " << govobj.GetDataAsString()
                 << ", hash = " << govobj.GetHash().GetHex()
                 << ", txidFee = " << wtx.GetHash().GetHex()
                 << endl;);
        return wtx.GetHash().ToString();
    }
    return std::string();
}

int GetNextSuperblock()
{
    int nLastSuperblock, nNextSuperblock;
    // Get current block height
    int nBlockHeight = 0;
    {
        LOCK(cs_main);
        nBlockHeight = (int)chainActive.Height();
    }

    // Get chain parameters
    int nSuperblockStartBlock = Params().GetConsensus().nSuperblockStartBlock;
    int nSuperblockCycle = Params().GetConsensus().nSuperblockCycle;

    // Get first superblock
    int nFirstSuperblockOffset = (nSuperblockCycle - nSuperblockStartBlock % nSuperblockCycle) % nSuperblockCycle;
    int nFirstSuperblock = nSuperblockStartBlock + nFirstSuperblockOffset;

    if (nBlockHeight < nFirstSuperblock) {
        nLastSuperblock = 0;
        nNextSuperblock = nFirstSuperblock;
    } else {
        nLastSuperblock = nBlockHeight - nBlockHeight % nSuperblockCycle;
        nNextSuperblock = nLastSuperblock + nSuperblockCycle;
    }
    return nNextSuperblock;
}


UniValue GetJsonObjectFromURL(std::string sURL1, std::string sURL2, bool& fInvalid)
{
    UniValue o(UniValue::VOBJ);
    DACResult b;
    std::map<std::string, std::string> mapRequestHeaders;
    b.Response = Uplink(false, "", sURL1, sURL2, 443, 30, 4, mapRequestHeaders, "", true);
    std::size_t i1 = b.Response.find("{\"");
    if (i1 < 1) {
        fInvalid = true;
        return o;
    }
    std::string sData = Mid(b.Response, i1 - 2, 1000000);
    fInvalid = Contains(b.Response, "sending too many requests") || Contains(b.Response, "blacklisted due to exceeding usage");
    o.read(sData);
    return o;
}

std::string BlockChairTickerToName(std::string sTicker)
{
    if (sTicker == "DOGE") {
        return "dogecoin";
    } else if (sTicker == "BTC") {
        return "bitcoin";
    } else if (sTicker == "DASH") {
        return "dash";
    } else if (sTicker == "LTC") {
        return "litecoin";
    } else if (sTicker == "ETH") {
        return "ethereum";
    } else if (sTicker == "ZEC") {
        return "zcash";
    } else if (sTicker == "BCH") {
        return "bitcoin-cash";
    } else if (sTicker == "XRP") {
        return "ripple";
    } else if (sTicker == "XLM") {
        return "stellar";
    }

    return "";
}

void LogIt(SimpleUTXO u)
{
    LogPrintf("\nSimpleUTXO::TXID %s, Amount %f, Ticker %s, Address %s, AssetType %s, Total Balance %f",
        u.TXID, AmountToString(u.nAmount), u.Ticker, u.Address, u.AssetType, AmountToString(u.nTotalBalance));
}


std::vector<SimpleUTXO> QueryUTXOList(std::string sTicker, std::string sAddress, int64_t nTimestamp, int nConfiguration)
{
    double nConfig = GetSporkDouble("utxoqueryresilience", 0);
    if (nConfiguration > 0)
        nConfig = nConfiguration;

    std::vector<SimpleUTXO> l;
    if (nConfig == 0) {
        l = QueryUTXOListB(sTicker, sAddress, nTimestamp);
        int nTrace = 0;
        if (l.size() > 0) {
            nTrace = l[0].Trace;
            if (nTrace == 0) {
                l = QueryUTXOListA(sTicker, sAddress, nTimestamp);
                return l;
            }
        }
    } else {
        l = QueryUTXOListA(sTicker, sAddress, nTimestamp);
    }
    return l;
}


std::vector<SimpleUTXO> QueryUTXOListB(std::string sTicker, std::string sAddress, int64_t nTimestamp)
{
    std::vector<SimpleUTXO> l;
    boost::to_upper(sTicker);

    if (sTicker == "BBP") {
        l = GetAddressUTXOs_BBP(sAddress);
        return l;
    }

    std::string sURL = "https://" + GetSporkValue("bms");
    std::string sR = "Server?action=QUERY_UTXOS";
    std::string sXML = "<ticker>" + sTicker + "</ticker><address>" + sAddress + "</address><timestamp>" + RoundToString(nTimestamp, 0) + "</timestamp>";
    std::string sResponse = Uplink(false, sXML, sURL, sR, SSL_PORT, 25, 1);
    std::vector<std::string> vL = Split(sResponse.c_str(), "<record>");
    for (int i = 0; i < vL.size(); i++) {
        SimpleUTXO u;
        u.nAmount = StringToAmount(ExtractXML(vL[i], "<amount>", "</amount>"));
        u.TXID = ExtractXML(vL[i], "<txid>", "</txid>");
        u.nOrdinal = cdbl(ExtractXML(vL[i], "<ordinal>", "</ordinal>"), 0);
        u.Height = cdbl(ExtractXML(vL[i], "<height>", "</height>"), 0);
        u.Address = ExtractXML(vL[i], "<address>", "</address>");
        u.Ticker = ExtractXML(vL[i], "<ticker>", "</ticker>");
        u.Trace = cdbl(ExtractXML(vL[i], "<trace>", "</trace>"), 0);
        if (u.nAmount > 0) {
            l.push_back(u);
        }
    }
    return l;
}

std::vector<SimpleUTXO> QueryUTXOListA(std::string sTicker, std::string sAddress, int64_t nTimestamp)
{
    boost::to_upper(sTicker);
    bool fInvalid = false;
    std::string sURL1 = "https://api.blockchair.com";
    std::string sURL2;
    std::vector<SimpleUTXO> l;
    UniValue o(UniValue::VOBJ);

    if (sTicker == "XLM") {
        sURL2 = "stellar/raw/account/" + sAddress + "?transactions=true&operations=true";
        o = GetJsonObjectFromURL(sURL1, sURL2, fInvalid);
        UniValue oBalances = o["data"][sAddress]["account"]["balances"];
        UniValue oOps = o["data"][sAddress]["operations"];
        CAmount nBalance = 0;
        for (int i = 0; i < oBalances.size(); i++) {
            if (oBalances["asset_type"].getValStr() == "native") {
                nBalance = cdbl(oBalances[i]["balance"].getValStr(), 8) * COIN;
            }
        }
        for (int i = 0; i < oOps.size(); i++) {
            SimpleUTXO u;
            u.nTotalBalance = nBalance;
            u.Ticker = sTicker;
            u.Address = sAddress;
            std::string sTxType = oOps[i]["type"].getValStr();
            std::string sType = oOps[i]["asset_type"].getValStr();
            if (sType == "native") {
                u.nAmount = cdbl(oOps[i]["amount"].getValStr(), 8) * COIN;
                u.TXID = GetSHA256Hash(u.Address + AmountToString(u.nAmount)).GetHex();
                if (AmountToDouble(u.nAmount) > 0) {
                    l.push_back(u);
                    LogIt(u);
                }
            }
        }
    } else if (sTicker == "XRP") {
        sURL2 = "ripple/raw/account/" + sAddress + "?transactions=true";
        o = GetJsonObjectFromURL(sURL1, sURL2, fInvalid);
        UniValue oBalances = o["data"][sAddress]["account"]["account_data"];
        UniValue oTx = o["data"][sAddress]["transactions"]["transactions"];
        CAmount nBalance = cdbl(oBalances["Balance"].getValStr(), 8) / 1000000 * COIN;
        int nTxCount = 0;
        for (int i = 0; i < oTx.size(); i++) {
            nTxCount++;
            SimpleUTXO u;
            u.Ticker = sTicker;
            u.Address = sAddress;
            u.nAmount = cdbl(oTx[i]["tx"]["Amount"].getValStr(), 8) / 100000000 * COIN;
            u.TXID = GetSHA256Hash(u.Address + AmountToString(u.nAmount)).GetHex();
            u.nTotalBalance = nBalance;
            if (AmountToDouble(u.nAmount) > 0) {
                l.push_back(u);
                LogIt(u);
            }
        }
    } else if (sTicker == "DOGE" || sTicker == "BTC" || sTicker == "DASH" || sTicker == "LTC" || sTicker == "BCH" || sTicker == "ZEC") {
        std::string sTickerName = BlockChairTickerToName(sTicker);
        sURL2 = sTickerName + "/dashboards/address/" + sAddress;
        o = GetJsonObjectFromURL(sURL1, sURL2, fInvalid);
        UniValue oBalances = o["data"][sAddress]["utxo"];
        for (int i = 0; i < oBalances.size(); i++) {
            SimpleUTXO u;
            u.Ticker = sTicker;
            u.Address = sAddress;
            u.nAmount = cdbl(oBalances[i]["value"].getValStr(), 8) / 100000000 * COIN;
            u.TXID = oBalances[i]["transaction_hash"].getValStr();
            u.nOrdinal = cdbl(oBalances[i]["index"].getValStr(), 0);
            u.Height = cdbl(oBalances[i]["block_id"].getValStr(), 0);
            if (AmountToDouble(u.nAmount) > 0) {
                l.push_back(u);
                LogIt(u);
            }
        }
    } else if (sTicker == "ETH") {
        boost::to_lower(sAddress);
        sURL2 = "ethereum/dashboards/address/" + sAddress + "?transactions=true";
        o = GetJsonObjectFromURL(sURL1, sURL2, fInvalid);
        UniValue oBalance = o["data"][sAddress]["address"];
        CAmount nBalance = cdbl(oBalance["balance"].getValStr(), 8) / 100000000 / 10000000000 * COIN;
        UniValue oCalls = o["data"][sAddress]["calls"];

        for (int i = 0; i < oCalls.size(); i++) {
            SimpleUTXO u;
            u.Ticker = sTicker;
            u.Address = sAddress;
            u.nAmount = cdbl(oBalance["balance"].getValStr(), 8) / 100000000 / 10000000000 * COIN;
            u.TXID = GetSHA256Hash(u.Address + AmountToString(u.nAmount)).GetHex();
            u.nTotalBalance = nBalance;
            if (AmountToDouble(u.nAmount) > 0) {
                l.push_back(u);
                LogIt(u);
            }
        }
    }
    return l;
}


bool is_email_valid(const std::string& e)
{
    return (Contains(e, "@") && Contains(e, ".") && e.length() > MINIMUM_EMAIL_LENGTH) ? true : false;
}

int64_t GETFILESIZE(std::string sPath)
{
    // Due to Windows taking up "getfilesize" we changed this to uppercase.
    if (!boost::filesystem::exists(sPath))
        return 0;
    if (!boost::filesystem::is_regular_file(sPath))
        return 0;
    return (int64_t)boost::filesystem::file_size(sPath);
}

bool CheckNonce(bool f9000, unsigned int nNonce, int nPrevHeight, int64_t nPrevBlockTime, int64_t nBlockTime, const Consensus::Params& params)
{
    if (!f9000 || (nPrevHeight > params.EVOLUTION_CUTOVER_HEIGHT && nPrevHeight <= params.ANTI_GPU_HEIGHT))
        return true;

    if (nPrevHeight >= params.RANDOMX_HEIGHT)
        return true;

    int64_t MAX_AGE = 30 * 60;
    int NONCE_FACTOR = 256;
    int MAX_NONCE = 512;
    int64_t nElapsed = nBlockTime - nPrevBlockTime;
    if (nElapsed > MAX_AGE)
        return true;
    int64_t nMaxNonce = nElapsed * NONCE_FACTOR;
    if (nMaxNonce < MAX_NONCE) nMaxNonce = MAX_NONCE;
    return (nNonce > nMaxNonce) ? false : true;
}

static CCriticalSection csClearWait;
void ClearCache(std::string sSection)
{
    LOCK(csClearWait);
    boost::to_upper(sSection);
    for (auto ii : mvApplicationCache) {
        if (Contains(ii.first, sSection + "[-]")) {
            mvApplicationCache[ii.first] = std::make_pair(std::string(), 0);
        }
    }
}

static CCriticalSection csWriteWait;
void WriteCache(std::string sSection, std::string sKey, std::string sValue, int64_t locktime, bool IgnoreCase)
{
    LOCK(csWriteWait);
    if (sSection.empty() || sKey.empty()) return;
    if (IgnoreCase) {
        boost::to_upper(sSection);
        boost::to_upper(sKey);
    }
    // Record Cache Entry timestamp
    std::pair<std::string, int64_t> v1 = std::make_pair(sValue, locktime);
    mvApplicationCache[sSection + "[-]" + sKey] = v1;
}

void WriteCacheDouble(std::string sKey, double dValue)
{
    std::string sValue = RoundToString(dValue, 2);
    WriteCache(sKey, "double", sValue, GetAdjustedTime(), true);
}

double ReadCacheDouble(std::string sKey)
{
    double dVal = cdbl(ReadCache(sKey, "double"), 2);
    return dVal;
}

std::string GetArrayElement(std::string s, std::string delim, int iPos)
{
    std::vector<std::string> vGE = Split(s.c_str(), delim);
    if (iPos > (int)vGE.size())
        return std::string();
    return vGE[iPos];
}

void GetMiningParams(int nPrevHeight, bool& f7000, bool& f8000, bool& f9000, bool& fTitheBlocksActive)
{
    const Consensus::Params& consensusParams = Params().GetConsensus();
    f7000 = nPrevHeight > consensusParams.F7000_CUTOVER_HEIGHT;
    f8000 = nPrevHeight >= consensusParams.F8000_CUTOVER_HEIGHT;
    f9000 = nPrevHeight >= consensusParams.F9000_CUTOVER_HEIGHT;
    int nLastTitheBlock = consensusParams.LAST_TITHE_BLOCK;
    fTitheBlocksActive = (nPrevHeight + 1) < nLastTitheBlock;
}

void SendChat(CChat chat)
{
    int nSent = 0;
    g_connman->ForEachNode([&chat, &nSent](CNode* pnode) {
        chat.RelayTo(pnode, *g_connman);
    });

    chat.ProcessChat();
}

bool SubmitProposalToNetwork(uint256 txidFee, int64_t nStartTime, std::string sHex, std::string& sError, std::string& out_sGovObj)
{
    if (!masternodeSync.IsBlockchainSynced()) {
        sError = "Must wait for client to sync with masternode network. ";
        return false;
    }
    // ASSEMBLE NEW GOVERNANCE OBJECT FROM USER PARAMETERS
    uint256 hashParent = uint256();
    int nRevision = 1;
    CGovernanceObject govobj(hashParent, nRevision, nStartTime, txidFee, sHex);
    DBG(cout << "gobject: submit "
             << " strData = " << govobj.GetDataAsString()
             << ", hash = " << govobj.GetHash().GetHex()
             << ", txidFee = " << txidFee.GetHex()
             << endl;);

    std::string strHash = govobj.GetHash().ToString();

    bool fAlwaysCheck = true;
    if (fAlwaysCheck) {
        if (!govobj.IsValidLocally(sError, true)) {
            sError += "Object submission rejected because object is not valid.";
            LogPrintf("\n OBJECT REJECTED:\n gobject submit 0 1 %f %s %s \n", (double)nStartTime, sHex.c_str(), txidFee.GetHex().c_str());
            return false;
        }
    }
    // RELAY THIS OBJECT - Reject if rate check fails but don't update buffer

    bool fRateCheckBypassed = false;
    if (!governance.MasternodeRateCheck(govobj, true, false, fRateCheckBypassed)) {
        sError = "Object creation rate limit exceeded";
        return false;
    }
    //governance.AddSeenGovernanceObject(govobj.GetHash(), SEEN_OBJECT_IS_VALID);
    govobj.Relay(*g_connman);
    governance.AddGovernanceObject(govobj, *g_connman);
    out_sGovObj = govobj.GetHash().ToString();
    return true;
}

std::vector<char> ReadBytesAll(char const* filename)
{
    int iFileSize = GETFILESIZE(filename);
    if (iFileSize < 1) {
        std::vector<char> z(0);
        return z;
    }

    std::ifstream ifs(filename, std::ios::binary | std::ios::ate);
    std::ifstream::pos_type pos = ifs.tellg();
    std::vector<char> result(pos);
    ifs.seekg(0, std::ios::beg);
    ifs.read(&result[0], pos);
    ifs.close();
    return result;
}

void WriteBinaryToFile(char const* filename, std::vector<char> data)
{
    std::ofstream OutFile;
    OutFile.open(filename, std::ios::out | std::ios::binary);
    OutFile.write(&data[0], data.size());
    OutFile.close();
}

std::string GetFileNameFromPath(std::string sPath)
{
    sPath = strReplace(sPath, "/", "\\");
    std::vector<std::string> vRows = Split(sPath.c_str(), "\\");
    std::string sFN = "";
    for (int i = 0; i < (int)vRows.size(); i++) {
        sFN = vRows[i];
    }
    return sFN;
}

UniValue GetDataList(std::string sType, int iMaxAgeInDays, int& iSpecificEntry, std::string sSearch, std::string& outEntry)
{
    int64_t nEpoch = GetAdjustedTime() - (iMaxAgeInDays * 86400);
    if (nEpoch < 0) nEpoch = 0;
    UniValue ret(UniValue::VOBJ);
    boost::to_upper(sType);
    ret.push_back(Pair("DataList", sType));
    int iPos = 0;
    int iTotalRecords = 0;
    for (auto ii : mvApplicationCache) {
        if (Contains(ii.first, sType + "[-]")) {
            std::pair<std::string, int64_t> v = mvApplicationCache[ii.first];
            int64_t nTimestamp = v.second;
            if (nTimestamp > nEpoch || nTimestamp == 0) {
                iTotalRecords++;
                if (iPos == iSpecificEntry)
                    outEntry = v.first;
                std::string sTimestamp = TimestampToHRDate((double)nTimestamp);
                if (!sSearch.empty()) {
                    if (boost::iequals(ii.first, sSearch) || Contains(ii.first, sSearch)) {
                        ret.push_back(Pair(ii.first + " (" + sTimestamp + ")", v.first));
                    }
                } else {
                    ret.push_back(Pair(ii.first + " (" + sTimestamp + ")", v.first));
                }
                iPos++;
            }
        }
    }
    iSpecificEntry++;
    if (iSpecificEntry >= iTotalRecords)
        iSpecificEntry = 0; // Reset the iterator.
    return ret;
}

std::map<std::string, std::string> SearchForDataList(std::string sType, std::string sSearch)
{
    boost::to_upper(sType);
    std::map<std::string, std::string> mData;

    for (auto ii : mvApplicationCache) {
        std::string sKey1 = GetElement(ii.first, "[-]", 0);
        std::string sKey2 = GetElement(ii.first, "[-]", 1);

        if (sKey1 == sType) {
            std::pair<std::string, int64_t> v = mvApplicationCache[ii.first];
            int64_t nTimestamp = v.second;
            std::string sTimestamp = TimestampToHRDate((double)nTimestamp);
            if (Contains(sKey2, sSearch) || Contains(v.first, sSearch)) {
                mData[ii.first] = v.first;
            }
        }
    }
    return mData;
}


void SerializePrayersToFile(int nHeight)
{
    if (nHeight < 100) return;
    std::string sSuffix = fProd ? "_prod" : "_testnet";
    std::string sTarget = GetSANDirectory1() + "prayers3" + sSuffix;
    FILE* outFile = fopen(sTarget.c_str(), "w");
    LogPrintf("Serializing Prayers... %f ", GetAdjustedTime());
    for (auto ii : mvApplicationCache) {
        std::pair<std::string, int64_t> v = mvApplicationCache[ii.first];
        int64_t nTimestamp = v.second;
        std::string sValue = v.first;
        bool bSkip = false;
        if (Contains(ii.first, "MESSAGE[-]") && (sValue == std::string()))
            bSkip = true;
        if (!bSkip) {
            std::string sRow = RoundToString(nTimestamp, 0) + "<colprayer>" + RoundToString(nHeight, 0) + "<colprayer>" + ii.first + "<colprayer>" + sValue + "<rowprayer>";
            sRow = strReplace(sRow, "\r", "[~r]");
            sRow = strReplace(sRow, "\n", "[~n]");
            sRow += "\r\n";
            fputs(sRow.c_str(), outFile);
        }
    }
    LogPrintf("...Done Serializing Prayers... %f ", GetAdjustedTime());
    fclose(outFile);
}

int DeserializePrayersFromFile()
{
    LogPrintf("\nDeserializing prayers from file %f", GetAdjustedTime());
    std::string sSuffix = fProd ? "_prod" : "_testnet";
    std::string sSource = GetSANDirectory1() + "prayers3" + sSuffix;

    boost::filesystem::path pathIn(sSource);
    std::ifstream streamIn;
    streamIn.open(pathIn.string().c_str());
    if (!streamIn) return -1;
    int nHeight = 0;
    std::string line;
    int iRows = 0;
    while (std::getline(streamIn, line)) {
        line = strReplace(line, "[~r]", "\r");
        line = strReplace(line, "[~n]", "\n");

        std::vector<std::string> vRows = Split(line.c_str(), "<rowprayer>");
        for (int i = 0; i < (int)vRows.size(); i++) {
            std::vector<std::string> vCols = Split(vRows[i].c_str(), "<colprayer>");
            if (vCols.size() > 3) {
                int64_t nTimestamp = cdbl(vCols[0], 0);
                int cHeight = cdbl(vCols[1], 0);
                if (cHeight > nHeight) nHeight = cHeight;
                std::string sKey = vCols[2];
                std::string sValue = vCols[3];
                std::vector<std::string> vKeys = Split(sKey.c_str(), "[-]");
                if (vKeys.size() > 1) {
                    WriteCache(vKeys[0], vKeys[1], sValue, nTimestamp, true); //ignore case
                    iRows++;
                }
            }
        }
    }
    streamIn.close();
    LogPrintf(" Processed %f prayer rows - %f\n", iRows, GetAdjustedTime());
    return nHeight;
}


CAmount GetTxTotalFromAddress(CTransactionRef ctx, std::string sAddress)
{
    const Consensus::Params& consensusParams = Params().GetConsensus();
    CAmount nTotal = 0;
    for (unsigned int z = 0; z < ctx->vout.size(); z++) {
        std::string sRecip = PubKeyToAddress(ctx->vout[z].scriptPubKey);
        if (sRecip == sAddress) {
            nTotal += ctx->vout[z].nValue;
        }
    }
    return nTotal;
}

CAmount StringToAmount(std::string sValue)
{
    if (sValue.empty())
        return 0;
    CAmount amount;
    if (!ParseFixedPoint(sValue, 8, &amount))
        return 0;
    if (!MoneyRange(amount)) {
        return 0; // TODO:  Rather than crash.
    }
    return amount;
}

bool CompareMask(CAmount nValue, CAmount nMask)
{
    if (nMask == 0) return false;
    std::string sAmt = "0000000000000000000000000" + AmountToString(nValue);
    std::string sMask = AmountToString(nMask);
    std::string s1 = sAmt.substr(sAmt.length() - sMask.length() + 1, sMask.length() - 1);
    std::string s2 = sMask.substr(1, sMask.length() - 1);
    return (s1 == s2);
}

std::string AmtToString(CAmount nAmount)
{
    std::string s = strprintf("%d", nAmount);
    return s;
}

bool CompareMask2(CAmount nAmount, double nMask)
{
    if (nMask == 0)
        return false;
    std::string sMask = RoundToString(nMask, 0);
    bool fZero = false;
    if (sMask.length() == 5) {
        if (Mid(sMask, 4, 1) == "0")
            fZero = true;
        nMask = cdbl(Mid(sMask, 0, 4), 0);
    }
    std::string sFull = AmtToString(nAmount);
    std::string sSec = RoundToString(nMask, 0);
    bool fExists = Contains(sFull, sSec);
    return fExists;
}

bool CopyFile(std::string sSrc, std::string sDest)
{
    boost::filesystem::path fSrc(sSrc);
    boost::filesystem::path fDest(sDest);
    try {
#if BOOST_VERSION >= 104000
        boost::filesystem::copy_file(fSrc, fDest, boost::filesystem::copy_option::overwrite_if_exists);
#else
        boost::filesystem::copy_file(fSrc, fDest);
#endif
    } catch (const boost::filesystem::filesystem_error& e) {
        LogPrintf("CopyFile failed - %s ", e.what());
        return false;
    }
    return true;
}

std::string Caption(std::string sDefault, int iMaxLen)
{
    if (sDefault.length() > iMaxLen)
        sDefault = sDefault.substr(0, iMaxLen);
    std::string sValue = ReadCache("message", sDefault);
    return sValue.empty() ? sDefault : sValue;
}

double GetBlockVersion(std::string sXML)
{
    std::string sBlockVersion = ExtractXML(sXML, "<VER>", "</VER>");
    sBlockVersion = strReplace(sBlockVersion, ".", "");
    sBlockVersion = strReplace(sBlockVersion, "v", "");
    sBlockVersion = strReplace(sBlockVersion, "-", "");

    if (sBlockVersion.length() == 3) sBlockVersion += "0";
    double dBlockVersion = cdbl(sBlockVersion, 0);
    return dBlockVersion;
}

struct TxMessage {
    std::string sMessageType;
    std::string sMessageKey;
    std::string sMessageValue;
    std::string sSig;
    std::string sNonce;
    std::string sSporkSig;
    std::string sIPFSHash;
    std::string sBOSig;
    std::string sBOSigner;
    std::string sTimestamp;
    std::string sIPFSSize;
    std::string sTxId;
    std::string sVoteSignal;
    std::string sVoteHash;
    double nNonce;
    double dAmount;
    bool fNonceValid;
    bool fPrayersMustBeSigned;
    bool fSporkSigValid;
    bool fBOSigValid;
    bool fPassedSecurityCheck;
    int64_t nAge;
    int64_t nTime;
};

bool CheckSporkSig(TxMessage t)
{
    std::string sError = "";
    const CChainParams& chainparams = Params();
    CWallet* const pwallet = GetWalletForGenericRequest();

    bool fSigValid = pwallet->CheckStakeSignature(chainparams.GetConsensus().FoundationAddress, t.sSporkSig, t.sMessageValue + t.sNonce, sError);
    bool bValid = (fSigValid && t.fNonceValid);
    if (!bValid) {
        LogPrint(BCLog::NET, "CheckSporkSig:SigFailed - Type %s, Nonce %f, Time %f, Bad spork Sig %s on message %s on TXID %s \n", t.sMessageType.c_str(), t.nNonce, t.nTime,
            t.sSporkSig.c_str(), t.sMessageValue.c_str(), t.sTxId.c_str());
    }
    return bValid;
}

bool CheckBusinessObjectSig(TxMessage t)
{
    if (!t.sBOSig.empty() && !t.sBOSigner.empty()) {
        std::string sError = "";
        CWallet* const pwallet = GetWalletForGenericRequest();
        bool fBOSigValid = pwallet->CheckStakeSignature(t.sBOSigner, t.sBOSig, t.sMessageValue + t.sNonce, sError);
        if (!fBOSigValid) {
            LogPrint(BCLog::NET, "MemorizePrayers::BO_SignatureFailed - Type %s, Nonce %f, Time %f, Bad BO Sig %s on message %s on TXID %s \n",
                t.sMessageType.c_str(), t.nNonce, t.nTime, t.sBOSig.c_str(), t.sMessageValue.c_str(), t.sTxId.c_str());
        }
        return fBOSigValid;
    }
    return false;
}

TxMessage ProcessTxMessage(CTransactionRef tx1, std::string sMessage, int64_t nTime, int iPosition, std::string sTxId, double dAmount, double dFoundationDonation, int nHeight, CAmount nAmountBurned)
{
    TxMessage t;
    t.sMessageType = ExtractXML(sMessage, "<MT>", "</MT>");
    t.sMessageKey = ExtractXML(sMessage, "<MK>", "</MK>");
    t.sMessageValue = ExtractXML(sMessage, "<MV>", "</MV>");
    t.sSig = ExtractXML(sMessage, "<MS>", "</MS>");
    t.sNonce = ExtractXML(sMessage, "<NONCE>", "</NONCE>");
    t.nNonce = cdbl(t.sNonce, 0);
    t.sSporkSig = ExtractXML(sMessage, "<SPORKSIG>", "</SPORKSIG>");
    t.sIPFSHash = ExtractXML(sMessage, "<IPFSHASH>", "</IPFSHASH>");
    t.sBOSig = ExtractXML(sMessage, "<BOSIG>", "</BOSIG>");
    t.sBOSigner = ExtractXML(sMessage, "<BOSIGNER>", "</BOSIGNER>");
    t.sIPFSHash = ExtractXML(sMessage, "<ipfshash>", "</ipfshash>");
    t.sIPFSSize = ExtractXML(sMessage, "<ipfssize>", "</ipfssize>");
    t.sTxId = sTxId;
    t.nTime = nTime;
    t.dAmount = dAmount;
    t.fPassedSecurityCheck = false;
    boost::to_upper(t.sMessageType);
    boost::to_upper(t.sMessageKey);
    bool fGSC = CSuperblock::IsSmartContract(nHeight);
    CWallet* const pwallet = GetWalletForGenericRequest();

    t.sTimestamp = TimestampToHRDate((double)nTime + iPosition);
    t.fNonceValid = (!(t.nNonce > (nTime + (60 * 60)) || t.nNonce < (nTime - (60 * 60))));
    t.nAge = GetAdjustedTime() - nTime;
    t.fPrayersMustBeSigned = (GetSporkDouble("prayersmustbesigned", 0) == 1);

    std::string sDiary = ExtractXML(sMessage, "<diary>", "</diary>");

    if (!sDiary.empty()) {
        std::string sCPK = ExtractXML(sMessage, "<abncpk>", "</abncpk>");
        CPK oPrimary = GetCPKFromProject("cpk", sCPK);
        std::string sNickName = Caption(oPrimary.sNickName, 10);
        //bool fWL = IsCPKWL(sCPK, sNickName);
        //if (fWL)
        //{
        if (sNickName.empty()) sNickName = "NA";
        std::string sEntry = sDiary + " [" + sNickName + "]";
        WriteCache("diary", RoundToString(nTime, 0), sEntry, nTime);
        //}
    }

    std::string sGift = ExtractXML(sMessage, "<gift>", "</gift>");
    if (!sGift.empty()) {
        double nAmt = cdbl(ExtractXML(sGift, "<amount>", "</amount>"), 2);
        WriteCache("GIFT", tx1->GetHash().GetHex(), RoundToString(nAmt, 2), nTime, false);
    }

    // For Coin-Age voting
    std::string sGobjectID = ExtractXML(sMessage, "<gobject>", "</gobject>");
    std::string sGSCCampaign = ExtractXML(sMessage, "<gsccampaign>", "</gsccampaign>");
    std::string sCPK = ExtractXML(sMessage, "<abncpk>", "</abncpk>");
    if (!sGobjectID.empty() && t.sMessageType == "GSCTransmission" && sGSCCampaign == "COINAGEVOTE" && !sCPK.empty()) {
        // This user voted on a poll with coin-age:
        double nCoinAge = GetVINCoinAge(nTime, tx1, false);
        // At this point we can do two cool things to extend the sanctuary gobject vote:
        // 1: Increment the vote count by distinct voter (1 vote per distinct GobjectID-CPK), and, 2: increment the vote coin-age-tally by coin-age spent (sum(coinage(gobjectid-cpk))):
        std::string sOutcome = ExtractXML(sMessage, "<outcome>", "</outcome>");
        if (sOutcome == "YES" || sOutcome == "NO" || sOutcome == "ABSTAIN") {
            WriteCache("coinage-vote-count-" + sGobjectID, sCPK, sOutcome, GetAdjustedTime());
            // Note, if someone votes more than once, we only count it once (see above line), but, we do tally coin-age (within the duration of the poll start-end).  This means a whale who accidentally voted with 10% of the coin-age on Monday may vote with the rest of their 90% of coin age as long as the poll is not expired and the coin-age will be counted in total.  But, we will display one vote for the cpk, with the sum of the coinage spent.
            WriteCache("coinage-vote-sum-" + sOutcome + "-" + sGobjectID, sCPK + "-" + tx1->GetHash().GetHex(), RoundToString(nCoinAge, 2), GetAdjustedTime());
            // TODO - limit voting to start date and end date here
            LogPrintf("\nVoted with %f coinage outcome %s for %s from %s ", nCoinAge, sOutcome, sGobjectID, sCPK);
        }
    }


    if (t.sMessageType == "PRAYER" && (!(Contains(t.sMessageKey, "(")))) t.sMessageKey += " (" + t.sTimestamp + ")";
    // The following area allows us to be a true decentralized autonomous charity (DAC):
    if (t.sMessageType == "SPORK2" && fGSC) {
        t.fSporkSigValid = true;
        t.fPassedSecurityCheck = true;
        std::vector<std::string> vInput = Split(sMessage.c_str(), "<SPORK>");
        for (int i = 0; i < (int)vInput.size(); i++) {
            std::string sKey = ExtractXML(vInput[i], "<SPORKKEY>", "</SPORKKEY>");
            std::string sValue = ExtractXML(vInput[i], "<SPORKVAL>", "</SPORKVAL>");
            std::string sSporkType = ExtractXML(vInput[i], "<SPORKTYPE>", "</SPORKTYPE>");

            if (!sKey.empty() && !sValue.empty() && !sSporkType.empty()) {
                if (sSporkType == "XSPORK-ORPHAN" || sSporkType == "XSPORK-CHARITY" || sSporkType == "XSPORK-EXPENSE") {
                    WriteCache(sSporkType, sKey, sValue, t.nTime);
                } else if (sSporkType == "SPORK") {
                    WriteCache("spork", sKey, sValue, t.nTime);
                }
            }
        }
        t.sMessageValue = "";
    } else if ((t.sMessageType == "XSPORK-ORPHAN" || t.sMessageType == "XSPORK-CHARITY" || t.sMessageType == "XSPORK-EXPENSE") && !fGSC) {
        t.fSporkSigValid = CheckSporkSig(t);
        if (!t.fSporkSigValid) t.sMessageValue = "";
        t.fPassedSecurityCheck = t.fSporkSigValid;
    } else if (t.sMessageType == "SPORK") {
        t.fSporkSigValid = CheckSporkSig(t);
        if (!t.fSporkSigValid) t.sMessageValue = "";
        t.fPassedSecurityCheck = t.fSporkSigValid;
    } else if (t.sMessageType == "REMOVAL") {
        t.fPassedSecurityCheck = false;
    } else if (t.sMessageType == "DWS-BURN") {
        t.fPassedSecurityCheck = false;
    } else if (t.sMessageType == "UTXO-BURN") {
        t.fPassedSecurityCheck = false;
    } else if (t.sMessageType == "DASH-BURN") {
        t.fPassedSecurityCheck = false;
    } else if (t.sMessageType == "PRAYER" && t.fPrayersMustBeSigned) {
        double dMinimumUnsignedPrayerDonation = GetSporkDouble("minimumunsignedprayerdonationamount", 3000);
        // If donation is to Foundation and meets minimum amount and is not signed
        if (dFoundationDonation >= dMinimumUnsignedPrayerDonation) {
            t.fPassedSecurityCheck = true;
        } else {
            t.fSporkSigValid = CheckSporkSig(t);
            if (!t.fSporkSigValid) t.sMessageValue = "";
            t.fPassedSecurityCheck = t.fSporkSigValid;
        }
    } else if (t.sMessageType == "PRAYER" && !t.fPrayersMustBeSigned) {
        // We allow unsigned prayers, as long as abusers don't deface the system (if they do, we set the spork requiring signed prayers and we manually remove the offensive prayers using a signed update)
        t.fPassedSecurityCheck = true;
    } else if (t.sMessageType == "ATTACHMENT") {
        t.fPassedSecurityCheck = true;
    } else if (t.sMessageType == "REPENT") {
        t.fPassedSecurityCheck = true;
    } else if (t.sMessageType == "MESSAGE") {
        // these are sent by our users to each other
        t.fPassedSecurityCheck = true;
    } else if (t.sMessageType == "DCC" || t.sMessageType == "CPK" || boost::starts_with(t.sMessageType, "CPK-")) {
        t.fBOSigValid = CheckBusinessObjectSig(t);
        if (false)
            LogPrintf("\nAcceptPrayer::CPKType %s, %s, %s, bosigvalid %f ", t.sMessageType, sMessage, t.sMessageValue, t.fBOSigValid);
        t.fPassedSecurityCheck = true;
    } else if (t.sMessageType == "INVOICE") {
        std::string sMsg = ExtractXML(t.sMessageValue, "<BOMSG>", "</BOMSG>");
        std::string sError;
        t.fPassedSecurityCheck = pwallet->CheckStakeSignature(t.sBOSigner, t.sBOSig, sMsg, sError);
        t.sMessageValue += "<txid>" + tx1->GetHash().GetHex() + "</txid>";
        LogPrintf("\nAcceptPrayer::INVOICE bosigvalid %f for %s", t.fPassedSecurityCheck, t.sMessageValue);
        // Harvest what do we do to map shortcut to invoices (from mvApplicationCache) etc...
    } else if (t.sMessageType == "UTXOSTAKE3") {
        std::string sMsg = ExtractXML(t.sMessageValue, "<BOMSG>", "</BOMSG>");
        std::string sError;
        bool fPassed = pwallet->CheckStakeSignature(t.sBOSigner, t.sBOSig, sMsg, sError);
        t.sMessageValue += "<txid>" + tx1->GetHash().GetHex() + "</txid>";
        if (fPassed && nAmountBurned >= UTXO_COST * COIN) {
            t.fPassedSecurityCheck = true;
            LogPrintf("\nAcceptPrayer::UTXOSTAKE3 amountburned %f , bosigvalid %f for %s ",
                (double)nAmountBurned / COIN, t.fPassedSecurityCheck, t.sMessageValue);
        }

    } else if (t.sMessageType == "DSQL") {
        std::string sMsg = ExtractXML(t.sMessageValue, "<BOMSG>", "</BOMSG>");
        std::string sError;
        t.fPassedSecurityCheck = pwallet->CheckStakeSignature(t.sBOSigner, t.sBOSig, sMsg, sError);
        t.sMessageValue += "<txid>" + tx1->GetHash().GetHex() + "</txid>";
        // LogPrintf("\nAcceptPrayer::DSQL bosigvalid %f for %s", t.fPassedSecurityCheck, t.sMessageValue);
    } else if (t.sMessageType == "PAYMENT") {
        std::string sMsg = ExtractXML(t.sMessageValue, "<BOMSG>", "</BOMSG>");
        std::string sError;
        t.fPassedSecurityCheck = pwallet->CheckStakeSignature(t.sBOSigner, t.sBOSig, sMsg, sError);
        t.sMessageValue += "<txid>" + tx1->GetHash().GetHex() + "</txid>";
        // LogPrintf("\nAcceptPrayer::PAYMENT bosigvalid %f for %s  ", 			t.fPassedSecurityCheck, t.sMessageValue);
    } else if (t.sMessageType == "REFERRALCODE" || t.sMessageType == "CLAIMREFERRALCODE") {
        std::string sMsg = ExtractXML(t.sMessageValue, "<BOMSG>", "</BOMSG>");
        std::string sError;
        t.fBOSigValid = pwallet->CheckStakeSignature(t.sBOSigner, t.sBOSig, sMsg, sError);
        t.fPassedSecurityCheck = t.fBOSigValid;
        LogPrintf("\nAP::ClaimReferralCode::%s, %s, %s, %f ", t.sMessageType, t.sBOSigner, sMsg, (int)t.fBOSigValid);
    } else if (t.sMessageType == "NFT") {
        NFT n = GetNFT(tx1);
        if (n.found) {
            std::string sMsg = ExtractXML(t.sMessageValue, "<BOMSG>", "</BOMSG>");
            std::string sAction = ExtractXML(t.sMessageValue, "<BOACTION>", "</BOACTION>");
            bool fDuplicate = IsDuplicateNFT(n);
            std::string sError;
            CWallet* const pwallet = GetWalletForGenericRequest();
            t.fBOSigValid = pwallet->CheckStakeSignature(t.sBOSigner, t.sBOSig, sMsg, sError);
            LogPrintf("\nAcceptPrayer::ProcessNFT -- CPKType %s, %s, %s, bosigvalid %f, Action=%s ", t.sMessageType, t.sMessageValue, t.sMessageValue, t.fBOSigValid, sAction);
            std::string sOldNftTxId = ExtractXML(t.sMessageValue, "<nfttxid>", "</nfttxid>");
            boost::replace_all(t.sMessageValue, sOldNftTxId, "");
            t.sMessageValue += "<nfttxid>" + tx1->GetHash().GetHex() + "</nfttxid>";
            LogPrintf("\nAcceptPrayer::OldNftTxid %s\n newtxid:%s", sOldNftTxId, t.sMessageValue);
            if (sAction == "CREATE") {
                // During create, we must never have a duplicate
                // (No need to check the signature during create or buy, but only during EDIT)
                if (!fDuplicate) {
                    t.fPassedSecurityCheck = true;
                }
            } else if (sAction == "EDIT") {
                // During edit, the NFT shows up as a 'duplicate', therefore duplicates are allowed,
                // However we require that the owner of the CPK signed this edit
                NFT oldNFT = GetSpecificNFT(n.GetHash());
                bool fOrphan = findStringCaseInsensitive(oldNFT.sType, "orphan");
                // We made it impossible to edit an orphan.  Force the user to do it all during create; then the orphan expires after s/he is sponsored for the first time.
                // This simplifies the process flow (for Reserve, Buy It now, edit security, etc.).  The orphanage can still delete an orphan who is no longer sponsorable.
                if (t.fBOSigValid) {
                    t.fPassedSecurityCheck = true;
                }

                if (fOrphan) {
                    if (n.fDeleted) {
                        // OK
                    } else {
                        LogPrintf("\nAcceptPrayer::ProcessNFT::Orphan Edit Failed - We do not allow you to make changes to an orphan.  %f", 4172021);
                        t.fPassedSecurityCheck = false;
                    }
                }
            } else if (sAction == "BUY") {
                // During buy, the NFT is a duplicate, and we do not check the signature of the BUYER,
                // However, the buy amount must be > than the buy_it_now amount (or the reserve price), and the item must be marketable
                NFT oldNFT = GetSpecificNFT(n.GetHash());
                bool fOrphan = findStringCaseInsensitive(oldNFT.sType, "orphan");
                if (oldNFT.fMarketable && oldNFT.found && oldNFT.LowestAcceptableAmount() > 0 && !oldNFT.fDeleted) {
                    CAmount nPaid = GetAmountPaidToRecipient(tx1, oldNFT.sCPK);
                    LogPrintf("\nAcceptPrayer::ProcessNFT::Buy Old NFT found min bid amount %f ", oldNFT.nMinimumBidAmount / COIN);
                    if (nPaid >= oldNFT.LowestAcceptableAmount()) {
                        // When we transfer ownership to the new CPK, we throw the Marketable switch to OFF, and the minimumBidAmount back to zero.
                        // This way the new owner must manually make the NFT marketable, and manually set the new sale amount.
                        // Otherwise a sniper could swoop in and steal this nft while it is being transferred.
                        if (n.fMarketable || n.nMinimumBidAmount > 0) {
                            t.fPassedSecurityCheck = false;
                            LogPrintf("\nProcessNFT::BUY_FAILED::ERROR minbidamount=%f,marketable=%f",
                                (double)n.nMinimumBidAmount / COIN, (int)n.fMarketable);
                        } else {
                            t.fPassedSecurityCheck = true;
                            LogPrintf("\nProcessNFT:Succeeded-BUY-%s", t.sMessageValue);
                        }
                    }
                }
            }
        }
    } else if (t.sMessageType == "EXPENSE" || t.sMessageType == "REVENUE" || t.sMessageType == "ORPHAN") {
        t.sSporkSig = t.sBOSig;
        t.fSporkSigValid = CheckSporkSig(t);
        if (!t.fSporkSigValid) {
            t.sMessageValue = "";
        }
        t.fPassedSecurityCheck = t.fSporkSigValid;
    } else if (t.sMessageType == "VOTE") {
        t.fBOSigValid = CheckBusinessObjectSig(t);
        t.fPassedSecurityCheck = t.fBOSigValid;
    } else {
        if (!t.sMessageType.empty()) {
            // We assume this is a business object
            t.fBOSigValid = CheckBusinessObjectSig(t);
            if (!t.fBOSigValid)
                t.sMessageValue = "";
            t.fPassedSecurityCheck = t.fBOSigValid;
            if (false)
                LogPrintf("\nMemorizeBlockchainPrayers::UNHANDLED type %s, key %s, valid %f", t.sMessageType, t.sMessageKey, (int)t.fPassedSecurityCheck);
        }
    }

    if (t.fPassedSecurityCheck && !t.sMessageType.empty() && !t.sMessageKey.empty() && !t.sMessageValue.empty()) {
        WriteCache(t.sMessageType, t.sMessageKey, t.sMessageValue, nTime, true);
    }

    return t;
}

bool IsCPKWL(std::string sCPK, std::string sNN)
{
    std::string sWL = GetSporkValue("cpkdiarywl");
    return (Contains(sWL, sNN));
}

void ProcessLegacyUtxoData(std::string sBlockHash, std::string sPrayer, std::string sPK, int nHeight, int nOrdinal, int iOrdinal, double dAmount)
{
    const Consensus::Params& consensusParams = Params().GetConsensus();
    if (sPK == consensusParams.BurnAddress) {
        /*
		std::string sXML = ExtractXML(sPrayer, "<dws>", "</dws>");
		if (!sXML.empty())
		{
			WriteCache("dws-burn", sBlockHash, sXML, GetAdjustedTime());
		}
		std::string sUTXOStake = ExtractXML(sPrayer, "<utxostake>", "</utxostake>");
		if (!sUTXOStake.empty())
		{
			double dCommitment = cdbl(ExtractXML(sUTXOStake, "<commitment>", "</commitment>"), 0);
			if (dCommitment > 0)
			{
					std::string sBBPAddress = ExtractXML(sUTXOStake, "<bbpaddress>", "</bbpaddress>");
					std::string sBBPSig = ExtractXML(sUTXOStake, "<bbpsig>", "</bbpsig>");
					std::string sBBPUTXO = ExtractXML(sUTXOStake, "<bbputxo>", "</bbputxo>");
					CWallet * const pwallet = GetWalletForGenericRequest();
					bool fSigValid = pwallet->CheckStakeSignature(sBBPAddress, sBBPSig, sBBPUTXO, sBBPSig);
					if (fSigValid)
					{
						sUTXOStake += "<burnutxo>" + sBlockHash + "-" + RoundToString(iOrdinal, 0) + "</burnutxo>";
						WriteCache("utxo-commitment", sBBPUTXO, sUTXOStake, GetAdjustedTime());
					}
					else
					{
						LogPrintf("\nMemorizeBlockChainPrayers stakesig failed %s %s %s", sBBPAddress, sBBPSig, sBBPUTXO);
					}
			}
			WriteCache("utxo-burn", sBlockHash, sUTXOStake, GetAdjustedTime());
		}
		*/
    } else if (sPK == consensusParams.BurnAddressOrphanDonations) {
        std::string sRow = "<height>" + RoundToString(nHeight, 0) + "</height><amount>" + RoundToString(dAmount, 2) + "</amount>";
        WriteCache("dac-donation", sBlockHash, sRow, GetAdjustedTime());
    } else if (sPK == consensusParams.BurnAddressWhaleMatches) {
        std::string sRow = "<height>" + RoundToString(nHeight, 0) + "</height><amount>" + RoundToString(dAmount, 2) + "</amount>";
        WriteCache("dac-whalematch", sBlockHash, sRow, GetAdjustedTime());
    }
}

void MemorizeBlockChainPrayers(bool fDuringConnectBlock, bool fSubThread, bool fColdBoot, bool fDuringSanctuaryQuorum)
{
    int nDeserializedHeight = 0;

    if (fColdBoot) {
        nDeserializedHeight = DeserializePrayersFromFile();
        if (chainActive.Tip()->nHeight < nDeserializedHeight && nDeserializedHeight > 0) {
            LogPrintf(" Chain Height %f, Loading entire prayer index\n", chainActive.Tip()->nHeight);
            nDeserializedHeight = 0;
        }
    }

    int nMaxDepth = chainActive.Tip()->nHeight;
    int nMinDepth = fDuringConnectBlock ? nMaxDepth - 1 : nMaxDepth - (BLOCKS_PER_DAY * 30 * 12 * 7); // Seven years
    if (fDuringSanctuaryQuorum)
        nMinDepth = nMaxDepth - (BLOCKS_PER_DAY * 14); // Two Weeks
    if (nDeserializedHeight > 0 && nDeserializedHeight < nMaxDepth)
        nMinDepth = nDeserializedHeight;
    if (nMinDepth < 0)
        nMinDepth = 0;
    CBlockIndex* pindex = FindBlockByHeight(nMinDepth);
    const Consensus::Params& consensusParams = Params().GetConsensus();
    while (pindex && pindex->nHeight < nMaxDepth) {
        if (!pindex)
            break;

        if (pindex->nHeight < chainActive.Tip()->nHeight)
            pindex = chainActive.Next(pindex);

        CBlock block;
        if (ReadBlockFromDisk(block, pindex, consensusParams)) {
            if (pindex->nHeight % 25000 == 0)
                LogPrintf(" MBCP %f @ %f, ", pindex->nHeight, GetAdjustedTime());
            for (unsigned int n = 0; n < block.vtx.size(); n++) {
                double dTotalSent = 0;
                std::string sPrayer = "";
                double dFoundationDonation = 0;
                CAmount nTotalBurned = 0;
                for (unsigned int i = 0; i < block.vtx[n]->vout.size(); i++) {
                    sPrayer += block.vtx[n]->vout[i].sTxOutMessage;
                    double dAmount = block.vtx[n]->vout[i].nValue / COIN;
                    dTotalSent += dAmount;
                    std::string sPK = PubKeyToAddress(block.vtx[n]->vout[i].scriptPubKey);
                    if (sPK == consensusParams.FoundationAddress || sPK == consensusParams.FoundationPODSAddress) {
                        dFoundationDonation += dAmount;
                    }
                    if (sPK == consensusParams.BurnAddress) {
                        nTotalBurned += block.vtx[n]->vout[i].nValue;
                    }
                    ProcessLegacyUtxoData(block.vtx[n]->GetHash().GetHex(), sPrayer, sPK, pindex->nHeight, n, i, dAmount);
                }
                double dAge = GetAdjustedTime() - block.GetBlockTime();
                ProcessTxMessage(block.vtx[n], sPrayer, block.GetBlockTime(), 0, block.vtx[n]->GetHash().GetHex(), dTotalSent, dFoundationDonation, pindex->nHeight, nTotalBurned);
            }
        }
    }
    if (fColdBoot) {
        if (nMaxDepth > (nDeserializedHeight - 1000)) {
            SerializePrayersToFile(nMaxDepth - 1);
        }
    }
}

std::string SignMessageEvo(std::string strAddress, std::string strMessage, std::string& sError)
{
    CWallet* const pwallet = GetWalletForGenericRequest();

    LOCK2(cs_main, pwallet->cs_wallet);
    if (pwallet->IsLocked()) {
        sError = "Sorry, wallet must be unlocked.";
        return std::string();
    }

    CTxDestination dest = DecodeDestination(strAddress);
    if (!IsValidDestination(dest)) {
        sError = "Invalid Sign-Message-Address.";
        return std::string();
    }

    const CKeyID* keyID = boost::get<CKeyID>(&dest);
    if (!keyID) {
        sError = "Address does not refer to key";
        return std::string();
    }
    CKey key;
    if (!pwallet->GetKey(*keyID, key)) {
        sError = "Private key not available";
        return std::string();
    }

    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << strMessage;
    std::vector<unsigned char> vchSig;
    if (!key.SignCompact(ss.GetHash(), vchSig)) {
        sError = "Sign failed";
        return std::string();
    }

    return EncodeBase64(&vchSig[0], vchSig.size());
}

std::string SignMessage(std::string sMsg, std::string sPrivateKey)
{
    CKey key;
    std::vector<unsigned char> vchMsg = std::vector<unsigned char>(sMsg.begin(), sMsg.end());
    std::vector<unsigned char> vchPrivKey = ParseHex(sPrivateKey);
    std::vector<unsigned char> vchSig;
    key.SetPrivKey(CPrivKey(vchPrivKey.begin(), vchPrivKey.end()), false);
    if (!key.Sign(Hash(vchMsg.begin(), vchMsg.end()), vchSig)) {
        return "Unable to sign message, check private key.";
    }
    const std::string sig(vchSig.begin(), vchSig.end());
    std::string SignedMessage = EncodeBase64(sig);
    return SignedMessage;
}

std::string FormatHTML(std::string sInput, int iInsertCount, std::string sStringToInsert)
{
    std::vector<std::string> vInput = Split(sInput.c_str(), " ");
    std::string sOut = "";
    int iCt = 0;
    for (int i = 0; i < (int)vInput.size(); i++) {
        sOut += vInput[i] + " ";
        iCt++;
        if (iCt >= iInsertCount) {
            iCt = 0;
            sOut += sStringToInsert;
        }
    }
    return sOut;
}

std::string GetDomainFromURL(std::string sURL)
{
    std::string sDomain;
    int HTTPS_LEN = 8;
    int HTTP_LEN = 7;
    if (sURL.find("https://") != std::string::npos) {
        sDomain = sURL.substr(HTTPS_LEN, sURL.length() - HTTPS_LEN);
    } else if (sURL.find("http://") != std::string::npos) {
        sDomain = sURL.substr(HTTP_LEN, sURL.length() - HTTP_LEN);
    } else {
        sDomain = sURL;
    }
    return sDomain;
}

bool TermPeekFound(std::string sData, int iBOEType)
{
    std::string sVerbs = "</html>|</HTML>|<EOF>|<END>|</account_out>|</am_set_info_reply>|</am_get_info_reply>|</MemberStats>";
    std::vector<std::string> verbs = Split(sVerbs, "|");
    bool bFound = false;
    for (int i = 0; i < verbs.size(); i++) {
        if (sData.find(verbs[i]) != std::string::npos)
            bFound = true;
    }
    if (iBOEType == 1) {
        if (sData.find("</user>") != std::string::npos) bFound = true;
        if (sData.find("</error>") != std::string::npos) bFound = true;
        if (sData.find("</Error>") != std::string::npos) bFound = true;
        if (sData.find("</error_msg>") != std::string::npos) bFound = true;
    } else if (iBOEType == 2) {
        if (sData.find("</results>") != std::string::npos) bFound = true;
        if (sData.find("}}") != std::string::npos) bFound = true;
    } else if (iBOEType == 3) {
        if (sData.find("}") != std::string::npos) bFound = true;
    } else if (iBOEType == 4) {
        if (sData.find("tx_url") != std::string::npos) bFound = true;
        if (sData.find("request_cost") != std::string::npos) bFound = true;
    }
    return bFound;
}

std::string PrepareHTTPPost(bool bPost, std::string sPage, std::string sHostHeader, const std::string& sMsg, const std::map<std::string, std::string>& mapRequestHeaders)
{
    std::ostringstream s;
    std::string sUserAgent = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_11_2) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/47.0.2526.80 Safari/537.36";
    std::string sMethod = bPost ? "POST" : "GET";

    s << sMethod + " /" + sPage + " HTTP/1.1\r\n"
      << "User-Agent: " + sUserAgent + "/" << FormatFullVersion() << "\r\n"
      << "Host: " + sHostHeader + ""
      << "\r\n"
      << "Content-Length: " << sMsg.size() << "\r\n";

    for (auto item : mapRequestHeaders) {
        s << item.first << ": " << item.second << "\r\n";
    }
    s << "\r\n"
      << sMsg;
    return s.str();
}

DACResult SubmitIPFSPart(int iPort, std::string sWebPath, std::string sTXID, std::string sBaseURL, std::string sPage, std::string sOriginalName, std::string sFileName, int iPartNumber, int iTotalParts, int iDensity, int iDuration, bool fEncrypted, CAmount nFee)
{
    std::map<std::string, std::string> mapRequestHeaders;
    mapRequestHeaders["PartNumber"] = RoundToString(iPartNumber, 0);
    mapRequestHeaders["TXID"] = sTXID;
    mapRequestHeaders["Fee"] = RoundToString(nFee / COIN, 2);
    mapRequestHeaders["WebPath"] = sWebPath;
    mapRequestHeaders["Density"] = RoundToString(iDensity, 0);
    mapRequestHeaders["Duration"] = RoundToString(iDuration, 0);
    mapRequestHeaders["Part"] = sFileName;
    mapRequestHeaders["OriginalName"] = sOriginalName;
    mapRequestHeaders["TotalParts"] = RoundToString(iTotalParts, 0);
    mapRequestHeaders["BlockHash"] = chainActive.Tip()->GetBlockHash().GetHex();
    mapRequestHeaders["BlockHeight"] = RoundToString(chainActive.Tip()->nHeight, 0);
    std::string sCPK = DefaultRecAddress("Christian-Public-Key");
    mapRequestHeaders["CPK"] = sCPK;
    std::string sData = GetAttachmentData(sFileName, fEncrypted);
    LogPrintf("IPFS::SubmitIPFSPart Part # %f, DataLen %s", iPartNumber, sData.size());

    DACResult b;
    b.Response = Uplink(true, sData, sBaseURL, sPage, iPort, 600, 1, mapRequestHeaders);
    return b;
}

std::vector<char> ReadAllBytesFromFile(char const* filename)
{
    std::ifstream ifs(filename, std::ios::binary | std::ios::ate);
    std::ifstream::pos_type pos = ifs.tellg();
    std::vector<char> result(pos);
    ifs.seekg(0, std::ios::beg);
    ifs.read(&result[0], pos);
    return result;
}

DACResult DownloadFile(std::string sBaseURL, std::string sPage, int iPort, int iTimeoutSecs, std::string sTargetFileName, bool fEncrypted)
{
    std::map<std::string, std::string> mapRequestHeaders;
    DACResult dResult;
    std::string sTargetPath = sTargetFileName;
    if (fEncrypted) {
        std::string sTargetPath = sTargetFileName;
        sTargetFileName = sTargetFileName + ".temp";
    }
    dResult.Response = Uplink(false, "", sBaseURL, sPage, iPort, iTimeoutSecs, 1, mapRequestHeaders, sTargetFileName);
    if (fEncrypted) {
        DecryptFile(sTargetFileName, sTargetPath);
    }
    return dResult;
}

static double HTTP_PROTO_VERSION = 2.0;
std::string Uplink(bool bPost, std::string sPayload, std::string sBaseURL, std::string sPage, int iPort, int iTimeoutSecs, int iBOE, std::map<std::string, std::string> mapRequestHeaders, std::string TargetFileName, bool fJson)
{
    std::string sData;
    int iRead = 0;
    int iMaxSize = 20000000;
    double dMaxSize = 0;
    std::ofstream OutFile;

    if (!TargetFileName.empty()) {
        OutFile.open(TargetFileName, std::ios::out | std::ios::binary);
        iMaxSize = 300000000;
    }

    bool fContentLengthFound = false;

    // The OpenSSL version of Post *only* works with SSL websites, hence the need for HTTPPost(2) (using BOOST).  The dev team is working on cleaning this up before the end of 2019 to have one standard version with cleaner code and less internal parts. //
    try {
        double dDebugLevel = cdbl(gArgs.GetArg("-devdebuglevel", "0"), 0);

        if (dDebugLevel == 1)
            LogPrintf("\r\nUplink::Connecting to %s [/] %s ", sBaseURL, sPage);

        mapRequestHeaders["Agent"] = FormatFullVersion();
        // Supported pool Network Chain modes: main, test, regtest
        const CChainParams& chainparams = Params();
        mapRequestHeaders["NetworkID"] = chainparams.NetworkIDString();
        mapRequestHeaders["OS"] = sOS;
        mapRequestHeaders["SessionID"] = msSessionID;
        if (sPayload.length() < 7777)
            mapRequestHeaders["Action"] = sPayload;
        mapRequestHeaders["HTTP_PROTO_VERSION"] = RoundToString(HTTP_PROTO_VERSION, 0);
        if (bPost)
            mapRequestHeaders["Content-Type"] = "application/octet-stream";

        if (fJson)
            mapRequestHeaders["Content-Type"] = "application/json";

        BIO* bio;
        // Todo add connection timeout here to bio object

        SSL_CTX* ctx;
        //   Registers the SSL/TLS ciphers and digests and starts the security layer.
        SSL_library_init();
        ctx = SSL_CTX_new(SSLv23_client_method());
        if (ctx == NULL) {
            if (!TargetFileName.empty())
                OutFile.close();
            BIO_free_all(bio);

            return "<ERROR>CTX_IS_NULL</ERROR>";
        }
        bio = BIO_new_ssl_connect(ctx);
        std::string sDomain = GetDomainFromURL(sBaseURL);
        std::string sDomainWithPort = sDomain + ":" + RoundToString(iPort, 0);

        // Compatibility with strict d-dos prevention rules (like cloudflare)
        SSL* ssl(nullptr);
        BIO_get_ssl(bio, &ssl);
        SSL_set_mode(ssl, SSL_MODE_AUTO_RETRY);
        SSL_set_tlsext_host_name(ssl, const_cast<char*>(sDomain.c_str()));
        BIO_set_conn_hostname(bio, sDomainWithPort.c_str());
        BIO_set_conn_int_port(bio, &iPort);

        if (dDebugLevel == 1)
            LogPrintf("Connecting to %s", sDomainWithPort.c_str());
        int nRet = 0;
        if (sDomain.empty()) {
            BIO_free_all(bio);
            return "<ERROR>DOMAIN_MISSING</ERROR>";
        }

        nRet = BIO_do_connect(bio);
        if (nRet <= 0) {
            if (dDebugLevel == 1)
                LogPrintf("Failed connection to %s ", sDomainWithPort);
            if (!TargetFileName.empty())
                OutFile.close();
            BIO_free_all(bio);

            return "<ERROR>Failed connection to " + sDomainWithPort + "</ERROR>";
        }

        // Evo requires 2 args instead of 3, the last used to be true for DNS resolution=true

        std::string sPost = PrepareHTTPPost(bPost, sPage, sDomain, sPayload, mapRequestHeaders);
        const char* write_buf = sPost.c_str();
        if (dDebugLevel == 1)
            LogPrintf("BioPost %f", 801);
        if (BIO_write(bio, write_buf, strlen(write_buf)) <= 0) {
            if (!TargetFileName.empty())
                OutFile.close();
            BIO_free_all(bio);

            return "<ERROR>FAILED_HTTPS_POST</ERROR>";
        }
        //  Variables used to read the response from the server
        int size;
        clock_t begin = clock();
        char buf[16384];
        for (;;) {
            if (dDebugLevel == 1)
                LogPrintf("BioRead %f", 803);

            size = BIO_read(bio, buf, 16384);
            if (size <= 0) {
                break;
            }
            iRead += (int)size;
            buf[size] = 0;
            std::string MyData(buf);
            int iOffset = 0;

            if (!TargetFileName.empty()) {
                if (!fContentLengthFound) {
                    if (MyData.find("Content-Length:") != std::string::npos) {
                        std::size_t iFoundPos = MyData.find("\r\n\r\n");
                        if ((int)iFoundPos > 1) {
                            iOffset = (int)iFoundPos + 4;
                            size -= iOffset;
                        }
                    }
                }

                OutFile.write(&buf[iOffset], size);
            } else {
                sData += MyData;
            }

            if (dDebugLevel == 1)
                LogPrintf(" BioReadFinished maxsize %f datasize %f ", dMaxSize, iRead);

            clock_t end = clock();
            double elapsed_secs = double(end - begin) / (CLOCKS_PER_SEC + .01);
            if (elapsed_secs > iTimeoutSecs) break;
            if (TermPeekFound(sData, iBOE)) break;

            if (!fContentLengthFound) {
                if (MyData.find("Content-Length:") != std::string::npos) {
                    dMaxSize = cdbl(ExtractXML(MyData, "Content-Length: ", "\n"), 0);
                    std::size_t foundPos = MyData.find("Content-Length:");
                    if (dMaxSize > 0) {
                        iMaxSize = dMaxSize + (int)foundPos + 16;
                        fContentLengthFound = true;
                    }
                }
            }

            if (iRead >= iMaxSize)
                break;
        }
        // Free bio resources
        BIO_free_all(bio);
        if (!TargetFileName.empty())
            OutFile.close();

        return sData;
    } catch (std::exception& e) {
        return "<ERROR>WEB_EXCEPTION</ERROR>";
    } catch (...) {
        return "<ERROR>GENERAL_WEB_EXCEPTION</ERROR>";
    }
}

static std::string DECENTRALIZED_SERVER_FARM_PREFIX = "web.";
static std::string SSL_PROTOCOL_WEB = "https://";
static int SSL_TIMEOUT = 15;
static int SSL_CONN_TIMEOUT = 10000;
DACResult DSQL(UniValue uObject, std::string sXML)
{
    std::string sEndpoint = SSL_PROTOCOL_WEB + DECENTRALIZED_SERVER_FARM_PREFIX + DOMAIN_NAME;
    std::string sMVC = "BMS/SubmitChristianObject";
    std::string sJson = uObject.write().c_str();
    std::string sPayload = "<jsondata>" + sJson + "</jsondata>" + sXML;
    DACResult b;
    b.Response = Uplink(true, sPayload, sEndpoint, sMVC, SSL_PORT, SSL_TIMEOUT, 1);
    b.ErrorCode = ExtractXML(b.Response, "<ERRORS>", "<ERRORS>");
    return b;
}

bool WriteKey(std::string sKey, std::string sValue)
{
    std::string sDelimiter = sOS == "WIN" ? "\r\n" : "\n";
    // Allows DAC to store the key value in the config file.
    boost::filesystem::path pathConfigFile(gArgs.GetArg("-conf", GetConfFileName()));
    if (!pathConfigFile.is_complete()) pathConfigFile = GetDataDir(false) / pathConfigFile;
    if (!boost::filesystem::exists(pathConfigFile)) {
        // Config is empty, create it:
        FILE* outFileNew = fopen(pathConfigFile.string().c_str(), "w");
        fputs("", outFileNew);
        fclose(outFileNew);
        LogPrintf("** Created brand new .conf file **\n");
    }

    // Allow camel-case keys (required by our external purse feature)
    std::string sLine;
    std::ifstream streamConfigFile;
    streamConfigFile.open(pathConfigFile.string().c_str());
    std::string sConfig = "";
    bool fWritten = false;
    if (streamConfigFile) {
        while (getline(streamConfigFile, sLine)) {
            std::vector<std::string> vEntry = Split(sLine, "=");
            if (vEntry.size() == 2) {
                std::string sSourceKey = vEntry[0];
                std::string sSourceValue = vEntry[1];
                // Don't force lowercase anymore in the .conf file for mechanically added values
                if (sSourceKey == sKey) {
                    sSourceValue = sValue;
                    sLine = sSourceKey + "=" + sSourceValue;
                    fWritten = true;
                }
            }
            sLine = strReplace(sLine, "\r", "");
            sLine = strReplace(sLine, "\n", "");
            if (!sLine.empty()) {
                sLine += sDelimiter;
                sConfig += sLine;
            }
        }
    }
    if (!fWritten) {
        sLine = sKey + "=" + sValue + sDelimiter;
        sConfig += sLine;
    }

    streamConfigFile.close();
    FILE* outFile = fopen(pathConfigFile.string().c_str(), "w");
    fputs(sConfig.c_str(), outFile);
    fclose(outFile);
    gArgs.ReadConfigFile(pathConfigFile.string().c_str());
    return true;
}

bool InstantiateOneClickMiningEntries()
{
    WriteKey("addnode", "node." + DOMAIN_NAME);
    WriteKey("addnode", "explorer." + DOMAIN_NAME);
    WriteKey("genproclimit", "1");
    WriteKey("gen", "1");
    return true;
}

std::string GetCPKData(std::string sProjectId, std::string sPK)
{
    return ReadCache(sProjectId, sPK);
}

bool AdvertiseChristianPublicKeypair(std::string sProjectId, std::string sNickName, std::string sEmail, std::string sVendorType, bool fUnJoin, bool fForce, CAmount nFee, std::string sOptData, std::string& sError)
{
    std::string sCPK = DefaultRecAddress("Christian-Public-Key");
    CWallet* const pwallet = GetWalletForGenericRequest();

    boost::to_lower(sProjectId);

    if (sProjectId == "cpk-bmsuser") {
        sCPK = DefaultRecAddress("PUBLIC-FUNDING-ADDRESS");
        bool fMine = false;
        bool fExists = NickNameExists(sProjectId, sNickName, fMine);
        if (fExists && false) {
            sError = "Sorry, BMS Nick Name is already taken.";
            return false;
        }
    } else {
        if (!sNickName.empty()) {
            bool fIsMine;
            bool fExists = NickNameExists("cpk", sNickName, fIsMine);
            if (fExists && !fIsMine) {
                sError = "Sorry, NickName is already taken.";
                return false;
            }
        }
    }
    std::string sRec = GetCPKData(sProjectId, sCPK);
    if (fUnJoin) {
        if (sRec.empty()) {
            sError = "Sorry, you are not enrolled in this project.";
            return false;
        }
    } else if (!sRec.empty() && !fForce) {
        sError = "ALREADY_IN_CHAIN";
        return false;
    }

    if (sNickName.length() > 20 && sVendorType.empty()) {
        sError = "Sorry, nickname length must be 10 characters or less.";
        return false;
    }

    double nLastCPK = ReadCacheDouble(sProjectId);
    const Consensus::Params& consensusParams = Params().GetConsensus();
    if ((chainActive.Tip()->nHeight - nLastCPK) < 4 && nLastCPK > 0 && !fForce) {
        sError = _("A CPK was advertised less then 4 blocks ago. Please wait for your CPK to enter the chain.");
        return false;
    }

    CAmount nBalance = pwallet->GetBalance();
    double nCPKAdvertisementFee = GetSporkDouble("CPKAdvertisementFee", 1);
    if (nFee == 0)
        nFee = nCPKAdvertisementFee * COIN;

    if (nBalance < nFee) {
        sError = "Balance too low to advertise CPK, 1 coin minimum is required.";
        return false;
    }

    sNickName = SanitizeString(sNickName);
    LIMITED_STRING(sNickName, 20);
    std::string sMsg = GetRandHash().GetHex();
    std::string sDataPK = sCPK;

    if (fUnJoin) {
        sDataPK = "";
    }
    std::string sData = sDataPK + "|" + sNickName + "|" + RoundToString(GetAdjustedTime(), 0) + "|" + sMsg;
    std::string sSignature;
    bool bSigned = false;
    bSigned = SignStake(sCPK, sMsg, sError, sSignature);
    // Only append the signature after we prove they can sign...
    if (bSigned) {
        sData += "|" + sSignature + "|" + sEmail + "|" + sVendorType + "|" + sOptData;
    } else {
        sError = "Unable to sign CPK " + sCPK + " (" + sError + ").  Error 837.  Please ensure wallet is unlocked.";
        return false;
    }

    std::string sExtraGscPayload = "<gscsig>" + sSignature + "</gscsig><abncpk>" + sCPK + "</abncpk><abnmsg>" + sMsg + "</abnmsg>";
    sError = std::string();
    std::string sResult = SendBlockchainMessage(sProjectId, sCPK, sData, nFee / COIN, 0, sExtraGscPayload, sError);
    if (!sError.empty()) {
        return false;
    }
    WriteCacheDouble(sProjectId, chainActive.Tip()->nHeight);
    // For the user edit page:  2-26-2021
    if (sProjectId == "cpk") {
        WriteCache("CPK", sCPK, sData, GetAdjustedTime());
        LogPrintf("\r\ncpk %s", sData);
    }
    return true;
}

std::string GetTransactionMessage(CTransactionRef tx)
{
    std::string sMsg;
    for (unsigned int i = 0; i < tx->vout.size(); i++) {
        sMsg += tx->vout[i].sTxOutMessage;
    }
    return sMsg;
}

void ProcessBLSCommand(CTransactionRef tx)
{
    std::string sXML = GetTransactionMessage(tx);
    std::string sEnc = ExtractXML(sXML, "<blscommand>", "</blscommand>");

    if (msMasterNodeLegacyPrivKey.empty())
        return;

    if (sEnc.empty())
        return;
    // Decrypt using the masternodeprivkey
    std::string sCommand = DecryptAES256(sEnc, msMasterNodeLegacyPrivKey);
    if (!sCommand.empty()) {
        std::vector<std::string> vCmd = Split(sCommand, "=");
        if (vCmd.size() == 2) {
            if (vCmd[0] == "masternodeblsprivkey" && !vCmd[1].empty()) {
                LogPrintf("\nProcessing bls command %s with %s", vCmd[0], vCmd[1]);
                WriteKey(vCmd[0], vCmd[1]);
                // At this point we should re-read the masternodeblsprivkey.  Then ensure the activeMasternode info is updated and the deterministic masternode starts.
            }
        }
    }
}

double GetVINCoinAge(int64_t nBlockTime, CTransactionRef tx, bool fDebug)
{
    double dTotal = 0;
    std::string sDebugData = "\nGetVINCoinAge: ";
    for (int i = 0; i < (int)tx->vin.size(); i++) {
        int n = tx->vin[i].prevout.n;
        CAmount nAmount = 0;
        int64_t nTime = 0;
        bool fOK = GetTransactionTimeAndAmount(tx->vin[i].prevout.hash, n, nTime, nAmount);
        double nSancScalpingDisabled = GetSporkDouble("preventsanctuaryscalping", 0);
        if (nSancScalpingDisabled == 1 && nAmount == (SANCTUARY_COLLATERAL * COIN)) {
            LogPrintf("\nGetVinCoinAge, Detected unlocked sanctuary in txid %s, Amount %f ", tx->GetHash().GetHex(), nAmount / COIN);
            nAmount = 0;
        }
        if (fOK && nTime > 0 && nAmount > 0) {
            double nAge = (nBlockTime - nTime) / (86400 + .01);
            if (nAge > 365) nAge = 365;
            if (nAge < 0) nAge = 0;
            double dWeight = nAge * (nAmount / COIN);
            dTotal += dWeight;
        }
    }
    return dTotal;
}

double GetVINAge2(int64_t nBlockTime, CTransactionRef tx, CAmount nMinAmount, bool fDebug)
{
    double dTotal = 0;
    CAmount nTotalSpent = 0;
    std::string sDebugData = "\nGetVINAge: ";
    for (int i = 0; i < (int)tx->vin.size(); i++) {
        int n = tx->vin[i].prevout.n;
        CAmount nAmount = 0;
        int64_t nTime = 0;
        bool fOK = GetTransactionTimeAndAmount(tx->vin[i].prevout.hash, n, nTime, nAmount);
        double nSancScalpingDisabled = GetSporkDouble("preventsanctuaryscalping", 0);
        if (nSancScalpingDisabled == 1 && nAmount == (SANCTUARY_COLLATERAL * COIN)) {
            LogPrintf("\nGetVinCoinAge, Detected unlocked sanctuary in txid %s, Amount %f ", tx->GetHash().GetHex(), nAmount / COIN);
            nAmount = 0;
        }
        if (fOK && nTime > 0 && nAmount >= nMinAmount) {
            double nAge = (nBlockTime - nTime) / (86400 + .01);
            if (nAge > 365) nAge = 365;
            if (nAge < 0) nAge = 0;
            double dWeight = nAge * 1;
            dTotal += dWeight;
            nTotalSpent += nAmount;
        }
    }
    return (nTotalSpent >= nMinAmount) ? dTotal : 0;
}

std::string GetPOGBusinessObjectList(std::string sType1, std::string sFields)
{
    const Consensus::Params& consensusParams = Params().GetConsensus();
    if (chainActive.Tip()->nHeight < consensusParams.EVOLUTION_CUTOVER_HEIGHT)
        return "";

    CPK myCPK = GetMyCPK("cpk");
    int iNextSuperblock = 0;
    int iLastSuperblock = GetLastGSCSuperblockHeight(chainActive.Tip()->nHeight, iNextSuperblock);
    std::string sData;
    CAmount nPaymentsLimit = CSuperblock::GetPaymentsLimit(iNextSuperblock, false);
    nPaymentsLimit -= MAX_BLOCK_SUBSIDY * COIN;

    std::string sContract = GetGSCContract(iNextSuperblock, false);
    std::string s1 = ExtractXML(sContract, "<DATA>", "</DATA>");
    std::string sDetails = ExtractXML(sContract, "<DETAILS>", "</DETAILS>");
    std::string sSplitBy = (sType1 == "pog") ? s1 : sDetails;
    std::vector<std::string> vData = Split(sSplitBy.c_str(), "\n");

    LogPrintf("\nGetPogBusinessObjectList details %s ", sDetails);

    //	Detail Row Format: sCampaignName + "|" + CPKAddress + "|" + nPoints + "|" + nProminence + "|" + Members.second.sNickName
    //	Leaderboard Fields: "campaign,nickname,cpk,points,owed,prominence";

    double dTotalPaid = 0;
    double nTotalPoints = 0;
    double nMyPoints = 0;
    double dLimit = (double)nPaymentsLimit / COIN;
    for (int i = 0; i < vData.size(); i++) {
        std::vector<std::string> vRow = Split(vData[i].c_str(), "|");
        if (vRow.size() >= 6) {
            double nPoints = cdbl(vRow[2], 2);
            nTotalPoints += nPoints;
            std::string sCPK = vRow[1];
            double nProminence = cdbl(vRow[3], 8) * 100;
            std::string sNickName = Caption(vRow[4], 10);
            if (sNickName.empty())
                sNickName = "N/A";
            double nOwed = (sType1 == "pog") ? cdbl(vRow[5], 4) : dLimit * nProminence / 100;
            if (sCPK == myCPK.sAddress)
                nMyPoints += nPoints;
            std::string sRow = vRow[0] + "<col>" + sNickName + "<col>" + vRow[1] + "<col>" + vRow[6] + "<col>" + vRow[7] + "<col>" + RoundToString(nPoints, 2) + "<col>" + RoundToString(nOwed, 2) + "<col>" + RoundToString(nProminence, 2) + "<object>";
            sData += sRow;
        }
    }
    double dPD = 1;
    sData += "<difficulty>" + RoundToString(GetDifficulty(chainActive.Tip()), 2) + "</difficulty>";
    sData += "<my_points>" + RoundToString(nMyPoints, 0) + "</my_points>";
    sData += "<my_nickname>" + myCPK.sNickName + "</my_nickname>";
    sData += "<total_points>" + RoundToString(nTotalPoints, 0) + "</total_points>";
    sData += "<participants>" + RoundToString(vData.size() - 1, 0) + "</participants>";
    sData += "<lowblock>" + RoundToString(iNextSuperblock - BLOCKS_PER_DAY, 0) + "</lowblock>";
    sData += "<highblock>" + RoundToString(iNextSuperblock, 0) + "</highblock>";

    return sData;
}

CTransactionRef GetTxRef(uint256 hash)
{
    CBlockIndex* pindexHistorical;
    CTransactionRef tx1;
    uint256 hashBlock1;
    if (GetTransaction(hash, tx1, Params().GetConsensus(), hashBlock1, true)) {
        return tx1;
    }
    return tx1;
}

const CBlockIndex* GetBlockIndexByTransactionHash(const uint256& hash)
{
    CBlockIndex* pindexHistorical;
    CTransactionRef tx1;
    uint256 hashBlock1;
    if (GetTransaction(hash, tx1, Params().GetConsensus(), hashBlock1, true)) {
        BlockMap::iterator mi = mapBlockIndex.find(hashBlock1);
        if (mi != mapBlockIndex.end())
            return mapBlockIndex[hashBlock1];
    }
    return pindexHistorical;
}

double AddVector(std::string sData, std::string sDelim)
{
    double dTotal = 0;
    std::vector<std::string> vAdd = Split(sData.c_str(), sDelim);
    for (int i = 0; i < (int)vAdd.size(); i++) {
        std::string sElement = vAdd[i];
        double dAmt = cdbl(sElement, 2);
        dTotal += dAmt;
    }
    return dTotal;
}

double GetFees(CTransactionRef tx)
{
    CAmount nFees = 0;
    CAmount nValueIn = 0;
    CAmount nValueOut = 0;
    for (int i = 0; i < (int)tx->vin.size(); i++) {
        int n = tx->vin[i].prevout.n;
        CAmount nAmount = 0;
        int64_t nTime = 0;
        bool fOK = GetTransactionTimeAndAmount(tx->vin[i].prevout.hash, n, nTime, nAmount);
        if (fOK && nTime > 0 && nAmount > 0) {
            nValueIn += nAmount;
        }
    }
    for (int i = 0; i < (int)tx->vout.size(); i++) {
        nValueOut += tx->vout[i].nValue;
    }
    nFees = nValueIn - nValueOut;
    return nFees;
}

int64_t GetCacheEntryAge(std::string sSection, std::string sKey)
{
    std::pair<std::string, int64_t> v = mvApplicationCache[sSection + "[-]" + sKey];
    int64_t nTimestamp = v.second;
    int64_t nAge = GetAdjustedTime() - nTimestamp;
    return nAge;
}

std::vector<std::string> GetVectorOfFilesInDirectory(const std::string& dirPath, const std::vector<std::string> dirSkipList = {})
{
    std::vector<std::string> listOfFiles;
    boost::system::error_code ec;

    try {
        if (boost::filesystem::exists(dirPath) && boost::filesystem::is_directory(dirPath)) {
            boost::filesystem::recursive_directory_iterator iter(dirPath);
            boost::filesystem::recursive_directory_iterator end;
            while (iter != end) {
                if (boost::filesystem::is_directory(iter->path()) &&
                    (std::find(dirSkipList.begin(), dirSkipList.end(), iter->path().filename()) != dirSkipList.end())) {
                    iter.no_push();
                } else {
                    listOfFiles.push_back(iter->path().string());
                }
                iter.increment(ec);
                if (ec) {
                    std::cerr << "Error While Accessing : " << iter->path().string() << " :: " << ec.message() << '\n';
                }
            }
        }
    } catch (std::system_error& e) {
        std::cerr << "Exception :: " << e.what();
    }
    return listOfFiles;
}

std::string GetAttachmentData(std::string sPath, bool fEncrypted)
{
    if (sPath.empty())
        return "";

    if (GETFILESIZE(sPath) == 0) {
        LogPrintf("IPFS::GetAttachmentData::Empty %f", 1);
        return "";
    }

    if (fEncrypted) {
        std::string sOriginalName = sPath;
        sPath = sOriginalName + ".enc";

        bool fResult = EncryptFile(sOriginalName, sPath);
        if (!fResult) {
            LogPrintf("GetAttachmentData::FAIL Unable to access encrypted file %s ", sPath);
            return std::string();
        }
    }
    std::vector<char> v = ReadBytesAll(sPath.c_str());
    std::vector<unsigned char> uData(v.begin(), v.end());
    std::string s64 = EncodeBase64(&uData[0], uData.size());
    return s64;
}

std::string ConstructCall(std::string sCallName, std::string sArgs)
{
    std::string s1 = "Server";
    std::string sActName = "action=";
    std::string s2 = s1 + "?" + sActName + sCallName;
    s2 += "&";
    s2 += sArgs;
    return s2;
}

DACResult DSQL_ReadOnlyQuery(std::string sXMLSource)
{
    std::string sDomain = "https://" + GetSporkValue("bms");
    int iTimeout = 30000;
    DACResult b;
    b.Response = Uplink(false, "", sDomain, sXMLSource, SSL_PORT, iTimeout, 4);
    return b;
}

DACResult DSQL_ReadOnlyQuery(std::string sEndpoint, std::string sXML)
{
    std::string sDomain = "https://" + GetSporkValue("bms");
    int iTimeout = 30;
    DACResult b;
    b.Response = Uplink(true, sXML, sDomain, sEndpoint, SSL_PORT, iTimeout, 4);
    return b;
}

DACResult DSQL_ReadOnlyQuery2(std::string sEndpoint, std::string sXML)
{
    std::string sDomain = "https://" + GetSporkValue("bms");
    int iTimeout = 30;
    DACResult b;
    b.Response = Uplink(true, sXML, sDomain, sEndpoint, 443, iTimeout, 4);
    return b;
}

std::string Path_Combine(std::string sPath, std::string sFileName)
{
    if (sFileName.empty())
        return "";
    std::string sDelimiter = "/";
    if (sFileName.substr(0, 1) == sDelimiter)
        sFileName = sFileName.substr(1, sFileName.length() - 1);
    std::string sFullPath = sPath + sDelimiter + sFileName;
    return sFullPath;
}

std::string GetResElement(std::string data, int iElement)
{
    if (data.empty())
        return "";
    std::vector<std::string> vEle = Split(data.c_str(), "|");
    if (iElement + 1 > vEle.size())
        return std::string();
    return vEle[iElement];
}

UserRecord GetUserRecord(std::string sSourceCPK)
{
    UserRecord u;

    if (sSourceCPK == "all") {
        u.NickName = "ALL";
        u.InternalEmail = "all@biblepay.core";
        u.Found = true;
        return u;
    }

    u.NickName = "N/A";
    u.Found = false;
    for (auto ii : mvApplicationCache) {
        if (Contains(ii.first, "CPK[-]")) {
            std::pair<std::string, int64_t> v = mvApplicationCache[ii.first];
            std::string sValue = v.first;
            std::string sCPK = GetResElement(sValue, 0);
            std::string sNickName = GetResElement(sValue, 1);
            if (boost::iequals(sCPK, sSourceCPK) || boost::iequals(sSourceCPK, sNickName)) {
                // DeSerialize
                UserRecord u;
                u.CPK = GetResElement(sValue, 0);
                u.NickName = GetResElement(sValue, 1);
                u.ExternalEmail = GetResElement(sValue, 5);
                // 6=VendorType(User)
                u.URL = GetResElement(sValue, 7);
                u.InternalEmail = u.NickName + "@biblepay.core";
                u.Longitude = GetResElement(sValue, 9);
                u.Latitude = GetResElement(sValue, 10);
                u.RSAPublicKey = GetResElement(sValue, 11);
                u.AuthorizePayments = (int)cdbl(GetResElement(sValue, 12), 0);
                u.Found = true;
                //LogPrintf("\r\n user record for %s, %s, %s, %s", u.NickName, u.ExternalEmail, u.RSAPublicKey, u.Longitude);
                return u;
            }
        }
    }
    return u;
}

int64_t GetTxTime(uint256 blockHash, int& iHeight)
{
    BlockMap::iterator mi = mapBlockIndex.find(blockHash);
    if (mi != mapBlockIndex.end()) {
        CBlockIndex* pindexHistorical = mapBlockIndex[blockHash];
        iHeight = pindexHistorical->nHeight;
        return pindexHistorical->GetBlockTime();
    }
    return 0;
}

bool GetTxDAC(uint256 txid, CTransactionRef& tx1)
{
    uint256 hashBlock1;
    return GetTransaction(txid, tx1, Params().GetConsensus(), hashBlock1, true);
}

std::string GetUTXO(std::string sHash, int nOrdinal, CAmount& nValue, std::string& sError)
{
    nValue = 0;
    Coin coin;
    int nTypeOrdinal = nOrdinal;
    std::string sOriginalHash = sHash;

    if (nTypeOrdinal < 0) {
        std::vector<std::string> vU = Split(sHash.c_str(), "-");
        if (vU.size() < 2) {
            sError = "Malformed UTXO";
            return "";
        }

        sHash = vU[0];
        nOrdinal = (int)cdbl(vU[1], 0);
        if (nTypeOrdinal == -2) {
            sError = "Not implemented.";
            return "";
        }
    }

    uint256 hash(uint256S(sHash));
    COutPoint out(hash, nOrdinal);
    nValue = -1;
    if (GetUTXOCoin(out, coin)) {
        CTxDestination txDest;
        CKeyID keyID;
        if (!ExtractDestination(coin.out.scriptPubKey, txDest)) {
            sError = "No destination.";
            return "";
        }
        nValue = coin.out.nValue;
        std::string sAddress = EncodeDestination(txDest);
        return sAddress;
    }
    sError = "Unable to find UTXO Coin";
    return "";
}

NFT GetNFT(CTransactionRef tx1)
{
    NFT w;
    const Consensus::Params& consensusParams = Params().GetConsensus();
    std::string sData;
    for (unsigned int i = 0; i < tx1->vout.size(); i++) {
        sData += tx1->vout[i].sTxOutMessage;
    }
    w.sXML = ExtractXML(sData, "<nft>", "</nft>");
    w.sCPK = ExtractXML(w.sXML, "<cpk>", "</cpk>");
    w.sName = ExtractXML(w.sXML, "<name>", "</name>");
    w.nTime = (int64_t)cdbl(ExtractXML(w.sXML, "<time>", "</time>"), 0);
    w.sDescription = ExtractXML(w.sXML, "<description>", "</description>");
    w.sType = ExtractXML(w.sXML, "<type>", "</type>");
    w.sLoQualityURL = ExtractXML(w.sXML, "<loqualityurl>", "</loqualityurl>");
    w.sHiQualityURL = ExtractXML(w.sXML, "<hiqualityurl>", "</hiqualityurl>");
    w.fMarketable = cdbl(ExtractXML(w.sXML, "<marketable>", "</marketable>"), 0) == 1 ? true : false;
    w.nIteration = cdbl(ExtractXML(w.sXML, "<iteration>", "</iteration>"), 0);
    w.fDeleted = cdbl(ExtractXML(w.sXML, "<deleted>", "</deleted>"), 0) == 1 ? true : false;
    w.nMinimumBidAmount = cdbl(ExtractXML(w.sXML, "<minbidamount>", "</minbidamount>"), 2) * COIN;
    w.nReserveAmount = cdbl(ExtractXML(w.sXML, "<reserveamount>", "</reserveamount>"), 2) * COIN;
    w.nBuyItNowAmount = cdbl(ExtractXML(w.sXML, "<buyitnowamount>", "</buyitnowamount>"), 2) * COIN;
    w.TXID = tx1->GetHash();
    if (!w.sCPK.empty() && !w.sLoQualityURL.empty()) {
        w.found = true;
        return w;
    }
    return w;
}

std::vector<NFT> GetNFTs(bool fIncludeMemoryPool)
{
    std::vector<NFT> uNFTs;
    for (auto ii : mvApplicationCache) {
        if (Contains(ii.first, "NFT[-]")) {
            std::pair<std::string, int64_t> v = mvApplicationCache[ii.first];
            int64_t nTimestamp = v.second;
            std::string sNFTID = GetElement(ii.first, "[-]", 1);
            uint256 hashNFT = uint256S(sNFTID); // The sha256 hash of the nft (not the txid)
            std::string sTXID = ExtractXML(v.first, "<nfttxid>", "</nfttxid>");
            uint256 hashTXID = uint256S(sTXID);
            CTransactionRef tx1;
            bool fGot = GetTxDAC(hashTXID, tx1);
            if (fGot) {
                NFT w = GetNFT(tx1);
                if (w.found) {
                    uNFTs.push_back(w);
                    LogPrint(BCLog::NET, "\nGetNFT::found %s", sTXID);
                } else {
                    LogPrint(BCLog::NET, "\nGetNFT::cant find %s", sTXID);
                }
            } else {
                LogPrint(BCLog::NET, "\nGetNFTs::tx-dac-not found %s ", sTXID);
            }
        }
    }

    if (fIncludeMemoryPool) {
        for (auto e : mempool.mapTx) {
            const CTransaction& tx = e.GetTx();
            CTransactionRef tx1 = MakeTransactionRef(std::move(tx));
            NFT w = GetNFT(tx1);
            if (w.found)
                uNFTs.push_back(w);
        }
    }
    return uNFTs;
}


void UpdatePricingInformationOnUTXO(UTXOStake& u)
{
    std::vector<SimpleUTXO> vU = GetUTXOStatus(u.Address);
    double nPin = AddressToPin(u.Address);
    CAmount nNativeTotal = 0;
    CAmount nForeignTotal = 0;
    double dBTC = GetCryptoPrice("btc");
    double d1 = GetCryptoPrice(u.Ticker);
    if (u.Ticker == "BTC" || u.Ticker == "btc") {
        d1 = 1;
    }
    double d1USD = d1 * dBTC;

    for (auto sU : vU) {
        if (sU.nAmount > 0) {
            // Harvest ToDo::Compare fLockTimeCheck here to block out any chance of utxo-farming
            bool fMask = CompareMask2(sU.nAmount, nPin);
            if (!fMask) {
                // Reserved.. Cache non-matching utxos for a debug view for the user... Maybe they don't know they are doing this...
            }
            if (fMask) {
                if (u.Ticker == "BBP") {
                    nNativeTotal += sU.nAmount;
                } else {
                    nForeignTotal += sU.nAmount;
                }
            }
        }
    }
    u.nNativeTotal = nNativeTotal;
    u.nForeignTotal = nForeignTotal;
    u.nNativeTotalUSD = d1USD * AmountToDouble(u.nNativeTotal);
    u.nForeignTotalUSD = d1USD * AmountToDouble(u.nForeignTotal);
    u.nValueUSD = u.nNativeTotalUSD + u.nForeignTotalUSD;
}

void RetrieveUTXOWorth(std::vector<UTXOStake> vu, std::string sCPK, double& nNative, double& nForeign)
{
    nNative = 0;
    nForeign = 0;
    for (auto u : vu) {
        if (u.nValueUSD > 0 && sCPK == u.CPK) {
            nNative += u.nNativeTotalUSD;
            nForeign += u.nForeignTotalUSD;
        }
    }
}

void CalculateForeignCoverage(std::vector<UTXOStake>& vu)
{
    // Calculate foreign coverage % (User must have $1 of BBP for each $1 of Foreign)

    for (auto& u : vu) {
        UpdatePricingInformationOnUTXO(u);
    }

    for (auto& u : vu) {
        if (u.nValueUSD > 0) {
            double nNative = 0;
            double nForeign = 0;
            RetrieveUTXOWorth(vu, u.CPK, nNative, nForeign);
            u.nNativeUSDGrandTotal = nNative;
            u.nForeignUSDGrandTotal = nForeign;
            u.nCoverage = u.nNativeUSDGrandTotal / (u.nForeignUSDGrandTotal + .01);
            if (u.nCoverage > 1.0)
                u.nCoverage = 1.0;
            if (u.Ticker == "BBP") {
                // BBP is always covered
                u.nCoverage = 1.0;
            }
        }
    }
}


std::vector<UTXOStake> GetUTXOStakes(bool fWithPriceInfo)
{
    std::vector<UTXOStake> uStakes;
    for (auto ii : mvApplicationCache) {
        if (Contains(ii.first, "UTXOSTAKE3[-]")) {
            std::pair<std::string, int64_t> v = mvApplicationCache[ii.first];
            UTXOStake u;
            u.FromXML(v.first);
            if (u.found)
                uStakes.push_back(u);
        }
    }

    /*

	if (fIncludeMemoryPool)
	{
		for (auto e : mempool.mapTx) 
		{
			const CTransaction& tx = e.GetTx();
			CTransactionRef tx1 = MakeTransactionRef(std::move(tx));
			UTXOStake w = GetUTXOStake(tx1);
			if (w.found && w.nBBPAmount)
				uStakes.push_back(w);
		}
	}
	*/
    if (fWithPriceInfo) {
        CalculateForeignCoverage(uStakes);
    }
    return uStakes;
}


NFT GetSpecificNFT(uint256 txid)
{
    NFT n;
    std::vector<NFT> nfts = GetNFTs(true);
    for (int i = 0; i < nfts.size(); i++) {
        n = nfts[i];
        if (n.GetHash() == txid)
            return n;
    }
    return n;
}

bool IsDuplicateNFT(NFT& nft)
{
    std::vector<NFT> nfts = GetNFTs(false);
    for (int i = 0; i < nfts.size(); i++) {
        NFT n = nfts[i];
        if (n.GetHash() == nft.GetHash())
            return true;
    }
    return false;
}

std::string NFTOwnedBy(NFT& nft)
{
    std::vector<NFT> nfts = GetNFTs(true);
    for (int i = 0; i < nfts.size(); i++) {
        NFT n = nfts[i];
        if (n.GetHash() == nft.GetHash()) {
            return n.sCPK;
        }
    }
    return std::string();
}

bool IsDuplicateUTXO(std::string Address)
{
    if (Address.empty())
        return false;
    std::vector<UTXOStake> utxoStakes = GetUTXOStakes();
    for (int i = 0; i < utxoStakes.size(); i++) {
        UTXOStake d = utxoStakes[i];
        if (d.Address == Address)
            return true;
    }
    return false;
}

bool Tolerance(double nActualPrice, double nPurported, double nTolerance)
{
    if (nPurported >= nActualPrice * (1 - nTolerance) && nPurported <= nActualPrice * (1 + nTolerance))
        return true;
    return false;
}

double GetVinAge(int64_t nVINTime, int64_t nSpendTime, CAmount nAmount)
{
    double nAge = (double)(nSpendTime - nVINTime) / 86400;
    if (nAge < 000) nAge = 0;
    if (nAge > 365) nAge = 365;
    double nWeight = (nAmount / COIN) * nAge;
    return nWeight;
}

CoinVin GetCoinVIN(COutPoint o, int64_t nTxTime)
{
    CoinVin b;
    b.OutPoint = o;
    b.HashBlock = uint256();
    // Special case if the transaction is not in a block:
    for (auto e : mempool.mapTx) {
        const uint256& hash = e.GetTx().GetHash();
        if (hash == o.hash) {
            const CTransaction& tx = e.GetTx();
            CTransactionRef tx1 = MakeTransactionRef(std::move(tx));
            b.TxRef = tx1;
            b.BlockTime = GetAdjustedTime(); //Memory Pool
            b.Amount = b.TxRef->vout[b.OutPoint.n].nValue;
            b.Destination = PubKeyToAddress(b.TxRef->vout[b.OutPoint.n].scriptPubKey);
            b.CoinAge = GetVinAge(b.BlockTime, nTxTime, b.Amount);
            b.Found = true;
            return b;
        }
    }

    if (GetTransaction(b.OutPoint.hash, b.TxRef, Params().GetConsensus(), b.HashBlock, true)) {
        BlockMap::iterator mi = mapBlockIndex.find(b.HashBlock);
        if (mi != mapBlockIndex.end() && (*mi).second) {
            CBlockIndex* pMNIndex = (*mi).second;
            b.BlockTime = pMNIndex->GetBlockTime();
            if (b.OutPoint.n <= b.TxRef->vout.size() - 1) {
                b.Amount = b.TxRef->vout[b.OutPoint.n].nValue;
                b.Destination = PubKeyToAddress(b.TxRef->vout[b.OutPoint.n].scriptPubKey);
                b.CoinAge = GetVinAge(b.BlockTime, nTxTime, b.Amount);
            }
            b.Found = true;
            return b;
        } else {
            b.Destination = "NOT_IN_INDEX";
            b.Amount = 0;
        }
    }
    b.Destination = "NOT_FOUND";
    b.Amount = 0;
    return b;
}


uint256 ComputeRandomXTarget(uint256 dac_hash, int64_t nPrevBlockTime, int64_t nBlockTime)
{
    static int MAX_AGE = 60 * 30;
    static int MAX_AGE2 = 60 * 45;
    static int MAX_AGE3 = 60 * 15;
    static int64_t nDivisor = 8400;
    int64_t nElapsed = nBlockTime - nPrevBlockTime;
    if (nElapsed > MAX_AGE) {
        arith_uint256 bnHash = UintToArith256(dac_hash);
        bnHash *= 700;
        bnHash /= nDivisor;
        uint256 nBH = ArithToUint256(bnHash);
        return nBH;
    }

    if (nElapsed > MAX_AGE2) {
        arith_uint256 bnHash = UintToArith256(dac_hash);
        bnHash *= 200;
        bnHash /= nDivisor;
        uint256 nBH = ArithToUint256(bnHash);
        return nBH;
    }

    if (nElapsed > MAX_AGE3 && !fProd) {
        arith_uint256 bnHash = UintToArith256(dac_hash);
        bnHash *= 10;
        bnHash /= nDivisor;
        uint256 nBH = ArithToUint256(bnHash);
        return nBH;
    }

    return dac_hash;
}

std::string GenerateFaucetCode()
{
    std::string sCPK = DefaultRecAddress("Christian-Public-Key");
    CWallet* const pwallet = GetWalletForGenericRequest();
    std::string s1 = RoundToString(((double)(pwallet->GetBalance() / COIN) / 1000), 0);
    std::string sXML = "<cpk>" + sCPK + "</cpk><s1>" + s1 + "</s1>";
    DACResult b = DSQL_ReadOnlyQuery("BMS/FaucetID", sXML);
    std::string sResponse = ExtractXML(b.Response, "<response>", "</response>");
    if (sResponse.empty())
        sResponse = "N/A";
    return sResponse;
}

std::string ReverseHex(std::string const& src)
{
    if (src.size() % 2 != 0)
        return std::string();
    std::string result;
    result.reserve(src.size());

    for (std::size_t i = src.size(); i != 0; i -= 2) {
        result.append(src, i - 2, 2);
    }

    return result;
}

static std::map<int, std::mutex> cs_rxhash;
uint256 GetRandomXHash(std::string sHeaderHex, uint256 key, uint256 hashPrevBlock, int iThreadID)
{
    // *****************************************                      RandomX                                    ************************************************************************
    // Starting at RANDOMX_HEIGHT, we now solve for an equation, rather than simply the difficulty and target.  (See prevention of preimage attacks in our wiki https://wiki.biblepay.org/Preventing_Preimage_Attacks)
    // This is so our miners may earn a dual revenue stream (RandomX coins + DAC/BiblePay Coins).
    // The equation is:  BlakeHash(Previous_DAC_Hash + RandomX_Hash(RandomX_Coin_Header)) < Current_DAC_Block_Difficulty
    // **********************************************************************************************************************************************************************************
    std::unique_lock<std::mutex> lock(cs_rxhash[iThreadID]);
    std::vector<unsigned char> vch(160);
    CVectorWriter ss(SER_NETWORK, PROTOCOL_VERSION, vch, 0);
    std::string randomXBlockHeader = ExtractXML(sHeaderHex, "<rxheader>", "</rxheader>");
    std::vector<unsigned char> data0 = ParseHex(randomXBlockHeader);
    uint256 uRXMined = RandomX_Hash(data0, key, iThreadID);
    ss << hashPrevBlock << uRXMined;
    return HashBlake((const char*)vch.data(), (const char*)vch.data() + vch.size());
}

uint256 GetRandomXHash2(std::string sHeaderHex, uint256 key, uint256 hashPrevBlock, int iThreadID)
{
    // *****************************************                      RandomX - Hash Only                          ************************************************************************
    std::unique_lock<std::mutex> lock(cs_rxhash[iThreadID]);
    std::string randomXBlockHeader = ExtractXML(sHeaderHex, "<rxheader>", "</rxheader>");
    std::vector<unsigned char> data0 = ParseHex(randomXBlockHeader);
    uint256 uRXMined = RandomX_Hash(data0, key, iThreadID);
    return uRXMined;
}

std::tuple<std::string, std::string, std::string> GetOrphanPOOSURL(std::string sSanctuaryPubKey)
{
    std::string sURL = "https://";
    std::string sDomain = GetSporkValue("poseorphandomain");
    if (sDomain.empty())
        sDomain = "biblepay.cameroonone.org";
    sURL += sDomain;
    if (sSanctuaryPubKey.empty())
        return std::make_tuple("", "", "");
    std::string sPrefix = sSanctuaryPubKey.substr(0, std::min((int)sSanctuaryPubKey.length(), 8));
    std::string sPage = "bios/" + sPrefix + ".htm";
    return std::make_tuple(sURL, sPage, sPrefix);
}

bool POOSOrphanTest(std::string sSanctuaryPubKey, int64_t nTimeout)
{
    std::tuple<std::string, std::string, std::string> t = GetOrphanPOOSURL(sSanctuaryPubKey);
    std::string sResponse = Uplink(false, "", std::get<0>(t), std::get<1>(t), SSL_PORT, 25, 1);
    std::string sOK = ExtractXML(sResponse, "Status:", "\r\n");
    bool fOK = Contains(sOK, "OK");
    return fOK;
}

double GetHighDWURewardPercentage(double dCommitment)
{
    double nHighRewardsEnabled = GetSporkDouble("EnableHighRewards", 0);
    if (nHighRewardsEnabled == 86)
        return 1.0;

    // dCommitment is in seconds
    double dPenalty = 0;
    double dDays = dCommitment * 1.0;
    double MAX_ADDL_MULTIPLIER = 1.0;
    double DAY_FACTOR = MAX_ADDL_MULTIPLIER / (365 - 30);
    if (dDays >= 30 && dDays <= 720) {
        dPenalty = dDays * DAY_FACTOR;
    }
    if (dPenalty > MAX_ADDL_MULTIPLIER)
        dPenalty = MAX_ADDL_MULTIPLIER;
    return dPenalty + 1.0;
}

CAmount GetTxAmount1(uint256 hash, int ordinal)
{
    int64_t nTime1 = 0;
    CAmount nAmount1 = 0;
    GetTransactionTimeAndAmount(hash, ordinal, nTime1, nAmount1);
    return nAmount1;
}

int64_t GetTxTime1(uint256 hash, int ordinal)
{
    int64_t nTime1 = 0;
    CAmount nAmount1 = 0;
    GetTransactionTimeAndAmount(hash, ordinal, nTime1, nAmount1);
    return nTime1;
}

CAmount GetBurnedTotal(CTransaction tx)
{
    CAmount nAmountBurned = 0;
    const Consensus::Params& consensusParams = Params().GetConsensus();
    for (unsigned int i = 0; i < tx.vout.size(); i++) {
        std::string sPK = PubKeyToAddress(tx.vout[i].scriptPubKey);
        if (sPK == consensusParams.BurnAddress) {
            nAmountBurned += tx.vout[i].nValue;
        }
    }
    return nAmountBurned;
}

CAmount GetUTXOPenalty(CTransaction tx, double& nPenaltyPercentage, CAmount& nAmountBurned)
{
    /*
	CAmount caTotalAmountRequired = 0;
	nAmountBurned = GetBurnedTotal(tx);
	nPenaltyPercentage = 0;
    for (unsigned int i = 0; i < tx.vin.size(); i++) 
	{
		std::string sBBPUTXO = tx.vin[i].prevout.hash.GetHex() + "-" + RoundToString(tx.vin[i].prevout.n, 0);
		std::string sStake = ReadCache("utxo-commitment", sBBPUTXO);
		std::string sBurnUTXO = ExtractXML(sStake, "<burnutxo>", "</burnutxo>");
		if (!sBurnUTXO.empty())
		{
			CAmount nAmountOriginallyStaked = GetTxAmount1(tx.vin[i].prevout.hash, tx.vin[i].prevout.n);
			uint256 burnhash = uint256S("0x" + GetElement(sBurnUTXO, "-", 0));
			int nburnOrdinal = (int)cdbl(GetElement(sBurnUTXO, "-", 1), 0);
			UTXOStake u = GetUTXOStakeByHash(burnhash);
			if (u.found && u.CommitmentFulfilledPctg < 1.0)
			{
				double nDWU = CalculateUTXOReward(u.nType, u.nCommitment); //nType contains stake-type (1=bbp only, 2=foreign+bbp)
				nPenaltyPercentage = nDWU * 1.75;
				if (nPenaltyPercentage > .50)
					nPenaltyPercentage = .50;
				double nAmountRequired = nPenaltyPercentage * ((double)nAmountOriginallyStaked/COIN);
				CAmount caAmountRequired = nAmountRequired * COIN;
				caTotalAmountRequired += caAmountRequired;
				LogPrintf("\nApproveUTXOSpendTx AmountOriginallyStaked %f, bareDWU %f, ActualDWU %f, StakeType %f, Requires %f to be burned, found %f burned, TotalAmountRequired %f, DaysElapsed %f, DaysReq %f", 
							(double)nAmountOriginallyStaked/COIN, nDWU, nPenaltyPercentage, u.nType, (double)caAmountRequired/COIN, (double)nAmountBurned/COIN, 
							(double)caTotalAmountRequired/COIN, u.DaysElapsed, u.nCommitment);
			}
			
		}
	}
	return caTotalAmountRequired;
	*/
    return 0;
}

bool ApproveUTXOSpendTransaction(CTransaction tx)
{
    // In high-return staking, the user agrees to pay a network penalty if they spend the stake earlier than contract promise duration.
    // Here we check if they made a commitment, and receive the commitment duration length in seconds.
    // Then we pull up the original stake, and calculate its DWU.  We multiply this * the DWU penalty.
    // We convert the penalty to amount of burned coins required to free the tx.
    // If the burned coins are not present in the tx, the tx is rejected.
    const Consensus::Params& consensusParams = Params().GetConsensus();
    double nPenaltyPercentage = 0;
    CAmount nAmountBurned = 0;
    CAmount nRequired = GetUTXOPenalty(tx, nPenaltyPercentage, nAmountBurned);
    if (nRequired > (nAmountBurned + (1 * COIN))) {
        LogPrintf("\nApproveUTXOSpendTransaction::REJECTED::Amount burned in tx [%s]=[%f] is less than amount required [%f].",
            tx.GetHash().GetHex(), nAmountBurned / COIN, nRequired / COIN);
        return false;
    }
    return true;
}

void ProcessDSQLInstantSendTransaction(CTransaction tx)
{
    // Immediately process DSQL database updates for transactions that are paid
    CAmount nAmountBurned = 0;
    CTransactionRef tx1 = MakeTransactionRef(std::move(tx));
    nAmountBurned = GetBurnedTotal(tx);
    CAmount MIN_DSQL_FEE = 1 * COIN;
    std::string XML = GetTransactionMessage(tx1);
    std::string sMsgType = ExtractXML(XML, "<MT>", "</MT>");

    if (sMsgType == "DSQL" && nAmountBurned >= MIN_DSQL_FEE) {
        std::string sMsg = ExtractXML(XML, "<BOMSG>", "</BOMSG>");
        std::string sBOSigner = ExtractXML(XML, "<BOSIGNER>", "</BOSIGNER>");
        std::string sBOSig = ExtractXML(XML, "<BOSIG>", "</BOSIG>");
        std::string sMsgKey = ExtractXML(XML, "<MK>", "</MK>");
        std::string sMV = ExtractXML(XML, "<MV>", "</MV>");
        std::string sError;
        CWallet* const pwallet = GetWalletForGenericRequest();

        bool fPassedSecurityCheck = pwallet->CheckStakeSignature(sBOSigner, sBOSig, sMsg, sError);
        if (fPassedSecurityCheck) {
            sMV += "<txid>" + tx.GetHash().GetHex() + "</txid>";
            WriteCache(sMsgType, sMsgKey, sMV, GetAdjustedTime(), true);
        }
    }
}

bool ApproveSanctuaryRevivalTransaction(CTransaction tx)
{
    double nOrphanBanning = GetSporkDouble("EnableOrphanSanctuaryBanning", 0);
    bool fConnectivity = POOSOrphanTest("status", 60);
    if (nOrphanBanning != 1)
        return true;
    if (!fConnectivity)
        return true;
    if (tx.nVersion != 3)
        return true;
    // POOS will only check special TXs
    if (tx.nType == TRANSACTION_PROVIDER_UPDATE_SERVICE) {
        CProUpServTx proTx;
        if (!GetTxPayload(tx, proTx)) {
            return true;
        }
        CDeterministicMNList newList = deterministicMNManager->GetListForBlock(chainActive.Tip());
        CDeterministicMNCPtr dmn = newList.GetMN(proTx.proTxHash);
        if (!dmn) {
            return true;
        }
        bool fPoosValid = mapPOOSStatus[dmn->pdmnState->pubKeyOperator.Get().ToString()] == 1;
        LogPrintf("\nApproveSanctuaryRevivalTx TXID=%s, Op=%s, Approved=%f ", tx.GetHash().GetHex(), dmn->pdmnState->pubKeyOperator.Get().ToString(), (double)fPoosValid);
        return fPoosValid;
    } else {
        return true;
    }
}

bool VoteWithCoinAge(std::string sGobjectID, std::string sOutcome, std::string& ERROR_OUT)
{
    bool fGood = false;
    if (sOutcome == "YES" || sOutcome == "NO" || sOutcome == "ABSTAIN")
        fGood = true;
    std::string sError = std::string();
    std::string sWarning = std::string();

    if (!fGood) {
        ERROR_OUT = "Invalid outcome (Yes, No, Abstain).";
        return false;
    }
    const Consensus::Params& consensusParams = Params().GetConsensus();

    CreateLegacyGSCTransmission(1 * COIN, consensusParams.BurnAddress, "coinagevote", sGobjectID, sOutcome, "", sError);
    if (!sError.empty()) {
        LogPrintf("\nVoteWithCoinAge::ERROR %f, WARNING %s, Campaign %s, Error [%s].\n", GetAdjustedTime(), "coinagevote", sError, sWarning);
        ERROR_OUT = sError;
        return false;
    }
    return true;
}

double GetCoinAge(std::string txid)
{
    uint256 hashBlock = uint256();
    uint256 uTx = ParseHashV(txid, "txid");
    COutPoint out1(uTx, 0);
    CoinVin b = GetCoinVIN(out1, 0);
    double nCoinAge = 0;
    if (b.Found) {
        CBlockIndex* pblockindex = mapBlockIndex[b.HashBlock];
        int64_t nBlockTime = GetAdjustedTime();
        if (pblockindex != NULL)
            nBlockTime = pblockindex->GetBlockTime();
        double nCoinAge = GetVINCoinAge(nBlockTime, b.TxRef, false);
        return nCoinAge;
    }
    return 0;
}

CoinAgeVotingDataStruct GetCoinAgeVotingData(std::string sGobjectID)
{
    CoinAgeVotingDataStruct c;
    std::string sOutcomes = "YES;NO;ABSTAIN";
    std::vector<std::string> vOutcomes = Split(sOutcomes.c_str(), ";");

    for (auto ii : mvApplicationCache) {
        std::pair<std::string, int64_t> v = mvApplicationCache[ii.first];
        std::string sCPK = GetElement(ii.first, "[-]", 1);
        std::string sValue = v.first;
        // Calculate the coin-age-sums
        for (int i = 0; i < vOutcomes.size(); i++) {
            std::string sSumKey = "COINAGE-VOTE-SUM-" + vOutcomes[i] + "-" + sGobjectID;
            boost::to_upper(sSumKey);
            if (Contains(ii.first, sSumKey)) {
                double nValue = cdbl(v.first, 2);
                c.mapsVoteAge[i][sCPK] += nValue;
                c.mapTotalCoinAge[i] += nValue;
            }
        }

        // Calculate the vote-totals
        std::string sVoteKey = "COINAGE-VOTE-COUNT-" + sGobjectID;
        boost::to_upper(sVoteKey);
        if (Contains(ii.first, sVoteKey)) {
            std::string sOutcome = v.first;
            if (sOutcome == "YES") {
                c.mapsVoteCount[0][sCPK]++;
                c.mapTotalVotes[0]++;
            } else if (sOutcome == "NO") {
                c.mapsVoteCount[1][sCPK]++;
                c.mapTotalVotes[1]++;
            } else if (sOutcome == "ABSTAIN") {
                c.mapsVoteCount[2][sCPK]++;
                c.mapTotalVotes[2]++;
            }
        }
    }
    return c;
}

std::string APMToString(double nAPM)
{
    std::string sAPM;
    if (nAPM == 0) {
        sAPM = "PRICE_MISSING";
    } else if (nAPM == 1) {
        sAPM = "PRICE_UNCHANGED";
    } else if (nAPM == 2) {
        sAPM = "PRICE_INCREASED";
    } else if (nAPM == 3) {
        sAPM = "PRICE_DECREASED";
    } else {
        sAPM = "N/A";
    }
    return sAPM;
}

std::string GetAPMNarrative()
{
    int iNextSuperblock = 0;
    int iLastSuperblock = GetLastGSCSuperblockHeight(chainActive.Tip()->nHeight, iNextSuperblock);
    double dLastPrice = cdbl(ExtractXML(ExtractBlockMessage(iLastSuperblock), "<bbpprice>", "</bbpprice>"), 12);
    double out_BTC = 0;
    double out_BBP = 0;
    double dPrice = GetPBase(out_BTC, out_BBP);
    CBlockIndex* pindexSuperblock = chainActive[iLastSuperblock];
    if (pindexSuperblock != NULL) {
        std::string sHistoricalTime = TimestampToHRDate(pindexSuperblock->GetBlockTime());
        double nAPM = CalculateAPM(iLastSuperblock);
        std::string sAPMNarr = APMToString(nAPM);
        std::string sNarr = "Prior Open " + RoundToString(dLastPrice, 12) + " @" + sHistoricalTime + " [" + RoundToString(iLastSuperblock, 0) + "]" + "<br>Current Price " + RoundToString(out_BBP, 12) + ", Next SB=" + RoundToString(iNextSuperblock, 0) + ", APM=" + sAPMNarr;

        return sNarr;
    }
    return std::string();
}

std::string GetSANDirectory1()
{
    boost::filesystem::path pathConfigFile(gArgs.GetArg("-conf", GetConfFileName()));
    if (!pathConfigFile.is_complete()) pathConfigFile = GetDataDir(false) / pathConfigFile;
    boost::filesystem::path dir = pathConfigFile.parent_path();
    std::string sDir = dir.string() + "/SAN/";
    boost::filesystem::path pathSAN(sDir);
    if (!boost::filesystem::exists(pathSAN)) {
        boost::filesystem::create_directory(pathSAN);
    }
    return sDir;
}

bool RelinquishSpace(std::string sPath)
{
    if (sPath.empty())
        return false;
    std::string sMD5 = RetrieveMd5(sPath);
    std::string sDir = GetSANDirectory1() + sMD5;
    boost::filesystem::path pathIPFS = sDir;
    boost::filesystem::remove_all(pathIPFS);
    return true;
}

std::vector<char> HexToBytes(const std::string& hex)
{
    std::vector<char> bytes;

    for (unsigned int i = 0; i < hex.length(); i += 2) {
        std::string byteString = hex.substr(i, 2);
        char byte = (char)strtol(byteString.c_str(), NULL, 16);
        bytes.push_back(byte);
    }

    return bytes;
}

bool EncryptFile(std::string sPath, std::string sTargetPath)
{
    int iFileSize = GETFILESIZE(sPath);
    if (iFileSize < 1) {
        return false;
    }
    std::ifstream ifs(sPath, std::ios::binary | std::ios::ate);
    std::ofstream OutFile;
    OutFile.open(sTargetPath.c_str(), std::ios::out | std::ios::binary);
    int iPos = 0;
    int OP_SIZE = 1024;
    // BIBLEPAY - We currently get the key from the biblepay.conf file (from the encryptionkey setting)
    std::string sEncryptionKey = gArgs.GetArg("-encryptionkey", "");
    if (sEncryptionKey.empty()) {
        LogPrintf("IPFS::EncryptFile::EncryptionKey Empty %f", 1);
        return false;
    }
    LogPrintf(" IPFS::Encrypting file %s", sTargetPath);

    while (true) {
        // Encrypt chunks of 64K at a time
        int iBytesLeft = iFileSize - iPos;
        int iBytesToRead = iBytesLeft;
        if (iBytesToRead > OP_SIZE)
            iBytesToRead = OP_SIZE;
        std::vector<char> buffer(1024);
        ifs.seekg(iPos, std::ios::beg);
        ifs.read(&buffer[0], iBytesToRead);
        // Encryption Section
        std::string sBlockHex = HexStr(buffer.begin(), buffer.end());
        std::string sEncrypted = EncryptAES256(sBlockHex, sEncryptionKey);
        // End of Encryption Section
        OutFile.write(&sEncrypted[0], sEncrypted.size());
        if (iPos >= iFileSize)
            break;
        iPos += iBytesToRead;
    }
    OutFile.close();
    ifs.close();
    return true;
}

bool DecryptFile(std::string sPath, std::string sTargetPath)
{
    int iFileSize = GETFILESIZE(sPath);
    if (iFileSize < 1) {
        return false;
    }
    std::ifstream ifs(sPath, std::ios::binary | std::ios::ate);
    std::ofstream OutFile;
    OutFile.open(sTargetPath.c_str(), std::ios::out | std::ios::binary);
    int iPos = 0;
    // OP_SIZE = Base64(EncryptSize(HexSize(Binary(BLOCK_SIZE)))), note that AES256 padding increases the chunk size by 16.  In summary the .enc file is about twice as large as the unencrypted file.
    int OP_SIZE = 2752;
    std::string sEncryptionKey = gArgs.GetArg("-encryptionkey", "");
    if (sEncryptionKey.empty()) {
        LogPrintf("IPFS::DecryptFile::EncryptionKey Empty %f", 1);
        return false;
    }

    while (true) {
        int iBytesToRead = iFileSize - iPos;
        if (iBytesToRead > OP_SIZE)
            iBytesToRead = OP_SIZE;
        std::vector<char> buffer(OP_SIZE);
        ifs.seekg(iPos, std::ios::beg);
        ifs.read(&buffer[0], iBytesToRead);
        std::string sTemp(buffer.begin(), buffer.end());
        std::string sDec = DecryptAES256(sTemp, sEncryptionKey);
        std::vector<char> decBuffer = HexToBytes(sDec);
        OutFile.write(&decBuffer[0], decBuffer.size());
        if (iPos >= iFileSize)
            break;
        iPos += iBytesToRead;
    }
    OutFile.close();
    ifs.close();
    return true;
}


static int MAX_SPLITTER_PARTS = 7000;
static int MAX_PART_SIZE = 10000000;
std::string SplitFile(std::string sPath)
{
    std::string sMD5 = RetrieveMd5(sPath);
    std::string sDir = GetSANDirectory1() + sMD5;
    boost::filesystem::path pathSAN(sDir);
    if (!boost::filesystem::exists(pathSAN)) {
        boost::filesystem::create_directory(pathSAN);
    }
    int iFileSize = GETFILESIZE(sPath);
    std::ifstream ifs(sPath, std::ios::binary | std::ios::ate);
    int iPos = 0;
    int iPart = 0;
    for (int i = 0; i < MAX_SPLITTER_PARTS; i++) {
        int iBytesLeft = iFileSize - iPos;
        int iBytesToRead = iBytesLeft;
        if (iBytesToRead > MAX_PART_SIZE)
            iBytesToRead = MAX_PART_SIZE;
        std::vector<char> buffer(10000000);
        ifs.seekg(iPos, std::ios::beg);
        ifs.read(&buffer[0], iBytesToRead);
        std::string sPartPath = sDir + "/" + RoundToString(iPart, 0) + ".dat";
        std::ofstream OutFile;
        OutFile.open(sPartPath.c_str(), std::ios::out | std::ios::binary);
        OutFile.write(&buffer[0], iBytesToRead);
        OutFile.close();
        iPos += iBytesToRead;
        if (iPos >= iFileSize)
            break;
        iPart++;
    }
    ifs.close();
    // We calculate the md5 hash of the splitter directory (for safety), and return the path to the caller.  (This prevents biblepay from deleting any of the users files by accident).
    return sDir;
}

CAmount CalculateIPFSFee(int nTargetDensity, int nDurationDays, int nSize)
{
    if (nTargetDensity < 1 || nTargetDensity > 4) {
        LogPrintf("IPFS::CalculateIPFSFee Invalid Density %f", nTargetDensity);
        return 0;
    }
    if (nSize < 1) {
        LogPrintf("IPFS::CalculateIPFSFee Invalid Size %f", nSize);
        return 0;
    }
    if (nDurationDays < 1) {
        LogPrintf("IPFS::CalculateIPFSFee Invalid Duration %f", nSize);
        return 0;
    }
    double nSizeFee = nSize / 25000;
    if (nSizeFee < 1000)
        nSizeFee = 1000;
    double nDurationFee = nDurationDays / 30;
    if (nDurationFee < 1)
        nDurationFee = 1;
    double nFee = nSizeFee * nDurationFee * nTargetDensity;
    LogPrintf(" Fee %f D=%f, DUR=%f, sz=%f ", nFee, nTargetDensity, nDurationDays, nSize);
    return nFee * COIN;
}

DACResult BIPFS_UploadFile(std::string sLocalPath, std::string sWebPath, std::string sTXID, int iTargetDensity, int nDurationDays, bool fDryRun, bool fEncrypted)
{
    // The sidechain stored file must contain the target density, the lease duration, and the correct amount.
    // The corresponding TXID must contain the hash of the file URL
    std::string sDir = SplitFile(sLocalPath);
    DACResult d;

    if (sDir.empty()) {
        d.ErrorCode = "DIRECTORY_EMPTY";
        return d;
    }
    boost::filesystem::path p(sLocalPath);
    std::string sOriginalName = p.filename().string();
    std::string sURL = "https://" + GetSporkValue("bms");
    boost::filesystem::path pathDir = sDir;
    int iFileSize = GETFILESIZE(sLocalPath);
    if (iFileSize < 1) {
        d.ErrorCode = "FILE_MISSING";
        return d;
    }
    // Calculation
    CAmount nFee = CalculateIPFSFee(iTargetDensity, nDurationDays, iFileSize);
    if (nFee / COIN < 1) {
        d.ErrorCode = "FEE_ERROR";
        return d;
    }
    d.nFee = nFee;
    d.nSize = iFileSize;
    int iTotalParts = -1;
    int iPort = SSL_PORT;
    std::string sPage = "UnchainedUpload";
    int MAX_SPLITTER_PARTS = 7000;
    for (int i = 0; i < MAX_SPLITTER_PARTS; i++) {
        std::string sPartial = RoundToString(i, 0) + ".dat";
        boost::filesystem::path pPath = pathDir / sPartial;
        int iFileSize = GETFILESIZE(pPath.string());
        if (iFileSize > 0) {
            iTotalParts = i;
        } else {
            break;
        }
    }

    for (int i = 0; i <= iTotalParts; i++) {
        std::string sPartial = RoundToString(i, 0) + ".dat";
        boost::filesystem::path pPath = pathDir / sPartial;
        int iFileSize = GETFILESIZE(pPath.string());
        if (iFileSize > 0) {
            LogPrintf(" Submitting # %f", i);
            DACResult dInd;
            if (!fDryRun) {
                // ToDo - ensure WebPath is robust enough to handle the Name+Orig Name
                dInd = SubmitIPFSPart(iPort, sWebPath, sTXID, sURL, sPage, sOriginalName, pPath.string(), i, iTotalParts, iTargetDensity, nDurationDays, fEncrypted, nFee);
            }

            std::string sStatus = ExtractXML(dInd.Response, "<status>", "</status>");
            std::string out_URL = ExtractXML(dInd.Response, "<url>", "</url>");
            double nStatus = cdbl(sStatus, 0);
            if (fDryRun)
                nStatus = 1;

            if (nStatus != 1) {
                bool fResult = RelinquishSpace(sLocalPath);
                d.fError = true;
                d.ErrorCode = "ERROR_IN_" + RoundToString(i, 0);
                return d;
            }
            if (i == iTotalParts) {
                RelinquishSpace(sLocalPath);
                d.Response = out_URL;
                d.TXID = sTXID + "-" + RetrieveMd5(sLocalPath);

                IPFSTransaction t1;
                t1.File = sLocalPath;
                t1.Response = d.Response;
                t1.nFee = d.nFee;
                t1.nSize = d.nSize;
                t1.ErrorCode = d.ErrorCode;
                t1.TXID = d.TXID;

                for (int i = 0; i < iTargetDensity; i++) {
                    std::string sRegionName = "<url" + RoundToString(i, 0) + ">";
                    std::string sSuffix = "</url" + RoundToString(i, 0) + ">";
                    std::string sStorageURL = ExtractXML(dInd.Response, sRegionName, sSuffix);
                    if (!sStorageURL.empty())
                        t1.mapRegions.insert(std::make_pair("region_" + RoundToString(i, 0), sStorageURL));
                }

                d.mapResponses.insert(std::make_pair(d.TXID, t1));
                d.fError = false;
                if (fDryRun)
                    d.Response = sOriginalName;
                return d;
            }
        }
    }
    RelinquishSpace(sLocalPath);
    d.fError = true;
    d.ErrorCode = "NOTHING_TO_PROCESS";
    return d;
}


DACResult BIPFS_UploadFolder(std::string sDirPath, std::string sWebPath, std::string sTXID, int iTargetDensity, int nDurationDays, bool fDryRun, bool fEncrypted)
{
    std::vector<std::string> skipList;
    std::vector<std::string> g = GetVectorOfFilesInDirectory(sDirPath, skipList);
    std::string sOut;
    DACResult dOverall;
    for (auto sFileName : g) {
        std::string sRelativeFileName = strReplace(sFileName, sDirPath, "");
        std::string sFullWebPath = Path_Combine(sWebPath, sRelativeFileName);
        std::string sFullSourcePath = Path_Combine(sDirPath, sFileName);
        LogPrintf("BIPFS_UploadFolder::Iterated Filename %s, RelativeFile %s, FullWebPath %s",
            sFileName.c_str(), sRelativeFileName.c_str(), sFullWebPath.c_str());
        DACResult dInd = BIPFS_UploadFile(sFullSourcePath, sWebPath, sTXID, iTargetDensity, nDurationDays, fDryRun, fEncrypted);
        if (dInd.fError) {
            return dInd;
        } else {
            dOverall.nFee += dInd.nFee;
            dOverall.nSize += dInd.nSize;
        }

        dOverall.mapResponses.insert(std::make_pair(dInd.TXID, dInd.mapResponses[dInd.TXID]));
    }
    dOverall.Response = sOut;
    dOverall.fError = false;
    return dOverall;
}

std::string GetHowey(bool fRPC, bool fBurn)
{
    std::string sPrefix = !fRPC ? "clicking [YES]," : "typing I_AGREE in uppercase,";
    std::string sAction;
    std::string sAction2;
    if (fBurn) {
        sAction = "BURN";
        sAction2 = "BURNING";
    } else {
        sAction = "STAKE";
        sAction2 = "STAKING";
    }

    std::string sHowey = "By " + sPrefix + " you agree to the following conditions:"
                                           "\n1.  I AM MAKING A SELF DIRECTED DECISION TO " +
                         sAction2 + " THESE COINS, AND DO NOT EXPECT AN INCREASE IN VALUE."
                                    "\n2.  I HAVE NOT BEEN PROMISED A PROFIT, AND THIS ACTION IS NOT PROMISING ME ANY HOPES OF PROFIT IN ANY WAY NOR IS THE COMMUNITY OR ORGANIZATION."
                                    "\n3.  " +
                         CURRENCY_NAME + " IS NOT ACTING AS A COMMON ENTERPRISE OR THIRD PARTY IN THIS ENDEAVOR."
                                         "\n4.  I HOLD " +
                         CURRENCY_NAME + " AS A HARMLESS UTILITY."
                                         "\n5.  I REALIZE I AM RISKING 100% OF MY CRYPTO-HOLDINGS BY " +
                         sAction2 + " THEM, AND " + CURRENCY_NAME + " IS NOT OBLIGATED TO REFUND MY CRYPTO-HOLDINGS OR GIVE ME ANY REWARD.";
    return sHowey;
}


std::string GenerateXMLSignature(std::string sPrimaryKey, std::string sSigningPublicKey)
{
    std::string sBBPSig;
    std::string sXML;
    std::string sError;
    LogPrintf("\n%f", 8109);

    bool bSigned = SignStake(sSigningPublicKey, sPrimaryKey, sError, sBBPSig);
    LogPrintf("\n%f", 8110);

    sXML = "<BOSIGNER>" + sSigningPublicKey + "</BOSIGNER><BOSIG>" + sBBPSig + "</BOSIG><BOMSG>" + sPrimaryKey + "</BOMSG>";
    return sXML;
}

DACResult SendPayment(Payment p)
{
    const Consensus::Params& consensusParams = Params().GetConsensus();
    DACResult dr;

    if (!ValidateAddress2(p.sFromAddress) || !ValidateAddress2(p.sToAddress)) {
        dr.ErrorCode = "Invalid Invoice Address or invalid source cpk address.";
        return dr;
    }
    dr.TXID = RPCSendMessage(p.nAmount, p.sToAddress, false, dr.ErrorCode, p.ToXML());
    return dr;
}

DACResult SendDSQL(UniValue& oDSQLObject, std::string sTable, std::string ID)
{
    const Consensus::Params& consensusParams = Params().GetConsensus();
    DACResult dr;
    CDSQLQuery qe;
    qe.sData = oDSQLObject.write().c_str();
    qe.nTime = GetAdjustedTime();
    qe.sOwnerAddress = DefaultRecAddress("Christian-Public-Key");
    qe.sTable = sTable;
    qe.sID = ID;
    // Push back required fields
    oDSQLObject.push_back(Pair("time", qe.nTime));
    oDSQLObject.push_back(Pair("id", qe.sID));
    oDSQLObject.push_back(Pair("table", sTable));

    dr.TXID = RPCSendMessage(1 * COIN, consensusParams.BurnAddress, false, dr.ErrorCode, qe.ToXML());
    return dr;
}

DACResult SendInvoice(Invoice i)
{
    DACResult d;

    if (!ValidateAddress2(i.sFromAddress) || !ValidateAddress2(i.sToAddress)) {
        d.ErrorCode = "Invalid Invoice Address or invalid source cpk address.";
        return d;
    }

    d.TXID = RPCSendMessage(1 * COIN, i.sToAddress, false, d.ErrorCode, i.ToXML());
    return d;
}

std::string FormatURL(std::string URL, int iPart)
{
    if (URL.empty())
        return std::string();
    std::vector<std::string> vInput = Split(URL.c_str(), "/");
    if (vInput.size() < 4)
        return std::string();
    std::string sDomain = vInput[0] + "//" + vInput[2];
    std::string sPage;
    for (int i = 3; i < (int)vInput.size(); i++) {
        sPage += vInput[i];
        if (i < (int)vInput.size() - 1)
            sPage += "/";
    }
    if (iPart == 0)
        return sDomain;

    if (iPart == 1)
        return sPage;
    return URL;
}

bool IntToBool(int nValue)
{
    if (nValue == 1) {
        return true;
    } else
        return false;
}

void ProcessSidechainData(std::string sData, int nSyncHeight)
{
    const CChainParams& chainparams = Params();
    std::vector<std::string> vInput = Split(sData.c_str(), "<data>");
    for (int i = 0; i < (int)vInput.size(); i++) {
        std::vector<std::string> vDataRow = Split(vInput[i].c_str(), "[~]");
        if (vDataRow.size() > 10) {
            IPFSTransaction i;
            i.TXID = vDataRow[1];
            i.nHeight = (int)cdbl(vDataRow[9], 0);
            i.Network = vDataRow[8];
            if (i.nHeight > 0 && !i.TXID.empty() && i.Network == chainparams.NetworkIDString()) {
                i.BlockHash = vDataRow[0];
                i.FileName = vDataRow[2];
                i.nFee = cdbl(vDataRow[3], 2) * COIN;
                i.URL = vDataRow[4];
                i.CPK = vDataRow[5];
                i.nDuration = cdbl(vDataRow[6], 0);
                i.nDensity = (int)cdbl(vDataRow[7], 0);
                i.nSize = cdbl(vDataRow[10], 0);
                mapSidechainTransactions[i.TXID] = i;
                if (i.nHeight > nSideChainHeight)
                    nSideChainHeight = i.nHeight;
            }
        }
    }
}

RSAKey GetMyRSAKey()
{
    std::string sPubPath = GetSANDirectory1() + "pubkey.pub";
    std::string sPrivPath = GetSANDirectory1() + "privkey.priv";
    int64_t nPub = GETFILESIZE(sPubPath);
    int64_t nPriv = GETFILESIZE(sPrivPath);
    RSAKey RSA;
    RSA.Valid = false;

    if (nPub <= 0 || nPriv <= 0) {
        int i = RSA_GENERATE_KEYPAIR(sPubPath, sPrivPath);
        if (i != 1) {
            RSA.Error = "Unable to generate new keypair";
            return RSA;
        }
    }

    std::vector<char> bPub = ReadAllBytesFromFile(sPubPath.c_str());
    std::vector<char> bPriv = ReadAllBytesFromFile(sPrivPath.c_str());
    if (bPub.size() < 400 || bPriv.size() < 1700) {
        RSA.Error = "Public key or private key corrupted.";
        return RSA;
    }
    RSA.PublicKey = std::string(bPub.begin(), bPub.end());
    RSA.PrivateKey = std::string(bPriv.begin(), bPriv.end());
    RSA.Valid = true;
    return RSA;
}

RSAKey GetTestRSAKey()
{
    // Note:  We can remove this function once we go to prod.  This just lets the devs test a prod RSA key against a second prod RSA key (simulating user->user encrypted traffic).
    std::string sPubPath = GetSANDirectory1() + "pubkeytest.pub";
    std::string sPrivPath = GetSANDirectory1() + "privkeytest.priv";
    int64_t nPub = GETFILESIZE(sPubPath);
    int64_t nPriv = GETFILESIZE(sPrivPath);
    RSAKey RSA;
    RSA.Valid = false;

    if (nPub <= 0 || nPriv <= 0) {
        int i = RSA_GENERATE_KEYPAIR(sPubPath, sPrivPath);
        if (i != 1) {
            RSA.Error = "Unable to generate new keypair";
            return RSA;
        }
    }

    std::vector<char> bPub = ReadAllBytesFromFile(sPubPath.c_str());
    std::vector<char> bPriv = ReadAllBytesFromFile(sPrivPath.c_str());
    if (bPub.size() < 400 || bPriv.size() < 1700) {
        RSA.Error = "Public key or private key corrupted.";
        return RSA;
    }
    RSA.PublicKey = std::string(bPub.begin(), bPub.end());
    RSA.PrivateKey = std::string(bPriv.begin(), bPriv.end());
    RSA.Valid = true;
    return RSA;
}

UTXOStake GetUTXOStakeByAddress(std::string Address)
{
    std::vector<UTXOStake> uStakes = GetUTXOStakes();
    UTXOStake e;
    for (int i = 0; i < uStakes.size(); i++) {
        UTXOStake d = uStakes[i];
        if (d.found && (d.Address == Address))
            return d;
    }
    return e;
}

std::string Mid(std::string data, int nStart, int nLength)
{
    // Ported from VB6, except this version is 0 based (NOT 1 BASED)
    if (nStart > data.length()) {
        return std::string();
    }

    int nNewLength = nLength;
    int nEndPos = nLength + nStart;
    if (nEndPos > data.length()) {
        nNewLength = data.length() - nStart;
    }
    if (nNewLength < 1)
        return "";

    std::string sOut = data.substr(nStart, nNewLength);
    if (sOut.length() > nLength) {
        sOut = sOut.substr(0, nLength);
    }
    return sOut;
}

void SendEmail(CEmail email)
{
    int nSent = 0;
    g_connman->ForEachNode([&email, &nSent](CNode* pnode) {
        email.RelayTo(pnode, *g_connman);
    });

    email.ProcessEmail();
}

bool PayEmailFees(CEmail email)
{
    // This gets called when the user authorizes biblepay to pay the network fees to forward (and store) a new e-mail for 30 days.
    std::string sError;
    std::string sExtraData;
    std::string sData = "<email>" + email.GetHash().GetHex() + "</email><size>" + RoundToString(email.Body.length(), 0) + "</size><from>" + email.FromEmail + "</from><to>" + email.ToEmail + "</to>";
    double nAmount = email.Body.length() * .025;
    if (nAmount < 100)
        nAmount = 100;
    std::string sResult = SendBlockchainMessage("EMAIL", email.GetHash().GetHex(), sData, nAmount, 2, sExtraData, sError);
    LogPrintf("\nSMTP::PayEmailFees Sending Email with transport fee %f %s %s", nAmount, sResult, sError);

    bool fValid = sError.empty() && !sResult.empty();
    return fValid;
}

void WriteUnsignedBytesToFile(char const* filename, std::vector<unsigned char> outchar)
{
    FILE* file = fopen(filename, "w+");
    int bytes_written = fwrite(&outchar, sizeof(unsigned char), outchar.size(), file);
    fclose(file);
}

double GetDACDonationsByRange(int nStartHeight, int nRange)
{
    double nTotal = 0;
    int nEndHeight = nStartHeight + nRange;
    for (auto ii : mvApplicationCache) {
        if (Contains(ii.first, "DAC-DONATION") || Contains(ii.first, "DAC-WHALEMATCH")) {
            std::pair<std::string, int64_t> v = mvApplicationCache[ii.first];
            int64_t nTimestamp = v.second;
            double nAmt = cdbl(ExtractXML(v.first, "<amount>", "</amount>"), 2);
            double nHeight = cdbl(ExtractXML(v.first, "<height>", "</height>"), 0);
            if (nHeight >= nStartHeight && nHeight <= nEndHeight)
                nTotal += nAmt;
        }
    }
    return nTotal;
}

static UserRecord myUserRecord;
UserRecord GetMyUserRecord()
{
    if (myUserRecord.Found) {
        return myUserRecord;
    }
    // Note this function can SEGFAULT if this is loaded before the wallet boots.  So, we now call this at a later stage in init.cpp
    std::string sCPK = DefaultRecAddress("Christian-Public-Key");
    myUserRecord = GetUserRecord(sCPK);
    return myUserRecord;
}

bool WriteDataToFile(std::string sPath, std::string data)
{
    std::ofstream fd(sPath.c_str(), std::ios::binary);
    fd.write((const char*)data.c_str(), data.length());
    fd.close();
    return true;
}

CCriticalSection cs_mapUTXOStatus;
std::vector<SimpleUTXO> GetUTXOStatus(std::string sAddress)
{
    LOCK(cs_mapUTXOStatus);
    {
        return mapUTXOStatus[sAddress];
    }
}

int AssimilateUTXO(UTXOStake d, int nConfiguration)
{
    int nStatus = 0;
    std::string sError;
    CAmount nBBPAmount = 0;
    std::string sErr;
    mapUTXOStatus[d.Address] = QueryUTXOList(d.Ticker, d.Address, d.Time, nConfiguration);
    for (auto& su : mapUTXOStatus[d.Address]) {
        su.AssimilationTime = GetAdjustedTime();
    }
    return 1;
}

CAmount GetBBPSizeFromPortfolio(std::string sCPK)
{
    std::vector<UTXOStake> uStakes = GetUTXOStakes(true);

    CAmount nSize = 0;
    for (auto d : uStakes) {
        if (d.CPK == sCPK) {
            nSize += d.nNativeTotal;
        }
    }
    return nSize;
}

std::string GetUTXOSummary(std::string sCPK, CAmount& nBBPQuantity)
{
    if (sCPK.empty())
        return "";

    std::map<std::string, std::string> mapTickers;
    CWallet* const pwallet = GetWalletForGenericRequest();

    std::vector<UTXOStake> uStakes = GetUTXOStakes(true);
    std::vector<ReferralCode> uRC = GetClaimedReferralCodes();
    std::vector<ReferralCode> uGRC = GetGeneratedReferralCodes();
    UniValue details;
    ReferralCode rc1 = GetTotalPortfolioImpactFromReferralCodes(uGRC, uRC, uStakes, sCPK, details);

    double nTotal = 0;
    int nCount = 0;
    CAmount nForeignQuantity = 0;
    double nForeignValue = 0;
    double nBBPValue = 0;
    double nBBPCount = 0;
    double nForeignCount = 0;
    std::string sBBPQuants;
    std::string sForeignQuants;
    double nTotalStakeWeight = 0;
    double nBBPStakeWeight = 0;
    double nForeignStakeWeight = 0;
    for (auto d : uStakes) {
        if (d.CPK == sCPK) {
            mapTickers[d.Ticker] = d.Ticker;
            nCount++;
            nTotalStakeWeight += d.nValueUSD;
            nBBPQuantity += d.nNativeTotal;
        }
    }
    std::string sTickers;
    for (auto tick : mapTickers) {
        sTickers += tick.first + ", ";
    }
    sTickers = Mid(sTickers, 0, sTickers.length() - 2);

    // DWU
    double nDWU = CalculateUTXOReward();

    std::string sSummary = "Tickers: " + sTickers + "<br>Total Stake Count: " + RoundToString(nCount, 0);
    sSummary += "<br>Total Stake Weight: " + RoundToString(nTotalStakeWeight, 2) + "<br>Portfolio DWU: " + RoundToString(nDWU * 100, 2) + "<br>Referral Rewards: " + RoundToString(rc1.ReferralRewards, 10) + "</br>";
    sSummary += "<br>Gifts: " + RoundToString(rc1.dGiftAmount, 2) + "</br><br>Gift Details: " + rc1.GiftDetails + "</br>";
    sSummary += "<br>Referral BBP Claimed: " + RoundToString((double)rc1.TotalClaimed / COIN, 2) + "<br>Referral BBP Earned: " + RoundToString((double)rc1.TotalEarned / COIN, 2);

    return sSummary;
}

std::string ScanBlockForNewUTXO(const CBlock& block)
{
    CAmount nMinAmount = 10000;

    for (unsigned int n = 0; n < block.vtx.size(); n++) {
        if (block.vtx[n]->vout.size() > 0) {
            std::string sUTXO = ExtractXML(block.vtx[n]->vout[0].sTxOutMessage, "<utxostake>", "</utxostake>");
            std::string sBBPUTXO = ExtractXML(sUTXO, "<bbputxo>", "</bbputxo>");
            if (!sUTXO.empty() && !sBBPUTXO.empty()) {
                CAmount nValue = 0;
                std::string sErr;
                std::string sBBPAddress = GetUTXO(sUTXO, 0, nValue, sErr);
                CTransactionRef tx = block.vtx[n];
                double nVinAge = GetVINAge2(block.GetBlockTime(), tx, nMinAmount, false);
                // We only pay the mining reward on UTXOs with 24 hours+ of coin age
                if (nValue > 0 && nVinAge > 1 && !sBBPAddress.empty()) {
                    if (ValidateAddress2(sBBPAddress))
                        return sBBPAddress;
                }
            }
        }
    }
    return std::string();
}

double SumUTXO(CAmount& nTotalNative)
{
    nTotalNative = 0;

    double nLast = cdbl(ReadCacheWithMaxAge("sumutxo", "all", (60 * 30)), 2);
    if (nLast > 0) {
        nTotalNative = cdbl(ReadCacheWithMaxAge("sumutxo", "totalnative", (60 * 30)), 2) * COIN;
        return nLast;
    }

    std::vector<UTXOStake> uStakes = GetUTXOStakes(true);
    double nTotal = 0;
    int nCount = 0;
    for (auto d : uStakes) {
        nCount++;
        nTotal += d.nValueUSD;
        nTotalNative += d.nNativeTotal;
    }
    WriteCache("sumutxo", "all", RoundToString(nTotal, 2), GetAdjustedTime());
    WriteCache("sumutxo", "totalnative", AmountToString(nTotalNative), GetAdjustedTime());
    return nTotal;
}

double CalculateUTXOReward()
{
    int iNextSuperblock = 0;
    int iLastSuperblock = GetLastGSCSuperblockHeight(chainActive.Tip()->nHeight, iNextSuperblock);
    CAmount nPaymentsLimit = CSuperblock::GetPaymentsLimit(iNextSuperblock, false);
    CAmount nNativeTotal = 0;
    double nBTCPrice = GetCryptoPrice("btc");
    double nBBPPrice = GetCryptoPrice("bbp");
    double nUSDBBP = nBTCPrice * nBBPPrice;
    double nAnnualReward = (nPaymentsLimit / COIN) * 365 * nUSDBBP;
    double nTotal = SumUTXO(nNativeTotal);
    double nGlobalBBPPortfolio = AmountToDouble(nNativeTotal) * nUSDBBP;
    double nDWU = nAnnualReward / (nGlobalBBPPortfolio + .01);
    LogPrintf("\nReward %f, Total %f, DWU %f, USDBBP %f ", nAnnualReward, (double)nNativeTotal/COIN, nDWU, nUSDBBP);
    if (nDWU > 2.0)
        nDWU = 2.0;

    return nDWU;
}

std::string strReplace(std::string str_input, std::string str_to_find, std::string str_to_replace_with)
{
    boost::replace_all(str_input, str_to_find, str_to_replace_with);
    return str_input;
}

int HexToInteger2(const std::string& hex)
{
    int x = 0;
    std::stringstream ss;
    ss << std::hex << hex;
    ss >> x;
    return x;
}

double ConvertHexToDouble2(std::string hex)
{
    int d = HexToInteger2(hex);
    double dOut = (double)d;
    return dOut;
}


double AddressToPin(std::string sAddress)
{
    if (sAddress.length() < 20)
        return -1;

    std::string sHash = RetrieveMd5(sAddress);
    std::string sMath5 = sHash.substr(0, 5); // 0 - 1,048,575
    double d = ConvertHexToDouble2("0x" + sMath5) / 11.6508;

    int nMin = 10000;
    int nMax = 99999;
    d += nMin;

    if (d > nMax)
        d = nMax;

    d = std::floor(d);
    return d;

    // Why a 5 digit pin?  Looking at the allowable suffix size (digits of scale after the decimal point), we have 8 in scale to work with.  Since BTC is currently at $32,000 per coin as of Jan 2021, doing a 6 digit pin would mean the stake overhead would be at least $250 before the user even uses their first $1 of stake value (not very appetizing).
    // So we move down to a 5 digit pin, and then the stake suffix consumes about $22 of value (affordable for most people).
    // Note that this monetary overhead is not actually *lost*, it is given right back to the user, but this issue is more of a convenience issue (people dont want to tie up $225 just to test a $2 stake).
    // With 5 digits, they can tie up $1-$22 to test a stake.
}

std::vector<DACResult> GetDataListVector(std::string sType, int nDays)
{
    std::vector<DACResult> vec;
    boost::to_upper(sType);
    for (auto ii : mvApplicationCache) {
        if (Contains(ii.first, sType)) {
            std::pair<std::string, int64_t> v = mvApplicationCache[ii.first];
            int64_t nTimestamp = v.second;
            int nElapsedDays = (GetAdjustedTime() - nTimestamp) / 86400;
            if ((nTimestamp > 0 && nDays == 0) || (nElapsedDays <= nDays && nTimestamp > 0 && nDays != 0)) {
                DACResult d;
                d.PrimaryKey = GetElement(ii.first, "[-]", 1);
                d.Response = v.first;
                d.nTime = nTimestamp;
                bool fPush = true;
                if (sType == "memorizer" && v.first == "0")
                    fPush = false;
                if (fPush)
                    vec.push_back(d);
            }
        }
    }
    return vec;
}

PriceQuote GetPriceQuote(std::string sForeignSymbol, CAmount xBBPQty, CAmount xForeignQty)
{
    PriceQuote p;
    p.nBBPQty = xBBPQty;
    p.nForeignQty = xForeignQty;
    p.nForeignPrice = GetCryptoPrice(sForeignSymbol);
    p.nBTCPrice = GetCryptoPrice("btc");
    p.nBBPPrice = GetCryptoPrice("bbp");
    p.nUSDBBP = p.nBTCPrice * p.nBBPPrice;
    p.nUSDForeign = p.nBTCPrice * p.nForeignPrice;
    if (boost::iequals(sForeignSymbol, "BTC"))
        p.nUSDForeign = p.nBTCPrice;
    p.nBBPValueUSD = p.nUSDBBP * ((double)p.nBBPQty / COIN);
    p.nForeignValueUSD = p.nUSDForeign * ((double)p.nForeignQty / COIN);
    return p;
}

void AppendStorageFile(std::string sDataStoreName, std::string sData)
{
    std::string sSuffix = fProd ? "_prod" : "_testnet";
    std::string sTarget = GetSANDirectory1() + sDataStoreName + sSuffix;
    FILE* outFile = fopen(sTarget.c_str(), "a");
    std::string sRow = sData + "\r\n";
    fputs(sRow.c_str(), outFile);
    fclose(outFile);
}

bool findStringCaseInsensitive(const std::string& strHaystack, const std::string& strNeedle)
{
    auto it = std::search(
        strHaystack.begin(), strHaystack.end(),
        strNeedle.begin(), strNeedle.end(),
        [](char ch1, char ch2) { return std::toupper(ch1) == std::toupper(ch2); });
    return (it != strHaystack.end());
}

bool HashExistsInDataFile(std::string sDataStoreName, std::string sHash)
{
    std::string sSuffix = fProd ? "_prod" : "_testnet";
    std::string sTarget = GetSANDirectory1() + sDataStoreName + sSuffix;
    int iFileSize = GETFILESIZE(sTarget);
    if (iFileSize < 1)
        return false;

    boost::filesystem::path pathIn(sTarget);
    std::ifstream streamIn;
    streamIn.open(pathIn.string().c_str());
    if (!streamIn)
        return false;
    std::string line;
    int iRows = 0;
    while (std::getline(streamIn, line)) {
        if (findStringCaseInsensitive(line, sHash)) {
            streamIn.close();
            return true;
        }
    }
    streamIn.close();
    return false;
}

std::string GetPopUpVerses(std::string sRange)
{
    std::vector<std::string> vR = Split(sRange.c_str(), " ");
    if (vR.size() < 2)
        return "";

    std::string sBook = vR[0];
    std::string sChapterRange = vR[1];
    std::vector<std::string> vChap = Split(sChapterRange.c_str(), ":");
    if (vChap.size() < 2)
        return "";

    double nChapter = cdbl(vChap[0], 0);
    if (nChapter < 1)
        return "";
    std::vector<std::string> vChapRange = Split(vChap[1].c_str(), "-");

    if (vChapRange.size() < 2) {
        vChap[1] = vChap[1] + "-" + vChap[1];
        vChapRange = Split(vChap[1].c_str(), "-");
    }
    double nVerseStart = cdbl(vChapRange[0], 0);
    double nVerseEnd = cdbl(vChapRange[1], 0);
    if (nVerseStart < 1 || nVerseEnd < 1)
        return "";

    if (sBook == "I Corinthians")
        sBook = "1 Corinthians"; // Harvest Time format->KJV format
    if (sBook == "I John")
        sBook = "1 John";
    if (sBook == "Corinthians")
        sBook = "1 Corinthians";

    std::string sShortBook = GetBookByName(sBook);

    int iStart = 0;
    int iEnd = 0;
    GetBookStartEnd(sShortBook, iStart, iEnd);
    std::string sTotalVerses = sRange + "\r\n";
    LogPrintf("\r\nShort Book %s, chapter %f st %f, end %f", sShortBook, nChapter, iStart, iEnd);

    if (nVerseEnd < nVerseStart)
        nVerseEnd = nVerseStart;
    for (int j = nVerseStart; j <= nVerseEnd; j++) {
        std::string sVerse = GetVerseML("EN", sShortBook, nChapter, j, iStart - 1, iEnd);
        sTotalVerses += sVerse + "\r\n";
    }
    return sTotalVerses;
}


std::string ToYesNo(bool bValue)
{
    std::string sYesNo = bValue ? "Yes" : "No";
    return sYesNo;
}

std::string SendBlockchainMessage(std::string sType, std::string sPrimaryKey, std::string sValue, double dStorageFee, int nSign, std::string sExtraPayload, std::string& sError)
{
    const Consensus::Params& consensusParams = Params().GetConsensus();
    std::string sAddress = consensusParams.FoundationAddress;
    CAmount nAmount = dStorageFee * COIN;
    CAmount nMinimumBalance = dStorageFee * COIN;
    CWalletTx wtx;
    boost::to_upper(sPrimaryKey); // DC Message can't be found if not uppercase
    boost::to_upper(sType);
    std::string sNonceValue = RoundToString(GetAdjustedTime(), 0);
    std::string sMessageType = "<MT>" + sType + "</MT>";
    std::string sMessageKey = "<MK>" + sPrimaryKey + "</MK>";
    std::string sMessageValue = "<MV>" + sValue + "</MV>";
    std::string sNonce = "<NONCE>" + sNonceValue + "</NONCE>";
    std::string sMessageSig = "";
    std::string sSignature = "";

    if (nSign == 1) {
        // Sign as if this is a spork
        bool bSigned = SignStake(consensusParams.FoundationAddress, sValue + sNonceValue, sError, sSignature);
        if (bSigned) {
            sMessageSig = "<SPORKSIG>" + sSignature + "</SPORKSIG>";
            sMessageSig += "<BOSIG>" + sSignature + "</BOSIG>";
            sMessageSig += "<BOSIGNER>" + consensusParams.FoundationAddress + "</BOSIGNER>";
        }
        if (!bSigned)
            LogPrintf("Unable to sign spork %s ", sError);
        LogPrintf(" Signing Nonce%f , With spork Sig %s on message %s  \n", (double)GetAdjustedTime(),
            sMessageSig.c_str(), sValue.c_str());
    } else if (nSign == 2) {
        // Sign as if this is a business object
        std::string sSignCPK = DefaultRecAddress("Christian-Public-Key");

        bool bSigned = SignStake(sSignCPK, sValue + sNonceValue, sError, sSignature);
        if (bSigned) {
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
    CScript scriptPubKey = GetScriptForDestination(DecodeDestination(sAddress));

    bool fSent = RPCSendMoney(sError, scriptPubKey, nAmount, fSubtractFee, wtx, fInstantSend, s1);

    if (!sError.empty())
        return std::string();
    return wtx.GetHash().GetHex().c_str();
}

double GetCryptoPrice(std::string sSymbol)
{
    if (sSymbol.empty())
        return 0;

    boost::to_lower(sSymbol);
    std::string sSymbolUpper = sSymbol;
    boost::to_upper(sSymbolUpper);
    double nLast = cdbl(ReadCacheWithMaxAge("price", sSymbol, (60 * 30)), 12);
    if (nLast > 0)
        return nLast;
    std::string sServer = "Server?action=" + sSymbolUpper + "_PRICE_QUOTE";
    std::string sC1 = Uplink(false, "", GetSporkValue("bms"), sServer, SSL_PORT, 15, 1);
    std::string sPrice = ExtractXML(sC1, "<MIDPOINT>", "</MIDPOINT>");
    double dMid = cdbl(sPrice, 12);
    WriteCache("price", sSymbol, RoundToString(dMid, 12), GetAdjustedTime());
    return dMid;
}

double GetPBase(double& out_BTC, double& out_BBP)
{
    // Get the midpoint of bid-ask in Satoshi * BTC price in USD
    double dBBPPrice = GetCryptoPrice("bbp");
    double dBTC = GetCryptoPrice("btc");
    out_BTC = dBTC;
    out_BBP = dBBPPrice;

    double nBBPOverride = cdbl(GetSporkValue("BBPPRICE"), 12);
    if (!fProd && nBBPOverride > 0) {
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
    CWallet* const pwallet = GetWalletForGenericRequest();

    bool fValid = pwallet->CheckStakeSignature(sSigner, sSignature, sMessage, sError);
    return fValid;
}

bool GetTransactionTimeAndAmount(uint256 txhash, int nVout, int64_t& nTime, CAmount& nAmount)
{
    uint256 hashBlock = uint256();
    CTransactionRef tx2;
    if (GetTransaction(txhash, tx2, Params().GetConsensus(), hashBlock, true)) {
        BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
        if (mi != mapBlockIndex.end() && (*mi).second) {
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
    std::string sPadding = std::string(iPadding, ' ');
    std::string sOut = data + sPadding;
    return sOut;
}

int64_t GetFileAge(std::string sPath)
{
    boost::filesystem::path pathFiltered(sPath);
    if (!boost::filesystem::exists(pathFiltered))
        return GetAdjustedTime() - 0;
    int64_t nTime = last_write_time(pathFiltered);
    int64_t nAge = GetAdjustedTime() - nTime;
    return nAge;
}

bool CreateLegacyGSCTransmission(CAmount nAmount, std::string sAddress, std::string sCampaign, std::string sGobjectID, std::string sOutcome, std::string sDiary, std::string& sError)
{
    std::string sSignature;
    std::string sCPK = DefaultRecAddress("Christian-Public-Key");
    std::string sXML = "<MT>GSCTransmission</MT><gscsig>" + sSignature + "</gscsig><gobject>" + sGobjectID + "</gobject><outcome>" + sOutcome + "</outcome><abncpk>" + sCPK + "</abncpk><gsccampaign>" + sCampaign + "</gsccampaign><diary>" + sDiary + "</diary>";
    const Consensus::Params& consensusParams = Params().GetConsensus();
    std::string sTXID = RPCSendMessage(nAmount, sAddress, false, sError, sXML);

    if (!sError.empty()) {
        return false;
    }
    return true;
}

bool ValidateAddress2(std::string sAddress)
{
    CTxDestination dest = DecodeDestination(sAddress);
    return IsValidDestination(dest);
}

bool ValidateAddressLength(std::string sAddress, int nReqLength)
{
    int nLen = sAddress.length();
    if (nLen != nReqLength)
        return false;
    return true;
}

bool ValidateAddress3(std::string sTicker, std::string sAddress)
{
    if (sTicker == "BBP") {
        return ValidateAddress2(sAddress);
    }
    if (sAddress.empty())
        return false;

    bool fValidateTicker = ValidateTicker(sTicker);
    if (!fValidateTicker)
        return false;

    if (sTicker == "DASH" || sTicker == "BTC" || sTicker == "DOGE") 
    {
        if (ValidateAddressLength(sAddress, 42))
            return true;
        return ValidateAddressLength(sAddress, 34);
    } 
    else if (sTicker == "LTC") 
    {
        if (ValidateAddressLength(sAddress, 34)) 
        {
            return true;
        }
        else
        {
            return ValidateAddressLength(sAddress, 43);
        }
    }
    else if (sTicker == "ETH" || sTicker == "BCH") 
    {
        return ValidateAddressLength(sAddress, 42);
    }
    else if (sTicker == "XRP") 
    {
        return ValidateAddressLength(sAddress, 34);
    }
    else if (sTicker == "XLM") 
    {
        return ValidateAddressLength(sAddress, 56);
    }
    else if (sTicker == "ZEC") 
    {
        return ValidateAddressLength(sAddress, 35);
    }

    return false;
}

bool SignStake(std::string sBitcoinAddress, std::string strMessage, std::string& sError, std::string& sSignature)
{
    CWallet* const pwallet = GetWalletForGenericRequest();
    LOCK2(cs_main, pwallet->cs_wallet);
    {
        if (pwallet->IsLocked()) {
            sError = "Please unlock the wallet first.";
            return false;
        }

        if (!ValidateAddress2(sBitcoinAddress)) {
            sError = "Invalid Address.";
            return false;
        }

        if (strMessage.empty()) {
            sError = "Signing message empty.";
            return false;
        }

        CTxDestination dest = DecodeDestination(sBitcoinAddress);
        const CKeyID* keyID = boost::get<CKeyID>(&dest);
        if (!keyID) {
            sError = "Address does not refer to key";
            return false;
        }
        CKey key;
        if (!pwallet->GetKey(*keyID, key)) {
            sError = "Private key not available";
            return false;
        }

        CHashWriter ss(SER_GETHASH, 0);
        ss << strMessageMagic;
        ss << strMessage;
        std::vector<unsigned char> vchSig;
        if (!key.SignCompact(ss.GetHash(), vchSig)) {
            sError = "Sign failed";
            return false;
        }

        sSignature = EncodeBase64(vchSig.data(), vchSig.size());

        return true;
    }
}

boost::filesystem::path GetDeterministicConfigFile()
{
    boost::filesystem::path pathConfigFile(gArgs.GetArg("-mndeterministicconf", "deterministic.conf"));
    if (!pathConfigFile.is_complete())
        pathConfigFile = GetDataDir() / pathConfigFile;
    return pathConfigFile;
}

boost::filesystem::path GetMasternodeConfigFile()
{
    boost::filesystem::path pathConfigFile(gArgs.GetArg("-mnconf", "masternode.conf"));
    if (!pathConfigFile.is_complete())
        pathConfigFile = GetDataDir() / pathConfigFile;
    return pathConfigFile;
}

CAmount ARM64()
{
    // If biblepay is compiled for ARM64, there is a floating point math problem that results in the block.vtx[0]->GetValueOut() being 1/10000000 satoshi higher than the block subsidy limit (for example actual=596050766864 vs limit=596050766863)
    // To deal with this we are looking in biblepay.conf for the 'arm64=1' setting.
    // If set, we pass a value back that is added to the blockReward for consensus purposes.
    // We are passing back 1 * COIN to allow the Subsidy max to be greater than the value out.
    double dARM = cdbl(gArgs.GetArg("-arm64", "0"), 0);
    CAmount nARM = (dARM == 1) ? 1 * COIN : 0;
    return nARM;
}

bool ARM64Matches(CAmount n1, CAmount n2)
{
	double dARM = cdbl(gArgs.GetArg("-arm64", "0"), 0);
    if (dARM == 0)
	{
		return n1 == n2;
	}
	// Arm section
	if (n1 == n2)
		return true;
	double d1 = AmountToDouble(n1);
	double d2 = AmountToDouble(n2);
 	double d3 = std::abs(d1-d2);
	bool fPass = (d3 < .50);
	return fPass;
}

bool ProcessNFT(NFT& nft, std::string sAction, std::string sBuyerCPK, CAmount nBuyPrice, bool fDryRun, std::string& sError)
{
    // Create, Buy,  Edit
    const Consensus::Params& consensusParams = Params().GetConsensus();
    CWallet* const pwallet = GetWalletForGenericRequest();

    std::string sPK = "NFT-" + nft.GetHash().GetHex();
    std::string sSignature;
    std::string sSellerCPK;
    CScript scriptDestination;
    CAmount nSend = 100 * COIN;
    std::string sToAddress;

    if (sAction == "EDIT") {
        bool bSigned = false;
        bSigned = SignStake(sBuyerCPK, nft.GetHash().GetHex(), sError, sSignature);
        if (!bSigned) {
            sError = "Unable to sign NFT.";
            return false;
        }
        std::string sOwnedBy = NFTOwnedBy(nft);
        if (!sOwnedBy.empty() && sOwnedBy != nft.sCPK) {
            // This is an edit, and the user is trying to edit another users NFT
            sError = "Sorry, you cannot edit an NFT that is not yours.";
            return false;
        }
        sToAddress = consensusParams.BurnAddress;

        bool fOrphan = findStringCaseInsensitive(nft.sType, "orphan");
        if (fOrphan && !nft.fDeleted) {
            sError = "Sorry, you may not edit an orphan.  But you may delete an orphan.";
            return false;
        }
    } else if (sAction == "CREATE") {
        std::string sOwnedBy = NFTOwnedBy(nft);
        if (!sOwnedBy.empty()) {
            sError = "Sorry, this NFT already exists.";
            return false;
        }
        sToAddress = consensusParams.BurnAddress;

    } else if (sAction == "BUY") {
        NFT oldNFT = GetSpecificNFT(nft.GetHash());
        sSellerCPK = oldNFT.sCPK;
        if (!oldNFT.found) {
            sError = "Sorry, we can't locate this NFT.";
            return false;
        }

        if (nBuyPrice < oldNFT.LowestAcceptableAmount()) {
            sError = "Sorry, your buy price is less than the minimum sale amount.";
            return false;
        }
        if (!oldNFT.fMarketable || oldNFT.fDeleted) {
            sError = "Sorry, this NFT is not for sale, or, this orphan is not available for sponsorship.";
            return false;
        }
        nft.fMarketable = false;
        nft.fDeleted = false;
        /*
			nft.nMinimumBidAmount = 0;
			nft.nReserveAmount = 0;
			nft.nBuyItNowAmount = 0;
		*/
        if (!ValidateAddress2(sSellerCPK)) {
            sError = "Invalid seller Address " + sSellerCPK;
            return false;
        }
        sToAddress = sSellerCPK;
        nSend = nBuyPrice;
    }

    if (!ValidateAddress2(sBuyerCPK)) {
        sError = "Invalid owner Address " + sBuyerCPK;
        return false;
    }

    nft.nIteration++;

    std::string sPayload = "<MT>NFT</MT><MK>" + sPK + "</MK><MV><nft><cpk>" + sBuyerCPK + "</cpk><name>" + nft.sName + "</name><description>" 
        + nft.sDescription + "</description><loqualityurl>" + nft.sLoQualityURL + "</loqualityurl><hiqualityurl>" + nft.sHiQualityURL 
        + "</hiqualityurl><deleted>" + (nft.fDeleted ? "1" : "0") + "</deleted><marketable>" + (nft.fMarketable ? "1" : "0") 
        + "</marketable><time>" + RoundToString(GetAdjustedTime(), 0) + "</time><type>" 
        + nft.sType + "</type><iteration>" + RoundToString(nft.nIteration, 0) + "</iteration><minbidamount>" + RoundToString((double)nft.nMinimumBidAmount / COIN, 2) + "</minbidamount>" 
        + "<reserveamount>" + RoundToString((double)nft.nReserveAmount / COIN, 2) + "</reserveamount><buyitnowamount>" + RoundToString((double)nft.nBuyItNowAmount / COIN, 2) 
        + "</buyitnowamount>" + "</nft><BOACTION>" + sAction + "</BOACTION><BOSIGNER>" + sBuyerCPK + "</BOSIGNER><BOSIG>" 
        + sSignature + "</BOSIG><BOMSG>" + nft.GetHash().GetHex() + "</BOMSG></MV>";

    std::string sTXID = RPCSendMessage(nSend, sToAddress, false, sError, sPayload);
    if (!sTXID.empty() && sError.empty()) {
        nft.TXID = uint256S("0x" + sTXID);
        return true;
    } else {
        return false;
    }
    return true;
}

CAmount GetAmountPaidToRecipient(CTransactionRef tx1, std::string sRecipient)
{
    const Consensus::Params& consensusParams = Params().GetConsensus();
    CAmount nPaid = 0;
    for (unsigned int i = 0; i < tx1->vout.size(); i++) {
        std::string sPK = PubKeyToAddress(tx1->vout[i].scriptPubKey);
        if (sPK == sRecipient) {
            nPaid += tx1->vout[i].nValue;
        }
    }
    return nPaid;
}

uint256 CoordToUint256(int row, int col)
{
    std::string sData = Mid("0x" + RoundToString(row, 0) + "00000" + RoundToString(col, 0) + "00000000000000000000000000000000", 0, 32);
    uint256 h = uint256S(sData);
    return h;
}

COutPoint OutPointFromUTXO(std::string sUTXO)
{
    std::vector<std::string> vU = Split(sUTXO.c_str(), "-");
    COutPoint c;
    if (vU.size() < 2)
        return c;

    std::string sHash = vU[0];
    int nOrdinal = (int)cdbl(vU[1], 0);
    c = COutPoint(uint256S(sHash), nOrdinal);
    return c;
}

void LockCoinsInAddress(std::string sAddress)
{
    CWallet* const pwallet = GetWalletForGenericRequest();
    if (!pwallet)
        return;

    std::vector<SimpleUTXO> vS = GetAddressUTXOs_BBP(sAddress);
    for (auto s : vS) {
        double nPin = AddressToPin(sAddress);
        bool fMask = CompareMask2(s.nAmount, nPin);
        if (fMask) {
            std::string sUTXO = s.TXID + "-" + RoundToString(s.nOrdinal, 0);
            COutPoint c = OutPointFromUTXO(sUTXO);
            pwallet->LockCoin(c);
        }
    }
}

void LockUTXOStakes()
{
    CWallet* const pwallet = GetWalletForGenericRequest();
    if (!pwallet)
        return;

    std::vector<UTXOStake> uStakes = GetUTXOStakes();
    LOCK(pwallet->cs_wallet);
    std::string sCPK = DefaultRecAddress("Christian-Public-Key");
    for (auto d : uStakes) {
        if (d.Ticker == "BBP" && d.CPK == sCPK) {
            LockCoinsInAddress(d.Address);
        }
    }
    pwallet->LockByMask(1537);
}

std::string RPCSendMessage(CAmount nAmount, std::string sToAddress, bool fDryRun, std::string& sError, std::string sPayload, std::string sOptFundAddress, CAmount nOptFundAmount)
{
    // Returns the TXID, otherwise an error in sError&
    std::string sTXID;
    bool fSubtractFee = false;
    bool fInstantSend = false;
    CWalletTx wtx;
    // Dry Run step 1:
    std::vector<CRecipient> vec;
    int nChangePosRet = -1;
    if (!ValidateAddress2(sToAddress)) {
        sError = "Invalid destination address.";
        return std::string();
    }
    CWallet* const pwallet = GetWalletForGenericRequest();

    CScript scr = GetScriptForDestination(DecodeDestination(sToAddress));
    // Support for super long tx's:
    double nReq = ceil(sPayload.length() / MAX_BBP_VOUT_MESSAGE_LENGTH);

    CRecipient rec = {scr, nAmount, false, fSubtractFee};
    vec.push_back(rec);
    const Consensus::Params& consensusParams = Params().GetConsensus();
    CAmount nLgMsgFee = 1541 + (1 * COIN); // 1541 is the mask for DSQL
    if (nReq > 1) {
        for (int i = 0; i <= nReq; i++) {
            CScript scrBA = GetScriptForDestination(DecodeDestination(consensusParams.BurnAddress));
            CRecipient rec = {scrBA, nLgMsgFee, false, fSubtractFee};
            vec.push_back(rec);
        }
    }

    if (!sOptFundAddress.empty()) {
        CScript scrOpt = GetScriptForDestination(DecodeDestination(sOptFundAddress));
        CRecipient xrec = {scrOpt, nOptFundAmount, false, fSubtractFee};
        vec.push_back(xrec);
    }

    CAmount nFeeRequired = 0;
    CReserveKey reserveKey(pwallet);
    CCoinControl coinControl;

    bool fCre = pwallet->CreateTransaction(vec, wtx, reserveKey, nFeeRequired, nChangePosRet, sError, coinControl, true, 0, sPayload);
    if (!fCre) {
        sError += "Unable to Create Transaction.";
        return std::string();
    }

    if (!fDryRun) {
        CValidationState state;

        if (!pwallet->CommitTransaction(wtx, reserveKey, g_connman.get(), state)) {
            sError += "Commit failed.";
            return std::string();
        } else {
            return wtx.GetHash().GetHex();
        }
    }
    return std::string();
}

std::string GetTxMessages(uint256 hashTXID)
{
    CTransactionRef tx1;
    std::string sData;
    const Consensus::Params& consensusParams = Params().GetConsensus();

    if (GetTxDAC(hashTXID, tx1)) {
        for (unsigned int i = 0; i < tx1->vout.size(); i++) {
            std::string sPK = PubKeyToAddress(tx1->vout[i].scriptPubKey);
            if (sPK == consensusParams.BurnAddress) {
                sData += tx1->vout[i].sTxOutMessage;
            }
        }
        return sData;
    }
    return std::string();
}

ReferralCode DeserializeReferralCode(std::string sCode)
{
    ReferralCode r;
    if (sCode.empty())
        return r;
    uint256 hashTXID = uint256S(sCode);
    std::string sMsg = GetTxMessages(hashTXID);
    std::string sXML = ExtractXML(sMsg, "<referralcode>", "</referralcode>");
    r.CPK = ExtractXML(sMsg, "<BOSIGNER>", "</BOSIGNER>");
    r.OriginatorCPK = r.CPK;
    std::string sSig = ExtractXML(sMsg, "<BOSIG>", "</BOSIG>");
    std::string sSigMsg = ExtractXML(sMsg, "<BOMSG>", "</BOMSG>");
    r.GiftAmount = cdbl(ExtractXML(sMsg, "<giftamount>", "</giftamount>"), 2) * COIN;
    r.Time = cdbl(ExtractXML(sMsg, "<time>", "</time>"), 0);

    double nExp1 = r.GiftAmount == 0 ? (60 * 60 * 24 * 30 * 12) : (60 * 60 * 24 * 30);
    r.Expiration = r.Time + nExp1;

    CAmount nBBPQty = GetBBPSizeFromPortfolio(r.CPK);
    r.Size = AmountToDouble(nBBPQty) * .10 * COIN; // Eligible for up to 10% of foreign portfolio
    if (GetAdjustedTime() < r.Expiration) {
        r.found = true;
    }
    return r;
}

std::string SendReferralCode(std::string& sError, double nGiftAmount)
{
    const Consensus::Params& consensusParams = Params().GetConsensus();
    std::string sSignature;
    std::string sCPK = DefaultRecAddress("Christian-Public-Key");
    std::string sMsg = RoundToString(GetAdjustedTime(), 0);
    std::string sPK = "REFERRALCODE-" + sCPK;
    if (!ValidateAddress2(sCPK)) {
        sError = "Invalid Referral Address " + sCPK;
        return std::string();
    }
    bool bSigned = false;
    bSigned = SignStake(sCPK, sMsg, sError, sSignature);
    if (!bSigned) {
        sError = "Unabled to sign " + sCPK;
        return std::string();
    }
    CAmount nBBPQty = 0;
    GetUTXOSummary(sCPK, nBBPQty);
    if (nBBPQty < 1) {
        sError = "Sorry, you must have an established portfolio to generate a referral code. ";
        return std::string();
    }
    // People cannot use more than 10% of a portfolio
    std::string sPayload = "<MT>REFERRALCODE</MT><MK>" + sPK + "</MK><MV><referralcode><time>" + RoundToString(GetAdjustedTime(), 0) + "</time><height>" + RoundToString(chainActive.Tip()->nHeight, 0) + "</height><giftamount>" + RoundToString(nGiftAmount, 0) + "</giftamount><cpk>" + sCPK + "</cpk>" + "<BOSIGNER>" + sCPK + "</BOSIGNER><BOSIG>" + sSignature + "</BOSIG><BOMSG>" + sMsg + "</BOMSG></MV></referralcode>";
    std::string sTXID = RPCSendMessage(1 * COIN, consensusParams.BurnAddress, false, sError, sPayload);
    return sTXID;
}

std::string ClaimReferralCode(std::string sCode, std::string& sError)
{
    const Consensus::Params& consensusParams = Params().GetConsensus();
    std::string sSignature;
    std::string sCPK = DefaultRecAddress("Christian-Public-Key");
    std::string sMsg = RoundToString(GetAdjustedTime(), 0);
    std::string sPK = "CLAIMREFERRALCODE-" + sCPK + "-" + sCode;
    if (!ValidateAddress2(sCPK)) {
        sError = "Invalid Claim Address " + sCPK;
        return std::string();
    }
    bool bSigned = false;
    bSigned = SignStake(sCPK, sMsg, sError, sSignature);
    if (!bSigned) {
        sError = "Unable to sign " + sCPK;
        return std::string();
    }

    ReferralCode r = DeserializeReferralCode(sCode);

    if (r.OriginatorCPK.empty()) {
        sError = "Unable to find referral code.";
        return std::string();
    }

    if (GetAdjustedTime() > r.Expiration) {
        sError = "Expired.";
        return std::string();
    }

    if (r.OriginatorCPK == sCPK) {
        sError = "You may not use your own referral code for your own portfolio. ";
        return std::string();
    }

    CAmount nBBPQty = 0;
    GetUTXOSummary(sCPK, nBBPQty);
    if (nBBPQty < 1) {
        sError = "Sorry, you must have an established portfolio to use a referral code.  ";
        return std::string();
    }
    std::string sPayload = "<MT>CLAIMREFERRALCODE</MT><MK>" + sPK + "</MK><MV><claimreferralcode><height>" + RoundToString(chainActive.Tip()->nHeight, 0) + "</height><cpk>" + sCPK + "</cpk>" + "<BOSIGNER>" + sCPK + "</BOSIGNER><BOSIG>" + sSignature + "</BOSIG><originatorcpk>" + r.OriginatorCPK + "</originatorcpk><BOMSG>" + sMsg + "</BOMSG></MV></claimreferralcode>";
    std::string sTXID = RPCSendMessage(1 * COIN, consensusParams.BurnAddress, false, sError, sPayload);
    return sTXID;
}

double GetReferralCodeEffectivity(int nPortfolioTime)
{
    // This function applies a decay factor to provide the 'effectivity' of a referral code.
    // Effectivity is based on the average portfolio age (not the age of the coupon).
    // Referral codes start at 100% effective, and diminish by .0027% per portfolio day (meaning, that they decay fully in one year, based on your avg portfolio date).
    // This means that a brand new portfolio (IE new user) will start at 100% effective, but a user who has a 180 day old portfolio starts at the half-life of the referral code.
    // At your portfolio-age half-life, a referral code is worth .50* (half) of its original effectivity.
    // Referral codes only cover up to 10% of the size of the generators portfolio.
    // As an example, let us hypothesize the referral-generator portfolio is worth 1MM and the subject portfolio is worth 10MM.
    // The referral code will only be applied to 1MM of the positions in the subject portfolio because it is limited by the portfolio size it was generated from.
    // As a second example, if you have 1MM of positions, and the generator has 10MM, the code may be applied to your entire portfolio because the generator has a large enough size.

    int64_t nPortfolioAgeSecs = GetAdjustedTime() - nPortfolioTime;
    double nDays = nPortfolioAgeSecs / 86400;
    // Our decay rate is currently set at two years, meaning in two years, the portfolios are all mature.
    double nDecay = .00135 * nDays;
    if (nDecay > 1)
        nDecay = 1;
    if (nDecay < .01)
        nDecay = .01;
    double nDWUFactor = 1.0 - nDecay;
    double nDWU = .20;
    double nBonus = nDWU * nDWUFactor;
    if (nBonus < .01)
        nBonus = 0;
    if (nBonus > nDWU)
        nBonus = nDWU;
    return nBonus;
}


uint64_t GetPortfolioTimeAndSize(std::vector<UTXOStake>& uStakes, std::string sCPK, CAmount& nBBPSize)
{
    double nAvg = 0;
    double nTotal = 0;
    int nCt = 0;
    nBBPSize = 0;
    for (auto uStake : uStakes) {
        if (boost::iequals(uStake.CPK, sCPK)) {
            nBBPSize += uStake.nNativeTotal;
            nTotal += uStake.Time;
            nCt++;
        }
    }
    nAvg = nTotal / (nCt + .01);
    return (int64_t)nAvg;
}

std::vector<ReferralCode> GetGeneratedReferralCodes()
{
    std::vector<ReferralCode> vRC;
    std::vector<UTXOStake> vU = GetUTXOStakes(true);
    for (auto ii : mvApplicationCache) {
        if (Contains(ii.first, "REFERRALCODE[-]")) {
            std::pair<std::string, int64_t> v = mvApplicationCache[ii.first];
            ReferralCode r;
            std::string sID = GetElement(ii.first, "[-]", 1);
            r.Code = GetElement(sID, "-", 2);
            CAmount nSize = 0;
            ReferralCode rc = DeserializeReferralCode(r.Code);

            //LogPrintf("\n cpk=%s, Size %f, Orig %s, Gift Amount %s,Expiration %f, Found %f",				r.CPK, (double)r.Size/COIN, r.OriginatorCPK, AmountToString(r.GiftAmount), rc.Expiration, rc.found);
            if (rc.found) {
                vRC.push_back(rc);
            }
        }
    }
    return vRC;
}


std::vector<ReferralCode> GetClaimedReferralCodes()
{
    std::vector<ReferralCode> vRC;
    std::vector<UTXOStake> vU = GetUTXOStakes(true);
    for (auto ii : mvApplicationCache) {
        if (Contains(ii.first, "CLAIMREFERRALCODE[-]")) {
            std::pair<std::string, int64_t> v = mvApplicationCache[ii.first];
            std::string sID = GetElement(ii.first, "[-]", 1);
            ReferralCode rClaimed;
            rClaimed.Code = GetElement(sID, "-", 2);
            CAmount nSize = 0;
            ReferralCode rcOriginal = DeserializeReferralCode(rClaimed.Code);
            rClaimed.CPK = ExtractXML(v.first, "<cpk>", "</cpk>");
            rClaimed.OriginatorCPK = rcOriginal.CPK;
            GetPortfolioTimeAndSize(vU, rClaimed.OriginatorCPK, nSize);
            rClaimed.Size = ((double)nSize / COIN) * .10 * COIN;
            rClaimed.GiftAmount = rcOriginal.GiftAmount;
            rClaimed.Time = v.second;
            //LogPrintf("\n cpk=%s, Size %f, Orig %s, Gift Amount %s,Expiration %f, Found %f",				r.CPK, (double)r.Size/COIN, r.OriginatorCPK, AmountToString(r.GiftAmount), rc.Expiration, rc.found);
            if (GetAdjustedTime() < rcOriginal.Expiration && rcOriginal.found) {
                if (!rClaimed.CPK.empty() && !rClaimed.OriginatorCPK.empty() && nSize > 0) {
                    rClaimed.found = true;
                    vRC.push_back(rClaimed);
                }
            }
        }
    }
    return vRC;
}

std::vector<Invoice> GetInvoices()
{
    std::vector<Invoice> vI;
    for (auto ii : mvApplicationCache) {
        if (Contains(ii.first, "INVOICE[-]")) {
            std::pair<std::string, int64_t> v = mvApplicationCache[ii.first];
            Invoice i;
            i.FromXML(v.first);
            if (i.found)
                vI.push_back(i);
        }
    }
    return vI;
}

std::vector<Payment> GetPayments()
{
    std::vector<Payment> vP;
    for (auto ii : mvApplicationCache) {
        if (Contains(ii.first, "PAYMENT[-]")) {
            std::pair<std::string, int64_t> v = mvApplicationCache[ii.first];
            Payment p;
            p.FromXML(v.first);
            if (p.found)
                vP.push_back(p);
        }
    }
    return vP;
}

std::vector<CDSQLQuery> DSQLQuery(std::string sFilter)
{
    std::vector<CDSQLQuery> vSQL;
    for (auto ii : mvApplicationCache) {
        if (Contains(ii.first, "DSQL[-]")) {
            std::pair<std::string, int64_t> v = mvApplicationCache[ii.first];
            bool fContains = Contains(v.first, sFilter);
            if (fContains || sFilter.empty()) {
                CDSQLQuery q;
                q.FromXML(v.first);
                vSQL.push_back(q);
            }
        }
    }
    return vSQL;
}

static std::map<std::string, std::string> mapGiftDetails;

std::map<std::string, double> GetImpactFromReferralCodeGifts(std::vector<ReferralCode>& vGRC, std::vector<ReferralCode>& vCRC, std::vector<UTXOStake>& vU)
{
    // Gifts expire in 30 days; regular referrals expire in 365 days
    // Gifts subtract BBP from one portfolio and gives to the community subset who 'attached' the referral code
    std::map<std::string, double> mapGiftImpact;
    std::map<std::string, CAmount> mapRCClaimed;
    mapGiftDetails.clear();

    double nPR = GetSporkDouble("referralrewards", 0);
    ReferralCode r;
    if (nPR == 86) {
        return mapGiftImpact;
    }

    double nTotalConsumers = 0;
    CAmount nTotalGiftsAvailable = 0;
    // Pre-discover how many gifts are attached here

    for (auto rc : vCRC) {
        if (rc.found && AmountToDouble(rc.GiftAmount) > 0) {
            CAmount nSize = 0;
            GetPortfolioTimeAndSize(vU, rc.OriginatorCPK, nSize);
            // Ensure they still have a portfolio that supports the gift amount
            if (AmountToDouble(nSize) > AmountToDouble(rc.GiftAmount)) {
                nTotalConsumers++;
                boost::to_lower(rc.OriginatorCPK);
                mapRCClaimed[rc.OriginatorCPK] = rc.GiftAmount;
            }
        }
    }
    for (auto claimed : mapRCClaimed) {
        nTotalGiftsAvailable += mapRCClaimed[claimed.first];
    }
    double nAmountAvailPerCPK = AmountToDouble(nTotalGiftsAvailable) / (nTotalConsumers + .01);
    /* LogPrintf("\r\nGIFRCG::TotalGifts %s, TotalConsumers %f, AmountPerCPK %f", AmountToString(nTotalGiftsAvailable), 		nTotalConsumers, nAmountAvailPerCPK); */

    for (auto rc : vCRC) {
        if (rc.found && AmountToDouble(rc.GiftAmount) > 0) {
            CAmount nSize = 0;
            GetPortfolioTimeAndSize(vU, rc.OriginatorCPK, nSize);
            // Originator has a portfolio that supports the gift
            if (AmountToDouble(nSize) > AmountToDouble(rc.GiftAmount)) {
                CAmount nConsumerSize = 0;
                GetPortfolioTimeAndSize(vU, rc.CPK, nConsumerSize);
                double dConsumerSize = AmountToDouble(nConsumerSize);
                double dCapAmount = nAmountAvailPerCPK;
                if (dCapAmount > dConsumerSize)
                    dCapAmount = dConsumerSize;
                // credit rc.cpk, debit rc.originatorcpk
                mapGiftImpact[rc.CPK] += dCapAmount;
                mapGiftImpact[rc.OriginatorCPK] += -1 * dCapAmount;
                std::string sRow = rc.OriginatorCPK + "=" + RoundToString(dCapAmount, 2) + ", ";
                std::string sRow2 = rc.CPK + "=" + RoundToString(-1 * dCapAmount, 2) + ", ";
                mapGiftDetails[rc.CPK] += sRow;
                mapGiftDetails[rc.OriginatorCPK] += sRow2;
                //LogPrintf("\r\nGIFRCG2::cpk %s, gift %f", rc.CPK, dCapAmount);
            }
        }
    }
    return mapGiftImpact;
}


ReferralCode GetTotalPortfolioImpactFromReferralCodes(std::vector<ReferralCode>& vGRC, std::vector<ReferralCode>& vCRC, std::vector<UTXOStake>& vU, std::string sCPK, UniValue& details)
{
    double nPR = GetSporkDouble("referralrewards", 0);
    ReferralCode r;
    if (nPR == 86) {
        r.ReferralRewards = 1.0;
        return r;
    }

    std::map<std::string, double> mapGifts = GetImpactFromReferralCodeGifts(vGRC, vCRC, vU);

    // Given a CPK (this is a pointer to a portfolio), assess the portfolio determining the impact of the collection of referral codes when applied.
    // A user may have multiple referral codes cashed in.  Each one has a different max size based on the originators portfolio.
    // Next the age of this users portfolio determines the effectiveness of each coupon.
    // The coupon originator also gets a portfolio bonus for coupon utilization by others.
    CAmount nBBPSize = 0;
    uint64_t nPortfolioTime = GetPortfolioTimeAndSize(vU, sCPK, nBBPSize);
    r.ReferralEffectivity = GetReferralCodeEffectivity(nPortfolioTime);
    std::vector<std::string> vCPKEarned;
    std::vector<std::string> vCPKClaimed;

    // Step 1 - Process earnings from attached codes
    std::string sType;

    for (auto rc : vCRC) {
        if (rc.found) {
            // Block multiple codes originating from the same foreign CPK here: (but allow the user to attach more than one code from DISTINCT cpks):
            bool fUsed = std::find(vCPKClaimed.begin(), vCPKClaimed.end(), rc.OriginatorCPK) != vCPKClaimed.end();
            if (!fUsed) {
                CAmount nSize = 0;
                uint64_t nT1 = GetPortfolioTimeAndSize(vU, rc.OriginatorCPK, nSize);
                // This is when a user has attached a referral code:
                if (findStringCaseInsensitive(rc.CPK, sCPK) && !findStringCaseInsensitive(rc.OriginatorCPK, sCPK)) {
                    r.TotalClaimed += nSize;
                    vCPKClaimed.push_back(rc.OriginatorCPK);
                    sType = "Claimed";
                    std::string sRow = "PortfolioSize: " + AmountToString(nSize) + ", Value: " + AmountToString(rc.Size) + ", Gift: " + AmountToString(rc.GiftAmount);

                    details.push_back(Pair(rc.Code + "-" + sType + "-Originator " + rc.OriginatorCPK + "-Consumer " + rc.CPK, sRow));
                }
            }
        }
    }

    // Step 2 - Process earnings from others
    for (auto rc : vCRC) {
        if (rc.found) {
            // Block duplicate foreign portfolio earnings here:
            bool fUsed = std::find(vCPKEarned.begin(), vCPKEarned.end(), rc.CPK) != vCPKEarned.end();
            if (!fUsed) {
                CAmount nSize = 0;
                uint64_t nT1 = GetPortfolioTimeAndSize(vU, rc.CPK, nSize);
                // Case 2:  This is when the originator earns because other people have attached:
                if (findStringCaseInsensitive(rc.OriginatorCPK, sCPK) && !findStringCaseInsensitive(rc.CPK, sCPK)) {
                    r.TotalEarned += nSize;
                    vCPKEarned.push_back(rc.CPK);
                    sType = "Earned";
                    std::string sRow = "PortfolioSize: " + AmountToString(nSize) + ", Value: " + AmountToString(rc.Size) + ", Gift: " + AmountToString(rc.dGiftAmount);
                    details.push_back(Pair(rc.Code + "-" + sType + "-Originator " + rc.OriginatorCPK + "-Consumer " + rc.CPK, sRow));
                }
            }
        }
    }

    details.push_back(Pair("Referral BBP Claimed", AmountToString(r.TotalClaimed)));
    details.push_back(Pair("Referral BBP Earned", AmountToString(r.TotalEarned)));
    details.push_back(Pair("Gifts", mapGifts[sCPK]));
    details.push_back(Pair("Gift Details", mapGiftDetails[sCPK]));
    r.GiftDetails = mapGiftDetails[sCPK];
    r.dGiftAmount = mapGifts[sCPK];

    r.TotalReferralReward = r.TotalClaimed + r.TotalEarned;
    if (r.TotalReferralReward > nBBPSize)
        r.TotalReferralReward = nBBPSize;
    r.PercentageAffected = AmountToDouble(r.TotalReferralReward) / (AmountToDouble(nBBPSize) + .01);
    if (r.PercentageAffected > 1)
        r.PercentageAffected = 1;
    // Calculate the Total ReferralRewards
    r.ReferralRewards = 1.0 + (r.PercentageAffected * r.ReferralEffectivity);
    if (r.ReferralRewards < 1)
        r.ReferralRewards = 1.0;
    if (r.ReferralRewards > 1.5)
        r.ReferralRewards = 1.5;

    details.push_back(Pair("Total BBP in Portfolio affected", (double)r.TotalReferralReward / COIN));
    details.push_back(Pair("Portfolio % affected by rewards", r.PercentageAffected * 100));
    details.push_back(Pair("Referral Code Effectivity %", r.ReferralEffectivity * 100));
    details.push_back(Pair("Total Portfolio DWU Impact %", r.ReferralRewards * 100));
    std::string sNarr = "You are earning " + RoundToString((r.ReferralRewards - 1) * 100, 2) + "% in referral rewards sourced from " + AmountToString(r.TotalClaimed) + " claimed and " + AmountToString(r.TotalEarned) + " earned.";
    details.push_back(Pair("Summary", sNarr));

    return r;
}

std::string SerializeDMAddress(DMAddress d)
{
    std::string sXML = "<Name>" + d.Name + "</Name><AddressLine1>" + d.AddressLine1 + "</AddressLine1><AddressLine2>" + d.AddressLine2 + "</AddressLine2><City>" + d.City + "</City><State>" + d.State + "</State><Zip>" + d.Zip + "</Zip>";
    return sXML;
}

CAmount GetBBPValueUSD(double nUSD, double nMask)
{
    double nBTCPrice = GetCryptoPrice("btc");
    double nBBPPrice = GetCryptoPrice("bbp");
    double nUSDBBP = nBTCPrice * nBBPPrice;
    double nCost = nUSD / (nUSDBBP + .000001);
    nCost = cdbl(RoundToString(nCost, 0) + "." + RoundToString(nMask, 0), 6);
    CAmount nOut = nCost * COIN;
    return nOut;
}

double GetUSDValueBBP(CAmount nBBP)
{
    double nBTCPrice = GetCryptoPrice("btc");
    double nBBPPrice = GetCryptoPrice("bbp");
    double nUSDBBP = nBTCPrice * nBBPPrice;
    double nAmt = nUSDBBP * (nBBP / COIN);
    return nAmt;
}

DACResult MailLetter(DMAddress dmFrom, DMAddress dmTo, bool fDryRun)
{
    std::string sTXID;
    std::string sError;
    const Consensus::Params& consensusParams = Params().GetConsensus();
    std::string sFeeAddress = consensusParams.BurnAddress;

    if (!fDryRun) {
        CAmount nCost = GetBBPValueUSD(1, 0);
        if (nCost > 250000 * COIN)
            nCost = 250000 * COIN;
        std::string sPayload = "<mail>" + RoundToString((double)nCost / COIN, 2) + "</mail>";
        sTXID = RPCSendMessage(nCost, sFeeAddress, fDryRun, sError, sPayload);
    }
    std::string sCPK = DefaultRecAddress("Christian-Public-Key");
    std::string sXML = "<cpk>" + sCPK + "</cpk><paragraph1>" + dmTo.Paragraph1 + "</paragraph1><paragraph2>" 
        + dmTo.Paragraph2 + "</paragraph2><Template>" + dmTo.Template + "</Template>" + "<from>" + SerializeDMAddress(dmFrom) 
        + "</from>" + "<to>" + SerializeDMAddress(dmTo) + "<OpeningSalutation>" + dmTo.OpeningSalutation + "</OpeningSalutation><ClosingSalutation>" 
        + dmTo.ClosingSalutation + "</ClosingSalutation></to><dryrun>" 
        + (fDryRun ? "1" : "0") + "</dryrun><txid>" + sTXID + "</txid><feeaddress>" + sFeeAddress + "</feeaddress>";
    std::string sXML2 = EncodeBase64(sXML);
    DACResult b = DSQL_ReadOnlyQuery("Server?action=MAIL", sXML2);
    return b;
}

std::string CleansePhrase(std::string sPhrase)
{
    // Remove special characters from the passphrase so they can't hose it up between machines.
    sPhrase = SanitizeString(sPhrase);
    boost::replace_all(sPhrase, "'", "");
    boost::replace_all(sPhrase, "\"", "");
    boost::replace_all(sPhrase, " ", "");
    return sPhrase;
}

DACResult MakeDerivedKey(std::string sPhrase)
{
    sPhrase = CleansePhrase(sPhrase);
    CWallet* const pwallet = GetWalletForGenericRequest();
    CPubKey dpk;
    DACResult d;
    // Derive new Key
    bool fResult = pwallet->GetDerivedKey(dpk, sPhrase);
    if (!fResult) {
        d.ErrorCode = "Creation failed (04162021).";
        return d;
    }
    d.PublicKey = HexStr(dpk);
    CKeyID keyID = dpk.GetID();
    d.Address = EncodeDestination(keyID);
    CKey vchSecret;
    if (!pwallet->GetKey(keyID, vchSecret)) {
        d.ErrorCode = "Private key for address is not known";
        return d;
    }
    d.SecretKey = CBitcoinSecret(vchSecret).ToString();
    pwallet->SetAddressBook(keyID, sPhrase, "receive");
    pwallet->UnlockGift(d.Address);
    return d;
}

bool WriteAccountingEntry(std::string sKey, std::string sKey2, std::string sValue, CAmount nAmount)
{
    CWallet* const pwallet = GetWalletForGenericRequest();

    LOCK2(cs_main, pwallet->cs_wallet);

    if (!pwallet->AccountMove2(sKey, sKey2, nAmount, sValue)) {
        return false;
    }
    return true;
}

DACResult ReadAccountingEntry(std::string sKey, std::string sKey2)
{
    CWallet* const pwallet = GetWalletForGenericRequest();
    LOCK2(cs_main, pwallet->cs_wallet);
    std::string strAccount = "*";
    const CWallet::TxItems& txOrdered = pwallet->wtxOrdered;
    DACResult d;
    for (CWallet::TxItems::const_reverse_iterator it = txOrdered.rbegin(); it != txOrdered.rend(); ++it) {
        CWalletTx* const pwtx = (*it).second.first;
        CAccountingEntry* const pacentry = (*it).second.second;
        if (pacentry != nullptr) {
            // std::string sRow = "Key1 " + pacentry->strOtherAccount + "," + pacentry->strAccount + ",Value=" + pacentry->strComment + ",Amount=" + RoundToString((double)pacentry->nCreditDebit/COIN, 2);
            // LogPrintf("\nReadAccountingEntry::%s", sRow);
            if (sKey == pacentry->strOtherAccount && sKey2 == pacentry->strAccount) {
                d.Response = pacentry->strComment;
                d.nAmount = pacentry->nCreditDebit;
                return d;
            }
        }
    }
    return d;
}

std::string RSAEncryptHQURL(std::string sSourceData, std::string& sError)
{
    std::string sFoundation = "foundation"; // This is not a literal yet because we may have more than one marketplace.  We need to discuss this.
    UserRecord u = GetUserRecord(sFoundation);
    if (u.RSAPublicKey.length() < 256) {
        sError = "Bad public key.";
        return "";
    }
    std::string sEnc = RSA_Encrypt_String_With_Key(u.RSAPublicKey, sSourceData, sError);
    return sEnc;
}

std::string RSADecryptHQURL(std::string sEncData, std::string& sError)
{
    std::string sPrivPath = GetSANDirectory1() + "privkey.priv";
    std::string sDec = RSA_Decrypt_String(sPrivPath, sEncData, sError);
    return sDec;
}

std::string RemoveQuotesAndConvertCommas(std::string sData)
{
    bool fInQuote = false;
    std::string sDelim = "<col>";
    std::string sOut;
    for (int i = 0; i < sData.length(); i++) {
        std::string sChar = sData.substr(i, 1);
        if (sChar == "\"") {
            fInQuote = !fInQuote;
        }
        if (sChar == "," && !fInQuote)
            sChar = sDelim;

        if (sChar == "\"")
            sChar = "";

        sOut += sChar;
    }
    sOut = strReplace(sOut, "\r", "");
    sOut = strReplace(sOut, "\n", "");
    return sOut;
}

std::vector<DMAddress> ImportGreetingCardCSVFile(std::string sFullPath)
{
    boost::filesystem::path pathIn(sFullPath);

    boost::filesystem::ifstream streamCSV(pathIn);
    std::vector<DMAddress> d;
    if (!streamCSV.good()) {
        return d;
    }

    // Format: Name, Address1, Address2, City, State, Zip
    int linenumber = 0;
    for (std::string line; std::getline(streamCSV, line); linenumber++) {
        if (line.empty())
            continue;
        line = RemoveQuotesAndConvertCommas(line);
        std::vector<std::string> v = Split(line.c_str(), "<col>");
        if (v.size() >= 6) {
            DMAddress d1;
            d1.Name = v[0];
            d1.AddressLine1 = v[1];
            d1.AddressLine2 = v[2];
            d1.City = v[3];
            d1.State = v[4];
            d1.Zip = v[5];
            d.push_back(d1);
            linenumber++;
        }
    }
    LogPrintf("\nImportGreetingCards - Imported %f Lines", linenumber);

    streamCSV.close();
    return d;
}

uint256 GetSHA256Hash(std::string sData)
{
    uint256 h;
    CSHA256 sha256;
    std::vector<unsigned char> vch1 = std::vector<unsigned char>(sData.begin(), sData.end());
    sha256.Write(&vch1[0], vch1.size());
    sha256.Finalize(h.begin());
    return h;
}

bool height_sort(std::pair<CAddressUnspentKey, CAddressUnspentValue> a,
    std::pair<CAddressUnspentKey, CAddressUnspentValue> b)
{
    return a.second.blockHeight < b.second.blockHeight;
};

bool getIndexKey(const std::string& str, uint160& hashBytes, int& type);
bool getAddressFromIndex(const int& type, const uint160& hash, std::string& address);
std::vector<SimpleUTXO> GetAddressUTXOs_BBP(std::string sAddress)
{
    std::vector<SimpleUTXO> v;
    std::vector<std::pair<uint160, int>> addresses;

    uint160 hashBytes;
    int type = 0;
    if (!getIndexKey(sAddress, hashBytes, type)) {
        // Invalid Address
        return v;
    }

    addresses.push_back(std::make_pair(hashBytes, type));
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue>> unspentOutputs;

    for (std::vector<std::pair<uint160, int>>::iterator it = addresses.begin(); it != addresses.end(); it++) {
        if (!GetAddressUnspent((*it).first, (*it).second, unspentOutputs)) {
            //No information available for address
            return v;
        }
    }

    std::sort(unspentOutputs.begin(), unspentOutputs.end(), height_sort);
    UniValue result(UniValue::VARR);
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue>>::const_iterator it = unspentOutputs.begin(); it != unspentOutputs.end(); it++) {
        std::string address;
        if (!getAddressFromIndex(it->first.type, it->first.hashBytes, address)) {
            // Unknown Address Type
            return v;
        }

        SimpleUTXO u;
        u.Address = address;
        u.TXID = it->first.txhash.GetHex();
        u.nOrdinal = (int)it->first.index;
        u.nAmount = it->second.satoshis;
        u.Height = it->second.blockHeight;
        u.Ticker = "BBP";
        u.Trace = 1;

        if (u.nAmount > 0) {
            // Harvest ToDo: ensure that the amount matches the pin
            v.push_back(u);
        }
    }

    return v;
}

static std::string sTickers = "DASH,BTC,DOGE,ETH,LTC,XRP,XLM,BBP,ZEC,BCH";
bool ValidateTicker(std::string sTicker)
{
    std::vector<std::string> vTickers = Split(sTickers.c_str(), ",");
    for (int i = 0; i < vTickers.size(); i++) {
        if (vTickers[i] == sTicker)
            return true;
    }
    return false;
}

void AddUTXOStake(UTXOStake& u, bool fDryRun, std::string& sError, std::string sOptFundAddress, CAmount nOptFundAmount)
{
    const Consensus::Params& consensusParams = Params().GetConsensus();

    boost::to_upper(u.Ticker);

    u.CPK = DefaultRecAddress("Christian-Public-Key");
    u.Time = GetAdjustedTime();

    CWallet* const pwallet = GetWalletForGenericRequest();

    bool fTickerValid = ValidateTicker(u.Ticker);

    if (!fTickerValid) {
        sError = "Ticker must be " + sTickers;
        return;
    }

    if (u.Ticker == "BBP") {
        if (nOptFundAmount < 5000 * COIN) {
            sError = "A minimum of 5000 bbp is required to create a new BBP portfolio record.";
            return;
        }
    } else {
        nOptFundAmount = 0;
        sOptFundAddress = "";
    }

    std::string sPayload = u.ToXML();
    CAmount nFeeAmount = UTXO_COST * COIN;
    bool fDuplicate = IsDuplicateUTXO(u.Address);
    if (fDuplicate && u.Ticker != "BBP") {
        sError = "Duplicate UTXO. ";
        return;
    } else if (fDuplicate && u.Ticker == "BBP") {
        nFeeAmount = 1 * COIN;
        sPayload = "<utxoadd>" + AmountToString(nOptFundAmount) + "</utxoadd>";
    }

    std::string sTXID = RPCSendMessage(nFeeAmount, consensusParams.BurnAddress, fDryRun, sError, sPayload, sOptFundAddress, nOptFundAmount);
    u.TXID = uint256S(sTXID);
    // Lock the coins so they cant be spent while waiting to be in a block
    if (!sOptFundAddress.empty()) {
        double nPin = AddressToPin(sOptFundAddress);
        pwallet->LockByMask(nPin);
    }
}
