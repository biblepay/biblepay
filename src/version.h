// Copyright (c) 2012-2014 The Bitcoin Core developers
// Copyright (c) 2014-2020 The Däsh Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VERSION_H
#define BITCOIN_VERSION_H

/**
 * network protocol versioning
 */


// 70762 is the last BBP 1.5.5.1 version, 70770 is the first BBP 1.6.0.1 version
static const int PROTOCOL_VERSION = 70771;

//! initial proto version, to be increased after version/verack negotiation
static const int INIT_PROTO_VERSION = 209;

//! In this version, 'getheaders' was introduced.
static const int GETHEADERS_VERSION = 70077;

//! disconnect from peers older than this proto version
static const int MIN_PEER_PROTO_VERSION = 70700;
static const int MIN_PEER_PROTO_TESTNET_VERSION = 70771;

//! minimum proto version of masternode to accept in DKGs
static const int MIN_MASTERNODE_PROTO_VERSION = 70700;
// remove static const int MIN_PEER_PROTO_VERSION_DIP3 = 70756;

//! nTime field added to CAddress, starting with this version;
//! if possible, avoid requesting addresses nodes older than this
static const int CADDR_TIME_VERSION = 31402;

//! BIP 0031, pong message, is enabled for all versions AFTER this one
static const int BIP0031_VERSION = 60000;

//! "filter*" commands are disabled without NODE_BLOOM after and including this version
static const int NO_BLOOM_VERSION = 70720;

//! "sendheaders" command and announcing blocks with headers starts with this version
static const int SENDHEADERS_VERSION = 70210;

//! DIP0001 was activated in this version
static const int DIP0001_PROTOCOL_VERSION = 70210;

//! short-id-based block download starts with this version
static const int SHORT_IDS_BLOCKS_VERSION = 70210;

//! introduction of DIP3/deterministic masternodes
static const int DMN_PROTO_VERSION = 70210;

//! introduction of LLMQs
static const int LLMQS_PROTO_VERSION = 70214;

//! introduction of SENDDSQUEUE
//! TODO we can remove this in 0.15.0.0
static const int SENDDSQUEUE_PROTO_VERSION = 70214;

//! protocol version is included in MNAUTH starting with this version
static const int MNAUTH_NODE_VER_VERSION = 70218;

#endif // BITCOIN_VERSION_H
