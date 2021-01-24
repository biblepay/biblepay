// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "memorizescripturedialog.h"
#include "ui_memorizescripturedialog.h"
#include "kjv.h"

#include "guiutil.h"
#include "util.h"
#include "optionsmodel.h"
#include "timedata.h"
#include "platformstyle.h"

#include "walletmodel.h"
#include "validation.h"
#include "rpcpodc.h"
#include "rpcpog.h"
#include <QAction>
#include <QCursor>
#include <QItemSelection>
#include <QMessageBox>
#include <QScrollBar>
#include <QTextDocument>
#include <boost/algorithm/string/case_conv.hpp> // for to_lower()
double nTestingQuestionsTaken = 0;
double nTestingScore = 0;
double nTrainingQuestionsTaken = 0;
double nTrainingScore = 0;
bool fMode = 0;

MemorizeScriptureDialog::MemorizeScriptureDialog(const PlatformStyle *platformStyle, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::MemorizeScriptureDialog),
    model(0),
    platformStyle(platformStyle)
{
    ui->setupUi(this);
    QString theme = GUIUtil::getThemeName();
}

// In scripture memorizer, there are two modes:  Learn, and Quiz.  In learn mode, the user freely types in and recites the verse and clicks next.
// In Quiz mode, we show the verse and ask the user for the book name, chapter and verse.
// We tally the results and show a dialog with the appropriate test results (grade).
int nCorrectChapter = 0;
int nCorrectVerse = 0;
std::string sCorrectBook;

void MemorizeScriptureDialog::PopulateNewVerse()
{
	std::vector<DACResult> d = GetDataListVector("memorizer");
	int i = rand() % d.size();

	std::string sBook = GetElement(d[i].PrimaryKey, "|", 0);
	int iChapter = cdbl(GetElement(d[i].PrimaryKey, "|", 1), 0);
	int iVerse = cdbl(GetElement(d[i].PrimaryKey, "|", 2), 0);
	int iStart = 0;
	int iEnd = 0;
	GetBookStartEnd(sBook, iStart, iEnd);
	std::string sVerse = GetVerseML("EN", sBook, iChapter, iVerse, iStart - 1, iEnd);
	clear();
	ui->txtChapter->setText(GUIUtil::TOQS(RoundToString(iChapter, 0)));
	ui->txtVerse->setText(GUIUtil::TOQS(RoundToString(iVerse, 0)));
	std::string sLocalBook = GetBookByName(sBook);
	if (sLocalBook.empty())
		sLocalBook = sBook;
	sCorrectBook = sLocalBook;
	nCorrectVerse = iVerse;
	nCorrectChapter = iChapter;
	ui->txtBook->setText(GUIUtil::TOQS(sLocalBook));
	ui->txtScripture->setPlainText(GUIUtil::TOQS(sVerse));
	// Set read only

	ui->txtChapter->setReadOnly(!fMode);
	ui->txtBook->setReadOnly(!fMode);
	ui->txtVerse->setReadOnly(!fMode);

	if (fMode == 1)
	{
		// In test mode we need to clear the fields
		ui->txtChapter->setText("");
		ui->txtVerse->setText("");
		ui->txtBook->setText("");
		ui->txtBook->setFocus();
	}
	else
	{
		ui->txtPractice->setFocus();
	}

	std::string sCaption = fMode == 0  ?  "Switch to TEST Mode" : "Switch to TRAIN Mode";
	ui->btnMode->setText(GUIUtil::TOQS(sCaption));

}

double MemorizeScriptureDialog::WordComparer(std::string Verse, std::string UserEntry)
{
	boost::to_upper(Verse);
	boost::to_upper(UserEntry);
	std::vector<std::string> vVerse = Split(Verse.c_str(), " ");
	std::vector<std::string> vUserEntry = Split(UserEntry.c_str(), " ");
	double dTotal = vVerse.size();
	double dCorrect = 0;
	for (int i = 0; i < vVerse.size(); i++)
	{
		bool f = Contains(UserEntry, vVerse[i]);
		if (f) 
			dCorrect++;
	}
	double dPct = dCorrect / (dTotal+.01);
	return dPct;
}

void MemorizeScriptureDialog::ShowResults()
{
	std::string sTitle = "Results";
	double nTrainingPct = nTrainingScore / (nTrainingQuestionsTaken+.01);
	double nTestingPct = nTestingScore / (nTestingQuestionsTaken + .01);

	std::string sSummary = "Congratulations!";
	if (nTrainingQuestionsTaken > 0)
	{
		sSummary += "<br>In training mode you worked through " + RoundToString(nTrainingQuestionsTaken, 0) + ", and your score is " + RoundToString(nTrainingPct*100, 2) + "%!  ";
	}
	
	if (nTestingQuestionsTaken > 0)
	{
		sSummary += "<br>In testing mode you worked through " + RoundToString(nTestingQuestionsTaken, 0) + ", and your score is " + RoundToString(nTestingPct*100, 2) + "%! ";
	}
	sSummary += "<br>Please come back and see us again. ";
	// Clear the results so they can start again if they want:
	nTrainingQuestionsTaken = 0;
	nTestingQuestionsTaken = 0;
	nTrainingScore = 0;
	nTestingScore = 0;

	QMessageBox::information(this, GUIUtil::TOQS(sTitle), GUIUtil::TOQS(sSummary), QMessageBox::Ok, QMessageBox::Ok);
}

void MemorizeScriptureDialog::UpdateDisplay()
{
	// Load initial values
	std::string sCPK = DefaultRecAddress("Christian-Public-Key");
	UserRecord r = GetUserRecord(sCPK);
	std::string sMode = fMode==0 ? "<font color=red>LEARNING MODE</font>" : "<font color=red>TESTING MODE</font>";

	std::string sInfo = sMode + "<br><br>Welcome to the Scripture Memorizer, " + r.NickName + " " + sCPK + "!";

	ui->txtInfo->setText(GUIUtil::TOQS(sInfo));
	// Find the first verse to do the initial population.
	PopulateNewVerse();

}

void MemorizeScriptureDialog::setModel(WalletModel *model)
{
    this->model = model;

    if(model && model->getOptionsModel())
    {
		UpdateDisplay();
    }
}

MemorizeScriptureDialog::~MemorizeScriptureDialog()
{
    delete ui;
}

void MemorizeScriptureDialog::clear()
{
	ui->txtScripture->setPlainText("");
	ui->txtPractice->setPlainText("");
	ui->txtVerse->setText("");
	ui->txtChapter->setText("");
	ui->txtBook->setText("");
}


void MemorizeScriptureDialog::on_btnDone_clicked()
{
    if(!model || !model->getOptionsModel())
        return;
	Score();
	ShowResults();
	// go to the wallet overview page here

}

double MemorizeScriptureDialog::Grade()
{
	std::string sUserBook = GUIUtil::FROMQS(ui->txtBook->text());
	int iChapter = cdbl(GUIUtil::FROMQS(ui->txtChapter->text()), 0);
	int iVerse = cdbl(GUIUtil::FROMQS(ui->txtVerse->text()), 0);
	boost::to_upper(sUserBook);
	boost::to_upper(sCorrectBook);
	double nResult = 0;
	if (sUserBook == sCorrectBook)
		nResult += .3333;
	if (nCorrectChapter == iChapter)
		nResult += .3333;
	
	if (nCorrectVerse == iVerse)
		nResult += .3334;
	return nResult;
}

void MemorizeScriptureDialog::Score()
{
	if (fMode == 0)
	{
		double nPct = WordComparer(GUIUtil::FROMQS(ui->txtScripture->toPlainText()), GUIUtil::FROMQS(ui->txtPractice->toPlainText()));
		nTrainingQuestionsTaken++;
		nTrainingScore += nPct;
	}
	// Score the current Testing session
	if (fMode == 1)
	{
		nTestingQuestionsTaken++;
		double nTestPct = Grade();
		nTestingScore += nTestPct;
	}
}

void MemorizeScriptureDialog::on_btnNextScripture_clicked()
{
    if(!model || !model->getOptionsModel())
        return;
	Score();
	PopulateNewVerse();
}

void MemorizeScriptureDialog::on_btnMode_clicked()
{
   if(!model || !model->getOptionsModel())
        return;
   fMode = !fMode;
   UpdateDisplay();
}