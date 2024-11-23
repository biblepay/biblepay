#include "pose.h"
#include "clientversion.h"
#include "chainparams.h"
#include "init.h"
#include "net.h"
#include <shutdown.h>
#include <validation.h>
#include <validationinterface.h>

static int64_t nPovsProcessTime = 0;
static int64_t nSleepTime = 0;
static bool fProcessing = false;
static int nIterations = 0;
static bool fPOVSEnabled = false;
static bool fPOVSBanningChecked = false;
static int nCurPos = 0;

void ClearDictionary()
{
    int64_t nElapsed = GetAdjustedTime() - nPovsProcessTime;
    if (nElapsed > (60 * 60 * 24)) {
        // Once every 24 hours we clear the POVS statuses and start over (in case sanctuaries dropped out or added, or if the entire POVS system was disabled etc).
        mapPOVSStatus.clear();
        nPovsProcessTime = GetAdjustedTime();
        LogPrintf("\r\nPOVS::Clearing dictionary %f", nElapsed);
        fPOVSBanningChecked = false;
    }
    nIterations++;
    /*
    CBlockIndex* pindexTip = WITH_LOCK(cs_main, return g_chainman.ActiveChain().Tip());
    int64_t nTipAge = GetAdjustedTime() - pindexTip->GetBlockTime();
    */ 
}



void SetBanningCheck()
{
    if (!fPOVSBanningChecked) {
        double nBanning = 1;
        bool fConnectivity = POVSTest("Status", "209.145.56.214:40000", 5, 2);
        fPOVSEnabled = nBanning == 1 && fConnectivity;
        LogPrintf("\r\nPOVS::Sanctuary Connectivity Test::Iter %f, Time %f, Lock %f, %f", nIterations, GetAdjustedTime(), fProcessing, fConnectivity);
    }
}

void ThreadPOVS(CConnman& connman)
{
    
        SetBanningCheck();

        // Called once per minute from the scheduler
        try
        {
            if (!ShutdownRequested() && fPOVSEnabled && !fProcessing)
            {
                LOCK(cs_main);
                {
                    fProcessing = true;
                    ClearDictionary();
                    std::vector<uint256> toBan;

                    auto mnList = deterministicMNManager->GetListAtChainTip();
                    int iPos = 0;
                    mnList.ForEachMN(false, [&](auto& dmn) {
                        if (iPos == nCurPos) {
                            std::string sPubKey = dmn.pdmnState->pubKeyOperator.Get().ToString();
                            std::string sIP1 = dmn.pdmnState->addr.ToString();
                            bool fOK = POVSTest(sPubKey, sIP1, 5, 0);
                            int iSancOrdinal = 0;
                            int nStatus = fOK ? 1 : 255;
                            mapPOVSStatus[sPubKey] = nStatus;
                            if (!fOK) {
                                LogPrintf("\r\nPOVS::BAN v1.2::Pos %f", iPos);
                                toBan.emplace_back(dmn.proTxHash);
                                LogPrintf("\r\nPOVS::BAN v1.131::Pos %f", iPos);
                            }
                            LogPrintf("\r\nPOVS::1.141 Pos %f", nCurPos);
                        }
                        iPos++;
                    });

                    // Ban
                    for (const auto& proTxHash : toBan)
                    {
                        LogPrintf("\r\nPOVS::1.151 BANNING::Pos %f", nCurPos);
                        mnList.PoSePunish(proTxHash, mnList.CalcPenalty(100), false);
                    }

                    nCurPos++;
                    if (nCurPos > iPos) {
                        nCurPos = 0;
                        LogPrintf("\r\nPOVS::Starting over %f", 0);
                    }

                    fProcessing = false;
                }
            }

        }
        catch (...)
        {
             LogPrintf("Error encountered in POVS main loop. %f \n", 0);
        }
    
}

