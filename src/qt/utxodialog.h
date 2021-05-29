// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_UTXODIALOG_H
#define BITCOIN_QT_UTXODIALOG_H

#include "guiutil.h"

#include <QDialog>
#include <QHeaderView>
#include <QItemSelection>
#include <QKeyEvent>
#include <QMenu>
#include <QPoint>
#include <QVariant>

class OptionsModel;
class PlatformStyle;
class WalletModel;

namespace Ui {
    class UTXODialog;
}

QT_BEGIN_NAMESPACE
class QModelIndex;
class uint256;
QT_END_NAMESPACE

class UTXODialog : public QDialog
{
    Q_OBJECT

public:

    explicit UTXODialog(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~UTXODialog();
    void setModel(WalletModel *model);
	void UpdateDisplay();

public Q_SLOTS:
    void clear();
	void ChangeCurrency(int i);
	void onAddressChanged();
    void on_btnSubmit_clicked();

private:
    Ui::UTXODialog *ui;
    WalletModel *model;
    QMenu *contextMenu;
    const PlatformStyle *platformStyle;

};

#endif // BITCOIN_QT_UTXODIALOG_H
