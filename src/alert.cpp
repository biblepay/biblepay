// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "alert.h"

#include "clientversion.h"
#include "net.h"
#include "netmessagemaker.h"
#include "pubkey.h"
#include "timedata.h"
#include "ui_interface.h"
#include "util.h"
#include "utilstrencodings.h"

#include <stdint.h>
#include <algorithm>
#include <map>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/thread.hpp>

std::map<uint256, CAlert> mapAlerts;
CCriticalSection cs_mapAlerts;

void CUnsignedAlert::SetNull()
{
    nVersion = 1;
    nRelayUntil = 0;
    nExpiration = 0;
    nID = 0;
    nCancel = 0;
    setCancel.clear();
    nMinVer = 0;
    nMaxVer = 0;
    setSubVer.clear();
    nPriority = 0;

    strComment.clear();
    strStatusBar.clear();
    strReserved.clear();
}

std::string CUnsignedAlert::ToString() const
{
    std::string strSetCancel;
    for (const auto& n : setCancel)
        strSetCancel += strprintf("%d ", n);
    std::string strSetSubVer;
    for (const auto& str : setSubVer)
        strSetSubVer += "\"" + str + "\" ";
    return strprintf(
        "CAlert(\n"
        "    nVersion     = %d\n"
        "    nRelayUntil  = %d\n"
        "    nExpiration  = %d\n"
        "    nID          = %d\n"
        "    nCancel      = %d\n"
        "    setCancel    = %s\n"
        "    nMinVer      = %d\n"
        "    nMaxVer      = %d\n"
        "    setSubVer    = %s\n"
        "    nPriority    = %d\n"
        "    strComment   = \"%s\"\n"
        "    strStatusBar = \"%s\"\n"
        ")\n",
        nVersion,
        nRelayUntil,
        nExpiration,
        nID,
        nCancel,
        strSetCancel,
        nMinVer,
        nMaxVer,
        strSetSubVer,
        nPriority,
        strComment,
        strStatusBar);
}

void CAlert::SetNull()
{
    CUnsignedAlert::SetNull();
    vchMsg.clear();
    vchSig.clear();
}

bool CAlert::IsNull() const
{
    return (nExpiration == 0);
}

uint256 CAlert::GetHash() const
{
    return Hash(this->vchMsg.begin(), this->vchMsg.end());
}

bool CAlert::IsInEffect() const
{
    return (GetAdjustedTime() < nExpiration);
}

bool CAlert::Cancels(const CAlert& alert) const
{
    if (!IsInEffect())
        return false; // this was a no-op before 31403
    return (alert.nID <= nCancel || setCancel.count(alert.nID));
}

bool CAlert::AppliesTo(int nVersion, const std::string& strSubVerIn) const
{
    // TODO: rework for client-version-embedded-in-strSubVer ?
    return (IsInEffect() &&
            nMinVer <= nVersion && nVersion <= nMaxVer &&
            (setSubVer.empty() || setSubVer.count(strSubVerIn)));
}

bool CAlert::AppliesToMe() const
{
    return AppliesTo(PROTOCOL_VERSION, FormatSubVersion(CLIENT_NAME, CLIENT_VERSION, std::vector<std::string>()));
}

bool CAlert::RelayTo(CNode* pnode, CConnman& connman) const
{
    if (!IsInEffect())
        return false;
    // don't relay to nodes which haven't sent their version message
    if (pnode->nVersion == 0)
        return false;
    // returns true if wasn't already contained in the set
    if (pnode->setKnown.insert(GetHash()).second)
    {
        if (AppliesTo(pnode->nVersion, pnode->strSubVer) ||
            AppliesToMe() ||
            GetAdjustedTime() < nRelayUntil)
        {
            connman.PushMessage(pnode, CNetMsgMaker(pnode->GetSendVersion()).Make(NetMsgType::ALERT, *this));
            return true;
        }
    }
    return false;
}

bool CAlert::Sign()
{
    CDataStream sMsg(SER_NETWORK, CLIENT_VERSION);
    sMsg << *(CUnsignedAlert*)this;
    vchMsg = std::vector<unsigned char>(sMsg.begin(), sMsg.end());
    CBitcoinSecret vchSecret;
    if (!vchSecret.SetString(gArgs.GetArg("-alertkey", "")))
    {
        printf("CAlert::SignAlert() : vchSecret.SetString failed\n");
        return false;
    }
    CKey key = vchSecret.GetKey();
	if (!key.SignCompact(Hash(vchMsg.begin(), vchMsg.end()), vchSig))
    {
        printf("CAlert::SignAlert() : key.Sign failed\n");
        return false;
    }

    return true;
}


bool CAlert::CheckSignature() const
{
	/*
	const Consensus::Params& consensusParams = Params().GetConsensus();
	CBitcoinAddress addrAlertPubAddress(consensusParams.FoundationAddress);

	if (!addrAlertPubAddress.IsValid()) 
	{
		LogPrintf("Invalid alert address. %f", 700);
		return false;
	}

	CKeyID keyID2;
	if (!addrAlertPubAddress.GetKeyID(keyID2)) 
	{
		LogPrintf("Address does not refer to key %f", 701);
		return false;
	}

	bool fInvalid = false;

	CDataStream sMsg(vchMsg, SER_NETWORK, CLIENT_VERSION);
    sMsg >> *(CUnsignedAlert*)this;

	CPubKey pubkey2;
    if (!pubkey2.RecoverCompact(Hash(vchMsg.begin(), vchMsg.end()), vchSig)) 
	{
		LogPrintf("Unable to recover public key. %f", 702);
		return false;
	}
	return (pubkey2.GetID() == keyID2);
	*/
	return false;
}

CAlert CAlert::getAlertByHash(const uint256 &hash)
{
    CAlert retval;
    {
        LOCK(cs_mapAlerts);
        std::map<uint256, CAlert>::iterator mi = mapAlerts.find(hash);
        if(mi != mapAlerts.end())
            retval = mi->second;
    }
    return retval;
}

bool CAlert::ProcessAlert(const std::vector<unsigned char>& alertKey, bool fThread) const
{
	/*
    if (!CheckSignature())
        return false;
    if (!IsInEffect())
        return false;

    // alert.nID=max is reserved for if the alert key is
    // compromised. It must have a pre-defined message,
    // must never expire, must apply to all versions,
    // and must cancel all previous
    // alerts or it will be ignored (so an attacker can't
    // send an "everything is OK, don't panic" version that
    // cannot be overridden):
    int maxInt = std::numeric_limits<int>::max();
    if (nID == maxInt)
    {
        if (!(
                nExpiration == maxInt &&
                nCancel == (maxInt-1) &&
                nMinVer == 0 &&
                nMaxVer == maxInt &&
                setSubVer.empty() &&
                nPriority == maxInt &&
                strStatusBar == "URGENT: Alert key compromised, upgrade required"
                ))
            return false;
    }

    {
        LOCK(cs_mapAlerts);
        // Cancel previous alerts
        for (std::map<uint256, CAlert>::iterator mi = mapAlerts.begin(); mi != mapAlerts.end();)
        {
            const CAlert& alert = (*mi).second;
            if (Cancels(alert))
            {
                LogPrint(BCLog::NET, "cancelling alert %d\n", alert.nID);
                uiInterface.NotifyAlertChanged((*mi).first, CT_DELETED);
                mapAlerts.erase(mi++);
            }
            else if (!alert.IsInEffect())
            {
                LogPrint(BCLog::NET, "expiring alert %d\n", alert.nID);
                uiInterface.NotifyAlertChanged((*mi).first, CT_DELETED);
                mapAlerts.erase(mi++);
            }
            else
                mi++;
        }

        // Check if this alert has been cancelled
        for (const auto& item : mapAlerts)
        {
            const CAlert& alert = item.second;
            if (alert.Cancels(*this))
            {
                LogPrint(BCLog::NET, "alert already cancelled by %d\n", alert.nID);
                return false;
            }
        }

        // Add to mapAlerts
        mapAlerts.insert(std::make_pair(GetHash(), *this));
        // Notify UI and -alertnotify if it applies to me
        if(AppliesToMe())
        {
            uiInterface.NotifyAlertChanged(GetHash(), CT_NEW);
            Notify(strStatusBar, fThread);
        }
    }

    LogPrint(BCLog::NET, "accepted alert %d, AppliesToMe()=%d\n", nID, AppliesToMe());
    return true;
	*/
	return false;
}

void
CAlert::Notify(const std::string& strMessage, bool fThread)
{
    std::string strCmd = gArgs.GetArg("-alertnotify", "");
    if (strCmd.empty()) return;

    // Alert text should be plain ascii coming from a trusted source, but to
    // be safe we first strip anything not in safeChars, then add single quotes around
    // the whole string before passing it to the shell:
    std::string singleQuote("'");
    std::string safeStatus = SanitizeString(strMessage);
    safeStatus = singleQuote+safeStatus+singleQuote;
    boost::replace_all(strCmd, "%s", safeStatus);

    if (fThread)
        boost::thread t(runCommand, strCmd); // thread runs free
    else
        runCommand(strCmd);
}
