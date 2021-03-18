// Copyright (c) 2014-2019 The Dash-Core Developers, The DAC Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rpcutxo.h"
#include "spork.h"
#include "utilmoneystr.h"
#include "smartcontract-server.h"
#include "rpcpog.h"
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string/case_conv.hpp> // for to_lower()
#include <boost/algorithm/string.hpp> // for trim()
#include <boost/date_time/posix_time/posix_time.hpp> // for StringToUnixTime()
#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <openssl/crypto.h>
#include <stdint.h>
#include <univalue.h>
#include <fstream>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>


std::string GetGithubVersion()
{
	std::string sURL = "https://" + GetSporkValue("bms");
	std::string sRestfulURL = "BMS/LAST_MANDATORY_VERSION";
	std::string sV = ExtractXML(Uplink(false, "", sURL, sRestfulURL, SSL_PORT, 25, 1), "<VERSION>", "</VERSION>");
	return sV;
}

COutPoint OutPointFromUTXO(std::string sUTXO)
{
	std::vector<std::string> vU = Split(sUTXO.c_str(), "-");
	COutPoint c;
	if (vU.size() < 2)
		return c;

	std::string sHash = vU[0];
	int nOrdinal = (int)cdbl(vU[1], 0);
	c = COutPoint(uint256S(sHash), nOrdinal);
	return c;
}

void LockUTXOStakes()
{
	if (!pwalletpog)
		return;

	std::vector<UTXOStake> uStakes = GetUTXOStakes(false);
    LOCK(pwalletpog->cs_wallet);
	std::string sCPK = DefaultRecAddress("Christian-Public-Key"); 
	for (int i = 0; i < uStakes.size(); i++)
	{
		UTXOStake d = uStakes[i];
		if (d.found)
		{
			COutPoint c = OutPointFromUTXO(d.BBPUTXO);
			if (d.CPK == sCPK)
			{
				pwalletpog->LockCoin(c);
			}
		}
	}
}

