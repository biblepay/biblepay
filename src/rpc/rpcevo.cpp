// Copyright (c) 2018-2020 The Däsh Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <base58.h>
#include <consensus/validation.h>
#include <core_io.h>
#include <init.h>
#include <messagesigner.h>
#include <rpc/safemode.h>
#include <rpc/server.h>
#include <txmempool.h>
#include <utilmoneystr.h>
#include <validation.h>

#ifdef ENABLE_WALLET
#include <wallet/coincontrol.h>
#include <wallet/wallet.h>
#include <wallet/rpcwallet.h>
#endif//ENABLE_WALLET

#include <netbase.h>

#include <evo/specialtx.h>
#include <evo/providertx.h>
#include <evo/deterministicmns.h>
#include <evo/simplifiedmns.h>

#include <bls/bls.h>

#include "masternode/masternode-meta.h"
#include "validation.h"
#include "kjv.h"
#include "evo/cbtx.h"
#include "smartcontract-client.h"
#include "smartcontract-server.h"
#include "rpcutxo.h"
#include <boost/algorithm/string/case_conv.hpp> // for to_lower()

#ifdef ENABLE_WALLET
extern UniValue signrawtransaction(const JSONRPCRequest& request);
extern UniValue sendrawtransaction(const JSONRPCRequest& request);
extern UniValue protx_register(const JSONRPCRequest& request);
#endif//ENABLE_WALLET

std::string GetHelpString(int nParamNum, std::string strParamName)
{
    static const std::map<std::string, std::string> mapParamHelp = {
        {"collateralAddress",
            "%d. \"collateralAddress\"        (string, required) The biblepay address to send the collateral to.\n"
        },
        {"collateralHash",
            "%d. \"collateralHash\"           (string, required) The collateral transaction hash.\n"
        },
        {"collateralIndex",
            "%d. collateralIndex            (numeric, required) The collateral transaction output index.\n"
        },
        {"feeSourceAddress",
            "%d. \"feeSourceAddress\"         (string, optional) If specified wallet will only use coins from this address to fund ProTx.\n"
            "                              If not specified, payoutAddress is the one that is going to be used.\n"
            "                              The private key belonging to this address must be known in your wallet.\n"
        },
        {"fundAddress",
            "%d. \"fundAddress\"              (string, optional) If specified wallet will only use coins from this address to fund ProTx.\n"
            "                              If not specified, payoutAddress is the one that is going to be used.\n"
            "                              The private key belonging to this address must be known in your wallet.\n"
        },
        {"ipAndPort",
            "%d. \"ipAndPort\"                (string, required) IP and port in the form \"IP:PORT\".\n"
            "                              Must be unique on the network. Can be set to 0, which will require a ProUpServTx afterwards.\n"
        },
        {"operatorKey",
            "%d. \"operatorKey\"              (string, required) The operator BLS private key associated with the\n"
            "                              registered operator public key.\n"
        },
        {"operatorPayoutAddress",
            "%d. \"operatorPayoutAddress\"    (string, optional) The address used for operator reward payments.\n"
            "                              Only allowed when the ProRegTx had a non-zero operatorReward value.\n"
            "                              If set to an empty string, the currently active payout address is reused.\n"
        },
        {"operatorPubKey_register",
            "%d. \"operatorPubKey\"           (string, required) The operator BLS public key. The BLS private key does not have to be known.\n"
            "                              It has to match the BLS private key which is later used when operating the masternode.\n"
        },
        {"operatorPubKey_update",
            "%d. \"operatorPubKey\"           (string, required) The operator BLS public key. The BLS private key does not have to be known.\n"
            "                              It has to match the BLS private key which is later used when operating the masternode.\n"
            "                              If set to an empty string, the currently active operator BLS public key is reused.\n"
        },
        {"operatorReward",
            "%d. \"operatorReward\"           (numeric, required) The fraction in %% to share with the operator. The value must be\n"
            "                              between 0.00 and 100.00.\n"
        },
        {"ownerAddress",
            "%d. \"ownerAddress\"             (string, required) The biblepay address to use for payee updates and proposal voting.\n"
            "                              The private key belonging to this address must be known in your wallet. The address must\n"
            "                              be unused and must differ from the collateralAddress\n"
        },
        {"payoutAddress_register",
            "%d. \"payoutAddress\"            (string, required) The biblepay address to use for masternode reward payments.\n"
        },
        {"payoutAddress_update",
            "%d. \"payoutAddress\"            (string, required) The biblepay address to use for masternode reward payments.\n"
            "                              If set to an empty string, the currently active payout address is reused.\n"
        },
        {"proTxHash",
            "%d. \"proTxHash\"                (string, required) The hash of the initial ProRegTx.\n"
        },
        {"reason",
            "%d. reason                     (numeric, optional) The reason for masternode service revocation.\n"
        },
        {"votingAddress_register",
            "%d. \"votingAddress\"            (string, required) The voting key address. The private key does not have to be known by your wallet.\n"
            "                              It has to match the private key which is later used when voting on proposals.\n"
            "                              If set to an empty string, ownerAddress will be used.\n"
        },
        {"votingAddress_update",
            "%d. \"votingAddress\"            (string, required) The voting key address. The private key does not have to be known by your wallet.\n"
            "                              It has to match the private key which is later used when voting on proposals.\n"
            "                              If set to an empty string, the currently active voting key address is reused.\n"
        },
    };

    auto it = mapParamHelp.find(strParamName);
    if (it == mapParamHelp.end())
        throw std::runtime_error(strprintf("FIXME: WRONG PARAM NAME %s!", strParamName));

    return strprintf(it->second, nParamNum);
}

// Allows to specify BiblePay address or priv key. In case of BiblePay address, the priv key is taken from the wallet
static CKey ParsePrivKey(CWallet* pwallet, const std::string &strKeyOrAddress, bool allowAddresses = true) {
    CTxDestination dest = DecodeDestination(strKeyOrAddress);
    if (allowAddresses && IsValidDestination(dest)) {
#ifdef ENABLE_WALLET
        if (!pwallet) {
            throw std::runtime_error("addresses not supported when wallet is disabled");
        }
        EnsureWalletIsUnlocked(pwallet);
        const CKeyID *keyID = boost::get<CKeyID>(&dest);
        CKey key;
        if (!keyID || !pwallet->GetKey(*keyID, key))
            throw std::runtime_error(strprintf("non-wallet or invalid address %s", strKeyOrAddress));
        return key;
#else//ENABLE_WALLET
        throw std::runtime_error("addresses not supported in no-wallet builds");
#endif//ENABLE_WALLET
    }

    CBitcoinSecret secret;
    if (!secret.SetString(strKeyOrAddress) || !secret.IsValid()) {
        throw std::runtime_error(strprintf("invalid priv-key/address %s", strKeyOrAddress));
    }
    return secret.GetKey();
}

static CKeyID ParsePubKeyIDFromAddress(const std::string& strAddress, const std::string& paramName)
{
    CTxDestination dest = DecodeDestination(strAddress);
    const CKeyID *keyID = boost::get<CKeyID>(&dest);
    if (!keyID) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("%s must be a valid P2PKH address, not %s", paramName, strAddress));
    }
    return *keyID;
}

static CBLSPublicKey ParseBLSPubKey(const std::string& hexKey, const std::string& paramName)
{
    CBLSPublicKey pubKey;
    if (!pubKey.SetHexStr(hexKey)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("%s must be a valid BLS public key, not %s", paramName, hexKey));
    }
    return pubKey;
}

static CBLSSecretKey ParseBLSSecretKey(const std::string& hexKey, const std::string& paramName)
{
    CBLSSecretKey secKey;
    if (!secKey.SetHexStr(hexKey)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("%s must be a valid BLS secret key", paramName));
    }
    return secKey;
}

#ifdef ENABLE_WALLET

template<typename SpecialTxPayload>
static void FundSpecialTx(CWallet* pwallet, CMutableTransaction& tx, const SpecialTxPayload& payload, const CTxDestination& fundDest)
{
    assert(pwallet != nullptr);

    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    LOCK2(cs_main, mempool.cs);
    LOCK(pwallet->cs_wallet);

    CTxDestination nodest = CNoDestination();
    if (fundDest == nodest) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "No source of funds specified");
    }

    CDataStream ds(SER_NETWORK, PROTOCOL_VERSION);
    ds << payload;
    tx.vExtraPayload.assign(ds.begin(), ds.end());

    static CTxOut dummyTxOut(0, CScript() << OP_RETURN);
    std::vector<CRecipient> vecSend;
    bool dummyTxOutAdded = false;

    if (tx.vout.empty()) {
        // add dummy txout as CreateTransaction requires at least one recipient
        tx.vout.emplace_back(dummyTxOut);
        dummyTxOutAdded = true;
    }

    for (const auto& txOut : tx.vout) {
        CRecipient recipient = {txOut.scriptPubKey, txOut.nValue, false};
        vecSend.push_back(recipient);
    }

    CCoinControl coinControl;
    coinControl.destChange = fundDest;
    coinControl.fRequireAllInputs = false;

    std::vector<COutput> vecOutputs;
    pwallet->AvailableCoins(vecOutputs);

    for (const auto& out : vecOutputs) {
        CTxDestination txDest;
        if (ExtractDestination(out.tx->tx->vout[out.i].scriptPubKey, txDest) && txDest == fundDest) {
            coinControl.Select(COutPoint(out.tx->tx->GetHash(), out.i));
        }
    }

    if (!coinControl.HasSelected()) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("No funds at specified address %s", EncodeDestination(fundDest)));
    }

    CWalletTx wtx;
    CReserveKey reservekey(pwallet);
    CAmount nFee;
    int nChangePos = -1;
    std::string strFailReason;

    if (!pwallet->CreateTransaction(vecSend, wtx, reservekey, nFee, nChangePos, strFailReason, coinControl, false, tx.vExtraPayload.size())) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, strFailReason);
    }

    tx.vin = wtx.tx->vin;
    tx.vout = wtx.tx->vout;

    if (dummyTxOutAdded && tx.vout.size() > 1) {
        // CreateTransaction added a change output, so we don't need the dummy txout anymore.
        // Removing it results in slight overpayment of fees, but we ignore this for now (as it's a very low amount).
        auto it = std::find(tx.vout.begin(), tx.vout.end(), dummyTxOut);
        assert(it != tx.vout.end());
        tx.vout.erase(it);
    }
}

template<typename SpecialTxPayload>
static void UpdateSpecialTxInputsHash(const CMutableTransaction& tx, SpecialTxPayload& payload)
{
    payload.inputsHash = CalcTxInputsHash(tx);
}

template<typename SpecialTxPayload>
static void SignSpecialTxPayloadByHash(const CMutableTransaction& tx, SpecialTxPayload& payload, const CKey& key)
{
    UpdateSpecialTxInputsHash(tx, payload);
    payload.vchSig.clear();

    uint256 hash = ::SerializeHash(payload);
    if (!CHashSigner::SignHash(hash, key, payload.vchSig)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "failed to sign special tx");
    }
}

template<typename SpecialTxPayload>
static void SignSpecialTxPayloadByString(const CMutableTransaction& tx, SpecialTxPayload& payload, const CKey& key)
{
    UpdateSpecialTxInputsHash(tx, payload);
    payload.vchSig.clear();

    std::string m = payload.MakeSignString();
    if (!CMessageSigner::SignMessage(m, payload.vchSig, key)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "failed to sign special tx");
    }
}

template<typename SpecialTxPayload>
static void SignSpecialTxPayloadByHash(const CMutableTransaction& tx, SpecialTxPayload& payload, const CBLSSecretKey& key)
{
    UpdateSpecialTxInputsHash(tx, payload);

    uint256 hash = ::SerializeHash(payload);
    payload.sig = key.Sign(hash);
}

static std::string SignAndSendSpecialTx(const CMutableTransaction& tx)
{
    {
    LOCK(cs_main);

    CValidationState state;
    if (!CheckSpecialTx(tx, chainActive.Tip(), state)) {
        throw std::runtime_error(FormatStateMessage(state));
    }
    } // cs_main

    CDataStream ds(SER_NETWORK, PROTOCOL_VERSION);
    ds << tx;

    JSONRPCRequest signRequest;
    signRequest.params.setArray();
    signRequest.params.push_back(HexStr(ds.begin(), ds.end()));
    UniValue signResult = signrawtransaction(signRequest);

    JSONRPCRequest sendRequest;
    sendRequest.params.setArray();
    sendRequest.params.push_back(signResult["hex"].get_str());
    return sendrawtransaction(sendRequest).get_str();
}

void protx_register_fund_help(CWallet* const pwallet)
{
    throw std::runtime_error(
            "protx register_fund \"collateralAddress\" \"ipAndPort\" \"ownerAddress\" \"operatorPubKey\" \"votingAddress\" operatorReward \"payoutAddress\" ( \"fundAddress\" )\n"
            "\nCreates, funds and sends a ProTx to the network. The resulting transaction will move 1000 BiblePay\n"
            "to the address specified by collateralAddress and will then function as the collateral of your\n"
            "masternode.\n"
            "A few of the limitations you see in the arguments are temporary and might be lifted after DIP3\n"
            "is fully deployed.\n"
            + HelpRequiringPassphrase(pwallet) + "\n"
            "\nArguments:\n"
            + GetHelpString(1, "collateralAddress")
            + GetHelpString(2, "ipAndPort")
            + GetHelpString(3, "ownerAddress")
            + GetHelpString(4, "operatorPubKey_register")
            + GetHelpString(5, "votingAddress_register")
            + GetHelpString(6, "operatorReward")
            + GetHelpString(7, "payoutAddress_register")
            + GetHelpString(8, "fundAddress") +
            "\nResult:\n"
            "\"txid\"                        (string) The transaction id.\n"
            "\nExamples:\n"
            + HelpExampleCli("protx", "register_fund \"XrVhS9LogauRJGJu2sHuryjhpuex4RNPSb\" \"1.2.3.4:1234\" \"Xt9AMWaYSz7tR7Uo7gzXA3m4QmeWgrR3rr\" \"93746e8731c57f87f79b3620a7982924e2931717d49540a85864bd543de11c43fb868fd63e501a1db37e19ed59ae6db4\" \"Xt9AMWaYSz7tR7Uo7gzXA3m4QmeWgrR3rr\" 0 \"XrVhS9LogauRJGJu2sHuryjhpuex4RNPSb\"")
    );
}

void protx_register_help(CWallet* const pwallet)
{
    throw std::runtime_error(
            "protx register \"collateralHash\" collateralIndex \"ipAndPort\" \"ownerAddress\" \"operatorPubKey\" \"votingAddress\" operatorReward \"payoutAddress\" ( \"feeSourceAddress\" )\n"
            "\nSame as \"protx register_fund\", but with an externally referenced collateral.\n"
            "The collateral is specified through \"collateralHash\" and \"collateralIndex\" and must be an unspent\n"
            "transaction output spendable by this wallet. It must also not be used by any other masternode.\n"
            + HelpRequiringPassphrase(pwallet) + "\n"
            "\nArguments:\n"
            + GetHelpString(1, "collateralHash")
            + GetHelpString(2, "collateralIndex")
            + GetHelpString(3, "ipAndPort")
            + GetHelpString(4, "ownerAddress")
            + GetHelpString(5, "operatorPubKey_register")
            + GetHelpString(6, "votingAddress_register")
            + GetHelpString(7, "operatorReward")
            + GetHelpString(8, "payoutAddress_register")
            + GetHelpString(9, "feeSourceAddress") +
            "\nResult:\n"
            "\"txid\"                        (string) The transaction id.\n"
            "\nExamples:\n"
            + HelpExampleCli("protx", "register \"0123456701234567012345670123456701234567012345670123456701234567\" 0 \"1.2.3.4:1234\" \"Xt9AMWaYSz7tR7Uo7gzXA3m4QmeWgrR3rr\" \"93746e8731c57f87f79b3620a7982924e2931717d49540a85864bd543de11c43fb868fd63e501a1db37e19ed59ae6db4\" \"Xt9AMWaYSz7tR7Uo7gzXA3m4QmeWgrR3rr\" 0 \"XrVhS9LogauRJGJu2sHuryjhpuex4RNPSb\"")
    );
}

void protx_register_prepare_help()
{
    throw std::runtime_error(
            "protx register_prepare \"collateralHash\" collateralIndex \"ipAndPort\" \"ownerAddress\" \"operatorPubKey\" \"votingAddress\" operatorReward \"payoutAddress\" ( \"feeSourceAddress\" )\n"
            "\nCreates an unsigned ProTx and returns it. The ProTx must be signed externally with the collateral\n"
            "key and then passed to \"protx register_submit\". The prepared transaction will also contain inputs\n"
            "and outputs to cover fees.\n"
            "\nArguments:\n"
            + GetHelpString(1, "collateralHash")
            + GetHelpString(2, "collateralIndex")
            + GetHelpString(3, "ipAndPort")
            + GetHelpString(4, "ownerAddress")
            + GetHelpString(5, "operatorPubKey_register")
            + GetHelpString(6, "votingAddress_register")
            + GetHelpString(7, "operatorReward")
            + GetHelpString(8, "payoutAddress_register")
            + GetHelpString(9, "feeSourceAddress") +
            "\nResult:\n"
            "{                             (json object)\n"
            "  \"tx\" :                      (string) The serialized ProTx in hex format.\n"
            "  \"collateralAddress\" :       (string) The collateral address.\n"
            "  \"signMessage\" :             (string) The string message that needs to be signed with\n"
            "                              the collateral key.\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("protx", "register_prepare \"0123456701234567012345670123456701234567012345670123456701234567\" 0 \"1.2.3.4:1234\" \"Xt9AMWaYSz7tR7Uo7gzXA3m4QmeWgrR3rr\" \"93746e8731c57f87f79b3620a7982924e2931717d49540a85864bd543de11c43fb868fd63e501a1db37e19ed59ae6db4\" \"Xt9AMWaYSz7tR7Uo7gzXA3m4QmeWgrR3rr\" 0 \"XrVhS9LogauRJGJu2sHuryjhpuex4RNPSb\"")
    );
}

void protx_register_submit_help(CWallet* const pwallet)
{
    throw std::runtime_error(
            "protx register_submit \"tx\" \"sig\"\n"
            "\nSubmits the specified ProTx to the network. This command will also sign the inputs of the transaction\n"
            "which were previously added by \"protx register_prepare\" to cover transaction fees\n"
            + HelpRequiringPassphrase(pwallet) + "\n"
            "\nArguments:\n"
            "1. \"tx\"                 (string, required) The serialized transaction previously returned by \"protx register_prepare\"\n"
            "2. \"sig\"                (string, required) The signature signed with the collateral key. Must be in base64 format.\n"
            "\nResult:\n"
            "\"txid\"                  (string) The transaction id.\n"
            "\nExamples:\n"
            + HelpExampleCli("protx", "register_submit \"tx\" \"sig\"")
    );
}

// handles register, register_prepare and register_fund in one method
UniValue protx_register(const JSONRPCRequest& request)
{
    CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
    bool isExternalRegister = request.params[0].get_str() == "register";
    bool isFundRegister = request.params[0].get_str() == "register_fund";
    bool isPrepareRegister = request.params[0].get_str() == "register_prepare";

    if (isFundRegister && (request.fHelp || (request.params.size() != 8 && request.params.size() != 9))) {
        protx_register_fund_help(pwallet);
    } else if (isExternalRegister && (request.fHelp || (request.params.size() != 9 && request.params.size() != 10))) {
        protx_register_help(pwallet);
    } else if (isPrepareRegister && (request.fHelp || (request.params.size() != 9 && request.params.size() != 10))) {
        protx_register_prepare_help();
    }

    ObserveSafeMode();

    if (!EnsureWalletIsAvailable(pwallet, request.fHelp))
        return NullUniValue;

    if (isExternalRegister || isFundRegister) {
        EnsureWalletIsUnlocked(pwallet);
    }

    size_t paramIdx = 1;

    CAmount collateralAmount = SANCTUARY_COLLATERAL * COIN;

    CMutableTransaction tx;
    tx.nVersion = 3;
    tx.nType = TRANSACTION_PROVIDER_REGISTER;

    CProRegTx ptx;
    ptx.nVersion = CProRegTx::CURRENT_VERSION;

    if (isFundRegister) {
        CTxDestination collateralDest = DecodeDestination(request.params[paramIdx].get_str());
        if (!IsValidDestination(collateralDest)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("invalid collaterall address: %s", request.params[paramIdx].get_str()));
        }
        CScript collateralScript = GetScriptForDestination(collateralDest);

        CTxOut collateralTxOut(collateralAmount, collateralScript);
        tx.vout.emplace_back(collateralTxOut);

        paramIdx++;
    } else {
        uint256 collateralHash = ParseHashV(request.params[paramIdx], "collateralHash");
        int32_t collateralIndex = ParseInt32V(request.params[paramIdx + 1], "collateralIndex");
        if (collateralHash.IsNull() || collateralIndex < 0) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("invalid hash or index: %s-%d", collateralHash.ToString(), collateralIndex));
        }

        ptx.collateralOutpoint = COutPoint(collateralHash, (uint32_t)collateralIndex);
        paramIdx += 2;

        // TODO unlock on failure
        LOCK(pwallet->cs_wallet);
        pwallet->LockCoin(ptx.collateralOutpoint);
    }

    if (request.params[paramIdx].get_str() != "") {
		// Allow the underlying code to throw the error 
		if (false)
		{
			if (!Lookup(request.params[paramIdx].get_str().c_str(), ptx.addr, Params().GetDefaultPort(), false)) {
				throw std::runtime_error(strprintf("invalid network address %s", request.params[paramIdx].get_str()));
			}
		}
    }

    CKey keyOwner = ParsePrivKey(pwallet, request.params[paramIdx + 1].get_str(), true);
    CBLSPublicKey pubKeyOperator = ParseBLSPubKey(request.params[paramIdx + 2].get_str(), "operator BLS address");
    CKeyID keyIDVoting = keyOwner.GetPubKey().GetID();
    if (request.params[paramIdx + 3].get_str() != "") {
        keyIDVoting = ParsePubKeyIDFromAddress(request.params[paramIdx + 3].get_str(), "voting address");
    }

    int64_t operatorReward;
    if (!ParseFixedPoint(request.params[paramIdx + 4].getValStr(), 2, &operatorReward)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "operatorReward must be a number");
    }
    if (operatorReward < 0 || operatorReward > 10000) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "operatorReward must be between 0.00 and 100.00");
    }
    ptx.nOperatorReward = operatorReward;

    CTxDestination payoutDest = DecodeDestination(request.params[paramIdx + 5].get_str());
    if (!IsValidDestination(payoutDest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("invalid payout address: %s", request.params[paramIdx + 5].get_str()));
    }

    ptx.keyIDOwner = keyOwner.GetPubKey().GetID();
    ptx.pubKeyOperator = pubKeyOperator;
    ptx.keyIDVoting = keyIDVoting;
    ptx.scriptPayout = GetScriptForDestination(payoutDest);

    if (!isFundRegister) {
        // make sure fee calculation works
        ptx.vchSig.resize(65);
    }

    CTxDestination fundDest = payoutDest;
    if (!request.params[paramIdx + 6].isNull()) {
        fundDest = DecodeDestination(request.params[paramIdx + 6].get_str());
        if (!IsValidDestination(fundDest))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid BiblePay address: ") + request.params[paramIdx + 6].get_str());
    }

    FundSpecialTx(pwallet, tx, ptx, fundDest);
    UpdateSpecialTxInputsHash(tx, ptx);

    if (isFundRegister) {
        uint32_t collateralIndex = (uint32_t) -1;
        for (uint32_t i = 0; i < tx.vout.size(); i++) {
            if (tx.vout[i].nValue == collateralAmount) {
                collateralIndex = i;
                break;
            }
        }
        assert(collateralIndex != (uint32_t) -1);
        ptx.collateralOutpoint.n = collateralIndex;

        SetTxPayload(tx, ptx);
        return SignAndSendSpecialTx(tx);
    } else {
        // referencing external collateral

        Coin coin;
        if (!GetUTXOCoin(ptx.collateralOutpoint, coin)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("collateral not found: %s", ptx.collateralOutpoint.ToStringShort()));
        }
        CTxDestination txDest;
        ExtractDestination(coin.out.scriptPubKey, txDest);
        const CKeyID *keyID = boost::get<CKeyID>(&txDest);
        if (!keyID) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("collateral type not supported: %s", ptx.collateralOutpoint.ToStringShort()));
        }

        if (isPrepareRegister) {
            // external signing with collateral key
            ptx.vchSig.clear();
            SetTxPayload(tx, ptx);

            UniValue ret(UniValue::VOBJ);
            ret.push_back(Pair("tx", EncodeHexTx(tx)));
            ret.push_back(Pair("collateralAddress", EncodeDestination(txDest)));
            ret.push_back(Pair("signMessage", ptx.MakeSignString()));
            return ret;
        } else {
            // lets prove we own the collateral
            CKey key;
            if (!pwallet->GetKey(*keyID, key)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("collateral key not in wallet: %s", EncodeDestination(txDest)));
            }
            SignSpecialTxPayloadByString(tx, ptx, key);
            SetTxPayload(tx, ptx);
            return SignAndSendSpecialTx(tx);
        }
    }
}

UniValue protx_register_submit(const JSONRPCRequest& request)
{
    CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
    if (request.fHelp || request.params.size() != 3) {
        protx_register_submit_help(pwallet);
    }

    ObserveSafeMode();

    if (!EnsureWalletIsAvailable(pwallet, request.fHelp))
        return NullUniValue;

    EnsureWalletIsUnlocked(pwallet);

    CMutableTransaction tx;
    if (!DecodeHexTx(tx, request.params[1].get_str())) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "transaction not deserializable");
    }
    if (tx.nType != TRANSACTION_PROVIDER_REGISTER) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "transaction not a ProRegTx");
    }
    CProRegTx ptx;
    if (!GetTxPayload(tx, ptx)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "transaction payload not deserializable");
    }
    if (!ptx.vchSig.empty()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "payload signature not empty");
    }

    ptx.vchSig = DecodeBase64(request.params[2].get_str().c_str());

    SetTxPayload(tx, ptx);
    return SignAndSendSpecialTx(tx);
}

void protx_update_service_help(CWallet* const pwallet)
{
    throw std::runtime_error(
            "protx update_service \"proTxHash\" \"ipAndPort\" \"operatorKey\" (\"operatorPayoutAddress\" \"feeSourceAddress\" )\n"
            "\nCreates and sends a ProUpServTx to the network. This will update the IP address\n"
            "of a masternode.\n"
            "If this is done for a masternode that got PoSe-banned, the ProUpServTx will also revive this masternode.\n"
            + HelpRequiringPassphrase(pwallet) + "\n"
            "\nArguments:\n"
            + GetHelpString(1, "proTxHash")
            + GetHelpString(2, "ipAndPort")
            + GetHelpString(3, "operatorKey")
            + GetHelpString(4, "operatorPayoutAddress")
            + GetHelpString(5, "feeSourceAddress") +
            "\nResult:\n"
            "\"txid\"                        (string) The transaction id.\n"
            "\nExamples:\n"
            + HelpExampleCli("protx", "update_service \"0123456701234567012345670123456701234567012345670123456701234567\" \"1.2.3.4:1234\" 5a2e15982e62f1e0b7cf9783c64cf7e3af3f90a52d6c40f6f95d624c0b1621cd")
    );
}

UniValue protx_update_service(const JSONRPCRequest& request)
{
    CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
    if (request.fHelp || (request.params.size() < 4 || request.params.size() > 6))
        protx_update_service_help(pwallet);

    ObserveSafeMode();

    if (!EnsureWalletIsAvailable(pwallet, request.fHelp))
        return NullUniValue;

    EnsureWalletIsUnlocked(pwallet);

    CProUpServTx ptx;
    ptx.nVersion = CProUpServTx::CURRENT_VERSION;
    ptx.proTxHash = ParseHashV(request.params[1], "proTxHash");

    if (!Lookup(request.params[2].get_str().c_str(), ptx.addr, Params().GetDefaultPort(), false)) {
        throw std::runtime_error(strprintf("invalid network address %s", request.params[2].get_str()));
    }

    CBLSSecretKey keyOperator = ParseBLSSecretKey(request.params[3].get_str(), "operatorKey");

    auto dmn = deterministicMNManager->GetListAtChainTip().GetMN(ptx.proTxHash);
    if (!dmn) {
        throw std::runtime_error(strprintf("masternode with proTxHash %s not found", ptx.proTxHash.ToString()));
    }

    if (keyOperator.GetPublicKey() != dmn->pdmnState->pubKeyOperator.Get()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("the operator key does not belong to the registered public key"));
    }

    CMutableTransaction tx;
    tx.nVersion = 3;
    tx.nType = TRANSACTION_PROVIDER_UPDATE_SERVICE;

    // param operatorPayoutAddress
    if (!request.params[4].isNull()) {
        if (request.params[4].get_str().empty()) {
            ptx.scriptOperatorPayout = dmn->pdmnState->scriptOperatorPayout;
        } else {
            CTxDestination payoutDest = DecodeDestination(request.params[4].get_str());
            if (!IsValidDestination(payoutDest)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("invalid operator payout address: %s", request.params[4].get_str()));
            }
            ptx.scriptOperatorPayout = GetScriptForDestination(payoutDest);
        }
    } else {
        ptx.scriptOperatorPayout = dmn->pdmnState->scriptOperatorPayout;
    }

    CTxDestination feeSource;

    // param feeSourceAddress
    if (!request.params[5].isNull()) {
        feeSource = DecodeDestination(request.params[5].get_str());
        if (!IsValidDestination(feeSource))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid BiblePay address: ") + request.params[5].get_str());
    } else {
        if (ptx.scriptOperatorPayout != CScript()) {
            // use operator reward address as default source for fees
            ExtractDestination(ptx.scriptOperatorPayout, feeSource);
        } else {
            // use payout address as default source for fees
            ExtractDestination(dmn->pdmnState->scriptPayout, feeSource);
        }
    }

    FundSpecialTx(pwallet, tx, ptx, feeSource);

    SignSpecialTxPayloadByHash(tx, ptx, keyOperator);
    SetTxPayload(tx, ptx);

    return SignAndSendSpecialTx(tx);
}

void protx_update_registrar_help(CWallet* const pwallet)
{
    throw std::runtime_error(
            "protx update_registrar \"proTxHash\" \"operatorPubKey\" \"votingAddress\" \"payoutAddress\" ( \"feeSourceAddress\" )\n"
            "\nCreates and sends a ProUpRegTx to the network. This will update the operator key, voting key and payout\n"
            "address of the masternode specified by \"proTxHash\".\n"
            "The owner key of the masternode must be known to your wallet.\n"
            + HelpRequiringPassphrase(pwallet) + "\n"
            "\nArguments:\n"
            + GetHelpString(1, "proTxHash")
            + GetHelpString(2, "operatorPubKey_update")
            + GetHelpString(3, "votingAddress_update")
            + GetHelpString(4, "payoutAddress_update")
            + GetHelpString(5, "feeSourceAddress") +
            "\nResult:\n"
            "\"txid\"                        (string) The transaction id.\n"
            "\nExamples:\n"
            + HelpExampleCli("protx", "update_registrar \"0123456701234567012345670123456701234567012345670123456701234567\" \"982eb34b7c7f614f29e5c665bc3605f1beeef85e3395ca12d3be49d2868ecfea5566f11cedfad30c51b2403f2ad95b67\" \"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwG\"")
    );
}

static std::map<std::string, double> mvBlockVersion;
void ScanBlockChainVersion(int nLookback)
{
    mvBlockVersion.clear();
    int nMaxDepth = chainActive.Tip()->nHeight;
    int nMinDepth = (nMaxDepth - nLookback);
    if (nMinDepth < 1) nMinDepth = 1;
    CBlock block;
    CBlockIndex* pblockindex = chainActive.Tip();
 	const Consensus::Params& consensusParams = Params().GetConsensus();
    while (pblockindex->nHeight > nMinDepth)
    {
         if (!pblockindex || !pblockindex->pprev) return;
         pblockindex = pblockindex->pprev;
         if (ReadBlockFromDisk(block, pblockindex, consensusParams)) 
		 {
			std::string sVersion = RoundToString(GetBlockVersion(block.vtx[0]->vout[0].sTxOutMessage), 0);
			mvBlockVersion[sVersion]++;
		 }
    }
}

 UniValue GetVersionReport()
{
	UniValue ret(UniValue::VOBJ);
    //Returns a report of the wallet version that has been solving blocks over the last N blocks
	ScanBlockChainVersion(BLOCKS_PER_DAY);
    std::string sBlockVersion;
    std::string sReport = "Version, Popularity\r\n";
    std::string sRow;
    double dPct = 0;
    ret.push_back(Pair("Version","Popularity,Percent %"));
    double Votes = 0;
	for (auto ii : mvBlockVersion) 
    {
		double Popularity = mvBlockVersion[ii.first];
		Votes += Popularity;
    }
    for (auto ii : mvBlockVersion)
	{
		double Popularity = mvBlockVersion[ii.first];
		sBlockVersion = ii.first;
        if (Popularity > 0)
        {
			sRow = sBlockVersion + "," + RoundToString(Popularity, 0);
            sReport += sRow + "\r\n";
            dPct = Popularity / (Votes+.01) * 100;
            ret.push_back(Pair(sBlockVersion,RoundToString(Popularity, 0) + "; " + RoundToString(dPct, 2) + "%"));
        }
    }
	return ret;
}

UniValue versionreport(const JSONRPCRequest& request)
{
	if (request.fHelp)
	{
		throw std::runtime_error("versionreport:  Shows a list of the versions of software running on users machines ranked by percent.  This information is gleaned from the last 205 mined blocks.");
	}
	UniValue uVersionReport = GetVersionReport();
	return uVersionReport;
}


UniValue protx_update_registrar(const JSONRPCRequest& request)
{
    CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
    if (request.fHelp || (request.params.size() != 5 && request.params.size() != 6)) {
        protx_update_registrar_help(pwallet);
    }

    ObserveSafeMode();

    if (!EnsureWalletIsAvailable(pwallet, request.fHelp))
        return NullUniValue;

    EnsureWalletIsUnlocked(pwallet);

    CProUpRegTx ptx;
    ptx.nVersion = CProUpRegTx::CURRENT_VERSION;
    ptx.proTxHash = ParseHashV(request.params[1], "proTxHash");

    auto dmn = deterministicMNManager->GetListAtChainTip().GetMN(ptx.proTxHash);
    if (!dmn) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("masternode %s not found", ptx.proTxHash.ToString()));
    }
    ptx.pubKeyOperator = dmn->pdmnState->pubKeyOperator.Get();
    ptx.keyIDVoting = dmn->pdmnState->keyIDVoting;
    ptx.scriptPayout = dmn->pdmnState->scriptPayout;

    if (request.params[2].get_str() != "") {
        ptx.pubKeyOperator = ParseBLSPubKey(request.params[2].get_str(), "operator BLS address");
    }
    if (request.params[3].get_str() != "") {
        ptx.keyIDVoting = ParsePubKeyIDFromAddress(request.params[3].get_str(), "voting address");
    }

    CTxDestination payoutDest;
    ExtractDestination(ptx.scriptPayout, payoutDest);
    if (request.params[4].get_str() != "") {
        payoutDest = DecodeDestination(request.params[4].get_str());
        if (!IsValidDestination(payoutDest)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("invalid payout address: %s", request.params[4].get_str()));
        }
        ptx.scriptPayout = GetScriptForDestination(payoutDest);
    }

    CKey keyOwner;
    if (!pwallet->GetKey(dmn->pdmnState->keyIDOwner, keyOwner)) {
        throw std::runtime_error(strprintf("Private key for owner address %s not found in your wallet", EncodeDestination(dmn->pdmnState->keyIDOwner)));
    }

    CMutableTransaction tx;
    tx.nVersion = 3;
    tx.nType = TRANSACTION_PROVIDER_UPDATE_REGISTRAR;

    // make sure we get anough fees added
    ptx.vchSig.resize(65);

    CTxDestination feeSourceDest = payoutDest;
    if (!request.params[5].isNull()) {
        feeSourceDest = DecodeDestination(request.params[5].get_str());
        if (!IsValidDestination(feeSourceDest))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid BiblePay address: ") + request.params[5].get_str());
    }

    FundSpecialTx(pwallet, tx, ptx, feeSourceDest);
    SignSpecialTxPayloadByHash(tx, ptx, keyOwner);
    SetTxPayload(tx, ptx);

    return SignAndSendSpecialTx(tx);
}

void protx_revoke_help(CWallet* const pwallet)
{
    throw std::runtime_error(
            "protx revoke \"proTxHash\" \"operatorKey\" ( reason \"feeSourceAddress\")\n"
            "\nCreates and sends a ProUpRevTx to the network. This will revoke the operator key of the masternode and\n"
            "put it into the PoSe-banned state. It will also set the service field of the masternode\n"
            "to zero. Use this in case your operator key got compromised or you want to stop providing your service\n"
            "to the masternode owner.\n"
            + HelpRequiringPassphrase(pwallet) + "\n"
            "\nArguments:\n"
            + GetHelpString(1, "proTxHash")
            + GetHelpString(2, "operatorKey")
            + GetHelpString(3, "reason")
            + GetHelpString(4, "feeSourceAddress") +
            "\nResult:\n"
            "\"txid\"                        (string) The transaction id.\n"
            "\nExamples:\n"
            + HelpExampleCli("protx", "revoke \"0123456701234567012345670123456701234567012345670123456701234567\" \"072f36a77261cdd5d64c32d97bac417540eddca1d5612f416feb07ff75a8e240\"")
    );
}

UniValue protx_revoke(const JSONRPCRequest& request)
{
    CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
    if (request.fHelp || (request.params.size() < 3 || request.params.size() > 5)) {
        protx_revoke_help(pwallet);
    }

    ObserveSafeMode();

    if (!EnsureWalletIsAvailable(pwallet, request.fHelp))
        return NullUniValue;

    EnsureWalletIsUnlocked(pwallet);

    CProUpRevTx ptx;
    ptx.nVersion = CProUpRevTx::CURRENT_VERSION;
    ptx.proTxHash = ParseHashV(request.params[1], "proTxHash");

    CBLSSecretKey keyOperator = ParseBLSSecretKey(request.params[2].get_str(), "operatorKey");

    if (!request.params[3].isNull()) {
        int32_t nReason = ParseInt32V(request.params[3], "reason");
        if (nReason < 0 || nReason > CProUpRevTx::REASON_LAST) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("invalid reason %d, must be between 0 and %d", nReason, CProUpRevTx::REASON_LAST));
        }
        ptx.nReason = (uint16_t)nReason;
    }

    auto dmn = deterministicMNManager->GetListAtChainTip().GetMN(ptx.proTxHash);
    if (!dmn) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("masternode %s not found", ptx.proTxHash.ToString()));
    }

    if (keyOperator.GetPublicKey() != dmn->pdmnState->pubKeyOperator.Get()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("the operator key does not belong to the registered public key"));
    }

    CMutableTransaction tx;
    tx.nVersion = 3;
    tx.nType = TRANSACTION_PROVIDER_UPDATE_REVOKE;

    if (!request.params[4].isNull()) {
        CTxDestination feeSourceDest = DecodeDestination(request.params[4].get_str());
        if (!IsValidDestination(feeSourceDest))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid BiblePay address: ") + request.params[4].get_str());
        FundSpecialTx(pwallet, tx, ptx, feeSourceDest);
    } else if (dmn->pdmnState->scriptOperatorPayout != CScript()) {
        // Using funds from previousely specified operator payout address
        CTxDestination txDest;
        ExtractDestination(dmn->pdmnState->scriptOperatorPayout, txDest);
        FundSpecialTx(pwallet, tx, ptx, txDest);
    } else if (dmn->pdmnState->scriptPayout != CScript()) {
        // Using funds from previousely specified masternode payout address
        CTxDestination txDest;
        ExtractDestination(dmn->pdmnState->scriptPayout, txDest);
        FundSpecialTx(pwallet, tx, ptx, txDest);
    } else {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "No payout or fee source addresses found, can't revoke");
    }

    SignSpecialTxPayloadByHash(tx, ptx, keyOperator);
    SetTxPayload(tx, ptx);

    return SignAndSendSpecialTx(tx);
}
#endif//ENABLE_WALLET

void protx_list_help()
{
    throw std::runtime_error(
            "protx list (\"type\" \"detailed\" \"height\")\n"
            "\nLists all ProTxs in your wallet or on-chain, depending on the given type.\n"
            "If \"type\" is not specified, it defaults to \"registered\".\n"
            "If \"detailed\" is not specified, it defaults to \"false\" and only the hashes of the ProTx will be returned.\n"
            "If \"height\" is not specified, it defaults to the current chain-tip.\n"
            "\nAvailable types:\n"
            "  registered   - List all ProTx which are registered at the given chain height.\n"
            "                 This will also include ProTx which failed PoSe verfication.\n"
            "  valid        - List only ProTx which are active/valid at the given chain height.\n"
#ifdef ENABLE_WALLET
            "  wallet       - List only ProTx which are found in your wallet at the given chain height.\n"
            "                 This will also include ProTx which failed PoSe verfication.\n"
#endif
    );
}

static bool CheckWalletOwnsKey(CWallet* pwallet, const CKeyID& keyID) {
#ifndef ENABLE_WALLET
    return false;
#else
    if (!pwallet) {
        return false;
    }
    return pwallet->HaveKey(keyID);
#endif
}

static bool CheckWalletOwnsScript(CWallet* pwallet, const CScript& script) {
#ifndef ENABLE_WALLET
    return false;
#else
    if (!pwallet) {
        return false;
    }

    CTxDestination dest;
    if (ExtractDestination(script, dest)) {
        if ((boost::get<CKeyID>(&dest) && pwallet->HaveKey(*boost::get<CKeyID>(&dest))) || (boost::get<CScriptID>(&dest) && pwallet->HaveCScript(*boost::get<CScriptID>(&dest)))) {
            return true;
        }
    }
    return false;
#endif
}

UniValue BuildDMNListEntry(CWallet* pwallet, const CDeterministicMNCPtr& dmn, bool detailed)
{
    if (!detailed) {
        return dmn->proTxHash.ToString();
    }

    UniValue o(UniValue::VOBJ);

    dmn->ToJson(o);

    int confirmations = GetUTXOConfirmations(dmn->collateralOutpoint);
    o.push_back(Pair("confirmations", confirmations));

    bool hasOwnerKey = CheckWalletOwnsKey(pwallet, dmn->pdmnState->keyIDOwner);
    bool hasOperatorKey = false; //CheckWalletOwnsKey(dmn->pdmnState->keyIDOperator);
    bool hasVotingKey = CheckWalletOwnsKey(pwallet, dmn->pdmnState->keyIDVoting);

    bool ownsCollateral = false;
    CTransactionRef collateralTx;
    uint256 tmpHashBlock;
    if (GetTransaction(dmn->collateralOutpoint.hash, collateralTx, Params().GetConsensus(), tmpHashBlock)) {
        ownsCollateral = CheckWalletOwnsScript(pwallet, collateralTx->vout[dmn->collateralOutpoint.n].scriptPubKey);
    }

#ifdef ENABLE_WALLET
    if (pwallet) {
        UniValue walletObj(UniValue::VOBJ);
        walletObj.push_back(Pair("hasOwnerKey", hasOwnerKey));
        walletObj.push_back(Pair("hasOperatorKey", hasOperatorKey));
        walletObj.push_back(Pair("hasVotingKey", hasVotingKey));
        walletObj.push_back(Pair("ownsCollateral", ownsCollateral));
        walletObj.push_back(Pair("ownsPayeeScript", CheckWalletOwnsScript(pwallet, dmn->pdmnState->scriptPayout)));
        walletObj.push_back(Pair("ownsOperatorRewardScript", CheckWalletOwnsScript(pwallet, dmn->pdmnState->scriptOperatorPayout)));
        o.push_back(Pair("wallet", walletObj));
    }
#endif

    auto metaInfo = mmetaman.GetMetaInfo(dmn->proTxHash);
    o.push_back(Pair("metaInfo", metaInfo->ToJson()));

    return o;
}

UniValue protx_list(const JSONRPCRequest& request)
{
    if (request.fHelp) {
        protx_list_help();
    }

#ifdef ENABLE_WALLET
    CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
#else
    CWallet* const pwallet = nullptr;
#endif

    std::string type = "registered";
    if (!request.params[1].isNull()) {
        type = request.params[1].get_str();
    }

    UniValue ret(UniValue::VARR);

    LOCK(cs_main);

    if (type == "wallet") {
        if (!pwallet) {
            throw std::runtime_error("\"protx list wallet\" not supported when wallet is disabled");
        }
#ifdef ENABLE_WALLET
        LOCK2(cs_main, pwallet->cs_wallet);

        if (request.params.size() > 4) {
            protx_list_help();
        }

        bool detailed = !request.params[2].isNull() ? ParseBoolV(request.params[2], "detailed") : false;

        int height = !request.params[3].isNull() ? ParseInt32V(request.params[3], "height") : chainActive.Height();
        if (height < 1 || height > chainActive.Height()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid height specified");
        }

        std::vector<COutPoint> vOutpts;
        pwallet->ListProTxCoins(vOutpts);
        std::set<COutPoint> setOutpts;
        for (const auto& outpt : vOutpts) {
            setOutpts.emplace(outpt);
        }

        CDeterministicMNList mnList = deterministicMNManager->GetListForBlock(chainActive[height]);
        mnList.ForEachMN(false, [&](const CDeterministicMNCPtr& dmn) {
            if (setOutpts.count(dmn->collateralOutpoint) ||
                CheckWalletOwnsKey(pwallet, dmn->pdmnState->keyIDOwner) ||
                CheckWalletOwnsKey(pwallet, dmn->pdmnState->keyIDVoting) ||
                CheckWalletOwnsScript(pwallet, dmn->pdmnState->scriptPayout) ||
                CheckWalletOwnsScript(pwallet, dmn->pdmnState->scriptOperatorPayout)) {
                ret.push_back(BuildDMNListEntry(pwallet, dmn, detailed));
            }
        });
#endif
    } else if (type == "valid" || type == "registered") {
        if (request.params.size() > 4) {
            protx_list_help();
        }

        LOCK(cs_main);

        bool detailed = !request.params[2].isNull() ? ParseBoolV(request.params[2], "detailed") : false;

        int height = !request.params[3].isNull() ? ParseInt32V(request.params[3], "height") : chainActive.Height();
        if (height < 1 || height > chainActive.Height()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid height specified");
        }

        CDeterministicMNList mnList = deterministicMNManager->GetListForBlock(chainActive[height]);
        bool onlyValid = type == "valid";
        mnList.ForEachMN(onlyValid, [&](const CDeterministicMNCPtr& dmn) {
            ret.push_back(BuildDMNListEntry(pwallet, dmn, detailed));
        });
    } else {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid type specified");
    }

    return ret;
}

void protx_info_help()
{
    throw std::runtime_error(
            "protx info \"proTxHash\"\n"
            "\nReturns detailed information about a deterministic masternode.\n"
            "\nArguments:\n"
            + GetHelpString(1, "proTxHash") +
            "\nResult:\n"
            "{                             (json object) Details about a specific deterministic masternode\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("protx", "info \"0123456701234567012345670123456701234567012345670123456701234567\"")
    );
}

UniValue protx_info(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 2) {
        protx_info_help();
    }

#ifdef ENABLE_WALLET
    CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
#else
    CWallet* const pwallet = nullptr;
#endif

    uint256 proTxHash = ParseHashV(request.params[1], "proTxHash");
    auto mnList = deterministicMNManager->GetListAtChainTip();
    auto dmn = mnList.GetMN(proTxHash);
    if (!dmn) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("%s not found", proTxHash.ToString()));
    }
    return BuildDMNListEntry(pwallet, dmn, true);
}

void protx_diff_help()
{
    throw std::runtime_error(
            "protx diff \"baseBlock\" \"block\"\n"
            "\nCalculates a diff between two deterministic masternode lists. The result also contains proof data.\n"
            "\nArguments:\n"
            "1. \"baseBlock\"           (numeric, required) The starting block height.\n"
            "2. \"block\"               (numeric, required) The ending block height.\n"
    );
}

static uint256 ParseBlock(const UniValue& v, std::string strName)
{
    AssertLockHeld(cs_main);

    try {
        return ParseHashV(v, strName);
    } catch (...) {
        int h = ParseInt32V(v, strName);
        if (h < 1 || h > chainActive.Height())
            throw std::runtime_error(strprintf("%s must be a block hash or chain height and not %s", strName, v.getValStr()));
        return *chainActive[h]->phashBlock;
    }
}

UniValue protx_diff(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 3) {
        protx_diff_help();
    }

    LOCK(cs_main);
    uint256 baseBlockHash = ParseBlock(request.params[1], "baseBlock");
    uint256 blockHash = ParseBlock(request.params[2], "block");

    CSimplifiedMNListDiff mnListDiff;
    std::string strError;
    if (!BuildSimplifiedMNListDiff(baseBlockHash, blockHash, mnListDiff, strError)) {
        throw std::runtime_error(strError);
    }

    UniValue ret;
    mnListDiff.ToJson(ret);
    return ret;
}

[[ noreturn ]] void protx_help()
{
    throw std::runtime_error(
            "protx \"command\" ...\n"
            "Set of commands to execute ProTx related actions.\n"
            "To get help on individual commands, use \"help protx command\".\n"
            "\nArguments:\n"
            "1. \"command\"        (string, required) The command to execute\n"
            "\nAvailable commands:\n"
#ifdef ENABLE_WALLET
            "  register          - Create and send ProTx to network\n"
            "  register_fund     - Fund, create and send ProTx to network\n"
            "  register_prepare  - Create an unsigned ProTx\n"
            "  register_submit   - Sign and submit a ProTx\n"
#endif
            "  list              - List ProTxs\n"
            "  info              - Return information about a ProTx\n"
#ifdef ENABLE_WALLET
            "  update_service    - Create and send ProUpServTx to network\n"
            "  update_registrar  - Create and send ProUpRegTx to network\n"
            "  revoke            - Create and send ProUpRevTx to network\n"
#endif
            "  diff              - Calculate a diff and a proof between two masternode lists\n"
    );
}

UniValue protx(const JSONRPCRequest& request)
{
    if (request.fHelp && request.params.empty()) {
        protx_help();
    }

    std::string command;
    if (!request.params[0].isNull()) {
        command = request.params[0].get_str();
    }

#ifdef ENABLE_WALLET
    if (command == "register" || command == "register_fund" || command == "register_prepare") {
        return protx_register(request);
    } else if (command == "register_submit") {
        return protx_register_submit(request);
    } else if (command == "update_service") {
        return protx_update_service(request);
    } else if (command == "update_registrar") {
        return protx_update_registrar(request);
    } else if (command == "revoke") {
        return protx_revoke(request);
    } else
#endif
    if (command == "list") {
        return protx_list(request);
    } else if (command == "info") {
        return protx_info(request);
    } else if (command == "diff") {
        return protx_diff(request);
    } else {
        protx_help();
    }
}

void bls_generate_help()
{
    throw std::runtime_error(
            "bls generate\n"
            "\nReturns a BLS secret/public key pair.\n"
            "\nResult:\n"
            "{\n"
            "  \"secret\": \"xxxx\",        (string) BLS secret key\n"
            "  \"public\": \"xxxx\",        (string) BLS public key\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("bls generate", "")
    );
}

UniValue bls_generate(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1) {
        bls_generate_help();
    }

    CBLSSecretKey sk;
    sk.MakeNewKey();

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("secret", sk.ToString()));
    ret.push_back(Pair("public", sk.GetPublicKey().ToString()));
    return ret;
}

void bls_fromsecret_help()
{
    throw std::runtime_error(
            "bls fromsecret \"secret\"\n"
            "\nParses a BLS secret key and returns the secret/public key pair.\n"
            "\nArguments:\n"
            "1. \"secret\"                (string, required) The BLS secret key\n"
            "\nResult:\n"
            "{\n"
            "  \"secret\": \"xxxx\",        (string) BLS secret key\n"
            "  \"public\": \"xxxx\",        (string) BLS public key\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("bls fromsecret", "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f")
    );
}

UniValue bls_fromsecret(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 2) {
        bls_fromsecret_help();
    }

    CBLSSecretKey sk;
    if (!sk.SetHexStr(request.params[1].get_str())) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Secret key must be a valid hex string of length %d", sk.SerSize*2));
    }

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("secret", sk.ToString()));
    ret.push_back(Pair("public", sk.GetPublicKey().ToString()));
    return ret;
}

[[ noreturn ]] void bls_help()
{
    throw std::runtime_error(
            "bls \"command\" ...\n"
            "Set of commands to execute BLS related actions.\n"
            "To get help on individual commands, use \"help bls command\".\n"
            "\nArguments:\n"
            "1. \"command\"        (string, required) The command to execute\n"
            "\nAvailable commands:\n"
            "  generate          - Create a BLS secret/public key pair\n"
            "  fromsecret        - Parse a BLS secret key and return the secret/public key pair\n"
            );
}

bool AcquireWallet3()
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

UniValue _bls(const JSONRPCRequest& request)
{
    if (request.fHelp && request.params.empty()) {
        bls_help();
    }

    std::string command;
    if (!request.params[0].isNull()) {
        command = request.params[0].get_str();
    }

    if (command == "generate") {
        return bls_generate(request);
    } else if (command == "fromsecret") {
        return bls_fromsecret(request);
    } else {
        bls_help();
    }
}

UniValue trackdashpay(const JSONRPCRequest& request)
{
	if (request.fHelp)
		throw std::runtime_error(
		"trackdashpay txid"
		"\nThis command displays the status of a dashpay transaction that is still in process. "
		"\nExample: trackdashpay txid");

	if (request.params.size() != 1)
			throw std::runtime_error("You must specify the txid.");
	std::string sError;
	std::string sTXID = request.params[0].get_str();
	UniValue results(UniValue::VOBJ);
	std::string sXML = "<txid>" + sTXID + "</txid>";
	DACResult b = DSQL_ReadOnlyQuery("BMS/TrackDashPay", sXML);
	std::string sResponse = ExtractXML(b.Response, "<response>", "</response>");
	std::string sUpdated = ExtractXML(b.Response, "<updated>", "</updated>");
	std::string sDashTXID = ExtractXML(b.Response, "<dashtxid>", "</dashtxid>");
	results.push_back(Pair("Response", sResponse));
	if (!sUpdated.empty())
		results.push_back(Pair("Updated", sUpdated));
	if (!sDashTXID.empty())
		results.push_back(Pair("dash-txid", sDashTXID));

	return results;	
}

UniValue dashpay(const JSONRPCRequest& request)
{
	if (request.fHelp)
		throw std::runtime_error(
		"dashpay address amount_in_DASH [0=test/1=authorize]"
		"\nThis command sends an amount denominated in DASH to the DASH receive address via InstantSend. "
		"\nNOTE: You can expiriment to find the right amount by executing this command in test mode. "
		"\nExample:  dashpay dash_recv_address 1.23 0");

	if (request.params.size() != 3)
			throw std::runtime_error("You must specify dashpay dash_recv_address Dash_Amount 0=test/1=authorize. ");
	std::string sError;
	UniValue results(UniValue::VOBJ);
	
	std::string sCPK = DefaultRecAddress("Christian-Public-Key");  // We use this address to send the refund if the IX is rejected

	std::string sDashAddress = request.params[0].get_str();
	double nDashAmount = cdbl(request.params[1].get_str(), 4);
	double nMode = cdbl(request.params[2].get_str(), 0);
	if (nMode != 0 && nMode != 1)
	{
		throw std::runtime_error("Sorry, the mode must be 0 or 1.  0=Test.  1=Authorize.");
	}

	double nCoinUSDPrice = GetBBPUSDPrice();
	double dDASH = GetCryptoPrice("dash"); // Dash->BTC price
	double dBTC = GetCryptoPrice("btc");
		
	double nDashPriceUSD = dBTC * dDASH;  // Dash price in USD

	if (nCoinUSDPrice < .00001 || nDashPriceUSD < 1)
	{
		sError = CURRENCY_NAME + " Price too low to use feature.  Price must be above .00001USD/" + CURRENCY_NAME + ".  Dash price must be above 1.0/USD. ";
		nCoinUSDPrice = .00001;
	}

	double nAmountUSD = nDashPriceUSD * nDashAmount;
	results.push_back(Pair("DASH/USD_Price", nDashPriceUSD));
	
	if (nAmountUSD < .99)
	{
		sError += "You must enter a USD value greater than or equal to $1.00 to use this feature. ";
		nAmountUSD = .01;
	}

	if (nDashAmount < .0001)
	{
		sError += "Dash amount must be >= .0001";
	}

	if (nMode == 0)
	{
		sError += "Running in test mode. ";
	}

	double nCoinAmount = cdbl(RoundToString(nAmountUSD / nCoinUSDPrice, 2), 2);
	results.push_back(Pair(CURRENCY_NAME + "/USD_Price", nCoinUSDPrice));
	results.push_back(Pair("USD Amount Required", nAmountUSD));

	std::string sXML = "<cpk>" + sCPK + "</cpk><dashaddress>" + sDashAddress 
		+ "</dashaddress><dashamount>" + RoundToString(nDashAmount, 8) + "</dashamount><" + GetLcaseTicker() + "amount>" + RoundToString(nCoinAmount, 8) + "</" + GetLcaseTicker() + "amount>";
	
	// Verify this transaction will not fail first
	
	DACResult b = DSQL_ReadOnlyQuery("BMS/DashPay", sXML);
	std::string sHealth = ExtractXML(b.Response, "<health>", "</health>");
	if (!sHealth.empty() && sHealth != "UP")
		results.push_back(Pair("health", sHealth));
	if (Contains(sHealth, "DOWN"))
	{
		nMode = 0;
		sError += sHealth;
	}
	// Verify dry run results
	std::string sErrorDryRun = ExtractXML(b.Response, "<error>", "</error>");
	if (!sErrorDryRun.empty())
	{
		results.push_back(Pair("Error", sErrorDryRun));
	}
	std::string sWarning = ExtractXML(b.Response, "<warning>", "</warning>");
	if (!sWarning.empty())
		results.push_back(Pair("Warning", sWarning));

	std::string sDashPayAddress = GetSporkValue("DashPayAddress");
	const CChainParams& chainparams = Params();

	if (sDashAddress.empty())
		throw std::runtime_error("Dash Destination address must be populated.");

	if (!ValidateAddress2(sDashAddress) || sDashAddress.length() != 34 || sDashAddress.substr(0,1) != "X")
		throw std::runtime_error("Sorry, DashPay destination address is invalid for this IX transaction.");
	
	bool fSubtractFee = false;
	bool fInstantSend = true;
	CWalletTx wtx;
	bool fSent = false;
	if (sErrorDryRun.empty() && sError.empty() && nMode == 1)
	{
		// Set up an atomic transaction here
		CScript spkDest = GetScriptForDestination(DecodeDestination(sDashAddress));
			
		fSent = RPCSendMoney(sError, spkDest, nCoinAmount * COIN, fSubtractFee, wtx, fInstantSend, sXML);
		if (fSent)
		{
			sXML += "<txid>" + wtx.GetHash().GetHex() + "</txid>";
			b = DSQL_ReadOnlyQuery("BMS/DashPay", sXML);
			std::string sDashPayResponse = ExtractXML(b.Response,"<response>", "</response>");
			std::string sWarnings = ExtractXML(b.Response, "<warning>", "</warning>");
			if (!sWarnings.empty())
				results.push_back(Pair("Warning_1", sWarnings));
			std::string sDashPayError = ExtractXML(b.Response, "<error>", "</error>");
			if (!sDashPayError.empty())
				results.push_back(Pair("Error_1", sDashPayError));
			results.push_back(Pair("dashpay-txid", sDashPayResponse));
		}
	}
	
	results.push_back(Pair(CURRENCY_NAME + " Amount being spent", nCoinAmount));
	if (!sError.empty())
		results.push_back(Pair("Errors", sError));

	if (fSent && nMode == 0)
	{
		results.push_back(Pair(CURRENCY_NAME + "-txid", wtx.GetHash().GetHex()));
	}
	return results;
}


UniValue faucetcode(const JSONRPCRequest& request)
{
	if (request.fHelp)
		throw std::runtime_error(
		"faucetcode\nProvides a code to allow you to claim a faucet reward.");

	std::string sCode = GenerateFaucetCode();
	UniValue results(UniValue::VOBJ);
	results.push_back(Pair("Code", sCode));
	return results;
}

UniValue bookname(const JSONRPCRequest& request)
{
	if (request.fHelp || request.params.size() != 1)	
	{
		throw std::runtime_error("bookname short_book_name:  Shows the long Bible Book Name corresponding to the short name.  IE: bookname JDE.");
	}
    UniValue results(UniValue::VOBJ);
	std::string sBookName = request.params[0].get_str();
	std::string sReversed = GetBookByName(sBookName);
	results.push_back(Pair(sBookName, sReversed));
	return results;
}

 UniValue books(const JSONRPCRequest& request)
{
	if (request.fHelp)	
	{
		throw std::runtime_error("books:  Shows the book names of the Bible.");
	}
    UniValue results(UniValue::VOBJ);
	for (int i = 0; i <= BIBLE_BOOKS_COUNT; i++)
	{
		std::string sBookName = GetBook(i);
		std::string sReversed = GetBookByName(sBookName);
		results.push_back(Pair(sBookName, sReversed));
	}
	return results;
}

UniValue sendgscc(const JSONRPCRequest& request)
{
	if (request.fHelp)
		throw std::runtime_error(
		"sendgscc"
		"\nSends a generic smart contract campaign transmission."
		"\nYou must specify sendgscc campaign_name [optional:diary_entry] : IE 'exec sendgscc healing [\"prayed for Jane Doe who had broken ribs, this happened\"].");
	if (request.params.size() < 1 || request.params.size() > 3)
		throw std::runtime_error("You must specify sendgscc campaign_name [foundation_donation_amount] [optional:diary_entry] : IE 'exec sendgscc healing [\"prayed for Jane Doe who had broken ribs, this happened\"].");
	std::string sDiary;
	std::string sCampaignName;
	if (request.params.size() > 0)
	{
		sCampaignName = request.params[0].get_str();
	}
	if (request.params.size() > 1)
	{
		sDiary = request.params[1].get_str();
		if (sDiary.length() < 10)
			throw std::runtime_error("Diary entry incomplete (must be 10 chars or more).");
	}
	WriteCache("gsc", "errors", "", GetAdjustedTime());
	std::string sError;
	std::string sWarning;
	std::string TXID_OUT;
    UniValue results(UniValue::VOBJ);
	bool fCreated = CreateGSCTransmission("", "", true, sDiary, sError, sCampaignName, sWarning, TXID_OUT);

	if (!sError.empty())
		results.push_back(Pair("Error!", sError));
	std::string sFullError = ReadCache("gsc", "errors");
	if (!sFullError.empty())
		results.push_back(Pair("Error!", sFullError));
	if (!sWarning.empty())
		results.push_back(Pair("Warning!", sWarning));
	if (fCreated)
		results.push_back(Pair("Results", fCreated));
 	return results;
}

UniValue datalist(const JSONRPCRequest& request)
{
	if (request.fHelp || (request.params.size() != 1 && request.params.size() != 2))
			throw std::runtime_error("You must specify type: IE 'datalist PRAYER'.  Optionally you may enter a lookback period in days: IE 'exec datalist PRAYER 30'.");
	std::string sType = request.params[0].get_str();
	double dDays = 30;
	if (request.params.size() > 1)
		dDays = cdbl(request.params[1].get_str(),0);
	int iSpecificEntry = 0;
	std::string sEntry;
	UniValue aDataList = GetDataList(sType, (int)dDays, iSpecificEntry, "", sEntry);
	return aDataList;
}

UniValue getpobhhash(const JSONRPCRequest& request)
{
	if (request.fHelp || request.params.size() != 1)
		throw std::runtime_error("getpobhhash: returns a pobh hash for a given x11 hash");
	std::string sInput = request.params[0].get_str();
	uint256 hSource = uint256S("0x" + sInput);
	uint256 h = BibleHashDebug(hSource, 0);
    UniValue results(UniValue::VOBJ);
	results.push_back(Pair("inhash", hSource.GetHex()));
	results.push_back(Pair("outhash", h.GetHex()));
	return results;
}

std::string GetSymbolFromAddress(std::string sAddress)
{
	// Return the cryptocurrency symbol from a base58 address:
	std::string sSymbol;
	if (sAddress.empty() || sAddress.length() < 34)
	{
		return "N/A";
	}
	std::string sPrefix = sAddress.substr(0, 1);
	std::string sPrefixEth = sAddress.substr(0, 2);
	boost::to_upper(sPrefixEth);

	if (sPrefix == "B")
	{
		sSymbol = "BBP";
	}
	else if (sPrefix == "X")
	{
		sSymbol = "DASH";
	}
	else if (sPrefix == "M" || sPrefix == "L")
	{
		sSymbol = "LTC";
	}
	else if (sPrefix ==  "D")
	{
		sSymbol = "DOGE";
	}
	else if (sPrefix == "1")
	{
		sSymbol = "BTC";
	}
	else if (sPrefixEth == "0X" && sAddress.length() == 42)
	{
		sSymbol = "ETH";
	}
	else
	{
		sSymbol = "N/A";
	}
	return sSymbol;
}

std::string GetHighYieldWarning(int nDays)
{
	if (nDays < 31)
		return "";
	std::string sNarr = "When you promise to lock up your coins for an additional " + RoundToString(nDays, 0) + " days for an additional high yield reward, "
		+ " you hereby AGREE that you may be PENALIZED by 200% of the TOTAL reward amount if you decide to spend your UTXO early!  "
		+ " Use caution when selecting the high risk option.  This condition can cause you to lose up to 100% of your staked collateral to network fees.  "
		+ " If you do not fulfill your obligation, your original UTXO will have a certain amount of burn fees deducted from it, and these fees will be burned.  "
		+ " You will be UNABLE to send this UTXO back to your own address for spending.  EXAMPLE:  You have a 100,000 BBP coin.  You decide to lock it for an extra high risk reward for 30 days for a 60% DWU.  On the 15th day you try to spend the coin (breaking your obligation).  You will be penalized 60%*2=120% of your original UTXO, which is MORE THAN THE ORIGINAL VALUE.  Therefore due to our 99% cap, we will charge you a 99% BURN FEE to spend the coin.  Note that our conservative UTXO staking option does not carry this risk.  If you want conservative staking, select 0 days for the commitment length.  Thank you for using BIBLEPAY.  ";
	return sNarr;
}

UniValue easystake(const JSONRPCRequest& request)
{
	// Jan 20, 2021 - Easy Stake - R ANDREWS - BIBLEPAY
		
	std::string sHelp = "You must specify easystake minimum_bbp_amount foreign_address foreign_amount commitment_days[0=conservative,30-365=high-risk] 0=dry_run/1=real  [Optional `Ticker`=Override Foreign Ticker]";
	std::string sError;
	if (request.fHelp || (request.params.size() != 5 && request.params.size() != 6))
		throw std::runtime_error(sHelp.c_str());
	
	std::string sBBPAddress;
	std::string sTickerOverride;
	CAmount nBBPReturnAmount = 0;
	AcquireWallet3();
	
	double nMin = cdbl(request.params[0].getValStr(), 2);
	std::string sForeignAddress = request.params[1].getValStr();
	double nForeignAmount = cdbl(request.params[2].getValStr(), 8);
	double nDays = cdbl(request.params[3].getValStr(), 2);
	bool fDryRun = cdbl(request.params[4].getValStr(), 0) == 0;
	if (request.params.size() > 5)
		sTickerOverride = request.params[5].getValStr();
	
	std::string sBBPUTXO = pwalletpog->GetBestUTXO(nMin * COIN, .01, sBBPAddress, nBBPReturnAmount);
	if (sBBPUTXO.empty())
	{
		// They dont have one with 1+ day of age; try one without age
		LogPrintf("\nEasyStake::NO_AGE We cant find a UTXO with age, so we will try one without age. %f", 10001);
		sBBPUTXO = pwalletpog->GetBestUTXO(nMin * COIN, .01, sBBPAddress, nBBPReturnAmount);
	}
	if (sBBPUTXO.empty())
	{
		sError = "Unable to find a BBP UTXO greater than " + RoundToString(nMin, 2) + ".";
		throw std::runtime_error(sError);
	}
	UniValue results(UniValue::VOBJ);

	results.push_back(Pair("BBP UTXO", sBBPUTXO));
	results.push_back(Pair("BBP Address", sBBPAddress));
	results.push_back(Pair("BBP Amount", (double)nBBPReturnAmount/COIN));
	std::string sForeignTicker = GetSymbolFromAddress(sForeignAddress);
	if (!sTickerOverride.empty())
		sForeignTicker = sTickerOverride;
	boost::to_upper(sForeignTicker);
		
	bool fReturnFirst = (nForeignAmount == -1);
	// Find the UTXO by amount
	SimpleUTXO s = QueryUTXO(GetAdjustedTime(), nForeignAmount, sForeignTicker, sForeignAddress, "", 0, sError, fReturnFirst);
	results.push_back(Pair("Foreign Symbol", sForeignTicker));

	results.push_back(Pair("Foreign Amount", (double)s.nAmount/COIN));
	results.push_back(Pair("Foreign UTXO", s.TXID));
	results.push_back(Pair("Foreign Ordinal", s.nOrdinal));
	
	int nStakeCount = sForeignTicker.empty() ? 1 : 2;
	double nDWU = CalculateUTXOReward(nStakeCount, nDays);
	PriceQuote p = GetPriceQuote(sForeignTicker, nBBPReturnAmount, s.nAmount);

	results.push_back(Pair("Foreign Value USD", p.nForeignValueUSD));
	results.push_back(Pair("BBP Value USD", p.nBBPValueUSD));
	results.push_back(Pair("DWU", nDWU * 100));
	std::string sWarning = GetHighYieldWarning(nDays);
	if (!sWarning.empty())
		results.push_back(Pair("!WARNING!", sWarning));

	results.push_back(Pair("Foreign Address", sForeignAddress));

	double nPin = AddressToPin(sForeignAddress);
	results.push_back(Pair("Pin", nPin));
	bool fValid = CompareMask2(s.nAmount, nPin);
	if (!fValid)
	{
		if (s.nAmount == 0)
		{
			sError = "Unable to find foreign UTXO.  (Ensure it is at least 1 block deep in the foreign chain). ";
		}
		else
		{
			sError = "The pin is not valid for this receive address. [Address=" + sForeignAddress + "]";
		}
	}
	results.push_back(Pair("pin_valid", fValid));
	results.push_back(Pair("Error", sError));
	std::string sTXID;
	UTXOStake ds;

	std::string sBBPSig = pwalletpog->SignBBPUTXO(sBBPUTXO, sError);
	if (!sError.empty())
	{
		results.push_back(Pair("BBP Signing Error", sError));
		return results;
	}

	// At this point if it is not a dry run; send it
	if (!fDryRun && sError.empty())
	{
		LockUTXOStakes();
		std::string sCPK = DefaultRecAddress("Christian-Public-Key");
		std::string sForeignUTXO = s.TXID + "-" + RoundToString(s.nOrdinal, 0);
		bool fSent = SendUTXOStake(0, sForeignTicker, sTXID, sError, sBBPAddress, sBBPUTXO, sForeignAddress, sForeignUTXO, sBBPSig, "", sCPK, false, ds, nDays);
		if (!fSent || !sError.empty())
		{
			results.push_back(Pair("Error (Not Sent)", sError));
		}
		else
		{
			ds = GetUTXOStakeByUTXO(sBBPUTXO);
			LockUTXOStakes();

			if (!sForeignTicker.empty())
			{
				results.push_back(Pair("Foreign Ticker", sForeignTicker));
				results.push_back(Pair("Foreign Value USD", ds.nForeignValueUSD)); 
				results.push_back(Pair("Foreign Amount", (double)ds.nForeignAmount/COIN));
			}
			results.push_back(Pair("UTXO Value", ds.nValue));
			results.push_back(Pair("Results", "The UTXO Stake Contract was created successfully.  Thank you for using BIBLEPAY. "));
			results.push_back(Pair("TXID", sTXID));
		}
	}
	else
	{
		results.push_back(Pair("Dry Run Mode", fDryRun));
	}
	return results;
}

UniValue easybbpstake(const JSONRPCRequest& request)
{
	std::string sHelp = "You must specify easybbpstake minimum_bbp_amount commitment_days[0=conservative,30-365=high_risk] 0=dry_run/1=real";
	std::string sError;
	if (request.fHelp || (request.params.size() != 3))
		throw std::runtime_error(sHelp.c_str());
	double nMin = cdbl(request.params[0].getValStr(), 2);
	double nDays = cdbl(request.params[1].getValStr(), 2);
	if (nDays > 0 && (nDays < 30 || nDays > 365))
	{
		sError = "Commitment days must be between 30 and 365 if specified. ";
		throw std::runtime_error(sError);
	}
	bool fDryRun = cdbl(request.params[2].getValStr(), 0) == 0;

	std::string sBBPAddress;
	CAmount nBBPReturnAmount = 0;
	bool fWallet = AcquireWallet3();

	std::string sBBPUTXO = pwalletpog->GetBestUTXO(nMin * COIN, .01, sBBPAddress, nBBPReturnAmount);
	
	if (sBBPUTXO.empty())
	{
		sError = "Unable to find a BBP UTXO greater than " + RoundToString(nMin, 2) + ".";
		throw std::runtime_error(sError);
	}
	UniValue results(UniValue::VOBJ);
	results.push_back(Pair("BBP UTXO", sBBPUTXO));
	results.push_back(Pair("BBP Address", sBBPAddress));
	results.push_back(Pair("BBP Amount", (double)nBBPReturnAmount/COIN));
	results.push_back(Pair("Commitment Days", nDays));
	double nDWU = CalculateUTXOReward(1, nDays);
	results.push_back(Pair("DWU", nDWU * 100));
	std::string sWarning = GetHighYieldWarning(nDays);
	if (!sWarning.empty())
		results.push_back(Pair("!WARNING!", sWarning));

	PriceQuote p = GetPriceQuote("bbp", nBBPReturnAmount, 0);

	results.push_back(Pair("BBP Value USD", p.nBBPValueUSD));

	std::string sTXID;
	UTXOStake ds;
	std::string sBBPSig = pwalletpog->SignBBPUTXO(sBBPUTXO, sError);
	if (!sError.empty())
	{
		results.push_back(Pair("BBP Signing Error", sError));
		return results;
	}

	// At this point if it is not a dry run; send it
	if (!fDryRun && sError.empty())
	{
		LockUTXOStakes();
		std::string sCPK = DefaultRecAddress("Christian-Public-Key");
		std::string sForeignUTXO = "";
		bool fSent = SendUTXOStake(0, "", sTXID, sError, sBBPAddress, sBBPUTXO, "", sForeignUTXO, sBBPSig, "", sCPK, false, ds, nDays);
		if (!fSent || !sError.empty())
		{
			results.push_back(Pair("Error (Not Sent)", sError));
		}
		else
		{
			ds = GetUTXOStakeByUTXO(sBBPUTXO);
			LockUTXOStakes();

			results.push_back(Pair("UTXO Value", ds.nValue));
			results.push_back(Pair("Results", "The UTXO Stake Contract was created successfully.  Thank you for using BIBLEPAY. "));
			results.push_back(Pair("TXID", sTXID));
		}
	}
	else
	{
		results.push_back(Pair("Dry Run Mode", fDryRun));
	}

	return results;
}

UniValue utxostake(const JSONRPCRequest& request)
{
	// UTXO Staking and UTXO mining
	// This allows you to lock up Y amount of a foreign currency + Z amount of BBP in a contract, and receive daily rewards on this contract.

	// Example:
	// utxostake BBP_UTXO-ORDINAL DASH_UTXO+ORDINAL DASH_SIGNATURE 0=test/1=authorize
	// To create a signature, from DASH type:  signmessage dash_public_key DASH-UTXO-ORDINAL <enter>.  Copy the Dash signature into the BiblePay 'dashstake' command.

	const Consensus::Params& consensusParams = Params().GetConsensus();
	std::string sHelp = "You must specify utxostake BBP_ADDRESS BBP_UTXO-ORDINAL I_AGREE=Authorize [Optional: FOREIGN_TICKER FOREIGN_ADDRESS FOREIGN_UTXO-ORDINAL FOREIGN_CURRENCY_SIGNATURE]\n" + GetHowey(true, false);
	if (request.fHelp || (request.params.size() != 7 && request.params.size() != 3))
		throw std::runtime_error(sHelp.c_str());
		
	std::string sCPK = DefaultRecAddress("Christian-Public-Key");
	std::string sBBPAddress = request.params[0].get_str();
	std::string sBBPUTXO = request.params[1].get_str();
	std::string sAuth = request.params[2].get_str();
	std::string sForeignTicker, sForeignAddress, sForeignUTXO, sForeignSig;

	if (request.params.size() > 3)
	{
		sForeignTicker = request.params[3].get_str();
		sForeignAddress = request.params[4].get_str();
		sForeignUTXO = request.params[5].get_str();
		sForeignSig = request.params[6].get_str();
	}

	std::string sError;
	std::string sBBPSig = pwalletpog->SignBBPUTXO(sBBPUTXO, sError);
	UniValue results(UniValue::VOBJ);

	if (!sError.empty())
	{
		results.push_back(Pair("BBP Signing Error", sError));
		return results;
	}

	std::string sTXID;
	UTXOStake ds;
	if (sAuth == "I_AGREE")
	{
		LockUTXOStakes();

		bool fSent = SendUTXOStake(0, sForeignTicker, sTXID, sError, sBBPAddress, sBBPUTXO, sForeignAddress, sForeignUTXO, sBBPSig, sForeignSig, sCPK, false, ds, 0);
		if (!fSent || !sError.empty())
		{
			results.push_back(Pair("Error (Not Sent)", sError));
		}
		else
		{
			ds = GetUTXOStakeByUTXO(sBBPUTXO);
			results.push_back(Pair("BBP Value USD", ds.nBBPValueUSD));
			results.push_back(Pair("BBP Amount", (double)ds.nBBPAmount/COIN));

			if (!sForeignTicker.empty())
			{
				results.push_back(Pair("Foreign Ticker", sForeignTicker));
				results.push_back(Pair("Foreign Value USD", ds.nForeignValueUSD)); //
				results.push_back(Pair("Foreign Amount", (double)ds.nForeignAmount/COIN));
			}
			results.push_back(Pair("UTXO Value", ds.nValue));
			results.push_back(Pair("Results", "The UTXO Stake Contract was created successfully.  Thank you for using BIBLEPAY. "));
			results.push_back(Pair("TXID", sTXID));
		}
	}
	else
	{
		throw std::runtime_error(sHelp.c_str());
	}
	return results;
}

UniValue listexpenses(const JSONRPCRequest& request)
{
	std::string sHelp = "You must specify listexpenses max_days";
	UniValue results(UniValue::VOBJ);

	if (request.fHelp || (request.params.size() != 1))
		throw std::runtime_error(sHelp.c_str());
	double nMaxDays = cdbl(request.params[0].get_str(), 0);
	std::map<std::string, Orphan> mapOrphans;
	std::map<std::string, Expense> mapExpenses;
	std::map<std::string, double> mapDAC = DACEngine(mapOrphans, mapExpenses);
	double nTotalExp = 0;
	for (auto expense : mapExpenses)
	{
		int nType = expense.second.nUSDAmount > 0 ? 0 : 1;
		std::string sNarr = nType == 0 ? "Exp" : "Rev";
		int nElapsedDays  = GetAdjustedTime() - expense.second.nTime / 86400;
		if (nElapsedDays < nMaxDays)
		{
			std::string sRow = sNarr + " USD: " + RoundToString(expense.second.nUSDAmount, 2) + " BBP: " + RoundToString(expense.second.nBBPAmount, 2);
			std::string sKey = expense.second.Added + "-" + expense.second.Charity; 
			results.push_back(Pair(sKey, sRow));
			if (expense.second.nUSDAmount > 0)
				nTotalExp += expense.second.nUSDAmount;
		}

	}
	results.push_back(Pair("Total USD Expense", nTotalExp));
	return results;
}

UniValue getpin(const JSONRPCRequest& request)
{
	if (request.fHelp || (request.params.size() != 1))
		throw std::runtime_error("You must specify getpin receive_address.  \r\nYou may use any base58 receiving address such as BBP, BTC, DOGE, LTC, or ERC-20: ETH, etc. \r\n");

	UniValue results(UniValue::VOBJ);
	std::string s2 = request.params[0].get_str();
	if (s2.length() != 34 && s2.length() != 42)
	{
		throw std::runtime_error("Address must be 34 characters long (BTC/ALTCOIN) or 42 characters long (ETH). ");
	}
	double nPin = AddressToPin(s2);
	results.push_back(Pair("pin", nPin));
	return results;
}

UniValue give(const JSONRPCRequest& request)
{
	if (request.fHelp || (request.params.size() != 1))
		throw std::runtime_error("You must specify give amount.  \r\nThis amount will be donated to our decentralized autonomous charity automatically, and auto distributed across our orphans.  To see the current percentages and orphans type exec dacengine.\r\n");
	UniValue results(UniValue::VOBJ);

	double dGive = cdbl(request.params[0].get_str(), 2);
    
	int nChangePosRet = -1;
	bool fSubtractFeeFromAmount = true;
	std::vector<CRecipient> vecSend;
	std::map<std::string, Orphan> mapOrphan;
	std::map<std::string, Expense> mapExpenses;
	std::map<std::string, double> mapDAC = DACEngine(mapOrphan, mapExpenses);
	for (auto dacAllocation : mapDAC)
	{
		std::string sAllocatedCharity = dacAllocation.first;
		double nPct = dacAllocation.second;
		if (nPct > 0 && nPct <= 1)
		{
			// ToDo:  Report to the user the % and Charity Name as we iterate through the gift, and show which orphans were affected by this persons generosity
			results.push_back(Pair("Allocated Charity", sAllocatedCharity));
			CScript spkCharity = GetScriptForDestination(DecodeDestination(sAllocatedCharity));
			CAmount nAmount = dGive * nPct * COIN;
			results.push_back(Pair("Amount", (double)nAmount/COIN));
			CRecipient recipient = {spkCharity, nAmount, false, fSubtractFeeFromAmount};
			vecSend.push_back(recipient);
		}
	}
	CAmount nFeeRequired = 0;
	bool fUseInstantSend = false;
	std::string sError;
	CValidationState state;
	AcquireWallet3();

	CReserveKey reserveKey(pwalletpog);
	CWalletTx wtx;
	std::string sGiftXML = "<gift><amount>" + RoundToString(dGive, 2) + "</amount></gift>";
    CCoinControl coinControl;
    
	bool fCreated = pwalletpog->CreateTransaction(vecSend, wtx, reserveKey, nFeeRequired, nChangePosRet, sError, coinControl,
		true, 0, sGiftXML);
	if (!fCreated)    
	{
		results.push_back(Pair("Error", sError));
	}
	else
	{
		if (!pwalletpog->CommitTransaction(wtx, reserveKey, g_connman.get(), state))
		{
			throw JSONRPCError(RPC_WALLET_ERROR, "Transaction commit failed");
		}
	}
	results.push_back(Pair("txid", wtx.GetHash().GetHex()));
	if (fCreated)
	{
		results.push_back(Pair("Thank You", "May your family be blessed with the richest blessings of Abraham, Isaac and Jacob. "));
	}
	return results;
}

UniValue listdacdonations(const JSONRPCRequest& request)
{
	if (request.fHelp || (request.params.size() != 1))
		throw std::runtime_error("You must specify listdacdonations daylimit.");
	double dDayLimit = cdbl(request.params[0].get_str(), 0);
	UniValue results(UniValue::VOBJ);

	std::vector<DACResult> d = GetDataListVector("gift", dDayLimit);
	CAmount nTotal = 0;
	for (int i = 0; i < d.size(); i++)
	{
		CAmount nAmount = cdbl(d[i].Response, 2) * COIN;
		nTotal += nAmount;
		results.push_back(Pair(d[i].PrimaryKey, (double)nAmount/COIN));
	}
	results.push_back(Pair("Total", (double)nTotal/COIN));
	return results;
}

UniValue hexblocktocoinbase(const JSONRPCRequest& request)
{
	if (request.fHelp || (request.params.size() != 1  &&  request.params.size() != 2 ))
		throw std::runtime_error("hexblocktocoinbase: returns block information used by the pool(s) for a given serialized hexblock.");

	// This call is used by legacy pools to verify a serialized solution
	std::string sBlockHex = request.params[0].get_str();
	double dDetails = 0;
	if (request.params.size() > 1)
		dDetails = cdbl(request.params[1].get_str(), 0);
	CBlock block;
    if (!DecodeHexBlk(block, sBlockHex))
           throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block decode failed");

	UniValue results(UniValue::VOBJ);
	
    results.push_back(Pair("txid", block.vtx[0]->GetHash().GetHex()));
	results.push_back(Pair("recipient", PubKeyToAddress(block.vtx[0]->vout[0].scriptPubKey)));
	CBlockIndex* pindexPrev = chainActive.Tip();
	bool f7000;
	bool f8000;
	bool f9000;
	bool fTitheBlocksActive;
	results.push_back(Pair("blockhash", block.GetHash().GetHex()));
	results.push_back(Pair("nonce", (uint64_t)block.nNonce));
	results.push_back(Pair("version", block.nVersion));
	results.push_back(Pair("versionHex", strprintf("%08x", block.nVersion)));
	results.push_back(Pair("nTime", block.GetBlockTime()));
	results.push_back(Pair("subsidy", block.vtx[0]->vout[0].nValue/COIN));
	results.push_back(Pair("blockversion", GetBlockVersion(block.vtx[0]->vout[0].sTxOutMessage)));
	std::string sMsg;
	for (unsigned int i = 0; i < block.vtx[0]->vout.size(); i++)
	{
		sMsg += block.vtx[0]->vout[i].sTxOutMessage;
	}
	results.push_back(Pair("blockmessage", sMsg));
	results.push_back(Pair("height", pindexPrev->nHeight + 1));
	arith_uint256 hashTarget = arith_uint256().SetCompact(block.nBits);
	results.push_back(Pair("target", hashTarget.GetHex()));
	results.push_back(Pair("bits", strprintf("%08x", block.nBits)));
	results.push_back(Pair("merkleroot", block.hashMerkleRoot.GetHex()));
	// RandomX
	if (dDetails == 1)
	{
		results.push_back(Pair("rxheader", ExtractXML(block.RandomXData, "<rxheader>", "</rxheader>")));
		results.push_back(Pair("rxkey", block.RandomXKey.GetHex()));
	} 
	return results;
}

UniValue listutxostakes(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 2) 
		throw std::runtime_error("You may specify listutxostakes 0=mine/1=all 0=unspent/1=all");
	UniValue results(UniValue::VOBJ);
	
	double nMineType = cdbl(request.params[0].get_str(), 0);
	double nSpentType = cdbl(request.params[0].get_str(), 0);
	std::string sCPK = DefaultRecAddress("Christian-Public-Key"); 
	CAmount nTotalForeignAmount = 0;
	CAmount nTotalAmount = 0;
	double nCommitmentQuantity = 0;
	double nCommitmentPctg = 0;
	std::map<std::string, CAmount> mapAmounts;

	std::vector<UTXOStake> uStakes = GetUTXOStakes(false);
	for (int i = 0; i < uStakes.size(); i++)
	{
		UTXOStake d = uStakes[i];
		int nStatus = GetUTXOStatus(d.TXID);
		if (d.found && d.nValue > 0)
		{
			if ((nMineType == 0 && sCPK == d.CPK) || nMineType == 1)
			{
				if (nStatus != -1 || nSpentType == 1)
				{
					if (d.nCommitment > 0)
						nCommitmentQuantity++;
					std::string sSigs = "Sigs: " + d.SignatureNarr;
					std::string sRow = "#" + RoundToString(i+1, 0) + ":  Total_Value: $" + RoundToString(d.nValue, 2) + ", Ticker: " + d.ReportTicker 
						+ ", Status: " + RoundToString(nStatus, 0) + ", CPK: " + d.CPK + ", " + sSigs + ", BBPAmount: " + AmountToString(d.nBBPAmount) + ", ForeignAmount: " 
						+ AmountToString(d.nForeignAmount) + ", BBPValue: $" + RoundToString(d.nBBPValueUSD, 2) + ", ForeignValue: $" + RoundToString(d.nForeignValueUSD, 2)
					    + ", Commitment: e" + RoundToString(d.DaysElapsed, 2) + "/c" + RoundToString(d.nCommitment, 0) + "=" + RoundToString(d.CommitmentFulfilledPctg * 100, 2) + "%"
						+ ", Time: " + RoundToString(d.Time,0) + ", Age: " + RoundToString(d.Age, 2) + ", bbpspent: " + ToYesNo(d.fBBPSpent);

					results.push_back(Pair(d.TXID.GetHex(), sRow));
					nTotalAmount += d.nBBPAmount;
					nTotalForeignAmount += d.nForeignAmount;
					mapAmounts[d.ReportTicker] += d.ReportTicker == "BBP" ? d.nBBPAmount : d.nForeignAmount;
				}
			}
		}
		else
		{
				std::string sSigs = "Sigs: " + d.SignatureNarr;
				std::string sRow = "#" + RoundToString(i+1, 0) + ":  TXID=" + d.TXID.GetHex() + ", Total_Value: $" + RoundToString(d.nValue, 2) + ", Ticker: " + d.ReportTicker 
						+ ", Status: " + RoundToString(nStatus, 0) + ", CPK: " + d.CPK + ", " + sSigs + ", BBPAmount: " + AmountToString(d.nBBPAmount) + ", ForeignAmount: " 
						+ AmountToString(d.nForeignAmount) + ", BBPValue: $" + RoundToString(d.nBBPValueUSD, 2) + ", ForeignValue: $" + RoundToString(d.nForeignValueUSD, 2);
				// LogPrintf("\nlistutxostakes::Skipping %s", sRow);
		}
		if (nSpentType == 3)
		{
			AssimilateUTXO(d);
		}
	}
	for (const auto& kv : mapAmounts) 
	{
		results.push_back(Pair("Total " + kv.first, AmountToString(kv.second)));
	}
	nCommitmentPctg = nCommitmentQuantity / (uStakes.size()+.01);

	results.push_back(Pair("Total Foreign", AmountToString(nTotalForeignAmount)));
    results.push_back(Pair("Total Hybrid BBP", AmountToString(nTotalAmount)));
	results.push_back(Pair("High Risk Pctg", nCommitmentPctg));
   	double nDWU = CalculateUTXOReward(1, 0) * (nCommitmentPctg+1) * 1.5 * 100;
	results.push_back(Pair("DWU", nDWU));

	return results;
}

UniValue buynft(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 3) 
		throw std::runtime_error("You may specify buynft new_owner_bbpaddress nftid amount");
	UniValue results(UniValue::VOBJ);
	std::string sNewOwnerAddress = request.params[0].get_str();
	uint256 nftid = ParseHashV(request.params[1], "nftid");
	CAmount nAmount = cdbl(request.params[2].get_str(), 2) * COIN;
	
	NFT n = GetSpecificNFT(nftid);
	if (!n.found)
	{
		throw std::runtime_error("NFT not found.");
	}
	std::string sError;
	bool fCreated = ProcessNFT(n, "BUY", sNewOwnerAddress, nAmount, false, sError);
	if (!sError.empty())
	{
		results.push_back(Pair("Error", sError));
	}
	else
	{
		results.push_back(Pair("Result", "Success"));
		results.push_back(Pair("NOTE", "Please wait a few blocks to see your new NFT in listnfts."));
	}
	return results;
}

UniValue price(const JSONRPCRequest& request)
{
    if (request.fHelp) 
		throw std::runtime_error("You may specify price");
	UniValue results(UniValue::VOBJ);

	double dBBPPrice = GetCryptoPrice("bbp"); 
	double dBTC = GetCryptoPrice("btc");
	double dDASH = GetCryptoPrice("dash");
	double dXMR = GetCryptoPrice("xmr");
	double dDOGE = GetCryptoPrice("doge");
	double dLTC = GetCryptoPrice("ltc");
	double dETH = GetCryptoPrice("eth");
	results.push_back(Pair(CURRENCY_TICKER + "/BTC", RoundToString(dBBPPrice, 12)));
	results.push_back(Pair("DASH/BTC", RoundToString(dDASH, 12)));
	results.push_back(Pair("LTC/BTC", dLTC));
	results.push_back(Pair("DOGE/BTC", dDOGE));
	results.push_back(Pair("XMR/BTC", dXMR));
	results.push_back(Pair("ETH/BTC", dETH));
	results.push_back(Pair("BTC/USD", dBTC));
	double nPrice = GetBBPUSDPrice();
	double nDashPriceUSD = dBTC * dDASH;
	double nXMRPriceUSD = dBTC * dXMR;
	double nETHPriceUSD = dBTC * dETH;
	std::string sAPM = GetAPMNarrative();
	results.push_back(Pair("APM", sAPM));
	results.push_back(Pair("DASH/USD", nDashPriceUSD));
	results.push_back(Pair("XMR/USD", nXMRPriceUSD));
	results.push_back(Pair("ETH/USD", nETHPriceUSD));
	results.push_back(Pair(CURRENCY_TICKER + "/USD", nPrice));
	return results;
}

UniValue listnfts(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 2) 
		throw std::runtime_error("You may specify listnfts 0=mine/1=all 0=marketable(for sale)/1=all");
	UniValue results(UniValue::VOBJ);
	double nMineType = cdbl(request.params[0].get_str(), 0);
	double nMarketableType = cdbl(request.params[1].get_str(), 0);
	std::string sCPK = DefaultRecAddress("Christian-Public-Key"); 
	std::vector<NFT> uNFTs = GetNFTs(false);
	for (int i = 0; i < uNFTs.size(); i++)
	{
		NFT n = uNFTs[i];
		if (n.found)
		{
			if ((nMineType == 0 && sCPK == n.sCPK) || nMineType != 0)
			{
				if (nMarketableType == 1 || (nMarketableType == 0 && n.fMarketable))
				{
				    UniValue o(UniValue::VOBJ);
					n.ToJson(o);
					results.push_back(Pair(n.GetHash().GetHex(), o));
				}
			}
		}
	}
	return results;
}

UniValue generatereferralcode(const JSONRPCRequest& request)
{
	
    if (request.fHelp) 
		throw std::runtime_error("You may specify generatereferralcode");
	UniValue results(UniValue::VOBJ);
	std::string sError;
	AcquireWallet3();
	std::string sTXID = SendReferralCode(sError);
	if (!sError.empty())
	{
		results.push_back(Pair("Error", sError));
	}
	else
	{
		results.push_back(Pair("referral_code", sTXID));
	}
	return results;
}

UniValue getreferralcodeimpact(const JSONRPCRequest& request)
{
    if (request.fHelp) 
		throw std::runtime_error("You may specify getreferralcodeimpact");
	UniValue results(UniValue::VOBJ);
	
	std::string sCPK = DefaultRecAddress("Christian-Public-Key");

	ReferralCode rc = GetTotalPortfolioImpactFromReferralCodes(sCPK);
	results.push_back(Pair("Referral BBP Claimed", (double)rc.TotalClaimed/COIN));
	results.push_back(Pair("Referral BBP Earned", (double)rc.TotalEarned/COIN));
	results.push_back(Pair("Total BBP in Portfolio affected", (double)rc.TotalReferralReward/COIN));
	results.push_back(Pair("Referral Code Effectivity %", rc.ReferralEffectivity * 100));
	results.push_back(Pair("Total Portfolio Impact %", rc.ReferralRewards * 100));
	return results;
}

UniValue checkreferralcode(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
		throw std::runtime_error("You may specify checkreferralcode code");
	UniValue results(UniValue::VOBJ);
	AcquireWallet3();
	std::string sCode = request.params[0].get_str();
	CAmount nAmount = CheckReferralCode(sCode);
	results.push_back(Pair("value", (double)nAmount/COIN));
	results.push_back(Pair("instructions", "You may use this code for a position containing up to the quantity of biblepay listed in value.  This code decays at the rate of age of your portfolio.  You will receive an additional 0-10% DWU on this portion of your portfolio based on age (newest gets the highest reward, oldest gets the lowest reward).  You may not use a code generated by yourself for your own portfolio, it must be a code generated by another CPK.  "));
	return results;
}

static const CRPCCommand commands[] =
{ //  category              name                      actor (function)
  //  --------------------- ------------------------  -----------------------
    { "evo",                "bls",                          &_bls,                 {}  },
    { "evo",                "protx",                        &protx,                {}  },
	{ "evo",                "bookname",                     &bookname,             {}  },
	{ "evo",                "books",                        &books,                {}  },
	{ "evo",                "buynft",                       &buynft,               {}  },
	{ "evo",                "checkreferralcode",            &checkreferralcode,    {}  },
	{ "evo",                "datalist",                     &datalist,             {}  },
	{ "evo",                "dashpay",                      &dashpay,              {}  },
	{ "evo",                "give",                         &give,                 {}  },
	{ "evo",                "getpin",                       &getpin,               {}  },
	{ "evo",                "getreferralcodeimpact",        &getreferralcodeimpact,{}  },
	{ "evo",                "easystake",                    &easystake,            {}  },
	{ "evo",                "listutxostakes",               &listutxostakes,       {}  },
	{ "evo",                "listdacdonations",             &listdacdonations,     {}  },
	{ "evo",                "listexpenses",                 &listexpenses,         {}  },
	{ "evo",                "listnfts",                     &listnfts,             {}  },
	{ "evo",                "easybbpstake",                 &easybbpstake,         {}  },
	{ "evo",                "utxostake",                    &utxostake,            {}  },
	{ "evo",                "generatereferralcode",         &generatereferralcode, {} },
	{ "evo",                "hexblocktocoinbase",           &hexblocktocoinbase,   {}  },
	{ "evo",                "faucetcode",                   &faucetcode,           {}  },
	{ "evo",                "price",                        &price,                {}  },
	{ "evo",                "trackdashpay",                 &trackdashpay,         {}  },
	{ "evo",                "versionreport",                &versionreport,        {}  },
};

void RegisterEvoRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++) {
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
    }
}
