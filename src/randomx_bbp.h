// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_RANDOMX_BBP_H
#define BITCOIN_RANDOMX_BBP_H

#include "crypto/RandomX/src/randomx.h"
#include "uint256.h"

uint256 RandomX_Hash(uint256 hash, uint256 uKey, int iThreadID);
uint256 RandomX_Hash(std::vector<unsigned char> data0, uint256 uKey, int iThreadID);
uint256 RandomX_Hash(std::vector<unsigned char> data0, std::vector<unsigned char> datakey);
uint256 RandomX_SlowHash(std::vector<unsigned char> data0, uint256 uKey);
uint256 GetRandomXHash3(std::string sHeaderHex, uint256 key, int iThreadID);

#endif
