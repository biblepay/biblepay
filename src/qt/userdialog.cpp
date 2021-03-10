// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "userdialog.h"
#include "ui_userdialog.h"
#include "guiutil.h"
#include "util.h"
#include "optionsmodel.h"
#include "timedata.h"
#include "platformstyle.h"

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

UserDialog::UserDialog(const PlatformStyle *platformStyle, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::UserDialog),
    model(0),
    platformStyle(platformStyle)
{
    ui->setupUi(this);
    QString theme = GUIUtil::getThemeName();
 }


void UserDialog::UpdateDisplay()
{
	// Load initial values
	std::string sCPK = DefaultRecAddress("Christian-Public-Key");

	UserRecord r = GetUserRecord(sCPK);

	ui->txtNickName->setText(GUIUtil::TOQS(r.NickName));
	ui->txtCPK->setText(GUIUtil::TOQS(sCPK));
	std::string sInfo = "Welcome to BiblePay " + r.NickName + " " + sCPK;

	ui->txtURL->setText(GUIUtil::TOQS(r.URL));
	ui->txtExternalEmail->setText(GUIUtil::TOQS(r.ExternalEmail));
	// Calculate internal email
	std::string sMyEmail = r.NickName + "@biblepay.core";
	ui->txtInternalEmail->setText(GUIUtil::TOQS(sMyEmail));
	ui->txtLongitude->setText(GUIUtil::TOQS(r.Longitude));
	ui->txtLatitude->setText(GUIUtil::TOQS(r.Latitude));
	ui->chkAuthorizePayments->setChecked((int)r.AuthorizePayments);
	RSAKey rsa = GetMyRSAKey();
	ui->txtRSAPublicKey->setText(GUIUtil::TOQS(rsa.PublicKey));
	ui->txtInfo->setText(GUIUtil::TOQS(sInfo));
}

void UserDialog::setModel(WalletModel *model)
{
    this->model = model;

    if(model && model->getOptionsModel())
    {
		UpdateDisplay();
    }
}

UserDialog::~UserDialog()
{
    delete ui;
}

void UserDialog::clear()
{
    ui->txtNickName->setText("");
    ui->txtURL->setText("");
	ui->txtRSAPublicKey->setText("");
	ui->txtExternalEmail->setText("");
	ui->txtInternalEmail->setText("");
	ui->txtLongitude->setText("");
	ui->txtLatitude->setText("");
}


void UserDialog::on_btnSave_clicked()
{
    if(!model || !model->getOptionsModel())
        return;
	std::string sNickName = GUIUtil::FROMQS(ui->txtNickName->text());
	std::string sURL = GUIUtil::FROMQS(ui->txtURL->text());
	std::string sExternalEmail = GUIUtil::FROMQS(ui->txtExternalEmail->text());
	std::string sInternalEmail = GUIUtil::FROMQS(ui->txtInternalEmail->text());
	std::string sLongitude = GUIUtil::FROMQS(ui->txtLongitude->text());
	std::string sLatitude = GUIUtil::FROMQS(ui->txtLatitude->text());
	std::string sRSAPublicKey = GUIUtil::FROMQS(ui->txtRSAPublicKey->text());
	int nChecked = ui->chkAuthorizePayments->isChecked();
	
	std::string sError;
	std::string sErrConcat = sNickName + sURL + sExternalEmail + sInternalEmail + sLongitude + sLatitude + sRSAPublicKey;
	if (Contains(sNickName, " "))
		sError += "Please remove spaces from Nickname as this might break pop3 compatibility.  You may use underscores. ";

	if (Contains(sErrConcat, "|"))
		sError += "User input contains illegal characters. ";

	if (Contains(sNickName, " ") || Contains(sURL, " "))
		sError += "User input contains spaces. ";

	if (sNickName.length() < 3)
		sError += "Nick Name must be populated. ";

	if (sNickName.length() > 32)
		sError += "Nick Name must be shorter than 33 bytes. ";

	if (sURL.length() > 255)
		sError += "URL must be less than 256 bytes. ";

	if (sExternalEmail.length() > 100)
		sError += "External Email length must be shorter than 101 bytes.";

	if (sLongitude.length() > 0 && cdbl(sLongitude, 0) == 0)
		sError += "Longitude must be a number. ";

	if (sLongitude.length() > 32 || sLatitude.length() > 32)
		sError += "Both Longitude and Latitude must be shorter than 32 decimals. ";

	if (sLatitude.length() > 0 && cdbl(sLatitude, 0) == 0)
		sError += "Latitude must be a number. ";

	if (sRSAPublicKey.length() > 512)
		sError += "RSA Public key must be less than 513 bytes. ";

	if (sExternalEmail.length() > 0 && !is_email_valid(sExternalEmail))
		sError += "E-mail must be valid. ";

	// Pull RSA from the chain

	CAmount nBalance = GetRPCBalance();
	if (nBalance < (50*COIN)) 
		sError += "Sorry balance too low to create user record. ";

	 if (model->getEncryptionStatus() == WalletModel::Locked)
		 sError += "Sorry, wallet must be unlocked. ";
    
	std::string sNarr = "NOTE:  Please wait at least 3 blocks for this data to be saved in the chain.  ";
	if (sError.empty())
	{
		// Save the record
		std::string sOptData = sURL + "|" + sInternalEmail + "|" + sLongitude + "|" + sLatitude + "|" + sRSAPublicKey + "|" + RoundToString(nChecked, 0);

		bool fAdv = AdvertiseChristianPublicKeypair("cpk", sNickName, sExternalEmail, "user", false, true, 0, sOptData, sError);
		if (!fAdv)
		{
		 	QMessageBox::warning(this, tr("User Edit - Save Failure"), GUIUtil::TOQS(sError), QMessageBox::Ok, QMessageBox::Ok);
		}
		else
		{
			QMessageBox::warning(this, tr("User Edit - Save was Successful"), GUIUtil::TOQS(sNarr), QMessageBox::Ok, QMessageBox::Ok);
		    UpdateDisplay();
		}
	}
	else
	{
	 	QMessageBox::warning(this, tr("User Edit - Save Failure"), GUIUtil::TOQS(sError), QMessageBox::Ok, QMessageBox::Ok);
	}

}

