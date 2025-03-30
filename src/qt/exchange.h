// Copyright (c) 2016-2023 The BiblePay Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_EXCHANGE_H
#define BITCOIN_QT_EXCHANGE_H

#include <primitives/transaction.h>
#include <sync.h>
#include <util/system.h>

#include <QMenu>
#include <QTimer>
#include <QWidget>


namespace Ui
{
class Exchange;
}


class ClientModel;
class WalletModel;

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

/** Masternode Manager page widget */
class Exchange : public QWidget
{
    Q_OBJECT

public:
    explicit Exchange(QWidget* parent = 0);
    ~Exchange();

    enum {
        COLUMN_PRICE,
        COLUMN_QUANTITY,
        COLUMN_TOTAL,
        COLUMN_ID,
        COLUMN_AGE,
        COLUMN_FLAGS
    };

    void setClientModel(ClientModel* clientModel);
    void setWalletModel(WalletModel* walletModel);

private:
    QMenu* contextMenuDIP3;
 
    QTimer* timer;
    Ui::Exchange* ui;
    ClientModel* clientModel{nullptr};
    WalletModel* walletModel{nullptr};

    // Protects tableWidget
    RecursiveMutex cs_tbl;

    void PerformUpdateTables();
    void updateTables();
    void PopulateRoomBanner();
    void PopulateRoomIcon();

Q_SIGNALS:

private Q_SLOTS:
    void buyClicked();
    void sellClicked();
    void wrapClicked();
    void unwrapClicked();
    void getBalanceClicked();
    void cancelClicked();

    void cmbRoomChanged(int iIndex);

    void EntireRowClickedBuy(const QModelIndex& q);
    void EntireRowClickedSell(const QModelIndex& q);
    void mouseMoveEvent(QMouseEvent* event);
};
#endif // BITCOIN_QT_EXCHANGE_H
