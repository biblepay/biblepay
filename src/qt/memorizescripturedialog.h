// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_MEMORIZESCRIPTUREDIALOG_H
#define BITCOIN_QT_MEMORIZESCRIPTUREDIALOG_H

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
    class MemorizeScriptureDialog;
}

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

/** Dialog for BiblePay Memorize Scripture Tool */
class MemorizeScriptureDialog : public QDialog
{
    Q_OBJECT

public:

    explicit MemorizeScriptureDialog(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~MemorizeScriptureDialog();

    void setModel(WalletModel *model);
	void UpdateDisplay();

public Q_SLOTS:
    void clear();
	
protected:
   double WordComparer(std::string Verse, std::string UserEntry);
   double Grade();

private:
    Ui::MemorizeScriptureDialog *ui;
    WalletModel *model;
    QMenu *contextMenu;
    const PlatformStyle *platformStyle;
	void PopulateNewVerse();
	void ShowResults();
	void Score();

private Q_SLOTS:
    void on_btnDone_clicked();
	void on_btnNextScripture_clicked();
	void on_btnMode_clicked();
};

#endif // BITCOIN_QT_MEMORIZESCRIPTUREDIALOG_H
