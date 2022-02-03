// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "randomx_bbp.h"
#include "hash.h"
#include "util.h"
#include "rpcpog.h"
#include <utilstrencodings.h>

static std::map<int, randomx_cache*> rxcache;
static std::map<int, randomx_vm*> myvm;
static std::map<int, bool> fInitialized;
static std::map<int, bool> fBusy;
static std::map<int, uint256> msGlobalKey;

// Statement from Rob Andrews about RandomX in regards to MAC-OS support
// May 13th, 2021
// RandomX works on Linux compiles and Windows compiles, but we are unable to compile the randomx hash algorithm on the MAC.  
// To my knowledge, since RandomX uses a virtual machine, we need to reach out to Tevador for assistance with a special slow clang build, so that we can link to that library.
// In the mean time, since Mac is only 1% of our user base, we are providing a conditional compilation solution that blocks the RandomX hash functions on mac.
// This effectively turns the MAC nodes into SPV clients.  (This means that BiblePay runs on MAC, but in order to mine, they will need to convince Tevador to build an XMRIG miner for MAC).


static std::map<int, std::mutex> cs_rxhash;
uint256 GetRandomXHash3(std::string sHeaderHex, uint256 key, int iThreadID)
{
    std::unique_lock<std::mutex> lock(cs_rxhash[iThreadID]);
    std::string randomXBlockHeader = ExtractXML(sHeaderHex, "<rxheader>", "</rxheader>");
    std::vector<unsigned char> data0 = ParseHex(randomXBlockHeader);
    uint256 uRXMined = RandomX_Hash(data0, key, iThreadID);
    return uRXMined;
}


void init(uint256 uKey, int iThreadID)
{
#if defined(MAC_OSX)
	return;
#endif

	std::vector<unsigned char> hashKey = std::vector<unsigned char>(uKey.begin(), uKey.end());
	randomx_flags flags = randomx_get_flags();
	rxcache[iThreadID] = randomx_alloc_cache(flags);
	randomx_init_cache(rxcache[iThreadID], hashKey.data(), hashKey.size());
	myvm[iThreadID] = randomx_create_vm(flags, rxcache[iThreadID], NULL);
	fInitialized[iThreadID] = true;
	msGlobalKey[iThreadID] = uKey;
}

void destroy(int iThreadID)
{
#if defined(MAC_OSX)
	return;
#endif

	randomx_destroy_vm(myvm[iThreadID]);
	randomx_release_cache(rxcache[iThreadID]);
	fInitialized[iThreadID] = false;
	fBusy[iThreadID] = false;
}

uint256 RandomX_Hash(uint256 hash, uint256 uKey, int iThreadID)
{
#if defined(MAC_OSX)
	uint256 u = uint256S("0x0");
	return u;
#endif

		if (fInitialized[iThreadID] && msGlobalKey[iThreadID] != uKey)
		{
			destroy(iThreadID);
		}

		if (!fInitialized[iThreadID] || uKey != msGlobalKey[iThreadID])
		{
			init(uKey, iThreadID);
		}
		std::vector<unsigned char> hashIn = std::vector<unsigned char>(hash.begin(), hash.end());
		char *hashOut1 = (char*)malloc(RANDOMX_HASH_SIZE + 1);
		fBusy[iThreadID] = true;
		randomx_calculate_hash(myvm[iThreadID], hashIn.data(), hashIn.size(), hashOut1);
		std::vector<unsigned char> data1(hashOut1, hashOut1 + RANDOMX_HASH_SIZE);
		free(hashOut1);
		fBusy[iThreadID] = false;
		return uint256(data1);
}

uint256 RandomX_Hash(std::vector<unsigned char> data0, uint256 uKey, int iThreadID)
{
#if defined(MAC_OSX)
	uint256 u = uint256S("0x0");
	return u;
#endif

		if (fInitialized[iThreadID] && msGlobalKey[iThreadID] != uKey)
		{
			destroy(iThreadID);
		}

		if (!fInitialized[iThreadID] || uKey != msGlobalKey[iThreadID])
		{
			init(uKey, iThreadID);
		}
		char *hashOut0 = (char*)malloc(RANDOMX_HASH_SIZE + 1);
		fBusy[iThreadID] = true;
		randomx_calculate_hash(myvm[iThreadID], data0.data(), data0.size(), hashOut0);
		std::vector<unsigned char> data1(hashOut0, hashOut0 + RANDOMX_HASH_SIZE);
		free(hashOut0);
		fBusy[iThreadID] = false;
		return uint256(data1);
}


uint256 RandomX_Hash(std::vector<unsigned char> data0, std::vector<unsigned char> datakey)
{
#if defined(MAC_OSX)
	uint256 u = uint256S("0x0");
	return u;
#endif

	int iThreadID = 101;
	randomx_flags flags = randomx_get_flags();
	rxcache[iThreadID] = randomx_alloc_cache(flags);
	randomx_init_cache(rxcache[iThreadID], datakey.data(), datakey.size());
	myvm[iThreadID] = randomx_create_vm(flags, rxcache[iThreadID], NULL);
	char *hashOut0 = (char*)malloc(RANDOMX_HASH_SIZE + 1);
	randomx_calculate_hash(myvm[iThreadID], data0.data(), data0.size(), hashOut0);
	std::vector<unsigned char> data1(hashOut0, hashOut0 + RANDOMX_HASH_SIZE);
	free(hashOut0);
	randomx_destroy_vm(myvm[iThreadID]);
	randomx_release_cache(rxcache[iThreadID]);
	return uint256(data1);
}


uint256 RandomX_SlowHash(std::vector<unsigned char> data0, uint256 uKey)
{
#if defined(MAC_OSX)
	uint256 u = uint256S("0x0");
	return u;
#endif

	randomx_cache* rxc;
	randomx_vm* vm1;
	std::vector<unsigned char> hashKey = std::vector<unsigned char>(uKey.begin(), uKey.end());
	randomx_flags flags = randomx_get_flags();
	rxc = randomx_alloc_cache(flags);
	randomx_init_cache(rxc, hashKey.data(), hashKey.size());
	vm1 = randomx_create_vm(flags, rxc, NULL);
	char *hashOut0 = (char*)malloc(RANDOMX_HASH_SIZE + 1);
	randomx_calculate_hash(vm1, data0.data(), data0.size(), hashOut0);
	std::vector<unsigned char> data1(hashOut0, hashOut0 + RANDOMX_HASH_SIZE);
	randomx_destroy_vm(vm1);
	randomx_release_cache(rxc);
	return uint256(data1);
}

