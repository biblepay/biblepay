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
    connect(ui->btnCSV, SIGNAL(clicked()), this, SLOT(on_btnCSVClicked()));

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

void MailSendDialog::on_btnSubmit_clicked(bool fAutomatic)
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
		LockUTXOStakes();
		std::string sPhrase = "<span style='white-space: nowrap;'><font color=lime> \"" + dmTo.AddressLine1 + "\"</font></span>";
		std::string sInstructions = "<br><small>To redeem your gift, download biblepay Desktop PC wallet from www.biblepay.org | Tools | Debug Console | acceptgift " + sPhrase + "</small>";
		std::string sNarr = "<p><br>A gift of $" + RoundToString(dSrcAmount, 2) + " (" + RoundToString((double)dmTo.Amount/COIN, 2) + " BBP) has been sent to you!<br>" + sInstructions;
		dmTo.Paragraph2 = dmTo.Paragraph2 + sNarr;
		LogPrintf("\nCreating gift with virtual amount %f, containing %s %s ", dmTo.Amount/COIN, dmTo.Paragraph1, dmTo.Paragraph2);
	}
	std::string sPDF;
	if (sError.empty())
	{
		// Todo Harvest: change true to fDryRun = !fSend
		DACResult b = MailLetter(dmFrom, dmTo, fDryRun);
		sError = ExtractXML(b.Response, "<error>", "</error>");
		sPDF = ExtractXML(b.Response, "<pdf>", "</pdf>");
	}
		
	std::string sNarr = (sError.empty()) ? "<small>Successfully created Mail Delivery " + sTXID + ". <br><small><a href='" 
		+ sPDF + "' style='text-decoration:none;color:pink;font-size:150%'><b>Click HERE to Review PROOF</b></a><br>Thank you for using BiblePay Physical Mail Delivery." : sError;

	ui->txtInfo->setText(GUIUtil::TOQS(sNarr));
	if (!fAutomatic)
	{
 		QMessageBox::warning(this, tr("Mail Delivery Result"), GUIUtil::TOQS(sNarr), QMessageBox::Ok, QMessageBox::Ok);
		if (sError.empty())
		{
			clear();
			on_TypeChanged(0);
		}
	}
}

void MailSendDialog::on_btnCSVClicked()
{
    QString sFileName = GUIUtil::getOpenFileName(this, tr("Select CSV file to import"), "", "", nullptr);
    if (sFileName.isEmpty())
	{
		LogPrintf("\nCSV::User entered empty filename %f", 5092021);
        return;
	}
	std::string sFN = GUIUtil::FROMQS(sFileName);
	std::vector<DMAddress> dm = ImportGreetingCardCSVFile(sFN);
	int iQuestion = 0;
	bool fConfirming = true;
	bool fCancelled = false;
	for (int i = 0; i < dm.size(); i++)
	{
		std::string sRow = dm[i].Name + "," + dm[i].AddressLine1 + "," + dm[i].City + "," + dm[i].State + "," + dm[i].Zip;
		if (fConfirming)
		{
			iQuestion++;
			std::string sConfirm = "Warning #" + RoundToString(iQuestion, 0) + ":  <br>We will confirm the first 5 addresses, giving you a chance to cancel the batch.  <br><br>After the fifth prompt, we will process the rest unless you CANCEL.  <br><br>"
				+ "The next record looks like: " + sRow + ".  <br><br>Press [OK] to process this record and continue, or [CANCEL] to cancel the entire batch.  ";

			int ret = QMessageBox::warning(this, tr("Automated Mail Delivery System"), GUIUtil::TOQS(sConfirm), QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok);

			if (ret==QMessageBox::Ok)
			{
				if (iQuestion == 5)
				{
					fConfirming = false;
				}
			}
			else if (ret == QMessageBox::Cancel)
			{
				fCancelled = true;
				break;
			}
		}

		if (!fCancelled)
		{
			ui->txtName->setText(GUIUtil::TOQS(dm[i].Name));
			ui->txtAddressLine1->setText(GUIUtil::TOQS(dm[i].AddressLine1));
			ui->txtAddressLine2->setText(GUIUtil::TOQS(dm[i].AddressLine2));
			ui->txtCity->setText(GUIUtil::TOQS(dm[i].City));
			ui->txtState->setText(GUIUtil::TOQS(dm[i].State));
			ui->txtZip->setText(GUIUtil::TOQS(dm[i].Zip));
			on_btnSubmit_clicked(true);
		}
	}
}

