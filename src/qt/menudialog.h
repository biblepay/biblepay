// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_MENUDIALOG_H
#define BITCOIN_QT_MENUDIALOG_H

#include <QDialog>
#include <QObject>
#include "wallet/crypter.h"

class BitcoinGUI;
class ClientModel;

namespace Ui {
    class MenuDialog;
}

/** "BiblePay University" dialog box */
class MenuDialog : public QDialog
{
    Q_OBJECT

public:

    explicit MenuDialog(QWidget *parent);
    ~MenuDialog();


private:
    Ui::MenuDialog *ui;
    QString text;

private Q_SLOTS:
    void on_btnOK_accepted();
	void myLink(QString h);
};



#endif // BITCOIN_QT_MENUDIALOG_H
