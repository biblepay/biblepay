// Copyright (c) 2014-2019 The Dash Core Developers, The DAC Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "smartcontract-client.h"
#include "smartcontract-server.h"
#include "util.h"
#include "utilmoneystr.h"
#include "rpcpog.h"
#include "init.h"
#include "messagesigner.h"
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string/case_conv.hpp> // for to_lower()
#include <boost/algorithm/string.hpp> // for trim()
#include <boost/date_time/posix_time/posix_time.hpp> // for StringToUnixTime()
#include <math.h>       /* round, floor, ceil, trunc */
#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <openssl/crypto.h>
#include <stdint.h>
#include <univalue.h>
#include <fstream>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>


//////////////////////////////////////////////////////////////////// DAC - SMART CONTRACTS - CLIENT SIDE ///////////////////////////////////////////////////////////////////////////////////////////////

CPK GetCPKFromProject(std::string sProjName, std::string sCPKPtr)
{
	std::string sRec = GetCPKData(sProjName, sCPKPtr);
	CPK oCPK = GetCPK(sRec);
	return oCPK;
}

CPK GetMyCPK(std::string sProjectName)
{
	std::string sCPK = DefaultRecAddress("Christian-Public-Key");
	std::string sRec = GetCPKData(sProjectName, sCPK);
	CPK myCPK = GetCPK(sRec);
	return myCPK;
}


//////////////////////////////////////////////////////////////////////////// CAMPAIGN #1 - Created March 23rd, 2019 - PROOF-OF-GIVING /////////////////////////////////////////////////////////////////////////////
// Loop through each campaign, create the applicable client-side transaction 

bool Enrolled(std::string sCampaignName, std::string& sError)
{
	// First, verify the user is in good standing
	CPK myCPK = GetMyCPK("cpk");
	if (myCPK.sAddress.empty()) 
	{
		LogPrintf("CPK Missing %f\n", 789);
		sError = "User has no CPK.";
		return false;
	}
	// If we got this far, it was signed.
	if (sCampaignName == "cpk")
		return true;
	// Now check the project signature.
	std::string scn = "cpk-" + sCampaignName;
	boost::to_upper(scn);

	CPK myProject = GetMyCPK(scn);
	if (myProject.sAddress.empty())
	{
		sError = "User is not enrolled in " + scn + ".";
		return false;
	}
	return true;
}

