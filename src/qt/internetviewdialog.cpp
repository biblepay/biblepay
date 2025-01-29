// Copyright (c) 2011-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/internetviewdialog.h>
#include <qt/forms/ui_internetviewdialog.h>

#include <qt/guiutil.h>
#include <qt/transactiontablemodel.h>

#include <QModelIndex>
#include <QSettings>
#include <QString>

InternetViewDialog::InternetViewDialog(const QModelIndex &idx, QWidget *parent) :
    QDialog(parent, GUIUtil::dialog_flags),
    ui(new Ui::InternetViewDialog)
{
    ui->setupUi(this);
    GUIUtil::updateFonts();
    setWindowTitle(tr("Details for The Proof Of Concept"));

    QString desc = "<html><br><table border=\"1\" border-collapse=\"collapse\" border-color=\"red\"><tr><th>Col1<th>Col2</tr><tr><td>Val1<td>Val2</tr></table></html>";
    ui->detailText->setHtml(desc);

    GUIUtil::handleCloseWindowShortcut(this);
}

InternetViewDialog::~InternetViewDialog()
{
    delete ui;
}
