// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chat.h"
#include "base58.h"
#include "clientversion.h"
#include "net.h"
#include "pubkey.h"
#include "timedata.h"

#include "ui_interface.h"
#include "util.h"
#include "utilstrencodings.h"
#include "rpcpog.h"
#include <stdint.h>
#include <algorithm>
#include <map>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/foreach.hpp>
#include <boost/thread.hpp>
#include <boost/algorithm/string.hpp>
using namespace std;

map<uint256, CChat> mapChats;
CCriticalSection cs_mapChats;

void CChat::SetNull()
{
    nVersion = 1;
    nTime = 0;
    nID = 0;
	bPrivate = false;
	bEncrypted = false;
    nPriority = 0;
	sPayload.clear();
    sFromNickName.clear();
    sToNickName.clear();
	sDestination.clear();
}

std::string CChat::ToString() const
{
    return strprintf(
        "CChat(\n"
        "    nVersion     = %d\n"
        "    nTime        = %d\n"
        "    nID          = %d\n"
        "    bPrivate     = %d\n"
		"    bEncrypted   = %d\n"
        "    nPriority    = %d\n"
		"    sDestination = %s\n"
        "    sPayload     = \"%s\"\n"
	    "    sFromNickName= \"%s\"\n"
        "    sToNickName  = \"%s\"\n"
        ")\n",
        nVersion,
        nTime,
        nID,
        bPrivate,
		bEncrypted,
		nPriority,
		sDestination,
        sPayload,
        sFromNickName,
        sToNickName);
}

std::string CChat::Serialized() const
{
   return strprintf(
	   "%d<COL>%d<COL>%d<COL>%d<COL>%d<COL>%d<COL>%s<COL>%s<COL>%s<COL>%s",
        nVersion,
        nTime,
        nID,
        bPrivate,
		bEncrypted,
		nPriority,
		sDestination,
        sPayload,
        sFromNickName,
        sToNickName);
}

void CChat::Deserialize(std::string sData)
{
	std::vector<std::string> vInput = Split(sData.c_str(),"<COL>");
	if (vInput.size() > 8)
	{
		this->nVersion = cdbl(vInput[0], 0);
		this->nTime = cdbl(vInput[1], 0);
		this->nID = cdbl(vInput[2], 0);
		this->bPrivate = (bool)(cdbl(vInput[3], 0));
		this->bEncrypted = (bool)(cdbl(vInput[4], 0));
		this->nPriority = cdbl(vInput[5], 0);
		this->sDestination = vInput[6];
		this->sPayload = vInput[7];
		this->sFromNickName = vInput[8];
		this->sToNickName = vInput[9];
	}
}

bool CChat::IsNull() const
{
    return (nTime == 0);
}

uint256 CChat::GetHash() const
{
	CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << nTime;
	ss << nPriority;	
	ss << sPayload;
	ss << sFromNickName;
	ss << sToNickName;
	ss << sDestination;
    return ss.GetHash();
}

bool CChat::RelayTo(CNode* pnode, CConnman& connman) const
{
    if (pnode->nVersion == 0) return false;
    // returns true if wasn't already contained in the set
    if (pnode->setKnown.insert(GetHash()).second)
    {
		connman.PushMessage(pnode, CNetMsgMaker(pnode->GetSendVersion()).Make(NetMsgType::CHAT, *this));
        return true;
    }
    return false;
}

CChat CChat::getChatByHash(const uint256 &hash)
{
    CChat retval;
    {
        LOCK(cs_mapChats);
        map<uint256, CChat>::iterator mi = mapChats.find(hash);
        if(mi != mapChats.end())
            retval = mi->second;
    }
    return retval;
}

bool CChat::ProcessChat()
{
	map<uint256, CChat>::iterator mi = mapChats.find(GetHash());
    if(mi != mapChats.end()) return false;
	// Never seen this chat record
	mapChats.insert(make_pair(GetHash(), *this));
    // Notify UI 
	UserRecord rec = GetMyUserRecord();
	if (boost::iequals(rec.NickName, sToNickName) && sPayload == "<RING>" && bPrivate && !bEncrypted)
	{
		msPagedFrom = sFromNickName;
		mlPaged++;
	}
	else if (boost::iequals(rec.NickName, sToNickName) && sPayload == "<RING>" && bEncrypted)
	{
		msPagedFrom = sFromNickName;
		mlPagedEncrypted++;
	}
	
	uiInterface.NotifyChatEvent(Serialized());
    return true;
}

