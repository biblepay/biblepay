// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_MAILSENDDIALOG_H
#define BITCOIN_QT_MAILSENDDIALOG_H

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
    class MailSendDialog;
}

QT_BEGIN_NAMESPACE
class QModelIndex;
class uint256;
QT_END_NAMESPACE

/** Dialog for Sending a Greeting Card */
class MailSendDialog : public QDialog
{
    Q_OBJECT

public:
    enum ColumnWidths {
        DATE_COLUMN_WIDTH = 130,
        LABEL_COLUMN_WIDTH = 120,
        AMOUNT_MINIMUM_COLUMN_WIDTH = 160,
        MINIMUM_COLUMN_WIDTH = 130
    };

    explicit MailSendDialog(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~MailSendDialog();

    void setModel(WalletModel *model);
	void UpdateDisplay(std::string sAction);

public Q_SLOTS:
    void clear();
	
protected:
   std::string msMode;

private:
    Ui::MailSendDialog *ui;
    GUIUtil::TableViewLastColumnResizingFixer *columnResizingFixer;
    WalletModel *model;
    QMenu *contextMenu;
    const PlatformStyle *platformStyle;

private Q_SLOTS:
    void on_btnSubmit_clicked();
	void on_TypeChanged(int iType);

};

#endif // BITCOIN_QT_MAILSENDDIALOG_H
