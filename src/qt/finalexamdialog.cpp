// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "finalexamdialog.h"
#include "ui_finalexamdialog.h"
#include "kjv.h"

#include "guiutil.h"
#include "util.h"
#include "optionsmodel.h"
#include "timedata.h"
#include "platformstyle.h"

#include "walletmodel.h"
#include "validation.h"
#include "rpcpog.h"
#include <QAction>
#include <QCursor>
#include <QItemSelection>
#include <QMessageBox>
#include <QScrollBar>
#include <QTimer>

#include <QTextDocument>
#include <boost/algorithm/string/case_conv.hpp> // for to_lower()
#include <boost/algorithm/string.hpp> // for trim()

FinalExamDialog::FinalExamDialog(QWidget *parent, std::string sFinalExam) :
    QDialog(parent),
    ui(new Ui::FinalExamDialog)
{
    ui->setupUi(this);
	if (sFinalExam.empty())
		return;
    QString theme = GUIUtil::getThemeName();
	// Pull in the final exam
	DACResult b = DSQL_ReadOnlyQuery("Univ/" + sFinalExam);
	
	std::string sAnswerKey = ExtractXML(b.Response, "<KEY>", "</KEY>");
	if (sAnswerKey.empty())
	{
		LogPrintf("\nAnswerKey Missing %f", 8054);
		this->close();
		return;
	}
	vecAnswerKey = Split(sAnswerKey.c_str(), ",");
	std::string sQ = ExtractXML(b.Response, "<QUESTIONS>", "</QUESTIONS>");
	std::string sCourse = "Final Exam: " + ExtractXML(b.Response, "<COURSE>", "</COURSE>");
	setWindowTitle(QString::fromStdString(sCourse));
	vecQ.clear();
	vecA.clear();
	mapChosen.clear();
	nCurrentQuestion = 0;
	std::vector<std::string> vQ = Split(sQ.c_str(), "<QUESTIONRECORD>");
	for (int i = 0; i < (int)vQ.size(); i++)
	{
		std::string sQ1 = ExtractXML(vQ[i], "<Q>", "</Q>");
		std::string sA1 = ExtractXML(vQ[i], "<A>", "</A>");
		if (!sQ1.empty() && !sA1.empty())
		{
			vecQ.push_back(sQ1);
			vecA.push_back(sA1);
		}
	}
	ui->lcdSecs->display(0);
	ui->lcdMins->display(0);

	UpdateDisplay();
	fTesting = true;
	ClockTick();
}

void FinalExamDialog::ClockTick()
{
	if (fTesting)
	{
		ui->lcdSecs->display(ui->lcdSecs->intValue() + 1);
		if (ui->lcdSecs->intValue() > 59)
		{
			ui->lcdSecs->display(0);
			ui->lcdMins->display(ui->lcdMins->intValue() + 1);
		}
		QTimer::singleShot(1000, this, SLOT(ClockTick()));
	}
}

// There are two modes:  Learn, and Test.  In learn mode, the user can see the final exam answers.  In TEST mode, the user is tested.
// We tally the results and show a dialog with the appropriate test results (grade).

std::string FinalExamDialog::ExtractAnswer(std::string sLetter)
{
	std::vector<std::string> vAnswers = Split(vecA[nCurrentQuestion].c_str(), "|");
    
	if (sLetter == "A")
	{
		return vAnswers[0];
	}
	else if (sLetter == "B")
	{
		return vAnswers[1];
	}
	else if (sLetter == "C")
	{
		return vAnswers[2];
	}
	else if (sLetter == "D")
	{
		return vAnswers[3];
	}
	return "N/A";
}

void FinalExamDialog::ResetRadios()
{
	ui->radioA->setAutoExclusive(false);
	ui->radioB->setAutoExclusive(false);
	ui->radioC->setAutoExclusive(false);
	ui->radioD->setAutoExclusive(false);
	ui->radioA->setChecked(false);
	ui->radioB->setChecked(false);
	ui->radioC->setChecked(false);
	ui->radioD->setChecked(false);
	ui->radioA->setAutoExclusive(true);
	ui->radioB->setAutoExclusive(true);
	ui->radioC->setAutoExclusive(true);
	ui->radioD->setAutoExclusive(true);
}

void FinalExamDialog::StripNumber()
{
	std::string sSource = vecQ[nCurrentQuestion];
	int pos = sSource.find(".");
	std::string sPrefix = sSource.substr(0, pos + 1);
	std::string sMyQ = strReplace(sSource, sPrefix, "");
	boost::trim(sMyQ);
	int nQN = cdbl(sPrefix + "0", 0);
	ui->txtQuestionNbr->setText(GUIUtil::TOQS(RoundToString(nQN, 0)));
	ui->txtQuestion->setPlainText(GUIUtil::TOQS(sMyQ));
}

std::string ScanAnswerForPopUpVerses(std::string sRefText)
{
	std::vector<std::string> vSourceScripture = Split(sRefText.c_str(), " ");
	std::string sExpandedAnswer;
	for (int i = 0; i < (int)vSourceScripture.size()-1; i++)
	{
		std::string sScrip = vSourceScripture[i] + " " + vSourceScripture[i + 1];
		std::string sExpandedVerses = GetPopUpVerses(sScrip);
		if (!sExpandedVerses.empty())
		{
			sExpandedAnswer += sExpandedVerses + "\r\n\r\n";
		}
	}
	return sExpandedAnswer;
}

void FinalExamDialog::PopulateQuestion()
{
	if (nCurrentQuestion > vecA.size())
	{
		LogPrintf("\nFinalExam::Error Size too small %f ", vecA.size());
		return;
	}

	std::vector<std::string> vAnswers = Split(vecA[nCurrentQuestion].c_str(), "|");
    if (vAnswers.size() > 3)
    {
		ui->radioA->setText(GUIUtil::TOQS(vAnswers[0]));
		ui->radioB->setText(GUIUtil::TOQS(vAnswers[1]));
		ui->radioC->setText(GUIUtil::TOQS(vAnswers[2]));
		ui->radioD->setText(GUIUtil::TOQS(vAnswers[3]));
		ui->radioC->setVisible(true);
		ui->radioD->setVisible(true);
    }
	else if (vAnswers.size() > 1)
	{
		ui->radioA->setText(GUIUtil::TOQS(vAnswers[0]));
		ui->radioB->setText(GUIUtil::TOQS(vAnswers[1]));
		ui->radioC->setText(GUIUtil::TOQS(""));
		ui->radioD->setText(GUIUtil::TOQS(""));
		ui->radioC->setVisible(false);
		ui->radioD->setVisible(false);
	}

	ResetRadios();

	std::string sChosen = mapChosen[nCurrentQuestion];
	if (sChosen == "A")
	{
		ui->radioA->setChecked(true);
	}
	else if (sChosen == "B")
	{
		ui->radioB->setChecked(true);
	}
	else if (sChosen == "C")
	{
		ui->radioC->setChecked(true);
	}
	else if (sChosen == "D")
	{
		ui->radioD->setChecked(true);
	}

	StripNumber();

	if (fMode == 1)
	{
		// TEST mode
		ui->txtAnswer->setPlainText("");
	}
	else
	{
		// Training Mode
		// In Training Mode, if we have any bible verses in the answer, lets include the actual scripture to help the student:
		std::string sExpandedAnswer = ExtractAnswer(vecAnswerKey[nCurrentQuestion]);
		std::string sRefText = sExpandedAnswer + " " + vecQ[nCurrentQuestion];
		std::string sBiblicalRefs = ScanAnswerForPopUpVerses(sRefText);
		sExpandedAnswer += "\r\n\r\n" + sBiblicalRefs;
		ui->txtAnswer->setPlainText(GUIUtil::TOQS(sExpandedAnswer));
	}

	std::string sCaption = fMode == 0  ?  "Switch to TEST Mode" : "Switch to REVIEW Mode";
	ui->btnMode->setText(GUIUtil::TOQS(sCaption));
}

double FinalExamDialog::CalculateScores()
{
	double nTotalCorrect = 0;
	double nTaken = 0;
	for (int i = 0; i < vecAnswerKey.size(); i++)
	{
		std::string sChosen = mapChosen[i];
		double nCorrect = vecAnswerKey[i] == sChosen ? 1 : 0;
		nTotalCorrect += nCorrect;
		if (!sChosen.empty())
		{
			nTaken++;
		}

	}
	nTestingQuestionsTaken = nTaken;
	double nScore = nTotalCorrect / (vecAnswerKey.size() + .001);
	return nScore;
}

void FinalExamDialog::ShowResults()
{
	std::string sTitle = "Results";
	double nTestingPct = CalculateScores();
	
	std::string sSummary = "Congratulations!";
	sSummary += "<br>You worked through " + RoundToString(nTestingQuestionsTaken, 0) + ", and your score is " + RoundToString(nTestingPct*100, 2) + "%! ";
	sSummary += "<br>Please come back and see us again. ";
	// Clear the results so they can start again if they want:
	nTrainingQuestionsTaken = 0;
	nTestingQuestionsTaken = 0;
	nTrainingScore = 0;
	nTestingScore = 0;

	QMessageBox::information(this, GUIUtil::TOQS(sTitle), GUIUtil::TOQS(sSummary), QMessageBox::Ok, QMessageBox::Ok);
}

void FinalExamDialog::UpdateDisplay()
{
	// Load initial values
	std::string sCPK = DefaultRecAddress("Christian-Public-Key");
	UserRecord r = GetUserRecord(sCPK);
	std::string sMode = fMode==0 ? "<font color=red>LEARNING MODE</font>" : "<font color=red>TESTING MODE</font>";

	std::string sInfo = sMode + "<br><br>Welcome to your Final Exam, " + r.NickName + " " + sCPK + "!";

	ui->txtInfo->setText(GUIUtil::TOQS(sInfo));
	ui->lcdSecs->display(0);
	ui->lcdMins->display(0);

	// Find the first verse to do the initial population.
	PopulateQuestion();
}

void FinalExamDialog::setModel(WalletModel *model)
{
}

FinalExamDialog::~FinalExamDialog()
{
    delete ui;
}

void FinalExamDialog::clear()
{
	ui->txtAnswer->setPlainText("");
}

void FinalExamDialog::on_btnDone_clicked()
{
	fTesting = false;
	Score();
	ShowResults();
}

double FinalExamDialog::Grade()
{
	std::string sChosen;
	if (ui->radioA->isChecked())
	{
		sChosen = "A";
	}
	else if (ui->radioB->isChecked())
	{
		sChosen = "B";
	}
	else if (ui->radioC->isChecked())
	{
		sChosen = "C";
	}
	else if (ui->radioD->isChecked())
	{
		sChosen = "D";
	}

	mapChosen[nCurrentQuestion] = sChosen;
	double nCorrect = (vecAnswerKey[nCurrentQuestion] == sChosen)  ? 1 : 0;

	return nCorrect;
}

void FinalExamDialog::OnAfterUpdated()
{
	double nCorrect = Grade();
	if (fMode == 1)
	{
		ui->lblGrade->setText("");
	}
	else if (fMode == 0)
	{	
		std::string sChosen = mapChosen[nCurrentQuestion];
		if (!sChosen.empty())
		{
			std::string sCorrNarr = nCorrect == 1 ? "<font color=red>Correct</font>" : "<font color=red>Incorrect</font>";
			ui->lblGrade->setText(GUIUtil::TOQS(sCorrNarr));
		}
		else
		{
			ui->lblGrade->setText("");
		}
	}
}

void FinalExamDialog::Score()
{
	double nTestPct = Grade();

	if (fMode == 0)
	{
		nTrainingQuestionsTaken++;
		nTrainingScore += nTestPct;
	}
	// Score the current Testing session
	if (fMode == 1)
	{
		nTestingQuestionsTaken++;
		nTestingScore += nTestPct;
	}
}

void FinalExamDialog::ContinueTimer()
{
	if (!fTesting)
	{
		fTesting = true;
		ui->lcdSecs->display(0);
		ui->lcdMins->display(0);
		QTimer::singleShot(1000, this, SLOT(ClockTick()));
	}
}

void FinalExamDialog::on_btnNext_clicked()
{
	Score();
	nCurrentQuestion++;
	if (nCurrentQuestion > vecQ.size()-1)
	{
		nCurrentQuestion = vecQ.size()-1;
		return;
	}
	PopulateQuestion();
	OnAfterUpdated();

	ContinueTimer();
}

void FinalExamDialog::on_btnBack_clicked()
{
	Score();
	nCurrentQuestion--;
	if (nCurrentQuestion < 0)
	{
		nCurrentQuestion = 0;
		return;
	}
	PopulateQuestion();
	OnAfterUpdated();
	ContinueTimer();
}

void FinalExamDialog::on_btnMode_clicked()
{
	if (!fTesting)
	{
		fTesting = true;
		QTimer::singleShot(1000, this, SLOT(ClockTick()));
	}
	ui->lcdSecs->display(0);
	ui->lcdMins->display(0);
	fMode = !fMode;
	UpdateDisplay();
}

