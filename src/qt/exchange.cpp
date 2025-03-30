// Copyright (c) 2016-2023 The BiblePay Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/exchange.h>
#include <qt/forms/ui_exchange.h>

#include <qt/clientmodel.h>
#include <clientversion.h>
#include <coins.h>
#include <qt/guiutil.h>
#include <netbase.h>
#include <qt/walletmodel.h>

#include <interfaces/node.h>
#include <rpc/server.h>
#include <rpc/blockchain.h>

#include <univalue.h>
#include <QMessageBox>
#include <QTableWidgetItem>
#include <QInputDialog>
#include <QtGui/QClipboard>
#include "rpcpog.h"

static AtomicSymbol moSymbol;

void Exchange::PopulateRoomBanner()
{
    // Room Name
    std::string PriceBanner = "Price " + moSymbol.Symbol;
    std::string QtyBanner = "Qty BBP";
    ui->tableSell->setHorizontalHeaderItem(0, new QTableWidgetItem(GUIUtil::TOQS(PriceBanner)));
    ui->tableBuy->setHorizontalHeaderItem(0, new QTableWidgetItem(GUIUtil::TOQS(PriceBanner)));
    ui->tableSell->setHorizontalHeaderItem(1, new QTableWidgetItem(GUIUtil::TOQS(QtyBanner)));
    ui->tableBuy->setHorizontalHeaderItem(1, new QTableWidgetItem(GUIUtil::TOQS(QtyBanner)));
    std::string sBBPUSDCaption = "BBP/USD";
    std::string sAltUSDCaption = moSymbol.Symbol + "/USD";
    ui->lblBBPUSDCaption->setText(GUIUtil::TOQS(sBBPUSDCaption));
    ui->lblAltUSDCaption->setText(GUIUtil::TOQS(sAltUSDCaption));
    ui->lblSellBanner->setText("Sells");
    ui->lblBuyBanner->setText("Buys");
    std::string sBanner = moSymbol.LongAssetName + " Trading Room";
    ui->lblRoomNameBanner->setText(GUIUtil::TOQS(sBanner));
    ui->lblBBPALTPrice->setText("...");
    ui->lblALTUSD->setText("...");
    ui->lblBBPUSD->setText("...");
    // clear the alt balance
    ui->lblNativeAltBalance->setText(GUIUtil::TOQS(DoubleToString(0, 6)));
 }

void Exchange::PopulateRoomIcon()
{
    // Icon for Room
    QPixmap emptyPM;
    ui->lblDisplayImage->setPixmap(emptyPM);
    std::string sPath = GetTradingRoomIcon(moSymbol.IconName);
    if (!sPath.empty()) {
        QPixmap pixmap(GUIUtil::TOQS(sPath));
        if (!pixmap.isNull()) {
            ui->lblDisplayImage->setPixmap(pixmap);
            ui->lblDisplayImage->setScaledContents(true);
        }
    }
}

Exchange::Exchange(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::Exchange)
{
    ui->setupUi(this);
    setMouseTracking(true);

    int columnPriceWidth    = 125;
    int columnQuantityWidth = 100;
    int columnTotalWidth    = 100;
    int columnIDWidth       = 140;
    int columnAGEWidth      = 100;
    int columnFLAGSWidth    = 50;

    ui->tableBuy->setColumnWidth(COLUMN_PRICE, columnPriceWidth);
    ui->tableBuy->setColumnWidth(COLUMN_QUANTITY, columnQuantityWidth);
    ui->tableBuy->setColumnWidth(COLUMN_TOTAL, columnTotalWidth);
    ui->tableBuy->setColumnWidth(COLUMN_ID, columnIDWidth);
    ui->tableBuy->setColumnWidth(COLUMN_AGE, columnAGEWidth);
    ui->tableBuy->setColumnWidth(COLUMN_FLAGS, columnFLAGSWidth);

    ui->tableBuy->verticalHeader()->setVisible(false);
    ui->tableSell->verticalHeader()->setVisible(false);

    ui->tableSell->setColumnWidth(COLUMN_PRICE, columnPriceWidth);
    ui->tableSell->setColumnWidth(COLUMN_QUANTITY, columnQuantityWidth);
    ui->tableSell->setColumnWidth(COLUMN_TOTAL, columnTotalWidth);
    ui->tableSell->setColumnWidth(COLUMN_ID, columnIDWidth);
    ui->tableSell->setColumnWidth(COLUMN_AGE, columnAGEWidth);
    ui->tableSell->setColumnWidth(COLUMN_FLAGS, columnFLAGSWidth);

    connect(ui->btnBuy, SIGNAL(clicked()), this, SLOT(buyClicked()));
    connect(ui->btnSell, SIGNAL(clicked()), this, SLOT(sellClicked()));
    connect(ui->btnCancel, SIGNAL(clicked()), this, SLOT(cancelClicked()));
    connect(ui->btnGetBalance, SIGNAL(clicked()), this, SLOT(getBalanceClicked()));
    connect(ui->btnWrap, SIGNAL(clicked()), this, SLOT(wrapClicked()));
    connect(ui->btnUnwrap, SIGNAL(clicked()), this, SLOT(unwrapClicked()));

    connect(ui->tableBuy->selectionModel(), SIGNAL(currentChanged(const QModelIndex&, const QModelIndex&)), this, SLOT(EntireRowClickedBuy(const QModelIndex&)));
    connect(ui->tableSell->selectionModel(), SIGNAL(currentChanged(const QModelIndex&, const QModelIndex&)), this, SLOT(EntireRowClickedSell(const QModelIndex&)));
    // First Default.
    moSymbol = GetAtomicSymbol("DOGE");

    ui->cmbRoom->clear();
    ui->cmbRoom->addItem("BBP/DOGE");
    ui->cmbRoom->addItem("BBP/XLM");

    connect(ui->cmbRoom, SIGNAL(currentIndexChanged(int)), this, SLOT(cmbRoomChanged(int)));

    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &Exchange::PerformUpdateTables);
    timer->start(10000);

    GUIUtil::updateFonts();
    PopulateRoomBanner();
}


static int nWalletStartTime = 0;
static int nRedrawing = 0;
static int nLastClick = 0;
static int nCurrentSelectedRowBuy = 0;
static int nCurrentSelectedRowSell = 0;

void Exchange::mouseMoveEvent(QMouseEvent* event)
{
    nLastClick = GetAdjustedTime();
}

Exchange::~Exchange()
{
    delete ui;
}

void Exchange::setClientModel(ClientModel* model)
{
    this->clientModel = model;
    if (model)
    {
    }
}

void Exchange::setWalletModel(WalletModel* model)
{
    this->walletModel = model;
}

bool ValidateAtomic(AtomicTrade a)
{
    if (a.Quantity <= 0) {
        QMessageBox::information(nullptr, "Error", "The Quantity must be greater than zero.");
        return false;
    }
    if (a.Price <= 0) {
        QMessageBox::information(nullptr, "Error", "The price of alt per BBP must be greater than zero.");
        return false;
    }
    return true;
}

void Exchange::cmbRoomChanged(int iIndex)
{
    std::string sRoomFull = ui->cmbRoom->currentText().toStdString();
    std::string sTicker = GetElement(sRoomFull,"/",1);
    moSymbol = GetAtomicSymbol(sTicker);
    GetOrderBookData(true, sTicker);
    nLastClick = 0;
    PopulateRoomBanner();
    PerformUpdateTables();

    LogPrintf("\r\nEXCHANGE %f %s", iIndex, sTicker);
}

void Exchange::buyClicked()
{
    LOCK(cs_tbl);
    LOCK(cs_main);

    AtomicTrade a;
    const CoreContext& cc = CGlobalNode::GetGlobalCoreContext();
    JSONRPCRequest r0(cc);
    a.Version = 1;
    a.Action = "buy";
    a.SymbolBuy = "bbp";
    a.Time = GetAdjustedTime();
    a.SymbolSell = moSymbol.Symbol;
    boost::to_lower(a.SymbolSell);

    a.Quantity = StringToDouble(ui->txtQuantity->text().toStdString(), 2);
    a.Price = StringToDouble(ui->txtPrice->text().toStdString(), 8); // Price for each BBP in ALT
    a.Status = "open";
    bool fValid = ValidateAtomic(a);
    if (!fValid) return;

    std::string sAltColoredPubKey = IsInAddressBook(r0, moSymbol.AddressBookEntry);
    if (sAltColoredPubKey == "")
    {
        a.Error = "Sorry, the " + moSymbol.Symbol + " asset address has not been mined yet.";
        QMessageBox::information(nullptr, "Error", GUIUtil::TOQS(a.Error));
        return;  
    }
    double nColoredAltBalance = GetAssetBalanceNoWallet(moSymbol.ShortCode);
    double nTotal = a.Price * a.Quantity;
    LogPrintf("\nExchange::BuyClick AssetBal %f  TotalBuy %f", nColoredAltBalance, nTotal);

    if (nTotal > nColoredAltBalance)
    {
        a.Error = "Sorry, your " + moSymbol.Symbol + " colored asset balance is "
            + DoubleToString(nColoredAltBalance, 4)
            + " which is less than the buy amount of "
            + DoubleToString(nTotal, 4) + ".";
        QMessageBox::information(nullptr, "Error", GUIUtil::TOQS(a.Error));
        return;
    }

    a = TransmitAtomicTrade(r0, a, "TransmitAtomicTransactionV2", moSymbol.AddressBookEntry);
    if (a.Error.length() > 1)
    {
        QMessageBox::information(nullptr, "Error", GUIUtil::TOQS(a.Error));
        return;
    }
    if (a.id.length() < 4)
    {
        QMessageBox::information(nullptr, "Error", "Unable to create atomic tx.");
    }
    
    GetOrderBookData(true, moSymbol.Symbol);
    nLastClick = 0;
}


void Exchange::sellClicked()
{
    LOCK(cs_tbl);
    LOCK(cs_main);

    AtomicTrade a;
    const CoreContext& cc = CGlobalNode::GetGlobalCoreContext();
    JSONRPCRequest r0(cc);
    a.Version = 1;
    a.Action = "sell";
    a.Time = GetAdjustedTime();
    a.SymbolBuy = moSymbol.Symbol;
    
    boost::to_lower(a.SymbolBuy);

    a.SymbolSell = "bbp";
    a.Quantity = StringToDouble(ui->txtQuantity->text().toStdString(), 2);
    a.Price = StringToDouble(ui->txtPrice->text().toStdString(), 8); // Price for each BBP in ALT
    a.Status = "open";
    bool fValid = ValidateAtomic(a);
    if (!fValid) return;
    // Verify they have enough non colored BBP to sell
    std::string sMyAddress = GetDefaultReceiveAddress("Trading-Public-Key");
    CAmount nAmt = GetWalletBalanceForSpecificAddress(sMyAddress);
    double nBBPNonColoredBalance = AmountToDouble(nAmt);

    if (a.Quantity < 1000) {
        a.Error = "Sell quantity must be >= 1000.";
        QMessageBox::information(nullptr, "Error", GUIUtil::TOQS(a.Error));
        return;
    }

    if (nBBPNonColoredBalance < a.Quantity)
    {
        a.Error = "Sorry, your BBP Trading-public-key balance is " + DoubleToString(nBBPNonColoredBalance, 4) + " which is less than the sell quantity of "
            + DoubleToString(a.Quantity, 4) + ".";
        QMessageBox::information(nullptr, "Error", GUIUtil::TOQS(a.Error));
        return;
    }

    std::string sAltColoredPubKey = IsInAddressBook(r0, moSymbol.AddressBookEntry);
    if (sAltColoredPubKey == "") {
        a.Error = "Sorry, the " + moSymbol.Symbol + " asset address has not been mined yet.";
        QMessageBox::information(nullptr, "Error", GUIUtil::TOQS(a.Error));
        return;
    }

    a = TransmitAtomicTrade(r0, a, "TransmitAtomicTransactionV2", moSymbol.AddressBookEntry);
    if (a.Error.length() > 1)
    {
        QMessageBox::information(nullptr, "Error", GUIUtil::TOQS(a.Error));
        return;
    }

    if (a.id.length() < 4)
    {
        QMessageBox::information(nullptr, "Error", "Unable to create atomic tx.");
    }
    GetOrderBookData(true, moSymbol.Symbol);
    nLastClick = 0;
}

void Exchange::cancelClicked()
{
    LOCK(cs_tbl);
    LOCK(cs_main);

    const CoreContext& cc = CGlobalNode::GetGlobalCoreContext();
    JSONRPCRequest r0(cc);
    std::string sOrderID = ui->txtID->text().toStdString();
    if (sOrderID.length() < 4)
    {
        QMessageBox::information(nullptr, "Error", "You must enter the full OrderID.");
        return;
    }
    AtomicTrade a;
    a.id = sOrderID;
    a = TransmitAtomicTrade(r0, a, "CancelAtomicTransactionV2", "");
    if (a.Error.length() > 1)
    {
        QMessageBox::information(nullptr, "Error", GUIUtil::TOQS(a.Error));
        return;
    }
    GetOrderBookData(true, moSymbol.Symbol);
    nLastClick = 0;
}

void Exchange::PerformUpdateTables()
{ 
    if (!clientModel || clientModel->node().shutdownRequested())
    {
        return;
    }
    if (nWalletStartTime == 0) nWalletStartTime = GetAdjustedTime();
    int nStartElapsed = GetAdjustedTime() - nWalletStartTime;
    if (nStartElapsed < (1))
    {
        // Prevent race condition on cold boot; allow wallet to warm up first.
        return;
    }

    int nElapsed = GetAdjustedTime() - nLastClick;
    bool bRefresh = false;
    if (nLastClick == 0 || nElapsed < 60 * 5)
        bRefresh = true;

    if (!bRefresh) return;
    if (nLastClick == 0)
    {
        nLastClick = GetAdjustedTime();
    }

    LOCK(cs_tbl);
    int nLastSelRowBuy = nCurrentSelectedRowBuy;
    int nLastSelRowSell = nCurrentSelectedRowSell;
    nRedrawing = 1;

    ui->tableBuy->setSortingEnabled(false);
    ui->tableSell->setSortingEnabled(false);
    ui->tableBuy->clearContents();
    ui->tableSell->clearContents();

    ui->tableBuy->setRowCount(0);
    ui->tableSell->setRowCount(0);
    //Price,Qty,Total

    std::vector<std::pair<std::string, AtomicTrade>> orderBookBuy = GetSortedOrderBook("buy", "open", moSymbol.Symbol);
    std::vector<std::pair<std::string, AtomicTrade>> orderBookSell = GetSortedOrderBook("sell", "open", moSymbol.Symbol);
    std::map<std::string, AtomicTrade> mapAT = GetOrderBookData(false, moSymbol.Symbol);
    std::string sMyAddress = GetDefaultReceiveAddress("Trading-Public-Key");

    double nTotalALT = 0;
    int iRow = 0;
    for (auto ii : orderBookBuy)
    {
        AtomicTrade a = ii.second;
        double nRowAmount = a.Quantity * a.Price;
        nTotalALT += nRowAmount;
        std::string sFlags = GetFlags(a, sMyAddress, mapAT);
        ui->tableBuy->insertRow(iRow);
        ui->tableBuy->setItem(iRow, 0, new QTableWidgetItem(GUIUtil::TOQS(DoubleToString(a.Price,8))));
        ui->tableBuy->setItem(iRow, 1, new QTableWidgetItem(GUIUtil::TOQS(DoubleToStringWithLeadingZeroes(a.Quantity, 0, 8))));
        ui->tableBuy->setItem(iRow, 2, new QTableWidgetItem(GUIUtil::TOQS(DoubleToStringWithLeadingZeroes(nRowAmount, 4, 12))));
        ui->tableBuy->setItem(iRow, 3, new QTableWidgetItem(GUIUtil::TOQS(a.id)));
        ui->tableBuy->setItem(iRow, 4, new QTableWidgetItem(GUIUtil::TOQS(DoubleToStringWithLeadingZeroes(StringToDouble(GetDisplayAgeInDays(a.Time), 0), 0, 3))));
        ui->tableBuy->setItem(iRow, 5, new QTableWidgetItem(GUIUtil::TOQS(sFlags)));
        iRow++;
    }

    iRow = 0;
    for (auto ii : orderBookSell)
    {
        AtomicTrade a = ii.second;
        double nRowAmount = a.Quantity * a.Price;
        nTotalALT += nRowAmount;
        std::string sFlags = GetFlags(a, sMyAddress, mapAT);
        ui->tableSell->insertRow(iRow);
        ui->tableSell->setItem(iRow, 0, new QTableWidgetItem(GUIUtil::TOQS(DoubleToString(a.Price, 8))));
        ui->tableSell->setItem(iRow, 1, new QTableWidgetItem(GUIUtil::TOQS(DoubleToStringWithLeadingZeroes(a.Quantity, 0, 8))));
        ui->tableSell->setItem(iRow, 2, new QTableWidgetItem(GUIUtil::TOQS(DoubleToStringWithLeadingZeroes(nRowAmount, 4, 12))));
        ui->tableSell->setItem(iRow, 3, new QTableWidgetItem(GUIUtil::TOQS(a.id)));
        ui->tableSell->setItem(iRow, 4, new QTableWidgetItem(GUIUtil::TOQS(DoubleToStringWithLeadingZeroes(StringToDouble(GetDisplayAgeInDays(a.Time), 0), 0, 3))));
        ui->tableSell->setItem(iRow, 5, new QTableWidgetItem(GUIUtil::TOQS(sFlags)));
        iRow++;
    }
    // Put the selected row back so the user does not see the "flash"
    ui->tableBuy->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableSell->setSelectionBehavior(QAbstractItemView::SelectRows);
    if (nLastSelRowBuy  >= 0)     nCurrentSelectedRowBuy = nLastSelRowBuy;
    if (nLastSelRowSell >= 0)     nCurrentSelectedRowSell = nLastSelRowSell;

    if (ui->tableBuy->rowCount() > nCurrentSelectedRowBuy && nCurrentSelectedRowBuy >= 0)
    {
         ui->tableBuy->selectRow(nCurrentSelectedRowBuy);
    }
    if (ui->tableSell->rowCount() > nCurrentSelectedRowSell && nCurrentSelectedRowSell >= 0)
    {
         ui->tableSell->selectRow(nCurrentSelectedRowSell);
    }
    // Populate Price Feeds
    ui->lblBBPALTPrice->setText(GUIUtil::TOQS(mapAT["BBP" + moSymbol.Symbol].Message));
    ui->lblALTUSD->setText(GUIUtil::TOQS(mapAT[moSymbol.Symbol + "USD"].Message));
    ui->lblBBPUSD->setText(GUIUtil::TOQS(mapAT["BBPUSD"].Message));

    // Populate Ease-of-Use Wallet Balances

    const CoreContext& cc = CGlobalNode::GetGlobalCoreContext();
    JSONRPCRequest r0(cc);
    std::string sTradingPublicKey = DefaultRecAddress(r0, "Trading-Public-Key");
    std::string sAltColoredPubKey = IsInAddressBook(r0, moSymbol.AddressBookEntry);

    CWallet* pwallet = GetInternalWallet(r0);
    if (EnsureWalletIsAvailable(pwallet, false))
    {
         if (!pwallet->IsLocked())
         {
             ui->txtColoredAltAddress->setText(GUIUtil::TOQS(sAltColoredPubKey));
             std::string sNativeAltAddress = GetAltPublicKey(moSymbol.Symbol);
             ui->txtNativeAltAddress->setText(GUIUtil::TOQS(sNativeAltAddress));
             ui->txtBBPTradingAddress->setText(GUIUtil::TOQS(sTradingPublicKey));
             std::string sCAABanner = "Colored " + moSymbol.Symbol + " Address";
             ui->lblColoredAltAddress->setText(GUIUtil::TOQS(sCAABanner));
             std::string sNAABanner = "Native " + moSymbol.Symbol + " Address";
             ui->lblNativeAltAddress->setText(GUIUtil::TOQS(sNAABanner));

             double nColoredAltBalance = GetAssetBalanceNoWallet(moSymbol.ShortCode);
             ui->lblColoredAltBalance->setText(GUIUtil::TOQS(DoubleToString(nColoredAltBalance, 6)));

             double nBBPBalance = GetAssetBalance(r0, sTradingPublicKey);
             ui->lblBBPAssetBalance->setText(GUIUtil::TOQS(DoubleToString(nBBPBalance, 6)));
             //LogPrintf("\nWalletBalance COLORED %f, BBP %f", nColoredBalance, nBBPBalance);
         }
    }
    PopulateRoomIcon();

    nRedrawing = 0;
}

void Exchange::getBalanceClicked()
{
    std::string sNativeAltAddress = ui->txtNativeAltAddress->text().toStdString();
    double nNativeAltBalance = GetAltBalance(moSymbol.Symbol, sNativeAltAddress);
    ui->lblNativeAltBalance->setText(GUIUtil::TOQS(DoubleToString(nNativeAltBalance, 6)));
}

void Exchange::wrapClicked()
{
    QString sQty = QInputDialog::getText(nullptr, "Wrap Native Asset to Colored Asset", "How much would you like to wrap?");
    double nQuantity = StringToDouble(sQty.toStdString(), 4);
    if (nQuantity <= 0)
    {
         return;
    }
 
    AtomicTrade a = WrapCoin(moSymbol, nQuantity);

    if (a.Error.empty())
    {
         std::string sReport = "<html><br>ID: " + a.id + "<br>Tx:" + a.ToString() + "</html>";
         QMessageBox::information(nullptr, "Successful Wrap", GUIUtil::TOQS(sReport));
    }
    else
    {
         QMessageBox::information(nullptr, "Error", GUIUtil::TOQS(a.Error));
    }
}

void Exchange::unwrapClicked()
{
    QString sQty = QInputDialog::getText(nullptr, "Unwrap Colored Asset to Native Asset", "How much would you like to unwrap?");
    double nQuantity = StringToDouble(sQty.toStdString(), 4);
    if (nQuantity <= 0) {
         return;
    }
 
    AtomicTrade a = UnwrapCoin(moSymbol, nQuantity);
    if (a.Error.empty())
    {
         std::string sReport = "<html><br>ID: " + a.id + "<br>Tx:" + a.ToString() + "</html>";
         QMessageBox::information(nullptr, "Successful Unwrap", GUIUtil::TOQS(sReport));
    }
    else
    {
         QMessageBox::information(nullptr, "Error", GUIUtil::TOQS(a.Error));
    }
}

void Exchange::EntireRowClickedBuy(const QModelIndex& qmodelindex)
{
    LOCK(cs_tbl);
    int nSelRow = qmodelindex.row();
    if (nSelRow < 0 || nSelRow > ui->tableBuy->rowCount()-1) return;
    if (nRedrawing == 1) return;

    ui->txtPrice->setText(ui->tableBuy->item(nSelRow, COLUMN_PRICE)->text().trimmed());
    ui->txtQuantity->setText(ui->tableBuy->item(nSelRow, COLUMN_QUANTITY)->text().trimmed());
    ui->txtID->setText(ui->tableBuy->item(nSelRow, COLUMN_ID)->text().trimmed());
    nLastClick = GetAdjustedTime();
    nCurrentSelectedRowBuy = nSelRow;
    return;
}


void Exchange::EntireRowClickedSell(const QModelIndex& qmodelindex)
{
    LOCK(cs_tbl);
    nLastClick = GetAdjustedTime();

    int nSelRow = qmodelindex.row();
    if (nSelRow < 0 || nSelRow > ui->tableSell->rowCount()-1) return;
    if (nRedrawing == 1) return;

    ui->txtPrice->setText(ui->tableSell->item(nSelRow, COLUMN_PRICE)->text().trimmed());
    ui->txtQuantity->setText(ui->tableSell->item(nSelRow, COLUMN_QUANTITY)->text().trimmed());
    ui->txtID->setText(ui->tableSell->item(nSelRow, COLUMN_ID)->text().trimmed());
    nCurrentSelectedRowSell = nSelRow;
    return;
}

