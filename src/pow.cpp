// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>

#include <arith_uint256.h>
#include <chain.h>
#include <chainparams.h>
#include <primitives/block.h>
#include <uint256.h>
#include "rpcpog.h"
#include "kjv.h"

#include <math.h>

unsigned int static KimotoGravityWell(const CBlockIndex* pindexLast, const Consensus::Params& params) {
    const CBlockIndex *BlockLastSolved = pindexLast;
    const CBlockIndex *BlockReading = pindexLast;
    uint64_t PastBlocksMass = 0;
    int64_t PastRateActualSeconds = 0;
    int64_t PastRateTargetSeconds = 0;
    double PastRateAdjustmentRatio = double(1);
    arith_uint256 PastDifficultyAverage;
    arith_uint256 PastDifficultyAveragePrev;
    double EventHorizonDeviation;
    double EventHorizonDeviationFast;
    double EventHorizonDeviationSlow;

    uint64_t pastSecondsMin = params.nPowTargetTimespan * 0.025;
    uint64_t pastSecondsMax = params.nPowTargetTimespan * 7;
    uint64_t PastBlocksMin = pastSecondsMin / params.nPowTargetSpacing;
    uint64_t PastBlocksMax = pastSecondsMax / params.nPowTargetSpacing;

    if (BlockLastSolved == nullptr || BlockLastSolved->nHeight == 0 || (uint64_t)BlockLastSolved->nHeight < PastBlocksMin) { return UintToArith256(params.powLimit).GetCompact(); }

    for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++) {
        if (PastBlocksMax > 0 && i > PastBlocksMax) { break; }
        PastBlocksMass++;

        PastDifficultyAverage.SetCompact(BlockReading->nBits);
        if (i > 1) {
            // handle negative arith_uint256
            if(PastDifficultyAverage >= PastDifficultyAveragePrev)
                PastDifficultyAverage = ((PastDifficultyAverage - PastDifficultyAveragePrev) / i) + PastDifficultyAveragePrev;
            else
                PastDifficultyAverage = PastDifficultyAveragePrev - ((PastDifficultyAveragePrev - PastDifficultyAverage) / i);
        }
        PastDifficultyAveragePrev = PastDifficultyAverage;

        PastRateActualSeconds = BlockLastSolved->GetBlockTime() - BlockReading->GetBlockTime();
        PastRateTargetSeconds = params.nPowTargetSpacing * PastBlocksMass;
        PastRateAdjustmentRatio = double(1);
        if (PastRateActualSeconds < 0) { PastRateActualSeconds = 0; }
        if (PastRateActualSeconds != 0 && PastRateTargetSeconds != 0) {
            PastRateAdjustmentRatio = double(PastRateTargetSeconds) / double(PastRateActualSeconds);
        }
        EventHorizonDeviation = 1 + (0.7084 * pow((double(PastBlocksMass)/double(28.2)), -1.228));
        EventHorizonDeviationFast = EventHorizonDeviation;
        EventHorizonDeviationSlow = 1 / EventHorizonDeviation;

        if (PastBlocksMass >= PastBlocksMin) {
                if ((PastRateAdjustmentRatio <= EventHorizonDeviationSlow) || (PastRateAdjustmentRatio >= EventHorizonDeviationFast))
                { assert(BlockReading); break; }
        }
        if (BlockReading->pprev == nullptr) { assert(BlockReading); break; }
        BlockReading = BlockReading->pprev;
    }

    arith_uint256 bnNew(PastDifficultyAverage);
    if (PastRateActualSeconds != 0 && PastRateTargetSeconds != 0) {
        bnNew *= PastRateActualSeconds;
        bnNew /= PastRateTargetSeconds;
    }

    if (bnNew > UintToArith256(params.powLimit)) {
        bnNew = UintToArith256(params.powLimit);
    }

    return bnNew.GetCompact();
}

unsigned int static DarkGravityWave(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params) {
    /* current difficulty formula, DarkGravity v3, written by Evan Duffield */
    const CBlockIndex *BlockLastSolved = pindexLast;
    const CBlockIndex *BlockReading = pindexLast;
    int64_t nActualTimespan = 0;
    int64_t LastBlockTime = 0;
    int64_t PastBlocksMin = 4; 
    int64_t PastBlocksMax = 4; 
    int64_t CountBlocks = 0;
    arith_uint256 PastDifficultyAverage;
    arith_uint256 PastDifficultyAveragePrev;
	static int64_t BPL_TRIGGER_SIZE = 500000;

	bool fProdChain = Params().NetworkIDString() == "main" ? true : false;
	int64_t nTotalBlockSize = 0;
	int64_t nAvgBlockSize = 0;

	if (BlockLastSolved == NULL || BlockLastSolved->nHeight == 0 || BlockLastSolved->nHeight < PastBlocksMin) 
	{
        return UintToArith256(params.powLimit).GetCompact();
    }

    for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++) 
	{
        if (PastBlocksMax > 0 && i > PastBlocksMax) { break; }
        CountBlocks++;

        if(CountBlocks <= PastBlocksMin) 
		{
            if (CountBlocks == 1) { PastDifficultyAverage.SetCompact(BlockReading->nBits); }
              else { PastDifficultyAverage = ((PastDifficultyAveragePrev * CountBlocks) + (arith_uint256().SetCompact(BlockReading->nBits))) / (CountBlocks + 1); }
            PastDifficultyAveragePrev = PastDifficultyAverage;
        }
		nTotalBlockSize += ::GetSerializeSize(*pblock, SER_NETWORK, PROTOCOL_VERSION);
	    if(LastBlockTime > 0)
		{
            int64_t Diff = (LastBlockTime - BlockReading->GetBlockTime());
            nActualTimespan += Diff;
        }
        LastBlockTime = BlockReading->GetBlockTime();

        if (BlockReading->pprev == NULL) { assert(BlockReading); break; }
        BlockReading = BlockReading->pprev;
    }

	if (CountBlocks < 1)
	{
		nAvgBlockSize = nTotalBlockSize;
	}
	else
	{
		nAvgBlockSize = nTotalBlockSize / CountBlocks;
	}

	// BPL
	// Rule 1. When the average block size over the last 4 blocks exceeds 500K (50% of 1meg), we target 20 second blocks.  Since we have chainlocks, this is safe.
	// End of BPL
    arith_uint256 bnNew(PastDifficultyAverage);
    int64_t _nTargetTimespan = CountBlocks * params.nPowTargetSpacing; 
	// Note that BBP has 7 minute block spacing.
	
	if (pindexLast->nHeight >= params.F11000_CUTOVER_HEIGHT && pindexLast->nHeight < params.F12000_CUTOVER_HEIGHT)
	{
		_nTargetTimespan = CountBlocks * 390;
	}
	else if (pindexLast->nHeight >= params.F12000_CUTOVER_HEIGHT && pindexLast->nHeight < params.F13000_CUTOVER_HEIGHT)
	{
		_nTargetTimespan = CountBlocks * 310;
	}
	else if (pindexLast->nHeight >= params.F13000_CUTOVER_HEIGHT)
	{
		_nTargetTimespan = CountBlocks * 296;
	}
	
	if (!fProdChain)
	{
		if (pindexLast->nHeight < 250)
		{
			_nTargetTimespan = CountBlocks; // One second blocks in testnet
		}
		else if (pindexLast->nHeight >= 250 && pindexLast->nHeight < 5000)
		{
			_nTargetTimespan = 30; // 30 second blocks 
		}
		else if (pindexLast->nHeight >= 5000)
		{
			_nTargetTimespan = CountBlocks * 300;  // 7 minute blocks in testnet
		}
	}
	// BPL

	if (pindexLast->nHeight > params.POOM_PHASEOUT_HEIGHT)
	{
		if (nAvgBlockSize > BPL_TRIGGER_SIZE)
			_nTargetTimespan = CountBlocks * 20;
	}

    if (nActualTimespan < _nTargetTimespan / 2)
        nActualTimespan = _nTargetTimespan / 2;

    if (nActualTimespan > _nTargetTimespan * 2)
        nActualTimespan = _nTargetTimespan * 2;

    // Retarget
    bnNew *= nActualTimespan;
    bnNew /= _nTargetTimespan;

    if (bnNew > UintToArith256(params.powLimit))
	{
        bnNew = UintToArith256(params.powLimit);
    }

    return bnNew.GetCompact();
}

unsigned int GetNextWorkRequiredBTC(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);
    unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();

    // Only change once per interval
    if ((pindexLast->nHeight+1) % params.DifficultyAdjustmentInterval() != 0)
    {
        if (params.fPowAllowMinDifficultyBlocks)
        {
            // Special difficulty rule for testnet:
            // If the new block's timestamp is more than 2* 2.5 minutes
            // then allow mining of a min-difficulty block.
            if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing*2)
                return nProofOfWorkLimit;
            else
            {
                // Return the last non-special-min-difficulty-rules-block
                const CBlockIndex* pindex = pindexLast;
                while (pindex->pprev && pindex->nHeight % params.DifficultyAdjustmentInterval() != 0 && pindex->nBits == nProofOfWorkLimit)
                    pindex = pindex->pprev;
                return pindex->nBits;
            }
        }
        return pindexLast->nBits;
    }

    // Go back by what we want to be 1 day worth of blocks
    int nHeightFirst = pindexLast->nHeight - (params.DifficultyAdjustmentInterval()-1);
    assert(nHeightFirst >= 0);
    const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
    assert(pindexFirst);

   return CalculateNextWorkRequired(pindexLast, pindexFirst->GetBlockTime(), params);
}

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);
    assert(pblock != nullptr);

	// BiblePay only uses DGW
	return DarkGravityWave(pindexLast, pblock, params);

	/*
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);

    // this is only active on devnets
    if (pindexLast->nHeight < params.nMinimumDifficultyBlocks) {
        return bnPowLimit.GetCompact();
    }

    if (pindexLast->nHeight + 1 < params.nPowKGWHeight) {
        return GetNextWorkRequiredBTC(pindexLast, pblock, params);
    }

    // Note: GetNextWorkRequiredBTC has it's own special difficulty rule,
    // so we only apply this to post-BTC algos.
    if (params.fPowAllowMinDifficultyBlocks) {
        // recent block is more than 2 hours old
        if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + 2 * 60 * 60) {
            return bnPowLimit.GetCompact();
        }
        // recent block is more than 10 minutes old
        if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing * 4) {
            arith_uint256 bnNew = arith_uint256().SetCompact(pindexLast->nBits) * 10;
            if (bnNew > bnPowLimit) {
                return bnPowLimit.GetCompact();
            }
            return bnNew.GetCompact();
        }
    }

    if (pindexLast->nHeight + 1 < params.nPowDGWHeight) {
        return KimotoGravityWell(pindexLast, params);
    }

    return DarkGravityWave(pindexLast, params);
	*/

}

// for DIFF_BTC only!
unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    if (nActualTimespan < params.nPowTargetTimespan/4)
        nActualTimespan = params.nPowTargetTimespan/4;
    if (nActualTimespan > params.nPowTargetTimespan*4)
        nActualTimespan = params.nPowTargetTimespan*4;

    // Retarget
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    arith_uint256 bnNew;
    bnNew.SetCompact(pindexLast->nBits);
    bnNew *= nActualTimespan;
    bnNew /= params.nPowTargetTimespan;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    return bnNew.GetCompact();
}

bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params, 
	int64_t nBlockTime, int64_t nPrevBlockTime, int nPrevHeight, unsigned int nNonce, 
	const CBlockIndex* pindexPrev, std::string sHeaderHex, uint256 uRXKey, int iThreadID, bool bLoadingBlockIndex)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;
    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);
    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit))
        return false;
	// RandomX performance:
	int64_t nElapsed = GetAdjustedTime() - nPrevBlockTime;
	if ((nElapsed > (60 * 60 * 8) && bLoadingBlockIndex) || (nElapsed > (60 * 60 * 24)))
		return true;

    if (nPrevHeight < params.EVOLUTION_CUTOVER_HEIGHT)
	{
		bool f_7000;
		bool f_8000;
		bool f_9000; 
		bool fTitheBlocksActive;
		GetMiningParams(nPrevHeight, f_7000, f_8000, f_9000, fTitheBlocksActive);
		uint256 uBibleHashClassic = BibleHashClassic(hash, nBlockTime, nPrevBlockTime, true, nPrevHeight, NULL, false, f_7000, f_8000, f_9000, fTitheBlocksActive, nNonce, params);
		if (UintToArith256(uBibleHashClassic) > bnTarget && nPrevBlockTime > 0) 
		{
			LogPrintf("\nCheckBlockHeader::ERROR-FAILED[0] height %f, nonce %f", nPrevHeight, nNonce);
			return false;
		}
	}
	else if (nPrevHeight >= params.EVOLUTION_CUTOVER_HEIGHT && nPrevHeight < params.RANDOMX_HEIGHT)
	{
		// Anti-GPU check:
		bool fNonce = CheckNonce(true, nNonce, nPrevHeight, nPrevBlockTime, nBlockTime, params);
		if (!fNonce)
		{
				LogPrintf("\nCheckBlockHeader::ERROR-FAILED[3] height %f, nonce %f", nPrevHeight, nNonce);
  
			return error("CheckProofOfWork: ERROR: High Nonce, PrevTime %f, Time %f, Nonce %f ", (double)nPrevBlockTime, (double)nBlockTime, (double)nNonce);
		}
		
		
		uint256 uBibleHash = BibleHashV2(hash, nBlockTime, nPrevBlockTime, true, nPrevHeight, sHeaderHex, uRXKey, pindexPrev->GetBlockHash(), iThreadID);
		if (UintToArith256(uBibleHash) > bnTarget && nPrevBlockTime > 0) 
		{
			LogPrintf("\nCheckBlockHeader::ERROR-FAILED[1] height %f, nonce %f", nPrevHeight, nNonce);
 			return false;
		}
	}
	else if (nPrevHeight >= params.RANDOMX_HEIGHT && nPrevHeight <= params.POOM_PHASEOUT_HEIGHT)
	{
		// RandomX Era:
		uint256 rxhash = GetRandomXHash(sHeaderHex, uRXKey, pindexPrev->GetBlockHash(), iThreadID);
		if (UintToArith256(ComputeRandomXTarget(rxhash, nPrevBlockTime, nBlockTime)) > bnTarget) 
		{
			LogPrintf("\nCheckBlockHeader::ERROR-FAILED[2] height %f, nonce %f", nPrevHeight, nNonce);
			return error("CheckProofOfWork Failed:ERROR: RandomX high-hash, Height %f, PrevTime %f, Time %f, Nonce %f ", (double)nPrevHeight, 
				(double)nPrevBlockTime, (double)nBlockTime, (double)nNonce);
		}
	}
	else if (nPrevHeight > params.POOM_PHASEOUT_HEIGHT)
	{
		// RandomX Era (Phase II):
		uint256 rxhash = GetRandomXHash2(sHeaderHex, uRXKey, pindexPrev->GetBlockHash(), iThreadID);
		if (UintToArith256(ComputeRandomXTarget(rxhash, nPrevBlockTime, nBlockTime)) > bnTarget) 
		{
			LogPrintf("\nCheckBlockHeader::ERROR-FAILED[4] height %f, nonce %f", nPrevHeight, nNonce);
     		return error("CheckProofOfWork Failed:ERROR: RandomX high-hash, Height %f, PrevTime %f, Time %f, Nonce %f ", (double)nPrevHeight, 
				(double)nPrevBlockTime, (double)nBlockTime, (double)nNonce);
		}
	}
	
    return true;
}

