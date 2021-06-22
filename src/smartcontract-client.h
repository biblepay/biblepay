// Copyright (c) 2014-2019 The Dash Core Developers, The DAC Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SMARTCONTRACTCLIENT_H
#define SMARTCONTRACTCLIENT_H

#include "wallet/wallet.h"
#include "hash.h"
#include "net.h"
#include "rpcpog.h"
#include "utilstrencodings.h"
#include <univalue.h>


UniValue GetCampaigns();
bool Enrolled(std::string sCampaignName, std::string& sError);
CPK GetCPKFromProject(std::string sProjName, std::string sCPKPtr);
CPK GetMyCPK(std::string sProjectName);

#endif
