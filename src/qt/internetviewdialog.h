// Copyright (c) 2011-2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_INTERNETVIEWDIALOG_H
#define BITCOIN_QT_INTERNETVIEWDIALOG_H

#include <QDialog>

namespace Ui {
    class InternetViewDialog;
}

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

/** Dialog showing transaction details. */
class InternetViewDialog : public QDialog
{
    Q_OBJECT

public:
    explicit InternetViewDialog(const QModelIndex &idx, QWidget *parent = nullptr);
    ~InternetViewDialog();

private:
    Ui::InternetViewDialog *ui;
};

#endif // BITCOIN_QT_INTERNETVIEWDIALOG_H
