// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_EMAIL_H
#define BITCOIN_EMAIL_H

#include "serialize.h"
#include "sync.h"
#include "rpcpog.h"

#include <map>
#include <set>
#include <stdint.h>
#include <string>

class CEmail;
class CNode;
class CConnman;
class uint256;
class WalletModel;

extern std::map<uint256, CEmail> mapEmails;
extern CCriticalSection cs_mapEmails;

/** An e-mail Request is a network wide relayed request for a missing e-mail **/

class CEmailRequest
{
public:
	uint256 RequestID;

	ADD_SERIALIZE_METHODS;
	template <typename Stream, typename Operation>
	inline void SerializationOp(Stream& s, Operation ser_action) 
	{
        READWRITE(RequestID);
    }

    void SetNull();
    bool IsNull() const;

    CEmailRequest()
    {
        SetNull();
    }

    uint256 GetHash() const;

    bool RelayTo(CNode* pnode, CConnman& connman) const;
};
/** An e-mail object represents an encrypted decentralized e-mail.  **/
class CEmail
{
public:
	int nType;
    int nVersion;
    int64_t nTime;
	bool Encrypted;
	std::string Headers;
	std::string Body;
	std::string FromEmail;
	std::string ToEmail;
	uint256 AccessHash;
	void setWalletModel(WalletModel *walletModel);
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) 
	{
        READWRITE(this->nVersion);
        READWRITE(nTime);
        READWRITE(Encrypted);
		READWRITE(AccessHash);
        READWRITE(LIMITED_STRING(Headers, 32767));
        READWRITE(LIMITED_STRING(Body, 3100000));
		READWRITE(LIMITED_STRING(FromEmail, 1000));
		READWRITE(LIMITED_STRING(ToEmail, 32767));
    }

    void SetNull();
    bool IsNull() const;
	std::string Serialize1();
	void Deserialize1(std::string sData);

	int EDeserialize(uint256 Hash);
	double getMemoryUsage();

	std::string GetFileName();

    std::string ToString() const;
	std::string Serialized() const;

    CEmail()
    {
        SetNull();
    }

    uint256 GetHash();
	bool ProcessEmail(); 
	bool IsMine();

    bool RelayTo(CNode* pnode, CConnman& connman);
	
    static void Notify(const std::string& strMessage, bool fThread);

    /*
     * Get a copy of an Email. Returns a null if it is not found.
     */
    static CEmail getEmailByHash(const uint256 &hash);
};

#endif // BITCOIN_EMAIL_H
