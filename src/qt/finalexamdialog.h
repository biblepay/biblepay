// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_FINALEXAMDIALOG_H
#define BITCOIN_QT_FINALEXAMDIALOG_H

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
    class FinalExamDialog;
}

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

/** Dialog for BiblePay Final Exam */
class FinalExamDialog : public QDialog
{
    Q_OBJECT

public:

    explicit FinalExamDialog(QWidget *parent, std::string sFinalExam);
    ~FinalExamDialog();

    void setModel(WalletModel *model);
	void UpdateDisplay();

public Q_SLOTS:
    void clear();
	
protected:
	void PopulateQuestion();
	void Score();
	void ShowResults();
    double Grade();
	void StripNumber();
	void ResetRadios();
	std::string ExtractAnswer(std::string sLetter);
	void ContinueTimer();
	double CalculateScores();
	void OnAfterUpdated();

private:
    Ui::FinalExamDialog *ui;
    WalletModel *model;
    QMenu *contextMenu;
    const PlatformStyle *platformStyle;
	double nTestingQuestionsTaken = 0;
	double nTestingScore = 0;
	double nTrainingQuestionsTaken = 0;
	double nTrainingScore = 0;
	bool fMode = 1;
	int nCurrentQuestion = 0;
	std::vector<std::string> vecQ;
	std::vector<std::string> vecA;
	std::map<int, std::string> mapChosen;
	std::vector<std::string> vecAnswerKey;
	bool fTesting = false;

private Q_SLOTS:
    void on_btnDone_clicked();
	void on_btnNext_clicked();
	void on_btnMode_clicked();
	void on_btnBack_clicked();
	void ClockTick();
};

#endif // BITCOIN_QT_FINALEXAMDIALOG_H
