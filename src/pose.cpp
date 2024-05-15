#include "pose.h"
#include "clientversion.h"
#include "chainparams.h"
#include "init.h"
#include "net.h"
#include <shutdown.h>

static int64_t nPovsProcessTime = 0;
static int64_t nSleepTime = 0;
static bool fProcessing = false;
static int nIterations = 0;

void ClearDictionary()
{
    int64_t nElapsed = GetAdjustedTime() - nPovsProcessTime;
    if (nElapsed > (60 * 60 * 24)) {
        // Once every 24 hours we clear the POVS statuses and start over (in case sanctuaries dropped out or added, or if the entire POVS system was disabled etc).
        mapPOVSStatus.clear();
        nPovsProcessTime = GetAdjustedTime();
        LogPrintf("\r\nPOVS::Clearing dictionary %f", nElapsed);
    }
    nIterations++;
    /*
    CBlockIndex* pindexTip = WITH_LOCK(cs_main, return g_chainman.ActiveChain().Tip());
    int64_t nTipAge = GetAdjustedTime() - pindexTip->GetBlockTime();
    */
}

void ThreadPOVS(CConnman& connman)
{
        // Called once per minute from the scheduler
        try
        {
        if (!fProcessing) {
            double nBanning = 1;
            bool fConnectivity = POVSTest("Status", "209.145.56.214:40000", 15, 2);
            ClearDictionary();
            LogPrintf("\r\nPOVS::Sanctuary Connectivity Test::Iter %f, Time %f, Lock %f, %f", nIterations, GetAdjustedTime(), fProcessing, fConnectivity);
            bool fPOVSEnabled = nBanning == 1 && fConnectivity;
            if (fPOVSEnabled && !fProcessing) {
                // Lock
                fProcessing = true;
                auto mnList = deterministicMNManager->GetListAtChainTip();
                std::vector<uint256> toBan;

                int iPos = 0;
                mnList.ForEachMN(false, [&](auto& dmn)
                {
                    if (!ShutdownRequested())
                    {
                        std::string sPubKey = dmn.pdmnState->pubKeyOperator.Get().ToString();
                        std::string sIP1 = dmn.pdmnState->addr.ToString();
                        bool fOK = POVSTest(sPubKey, sIP1, 30, 0);
                        int iSancOrdinal = 0;
                        int nStatus = fOK ? 1 : 255;
                        mapPOVSStatus[sPubKey] = nStatus;
                        if (!fOK) {
                            toBan.emplace_back(dmn.proTxHash);
                            LogPrintf("\r\nPOVS::Pos %f", iPos);
                        }
                        MilliSleep(3000);
                        iPos++;
                    }
                });
                // Ban
                for (const auto& proTxHash : toBan) {
                    mnList.PoSePunish(proTxHash, mnList.CalcPenalty(100), false);
                }
                // Unlock
                fProcessing = false;
            }
        }
     }
     catch (...)
     {
         LogPrintf("Error encountered in POVS main loop. %f \n", 0);
     }
    
}

