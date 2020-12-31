// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "email.h"
#include "base58.h"
#include "clientversion.h"
#include "net.h"
#include "pubkey.h"
#include "timedata.h"

#include "ui_interface.h"
#include "util.h"
#include "utilstrencodings.h"
#include "rpcpog.h"
#include <iostream>
#include <fstream>
#include <stdint.h>
#include <algorithm>
#include <map>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/foreach.hpp>
#include <boost/thread.hpp>
#include <boost/algorithm/string.hpp>
using namespace std;

CCriticalSection cs_mapEmails;

void CEmailRequest::SetNull()
{
	RequestID = uint256S("0x0");
}

bool CEmailRequest::IsNull() const
{
	return (RequestID == uint256S("0x0"));
}

uint256 CEmailRequest::GetHash() const
{
	CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << RequestID;
    return ss.GetHash();
}

bool CEmailRequest::RelayTo(CNode* pnode, CConnman& connman) const
{
	if (pnode->nVersion == 0)
		return false;
	if (pnode->setKnown.insert(GetHash()).second)
	{
		connman.PushMessage(pnode, CNetMsgMaker(pnode->GetSendVersion()).Make(NetMsgType::EMAILREQUEST, *this));
		return true;
	}
	return false;
}

void CEmail::SetNull()
{
    nVersion = 1;
    nTime = 0;
	Encrypted = false;
	Headers.clear();
	Body.clear();
	FromEmail.clear();
	ToEmail.clear();
	AccessHash = uint256S("0x0");
}

std::string CEmail::ToString() const
{
    return strprintf(
        "CEmail(\n"
        "    nVersion     = %d\n"
        "    nTime        = %d\n"
        "    Encrypted    = %d\n"
        "    Headers      = \"%s\"\n"
        "    Body         = \"%s\"\n"
	    "    FromEmail    = \"%s\"\n"
        "    ToEmail      = \"%s\"\n"
		"    AccessHash   = \"%s\"\n"
        ")\n",
        nVersion,
        nTime,
 		Encrypted,
		Headers,
		Body,
        FromEmail,
		ToEmail,
		AccessHash.GetHex());
}

std::string CEmail::Serialize1() const
{
	std::string sData = "<headers1>" + Headers + "</headers1><fromemail1>" + FromEmail + "</fromemail1><toemail1>" + ToEmail + "</toemail1><time1>" + RoundToString(nTime, 0) + "</time1><body1>" + Body + "</body1>";
	return sData;
}

void CEmail::Deserialize1(std::string sData)
{
	FromEmail = ExtractXML(sData, "<fromemail1>", "</fromemail1>");
	ToEmail = ExtractXML(sData, "<toemail1>", "</toemail1>");
	nTime = cdbl(ExtractXML(sData, "<time1>", "</time1>"), 0);
	Body = ExtractXML(sData, "<body1>", "</body1>");
	Headers = ExtractXML(sData, "<headers1>", "</headers1>");
}


bool CEmail::IsNull() const
{
    return (nTime == 0);
}

uint256 CEmail::GetHash()
{
	CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
	ss << Headers;
    ss << nTime;
	ss << FromEmail;
	ss << ToEmail;
	return ss.GetHash();
}


bool CEmail::RelayTo(CNode* pnode, CConnman& connman)
{
    if (pnode->nVersion == 0)
		return false;
    // returns true if wasn't already contained in the set
	uint256 uHash = GetHash();
    if (pnode->setKnown.insert(uHash).second)
    {
		CEmail cemail;
		int iDes1 = cemail.EDeserialize(uHash);
		if (iDes1 == 1)
		{
			connman.PushMessage(pnode, CNetMsgMaker(pnode->GetSendVersion()).Make(NetMsgType::EMAIL, cemail));
			return true;
		}
    }
    return false;
}


CEmail CEmail::getEmailByHash(const uint256 &hash)
{
    CEmail retval;
    //map<uint256, CEmail>::iterator mi = mapEmails.find(hash);
    //if(mi != mapEmails.end())
     //       retval = mi->second;
	// Ensure the body is deserialized
	int iDes = retval.EDeserialize(hash);
	if (retval.IsNull())
	{
		mapEmails[retval.GetHash()] = retval;
	}
	LogPrintf("\r\nDeserialized %f %f %s %s", retval.Body.length(),  iDes, retval.FromEmail, retval.ToEmail);

    return retval;
}

double CEmail::getMemoryUsage()
{
	int iCount = 0;
	double iSize = 0;
	for (auto item : mapEmails) 
	{
		iCount++;
		iSize += item.second.Body.length();
		CEmail e = item.second;
		e.EDeserialize(item.first);
		LogPrintf("\nGetMemoryUsage %s ", Mid(e.Body, 1, 100));
		
	}
	return iSize;
}

std::string CEmail::GetFileName()
{
	std::string sFileName = "email_" + GetHash().GetHex() + ".eml";
	std::string sTarget = GetSANDirectory4() + sFileName;
	return sTarget;
}

int CEmail::EDeserialize(uint256 Hash)
{
	std::string sFileName = "email_" + Hash.GetHex() + ".eml";
	std::string sSource = GetSANDirectory4() + sFileName;
	int64_t nSz = GETFILESIZE(sSource);
	if (nSz <= 0)
	{
		return -9;
	}

	std::vector<char> bEmail = ReadAllBytesFromFile(sSource.c_str());
	std::string data = std::string(bEmail.begin(), bEmail.end());

	Deserialize1(data);
	return 1;
}

bool CEmail::IsMine()
{
	if (ToEmail.find(msMyInternalEmailAddress) != std::string::npos)
		return true;
	return false;
}

bool CEmail::ProcessEmail()
{
	map<uint256, CEmail>::iterator mi = mapEmails.find(GetHash());
    if(mi != mapEmails.end())
		return false;
	// Never seen this email:
	// Store to disk
	// Verify the disk is not more than 90% full too
	std::string sTarget = GetFileName();
	int64_t nSz = GETFILESIZE(sTarget);
	if (nSz <= 0)
	{
		/*
		CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
		ss << *(CEmail*)this;
		std::vector<unsigned char> uData(ss.begin(), ss.end());
		WriteUnsignedBytesToFile(sTarget.c_str(), uData);
		*/
		std::string sData = Serialize1();
		WriteDataToFile(sTarget, sData);
	}
	// Relinquish Memory
	LogPrintf("\npop3 collection size %f", mapEmails.size());
	uint256 MyHash = GetHash();
	Body = std::string();
	mapEmails.insert(make_pair(MyHash, *this));

    return true;
}

