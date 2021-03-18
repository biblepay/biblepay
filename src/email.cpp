// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "email.h"
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
#include <boost/filesystem.hpp>

using namespace std;

class CEmail;
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
    nVersion = 2;
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

std::string CEmail::Serialize1()
{
	std::string sData = "<emailpacket1><headers1>" + Headers + "</headers1><fromemail1>" + FromEmail + "</fromemail1><toemail1>" + ToEmail + "</toemail1><time1>" 
		+ RoundToString(nTime, 0) + "</time1><body1>" + Body + "</body1><version1>" + RoundToString(nVersion, 0) + "</version1></emailpacket1>";
	return sData;
}

void CEmail::Deserialize1(std::string sData)
{
	std::string sPacket = ExtractXML(sData, "<emailpacket1>", "</emailpacket1>");

	FromEmail = ExtractXML(sPacket, "<fromemail1>", "</fromemail1>");
	ToEmail = ExtractXML(sPacket, "<toemail1>", "</toemail1>");
	nTime = cdbl(ExtractXML(sPacket, "<time1>", "</time1>"), 0);
	Body = ExtractXML(sPacket, "<body1>", "</body1>");
	Headers = ExtractXML(sPacket, "<headers1>", "</headers1>");
	nVersion = (int)cdbl(ExtractXML(sPacket, "<version1>", "</version1>"), 0);
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
    int iDes = retval.EDeserialize(hash);
	if (!retval.IsNull())
	{
		mapEmails[retval.GetHash()] = retval;
	}
	bool fIsMine = retval.IsMine();
    return retval;
}

double CEmail::getMemoryUsage()
{
	/*
	int iCount = 0;
	double iSize = 0;
	for (auto item : mapEmails) 
	{
		iCount++;
		iSize += item.second.Body.length();
		CEmail e = item.second;
		e.EDeserialize(item.first);
		LogPrintf("\nGetMemoryUsage %s ", Mid(e.Body, 0, 100));
		
	}
	return iSize;
	*/
	return 0;
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
	bool fDebuggingEmail = cdbl(gArgs.GetArg("-debuggingemail", "0"), 0) == 1;

	Deserialize1(data);
	int64_t nElapsed = GetAdjustedTime() - nTime;
	if (nElapsed > MAX_EMAIL_AGE)
	{
		boost::filesystem::path pathEmail = sSource.c_str();
		if (boost::filesystem::exists(pathEmail)) 
		{
			boost::filesystem::remove(pathEmail);
			if (fDebuggingEmail)
				LogPrintf("\nSMTP::DeletingEmail %s", sSource.c_str());
			Body = std::string();
			nTime = 0;
			return -2;
		}
	}
	return 1;
}


bool ScanBody(std::string data)
{
	std::string sCopy(data.c_str());
	boost::to_upper(sCopy);
	bool fScan = (Contains(sCopy, "SUBJECT:") && Contains(sCopy, "TO:") && Contains(sCopy, "FROM:"));
	return fScan;
}

bool CEmail::IsRead()
{
	bool fRead = HashExistsInDataFile("emails_read", GetHash().GetHex());
	return fRead;
}

bool CEmail::IsMine()
{
	bool fExpired = nTime < (GetAdjustedTime() - (86400 * 30));
	if (fExpired || Body.empty() || FromEmail.empty() || ToEmail.empty() || nVersion < 2 || nVersion > 3)
		return false;

	if (ToEmail.find(msMyInternalEmailAddress) != std::string::npos && nVersion == 3 && !msMyInternalEmailAddress.empty())
	{
		// Check for decryption
		std::string sPrivPath = GetSANDirectory4() + "privkey.priv";
		std::string sError;
		std::string sDec = RSA_Decrypt_String(sPrivPath, Body, sError);
		bool fScan = findStringCaseInsensitive(sDec, "subject:");
		if (sError.empty() && fScan)
		{
			return true;
		}
		else
		{
			LogPrintf("\nCEmail::IsMine()::ERROR::Unable to Decrypt email destined to Us!  From %s, Length %f ", FromEmail.c_str(), Body.length());
		}
	}

	bool fScan = findStringCaseInsensitive(Body, "subject:");
	if (!fScan)
		return false;

	if (ToEmail.find("all@biblepay.core") != std::string::npos && nVersion == 2)
	{
		// Mail to all
		return true;
	}
	else if (ToEmail.find(msMyInternalEmailAddress) != std::string::npos && nVersion == 2 && !msMyInternalEmailAddress.empty())
	{
		return true;
	}
	return false;
}

bool CEmail::ProcessEmail()
{
	if (IsNull())
	{
		return false;
	}
	if (Body.empty())
	{
		return false;
	}

	uint256 MyHash = GetHash();
	
	map<uint256, CEmail>::iterator mi = mapEmails.find(MyHash);
    if(mi != mapEmails.end())
	{
		mapEmails.erase(MyHash);
	}
	// Store to disk
	// Verify the disk is not more than 90% full too
	std::string sTarget = GetFileName();
	int64_t nSz = GETFILESIZE(sTarget);
	int64_t nAge = GetAdjustedTime() - nTime;
	if (nAge > MAX_EMAIL_AGE)
	{
		LogPrintf("\nSMTP::ProcessEmail -- Email too old %f ", nAge);
		return false;
	}
	if (nSz <= 0)
	{
		std::string sData = Serialize1();
		WriteDataToFile(sTarget, sData);
		LogPrintf("\nSMTP::WriteDataToFile2 %f %s [%s]", sData.length(), MyHash.GetHex(), ToEmail);
	}
	// Relinquish Memory
	Body = std::string();
	mapEmails.insert(make_pair(MyHash, *this));
    return true;
}

