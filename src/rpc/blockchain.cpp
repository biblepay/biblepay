// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2014-2020 The Däsh Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <rpc/blockchain.h>

#include <amount.h>
#include <chain.h>
#include <chainparams.h>
#include <checkpoints.h>
#include <coins.h>
#include <core_io.h>
#include <consensus/validation.h>
#include <validation.h>
#include <core_io.h>
// #include <rpc/index/txindex.h>
#include <policy/feerate.h>
#include <policy/policy.h>
#include <primitives/transaction.h>
#include <rpc/server.h>
#include <streams.h>
#include <sync.h>
#include <txdb.h>
#include <txmempool.h>
#include <util.h>
#include <utilstrencodings.h>
#include <hash.h>
#include <warnings.h>
#include <masternode/masternode-sync.h>

#include <evo/specialtx.h>
#include <evo/cbtx.h>

#include <llmq/quorums_chainlocks.h>
#include <llmq/quorums_instantsend.h>

#include <stdint.h>

#include <univalue.h>

#include <boost/algorithm/string.hpp>
#include <boost/thread/thread.hpp> // boost::thread::interrupt

#include <memory>
#include <mutex>
#include <condition_variable>
#include "rpcpog.h"
#include "smartcontract-server.h"
#include "kjv.h"
#include "governance/governance-classes.h"
#include "randomx_bbp.h"
#include "rpcutxo.h"

struct CUpdatedBlock
{
    uint256 hash;
    int height;
};

static std::mutex cs_blockchange;
static std::condition_variable cond_blockchange;
static CUpdatedBlock latestblock;

extern void TxToJSON(const CTransaction& tx, const uint256 hashBlock, UniValue& entry);
UniValue protx_register(const JSONRPCRequest& request);
UniValue protx(const JSONRPCRequest& request);
UniValue _bls(const JSONRPCRequest& request);
UniValue hexblocktocoinbase(const JSONRPCRequest& request);
UniValue createmultisig(const JSONRPCRequest& request);
UniValue listunspent(const JSONRPCRequest& request);
UniValue createrawtransaction(const JSONRPCRequest& request);
UniValue decoderawtransaction(const JSONRPCRequest& request);
UniValue signrawtransaction(const JSONRPCRequest& request);
UniValue dumpprivkey(const JSONRPCRequest& request); 
UniValue sendrawtransaction(const JSONRPCRequest& request);
UniValue importprivkey(const JSONRPCRequest& request);
/* Calculate the difficulty for a given block index,
 * or the block index of the given chain.
 */
double GetDifficulty(const CChain& chain, const CBlockIndex* blockindex)
{
    if (blockindex == nullptr)
    {
        if (chain.Tip() == nullptr)
            return 1.0;
        else
            blockindex = chain.Tip();
    }

    int nShift = (blockindex->nBits >> 24) & 0xff;
    double dDiff =
        (double)0x0000ffff / (double)(blockindex->nBits & 0x00ffffff);

    while (nShift < 29)
    {
        dDiff *= 256.0;
        nShift++;
    }
    while (nShift > 29)
    {
        dDiff /= 256.0;
        nShift--;
    }

    return dDiff * 10;
}

double GetDifficulty(const CBlockIndex* blockindex)
{
    return GetDifficulty(chainActive, blockindex);
}

boost::filesystem::path GetGenericFilePath(std::string sPath)
{
    boost::filesystem::path pathConfigFile(sPath);
    if (!pathConfigFile.is_complete())
        pathConfigFile = GetDataDir() / pathConfigFile;
    return pathConfigFile;
}

std::string ScanSanctuaryConfigFile(std::string sName)
{
    int linenumber = 1;
    boost::filesystem::path pathMasternodeConfigFile = GetMasternodeConfigFile();
    boost::filesystem::ifstream streamConfig(pathMasternodeConfigFile);
    if (!streamConfig.good()) 
		return std::string();
	for(std::string line; std::getline(streamConfig, line); linenumber++)
    {
        if(line.empty()) continue;
        std::istringstream iss(line);
        std::string comment, alias, ip, privKey, txHash, outputIndex;
        if (iss >> comment) 
		{
            if(comment.at(0) == '#') continue;
            iss.str(line);
            iss.clear();
        }

		if (comment == sName)
		{
			streamConfig.close();
			return line;
		}
    }
    streamConfig.close();
    return std::string();
}


void AppendSanctuaryFile(std::string sFile, std::string sData)
{
    boost::filesystem::path pathDeterministicConfigFile = GetGenericFilePath(sFile);
    boost::filesystem::ifstream streamConfig(pathDeterministicConfigFile);
	bool fReadable = streamConfig.good();
	if (fReadable)
		streamConfig.close();
    FILE* configFile = fopen(pathDeterministicConfigFile.string().c_str(), "a");
    if (configFile != nullptr) 
	{
	    if (!fReadable) 
		{
            std::string strHeader = "# Deterministic Sanctuary Configuration File\n"
				"# Format: Sanctuary_Name IP:port(40000=prod,40001=testnet) BLS_Public_Key BLS_Private_Key Collateral_output_txid Collateral_output_index Pro-Registration-TxId Pro-Reg-Collateral-Address Pro-Reg-Sent-TxId\n";
            fwrite(strHeader.c_str(), std::strlen(strHeader.c_str()), 1, configFile);
        }
    }
	fwrite(sData.c_str(), std::strlen(sData.c_str()), 1, configFile);
    fclose(configFile);
}


std::string ScanDeterministicConfigFile(std::string sName)
{
    int linenumber = 1;
    boost::filesystem::path pathDeterministicFile = GetDeterministicConfigFile();
    boost::filesystem::ifstream streamConfig(pathDeterministicFile);
    if (!streamConfig.good()) 
		return std::string();
	//Format: Sanctuary_Name IP:port(40000=prod,40001=testnet) BLS_Public_Key BLS_Private_Key Collateral_output_txid Collateral_output_index Pro-Registration-TxId Pro-Reg-Collateral-Address Pro-Reg-Se$

	for(std::string line; std::getline(streamConfig, line); linenumber++)
    {
        if(line.empty()) continue;
        std::istringstream iss(line);
        std::string sanctuary_name, ip, blsPubKey, BlsPrivKey, colOutputTxId, colOutputIndex, ProRegTxId, ProRegCollAddress, ProRegCollAddFundSentTxId;
        if (iss >> sanctuary_name) 
		{
            if(sanctuary_name.at(0) == '#') continue;
            iss.str(line);
            iss.clear();
        }

		if (sanctuary_name == sName)
		{
			streamConfig.close();
			return line;
		}
    }
    streamConfig.close();
    return std::string();
}


UniValue blockheaderToJSON(const CBlockIndex* blockindex)
{
    AssertLockHeld(cs_main);
    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("hash", blockindex->GetBlockHash().GetHex()));
    int confirmations = -1;
    // Only report confirmations if the block is on the main chain
    if (chainActive.Contains(blockindex))
        confirmations = chainActive.Height() - blockindex->nHeight + 1;
    result.push_back(Pair("confirmations", confirmations));
    result.push_back(Pair("height", blockindex->nHeight));
    result.push_back(Pair("version", blockindex->nVersion));
    result.push_back(Pair("versionHex", strprintf("%08x", blockindex->nVersion)));
    result.push_back(Pair("merkleroot", blockindex->hashMerkleRoot.GetHex()));
	
    result.push_back(Pair("time", (int64_t)blockindex->nTime));
    result.push_back(Pair("mediantime", (int64_t)blockindex->GetMedianTimePast()));
    result.push_back(Pair("nonce", (uint64_t)blockindex->nNonce));
    result.push_back(Pair("bits", strprintf("%08x", blockindex->nBits)));
    result.push_back(Pair("difficulty", GetDifficulty(blockindex)));
    result.push_back(Pair("chainwork", blockindex->nChainWork.GetHex()));
    result.push_back(Pair("nTx", (uint64_t)blockindex->nTx));

    if (blockindex->pprev)
        result.push_back(Pair("previousblockhash", blockindex->pprev->GetBlockHash().GetHex()));
    CBlockIndex *pnext = chainActive.Next(blockindex);
    if (pnext)
        result.push_back(Pair("nextblockhash", pnext->GetBlockHash().GetHex()));

    result.push_back(Pair("chainlock", llmq::chainLocksHandler->HasChainLock(blockindex->nHeight, blockindex->GetBlockHash())));

    return result;
}

UniValue blockToJSON(const CBlock& block, const CBlockIndex* blockindex, bool txDetails)
{
    AssertLockHeld(cs_main);
    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("hash", blockindex->GetBlockHash().GetHex()));
    int confirmations = -1;
    // Only report confirmations if the block is on the main chain
    if (chainActive.Contains(blockindex))
        confirmations = chainActive.Height() - blockindex->nHeight + 1;
    result.push_back(Pair("confirmations", confirmations));
    result.push_back(Pair("size", (int)::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION)));
    result.push_back(Pair("height", blockindex->nHeight));
    result.push_back(Pair("version", block.nVersion));
    result.push_back(Pair("versionHex", strprintf("%08x", block.nVersion)));
    result.push_back(Pair("merkleroot", block.hashMerkleRoot.GetHex()));
	bool chainLock = llmq::chainLocksHandler->HasChainLock(blockindex->nHeight, blockindex->GetBlockHash());
    UniValue txs(UniValue::VARR);
    for(const auto& tx : block.vtx)
    {
        if(txDetails)
        {
            UniValue objTx(UniValue::VOBJ);
            TxToUniv(*tx, uint256(), objTx, true);
            bool fLocked = llmq::quorumInstantSendManager->IsLocked(tx->GetHash());
            objTx.push_back(Pair("instantlock", fLocked || chainLock));
            objTx.push_back(Pair("instantlock_internal", fLocked));
            txs.push_back(objTx);
        }
        else
            txs.push_back(tx->GetHash().GetHex());
    }
    result.push_back(Pair("tx", txs));
    if (!block.vtx[0]->vExtraPayload.empty()) {
        CCbTx cbTx;
        if (GetTxPayload(block.vtx[0]->vExtraPayload, cbTx)) {
            UniValue cbTxObj;
            cbTx.ToJson(cbTxObj);
            result.push_back(Pair("cbTx", cbTxObj));
        }
    }
    result.push_back(Pair("time", block.GetBlockTime()));
    result.push_back(Pair("mediantime", (int64_t)blockindex->GetMedianTimePast()));
	result.push_back(Pair("hrtime", TimestampToHRDate(block.GetBlockTime())));
    result.push_back(Pair("nonce", (uint64_t)block.nNonce));
    result.push_back(Pair("bits", strprintf("%08x", block.nBits)));
    result.push_back(Pair("difficulty", GetDifficulty(blockindex)));
    result.push_back(Pair("chainwork", blockindex->nChainWork.GetHex()));
    result.push_back(Pair("nTx", (uint64_t)blockindex->nTx));
	result.push_back(Pair("subsidy", block.vtx[0]->vout[0].nValue/COIN));
	result.push_back(Pair("blockversion", GetBlockVersion(block.vtx[0]->vout[0].sTxOutMessage)));
	if (block.vtx.size() > 1)
		result.push_back(Pair("sanctuary_reward", block.vtx[0]->vout[1].nValue/COIN));
	// BiblePay
	bool bShowPrayers = true;
    
	if (blockindex->pprev)
	{
        result.push_back(Pair("previousblockhash", blockindex->pprev->GetBlockHash().GetHex()));
		const Consensus::Params& consensusParams = Params().GetConsensus();
		std::string sVerses = GetBibleHashVerses(block.GetHash(), block.GetBlockTime(), blockindex->pprev->nTime, blockindex->pprev->nHeight, blockindex->pprev);
		if (bShowPrayers) 
			result.push_back(Pair("verses", sVerses));
		result.push_back(Pair("chaindata", block.vtx[0]->vout[0].sTxOutMessage));
		bool fChainLock = llmq::chainLocksHandler->HasChainLock(blockindex->nHeight, blockindex->GetBlockHash());
		result.push_back(Pair("chainlock", fChainLock));
		/*
		UniValue objIPFS(UniValue::VOBJ);
		
		for (auto item: mapSidechainTransactions)
		{
			if (item.second.nHeight == blockindex->nHeight)
			{
				std::string sDesc = "FileName: " + item.second.FileName + ", Fee=" + RoundToString(item.second.nFee/COIN, 4) + ", Size=" + RoundToString(item.second.nSize, 2) 
					+ ", Duration=" + RoundToString(item.second.nDuration, 0)
					+ ", Density=" + RoundToString(item.second.nDensity, 0) + ", BlockHash=" + item.second.BlockHash + ", URL=" + item.second.URL + ", Network=" + item.second.Network 
					+ ", Height=" + RoundToString(item.second.nHeight, 0);
				objIPFS.push_back(Pair(item.second.TXID, sDesc));
			}
		}
		result.push_back(Pair("bipfs", objIPFS));
		*/
    }


    CBlockIndex *pnext = chainActive.Next(blockindex);
    if (pnext)
        result.push_back(Pair("nextblockhash", pnext->GetBlockHash().GetHex()));

	// Genesis Block only:
	if (blockindex && blockindex->nHeight==0)
	{
		int iStart=0;
		int iEnd=0;
		// Display a verse from Genesis 1:1 for The Genesis Block:
		GetBookStartEnd("gen", iStart, iEnd);
		std::string sVerse = GetVerse("gen", 1, 1, iStart - 1, iEnd);
		boost::trim(sVerse);
		result.push_back(Pair("verses", sVerse));
	}

    result.push_back(Pair("chainlock", chainLock));

    return result;
}

UniValue getblockcount(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getblockcount\n"
            "\nReturns the number of blocks in the longest blockchain.\n"
            "\nResult:\n"
            "n    (numeric) The current block count\n"
            "\nExamples:\n"
            + HelpExampleCli("getblockcount", "")
            + HelpExampleRpc("getblockcount", "")
        );

    LOCK(cs_main);
    return chainActive.Height();
}

UniValue getbestblockhash(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getbestblockhash\n"
            "\nReturns the hash of the best (tip) block in the longest blockchain.\n"
            "\nResult:\n"
            "\"hex\"      (string) the block hash hex encoded\n"
            "\nExamples:\n"
            + HelpExampleCli("getbestblockhash", "")
            + HelpExampleRpc("getbestblockhash", "")
        );

    LOCK(cs_main);
    return chainActive.Tip()->GetBlockHash().GetHex();
}

UniValue getbestchainlock(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getbestchainlock\n"
            "\nReturns the block hash of the best chainlock. Throws an error if there is no known chainlock yet. "
            "\nResult:\n"
            "{\n"
            "  \"blockhash\" : \"hash\",      (string) The block hash hex encoded\n"
            "  \"height\" : n,              (numeric) The block height or index\n"
            "  \"known_block\" : true|false (boolean) True if the block is known by our node\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getbestchainlock", "")
            + HelpExampleRpc("getbestchainlock", "")
        );
    UniValue result(UniValue::VOBJ);

    llmq::CChainLockSig clsig = llmq::chainLocksHandler->GetBestChainLock();
    if (clsig.IsNull()) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Unable to find any chainlock");
    }
    result.push_back(Pair("blockhash", clsig.blockHash.GetHex()));
    result.push_back(Pair("height", clsig.nHeight));
    LOCK(cs_main);
    result.push_back(Pair("known_block", mapBlockIndex.count(clsig.blockHash) > 0));
    return result;
}

void RPCNotifyBlockChange(bool ibd, const CBlockIndex * pindex)
{
    if(pindex) {
        std::lock_guard<std::mutex> lock(cs_blockchange);
        latestblock.hash = pindex->GetBlockHash();
        latestblock.height = pindex->nHeight;
    }
    cond_blockchange.notify_all();
}

UniValue waitfornewblock(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "waitfornewblock (timeout)\n"
            "\nWaits for a specific new block and returns useful info about it.\n"
            "\nReturns the current block on timeout or exit.\n"
            "\nArguments:\n"
            "1. timeout (int, optional, default=0) Time in milliseconds to wait for a response. 0 indicates no timeout.\n"
            "\nResult:\n"
            "{                           (json object)\n"
            "  \"hash\" : {       (string) The blockhash\n"
            "  \"height\" : {     (int) Block height\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("waitfornewblock", "1000")
            + HelpExampleRpc("waitfornewblock", "1000")
        );
    int timeout = 0;
    if (!request.params[0].isNull())
        timeout = request.params[0].get_int();

    CUpdatedBlock block;
    {
        std::unique_lock<std::mutex> lock(cs_blockchange);
        block = latestblock;
        if(timeout)
            cond_blockchange.wait_for(lock, std::chrono::milliseconds(timeout), [&block]{return latestblock.height != block.height || latestblock.hash != block.hash || !IsRPCRunning(); });
        else
            cond_blockchange.wait(lock, [&block]{return latestblock.height != block.height || latestblock.hash != block.hash || !IsRPCRunning(); });
        block = latestblock;
    }
    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("hash", block.hash.GetHex()));
    ret.push_back(Pair("height", block.height));
    return ret;
}

UniValue waitforblock(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "waitforblock <blockhash> (timeout)\n"
            "\nWaits for a specific new block and returns useful info about it.\n"
            "\nReturns the current block on timeout or exit.\n"
            "\nArguments:\n"
            "1. \"blockhash\" (required, string) Block hash to wait for.\n"
            "2. timeout       (int, optional, default=0) Time in milliseconds to wait for a response. 0 indicates no timeout.\n"
            "\nResult:\n"
            "{                           (json object)\n"
            "  \"hash\" : {       (string) The blockhash\n"
            "  \"height\" : {     (int) Block height\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("waitforblock", "\"0000000000079f8ef3d2c688c244eb7a4570b24c9ed7b4a8c619eb02596f8862\", 1000")
            + HelpExampleRpc("waitforblock", "\"0000000000079f8ef3d2c688c244eb7a4570b24c9ed7b4a8c619eb02596f8862\", 1000")
        );
    int timeout = 0;

    uint256 hash = uint256S(request.params[0].get_str());

    if (!request.params[1].isNull())
        timeout = request.params[1].get_int();

    CUpdatedBlock block;
    {
        std::unique_lock<std::mutex> lock(cs_blockchange);
        if(timeout)
            cond_blockchange.wait_for(lock, std::chrono::milliseconds(timeout), [&hash]{return latestblock.hash == hash || !IsRPCRunning();});
        else
            cond_blockchange.wait(lock, [&hash]{return latestblock.hash == hash || !IsRPCRunning(); });
        block = latestblock;
    }

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("hash", block.hash.GetHex()));
    ret.push_back(Pair("height", block.height));
    return ret;
}

UniValue waitforblockheight(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "waitforblockheight <height> (timeout)\n"
            "\nWaits for (at least) block height and returns the height and hash\n"
            "of the current tip.\n"
            "\nReturns the current block on timeout or exit.\n"
            "\nArguments:\n"
            "1. height  (required, int) Block height to wait for (int)\n"
            "2. timeout (int, optional, default=0) Time in milliseconds to wait for a response. 0 indicates no timeout.\n"
            "\nResult:\n"
            "{                           (json object)\n"
            "  \"hash\" : {       (string) The blockhash\n"
            "  \"height\" : {     (int) Block height\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("waitforblockheight", "\"100\", 1000")
            + HelpExampleRpc("waitforblockheight", "\"100\", 1000")
        );
    int timeout = 0;

    int height = request.params[0].get_int();

    if (!request.params[1].isNull())
        timeout = request.params[1].get_int();

    CUpdatedBlock block;
    {
        std::unique_lock<std::mutex> lock(cs_blockchange);
        if(timeout)
            cond_blockchange.wait_for(lock, std::chrono::milliseconds(timeout), [&height]{return latestblock.height >= height || !IsRPCRunning();});
        else
            cond_blockchange.wait(lock, [&height]{return latestblock.height >= height || !IsRPCRunning(); });
        block = latestblock;
    }
    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("hash", block.hash.GetHex()));
    ret.push_back(Pair("height", block.height));
    return ret;
}

UniValue getdifficulty(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getdifficulty\n"
            "\nReturns the proof-of-work difficulty as a multiple of the minimum difficulty.\n"
            "\nResult:\n"
            "n.nnn       (numeric) the proof-of-work difficulty as a multiple of the minimum difficulty.\n"
            "\nExamples:\n"
            + HelpExampleCli("getdifficulty", "")
            + HelpExampleRpc("getdifficulty", "")
        );

    LOCK(cs_main);
    return GetDifficulty();
}

std::string EntryDescriptionString()
{
    return "    \"size\" : n,                 (numeric) transaction size in bytes\n"
           "    \"fee\" : n,                  (numeric) transaction fee in " + CURRENCY_UNIT + "\n"
           "    \"modifiedfee\" : n,          (numeric) transaction fee with fee deltas used for mining priority\n"
           "    \"time\" : n,                 (numeric) local time transaction entered pool in seconds since 1 Jan 1970 GMT\n"
           "    \"height\" : n,               (numeric) block height when transaction entered pool\n"
           "    \"descendantcount\" : n,      (numeric) number of in-mempool descendant transactions (including this one)\n"
           "    \"descendantsize\" : n,       (numeric) size of in-mempool descendants (including this one)\n"
           "    \"descendantfees\" : n,       (numeric) modified fees (see above) of in-mempool descendants (including this one)\n"
           "    \"ancestorcount\" : n,        (numeric) number of in-mempool ancestor transactions (including this one)\n"
           "    \"ancestorsize\" : n,         (numeric) size of in-mempool ancestors (including this one)\n"
           "    \"ancestorfees\" : n,         (numeric) modified fees (see above) of in-mempool ancestors (including this one)\n"
           "    \"depends\" : [               (array) unconfirmed transactions used as inputs for this transaction\n"
           "        \"transactionid\",        (string) parent transaction id\n"
           "       ... ],\n"
           "    \"instantlock\" : true|false  (boolean) True if this transaction was locked via InstantSend\n";
}

void entryToJSON(UniValue &info, const CTxMemPoolEntry &e)
{
    AssertLockHeld(mempool.cs);

    info.push_back(Pair("size", (int)e.GetTxSize()));
    info.push_back(Pair("fee", ValueFromAmount(e.GetFee())));
    info.push_back(Pair("modifiedfee", ValueFromAmount(e.GetModifiedFee())));
    info.push_back(Pair("time", e.GetTime()));
    info.push_back(Pair("height", (int)e.GetHeight()));
    info.push_back(Pair("descendantcount", e.GetCountWithDescendants()));
    info.push_back(Pair("descendantsize", e.GetSizeWithDescendants()));
    info.push_back(Pair("descendantfees", e.GetModFeesWithDescendants()));
    info.push_back(Pair("ancestorcount", e.GetCountWithAncestors()));
    info.push_back(Pair("ancestorsize", e.GetSizeWithAncestors()));
    info.push_back(Pair("ancestorfees", e.GetModFeesWithAncestors()));
    const CTransaction& tx = e.GetTx();
    std::set<std::string> setDepends;
    for (const CTxIn& txin : tx.vin)
    {
        if (mempool.exists(txin.prevout.hash))
            setDepends.insert(txin.prevout.hash.ToString());
    }

    UniValue depends(UniValue::VARR);
    for (const std::string& dep : setDepends)
    {
        depends.push_back(dep);
    }

    info.push_back(Pair("depends", depends));
    info.push_back(Pair("instantlock", llmq::quorumInstantSendManager->IsLocked(tx.GetHash())));
}

UniValue mempoolToJSON(bool fVerbose)
{
    if (fVerbose)
    {
        LOCK(mempool.cs);
        UniValue o(UniValue::VOBJ);
        for (const CTxMemPoolEntry& e : mempool.mapTx)
        {
            const uint256& hash = e.GetTx().GetHash();
            UniValue info(UniValue::VOBJ);
            entryToJSON(info, e);
            o.push_back(Pair(hash.ToString(), info));
        }
        return o;
    }
    else
    {
        std::vector<uint256> vtxid;
        mempool.queryHashes(vtxid);

        UniValue a(UniValue::VARR);
        for (const uint256& hash : vtxid)
            a.push_back(hash.ToString());

        return a;
    }
}

UniValue getrawmempool(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "getrawmempool ( verbose )\n"
            "\nReturns all transaction ids in memory pool as a json array of string transaction ids.\n"
            "\nHint: use getmempoolentry to fetch a specific transaction from the mempool.\n"
            "\nArguments:\n"
            "1. verbose (boolean, optional, default=false) True for a json object, false for array of transaction ids\n"
            "\nResult: (for verbose = false):\n"
            "[                     (json array of string)\n"
            "  \"transactionid\"     (string) The transaction id\n"
            "  ,...\n"
            "]\n"
            "\nResult: (for verbose = true):\n"
            "{                           (json object)\n"
            "  \"transactionid\" : {       (json object)\n"
            + EntryDescriptionString()
            + "  }, ...\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getrawmempool", "true")
            + HelpExampleRpc("getrawmempool", "true")
        );

    bool fVerbose = false;
    if (!request.params[0].isNull())
        fVerbose = request.params[0].get_bool();

    return mempoolToJSON(fVerbose);
}

UniValue getmempoolancestors(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2) {
        throw std::runtime_error(
            "getmempoolancestors txid (verbose)\n"
            "\nIf txid is in the mempool, returns all in-mempool ancestors.\n"
            "\nArguments:\n"
            "1. \"txid\"                 (string, required) The transaction id (must be in mempool)\n"
            "2. verbose                  (boolean, optional, default=false) True for a json object, false for array of transaction ids\n"
            "\nResult (for verbose=false):\n"
            "[                       (json array of strings)\n"
            "  \"transactionid\"           (string) The transaction id of an in-mempool ancestor transaction\n"
            "  ,...\n"
            "]\n"
            "\nResult (for verbose=true):\n"
            "{                           (json object)\n"
            "  \"transactionid\" : {       (json object)\n"
            + EntryDescriptionString()
            + "  }, ...\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getmempoolancestors", "\"mytxid\"")
            + HelpExampleRpc("getmempoolancestors", "\"mytxid\"")
            );
    }

    bool fVerbose = false;
    if (!request.params[1].isNull())
        fVerbose = request.params[1].get_bool();

    uint256 hash = ParseHashV(request.params[0], "parameter 1");

    LOCK(mempool.cs);

    CTxMemPool::txiter it = mempool.mapTx.find(hash);
    if (it == mempool.mapTx.end()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not in mempool");
    }

    CTxMemPool::setEntries setAncestors;
    uint64_t noLimit = std::numeric_limits<uint64_t>::max();
    std::string dummy;
    mempool.CalculateMemPoolAncestors(*it, setAncestors, noLimit, noLimit, noLimit, noLimit, dummy, false);

    if (!fVerbose) {
        UniValue o(UniValue::VARR);
        for (CTxMemPool::txiter ancestorIt : setAncestors) {
            o.push_back(ancestorIt->GetTx().GetHash().ToString());
        }

        return o;
    } else {
        UniValue o(UniValue::VOBJ);
        for (CTxMemPool::txiter ancestorIt : setAncestors) {
            const CTxMemPoolEntry &e = *ancestorIt;
            const uint256& _hash = e.GetTx().GetHash();
            UniValue info(UniValue::VOBJ);
            entryToJSON(info, e);
            o.push_back(Pair(_hash.ToString(), info));
        }
        return o;
    }
}

UniValue getmempooldescendants(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2) {
        throw std::runtime_error(
            "getmempooldescendants txid (verbose)\n"
            "\nIf txid is in the mempool, returns all in-mempool descendants.\n"
            "\nArguments:\n"
            "1. \"txid\"                 (string, required) The transaction id (must be in mempool)\n"
            "2. verbose                  (boolean, optional, default=false) True for a json object, false for array of transaction ids\n"
            "\nResult (for verbose=false):\n"
            "[                       (json array of strings)\n"
            "  \"transactionid\"           (string) The transaction id of an in-mempool descendant transaction\n"
            "  ,...\n"
            "]\n"
            "\nResult (for verbose=true):\n"
            "{                           (json object)\n"
            "  \"transactionid\" : {       (json object)\n"
            + EntryDescriptionString()
            + "  }, ...\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getmempooldescendants", "\"mytxid\"")
            + HelpExampleRpc("getmempooldescendants", "\"mytxid\"")
            );
    }

    bool fVerbose = false;
    if (!request.params[1].isNull())
        fVerbose = request.params[1].get_bool();

    uint256 hash = ParseHashV(request.params[0], "parameter 1");

    LOCK(mempool.cs);

    CTxMemPool::txiter it = mempool.mapTx.find(hash);
    if (it == mempool.mapTx.end()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not in mempool");
    }

    CTxMemPool::setEntries setDescendants;
    mempool.CalculateDescendants(it, setDescendants);
    // CTxMemPool::CalculateDescendants will include the given tx
    setDescendants.erase(it);

    if (!fVerbose) {
        UniValue o(UniValue::VARR);
        for (CTxMemPool::txiter descendantIt : setDescendants) {
            o.push_back(descendantIt->GetTx().GetHash().ToString());
        }

        return o;
    } else {
        UniValue o(UniValue::VOBJ);
        for (CTxMemPool::txiter descendantIt : setDescendants) {
            const CTxMemPoolEntry &e = *descendantIt;
            const uint256& _hash = e.GetTx().GetHash();
            UniValue info(UniValue::VOBJ);
            entryToJSON(info, e);
            o.push_back(Pair(_hash.ToString(), info));
        }
        return o;
    }
}

UniValue getmempoolentry(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
            "getmempoolentry txid\n"
            "\nReturns mempool data for given transaction\n"
            "\nArguments:\n"
            "1. \"txid\"                   (string, required) The transaction id (must be in mempool)\n"
            "\nResult:\n"
            "{                           (json object)\n"
            + EntryDescriptionString()
            + "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getmempoolentry", "\"mytxid\"")
            + HelpExampleRpc("getmempoolentry", "\"mytxid\"")
        );
    }

    uint256 hash = ParseHashV(request.params[0], "parameter 1");

    LOCK(mempool.cs);

    CTxMemPool::txiter it = mempool.mapTx.find(hash);
    if (it == mempool.mapTx.end()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not in mempool");
    }

    const CTxMemPoolEntry &e = *it;
    UniValue info(UniValue::VOBJ);
    entryToJSON(info, e);
    return info;
}

UniValue getblockhashes(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 2)
        throw std::runtime_error(
            "getblockhashes timestamp\n"
            "\nReturns array of hashes of blocks within the timestamp range provided.\n"
            "\nArguments:\n"
            "1. high         (numeric, required) The newer block timestamp\n"
            "2. low          (numeric, required) The older block timestamp\n"
            "\nResult:\n"
            "[\n"
            "  \"hash\"         (string) The block hash\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("getblockhashes", "1231614698 1231024505")
            + HelpExampleRpc("getblockhashes", "1231614698, 1231024505")
        );

    unsigned int high = request.params[0].get_int();
    unsigned int low = request.params[1].get_int();
    std::vector<uint256> blockHashes;

    if (!GetTimestampIndex(high, low, blockHashes)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available for block hashes");
    }

    UniValue result(UniValue::VARR);
    for (std::vector<uint256>::const_iterator it=blockHashes.begin(); it!=blockHashes.end(); it++) {
        result.push_back(it->GetHex());
    }

    return result;
}

UniValue getblockhash(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "getblockhash height\n"
            "\nReturns hash of block in best-block-chain at height provided.\n"
            "\nArguments:\n"
            "1. height         (numeric, required) The height index\n"
            "\nResult:\n"
            "\"hash\"         (string) The block hash\n"
            "\nExamples:\n"
            + HelpExampleCli("getblockhash", "1000")
            + HelpExampleRpc("getblockhash", "1000")
        );

    LOCK(cs_main);

    int nHeight = request.params[0].get_int();
    if (nHeight < 0 || nHeight > chainActive.Height())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Block height out of range");

    CBlockIndex* pblockindex = chainActive[nHeight];
    return pblockindex->GetBlockHash().GetHex();
}

UniValue getblockheader(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "getblockheader \"hash\" ( verbose )\n"
            "\nIf verbose is false, returns a string that is serialized, hex-encoded data for blockheader 'hash'.\n"
            "If verbose is true, returns an Object with information about blockheader <hash>.\n"
            "\nArguments:\n"
            "1. \"hash\"          (string, required) The block hash\n"
            "2. verbose           (boolean, optional, default=true) true for a json object, false for the hex encoded data\n"
            "\nResult (for verbose = true):\n"
            "{\n"
            "  \"hash\" : \"hash\",     (string) the block hash (same as provided)\n"
            "  \"confirmations\" : n,   (numeric) The number of confirmations, or -1 if the block is not on the main chain\n"
            "  \"height\" : n,          (numeric) The block height or index\n"
            "  \"version\" : n,         (numeric) The block version\n"
            "  \"versionHex\" : \"00000000\", (string) The block version formatted in hexadecimal\n"
            "  \"merkleroot\" : \"xxxx\", (string) The merkle root\n"
            "  \"time\" : ttt,          (numeric) The block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"mediantime\" : ttt,    (numeric) The median block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"nonce\" : n,           (numeric) The nonce\n"
            "  \"bits\" : \"1d00ffff\", (string) The bits\n"
            "  \"difficulty\" : x.xxx,  (numeric) The difficulty\n"
            "  \"chainwork\" : \"0000...1f3\"     (string) Expected number of hashes required to produce the current chain (in hex)\n"
            "  \"nTx\" : n,             (numeric) The number of transactions in the block.\n"
            "  \"previousblockhash\" : \"hash\",  (string) The hash of the previous block\n"
            "  \"nextblockhash\" : \"hash\",      (string) The hash of the next block\n"
            "}\n"
            "\nResult (for verbose=false):\n"
            "\"data\"             (string) A string that is serialized, hex-encoded data for block 'hash'.\n"
            "\nExamples:\n"
            + HelpExampleCli("getblockheader", "\"00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09\"")
            + HelpExampleRpc("getblockheader", "\"00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09\"")
        );

    LOCK(cs_main);

    std::string strHash = request.params[0].get_str();
    uint256 hash(uint256S(strHash));

    bool fVerbose = true;
    if (!request.params[1].isNull())
        fVerbose = request.params[1].get_bool();

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    CBlockIndex* pblockindex = mapBlockIndex[hash];

    if (!fVerbose)
    {
        CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
        ssBlock << pblockindex->GetBlockHeader();
        std::string strHex = HexStr(ssBlock.begin(), ssBlock.end());
        return strHex;
    }

    return blockheaderToJSON(pblockindex);
}

UniValue getblockheaders(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 3)
        throw std::runtime_error(
            "getblockheaders \"hash\" ( count verbose )\n"
            "\nReturns an array of items with information about <count> blockheaders starting from <hash>.\n"
            "\nIf verbose is false, each item is a string that is serialized, hex-encoded data for a single blockheader.\n"
            "If verbose is true, each item is an Object with information about a single blockheader.\n"
            "\nArguments:\n"
            "1. \"hash\"          (string, required) The block hash\n"
            "2. count           (numeric, optional, default/max=" + strprintf("%s", MAX_HEADERS_RESULTS) +")\n"
            "3. verbose         (boolean, optional, default=true) true for a json object, false for the hex encoded data\n"
            "\nResult (for verbose = true):\n"
            "[ {\n"
            "  \"hash\" : \"hash\",               (string)  The block hash\n"
            "  \"confirmations\" : n,           (numeric) The number of confirmations, or -1 if the block is not on the main chain\n"
            "  \"height\" : n,                  (numeric) The block height or index\n"
            "  \"version\" : n,                 (numeric) The block version\n"
            "  \"merkleroot\" : \"xxxx\",         (string)  The merkle root\n"
            "  \"time\" : ttt,                  (numeric) The block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"mediantime\" : ttt,            (numeric) The median block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"nonce\" : n,                   (numeric) The nonce\n"
            "  \"bits\" : \"1d00ffff\",           (string)  The bits\n"
            "  \"difficulty\" : x.xxx,          (numeric) The difficulty\n"
            "  \"chainwork\" : \"0000...1f3\"     (string)  Expected number of hashes required to produce the current chain (in hex)\n"
            "  \"previousblockhash\" : \"hash\",  (string)  The hash of the previous block\n"
            "  \"nextblockhash\" : \"hash\",      (string)  The hash of the next block\n"
            "}, {\n"
            "       ...\n"
            "   },\n"
            "...\n"
            "]\n"
            "\nResult (for verbose=false):\n"
            "[\n"
            "  \"data\",                        (string)  A string that is serialized, hex-encoded data for block header.\n"
            "  ...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("getblockheaders", "\"00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09\" 2000")
            + HelpExampleRpc("getblockheaders", "\"00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09\" 2000")
        );

    LOCK(cs_main);

    std::string strHash = request.params[0].get_str();
    uint256 hash(uint256S(strHash));

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    int nCount = MAX_HEADERS_RESULTS;
    if (!request.params[1].isNull())
        nCount = request.params[1].get_int();

    if (nCount <= 0 || nCount > (int)MAX_HEADERS_RESULTS)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Count is out of range");

    bool fVerbose = true;
    if (!request.params[2].isNull())
        fVerbose = request.params[2].get_bool();

    CBlockIndex* pblockindex = mapBlockIndex[hash];

    UniValue arrHeaders(UniValue::VARR);

    if (!fVerbose)
    {
        for (; pblockindex; pblockindex = chainActive.Next(pblockindex))
        {
            CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
            ssBlock << pblockindex->GetBlockHeader();
            std::string strHex = HexStr(ssBlock.begin(), ssBlock.end());
            arrHeaders.push_back(strHex);
            if (--nCount <= 0)
                break;
        }
        return arrHeaders;
    }

    for (; pblockindex; pblockindex = chainActive.Next(pblockindex))
    {
        arrHeaders.push_back(blockheaderToJSON(pblockindex));
        if (--nCount <= 0)
            break;
    }

    return arrHeaders;
}

static CBlock GetBlockChecked(const CBlockIndex* pblockindex)
{
    CBlock block;
    if (fHavePruned && !(pblockindex->nStatus & BLOCK_HAVE_DATA) && pblockindex->nTx > 0) {
        throw JSONRPCError(RPC_MISC_ERROR, "Block not available (pruned data)");
    }

    if (!ReadBlockFromDisk(block, pblockindex, Params().GetConsensus())) {
        // Block not found on disk. This could be because we have the block
        // header in our index but don't have the block (for example if a
        // non-whitelisted node sends us an unrequested long chain of valid
        // blocks, we add the headers to our index, but don't accept the
        // block).
        throw JSONRPCError(RPC_MISC_ERROR, "Block not found on disk");
    }

    return block;
}


UniValue getmerkleblocks(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 3)
        throw std::runtime_error(
            "getmerkleblocks \"filter\" \"hash\" ( count )\n"
            "\nReturns an array of hex-encoded merkleblocks for <count> blocks starting from <hash> which match <filter>.\n"
            "\nArguments:\n"
            "1. \"filter\"        (string, required) The hex encoded bloom filter\n"
            "2. \"hash\"          (string, required) The block hash\n"
            "3. count           (numeric, optional, default/max=" + strprintf("%s", MAX_HEADERS_RESULTS) +")\n"
            "\nResult:\n"
            "[\n"
            "  \"data\",                        (string)  A string that is serialized, hex-encoded data for a merkleblock.\n"
            "  ...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("getmerkleblocks", "\"2303028005802040100040000008008400048141010000f8400420800080025004000004130000000000000001\" \"00000000007e1432d2af52e8463278bf556b55cf5049262f25634557e2e91202\" 2000")
            + HelpExampleRpc("getmerkleblocks", "\"2303028005802040100040000008008400048141010000f8400420800080025004000004130000000000000001\" \"00000000007e1432d2af52e8463278bf556b55cf5049262f25634557e2e91202\" 2000")
        );

    LOCK(cs_main);

    CBloomFilter filter;
    std::string strFilter = request.params[0].get_str();
    CDataStream ssBloomFilter(ParseHex(strFilter), SER_NETWORK, PROTOCOL_VERSION);
    ssBloomFilter >> filter;
    if (!filter.IsWithinSizeConstraints()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Filter is not within size constraints");
    }
    filter.UpdateEmptyFull();

    std::string strHash = request.params[1].get_str();
    uint256 hash(uint256S(strHash));

    if (mapBlockIndex.count(hash) == 0) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
    }

    int nCount = MAX_HEADERS_RESULTS;
    if (!request.params[2].isNull())
        nCount = request.params[2].get_int();

    if (nCount <= 0 || nCount > (int)MAX_HEADERS_RESULTS) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Count is out of range");
    }

    CBlockIndex* pblockindex = mapBlockIndex[hash];
    CBlock block = GetBlockChecked(pblockindex);

    UniValue arrMerkleBlocks(UniValue::VARR);

    for (; pblockindex; pblockindex = chainActive.Next(pblockindex))
    {
        if (--nCount < 0) {
            break;
        }

        if (!ReadBlockFromDisk(block, pblockindex, Params().GetConsensus())) {
            // this shouldn't happen, we already checked pruning case earlier
            throw JSONRPCError(RPC_MISC_ERROR, "Block not found on disk");
        }

        CMerkleBlock merkleblock(block, filter);
        if (merkleblock.vMatchedTxn.empty()) {
            // ignore blocks that do not match the filter
            continue;
        }

        CDataStream ssMerkleBlock(SER_NETWORK, PROTOCOL_VERSION);
        ssMerkleBlock << merkleblock;
        std::string strHex = HexStr(ssMerkleBlock);
        arrMerkleBlocks.push_back(strHex);
    }
    return arrMerkleBlocks;
}

UniValue getblock(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "getblock \"blockhash\" ( verbosity ) \n"
            "\nIf verbosity is 0, returns a string that is serialized, hex-encoded data for block 'hash'.\n"
            "If verbosity is 1, returns an Object with information about block <hash>.\n"
            "If verbosity is 2, returns an Object with information about block <hash> and information about each transaction. \n"
            "\nArguments:\n"
            "1. \"blockhash\"          (string, required) The block hash\n"
            "2. verbosity              (numeric, optional, default=1) 0 for hex-encoded data, 1 for a json object, and 2 for json object with transaction data\n"
            "\nResult (for verbosity = 0):\n"
            "\"data\"             (string) A string that is serialized, hex-encoded data for block 'hash'.\n"
            "\nResult (for verbose = 1):\n"
            "{\n"
            "  \"hash\" : \"hash\",     (string) the block hash (same as provided)\n"
            "  \"confirmations\" : n,   (numeric) The number of confirmations, or -1 if the block is not on the main chain\n"
            "  \"size\" : n,            (numeric) The block size\n"
            "  \"height\" : n,          (numeric) The block height or index\n"
            "  \"version\" : n,         (numeric) The block version\n"
            "  \"versionHex\" : \"00000000\", (string) The block version formatted in hexadecimal\n"
            "  \"merkleroot\" : \"xxxx\", (string) The merkle root\n"
            "  \"tx\" : [               (array of string) The transaction ids\n"
            "     \"transactionid\"     (string) The transaction id\n"
            "     ,...\n"
            "  ],\n"
            "  \"cbTx\" : {             (json object) The coinbase special transaction \n"
            "     \"version\"           (numeric) The coinbase special transaction version\n"
            "     \"height\"            (numeric) The block height\n"
            "     \"merkleRootMNList\" : \"xxxx\", (string) The merkle root of the masternode list\n"
            "     \"merkleRootQuorums\" : \"xxxx\", (string) The merkle root of the quorum list\n"
            "  },\n"
            "  \"time\" : ttt,          (numeric) The block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"mediantime\" : ttt,    (numeric) The median block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"nonce\" : n,           (numeric) The nonce\n"
            "  \"bits\" : \"1d00ffff\", (string) The bits\n"
            "  \"difficulty\" : x.xxx,  (numeric) The difficulty\n"
            "  \"chainwork\" : \"xxxx\",  (string) Expected number of hashes required to produce the chain up to this block (in hex)\n"
            "  \"nTx\" : n,             (numeric) The number of transactions in the block.\n"
            "  \"previousblockhash\" : \"hash\",  (string) The hash of the previous block\n"
            "  \"nextblockhash\" : \"hash\"       (string) The hash of the next block\n"
            "}\n"
            "\nResult (for verbosity = 2):\n"
            "{\n"
            "  ...,                     Same output as verbosity = 1.\n"
            "  \"tx\" : [               (array of Objects) The transactions in the format of the getrawtransaction RPC. Different from verbosity = 1 \"tx\" result.\n"
            "         ,...\n"
            "  ],\n"
            "  ,...                     Same output as verbosity = 1.\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getblock", "\"00000000000fd08c2fb661d2fcb0d49abb3a91e5f27082ce64feed3b4dede2e2\"")
            + HelpExampleRpc("getblock", "\"00000000000fd08c2fb661d2fcb0d49abb3a91e5f27082ce64feed3b4dede2e2\"")
        );

    LOCK(cs_main);

    std::string strHash = request.params[0].get_str();
    uint256 hash(uint256S(strHash));

    int verbosity = 1;
    if (!request.params[1].isNull()) {
        if(request.params[1].isNum())
            verbosity = request.params[1].get_int();
        else
            verbosity = request.params[1].get_bool() ? 1 : 0;
    }

	int NUMBER_LENGTH_NON_HASH = 10;
	if (strHash.length() < NUMBER_LENGTH_NON_HASH && !strHash.empty())
	{
		CBlockIndex* bindex = FindBlockByHeight(cdbl(strHash, 0));
		if (bindex==NULL)
		    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found by height");
		hash = bindex->GetBlockHash();
	}
   
    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    CBlockIndex* pblockindex = mapBlockIndex[hash];
    const CBlock block = GetBlockChecked(pblockindex);

    if (verbosity <= 0)
    {
        CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
        ssBlock << block;
        std::string strHex = HexStr(ssBlock.begin(), ssBlock.end());
        return strHex;
    }

    return blockToJSON(block, pblockindex, verbosity >= 2);
}

struct CCoinsStats
{
    int nHeight;
    uint256 hashBlock;
    uint64_t nTransactions;
    uint64_t nTransactionOutputs;
    uint64_t nBogoSize;
    uint256 hashSerialized;
    uint64_t nDiskSize;
    CAmount nTotalAmount = 0;
    CAmount nTotalBurned = 0;
    CAmount nTotalDAC = 0;
    CCoinsStats() : nHeight(0), nTransactions(0), nTransactionOutputs(0), nBogoSize(0), nDiskSize(0), nTotalAmount(0), nTotalBurned(0), nTotalDAC(0) {}
};

static void ApplyStats(CCoinsStats &stats, CHashWriter& ss, const uint256& hash, const std::map<uint32_t, Coin>& outputs)
{
    assert(!outputs.empty());
    ss << hash;
    ss << VARINT(outputs.begin()->second.nHeight * 2 + outputs.begin()->second.fCoinBase);
    stats.nTransactions++;
	const Consensus::Params& consensusParams = Params().GetConsensus();
			
    for (const auto output : outputs) {
        ss << VARINT(output.first + 1);
        ss << output.second.out.scriptPubKey;
        ss << VARINT(output.second.out.nValue);
        stats.nTransactionOutputs++;
        stats.nTotalAmount += output.second.out.nValue;
        stats.nBogoSize += 32 /* txid */ + 4 /* vout index */ + 4 /* height + coinbase */ + 8 /* amount */ +
                           2 /* scriptPubKey len */ + output.second.out.scriptPubKey.size() /* scriptPubKey */;


		// BIBLEPAY - Track Burned Coins
		std::string sPK = PubKeyToAddress(output.second.out.scriptPubKey);
		if (sPK == consensusParams.BurnAddress)
		{
			stats.nTotalBurned += output.second.out.nValue;
		}
		else if (sPK == consensusParams.BurnAddressOrphanDonations)
		{
			stats.nTotalDAC += output.second.out.nValue;
		}
		else if (sPK == consensusParams.BurnAddressWhaleMatches)
		{
			stats.nTotalDAC += output.second.out.nValue;
		}
		else
		{
		    stats.nTotalAmount += output.second.out.nValue;
		}


    }
    ss << VARINT(0);
}

//! Calculate statistics about the unspent transaction output set
static bool GetUTXOStats(CCoinsView *view, CCoinsStats &stats)
{
    std::unique_ptr<CCoinsViewCursor> pcursor(view->Cursor());
    assert(pcursor);

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    stats.hashBlock = pcursor->GetBestBlock();
    {
        LOCK(cs_main);
        stats.nHeight = mapBlockIndex.find(stats.hashBlock)->second->nHeight;
    }
    ss << stats.hashBlock;
    uint256 prevkey;
    std::map<uint32_t, Coin> outputs;
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        COutPoint key;
        Coin coin;
        if (pcursor->GetKey(key) && pcursor->GetValue(coin)) {
            if (!outputs.empty() && key.hash != prevkey) {
                ApplyStats(stats, ss, prevkey, outputs);
                outputs.clear();
            }
            prevkey = key.hash;
            outputs[key.n] = std::move(coin);
        } else {
            return error("%s: unable to read value", __func__);
        }
        pcursor->Next();
    }
    if (!outputs.empty()) {
        ApplyStats(stats, ss, prevkey, outputs);
    }
    stats.hashSerialized = ss.GetHash();
    stats.nDiskSize = view->EstimateSize();
    return true;
}

UniValue pruneblockchain(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "pruneblockchain\n"
            "\nArguments:\n"
            "1. \"height\"       (numeric, required) The block height to prune up to. May be set to a discrete height, or a unix timestamp\n"
            "                  to prune blocks whose block time is at least 2 hours older than the provided timestamp.\n"
            "\nResult:\n"
            "n    (numeric) Height of the last block pruned.\n"
            "\nExamples:\n"
            + HelpExampleCli("pruneblockchain", "1000")
            + HelpExampleRpc("pruneblockchain", "1000"));

    if (!fPruneMode)
        throw JSONRPCError(RPC_MISC_ERROR, "Cannot prune blocks because node is not in prune mode.");

    LOCK(cs_main);

    int heightParam = request.params[0].get_int();
    if (heightParam < 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative block height.");

    // Height value more than a billion is too high to be a block height, and
    // too low to be a block time (corresponds to timestamp from Sep 2001).
    if (heightParam > 1000000000) {
        // Add a 2 hour buffer to include blocks which might have had old timestamps
        CBlockIndex* pindex = chainActive.FindEarliestAtLeast(heightParam - TIMESTAMP_WINDOW);
        if (!pindex) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Could not find block with at least the specified timestamp.");
        }
        heightParam = pindex->nHeight;
    }

    unsigned int height = (unsigned int) heightParam;
    unsigned int chainHeight = (unsigned int) chainActive.Height();
    if (chainHeight < Params().PruneAfterHeight())
        throw JSONRPCError(RPC_MISC_ERROR, "Blockchain is too short for pruning.");
    else if (height > chainHeight)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Blockchain is shorter than the attempted prune height.");
    else if (height > chainHeight - MIN_BLOCKS_TO_KEEP) {
        LogPrint(BCLog::RPC, "Attempt to prune blocks close to the tip.  Retaining the minimum number of blocks.");
        height = chainHeight - MIN_BLOCKS_TO_KEEP;
    }

    PruneBlockFilesManual(height);
    return uint64_t(height);
}

UniValue gettxoutsetinfo(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "gettxoutsetinfo\n"
            "\nReturns statistics about the unspent transaction output set.\n"
            "Note this call may take some time.\n"
            "\nResult:\n"
            "{\n"
            "  \"height\":n,     (numeric) The current block height (index)\n"
            "  \"bestblock\": \"hex\",   (string) The hash of the block at the tip of the chain\n"
            "  \"transactions\": n,      (numeric) The number of transactions with unspent outputs\n"
            "  \"txouts\": n,            (numeric) The number of unspent transaction outputs\n"
            "  \"bogosize\": n,          (numeric) A meaningless metric for UTXO set size\n"
            "  \"hash_serialized_2\": \"hash\", (string) The serialized hash\n"
            "  \"disk_size\": n,         (numeric) The estimated size of the chainstate on disk\n"
            "  \"total_amount\": x.xxx          (numeric) The total amount\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("gettxoutsetinfo", "")
            + HelpExampleRpc("gettxoutsetinfo", "")
        );

    UniValue ret(UniValue::VOBJ);

    CCoinsStats stats;
    FlushStateToDisk();
    if (GetUTXOStats(pcoinsdbview.get(), stats)) {
        ret.push_back(Pair("height", (int64_t)stats.nHeight));
        ret.push_back(Pair("bestblock", stats.hashBlock.GetHex()));
        ret.push_back(Pair("transactions", (int64_t)stats.nTransactions));
        ret.push_back(Pair("txouts", (int64_t)stats.nTransactionOutputs));
        ret.push_back(Pair("bogosize", (int64_t)stats.nBogoSize));
        ret.push_back(Pair("hash_serialized_2", stats.hashSerialized.GetHex()));
        ret.push_back(Pair("disk_size", stats.nDiskSize));

		// Circulating = Emitted minus burned
		ret.push_back(Pair("total_circulating_money_supply", ValueFromAmount(stats.nTotalAmount)));
		double nPct = ((stats.nTotalAmount/COIN) + .01) / 5200000000;
		ret.push_back(Pair("percent_emitted", nPct));

		// Emission target, Dec 2021: https://wiki.biblepay.org/Emission_Schedule_2020
		double nEmissionTargetDec2021 = 2847218800;
		ret.push_back(Pair("emission_target_dec_2021", nEmissionTargetDec2021));

		int64_t Dec2021_Epoch = 1640470023;
		double dTotalDeflationComponent = .50; 
		// With APM on and total deflation to date, we are in money saving mode for approx 150 more days.  By Dec 2021 our total circulating supply should match the above wiki page.
		// Once our budget is reconciled, we will disable dws whale staking and stick with our static emission schedule (that uses UTXO staking via GSC).
		
		int64_t nRemainingDays = (Dec2021_Epoch - GetAdjustedTime()) / 86400;
		int64_t nGenesisBlock = 1496347844;
		int64_t nDaysSinceGenesis = (GetAdjustedTime() - nGenesisBlock) / 86400;

		double nEmissionPerDay = stats.nTotalAmount/COIN / nDaysSinceGenesis;
		double nSythesizedTarget2021 = (nRemainingDays * (nEmissionPerDay * dTotalDeflationComponent)) + (stats.nTotalAmount / COIN);
		ret.push_back(Pair("avg_emissions_per_day", nEmissionPerDay));
		ret.push_back(Pair("estimated_emissions_as_of_dec_2021", nSythesizedTarget2021));

		double nBudgetaryHealthPct = nSythesizedTarget2021 / nEmissionTargetDec2021;
		ret.push_back(Pair("budgetary_health_pct_dec_2021", nBudgetaryHealthPct));

        ret.push_back(Pair("total_amount", ValueFromAmount(stats.nTotalAmount)));
    } else {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Unable to read UTXO set");
    }
    return ret;
}

UniValue gettxout(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 3)
        throw std::runtime_error(
            "gettxout \"txid\" n ( include_mempool )\n"
            "\nReturns details about an unspent transaction output.\n"
            "\nArguments:\n"
            "1. \"txid\"             (string, required) The transaction id\n"
            "2. \"n\"                (numeric, required) vout number\n"
            "3. \"include_mempool\"  (boolean, optional) Whether to include the mempool. Default: true."
            "     Note that an unspent output that is spent in the mempool won't appear.\n"
            "\nResult:\n"
            "{\n"
            "  \"bestblock\":  \"hash\",    (string) The hash of the block at the tip of the chain\n"
            "  \"confirmations\" : n,       (numeric) The number of confirmations\n"
            "  \"value\" : x.xxx,           (numeric) The transaction value in " + CURRENCY_UNIT + "\n"
            "  \"scriptPubKey\" : {         (json object)\n"
            "     \"asm\" : \"code\",       (string) \n"
            "     \"hex\" : \"hex\",        (string) \n"
            "     \"reqSigs\" : n,          (numeric) Number of required signatures\n"
            "     \"type\" : \"pubkeyhash\", (string) The type, eg pubkeyhash\n"
            "     \"addresses\" : [          (array of string) array of biblepay addresses\n"
            "        \"address\"     (string) biblepay address\n"
            "        ,...\n"
            "     ]\n"
            "  },\n"
            "  \"coinbase\" : true|false   (boolean) Coinbase or not\n"
            "}\n"

            "\nExamples:\n"
            "\nGet unspent transactions\n"
            + HelpExampleCli("listunspent", "") +
            "\nView the details\n"
            + HelpExampleCli("gettxout", "\"txid\" 1") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("gettxout", "\"txid\", 1")
        );

    LOCK(cs_main);

    UniValue ret(UniValue::VOBJ);

    std::string strHash = request.params[0].get_str();
    uint256 hash(uint256S(strHash));
    int n = request.params[1].get_int();
    COutPoint out(hash, n);
    bool fMempool = true;
    if (!request.params[2].isNull())
        fMempool = request.params[2].get_bool();

    Coin coin;
    if (fMempool) {
        LOCK(mempool.cs);
        CCoinsViewMemPool view(pcoinsTip.get(), mempool);
        if (!view.GetCoin(out, coin) || mempool.isSpent(out)) {
            return NullUniValue;
        }
    } else {
        if (!pcoinsTip->GetCoin(out, coin)) {
            return NullUniValue;
        }
    }

    BlockMap::iterator it = mapBlockIndex.find(pcoinsTip->GetBestBlock());
    CBlockIndex *pindex = it->second;
    ret.push_back(Pair("bestblock", pindex->GetBlockHash().GetHex()));
    if (coin.nHeight == MEMPOOL_HEIGHT) {
        ret.push_back(Pair("confirmations", 0));
    } else {
        ret.push_back(Pair("confirmations", (int64_t)(pindex->nHeight - coin.nHeight + 1)));
    }
    ret.push_back(Pair("value", ValueFromAmount(coin.out.nValue)));
    UniValue o(UniValue::VOBJ);
    ScriptPubKeyToUniv(coin.out.scriptPubKey, o, true);
    ret.push_back(Pair("scriptPubKey", o));
    ret.push_back(Pair("coinbase", (bool)coin.fCoinBase));

    return ret;
}

UniValue verifychain(const JSONRPCRequest& request)
{
    int nCheckLevel = gArgs.GetArg("-checklevel", DEFAULT_CHECKLEVEL);
    int nCheckDepth = gArgs.GetArg("-checkblocks", DEFAULT_CHECKBLOCKS);
    if (request.fHelp || request.params.size() > 2)
        throw std::runtime_error(
            "verifychain ( checklevel nblocks )\n"
            "\nVerifies blockchain database.\n"
            "\nArguments:\n"
            "1. checklevel   (numeric, optional, 0-4, default=" + strprintf("%d", nCheckLevel) + ") How thorough the block verification is.\n"
            "2. nblocks      (numeric, optional, default=" + strprintf("%d", nCheckDepth) + ", 0=all) The number of blocks to check.\n"
            "\nResult:\n"
            "true|false       (boolean) Verified or not\n"
            "\nExamples:\n"
            + HelpExampleCli("verifychain", "")
            + HelpExampleRpc("verifychain", "")
        );

    LOCK(cs_main);

    if (!request.params[0].isNull())
        nCheckLevel = request.params[0].get_int();
    if (!request.params[1].isNull())
        nCheckDepth = request.params[1].get_int();

    return CVerifyDB().VerifyDB(Params(), pcoinsTip.get(), nCheckLevel, nCheckDepth);
}

/** Implementation of IsSuperMajority with better feedback */
static UniValue SoftForkMajorityDesc(int version, CBlockIndex* pindex, const Consensus::Params& consensusParams)
{
    UniValue rv(UniValue::VOBJ);
    bool activated = false;
    switch(version)
    {
        case 2:
            activated = pindex->nHeight >= consensusParams.BIP34Height;
            break;
        case 3:
            activated = pindex->nHeight >= consensusParams.BIP66Height;
            break;
        case 4:
            activated = pindex->nHeight >= consensusParams.BIP65Height;
            break;
    }
    rv.push_back(Pair("status", activated));
    return rv;
}

static UniValue SoftForkDesc(const std::string &name, int version, CBlockIndex* pindex, const Consensus::Params& consensusParams)
{
    UniValue rv(UniValue::VOBJ);
    rv.push_back(Pair("id", name));
    rv.push_back(Pair("version", version));
    rv.push_back(Pair("reject", SoftForkMajorityDesc(version, pindex, consensusParams)));
    return rv;
}

static UniValue BIP9SoftForkDesc(const Consensus::Params& consensusParams, Consensus::DeploymentPos id)
{
    UniValue rv(UniValue::VOBJ);
    const ThresholdState thresholdState = VersionBitsTipState(consensusParams, id);
    switch (thresholdState) {
    case THRESHOLD_DEFINED: rv.push_back(Pair("status", "defined")); break;
    case THRESHOLD_STARTED: rv.push_back(Pair("status", "started")); break;
    case THRESHOLD_LOCKED_IN: rv.push_back(Pair("status", "locked_in")); break;
    case THRESHOLD_ACTIVE: rv.push_back(Pair("status", "active")); break;
    case THRESHOLD_FAILED: rv.push_back(Pair("status", "failed")); break;
    }
    if (THRESHOLD_STARTED == thresholdState)
    {
        rv.push_back(Pair("bit", consensusParams.vDeployments[id].bit));
    }
    rv.push_back(Pair("startTime", consensusParams.vDeployments[id].nStartTime));
    rv.push_back(Pair("timeout", consensusParams.vDeployments[id].nTimeout));
    rv.push_back(Pair("since", VersionBitsTipStateSinceHeight(consensusParams, id)));
    if (THRESHOLD_STARTED == thresholdState)
    {
        UniValue statsUV(UniValue::VOBJ);
        BIP9Stats statsStruct = VersionBitsTipStatistics(consensusParams, id);
        statsUV.push_back(Pair("period", statsStruct.period));
        statsUV.push_back(Pair("threshold", statsStruct.threshold));
        statsUV.push_back(Pair("elapsed", statsStruct.elapsed));
        statsUV.push_back(Pair("count", statsStruct.count));
        statsUV.push_back(Pair("possible", statsStruct.possible));
        rv.push_back(Pair("statistics", statsUV));
    }
    return rv;
}

void BIP9SoftForkDescPushBack(UniValue& bip9_softforks, const Consensus::Params& consensusParams, Consensus::DeploymentPos id)
{
    // Deployments with timeout value of 0 are hidden.
    // A timeout value of 0 guarantees a softfork will never be activated.
    // This is used when softfork codes are merged without specifying the deployment schedule.
    if (consensusParams.vDeployments[id].nTimeout > 0)
        bip9_softforks.push_back(Pair(VersionBitsDeploymentInfo[id].name, BIP9SoftForkDesc(consensusParams, id)));
}

UniValue getblockchaininfo(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getblockchaininfo\n"
            "Returns an object containing various state info regarding blockchain processing.\n"
            "\nResult:\n"
            "{\n"
            "  \"chain\": \"xxxx\",              (string) current network name as defined in BIP70 (main, test, regtest)\n"
            "  \"blocks\": xxxxxx,             (numeric) the current number of blocks processed in the server\n"
            "  \"headers\": xxxxxx,            (numeric) the current number of headers we have validated\n"
            "  \"bestblockhash\": \"...\",       (string) the hash of the currently best block\n"
            "  \"difficulty\": xxxxxx,         (numeric) the current difficulty\n"
            "  \"mediantime\": xxxxxx,         (numeric) median time for the current best block\n"
            "  \"verificationprogress\": xxxx, (numeric) estimate of verification progress [0..1]\n"
            "  \"initialblockdownload\": xxxx, (bool) (debug information) estimate of whether this node is in Initial Block Download mode.\n"
            "  \"chainwork\": \"xxxx\"           (string) total amount of work in active chain, in hexadecimal\n"
            "  \"size_on_disk\": xxxxxx,       (numeric) the estimated size of the block and undo files on disk\n"
            "  \"pruned\": xx,                 (boolean) if the blocks are subject to pruning\n"
            "  \"pruneheight\": xxxxxx,        (numeric) lowest-height complete block stored (only present if pruning is enabled)\n"
            "  \"automatic_pruning\": xx,      (boolean) whether automatic pruning is enabled (only present if pruning is enabled)\n"
            "  \"prune_target_size\": xxxxxx,  (numeric) the target size used by pruning (only present if automatic pruning is enabled)\n"
            "  \"softforks\": [                (array) status of softforks in progress\n"
            "     {\n"
            "        \"id\": \"xxxx\",           (string) name of softfork\n"
            "        \"version\": xx,          (numeric) block version\n"
            "        \"reject\": {             (object) progress toward rejecting pre-softfork blocks\n"
            "           \"status\": xx,        (boolean) true if threshold reached\n"
            "        },\n"
            "     }, ...\n"
            "  ],\n"
            "  \"bip9_softforks\": {           (object) status of BIP9 softforks in progress\n"
            "     \"xxxx\" : {                 (string) name of the softfork\n"
            "        \"status\": \"xxxx\",       (string) one of \"defined\", \"started\", \"locked_in\", \"active\", \"failed\"\n"
            "        \"bit\": xx,              (numeric) the bit (0-28) in the block version field used to signal this softfork (only for \"started\" status)\n"
            "        \"startTime\": xx,        (numeric) the minimum median time past of a block at which the bit gains its meaning\n"
            "        \"timeout\": xx,          (numeric) the median time past of a block at which the deployment is considered failed if not yet locked in\n"
            "        \"since\": xx,            (numeric) height of the first block to which the status applies\n"
            "        \"statistics\": {         (object) numeric statistics about BIP9 signalling for a softfork (only for \"started\" status)\n"
            "           \"period\": xx,        (numeric) the length in blocks of the BIP9 signalling period \n"
            "           \"threshold\": xx,     (numeric) the number of blocks with the version bit set required to activate the feature \n"
            "           \"elapsed\": xx,       (numeric) the number of blocks elapsed since the beginning of the current period \n"
            "           \"count\": xx,         (numeric) the number of blocks with the version bit set in the current period \n"
            "           \"possible\": xx       (boolean) returns false if there are not enough blocks left in this period to pass activation threshold \n"
            "        }\n"
            "     }\n"
            "  }\n"
            "  \"warnings\" : \"...\",           (string) any network and blockchain warnings.\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getblockchaininfo", "")
            + HelpExampleRpc("getblockchaininfo", "")
        );

    LOCK(cs_main);

    UniValue obj(UniValue::VOBJ);
    obj.push_back(Pair("chain",                 Params().NetworkIDString()));
    obj.push_back(Pair("blocks",                (int)chainActive.Height()));
    obj.push_back(Pair("headers",               pindexBestHeader ? pindexBestHeader->nHeight : -1));
    obj.push_back(Pair("bestblockhash",         chainActive.Tip()->GetBlockHash().GetHex()));
    obj.push_back(Pair("difficulty",            (double)GetDifficulty()));
    obj.push_back(Pair("mediantime",            (int64_t)chainActive.Tip()->GetMedianTimePast()));
    obj.push_back(Pair("verificationprogress",  GuessVerificationProgress(Params().TxData(), chainActive.Tip())));
    obj.push_back(Pair("initialblockdownload",  IsInitialBlockDownload()));
    obj.push_back(Pair("chainwork",             chainActive.Tip()->nChainWork.GetHex()));
    obj.push_back(Pair("size_on_disk",          CalculateCurrentUsage()));
    obj.push_back(Pair("pruned",                fPruneMode));
    if (fPruneMode) {
        CBlockIndex* block = chainActive.Tip();
        assert(block);
        while (block->pprev && (block->pprev->nStatus & BLOCK_HAVE_DATA)) {
            block = block->pprev;
        }

        obj.push_back(Pair("pruneheight",        block->nHeight));

        // if 0, execution bypasses the whole if block.
        bool automatic_pruning = (gArgs.GetArg("-prune", 0) != 1);
        obj.push_back(Pair("automatic_pruning",  automatic_pruning));
        if (automatic_pruning) {
            obj.push_back(Pair("prune_target_size",  nPruneTarget));
        }
    }

    const Consensus::Params& consensusParams = Params().GetConsensus();
    CBlockIndex* tip = chainActive.Tip();
    UniValue softforks(UniValue::VARR);
    UniValue bip9_softforks(UniValue::VOBJ);
    // sorted by activation block
    softforks.push_back(SoftForkDesc("bip34", 2, tip, consensusParams));
    softforks.push_back(SoftForkDesc("bip66", 3, tip, consensusParams));
    softforks.push_back(SoftForkDesc("bip65", 4, tip, consensusParams));
    for (int pos = Consensus::DEPLOYMENT_CSV; pos != Consensus::MAX_VERSION_BITS_DEPLOYMENTS; ++pos) {
        BIP9SoftForkDescPushBack(bip9_softforks, consensusParams, static_cast<Consensus::DeploymentPos>(pos));
    }
    obj.push_back(Pair("softforks",             softforks));
	/*
	BiblePay has csv, dip1, bip147, dip3, and dip8 heights hardcoded in the code
	In the next rebase, I think we should bitshift these switches in the block version to make us fully compatible.
    obj.push_back(Pair("bip9_softforks", bip9_softforks));
	*/

    obj.push_back(Pair("warnings", GetWarnings("statusbar")));
    return obj;
}

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

UniValue getchaintips(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 2)
        throw std::runtime_error(
            "getchaintips ( count branchlen )\n"
            "Return information about all known tips in the block tree,"
            " including the main chain as well as orphaned branches.\n"
            "\nArguments:\n"
            "1. count       (numeric, optional) only show this much of latest tips\n"
            "2. branchlen   (numeric, optional) only show tips that have equal or greater length of branch\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"height\": xxxx,             (numeric) height of the chain tip\n"
            "    \"hash\": \"xxxx\",             (string) block hash of the tip\n"
            "    \"difficulty\" : x.xxx,       (numeric) The difficulty\n"
            "    \"chainwork\" : \"0000...1f3\"  (string) Expected number of hashes required to produce the current chain (in hex)\n"
            "    \"branchlen\": 0              (numeric) zero for main chain\n"
            "    \"forkpoint\": \"xxxx\",        (string) same as \"hash\" for the main chain\n"
            "    \"status\": \"active\"          (string) \"active\" for the main chain\n"
            "  },\n"
            "  {\n"
            "    \"height\": xxxx,\n"
            "    \"hash\": \"xxxx\",\n"
            "    \"difficulty\" : x.xxx,\n"
            "    \"chainwork\" : \"0000...1f3\"\n"
            "    \"branchlen\": 1              (numeric) length of branch connecting the tip to the main chain\n"
            "    \"forkpoint\": \"xxxx\",        (string) block hash of the last common block between this tip and the main chain\n"
            "    \"status\": \"xxxx\"            (string) status of the chain (active, valid-fork, valid-headers, headers-only, invalid)\n"
            "  }\n"
            "]\n"
            "Possible values for status:\n"
            "1.  \"invalid\"               This branch contains at least one invalid block\n"
            "2.  \"headers-only\"          Not all blocks for this branch are available, but the headers are valid\n"
            "3.  \"valid-headers\"         All blocks are available for this branch, but they were never fully validated\n"
            "4.  \"valid-fork\"            This branch is not part of the active chain, but is fully validated\n"
            "5.  \"active\"                This is the tip of the active main chain, which is certainly valid\n"
            "\nExamples:\n"
            + HelpExampleCli("getchaintips", "")
            + HelpExampleRpc("getchaintips", "")
        );

    LOCK(cs_main);

    /*
     * Idea:  the set of chain tips is chainActive.tip, plus orphan blocks which do not have another orphan building off of them.
     * Algorithm:
     *  - Make one pass through mapBlockIndex, picking out the orphan blocks, and also storing a set of the orphan block's pprev pointers.
     *  - Iterate through the orphan blocks. If the block isn't pointed to by another orphan, it is a chain tip.
     *  - add chainActive.Tip()
     */
    std::set<const CBlockIndex*, CompareBlocksByHeight> setTips;
    std::set<const CBlockIndex*> setOrphans;
    std::set<const CBlockIndex*> setPrevs;

    for (const std::pair<const uint256, CBlockIndex*>& item : mapBlockIndex)
    {
        if (!chainActive.Contains(item.second)) {
            setOrphans.insert(item.second);
            setPrevs.insert(item.second->pprev);
        }
    }

    for (std::set<const CBlockIndex*>::iterator it = setOrphans.begin(); it != setOrphans.end(); ++it)
    {
        if (setPrevs.erase(*it) == 0) {
            setTips.insert(*it);
        }
    }

    // Always report the currently active tip.
    setTips.insert(chainActive.Tip());

    int nBranchMin = -1;
    int nCountMax = INT_MAX;

    if(!request.params[0].isNull())
        nCountMax = request.params[0].get_int();

    if(!request.params[1].isNull())
        nBranchMin = request.params[1].get_int();

    /* Construct the output array.  */
    UniValue res(UniValue::VARR);
    for (const CBlockIndex* block : setTips)
    {
        const CBlockIndex* pindexFork = chainActive.FindFork(block);
        const int branchLen = block->nHeight - pindexFork->nHeight;
        if(branchLen < nBranchMin) continue;

        if(nCountMax-- < 1) break;

        UniValue obj(UniValue::VOBJ);
        obj.push_back(Pair("height", block->nHeight));
        obj.push_back(Pair("hash", block->phashBlock->GetHex()));
        obj.push_back(Pair("difficulty", GetDifficulty(block)));
        obj.push_back(Pair("chainwork", block->nChainWork.GetHex()));
        obj.push_back(Pair("branchlen", branchLen));
        obj.push_back(Pair("forkpoint", pindexFork->phashBlock->GetHex()));

        std::string status;
        if (chainActive.Contains(block)) {
            // This block is part of the currently active chain.
            status = "active";
        } else if (block->nStatus & BLOCK_FAILED_MASK) {
            // This block or one of its ancestors is invalid.
            status = "invalid";
        } else if (block->nChainTx == 0) {
            // This block cannot be connected because full block data for it or one of its parents is missing.
            status = "headers-only";
        } else if (block->IsValid(BLOCK_VALID_SCRIPTS)) {
            // This block is fully validated, but no longer part of the active chain. It was probably the active block once, but was reorganized.
            status = "valid-fork";
        } else if (block->IsValid(BLOCK_VALID_TREE)) {
            // The headers for this block are valid, but it has not been validated. It was probably never part of the most-work chain.
            status = "valid-headers";
        } else {
            // No clue.
            status = "unknown";
        }
        obj.push_back(Pair("status", status));

        res.push_back(obj);
    }

    return res;
}

UniValue mempoolInfoToJSON()
{
    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("size", (int64_t) mempool.size()));
    ret.push_back(Pair("bytes", (int64_t) mempool.GetTotalTxSize()));
    ret.push_back(Pair("usage", (int64_t) mempool.DynamicMemoryUsage()));
    size_t maxmempool = gArgs.GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000;
    ret.push_back(Pair("maxmempool", (int64_t) maxmempool));
    ret.push_back(Pair("mempoolminfee", ValueFromAmount(std::max(mempool.GetMinFee(maxmempool), ::minRelayTxFee).GetFeePerK())));
    ret.push_back(Pair("minrelaytxfee", ValueFromAmount(::minRelayTxFee.GetFeePerK())));
    ret.push_back(Pair("instantsendlocks", (int64_t)llmq::quorumInstantSendManager->GetInstantSendLockCount()));

    return ret;
}

UniValue getmempoolinfo(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getmempoolinfo\n"
            "\nReturns details on the active state of the TX memory pool.\n"
            "\nResult:\n"
            "{\n"
            "  \"size\": xxxxx,               (numeric) Current tx count\n"
            "  \"bytes\": xxxxx,              (numeric) Sum of all tx sizes\n"
            "  \"usage\": xxxxx,              (numeric) Total memory usage for the mempool\n"
            "  \"maxmempool\": xxxxx,         (numeric) Maximum memory usage for the mempool\n"
            "  \"mempoolminfee\": xxxxx       (numeric) Minimum fee rate in " + CURRENCY_UNIT + "/kB for tx to be accepted. Is the maximum of minrelaytxfee and minimum mempool fee\n"
            "  \"minrelaytxfee\": xxxxx       (numeric) Current minimum relay fee for transactions\n"
            "  \"instantsendlocks\": xxxxx,   (numeric) Number of unconfirmed instant send locks\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getmempoolinfo", "")
            + HelpExampleRpc("getmempoolinfo", "")
        );

    return mempoolInfoToJSON();
}

UniValue preciousblock(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "preciousblock \"blockhash\"\n"
            "\nTreats a block as if it were received before others with the same work.\n"
            "\nA later preciousblock call can override the effect of an earlier one.\n"
            "\nThe effects of preciousblock are not retained across restarts.\n"
            "\nArguments:\n"
            "1. \"blockhash\"   (string, required) the hash of the block to mark as precious\n"
            "\nResult:\n"
            "\nExamples:\n"
            + HelpExampleCli("preciousblock", "\"blockhash\"")
            + HelpExampleRpc("preciousblock", "\"blockhash\"")
        );

    std::string strHash = request.params[0].get_str();
    uint256 hash(uint256S(strHash));
    CBlockIndex* pblockindex;

    {
        LOCK(cs_main);
        if (mapBlockIndex.count(hash) == 0)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

        pblockindex = mapBlockIndex[hash];
    }

    CValidationState state;
    PreciousBlock(state, Params(), pblockindex);

    if (!state.IsValid()) {
        throw JSONRPCError(RPC_DATABASE_ERROR, state.GetRejectReason());
    }

    return NullUniValue;
}

UniValue invalidateblock(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "invalidateblock \"blockhash\"\n"
            "\nPermanently marks a block as invalid, as if it violated a consensus rule.\n"
            "\nArguments:\n"
            "1. \"blockhash\"   (string, required) the hash of the block to mark as invalid\n"
            "\nResult:\n"
            "\nExamples:\n"
            + HelpExampleCli("invalidateblock", "\"blockhash\"")
            + HelpExampleRpc("invalidateblock", "\"blockhash\"")
        );

    std::string strHash = request.params[0].get_str();
    uint256 hash(uint256S(strHash));
    CValidationState state;

    {
        LOCK(cs_main);
        if (mapBlockIndex.count(hash) == 0)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

        CBlockIndex* pblockindex = mapBlockIndex[hash];
        InvalidateBlock(state, Params(), pblockindex);
    }

    if (state.IsValid()) {
        ActivateBestChain(state, Params());
    }

    if (!state.IsValid()) {
        throw JSONRPCError(RPC_DATABASE_ERROR, state.GetRejectReason());
    }

    return NullUniValue;
}

UniValue reconsiderblock(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "reconsiderblock \"blockhash\"\n"
            "\nRemoves invalidity status of a block and its descendants, reconsider them for activation.\n"
            "This can be used to undo the effects of invalidateblock.\n"
            "\nArguments:\n"
            "1. \"blockhash\"   (string, required) the hash of the block to reconsider\n"
            "\nResult:\n"
            "\nExamples:\n"
            + HelpExampleCli("reconsiderblock", "\"blockhash\"")
            + HelpExampleRpc("reconsiderblock", "\"blockhash\"")
        );

    std::string strHash = request.params[0].get_str();
    uint256 hash(uint256S(strHash));

    {
        LOCK(cs_main);
        if (mapBlockIndex.count(hash) == 0)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

        CBlockIndex* pblockindex = mapBlockIndex[hash];
        ResetBlockFailureFlags(pblockindex);
    }

    CValidationState state;
    ActivateBestChain(state, Params());

    if (!state.IsValid()) {
        throw JSONRPCError(RPC_DATABASE_ERROR, state.GetRejectReason());
    }

    return NullUniValue;
}

UniValue getchaintxstats(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 2)
        throw std::runtime_error(
            "getchaintxstats ( nblocks blockhash )\n"
            "\nCompute statistics about the total number and rate of transactions in the chain.\n"
            "\nArguments:\n"
            "1. nblocks      (numeric, optional) Size of the window in number of blocks (default: one month).\n"
            "2. \"blockhash\"  (string, optional) The hash of the block that ends the window.\n"
            "\nResult:\n"
            "{\n"
            "  \"time\": xxxxx,                (numeric) The timestamp for the final block in the window in UNIX format.\n"
            "  \"txcount\": xxxxx,             (numeric) The total number of transactions in the chain up to that point.\n"
            "  \"window_block_count\": xxxxx,  (numeric) Size of the window in number of blocks.\n"
            "  \"window_tx_count\": xxxxx,     (numeric) The number of transactions in the window. Only returned if \"window_block_count\" is > 0.\n"
            "  \"window_interval\": xxxxx,     (numeric) The elapsed time in the window in seconds. Only returned if \"window_block_count\" is > 0.\n"
            "  \"txrate\": x.xx,               (numeric) The average rate of transactions per second in the window. Only returned if \"window_interval\" is > 0.\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getchaintxstats", "")
            + HelpExampleRpc("getchaintxstats", "2016")
        );

    const CBlockIndex* pindex;
    int blockcount = 30 * 24 * 60 * 60 / Params().GetConsensus().nPowTargetSpacing; // By default: 1 month

    bool havehash = !request.params[1].isNull();
    uint256 hash;
    if (havehash) {
        hash = uint256S(request.params[1].get_str());
    }

    {
        LOCK(cs_main);
        if (havehash) {
            auto it = mapBlockIndex.find(hash);
            if (it == mapBlockIndex.end()) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
            }
            pindex = it->second;
            if (!chainActive.Contains(pindex)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Block is not in main chain");
            }
        } else {
            pindex = chainActive.Tip();
        }
    }

    assert(pindex != nullptr);

    if (request.params[0].isNull()) {
        blockcount = std::max(0, std::min(blockcount, pindex->nHeight - 1));
    } else {
        blockcount = request.params[0].get_int();

        if (blockcount < 0 || (blockcount > 0 && blockcount >= pindex->nHeight)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid block count: should be between 0 and the block's height - 1");
        }
    }

    const CBlockIndex* pindexPast = pindex->GetAncestor(pindex->nHeight - blockcount);
    int nTimeDiff = pindex->GetMedianTimePast() - pindexPast->GetMedianTimePast();
    int nTxDiff = pindex->nChainTx - pindexPast->nChainTx;

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("time", (int64_t)pindex->nTime));
    ret.push_back(Pair("txcount", (int64_t)pindex->nChainTx));
    ret.push_back(Pair("window_block_count", blockcount));
    if (blockcount > 0) {
        ret.push_back(Pair("window_tx_count", nTxDiff));
        ret.push_back(Pair("window_interval", nTimeDiff));
        if (nTimeDiff > 0) {
            ret.push_back(Pair("txrate", ((double)nTxDiff) / nTimeDiff));
        }
    }

    return ret;
}

template<typename T>
static T CalculateTruncatedMedian(std::vector<T>& scores)
{
    size_t size = scores.size();
    if (size == 0) {
        return 0;
    }

    std::sort(scores.begin(), scores.end());
    if (size % 2 == 0) {
        return (scores[size / 2 - 1] + scores[size / 2]) / 2;
    } else {
        return scores[size / 2];
    }
}

template<typename T>
static inline bool SetHasKeys(const std::set<T>& set) {return false;}
template<typename T, typename Tk, typename... Args>
static inline bool SetHasKeys(const std::set<T>& set, const Tk& key, const Args&... args)
{
    return (set.count(key) != 0) || SetHasKeys(set, args...);
}

// outpoint (needed for the utxo index) + nHeight + fCoinBase
static constexpr size_t PER_UTXO_OVERHEAD = sizeof(COutPoint) + sizeof(uint32_t) + sizeof(bool);

static UniValue getblockstats(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 4) {
        throw std::runtime_error(
            "getblockstats hash_or_height ( stats )\n"
            "\nCompute per block statistics for a given window. All amounts are in duffs.\n"
            "It won't work for some heights with pruning.\n"
            "It won't work without -txindex for utxo_size_inc, *fee or *feerate stats.\n"
            "\nArguments:\n"
            "1. \"hash_or_height\"     (string or numeric, required) The block hash or height of the target block\n"
            "2. \"stats\"              (array,  optional) Values to plot, by default all values (see result below)\n"
            "    [\n"
            "      \"height\",         (string, optional) Selected statistic\n"
            "      \"time\",           (string, optional) Selected statistic\n"
            "      ,...\n"
            "    ]\n"
            "\nResult:\n"
            "{                           (json object)\n"
            "  \"avgfee\": xxxxx,          (numeric) Average fee in the block\n"
            "  \"avgfeerate\": xxxxx,      (numeric) Average feerate (in duffs per byte)\n"
            "  \"avgtxsize\": xxxxx,       (numeric) Average transaction size\n"
            "  \"blockhash\": xxxxx,       (string) The block hash (to check for potential reorgs)\n"
            "  \"height\": xxxxx,          (numeric) The height of the block\n"
            "  \"ins\": xxxxx,             (numeric) The number of inputs (excluding coinbase)\n"
            "  \"maxfee\": xxxxx,          (numeric) Maximum fee in the block\n"
            "  \"maxfeerate\": xxxxx,      (numeric) Maximum feerate (in duffs per byte)\n"
            "  \"maxtxsize\": xxxxx,       (numeric) Maximum transaction size\n"
            "  \"medianfee\": xxxxx,       (numeric) Truncated median fee in the block\n"
            "  \"medianfeerate\": xxxxx,   (numeric) Truncated median feerate (in duffs per byte)\n"
            "  \"mediantime\": xxxxx,      (numeric) The block median time past\n"
            "  \"mediantxsize\": xxxxx,    (numeric) Truncated median transaction size\n"
            "  \"minfee\": xxxxx,          (numeric) Minimum fee in the block\n"
            "  \"minfeerate\": xxxxx,      (numeric) Minimum feerate (in duffs per byte)\n"
            "  \"mintxsize\": xxxxx,       (numeric) Minimum transaction size\n"
            "  \"outs\": xxxxx,            (numeric) The number of outputs\n"
            "  \"subsidy\": xxxxx,         (numeric) The block subsidy\n"
            "  \"time\": xxxxx,            (numeric) The block time\n"
            "  \"total_out\": xxxxx,       (numeric) Total amount in all outputs (excluding coinbase and thus reward [ie subsidy + totalfee])\n"
            "  \"total_size\": xxxxx,      (numeric) Total size of all non-coinbase transactions\n"
            "  \"totalfee\": xxxxx,        (numeric) The fee total\n"
            "  \"txs\": xxxxx,             (numeric) The number of transactions (excluding coinbase)\n"
            "  \"utxo_increase\": xxxxx,   (numeric) The increase/decrease in the number of unspent outputs\n"
            "  \"utxo_size_inc\": xxxxx,   (numeric) The increase/decrease in size for the utxo index (not discounting op_return and similar)\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getblockstats", "1000 '[\"minfeerate\",\"avgfeerate\"]'")
            + HelpExampleRpc("getblockstats", "1000 '[\"minfeerate\",\"avgfeerate\"]'")
        );
    }

    LOCK(cs_main);

    CBlockIndex* pindex;
    if (request.params[0].isNum()) {
        const int height = request.params[0].get_int();
        const int current_tip = chainActive.Height();
        if (height < 0) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Target block height %d is negative", height));
        }
        if (height > current_tip) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Target block height %d after current tip %d", height, current_tip));
        }

        pindex = chainActive[height];
    } else {
        const uint256 hash = ParseHashV(request.params[0], "parameter 1");
        if (mapBlockIndex.count(hash) == 0)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
        pindex = mapBlockIndex[hash];
        // pindex = LookupBlockIndex(hash);
        // if (!pindex) {
        //     throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
        // }
        if (!chainActive.Contains(pindex)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Block is not in chain %s", Params().NetworkIDString()));
        }
    }

    assert(pindex != nullptr);

    std::set<std::string> stats;
    if (!request.params[1].isNull()) {
        const UniValue stats_univalue = request.params[1].get_array();
        for (unsigned int i = 0; i < stats_univalue.size(); i++) {
            const std::string stat = stats_univalue[i].get_str();
            stats.insert(stat);
        }
    }

    const CBlock block = GetBlockChecked(pindex);

    const bool do_all = stats.size() == 0; // Calculate everything if nothing selected (default)
    const bool do_mediantxsize = do_all || stats.count("mediantxsize") != 0;
    const bool do_medianfee = do_all || stats.count("medianfee") != 0;
    const bool do_medianfeerate = do_all || stats.count("medianfeerate") != 0;
    const bool loop_inputs = do_all || do_medianfee || do_medianfeerate ||
        SetHasKeys(stats, "utxo_size_inc", "totalfee", "avgfee", "avgfeerate", "minfee", "maxfee", "minfeerate", "maxfeerate");
    const bool loop_outputs = do_all || loop_inputs || stats.count("total_out");
    const bool do_calculate_size = do_all || do_mediantxsize ||
        SetHasKeys(stats, "total_size", "avgtxsize", "mintxsize", "maxtxsize", "avgfeerate", "medianfeerate", "minfeerate", "maxfeerate");

    CAmount maxfee = 0;
    CAmount maxfeerate = 0;
    CAmount minfee = MAX_MONEY;
    CAmount minfeerate = MAX_MONEY;
    CAmount total_out = 0;
    CAmount totalfee = 0;
    int64_t inputs = 0;
    int64_t maxtxsize = 0;
    int64_t mintxsize = MaxBlockSize(true);
    int64_t outputs = 0;
    int64_t total_size = 0;
    int64_t utxo_size_inc = 0;
    std::vector<CAmount> fee_array;
    std::vector<CAmount> feerate_array;
    std::vector<int64_t> txsize_array;

    for (const auto& tx : block.vtx) {
        outputs += tx->vout.size();

        CAmount tx_total_out = 0;
        if (loop_outputs) {
            for (const CTxOut& out : tx->vout) {
                tx_total_out += out.nValue;
                utxo_size_inc += GetSerializeSize(out, SER_NETWORK, PROTOCOL_VERSION) + PER_UTXO_OVERHEAD;
            }
        }

        if (tx->IsCoinBase()) {
            continue;
        }

        inputs += tx->vin.size(); // Don't count coinbase's fake input
        total_out += tx_total_out; // Don't count coinbase reward

        int64_t tx_size = 0;
        if (do_calculate_size) {

            tx_size = tx->GetTotalSize();
            if (do_mediantxsize) {
                txsize_array.push_back(tx_size);
            }
            maxtxsize = std::max(maxtxsize, tx_size);
            mintxsize = std::min(mintxsize, tx_size);
            total_size += tx_size;
        }

        if (loop_inputs) {

            if (!fTxIndex) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "One or more of the selected stats requires -txindex enabled");
            }
            CAmount tx_total_in = 0;
            for (const CTxIn& in : tx->vin) {
                CTransactionRef tx_in;
                uint256 hashBlock;
                if (!GetTransaction(in.prevout.hash, tx_in, Params().GetConsensus(), hashBlock, false)) {
                    throw JSONRPCError(RPC_INTERNAL_ERROR, std::string("Unexpected internal error (tx index seems corrupt)"));
                }

                CTxOut prevoutput = tx_in->vout[in.prevout.n];

                tx_total_in += prevoutput.nValue;
                utxo_size_inc -= GetSerializeSize(prevoutput, SER_NETWORK, PROTOCOL_VERSION) + PER_UTXO_OVERHEAD;
            }

            CAmount txfee = tx_total_in - tx_total_out;
            assert(MoneyRange(txfee));
            if (do_medianfee) {
                fee_array.push_back(txfee);
            }
            maxfee = std::max(maxfee, txfee);
            minfee = std::min(minfee, txfee);
            totalfee += txfee;

            CAmount feerate = tx_size ? txfee / tx_size : 0;
            if (do_medianfeerate) {
                feerate_array.push_back(feerate);
            }
            maxfeerate = std::max(maxfeerate, feerate);
            minfeerate = std::min(minfeerate, feerate);
        }
    }

    UniValue ret_all(UniValue::VOBJ);
    ret_all.pushKV("avgfee", (block.vtx.size() > 1) ? totalfee / (block.vtx.size() - 1) : 0);
    ret_all.pushKV("avgfeerate", total_size ? totalfee / total_size : 0); // Unit: sat/byte
    ret_all.pushKV("avgtxsize", (block.vtx.size() > 1) ? total_size / (block.vtx.size() - 1) : 0);
    ret_all.pushKV("blockhash", pindex->GetBlockHash().GetHex());
    ret_all.pushKV("height", (int64_t)pindex->nHeight);
    ret_all.pushKV("ins", inputs);
    ret_all.pushKV("maxfee", maxfee);
    ret_all.pushKV("maxfeerate", maxfeerate);
    ret_all.pushKV("maxtxsize", maxtxsize);
    ret_all.pushKV("medianfee", CalculateTruncatedMedian(fee_array));
    ret_all.pushKV("medianfeerate", CalculateTruncatedMedian(feerate_array));
    ret_all.pushKV("mediantime", pindex->GetMedianTimePast());
    ret_all.pushKV("mediantxsize", CalculateTruncatedMedian(txsize_array));
    ret_all.pushKV("minfee", (minfee == MAX_MONEY) ? 0 : minfee);
    ret_all.pushKV("minfeerate", (minfeerate == MAX_MONEY) ? 0 : minfeerate);
    ret_all.pushKV("mintxsize", mintxsize == MaxBlockSize(true) ? 0 : mintxsize);
    ret_all.pushKV("outs", outputs);
    ret_all.pushKV("subsidy", pindex->pprev ? GetBlockSubsidy(pindex->pprev->nBits, pindex->pprev->nHeight, Params().GetConsensus()) : 50 * COIN);
    ret_all.pushKV("time", pindex->GetBlockTime());
    ret_all.pushKV("total_out", total_out);
    ret_all.pushKV("total_size", total_size);
    ret_all.pushKV("totalfee", totalfee);
    ret_all.pushKV("txs", (int64_t)block.vtx.size());
    ret_all.pushKV("utxo_increase", outputs - inputs);
    ret_all.pushKV("utxo_size_inc", utxo_size_inc);

    if (do_all) {
        return ret_all;
    }

    UniValue ret(UniValue::VOBJ);
    for (const std::string& stat : stats) {
        const UniValue& value = ret_all[stat];
        if (value.isNull()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid selected statistic %s", stat));
        }
        ret.pushKV(stat, value);
    }
    return ret;
}

std::string MultiSigSignRawTransaction(UniValue& results, std::string sHex, std::string sOriginalHex, std::string sRedeem, std::string sMultiSigKeypairKey1)
{

	if (sRedeem.empty())
	{
		JSONRPCRequest mySignedTransaction;
		mySignedTransaction.params.setArray();
		mySignedTransaction.params.push_back(sHex);
		UniValue mySignedTxOutput = signrawtransaction(mySignedTransaction);
		std::string sHex = mySignedTxOutput["hex"].getValStr();
		results.push_back(Pair("signrawtransaction_no_redeem", sHex));
		return sHex;
	}

	JSONRPCRequest myPrivKeyRequest;
	myPrivKeyRequest.params.setArray();
	myPrivKeyRequest.params.push_back(sMultiSigKeypairKey1);
	UniValue myM1PrivKey = dumpprivkey(myPrivKeyRequest);
	std::string sM1PrivKey = myM1PrivKey.getValStr();

	JSONRPCRequest myDecodeRequest;
	myDecodeRequest.params.setArray();
	myDecodeRequest.params.push_back(sOriginalHex);
	UniValue myDecodedOutput = decoderawtransaction(myDecodeRequest);
	std::string sDecodedTxId = myDecodedOutput["txid"].getValStr();
	std::string sDecodedVout = myDecodedOutput["vout"][0]["n"].getValStr();
	std::string sScriptHex = myDecodedOutput["vout"][0]["scriptPubKey"]["hex"].getValStr();
		
	JSONRPCRequest mySignedTransaction;
	std::string mySign1 = "[{\"txid\":\"" + sDecodedTxId + "\",\"vout\":" + sDecodedVout + ",\"scriptPubKey\":\"" + sScriptHex + "\",\"redeemScript\":\"" + sRedeem + "\"}]";
	// Note in step 5.1, we need to use the PRIVATE KEY for the first Multisig Public Key:
	// Because of this we need to call out and get the privkey here:
	std::string mySign2 = "[\"" + sM1PrivKey + "\"]";
	UniValue uSign1(UniValue::VOBJ);
	uSign1.read(mySign1);
	UniValue uSign2(UniValue::VOBJ);
	uSign2.read(mySign2);
	mySignedTransaction.params.setArray();
	mySignedTransaction.params.push_back(sHex);
	mySignedTransaction.params.push_back(uSign1);
	mySignedTransaction.params.push_back(uSign2);
	results.push_back(Pair("signrawtransaction_1", sHex));
	results.push_back(Pair("signrawtransaction_2", mySign1));
	results.push_back(Pair("signrawtransaction_3", mySign2));
	UniValue mySignedTxOutput = signrawtransaction(mySignedTransaction);
	std::string sSignedTxOutputHex = mySignedTxOutput["hex"].getValStr();
	results.push_back(Pair("SignedOutput_Hex", sSignedTxOutputHex));
	std::string sCommand = "     signrawtransaction " + sHex + " '" + mySign1 + "' '" + mySign2 + "'       ";
	results.push_back(Pair("command", sCommand));
	return sSignedTxOutputHex;
}


std::string MultiSigCreateRawTransaction(UniValue& results, std::string sSourceUTXO, std::string sSourceVOUT, std::string sScriptPubKeyHex, std::string sRedeemScript, std::string sDestAddress, std::string sAmount)
{
	std::string mySpend1 = "[{\"txid\":\"" + sSourceUTXO
		+ "\",\"vout\":" + sSourceVOUT + ",\"scriptPubKey\":\"" + sScriptPubKeyHex + "\",\"redeemScript\":\"" + sRedeemScript + "\"}]";
	if (sScriptPubKeyHex.empty())
	{
		std::string mySpend1 = "[{\"txid\":\"" + sSourceUTXO
			+ "\",\"vout\":" + sSourceVOUT + "}]";
	}
	std::string mySpend2 = "{\"" + sDestAddress + "\":" + sAmount + "}";
	results.push_back(Pair("MyRawSpend", mySpend1));
	results.push_back(Pair("MyRawSpendRecipient", mySpend2));
		
	JSONRPCRequest mySpendParams;
	mySpendParams.params.setArray();
		
	UniValue uTemp3(UniValue::VOBJ);
	uTemp3.read(mySpend1);
				
	mySpendParams.params.push_back(uTemp3);
	UniValue uTemp4(UniValue::VOBJ);
	uTemp4.read(mySpend2);
	mySpendParams.params.push_back(uTemp4);
		
	UniValue uvSpendOutput = createrawtransaction(mySpendParams);
	std::string sSpendHex = uvSpendOutput.getValStr();
	results.push_back(Pair("SpendTxHex", sSpendHex));
	return sSpendHex;
}

UniValue getspecialtxes(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 5)
        throw std::runtime_error(
            "getspecialtxes \"blockhash\" ( type count skip verbosity ) \n"
            "Returns an array of special transactions found in the specified block\n"
            "\nIf verbosity is 0, returns tx hash for each transaction.\n"
            "If verbosity is 1, returns hex-encoded data for each transaction.\n"
            "If verbosity is 2, returns an Object with information for each transaction.\n"
            "\nArguments:\n"
            "1. \"blockhash\"          (string, required) The block hash\n"
            "2. type                 (numeric, optional, default=-1) Filter special txes by type, -1 means all types\n"
            "3. count                (numeric, optional, default=10) The number of transactions to return\n"
            "4. skip                 (numeric, optional, default=0) The number of transactions to skip\n"
            "5. verbosity            (numeric, optional, default=0) 0 for hashes, 1 for hex-encoded data, and 2 for json object\n"
            "\nResult (for verbosity = 0):\n"
            "[\n"
            "  \"txid\" : \"xxxx\",    (string) The transaction id\n"
            "]\n"
            "\nResult (for verbosity = 1):\n"
            "[\n"
            "  \"data\",               (string) A string that is serialized, hex-encoded data for the transaction\n"
            "]\n"
            "\nResult (for verbosity = 2):\n"
            "[                       (array of Objects) The transactions in the format of the getrawtransaction RPC.\n"
            "  ...,\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("getspecialtxes", "\"00000000000fd08c2fb661d2fcb0d49abb3a91e5f27082ce64feed3b4dede2e2\"")
            + HelpExampleRpc("getspecialtxes", "\"00000000000fd08c2fb661d2fcb0d49abb3a91e5f27082ce64feed3b4dede2e2\"")
        );

    LOCK(cs_main);

    std::string strHash = request.params[0].get_str();
    uint256 hash(uint256S(strHash));

    int nTxType = -1;
    if (!request.params[1].isNull()) {
        nTxType = request.params[1].get_int();
    }

    int nCount = 10;
    if (!request.params[2].isNull()) {
        nCount = request.params[2].get_int();
        if (nCount < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative count");
    }

    int nSkip = 0;
    if (!request.params[3].isNull()) {
        nSkip = request.params[3].get_int();
        if (nSkip < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative skip");
    }

    int nVerbosity = 0;
    if (!request.params[4].isNull()) {
        nVerbosity = request.params[4].get_int();
        if (nVerbosity < 0 || nVerbosity > 2) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Verbosity must be in range 0..2");
        }
    }

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    CBlockIndex* pblockindex = mapBlockIndex[hash];
    const CBlock block = GetBlockChecked(pblockindex);

    int nTxNum = 0;
    UniValue result(UniValue::VARR);
    for(const auto& tx : block.vtx)
    {
        if (tx->nVersion != 3 || tx->nType == TRANSACTION_NORMAL // ensure it's in fact a special tx
            || (nTxType != -1 && tx->nType != nTxType)) { // ensure special tx type matches filter, if given
                continue;
        }

        nTxNum++;
        if (nTxNum <= nSkip) continue;
        if (nTxNum > nSkip + nCount) break;

        switch (nVerbosity)
        {
            case 0 : result.push_back(tx->GetHash().GetHex()); break;
            case 1 : result.push_back(EncodeHexTx(*tx)); break;
            case 2 :
                {
                    UniValue objTx(UniValue::VOBJ);
                    TxToJSON(*tx, uint256(), objTx);
                    result.push_back(objTx);
                    break;
                }
            default : throw JSONRPCError(RPC_INTERNAL_ERROR, "Unsupported verbosity");
        }
    }

    return result;
}

bool AcquireWallet2()
{
	std::vector<CWallet*> wallets = GetWallets();
	if (wallets.size() > 0)
	{
		pwalletpog = wallets[0];
		LogPrintf("\nAcquireWallets::GetWallets size=%f, acquired=1", (int)wallets.size());
		return true;
	}
	else
	{
		pwalletpog = NULL;
		LogPrintf("\nAcquireWallet::Unable to retrieve any wallet. %f", (int)3182021);
	}
	return false;
}

UniValue exec(const JSONRPCRequest& request)
{
    if (request.fHelp || (request.params.size() != 1 && request.params.size() != 2  && request.params.size() != 3 && request.params.size() != 4 
		&& request.params.size() != 5 && request.params.size() != 6 && request.params.size() != 7))
        throw std::runtime_error(
		"exec <string::itemname> <string::parameter> \r\n"
        "Executes an RPC command by name. run exec COMMAND for more info \r\n"
		"Available Commands:\r\n"
    );

    std::string sItem = request.params[0].get_str();
	if (sItem.empty()) throw std::runtime_error("Command argument invalid.");

    UniValue results(UniValue::VOBJ);
	results.push_back(Pair("Command",sItem));
	if (sItem == "subsidy")
	{
		// Used by the Pools
		if (request.params.size() != 2) 
			throw std::runtime_error("You must specify height.");
		std::string sHeight = request.params[1].get_str();
		int64_t nHeight = (int64_t)cdbl(sHeight,0);
		if (nHeight >= 1 && nHeight <= chainActive.Tip()->nHeight)
		{
			CBlockIndex* pindex = FindBlockByHeight(nHeight);
			const Consensus::Params& consensusParams = Params().GetConsensus();
			if (pindex)
			{
				CBlock block;
				if (ReadBlockFromDisk(block, pindex, consensusParams)) 
				{
        			results.push_back(Pair("subsidy", block.vtx[0]->vout[0].nValue/COIN));
					std::string sRecipient = PubKeyToAddress(block.vtx[0]->vout[0].scriptPubKey);
					results.push_back(Pair("recipient", sRecipient));
					results.push_back(Pair("blockversion", GetBlockVersion(block.vtx[0]->vout[0].sTxOutMessage)));
					results.push_back(Pair("minerguid", ExtractXML(block.vtx[0]->vout[0].sTxOutMessage,"<MINERGUID>","</MINERGUID>")));
				}
			}
		}
		else
		{
			results.push_back(Pair("error","block not found"));
		}
	}
	else if (sItem == "lockstakes")
	{
		LockUTXOStakes();
		results.push_back(Pair("lock", 1));
	}
	else if (sItem == "testmask")
	{
		CAmount nAmt = cdbl(request.params[1].get_str(), 8) * COIN;
		double nMask = cdbl(request.params[2].get_str(), 8);
		results.push_back(Pair("n1", nAmt));
		results.push_back(Pair("nMask", nMask));
		bool fCompare = CompareMask2(nAmt, nMask);
		results.push_back(Pair("cmask", fCompare));
	}
	else if (sItem == "testmem")
	{
		std::vector<DACResult> d = GetDataListVector("memorizer", 9999);
		for (int i = 0; i < d.size(); i++)
		{
			results.push_back(Pair("pk", d[i].PrimaryKey));
			std::string sBook = GetElement(d[i].PrimaryKey, "|", 0);
			int iChapter = cdbl(GetElement(d[i].PrimaryKey, "|", 1), 0);
			int iVerse = cdbl(GetElement(d[i].PrimaryKey, "|", 2), 0);
			int iStart = 0;
			int iEnd = 0;
			GetBookStartEnd(sBook, iStart, iEnd);
			std::string sVerse = GetVerseML("EN", sBook, iChapter, iVerse, iStart - 1, iEnd);
			results.push_back(Pair("v", sVerse));
		}
	}
	else if (sItem == "testic")
	{
		std::string sNN = request.params[1].get_str();
		bool h = HashExistsInDataFile("h", sNN);
		results.push_back(Pair(sNN, h));
		AppendStorageFile("h", sNN);
		h = HashExistsInDataFile("h", sNN);
		results.push_back(Pair(sNN, h));
	}
	else if (sItem == "testverse1")
	{
		std::string sRange = request.params[1].get_str();
		std::string sOutput = GetPopUpVerses(sRange);
		results.push_back(Pair("output", sOutput));
	}
	else if (sItem == "testrsacreate")
	{
		RSAKey r = GetMyRSAKey();
		results.push_back(Pair("pub", r.PublicKey));
		results.push_back(Pair("priv", r.PrivateKey));
		results.push_back(Pair("response", r.Error));
	}
	else if (sItem == "testnickname")
	{
		std::string sNN = request.params[1].get_str();
		bool fIsMine = false;
		bool fExist = NickNameExists("cpk", sNN, fIsMine);
		results.push_back(Pair("ismine", fIsMine));
	}
	else if (sItem == "testutxo")
	{
		std::string sError;
		std::string sTicker = request.params[1].get_str();
		std::string sAddress = request.params[2].get_str();
		double nTargetAmount = cdbl(request.params[3].get_str(), 8);
		int nOut = (int)cdbl(request.params[4].get_str(), 0);
		SimpleUTXO s = QueryUTXO(GetAdjustedTime(), nTargetAmount, sTicker, sAddress, "", nOut, sError);
		results.push_back(Pair("value", s.nAmount * COIN));
		results.push_back(Pair("txid", s.TXID));
		results.push_back(Pair("ordinal", s.nOrdinal));
		results.push_back(Pair("Err", sError));
	}
	else if (sItem == "testrsa")
	{
		std::string sPubPath =  GetSANDirectory2() + "pubkey.pub";
		std::string sPrivPath = GetSANDirectory2() + "privkey.priv";
		std::string sError;
		std::string sData = "1234567890-1234567890-1234567890";
		std::string sEnc = RSA_Encrypt_String(sPubPath, sData ,sError);
		results.push_back(Pair("enc", sEnc));
		std::string sDec = RSA_Decrypt_String(sPrivPath, sEnc, sError);
		results.push_back(Pair("dec", sDec));
	}
	else if (sItem == "testrsa2")
	{
		RSAKey rsa1 = GetMyRSAKey();
		RSAKey rsa2 = GetTestRSAKey();
		std::string s = "";
		for (int i = 0; i < 8192; i++)
		{
			s+= "n";
		}
		std::string sError;
		std::string sEnc = RSA_Encrypt_String_With_Key(rsa2.PublicKey, s, sError);
		results.push_back(Pair("enc", sEnc));
		std::string sPrivPath =  GetSANDirectory2() + "privkeytest.priv";

		std::string sDec = RSA_Decrypt_String(sPrivPath, sEnc, sError);
		results.push_back(Pair("dec", sDec));
		std::string sDec2 = RSA_Decrypt_String_With_Key(rsa2.PrivateKey, sEnc, sError);
		results.push_back(Pair("dec2", sDec));
	}
	else if (sItem == "setmyaddress")
	{
		std::string sData = request.params[1].get_str();
		std::vector<std::string> vD = Split(sData.c_str(), ",");
		if (vD.size() < 5)
		{
			results.push_back(Pair("Error", "Address must be:  Name,Address-Line1,City,State,Zip"));
		}
		else
		{

			std::string sName = vD[0];
			std::string sLine1 = vD[1];
			std::string sCity = vD[2];
			std::string sState = vD[3];
			std::string sZip = vD[4];
			results.push_back(Pair("Name", sName));
			results.push_back(Pair("Address-line-1", sLine1));
			results.push_back(Pair("City", sCity));
			results.push_back(Pair("State", sState));
			results.push_back(Pair("Zip", sZip));
			results.push_back(Pair("Address Set Successfully", "NOTE:  If the fields above are incorrect, please re-set your address."));
			std::string sHex = HexStr(sData.begin(), sData.end());
			WriteKey("myaddress", sHex);
		}
	}
	else if (sItem == "testnewkey")
	{
		std::string sPhrase = request.params[1].get_str();
		DACResult d = MakeDerivedKey(sPhrase);
		results.push_back(Pair("Address", d.Address));
		results.push_back(Pair("Secret", d.SecretKey));
	}
	else if (sItem == "testreadacentry")
	{
		std::string sKey1 = request.params[1].get_str();
		std::string sKey2 = request.params[2].get_str();

		DACResult d = ReadAccountingEntry(sKey1, sKey2);
		results.push_back(Pair("Amount", (double)d.nAmount/COIN));
		results.push_back(Pair("Value", d.Response));
	
	}
	else if (sItem == "testwriteacentry")
	{
		std::string sKey1 = request.params[1].get_str();
		std::string sKey2 = request.params[2].get_str();
		std::string sValue = request.params[3].get_str();
		CAmount nAmount = 5 * COIN;
		bool fResponse = WriteAccountingEntry(sKey1, sKey2, sValue, nAmount);
		results.push_back(Pair("Results", fResponse));	
	}
	else if (sItem == "testmail")
	{
		std::string sHelp = "exec testmail \"Name,Address-1,City,State,Zip\" \"Your customized recipient paragraph\" bbp_gift_amount 0=dry/1=real";

		std::string sAddress = gArgs.GetArg("-myaddress", "");
		std::vector<unsigned char> v = ParseHex(sAddress);
		std::string sData(v.begin(), v.end());
		std::vector<std::string> vD = Split(sData.c_str(), ",");
		if (vD.size() < 5)
		{
			results.push_back(Pair("Error", "From Address must be:  Name,Address-Line1,City,State,Zip.  Please set the address with exec setmyaddress."));
		}
		else
		{
			DMAddress dmFrom;
		
			dmFrom.Name = vD[0];
			dmFrom.AddressLine1 = vD[1];
			dmFrom.City = vD[2];
			dmFrom.State = vD[3];
			dmFrom.Zip = vD[4];
			results.push_back(Pair("From Name", dmFrom.Name));
			results.push_back(Pair("From Address-line-1", dmFrom.AddressLine1));
			results.push_back(Pair("From City", dmFrom.City));
			results.push_back(Pair("From State", dmFrom.State));
			results.push_back(Pair("From Zip", dmFrom.Zip));
	
			std::string sData = request.params[1].get_str();
			std::string sParagraph;
			if (request.params.size() > 2)
				sParagraph = request.params[2].get_str();
			double dAmount = 0;
			if (request.params.size() > 3)
				dAmount = cdbl(request.params[3].get_str(), 2);
			bool fDryRun = false;
			if (request.params.size() > 4)
				fDryRun = !(cdbl(request.params[4].get_str(), 0) == 1);


			CAmount nAmount = GetBBPValueUSD(dAmount);
		

			std::vector<std::string> vT = Split(sData.c_str(), ",");
			if (vT.size() < 5)
			{
				results.push_back(Pair("Error", "To Address must be:  Name,Address-Line1,City,State,Zip"));
			}
			else
			{
				DMAddress dmTo;

				dmTo.Name = vT[0];
				dmTo.AddressLine1 = vT[1];
				dmTo.City = vT[2];
				dmTo.State = vT[3];
				dmTo.Zip = vT[4];
				dmTo.Amount = dAmount * COIN;
				std::string sCode = dmTo.AddressLine1;

				results.push_back(Pair("To Name", dmTo.Name));
				results.push_back(Pair("To Address-line-1", dmTo.AddressLine1));
				results.push_back(Pair("To City", dmTo.City));
				results.push_back(Pair("To State", dmTo.State));
				results.push_back(Pair("To Zip", dmTo.Zip));
				
				if (dAmount > 0)
				{
					DACResult d = MakeDerivedKey(sCode);
					results.push_back(Pair("Gift Address", d.Address));
					results.push_back(Pair("Gift Secret", d.SecretKey));
					results.push_back(Pair("Gift Code", sCode));
					results.push_back(Pair("Gift Amount USD", dAmount));
					results.push_back(Pair("Gift Amount BBP", (double)nAmount/COIN));
					std::string sError;
					std::string sPayload = "<giftcard>" + RoundToString((double)nAmount/COIN, 2) + "</giftcard>";
	
					std::string sTXID = RPCSendMessage(nAmount, d.Address, fDryRun, sError, sPayload);
					results.push_back(Pair("Gift TXID", sTXID));
					sParagraph += "<p><br>A gift of $" + RoundToString(dAmount, 2) + " [" + RoundToString((double)nAmount/COIN, 2) 
						+ " BBP] has been sent to you!  Please use the code <span style='white-space: nowrap;'><font color=lime>\"" + sCode + "\"</font></span> to redeem your gift.<br>";
				}
	

				results.push_back(Pair("Extra Paragraph", sParagraph));
				dmTo.Paragraph = sParagraph;
				results.push_back(Pair("Gift Amount", dAmount));
				DACResult b = MailLetter(dmFrom, dmTo, true);
				std::string sError = ExtractXML(b.Response, "<error>", "</error>");
				std::string sPDF = ExtractXML(b.Response, "<pdf>", "</pdf>");
				if (sError.empty())
				{
					results.push_back(Pair("Proof", sPDF));
				}
				else
				{
					results.push_back(Pair("Error", sError));
				}
			}
		}
	}
	else if (sItem == "multisig1")
	{
		// Step 1:  Create a 1 of 2 multisig address (biblepayd createmultisig 1 N1, N2)
		std::string sM1 = DefaultRecAddress("M1");
		std::string sM2 = DefaultRecAddress("M2");

		JSONRPCRequest myMultisig;
		myMultisig.params.setArray();
		myMultisig.params.push_back(1);
		
		std::string sArray = "[\"" + sM1 + "\",\"" + sM2 + "\"]";
		results.push_back(Pair("Keypairs", sArray));

		UniValue u(UniValue::VOBJ);
		u.read(sArray);
				
		myMultisig.params.push_back(u);
		
		UniValue mySig = createmultisig(myMultisig);
		
		std::string myAddress = mySig["address"].getValStr();
		std::string myRedeem = mySig["redeemScript"].getValStr();
		results.push_back(Pair("Address", myAddress));
		results.push_back(Pair("RedeemScript", myRedeem));

		// Step 2:  Get a utxo from listunspent

		JSONRPCRequest myUTXORequest;
		myUTXORequest.params.setArray();
		UniValue myUTXOs = listunspent(myUTXORequest);
		std::string sUTXOID;
		std::string sUTXOVOUT;
		std::string sAmt;
		double dAmt = 0;
		for (int i = 0; i < myUTXOs.size(); i++)
		{
			sUTXOID = myUTXOs[i]["txid"].getValStr();
			sUTXOVOUT = myUTXOs[i]["vout"].getValStr();
			sAmt = myUTXOs[i]["amount"].getValStr();
			dAmt = cdbl(sAmt, 2);
			if (dAmt < 5)
				break;
		}


		results.push_back(Pair("UTXO-1", sUTXOID));
		results.push_back(Pair("UTXO-VOUT", sUTXOVOUT));
		results.push_back(Pair("Amount", sAmt));

		// Step 3: Create Raw Transaction that FUNDS the Multisig

		std::string sFundHex = MultiSigCreateRawTransaction(results, sUTXOID, sUTXOVOUT, "", "", myAddress, "1.07");
		
		// Step 4:  Sign the Raw Funding transaction

		std::string sFundHexSigned = MultiSigSignRawTransaction(results, sFundHex, "", "", "");

		results.push_back(Pair("Fund Hex Signed", sFundHexSigned));


		std::string sDestCPK = DefaultRecAddress("Christian-Public-Key");
		// Step 5: Grab the TXID from the sFundHexSigned
		JSONRPCRequest decodeReq;
		decodeReq.params.setArray();
		decodeReq.params.push_back(sFundHexSigned);
		UniValue myDecodedOutput = decoderawtransaction(decodeReq);
		std::string sDecodedTxId = myDecodedOutput["txid"].getValStr();
		std::string sDecodedVout = myDecodedOutput["vout"][0]["n"].getValStr();
		std::string sScriptPubKeyHex = myDecodedOutput["vout"][0]["scriptPubKey"]["hex"].getValStr();
		
		// Step 6: Create a raw transaction that SPENDS the multisig transaction into the CPK

		std::string sSpendHex = MultiSigCreateRawTransaction(results, sDecodedTxId, sDecodedVout, sScriptPubKeyHex, myRedeem,	sDestCPK, "0.50");
		
		// Step 7:  Sign the raw SPENDING transaction using the 1st keypair of the MultiSig keychain set
		
		std::string sSpentHexSigned = MultiSigSignRawTransaction(results, sSpendHex, sFundHexSigned, myRedeem, sM1);

		results.push_back(Pair("SpentHexSigned", sSpentHexSigned));


		// Step 8 : Relay the Funding transaction
		if (false)
		{
			JSONRPCRequest sendRawReq;
			sendRawReq.params.setArray();
			sendRawReq.params.push_back(sFundHexSigned);
			UniValue myRelayedFundingTxOutput = sendrawtransaction(sendRawReq);
			std::string sRelayedFundingTxOutputHex = myRelayedFundingTxOutput.getValStr();
			results.push_back(Pair("FundingTxTXID", sRelayedFundingTxOutputHex));
			// Step 9 : Relay the Spending Transaction
			JSONRPCRequest sendSpendReq;
			sendSpendReq.params.setArray();
			sendSpendReq.params.push_back(sSpentHexSigned);
			UniValue myRelayedSpendingTxOutput = sendrawtransaction(sendSpendReq);
			std::string sRelayedSpendingTxOutputHex = myRelayedSpendingTxOutput.getValStr();
			results.push_back(Pair("SpendingTX_TXID", sRelayedSpendingTxOutputHex));
		}

	}
	else if (sItem == "pinfo")
	{
		// Used by the Pools
		results.push_back(Pair("height", chainActive.Tip()->nHeight));
		int64_t nElapsed = GetAdjustedTime() - chainActive.Tip()->nTime;
		int64_t nMN = nElapsed * 256;
		if (nElapsed > (30 * 60)) 
			nMN=999999999;
		if (nMN < 512) nMN = 512;
		results.push_back(Pair("pinfo", nMN));
		results.push_back(Pair("elapsed", nElapsed));
	}
	else if (sItem == "rxpools")
	{
		std::string sPoolList = GetSporkValue("RX_POOLS_LIST");
		results.push_back(Pair("rx_pools", sPoolList));
	}
	else if (sItem == "sendalert")
	{
		/*
		// This command allows BiblePay devs to send out a network alert (or an upgrade notification etc).
		// We are able to fine tune this alert to reach only certain protocol version ranges.
		// The alert no longer puts the client in safe mode, so this is a safe process now.
		if (request.params.size() != 2) 
			throw std::runtime_error("You must specify the alert in quotes");
		std::string sAlert = request.params[1].get_str();
	    // Alerts are relayed around the network until nRelayUntil, flood filling to every node.
		// After the relay time is past, new nodes are told about alerts when they connect to peers, until either nExpiration or
		// the alert is cancelled by a newer alert.   Nodes never save alerts to disk, they are in-memory-only.
		CAlert alert;
		alert.nRelayUntil   = GetAdjustedTime() + 15 * 60;
		alert.nExpiration   = GetAdjustedTime() + 30 * 60 * 60;
		alert.nID           = 1;  // keep track of alert IDs somewhere
		alert.nCancel       = 0;   // cancels previous messages up to this ID number
		// These versions are protocol versions
		alert.nMinVer       = 70000;
		alert.nMaxVer       = 70755;
		//  1000 for Misc warnings like out of disk space and clock is wrong
		//  2000 for longer invalid proof-of-work chain
		//  Higher numbers mean higher priority
		alert.nPriority     = 5000;
		alert.strComment    = "";
		alert.strStatusBar  = sAlert;
		// Set specific client version/versions here. If setSubVer is empty, no filtering on subver is done:
		// alert.setSubVer.insert(std::string("/Core:0.12.0.58/"));
		// Sign
		if(!alert.Sign())
			throw std::runtime_error("Unable to sign.");
		CDataStream sBuffer(SER_NETWORK, CLIENT_VERSION);
		sBuffer << alert;
		CAlert alert2;
		sBuffer >> alert2;
		if (!alert2.CheckSignature())
			throw std::runtime_error("CheckSignature failed");
		assert(alert2.vchMsg == alert.vchMsg);
		assert(alert2.vchSig == alert.vchSig);
		alert.SetNull();
		results.push_back(Pair("hash", alert2.GetHash().ToString()));
		results.push_back(Pair("msg", HexStr(alert2.vchMsg)));
		results.push_back(Pair("sig", HexStr(alert2.vchSig)));
		// Send
		int nSent = 0;
		{
			g_connman->ForEachNode([&alert2, &nSent](CNode* pnode) {
			if (alert2.RelayTo(pnode, *g_connman))
			  {
					printf("ThreadSendAlert() : Sent alert to %s\n", pnode->addr.ToString().c_str());
					nSent++;
			  }
			});
		}
	   	results.push_back(Pair("relayed", nSent));
		*/
	}
	else if (sItem == "versioncheck")
	{
		std::string sGithubVersion = GetGithubVersion();
		std::string sCurrentVersion = FormatFullVersion();
		results.push_back(Pair("Github_version", sGithubVersion));
		results.push_back(Pair("Current_version", sCurrentVersion));

	}
	else if (sItem == "sins")
	{
		std::string sEntry = "";
		int iSpecificEntry = 0;
		UniValue aDataList = GetDataList("SIN", 7, iSpecificEntry, "", sEntry);
		return aDataList;
	}
	else if (sItem == "dacengine")
	{
		std::map<std::string, Orphan> mapOrphans;
		std::map<std::string, Expense> mapExpenses;

		std::map<std::string, double> mapDAC = DACEngine(mapOrphans, mapExpenses);
		for (auto dacAllocation : mapDAC)
		{
			std::string sAllocatedCharity = dacAllocation.first;
			double nPct = dacAllocation.second;
			results.push_back(Pair("Allocated Charity", sAllocatedCharity));
			results.push_back(Pair("Percentage", nPct));
		}
		for (auto orphan : mapOrphans)
		{
			std::string sRow = "Name: " + orphan.second.Name + ", Amount: " 
				+ RoundToString(orphan.second.MonthlyAmount, 2) + ", URL: " + orphan.second.URL + ", Charity: " + orphan.second.Charity;
			results.push_back(Pair(orphan.first, sRow));
		}

		double nTotalExpenses = 0;
		double nTotalRevenue = 0;
		double nTotalRevenueBBP = 0;
		double nTotalExpensesBBP = 0;
		for (auto expense : mapExpenses)
		{
			if (expense.second.nUSDAmount > 0)
			{
				nTotalExpenses += expense.second.nUSDAmount;
				nTotalExpensesBBP += expense.second.nBBPAmount;
			}
			else
			{
				nTotalRevenue += expense.second.nUSDAmount * -1;
				nTotalRevenueBBP += expense.second.nBBPAmount * -1;
			}
		}
		results.push_back(Pair("Expense Total (USD)", nTotalExpenses));
		results.push_back(Pair("Revenue Total (USD)", nTotalRevenue));
		// Show the Monthly Totals here
	}
	else if (sItem == "testpin")
	{
		std::string sCPK = DefaultRecAddress("Christian-Public-Key");
		std::string s2 = request.params[1].get_str();
		if (s2.empty())
			s2 = sCPK;
		double nPin = AddressToPin(s2);
		results.push_back(Pair("pin", nPin));
	}
	else if (sItem == "getbestutxo")
	{
		// Minimum Stake Amount
		double nMin = cdbl(request.params[1].getValStr(), 2);
		std::string sAddress;
		CAmount nReturnAmount;
		bool fWallet = AcquireWallet2();
		std::string sUTXO = pwalletpog->GetBestUTXO(nMin * COIN, .01, sAddress, nReturnAmount);
		results.push_back(Pair("UTXO", sUTXO));
		results.push_back(Pair("Address", sAddress));
		results.push_back(Pair("Amount", (double)nReturnAmount/COIN));
	}
	else if (sItem == "listorphans")
	{
		std::string sEntry;
		int iSE = 0;
		UniValue uDL = GetDataList("XSPORK-ORPHAN", 999, iSE, "XSPORK-ORPHAN", sEntry);
		return uDL;
	}
	else if (sItem == "listcharities")
	{
		std::string sEntry;
		int iSE = 0;
		UniValue uDL = GetDataList("XSPORK-CHARITY", 999, iSE, "XSPORK-CHARITY", sEntry);
		return uDL;
	}
	else if (sItem == "readverse")
	{
		if (request.params.size() != 3 && request.params.size() != 4 && request.params.size() != 5)
			throw std::runtime_error("You must specify Book and Chapter: IE 'readverse CO2 10'.  \nOptionally you may enter the Language (EN/CN) IE 'readverse CO2 10 CN'. \nOptionally you may enter the VERSE #, IE: 'readverse CO2 10 EN 2'.  To see a list of books: run getbooks.");
		std::string sBook = request.params[1].get_str();
		int iChapter = cdbl(request.params[2].get_str(),0);
		int iVerse = 0;

		if (request.params.size() > 3)
		{
			msLanguage = request.params[3].get_str();
		}
		if (request.params.size() > 4)
			iVerse = cdbl(request.params[4].get_str(), 0);

		if (request.params.size() == 4) iVerse = cdbl(request.params[3].get_str(), 0);
		results.push_back(Pair("Book", sBook));
		results.push_back(Pair("Chapter", iChapter));
		results.push_back(Pair("Language", msLanguage));
		if (iVerse > 0) results.push_back(Pair("Verse", iVerse));
		int iStart = 0;
		int iEnd = 0;
		GetBookStartEnd(sBook, iStart, iEnd);
		for (int i = iVerse; i < BIBLE_VERSE_COUNT; i++)
		{
			std::string sVerse = GetVerseML(msLanguage, sBook, iChapter, i, iStart - 1, iEnd);
			if (iVerse > 0 && i > iVerse) break;
			if (!sVerse.empty())
			{
				std::string sKey = sBook + " " + RoundToString(iChapter, 0) + ":" + RoundToString(i, 0);
			    results.push_back(Pair(sKey, sVerse));
			}
		}
	}
	else if (sItem == "testenc")
	{
		std::string sPath = request.params[1].get_str();
		bool fTest = EncryptFile(sPath, sPath + ".enc");
		results.push_back(Pair("res", fTest));
	}
	else if (sItem == "testdec")
	{
		std::string sPath = request.params[1].get_str();
		bool fTest = DecryptFile(sPath, sPath + ".dec");
		results.push_back(Pair("res", fTest));
	}
	else if (sItem == "bipfs_list")
	{
		//BOOST_FOREACH(PAIRTYPE(std::string, IPFSTransaction) item, mapSidechainTransactions)
		for (auto item : mapSidechainTransactions)
		{
			std::string sDesc = "FileName: " + item.second.FileName + ", Fee=" + RoundToString(item.second.nFee/COIN, 4) + ", Size=" + RoundToString(item.second.nSize, 2) 
				+ ", Duration=" + RoundToString(item.second.nDuration, 0)
				+ ", Density=" + RoundToString(item.second.nDensity, 0) + ", BlockHash=" + item.second.BlockHash + ", URL=" + item.second.URL + ", Network=" + item.second.Network 
				+ ", Height=" + RoundToString(item.second.nHeight, 0);
			results.push_back(Pair(item.second.TXID, sDesc));
		}
	}
	else if (sItem == "bipfs_get")
	{
		if (request.params.size() != 4)
			throw std::runtime_error("You must specify exec bipfs_get web_path local_path 0=not_encrypted/1=encrypted.  IE: exec bipfs_get web_path local_path 0.  "
			" ( The web_path is the web URL.  The file_path the target folder location on your machine.  ");
		
		std::string sWebPath = request.params[1].get_str();
		std::string sDirPath = request.params[2].get_str();
		double nEncrypted = cdbl(request.params[3].get_str(), 0);
		bool fEncrypted = nEncrypted == 1 ? true : false;
		std::string sURL = FormatURL(sWebPath, 0); 
		std::string sPage = FormatURL(sWebPath, 1);
		DACResult d = DownloadFile(sURL, sPage, 443, 30000, sDirPath, fEncrypted);
		results.push_back(Pair("Domain", sURL));
		results.push_back(Pair("Page", sPage));
		results.push_back(Pair("Results", d.Response));
		results.push_back(Pair("Error", d.ErrorCode));
	}
	else if (sItem == "bipfs_file")
	{
		if (request.params.size() != 7)
			throw std::runtime_error("You must specify exec bipfs_file file_path webpath target_density lease_duration_in_days 0=not_encrypted/1=encrypted 0=dryrun/1=real.  IE: exec bipfs_file file_path mywebpath 1 30 0 0.  "
			" ( The file_path is the location of the file on your machine.  The web_path is the target URL of the file.  "
			" The target density is how many world regions you would like the file to be stored in (choose 1-4).  "
			" The lease duration in days is the number of days you would like the file stored for.  Dry Run=0 means we will not charge for the transaction, we will test the outcome and send you a price quote.  "
			" Dry Run=1 means to actually perform the upload and charge the transaction, and make the file live on the BiblePay IPFS network. ");

		std::string sDirPath = request.params[1].get_str();
		std::string sWebPath = request.params[2].get_str();
		int nTargetDensity = cdbl(request.params[3].get_str(), 0);
		int nDurationDays = cdbl(request.params[4].get_str(), 0);
		if (nTargetDensity < 1 || nTargetDensity > 4)
			throw std::runtime_error("Invalid density. (Must be 1-4).");
		if (nDurationDays < 1 || nDurationDays > (365*10))
			throw std::runtime_error("Invalid lease duration (must be 1-36,500 in days).");

		int nEncrypted = cdbl(request.params[5].get_str(), 0);
		bool fEncrypted = nEncrypted == 1 ? true : false;

		int nDryRun = cdbl(request.params[6].get_str(), 0);
		if (nDryRun < 0 || nDryRun > 1)
			throw std::runtime_error("Invalid dry run value (must be 0 or 1).");
		bool fDryRun = nDryRun == 0 ? true : false;
		std::string sTXID;
		if (!fDryRun)
		{
			// Persist TXID
			DACResult dDry = BIPFS_UploadFile(sDirPath, sWebPath, sTXID, nTargetDensity, nDurationDays, true, fEncrypted);
			if (dDry.nFee/COIN < 1)
				throw std::runtime_error("Unable to calculate fee. ");

			std::string sCPK = DefaultRecAddress("Christian-Public-Key");
			std::string sHash = RetrieveMd5(sDirPath);
			std::string sXML = "<bipfs>" + sHash + "</bipfs>";
			std::string sError;
			std::string sExtraPayload = "<size>" + RoundToString(dDry.nSize, 0) + "</size>";
			sTXID = SendBlockchainMessage("bipfs", sCPK, sXML, dDry.nFee/COIN, 0, sExtraPayload, sError);
			if (!sError.empty())
			{
				throw std::runtime_error("IPFS::" + sError);
			}
			if (false)
				SyncSideChain(chainActive.Tip()->nHeight);
			results.push_back(Pair("TXID", sTXID));
		}
		else
		{
			sTXID = RetrieveMd5(sDirPath);
		}

		DACResult d = BIPFS_UploadFile(sDirPath, sWebPath, sTXID, nTargetDensity, nDurationDays, fDryRun, fEncrypted);

		//BOOST_FOREACH(PAIRTYPE(std::string, IPFSTransaction) item, d.mapResponses)
		for (auto item : d.mapResponses)
		{
			std::string sDesc = "File: " + item.second.File + ", Response: " + item.second.Response + ", Fee=" + RoundToString(item.second.nFee/COIN, 4) + ", Size=" + RoundToString(d.nSize, 2) + "] [Error=" + d.ErrorCode + "]";
			results.push_back(Pair(item.second.TXID, sDesc));
			// For each Density 
			//BOOST_FOREACH(PAIRTYPE(std::string, std::string) region, item.second.mapRegions)
			for (auto region : item.second.mapRegions)
			{
				results.push_back(Pair(region.first, region.second));
			}
		}

	}
	else if (sItem == "bipfs_folder")
	{
		if (request.params.size() != 7)
			throw std::runtime_error("You must specify exec bipfs_folder file_path webpath target_density lease_duration_in_days 0=unencrypted/1=encrypted 0=dryrun/1=real.  IE: exec bipfs_folder foldername mywebpath 1 30 0 0.  "
			" ( The file_path is the location of the file on your machine.  The web_path is the target URL of the file.  "
			" The target density is how many world regions you would like the file to be stored in (choose 1-4).  "
			" The lease duration in days is the number of days you would like the file stored for.  Dry Run=0 means we will not charge for the transaction, we will test the outcome and send you a price quote.  "
			" Dry Run=1 means to actually perform the upload and charge the transaction, and make the file live on the BiblePay IPFS network. ");

		std::string sDirPath = request.params[1].get_str();
		std::string sWebPath = request.params[2].get_str();
		int nTargetDensity = cdbl(request.params[3].get_str(), 0);
		int nDurationDays = cdbl(request.params[4].get_str(), 0);
		if (nTargetDensity < 1 || nTargetDensity > 4)
			throw std::runtime_error("Invalid density. (Must be 1-4).");
		if (nDurationDays < 1 || nDurationDays > (365*10))
			throw std::runtime_error("Invalid lease duration (must be 1-36,500 in days).");

		int nEncrypted = cdbl(request.params[5].get_str(), 0);
		bool fEncrypted = nEncrypted == 1 ? true : false;

		if (nEncrypted < 0 || nEncrypted > 1)
			throw std::runtime_error("Invalid encrypted (must be 0 or 1)");

		int nDryRun = cdbl(request.params[6].get_str(), 0);
		if (nDryRun < 0 || nDryRun > 1)
			throw std::runtime_error("Invalid dry run value (must be 0 or 1).");
		bool fDryRun = nDryRun == 0 ? true : false;

		std::string sTXID;
		if (!fDryRun)
		{
			// Persist TXID
			DACResult dDry = BIPFS_UploadFolder(sDirPath, sWebPath, sTXID, nTargetDensity, nDurationDays, true, fEncrypted);
			if (dDry.nFee/COIN < 1)
				throw std::runtime_error("Unable to calculate fee. ");

			std::string sCPK = DefaultRecAddress("Christian-Public-Key");
			std::string sHash = RetrieveMd5(sDirPath);
			std::string sXML = "<bipfs>" + sHash + "</bipfs>";
			std::string sError;
			std::string sExtraPayload = "<size>" + RoundToString(dDry.nSize, 0) + "</size>";
			sTXID = SendBlockchainMessage("bipfs", sCPK, sXML, dDry.nFee/COIN, 0, sExtraPayload, sError);
			if (!sError.empty())
			{
				throw std::runtime_error("IPFS::" + sError);
			}
			// Dry run succeeded
			results.push_back(Pair("TXID", sTXID));
		}
		else
		{
			sTXID = RetrieveMd5(sDirPath);
		}

		DACResult d = BIPFS_UploadFolder(sDirPath, sWebPath, sTXID, nTargetDensity, nDurationDays, fDryRun, fEncrypted);


		//BOOST_FOREACH(PAIRTYPE(std::string, IPFSTransaction) item, d.mapResponses)
		for (auto item : d.mapResponses)
		{
			std::string sDesc = "File: " + item.second.File + ", Response: " + item.second.Response + ", Fee=" + RoundToString(item.second.nFee/COIN, 4) + ", Size=" + RoundToString(d.nSize, 2) + "] [Error=" + d.ErrorCode + "]";
			results.push_back(Pair(item.second.TXID, sDesc));
			// For each Density region
			for (auto region : item.second.mapRegions)
			{
				results.push_back(Pair(region.first, region.second));
			}
		}

		results.push_back(Pair("Total Size", d.nSize));
		results.push_back(Pair("Total Fee", (double)(d.nFee / COIN)));
		results.push_back(Pair("Results", d.Response));

	}
	else if (sItem == "tgbv")
	{
		double nv = GetBlockVersion("v1601-Harvest");
		results.push_back(Pair("version", nv));
	}
	else if (sItem == "testgscvote")
	{
		int iNextSuperblock = 0;
		int iLastSuperblock = GetLastGSCSuperblockHeight(chainActive.Tip()->nHeight, iNextSuperblock);
		std::string sContract = GetGSCContract(0, true); // As of iLastSuperblock height
		results.push_back(Pair("end_height", iLastSuperblock));
		results.push_back(Pair("contract", sContract));
		std::string sAddresses;
		std::string sAmounts;
		std::string out_qtdata;
		int iVotes = 0;
		uint256 uGovObjHash = uint256S("0x0");
		uint256 uPAMHash = GetPAMHashByContract(sContract);
		results.push_back(Pair("pam_hash", uPAMHash.GetHex()));
	
		GetGSCGovObjByHeight(iNextSuperblock, uPAMHash, iVotes, uGovObjHash, sAddresses, sAmounts, out_qtdata);
		std::string sError;
		results.push_back(Pair("govobjhash", uGovObjHash.GetHex()));
		results.push_back(Pair("Addresses", sAddresses));
		results.push_back(Pair("Amounts", sAmounts));
		results.push_back(Pair("QTData", out_qtdata));
		double dTotal = AddVector(sAmounts, "|");
		results.push_back(Pair("Total_Target_Spend", dTotal));
		if (uGovObjHash == uint256S("0x0"))
		{
			results.push_back(Pair("contract_empty_voting", 1));
			// create the contract
			std::string sQuorumTrigger = SerializeSanctuaryQuorumTrigger(iLastSuperblock, iNextSuperblock, GetAdjustedTime(), sContract);
			std::string sGobjectHash;
			SubmitGSCTrigger(sQuorumTrigger, sGobjectHash, sError);
			results.push_back(Pair("quorum_hex", sQuorumTrigger));
			// Add the contract explanation as JSON
			std::vector<unsigned char> v = ParseHex(sQuorumTrigger);
			std::string sMyQuorumTrigger(v.begin(), v.end());
			UniValue u(UniValue::VOBJ);
			u.read(sMyQuorumTrigger);
			std::string sMyJsonQuorumTrigger = u.write().c_str();
			results.push_back(Pair("quorum_json", sMyJsonQuorumTrigger));
			results.push_back(Pair("quorum_gobject_trigger_hash", sGobjectHash));
			results.push_back(Pair("quorum_error", sError));
		}
		results.push_back(Pair("gsc_protocol_version", PROTOCOL_VERSION));
		double nMinGSCProtocolVersion = GetSporkDouble("MIN_GSC_PROTO_VERSION", 0);
		bool bVersionSufficient = (PROTOCOL_VERSION >= nMinGSCProtocolVersion);
		results.push_back(Pair("min_gsc_proto_version", nMinGSCProtocolVersion));
		results.push_back(Pair("version_sufficient", bVersionSufficient));
		results.push_back(Pair("votes_for_my_contract", iVotes));
		int iRequiredVotes = GetRequiredQuorumLevel(iNextSuperblock);
		results.push_back(Pair("required_votes", iRequiredVotes));
		results.push_back(Pair("last_superblock", iLastSuperblock));
		results.push_back(Pair("next_superblock", iNextSuperblock));
		CAmount nLastLimit = CSuperblock::GetPaymentsLimit(iLastSuperblock, true);
		results.push_back(Pair("last_payments_limit", (double)nLastLimit/COIN));
		CAmount nNextLimit = CSuperblock::GetPaymentsLimit(iNextSuperblock, true);
		results.push_back(Pair("next_payments_limit", (double)nNextLimit/COIN));
		bool fOverBudget = IsOverBudget(iNextSuperblock, GetAdjustedTime(), sAmounts);
		results.push_back(Pair("overbudget", fOverBudget));
		if (fOverBudget)
			results.push_back(Pair("! CAUTION !", "Superblock exceeds budget, will be rejected."));
	
		bool fTriggered = CSuperblockManager::IsSuperblockTriggered(iNextSuperblock);
		results.push_back(Pair("next_superblock_triggered", fTriggered));

		std::string sReqPay = CSuperblockManager::GetRequiredPaymentsString(iNextSuperblock);
		results.push_back(Pair("next_superblock_req_payments", sReqPay));

		bool bRes = VoteForGSCContract(iNextSuperblock, sContract, sError);
		results.push_back(Pair("vote_result", bRes));
		results.push_back(Pair("vote_error", sError));
	}
	else if (sItem == "blocktohex")
	{
		std::string sBlockHex = request.params[1].get_str();
		CBlock block;
        if (!DecodeHexBlk(block, sBlockHex))
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block decode failed");
		CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
		ssBlock << block;
		std::string sBlockHex1 = HexStr(ssBlock.begin(), ssBlock.end());
		CTransaction txCoinbase;
		std::string sTxCoinbaseHex1 = EncodeHexTx(*block.vtx[0]);
		results.push_back(Pair("blockhex", sBlockHex1));
		results.push_back(Pair("txhex", sTxCoinbaseHex1));

	}
	else if (sItem == "hexblocktocoinbase")
	{
		/*
		if (request.params.size() != 2)
			throw std::runtime_error("You must specify the block serialization hex.");
		JSONRPCRequest myCommand;
		myCommand.params.setArray();
		myCommand.params.push_back(request.params[1].get_str());
		results = hexblocktocoinbase(myCommand);
		*/
	}
	else if (sItem == "search")
	{
		if (request.params.size() != 2 && request.params.size() != 3)
			throw std::runtime_error("You must specify type: IE 'exec search PRAYER'.  Optionally you may enter a search phrase: IE 'exec search PRAYER MOTHER'.");
		std::string sType = request.params[1].get_str();
		std::string sSearch = "";
		if (request.params.size() == 3) 
			sSearch = request.params[2].get_str();
		int iSpecificEntry = 0;
		std::string sEntry = "";
		int iDays = 30;
		UniValue aDataList = GetDataList(sType, iDays, iSpecificEntry, sSearch, sEntry);
		return aDataList;
	}
	else if (sItem == "getsporkdouble")
	{
		std::string sType = request.params[1].get_str();
		double dValue = GetSporkDouble(sType, 0);
		results.push_back(Pair(sType, dValue));
	}
	else if (sItem == "persistsporkmessage")
	{
		std::string sError = "You must specify type, key, value: IE 'exec persistsporkmessage dcccomputingprojectname rosetta'";
		if (request.params.size() != 4)
			 throw std::runtime_error(sError);
		std::string sType = request.params[1].get_str();
		std::string sPrimaryKey = request.params[2].get_str();
		std::string sValue = request.params[3].get_str();

		if (sType.empty() || sPrimaryKey.empty() || sValue.empty())
			throw std::runtime_error(sError);
		sError;
		double dFee = fProd ? 10 : 5001;
		// Allows a datastore chain value to be saved if blank (an edit)
		if (sValue == "[null]")
			sValue = "";
    	std::string sResult = SendBlockchainMessage(sType, sPrimaryKey, sValue, dFee, 1, "", sError);
		results.push_back(Pair("Sent", sValue));
		results.push_back(Pair("TXID", sResult));
		if (!sError.empty()) results.push_back(Pair("Error", sError));
	}
	else if (sItem == "cpk")
	{
		std::string sError;
		if (request.params.size() != 2 && request.params.size() != 3 && request.params.size() != 4 && request.params.size() != 5)
			throw std::runtime_error("You must specify exec cpk nickname [optional e-mail address] [optional vendortype=church/user/vendor] [optional: force=true/false].");
		std::string sNickName = request.params[1].get_str();
		bool fForce = false;
		std::string sEmail;
		std::string sVendorType;

		if (request.params.size() >= 3)
			sEmail = request.params[2].get_str();
		
		if (request.params.size() >= 4)
			sVendorType = request.params[3].get_str();

		if (request.params.size() >= 5)
			fForce = request.params[4].get_str() == "true" ? true : false;

		bool fAdv = AdvertiseChristianPublicKeypair("cpk", sNickName, sEmail, sVendorType, false, fForce, 0, "", sError);
		results.push_back(Pair("Results", fAdv));
		if (!fAdv)
			results.push_back(Pair("Error", sError));
	}
	else if (sItem == "sendmanyxml")
	{
		// Pools: Allows pools to send a multi-output tx with ease
		// Format: exec sendmanyxml from_account xml_payload comment
		bool fWallet = AcquireWallet2();

		LOCK2(cs_main, pwalletpog->cs_wallet);
		std::string strAccount = request.params[1].get_str();
		if (strAccount == "*")
			throw JSONRPCError(RPC_WALLET_INVALID_ACCOUNT_NAME, "Invalid account name");
		std::string sXML = request.params[2].get_str();
		int nMinDepth = 1;
		CWalletTx wtx;
		wtx.strFromAccount = strAccount;
		wtx.mapValue["comment"] = request.params[3].get_str();
		std::set<CScript> setAddress;
		std::vector<CRecipient> vecSend;
		CAmount totalAmount = 0;
		std::string sRecipients = ExtractXML(sXML, "<RECIPIENTS>","</RECIPIENTS>");
		std::vector<std::string> vRecips = Split(sRecipients.c_str(), "<ROW>");
		for (int i = 0; i < (int)vRecips.size(); i++)
		{
			std::string sRecip = vRecips[i];
			if (!sRecip.empty())
			{
				std::string sRecipient = ExtractXML(sRecip, "<RECIPIENT>","</RECIPIENT>");
				double dAmount = cdbl(ExtractXML(sRecip,"<AMOUNT>","</AMOUNT>"),4);
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
				      CRecipient recipient = {spkAddress, nAmount, false, fSubtractFeeFromAmount};
					  vecSend.push_back(recipient);
				}
			}
		}
		EnsureWalletIsUnlocked(pwalletpog);
		// Send
		CReserveKey keyChange(pwalletpog);
		CAmount nFeeRequired = 0;
		int nChangePosRet = -1;
		std::string strFailReason;
		bool fUseInstantSend = false;
		bool fUsePrivateSend = false;
		CValidationState state;
	    CCoinControl coinControl;
		bool fCreated = pwalletpog->CreateTransaction(vecSend, wtx, keyChange, nFeeRequired, nChangePosRet, 
			strFailReason, coinControl, true, 0);
		if (!fCreated)
			throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, strFailReason);
    
		if (!pwalletpog->CommitTransaction(wtx, keyChange, g_connman.get(), state))
			throw JSONRPCError(RPC_WALLET_ERROR, "Transaction commit failed");
		results.push_back(Pair("txid", wtx.GetHash().GetHex()));
	}
	else if (sItem == "register")
	{
		if (request.params.size() != 2)
			throw std::runtime_error("The purpose of this command is to register your nickname with BMS (the decentralized web).  This feature will not be available until December 2019.  \nYou must specify your nickname.");
		std::string sProject = "cpk-bmsuser";
		std::string sNN;
		sNN = request.params[1].get_str();
		boost::to_lower(sProject);
		std::string sError;
		bool fAdv = AdvertiseChristianPublicKeypair(sProject, "", sNN, "", false, true, 0, "", sError);
		results.push_back(Pair("Results", fAdv));
		if (!fAdv)
			results.push_back(Pair("Error", sError));
	}
	else if (sItem == "funddsql")
	{
		if (request.params.size() != 2)
			throw std::runtime_error("funddsql: Make a DSQL payment.  Usage:  funddsql amount.");
		EnsureWalletIsUnlocked(pwalletpog);
	
		CAmount nAmount = cdbl(request.params[1].get_str(), 2) * COIN;
		if (nAmount < 1)
			throw std::runtime_error("Amount must be > 0.");

		// Ensure the DSQL server knows about it
		std::string sResult = BIPFS_Payment(nAmount, "", "");
		std::string sHash = ExtractXML(sResult, "<hash>", "</hash>");
		std::string sErrorsDSQL = ExtractXML(sResult, "<ERRORS>", "</ERRORS>");
		std::string sTXID = ExtractXML(sResult, "<TXID>", "</TXID>");
		results.push_back(Pair("TXID", sTXID));
		if (!sErrorsDSQL.empty())
			results.push_back(Pair("DSQL Errors", sErrorsDSQL));
		results.push_back(Pair("DSQL Hash", sHash));
	}
	else if (sItem == "blscommand")
	{
		if (request.params.size() != 2)	
			throw std::runtime_error("You must specify blscommand masternodeprivkey masternodeblsprivkey.");	

 		std::string sMNP = request.params[1].get_str();
		std::string sMNBLSPrivKey = request.params[2].get_str();
		std::string sCommand = "masternodeblsprivkey=" + sMNBLSPrivKey;
		std::string sEnc = EncryptAES256(sCommand, sMNP);
		std::string sCPK = DefaultRecAddress("Christian-Public-Key");
		std::string sXML = "<blscommand>" + sEnc + "</blscommand>";
		std::string sError;
		std::string sResult = SendBlockchainMessage("bls", sCPK, sXML, 1, 0, "", sError);
		if (!sError.empty())
			results.push_back(Pair("Errors", sError));
		results.push_back(Pair("blsmessage", sXML));
	}
	else if (sItem == "addexpense")
	{
		std::string sType = "XSPORK-EXPENSE";
		// XSPORK-EXPENSE (TYPE) | (VALUE1) expense-id | (RECORD) (added, charity, bbpamount, usdamount)
		
		std::string sDelim = "[-]";
		std::string sExpenseID = request.params[1].get_str();
		std::string sAdded = request.params[2].get_str();
		std::string sCharity = request.params[3].get_str();
		double nBBPAmount = cdbl(request.params[4].get_str(), 2);
		double nUSDAmount = cdbl(request.params[5].get_str(), 2);
		std::string sError;
		if (sExpenseID.empty() || sAdded.empty() || sCharity.empty() || nUSDAmount == 0)
		{
			sError = "Critical information missing!";
			throw std::runtime_error(sError);
		}
		std::string sValue = sAdded + sDelim + sCharity + sDelim + RoundToString(nBBPAmount, 2) + sDelim + RoundToString(nUSDAmount, 2);

		double dFee = 1000;
		std::string sResult = SendBlockchainMessage(sType, sExpenseID, sValue, dFee, 1, "", sError);
		if (!sError.empty())
			results.push_back(Pair("Error", sError));
		results.push_back(Pair("Result", sResult));

	}
	else if (sItem == "addorphan")
	{
		std::string sType = "XSPORK-ORPHAN";
		// XSPORK-ORPHAN (TYPE) | (VALUE1) orphan-id | (RECORD) (charity, name, URL, monthly Amount)
		std::string sDelim = "[-]";
		std::string sOrphanID = request.params[1].get_str();
		std::string sCharity = request.params[2].get_str();
		std::string sOrphanName = request.params[3].get_str();
		std::string sURL = request.params[4].get_str();
		double dAmt = cdbl(request.params[5].get_str(), 2);
		std::string sError;
		if (sOrphanID.empty() || sCharity.empty() || sOrphanName.empty() || dAmt == 0)
		{
			sError = "Critical orphan information missing!";
			throw std::runtime_error(sError);
		}
		std::string sValue = sCharity + sDelim + sOrphanName + sDelim + sURL + sDelim + RoundToString(dAmt, 2);
		double dFee = 1000;
		std::string sResult = SendBlockchainMessage(sType, sOrphanID, sValue, dFee, 1, "", sError);
		if (!sError.empty())
			results.push_back(Pair("Error", sError));
		results.push_back(Pair("Result", sResult));
	}
	else if (sItem == "addcharity")
	{
		std::string sType = "XSPORK-CHARITY";
		// XSPORK-CHARITY (TYPE) | (VALUE) Charity-Name | (RECORD) (BBPAddress, URL)
		std::string sDelim = "[-]";
		std::string sCharity = request.params[1].get_str();
		std::string sAddress = request.params[2].get_str();
		std::string sURL = request.params[3].get_str();
		std::string sError;
		if (sCharity.empty() || sAddress.empty() || sURL.empty())
		{
			sError = "Critical orphan information missing!";
			throw std::runtime_error(sError);
		}
		std::string sValue = sAddress + sDelim + sURL;
		double dFee = 1000;
		std::string sResult = SendBlockchainMessage(sType, sCharity, sValue, dFee, 1, "", sError);
		if (!sError.empty())
			results.push_back(Pair("Error", sError));
		results.push_back(Pair("Result", sResult));
	}
	else if (sItem == "testaes")
	{
		std::string sPass = "abcabcabcabcabcabcabcabcabcabcab";
		std::string sEnc = EncryptAES256("test", sPass);
		std::string sDec = DecryptAES256(sEnc, sPass);
		results.push_back(Pair("Enc", sEnc));
		results.push_back(Pair("Dec", sDec));
		std::string sIV = "";
		sEnc = EncryptAES256WithIV("test", sPass, sIV);
		sDec = DecryptAES256WithIV(sEnc, sPass, sIV);
		results.push_back(Pair("EncIV", sEnc));
		results.push_back(Pair("DecIV", sDec));
		sIV = "0000000000000000";
		sEnc = EncryptAES256WithIV("test", sPass, sIV);
		sDec = DecryptAES256WithIV(sEnc, sPass, sIV);
		results.push_back(Pair("EncIV00", sEnc));
		results.push_back(Pair("DecIV00", sDec));
	}
	else if (sItem == "bankroll")
	{
		if (request.params.size() != 3)
			throw std::runtime_error("You must specify type: IE 'exec bankroll quantity denomination'.  IE exec bankroll 10 100 (creates ten bills of value 100 each).");
		double nQty = cdbl(request.params[1].get_str(), 0);
		CAmount denomination = cdbl(request.params[2].get_str(), 4) * COIN;
		std::string sError = "";
		std::string sTxId = CreateBankrollDenominations(nQty, denomination, sError);
		if (!sError.empty())
		{
			if (sError == "Signing transaction failed") 
				sError += ".  (Please ensure your wallet is unlocked).";
			results.push_back(Pair("Error", sError));
		}
		else
		{
			results.push_back(Pair("TXID", sTxId));
		}
	}
	else if (sItem == "health")
	{
		// This command pulls the best-superblock (the one with the highest votes for the next height)
		bool bImpossible = (!masternodeSync.IsSynced());
		int iNextSuperblock = 0;
		int iLastSuperblock = GetLastGSCSuperblockHeight(chainActive.Tip()->nHeight, iNextSuperblock);
		std::string sAddresses;
		std::string sAmounts;
		int iVotes = 0;
		uint256 uGovObjHash = uint256S("0x0");
		uint256 uPAMHash = uint256S("0x0");
		std::string out_qtdata;
		GetGSCGovObjByHeight(iNextSuperblock, uPAMHash, iVotes, uGovObjHash, sAddresses, sAmounts, out_qtdata);

		uint256 hPam = GetPAMHash(sAddresses, sAmounts, out_qtdata);
		results.push_back(Pair("pam_hash", hPam.GetHex()));
		std::string sContract = GetGSCContract(iLastSuperblock, true);
		uint256 hPAMHash2 = GetPAMHashByContract(sContract);
		results.push_back(Pair("pam_hash_internal", hPAMHash2.GetHex()));
		if (hPAMHash2 != hPam)
		{
			results.push_back(Pair("WARNING", "Our internal PAM hash disagrees with the network. "));
		}
		results.push_back(Pair("govobjhash", uGovObjHash.GetHex()));
		int iRequiredVotes = GetRequiredQuorumLevel(iNextSuperblock);
		results.push_back(Pair("Amounts", sAmounts));
		results.push_back(Pair("Addresses", sAddresses));
		results.push_back(Pair("votes", iVotes));
		results.push_back(Pair("required_votes", iRequiredVotes));
		results.push_back(Pair("last_superblock", iLastSuperblock));
		results.push_back(Pair("next_superblock", iNextSuperblock));
		results.push_back(Pair("qt_data", out_qtdata));
		bool fTriggered = CSuperblockManager::IsSuperblockTriggered(iNextSuperblock);
		results.push_back(Pair("next_superblock_triggered", fTriggered));
		if (bImpossible)
		{
			results.push_back(Pair("WARNING", "Running in Lite Mode or Sanctuaries are not synced."));
		}

		bool fHealthy = (!sAmounts.empty() && !sAddresses.empty() && uGovObjHash != uint256S("0x0")) || bImpossible;
		results.push_back(Pair("Healthy", fHealthy));
		bool fPassing = (iVotes >= iRequiredVotes);
	    results.push_back(Pair("GSC_Voted_In", fPassing));
	}
	else if (sItem == "watchman")
	{
		std::string sContract;
		std::string sResponse = WatchmanOnTheWall(true, sContract);
		results.push_back(Pair("Response", sResponse));
		results.push_back(Pair("Contract", sContract));
	}
	else if (sItem == "getgschashes")
	{
		int iNextSuperblock = 0;
		int iLastSuperblock = GetLastGSCSuperblockHeight(chainActive.Tip()->nHeight, iNextSuperblock);
		std::string sContract = GetGSCContract(0, true); // As of iLastSuperblock height
		results.push_back(Pair("Contract", sContract));
		uint256 hPAMHash = GetPAMHashByContract(sContract);
		std::string sData;
		GetGovObjDataByPamHash(iNextSuperblock, hPAMHash, sData);
		results.push_back(Pair("Data", sData));
	}
	else if (sItem == "masterclock")
	{
		const Consensus::Params& consensusParams = Params().GetConsensus();
		CBlockIndex* pblockindexGenesis = FindBlockByHeight(0);
		CBlock blockGenesis;
		int64_t nBlockSpacing = 60 * 7;
		if (ReadBlockFromDisk(blockGenesis, pblockindexGenesis, consensusParams))
		{
			    int64_t nEpoch = blockGenesis.GetBlockTime();
				int64_t nNow = chainActive.Tip()->GetMedianTimePast();
				int64_t nElapsed = nNow - nEpoch;
				int64_t nTargetBlockCount = nElapsed / nBlockSpacing;
				results.push_back(Pair("Elapsed Time (Seconds)", nElapsed));
				results.push_back(Pair("Actual Blocks", chainActive.Tip()->nHeight));
				results.push_back(Pair("Target Block Count", nTargetBlockCount));
				double nClockAdjustment = 1.00 - ((double)chainActive.Tip()->nHeight / (double)nTargetBlockCount + .01);
				std::string sLTNarr = nClockAdjustment > 0 ? "Slow" : "Fast";
				results.push_back(Pair("Long Term Target DGW adjustment", nClockAdjustment));
				results.push_back(Pair("Long Term Trend Narr", sLTNarr));
				CBlockIndex* pblockindexRecent = FindBlockByHeight(chainActive.Tip()->nHeight * .90);
				CBlock blockRecent;
				if (ReadBlockFromDisk(blockRecent, pblockindexRecent, consensusParams))
				{
					int64_t nBlockSpan = chainActive.Tip()->nHeight - (chainActive.Tip()->nHeight * .90);
					int64_t nTimeSpan = chainActive.Tip()->GetMedianTimePast() - blockRecent.GetBlockTime();
					int64_t nProjectedBlockCount = nTimeSpan / nBlockSpacing;
					double nRecentTrend = 1.00 - ((double)nBlockSpan / (double)nProjectedBlockCount + .01);
					std::string sNarr = nRecentTrend > 0 ? "Slow" : "Fast";
					results.push_back(Pair("Recent Trend", nRecentTrend));
					results.push_back(Pair("Recent Trend Narr", sNarr));
					double nGrandAdjustment = nClockAdjustment + nRecentTrend;
					results.push_back(Pair("Recommended Next DGW adjustment", nGrandAdjustment));
				}
		}
	}
	else if (sItem == "getdacinfo")
	{
		results.push_back(Pair("currency_name", CURRENCY_NAME));
		results.push_back(Pair("DOMAIN_NAME", DOMAIN_NAME));
		results.push_back(Pair("TICKER", CURRENCY_TICKER));
	}
	else if (sItem == "revivesanc")
	{
		// Sanctuary Revival
		// The purpose of this command is to make it easy to Revive a POSE-banned deterministic sanctuary.  (In contrast to knowing how to create and send the protx update_service command).
		std::string sExtraHelp = "NOTE:  If you do not have a deterministic.conf file, you can still revive your sanctuary this way: protx update_service proreg_txID sanctuaryIP:Port sanctuary_blsPrivateKey\n\n NOTE: You can right click on the sanctuary in the Sanctuaries Tab in QT and obtain the proreg_txID, and, you can write the IP down from the list.  You still need to find your sanctuaryBLSPrivKey.\n";

		if (request.params.size() != 2)
			throw std::runtime_error("revivesanc v1.1: You must specify exec revivesanc sanctuary_name (where the sanctuary_name matches the name in the deterministic.conf file).\n\n" + sExtraHelp);
		std::string sSearch = request.params[1].get_str();
		std::string sSanc = ScanDeterministicConfigFile(sSearch);
		if (sSanc.empty())
			throw std::runtime_error("Unable to find sanctuary " + sSearch + " in deterministic.conf file.");
		std::vector<std::string> vSanc = Split(sSanc.c_str(), " ");
		if (vSanc.size() < 9)
			throw std::runtime_error("Sanctuary entry in deterministic.conf corrupted (does not contain at least 9 parts.) Format should be: Sanctuary_Name IP:port(40000=prod,40001=testnet) BLS_Public_Key BLS_Private_Key Collateral_output_txid Collateral_output_index Pro-Registration-TxId Pro-Reg-Collateral-Address Pro-Reg-funding-sent-txid.");

		std::string sSancName = vSanc[0];
		std::string sSancIP = vSanc[1];
		std::string sBLSPrivKey = vSanc[3];
		std::string sProRegTxId = vSanc[8];

		std::string sSummary = "Creating protx update_service command for Sanctuary " + sSancName + " with IP " + sSancIP + " with origin pro-reg-txid=" + sProRegTxId;
		sSummary += "(protx update_service " + sProRegTxId + " " + sSancIP + " " + sBLSPrivKey + ").";

		LogPrintf("\nCreating ProTx_Update_service %s for Sanc [%s].\n", sSummary, sSanc);

		std::string sError;
		results.push_back(Pair("Summary", sSummary));

	    JSONRPCRequest newRequest;
		newRequest.params.setArray();

		newRequest.params.push_back("update_service");
		newRequest.params.push_back(sProRegTxId);
		newRequest.params.push_back(sSancIP);
		newRequest.params.push_back(sBLSPrivKey);
		// Fee source address
		newRequest.params.push_back("");
		std::string sCPK = DefaultRecAddress("Christian-Public-Key");
		newRequest.params.push_back(sCPK);
		
		UniValue rProReg = protx(newRequest);
		results.push_back(rProReg);
		// If we made it this far and an error was not thrown:
		results.push_back(Pair("Results", "Sent sanctuary revival pro-tx successfully.  Please wait for the sanctuary list to be updated to ensure the sanctuary is revived.  This usually takes one to fifteen minutes."));

	}
	else if (sItem == "testhy")
	{
		double nAmount = cdbl(request.params[1].get_str(), 2);
		double nP = GetHighDWURewardPercentage(nAmount);
		results.push_back(Pair("hy", nP));
	}
	else if (sItem == "upgradesanc")
	{
		if (request.params.size() != 3)
			throw std::runtime_error("You must specify exec upgradesanc sanctuary_name (where the sanctuary_name matches the name in the masternode.conf file) 0/1 (where 0=dry-run, 1=real).   NOTE:  Please be sure your masternode.conf has a carriage return after the end of every sanctuary entry (otherwise we can't parse each entry correctly). ");

		std::string sSearch = request.params[1].get_str();
		int iDryRun = cdbl(request.params[2].get_str(), 0);
		std::string sSanc = ScanSanctuaryConfigFile(sSearch);
		if (sSanc.empty())
			throw std::runtime_error("Unable to find sanctuary " + sSearch + " in masternode.conf file.");
		// Legacy Sanc (masternode.conf) data format: sanc_name, ip, mnp, collat, collat ordinal

		std::vector<std::string> vSanc = Split(sSanc.c_str(), " ");
		if (vSanc.size() < 5)
			throw std::runtime_error("Sanctuary entry in masternode.conf corrupted (does not contain 5 parts.)");

		std::string sSancName = vSanc[0];
		std::string sSancIP = vSanc[1];
		std::string sMNP = vSanc[2];
		std::string sCollateralTXID = vSanc[3];
		std::string sCollateralTXIDOrdinal = vSanc[4];
		double dColOrdinal = cdbl(sCollateralTXIDOrdinal, 0);
		if (sCollateralTXIDOrdinal.length() != 1 || dColOrdinal > 9 || sCollateralTXID.length() < 16)
		{
			throw std::runtime_error("Sanctuary entry in masternode.conf corrupted (collateral txid missing, or there is no newline at the end of the entry in the masternode.conf file.)");
		}
		std::string sSummary = "Creating protx_register command for Sanctuary " + sSancName + " with IP " + sSancIP + " with TXID " + sCollateralTXID;
		// Step 1: Fund the protx fee
		// 1a. Create the new deterministic-sanctuary reward address
		std::string sPayAddress = DefaultRecAddress(sSancName + "-d"); //d means deterministic
		CScript baPayAddress = GetScriptForDestination(DecodeDestination(sPayAddress));

		std::string sVotingAddress = DefaultRecAddress(sSancName + "-v"); //v means voting
	
		std::string sError;
		std::string sData = "<protx></protx>";  // Reserved for future use

	    CWalletTx wtx;
		bool fSubtractFee = false;
		bool fInstantSend = false;
		// 1b. We must send 1 COIN to ourself first here, as the deterministic sanctuaries future fund receiving address must be prefunded with enough funds to cover the non-financial transaction transmission below
		bool fSent = RPCSendMoney(sError, baPayAddress, 1 * COIN, fSubtractFee, wtx, fInstantSend, sData);

		if (!sError.empty() || !fSent)
			throw std::runtime_error("Unable to fund protx_register fee: " + sError);

		results.push_back(Pair("Summary", sSummary));
		// Generate BLS keypair (This is the keypair for the sanctuary - the BLS public key goes in the chain, the private key goes into the Sanctuaries .conf file like this: masternodeblsprivkey=nnnnn
		JSONRPCRequest myBLS;
		myBLS.params.setArray();
		myBLS.params.push_back("generate");
		UniValue myBLSPair = _bls(myBLS);
		std::string myBLSPublic = myBLSPair["public"].getValStr();
		std::string myBLSPrivate = myBLSPair["secret"].getValStr();
		
	    JSONRPCRequest newRequest;
		newRequest.params.setArray();
		// Pro-tx-register_prepare preparation format: protx register_prepare 1.55mm_collateralHash 1.55mm_index_collateralIndex ipv4:port_ipAndPort home_voting_address_ownerKeyAddr blsPubKey_operatorPubKey delegate_or_home_votingKeyAddr 0_pctOf_operatorReward payout_address_payoutAddress optional_(feeSourceAddress_of_Pro_tx_fee)

		/*
		            + GetHelpString(1, "collateralHash")
            + GetHelpString(2, "collateralIndex")
            + GetHelpString(3, "ipAndPort")
            + GetHelpString(4, "ownerAddress")
            + GetHelpString(5, "operatorPubKey_register")
            + GetHelpString(6, "votingAddress_register")
            + GetHelpString(7, "operatorReward")
            + GetHelpString(8, "payoutAddress_register")
            + GetHelpString(9, "feeSourceAddress") +

			*/

		newRequest.params.push_back("register_prepare");
		newRequest.params.push_back(sCollateralTXID);
		newRequest.params.push_back(sCollateralTXIDOrdinal);
		newRequest.params.push_back(sSancIP);
		
		newRequest.params.push_back(sVotingAddress);  // Home Voting Address
		newRequest.params.push_back(myBLSPublic);     // Remote Sanctuary Public Key (Private and public keypair is stored in deterministicsanctuary.conf on the controller wallet)
		newRequest.params.push_back(sVotingAddress);  // Delegates Voting address (This is a person that can vote for you if you want) - in our case its the same 

		newRequest.params.push_back("0");             // Pct of rewards to share with Operator (This is the amount of reward we want to share with a Sanc Operator - IE a hosting company)
		newRequest.params.push_back(sPayAddress);     // Rewards Pay To Address (This can be changed to be a wallet outside of your wallet, maybe a hardware wallet)
		newRequest.params.push_back(sPayAddress);

		// 1c.  First send the pro-tx-register_prepare command, and look for the tx, collateralAddress and signMessage response:
		UniValue rProReg = protx(newRequest);
		std::string sProRegTxId = rProReg["tx"].getValStr();
		std::string sProCollAddr = rProReg["collateralAddress"].getValStr();
		std::string sProSignMessage = rProReg["signMessage"].getValStr();
		if (sProSignMessage.empty() || sProRegTxId.empty())
			throw std::runtime_error("Failed to create pro reg tx.");
		// Step 2: Sign the Pro-Reg Tx
		JSONRPCRequest newSig;
		newSig.params.setArray();
		newSig.params.push_back(sProCollAddr);
		newSig.params.push_back(sProSignMessage);
		std::string sProSignature = SignMessageEvo(sProCollAddr, sProSignMessage, sError);
		if (!sError.empty())
			throw std::runtime_error("Unable to sign pro-reg-tx: " + sError);

		std::string sSentTxId;
		if (iDryRun == 1)
		{
			// Note: If this is Not a dry-run, go ahead and submit the non-financial transaction to the network here:
			JSONRPCRequest newSend;
			newSend.params.setArray();
			newSend.params.push_back("register_submit");
			newSend.params.push_back(sProRegTxId);
			newSend.params.push_back(sProSignature);
			UniValue rProReg = protx(newSend);
			results.push_back(rProReg);
			sSentTxId = rProReg.getValStr();
		}
		// Step 3: Report this info back to the user
		results.push_back(Pair("bls_public_key", myBLSPublic));
		results.push_back(Pair("bls_private_key", myBLSPrivate));
		results.push_back(Pair("pro_reg_txid", sProRegTxId));
		results.push_back(Pair("pro_reg_collateral_address", sProCollAddr));
		results.push_back(Pair("pro_reg_signed_message", sProSignMessage));
		results.push_back(Pair("pro_reg_signature", sProSignature));
		results.push_back(Pair("sent_txid", sSentTxId));
	    // Step 4: Store the new deterministic sanctuary in deterministicsanc.conf
		std::string sDSD = sSancName + " " + sSancIP + " " + myBLSPublic + " " + myBLSPrivate + " " + sCollateralTXID + " " + sCollateralTXIDOrdinal + " " + sProRegTxId + " " + sProCollAddr + " " + sSentTxId + "\n";
		if (iDryRun == 1)
			AppendSanctuaryFile("deterministic.conf", sDSD);
	}
	else if (sItem == "randomx_pool")
	{
        std::unique_lock<std::mutex> lock(cs_blockchange);
		{
			std::string sHeader = request.params[1].get_str();
			std::string sKey = request.params[2].get_str();
			std::vector<unsigned char> v = ParseHex(sHeader);
			std::vector<unsigned char> vKey = ParseHex(sKey);

			std::string sRevKey = ReverseHex(sKey);
			uint256 uKey = uint256S("0x" + sRevKey);
			uint256 uRXMined = RandomX_Hash(v, uKey, 90);

			std::vector<unsigned char> vch(160);
			CVectorWriter ss(SER_NETWORK, PROTOCOL_VERSION, vch, 0);
			ss << chainActive.Tip()->GetBlockHash() << uRXMined;
			uint256 h = HashBlake((const char *)vch.data(), (const char *)vch.data() + vch.size());

			results.push_back(Pair("RX", h.GetHex()));
			results.push_back(Pair("RX_root", uRXMined.GetHex()));

		}
	}
	else if (sItem == "pobh")
	{
		std::string sInput = request.params[1].get_str();
		double d1 = cdbl(request.params[2].get_str(), 0);
		uint256 hSource = uint256S("0x" + sInput);
		uint256 h = BibleHashDebug(hSource, d1 == 1);
		results.push_back(Pair("in-hash", hSource.GetHex()));
		results.push_back(Pair("out-hash", h.GetHex()));
	}
	else if (sItem == "randomx")
	{
		std::string sHeader = request.params[1].get_str();
		std::string sKey = request.params[2].get_str();
		std::string sRevKey = ReverseHex(sKey);
		uint256 uKey = uint256S("0x" + sRevKey);
		std::vector<unsigned char> v = ParseHex(sHeader);
		uint256 uRX3 = RandomX_Hash(v, uKey, 99);
		results.push_back(Pair("hash2", uRX3.GetHex()));
		uint256 uRX4 = HashBlake(v.begin(), v.end());
		results.push_back(Pair("hashBlakeInSz", (int)v.size()));
		results.push_back(Pair("hashBlake", uRX4.GetHex()));
	}
	else if (sItem == "analyze")
	{
		if (request.params.size() != 3)
			throw std::runtime_error("You must specify height and nickname.");
		int nHeight = cdbl(request.params[1].get_str(), 0);
		std::string sNickName = request.params[2].get_str();
		WriteCache("analysis", "user", sNickName, GetAdjustedTime());
		UniValue p = GetProminenceLevels(nHeight + BLOCKS_PER_DAY, "");
		std::string sData1 = ReadCache("analysis", "data_1");
		std::string sData2 = ReadCache("analysis", "data_2");
		results.push_back(Pair("Campaign", "Totals"));

		std::vector<std::string> v = Split(sData2.c_str(), "\n");
		for (int i = 0; i < (int)v.size(); i++)
		{
			std::string sRow = v[i];
			results.push_back(Pair(RoundToString(i, 0), sRow));
		}

		results.push_back(Pair("Campaign", "Points"));

		v = Split(sData1.c_str(), "\n");
		for (int i = 0; i < (int)v.size(); i++)
		{
			std::string sRow = v[i];
			results.push_back(Pair(RoundToString(i, 0), sRow));
		}
	}
	else if (sItem == "vectoroffiles")
	{
		std::string dirPath = "/testbed";	
		std::vector<std::string> skipList;
		std::vector<std::string> g = GetVectorOfFilesInDirectory(dirPath, skipList);
		// Iterate over the vector and print all files
		for (auto str : g)
		{
			results.push_back(Pair("File", str));
		}
	}
	else if (sItem == "votewithcoinage")
	{
		std::string sGobjectID = request.params[1].get_str();
		std::string sOutcome = request.params[2].get_str();
		std::string TXID_OUT;
		std::string ERROR_OUT;
		bool fVoted = VoteWithCoinAge(sGobjectID, sOutcome, ERROR_OUT);
		results.push_back(Pair("vote-error", ERROR_OUT));
		results.push_back(Pair("vote-result", fVoted));
		if (!TXID_OUT.empty())
		{
			double nCoinAge = GetCoinAge(TXID_OUT);
			results.push_back(Pair("vote-coin-age", nCoinAge));
		}
	}
	else if (sItem == "getgobjectvotingdata")
	{
		std::string sGobjectID = request.params[1].get_str();

		CoinAgeVotingDataStruct c = GetCoinAgeVotingData(sGobjectID);
		for (int i = 0; i < 3; i++)
		{
			results.push_back(Pair("Vote Type", i));
			for (auto myVote : c.mapsVoteCount[i])
			{
				results.push_back(Pair(myVote.first, myVote.second));
			}
			results.push_back(Pair("Total Votes Type " + RoundToString(i, 0), c.mapTotalVotes[i]));
			for (auto myAge : c.mapsVoteAge[i])
			{
				results.push_back(Pair(myAge.first, myAge.second));
			}
			results.push_back(Pair("Total Age Type " + RoundToString(i, 0), c.mapTotalCoinAge[i]));
		}
	}
	else if (sItem == "apmtest")
	{
		int iNextSuperblock = 0;
		int nHeight = cdbl(request.params[1].get_str(), 0);
		int iLastSuperblock = GetLastGSCSuperblockHeight(nHeight, iNextSuperblock);
		double dAPM = CalculateAPM(iLastSuperblock);
		double dAPM2 = CalculateAPM(iNextSuperblock);
		double dAPM3 = ExtractAPM(iLastSuperblock);

		results.push_back(Pair("As of Height", iLastSuperblock));
		results.push_back(Pair("APM", dAPM));
		results.push_back(Pair("APM_Extract", dAPM3));
		results.push_back(Pair("APM as of Next Superblock " + RoundToString(iNextSuperblock, 0), dAPM2));
	}
	else if (sItem == "poostest")
	{
		std::string sBio = request.params[1].get_str();
		bool f1 = POOSOrphanTest(sBio, 60);
		results.push_back(Pair("bio", f1));
	}
	else if (sItem == "testhttps")
	{
		std::string sURL = "https://" + GetSporkValue("bms");
		std::string sRestfulURL = "BMS/LAST_MANDATORY_VERSION";
		std::string sResponse = Uplink(false, "", sURL, sRestfulURL, SSL_PORT, 25, 1);
		results.push_back(Pair(sRestfulURL, sResponse));
	}
	else if (sItem == "sendmessage")
	{
		std::string sError = "You must specify type, key, value: IE 'exec sendmessage PRAYER mother Please_pray_for_my_mother.'";
		if (request.params.size() != 4)
			 throw std::runtime_error(sError);

		std::string sType = request.params[1].get_str();
		std::string sPrimaryKey = request.params[2].get_str();
		std::string sValue = request.params[3].get_str();
		if (sType.empty() || sPrimaryKey.empty() || sValue.empty())
			throw std::runtime_error(sError);
		std::string sResult = SendBlockchainMessage(sType, sPrimaryKey, sValue, 1, 0, "", sError);
		results.push_back(Pair("Sent", sValue));
		results.push_back(Pair("TXID", sResult));
		results.push_back(Pair("Error", sError));
	}
	else if (sItem == "getgovlimit")
	{
		const Consensus::Params& consensusParams = Params().GetConsensus();
		int nBits = 486585255;
		int nHeight = cdbl(request.params[1].get_str(), 0);
		CAmount nLimit = CSuperblock::GetPaymentsLimit(nHeight, true);
		CAmount nReward = GetBlockSubsidy(nBits, nHeight, consensusParams, false);
		CAmount nRewardGov = GetBlockSubsidy(nBits, nHeight, consensusParams, true);
		CAmount nSanc = GetMasternodePayment(nHeight, nReward);
        results.push_back(Pair("Limit", (double)nLimit/COIN));
		results.push_back(Pair("Subsidy", (double)nReward/COIN));
		results.push_back(Pair("Sanc", (double)nSanc/COIN));
		// Evo Audit: 14700 gross, @98400=13518421, @129150=13225309/Daily = @129170=1013205
		results.push_back(Pair("GovernanceSubsidy", (double)nRewardGov/COIN));
	}
	else if (sItem == "hexblocktojson")
	{
		std::string sHex = request.params[1].get_str();
		CBlock block;
        if (!DecodeHexBlk(block, sHex))
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block decode failed");
        return blockToJSON(block, NULL, true);
	}
	else if (sItem == "hextxtojson")
	{
		std::string sHex = request.params[1].get_str();
		
		CMutableTransaction tx;
        if (!DecodeHexTx(tx, request.params[0].get_str()))
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Tx decode failed");
        UniValue objTx(UniValue::VOBJ);
        TxToJSON(tx, uint256(), objTx);
        results.push_back(objTx);
	}
	else if (sItem == "hextxtojson2")
	{
		std::string sHex = request.params[1].get_str();
		CMutableTransaction tx;
        DecodeHexTx(tx, request.params[0].get_str());
        UniValue objTx(UniValue::VOBJ);
        TxToJSON(tx, uint256(), objTx);
        results.push_back(objTx);
	}
	else if (sItem == "blocktohex")
	{
		std::string sBlock = request.params[1].get_str();
		int nHeight = (int)cdbl(sBlock,0);
		if (nHeight < 0 || nHeight > chainActive.Tip()->nHeight) 
			throw std::runtime_error("Block number out of range.");
		CBlockIndex* pblockindex = FindBlockByHeight(nHeight);
		if (pblockindex==NULL)   
			throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
		CBlock block;
		const Consensus::Params& consensusParams = Params().GetConsensus();
		ReadBlockFromDisk(block, pblockindex, consensusParams);
		CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
		ssBlock << block;
		std::string sBlockHex = HexStr(ssBlock.begin(), ssBlock.end());
		CTransaction txCoinbase;
		std::string sTxCoinbaseHex = EncodeHexTx(*block.vtx[0]);
		results.push_back(Pair("blockhex", sBlockHex));
		results.push_back(Pair("txhex", sTxCoinbaseHex));
	}
	else if (sItem == "getarg")
	{
		// Allows user to display a configuration value (useful if you are not sure if you entered a config value in your file)
		std::string sArg = request.params[1].get_str();
		std::string sValue = gArgs.GetArg("-" + sArg, "");
		results.push_back(Pair("arg v2.0", sValue));
	}
	else if (sItem == "xnonce")
	{
		const Consensus::Params& consensusParams = Params().GetConsensus();
		int nHeight = consensusParams.ANTI_GPU_HEIGHT + 1;
		double dNonce = cdbl(request.params[1].get_str(), 0);
		bool fNonce =  CheckNonce(true, (int)dNonce, nHeight, 1, 301, consensusParams);
		results.push_back(Pair("result", fNonce));
	}
	else
	{
		results.push_back(Pair("Error", "Command not found"));
	}

	return results;
}




UniValue savemempool(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0) {
        throw std::runtime_error(
            "savemempool\n"
            "\nDumps the mempool to disk.\n"
            "\nExamples:\n"
            + HelpExampleCli("savemempool", "")
            + HelpExampleRpc("savemempool", "")
        );
    }

    if (!DumpMempool()) {
        throw JSONRPCError(RPC_MISC_ERROR, "Unable to dump mempool to disk");
    }

    return NullUniValue;
}

static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         argNames
  //  --------------------- ------------------------  -----------------------  ----------
    { "blockchain",         "getblockchaininfo",      &getblockchaininfo,      {} },
    { "blockchain",         "getchaintxstats",        &getchaintxstats,        {"nblocks", "blockhash"} },
    { "blockchain",         "getblockstats",          &getblockstats,          {"hash_or_height", "stats"} },
    { "blockchain",         "getbestblockhash",       &getbestblockhash,       {} },
    { "blockchain",         "getbestchainlock",       &getbestchainlock,       {} },
    { "blockchain",         "getblockcount",          &getblockcount,          {} },
    { "blockchain",         "getblock",               &getblock,               {"blockhash","verbosity|verbose"} },
    { "blockchain",         "getblockhashes",         &getblockhashes,         {"high","low"} },
    { "blockchain",         "getblockhash",           &getblockhash,           {"height"} },
    { "blockchain",         "getblockheader",         &getblockheader,         {"blockhash","verbose"} },
    { "blockchain",         "getblockheaders",        &getblockheaders,        {"blockhash","count","verbose"} },
    { "blockchain",         "getmerkleblocks",        &getmerkleblocks,        {"filter","blockhash","count"} },
    { "blockchain",         "getchaintips",           &getchaintips,           {"count","branchlen"} },
    { "blockchain",         "getdifficulty",          &getdifficulty,          {} },
    { "blockchain",         "getmempoolancestors",    &getmempoolancestors,    {"txid","verbose"} },
    { "blockchain",         "getmempooldescendants",  &getmempooldescendants,  {"txid","verbose"} },
    { "blockchain",         "getmempoolentry",        &getmempoolentry,        {"txid"} },
    { "blockchain",         "getmempoolinfo",         &getmempoolinfo,         {} },
    { "blockchain",         "getrawmempool",          &getrawmempool,          {"verbose"} },
    { "blockchain",         "getspecialtxes",         &getspecialtxes,         {"blockhash", "type", "count", "skip", "verbosity"} },
    { "blockchain",         "gettxout",               &gettxout,               {"txid","n","include_mempool"} },
    { "blockchain",         "gettxoutsetinfo",        &gettxoutsetinfo,        {} },
    { "blockchain",         "pruneblockchain",        &pruneblockchain,        {"height"} },
    { "blockchain",         "savemempool",            &savemempool,            {} },
    { "blockchain",         "verifychain",            &verifychain,            {"checklevel","nblocks"} },
    { "blockchain",         "preciousblock",          &preciousblock,          {"blockhash"} },

    /* Not shown in help */
    { "hidden",             "invalidateblock",        &invalidateblock,        {"blockhash"} },
    { "hidden",             "reconsiderblock",        &reconsiderblock,        {"blockhash"} },
    { "hidden",             "waitfornewblock",        &waitfornewblock,        {"timeout"} },
	{ "hidden",             "exec",				      &exec,                   {"1","2","3","4","5","6","7"} },
    { "hidden",             "waitforblock",           &waitforblock,           {"blockhash","timeout"} },
    { "hidden",             "waitforblockheight",     &waitforblockheight,     {"height","timeout"} },
};

void RegisterBlockchainRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}


uint256 Sha256001(int nType, int nVersion, std::string data)
{
    CHash256 ctx;
	unsigned char *val = new unsigned char[data.length()+1];
	strcpy((char *)val, data.c_str());
	ctx.Write(val, data.length()+1);
    uint256 result;
	ctx.Finalize((unsigned char*)&result);
    return result;
}


		