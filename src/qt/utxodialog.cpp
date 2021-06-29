// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "utxodialog.h"
#include "forms/ui_utxodialog.h"
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

UTXODialog::UTXODialog(const PlatformStyle *platformStyle, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::UTXODialog),
    model(0),
    platformStyle(platformStyle)
{
    ui->setupUi(this);
	ui->cmbTicker->clear();
	ui->cmbTicker->addItem("");
	ui->cmbTicker->addItem("BBP (BiblePay)");
	ui->cmbTicker->addItem("BCH (Bitcoin-Cash)");
	ui->cmbTicker->addItem("BTC (Bitcoin)");
	ui->cmbTicker->addItem("DASH (Dashpay)");
	ui->cmbTicker->addItem("DOGE (Dogecoin)");
	ui->cmbTicker->addItem("ETH (Ethereum)");
	ui->cmbTicker->addItem("LTC (Litecoin)");
	ui->cmbTicker->addItem("XLM (Stellar Lumens)");
	ui->cmbTicker->addItem("XRP (Ripple)");
	ui->cmbTicker->addItem("ZEC (Z-Cash)");

	ui->txtPin->setValidator( new QIntValidator(0, 99999, this) );
	ui->txtAmount->setValidator( new QIntValidator(5000, 20000000, this) );
	ui->cmbTicker->setCurrentIndex(-1);

	ui->lblAmount->setVisible(false);
	ui->txtAmount->setVisible(false);
	ui->chkAdvanced->setVisible(false);

	connect(ui->cmbTicker, SIGNAL(currentIndexChanged(int)), this, SLOT(ChangeCurrency(int)));
	connect(ui->txtAddress, SIGNAL(textChanged(QString)), this, SLOT(onAddressChanged()));
    
}

void UTXODialog::onAddressChanged()
{
	std::string sAddress = GUIUtil::FROMQS(ui->txtAddress->text());
	double nPin = AddressToPin(sAddress);
	ui->txtPin->setText(GUIUtil::TOQS(RoundToString(nPin, 0)));
}

void UTXODialog::ChangeCurrency(int i)
{
	std::string sTicker = GUIUtil::FROMQS(ui->cmbTicker->currentText());
	std::string sTick1 = GetElement(sTicker, " ", 0);
	if (sTick1 == "BBP")
	{
		ui->lblAmount->setVisible(true);
		ui->txtAmount->setVisible(true);
		// For BBP we use the users staking address for this
		std::string sStaking = DefaultRecAddress("UTXO-Retirement-Account"); 
		ui->txtAddress->setText(GUIUtil::TOQS(sStaking));
	}
	else
	{
			ui->lblAmount->setVisible(false);
			ui->txtAmount->setVisible(false);
			ui->txtAddress->setText("");
	}

}

void UTXODialog::UpdateDisplay()
{
	std::string sInfo = "Note: It costs 500 BBP to create a UTXO record.";
	ui->txtInfo->setText(GUIUtil::TOQS(sInfo));
	// This generates a biblepay cryptocurrency index chart containing the weighted index over the last 6 months for each of the tickers (we have 8 as of May 28th, 2021)
	std::string sURL = "https://foundation.biblepay.org";
	std::string sPage = "Images/index.png";
	std::string sTargPath =  GetSANDirectory1() + "index.png";
	DACResult d = DownloadFile(sURL, sPage, 443, 30000, sTargPath, false);
	QString qsLoc = GUIUtil::TOQS(sTargPath);
	QPixmap img(qsLoc);
	QLabel *label = new QLabel(this);
	ui->lblIndex->setPixmap(img);
	std::string sDWU = "<font color='gold'><span style='font-size:140%'>DWU: " + RoundToString(CalculateUTXOReward() * 100, 2) + "%</font>";
	ui->lblInfo->setText(GUIUtil::TOQS(sDWU));
}

void UTXODialog::setModel(WalletModel *model)
{
    this->model = model;
}

UTXODialog::~UTXODialog()
{
    delete ui;
}

void UTXODialog::clear()
{
    ui->txtAmount->setText("");
	ui->txtPin->setText("");
	ui->txtAddress->setText("");
	ui->cmbTicker->setCurrentIndex(-1);
	ui->txtInfo->setText(""); 
	ui->chkAdvanced->setChecked(false);
}

void UTXODialog::on_btnQuery_clicked()
{
	std::string sError;
	std::string sTicker = GetElement(GUIUtil::FROMQS(ui->cmbTicker->currentText()), " ", 0);
	if (sTicker.empty()) 
		sError += "Ticker must be chosen. ";
	UTXOStake u;
	u.Address = GUIUtil::FROMQS(ui->txtAddress->text());
	
	if (u.Address.empty())
		sError += "Address must be populated. ";

	if (u.Address.length() > 128)
		sError += "Address too long. ";

	boost::to_upper(sTicker);
	
	bool fAddressValid = ValidateAddress3(sTicker, u.Address);
	if (!fAddressValid)
	{
		sError += "Address invalid. ";
	}
	if (!sError.empty())
	{
		QMessageBox::warning(this, tr("Error"), GUIUtil::TOQS(sError), QMessageBox::Ok, QMessageBox::Ok);
		return;
	}

	u.Ticker = sTicker;
	u.Time = 1;
	AssimilateUTXO(u, 0); // Harvest Todo: Make this configurable
	std::vector<SimpleUTXO> l = GetUTXOStatus(u.Address);
	std::string sHTML = "<table style='background-color:maroon;color:gold;border=1px'><tr><th>TXID<th>Amount</tr>";
	for (auto s : l)
	{
		std::string sRow = "<tr><td>" + Mid(s.TXID, 0, 20) + "&nbsp;</td><td>" + AmountToString(s.nAmount) + "</td></tr>";
		sHTML += sRow;
	}
	if (l.size() == 0)
	{
		sHTML = "This address has no UTXOs. ";
	}

	QMessageBox::warning(this, tr("UTXO Status"), GUIUtil::TOQS(sHTML), QMessageBox::Ok, QMessageBox::Ok);

}

void UTXODialog::on_btnSubmit_clicked()
{
	std::string sError;
	std::string sTicker = GetElement(GUIUtil::FROMQS(ui->cmbTicker->currentText()), " ", 0);
	if (sTicker.empty()) 
		sError += "Ticker must be chosen. ";

	UTXOStake u;
	u.Address = GUIUtil::FROMQS(ui->txtAddress->text());
	
	if (u.Address.empty())
		sError += "Address must be populated. ";

	if (u.Address.length() > 128)
		sError += "Address too long. ";

	boost::to_upper(sTicker);
	
	bool fAddressValid = ValidateAddress3(sTicker, u.Address);
	if (!fAddressValid)
	{
		sError += "Address invalid. ";
	}

	double nAmount = cdbl(GUIUtil::FROMQS(ui->txtAmount->text()), 0);
	u.Ticker = sTicker;

	UTXOStake uOld = GetUTXOStakeByAddress(u.Address);
	std::string sOptFundAddress;
	CAmount nOptFund = 0;
	if (u.Ticker == "BBP" && nAmount > 0)
	{
		// Apply mask
		double nPin = AddressToPin(u.Address);
		nOptFund = nPin + (nAmount * COIN);  
		sOptFundAddress = u.Address;
	}

	if (sError.empty())
	{
		AddUTXOStake(u, false, sError, sOptFundAddress, nOptFund);
	}
		
	std::string sNarr = (sError.empty()) ? "Successfully added UTXO record " + u.TXID.GetHex() + ". <br><br>Thank you for using BiblePay Retirement Accounts. " : sError;
	QMessageBox::warning(this, tr("UTXO Add Result"), GUIUtil::TOQS(sNarr), QMessageBox::Ok, QMessageBox::Ok);
	if (sError.empty())
	{
		clear();
	}
}

