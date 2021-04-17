// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "mailsenddialog.h"
#include "forms/ui_mailsenddialog.h"
#include "addressbookpage.h"
#include "addresstablemodel.h"
#include "bitcoinunits.h"
#include "guiutil.h"
#include "util.h"
#include "optionsmodel.h"
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
#include <QListView>
#include <QStandardItem>

MailSendDialog::MailSendDialog(const PlatformStyle *platformStyle, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::MailSendDialog),
    model(0),
    platformStyle(platformStyle)
{
    ui->setupUi(this);
	// Create Card section
	ui->cmbType->clear();
	ui->cmbType->addItem("Christmas Card");
	ui->txtGiftCardAmount->setValidator( new QIntValidator(0, 100000000, this) );
	ui->txtZip->setValidator( new QIntValidator(9999, 99999, this) );
	std::string sCPK = DefaultRecAddress("Christian-Public-Key");
}

void MailSendDialog::UpdateDisplay(std::string sAction)
{
	std::string sInfo = "<small><font color=red>Note: It costs $1 + gift-card-value to deliver a card.  Example:  Card with no gift:  $1; Card with $5 gift = $6.00. </small>";
	ui->txtInfo->setText(GUIUtil::TOQS(sInfo));
}

void MailSendDialog::setModel(WalletModel *model)
{
    this->model = model;
}

MailSendDialog::~MailSendDialog()
{
    delete ui;
}

void MailSendDialog::clear()
{
    ui->txtName->setText("");
	ui->txtParagraph->setPlainText("");
	ui->txtAddressLine1->setText("");
	ui->txtAddressLine2->setText("");
    ui->txtCity->setText("");
	ui->txtState->setText("");
	ui->txtZip->setText("");
	ui->txtGiftCardAmount->setText("");
	ui->chkDeliver->setChecked(false);
}

bool AcquireWallet17()
{
	std::vector<CWallet*> wallets = GetWallets();
	if (wallets.size() > 0)
	{
		pwalletpog = wallets[0];
		return true;
	}
	else
	{
		pwalletpog = NULL;
	}
	return false;
}


void MailSendDialog::on_btnSubmit_clicked()
{
	DMAddress dmFrom = DeserializeFrom();

	DMAddress dmTo;
	dmTo.Name = GUIUtil::FROMQS(ui->txtName->text());
	dmTo.AddressLine1 = GUIUtil::FROMQS(ui->txtAddressLine1->text());
	dmTo.AddressLine2 = GUIUtil::FROMQS(ui->txtAddressLine2->text());
	dmTo.City = GUIUtil::FROMQS(ui->txtCity->text());
	dmTo.State = GUIUtil::FROMQS(ui->txtState->text());
	dmTo.Zip = GUIUtil::FROMQS(ui->txtZip->text());
	double dSrcAmount = cdbl(GUIUtil::FROMQS(ui->txtGiftCardAmount->text()), 2);
	dmTo.Amount = GetBBPValueUSD(dSrcAmount, 1537);
	
	dmTo.Paragraph = GUIUtil::FROMQS(ui->txtParagraph->toPlainText());
	dmTo.Paragraph = strReplace(dmTo.Paragraph, "\t", "");
	dmTo.Paragraph = strReplace(dmTo.Paragraph, "\r\n", "<br>");
	dmTo.Paragraph = strReplace(dmTo.Paragraph, "\r", "<br>");
	dmTo.Paragraph = strReplace(dmTo.Paragraph, "\n", "<br>");

	bool fDryRun = !(ui->chkDeliver->checkState());
	// Temporary while in testnet:
	fDryRun = false;

	LogPrintf("\nMailSendDialog::Sending paragraph %s\n", dmTo.Paragraph);

	std::string sError;
	
	if (dmTo.Name.length() < 3)
		sError += "Name must be populated. ";
	if (dmFrom.Name.length() < 3 || dmFrom.AddressLine1.length() < 4 || dmFrom.City.length() < 4 || dmFrom.State.length() < 2 || dmFrom.Zip.length() < 4)
	{
		sError += "From information is not populated.  Please set your address with 'exec setmyaddress \"name,address,city,state,zip\"'";
	}
	if (dmTo.AddressLine1.length() < 4 || dmTo.City.length() < 3 || dmTo.State.length() < 2 || dmTo.Zip.length() < 4)
		sError += "You must enter the address, city, state and zip. ";
	std::string sType = GUIUtil::FROMQS(ui->cmbType->currentText());
	if (sType.empty()) 
		sError += "Type must be chosen. ";
	if (dmTo.Name.length() > 128)
		sError += "Name must be < 128 chars.";
	if (dmTo.Paragraph.length() > 777)
		sError += "Extra paragraph length must be < 777 chars.";
	AcquireWallet17();
	if (pwalletpog->IsLocked())
	{
		sError += "Sorry, wallet must be unlocked.";
	}
    std::string sTXID;   
	if (dSrcAmount > 0 && sError.empty())
	{
		DACResult d = MakeDerivedKey(dmTo.AddressLine1);
		std::string sPayload = "<giftcard>" + RoundToString((double)dmTo.Amount/COIN, 2) + "</giftcard>";
		sTXID = RPCSendMessage(dmTo.Amount, d.Address, fDryRun, sError, sPayload);
		// 4-16-2021 add complete instructions
		LockUTXOStakes();
		std::string sPhrase = "<span style='white-space: nowrap;'><font color=lime> &nbsp;\"" + dmTo.AddressLine1 + "\"&nbsp; </font></span>";
		std::string sInstructions = "<br><small>To redeem your gift, download biblepay Desktop PC wallet from www.biblepay.org | Tools | Debug Console | acceptgift " + sPhrase + "</small>";

		dmTo.Paragraph += "<p><br>A gift of $" + RoundToString(dSrcAmount, 2) + " [" + RoundToString((double)dmTo.Amount/COIN, 2) 
						+ " BBP] has been sent to you!  "
						+ " <br>" + sInstructions;

		LogPrintf("\nSendGiftDialog::Sending a gift to %s, amt=%f, Error %s", dmTo.Paragraph, (double)dmTo.Amount/COIN, sError);
	}
	std::string sPDF;
	if (sError.empty())
	{
		// Todo Harvest: change true to fDryRun = !fSend
		DACResult b = MailLetter(dmFrom, dmTo, true);
		sError = ExtractXML(b.Response, "<error>", "</error>");
		sPDF = ExtractXML(b.Response, "<pdf>", "</pdf>");
	}
		
	std::string sNarr = (sError.empty()) ? "<small>Successfully created Mail Delivery " + sTXID + ". <br><small><a href='" 
		+ sPDF + "' style='text-decoration:none;color:pink;font-size:150%'><b>Click HERE to Review PROOF</b></a><br>Thank you for using BiblePay Physical Mail Delivery." : sError;
	ui->txtInfo->setText(GUIUtil::TOQS(sNarr));
 	QMessageBox::warning(this, tr("Mail Delivery Result"), GUIUtil::TOQS(sNarr), QMessageBox::Ok, QMessageBox::Ok);
	clear();
}


