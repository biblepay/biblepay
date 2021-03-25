// Copyright (c) 2018-2019 The Dash Core developers
// Copyright (c) 2017-2019 The DAC Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "consensus/validation.h"
#include "core_io.h"
#include "init.h"
#include "messagesigner.h"
#include "rpc/server.h"
#include "utilmoneystr.h"
#include "validation.h"
#include "smartcontract-server.h"
#include "kjv.h"

#ifdef ENABLE_WALLET
#include "wallet/coincontrol.h"
#include "wallet/wallet.h"
#include "wallet/rpcwallet.h"
#endif//ENABLE_WALLET

#include "netbase.h"
#include "rpcpog.h"
#include <boost/algorithm/string/case_conv.hpp> // for to_lower()

#include "evo/specialtx.h"
#include "evo/providertx.h"
#include "evo/deterministicmns.h"
#include "evo/simplifiedmns.h"
#include "evo/cbtx.h"
#include "smartcontract-client.h"
#include "smartcontract-server.h"

#include "bls/bls.h"

#ifdef ENABLE_WALLET
extern UniValue signrawtransaction(const JSONRPCRequest& request);
extern UniValue sendrawtransaction(const JSONRPCRequest& request);
extern UniValue protx_register(const JSONRPCRequest& request);
#endif//ENABLE_WALLET

std::string GetHelpString(int nParamNum, std::string strParamName)
{
    static const std::map<std::string, std::string> mapParamHelp = {
        {"collateralAddress",
            "%d. \"collateralAddress\"        (string, required) The address to send the collateral to.\n"
            "                              Must be a P2PKH address.\n"
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
            "%d. \"operatorKey\"              (string, required) The operator private key belonging to the\n"
            "                              registered operator public key.\n"
        },
        {"operatorPayoutAddress",
            "%d. \"operatorPayoutAddress\"    (string, optional) The address used for operator reward payments.\n"
            "                              Only allowed when the ProRegTx had a non-zero operatorReward value.\n"
            "                              If set to an empty string, the currently active payout address is reused.\n"
        },
        {"operatorPubKey",
            "%d. \"operatorPubKey\"           (string, required) The operator BLS public key. The private key does not have to be known.\n"
            "                              It has to match the private key which is later used when operating the masternode.\n"
        },
        {"operatorReward",
            "%d. \"operatorReward\"           (numeric, required) The fraction in %% to share with the operator. The value must be\n"
            "                              between 0.00 and 100.00.\n"
        },
        {"ownerAddress",
            "%d. \"ownerAddress\"             (string, required) The address to use for payee updates and proposal voting.\n"
            "                              The private key belonging to this address must be known in your wallet. The address must\n"
            "                              be unused and must differ from the collateralAddress\n"
        },
        {"payoutAddress",
            "%d. \"payoutAddress\"            (string, required) The address to use for masternode reward payments.\n"
        },
        {"proTxHash",
            "%d. \"proTxHash\"                (string, required) The hash of the initial ProRegTx.\n"
        },
        {"reason",
            "%d. reason                     (numeric, optional) The reason for masternode service revocation.\n"
        },
        {"votingAddress",
            "%d. \"votingAddress\"            (string, required) The voting key address. The private key does not have to be known by your wallet.\n"
            "                              It has to match the private key which is later used when voting on proposals.\n"
            "                              If set to an empty string, ownerAddress will be used.\n"
        },
    };

    auto it = mapParamHelp.find(strParamName);
    if (it == mapParamHelp.end())
        throw std::runtime_error(strprintf("FIXME: WRONG PARAM NAME %s!", strParamName));

    return strprintf(it->second, nParamNum);
}

// Allows to specify an address or priv key. In case of an address, the priv key is taken from the wallet
static CKey ParsePrivKey(CWallet* pwallet, const std::string &strKeyOrAddress, bool allowAddresses = true) {
    CBitcoinAddress address;
    if (allowAddresses && address.SetString(strKeyOrAddress) && address.IsValid()) {
#ifdef ENABLE_WALLET
        if (!pwallet) {
            throw std::runtime_error("addresses not supported when wallet is disabled");
        }
        EnsureWalletIsUnlocked(pwallet);
        CKeyID keyId;
        CKey key;
        if (!address.GetKeyID(keyId) || !pwallet->GetKey(keyId, key))
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
    CBitcoinAddress address(strAddress);
    CKeyID keyID;
    if (!address.IsValid() || !address.GetKeyID(keyID)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("%s must be a valid P2PKH address, not %s", paramName, strAddress));
    }
    return keyID;
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
    assert(pwallet != NULL);
    LOCK2(cs_main, pwallet->cs_wallet);

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
        throw JSONRPCError(RPC_INTERNAL_ERROR, "No funds at specified address (special transaction error)");
    }

    CWalletTx wtx;
    CReserveKey reservekey(pwallet);
    CAmount nFee;
    int nChangePos = -1;
    std::string strFailReason;

    if (!pwallet->CreateTransaction(vecSend, wtx, reservekey, nFee, nChangePos, strFailReason, &coinControl, false, ALL_COINS, false, tx.vExtraPayload.size())) {
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
    LOCK(cs_main);

    CValidationState state;
    if (!CheckSpecialTx(tx, chainActive.Tip(), state)) {
        throw std::runtime_error(FormatStateMessage(state));
    }

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
            "\nCreates, funds and sends a ProTx to the network. The resulting transaction will move 1000 coins\n"
            "to the address specified by collateralAddress and will then function as the collateral of your\n"
            "masternode.\n"
            "A few of the limitations you see in the arguments are temporary and might be lifted after DIP3\n"
            "is fully deployed.\n"
            + HelpRequiringPassphrase(pwallet) + "\n"
            "\nArguments:\n"
            + GetHelpString(1, "collateralAddress")
            + GetHelpString(2, "ipAndPort")
            + GetHelpString(3, "ownerAddress")
            + GetHelpString(4, "operatorPubKey")
            + GetHelpString(5, "votingAddress")
            + GetHelpString(6, "operatorReward")
            + GetHelpString(7, "payoutAddress")
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
            + GetHelpString(5, "operatorPubKey")
            + GetHelpString(6, "votingAddress")
            + GetHelpString(7, "operatorReward")
            + GetHelpString(8, "payoutAddress")
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
            + GetHelpString(5, "operatorPubKey")
            + GetHelpString(6, "votingAddress")
            + GetHelpString(7, "operatorReward")
            + GetHelpString(8, "payoutAddress")
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
        CBitcoinAddress collateralAddress(request.params[paramIdx].get_str());
        if (!collateralAddress.IsValid()) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("invalid collaterall address: %s", request.params[paramIdx].get_str()));
        }
        CScript collateralScript = GetScriptForDestination(collateralAddress.Get());

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
        if (!Lookup(request.params[paramIdx].get_str().c_str(), ptx.addr, Params().GetDefaultPort(), false)) 
		{
			if (fEnforceSanctuaryPort)
				throw std::runtime_error(strprintf("invalid network address %s", request.params[paramIdx].get_str()));
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

    CBitcoinAddress payoutAddress(request.params[paramIdx + 5].get_str());
    if (!payoutAddress.IsValid()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("invalid payout address: %s", request.params[paramIdx + 5].get_str()));
    }

    ptx.keyIDOwner = keyOwner.GetPubKey().GetID();
    ptx.pubKeyOperator = pubKeyOperator;
    ptx.keyIDVoting = keyIDVoting;
    ptx.scriptPayout = GetScriptForDestination(payoutAddress.Get());

    if (!isFundRegister) {
        // make sure fee calculation works
        ptx.vchSig.resize(65);
    }

    CBitcoinAddress fundAddress = payoutAddress;
    if (request.params.size() > paramIdx + 6) {
        fundAddress = CBitcoinAddress(request.params[paramIdx + 6].get_str());
        if (!fundAddress.IsValid())
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid address: ") + request.params[paramIdx + 6].get_str());
    }

    FundSpecialTx(pwallet, tx, ptx, fundAddress.Get());
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
        CKeyID keyID;
        if (!ExtractDestination(coin.out.scriptPubKey, txDest) || !CBitcoinAddress(txDest).GetKeyID(keyID)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("collateral type not supported: %s", ptx.collateralOutpoint.ToStringShort()));
        }

        if (isPrepareRegister) {
            // external signing with collateral key
            ptx.vchSig.clear();
            SetTxPayload(tx, ptx);

            UniValue ret(UniValue::VOBJ);
            ret.push_back(Pair("tx", EncodeHexTx(tx)));
            ret.push_back(Pair("collateralAddress", CBitcoinAddress(txDest).ToString()));
            ret.push_back(Pair("signMessage", ptx.MakeSignString()));
            return ret;
        } else {
            // lets prove we own the collateral
            CKey key;
            if (!pwallet->GetKey(keyID, key)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("collateral key not in wallet: %s", CBitcoinAddress(keyID).ToString()));
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

    if (!EnsureWalletIsAvailable(pwallet, request.fHelp))
        return NullUniValue;

    EnsureWalletIsUnlocked(pwallet);

    CProUpServTx ptx;
    ptx.nVersion = CProRegTx::CURRENT_VERSION;
    ptx.proTxHash = ParseHashV(request.params[1], "proTxHash");

    if (!Lookup(request.params[2].get_str().c_str(), ptx.addr, Params().GetDefaultPort(), false)) 
	{
		if (fEnforceSanctuaryPort)
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
    if (request.params.size() >= 5) {
        if (request.params[4].get_str().empty()) {
            ptx.scriptOperatorPayout = dmn->pdmnState->scriptOperatorPayout;
        } else {
            CBitcoinAddress payoutAddress(request.params[4].get_str());
            if (!payoutAddress.IsValid()) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("invalid operator payout address: %s", request.params[4].get_str()));
            }
            ptx.scriptOperatorPayout = GetScriptForDestination(payoutAddress.Get());
        }
    } else {
        ptx.scriptOperatorPayout = dmn->pdmnState->scriptOperatorPayout;
    }

    CTxDestination feeSource;

    // param feeSourceAddress
    if (request.params.size() >= 6) {
        CBitcoinAddress feeSourceAddress = CBitcoinAddress(request.params[5].get_str());
        if (!feeSourceAddress.IsValid())
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid address: ") + request.params[5].get_str());
        feeSource = feeSourceAddress.Get();
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
            + GetHelpString(2, "operatorPubKey")
            + GetHelpString(3, "votingAddress")
            + GetHelpString(4, "payoutAddress")
            + GetHelpString(5, "feeSourceAddress") +
            "\nResult:\n"
            "\"txid\"                        (string) The transaction id.\n"
            "\nExamples:\n"
            + HelpExampleCli("protx", "update_registrar \"0123456701234567012345670123456701234567012345670123456701234567\" \"982eb34b7c7f614f29e5c665bc3605f1beeef85e3395ca12d3be49d2868ecfea5566f11cedfad30c51b2403f2ad95b67\" \"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwG\"")
    );
}

UniValue protx_update_registrar(const JSONRPCRequest& request)
{
    CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
    if (request.fHelp || (request.params.size() != 5 && request.params.size() != 6)) {
        protx_update_registrar_help(pwallet);
    }

    if (!EnsureWalletIsAvailable(pwallet, request.fHelp))
        return NullUniValue;

    EnsureWalletIsUnlocked(pwallet);

    CProUpRegTx ptx;
    ptx.nVersion = CProRegTx::CURRENT_VERSION;
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

    CBitcoinAddress payoutAddress(request.params[4].get_str());
    if (!payoutAddress.IsValid()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("invalid payout address: %s", request.params[4].get_str()));
    }
    ptx.scriptPayout = GetScriptForDestination(payoutAddress.Get());

    CKey keyOwner;
    if (!pwallet->GetKey(dmn->pdmnState->keyIDOwner, keyOwner)) {
        throw std::runtime_error(strprintf("Private key for owner address %s not found in your wallet", CBitcoinAddress(dmn->pdmnState->keyIDOwner).ToString()));
    }

    CMutableTransaction tx;
    tx.nVersion = 3;
    tx.nType = TRANSACTION_PROVIDER_UPDATE_REGISTRAR;

    // make sure we get anough fees added
    ptx.vchSig.resize(65);

    CBitcoinAddress feeSourceAddress = payoutAddress;
    if (request.params.size() > 5) {
        feeSourceAddress = CBitcoinAddress(request.params[5].get_str());
        if (!feeSourceAddress.IsValid())
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid address: ") + request.params[5].get_str());
    }

    FundSpecialTx(pwallet, tx, ptx, feeSourceAddress.Get());
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

    if (!EnsureWalletIsAvailable(pwallet, request.fHelp))
        return NullUniValue;

    EnsureWalletIsUnlocked(pwallet);

    CProUpRevTx ptx;
    ptx.nVersion = CProRegTx::CURRENT_VERSION;
    ptx.proTxHash = ParseHashV(request.params[1], "proTxHash");

    CBLSSecretKey keyOperator = ParseBLSSecretKey(request.params[2].get_str(), "operatorKey");

    if (request.params.size() > 3) {
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

    if (request.params.size() > 4) {
        CBitcoinAddress feeSourceAddress = CBitcoinAddress(request.params[4].get_str());
        if (!feeSourceAddress.IsValid())
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid address: ") + request.params[4].get_str());
        FundSpecialTx(pwallet, tx, ptx, feeSourceAddress.Get());
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

    UniValue walletObj(UniValue::VOBJ);
    walletObj.push_back(Pair("hasOwnerKey", hasOwnerKey));
    walletObj.push_back(Pair("hasOperatorKey", hasOperatorKey));
    walletObj.push_back(Pair("hasVotingKey", hasVotingKey));
    walletObj.push_back(Pair("ownsCollateral", ownsCollateral));
    walletObj.push_back(Pair("ownsPayeeScript", CheckWalletOwnsScript(pwallet, dmn->pdmnState->scriptPayout)));
    walletObj.push_back(Pair("ownsOperatorRewardScript", CheckWalletOwnsScript(pwallet, dmn->pdmnState->scriptOperatorPayout)));
    o.push_back(Pair("wallet", walletObj));

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
    if (request.params.size() > 1) {
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

        if (request.params.size() > 3) {
            protx_list_help();
        }

        bool detailed = request.params.size() > 2 ? ParseBoolV(request.params[2], "detailed") : false;

        int height = request.params.size() > 3 ? ParseInt32V(request.params[3], "height") : chainActive.Height();
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

        bool detailed = request.params.size() > 2 ? ParseBoolV(request.params[2], "detailed") : false;

        int height = request.params.size() > 3 ? ParseInt32V(request.params[3], "height") : chainActive.Height();
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

UniValue nonfinancialtxtojson(const JSONRPCRequest& request)
{
	if (request.fHelp)
		throw std::runtime_error("nonfinancialtxtojson txid\nDescribes a non-financial transaction (dev use only).");

	uint256 uHash = ParseHashV(request.params[0], "nonFinancialTxId");
    CTransactionRef nonFinTx;
	
    uint256 tmpHashBlock;
    if (!GetTransaction(uHash, nonFinTx, Params().GetConsensus(), tmpHashBlock)) 
		throw std::runtime_error("Unable to find this non-financial-txid.");
	
    CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
    ssTx << nonFinTx;
    std::string sNonFinHex = HexStr(ssTx.begin(), ssTx.end());

	CMutableTransaction mNonFinTx;
    if (!DecodeHexTx(mNonFinTx, sNonFinHex)) 
		throw std::runtime_error("Unable to decode non-financial-tx.");

	CNonFinancialTx ptx;
	if (!GetTxPayload(mNonFinTx, ptx)) 
		throw std::runtime_error("Unable to retrieve non-financial payload.");

    if (mNonFinTx.nType != TRANSACTION_NON_FINANCIAL) 
			throw std::runtime_error("Not a non-financial transaction.");
	UniValue obj;
    ptx.ToJson(obj);
	return obj;
}

UniValue createnonfinancialtransaction(const JSONRPCRequest& request)
{
	if (request.fHelp)
		throw std::runtime_error("createnonfinancialtransaction (dev use only)\nCreates a non-financial transaction (dev use only).");

	CNonFinancialTx ptx;
	ptx.nVersion = CProRegTx::CURRENT_VERSION;
	ptx.proTxHash = uint256S("0x01");
	ptx.inputsHash = uint256S("0x02");
	ptx.sNonce = GetRandHash().GetHex();
	ptx.sObjectType = "PRAYER";
	ptx.sKey = "OUT_TX_P_01";
	ptx.sValue = "Let us pray for divine assistance in Venezuela for those short of medicine or food.";
	ptx.dsqlHash = uint256S("0x03");
	ptx.iObjectSize = 125;
	ptx.sExtraPayload = "In Jesus Name, Amen.";
	ptx.nTimestamp = GetAdjustedTime();
	CMutableTransaction tx;
	tx.nVersion = 3;
	tx.nType = TRANSACTION_NON_FINANCIAL;
	std::string sPayAddress = DefaultRecAddress("Christian-Public-Key"); 
	CBitcoinAddress baPayAddress(sPayAddress);
	bool fSubtractFee = false;
	bool fInstantSend = false;
	std::string sError;
	std::string sData = "<NONFINANCIALTX/>";
	// Send minute amount of currency to CPK to ensure we can afford to fund the non-financial Tx
	CWalletTx wtx;
	bool fSent = RPCSendMoney(sError, baPayAddress.Get(), 1 * COIN, fSubtractFee, wtx, fInstantSend, sData);
	CTxDestination feeSource;
	feeSource = baPayAddress.Get();
	// Fund the non financial tx
	FundSpecialTx(pwalletMain, tx, ptx, feeSource);
	// sign the non financial with the CPK
	CKeyID keyID;
	if (!baPayAddress.GetKeyID(keyID))
	{
		sError = "Address does not refer to key";
		return sError;
	}
	CKey key;
	if (!pwalletMain->GetKey(keyID, key)) 
		throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("collateral key not in wallet: %s", CBitcoinAddress(keyID).ToString()));
    SignSpecialTxPayloadByString(tx, ptx, key);
 	SetTxPayload(tx, ptx);
	return SignAndSendSpecialTx(tx);
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

	double nCoinUSDPrice = GetCoinPrice();
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

	CBitcoinAddress baDest(sDashPayAddress);
	if (sDashAddress.empty())
		throw std::runtime_error("Dash Destination address must be populated.");

	if (!baDest.IsValid() || sDashAddress.length() != 34 || sDashAddress.substr(0,1) != "X")
		throw std::runtime_error("Sorry, DashPay destination address is invalid for this IX transaction.");
	
	bool fSubtractFee = false;
	bool fInstantSend = true;
	CWalletTx wtx;
	bool fSent = false;
	if (sErrorDryRun.empty() && sError.empty() && nMode == 1)
	{
		// Set up an atomic transaction here
		fSent = RPCSendMoney(sError, baDest.Get(), nCoinAmount * COIN, fSubtractFee, wtx, fInstantSend, sXML);
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
	if (!CheckCampaign(sCampaignName))
		throw std::runtime_error("Campaign does not exist.");
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

UniValue easystake(const JSONRPCRequest& request)
{
	// Jan 20, 2021 - Easy Stake - R ANDREWS - BIBLEPAY
		
	std::string sHelp = "You must specify easystake minimum_bbp_amount foreign_address foreign_amount 0=dry_run/1=real  [Optional `Ticker`=Override Foreign Ticker]";
	std::string sError;
	if (request.fHelp || (request.params.size() != 4 && request.params.size() != 5))
		throw std::runtime_error(sHelp.c_str());
		
	double nMin = cdbl(request.params[0].getValStr(), 2);
	std::string sTickerOverride;
	if (request.params.size() > 4)
		sTickerOverride = request.params[4].getValStr();

	std::string sBBPAddress;
	CAmount nBBPReturnAmount = 0;
	std::string sBBPUTXO = pwalletMain->GetBestUTXO(nMin * COIN, .01, sBBPAddress, nBBPReturnAmount);
	if (sBBPUTXO.empty())
	{
		// They dont have one with 1+ day of age; try one without age
		LogPrintf("\nEasyStake::NO_AGE We cant find a UTXO with age, so we will try one without age. %f", 10001);
		sBBPUTXO = pwalletMain->GetBestUTXO(nMin * COIN, .01, sBBPAddress, nBBPReturnAmount);
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
	std::string sForeignAddress = request.params[1].getValStr();
	std::string sForeignTicker = GetSymbolFromAddress(sForeignAddress);
	if (!sTickerOverride.empty())
		sForeignTicker = sTickerOverride;
	boost::to_upper(sForeignTicker);
		
	double nForeignAmount = cdbl(request.params[2].getValStr(), 8);
	bool fDryRun = cdbl(request.params[3].getValStr(), 0) == 0;
	bool fReturnFirst = (nForeignAmount == -1);
	// Find the UTXO by amount
	SimpleUTXO s = QueryUTXO(GetAdjustedTime(), nForeignAmount, sForeignTicker, sForeignAddress, "", 0, sError, fReturnFirst);
	results.push_back(Pair("Foreign Symbol", sForeignTicker));

	results.push_back(Pair("Foreign Amount", (double)s.nAmount/COIN));
	results.push_back(Pair("Foreign UTXO", s.TXID));
	results.push_back(Pair("Foreign Ordinal", s.nOrdinal));
	
	int nStakeCount = sForeignTicker.empty() ? 1 : 2;
	double nDWU = CalculateUTXOReward(nStakeCount);
	PriceQuote p = GetPriceQuote(sForeignTicker, nBBPReturnAmount, s.nAmount);

	results.push_back(Pair("Foreign Value USD", p.nForeignValueUSD));
	results.push_back(Pair("BBP Value USD", p.nBBPValueUSD));
	results.push_back(Pair("DWU", nDWU * 100));

	double nPin = AddressToPin(sForeignAddress);
	results.push_back(Pair("Pin", nPin));
	bool fValid = CompareMask2(s.nAmount, nPin);
	if (!fValid)
		sError = "The pin is not valid for this receive address.";
	results.push_back(Pair("pin_valid", fValid));
	results.push_back(Pair("Error", sError));
	std::string sTXID;
	UTXOStake ds;

	std::string sBBPSig = SignBBPUTXO(sBBPUTXO, sError);
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
		bool fSent = SendUTXOStake(0, sForeignTicker, sTXID, sError, sBBPAddress, sBBPUTXO, sForeignAddress, sForeignUTXO, sBBPSig, "", sCPK, false, ds);
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
	std::string sHelp = "You must specify easybbpstake minimum_bbp_amount 0=dry_run/1=real";
	std::string sError;
	if (request.fHelp || (request.params.size() != 2))
		throw std::runtime_error(sHelp.c_str());
	double nMin = cdbl(request.params[0].getValStr(), 2);
	std::string sBBPAddress;
	CAmount nBBPReturnAmount = 0;
	std::string sBBPUTXO = pwalletMain->GetBestUTXO(nMin * COIN, .01, sBBPAddress, nBBPReturnAmount);
	if (sBBPUTXO.empty())
	{
		// They dont have one with 1+ day of age; try one without age
		LogPrintf("\nEasyBBPStake::NO_AGE We cant find a UTXO with age, so we will try one without age. %f", 10001);
		sBBPUTXO = pwalletMain->GetBestUTXO(nMin * COIN, .01, sBBPAddress, nBBPReturnAmount);
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
	double nDWU = CalculateUTXOReward(1);
	results.push_back(Pair("DWU", nDWU * 100));
	
	bool fDryRun = cdbl(request.params[1].getValStr(), 0) == 0;

	PriceQuote p = GetPriceQuote("bbp", nBBPReturnAmount, 0);

	results.push_back(Pair("BBP Value USD", p.nBBPValueUSD));

	std::string sTXID;
	UTXOStake ds;
	std::string sBBPSig = SignBBPUTXO(sBBPUTXO, sError);
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
		bool fSent = SendUTXOStake(0, "", sTXID, sError, sBBPAddress, sBBPUTXO, "", sForeignUTXO, sBBPSig, "", sCPK, false, ds);
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

	std::string sBBPSig = SignBBPUTXO(sBBPUTXO, sError);
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

		bool fSent = SendUTXOStake(0, sForeignTicker, sTXID, sError, sBBPAddress, sBBPUTXO, sForeignAddress, sForeignUTXO, sBBPSig, sForeignSig, sCPK, false, ds);
		if (!fSent || !sError.empty())
		{
			results.push_back(Pair("Error (Not Sent)", sError));
		}
		else
		{
			ds = GetUTXOStakeByUTXO(sBBPUTXO);
			LockUTXOStakes();
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


UniValue dashstake(const JSONRPCRequest& request)
{
	// Dash Staking
	// This allows you to lock up Y amount of Dash + Z amount of BBP in a contract, and receive monthly rewards on this amount.
	// Starting initially as of September 15th, 2020, we will start with 6 month contracts (this is primarily to ensure the prices of each underlying currency do not change significatly since the start date of the contract).
	// IE, assets will need to be re-locked once every 6 months to ensure fresh price quotes (as we strive to lock roughly equal amounts of BBP with equal amounts of DASH).
	// However, we do tolerate price changes during the duration of the contract.
	// But, if either asset is spent, the contract is cancelled (cancelled during our next GSC height after an asset is spent).
	// You can see if a contract is in force by looking at the "expiration" and the "expired" and the "spent" fields of each contract.
	// Contracts pay interest rewards MONTHLY.  At the contract height + 30*205 successively, for each period.
	// To get a dashstake quote, type 'dashstakequote' first.
	// Example:
	// dashstake BBP_UTXO-ORDINAL DASH_UTXO-ORDINAL DASH_SIGNATURE 0=test/1=authorize
	// To create a signature, from DASH type:  signmessage dash_public_key DASH-UTXO-ORDINAL <enter>.  Copy the Dash signature into the BiblePay 'dashstake' command.

	// NOTE:  UTXOs cannot be re-used until a contract expires.
	// The lower stake amount denominated in BBP is used to assess the MonthlyEarnings based on the market value of Dash and BBP at the time the contract is created.
	// You may re-lock after expiration.

	const Consensus::Params& consensusParams = Params().GetConsensus();
		
	std::string sHelp = "You must specify dashstake BBP_UTXO-ORDINAL DASH_UTXO-ORDINAL DASH_SIGNATURE I_AGREE=Authorize.\n" + GetHowey(true, false);
	
	if (request.fHelp || (request.params.size() != 4))
		throw std::runtime_error(sHelp.c_str());
		
	std::string sCPK = DefaultRecAddress("Christian-Public-Key");
	std::string sBBPUTXO = request.params[0].get_str();
	std::string sDashUTXO = request.params[1].get_str();
	std::string sDashSig = request.params[2].get_str();

	std::string sError;

	std::string sBBPSig = SignBBPUTXO(sBBPUTXO, sError);
	UniValue results(UniValue::VOBJ);

	if (!sError.empty())
	{
		results.push_back(Pair("BBP Signing Error", sError));
		return results;
	}

	std::string sAuth = request.params[3].get_str();

	WhaleMetric wm = GetDashStakeMetrics(chainActive.Tip()->nHeight, true);
	std::string sTXID;
	DashStake ds;
	if (sAuth == "I_AGREE")
	{
		bool fSent = SendDashStake(sCPK, sTXID, sError, sBBPUTXO, sDashUTXO, sBBPSig, sDashSig, 30 * 6.5, sCPK, false, ds);
		if (!fSent || !sError.empty())
		{
			results.push_back(Pair("DWU", RoundToString(GetDWUBasedOnMaturity(30 * 6.5, wm.DWU) * 100, 4)));
			results.push_back(Pair("Error (Not Sent)", sError));
		}
		else
		{
			ds = GetDashStakeByUTXO(sBBPUTXO);
			LockUTXOStakes();
			results.push_back(Pair("DWU", ds.ActualDWU * 100));
			results.push_back(Pair("Next Payment Height", ds.Height + BLOCKS_PER_DAY));
			results.push_back(Pair("BBP Value USD", ds.nBBPValueUSD));
			results.push_back(Pair("Dash Value USD", ds.nDashValueUSD));
			results.push_back(Pair("BBP Qty", ds.nBBPQty));
			results.push_back(Pair("BBP Amount", (double)ds.nBBPAmount/COIN));
			results.push_back(Pair("Dash Amount", (double)ds.nDashAmount/COIN));
			results.push_back(Pair("Results", "The Dash Stake Contract was created successfully.  Thank you for using BIBLEPAY and DASH. "));
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

UniValue dws(const JSONRPCRequest& request)
{
	// Dynamic Whale Staking
	// dws amount duration_in_days 0=test/1=authorize
	const Consensus::Params& consensusParams = Params().GetConsensus();
		
	std::string sHelp = "You must specify dws amount duration_in_days 0=test/I_AGREE=Authorize [optional=SPECIFIC_STAKE_RETURN_ADDRESS (If Left Empty, we will send your stake back to your CPK)].\n" + GetHowey(true, true);
	
	if (request.fHelp || (request.params.size() != 3 && request.params.size() != 4))
		throw std::runtime_error(sHelp.c_str());
	double nAmt = cdbl(request.params[0].get_str(), 2);
	double nDuration = cdbl(request.params[1].get_str(), 0);
	std::string sAuthorize = request.params[2].get_str();
	
	std::string sReturnAddress = DefaultRecAddress("Christian-Public-Key");
	if (request.params.size() > 3)
	{
		sReturnAddress = request.params[3].get_str();
	}
	std::string sCPK = DefaultRecAddress("Christian-Public-Key");
	UniValue results(UniValue::VOBJ);

	results.push_back(Pair("Staking Amount", nAmt));
	results.push_back(Pair("Duration", nDuration));
	int64_t nStakeTime = GetAdjustedTime();
	int64_t nReclaimTime = (86400 * nDuration) + nStakeTime;
	WhaleMetric wm = GetWhaleMetrics(chainActive.Tip()->nHeight, true);
	results.push_back(Pair("Reclaim Date", TimestampToHRDate(nReclaimTime)));
	results.push_back(Pair("Return Address", sReturnAddress));
	results.push_back(Pair("DWU", RoundToString(GetDWUBasedOnMaturity(nDuration, wm.DWU) * 100, 4)));
	std::string sTXID;
	std::string sError;
	if (sAuthorize == "I_AGREE")
	{
		bool fSent = SendDWS(sTXID, sError, sReturnAddress, sCPK, nAmt, nDuration, false);
		if (!fSent || !sError.empty())
		{
			results.push_back(Pair("Error (Not Sent)", sError));
		}
		else
		{
			results.push_back(Pair("Results", "Burn was successful.  You will receive your original " + CURRENCY_NAME + " back on the Reclaim Date, plus the stake reward.  Please give the wallet an extra 48 hours after the reclaim date to process the return stake.  "));
			results.push_back(Pair("TXID", sTXID));
		}
	}
	else
	{
		// Dry Run
		results.push_back(Pair("Test Mode", GetHowey(true, true)));
	}
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
			CBitcoinAddress baCharity(sAllocatedCharity);
			CScript spkCharity = GetScriptForDestination(baCharity.Get());
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
	CReserveKey reserveKey(pwalletMain);
	CWalletTx wtx;
	std::string sGiftXML = "<gift><amount>" + RoundToString(dGive, 2) + "</amount></gift>";
	bool fCreated = pwalletMain->CreateTransaction(vecSend, wtx, reserveKey, nFeeRequired, nChangePosRet, sError, NULL, true, ALL_COINS, fUseInstantSend, 0, sGiftXML);
	if (!fCreated)    
	{
		results.push_back(Pair("Error", sError));
	}
	else
	{
		if (!pwalletMain->CommitTransaction(wtx, reserveKey, g_connman.get(), state, NetMsgType::TX))
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

UniValue dashstakequote(const JSONRPCRequest& request)
{
	// Dash Whale Staking
	if (request.fHelp || (request.params.size() != 0 && request.params.size() != 1 && request.params.size() != 2 && request.params.size() != 3))
		throw std::runtime_error("You must specify dashstakequote [optional 1=my dash stakes only, 2=all whale stakes] [optional 1=Include Expired, 2=Include Non-Expired only (default)].");
	std::string sCPK = DefaultRecAddress("Christian-Public-Key");
	UniValue results(UniValue::VOBJ);
	double dDetails = 0;
	double dAdvanced = 0;
	if (request.params.size() > 0)
		dDetails = cdbl(request.params[0].get_str(), 0);
	double dExpired = 2;
	if (request.params.size() > 1)
		dExpired = cdbl(request.params[1].get_str(), 0);

	if (request.params.size() > 2)
		dAdvanced = cdbl(request.params[2].get_str(), 0);

	if (dDetails == 1 || dDetails == 2)
	{
		std::vector<DashStake> w = GetDashStakes(true);
		results.push_back(Pair("Total Dash Stake Quantity", (int)w.size()));
		for (int i = 0; i < w.size(); i++)
		{
			DashStake ws = w[i];
			bool fIncExpired = (!ws.expired && dExpired == 2) || (dExpired == 1);
			if (ws.found && fIncExpired && ((dDetails == 2) || (dDetails==1 && ws.CPK == sCPK)))
			{
				std::string sRow = "BBPQty: "+ RoundToString(ws.nBBPQty, 2) + ", BBPAmount: " + RoundToString((double)ws.nBBPAmount/COIN, 2) 
					+ ", DashAmount: "+ RoundToString((double)ws.nDashAmount/COIN, 2)
					+ ", MonthlyReward: " + RoundToString(ws.MonthlyEarnings, 2) 
					+ ", DWU: " + RoundToString(GetDWUBasedOnMaturity(ws.Duration, ws.DWU) * 100, 4) 
					+ ", Duration: " + RoundToString(ws.Duration, 0) 
					+ ", Height: " + RoundToString(ws.Height, 0) 
					+ ", Time: " + TimestampToHRDate(ws.Time)
					+ ", Expiration: " + TimestampToHRDate(ws.MaturityTime)
					+ ", BBPUTXO: "+ ws.BBPUTXO + ", DASHUTXO: "+ ws.DashUTXO + ", BBPSIG: "+ ws.BBPSignature + ", DashSig: "+ ws.DashSignature 
					+ ", BBPPrice: "+ RoundToString(ws.nBBPPrice, 12) + ", DashPrice: "+ RoundToString(ws.nDashPrice, 12) + ", BTCPrice: "
					+ RoundToString(ws.nBTCPrice, 12) + ", BBP_VALUE_USD: "+ RoundToString(ws.nBBPValueUSD, 4) + ", DASH_VALUE_USD: "
					+ RoundToString(ws.nDashValueUSD, 4) + ", BBPAddress: "+ ws.BBPAddress + ", DashAddress: "+ ws.DashAddress;
				// Found, Not Expired, Not Spent, and SignatureValue, and MonthlyEarnings > 0
				bool fPassesPaymentRequirements = ws.found && !ws.expired && ws.MonthlyEarnings > 0 && ws.SignatureValid && !ws.spent;
				sRow += ", Expired: " + ToYesNo(ws.expired) + ", SigValid: "+ ToYesNo(ws.SignatureValid) + ", BBPSig: " + ToYesNo(ws.BBPSignatureValid) 
					+ ", DashSig: " + ToYesNo(ws.DashSignatureValid) + ", Spent: "+ ToYesNo(ws.spent) + ", Payable: " + ToYesNo(fPassesPaymentRequirements);

				std::string sKey = ws.CPK + "-" + RoundToString(i + 1, 0) + "-" + ws.BBPUTXO + "-" + ws.DashUTXO;

				if (fPassesPaymentRequirements)
					results.push_back(Pair(sKey, sRow));
			}
		}
	}
	results.push_back(Pair("Metrics", "v1.3"));
	// Call out for Dash Stake Metrics
	WhaleMetric wm = GetDashStakeMetrics(chainActive.Tip()->nHeight, true);
	if (dAdvanced == 1)
	{
		results.push_back(Pair("Total Gross Commitments Due Today", wm.nTotalGrossCommitmentsDueToday));
		results.push_back(Pair("Total Future Commitments", wm.nTotalFutureCommitments));
		results.push_back(Pair("Total Gross Future Commitments", wm.nTotalGrossFutureCommitments));
		results.push_back(Pair("Total Commitments Due Today", wm.nTotalCommitmentsDueToday));
		results.push_back(Pair("Total Monthly Commitments", wm.nTotalMonthlyCommitments));
		results.push_back(Pair("Total Gross Monthly Commitments", wm.nTotalGrossMonthlyCommitments));
		results.push_back(Pair("Total Annual Reward", wm.nTotalAnnualReward));
		results.push_back(Pair("Saturation Percent Annual", RoundToString(wm.nSaturationPercentAnnual * 100, 8)));
		results.push_back(Pair("Saturation Percent Monthly", RoundToString(wm.nSaturationPercentMonthly * 100, 8)));
	}
	results.push_back(Pair("Total Stakes Today", wm.nTotalBurnsToday));
	results.push_back(Pair("DWU", RoundToString(GetDWUBasedOnMaturity(30 * 6.5, wm.DWU) * 100, 4)));
	return results;
}

bool IsStakeInBlock(int nHeight, WhaleStake ws, double& nShortPct)
{
	CBlockIndex* pindex = FindBlockByHeight(nHeight);
	const Consensus::Params& consensusParams = Params().GetConsensus();
	if (!pindex) 
		return false;
	CBlock block;
	std::string sMsg;
	nShortPct = 0;
	double nHighAmount = 0;
	if (ReadBlockFromDisk(block, pindex, consensusParams)) 
	{
		double dFoundationDonation = 0;
		for (unsigned int i = 0; i < block.vtx[0]->vout.size(); i++)
		{
			sMsg += block.vtx[0]->vout[i].sTxOutMessage;
			double dAmount = (double)block.vtx[0]->vout[i].nValue / COIN;
			bool fWhaleReward = Contains(RoundToString(dAmount, 4), ".1527");
			std::string sPK = PubKeyToAddress(block.vtx[0]->vout[i].scriptPubKey);
			if (fWhaleReward && (sPK == ws.ReturnAddress || sPK == ws.CPK) && RoundToString(dAmount, 0) == RoundToString(ws.TotalOwed, 0))
				return true;
			if (fWhaleReward && (sPK == ws.ReturnAddress || sPK == ws.CPK))
			{
				if (dAmount > nHighAmount)
					nHighAmount = dAmount;
			}
		}
		if (nHighAmount > 0)
		{
			nShortPct = 1 - (nHighAmount / ws.TotalOwed+.0001);
			if (nShortPct > .99 && nShortPct < 1.01)
					nShortPct = 1.0;
			return true;
		}
	}
	if (block.vtx[0]->vout.size() < 5)
	{
		nShortPct = 1.01;
		return false;
	}
	nShortPct = 1.0;
	return false;
}

UniValue claimreward(const JSONRPCRequest& request)
{
	if (request.fHelp || (request.params.size() != 2))
		throw std::runtime_error("You must specify claimreward non_biblepay_public_address non_biblepay_signature.");
	std::string sCPK = DefaultRecAddress("Christian-Public-Key");
	UniValue results(UniValue::VOBJ);

	std::string sPub = request.params[0].get_str();
	std::string sSig = request.params[1].get_str();
	// ToDo:  Print the campaign name here along with the coin-type.  For now, we start with DASH (76).
	int nKeyType = 76;
	bool fVerified = VerifyNonstandardSignature(sPub, "bbp", sSig, nKeyType);
	if (!fVerified)
	{
		results.push_back(Pair("Results", "Sorry, the signature is invalid.  Please try copying the signature from the source wallet with https://wiki.biblepay.org/Scratchpad.  Maybe it has invalid characters. "));
	}
	else
	{
		// Check to see if there is a reward to be claimed
		std::string sRewardAddress = DefaultRecAddress("Christian-Public-Key");
		std::string sXML = "<non_bbp_pubkey>" + sPub + "</non_bbp_pubkey><non_bbp_signature>" + sSig + "</non_bbp_signature><non_bbp_keytype>" + RoundToString(nKeyType, 0) 
			+ "</non_bbp_keytype><bbp_pubkey>" + sRewardAddress + "</bbp_pubkey>";
		DACResult b = DSQL_ReadOnlyQuery("BMS/CheckReward", sXML);
		std::string sOutcome = ExtractXML(b.Response, "<outcome>", "</outcome>");
		std::string sNarr = ExtractXML(b.Response, "<narr>", "</narr>");
		double nAmount = cdbl(ExtractXML(b.Response, "<amount>", "</amount>"), 2);
		results.push_back(Pair("Outcome", sOutcome));
		results.push_back(Pair("Narrative", sNarr));
		results.push_back(Pair("Amount Rewarded", nAmount));
	}
	return results;
}

UniValue newuser(const JSONRPCRequest& request)
{
	if (request.fHelp || (request.params.size() != 1))
		throw std::runtime_error("You must specify newuser emailaddress");
	std::string sCPK = DefaultRecAddress("Christian-Public-Key");
	UniValue results(UniValue::VOBJ);
	std::string sEmail = request.params[0].get_str();
	std::string sRewardAddress = DefaultRecAddress("Christian-Public-Key");
	std::string sXML = "<email>" + sEmail + "</email><bbp_pubkey>" + sRewardAddress + "</bbp_pubkey>";
	DACResult b = DSQL_ReadOnlyQuery("BMS/NewUser", sXML);
	std::string sOutcome = ExtractXML(b.Response, "<outcome>", "</outcome>");
	std::string sNarr = ExtractXML(b.Response, "<narr>", "</narr>");
	double nAmount = cdbl(ExtractXML(b.Response, "<amount>", "</amount>"), 2);
	results.push_back(Pair("Outcome", sOutcome));
	results.push_back(Pair("Narrative", sNarr));
	results.push_back(Pair("Amount Rewarded", nAmount));
	return results;
}

UniValue dwsquote(const JSONRPCRequest& request)
{
	// Dynamic Whale Staking
	if (request.fHelp || (request.params.size() != 0 && request.params.size() != 1 && request.params.size() != 2 && request.params.size() != 3))
		throw std::runtime_error("You must specify dwsquote [optional 1=my whale stakes only, 2=all whale stakes, optional=User CPK] [optional 1=Include Paid/Unpaid, 2=Include Unpaid only (default), 3=Paid Only, 4=Paid with Anomolies only] [Optional 1=Advanced Totals,0=Default Totals].");
	std::string sCPK = DefaultRecAddress("Christian-Public-Key");
	UniValue results(UniValue::VOBJ);
	double dDetails = 0;
	double dAdvanced = 0;
	std::string sDrillCPK;
	std::string sReportCPK;
	if (request.params.size() > 0)
	{
		dDetails = cdbl(request.params[0].get_str(), 0);
		sDrillCPK = request.params[0].get_str();
		if (sDrillCPK.length() > 20)
		{
			sReportCPK = sDrillCPK;
			dDetails = 4;
		}
	}

	

	double dPaid = 2;
	if (request.params.size() > 1)
		dPaid = cdbl(request.params[1].get_str(), 0);

	if (request.params.size() > 2)
		dAdvanced = cdbl(request.params[2].get_str(), 0);

	double dTotal = 0;
	double dTotalImpact = 0;
	if (dDetails == 1 || dDetails == 2 || dDetails == 4)
	{
		std::vector<WhaleStake> w = GetDWS(true);
		results.push_back(Pair("Total DWS Quantity", (int)w.size()));
			for (int i = 0; i < w.size(); i++)
		{
			WhaleStake ws = w[i];
			bool fIncForPayment = (!ws.paid && dPaid == 2) || (dPaid == 1);
			int nRewardHeight = GetWhaleStakeSuperblockHeight(ws.MaturityHeight);

			if (dPaid == 3 || dPaid == 4)
				fIncForPayment = nRewardHeight < chainActive.Tip()->nHeight;
			double nShtPct = 0;
			bool fInBlock = IsStakeInBlock(nRewardHeight, ws, nShtPct);
			if (nRewardHeight > 1 && nRewardHeight < 233640)
			{
				nShtPct = 0;
				fInBlock = true;
			}

			if (dPaid == 4 && !(nShtPct > 0))
				fIncForPayment = false;

			if (ws.found && fIncForPayment && ((dDetails == 2) || (dDetails==1 && ws.CPK == sCPK) || (dDetails = 4 && ws.CPK == sReportCPK)))
			{
				std::string sInBlock = ToYesNo(fInBlock);
				if (nShtPct > 0)
					sInBlock += " (-" + RoundToString(nShtPct * 100, 2) + "%)";
				if (nRewardHeight > chainActive.Tip()->nHeight)
					sInBlock = "FUTURE";
				std::string sRow = "Burned: " + RoundToString(ws.Amount, 2) + ", Reward: " + RoundToString(ws.TotalOwed, 2) + ", DWU: " 
					+ RoundToString(ws.ActualDWU*100, 4) + ", Duration: " + RoundToString(ws.Duration, 0) + ", BurnHeight: " + RoundToString(ws.BurnHeight, 0) 
					+ ", RewardHeight: " + RoundToString(nRewardHeight, 0) + " [" + RoundToString(ws.MaturityHeight, 0) + "], MaturityDate: " + TimestampToHRDate(ws.MaturityTime) 
					+ ", ReturnAddress: " + ws.ReturnAddress + ", InBlock: " + sInBlock;
					std::string sKey = ws.CPK + " " + RoundToString(i+1, 0);
				if (dPaid == 4)
				{
					sRow += ", Impact: " + RoundToString(ws.TotalOwed * nShtPct, 2);
					dTotalImpact += ws.TotalOwed * nShtPct;
				}
				dTotal += ws.Amount;

				results.push_back(Pair(sKey, sRow));
			}
		}
	}
	// NOTE:  Rob paid for all anomalies up to block 233640, so we need to cross those off this report for prod.
	// Payment Round 2:  Paying BN1wgJYBL2VUCaCP3TYE6nHB9PNwFA2Gz8 5,342,000, Paying B7AuLcF2yYgPzhtPJXdwR8EjNLz4C7GLyH  1,207,000
	results.push_back(Pair("Metrics", "v1.3"));
	// Call out for Whale Metrics
	WhaleMetric wm = GetWhaleMetrics(chainActive.Tip()->nHeight, true);
	if (dPaid==4)
	{
		results.push_back(Pair("Total Impact", dTotalImpact));
	}
	if (dAdvanced == 1)
	{
		results.push_back(Pair("Total Gross Commitments Due Today", wm.nTotalGrossCommitmentsDueToday));
		results.push_back(Pair("Total Future Commitments", wm.nTotalFutureCommitments));
		results.push_back(Pair("Total Gross Future Commitments", wm.nTotalGrossFutureCommitments));
		results.push_back(Pair("Total Commitments Due Today", wm.nTotalCommitmentsDueToday));
		results.push_back(Pair("Total Monthly Commitments", wm.nTotalMonthlyCommitments));
		results.push_back(Pair("Total Gross Monthly Commitments", wm.nTotalGrossMonthlyCommitments));
		results.push_back(Pair("Total Annual Reward", wm.nTotalAnnualReward));
		results.push_back(Pair("Saturation Percent Annual", RoundToString(wm.nSaturationPercentAnnual * 100, 8)));
		results.push_back(Pair("Saturation Percent Monthly", RoundToString(wm.nSaturationPercentMonthly * 100, 8)));
	}
	results.push_back(Pair("Total Gross Burns Today", wm.nTotalGrossBurnsToday));
	
	results.push_back(Pair("Total Burns Today", wm.nTotalBurnsToday));
	results.push_back(Pair("Total Burns in Report", dTotal));

	results.push_back(Pair("30 day DWU", RoundToString(GetDWUBasedOnMaturity(30, wm.DWU) * 100, 4)));
	results.push_back(Pair("90 day DWU", RoundToString(GetDWUBasedOnMaturity(90, wm.DWU) * 100, 4)));
	results.push_back(Pair("180 day DWU", RoundToString(GetDWUBasedOnMaturity(180, wm.DWU) * 100, 4)));
	results.push_back(Pair("365 day DWU", RoundToString(GetDWUBasedOnMaturity(365, wm.DWU) * 100, 4)));
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
					std::string sSigs = "Sigs: " + d.SignatureNarr;
					std::string sRow = "#" + RoundToString(i+1, 0) + ":  Total_Value: $" + RoundToString(d.nValue, 2) + ", Ticker: " + d.ReportTicker 
						+ ", Status: " + RoundToString(nStatus, 0) + ", CPK: " + d.CPK + ", " + sSigs + ", BBPAmount: " + AmountToString(d.nBBPAmount) + ", ForeignAmount: " 
						+ AmountToString(d.nForeignAmount) + ", BBPValue: $" + RoundToString(d.nBBPValueUSD, 2) + ", ForeignValue: $" + RoundToString(d.nForeignValueUSD, 2);
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
				LogPrintf("\nlistutxostakes::Skipping %s", sRow);
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
	results.push_back(Pair("Total Foreign", AmountToString(nTotalForeignAmount)));
    results.push_back(Pair("Total Hybrid BBP", AmountToString(nTotalAmount)));
    
	return results;
}

UniValue protx(const JSONRPCRequest& request)
{
    if (request.fHelp && request.params.empty()) {
        protx_help();
    }

    std::string command;
    if (request.params.size() >= 1) {
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

UniValue _bls(const JSONRPCRequest& request)
{
    if (request.fHelp && request.params.empty()) {
        bls_help();
    }

    std::string command;
    if (request.params.size() >= 1) {
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

static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         okSafeMode
  //  --------------------- ------------------------  -----------------------  ----------
	{ "evo",                "bookname",                     &bookname,                      false, {}  },
	{ "evo",                "books",                        &books,                         false, {}  },
	{ "evo",                "datalist",                     &datalist,                      false, {}  },
	{ "evo",                "claimreward",                  &claimreward,                   false, {}  },
	{ "evo",                "dashpay",                      &dashpay,                       false, {}  },
	{ "evo",                "dashstakequote",               &dashstakequote,                false, {}  },
	{ "evo",                "dashstake",                    &dashstake,                     false, {}  },
	{ "evo",                "give",                         &give,                          false, {}  },
	{ "evo",                "getpin",                       &getpin,                        false, {}  },
	{ "evo",                "dws",                          &dws,                           false, {}  },
	{ "evo",                "dwsquote",                     &dwsquote,                      false, {}  },
	{ "evo",                "easystake",                    &easystake,                     false, {}  },
	{ "evo",                "listutxostakes",               &listutxostakes,                false, {}  },
	{ "evo",                "listdacdonations",             &listdacdonations,              false, {}  },
	{ "evo",                "listexpenses",                 &listexpenses,                  false, {}  },
	{ "evo",                "easybbpstake",                 &easybbpstake,                  false, {}  },
	{ "evo",                "newuser",                      &newuser,                       false, {}  },
	{ "evo",                "utxostake",                    &utxostake,                     false, {}  },
	{ "evo",                "hexblocktocoinbase",           &hexblocktocoinbase,            false, {}  },
	{ "evo",                "getpobhhash",                  &getpobhhash,                   false, {}  },
    { "evo",                "bls",                          &_bls,                          false, {}  },
    { "evo",                "protx",                        &protx,                         false, {}  },
	{ "evo",                "createnonfinancialtransaction",&createnonfinancialtransaction, false, {}  },
	{ "evo",                "nonfinancialtxtojson",         &nonfinancialtxtojson,          false, {}  },
	{ "evo",                "faucetcode",                   &faucetcode,                    false, {}  },
	{ "evo",                "trackdashpay",                 &trackdashpay,                  false, {}  },
	{ "evo",                "sendgscc",                     &sendgscc,                      false, {}  },
	{ "evo",                "versionreport",                &versionreport,                 false, {}  },
};

void RegisterEvoRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++) {
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
    }
}
