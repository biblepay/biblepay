#include "pose.h"
#include "clientversion.h"
#include "chainparams.h"
#include "init.h"
#include "net.h"
#include "utilstrencodings.h"
#include "utiltime.h"
#include "masternode-sync.h"


static int64_t nPoosProcessTime = 0;
void ThreadPOOS(CConnman& connman)
{
	if (false)
		SyncSideChain(0);
	int nIterations = 0;

	while (1 == 1)
	{
	    if (ShutdownRequested())
			return;

		try
		{
			double nOrphanBanning = GetSporkDouble("EnableOrphanSanctuaryBanning", 0);
			bool fConnectivity = POOSOrphanTest("status", 60 * 60);
			bool fPOOSEnabled = nOrphanBanning == 1 && fConnectivity;
			int64_t nElapsed = GetAdjustedTime() - nPoosProcessTime;
			if (nElapsed > (60 * 60 * 8))
			{
				// Once every 8 hours we clear the POOS statuses and start over (in case sanctuaries dropped out or added, or if the entire POOS system was disabled etc).
				mapPOOSStatus.clear();
				nPoosProcessTime = GetAdjustedTime();
				mapUTXOStatus.clear();
				fUTXOSTested = false;
			}
			if (nOrphanBanning != 1)
			{
				mapPOOSStatus.clear();
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
			if (nTipAge < (60 * 60 * 4) && chainActive.Tip()->nHeight % 10 == 0)
			{
				if (false)
					SyncSideChain(chainActive.Tip()->nHeight);
			}
			// Sanctuary side UTXO Oracle Process
			std::vector<UTXOStake> uStakes = GetUTXOStakes(false);

			for (int i = 0; i < uStakes.size(); i++)
			{
				UTXOStake d = uStakes[i];
				if (d.found)
				{
					int nStatus = GetUTXOStatus(d.TXID);
					if (nStatus == 0)
					{
						AssimilateUTXO(d);
					}
				}
			}
			fUTXOSTested = true;

			// End of Sanctuary side UTXO Oracle Process

		}
		catch(...)
		{
			LogPrintf("Error encountered in POOS main loop. %f \n", 0);
		}
		int nSleepLength = nIterations < 5 ? 60*5 : 60*30;

		for (int i = 0; i < nSleepLength; i++)
		{
			if (ShutdownRequested())
				break;
			MilliSleep(1000);
		}
	}
}

