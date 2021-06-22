// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2014-2019 The Däsh Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//#if defined(HAVE_CONFIG_H)
//#include "config/coin-config.h"
//#/endif

#include "menudialog.h"
#include "forms/ui_menudialog.h"
#include "finalexamdialog.h"
#include "rpcpog.h"
#include "bitcoingui.h"
#include "clientmodel.h"
#include "guiconstants.h"
#include "intro.h"
#include "guiutil.h"
#include "clientversion.h"
#include "init.h"
#include "util.h"
#include <stdio.h>

#include <QCloseEvent>
#include <QDesktopServices>
#include <QLabel>
#include <QRegExp>
#include <QTextTable>
#include <QTextCursor>
#include <QVBoxLayout>


std::string GetLink(std::string sID, std::string sDescription)
{
	std::string sLink = "<a href='" + sID + "' style='text-decoration:none;color:pink;'><u>" + sDescription + "</u></a>";
	return sLink;
}
/** "BiblePay University" menu page */
MenuDialog::MenuDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::MenuDialog)
{
    ui->setupUi(this);

   	std::string sTitle = "BiblePay University 1.0";
	setWindowTitle(QString::fromStdString(sTitle));
	
	std::string sWelcome = "Welcome to BiblePay University.  We are proud to offer our FREE college level courses to the general public to help make disciples globally.  We decided to start a satellite college of Harvestime.org, an academic publisher of gospel materials, who supplies many gospel based colleges in the US, and hundreds overseas. ";
	sWelcome += " We are now an official institute for administering college courses and final exams (we paid for the course material, and paid to become an institute).  We are currently not involved in generating degrees, but our course will include partners that allow you to transfer your credits to accepting schools (since our material is harvestime material).  An example will be provided explaing how to transfer credits toward a Bachelor of Theology.  Each course will show the number of college credit equivalents you may earn. ";
	
	std::string sFlow = "\nWhat type of doctrine is this based on?  100% of our material is based on the Gospel of Jesus Christ, from a non-denominational perspective.  The material teaches a literal bible, without symbolism (for example, the book of Acts miracles are taught as literally occurring).  The version taught is from the KJV bible, with both the Old Testament and New Testament included.  (The course is written entirely in English, but will be available in more languages on high demand).  ";
	sFlow += "\n\nPlease follow these instructions to successfully learn our academic material and become a disciple of Jesus:  ";
	sFlow += "\nOur program is broken up into 4 modules, with each module having approximately 4-6 courses.  Each course is equivalent to roughly 4 college level credits, taking approximately 3 months at one hour per college day * 2 days per week.  You may work at your own pace, and are encouraged to use an online bible for reference. ";
	sFlow += "\n\nHow do I take the courses, and where do I find the material?  Please read the following expected workflow:  You will need to pick a few courses first.  For example, if you are getting started, you will want to start with module 1, and for example pick 3 courses:  Foundations of Faith, Bible Study Methods, and Old Testament survey for example.  These will be your three current classes.  You can take all 4 if you want. ";
	sFlow += "To Take each class, you will start by clicking on the Course Material link for Foundations of Faith.  The PDF links for the course material are below.  You can think of this as your class school book, in PDF format.  You will progress through this at your own pace as if attending a college course.  You will be responsible for writing your own writing assignments (given in the course) and keeping those on your PC.  ";
	sFlow += "\n\nWhen you are ready for your final exam, you will click the Final Exam link next to Foundation of Faith.  This is our certification test interface.  We will allow you to use this interface as many times as necessary.  This interface supports Exam Testing Mode and Exam Review mode.  In review mode you will see all of the questions answers so you can go back and learn those subjects better in the course material and or online. ";
	sFlow += "BiblePay will store your final exam score by your wallet CPK so that you can print out your grades.  ";
	
	std::string s1 = "\n\n<hr>\n\nCourse Materials - Module 1 - Introduction to Biblical Studies - 18 Credits";
	s1 += "\n\n1.  " + GetLink("FOF", "Foundations of Faith") + " stresses the importance of proper spiritual foundations for life and ministry, by focusing on foundations of Christian Faith, identified by Hebrews 6:1 : Repentance, Faith, baptism, laying on of hands, resurrection, and eternal judgement.  ";
	s1 += "\n      " + GetLink("FOF_FE", "Final Exam");
	
	s1 += "\n\n2.  " + GetLink("CBSM", "Creative Bible Study Methods") + " equips students for personal study of the word of God.  Students learn how to study the Bible by book, chapter, paragraph, verse and word.  Other methods taught include biographical, devotional, theological, typological and topical.  Special guidelines for studying Bible Poetry and prophecy are presented and students are taught methods of charting and outlining.  ";
	s1 += "\n      " + GetLink("CBSM_FE", "Final Exam");
	
	s1 += "\n\n3.  " + GetLink("OTS", "Old Testament Survey") + " provides an overview of the Old Testament and outlines each book.";
	s1 += "\n      " + GetLink("OTS_FE", "Final Exam");

	s1 += "\n\n4.  " + GetLink("NTS", "New Testament Survey") + " provides an overview of the New Testament, and outlines each book.  ";
	s1 += "\n      " + GetLink("NTS_FE", "Final Exam");
	
	s1 += "\n\n<hr>\n\nCourse Materials - Module 2 - Applying Biblical Studies Personally - 21 Credits";
	s1 += "\n\n5.  " + GetLink("DBWV", "Developing a Biblical World View") + " examines the Biblical world view from Genesis through Revelation.  ";

	s1 += "  God's plan for the nations of the world from the beginning of time is detailed.  Current worldwide spiritual need is also presented. ";
	s1 += "\n      " + GetLink("DBWV_FE", "Final Exam");


	s1 += "\n\n6.  " + GetLink("KL", "Kingdom Living") + " is a course focusing on the Kingdom of God.  The \"Gospel of the Kingdom\" shall be preached in all the world before the return of the Lord Jesus Christ ";
	s1 += " (Matthew 24:14). Understanding of Kingdom principles is necessary if one is to spread the Gospel of the Kingdom.  ";
	s1 += "This course focuses on patterns and principles of Kingdom living applicable to life and ministry.";
	s1 += "\n      " + GetLink("KL_FE", "Final Exam");


	s1 += "\n\n7.  " + GetLink("KGV", "Knowing God's Voice") + " explains how God speaks to men today and how to find His general and specific plans for life.  A Christian model for decision making is presented, along with guidelines for overcoming wrong decisions, steps to take if you have missed the will of God, and methods for dealing with questionable practices. ";
	s1 += "\n      " + GetLink("KGV_FE", "Final Exam");


	s1 += "\n\n8.  " + GetLink("HSM", "Ministry of the Holy Spirit") + " focuses on the ministry of the Holy Spirit, spiritual fruit, and spiritual gifts.  Students are guided in discovery of their own spiritual gifts and position of ministry in the Body of Christ.";
	s1 += "\n      " + GetLink("HSM_FE", "Final Exam");

	s1 += "\n\n9.  " + GetLink("SW", "Spiritual Strategies") + " is a Manual of Spiritual Warfare.  This course moves participants beyond the natural world into the realm of the spirit.  Tactics of the enemy are analyzed and strategies of spiritual warfare assuring victory over the principalities and powers of the spirit world are explained.";
	s1 += "\n      " + GetLink("SW_FE", "Final Exam");

	s1 += "\n\n10. " + GetLink("PP", "Power Principles") + " explains how the early church was born in a demonstration of the power of God.  Power principles taught in this course equip students for spiritual harvest and moves them from being spectators to demonstrators of the power of God.";
	s1 += "\n      " + GetLink("PP_FE", "Final Exam");

	s1 += "\n\n\n\nNOTE:  Modules 3-4 are coming soon.  ";

	 std::string s2 = sWelcome + sFlow + s1;

	 QString qsp = QString::fromStdString(s2);
     qsp.replace("\n", "<br>");

     ui->lblMessage->setTextFormat(Qt::RichText);
     ui->scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
     ui->lblMessage->setText(qsp);
     ui->lblMessage->setWordWrap(true);
	 ui->lblMessage->setOpenExternalLinks(false);
	 ui->lblMessage->setTextInteractionFlags(Qt::TextBrowserInteraction);

     // The big TextEdit box:
     ui->txtMenu->setVisible(false);
	 if (true)
	 {
		ui->aboutLogo->setVisible(true);
		// Theme dependent Gfx in About popup
		QString helpMessageGfx = ":/images/about";
		QPixmap pixmap = QPixmap(helpMessageGfx);
		ui->aboutLogo->setPixmap(pixmap);
	 }

     connect(ui->lblMessage, SIGNAL(linkActivated(QString)), this, SLOT(myLink(QString)));

}

void MenuDialog::myLink(QString h)
{
	std::string s1 = GUIUtil::FROMQS(h); 
	std::string sURL;
	std::string sFA;
	std::string sDomain = "https://foundation.biblepay.org/Univ/";
	// Module 1, added Jan 2021
	if (s1 == "FOF")
	{
		sURL = sDomain + "Foundations Of Faith.pdf";
	}
	else if (s1 == "CBSM")
	{
		sURL = sDomain + "Creative Bible Study.pdf";
	}
	else if (s1 == "OTS")
	{
		sURL = sDomain + "Basic Bible Survey Old Testament.pdf";
	}
	else if (s1 == "NTS")
	{
		sURL = sDomain + "Basic Bible Survey New Testament.pdf";
	}
	else if (s1 == "FOF_FE")
	{
		sFA = "FoundationsOfFaith_key.xml";
	}
	else if (s1 == "CBSM_FE")
	{
		sFA = "CreativeBibleStudy_key.xml";
	}
	else if (s1 == "OTS_FE")
	{
		sFA = "OTSurvey_key.xml";
	}
	else if (s1 == "NTS_FE")
	{
		sFA = "NTSurvey_key.xml";
	}
	// Module 2, Added Feb 6th, 2021
	else if (s1 == "DBWV")
	{
		sURL = sDomain + "Developing A Biblical World View.pdf";
	}
	else if (s1 == "KL")
	{
		sURL = sDomain + "Kingdom Living.pdf";
	}
	else if (s1 == "KGV")
	{
		sURL = sDomain + "Knowing The Voice Of God.pdf";
	}
	else if (s1 == "HSM")
	{
		sURL = sDomain + "Ministry Of The Holy Spirit.pdf";
	}
	else if (s1 == "SW")
	{
		sURL = sDomain + "Spiritual Strategies (Warfare).pdf";
	}
	else if (s1 == "PP")
	{
		sURL = sDomain + "PowerPrinciples.pdf";
	}
	else if (s1 == "DBWV_FE")
	{
		sFA = "DevelopingBiblicalWorldView_key.xml";
	}
	else if (s1 == "KL_FE")
	{
		sFA = "KingdomLiving_key.xml";
	}
	else if (s1 == "KGV_FE")
	{
		sFA = "KnowingGodsVoice_key.xml";
	}
	else if (s1 == "HSM_FE")
	{
		sFA = "HolySpiritMinistry_key.xml";
	}
	else if (s1 == "SW_FE")
	{
		sFA = "SpiritualStrategies_key.xml";
	}
	else if (s1 == "PP_FE")
	{
		sFA = "PowerPrinciples_key.xml";
	}

	if (!sURL.empty())
	{
		QUrl pUrl(GUIUtil::TOQS(sURL));
		QDesktopServices::openUrl(pUrl);
	}
	else if (!sFA.empty())
	{
		// Our student clicked on an internal link (most likely a final exam).  Redirect them to the target page.
	    FinalExamDialog dlg(this, sFA);
		dlg.exec();
	}
}

MenuDialog::~MenuDialog()
{
    delete ui;
}


void MenuDialog::on_btnOK_accepted()
{
    close();
}


