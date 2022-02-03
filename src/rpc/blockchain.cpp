// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2014-2021 The DÃSH Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <rpc/blockchain.h>

#include <amount.h>
#include <chainparams.h>
#include <checkpoints.h>
#include <coins.h>
#include <node/coinstats.h>
#include <core_io.h>
#include <consensus/validation.h>
#include <validation.h>
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
#include <validationinterface.h>
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
#include "governance/governance-classes.h"
#include "randomx_bbp.h"


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

/* Calculate the difficulty for a given block index.
 */
double GetDifficulty(const CBlockIndex* blockindex)
{
    if (blockindex == nullptr)
    {
        return 1.0;
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

UniValue blockheaderToJSON(const CBlockIndex* blockindex)
{
    AssertLockHeld(cs_main);
    UniValue result(UniValue::VOBJ);
    result.pushKV("hash", blockindex->GetBlockHash().GetHex());
    int confirmations = -1;
    // Only report confirmations if the block is on the main chain
    if (chainActive.Contains(blockindex))
        confirmations = chainActive.Height() - blockindex->nHeight + 1;
    result.pushKV("confirmations", confirmations);
    result.pushKV("height", blockindex->nHeight);
    result.pushKV("version", blockindex->nVersion);
    result.pushKV("versionHex", strprintf("%08x", blockindex->nVersion));
    result.pushKV("merkleroot", blockindex->hashMerkleRoot.GetHex());
    result.pushKV("time", (int64_t)blockindex->nTime);
    result.pushKV("mediantime", (int64_t)blockindex->GetMedianTimePast());
    result.pushKV("nonce", (uint64_t)blockindex->nNonce);
    result.pushKV("bits", strprintf("%08x", blockindex->nBits));
    result.pushKV("difficulty", GetDifficulty(blockindex));
    result.pushKV("chainwork", blockindex->nChainWork.GetHex());
    result.pushKV("nTx", (uint64_t)blockindex->nTx);
	// Biblepay - RX
	result.pushKV("randomxdata", blockindex->RandomXData);
	result.pushKV("randomxkey", blockindex->RandomXKey.GetHex());

    if (blockindex->pprev)
        result.pushKV("previousblockhash", blockindex->pprev->GetBlockHash().GetHex());
    CBlockIndex *pnext = chainActive.Next(blockindex);
    if (pnext)
        result.pushKV("nextblockhash", pnext->GetBlockHash().GetHex());

    result.pushKV("chainlock", llmq::chainLocksHandler->HasChainLock(blockindex->nHeight, blockindex->GetBlockHash()));

    return result;
}

UniValue blockToJSON(const CBlock& block, const CBlockIndex* blockindex, bool txDetails)
{
    AssertLockHeld(cs_main);
    UniValue result(UniValue::VOBJ);
    result.pushKV("hash", blockindex->GetBlockHash().GetHex());
    int confirmations = -1;
    // Only report confirmations if the block is on the main chain
    if (chainActive.Contains(blockindex))
        confirmations = chainActive.Height() - blockindex->nHeight + 1;
    result.pushKV("confirmations", confirmations);
    result.pushKV("size", (int)::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION));
    result.pushKV("height", blockindex->nHeight);
    result.pushKV("version", block.nVersion);
    result.pushKV("versionHex", strprintf("%08x", block.nVersion));
    result.pushKV("merkleroot", block.hashMerkleRoot.GetHex());
    bool chainLock = llmq::chainLocksHandler->HasChainLock(blockindex->nHeight, blockindex->GetBlockHash());
    UniValue txs(UniValue::VARR);
    for(const auto& tx : block.vtx)
    {
        if(txDetails)
        {
            UniValue objTx(UniValue::VOBJ);
            TxToUniv(*tx, uint256(), objTx, true);
            bool fLocked = llmq::quorumInstantSendManager->IsLocked(tx->GetHash());
            objTx.pushKV("instantlock", fLocked || chainLock);
            objTx.pushKV("instantlock_internal", fLocked);
            txs.push_back(objTx);
        }
        else
            txs.push_back(tx->GetHash().GetHex());
    }
    result.pushKV("tx", txs);
    if (!block.vtx[0]->vExtraPayload.empty()) {
        CCbTx cbTx;
        if (GetTxPayload(block.vtx[0]->vExtraPayload, cbTx)) {
            UniValue cbTxObj;
            cbTx.ToJson(cbTxObj);
            result.pushKV("cbTx", cbTxObj);
        }
    }
    result.pushKV("time", block.GetBlockTime());
    result.pushKV("mediantime", (int64_t)blockindex->GetMedianTimePast());
    result.pushKV("nonce", (uint64_t)block.nNonce);
    result.pushKV("bits", strprintf("%08x", block.nBits));
    result.pushKV("difficulty", GetDifficulty(blockindex));
    result.pushKV("chainwork", blockindex->nChainWork.GetHex());
    result.pushKV("nTx", (uint64_t)blockindex->nTx);

    if (blockindex->pprev)
        result.pushKV("previousblockhash", blockindex->pprev->GetBlockHash().GetHex());
    CBlockIndex *pnext = chainActive.Next(blockindex);
    if (pnext)
        result.pushKV("nextblockhash", pnext->GetBlockHash().GetHex());

    result.pushKV("chainlock", chainLock);

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
            "\nReturns information about the best chainlock. Throws an error if there is no known chainlock yet."
            "\nResult:\n"
            "{\n"
            "  \"blockhash\" : \"hash\",      (string) The block hash hex encoded\n"
            "  \"height\" : n,              (numeric) The block height or index\n"
            "  \"signature\" : \"hash\",    (string) The chainlock's BLS signature.\n"
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
    result.pushKV("blockhash", clsig.blockHash.GetHex());
    result.pushKV("height", clsig.nHeight);
    result.pushKV("signature", clsig.sig.ToString());

    LOCK(cs_main);
    result.pushKV("known_block", mapBlockIndex.count(clsig.blockHash) > 0);
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
    ret.pushKV("hash", block.hash.GetHex());
    ret.pushKV("height", block.height);
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
    ret.pushKV("hash", block.hash.GetHex());
    ret.pushKV("height", block.height);
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
    ret.pushKV("hash", block.hash.GetHex());
    ret.pushKV("height", block.height);
    return ret;
}

UniValue syncwithvalidationinterfacequeue(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 0) {
        throw std::runtime_error(
            "syncwithvalidationinterfacequeue\n"
            "\nWaits for the validation interface queue to catch up on everything that was there when we entered this function.\n"
            "\nExamples:\n"
            + HelpExampleCli("syncwithvalidationinterfacequeue","")
            + HelpExampleRpc("syncwithvalidationinterfacequeue","")
        );
    }
    SyncWithValidationInterfaceQueue();
    return NullUniValue;
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
    return GetDifficulty(chainActive.Tip());
}

std::string EntryDescriptionString()
{
    return "    \"size\" : n,                 (numeric) transaction size in bytes\n"
           "    \"fee\" : n,                  (numeric) transaction fee in " + CURRENCY_UNIT + " (DEPRECATED)\n"
           "    \"modifiedfee\" : n,          (numeric) transaction fee with fee deltas used for mining priority (DEPRECATED)\n"
           "    \"time\" : n,                 (numeric) local time transaction entered pool in seconds since 1 Jan 1970 GMT\n"
           "    \"height\" : n,               (numeric) block height when transaction entered pool\n"
           "    \"descendantcount\" : n,      (numeric) number of in-mempool descendant transactions (including this one)\n"
           "    \"descendantsize\" : n,       (numeric) size of in-mempool descendants (including this one)\n"
           "    \"descendantfees\" : n,       (numeric) modified fees (see above) of in-mempool descendants (including this one) (DEPRECATED)\n"
           "    \"ancestorcount\" : n,        (numeric) number of in-mempool ancestor transactions (including this one)\n"
           "    \"ancestorsize\" : n,         (numeric) size of in-mempool ancestors (including this one)\n"
           "    \"ancestorfees\" : n,         (numeric) modified fees (see above) of in-mempool ancestors (including this one) (DEPRECATED)\n"
           "    \"fees\" : {\n"
           "        \"base\" : n,         (numeric) transaction fee in " + CURRENCY_UNIT + "\n"
           "        \"modified\" : n,     (numeric) transaction fee with fee deltas used for mining priority in " + CURRENCY_UNIT + "\n"
           "        \"ancestor\" : n,     (numeric) modified fees (see above) of in-mempool ancestors (including this one) in " + CURRENCY_UNIT + "\n"
           "        \"descendent\" : n,   (numeric) number of in-mempool ancestor transactions (including this one) in " + CURRENCY_UNIT + "\n"
           "    }\n"
           "    \"depends\" : [               (array) unconfirmed transactions used as inputs for this transaction\n"
           "        \"transactionid\",        (string) parent transaction id\n"
           "       ... ],\n"
           "    \"spentby\" : [           (array) unconfirmed transactions spending outputs from this transaction\n"
           "        \"transactionid\",    (string) child transaction id\n"
           "       ... ]\n"
           "    \"instantlock\" : true|false  (boolean) True if this transaction was locked via InstantSend\n";
}

void entryToJSON(UniValue &info, const CTxMemPoolEntry &e) EXCLUSIVE_LOCKS_REQUIRED(::mempool.cs)
{
    AssertLockHeld(mempool.cs);

    UniValue fees(UniValue::VOBJ);
    fees.pushKV("base", ValueFromAmount(e.GetFee()));
    fees.pushKV("modified", ValueFromAmount(e.GetModifiedFee()));
    fees.pushKV("ancestor", ValueFromAmount(e.GetModFeesWithAncestors()));
    fees.pushKV("descendant", ValueFromAmount(e.GetModFeesWithDescendants()));
    info.pushKV("fees", fees);

    info.pushKV("size", (int)e.GetTxSize());
    info.pushKV("fee", ValueFromAmount(e.GetFee()));
    info.pushKV("modifiedfee", ValueFromAmount(e.GetModifiedFee()));
    info.pushKV("time", e.GetTime());
    info.pushKV("height", (int)e.GetHeight());
    info.pushKV("descendantcount", e.GetCountWithDescendants());
    info.pushKV("descendantsize", e.GetSizeWithDescendants());
    info.pushKV("descendantfees", e.GetModFeesWithDescendants());
    info.pushKV("ancestorcount", e.GetCountWithAncestors());
    info.pushKV("ancestorsize", e.GetSizeWithAncestors());
    info.pushKV("ancestorfees", e.GetModFeesWithAncestors());
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

    info.pushKV("depends", depends);

    UniValue spent(UniValue::VARR);
    const CTxMemPool::txiter &it = mempool.mapTx.find(tx.GetHash());
    const CTxMemPool::setEntries &setChildren = mempool.GetMemPoolChildren(it);
    for (const CTxMemPool::txiter &childiter : setChildren) {
        spent.push_back(childiter->GetTx().GetHash().ToString());
    }

    info.pushKV("spentby", spent);
    info.pushKV("instantlock", llmq::quorumInstantSendManager->IsLocked(tx.GetHash()));
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
            o.pushKV(hash.ToString(), info);
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
            o.pushKV(_hash.ToString(), info);
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
            o.pushKV(_hash.ToString(), info);
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

    const CBlockIndex* pblockindex = LookupBlockIndex(hash);
    if (!pblockindex) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
    }

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
    if (IsBlockPruned(pblockindex)) {
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
		CBlockIndex* bindex = FindBlockByHeight(StringToDouble(strHash, 0));
		if (bindex==NULL)
		    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found by height");
		hash = bindex->GetBlockHash();
	}

    const CBlockIndex* pblockindex = LookupBlockIndex(hash);
    if (!pblockindex) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
    }

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
        LogPrint(BCLog::RPC, "Attempt to prune blocks close to the tip.  Retaining the minimum number of blocks.\n");
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
        ret.pushKV("height", (int64_t)stats.nHeight);
        ret.pushKV("bestblock", stats.hashBlock.GetHex());
        ret.pushKV("transactions", (int64_t)stats.nTransactions);
        ret.pushKV("txouts", (int64_t)stats.nTransactionOutputs);
        ret.pushKV("bogosize", (int64_t)stats.nBogoSize);
        ret.pushKV("hash_serialized_2", stats.hashSerialized.GetHex());
        ret.pushKV("disk_size", stats.nDiskSize);


		// Circulating = Emitted minus burned
		ret.pushKV("total_circulating_money_supply", ValueFromAmount(stats.nTotalAmount));
		ret.pushKV("total_burned", ValueFromAmount(stats.nTotalBurned));
	
		double nPct = ((stats.nTotalAmount/COIN) + .01) / 5200000000;
		ret.pushKV("percent_emitted", nPct);

		// Emission target, Dec 2022: https://wiki.biblepay.org/Emission_Schedule
		double nEmissionTargetDec2022 = 3297004487;
		ret.pushKV("emission_target_dec_2022", nEmissionTargetDec2022);

		int64_t Dec2021_Epoch = 1640470023;
		double dTotalDeflationComponent = .50; 
		
		int64_t nRemainingDays = (Dec2021_Epoch - GetAdjustedTime()) / 86400;
		int64_t nGenesisBlock = 1496347844;
		int64_t nDaysSinceGenesis = (GetAdjustedTime() - nGenesisBlock) / 86400;

		double nEmissionPerDay = stats.nTotalAmount/COIN / nDaysSinceGenesis;
		double nSythesizedTarget2022 = (nRemainingDays * (nEmissionPerDay * dTotalDeflationComponent)) + (stats.nTotalAmount / COIN);
		ret.pushKV("avg_emissions_per_day_since_inception", nEmissionPerDay);
		ret.pushKV("estimated_emissions_as_of_dec_2022", nSythesizedTarget2022);

		double nBudgetaryHealthPct = nSythesizedTarget2022 / nEmissionTargetDec2022;
		ret.pushKV("budgetary_health_pct_dec_2022", nBudgetaryHealthPct);

        ret.pushKV("total_amount", ValueFromAmount(stats.nTotalAmount));
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

    const CBlockIndex* pindex = LookupBlockIndex(pcoinsTip->GetBestBlock());
    ret.pushKV("bestblock", pindex->GetBlockHash().GetHex());
    if (coin.nHeight == MEMPOOL_HEIGHT) {
        ret.pushKV("confirmations", 0);
    } else {
        ret.pushKV("confirmations", (int64_t)(pindex->nHeight - coin.nHeight + 1));
    }
    ret.pushKV("value", ValueFromAmount(coin.out.nValue));
    UniValue o(UniValue::VOBJ);
    ScriptPubKeyToUniv(coin.out.scriptPubKey, o, true);
    ret.pushKV("scriptPubKey", o);
    ret.pushKV("coinbase", (bool)coin.fCoinBase);

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
    rv.pushKV("status", activated);
    return rv;
}

static UniValue SoftForkDesc(const std::string &name, int version, CBlockIndex* pindex, const Consensus::Params& consensusParams)
{
    UniValue rv(UniValue::VOBJ);
    rv.pushKV("id", name);
    rv.pushKV("version", version);
    rv.pushKV("reject", SoftForkMajorityDesc(version, pindex, consensusParams));
    return rv;
}

static UniValue BIP9SoftForkDesc(const Consensus::Params& consensusParams, Consensus::DeploymentPos id)
{
    UniValue rv(UniValue::VOBJ);
    const ThresholdState thresholdState = VersionBitsTipState(consensusParams, id);
    switch (thresholdState) {
    case ThresholdState::DEFINED: rv.pushKV("status", "defined"); break;
    case ThresholdState::STARTED: rv.pushKV("status", "started"); break;
    case ThresholdState::LOCKED_IN: rv.pushKV("status", "locked_in"); break;
    case ThresholdState::ACTIVE: rv.pushKV("status", "active"); break;
    case ThresholdState::FAILED: rv.pushKV("status", "failed"); break;
    }
    if (ThresholdState::STARTED == thresholdState)
    {
        rv.pushKV("bit", consensusParams.vDeployments[id].bit);
    }
    rv.pushKV("startTime", consensusParams.vDeployments[id].nStartTime);
    rv.pushKV("timeout", consensusParams.vDeployments[id].nTimeout);
    rv.pushKV("since", VersionBitsTipStateSinceHeight(consensusParams, id));
    if (ThresholdState::STARTED == thresholdState)
    {
        UniValue statsUV(UniValue::VOBJ);
        BIP9Stats statsStruct = VersionBitsTipStatistics(consensusParams, id);
        statsUV.pushKV("period", statsStruct.period);
        statsUV.pushKV("threshold", statsStruct.threshold);
        statsUV.pushKV("elapsed", statsStruct.elapsed);
        statsUV.pushKV("count", statsStruct.count);
        statsUV.pushKV("possible", statsStruct.possible);
        rv.pushKV("statistics", statsUV);
    }
    return rv;
}

void BIP9SoftForkDescPushBack(UniValue& bip9_softforks, const Consensus::Params& consensusParams, Consensus::DeploymentPos id)
{
    // Deployments with timeout value of 0 are hidden.
    // A timeout value of 0 guarantees a softfork will never be activated.
    // This is used when softfork codes are merged without specifying the deployment schedule.
    if (consensusParams.vDeployments[id].nTimeout > 0)
        bip9_softforks.pushKV(VersionBitsDeploymentInfo[id].name, BIP9SoftForkDesc(consensusParams, id));
}

UniValue getblockchaininfo(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getblockchaininfo\n"
            "Returns an object containing various state info regarding blockchain processing.\n"
            "\nResult:\n"
            "{\n"
            "  \"chain\": \"xxxx\",              (string) current network name as defined in BIP70 (main, test, regtest) and\n"
            "                                          devnet or devnet-<name> for \"-devnet\" and \"-devnet=<name>\" respectively\n"
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

    std::string strChainName = gArgs.IsArgSet("-devnet") ? gArgs.GetDevNetName() : Params().NetworkIDString();

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("chain",                 strChainName);
    obj.pushKV("blocks",                (int)chainActive.Height());
    obj.pushKV("headers",               pindexBestHeader ? pindexBestHeader->nHeight : -1);
    obj.pushKV("bestblockhash",         chainActive.Tip()->GetBlockHash().GetHex());
    obj.pushKV("difficulty",            (double)GetDifficulty(chainActive.Tip()));
    obj.pushKV("mediantime",            (int64_t)chainActive.Tip()->GetMedianTimePast());
    obj.pushKV("verificationprogress",  GuessVerificationProgress(Params().TxData(), chainActive.Tip()));
    obj.pushKV("initialblockdownload",  IsInitialBlockDownload());
    obj.pushKV("chainwork",             chainActive.Tip()->nChainWork.GetHex());
    obj.pushKV("size_on_disk",          CalculateCurrentUsage());
    obj.pushKV("pruned",                fPruneMode);
    if (fPruneMode) {
        CBlockIndex* block = chainActive.Tip();
        assert(block);
        while (block->pprev && (block->pprev->nStatus & BLOCK_HAVE_DATA)) {
            block = block->pprev;
        }

        obj.pushKV("pruneheight",        block->nHeight);

        // if 0, execution bypasses the whole if block.
        bool automatic_pruning = (gArgs.GetArg("-prune", 0) != 1);
        obj.pushKV("automatic_pruning",  automatic_pruning);
        if (automatic_pruning) {
            obj.pushKV("prune_target_size",  nPruneTarget);
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
    obj.pushKV("softforks",             softforks);
    obj.pushKV("bip9_softforks", bip9_softforks);

    obj.pushKV("warnings", GetWarnings("statusbar"));
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
        obj.pushKV("height", block->nHeight);
        obj.pushKV("hash", block->phashBlock->GetHex());
        obj.pushKV("difficulty", GetDifficulty(block));
        obj.pushKV("chainwork", block->nChainWork.GetHex());
        obj.pushKV("branchlen", branchLen);
        obj.pushKV("forkpoint", pindexFork->phashBlock->GetHex());

        std::string status;
        if (chainActive.Contains(block)) {
            // This block is part of the currently active chain.
            status = "active";
        } else if (block->nStatus & BLOCK_FAILED_MASK) {
            // This block or one of its ancestors is invalid.
            status = "invalid";
        } else if (block->nStatus & BLOCK_CONFLICT_CHAINLOCK) {
            // This block or one of its ancestors is conflicting with ChainLocks.
            status = "conflicting";
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
        obj.pushKV("status", status);

        res.push_back(obj);
    }

    return res;
}

UniValue mempoolInfoToJSON()
{
    UniValue ret(UniValue::VOBJ);
    ret.pushKV("size", (int64_t) mempool.size());
    ret.pushKV("bytes", (int64_t) mempool.GetTotalTxSize());
    ret.pushKV("usage", (int64_t) mempool.DynamicMemoryUsage());
    size_t maxmempool = gArgs.GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000;
    ret.pushKV("maxmempool", (int64_t) maxmempool);
    ret.pushKV("mempoolminfee", ValueFromAmount(std::max(mempool.GetMinFee(maxmempool), ::minRelayTxFee).GetFeePerK()));
    ret.pushKV("minrelaytxfee", ValueFromAmount(::minRelayTxFee.GetFeePerK()));
    ret.pushKV("instantsendlocks", (int64_t)llmq::quorumInstantSendManager->GetInstantSendLockCount());

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
        pblockindex = LookupBlockIndex(hash);
        if (!pblockindex) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
        }
    }

    CValidationState state;
    PreciousBlock(state, Params(), pblockindex);

    if (!state.IsValid()) {
        throw JSONRPCError(RPC_DATABASE_ERROR, FormatStateMessage(state));
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
        CBlockIndex* pblockindex = LookupBlockIndex(hash);
        if (!pblockindex) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
        }

        InvalidateBlock(state, Params(), pblockindex);
    }

    if (state.IsValid()) {
        ActivateBestChain(state, Params());
    }

    if (!state.IsValid()) {
        throw JSONRPCError(RPC_DATABASE_ERROR, FormatStateMessage(state));
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
        CBlockIndex* pblockindex = LookupBlockIndex(hash);
        if (!pblockindex) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
        }

        ResetBlockFailureFlags(pblockindex);
    }

    CValidationState state;
    ActivateBestChain(state, Params());

    if (!state.IsValid()) {
        throw JSONRPCError(RPC_DATABASE_ERROR, FormatStateMessage(state));
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
            "  \"time\": xxxxx,                         (numeric) The timestamp for the final block in the window in UNIX format.\n"
            "  \"txcount\": xxxxx,                      (numeric) The total number of transactions in the chain up to that point.\n"
            "  \"window_final_block_hash\": \"...\",      (string) The hash of the final block in the window.\n"
            "  \"window_block_count\": xxxxx,           (numeric) Size of the window in number of blocks.\n"
            "  \"window_tx_count\": xxxxx,              (numeric) The number of transactions in the window. Only returned if \"window_block_count\" is > 0.\n"
            "  \"window_interval\": xxxxx,              (numeric) The elapsed time in the window in seconds. Only returned if \"window_block_count\" is > 0.\n"
            "  \"txrate\": x.xx,                        (numeric) The average rate of transactions per second in the window. Only returned if \"window_interval\" is > 0.\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getchaintxstats", "")
            + HelpExampleRpc("getchaintxstats", "2016")
        );

    const CBlockIndex* pindex;
    int blockcount = 30 * 24 * 60 * 60 / Params().GetConsensus().nPowTargetSpacing; // By default: 1 month

    if (request.params[1].isNull()) {
        LOCK(cs_main);
        pindex = chainActive.Tip();
    } else {
        uint256 hash = uint256S(request.params[1].get_str());
        LOCK(cs_main);
        pindex = LookupBlockIndex(hash);
        if (!pindex) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
        }
        if (!chainActive.Contains(pindex)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Block is not in main chain");
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
    ret.pushKV("time", (int64_t)pindex->nTime);
    ret.pushKV("txcount", (int64_t)pindex->nChainTx);
    ret.pushKV("window_final_block_hash", pindex->GetBlockHash().GetHex());
    ret.pushKV("window_block_count", blockcount);
    if (blockcount > 0) {
        ret.pushKV("window_tx_count", nTxDiff);
        ret.pushKV("window_interval", nTimeDiff);
        if (nTimeDiff > 0) {
            ret.pushKV("txrate", ((double)nTxDiff) / nTimeDiff);
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
    int64_t mintxsize = MaxBlockSize();
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
    ret_all.pushKV("mintxsize", mintxsize == MaxBlockSize() ? 0 : mintxsize);
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
		int64_t nHeight = (int64_t)StringToDouble(sHeight, 0);
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
					results.push_back(Pair("blockinfo", block.vtx[0]->vout[0].sTxOutMessage));
					results.push_back(Pair("minerguid", ExtractXML(block.vtx[0]->vout[0].sTxOutMessage,"<MINERGUID>","</MINERGUID>")));
				}
			}
		}
		else
		{
			results.push_back(Pair("error","block not found"));
		}
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
	else if (sItem == "sendmanyxml")
	{
		// Pools: Allows pools to send a multi-output tx with ease
		// Format: exec sendmanyxml from_account xml_payload comment
		std::string strAccount = request.params[1].get_str();
		if (strAccount == "*")
			throw JSONRPCError(RPC_WALLET_INVALID_ACCOUNT_NAME, "Invalid account name");
		std::string sXML = request.params[2].get_str();
		std::string sTXID;
		SendManyXML(sXML, sTXID);
		results.push_back(Pair("txid", sTXID));
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
	else if (sItem == "testsc")
	{
		std::string sToAddress = DefaultRecAddress("sc");
		std::string sTXID;
		std::string sError;
		std::string sXML = "<sc><objtype>test</objtype><url>https://test.com/</url></sc>";
		bool fSent = RPCSendMoney(sError, sToAddress, 1 * COIN, sTXID, sXML);
		results.push_back(Pair("TXID", sTXID));
	}
	else if (sItem == "listsc")
	{
		results.push_back(Pair("scsz", mapSidechain.size()));
		for (auto ii : mapSidechain) 
		{
			Sidechain s = mapSidechain[ii.first];
		    UniValue o;
            s.ToJson(o);
    		results.push_back(Pair(ii.first, o));
		}
	}
	else if (sItem == "upgradesanc")
	{
		if (request.params.size() != 3)
			throw std::runtime_error("You must specify exec upgradesanc sanctuary_name (where the sanctuary_name matches the name in the masternode.conf file) 0/1 (where 0=dry-run, 1=real).   NOTE:  Please be sure your masternode.conf has a carriage return after the end of every sanctuary entry (otherwise we can't parse each entry correctly). ");

		std::string sSearch = request.params[1].get_str();
		int iDryRun = StringToDouble(request.params[2].get_str(), 0);
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
		double dColOrdinal = StringToDouble(sCollateralTXIDOrdinal, 0);
		if (sCollateralTXIDOrdinal.length() != 1 || dColOrdinal > 9 || sCollateralTXID.length() < 16)
		{
			throw std::runtime_error("Sanctuary entry in masternode.conf corrupted (collateral txid missing, or there is no newline at the end of the entry in the masternode.conf file.)");
		}
		std::string sSummary = "Creating protx_register command for Sanctuary " + sSancName + " with IP " + sSancIP + " with TXID " + sCollateralTXID;
		// Step 1: Fund the protx fee
		// 1a. Create the new deterministic-sanctuary reward address
		std::string sPayAddress = DefaultRecAddress(sSancName + "-d"); //d means deterministic
		std::string sVotingAddress = DefaultRecAddress(sSancName + "-v"); //v means voting
		std::string sError;
		
		bool fSubtractFee = false;
		bool fInstantSend = false;
		// 1b. We must send 1 COIN to ourself first here, as the deterministic sanctuaries future fund receiving address must be prefunded with enough funds to cover the non-financial transaction transmission below
		std::string sTXID;
		bool fSent = RPCSendMoney(sError, sPayAddress, 1 * COIN, sTXID);

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
		
		newRequest.params.push_back(sVotingAddress);  // [4] = Home Voting Address
		newRequest.params.push_back(myBLSPublic);     // Remote Sanctuary Public Key (Private and public keypair is stored in deterministicsanctuary.conf on the controller wallet)
		newRequest.params.push_back(sVotingAddress);  // Delegates Voting address (This is a person that can vote for you if you want) - in our case its the same 

		newRequest.params.push_back("0");             // Pct of rewards to share with Operator (This is the amount of reward we want to share with a Sanc Operator - IE a hosting company)
		newRequest.params.push_back(sPayAddress);     // [8] = Rewards Pay To Address (This can be changed to be a wallet outside of your wallet, maybe a hardware wallet)
		// Fee Source Address (prevents Fee 404)
		// 		std::string sCPK = DefaultRecAddress("Christian-Public-Key");
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
	else if (sItem == "randomx")
	{
		std::string sHeader = request.params[1].get_str();
		std::string sKey = request.params[2].get_str();
		std::string sRevKey = ReverseHex(sKey);
		uint256 uKey = uint256S("0x" + sRevKey);
		std::vector<unsigned char> v = ParseHex(sHeader);
		uint256 uRX3 = RandomX_Hash(v, uKey, 99);
		results.push_back(Pair("hash2", uRX3.GetHex()));
		std::string sFakeHeader = "<rxheader>" + sHeader + "</rxheader>";
		uint256 rxhash3 = GetRandomXHash3(sFakeHeader, uKey, 2);
		results.push_back(Pair("hash3", rxhash3.GetHex()));

	}
	else if (sItem == "getgovlimit")
	{
		const Consensus::Params& consensusParams = Params().GetConsensus();
		int nBits = 486585255;
		int nHeight = StringToDouble(request.params[1].get_str(), 0);
		CAmount nLimit = CSuperblock::GetPaymentsLimit(nHeight);
		CAmount nReward = GetBlockSubsidy(nBits, nHeight, consensusParams, false);
		CAmount nRewardGov = GetBlockSubsidy(nBits, nHeight, consensusParams, true);
		CAmount nSanc = GetMasternodePayment(nHeight, nReward);
        results.push_back(Pair("Limit", (double)nLimit/COIN));
		results.push_back(Pair("Subsidy", (double)nReward/COIN));
		results.push_back(Pair("Sanc", (double)nSanc/COIN));
		results.push_back(Pair("GovernanceSubsidy", (double)nRewardGov/COIN));
	}
	else if (sItem == "hexblocktojson")
	{
		std::string sHex = request.params[1].get_str();
		CBlock block;
        if (!DecodeHexBlk(block, sHex))
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block decode failed");
		UniValue obj(UniValue::VOBJ);
		obj = blockToJSON(block, NULL, true);
		results.push_back(obj);
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
	else if (sItem == "nextcontract")
	{
		int nNextDailyHeight = 0;
		if (request.params.size() > 1)
		{
			nNextDailyHeight = (int)StringToDouble(request.params[1].get_str(), 0);
		}
		else
		{
			int nBlockHeight = chainActive.Height();
			nNextDailyHeight = GetNextDailySuperblock(nBlockHeight);
		}

		std::string s = ScanChainForData(nNextDailyHeight);
		results.push_back(Pair("nextdailysuperblock", nNextDailyHeight));
		results.push_back(Pair("nextcontract", s));
		std::vector<Portfolio> vPortfolio = GetDailySuperblock(nNextDailyHeight);
		CAmount nTotalPayments = 0;
		for (int i = 0; i < vPortfolio.size(); i++)
		{
			std::string sRecipient1 = vPortfolio[i].OwnerAddress;
			CAmount nAmount1 = vPortfolio[i].Owed * COIN;
			nTotalPayments += nAmount1;
			results.push_back(Pair("Recipient" + DoubleToString(i, 0), sRecipient1));
			results.push_back(Pair("Amount" + DoubleToString(i,0) , AmountToString(nAmount1)));
		
		}
		results.push_back(Pair("Total", AmountToString(nTotalPayments)));
	}
	else if (sItem == "blocktohex")
	{
		std::string sBlock = request.params[1].get_str();
		int nHeight = (int)StringToDouble(sBlock, 0);
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
            "\nDumps the mempool to disk. It will fail until the previous dump is fully loaded.\n"
            "\nExamples:\n"
            + HelpExampleCli("savemempool", "")
            + HelpExampleRpc("savemempool", "")
        );
    }

    if (!g_is_mempool_loaded) {
        throw JSONRPCError(RPC_MISC_ERROR, "The mempool was not loaded yet");
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
	{ "hidden",             "exec",				      &exec,                   {"1","2","3","4","5","6","7"} },
    { "hidden",             "waitfornewblock",        &waitfornewblock,        {"timeout"} },
    { "hidden",             "waitforblock",           &waitforblock,           {"blockhash","timeout"} },
    { "hidden",             "waitforblockheight",     &waitforblockheight,     {"height","timeout"} },
    { "hidden",             "syncwithvalidationinterfacequeue", &syncwithvalidationinterfacequeue, {} },
};

void RegisterBlockchainRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
