// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_CHATDIALOG_H
#define BITCOIN_QT_CHATDIALOG_H

#include "forms/ui_chatdialog.h"
#include "clientmodel.h"

class ChatDialog : public QDialog, private Ui::ChatDialog
{
     Q_OBJECT

 public:
     ChatDialog(QWidget *parent = 0, bool fPrivateChat = false, bool fEncrypted = false, std::string sMyNickname = "", std::string sDestRoom = "");
	 bool fPrivateChat;
	 bool fEncryptedChat;
	 std::string sRecipientName;
	 std::string sNickName;
	 std::string sRoom;
	 // Note:  The RSA private key never leaves the local machine (the next two variables are for the chat window only, allowing on the fly encryption of each message) :
	 std::string PrivateKey;
	 std::string PublicKey;
     void setClientModel(ClientModel *model);

public Q_SLOTS:
     void appendMessage(std::string sFrom, std::string sMessage, int nPriority, int64_t nTime);
     void newParticipant(std::string sNickName);
     void participantLeft(std::string sNickName);
 
 private Q_SLOTS:
     void returnPressed();
     void queryRecipientName();
	 void receivedEvent(QString sMessage);
	 void ringRecipient();
	 void closeEvent(QCloseEvent *event);

 private:
     QTextTableFormat tableFormat;
	 ClientModel *clientModel;
	 std::string Decrypt(std::string sEnc);
	 void setTitle();

};


#endif // BITCOIN_QT_CHATDIALOG_H
