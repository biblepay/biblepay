// Copyright (c) 2014-2019 The Dash Core Developers, The DAC Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <base58.h>
#include <node/blockstorage.h>
#include <boost/algorithm/string.hpp>           // for trim()
#include <boost/algorithm/string/case_conv.hpp> // for to_lower()
#include <boost/algorithm/string/replace.hpp>
#include <coinjoin/client.h>
#include <coinjoin/context.h>
#include <interfaces/chain.h>
#include <interfaces/coinjoin.h>
#include <interfaces/node.h>
#include <boost/thread.hpp>
#include <rpc/blockchain.h>
#include <boost/asio.hpp>
#include <boost/date_time/posix_time/posix_time.hpp> // for StringToUnixTime()
#include <boost/lexical_cast.hpp>
#include <boost/thread.hpp>
#include <evo/deterministicmns.h>

#include <consensus/validation.h>
#include <core_io.h>
#include <fstream>
#include <init.h>
#include <key_io.h>
#include <iostream>    
#include <algorithm>
#include <masternode/sync.h>
#include <masternode/node.h>
#include <governance/vote.h>
#include <governance/governance.h>
#include <node/context.h>
#include <math.h> /* round, floor, ceil, trunc */
#include <messagesigner.h>
#include <net.h> // for CService
#include <netaddress.h>
#include <netbase.h>
#include <policy/policy.h>

#include <rpcpog.h>
#include <rpc/server.h>

#include <txmempool.h>
#include <validation.h>
#include <stdint.h>
#include <univalue.h>
#include <sstream>
#include <wallet/scriptpubkeyman.h>

#include <openssl/bio.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/md5.h>
#include <openssl/ssl.h>
#include <wallet/coincontrol.h>
#include <wallet/rpcwallet.h>
#include <wallet/wallet.h>
#include <wallet/walletdb.h>
#include <wallet/walletutil.h>
#include <chrono> //sleep


UniValue protx(const JSONRPCRequest& request);
UniValue _bls(const JSONRPCRequest& request);
UniValue protx_update_service_common_wrapper(const JSONRPCRequest& request, const ChainstateManager& chainman, const MnType mnType);
UniValue createwallet(const JSONRPCRequest& request);
std::string createwalletformining(const JSONRPCRequest& request);
UniValue VoteWithMasternodes(const std::map<uint256, CKey>& keys,
    const uint256& hash,
    vote_signal_enum_t eVoteSignal,
    vote_outcome_enum_t eVoteOutcome);

std::string DoubleToString(double d, int place)
{
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(place) << d;
    return ss.str();
}

bool Contains(std::string data, std::string instring)
{
    std::size_t found = 0;
    found = data.find(instring);
    if (found != std::string::npos) return true;
    return false;
}


CWallet* GetInternalWallet(JSONRPCRequest r)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(r);
    CWallet* const pwallet = wallet.get();
    return pwallet;
}

void CreateWalletIfNotExists(JSONRPCRequest r)
{
    try
    {
        std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(r);
    }
    catch(...)
    {
        LogPrintf("\nWALLET::Sorry, no wallet exists.  Creating default wallet for sanctuary mining.\r\n%f", 1);
        std::string cOut = createwalletformining(r);
        LogPrintf("\nWALLET::%s",cOut);
    }
}


void MilliSleep(int64_t n)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(n));
}


template <typename T>
std::string HexStrBBP(const T itbegin, const T itend, bool fSpaces = false)
{
    std::string rv;
    static const char hexmap[16] = {'0', '1', '2', '3', '4', '5', '6', '7',
                                    '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    rv.reserve((itend - itbegin) * 3);
    for (T it = itbegin; it < itend; ++it) {
        unsigned char val = (unsigned char)(*it);
        if (fSpaces && it != itbegin)
            rv.push_back(' ');
        rv.push_back(hexmap[val >> 4]);
        rv.push_back(hexmap[val & 15]);
    }

    return rv;
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

std::string strReplace(std::string str_input, std::string str_to_find, std::string str_to_replace_with)
{
    boost::replace_all(str_input, str_to_find, str_to_replace_with);
    return str_input;
}

double StringToDouble(std::string s, int place)
{
    if (s == "") 
		s = "0";
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
        LogPrintf("caught stdbl bad lexical cast %f from %s with %f", 1, s, (double)place);
        return 0;
    } catch (...) {
        LogPrintf("caught stdbl bad lexical cast %f", 2);
    }
    double d = Round(r, place);
    return d;
}


CBlockIndex* GetNextBlockIndex(CBlockIndex* pindex)
{
    CBlockIndex* next = g_chainman.ActiveChain().Tip()->GetAncestor(pindex->nHeight + 1);
    if (next && next->pprev == pindex)
    {
        return next;
    }
    next = nullptr;
    return next;
}


static CBlockIndex* pblockindexFBBHLast;
CBlockIndex* FindBlockByHeight(int nHeight)
{
    CBlockIndex* pindexTip = WITH_LOCK(cs_main, return g_chainman.ActiveChain().Tip());
    
    CBlockIndex* pblockindex;
    if (nHeight < 0 || nHeight > pindexTip->nHeight) return NULL;

    if (nHeight < (pindexTip->nHeight / 2))
    {
        pblockindex = g_chainman.m_blockman.LookupBlockIndex(g_chainman.ActiveChain().Genesis()->GetBlockHash());
    }
    else
    {
        pblockindex = pindexTip;
    }

    if (pblockindexFBBHLast && abs(nHeight - pblockindex->nHeight) > abs(nHeight - pblockindexFBBHLast->nHeight))
    {
        pblockindex = pblockindexFBBHLast;
    }

    while (pblockindex->nHeight > nHeight)
    {
        pblockindex = pblockindex->pprev;
    }
    while (pblockindex->nHeight < nHeight)
    {
        pblockindex = GetNextBlockIndex(pblockindex);
    }
    pblockindexFBBHLast = pblockindex;
    return pblockindex;
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


CAmount GetWalletBalance(JSONRPCRequest r)
{
    CWallet* pwallet = GetInternalWallet(r);
    if (!EnsureWalletIsAvailable(pwallet, false))
        return 0;
    const auto bal = pwallet->GetBalance(0, false, false);
    CAmount nBal = bal.m_mine_trusted;
    return nBal;
}


std::string GetPrivKey2(JSONRPCRequest r, std::string sPubKey, std::string& sError)
{
    CTxDestination dest = DecodeDestination(sPubKey);
    CWallet* pwallet = GetInternalWallet(r);

    if (!EnsureWalletIsAvailable(pwallet, false)) {
        sError = "WALLET_NEEDS_UNLOCKED";
        return "";
    }

    LegacyScriptPubKeyMan* spk_man = pwallet->GetLegacyScriptPubKeyMan();

    if (!IsValidDestination(dest)) {
        sError = "Invalid BiblePay address";
        return "";
    }
    const PKHash* pkhash = std::get_if<PKHash>(&dest);
    if (!pkhash) {
        sError = "Address does not refer to a key";
        return "";
    }

    CKey vchSecret;
    if (!spk_man->GetKey(ToKeyID(*pkhash), vchSecret)) {
        sError = "Private key for address is not known";
        return "";
    }
    std::string sPK = EncodeSecret(vchSecret);
    return sPK;
}

bool RPCSendMoney(JSONRPCRequest r, std::string& sError, std::string sAddress, CAmount nValue, std::string& sTXID, std::string sOptionalData, int& nVoutPosition)
{
    CWallet* pwallet = GetInternalWallet(r);

    if (!EnsureWalletIsAvailable(pwallet, false))
        return false;
	CScript scriptPubKey = GetScriptForDestination(DecodeDestination(sAddress));
	
    CAmount nFeeRequired;
    bilingual_str strFailReason;

    std::vector<CRecipient> vecSend;
    int nChangePosRet = -1;

	// BiblePay - Handle extremely large data transactions:
    int nMaxLength = 2999;
    if (sOptionalData.length() > nMaxLength && nValue > 0)
    {
        double nReq = ceil(sOptionalData.length() / nMaxLength);
        double n1 = (double)nValue / COIN;
        double n2 = n1 / nReq;
        int nCharPos = 0;

        for (int n3 = 0; n3 < nReq; n3++)
        {
            CAmount indAmt = n2 * COIN;
            CRecipient recipient = {scriptPubKey, indAmt, false};
            int nCurLength = (nCharPos + nCharPos) <= sOptionalData.length() ? nMaxLength : sOptionalData.length() - nCharPos;
            std::string sOptDataChunk = sOptionalData.substr(nCharPos, nCurLength);
            recipient.sTxOutMessage = sOptDataChunk;
            vecSend.push_back(recipient);
            nCharPos += nMaxLength;
        }
    }
    else
    {
        CRecipient recipient = {scriptPubKey, nValue, false};
        recipient.sTxOutMessage = sOptionalData;
        vecSend.push_back(recipient);
    }
    // END OF BIBLEPAY



    int nMinConfirms = 0;
    CCoinControl coinControl;
    int nChangePos = -1;
    CTransactionRef tx;

    if (!pwallet->CreateTransaction(vecSend, tx, nFeeRequired, nChangePos, strFailReason, coinControl, true, 0)) 
	{
        // Todo : Convert bilingual string(strError) and append to the error
        sError = "Unable to Create Transaction";
        return false;
    }

    pwallet->CommitTransaction(tx, {}, {});

    for (unsigned int i = 0; i < tx->vout.size(); i++) 
    {
        if (tx->vout[i].nValue == nValue)
        {
           nVoutPosition = i;
        }
    }

    sTXID = tx->GetHash().GetHex();
	return true;
}


bool IsMyAddress(JSONRPCRequest r,std::string sAddress)
{
        isminetype result;
        CWallet* pwallet = GetInternalWallet(r);
        LegacyScriptPubKeyMan* spk_man = pwallet->GetLegacyScriptPubKeyMan();
        CScript spkAddress = GetScriptForDestination(DecodeDestination(sAddress));
        result = spk_man->IsMine(spkAddress);
        bool fMine = result != ISMINE_NO;
        return fMine;
}

std::string DefaultRecAddress(JSONRPCRequest r,std::string sNamedEntry)
{
    CWallet* pwallet = GetInternalWallet(r);

    if (!EnsureWalletIsAvailable(pwallet, false))
	{
        LogPrintf("\nDefaultRecAddress::ERROR %f No pwallet", 03142021);
		return "";
	}

    boost::to_upper(sNamedEntry);
    std::string sDefaultRecAddress;
        
    for (const std::pair<const CTxDestination, CAddressBookData>& item : pwallet->m_address_book)
    {
        CTxDestination txd1 = item.first;
        std::string sAddr = EncodeDestination(txd1);
        std::string strName = item.second.GetLabel();
        bool fMine = IsMyAddress(r,sAddr);
        if (fMine) {
            boost::to_upper(strName);
            if (strName == sNamedEntry) {
                return sAddr;
            }
        }
    }

	// Not in the book
    std::string sError;
    if (sNamedEntry == "")
		sNamedEntry = "NA";
  
    CPubKey pubkey;

    LegacyScriptPubKeyMan* spk_man = pwallet->GetLegacyScriptPubKeyMan();
    if (!spk_man->GetKeyFromPool(pubkey, false))
    {
	   LogPrintf("\r\nDefaultRecAddress cant get key from keypool: denied %f",831);
	   return "";
    }

    CKeyID vchAddress = pubkey.GetID();
    pwallet->MarkDirty();
    CTxDestination dTo = PKHash(vchAddress);

    pwallet->SetAddressBook(dTo, sNamedEntry, "receive");

    sDefaultRecAddress = EncodeDestination(PKHash(vchAddress));
    LogPrintf("\r\nDefaultRecAddress for %s=%s, Error=%s", sNamedEntry, sDefaultRecAddress, sError);
   
    return sDefaultRecAddress;
}


std::string SignMessageEvo(JSONRPCRequest r,std::string strAddress, std::string strMessage, std::string& sError)
{
    CWallet* pwallet = GetInternalWallet(r);
    if (!EnsureWalletIsAvailable(pwallet, false))
        return "no wallet";

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

    const PKHash* pkhash = std::get_if<PKHash>(&dest);
    if (!pkhash) {
        sError = "collateral type not supported";
        return std::string();
    }

    LegacyScriptPubKeyMan* spk_man = pwallet->GetLegacyScriptPubKeyMan();

    CKey key;
    if (!spk_man->GetKey(ToKeyID(*pkhash), key)) {
        sError = "Private key not available";
        return std::string();
    }

    
    CHashWriter ss(SER_GETHASH, 0);
    ss << MESSAGE_MAGIC_BBP;
    ss << strMessage;
    std::vector<unsigned char> vchSig;
    if (!key.SignCompact(ss.GetHash(), vchSig)) {
        sError = "Sign failed";
        return std::string();
    }

    return EncodeBase64(vchSig);
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

boost::filesystem::path GetUnchainedConfigFile()
{
    boost::filesystem::path pathConfigFile(gArgs.GetArg("-unchainedconf", "unchained.conf"));
    if (!pathConfigFile.is_complete())
        pathConfigFile = GetDataDir() / pathConfigFile;
    return pathConfigFile;
}

void WriteUnchainedConfigFile(std::string sPub, std::string sPriv)
{
    boost::filesystem::path unchainedConfigFile = GetUnchainedConfigFile();
    boost::filesystem::ifstream streamConfig(unchainedConfigFile);
    bool fReadable = streamConfig.good();
    if (fReadable)        streamConfig.close();
    FILE* configFile = fopen(unchainedConfigFile.string().c_str(), "wt");
    std::string s1 = "unchained_mainnet_pubkey=" + sPub + "\r\n";
    fwrite(s1.c_str(), std::strlen(s1.c_str()), 1, configFile);

    std::string s2 = "unchained_mainnet_privkey=" + sPriv + "\r\n";
    fwrite(s2.c_str(), std::strlen(s2.c_str()), 1, configFile);
    fclose(configFile);
    LogPrintf("Writing unchained conf file pubkey=%s\r\n", s1);
}

void ReadUnchainedConfigFile(std::string& sPub, std::string& sPriv)
{
    boost::filesystem::path pathUnchainedFile = GetUnchainedConfigFile();
    boost::filesystem::ifstream streamConfig(pathUnchainedFile);
    if (!streamConfig.good())
        return;
    std::getline(streamConfig, sPub);
    std::getline(streamConfig, sPriv);
    sPub = strReplace(sPub, "\r", "");
    sPub = strReplace(sPub, "\n", "");
    sPriv = strReplace(sPriv, "\r", "");
    sPriv = strReplace(sPriv, "\n", "");
    sPub = strReplace(sPub, "unchained_mainnet_pubkey=", "");
    sPriv = strReplace(sPriv, "unchained_mainnet_privkey=", "");
    streamConfig.close();
    return;
}


boost::filesystem::path GetGenericFilePath(std::string sPath)
{
    boost::filesystem::path pathConfigFile(sPath);
    if (!pathConfigFile.is_complete())
        pathConfigFile = GetDataDir() / pathConfigFile;
    return pathConfigFile;
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

bool ValidateAddress2(std::string sAddress)
{
    CTxDestination dest = DecodeDestination(sAddress);
    return IsValidDestination(dest);
}

/*
bool SendManyXML(std::string XML, std::string& sTXID)
{
	std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(r);
    CWallet* const pwallet = wallet.get();
	LOCK2(cs_main, pwallet->cs_wallet);
	int nMinDepth = 1;
    CTransactionRef tx;
	std::set<CScript> setAddress;
	std::vector<CRecipient> vecSend;
	CAmount totalAmount = 0;
	std::string sRecipients = ExtractXML(XML, "<RECIPIENTS>", "</RECIPIENTS>");
	std::vector<std::string> vRecips = Split(sRecipients.c_str(), "<ROW>");
	for (int i = 0; i < (int)vRecips.size(); i++)
	{
			std::string sRecip = vRecips[i];
			if (!sRecip.empty())
			{
				std::string sRecipient = ExtractXML(sRecip, "<RECIPIENT>","</RECIPIENT>");
				double dAmount = StringToDouble(ExtractXML(sRecip,"<AMOUNT>","</AMOUNT>"), 4);
				if (!sRecipient.empty() && dAmount > 0)
				{
					  CScript spkAddress = GetScriptForDestination(DecodeDestination(sRecipient));

	   		   	      if (!ValidateAddress2(sRecipient))
						  throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid address: ") + sRecipient);
					  if (setAddress.count(spkAddress))
						  throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, duplicated address: ") + sRecipient);
					  setAddress.insert(spkAddress);
					  CAmount nAmount = dAmount * COIN;
					  if (nAmount <= 0) 
						  throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for send");
					  totalAmount += nAmount;
					  bool fSubtractFeeFromAmount = false;
				      CRecipient recipient = {spkAddress, nAmount, false};
					  vecSend.push_back(recipient);
				}
			}
	}
	CReserveKey keyChange(pwallet);
	CAmount nFeeRequired = 0;
	int nChangePosRet = -1;
	std::string strFailReason;
	bool fUseInstantSend = false;
	bool fUsePrivateSend = false;
	CValidationState state;
    CCoinControl coinControl;
	bool fCreated = pwallet->CreateTransaction(vecSend, tx, keyChange, nFeeRequired, nChangePosRet, strFailReason, coinControl, true);
	if (!fCreated)
	{
			throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, strFailReason);
	}
    
	if (!pwallet->CommitTransaction(tx, {}, {}, {}, keyChange, an.get(), state))
	{
			throw JSONRPCError(RPC_WALLET_ERROR, "Transaction commit failed");
	}
	sTXID = tx->GetHash().GetHex();
	return true;
}
*/


uint256 CRXT(uint256 hash, int64_t nPrevBlockTime, int64_t nBlockTime)
{
    static int MAX_AGE = 60 * 30;
    static int MAX_AGE2 = 60 * 45;
    static int64_t nDivisor = 8400;
    int64_t nElapsed = nBlockTime - nPrevBlockTime;
    if (nElapsed > MAX_AGE) {
        arith_uint256 bnHash = UintToArith256(hash);
        bnHash *= 700;
        bnHash /= nDivisor;
        uint256 nBH = ArithToUint256(bnHash);
        return nBH;
    }

    if (nElapsed > MAX_AGE2) {
        arith_uint256 bnHash = UintToArith256(hash);
        bnHash *= 200;
        bnHash /= nDivisor;
        uint256 nBH = ArithToUint256(bnHash);
        return nBH;
    }

    return hash;
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
    std::string sVerbs = "</html>|</HTML>|<EOF>|<END>|</account_out>|</am_set_info_reply>|</am_get_info_reply>|</MemberStats>|NoSuchKey";
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

std::string PrepareReq(bool bPost, std::string sPage, std::string sHostHeader, const std::string& sMsg, const std::map<std::string, std::string>& mapRequestHeaders)
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


static int nConnectSucceeded = 0;

void private_connect_handler(const boost::system::error_code& error)
{
    if (!error) {
        // Connect succeeded.
        nConnectSucceeded = 1;
    }
    else
    {
        nConnectSucceeded = -1;
    }
}


bool TcpTest(std::string sIP, int nPort, int nTimeout)
{
    try 
    {
        typedef boost::asio::ip::tcp tcp;
        boost::shared_ptr<boost::asio::io_service> io_service(new boost::asio::io_service);
        boost::shared_ptr<tcp::socket> socket(new tcp::socket(*io_service));
        tcp::resolver resolver(*io_service);
        tcp::resolver::query query(sIP, boost::lexical_cast<std::string>(nPort));
        tcp::resolver::iterator myEP = resolver.resolve(query);
        tcp::endpoint myEndpoint(boost::asio::ip::address::from_string(sIP), nPort);
        boost::system::error_code connectEc;
        nConnectSucceeded = 0;
        socket->async_connect(myEndpoint, private_connect_handler);
        for (int i = 0; i < nTimeout; i++)
        {
            io_service->poll();
            if (nConnectSucceeded != 0)
            {
                 break;
            }
            if (ShutdownRequested()) {
                 break;
            }
            MilliSleep(1000);
        }
        return (nConnectSucceeded == 1) ? true : false;
    }
    catch (...)
    {
        return false;
    }
}



static double HTTP_PROTO_VERSION = 2.0;
std::string Uplink(bool bPost, std::string sPayload, std::string sBaseURL, std::string sPage, int iPort, 
        int iTimeoutSecs, int iBOE, std::map<std::string, std::string> mapRequestHeaders, std::string TargetFileName, bool fJson)
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
    try 
    {
        double dDebugLevel = StringToDouble(gArgs.GetArg("-devdebuglevel", "0"), 0);
        if (dDebugLevel == 1)
            LogPrintf("\r\nUplink::Connecting to %s [/] %s ", sBaseURL, sPage);

        mapRequestHeaders["Agent"] = FormatFullVersion();
        // Supported pool Network Chain modes: main, test, regtest
        const CChainParams& chainparams = Params();
        mapRequestHeaders["NetworkID"] = chainparams.NetworkIDString();
        if (sPayload.length() < 7000)
            mapRequestHeaders["Action"] = sPayload;
        mapRequestHeaders["HTTP_PROTO_VERSION"] = DoubleToString(HTTP_PROTO_VERSION, 0);
        if (bPost)
            mapRequestHeaders["Content-Type"] = "application/octet-stream";

        if (fJson)
            mapRequestHeaders["Content-Type"] = "application/json";
        
        // In this section, we provide the Public unchained key, and signature, so we can query cockroachdb
        // for a report
        std::string sPublicKey;
        std::string sPrivKey;
        ReadUnchainedConfigFile(sPublicKey, sPrivKey);
        mapRequestHeaders["unchained-public-key"] = sPublicKey;
        std::string sSignError;
        std::string sSig;


        // Start of BBP Signer
        const CoreContext& cc = CGlobalNode::GetGlobalCoreContext();
        JSONRPCRequest r0(cc);
        SignMessageEvo(r0, sPublicKey, "authenticate", sSignError);
        LogPrintf("\nUplink::BBPPub::%s,privlen%f,Signed::%s", sPublicKey, sPrivKey.length(), sSig);
        mapRequestHeaders["unchained-auth-signature"] = sSig;
        // End of BBP Signer

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
        std::string sDomainWithPort = sDomain + ":" + DoubleToString(iPort, 0);

        // Compatibility with strict d-dos prevention rules (like cloudflare)
        SSL* ssl(nullptr);
        BIO_get_ssl(bio, &ssl);
        SSL_set_mode(ssl, SSL_MODE_AUTO_RETRY);
        SSL_set_tlsext_host_name(ssl, const_cast<char*>(sDomain.c_str()));
        BIO_set_conn_hostname(bio, sDomainWithPort.c_str());
        std::string sPortNum = DoubleToString(iPort, 0);
        BIO_set_conn_port(bio, const_cast<char*>(sPortNum.c_str()));

        if (dDebugLevel == 1)
            LogPrintf("Connecting to %s", sDomainWithPort.c_str());
        if (sDomain.empty()) {
            BIO_free_all(bio);
            return "<ERROR>DOMAIN_MISSING</ERROR>";
        }

        // **** SET TO NON BLOCKING IO ****
        BIO_set_nbio(bio, 1);

        // Refactor Do Connect here
        int nRet = 0;
        if (dDebugLevel == 1)
            LogPrintf("\r\nDo Connect %f",GetAdjustedTime());

        for (int x = 0; x < iTimeoutSecs*10; x++)
        {
            nRet = BIO_do_connect(bio);
            if (nRet <= 0 && BIO_should_retry(bio))
            {
                MilliSleep(100);
            }
        }

        if (nRet <= 0) {
            if (dDebugLevel == 1)
                LogPrintf("Failed connection to %s ", sDomainWithPort);
            if (!TargetFileName.empty())
                OutFile.close();
            BIO_free_all(bio);
            return "<ERROR>Failed connection to " + sDomainWithPort + "</ERROR>";
        }
        // Refactor Do Handshake here
        if (dDebugLevel == 1)
              LogPrintf("\r\nDo Handshake %f",GetAdjustedTime());

        int nHandshake = 0;
        for (int x = 0; x < iTimeoutSecs*10; x++)
        {
            nHandshake = BIO_do_handshake(bio);
            if (nHandshake <= 0 && BIO_should_retry(bio))
            {
                MilliSleep(100);
            }
        }

        if (nHandshake <= 0) {
            if (dDebugLevel == 1)
                LogPrintf("Failed handshake for %s ", sDomainWithPort);
            if (!TargetFileName.empty())
                OutFile.close();
            BIO_free_all(bio);
            return "<ERROR>Failed connection to " + sDomainWithPort + "</ERROR>";
        }

        if (dDebugLevel == 1)
            LogPrintf("\r\nDo Write %f",GetAdjustedTime());

        // Write the output Post buffer here
        std::string sPost = PrepareReq(bPost, sPage, sDomain, sPayload, mapRequestHeaders);
        const char* write_buf = sPost.c_str();
        if (dDebugLevel == 1)
            LogPrintf("BioPost %f", 801);
        if (BIO_write(bio, write_buf, strlen(write_buf)) <= 0) {
            if (!TargetFileName.empty())
                OutFile.close();
            BIO_free_all(bio);

            return "<ERROR>FAILED_HTTPS_POST</ERROR>";
        }
        if (dDebugLevel == 1)
            LogPrintf("\r\nDo Read %f",GetAdjustedTime());

        //  Variables used to read the response from the server
        int size;
        clock_t begin = clock();
        char buf[16384];
        for (;;) {
            if (dDebugLevel == 1)
                LogPrintf("BioRead %f", 803);

            size = BIO_read(bio, buf, 16384);
            if (size <= 0 && !BIO_should_retry(bio)) {
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
                    dMaxSize = StringToDouble(ExtractXML(MyData, "Content-Length: ", "\n"), 0);
                    std::size_t foundPos = MyData.find("Content-Length:");
                    if (dMaxSize > 0) {
                        iMaxSize = dMaxSize + (int)foundPos + 16;
                        fContentLengthFound = true;
                    }
                }
            }

            if (iRead >= iMaxSize)
                break;
            MilliSleep(100);
        }
        if (dDebugLevel == 1)
            LogPrintf("\r\nDo Exit %f",GetAdjustedTime());

        // Free bio resources
        BIO_free_all(bio);
        if (!TargetFileName.empty())
            OutFile.close();

        return sData;
    } catch (std::exception& e) {
        return "<ERROR>READ_EXCEPTION</ERROR>";
    } catch (...) {
        return "<ERROR>GENERAL_READ_EXCEPTION</ERROR>";
    }
}

static std::string msBaseDomain = "https://unchained.biblepay.org";

static int SSL_PORT = 443;
BBPResult UnchainedQuery(std::string sXMLSource, std::string sAPI)
{
    int iTimeout = 30000;
    BBPResult b;
    b.Response = Uplink(false, "", msBaseDomain, sAPI, SSL_PORT, iTimeout, 4);
    return b;
}

BBPResult UnchainedGet(std::string sAPIPath)
{
    std::string sRandom = DoubleToString(GetAdjustedTime(), 0);
    std::string sPage = sAPIPath + "?rand=" + sRandom;
    int nBMS_PORT = 443;
    std::string sResponse = Uplink(false, sAPIPath, msBaseDomain, sPage, nBMS_PORT, 15, 1);
    BBPResult b;
    
    b.Response = ExtractXML(sResponse, "<json>", "</json>");
    b.ErrorCode = ExtractXML(sResponse, "<error>", "</error>");
    b.Response = strReplace(b.Response, "\t", "     ");
    return b;
}

BBPResult UnchainedApiGet()
{
    std::string msBase = "https://api.biblepay.org";
    std::string sPage = "api/pin/getnft/56";
    // Mission critical todo
    int iTimeout = 10000;
    BBPResult b;
    b.Response = Uplink(false, "", msBase, sPage, SSL_PORT, iTimeout, 4);
    return b;
}

std::string DateTimeStrFormat(const char* pszFormat, int64_t nTime)
{
    static std::locale classic(std::locale::classic());
    // std::locale takes ownership of the pointer
    std::locale loc(classic, new boost::posix_time::time_facet(pszFormat));
    std::stringstream ss;
    ss.imbue(loc);
    ss << boost::posix_time::from_time_t(nTime);
    return ss.str();
}


BBPResult SidechainQuery(std::string sXMLSource, std::string sAPI)
{
    std::string sDomain = "https://bbpdb.s3.filebase.com";
    int iTimeout = 60000;
    BBPResult b;
    b.Response = Uplink(false, "", sDomain, sAPI, SSL_PORT, iTimeout, 4);
    return b;
}

std::string TimestampToHRDate(double dtm)
{
    if (dtm == 0) return "1-1-1970 00:00:00";
    if (dtm > 9888888888) return "1-1-2199 00:00:00";
    std::string sDt = DateTimeStrFormat("%m-%d-%Y %H:%M:%S", dtm);
    return sDt;
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

CScript GetScriptForMining(JSONRPCRequest r)
{
    std::string address = DefaultRecAddress(r,"MINING");
    CTxDestination dest = DecodeDestination(address);
    CScript script = GetScriptForDestination(dest);
    return script;
}


bool IsDailySuperblock(int nHeight)
{
	const CChainParams& chainparams = Params();
    if (nHeight >= chainparams.GetConsensus().REDSEA_HEIGHT)
    {
        // No more daily superblocks after redsea_height
        return false;
    }

	bool fDaily = (nHeight % 205 == 20);
	return fDaily;
}

int GetNextDailySuperblock(int nHeight)
{
	for (int i = 0; i < BLOCKS_PER_DAY*2; i++)
	{
		int nNewHeight = nHeight + i;
		if (IsDailySuperblock(nNewHeight))
			return nNewHeight;
	}
	return 0;
}

CAmount GetDailyPaymentsLimit(int nHeight)
{
	const CChainParams& chainparams = Params();
    const Consensus::Params& consensusParams = Params().GetConsensus();
    
    double UTXO_BLOCK_PERCENTAGE = .30;
    if (nHeight >= consensusParams.EXODUS_HEIGHT)
    {
        UTXO_BLOCK_PERCENTAGE = .55;
    }
	int nDiff = 500000000;
	double nSubsidy = (double)GetBlockSubsidyInner(nDiff, nHeight, chainparams.GetConsensus(), true)/COIN;
	double nLimit = nSubsidy * UTXO_BLOCK_PERCENTAGE * BLOCKS_PER_DAY;
	LogPrintf("\nGetDailyPaymentsLimit Subsidy %f limit %f", nSubsidy, nLimit);
	CAmount nPaymentsLimit = nLimit * COIN;
	return nPaymentsLimit;
}

std::vector<Portfolio> GetDailySuperblock(int nHeight)
{
	const CChainParams& chainparams = Params();
	CAmount nPaymentsLimit = GetDailyPaymentsLimit(nHeight) - (MAX_BLOCK_SUBSIDY * COIN);
	std::string sChain = chainparams.NetworkIDString();
    LogPrintf("GetDailySuperblock::Payments Limits %f %f ", nHeight, nPaymentsLimit/COIN);
	std::string sData0 = ScanChainForData(nHeight);
	std::string sHash = ExtractXML(sData0, "<hash>", "</hash>");
	std::string sData = ExtractXML(sData0, "<data>", "</data>");
	LogPrintf("\nHash %s, Data %s", sHash, sData);

	std::vector<Portfolio> vPortfolio;
	std::vector<std::string> vRows = Split(sData, "<row>");
	double nTotal = 0;
	double nSuperblockLimit = (nPaymentsLimit/COIN) * .99;
	for (int i = 0; i < vRows.size(); i++)
	{
		std::vector<std::string> vCols = Split(vRows[i], "<col>");
		if (vCols.size() > 8)
		{
			Portfolio p;
			p.OwnerAddress = vCols[0];
			p.NickName = vCols[1];
			p.AmountBBP = StringToDouble(vCols[3], 4);
			p.AmountForeign = StringToDouble(vCols[4], 4);
			p.AmountUSDBBP = StringToDouble(vCols[5], 4);
			p.AmountUSDForeign = StringToDouble(vCols[6], 4);
			p.AmountUSD = StringToDouble(vCols[7], 4);
			p.Coverage = StringToDouble(vCols[8], 4);
			p.Strength = StringToDouble(vCols[9], 4);
			p.Owed = p.Strength * nSuperblockLimit;
			nTotal += p.Strength;
			LogPrintf("\r\n Owner %s, BBP %f, Strength %f ", p.OwnerAddress, p.AmountBBP, p.Strength);
			bool fValid = ValidateAddress2(p.OwnerAddress) && p.Owed > 1;
			if (fValid)
				vPortfolio.push_back(p);
		}
	}
	if (nTotal > 1.0)
	{
		LogPrintf("\r\nERROR::GetDailySuperblock:: Superblock corrupted with more than %f payment factor. ", nTotal);
		vPortfolio.clear();
		return vPortfolio;
	}
	if (sHash.empty() || sHash.length() != 64)
	{
		LogPrintf("\r\nERROR::GetDailySuperblock:: Superblock hash empty. %f", 0);
		vPortfolio.clear();
		return vPortfolio;
	}
	/* Reserved
	uint256 sha1 = GetSHA256Hash("<data>" + sData + "</data>");
	uint256 sha2 = uint256S("0x" + sHash);
	*/
	// Mission critical todo: Before testing is over; test with 100 participants (stress test the data size)
	return vPortfolio;
}

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

double AmountToDouble(const CAmount& amount)
{
    double nAmount = (double)amount / COIN;
    return nAmount;
}

bool IsBetween(CAmount n1, CAmount n2)
{
	// Allows for floating point rounding differences which we have observed on arm64.
	if (n1 == n2)
		return true;

    if (n1 >= n2 - (ARM64() * 100) && n1 <= n2 + (ARM64() * 100))
        return true;

    if (n2 >= n1 - (ARM64() * 100) && n2 <= n1 + (ARM64() * 100))
        return true;

	return false;
}

bool ValidateDailySuperblock(const CTransaction& txNew, int nBlockHeight, int64_t nBlockTime)
{
    const Consensus::Params& consensusParams = Params().GetConsensus();
    
	if (nBlockHeight < consensusParams.BARLEY_HARVEST_HEIGHT)
		return true;

	int64_t nElapsed = GetAdjustedTime() - nBlockTime;
	
    if (nElapsed > 60 * 60 * 24 * 7) {
		// After 7 days, we no longer need to check this old superblock.  This allows us to sync from zero efficiently.
        return true;
    }
     
	CAmount nTotalPayments = 0;
	std::vector<Portfolio> vPortfolio = GetDailySuperblock(nBlockHeight);
	for (int i = 0; i < vPortfolio.size(); i++)
	{
        bool found = false; 
		std::string sRecipient1 = vPortfolio[i].OwnerAddress; 
		CAmount nAmount1 = vPortfolio[i].Owed * COIN;
		nTotalPayments += nAmount1;
	    for (const auto& txout2 : txNew.vout) 
		{
			std::string sRecipient2 = PubKeyToAddress(txout2.scriptPubKey);
			CAmount nAmount2 = txout2.nValue;
            if (sRecipient1 == sRecipient2 && IsBetween(nAmount1, nAmount2))
            {
                 found = true;
                 break;
            }
            else if (sRecipient1 == sRecipient2)
            {
                LogPrintf("\nValidateDailySuperblock::ERROR Recip %s matches, but amounts %s and %s do not match.", sRecipient1, AmountToString(nAmount1), AmountToString(nAmount2));
            }
		}
	    if (!found) 
		{
            LogPrintf("\nValidateDailySuperblock::ERROR failed-to-find expected payee %s at height %s\n", sRecipient1, nBlockHeight);
			return false;
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////////// Watchman-On-The-Wall /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//								                          			BBP's version of The Sentinel, March 31st, 2019                                                                                                  //
//                                                                                                                                                                                                                   //

/*
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
*/

uint256 GetPAMHash(std::string sAddresses, std::string sAmounts, std::string sQTPhase)
{
	std::string sConcat = sAddresses + sAmounts + sQTPhase;
	if (sConcat.empty()) return uint256S("0x0");
    uint256 h = GetSHA256Hash(sConcat);
    std::string sHash = h.GetHex();
	return uint256S("0x" + sHash);
}

uint256 GetPAMHashByContract(std::string sContract)
{
	std::string sAddresses = ExtractXML(sContract, "<ADDRESSES>","</ADDRESSES>");
	std::string sAmounts = ExtractXML(sContract, "<PAYMENTS>","</PAYMENTS>");
	std::string sQTPhase = ExtractXML(sContract, "<QTPHASE>", "</QTPHASE>");
	uint256 u = GetPAMHash(sAddresses, sAmounts, sQTPhase);
	return u;
}

bool IsOverBudget(int nHeight, int64_t nTime, std::string sAmounts)
{
	CAmount nPaymentsLimit = CSuperblock::GetPaymentsLimit(nHeight);
	if (sAmounts.empty()) return false;
	std::vector<std::string> vPayments = Split(sAmounts.c_str(), "|");
	double dTotalPaid = 0;
	for (int i = 0; i < vPayments.size(); i++)
	{
		dTotalPaid += StringToDouble(vPayments[i], 2);
	}
	if ((dTotalPaid * COIN) > nPaymentsLimit)
		return true;
	return false;
}

bool GetContractPaymentData(std::string sContract, int nBlockHeight, int nTime, std::string& sPaymentAddresses, std::string& sAmounts)
{
	CAmount nPaymentsLimit = CSuperblock::GetPaymentsLimit(nBlockHeight);
	sPaymentAddresses = ExtractXML(sContract, "<ADDRESSES>", "</ADDRESSES>");
	sAmounts = ExtractXML(sContract, "<PAYMENTS>", "</PAYMENTS>");
	std::vector<std::string> vPayments = Split(sAmounts.c_str(), "|");
	double dTotalPaid = 0;
	for (int i = 0; i < vPayments.size(); i++)
	{
		dTotalPaid += StringToDouble(vPayments[i], 2);
	}
	if (dTotalPaid < 1 || (dTotalPaid * COIN) > nPaymentsLimit)
	{
		LogPrintf("\n**GetContractPaymentData::Error::Superblock Payment Budget is out of bounds:  Limit %f,  Actual %f  ** \n", (double)nPaymentsLimit/COIN, (double)dTotalPaid);
		return false;
	}
	return true;
}

bool ChainSynced(CBlockIndex* pindex)
{
	int64_t nAge = GetAdjustedTime() - pindex->GetBlockTime();
	return (nAge > (60 * 60)) ? false : true;
}

bool SubmitGSCTrigger(std::string sHex, std::string& gobjecthash, std::string& sError)
{
    
    bool fSynced = ::masternodeSync.get()->IsBlockchainSynced();

	if(!fSynced) 
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

    DBG( std::cout << "gobject: submit "
         << " GetDataAsPlainString = " << govobj.GetDataAsPlainString()
         << ", hash = " << govobj.GetHash().GetHex()
         << ", txidFee = " << txidFee.GetHex()
         << std::endl; );

    
    auto mnList = ::deterministicMNManager.get()->GetListAtChainTip();


    bool fMnFound = mnList.HasValidMNByCollateral(activeMasternodeInfo.outpoint);
	if (!fMnFound)
	{
		sError = "Unable to find deterministic sanctuary in latest sanctuary list.";
		return false;
	}

	if (govobj.GetObjectType() == GovernanceObject::TRIGGER) 
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
        if (!govobj.IsValidLocally(strError, true)) 
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

    auto govman = ::governance.get();
    const NodeContext& node = CGlobalNode::GetGlobalNodeContext();
    
	if (fMissingConfirmations) 
	{
        govman->AddPostponedObject(govobj);
        govobj.Relay(*node.connman);
    } 
	else 
	{
        govman->AddGovernanceObject(govobj, *node.connman);
    }

	gobjecthash = govobj.GetHash().ToString();
	nLastGSCSubmitted = GetAdjustedTime();

	return true;
}


void GetGSCGovObjByHeight(int nHeight, uint256 uOptFilter, int& out_nVotes, uint256& out_uGovObjHash, std::string& out_PaymentAddresses, std::string& out_PaymentAmounts, std::string& out_qtdata)
{
	int nStartTime = 0; 
    std::vector<CGovernanceObject> objs;
    auto govman = ::governance.get();
    govman->GetAllNewerThan(objs, nStartTime);
    LOCK2(cs_main, govman->cs);
	std::string sPAM;
	std::string sPAD;
	for (const auto& pGovObj : objs) 
	{
		CGovernanceObject* myGov = govman->FindGovernanceObject(pGovObj.GetHash());
        if (myGov->GetObjectType() != GovernanceObject::TRIGGER) continue;
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
			out_PaymentAddresses = sPAD;
			out_PaymentAmounts = sPAM;
            out_nVotes = iVotes;
			out_uGovObjHash = myGov->GetHash();
			out_qtdata = sQT;
		}
	}
}

int GetHeightByEpochTime(int64_t nEpoch)
{
    CBlockIndex* pindexTip = WITH_LOCK(cs_main, return g_chainman.ActiveChain().Tip());

    if (!pindexTip) return 0;
    int nLast = pindexTip->nHeight;
    if (nLast < 1) return 0;
    for (int nHeight = nLast; nHeight > 0; nHeight--)
    {
        CBlockIndex* pindex = FindBlockByHeight(nHeight);
        if (pindex) {
            int64_t nTime = pindex->GetBlockTime();
            if (nEpoch > nTime) return nHeight;
        }
    }
    return -1;
}

BBPProposal GetProposalByHash(uint256 govObj, int nLastSuperblock)
{
    int nSancCount = ::deterministicMNManager.get()->GetListAtChainTip().GetValidMNsCount();

    auto govman = ::governance.get();

	int nMinPassing = nSancCount * .10;
	if (nMinPassing < 1) nMinPassing = 1;

    CGovernanceObject* myGov = govman->FindGovernanceObject(govObj);
	UniValue obj = myGov->GetJSONObject();
	BBPProposal bbpProposal;
	bbpProposal.sName = obj["name"].getValStr();
	bbpProposal.nStartEpoch = StringToDouble(obj["start_epoch"].getValStr(), 0);
	bbpProposal.nEndEpoch = StringToDouble(obj["end_epoch"].getValStr(), 0);
	bbpProposal.sURL = obj["url"].getValStr();
	bbpProposal.sExpenseType = obj["expensetype"].getValStr();
	bbpProposal.nAmount = StringToDouble(obj["payment_amount"].getValStr(), 2);
	bbpProposal.sAddress = obj["payment_address"].getValStr();
	bbpProposal.uHash = myGov->GetHash();
	bbpProposal.nHeight = GetHeightByEpochTime(bbpProposal.nStartEpoch);
	bbpProposal.nMinPassing = nMinPassing;
	bbpProposal.nYesVotes = myGov->GetYesCount(VOTE_SIGNAL_FUNDING);
	bbpProposal.nNoVotes = myGov->GetNoCount(VOTE_SIGNAL_FUNDING);
	bbpProposal.nAbstainVotes = myGov->GetAbstainCount(VOTE_SIGNAL_FUNDING);
	bbpProposal.nNetYesVotes = myGov->GetAbsoluteYesCount(VOTE_SIGNAL_FUNDING);
	bbpProposal.nLastSuperblock = nLastSuperblock;
	bbpProposal.sProposalHRTime = TimestampToHRDate(bbpProposal.nStartEpoch);
	bbpProposal.fPassing = bbpProposal.nNetYesVotes >= nMinPassing;
	bbpProposal.fIsPaid = bbpProposal.nHeight < nLastSuperblock;
	return bbpProposal;
}

std::string DescribeProposal(BBPProposal bbpProposal)
{
	std::string sReport = "Proposal StartDate: " + bbpProposal.sProposalHRTime + ", Hash: " + bbpProposal.uHash.GetHex() 
				+ " for Amount: " + DoubleToString(bbpProposal.nAmount, 2) + "BBP" + ", Name: " 
				+ bbpProposal.sName + ", ExpType: " + bbpProposal.sExpenseType + ", PAD: " + bbpProposal.sAddress 
				+ ", Height: " + DoubleToString(bbpProposal.nHeight, 0) 
				+ ", Votes: " + DoubleToString(bbpProposal.nNetYesVotes, 0) + ", LastSB: " 
				+ DoubleToString(bbpProposal.nLastSuperblock, 0);
	return sReport;
}

void GetGovSuperblockHeights(int& nNextSuperblock, int& nLastSuperblock)
{
    CBlockIndex* pindexTip = g_chainman.ActiveChain().Tip();

    int nBlockHeight = pindexTip->nHeight;

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

std::vector<BBPProposal> GetWinningSanctuarySporkProposals()
{
    auto govman = ::governance.get();

	int nStartTime = GetAdjustedTime() - (86400 * 7);
	// NOTE: Sanctuary sporks occur every week, and expire 7 days after creation.  They should be voted on regularly.
	int nLastSuperblock = 0;
	int nNextSuperblock = 0;
	GetGovSuperblockHeights(nNextSuperblock, nLastSuperblock);

    
    LOCK2(cs_main, govman->cs);

    std::vector<CGovernanceObject> objs;
    govman->GetAllNewerThan(objs, nStartTime);
	std::vector<BBPProposal> vSporks;
	for (CGovernanceObject GovObj : objs) 
    {
        if (GovObj.GetObjectType() != GovernanceObject::PROPOSAL) continue;

		BBPProposal bbpProposal = GetProposalByHash(GovObj.GetHash(), nLastSuperblock);
		// We need proposals that are sporks, that are older than 48 hours that are not expired
		int64_t nAge = GetAdjustedTime() - bbpProposal.nStartEpoch;
		if (bbpProposal.sExpenseType == "XSPORK-ORPHAN" || bbpProposal.sExpenseType == "XSPORK-CHARITY" || bbpProposal.sExpenseType == "XSPORK-EXPENSE" || bbpProposal.sExpenseType == "SPORK")
		{
			if (nAge > (60*60*24*1) && bbpProposal.fPassing)
			{
				// spork elements are contained in bbpProposal.sName, and URL in .sURL
				vSporks.push_back(bbpProposal);
				LogPrintf("\nSporkProposal Detected %s ", bbpProposal.sName);
			}
		}
	}
	return vSporks;
}

std::string SerializeSanctuaryQuorumTrigger(int iContractAssessmentHeight, int nEventBlockHeight, int64_t nTime, std::string sContract)
{
	std::string sEventBlockHeight = DoubleToString(nEventBlockHeight, 0);
	std::string sPaymentAddresses;
	std::string sPaymentAmounts;
	// For Evo compatibility and security purposes, we move the QT Phase into the GSC contract so all sancs must agree on the phase
	std::string sQTData = ExtractXML(sContract, "<QTDATA>", "</QTDATA>");
	std::string sHashes = ExtractXML(sContract, "<PROPOSALS>", "</PROPOSALS>");
	bool bStatus = GetContractPaymentData(sContract, iContractAssessmentHeight, nTime, sPaymentAddresses, sPaymentAmounts);
	if (!bStatus) 
	{
		LogPrintf("\nERROR::SerializeSanctuaryQuorumTrigger::Unable to Serialize %f", 1);
		return std::string();
	}
	std::string sVoteData = ExtractXML(sContract, "<VOTEDATA>", "</VOTEDATA>");
	std::string sSporkData = ExtractXML(sContract, "<SPORKS>", "</SPORKS>");
	
	std::string sProposalHashes = GetPAMHashByContract(sContract).GetHex();
	if (!sHashes.empty())
		sProposalHashes = sHashes;
	std::string sType = "2"; // GSC Trigger is always 2
	std::string sQ = "\"";
	std::string sJson = "[[" + sQ + "trigger" + sQ + ",{";
	sJson += GJE("event_block_height", sEventBlockHeight, true, false); // Must be an int
	sJson += GJE("start_epoch", DoubleToString(GetAdjustedTime(), 0), true, false);
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
	std::string sHex = HexStrBBP(vchJson.begin(), vchJson.end());
	return sHex;
}

bool VoteForGobject(uint256 govobj, std::string sVoteSignal, std::string sVoteOutcome, std::string& sError)
{
    auto govman = ::governance.get();

	if (sVoteSignal != "funding" && sVoteSignal != "delete")
	{
		LogPrintf("Sanctuary tried to vote in a way that is prohibited.  Vote failed. %s", sVoteSignal);
		return false;
	}

	vote_signal_enum_t eVoteSignal = CGovernanceVoting::ConvertVoteSignal(sVoteSignal);
	vote_outcome_enum_t eVoteOutcome = CGovernanceVoting::ConvertVoteOutcome(sVoteOutcome);
	int nSuccessful = 0;
	int nFailed = 0;
	GovernanceObject govObjType;
	{
        CGovernanceObject *pGovObj = govman->FindGovernanceObject(govobj);
        if (!pGovObj) 
		{
			sError = "Governance object not found";
			return false;
        }
        govObjType = pGovObj->GetObjectType();
    }

    CDeterministicMNCPtr dmn = ::deterministicMNManager.get()->GetListAtChainTip().GetValidMNByCollateral(activeMasternodeInfo.outpoint);


    if (!dmn) 
	{
        sError = "Can't find masternode by collateral output";
		return false;
    }

    CGovernanceVote vote(dmn->collateralOutpoint, govobj, eVoteSignal, eVoteOutcome);

    bool signSuccess = false;
    if (govObjType == GovernanceObject::PROPOSAL && eVoteSignal == VOTE_SIGNAL_FUNDING)
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

    const NodeContext& node = CGlobalNode::GetGlobalNodeContext();
    
    if (govman->ProcessVoteAndRelay(vote, exception, *node.connman)) 
	{
        nSuccessful++;
    }
    else
    {
        nFailed++;
    }

    return (nSuccessful > 0) ? true : false;
   
}

std::vector<std::pair<int64_t, uint256>> GetGSCSortedByGov(int nHeight, uint256 inPamHash, bool fIncludeNonMatching)
{
	int nStartTime = 0;
    auto govman = ::governance.get();
        
    LOCK2(cs_main, govman->cs);
    std::vector<CGovernanceObject> objs;
    govman->GetAllNewerThan(objs, nStartTime);

	std::string sPAM;
	std::string sPAD;
	std::vector<std::pair<int64_t, uint256> > vPropByGov;
	vPropByGov.reserve(objs.size() + 1);
	int iOffset = 0;
	for (const auto& pGovObj : objs) 
	{
		CGovernanceObject* myGov = govman->FindGovernanceObject(pGovObj.GetHash());
        if (myGov->GetObjectType() != GovernanceObject::TRIGGER) continue;
		UniValue obj = myGov->GetJSONObject();
		int nLocalHeight = obj["event_block_height"].get_int();
		if (nLocalHeight == nHeight)
		{
			iOffset++;
			// Resilience
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

bool VoteForGSCContract(int nHeight, std::string sMyContract, std::string& sError)
{
	int iPendingVotes = 0;
	uint256 uGovObjHash;
	std::string sPaymentAddresses;
	std::string sAmounts;
	std::string sQTData;
	uint256 uPamHash = GetPAMHashByContract(sMyContract);
    auto govman = ::governance.get();

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
    int nSancCount = ::deterministicMNManager.get()->GetListAtChainTip().GetValidMNsCount();

    
	for (int i = 0; i < vPropByGov.size(); i++)
	{    
		CGovernanceObject* myGov = govman->FindGovernanceObject(vPropByGov[i].second);
		sAction = (i==0) ? "yes" : "no";
		if (fOverBudget) 
			sAction = "no";
		iVotes = myGov->GetAbsoluteYesCount(VOTE_SIGNAL_FUNDING);
        bool fSkipVote = false;

        if (sAction == "yes" && iVotes > nSancCount)
        {
            fSkipVote = true;
        }

		LogPrintf("\nSmartContract-Server::VoteForGSCContractOrderedByHash::Voting %s for govHash %s, with pre-existing-votes %f (created %f) Overbudget %f ",
			sAction, myGov->GetHash().GetHex(), iVotes, myGov->GetCreationTime(), (double)fOverBudget);
        if (!fSkipVote) {
                VoteForGobject(myGov->GetHash(), "funding", sAction, sError);
                // Additionally, clear the delete flag, just in case another node saw this contract as a negative earlier in the cycle
                VoteForGobject(myGov->GetHash(), "delete", "no", sError);
        }
		break;
	}
	// Phase 2: Vote against contracts at this height that do not match our hash
	int iVotedNo = 0;
	if (uPamHash != uint256S("0x0"))
	{
		vPropByGov = GetGSCSortedByGov(nHeight, uPamHash, true);
		for (int i = 0; i < vPropByGov.size(); i++)
		{
			CGovernanceObject* myGovForRemoval = govman->FindGovernanceObject(vPropByGov[i].second);
			sAction = "no";
            bool fSkipVote = false;
            int iVotes = myGovForRemoval->GetAbsoluteYesCount(VOTE_SIGNAL_FUNDING);
            
            if (sAction == "no" && iVotes < (nSancCount * -1))
            {
                  fSkipVote = true;
            }
            if (!fSkipVote)
            {
                LogPrintf("\nSmartContract-Server::VoteDownBadGCCContracts::Voting %s for govHash %s, with pre-existing-votes %f (created %f)",
                                          sAction, myGovForRemoval->GetHash().GetHex(), iVotes, myGovForRemoval->GetCreationTime());
                VoteForGobject(myGovForRemoval->GetHash(), "funding", sAction, sError);
                iVotedNo++;
                if (iVotedNo > 2)
                        break;
            }
		}
	}

	return sError.empty() ? true : false;
}

static uint256 LAST_CREATED_GSC_CONTRACT_HASH = uint256S("0x0");
std::string WatchmanOnTheWall(bool fForce, std::string& sContract)
{
    CBlockIndex* pindexTip = WITH_LOCK(cs_main, return g_chainman.ActiveChain().Tip());
    auto govman = ::governance.get();

	if (pindexTip->nHeight % 5 != 0 && !fForce)
		return "WAITING...";
	if (!fMasternodeMode && !fForce)   
		return "NOT_A_WATCHMAN_SANCTUARY";
	if (!pindexTip) 
		return "WATCHMAN_INVALID_CHAIN";
	if (!ChainSynced(pindexTip))
		return "WATCHMAN_CHAIN_NOT_SYNCED";

    if (GetSanctuaryCollateralAmount() != SANCTUARY_COLLATERAL_TEMPLE * COIN)
    {
        return "NOT_A_WATCHMAN_TEMPLE";
    }

	const Consensus::Params& consensusParams = Params().GetConsensus();
	int MIN_EPOCH_BLOCKS = consensusParams.nSuperblockCycle * .07; // TestNet Weekly superblocks (1435), Prod Monthly superblocks (6150), this means a 75 block warning in TestNet, and a 210 block warning in Prod

	int nLastSuperblock = 0;
	int nNextSuperblock = 0;
	GetGovSuperblockHeights(nNextSuperblock, nLastSuperblock);

    // MISSION CRITICAL TODO HERE - CRASHING BECAUSE GETGLOBALNODECONTEXT RETURNS NULL.

	int nSancCount = ::deterministicMNManager.get()->GetListAtChainTip().GetValidMNsCount();

	std::string sReport;

	int nBlocksUntilEpoch = nNextSuperblock - pindexTip->nHeight;
	if (nBlocksUntilEpoch < 0)
		return "WATCHMAN_LOW_HEIGHT";

	if (nBlocksUntilEpoch < MIN_EPOCH_BLOCKS && !fForce)
		return "WATCHMAN_TOO_EARLY_FOR_COMING";

	int nStartTime = GetAdjustedTime() - (86400 * 32);

    std::vector<CGovernanceObject> objs;
    govman->GetAllNewerThan(objs, nStartTime);

	std::vector<std::pair<int, uint256> > vProposalsSortedByVote;
	vProposalsSortedByVote.reserve(objs.size() + 1);
    
	for (CGovernanceObject GovObj : objs) 
    {
        if (GovObj.GetObjectType() != GovernanceObject::PROPOSAL) continue;
		BBPProposal bbpProposal = GetProposalByHash(GovObj.GetHash(), nLastSuperblock);
		// We need unpaid, passing that fit within the budget
		sReport = DescribeProposal(bbpProposal);
		if (!bbpProposal.fIsPaid)
		{
			if (bbpProposal.fPassing)
			{
				LogPrintf("\n Watchman::Inserting %s for NextSB: %f", sReport, (double)nNextSuperblock);
				vProposalsSortedByVote.push_back(std::make_pair(bbpProposal.nNetYesVotes, bbpProposal.uHash));
			}
			else
			{
				LogPrintf("\n Watchman (not inserting) %s because we have Votes %f (req votes %f)", sReport, bbpProposal.nNetYesVotes, bbpProposal.nMinPassing);
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
    
	CAmount nPaymentsLimit = CSuperblock::GetPaymentsLimit(nNextSuperblock);
	CAmount nSpent = 0;
	for (auto item : vProposalsSortedByVote)
    {
		BBPProposal p = GetProposalByHash(item.second, nLastSuperblock);
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
		BBPProposal p = GetProposalByHash(item.second, nLastSuperblock);
		if (ValidateAddress2(p.sAddress) && p.nAmount > .01)
		{
			sAddresses += p.sAddress + "|";
			sPayments += DoubleToString(p.nAmount, 2) + "|";
			sHashes += p.uHash.GetHex() + "|";
			sVotes += DoubleToString(p.nNetYesVotes, 0) + "|";
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
	sContract += "<VOTES>" + DoubleToString(iTriggerVotes, 0) + "</VOTES><METRICS><HASH>" + uGovObjHash.GetHex() + "</HASH><PAMHASH>" 
		+ uPamHash.GetHex() + "</PAMHASH><SANCTUARYCOUNT>" + DoubleToString(nSancCount, 0) + "</SANCTUARYCOUNT></METRICS><VOTEDATA>" + sVotes + "</VOTEDATA>";

    // BBP: Do not allow the node to keep creating the same contract over and over.

	if (uGovObjHash == uint256S("0x0") && uPamHash != LAST_CREATED_GSC_CONTRACT_HASH)
	{
        LAST_CREATED_GSC_CONTRACT_HASH = uPamHash;
		std::string sWatchmanTrigger = SerializeSanctuaryQuorumTrigger(nNextSuperblock, nNextSuperblock, GetAdjustedTime(), sContract);
		std::string sGobjectHash;
		SubmitGSCTrigger(sWatchmanTrigger, sGobjectHash, sError);
		LogPrintf("**WatchmanOnTheWall::SubmitWatchmanTrigger::CreatingWatchmanContract out_gov_hash %s, gobject %s, pamhash %s, results %s **\n",
            sWatchmanTrigger, sGobjectHash, uPamHash.GetHex(), sError);
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
    if (iTriggerVotes >= (nSancCount / 2))
    {
        LogPrintf("**WatchmanOnTheWall::DORMANT::Not voting for anything::Sanc Ct %f  Trigger Votes %f",
                          nSancCount, (double)iTriggerVotes);
    }

	return "WATCHMAN_SUCCESS";
}


//////////////////////////////////////////////////////////////////////////////// End of Watchman On The Wall ////////////////////////////////////////////////////////////////////////////////////////////////


bool CheckStakeSignature(std::string sBitcoinAddress, std::string sSignature, std::string strMessage, std::string& strError)
{
	CTxDestination destAddr2 = DecodeDestination(sBitcoinAddress);
	bool isValid = IsValidDestination(destAddr2);

	if (!isValid) 
	{
		strError = "Invalid address";
		return false;
	}
	if (sSignature.empty() || sBitcoinAddress.empty() || strMessage.empty())
	{
		strError = "Vitals empty.";
		return false;
	}

	bool fInvalid = false;
	std::vector<unsigned char> vchSig2 = DecodeBase64(sSignature.c_str(), &fInvalid);
	if (fInvalid)
	{
		strError = "Malformed base64 encoding";
		return false;
	}
	CHashWriter ss2(SER_GETHASH, 0);
	ss2 << MESSAGE_MAGIC_BBP;
	ss2 << strMessage;
	
	CPubKey pubkey2;
    if (!pubkey2.RecoverCompact(ss2.GetHash(), vchSig2)) 
	{
		strError = "Unable to recover public key.";
		return false;
	}
    bool fSuccess = (EncodeDestination(PKHash(pubkey2.GetID())) == sBitcoinAddress);
	return fSuccess;
}

std::string GetTransactionMessage(CTransactionRef tx)
{
    std::string sMsg;
    for (unsigned int i = 0; i < tx->vout.size(); i++) {
        sMsg += tx->vout[i].sTxOutMessage;
    }
    return sMsg;
}

std::string ScanChainForData(int nHeight)
{
    CBlockIndex* pindexTip = g_chainman.ActiveChain().Tip();
    int nMaxDepth = nHeight;
    int nMinDepth = nHeight - (BLOCKS_PER_DAY * 2);
	if (nMaxDepth > pindexTip->nHeight)
		nMaxDepth = pindexTip->nHeight;
	if (nMinDepth > pindexTip->nHeight)
	{
		return "";
	}
    CBlockIndex* pindex = FindBlockByHeight(nMaxDepth);

    const Consensus::Params& consensusParams = Params().GetConsensus();
	std::string sDataOut;

    while (pindex && pindex->nHeight >= nMinDepth) 
	{
        CBlock block;
        if (ReadBlockFromDisk(block, pindex, consensusParams)) 
		{
            if (pindex->nHeight % 100 == 0)
			{
               // LogPrintf("SCFD %f @ %f, ", pindex->nHeight, GetAdjustedTime());
			}
            for (unsigned int n = 0; n < block.vtx.size(); n++) 
			{
				std::string sData = GetTransactionMessage(block.vtx[n]);
				std::string sMsg = ExtractXML(sData, "<BOMSG>", "</BOMSG>");
				std::string sBOSig = ExtractXML(sData, "<BOSIG>", "</BOSIG>");
				std::string sMsgKey = ExtractXML(sData, "<MK>", "</MK>");
				std::string sMV = ExtractXML(sData, "<MV>", "</MV>");
				if (sMsgKey == "GSC")
				{
					std::string sError;
					bool fPassed = CheckStakeSignature(consensusParams.FoundationAddress, sBOSig, sMsg, sError);
					if (fPassed)
					{
						// GSC data is signed, in chain, hard (not dynamic) at the *earliest* height
						int nGSCHeight = (int)StringToDouble(ExtractXML(sData, "<height>", "</height>"), 0);
						if (nGSCHeight == nHeight)
						{
							// NOTE here, we deliberately return the *earliest* data (first in chain wins).
							sDataOut = sData;
						}
					}
				}
		    }
		}
        if (pindex->pprev)
		{
			pindex = pindex->pprev;
		}
		else
		{
			break;
		}
    }
	return sDataOut;
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

std::string AmtToString(CAmount nAmount)
{
    std::string s = strprintf("%d", nAmount);
    return s;
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

bool CompareMask2(CAmount nAmount, double nMask)
{
    if (nMask == 0)
        return false;
    std::string sMask = DoubleToString(nMask, 0);
    bool fZero = false;
    if (sMask.length() == 5) {
        if (Mid(sMask, 4, 1) == "0")
            fZero = true;
        nMask = StringToDouble(Mid(sMask, 0, 4), 0);
    }
    std::string sFull = AmtToString(nAmount);
    std::string sSec = DoubleToString(nMask, 0);
    bool fExists = Contains(sFull, sSec);
    return fExists;
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

double AddressToPinV2(std::string sUnchainedAddress, std::string sCryptoAddress)
{
    std::string sAddress = sUnchainedAddress + sCryptoAddress;

    if (sAddress.length() < 20)
        return -1;

    uint256 h = GetSHA256Hash(sAddress);
    std::string sHash = h.GetHex();
    std::string sMath5 = sHash.substr(0, 5); // 0 - 1,048,575
    double d = ConvertHexToDouble2("0x" + sMath5) / 11.6508;

    int nMin = 10000;
    int nMax = 99999;
    d += nMin;

    if (d > nMax)
        d = nMax;

    d = std::floor(d);
    return d;

    // Why a 5 digit pin?  This reduces the price impact biblepay consumes in expensive utxo stakes.
}

void LockStakes()
{
    /* WAITING TO SEE IF WE NEED THIS OR NOT
    *
    CWallet* pwallet = GetInternalWallet();
	pwallet->LockByMask(sUA);
    */
}


const CBlockIndex* GetBlockIndexByTransactionHash(const uint256& hash)
{
    CBlockIndex* pindexHistorical;
    CTransactionRef tx1;
    uint256 hashBlock1;
    const CoreContext& cc = CGlobalNode::GetGlobalCoreContext();
    JSONRPCRequest r0(cc);
    const NodeContext& node = EnsureAnyNodeContext(r0.context);
    const CTxMemPool& mempool = EnsureMemPool(node);
    const CTxMemPool* ptrMemPool = &mempool;

    CTransactionRef tx = GetTransaction(nullptr, ptrMemPool, hash, Params().GetConsensus(), hashBlock1);
    if (tx)
    {
        const CBlockIndex* pindexOut = g_chainman.m_blockman.LookupBlockIndex(hashBlock1);
        if (pindexOut)
        {
            return pindexOut;
        }
    }
    return pindexHistorical;
}

std::string GetElement(std::string sData, std::string sDelimiter, int iPos)
{
    std::vector<std::string> vData = Split(sData.c_str(), sDelimiter);
    if (iPos > vData.size()-1)
    {
        return "";
    }
    return vData[iPos];
}

std::tuple<std::string, std::string, std::string> GetPOVSURL(std::string sSanctuaryPubKey, std::string sSanctuaryIPIN, int iType)
{
    std::string sURL = "https://";
    std::string sDomain = "";
	const CChainParams& chainparams = Params();
    std::string sNetworkID = chainparams.NetworkIDString();
    std::string sSanctuaryIP = GetElement(sSanctuaryIPIN, ":", 0);

    if (sDomain.empty())
	{
        sDomain = sSanctuaryIP;
	}
    sURL += sDomain;
    if (sSanctuaryPubKey.empty())
        return std::make_tuple("", "", "");
    std::string sPrefix = sSanctuaryPubKey.substr(0, std::min((int)sSanctuaryPubKey.length(), 8));
    std::string sPage = "";
    if (iType == 0)
    { 
      sPage = "BMS/POSE";
    }
    else if (iType == 1)
    {
      sPage = "BMS/VideoList";
    }
    else if (iType==2)
    {
      sPage = "BMS/Status";
    }
    return std::make_tuple(sURL, sPage, sPrefix);
}

static std::string msChecksumKey;
bool POVSTest(std::string sSanctuaryPubKey, std::string sIPIN, int64_t nTimeout, int nType)
{
    std::string sIP = GetElement(sIPIN, ":", 0); // Remove the Port
    int nPort = StringToDouble(GetElement(sIPIN, ":", 1), 0);
    // As of September 2023, first we verify the sanc is on an approved port
    // POVS requires that the sanctuary runs on port 40000,40001,10001,10002,10003,10004...
    bool fPortPassed = false;
    if (nPort == 40000 || nPort == 40001 || (nPort >= 10000 && nPort <= 10100))
        fPortPassed=true;
    if (!fPortPassed)
        return false;
    // Second, we verify the sanc is up and running
    bool fOK = TcpTest(sIP, nPort, 9);
    return fOK;
}

std::string GetSANDirectory1()
{
	boost::filesystem::path pathConfigFile = GetDataDir(false) / "conf.dat";
    boost::filesystem::path dir = pathConfigFile.parent_path();
    std::string sDir = dir.string() + "/SAN/";
    boost::filesystem::path pathSAN(sDir);
    if (!boost::filesystem::exists(pathSAN)) {
        boost::filesystem::create_directory(pathSAN);
    }
    return sDir;
}

void SerializeSidechainToFile(int nHeight)
{
    if (nHeight < 100) 
		return;
    std::string sPort = gArgs.GetArg("-port", "0");
	const CChainParams& chainparams = Params();

	std::string sNetworkName = chainparams.NetworkIDString();
    std::string sTarget = GetSANDirectory1() + "sidechain_" + sPort + sNetworkName;
    FILE* outFile = fopen(sTarget.c_str(), "w");
    LogPrintf("Serializing Sidechain... %s at %f", sTarget, GetAdjustedTime());
    for (auto ii : mapSidechain) 
	{
        Sidechain s = mapSidechain[ii.first];
        std::string sRow = DoubleToString(nHeight, 0) + "<col-sidechain>" + s.TXID
            + "<col-sidechain>" + s.ObjectType + "<col-sidechain>" + s.URL + "<col-sidechain>" 
			+ DoubleToString(s.Time, 0) + "<col-sidechain>" + DoubleToString(s.Height, 0) + "<row-sidechain>";
        sRow = strReplace(sRow, "\r", "[~r]");
        sRow = strReplace(sRow, "\n", "[~n]");
        sRow += "\r\n";
        fputs(sRow.c_str(), outFile);
    }
    LogPrintf("...Done Serializing Sidechain... %f ", GetAdjustedTime());
    fclose(outFile);
}

int DeserializeSidechainFromFile()
{
	const CChainParams& chainparams = Params();
	std::string sNetworkName = chainparams.NetworkIDString();
    std::string sPort = gArgs.GetArg("-port", "0");
    std::string sSource = GetSANDirectory1() + "sidechain_" + sPort + sNetworkName;
    LogPrintf("\nDeserializing sidechain from file %s at %f", sSource, GetAdjustedTime());
    boost::filesystem::path pathIn(sSource);
    std::ifstream streamIn;
    streamIn.open(pathIn.string().c_str());
    if (!streamIn) return -1;
    std::string line;
    int iRows = 0;
    int nHeight = 0;
    while (std::getline(streamIn, line)) {
        line = strReplace(line, "[~r]", "\r");
        line = strReplace(line, "[~n]", "\n");
        std::vector<std::string> vRows = Split(line.c_str(), "<row-sidechain>");
        for (int i = 0; i < (int)vRows.size(); i++) {
            std::vector<std::string> vCols = Split(vRows[i].c_str(), "<col-sidechain>");
            if (vCols.size() > 4) 
			{
				Sidechain s;
                s.TXID = vCols[1];
				s.ObjectType = vCols[2];
				s.URL = vCols[3];
				s.Time = (int64_t)StringToDouble(vCols[4], 0);
				s.Height = StringToDouble(vCols[5], 0);
                if (s.Height > nHeight) {
                         nHeight = s.Height;
                }
				mapSidechain[s.Time] = s;
                iRows++;
            }
        }
    }
    streamIn.close();
    LogPrintf(" Processed %f sidechain rows - %f\n", iRows, GetAdjustedTime());
    return nHeight;
}


void MemorizeSidechain(bool fDuringConnectBlock, bool fColdBoot)
{
    int nDeserializedHeight = 0;

    CBlockIndex* pindexActive = g_chainman.ActiveChain().Tip();
    if (!pindexActive) {
        return;
    }
    
    if (fColdBoot) {
        nDeserializedHeight = DeserializeSidechainFromFile();
        if (pindexActive->nHeight < nDeserializedHeight && nDeserializedHeight > 0) {
            LogPrintf(" Chain Height %f, Loading entire sidechain index\n", pindexActive->nHeight);
            nDeserializedHeight = 0;
        }
    }

    int nMaxDepth = pindexActive->nHeight;

    int nMinDepth = fDuringConnectBlock ? nMaxDepth : nMaxDepth - (BLOCKS_PER_DAY * 30 * 12 * 7); // Seven years
    if (nDeserializedHeight > 0 && nDeserializedHeight < nMaxDepth)
        nMinDepth = nDeserializedHeight;
    if (nMinDepth < 0)
        nMinDepth = 0;
    CBlockIndex* pindex = FindBlockByHeight(nMinDepth);
    const Consensus::Params& consensusParams = Params().GetConsensus();
    while (pindex && pindex->nHeight <= nMaxDepth)
    {
        if (!pindex)
            break;

        if (pindex->nHeight < pindexActive->nHeight)
            pindex = GetNextBlockIndex(pindex);

        CBlock block;
        if (ReadBlockFromDisk(block, pindex, consensusParams)) {
            if (pindex->nHeight % 25000 == 0)
                LogPrintf(" MSC %f @ %f, ", pindex->nHeight, GetAdjustedTime());
            for (unsigned int n = 0; n < block.vtx.size(); n++) 
			{
                double dTotalSent = 0;
                std::string sTxMsg = "";
                double dFoundationDonation = 0;
                CAmount nTotalBurned = 0;
                for (unsigned int i = 0; i < block.vtx[n]->vout.size(); i++) 
				{
                    sTxMsg += block.vtx[n]->vout[i].sTxOutMessage;
                }
				std::string sSC = ExtractXML(sTxMsg, "<sc>", "</sc>");
				if (!sSC.empty())
				{
					Sidechain s;
					s.ObjectType = ExtractXML(sSC, "<objtype>", "</objtype>");
					s.URL = ExtractXML(sSC, "<url>", "</url>");
					s.Time = block.GetBlockTime();
					s.Height = pindex->nHeight;
                    s.TXID = block.vtx[n]->GetHash().GetHex();
					mapSidechain[s.Time] = s;
					LogPrintf("Processing Sidechain TXID %s [%s] URL [%s] sz %f ", s.TXID, sSC, s.URL, (double)mapSidechain.size());
				}
            }
        }

        if (pindex->nHeight == pindexActive->nHeight) {
            break;
        }
    }
    if (fColdBoot) {
        if (nMaxDepth > (nDeserializedHeight - 1000)) {
            SerializeSidechainToFile(nMaxDepth - 1);
        }
    }
}

CAmount ARM64()
{
    // If biblepay is compiled for ARM64, there is a floating point math problem that results in the block.vtx[0]->GetValueOut() being 1/10000000 satoshi higher than the block subsidy limit (for example actual=596050766864 vs limit=596050766863)
    // To deal with this we are looking in biblepay.conf for the 'arm64=1' setting.
    // If set, we pass a value back that is added to the blockReward for consensus purposes.
    // We are passing back 1 * COIN to allow the Subsidy max to be greater than the value out.
    CAmount nARM = 1 * COIN;
    return nARM;
}

int ConvertBase58ToInt(std::string sInput)
{
    std::string sBase58 = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    for (int i = 0; i < sBase58.length(); i++)
    {
        std::string sChar = sBase58.substr(i, 1);
        if (sInput == sChar)
           return i + 1;
    }
    return 0;
}

uint64_t ConvertDateToTimeStamp(int nMonth, int nYear)
{
    time_t rawtime;
    struct tm * timeinfo;
    time ( &rawtime ); 
    timeinfo = localtime ( &rawtime ); 
    timeinfo->tm_year   = nYear - 1900;
    timeinfo->tm_mon    = nMonth - 1;    // months since January - [0,11]
    timeinfo->tm_mday   = 1;             // day of the month - [1,31] 
    timeinfo->tm_hour   = 1;             // hours since midnight - [0,23]
    timeinfo->tm_min    = 1;             // minutes after the hour - [0,59]
    timeinfo->tm_sec    = 1;             // seconds after the minute - [0,59]
    int64_t nStamp = mktime(timeinfo);
    return nStamp;
}

uint64_t IsHODLAddress(std::string sAddress)
{
    // If this is not a HODL, return 0
    int nHODL = sAddress.find("HD");
    if (nHODL == std::string::npos)
        return 0;
    if (nHODL + 3 > sAddress.length() -1)
        return 0;
    std::string sMM = sAddress.substr(nHODL+2, 1);
    std::string sYY = sAddress.substr(nHODL+3, 1);
    int nMM = ConvertBase58ToInt(sMM);
    int nYY = ConvertBase58ToInt(sYY) + 2020;
    if (nMM < 1 || nMM > 12)
       return 0;
    // This is a HODL wallet address, convert the timestamp to a unix timestamp (the MaturityDate)
    int64_t nMaturityDate = ConvertDateToTimeStamp(nMM, nYY);
    return nMaturityDate;
}

bool CheckTLTTx(const CTransaction& tx, const CCoinsViewCache& view)
{
    // If this is a Time Locked Trust Wallet, reject early spends
    for (unsigned int k = 0; k < tx.vin.size(); k++) 
    {
         const Coin &coin = view.AccessCoin(tx.vin[k].prevout);
         const CTxOut &txOutPrevOut = coin.out;
         std::string sFromAddress = PubKeyToAddress(txOutPrevOut.scriptPubKey);
         double nTime = IsHODLAddress(sFromAddress);
         if (nTime > 0 && nTime > GetAdjustedTime())
         {
             std::string sMaturity = TimestampToHRDate(nTime);
             LogPrintf("AccptToMemoryPool::CheckTLTTx::Tx Rejected; Maturity %f, Amount %f, Address %s, MaturityDate %s ", 
             (double)nTime, (double)txOutPrevOut.nValue/COIN, sFromAddress, sMaturity);
             return false;
         }
    }
    return true;
}

CAmount GetSanctuaryCollateralAmount()
{
    auto mnList = ::deterministicMNManager.get()->GetListAtChainTip();
    if (fMasternodeMode)
    {
         CDeterministicMNCPtr dmn{nullptr};
         dmn = mnList.GetMNByCollateral(activeMasternodeInfo.outpoint);
         if (dmn)
         {
             return dmn->GetCollateralAmount();
         }
    }
    else
    {
         return 0;
    }
    return 0;
}


std::string GetSanctuaryMiningAddress()
{
    
    auto mnList = ::deterministicMNManager.get()->GetListAtChainTip();

    if (fMasternodeMode)
    {
        CDeterministicMNCPtr dmn{nullptr};
        dmn = mnList.GetMNByCollateral(activeMasternodeInfo.outpoint);
        if (dmn) 
        {
            CTxDestination dest;
            if (ExtractDestination(dmn->pdmnState->scriptPayout, dest)) 
            {
                return EncodeDestination(dest);
            }
        }
    }
    else
    {
        std::string sPayout = mnList.GetFirstMNPayoutAddress();
        if (!sPayout.empty())
        {
            return sPayout;
        }
    }
    // No sancs found
    const Consensus::Params& consensusParams = Params().GetConsensus();
	return consensusParams.FoundationAddress;
}


std::string url_encode(std::string value)
{
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    int n = 0;
    for (std::string::const_iterator i = value.begin(), n = value.end(); i != n; ++i) {
        std::string::value_type c = (*i);
        // Keep alphanumeric and other accepted characters intact
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
            continue;
        }
        // Any other characters are percent-encoded
        escaped << std::uppercase;
        escaped << '%' << std::setw(2) << int((unsigned char)c);
        escaped << std::nouppercase;
    }
    return escaped.str();
}

std::string ScanDeterministicConfigFile(std::string sSearch)
{
    // Scans by friendly sanc name, or ProTxHash
    int linenumber = 1;
    boost::filesystem::path pathDeterministicFile = GetDeterministicConfigFile();
    boost::filesystem::ifstream streamConfig(pathDeterministicFile);
    if (!streamConfig.good())
        return std::string();
    //Format: Sanctuary_Name IP:port(40000=prod,40001=testnet) BLS_Public_Key BLS_Private_Key Collateral_output_txid Collateral_output_index Pro-Registration-TxId Pro-Reg-Collateral-Address Pro-Reg-Se$
    for (std::string line; std::getline(streamConfig, line); linenumber++) 
    {
        if (line.empty()) continue;
        std::vector<std::string> vSanc = Split(line.c_str(), " ");
        if (vSanc.size() >= 9) 
        {
            std::string sanctuary_name = vSanc[0];
            std::string sProRegTxId = vSanc[8];
            if (sanctuary_name.at(0) == '#') continue;
            if (sanctuary_name == sSearch) {
                streamConfig.close();
                return line;
            }
            if (sProRegTxId == sSearch) {
               streamConfig.close();
               return line;
            }
            LogPrintf("\r\nEnumberiating %s %s", sanctuary_name, sProRegTxId);
        }
    }
    streamConfig.close();
    return std::string();
}

bool ReviveSanctuaryEnhanced(JSONRPCRequest rold, std::string sSancSearch, std::string& sError, UniValue& uSuccess)
{
    sError = std::string();
    std::string sSanc = ScanDeterministicConfigFile(sSancSearch);
    if (sSanc.empty()) {
        sError = "Unable to find sanctuary " + sSancSearch + " in deterministic.conf file.";
        return false;
    }

    std::vector<std::string> vSanc = Split(sSanc.c_str(), " ");
    if (vSanc.size() < 9) 
    {
        sError = "Sanctuary entry in deterministic.conf corrupted (does not contain at least 9 parts.) Format should be: Sanctuary_Name IP:port(40000=prod,40001=testnet) BLS_Public_Key BLS_Private_Key Collateral_output_txid Collateral_output_index Pro-Registration-TxId Pro-Reg-Collateral-Address Pro-Reg-funding-sent-txid.";
        return false;
    }
    std::string sSancName = vSanc[0];
    std::string sSancIP = vSanc[1];
    std::string sBLSPrivKey = vSanc[3];
    std::string sProRegTxId = vSanc[8];

    std::string sSummary = "Creating protx update_service command for Sanctuary " + sSancName + " with IP " + sSancIP + " with origin pro-reg-txid=" + sProRegTxId;
    sSummary += "(protx update_service " + sProRegTxId + " " + sSancIP + " " + sBLSPrivKey + ").";
    LogPrintf("\nCreating ProTx_Update_service %s for Sanc [%s].\n", sSummary, sSanc);
    
    JSONRPCRequest newRequest(rold);

    newRequest.params.setArray(); //protx update_service proTxHash IpPort OpKey OpPayoutAddr feeScAddress
    //newRequest.params.push_back("update_service");
    newRequest.params.push_back(sProRegTxId);
    newRequest.params.push_back(sSancIP);
    newRequest.params.push_back(sBLSPrivKey);
    // Fee source address does not need to be specified
    /*
    newRequest.params.push_back("");
    std::string sCPK = DefaultRecAddress(rold,"Christian-Public-Key");
    newRequest.params.push_back(sCPK);
    */
    
    uSuccess = protx_update_service_common_wrapper(newRequest, g_chainman, MnType::Regular);

    // If we made it this far and an error was not thrown:
    LogPrintf("Sent sanctuary revival pro-tx successfully.  Please wait for the sanctuary list to be updated to ensure the sanctuary is revived.  This usually takes one to fifteen minutes.%f",1);
    return true;
}

std::string ProvisionUnchained2(JSONRPCRequest r, std::string& sError)
{
    // BIBLEPAY UNCHAINED
    std::string s1;
    std::string s2;
    ReadUnchainedConfigFile(s1, s2);

    if (!s1.empty() && !s2.empty()) {
        // Already provisioned
        return "";
    }

    std::string sUnchainedAddress = DefaultRecAddress(r,"Unchained");
    std::string sPK = GetPrivKey2(r, sUnchainedAddress, sError);
    if (!sError.empty()) {
        return "";
    }

    WriteUnchainedConfigFile(sUnchainedAddress, sPK);
    return "";
}


void WriteIPC(std::string sData)
{
    boost::filesystem::path pathIPC = GetGenericFilePath("ipc.dat");
    std::ofstream OutFile(pathIPC.string());
    OutFile.write(sData.c_str(), std::strlen(sData.c_str())); 
    OutFile.close();
}

std::string ReceiveIPC()
{
    boost::filesystem::path pathIPC = GetGenericFilePath("ipc.dat");
    boost::filesystem::ifstream streamIPC(pathIPC);
    if (!streamIPC.good())
            return std::string();
    std::string sData;
    int linenumber = 0;
    for (std::string line; std::getline(streamIPC, line); linenumber++) {
        if (line.empty()) continue;
        sData += line + "\r\n";
    }
    streamIPC.close();
    return sData;
}

std::string ReviveSanctuariesJob(JSONRPCRequest r)
{
    // Check Biblepay.conf first to see if feature is enabled
    int nEnabled = (int)gArgs.GetArg("-revivesanctuaries", 0);
    if (nEnabled == 0) {
        return "NOT_ENABLED";
    }
    // The wallet has to be unlocked in this case so we can pay the txid for investor sancs:
    CWallet* const pwallet = GetInternalWallet(r);
    if (!EnsureWalletIsAvailable(pwallet, false)) {
        LogPrintf("\r\n******* ReviveSanctuariesJob::%s", "WALLET_NEEDS_UNLOCKED");
        return "WALLET_NEEDS_UNLOCKED";
    }
    
    // Get the list of investor sancs:
    
    auto mnList = ::deterministicMNManager.get()->GetListAtChainTip();

    auto dmnToStatus = [&](auto& dmn)
    {
        if (mnList.IsMNValid(dmn.proTxHash)) {
            return "ENABLED";
        }
        if (mnList.IsMNPoSeBanned(dmn.proTxHash)) {
            return "POSE_BANNED";
        }
        return "UNKNOWN";
    };

    std::string sReport;

    mnList.ForEachMN(false, [&](auto& dmn)
    {
        std::string strOutpoint = dmn.collateralOutpoint.ToStringShort();
        Coin coin;
        std::string sCollateralAddress = "UNKNOWN";
        if (GetUTXOCoin(dmn.collateralOutpoint, coin)) {
            CTxDestination collateralDest;
            if (ExtractDestination(coin.out.scriptPubKey, collateralDest)) {
                sCollateralAddress = EncodeDestination(collateralDest);
            }
        }
        CScript payeeScript = dmn.pdmnState->scriptPayout;
        CTxDestination payeeDest;
        std::string payeeStr = "UNKNOWN";
        if (ExtractDestination(payeeScript, payeeDest)) {
            payeeStr = EncodeDestination(payeeDest);
        }

        bool fMine = IsMyAddress(r,payeeStr);
        std::string sProRegTxHash = dmn.proTxHash.ToString();
        std::string sSancStatus = dmnToStatus(dmn);  //POSE_BANNED or ENABLED
        std::string sOwnerAddress = EncodeDestination(PKHash(dmn.pdmnState->keyIDOwner));

        std::string sPubKeyOperator = dmn.pdmnState->pubKeyOperator.Get().ToString();
        if (dmn.GetCollateralAmount() != SANCTUARY_COLLATERAL_TEMPLE * COIN)
        {
            if (sSancStatus == "POSE_BANNED" && fMine)
            {
                UniValue uResponse;
                std::string sError;
                if (true)
                {
                    bool fResult = ReviveSanctuaryEnhanced(r, sProRegTxHash, sError, uResponse);
                    std::string sMyResult = fResult ? "SUCCESS" : "FAIL";
                    sReport += "Restart Result::" + sProRegTxHash + " and " + sError + ", "
                        + sMyResult + "\r\n";
                }
                sReport += "Restarting " + sCollateralAddress + " in state " + sSancStatus + "\r\n";
            }
        }
    });
    LogPrintf("\r\nReviveSanctuariesBatchJob::%s", sReport);
    return sReport;
}

bool IsMySanc(JSONRPCRequest r,std::string sSearchProRegTxHash)
{
    bool fExists = false;
    auto mnList = ::deterministicMNManager.get()->GetListAtChainTip();
    {
        mnList.ForEachMN(false, [&](auto& dmn)
        {
            std::string strOutpoint = dmn.collateralOutpoint.ToStringShort();
            Coin coin;
            std::string sCollateralAddress = "UNKNOWN";
            if (GetUTXOCoin(dmn.collateralOutpoint, coin)) {
                CTxDestination collateralDest;
                if (ExtractDestination(coin.out.scriptPubKey, collateralDest)) {
                    sCollateralAddress = EncodeDestination(collateralDest);
                }
            }
            CScript payeeScript = dmn.pdmnState->scriptPayout;
            CTxDestination payeeDest;
            std::string payeeStr = "UNKNOWN";
            if (ExtractDestination(payeeScript, payeeDest))
            {
                payeeStr = EncodeDestination(payeeDest);
            }
            bool fMine = IsMyAddress(r, payeeStr);
            std::string sProRegTxHash = dmn.proTxHash.ToString();
            if (sSearchProRegTxHash == sProRegTxHash) {
                fExists = fMine;
            }
        });
    }
    return fExists;
}

CAmount GetSancCollateralAmount(std::string sSearch)
{
    CAmount nAmount = 0;
    

    auto mnList = ::deterministicMNManager.get()->GetListAtChainTip();
    {
        mnList.ForEachMN(false, [&](auto& dmn) {
            std::string strOutpoint = dmn.collateralOutpoint.ToStringShort();
            Coin coin;
            std::string sCollateralAddress = "UNKNOWN";
            if (GetUTXOCoin(dmn.collateralOutpoint, coin)) {
                CTxDestination collateralDest;
                if (ExtractDestination(coin.out.scriptPubKey, collateralDest)) {
                    sCollateralAddress = EncodeDestination(collateralDest);
                }
            }
            CScript payeeScript = dmn.pdmnState->scriptPayout;
            CTxDestination payeeDest;
            std::string payeeStr = "UNKNOWN";
            if (ExtractDestination(payeeScript, payeeDest)) {
                payeeStr = EncodeDestination(payeeDest);
            }
            std::string sProRegTxHash = dmn.proTxHash.ToString();

            if (sSearch == sProRegTxHash) {
                nAmount = coin.out.nValue;
            }
        });
        
    }
    return nAmount;
}

bool IsSanctuaryCollateral(CAmount nAmount)
{
    bool fCollateral = (nAmount == SANCTUARY_COLLATERAL * COIN 
        || nAmount == SANCTUARY_COLLATERAL_TEMPLE * COIN 
        || nAmount == SANCTUARY_COLLATERAL_ALTAR * COIN) ? true : false;
    return fCollateral;
}

int GetSanctuaryMultiplier(CDeterministicMNCPtr dmnPayee)
{
    CAmount ca = dmnPayee->GetCollateralAmount();
    if (ca == SANCTUARY_COLLATERAL_TEMPLE * COIN) return 1000;
    if (ca == SANCTUARY_COLLATERAL * COIN) return 100;
    if (ca == SANCTUARY_COLLATERAL_ALTAR * COIN) return 10;
    return 0;
}

std::string GetSanctuaryTypeName(CDeterministicMNCPtr dmnPayee)
{
    CAmount ca = dmnPayee->GetCollateralAmount();
    if (ca == SANCTUARY_COLLATERAL_TEMPLE * COIN) return "TEMPLE";
    if (ca == SANCTUARY_COLLATERAL * COIN) return "SANCTUARY";
    if (ca == SANCTUARY_COLLATERAL_ALTAR * COIN) return "ALTAR";
    return "UNKNOWN";
}

std::string GetSanctuaryTypeName(CDeterministicMN dmnPayee)
{
    CAmount ca = dmnPayee.GetCollateralAmount();
    if (ca == SANCTUARY_COLLATERAL_TEMPLE * COIN) return "TEMPLE";
    if (ca == SANCTUARY_COLLATERAL * COIN) return "SANCTUARY";
    if (ca == SANCTUARY_COLLATERAL_ALTAR * COIN) return "ALTAR";
    return "UNKNOWN";
}

CAmount ExtrapolateSubsidyInner(int nExtrapolator, CAmount cSubsidy)
{
    // Leave room for mining dust if someone forks this coin and block subsidy approaches the max
    static CAmount nMax = MAX_BLOCK_SUBSIDY * COIN * .98;
    CAmount cNew = cSubsidy * nExtrapolator / 100;
    if (cNew > nMax)
    {
        return nMax;
    }
    return cNew;
}

CAmount ExtrapolateSubsidy(CDeterministicMNCPtr dmnPayee, CAmount nAmount, bool fBlockChecking)
{
    // Part 1: Depending on the sanc type, adjust the payment (Temples get 10*, Altars get 1/10th, etc)
    int nExtrapolator = GetSanctuaryMultiplier(dmnPayee);
    CAmount nNew = ExtrapolateSubsidyInner(nExtrapolator, nAmount);
    // If they are POSE banned, they get half
    if (!fBlockChecking)
    {
        if (IsSanctuaryPoseBanned(dmnPayee))
        {
            nNew = nNew * 50 / 100;
        }
    }
    return nNew;
}

std::string GetSidechainValue(std::string sType, std::string sKey, int nMinTimestamp)
{
    const Consensus::Params& consensusParams = Params().GetConsensus();
    for (auto ii : mapSidechain) 
	{
		Sidechain s = mapSidechain[ii.first];
        if (s.ObjectType == sType || sType == "0")
        {
             std::string sKey = ExtractXML(s.URL, "<key>", "</key>");
             std::string sValue = ExtractXML(s.URL, "<value>", "</value>");
             std::string sSig = ExtractXML(s.URL, "<sig>", "</sig>");
             std::string sMsg = ExtractXML(s.URL, "<msg>", "</msg>");
             std::string sSigner = ExtractXML(s.URL, "<signer>", "</signer>");
             if (sKey == sKey && s.Time >= nMinTimestamp)
             {
                    std::string sError;
 					bool fPassed = CheckStakeSignature(sSigner, sSig, sMsg, sError);
                    if (fPassed)
                    {
                        return sValue;
                    }
             }
        }
	}
    return std::string();
}

bool ProTxHashIsValid(std::string sProTxHash)
{
    auto mnList = ::deterministicMNManager.get()->GetListAtChainTip();
    bool fValid = false;
    auto dmnToStatus = [&](auto& dmn) {
        if (dmn.proTxHash.ToString() == sProTxHash) {
            fValid = true;
        }
    };
    return fValid;
}

std::string ComputeMinedBlockVersion()
{
    std::string sVersion0 = FormatFullVersion();
    std::string sSancRow;

    if (fMasternodeMode)
    {
        auto mnList = ::deterministicMNManager.get()->GetListAtChainTip();
        CDeterministicMNCPtr dmn = mnList.GetMN(activeMasternodeInfo.proTxHash);
        if (dmn)
        {
             sSancRow = "<SANC>" + dmn->proTxHash.ToString() + "</SANC>";
        }
        else
        {
             sSancRow = "<SANC>?</SANC>";
        }
        // TODO - SANC SIG HERE determined by mnlist
    }
    else
    {
        sSancRow = "<SANC></SANC>";
    }

    std::string sFullMessage = "<VER>" + sVersion0 + "</VER>" + sSancRow;
    return sFullMessage;
}

bool ContextualCheckBlockMinedBySanc(const CBlock& block)
{
    const Consensus::Params& consensusParams = Params().GetConsensus();
    // Prior to this height, rule was not active
    if (block.nTime < consensusParams.BABYLON_FALLING_TIME) {
        return true;
    }
    /* In favor of this being dangerous */

    return true;

    /*
    std::string sSanc = ExtractXML(block.vtx[0]->vout[0].sTxOutMessage, "<SANC>", "</SANC>");
    // The light version
    if (sSanc.length() != 64)
    {
        // Note: Payment goes to the sanc either way, so anyone who mines a block by brute force does not get paid for it.  Hence the reason for light mode.
        LogPrintf("\nContextualCheckBlockMinedBySanc::ERROR Not solved by a sanc %s", sSanc);
        return false;
    }
    */

    /*
    std::string sNetworkName = Params().NetworkIDString();

    CBlockIndex* pblockTip = g_chainman.ActiveChain().Tip();
    bool fSynced = ::masternodeSync.get()->IsBlockchainSynced();

    if (!fSynced)
        return true;

    // The chain is synced, the masternode is synced, deterministic nodes are up, we know all the Sanc proTx Hashes, so verify that a sanc solved it
    // Only allow block to be solved by a non-sanc if block is over an hour old
    std::string sSanc = ExtractXML(block.vtx[0]->vout[0].sTxOutMessage, "<SANC>", "</SANC>");
    bool fProTxHashIsValid = ProTxHashIsValid(sSanc);
    if (!fProTxHashIsValid) {
        LogPrintf("\nContextualCheckBlockMinedBySanc::ERROR Not solved by a sanc %s", sSanc);
        return false;
    }
    return true;
    */
}

bool IsSanctuaryLegacyTempleOrAltar(CDeterministicMNCPtr dmn)
{
    // Before Dash Platform was released, BBP had its own version of Temples and Altars.
    // If this is a legacy Temple or Altar, we need to pay them 1 BBP to coerce them to upgrade the node.
    if (dmn->nType == MnType::Regular && dmn->GetCollateralAmount() != SANCTUARY_COLLATERAL * COIN)
    {
         return true;
    }
    else
    {
         return false;
    }
}

bool IsSanctuaryPoseBanned(CDeterministicMNCPtr dmnPayee)
{
    bool fReduced = false;
    std::string sKey = dmnPayee->pdmnState->pubKeyOperator.Get().ToString();
    int nStatus = mapPOVSStatus[sKey];
    int nPoseScore = dmnPayee->pdmnState->nPoSePenalty;
    // Note, the nStatus value will be 255 when the BMS POSE = 800 (that means their BMS endpoint is down)
    if (nPoseScore > 0 || nStatus == 255) {
         fReduced = true;
    }
    return fReduced;
}

std::string Test1000()
{
    const CoreContext& cc = CGlobalNode::GetGlobalCoreContext();
    JSONRPCRequest r0(cc);
    double nBalance = GetWalletBalance(r0);
    LogPrintf("\nTEST1000-bal %f", nBalance);
    const NodeContext& nb2 = CGlobalNode::GetGlobalNodeContext();
    ChainstateManager& c0 = EnsureChainman(nb2);
    const CBlockIndex* tip = c0.ActiveChain().Tip();
    LogPrintf("\r\nTest1000II-tipindexheight::%f", tip->nHeight);
    auto mnList = ::deterministicMNManager.get()->GetListAtChainTip();
    LogPrintf("\r\n1002::%f", 30003);
    std::string s1;
    mnList.ForEachMN(false, [&](auto& dmn) {
        s1 = dmn.collateralOutpoint.ToStringShort();
    });
    return s1;
}


std::map<std::string,NFT> GetNFTs()
{
    std::map<std::string, NFT> mapNFTs;
    for (auto ii : mapSidechain)
    {
         Sidechain s = mapSidechain[ii.first];
         if (s.ObjectType == "NFT")
         {
             std::string sKey = ExtractXML(s.URL, "<key>", "</key>");
             std::string sValue = ExtractXML(s.URL, "<value>", "</value>");
             std::string sSig = ExtractXML(s.URL, "<sig>", "</sig>");
             std::string sMsg = ExtractXML(s.URL, "<msg>", "</msg>");
             std::string sSigner = ExtractXML(s.URL, "<signer>", "</signer>");
             std::string sError;
                 
             bool fPassed = CheckStakeSignature(sSigner, sSig, sMsg, sError);
             if (fPassed)
             {
                LogPrintf("\nGETNFTS::Info %s", sValue);
    
                NFT n;
                n = n.FromJson(sValue);
                
                mapNFTs[n.id] = n;
                LogPrintf("\nGETNFTS::NAME=%s ID=%s", n.Name, n.id);

             }
         }
    }
    return mapNFTs;
}

NFT GetNFTById(std::string sID)
{
    std::map<std::string, NFT> nfts = GetNFTs();
    for (auto ii : nfts) {
         NFT n = nfts[ii.first];
         if (n.id == sID) {
             return n;
         }
    }
    NFT n;
    return n;
}

bool AuthorizeNFT(NFT n, std::string& sError)
{
    // Retrieve the prior version of the NFT.
    NFT nftOld = GetNFTById(n.id);
    if (n.IsValid()) {
         sError = "NFT signature failed.";
         return false;
    }

    if (n.Action == "CREATE")
    {
         if (nftOld.IsValid())
         {
             sError = "Sorry, the NFT already exists.";
             return false;
         }
         return true;
    }
    else if (n.Action == "BUY")
    {
        // dont let them buy a Deleted, Non Marketable, or Invalid (null ID or unsigned or bad sig) NFT.

         if (!nftOld.IsValid())
         {
             sError = "Unable to find NFT.";
             return false;
         }
         // Verify the amount in the tx is greater than buy it now.
         if (n.Marketable == 0) {
             sError = "Sorry, the NFT is not marketable.";
             return false;
         }
         if (n.Deleted == 1) {
             sError = "Sorry, this NFT is deleted.";
             return false;
         }
         if (0 < n.BuyItNowAmount) {
             sError = "Sorry, the purchase price must be greater or equal to the buy it now amount.";
             return false;
         }
         // the NFT now goes to the new owner.
         // Verify the PAYMENT is going to the Prior owner, and the amount is greater >= to buy it now amount;

         return true;
    }
    else if (n.Action == "EDIT")
    {
        if (!nftOld.IsValid())
        {
             sError = "Unable to find NFT";
             return false;
        }
        // the users verified address must match the old owners verified address.
        if (nftOld.Signer != n.Signer) {
             sError = "An NFT may only be edited by its owner.";
             return false;
        }
        return true;
    } else {
         sError = "Invalid Action.";
         return false;
    }
}

NFT GetNFTFromTransaction(const CTransaction& tx)
{
    NFT n;
    std::string sTxMsg;
    for (unsigned int i = 0; i < tx.vout.size(); i++) {
         sTxMsg += tx.vout[i].sTxOutMessage;
    }
    
    std::string sSC = ExtractXML(sTxMsg, "<sc>", "</sc>");
    LogPrintf("\r\nTxMsg %s Sc=%s", sTxMsg, sSC);

    if (!sSC.empty())
    {
         Sidechain s;
         s.ObjectType = ExtractXML(sSC, "<objtype>", "</objtype>");
         s.URL = ExtractXML(sSC, "<url>", "</url>");
         
         if (s.ObjectType == "NFT")
         {
             std::string sValue = ExtractXML(s.URL, "<value>", "</value>");
             n = n.FromJson(sValue);
             return n;
         }
    }
    return n;
}

CAmount CalculateTotalPaidToAddress(const CTransaction& tx, std::string sTargetAddress)
{
    CAmount nTotal = 0;
    for (unsigned int k = 0; k < tx.vout.size(); k++) {
         const CTxOut& txOut = tx.vout[k];
         std::string sTo = PubKeyToAddress(txOut.scriptPubKey);
         if (sTargetAddress == sTo) {
             nTotal += txOut.nValue;
         }
    }
    return nTotal;
}

bool CheckMemPoolTransactionBiblepay(const CTransaction& tx, const CBlockIndex* pindexPrev)
{
    NFT n = GetNFTFromTransaction(tx);
    if (n.IsValid())
    {
         LogPrintf("\r\nMEMPOOL_BBP::DATA %s", n.id);
         NFT nOldNFT = GetNFTById(n.id);
         
         if (n.Action == "buy")
         {
             if (nOldNFT.IsValid())
             {
                // Scan outputs to see the total being spent to the prior NFT address.
                CAmount nPaid = CalculateTotalPaidToAddress(tx, nOldNFT.Signer);
                LogPrintf("\r\nMEMPOOL_BBP::TXID %s::NewAction %s, OldMarketable %f, OldDeleted %f, BuyItNowAmount %f, Paid to %s, Amount %f, OldNFT Signer %s, NewNFT Signer %s",
                    tx.GetHash().GetHex() ,
                    n.Action,
                    nOldNFT.Marketable,
                    nOldNFT.Deleted,
                    nOldNFT.BuyItNowAmount, 
                    nOldNFT.Signer,
                    (double)nPaid / COIN, nOldNFT.Signer,
                    n.Signer);
                // Critical Section
                if (nOldNFT.Deleted == 1)
                {
                     LogPrintf("\r\nMEMPOOL_BBP::Unable to buy Deleted NFT %f", nOldNFT.Deleted);
                     return false;
                }
                if (nOldNFT.Marketable == 0)
                {
                     LogPrintf("\r\nMEMPOOL_BBP::Unable to buy a non marketable NFT %f", nOldNFT.Marketable);
                     return false;
                }
                if (nPaid < (nOldNFT.BuyItNowAmount * COIN))
                {
                     LogPrintf("\r\nMEMPOOL_BBP::Sorry, the amount paid %f is less than the buy-it-now-amount of %f for tx %s",
                               (double)nPaid / COIN, nOldNFT.BuyItNowAmount, tx.GetHash().GetHex());
                     return false;               
                }
             }
         }

         if (nOldNFT.Action == "edit")
         {
             NFT nOldNFT = GetNFTById(n.id);
             if (nOldNFT.IsValid())
             {
                if (nOldNFT.Deleted == 1)
                {
                     LogPrintf("\r\nMEMPOOL_BBP::Unable to edit Deleted NFT %f", nOldNFT.Deleted);
                     return false;
                }
                if (nOldNFT.Signer != n.Signer)
                {
                     LogPrintf("\r\nMEMPOOL_BBP::Only the current owner can edit an NFT %s id %s", tx.GetHash().GetHex(), n.id);
                     return false;
                }
             }
         }
    }
    return true;
}
