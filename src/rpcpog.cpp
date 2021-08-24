// Copyright (c) 2014-2019 The Dash Core Developers, The DAC Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <base58.h>
#include <boost/algorithm/string.hpp>           // for trim()
#include <boost/algorithm/string/case_conv.hpp> // for to_lower()
#include <boost/algorithm/string/replace.hpp>
#include <boost/asio.hpp>
#include <boost/date_time/posix_time/posix_time.hpp> // for StringToUnixTime()
#include <boost/lexical_cast.hpp>
#include <boost/thread.hpp>

#include <consensus/validation.h>
#include <core_io.h>
#include <fstream>

#include <init.h>
#include <key_io.h>

#include <masternode/masternode-sync.h>
#include <masternode/activemasternode.h>

#include <masternode/masternode-sync.h>
#include <governance/governance-vote.h>

#include <math.h> /* round, floor, ceil, trunc */

#include <messagesigner.h>

#include <net.h> // for CService
#include <netaddress.h>
#include <netbase.h>
#include <policy/policy.h>
#include <randomx_bbp.h>
#include <rpcpog.h>
#include <rpc/server.h>

#include <rpc/server.h>
#include <txmempool.h>
#include <util.h>

#include <utilmoneystr.h>
#include <validation.h>
#include <stdint.h>
#include <univalue.h>

#include <validation.h>
#include <sstream>

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


bool RPCSendMoney(std::string& sError, std::string sAddress, CAmount nValue, std::string& sTXID)
{
	JSONRPCRequest r;
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(r);
    CWallet* const pwallet = wallet.get();
    if (!EnsureWalletIsAvailable(pwallet, false))
        return false;
	CScript scriptPubKey = GetScriptForDestination(DecodeDestination(sAddress));
	
    CReserveKey reservekey(pwallet);
    CAmount nFeeRequired;
    std::string strError;
    std::vector<CRecipient> vecSend;
    int nChangePosRet = -1;
    CRecipient recipient = {scriptPubKey, nValue, false};
    vecSend.push_back(recipient);
    int nMinConfirms = 0;
    CCoinControl coinControl;
    int nChangePos = -1;
    CTransactionRef tx;

    if (!pwallet->CreateTransaction(vecSend, tx, reservekey, nFeeRequired, nChangePos, strError, coinControl, true, 0)) 
	{
        sError = "Unable to Create Transaction: " + strError;
        return false;
    }
    CValidationState state;
	if (!pwallet->CommitTransaction(tx, {}, {}, {}, reservekey, g_connman.get(), state))
	{
        sError = "Error: The transaction was rejected!";
        return false;
    }
    sTXID = tx->GetHash().GetHex();
	return true;
}

std::string DefaultRecAddress(std::string sType)
{
	JSONRPCRequest r;
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(r);
    CWallet* const pwallet = wallet.get();
    if (!EnsureWalletIsAvailable(pwallet, false))
        return "no wallet";
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
    return sDefaultRecAddress;
}

std::string SignMessageEvo(std::string strAddress, std::string strMessage, std::string& sError)
{
	JSONRPCRequest r;
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(r);
    CWallet* const pwallet = wallet.get();
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

bool SendManyXML(std::string XML, std::string& sTXID)
{
	JSONRPCRequest r;
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
    
	if (!pwallet->CommitTransaction(tx, {}, {}, {}, keyChange, g_connman.get(), state))
	{
			throw JSONRPCError(RPC_WALLET_ERROR, "Transaction commit failed");
	}
	sTXID = tx->GetHash().GetHex();
	return true;
}

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
    try {
        double dDebugLevel = StringToDouble(gArgs.GetArg("-devdebuglevel", "0"), 0);
        if (dDebugLevel == 1)
            LogPrintf("\r\nUplink::Connecting to %s [/] %s ", sBaseURL, sPage);

        mapRequestHeaders["Agent"] = FormatFullVersion();
        // Supported pool Network Chain modes: main, test, regtest
        const CChainParams& chainparams = Params();
        mapRequestHeaders["NetworkID"] = chainparams.NetworkIDString();
        if (sPayload.length() < 7777)
            mapRequestHeaders["Action"] = sPayload;
        mapRequestHeaders["HTTP_PROTO_VERSION"] = DoubleToString(HTTP_PROTO_VERSION, 0);
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
        std::string sDomainWithPort = sDomain + ":" + DoubleToString(iPort, 0);

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
        }
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

static int SSL_PORT = 443;
BBPResult UnchainedQuery(std::string sXMLSource, std::string sAPI)
{
    std::string sDomain = "https://unchained.biblepay.org";
    int iTimeout = 30000;
    BBPResult b;
    b.Response = Uplink(false, "", sDomain, sAPI, SSL_PORT, iTimeout, 4);
    return b;
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

std::shared_ptr<CReserveScript> GetScriptForMining()
{
	std::shared_ptr<CReserveScript> coinbaseScript;
	JSONRPCRequest r;
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(r);
	wallet->GetScriptForMining(coinbaseScript);
	return coinbaseScript;
}

bool IsDailySuperblock(int nHeight)
{
	bool fDaily = (nHeight % 205 == 20);
	return fDaily;
}

static double UTXO_BLOCK_PERCENTAGE = .25;
CAmount GetDailyPaymentsLimit(int nHeight)
{
	const CChainParams& chainparams = Params();
    CAmount nPaymentsLimit = GetBlockSubsidy(0, nHeight, chainparams.GetConsensus()) * UTXO_BLOCK_PERCENTAGE * BLOCKS_PER_DAY;
	return nPaymentsLimit;
}

std::vector<Portfolio> GetDailySuperblock(int64_t nTime, int nHeight)
{
	const CChainParams& chainparams = Params();

	CAmount nPaymentsLimit = GetDailyPaymentsLimit(nHeight);
	// The chain is either 'main' or 'test'
	std::string sChain = chainparams.NetworkIDString();
    LogPrintf("Payments Limits %f %f ", nHeight, nPaymentsLimit/COIN);
	std::string sDate = DateTimeStrFormat("%m/%d/%y", nTime);
	std::string sFile = "utxointegration_" + sChain + "_" + sDate + ".dat";
	std::vector<Portfolio> vPortfolio;
	BBPResult b = SidechainQuery("", sFile);
	std::string sHash = ExtractXML(b.Response, "<hash>", "</hash>");
	std::string sData = ExtractXML(b.Response, "<data>", "</data>");
	LogPrintf("\nHash %s, Data %s, File %s", sHash, sData, sFile);
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
			bool fValid = ValidateAddress2(p.OwnerAddress);
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
	uint256 sha1 = GetSHA256Hash("<data>" + sData + "</data>");
	uint256 sha2 = uint256S("0x" + sHash);
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
	std::vector<Portfolio> vPortfolio = GetDailySuperblock(nBlockTime, nBlockHeight);
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
            if (sRecipient1 == sRecipient2 && nAmount1 == nAmount2) 
            {
                 found = true;
                 break;
            }
		}
	    if (!found) 
		{
            LogPrintf("\nValidateDailySuperblock::ERROR failed to find expected payee %s at height %s\n", sRecipient1, nBlockHeight);
			return false;
        }
    }
	if (nTotalPayments == 0)
	{
		// Ensure this block is more than 8 hours old, since nothing is in it...
		if (nElapsed < 60 * 60 * 8)
		{
			LogPrintf("\nValidateDailySuperblock::ERROR - Superblock is less than 8 hours old, and superblock is empty!  Waiting for a valid superblock... ElapsedTime=%f\n", nElapsed);
			return false;
		}
		CAmount nTotalSubsidy = 0;
		for (const auto& txout3 : txNew.vout)
		{
			nTotalSubsidy += txout3.nValue;
		}
		if (nElapsed > (60 * 60 * 8) && nTotalSubsidy > (MAX_BLOCK_SUBSIDY*COIN))
		{
			LogPrintf("\nValidateDailySuperblock::ERROR - Superblock is greater than 8 hours old, and empty, with more than MAX_BLOCK_SUBSIDY[%f] in payments... Rejected.. \n", nTotalSubsidy/COIN);
			return false;
		}
	}
    return true;
}

//////////////////////////////////////////////////////////////////////////////// Watchman-On-The-Wall /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//								                          			BBP's version of The Sentinel, March 31st, 2019                                                                                                  //
//                                                                                                                                                                                                                   //

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

uint256 GetPAMHash(std::string sAddresses, std::string sAmounts, std::string sQTPhase)
{
	std::string sConcat = sAddresses + sAmounts + sQTPhase;
	if (sConcat.empty()) return uint256S("0x0");
	std::string sHash = RetrieveMd5(sConcat);
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
	if(!masternodeSync.IsBlockchainSynced()) 
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

	auto mnList = deterministicMNManager->GetListAtChainTip();
    bool fMnFound = mnList.HasValidMNByCollateral(activeMasternodeInfo.outpoint);
	if (!fMnFound)
	{
		sError = "Unable to find deterministic sanctuary in latest sanctuary list.";
		return false;
	}

	if (govobj.GetObjectType() == GOVERNANCE_OBJECT_TRIGGER) 
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

	if (fMissingConfirmations) 
	{
        governance.AddPostponedObject(govobj);
        govobj.Relay(*g_connman);
    } 
	else 
	{
        governance.AddGovernanceObject(govobj, *g_connman);
    }

	gobjecthash = govobj.GetHash().ToString();
	nLastGSCSubmitted = GetAdjustedTime();

	return true;
}

void GetGSCGovObjByHeight(int nHeight, uint256 uOptFilter, int& out_nVotes, uint256& out_uGovObjHash, std::string& out_PaymentAddresses, std::string& out_PaymentAmounts, std::string& out_qtdata)
{
	int nStartTime = 0; 
	LOCK2(cs_main, governance.cs);
	std::vector<const CGovernanceObject*> objs = governance.GetAllNewerThan(nStartTime);
	std::string sPAM;
	std::string sPAD;
	int iHighVotes = -1;
	for (const auto& pGovObj : objs) 
	{
		CGovernanceObject* myGov = governance.FindGovernanceObject(pGovObj->GetHash());
		if (myGov->GetObjectType() != GOVERNANCE_OBJECT_TRIGGER) continue;
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
			if (iVotes > iHighVotes) 
			{
				iHighVotes = iVotes;
				out_PaymentAddresses = sPAD;
				out_PaymentAmounts = sPAM;
				out_nVotes = iHighVotes;
				out_uGovObjHash = myGov->GetHash();
				out_qtdata = sQT;
			}
		}
	}
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

BBPProposal GetProposalByHash(uint256 govObj, int nLastSuperblock)
{
	int nSancCount = deterministicMNManager->GetListAtChainTip().GetValidMNsCount();
	int nMinPassing = nSancCount * .10;
	if (nMinPassing < 1) nMinPassing = 1;
	CGovernanceObject* myGov = governance.FindGovernanceObject(govObj);
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

std::vector<BBPProposal> GetWinningSanctuarySporkProposals()
{
	int nStartTime = GetAdjustedTime() - (86400 * 7);
	// NOTE: Sanctuary sporks occur every week, and expire 7 days after creation.  They should be voted on regularly.
	int nLastSuperblock = 0;
	int nNextSuperblock = 0;
	GetGovSuperblockHeights(nNextSuperblock, nLastSuperblock);
    LOCK2(cs_main, governance.cs);
    std::vector<const CGovernanceObject*> objs = governance.GetAllNewerThan(nStartTime);
	std::vector<BBPProposal> vSporks;
	for (const CGovernanceObject* pGovObj : objs) 
    {
		if (pGovObj->GetObjectType() != GOVERNANCE_OBJECT_PROPOSAL) continue;
		BBPProposal bbpProposal = GetProposalByHash(pGovObj->GetHash(), nLastSuperblock);
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
	std::string sHex = HexStr(vchJson.begin(), vchJson.end());
	return sHex;
}

bool VoteForGobject(uint256 govobj, std::string sVoteSignal, std::string sVoteOutcome, std::string& sError)
{

	if (sVoteSignal != "funding" && sVoteSignal != "delete")
	{
		LogPrintf("Sanctuary tried to vote in a way that is prohibited.  Vote failed. %s", sVoteSignal);
		return false;
	}

	vote_signal_enum_t eVoteSignal = CGovernanceVoting::ConvertVoteSignal(sVoteSignal);
	vote_outcome_enum_t eVoteOutcome = CGovernanceVoting::ConvertVoteOutcome(sVoteOutcome);
	int nSuccessful = 0;
	int nFailed = 0;
	int govObjType;
	{
        LOCK(governance.cs);
        CGovernanceObject *pGovObj = governance.FindGovernanceObject(govobj);
        if (!pGovObj) 
		{
			sError = "Governance object not found";
			return false;
        }
        govObjType = pGovObj->GetObjectType();
    }
	
    auto dmn = deterministicMNManager->GetListAtChainTip().GetValidMNByCollateral(activeMasternodeInfo.outpoint);

    if (!dmn) 
	{
        sError = "Can't find masternode by collateral output";
		return false;
    }

    CGovernanceVote vote(dmn->collateralOutpoint, govobj, eVoteSignal, eVoteOutcome);

    bool signSuccess = false;
    if (govObjType == GOVERNANCE_OBJECT_PROPOSAL && eVoteSignal == VOTE_SIGNAL_FUNDING)
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
    if (governance.ProcessVoteAndRelay(vote, exception, *g_connman)) 
	{
        nSuccessful++;
    } else {
        nFailed++;
    }

    return (nSuccessful > 0) ? true : false;
   
}

std::vector<std::pair<int64_t, uint256>> GetGSCSortedByGov(int nHeight, uint256 inPamHash, bool fIncludeNonMatching)
{
	int nStartTime = 0; 
	LOCK2(cs_main, governance.cs);
	std::vector<const CGovernanceObject*> objs = governance.GetAllNewerThan(nStartTime);
	std::string sPAM;
	std::string sPAD;
	std::vector<std::pair<int64_t, uint256> > vPropByGov;
	vPropByGov.reserve(objs.size() + 1);
	int iOffset = 0;
	for (const auto& pGovObj : objs) 
	{
		CGovernanceObject* myGov = governance.FindGovernanceObject(pGovObj->GetHash());
		if (myGov->GetObjectType() != GOVERNANCE_OBJECT_TRIGGER) continue;
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
	for (int i = 0; i < vPropByGov.size(); i++)
	{
		CGovernanceObject* myGov = governance.FindGovernanceObject(vPropByGov[i].second);
		sAction = (i==0) ? "yes" : "no";
		if (fOverBudget) 
			sAction = "no";
		iVotes = myGov->GetAbsoluteYesCount(VOTE_SIGNAL_FUNDING);
		LogPrintf("\nSmartContract-Server::VoteForGSCContractOrderedByHash::Voting %s for govHash %s, with pre-existing-votes %f (created %f) Overbudget %f ",
			sAction, myGov->GetHash().GetHex(), iVotes, myGov->GetCreationTime(), (double)fOverBudget);
		VoteForGobject(myGov->GetHash(), "funding", sAction, sError);
		// Additionally, clear the delete flag, just in case another node saw this contract as a negative earlier in the cycle
		VoteForGobject(myGov->GetHash(), "delete", "no", sError);
		break;
	}
	// Phase 2: Vote against contracts at this height that do not match our hash
	int iVotedNo = 0;
	if (uPamHash != uint256S("0x0"))
	{
		vPropByGov = GetGSCSortedByGov(nHeight, uPamHash, true);
		for (int i = 0; i < vPropByGov.size(); i++)
		{
			CGovernanceObject* myGovForRemoval = governance.FindGovernanceObject(vPropByGov[i].second);
			sAction = "no";
			int iVotes = myGovForRemoval->GetAbsoluteYesCount(VOTE_SIGNAL_FUNDING);
			LogPrintf("\nSmartContract-Server::VoteDownBadGCCContracts::Voting %s for govHash %s, with pre-existing-votes %f (created %f)",
				sAction, myGovForRemoval->GetHash().GetHex(), iVotes, myGovForRemoval->GetCreationTime());
			VoteForGobject(myGovForRemoval->GetHash(), "funding", sAction, sError);
			iVotedNo++;
			if (iVotedNo > 2)
				break;
		}
	}

	return sError.empty() ? true : false;
}

std::string WatchmanOnTheWall(bool fForce, std::string& sContract)
{
	if (chainActive.Tip()->nHeight % 5 != 0)
		return "WAITING...";
	if (!fMasternodeMode && !fForce)   
		return "NOT_A_WATCHMAN_SANCTUARY";
	if (!chainActive.Tip()) 
		return "WATCHMAN_INVALID_CHAIN";
	if (!ChainSynced(chainActive.Tip()))
		return "WATCHMAN_CHAIN_NOT_SYNCED";

	const Consensus::Params& consensusParams = Params().GetConsensus();
	int MIN_EPOCH_BLOCKS = consensusParams.nSuperblockCycle * .07; // TestNet Weekly superblocks (1435), Prod Monthly superblocks (6150), this means a 75 block warning in TestNet, and a 210 block warning in Prod

	int nLastSuperblock = 0;
	int nNextSuperblock = 0;
	GetGovSuperblockHeights(nNextSuperblock, nLastSuperblock);

	int nSancCount = deterministicMNManager->GetListAtChainTip().GetValidMNsCount();

	std::string sReport;

	int nBlocksUntilEpoch = nNextSuperblock - chainActive.Tip()->nHeight;
	if (nBlocksUntilEpoch < 0)
		return "WATCHMAN_LOW_HEIGHT";

	if (nBlocksUntilEpoch < MIN_EPOCH_BLOCKS && !fForce)
		return "WATCHMAN_TOO_EARLY_FOR_COMING";

	int nStartTime = GetAdjustedTime() - (86400 * 32);
    std::vector<const CGovernanceObject*> objs = governance.GetAllNewerThan(nStartTime);
	std::vector<std::pair<int, uint256> > vProposalsSortedByVote;
	vProposalsSortedByVote.reserve(objs.size() + 1);
    
	for (const CGovernanceObject* pGovObj : objs) 
    {
		if (pGovObj->GetObjectType() != GOVERNANCE_OBJECT_PROPOSAL) continue;
		BBPProposal bbpProposal = GetProposalByHash(pGovObj->GetHash(), nLastSuperblock);
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

	if (uGovObjHash == uint256S("0x0"))
	{
		std::string sWatchmanTrigger = SerializeSanctuaryQuorumTrigger(nNextSuperblock, nNextSuperblock, GetAdjustedTime(), sContract);
		std::string sGobjectHash;
		SubmitGSCTrigger(sWatchmanTrigger, sGobjectHash, sError);
		LogPrintf("**WatchmanOnTheWall::SubmitWatchmanTrigger::CreatingWatchmanContract hash %s , gobject %s, results %s **\n", sWatchmanTrigger, sGobjectHash, sError);
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

	return "WATCHMAN_SUCCESS";
}


//////////////////////////////////////////////////////////////////////////////// End of Watchman On The Wall ////////////////////////////////////////////////////////////////////////////////////////////////

