#include "generictabledialog.h"
#include "bitcoinunits.h"
#include "forms/ui_generictable.h"
#include "walletmodel.h"
#include "guiutil.h"
#include "rpcpog.h"
#include <QPainter>
#include <QTableWidget>
#include <QGridLayout>
#include <QUrl>
#include <QTimer>
#include <univalue.h>
#include <boost/algorithm/string/case_conv.hpp> // for to_lower()


GenericTableDialog::GenericTableDialog(const PlatformStyle *platformStyle, QWidget *parent) : ui(new Ui::GenericTableDialog)
{
    ui->setupUi(this);
	connect(ui->table1, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(slotCustomMenuRequested(QPoint)));
	connect(ui->table1->horizontalHeader(), SIGNAL(sectionPressed(int)),this, SLOT(HandleIndicatorChanged(int)));
	connect(ui->table1, &QTableWidget::cellDoubleClicked, this, &GenericTableDialog::cellDoubleClicked);
	connect(ui->table1, SIGNAL(cellActivated(int,int)), this, SLOT(selectTableRow()));
}

GenericTableDialog::~GenericTableDialog()
{
    delete ui;
}

void GenericTableDialog::setModel(WalletModel *model)
{
    this->model = model;
}


/* Data Query Models */
DataTable GenericTableDialog::GetDataTable(std::string sType)
{
	DataTable d;
	if (sType == "nft")
	{
		d.TableDescription = "My NFTs (Double click to Edit)";
		// Row 0 is always the header
		d.Set(0,0,"ID");
		d.Set(0,1,"Name");
		d.Set(0,2,"Description");
		d.Set(0,3,"URL");
		d.Set(0,4,"Min_Amount");
		d.Set(0,5,"Marketable?");


		d.Cols = 5;
		std::string sCPK = DefaultRecAddress("Christian-Public-Key"); 
		std::vector<NFT> uNFTs = GetNFTs(false);
		for (int i = 0; i < uNFTs.size(); i++)
		{
			NFT n = uNFTs[i];
			if (n.found && sCPK == n.sCPK && !n.fDeleted)
			{
				d.Rows++;

				d.Set(d.Rows,0, n.GetHash().GetHex());
				d.Set(d.Rows,1, n.sName);
				d.Set(d.Rows,2, n.sDescription);
				d.Set(d.Rows,3, n.sLoQualityURL);
				d.Set(d.Rows,4, RoundToString((double)n.nMinimumBidAmount/COIN,2));
				d.Set(d.Rows,5, ToYesNo(n.fMarketable));
			}
		}
		ui->btn1->setVisible(false);
		ui->lblTableName->setText(GUIUtil::TOQS(d.TableDescription));

	}
	return d;
}


void GenericTableDialog::UpdateDisplay(std::string sType)
{
	DataTable d = GetDataTable(sType);
    ui->table1->setShowGrid(true);
	ui->table1->setRowCount(0);
	ui->table1->setSortingEnabled(false);
    ui->table1->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->table1->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->table1->horizontalHeader()->setStretchLastSection(true);

    ui->table1->setRowCount(d.Rows+1);
    ui->table1->setColumnCount(d.Cols+1);
	// headers
	QStringList pHeaders;
	for (int i = 0; i <= d.Cols; i++)
	{
		pHeaders << GUIUtil::TOQS(d.Get(0,i));
	}
	ui->table1->setHorizontalHeaderLabels(pHeaders);
	ui->table1->setSortingEnabled(true);
	// Data

    for (int r = 1; r <= d.Rows; r++)
	{
	    for(int c = 0; c <= d.Cols; c++)
		{
			QTableWidgetItem* q = new QTableWidgetItem();
			bool bNumeric = false;
			/*
			if (bNumeric) 
			{
				double theValue = cdbl(GUIUtil::FROMQS(pMatrix[i][j]), 2);
				q->setData(Qt::DisplayRole, theValue);
           }
		   {
				q->setData(Qt::EditRole, "testhello"); 
            }
			
		   */
			q->setData(Qt::DisplayRole, GUIUtil::TOQS(d.Get(r,c)));
        	ui->table1->setItem(r - 1, c, q);
		}
	}
	
    ui->table1->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->table1->resizeRowsToContents();
    ui->table1->resizeColumnsToContents();
    ui->table1->setContextMenuPolicy(Qt::CustomContextMenu);

	// Column widths should be set 
	for (int c = 0; c <= d.Cols; c++)
	{
		ui->table1->setColumnWidth(c, (ui->table1->columnWidth(c)/2) + 15);
	}
	ui->table1->setColumnWidth(0,100); // Truncate ID column
	ui->table1->setColumnWidth(2,200); // Description is too wide

}

void GenericTableDialog::cellDoubleClicked(int Y, int X)
{
	QTableWidgetItem *item1(ui->table1->item(Y, 0));
	if (item1)
	{
		uint256 hash = uint256S("0x" + GUIUtil::FROMQS(item1->text()));
	//	LogPrintf("\nEncountered %s ", sHash);
		if (myWalletFrame != nullptr)
			myWalletFrame->gotoNFTAddPage("EDIT", hash);
		
	}
}

void GenericTableDialog::HandleIndicatorChanged(int logicalIndex)
{
	if (logicalIndex != 0 && logicalIndex != 1)
	{
		ui->table1->horizontalHeader()->setSortIndicatorShown(true);
		Qt::SortOrder soCurrentOrder = ui->table1->horizontalHeader()->sortIndicatorOrder();
		ui->table1->sortByColumn(logicalIndex, soCurrentOrder);
	}
}

void GenericTableDialog::slotCustomMenuRequested(QPoint pos)
{
    return;
    /* Create an object context menu 
    QMenu * menu = new QMenu(this);
    //  Create, Connect and Set the actions to the menu
    menu->addAction(tr("Navigate To"), this, SLOT(slotNavigateTo()));
	menu->addAction(tr("List"), this, SLOT(slotList()));
	menu->popup(ui->get->viewport()->mapToGlobal(pos));
	*/
}


void GenericTableDialog::slotList()
{
    int row = ui->table1->selectionModel()->currentIndex().row();
    if(row >= 0)
    {
        std::string sID = GUIUtil::FROMQS(ui->table1->item(row, 0)->text()); 
    }
}


void GenericTableDialog::slotNavigateTo()
{
    int row = ui->table1->selectionModel()->currentIndex().row();
    if(row >= 0)
    {
    }
}

void GenericTableDialog::selectTableRow()
{
   int row = ui->table1->selectionModel()->currentIndex().row();
   if(row >= 0)
   {
		LogPrintf("\nRow%f", row);
   }
}
