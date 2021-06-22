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


