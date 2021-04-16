// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "nftadddialog.h"
#include "forms/ui_nftadddialog.h"
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

NFTAddDialog::NFTAddDialog(const PlatformStyle *platformStyle, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::NFTAddDialog),
    model(0),
    platformStyle(platformStyle)
{
    ui->setupUi(this);
	// Add/Edit NFT section
	ui->cmbNFTType->clear();
	ui->cmbNFTType->addItem("Digital Good (MP3, PNG, GIF, JPEG, PDF)");
	ui->cmbNFTType->addItem("Orphan (Child to be sponsored)");
	ui->txtMinimumBidAmount->setValidator( new QIntValidator(0, 100000000, this) );
	ui->txtReserveAmount->setValidator( new QIntValidator(0, 100000000, this) );
	std::string sCPK = DefaultRecAddress("Christian-Public-Key");
	ui->txtOwnerAddress->setText(GUIUtil::TOQS(sCPK));
}

void NFTAddDialog::UpdateDisplay(std::string sAction, uint256 nftHash)
{
	// Action == CREATE or EDIT
	msMode = sAction;
	if (sAction == "CREATE")
	{
		ui->lblAction->setText(GUIUtil::TOQS("Add new NFT"));
	}
	else if (sAction == "EDIT")
	{
		ui->lblAction->setText(GUIUtil::TOQS("Edit NFT"));
		NFT n = GetSpecificNFT(nftHash);
		if (!n.found)
		{
			 //QMessageBox::information(this, "NFT Not Found", "Sorry, we cant find this nft " + nftHash.GetHex());
			 QMessageBox::warning(this, tr("NFT Not Found"), GUIUtil::TOQS("Sorry, we cannot find this nft " + nftHash.GetHex()), QMessageBox::Ok, QMessageBox::Ok);

			 return;
		}
		ui->lblAction->setText(GUIUtil::TOQS("Edit NFT " + nftHash.GetHex()));
		ui->txtName->setText(GUIUtil::TOQS(n.sName));
		ui->txtDescription->setPlainText(GUIUtil::TOQS(n.sDescription));
		ui->txtLoQualityURL->setText(GUIUtil::TOQS(n.sLoQualityURL));
		ui->txtHiQualityURL->setText(GUIUtil::TOQS(n.sHiQualityURL));
		ui->txtReserveAmount->setText(GUIUtil::TOQS(RoundToString((double)n.nReserveAmount/COIN, 2)));
		ui->txtMinimumBidAmount->setText(GUIUtil::TOQS(RoundToString((double)n.nMinimumBidAmount/COIN, 2)));
		ui->chkMarketable->setChecked(n.fMarketable);
		ui->chkDeleted->setChecked(n.fDeleted);
		ui->cmbNFTType->setCurrentIndex(ui->cmbNFTType->findText(GUIUtil::TOQS(n.sType)));

	}
	std::string sInfo = "Note: It costs 100 BBP to create or edit an NFT.";
	ui->txtInfo->setText(GUIUtil::TOQS(sInfo));

}


void NFTAddDialog::setModel(WalletModel *model)
{
    this->model = model;
}

NFTAddDialog::~NFTAddDialog()
{
    delete ui;
}

void NFTAddDialog::clear()
{
    ui->txtName->setText("");
	ui->txtDescription->setPlainText("");
    ui->txtLoQualityURL->setText("");
	ui->txtHiQualityURL->setText("");
	ui->txtMinimumBidAmount->setText("");
	ui->txtReserveAmount->setText("");
	ui->chkDeleted->setChecked(false);
	ui->chkMarketable->setChecked(false);
}

bool AcquireWallet7()
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


void NFTAddDialog::on_btnSubmit_clicked()
{
	NFT n;
	
	n.sName = GUIUtil::FROMQS(ui->txtName->text());
	n.sCPK = GUIUtil::FROMQS(ui->txtOwnerAddress->text());
	n.nMinimumBidAmount = cdbl(GUIUtil::FROMQS(ui->txtMinimumBidAmount->text()), 2) * COIN;
	n.nReserveAmount = cdbl(GUIUtil::FROMQS(ui->txtReserveAmount->text()), 2) * COIN;
	n.sLoQualityURL = GUIUtil::FROMQS(ui->txtLoQualityURL->text());
	n.sHiQualityURL = GUIUtil::FROMQS(ui->txtHiQualityURL->text());
	n.sDescription = GUIUtil::FROMQS(ui->txtDescription->toPlainText());
	n.fMarketable = ui->chkMarketable->checkState();
	n.fDeleted = ui->chkDeleted->checkState();
	std::string sError;
	if (n.sName.length() < 3)
		sError += "NFT Name must be populated. ";
	if (n.sDescription.length() < 5)
		sError += "NFT Description must be populated. ";

	if (!ValidateAddress2(n.sCPK)) 
		sError += "NFT Owner Address is invalid. ";
	if (n.sLoQualityURL.length() < 10 || n.sHiQualityURL.length() < 10) 
		sError += "You must enter an asset URL. ";
	//combobox->itemData(combobox->currentIndex())
	n.sType = GUIUtil::FROMQS(ui->cmbNFTType->currentText());
	if (n.sType.empty()) 
		sError += "NFT Type must be chosen. ";

	if (n.sName.length() > 128)
		sError += "NFT Name must be < 128 chars.";
	if (n.sDescription.length() > 512)
		sError += "NFT Description must be < 512 chars.";
	if (n.sLoQualityURL.length() > 256 || n.sHiQualityURL.length() > 256)
		sError += "URL Length must be < 256 chars.";

	CAmount nBalance = GetRPCBalance();

	if (nBalance < (100*COIN)) 
		sError += "Sorry balance too low to create an NFT. ";

	AcquireWallet7();
	if (pwalletpog->IsLocked())
	{
		sError += "Sorry, wallet must be unlocked.";
	}
    std::string sTXID;   
	bool fCreated = ProcessNFT(n, msMode, n.sCPK, 0, false, sError);
	
	std::string sNarr = (sError.empty()) ? "Successfully " + msMode + "(ed) NFT " + sTXID + ". <br><br>Thank you for using BiblePay Non Fungible Tokens." : sError;

 	QMessageBox::warning(this, tr("NFT Add Result"), GUIUtil::TOQS(sNarr), QMessageBox::Ok, QMessageBox::Ok);
	clear();
}

