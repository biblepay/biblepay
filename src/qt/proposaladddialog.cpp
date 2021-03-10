// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "proposaladddialog.h"
#include "ui_proposaladddialog.h"
#include "addressbookpage.h"
#include "addresstablemodel.h"
#include "bitcoinunits.h"
#include "guiutil.h"
#include "util.h"
#include "optionsmodel.h"
#include "timedata.h"
#include "platformstyle.h"
#include "receiverequestdialog.h"
#include "recentrequeststablemodel.h"
#include "governance.h"
#include "governance-vote.h"
#include "governance-classes.h"

#include "walletmodel.h"
#include "validation.h"
#include "rpcpog.h"
#include <QAction>
#include <QCursor>
#include <QItemSelection>
#include <QMessageBox>
#include <QScrollBar>
#include <QTextDocument>
#include <boost/algorithm/string/case_conv.hpp> // for to_lower()

ProposalAddDialog::ProposalAddDialog(const PlatformStyle *platformStyle, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ProposalAddDialog),
    model(0),
    platformStyle(platformStyle)
{
    ui->setupUi(this);
    QString theme = GUIUtil::getThemeName();
    
    if (!platformStyle->getImagesOnButtons()) {
        ui->btnSubmit->setIcon(QIcon());
    } else {
        ui->btnSubmit->setIcon(QIcon(":/icons/" + theme + "/receiving_addresses"));
    }

	ui->cmbExpenseType->clear();
 	ui->cmbExpenseType->addItem("Charity");
	ui->cmbExpenseType->addItem("PR");
	ui->cmbExpenseType->addItem("P2P");
	ui->cmbExpenseType->addItem("IT");
	ui->cmbExpenseType->addItem("SPORK");
	ui->cmbExpenseType->addItem("XSPORK-ORPHAN");
	ui->cmbExpenseType->addItem("XSPORK-CHARITY");
 }


void ProposalAddDialog::UpdateDisplay()
{
	int nNextHeight = GetNextSuperblock();

	std::string sInfo = "Note: Proposal Cost is 2500 " + CURRENCY_NAME + ".  Next Superblock at height: " + RoundToString(nNextHeight, 0);

	std::string sNarr;
	for (int i = 0; i < mvQueuedProposals.size(); i++)
	{
		QueuedProposal q = mvQueuedProposals[i];
		if (q.PrepareHeight > chainActive.Tip()->nHeight - 100)
		{
			std::string sSubmitted = q.Submitted ? "Submitted " + q.GovObj.GetHex() : "Waiting";
			std::string sErrNarr = q.Error.empty() ? "" : " (Error: " + q.Error + ")";
			sNarr += q.TXID.GetHex().substr(0, 10) + "=[height " + RoundToString(q.PrepareHeight, 0) + "] :: " + sSubmitted + " <br>" + sErrNarr + ";<br>";
		}
	}
	if (sNarr.length() > 5)
		sNarr = sNarr.substr(0, sNarr.length() - 5);

	sInfo += "<br>" + sNarr;
	ui->txtInfo->setText(GUIUtil::TOQS(sInfo));
}

/*
void ProposalAddDialog::attachFile()
{
    QString filename = GUIUtil::getOpenFileName(this, tr("Select a file to attach to this proposal"), "", "", NULL);
    if(filename.isEmpty()) return;
    
	QUrl fileUri = QUrl::fromLocalFile(filename);
	std::string sFN = GUIUtil::FROMQS(fileUri.toString());
	bool bFromWindows = Contains(sFN, "file:///C:") || Contains(sFN, "file:///D:") || Contains(sFN, "file:///E:");
	if (!bFromWindows)
	{
		sFN = strReplace(sFN, "file://", "");  // This leaves the full unix path
	}
	else
	{
		sFN = strReplace(sFN, "file:///", "");  // This leaves the windows drive letter
	}
	if (sFN.empty())
		return;

	if (sFN.length() < 5)
		return;

	std::string sExt = sFN.substr(sFN.length() - 3, 3);
	boost::to_lower(sExt);
	if (sExt != "pdf")
	{
		std::string sNarr = "Sorry, The only files supported are pdfs.";
		LogPrintf("Unable to add to a proposal %s::", sFN);
	 	QMessageBox::warning(this, tr("Proposal Add File Attachment Result"), GUIUtil::TOQS(sNarr), QMessageBox::Ok, QMessageBox::Ok);
		return;
	}
	// Check the size
	std::vector<char> v = ReadBytesAll(sFN.c_str());
	if (v.size() < 1)
	{
		std::string sNarr = "Sorry, the file is too small.  We only support up to 256K files currently.  We may increase this in the future, after we verify stability. ";
		QMessageBox::warning(this, tr("Proposal Add Attachment Failed"), GUIUtil::TOQS(sNarr), QMessageBox::Ok, QMessageBox::Ok);
		return;
	}

	std::string sVHex = HexStr(v.begin(), v.end());
	if (sVHex.length() > 256000 || sVHex.length() < 1)
	{
		std::string sNarr = "Sorry, the file is too big.  We only support up to 256K files currently.  We may increase this in the future, after we verify stability. ";
		QMessageBox::warning(this, tr("Proposal Add Attachment Failed"), GUIUtil::TOQS(sNarr), QMessageBox::Ok, QMessageBox::Ok);
		return;
	}
    ui->txtAttach->setText(GUIUtil::TOQS(sFN));
}
*/

void ProposalAddDialog::setModel(WalletModel *model)
{
    this->model = model;

    if(model && model->getOptionsModel())
    {
		UpdateDisplay();
    }
}

ProposalAddDialog::~ProposalAddDialog()
{
    delete ui;
}

void ProposalAddDialog::clear()
{
    ui->txtName->setText("");
    ui->txtURL->setText("");
	ui->txtAmount->setText("");
	ui->txtAddress->setText("");
}


void ProposalAddDialog::on_btnSubmit_clicked()
{
    if(!model || !model->getOptionsModel())
        return;
	std::string sName = GUIUtil::FROMQS(ui->txtName->text());
	std::string sAddress = GUIUtil::FROMQS(ui->txtAddress->text());
	std::string sAmount = GUIUtil::FROMQS(ui->txtAmount->text());
	std::string sURL = GUIUtil::FROMQS(ui->txtURL->text());
	std::string sError;
	if (sName.length() < 3)
		sError += "Proposal Name must be populated. ";
	CBitcoinAddress address(sAddress);
	if (!address.IsValid()) 
		sError += "Proposal Funding Address is invalid. ";
	if (cdbl(sAmount,0) < 100) 
		sError += "Proposal Amount is too low. ";
	if (sURL.length() < 10) 
		sError += "You must enter a discussion URL. ";
	std::string sExpenseType = GUIUtil::FROMQS(ui->cmbExpenseType->currentText());
	if (sExpenseType.empty()) 
		sError += "Expense Type must be chosen. ";
	CAmount nBalance = GetRPCBalance();

	if (nBalance < (2501*COIN)) 
		sError += "Sorry balance too low to create proposal collateral. ";

	 if (model->getEncryptionStatus() == WalletModel::Locked)
		 sError += "Sorry, wallet must be unlocked. ";
       
	std::string sPrepareTxId;
	std::string sHex;
	int64_t unixStartTimestamp = GetAdjustedTime();
	int64_t unixEndTimestamp = GetAdjustedTime() + (60 * 60 * 24 * 30);
	// Evo requires no spaces
	sName = strReplace(sName, " ", "_");

	if (sError.empty())
	{
		// gobject prepare 0 1 EPOCH_TIME HEX
		std::string sType = "1"; //Proposal
		std::string sQ = "\"";
		std::string sJson = "[[" + sQ + "proposal" + sQ + ",{";
		sJson += GJE("start_epoch", RoundToString(unixStartTimestamp, 0), true, false);
		sJson += GJE("end_epoch", RoundToString(unixEndTimestamp, 0), true, false);
		sJson += GJE("name", sName, true, true);
		sJson += GJE("payment_address", sAddress, true, true);
		sJson += GJE("payment_amount", sAmount, true, false);
		sJson += GJE("type", sType, true, false);
		sJson += GJE("expensetype", sExpenseType, true, true);

		CAmount nStorageFee = 0;

		/*
		// Anti-Censorship Features (ACF)
		if (!sPDFFilename.empty())
		{
			std::string sExt = sPDFFilename.substr(sPDFFilename.length() - 3, 3);
			boost::to_lower(sExt);
			if (sExt != "pdf")
				sError += "Invalid extension (only PDFs are supported).";

			std::vector<char> v = ReadBytesAll(sPDFFilename.c_str());
			if (v.size() > 1)
			{
				std::string sVHex = HexStr(v.begin(), v.end());
				if (sVHex.length() < 256000)
				{
					sJson += GJE("pdf", sVHex, true, true);
					boost::filesystem::path p(sPDFFilename);
					std::string sBaseName = p.filename().string();
					sJson += GJE("attachment_name", sBaseName, true, true);
					nStorageFee += (sVHex.length() * COIN);  // 1 bbp per byte per year?  I guess we need to poll this.
				}
				else
				{
					sError += "File too big.";
				}

			}
			else
			{
				sError += "File too small";
			}
		}
		// End of ACF
		*/

		sJson += GJE("url", sURL, false, true);
		sJson += "}]]";
		// make into hex
		std::vector<unsigned char> vchJson = std::vector<unsigned char>(sJson.begin(), sJson.end());
		sHex = HexStr(vchJson.begin(), vchJson.end());
		// ASSEMBLE NEW GOVERNANCE OBJECT FROM USER PARAMETERS
		uint256 hashParent = uint256();
		int nRevision = 1;
		// CREATE A NEW COLLATERAL TRANSACTION FOR THIS SPECIFIC OBJECT
		CGovernanceObject govobj(hashParent, nRevision, unixStartTimestamp, uint256(), sHex);
		if((govobj.GetObjectType() == GOVERNANCE_OBJECT_TRIGGER) || (govobj.GetObjectType() == GOVERNANCE_OBJECT_WATCHDOG)) 
		{
			sError += "Trigger and watchdog objects cannot be created from the UI yet.";
		}
		if (sError.empty())
		{
			if(!govobj.IsValidLocally(sError, false))
			{
				 LogPrintf("Error while creating governance object %s, object not valid. Error: %s \n", sJson, sError);
				 sError += "Governance object is not valid - " + govobj.GetHash().ToString();
			}

			if (nStorageFee > 0)
			{
				// Warn the user that we will charge them a storage fee.  
				double nTotalFee = (nStorageFee + govobj.GetMinCollateralFee()) / COIN;
				double nTimeLength = 1; // ToDo:  Make storage duration dynamic
				std::string sNarr = "A storage fee of " + RoundToString(nTotalFee, 2) + " applies to this object.  This allows biblepay to store the file for " 
					+ RoundToString(nTimeLength, 0) + " month(s).  Do you approve this fee?";
			    int ret = QMessageBox::warning(this, tr("Pay Storage Fee?"), GUIUtil::TOQS(sNarr), QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok);
				if (ret != QMessageBox::Ok)
				{
					sError = "User refused to pay the fee.";
				}
			}

			// For ACFs with PDF, increase the collateral fee to cover the cost of storage
			if (sError.empty())
			{
				sPrepareTxId = CreateGovernanceCollateral(govobj.GetHash(), govobj.GetMinCollateralFee() + nStorageFee, sError);
			}
		}
	}
	std::string sNarr = (sError.empty()) ? "Successfully Prepared Proposal " + sPrepareTxId + ".   NOTE: You must wait 6 confirms for the proposal to be submitted.  Please check back on this page periodically "
		+ " to ensure a successful transmission and that no error message is listed in the bottom area of the page. "
		+ "<br>WARNING: Do not shut down the core wallet until the proposal is submitted, otherwise you may lose your proposal submission and proposal collateral.  "
		+" <br><br>Thank you for using our Governance System." : sError;
	if (sError.empty())
	{
		// Set the proposal up to be submitted after 6 confirms using our Governance Service:
		QueuedProposal q;
		q.PrepareHeight = chainActive.Tip()->nHeight;
		q.TXID = uint256S(sPrepareTxId);
		q.StartTime = unixStartTimestamp;
		q.Hex = sHex;
		q.Submitted = false;
		mvQueuedProposals.push_back(q);
		clear();
	}
 	QMessageBox::warning(this, tr("Proposal Add Result"), GUIUtil::TOQS(sNarr), QMessageBox::Ok, QMessageBox::Ok);

    UpdateDisplay();
}


