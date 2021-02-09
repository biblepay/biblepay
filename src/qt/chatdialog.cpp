// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2014-2017 The Däsh Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chatdialog.h"
#include "chat.h"
#include "validation.h"
#include "guiutil.h"

#include <QClipboard>
#include <QUrl>
#include <QtWidgets>
#include <QtGui>
#include <boost/algorithm/string/case_conv.hpp> // for to_lower()
#include "timedata.h"
#include <boost/algorithm/string.hpp>

ChatDialog::ChatDialog(QWidget *parent, bool bPrivateChat, bool bEncryptedChat, std::string sMyName, std::string sDestRoom) : QDialog(parent)
{
     setupUi(this);
	 fPrivateChat = bPrivateChat;
	 this->setAttribute(Qt::WA_DeleteOnClose);
	 this->setWindowFlags(Qt::Window); 
	 this->fEncryptedChat = bEncryptedChat;
     lineEdit->setFocusPolicy(Qt::StrongFocus);
     textEdit->setFocusPolicy(Qt::NoFocus);
     textEdit->setReadOnly(true);
     listWidget->setFocusPolicy(Qt::NoFocus);

     connect(lineEdit, SIGNAL(returnPressed()), this, SLOT(returnPressed()));
 #ifdef Q_OS_SYMBIAN
     connect(sendButton, SIGNAL(clicked()), this, SLOT(returnPressed()));
 #endif
     connect(lineEdit, SIGNAL(returnPressed()), this, SLOT(returnPressed()));

	 sNickName = sMyName;
	 
     newParticipant(sMyName);
     tableFormat.setBorder(0);
	 if (fPrivateChat)
	 {
		if (sDestRoom.empty()) QTimer::singleShot(1000, this, SLOT(queryRecipientName()));
		sRecipientName = sDestRoom;
	 }
	 else
	 {
		sRecipientName = sDestRoom;
	 }
	 setTitle();
}

void ChatDialog::setTitle()
{
	std::string sExtraNarr = fEncryptedChat ? "Encrypted " : "";
	std::string sPM = fPrivateChat ? "Private " + sExtraNarr + "Messaging - " + sNickName + " & " + sRecipientName : "Public Chat - " + sRecipientName;
	this->setWindowTitle(GUIUtil::TOQS(sPM));
}

void ChatDialog::setClientModel(ClientModel *model)
{
    this->clientModel = model;
    if(model)
    {
        // Subscribe to chat signals
        connect(model, SIGNAL(chatEvent(QString)), this, SLOT(receivedEvent(QString)));
    }
}

void ChatDialog::closeEvent(QCloseEvent *event)
{
	CChat c;
	c.nVersion = 1;
	c.nTime = GetAdjustedTime();
	c.nID = GetAdjustedTime();
	c.bPrivate = fPrivateChat;
	c.bEncrypted = fEncryptedChat;
	c.nPriority = 1;
	c.sDestination = sRecipientName;
	c.sFromNickName = sNickName;
	c.sPayload = "<LEFT>";
	c.sToNickName = sRecipientName;
	SendChat(c);
}

std::string ChatDialog::Decrypt(std::string sEnc)
{
	std::string sMessage = sEnc;
	if (sEnc == "<RING>")
		return sEnc;
	if (fEncryptedChat)
	{
		std::string sError;
		std::string sPrivPath = GetSANDirectory4() + "privkey.priv";
		std::string sDec = RSA_Decrypt_String(sPrivPath, sEnc, sError);
		sMessage = "(Encrypted) " + sDec;
		if (!sError.empty())
		{
			sMessage = "(Encryption error: " + sError + ")";
			LogPrintf("\nEncChat::Failure decrypting %s %s ", sEnc, sError);
		}
	}
	return sMessage;
}

void ChatDialog::receivedEvent(QString sMessage)
{
	 // Deserialize back into the chat object
	 CChat c(GUIUtil::FROMQS(sMessage));
	 if (sRecipientName.empty()) return;
	 // If this is ours
	 if ((fPrivateChat && boost::iequals(c.sDestination, sNickName) && boost::iequals(c.sFromNickName,  sRecipientName))
     	  || (!fPrivateChat && boost::iequals(c.sDestination, sRecipientName)))
	 {
		// Clear any global rings
		bool bCancelDisplay = false;

		if (fPrivateChat && c.sPayload == "<RING>" && !fEncryptedChat)
		{
			mlPaged = 0;
			msPagedFrom = "";
		}
		else if (fEncryptedChat && c.sPayload == "<RING>")
		{
			mlPagedEncrypted = 0;
			msPagedFrom = "";
		}
		else if (c.sPayload == "<LEFT>")
		{
			participantLeft(c.sFromNickName);
			bCancelDisplay = true;
 		}

		if (!bCancelDisplay) 
		{
			std::string sMessage = Decrypt(c.sPayload);
			appendMessage(c.sFromNickName, sMessage, c.nPriority, c.nTime);
			QList<QListWidgetItem *> items = listWidget->findItems(GUIUtil::TOQS(c.sFromNickName), Qt::MatchExactly);
			if (items.isEmpty()) 
				newParticipant(c.sFromNickName);
		}

	 }

}

void ChatDialog::appendMessage(std::string sFrom, std::string sMessage, int nPriority, int64_t nTime)
{
     if (sFrom.empty() || sMessage.empty()) 
		 return;

     QTextCursor cursor(textEdit->textCursor());
     cursor.movePosition(QTextCursor::End);
     QTextTable *table = cursor.insertTable(1, 2, tableFormat);
	 // Suggested by MIP - Add timestamp - 2-8-2021
	 
     table->cellAt(0, 0).firstCursorPosition().insertText(GUIUtil::TOQS("<" + sFrom + " " + TimestampToHRDate(nTime) + "> "));
	
     QTextTableCell cell = table->cellAt(0, 1);
	 QTextCharFormat format = cell.format();
	
	 QBrush my_brush;
	 QColor red(Qt::red);
	 QColor black(Qt::black);
	 QColor blue(Qt::blue);
	 if (nPriority < 3)
	 {
		format.setForeground(black);
	 }
	 else if (nPriority == 3 || nPriority == 4)
	 {
		format.setForeground(blue);
	 }
	 else if (nPriority == 5)
	 {
		format.setForeground(red);
	 }
	 cell.setFormat(format);
	
	 table->cellAt(0, 1).firstCursorPosition().insertText(GUIUtil::TOQS(sMessage));
  	 
     QScrollBar *bar = textEdit->verticalScrollBar();
     bar->setValue(bar->maximum());
 }

 void ChatDialog::returnPressed()
 {
     QString text = lineEdit->text();
     if (text.isEmpty())
         return;

     if (text.startsWith(QChar('/'))) 
	 {
         QColor color = textEdit->textColor();
         textEdit->setTextColor(Qt::red);
         textEdit->append(tr("! Unknown command: %1").arg(text.left(text.indexOf(' '))));
         textEdit->setTextColor(color);
     }
	 else 
	 {
		 CChat c;
		 c.nVersion = 1;
		 c.nTime = GetAdjustedTime();
		 c.nID = GetAdjustedTime();
		 c.bPrivate = fPrivateChat;
		 c.bEncrypted = fEncryptedChat;
		 c.nPriority = 1;
		 c.sDestination = sRecipientName;
		 c.sFromNickName = sNickName;
		 c.sPayload = GUIUtil::FROMQS(text);
		 if (fEncryptedChat)
		 {
			std::string sError;

		 	if (this->PublicKey.empty())
			{
				UserRecord u = GetUserRecord(sRecipientName);
				this->PublicKey = u.RSAPublicKey;
				if (this->PublicKey.length() < 256)
				{
					LogPrintf("\nEncryptedChat::Unable to find recipient %s public key. ", sRecipientName);
				}
			}
	
			 c.sPayload = RSA_Encrypt_String_With_Key(this->PublicKey, c.sPayload, sError);

			 if (!sError.empty())
				 c.sPayload = "(Encryption error: " + sError + ")";
		 }
		 c.sToNickName = sRecipientName;
		 SendChat(c);
		 if (fPrivateChat || fEncryptedChat)
			 appendMessage(sNickName, GUIUtil::FROMQS(text), c.nPriority, c.nTime);
     }

     lineEdit->clear();
 }

 void ChatDialog::newParticipant(std::string sTheirNickName)
 {
     if (sNickName.empty())  return;
     QColor color = textEdit->textColor();
     textEdit->setTextColor(Qt::gray);
     textEdit->append(tr("* %1 has joined").arg(GUIUtil::TOQS(sTheirNickName)));
     textEdit->setTextColor(color);
     listWidget->addItem(GUIUtil::TOQS(sTheirNickName));
 }

 void ChatDialog::participantLeft(std::string sTheirNickName)
 {
     if (sNickName.empty()) return;

     QList<QListWidgetItem *> items = listWidget->findItems(GUIUtil::TOQS(sTheirNickName), Qt::MatchExactly);
     if (items.isEmpty()) return;

     delete items.at(0);
     QColor color = textEdit->textColor();
     textEdit->setTextColor(Qt::gray);
     textEdit->append(tr("* %1 has left").arg(GUIUtil::TOQS(sTheirNickName)));
     textEdit->setTextColor(color);
 }

 void ChatDialog::queryRecipientName()
 {
	bool bOK = false;
	sRecipientName = GUIUtil::FROMQS(QInputDialog::getText(this, tr("BiblePay Chat - Private Messaging"),
                                          tr("Please enter the recipient name you would like to Page >"),
										  QLineEdit::Normal, "", &bOK));
	std::string sErr;
	UserRecord u = GetUserRecord(sRecipientName);
	if (!u.Found) 
		sErr += "Sorry, we can't find this user [" + sRecipientName + "]. ";

	if (fEncryptedChat)
	{
		if (u.RSAPublicKey.length() < 256)
			sErr += "Sorry, we can't find this users RSA public key!  Please ask the user to save their User Record from the User Record page. ";
		RSAKey r = GetMyRSAKey();
		if (r.PublicKey.length() < 256 || r.PrivateKey.length() < 1000)
			sErr += "Sorry, we can't find your RSA public key.  To make one, please go to your User Record and save your nickname and ensure the RSA public key is generated. ";
		
		if (sErr.empty())
		{
			// set the keys
			this->PrivateKey = r.PrivateKey;
			this->PublicKey = u.RSAPublicKey;
			std::string sNarr = "This conversation is successfully being encrypted.  No bytes will be seen on the network in clear text.  ";
			QMessageBox::warning(this, tr("Success"), GUIUtil::TOQS(sNarr), QMessageBox::Ok, QMessageBox::Ok);
		}
	}

	if (!sErr.empty())
	{
		QMessageBox::warning(this, tr("Error instantiating Chat"), GUIUtil::TOQS(sErr), QMessageBox::Ok, QMessageBox::Ok);
		return;
	}



	if (sRecipientName.empty()) 
		return;
	setTitle();
	// Page the recipient 
	for (int x = 1; x < 5; x++)
	{
		QTimer::singleShot(1500 * x, this, SLOT(ringRecipient()));
	}
 }

 void ChatDialog::ringRecipient()
 {
	CChat c;
	c.nVersion = 1;
	c.nTime = GetAdjustedTime();
	c.nID = GetAdjustedTime();
	c.bPrivate = fPrivateChat;
	c.bEncrypted = fEncryptedChat;
	c.nPriority = 1;
	c.sDestination = sRecipientName;
	c.sFromNickName = sNickName;
	c.sPayload = "<RING>";
	c.sToNickName = sRecipientName;
	SendChat(c);
	if (fPrivateChat || fEncryptedChat) 
		appendMessage(sNickName, c.sPayload, c.nPriority, c.nTime);
}