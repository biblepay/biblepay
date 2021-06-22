#ifndef GENERICTABLEDIALOG_H
#define GENERICTABLEDIALOG_H

#include <QWidget>
#include <QCoreApplication>
#include <QString>
#include <QDebug>
#include <QVector>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QMenu>
#include <QTableWidget>
#include "rpcpog.h"
#include "walletframe.h"

class OptionsModel;
class PlatformStyle;
class WalletModel;


namespace Ui {
class GenericTableDialog;
}

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE


class GenericTableDialog : public QWidget
{
    Q_OBJECT

public:
    explicit GenericTableDialog(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~GenericTableDialog();
	void setModel(WalletModel *model);
	void UpdateDisplay(std::string);
	WalletFrame *myWalletFrame;


private:
    Ui::GenericTableDialog *ui;
	WalletModel *model;
    DataTable GetDataTable(std::string sType);


private Q_SLOTS:
    void slotNavigateTo();
	void HandleIndicatorChanged(int logicalIndex);
	void cellDoubleClicked(int Y, int X);
	void slotList();
	void slotCustomMenuRequested(QPoint pos);
	void selectTableRow();

};

#endif // GENERICTABLEDIALOG_H
