// Copyright (c) 2014-2019 The Dash Core Developers, The DAC Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RPCPOG_H
#define RPCPOG_H

#include "wallet/wallet.h"
#include "chat.h"
#include "email.h"
#include "governance/governance-classes.h"
#include "hash.h"
#include "net.h"
#include "utilstrencodings.h"
#include "validation.h"
#include <univalue.h>
#include "base58.h"
#include <boost/algorithm/string/case_conv.hpp> // for to_lower()
#include <boost/algorithm/string/trim.hpp>

class CWallet;

std::string RetrieveMd5(std::string s1);
uint256 CoordToUint256(int row, int col);
std::string RoundToString(double d, int place);
std::string AmountToString(const CAmount& amount);
double CalculateUTXOReward(int nStakeCount, int nDays);
int GetUTXOStatus(uint256 txid);
bool findStringCaseInsensitive(const std::string & strHaystack, const std::string & strNeedle);
CAmount GetBBPValueUSD(double nUSD, double nMask);
std::string RSADecryptHQURL(std::string sEncData, std::string& sError);
std::string RSAEncryptHQURL(std::string sSourceData, std::string& sError);
std::string ExtractXML(std::string XMLdata, std::string key, std::string key_end);
std::string GenerateXMLSignature(std::string sPrimaryKey, std::string sSigningPublicKey);
std::vector<std::string> Split(std::string s, std::string delim);
double cdbl(std::string s, int place);
std::string AmountToString(const CAmount& amount);
double AmountToDouble(const CAmount& amount);
CTransactionRef GetTxRef(uint256 hash);
CAmount GetTxTotalFromAddress(CTransactionRef ctx, std::string sAddress);

struct UserVote
{
	int nTotalYesCount = 0;
	int nTotalNoCount = 0;
	int nTotalAbstainCount = 0;
	int nTotalYesWeight = 0;
	int nTotalNoWeight = 0;
	int nTotalAbstainWeight = 0;
};

struct NFT
{
	std::string sCPK;
	std::string sName;
	std::string sDescription;
	std::string sLoQualityURL;
	std::string sHiQualityURL;
	std::string sType;
	std::string sXML;
	CAmount nMinimumBidAmount = 0;
	CAmount nReserveAmount = 0;
	CAmount nBuyItNowAmount = 0;
	bool fMarketable = false;
	bool fDeleted = false;
	bool found = false;
	uint64_t nTime = 0;
	uint256 TXID;
	uint256 GetHash()
	{
		uint256 h;
        CSHA256 sha256;
		std::string sURL1(sLoQualityURL.c_str());
		boost::to_lower(sURL1);
		boost::algorithm::trim(sURL1);
	    std::vector<unsigned char> vchURL = std::vector<unsigned char>(sURL1.begin(), sURL1.end());
		sha256.Write(&vchURL[0], vchURL.size());
        sha256.Finalize(h.begin());
		return h;
	}
	
	CAmount LowestAcceptableAmount()
	{
		CAmount nAcceptable = 100000000 * COIN;
		if (nReserveAmount > 0 && nBuyItNowAmount > 0)
		{
			// This is an Auction AND a buy-it-now NFT, so accept the lower of the two
			nAcceptable = std::min(nReserveAmount, nBuyItNowAmount);
		}
		else if (nReserveAmount > 0 && nBuyItNowAmount == 0)
		{
			// This is an auction (but not a buy it now)
			nAcceptable = nReserveAmount;
		}
		else if (nBuyItNowAmount > 0 && nReserveAmount == 0)
		{
			nAcceptable = nBuyItNowAmount;
		}
		return nAcceptable;		
	}

	void ToJson(UniValue& obj)
	{
		obj.clear();
		obj.setObject();
		obj.push_back(Pair("Type", sType));
		obj.push_back(Pair("CPK", sCPK));
		obj.push_back(Pair("Name", sName));
		obj.push_back(Pair("Description", sDescription));
	    obj.push_back(Pair("Lo Quality URL", sLoQualityURL));
		std::string sError;
		std::string sHiQ = RSADecryptHQURL(sHiQualityURL, sError);

		obj.push_back(Pair("Hi Quality URL", sHiQ));
		obj.push_back(Pair("TXID", TXID.GetHex()));
		obj.push_back(Pair("Hash", GetHash().GetHex()));
		obj.push_back(Pair("MinimumBidAmount", (double)nMinimumBidAmount/COIN));
		obj.push_back(Pair("ReserveAmount", (double)nReserveAmount/COIN));
		obj.push_back(Pair("BuyItNowAmount", (double)nBuyItNowAmount/COIN));
		obj.push_back(Pair("LowestAcceptableAmount", (double)LowestAcceptableAmount()/COIN));
		obj.push_back(Pair("Marketable", fMarketable));
		obj.push_back(Pair("Deleted", fDeleted));
		obj.push_back(Pair("Time", nTime));
		bool fOrphan = findStringCaseInsensitive(sType, "orphan");
		if (fOrphan)
		{
			// Remaining Sponsorship days
			CAmount nAmountPerDay = GetBBPValueUSD(40, 1538);
			double nDays = (nBuyItNowAmount/COIN) / ((nAmountPerDay/COIN) + .01);
			double nElapsed = (GetAdjustedTime() - nTime) / 86400;
			obj.push_back(Pair("Sponsorship Length", nDays));
			obj.push_back(Pair("Elapsed", nElapsed));
		}
    }
};

struct LineItem
{
	std::string Description;
	CAmount nLineAmount = 0;
	int nLineNumber = 0;
};

struct Invoice
{
	std::string sFromAddress;
	std::string sToAddress;
	std::string sName;
	std::string sDescription;
	std::vector<LineItem> vLineItems;
	CAmount nAmount;
	bool found = false;
	uint64_t nTime = 0;
	std::string GetKey()
	{
		std::string sKey = sFromAddress + "-" + sToAddress + "-" + RoundToString(nTime, 0);
		return sKey;
	}

	uint256 GetHash()
	{
		uint256 h;
        CSHA256 sha256;
		std::string sKey = GetKey();
	    std::vector<unsigned char> vchD = std::vector<unsigned char>(sKey.begin(), sKey.end());
		sha256.Write(&vchD[0], vchD.size());
        sha256.Finalize(h.begin());
		return h;
	}
	
	void AddLineItem(int LineNumber, CAmount nLineAmount, std::string sDesc)
	{
		LineItem l;
		l.nLineNumber = LineNumber;
		l.nLineAmount = nLineAmount;
		l.Description = sDesc;
		vLineItems.push_back(l);
	}

	void clear()
	{
		sFromAddress = "";
		sToAddress = "";
		sName = "";
		sDescription = "";
		nAmount = 0;
		found = false;
		nTime = 0;
		vLineItems.clear();
	}

	void ToJson(UniValue& obj)
	{
		obj.clear();
		obj.setObject();
		obj.push_back(Pair("FromAddress", sFromAddress));
		obj.push_back(Pair("ToAddress", sToAddress));
		obj.push_back(Pair("Name", sName));
		obj.push_back(Pair("Description", sDescription));
		obj.push_back(Pair("Time", nTime));
		UniValue li(UniValue::VOBJ);
		for (int i = 0; i < vLineItems.size(); i++)
		{
			li.push_back(Pair(RoundToString(vLineItems[i].nLineNumber, 0) + ": " + AmountToString(vLineItems[i].nLineAmount), vLineItems[i].Description));
		}
		obj.push_back(Pair("LineItems", li));
		obj.push_back(Pair("Amount", AmountToDouble(nAmount)));
    }
	std::string ToXML()
	{
		std::string XML;
		std::string sFrom1(sFromAddress.c_str());
		std::string sMyKey  = GetKey();
		uint256 hash = GetHash();
		std::string sSig = GenerateXMLSignature(sMyKey, sFrom1);
		XML = "<MT>INVOICE</MT><MK>"+ GetKey() + "</MK><MV><invoice><FromAddress>" + sFromAddress + "</FromAddress><ToAddress>"+ sToAddress + "</ToAddress><Name>" 
			+ sName + "</Name><Description>" + sDescription + "</Description><Time>"
			+ RoundToString(nTime, 0) + "</Time><Amount>" + AmountToString(nAmount) + "</Amount>";
		std::string sLI = "<lineitems>";
		for (int i = 0; i < vLineItems.size(); i++)
		{
			sLI += "<lineitem><linenumber>"+ RoundToString(vLineItems[i].nLineNumber, 0) + "</linenumber><lineamount>" + AmountToString(vLineItems[i].nLineAmount) 
				+ "</lineamount><description>"+ vLineItems[i].Description + "</description></lineitem>";
		}
		XML += sLI + "</lineitems>" + sSig + "</invoice></MV>";
		return XML;
	}
	void FromXML(std::string XML)
	{
		clear();
		sFromAddress = ExtractXML(XML, "<FromAddress>", "</FromAddress>");
		sToAddress = ExtractXML(XML, "<ToAddress>", "</ToAddress>");
		sName = ExtractXML(XML, "<Name>", "</Name>");
		sDescription = ExtractXML(XML, "<Description>", "</Description>");
		nTime = cdbl(ExtractXML(XML, "<Time>", "</Time>"), 0);
		nAmount = cdbl(ExtractXML(XML, "<Amount>", "</Amount>"), 2) * COIN;
		std::string lineItems = ExtractXML(XML, "<lineitems>", "</lineitems>");
		std::vector<std::string> vLI = Split(lineItems.c_str(), "<lineitem>");
		for (int i = 0; i < vLI.size(); i++)
		{
			LineItem l;
			l.nLineAmount = cdbl(ExtractXML(vLI[i], "<lineamount>", "</lineamount>"), 2) * COIN;
			l.nLineNumber = cdbl(ExtractXML(vLI[i], "</linenumber>", "</linenumber>"), 0);
			l.Description = ExtractXML(vLI[i], "<description>", "</description>");
			if (l.nLineNumber > 0)
			{
				vLineItems.push_back(l);
			}
		}
		if (nTime > 0)
			found = true;
	}
};

struct Payment
{
	std::string sFromAddress;
	std::string sToAddress;
	int64_t nTime = 0;
	bool found = false;
	
	CAmount nAmount;
	std::string sNotes;
	std::string sInvoiceNumber;
	uint256 TXID;
	std::string GetKey()
	{
		std::string sKey = sFromAddress + "-" + sToAddress + "-" + RoundToString(nTime, 0);
		return sKey;
	}
	uint256 GetHash()
	{
		uint256 h;
        CSHA256 sha256;
		std::string sKey = GetKey();
	    std::vector<unsigned char> vchD = std::vector<unsigned char>(sKey.begin(), sKey.end());
		sha256.Write(&vchD[0], vchD.size());
        sha256.Finalize(h.begin());
		return h;
	}

	void clear()
	{
		nTime = 0;
		sFromAddress = "";
		sToAddress = "";
		nAmount = 0;
		sNotes = "";
		sInvoiceNumber = "";
		TXID = uint256S("0x0");
	}
	void ToJson(UniValue& obj)
	{
		obj.clear();
		obj.setObject();
		obj.push_back(Pair("FromAddress", sFromAddress));
		obj.push_back(Pair("ToAddress", sToAddress));
		obj.push_back(Pair("Notes", sNotes));
		obj.push_back(Pair("InvoiceNumber", sInvoiceNumber));
		obj.push_back(Pair("Time", nTime));
		obj.push_back(Pair("Amount", AmountToDouble(nAmount)));
		obj.push_back(Pair("TXID", TXID.GetHex()));
    }
	std::string ToXML()
	{
		std::string XML;
		
		std::string sSig = GenerateXMLSignature(GetKey(), sFromAddress);
		XML = "<MT>PAYMENT</MT><MK>" + GetKey() + "</MK><MV><payment><FromAddress>" + sFromAddress + "</FromAddress><ToAddress>" 
			+ sToAddress + "</ToAddress><Notes>" + sNotes 
			+ "</Notes><InvoiceNumber>"+ sInvoiceNumber + "</InvoiceNumber><Time>"
			+ RoundToString(nTime, 0) + "</Time><Amount>" + AmountToString(nAmount) + "</Amount>"
			+ sSig + "</payment></MV>";
		return XML;
	}
	void FromXML(std::string XML)
	{
		clear();
		sFromAddress = ExtractXML(XML, "<FromAddress>", "</FromAddress>");
		sToAddress = ExtractXML(XML, "<ToAddress>", "</ToAddress>");
		sNotes = ExtractXML(XML, "<Notes>", "</Notes>");
		sInvoiceNumber = ExtractXML(XML, "<InvoiceNumber>", "</InvoiceNumber>");
		nTime = cdbl(ExtractXML(XML, "<Time>", "</Time>"), 0);
		// nAmount = cdbl(ExtractXML(XML, "<Amount>", "</Amount>"), 2) * COIN;
		TXID = uint256S("0x" + ExtractXML(XML, "<txid>", "</txid>"));
		CTransactionRef tx1 = GetTxRef(TXID);
		nAmount = GetTxTotalFromAddress(tx1, sToAddress);
		if (nTime > 0)
			found = true;
	}
};

struct CDSQLQuery
{
	int64_t nTime = 0;
	bool found = false;
	
	std::string sData;
	std::string sOwnerAddress;
	std::string sTable;
	std::string sID;
	uint256 TXID;
	std::string GetKey()
	{
		std::string sKey = sTable + "-" + sOwnerAddress + "-" + sID;
		return sKey;
	}
	uint256 GetHash()
	{
		uint256 h;
        CSHA256 sha256;
		std::string sKey = GetKey();
	    std::vector<unsigned char> vchD = std::vector<unsigned char>(sKey.begin(), sKey.end());
		sha256.Write(&vchD[0], vchD.size());
        sha256.Finalize(h.begin());
		return h;
	}

	void clear()
	{
		nTime = 0;
		sOwnerAddress = "";
		sTable = "";
		sID = "";
		sData = "";
		TXID = uint256S("0x0");
	}
	void ToJson(UniValue& obj)
	{
		obj.clear();
		obj.setObject();
		obj.push_back(Pair("Data", sData));
		obj.push_back(Pair("OwnerAddress", sOwnerAddress));
		obj.push_back(Pair("Table", sTable));
		obj.push_back(Pair("ID", sID));
		obj.push_back(Pair("Time", nTime));
		obj.push_back(Pair("TXID", TXID.GetHex()));
    }
	std::string ToXML()
	{
		std::string XML;
		
		std::string sSig = GenerateXMLSignature(GetKey(), sOwnerAddress);
		XML = "<MT>DSQL</MT><MK>" + GetKey() + "</MK><MV><dsql><OwnerAddress>" + sOwnerAddress + "</OwnerAddress>" 
			+ "<ID>"+ sID + "</ID><Time>"
			+ RoundToString(nTime, 0) + "</Time><data>" + sData + "</data><table>" + sTable + "</table>"
			+ sSig + "</dsql></MV>";
		return XML;
	}
	void FromXML(std::string XML)
	{
		clear();
		sOwnerAddress = ExtractXML(XML, "<OwnerAddress>", "</OwnerAddress>");
		sID = ExtractXML(XML, "<ID>", "</ID>");
		sData = ExtractXML(XML, "<data>", "</data>");
		sTable = ExtractXML(XML, "<table>", "</table>");
		nTime = cdbl(ExtractXML(XML, "<Time>", "</Time>"), 0);
		TXID = uint256S("0x" + ExtractXML(XML, "<txid>", "</txid>"));
		CTransactionRef tx1 = GetTxRef(TXID);
		if (nTime > 0)
			found = true;
		//LogPrintf("\nDSQL::From %s to Q %s", XML, sData);

	}
};

struct ReferralCode
{
	std::string CPK;
	std::string OriginatorCPK;
	std::string Code;
	CAmount Size = 0;
	CAmount TotalClaimed = 0;
	CAmount TotalEarned = 0;
	CAmount TotalReferralReward = 0;
	double ReferralEffectivity = 0;
	double PercentageAffected = 0;
	double ReferralRewards = 0;
	bool found;
};

struct DMAddress
{
	std::string Name;
	std::string AddressLine1;
	std::string AddressLine2;
	std::string Template;
	std::string City;
	std::string State;
	std::string Zip;
	std::string Paragraph1;
	std::string Paragraph2;
	std::string OpeningSalutation;
	std::string ClosingSalutation;
	CAmount Amount;
};

struct DataTable
{
	int Rows = 0;
	int Cols = 0;
	std::string TableName;
	std::string TableDescription;
	std::map<uint256, std::string> Values;
	void Set(int nRow, int nCol, std::string sData)
	{
		Values[CoordToUint256(nRow, nCol)] = sData;
	}
	std::string Get(int nRow, int nCol)
	{
		std::string sKey = RoundToString(nRow,0) + "-" + RoundToString(nCol,0);
		return Values[CoordToUint256(nRow,nCol)];
	}
};

struct CPK
{
  std::string sAddress;
  int64_t nLockTime = 0;
  std::string sCampaign;
  std::string sNickName;
  std::string sEmail;
  std::string sVendorType;
  std::string sError;
  std::string sChildId;
  std::string sOptData;
  double nProminence = 0;
  double nPoints = 0;
  bool fValid = false;
};

struct PriceQuote
{
	double nForeignPrice = 0;
	double nBTCPrice = 0;
	double nBBPPrice = 0;
	double nUSDBBP = 0;
	double nUSDForeign = 0;
	double nBBPValueUSD = 0;
	double nForeignValueUSD = 0;
	CAmount nBBPQty = 0;
	CAmount nForeignQty = 0;
};

struct UserRecord
{
	std::string CPK;
	std::string NickName;
	std::string ExternalEmail;
	std::string InternalEmail;
	std::string Longitude;
	std::string Latitude;
	std::string URL;
	std::string RSAPublicKey;
	// Only available to the biblepaycore user:
	std::string RSAPrivateKey;
	bool AuthorizePayments = false;
	bool Found = false;
};

struct RSAKey
{
	std::string PrivateKey;
	std::string PublicKey;
	std::string Error;
	bool Valid;
};

struct IPFSTransaction
{
	std::string File;
	std::string TXID;
	CAmount nFee = 0;
	CAmount nSize = 0;
	std::string FileName;
	std::string URL;
	double nDuration = 0;
	int nDensity = 0;
	std::string ErrorCode;
	std::string Response;
	std::string BlockHash;
	std::string Network;
	std::string CPK;
	int nHeight = 0;
	std::map<std::string, std::string> mapRegions;
};

struct DashUTXO
{
	std::string TXID = std::string();
	CAmount Amount = 0;
	std::string Address = std::string();
	std::string Network = std::string();
	bool Spent = false;
	bool Found = false;
};

struct DACResult
{
	std::string Response;
	bool fError = false;
	CAmount nFee = 0;
	int nSize = 0;
	std::string TXID;
	std::string ErrorCode;
	std::string PrimaryKey;
	std::string PublicKey;
	std::string Address;
	std::string SecretKey;
	int64_t nTime = 0;
	CAmount nAmount = 0;
	std::map<std::string, IPFSTransaction> mapResponses;
	std::map<std::string, std::string> mapRegions;
};

struct QueuedProposal
{
	bool Submitted = false;
	std::string Hex;
	uint64_t StartTime = 0;
	int PrepareHeight = 0;
	std::string Error;
	uint256 TXID = uint256S("0x0");
	uint256 GovObj = uint256S("0x0");
	int SubmissionCount = 0;
};

struct CoinAgeVotingDataStruct
{
	std::map<int, std::map<std::string, int>> mapsVoteCount;
	std::map<int, std::map<std::string, double>> mapsVoteAge;
	std::map<int, int> mapTotalVotes;
	std::map<int, double> mapTotalCoinAge;
};

struct CoinVin
{
	COutPoint OutPoint;
	uint256 HashBlock = uint256S("0x0");
	int64_t BlockTime = 0;
	double CoinAge = 0;
	CAmount Amount = 0;
	std::string Destination = std::string();
	bool Found = false;
	CTransactionRef TxRef;
};

struct Orphan
{
	std::string OrphanID;
	std::string Charity;
	std::string Name;
	std::string URL;
	double MonthlyAmount;
};

struct Expense
{
	int64_t nTime = 0;
	std::string ExpenseID = std::string();
	std::string Added = std::string();
	std::string Charity = std::string();
	double nBBPAmount = 0;
	double nUSDAmount = 0;
};

struct SimpleUTXO
{
	std::string TXID;
	int nOrdinal = 0;
	CAmount nAmount = 0;
	double ValueUSD = 0;
};

struct UTXOStake
{
	std::string XML = std::string();
	CAmount nBBPAmount = 0;
	CAmount nForeignAmount = 0;
	int64_t Time = 0;
	int64_t Age = 0;
	double DaysElapsed = 0;
	double CommitmentFulfilledPctg = 0;
	int Height = 0;
	int nType = 0;
	bool fBBPSpent = false;
	std::string CPK = std::string();
	std::string SignatureNarr = std::string();
	bool found = false;
	std::string ForeignTicker = std::string();
	std::string ReportTicker = std::string();
	std::string BBPUTXO = std::string();
	std::string ForeignUTXO = std::string();
	std::string BBPAddress = std::string();
	std::string ForeignAddress = std::string();
	std::string BBPSignature = std::string();
	std::string ForeignSignature = std::string();
	std::string sType = std::string();
	double nBBPPrice = 0;
	double nForeignPrice = 0;
	double nBTCPrice = 0;
	double nBBPValueUSD = 0;
	double nForeignValueUSD = 0;
	double nValue = 0;
	double nCommitment = 0;
	double nDWU = 0;
	int nStatus = 0;
	bool BBPSignatureValid = false;
	bool ForeignSignatureValid = false;
	bool SignatureValid = false;
	uint256 TXID = uint256S("0x0");
	void ToJson(UniValue& obj)
	{
		obj.clear();
		obj.setObject();
		obj.push_back(Pair("RiskType", sType));
		nDWU = CalculateUTXOReward(nType, nCommitment); //nType contains stake-type (1=bbp only, 2=foreign+bbp)
		nStatus = GetUTXOStatus(TXID);
			
		obj.push_back(Pair("DWU", nDWU * 100));
		obj.push_back(Pair("Sigs", SignatureNarr));
		obj.push_back(Pair("TotalValue", nValue));
		obj.push_back(Pair("Ticker", ReportTicker));
	    obj.push_back(Pair("Status", nStatus));
		obj.push_back(Pair("CPK", CPK));
		obj.push_back(Pair("BBPAmount", AmountToString(nBBPAmount)));
		obj.push_back(Pair("ForeignAmount", AmountToString(nForeignAmount)));
		obj.push_back(Pair("BBPValue", RoundToString(nBBPValueUSD, 2)));
		obj.push_back(Pair("ForeignValue", RoundToString(nForeignValueUSD, 2)));
		obj.push_back(Pair("Commitment", nCommitment));
		obj.push_back(Pair("Fulfilled %", CommitmentFulfilledPctg * 100));
		obj.push_back(Pair("Time", Time));
		obj.push_back(Pair("Spent", fBBPSpent));
    }
};

struct DashStake
{
	std::string XML = std::string();
	CAmount nBBPAmount = 0;
	CAmount nDashAmount = 0;
	double MonthlyEarnings = 0;
	int64_t Time = 0;
	int64_t MaturityTime = 0;
	int MaturityHeight = 0;
	int Height = 0;
	int Duration = 0;
	std::string CPK = std::string();
	std::string ReturnAddress = std::string();
	bool found = false;
	bool expired = false;
	bool spent = false;
	double DWU = 0;
	double ActualDWU = 0;
	std::string BBPUTXO = std::string();
	std::string DashUTXO = std::string();
	std::string BBPAddress = std::string();
	std::string DashAddress = std::string();
	std::string BBPSignature = std::string();
	std::string DashSignature = std::string();
	double nBBPPrice = 0;
	double nDashPrice = 0;
	double nBTCPrice = 0;
	double nBBPValueUSD = 0;
	double nDashValueUSD = 0;
	double nBBPQty = 0;
	bool BBPSignatureValid = false;
	bool DashSignatureValid = false;
	bool SignatureValid = false;
	uint256 TXID = uint256S("0x0");
};

static double MAX_DAILY_DAC_DONATIONS = 40000000;


struct DACProposal
{
	std::string sName;
	int64_t nStartEpoch = 0;
	int64_t nEndEpoch = 0;
	std::string sURL;
	std::string sExpenseType;
	double nAmount = 0;
	std::string sAddress;
	uint256 uHash = uint256S("0x0");
	int nHeight = 0;
	bool fPassing = false;
	int nNetYesVotes = 0;
	int nYesVotes = 0;
	int nNoVotes = 0;
	int nAbstainVotes = 0;
	int nMinPassing = 0;
	int nLastSuperblock = 0;
	bool fIsPaid = false;
	std::string sProposalHRTime;
};


std::string QueryBibleHashVerses(uint256 hash, uint64_t nBlockTime, uint64_t nPrevBlockTime, int nPrevHeight, CBlockIndex* pindexPrev);
CAmount GetDailyMinerEmissions(int nHeight);
std::string CreateBankrollDenominations(double nQuantity, CAmount denominationAmount, std::string& sError);
std::string DefaultRecAddress(std::string sType);
std::string GenerateNewAddress(std::string& sError, std::string sName);
std::string GetActiveProposals();
bool VoteManyForGobject(std::string govobj, std::string strVoteSignal, std::string strVoteOutcome, 
	int iVotingLimit, int& nSuccessful, int& nFailed, std::string& sError);
std::string CreateGovernanceCollateral(uint256 GovObjHash, CAmount caFee, std::string& sError);
int GetNextSuperblock();
bool is_email_valid(const std::string& e);
double GetSporkDouble(std::string sName, double nDefault);
int64_t GETFILESIZE(std::string sPath);
std::string ReadCache(std::string sSection, std::string sKey);
std::string ReadCacheWithMaxAge(std::string sSection, std::string sKey, int64_t nSeconds);
void ClearCache(std::string sSection);
void WriteCache(std::string sSection, std::string sKey, std::string sValue, int64_t locktime, bool IgnoreCase=true);
std::string GetSporkValue(std::string sKey);
std::string TimestampToHRDate(double dtm);
std::string GetArrayElement(std::string s, std::string delim, int iPos);
void GetMiningParams(int nPrevHeight, bool& f7000, bool& f8000, bool& f9000, bool& fTitheBlocksActive);
bool SubmitProposalToNetwork(uint256 txidFee, int64_t nStartTime, std::string sHex, std::string& sError, std::string& out_sGovObj);
UniValue GetDataList(std::string sType, int iMaxAgeInDays, int& iSpecificEntry, std::string sSearch, std::string& outEntry);
double GetDifficulty(const CBlockIndex* blockindex);
std::string PubKeyToAddress(const CScript& scriptPubKey);
int DeserializePrayersFromFile();
double Round(double d, int place);
void SerializePrayersToFile(int nHeight);
CBlockIndex* FindBlockByHeight(int nHeight);
std::string rPad(std::string data, int minWidth);
bool Contains(std::string data, std::string instring);
bool CheckNonce(bool f9000, unsigned int nNonce, int nPrevHeight, int64_t nPrevBlockTime, int64_t nBlockTime, const Consensus::Params& params);
bool RPCSendMoney(std::string& sError, const CTxDestination &address, CAmount nValue, bool fSubtractFeeFromAmount, CWalletTx& wtxNew, bool fUseInstantSend=false, std::string sOptionalData = "", double nCoinAge = 0);
CAmount StringToAmount(std::string sValue);
bool CompareMask(CAmount nValue, CAmount nMask);
bool POOSOrphanTest(std::string sSanctuaryPubKey, int64_t nTimeout);
std::string GetElement(std::string sIn, std::string sDelimiter, int iPos);
bool CopyFile(std::string sSrc, std::string sDest);
std::string Caption(std::string sDefault, int iMaxLen);
void MemorizeBlockChainPrayers(bool fDuringConnectBlock, bool fSubThread, bool fColdBoot, bool fDuringSanctuaryQuorum);
double GetBlockVersion(std::string sXML);
std::string Uplink(bool bPost, std::string sPayload, std::string sBaseURL, std::string sPage, int iPort, int iTimeoutSecs, int iBOE, 
	std::map<std::string, std::string> mapRequestHeaders = std::map<std::string, std::string>(), std::string TargetFileName = "", bool fJson = false);
std::string FormatHTML(std::string sInput, int iInsertCount, std::string sStringToInsert);
std::string GJE(std::string sKey, std::string sValue, bool bIncludeDelimiter, bool bQuoteValue);
bool InstantiateOneClickMiningEntries();
bool WriteKey(std::string sKey, std::string sValue);
std::string GetTransactionMessage(CTransactionRef tx);
bool AdvertiseChristianPublicKeypair(std::string sProjectId, std::string sNickName, std::string sEmail, std::string sVendorType, bool fUnJoin, bool fForce, CAmount nFee, std::string sOptData, std::string &sError);
std::map<std::string, std::string> GetSporkMap(std::string sPrimaryKey, std::string sSecondaryKey);
std::map<std::string, CPK> GetGSCMap(std::string sGSCObjType, std::string sSearch, bool fRequireSig);
void WriteCacheDouble(std::string sKey, double dValue);
double ReadCacheDouble(std::string sKey);
double GetVINCoinAge(int64_t nBlockTime, CTransactionRef tx, bool fDebug);
CPK GetCPK(std::string sData);
std::string GetCPKData(std::string sProjectId, std::string sPK);
CAmount GetRPCBalance();
void GetGovSuperblockHeights(int& nNextSuperblock, int& nLastSuperblock);
int GetHeightByEpochTime(int64_t nEpoch);
std::string GetPOGBusinessObjectList(std::string sType, std::string sFields);
std::string SignMessageEvo(std::string strAddress, std::string strMessage, std::string& sError);
const CBlockIndex* GetBlockIndexByTransactionHash(const uint256 &hash);
double AddVector(std::string sData, std::string sDelim);
int ReassessAllChains();
double GetFees(CTransactionRef tx);
int64_t GetCacheEntryAge(std::string sSection, std::string sKey);
std::vector<std::string> GetVectorOfFilesInDirectory(const std::string &dirPath, const std::vector<std::string> dirSkipList);
std::string GetAttachmentData(std::string sPath, bool fEncrypted);
std::string Path_Combine(std::string sPath, std::string sFileName);
void ProcessBLSCommand(CTransactionRef tx);
DACResult GetDecentralizedURL();
std::string BIPFS_Payment(CAmount nAmount, std::string sTXID, std::string sXML);
DACResult DSQL_ReadOnlyQuery(std::string sXMLSource);
DACResult DSQL_ReadOnlyQuery(std::string sEndpoint, std::string sXML);
std::string TeamToName(int iTeamID);
std::string GetEPArg(bool fPublic);
CoinVin GetCoinVIN(COutPoint o, int64_t nTxTime);
bool GetTxDAC(uint256 txid, CTransactionRef& tx1);
std::string SearchChain(int nBlocks, std::string sDest);
uint256 ComputeRandomXTarget(uint256 hash, int64_t nPrevBlockTime, int64_t nBlockTime);
std::string ReverseHex(std::string const & src);
uint256 GetRandomXHash(std::string sHeaderHex, uint256 key, uint256 hashPrevBlock, int iThreadID);
uint256 GetRandomXHash2(std::string sHeaderHex, uint256 key, uint256 hashPrevBlock, int iThreadID);
std::string GenerateFaucetCode();
void WriteBinaryToFile(char const* filename, std::vector<char> data);
std::tuple<std::string, std::string, std::string> GetOrphanPOOSURL(std::string sSanctuaryPubKey);
bool ApproveSanctuaryRevivalTransaction(CTransaction tx);
bool VoteWithCoinAge(std::string sGobjectID, std::string sOutcome, std::string& ERROR_OUT);
double GetCoinAge(std::string txid);
CoinAgeVotingDataStruct GetCoinAgeVotingData(std::string sGobjectID);
std::string GetAPMNarrative();
std::string SplitFile(std::string sPath);
DACResult SubmitIPFSPart(int iPort, std::string sWebPath, std::string sTXID, std::string sBaseURL, std::string sPage, std::string sOriginalName, std::string sFileName, int iPartNumber, int iTotalParts, int iDensity, int iDuration, bool fEncrypted, CAmount nFee);
DACResult DownloadFile(std::string sBaseURL, std::string sPage, int iPort, int iTimeoutSecs, std::string sTargetFileName, bool fEncrypted);
DACResult BIPFS_UploadFile(std::string sLocalPath, std::string sWebPath, std::string sTXID, int iTargetDensity, int nDurationDays, bool fDryRun, bool fEncrypted);
DACResult BIPFS_UploadFolder(std::string sDirPath, std::string sWebPath, std::string sTXID, int iTargetDensity, int nDurationDays, bool fDryRun, bool fEncrypted);
bool SendDWS(std::string& sTXID, std::string& sError, std::string sReturnAddress, std::string sCPK, double nAmt, double nDuration, bool fDryRun);
std::string GetHowey(bool fRPC, bool fBurn);
bool EncryptFile(std::string sPath, std::string sTargetPath);
bool DecryptFile(std::string sPath, std::string sTargetPath);
std::string FormatURL(std::string URL, int iPart);
void SyncSideChain(int nHeight);
std::string GetUTXO(std::string sHash, int nOrdinal, CAmount& nValue, std::string& sError);
bool IsDuplicateUTXO(std::string UTXO);
UTXOStake GetUTXOStakeByUTXO2(std::vector<UTXOStake>& utxoStakes, std::string UTXO, bool fIncludeSpent);
void SendChat(CChat chat);
UserRecord GetUserRecord(std::string sSourceCPK);
RSAKey GetMyRSAKey();
RSAKey GetTestRSAKey();
std::string Mid(std::string data, int nStart, int nLength);
std::string GetSANDirectory4();
void WriteUnsignedBytesToFile(char const* filename, std::vector<unsigned char> outchar);
bool PayEmailFees(CEmail email);
void SendEmail(CEmail email);
double GetDACDonationsByRange(int nStartHeight, int nRange);
UserRecord GetMyUserRecord();
bool WriteDataToFile(std::string sPath, std::string data);
std::vector<char> ReadAllBytesFromFile(char const* filename);
SimpleUTXO QueryUTXO(int64_t nTargetLockTime, double nTargetAmount, std::string sTicker, std::string sAddress, std::string sUTXO, int xnOut, std::string& sError, bool fReturnFirst = false);
bool SendUTXOStake(double nTargetAmount, std::string sForeignTicker, std::string& sTXID, std::string& sError, std::string sBBPAddress, std::string sBBPUTXO, std::string sForeignAddress, std::string sForeignUTXO, 
	std::string sBBPSig, std::string sForeignSig, std::string sCPK, bool fDryRun, UTXOStake& out_utxostake, int nCommitmentDays);
std::vector<UTXOStake> GetUTXOStakes(bool fIncludeMemoryPool);
int AssimilateUTXO(UTXOStake d);
UTXOStake GetUTXOStakeByUTXO(std::string sUTXOStake);
std::string GetUTXOSummary(std::string sCPK, CAmount& nBBPQty);
std::string ScanBlockForNewUTXO(const CBlock& block);
double GetVINAge2(int64_t nBlockTime, CTransactionRef tx, CAmount nMinAmount, bool fDebug);
std::string strReplace(std::string str_input, std::string str_to_find, std::string str_to_replace_with);
double AddressToPin(std::string sAddress);
bool CompareMask2(CAmount nAmount, double nMask);
std::vector<DACResult> GetDataListVector(std::string sType, int nDaysLimit);
PriceQuote GetPriceQuote(std::string sForeignSymbol, CAmount nBBPQty, CAmount nForeignQty);
int64_t HRDateToTimestamp(std::string sDate);
void AppendStorageFile(std::string sDataStoreName, std::string sData);
bool HashExistsInDataFile(std::string sDataStoreName, std::string sHash);
std::string GetPopUpVerses(std::string sRange);
bool SignStake(std::string sBitcoinAddress, std::string strMessage, std::string& sError, std::string& sSignature);
double GetCryptoPrice(std::string sURL);
bool VerifySigner(std::string sXML);
double GetPBase(double& out_BTC, double& out_BBP);
bool GetTransactionTimeAndAmount(uint256 txhash, int nVout, int64_t& nTime, CAmount& nAmount);
std::string SendBlockchainMessage(std::string sType, std::string sPrimaryKey, std::string sValue, double dStorageFee, int nSign, std::string sExtraPayload, std::string& sError);
std::string ToYesNo(bool bValue);
bool VoteForGobject(uint256 govobj, std::string sVoteOutcome, std::string& sError);
int64_t GetDCCFileAge();
std::string GetSANDirectory2();
bool CreateLegacyGSCTransmission(std::string sCampaign, std::string sGobjectID, std::string sOutcome, std::string sDiary, std::string& sError);
bool ValidateAddress2(std::string sAddress);
std::string GetReleaseSuffix();
boost::filesystem::path GetDeterministicConfigFile();
boost::filesystem::path GetMasternodeConfigFile();
CAmount ARM64();
std::vector<NFT> GetNFTs(bool fIncludeMemoryPool);
bool ProcessNFT(NFT& nft, std::string sAction, std::string sBuyerCPK, CAmount nBuyPrice, bool fDryRun, std::string& sError);
NFT GetNFT(CTransactionRef tx1);
NFT GetSpecificNFT(uint256 txid);
bool IsDuplicateNFT(NFT& nft);
CAmount GetAmountPaidToRecipient(CTransactionRef tx1, std::string sRecipient);
bool ApproveUTXOSpendTransaction(CTransaction tx);
double GetHighDWURewardPercentage(double dCommitment);
CAmount GetUTXOPenalty(CTransaction tx, double& nPenaltyPercentage, CAmount& nAmountBurned);
void LockUTXOStakes();
int64_t GetTxTime1(uint256 hash, int ordinal);
std::string RPCSendMessage(CAmount nAmount, std::string sToAddress, bool fDryRun, std::string& sError, std::string sPayload);
std::string SendReferralCode(std::string& sError);
CAmount CheckReferralCode(std::string sCode);
ReferralCode GetTotalPortfolioImpactFromReferralCodes(std::vector<ReferralCode>& vRC, std::vector<UTXOStake>& vU, std::string sCPK, UniValue& details);
std::string ClaimReferralCode(std::string sCode, std::string& sError);
double GetReferralCodeEffectivity(int nPortfolioTime);
uint64_t GetPortfolioTimeAndSize(std::vector<UTXOStake>& uStakes, std::string sCPK, CAmount& nBBPSize);
std::vector<ReferralCode> GetReferralCodes();
CAmount GetBBPSizeFromPortfolio(std::string sCPK);
DACResult MailLetter(DMAddress dmFrom, DMAddress dmTo, bool fDryRun);
DACResult MakeDerivedKey(std::string sPhrase);
DACResult ReadAccountingEntry(std::string sKey, std::string sKey2);
bool WriteAccountingEntry(std::string sKey, std::string sKey2, std::string sValue, CAmount nAmount);
DMAddress DeserializeFrom();
std::vector<DMAddress> ImportGreetingCardCSVFile(std::string sFullPath);
DACResult SendInvoice(Invoice i);
DACResult SendPayment(Payment p);
std::vector<Invoice> GetInvoices();
std::vector<Payment> GetPayments();
DACResult SendDSQL(UniValue& oDSQLObject, std::string sTable, std::string ID);
std::vector<CDSQLQuery> DSQLQuery(std::string sFilter);
void ProcessDSQLInstantSendTransaction(CTransaction tx);

#endif
