// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_USERDIALOG_H
#define BITCOIN_QT_USERDIALOG_H

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
    class UserDialog;
}

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

/** Dialog for Maintaining a User Record */
class UserDialog : public QDialog
{
    Q_OBJECT

public:

    explicit UserDialog(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~UserDialog();

    void setModel(WalletModel *model);
	void UpdateDisplay();
	
public Q_SLOTS:
    void clear();
	
protected:
   

private:
    Ui::UserDialog *ui;
    WalletModel *model;
    QMenu *contextMenu;
    const PlatformStyle *platformStyle;

private Q_SLOTS:
    void on_btnSave_clicked();
};

#endif // BITCOIN_QT_USERDIALOG_H
