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
	ui->cmbType->addItem("Easter Card");
	ui->cmbType->addItem("Kittens Card");
	connect(ui->cmbType, SIGNAL(currentIndexChanged(int)), SLOT(on_TypeChanged(int)));

	ui->txtGiftCardAmount->setValidator( new QIntValidator(0, 100000000, this) );
	ui->txtZip->setValidator( new QIntValidator(9999, 99999, this) );
	std::string sCPK = DefaultRecAddress("Christian-Public-Key");
	on_TypeChanged(0);
}

void MailSendDialog::on_TypeChanged(int iType)
{
	std::string sType = GUIUtil::FROMQS(ui->cmbType->currentText());
	if (sType == "Christmas Card")
	{
		ui->txtOpeningSalutation->setText("Merry Christmas!");
		ui->txtClosingSalutation->setText("Warm Christmas Greetings");
		ui->txtParagraph1->setPlainText("Peace and Joy to you and your family this Christmas Season.  We are thinking warmly of you.  May your family be blessed with the Richest Blessings of Abraham, Isaac and Jacob. ");
	}
	else if (sType == "Easter Card")
	{
		ui->txtOpeningSalutation->setText("Happy Easter!");
		ui->txtClosingSalutation->setText("He is Risen");
		ui->txtParagraph1->setPlainText("Peace and Joy to you and your family this Easter and Passover week! We are thinking warmly of you.  May your family be blessed with the Richest Blessings of Abraham, Isaac and Jacob. ");
	}
	else if (sType == "Kittens Card")
	{
		ui->txtOpeningSalutation->setText("May Kittens Surround You!");
		ui->txtClosingSalutation->setText("With Kitten Love");
		ui->txtParagraph1->setPlainText("I've been thinking of you and had to write you.  ");
	}
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
	ui->txtParagraph1->setPlainText("");
	ui->txtParagraph2->setPlainText("");
	ui->txtOpeningSalutation->setText("");
	ui->txtClosingSalutation->setText("");
	ui->txtAddressLine1->setText("");
	ui->txtAddressLine2->setText("");
    ui->txtCity->setText("");
	ui->txtState->setText("");
	ui->txtZip->setText("");
	ui->txtGiftCardAmount->setText("");
	ui->chkDeliver->setChecked(false);
}

std::string CleanseMe(std::string sStr)
{
	sStr = strReplace(sStr, "\t", "");
	sStr = strReplace(sStr, "\r\n", "<br>");
	sStr = strReplace(sStr, "\r", "<br>");
	sStr = strReplace(sStr, "\n", "<br>");
	return sStr;
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
	
	dmTo.Paragraph1 = CleanseMe(GUIUtil::FROMQS(ui->txtParagraph1->toPlainText()));
	dmTo.Paragraph2 = CleanseMe(GUIUtil::FROMQS(ui->txtParagraph2->toPlainText()));
	dmTo.OpeningSalutation = GUIUtil::FROMQS(ui->txtOpeningSalutation->text());
	dmTo.ClosingSalutation = GUIUtil::FROMQS(ui->txtClosingSalutation->text());

	bool fDryRun = !(ui->chkDeliver->checkState());
	// Temporary while in testnet:
	fDryRun = false;


	std::string sError;
	
	if (dmTo.Name.length() < 3)
		sError += "Name must be populated. ";
	if (dmFrom.Name.length() < 3 || dmFrom.AddressLine1.length() < 4 || dmFrom.City.length() < 4 || dmFrom.State.length() < 2 || dmFrom.Zip.length() < 4)
	{
		sError += "From information is not populated.  Please set your address with 'exec setmyaddress \"name,address,city,state,zip\"'";
	}
	if (dmTo.AddressLine1.length() < 4 || dmTo.City.length() < 3 || dmTo.State.length() < 2 || dmTo.Zip.length() < 4)
		sError += "You must enter the address, city, state and zip. ";
	dmTo.Template = GUIUtil::FROMQS(ui->cmbType->currentText());
	if (dmTo.Template.empty()) 
		sError += "Type must be chosen. ";
	if (dmTo.Name.length() > 128)
		sError += "Name must be < 128 chars.";
	if (dmTo.Paragraph1.length() > 777)
		sError += "Paragraph 1 length must be < 777 chars.";
	if (dmTo.Paragraph2.length() > 777)
		sError += "Paragraph 2 length must be < 777 chars.";

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

		dmTo.Paragraph2 += "<p><br>A gift of $" + RoundToString(dSrcAmount, 2) + " [" + RoundToString((double)dmTo.Amount/COIN, 2) 
						+ " BBP] has been sent to you!  "
						+ " <br>" + sInstructions;

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
	if (sError.empty())
	{
		clear();
	}
}


