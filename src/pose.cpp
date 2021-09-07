#include "pose.h"
#include "clientversion.h"
#include "chainparams.h"
#include "init.h"
#include "net.h"
#include "utilstrencodings.h"
#include "utiltime.h"

static int64_t nPoosProcessTime = 0;
static int64_t nSleepTime = 0;

void ThreadPOOS(CConnman& connman)
{
	int nIterations = 0;
	while (1 == 1)
	{
	    if (ShutdownRequested())
			return;
		try
		{
			double nOrphanBanning = 1;
			bool fConnectivity = POOSOrphanTest("status", 60 * 60);
			bool fPOOSEnabled = nOrphanBanning == 1 && fConnectivity;
			int64_t nElapsed = GetAdjustedTime() - nPoosProcessTime;
			if (nElapsed > (60 * 60 * 24))
			{
				// Once every 24 hours we clear the POOS statuses and start over (in case sanctuaries dropped out or added, or if the entire POOS system was disabled etc).
				mapPOOSStatus.clear();
				nPoosProcessTime = GetAdjustedTime();
			}
			if (fPOOSEnabled)
			{
				auto mnList = deterministicMNManager->GetListAtChainTip();
				std::vector<uint256> toBan;

				mnList.ForEachMN(false, [&](const CDeterministicMNCPtr& dmn) 
				{
					if (!ShutdownRequested())
					{
						std::string sPubKey = dmn->pdmnState->pubKeyOperator.Get().ToString();
						bool fOK = POOSOrphanTest(sPubKey, 60 * 60);
						int nStatus = fOK ? 1 : 255;
						mapPOOSStatus[sPubKey] = nStatus;
						if (!fOK)
						{
					        toBan.emplace_back(dmn->proTxHash);
						}
						MilliSleep(1000);
					}
				});
				// Ban 
				for (const auto& proTxHash : toBan) {
					mnList.PoSePunish(proTxHash, mnList.CalcPenalty(100), false);
				}
			}
			nIterations++;
			int64_t nTipAge = GetAdjustedTime() - chainActive.Tip()->GetBlockTime();
		}
		catch(...)
		{
			LogPrintf("Error encountered in POOS main loop. %f \n", 0);
		}
		int nSleepLength = nIterations < 6 ? 60 * (nIterations + 1) : 60 * 30;
		
		for (int i = 0; i < nSleepLength; i++)
		{
			if (ShutdownRequested())
				break;
			MilliSleep(1000);
			nSleepTime++;
			if (nSleepTime > (60 * 15))
			{
				nSleepTime = 0;
			}
		}
	}
}

