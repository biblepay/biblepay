#include "smtp.h"
#include "clientversion.h"
#include "chainparams.h"
#include "init.h"
#include "net.h"
#include "utilstrencodings.h"
#include "utiltime.h"

#include <ctime>
#include <iostream>
#include <string>
#include <boost/asio.hpp>
#include <boost/asio/buffer.hpp>
using namespace boost::asio;
using namespace boost::asio::ip;

io_service ioservice;
tcp::endpoint pop3_endpoint{tcp::v4(), 111};
tcp::acceptor pop3_acceptor{ioservice, pop3_endpoint};
tcp::socket pop3_socket{ioservice};

tcp::endpoint smtp_endpoint{tcp::v4(), 26};
tcp::acceptor smtp_acceptor{ioservice, smtp_endpoint};
tcp::socket smtp_socket{ioservice};

std::array<char, 1000000> pop3_bytes;
std::array<char, 1000000> smtp_bytes;
std::string smtp_buffer;
std::string pop3_buffer;

bool pop3_connected = false;
bool smtp_connected = false;
int64_t smtp_activity = 0;
int64_t pop3_activity = 0;
bool smtp_DATA_MODE = false;

bool fDebuggingEmail = false;

void pop3_write_handler(const boost::system::error_code &ec, std::size_t bytes_transferred)
{
    if (!ec)
    {
    }
	 else
    {
		std::cout << "Pop3::WriteHandlerError:" << ec.message() << std::endl;
    }
}

void smtp_write_handler(const boost::system::error_code &ec, std::size_t bytes_transferred)
{
    if (!ec)
    {
    }
	 else
    {
		std::cout << "Smtp::WriteHandlerError:" << ec.message() << std::endl;
    }
}

void Analyze()
{
	if (fDebuggingEmail)
		LogPrintf("\n analyzing %s ", pop3_buffer);
	for (int i = 0; i < pop3_buffer.length(); i++)
	{
		char c = pop3_buffer.at(i);
		LogPrintf(" %f ", (int)c);
	}
}

void pop3_send(std::string data)
{
	if (fDebuggingEmail)
		LogPrintf("pop3_SENTBACK %s", data);
	async_write(pop3_socket, buffer(data), pop3_write_handler);
}


void smtp_send(std::string data)
{
	async_write(smtp_socket, buffer(data), smtp_write_handler);
	if (fDebuggingEmail)
		LogPrintf("\nsmtp_SEND %s", data);
}

void pop3_close()
{
	pop3_socket.cancel();
	pop3_socket.shutdown(tcp::socket::shutdown_send);
	pop3_socket.close();
	if (fDebuggingEmail)
		std::cout << "pop3_shutdown " << std::endl;
	pop3_connected = false;
}

void smtp_close()
{
	smtp_socket.cancel();
	smtp_socket.shutdown(tcp::socket::shutdown_send);
	smtp_socket.close();
	if (fDebuggingEmail)
		std::cout << "smtp_shutdown " << std::endl;
	smtp_connected = false;
}

void pop3_CAPA()
{
	std::string sReply = "+OK\r\nCAPA\r\nTOP\r\nUIDL\r\nRESP-CODES\r\nPIPELINING\r\nAUTH-RESP-CODE\r\nUSER\r\nSASL PLAIN LOGIN\r\n.\r\n";
	pop3_send(sReply);
}

std::string sPopUser;
void pop3_USER(std::string sWho)
{
	sPopUser = GetElement(sWho, " ", 1);
	LogPrintf("\npop3_PopUser %s", sPopUser);
	std::string sReply = "+OK\r\n";
	pop3_send(sReply);
}

std::string sPopPass;
void pop3_PASS(std::string sPass)
{
	sPopPass = GetElement(sPass, " ", 1);
	LogPrintf("\npop3_Pass %s", sPopPass);
	std::string sReply = "+OK Logged in.\r\n";
	pop3_send(sReply); 
}


void pop3_QUIT()
{
	std::string sReply = "+OK Goodbye, and may God bless you with the Richest blessings of Abraham Isaac and Jacob.\r\n";
	pop3_send(sReply);
	pop3_close();
}

int GetMessageSize()
{
	int nTotal = 0;
	for (auto it : mapEmails) 
	{
		CEmail myEmail = CEmail::getEmailByHash(it.first);
		
		LogPrintf("Email hash %s ", myEmail.GetHash().GetHex());

		if (myEmail.IsMine())
		{
			nTotal += myEmail.Body.length();
		}
	}
	return nTotal;
}

int GetMessageCount()
{
	int nTotal = 0;
	for (auto it : mapEmails)
	{
		CEmail myEmail = mapEmails[it.first];
		if (fDebuggingEmail)
			LogPrintf("\nGetMessageCount %s %s", myEmail.ToEmail, msMyInternalEmailAddress);
		if (myEmail.IsMine())
		{
			nTotal++;
		}
	}
	return nTotal;
}

CEmail GetEmailByID(int nID)
{
	int nOrdinal = 0;
	for (auto it : mapEmails) 
	{
		CEmail myEmail = CEmail::getEmailByHash(it.first);
		if (myEmail.IsMine())
		{
			nOrdinal++;
			if (nOrdinal == nID)
			{
				return myEmail;
			}
		}
	}
	CEmail email;
	return email;
}

void pop3_STAT()
{
	// This command returns the number of messages in an inbox, and the sum of their size in bytes.  In 1970 they used to call the byte sum OCTETs, btw.
	std::string sReply = "+OK " + RoundToString(GetMessageCount(), 0) + " " + RoundToString(GetMessageSize(), 0) + "\r\n";
	pop3_send(sReply);
}

void Forensic()
{
	int nOrdinal=0;
		for (auto it : mapEmails) 
		{
			CEmail myEmail = CEmail::getEmailByHash(it.first);
			if (myEmail.IsMine())
			{
				nOrdinal++;
				LogPrintf("\r\nSMTP::Forensic From %s, To %s, Body %s, Size %f ", myEmail.ToEmail, myEmail.FromEmail, Mid(myEmail.Body, 1, 100), myEmail.Body.length());
			}
			else
			{
				LogPrintf("\r\nSMTP::Forensic-NotMine From %s, To %s, Body %s, Size %f ", myEmail.ToEmail, myEmail.FromEmail, Mid(myEmail.Body, 1, 100), myEmail.Body.length());
			
			}
		}

}

void pop3_LIST(std::string sWhat)
{
	double nMsgNo = cdbl(GetElement(sWhat, " ", 1), 0);
	Forensic();
	if (nMsgNo == 0)
	{
		// List all of them
		double nMsgCount = 2;
		std::string sReply = "+OK " + RoundToString(GetMessageCount(), 0) + " messages:\r\n";
		std::string sReplies = sReply;
		int nOrdinal = 0;
		std::map<uint256, CEmail>::iterator it;
		for (auto it : mapEmails) 
		{
			CEmail myEmail = CEmail::getEmailByHash(it.first);
			if (myEmail.IsMine())
			{
				nOrdinal++;
				std::string sRow = RoundToString(nOrdinal, 0) + " " + myEmail.GetHash().GetHex();
				sReplies += sRow + "\r\n";
			}
		}
		sReplies += ".\r\n";
		pop3_send(sReplies);
	}
	else
	{
		// Just list the one
		LogPrintf("POP3::GetEmailById %f ", nMsgNo);
		CEmail myemail = GetEmailByID(nMsgNo);
		std::string sReply = "+OK " + RoundToString(nMsgNo, 0) + " " + myemail.GetHash().GetHex() + "\r\n.\r\n";
		pop3_send(sReply);
	}

}

std::string FirstFiveLines(std::string sWhat)
{
	// First 5 are required by the protocol:
	std::vector<std::string> vEle = Split(sWhat.c_str(), "\r\n");
	std::string sData;
	for (int i = 0; i < 5 && i < vEle.size(); i++)
	{
		sData += vEle[i] + "\r\n";
	}
	return sData;
}

void pop3_TOP(std::string sWhat)
{
	double nMsgNo = cdbl(GetElement(sWhat, " ", 1), 0);
	double nLinesToGet = cdbl(GetElement(sWhat, " ", 2), 0);
	// Retrieves "MsgNo" + "#OfLines"
	CEmail e = GetEmailByID(nMsgNo);
	std::string sReply = "+OK " + RoundToString(e.Body.length(), 0) + " octets\r\n" + e.Headers + "Message-Id: <" + e.GetHash().GetHex()
		+ ">\r\n\r\n" + FirstFiveLines(e.Body) + "\r\n.\r\n";
	pop3_send(sReply);
}


void pop3_RETR(std::string sNo)
{
	double nMsgNo = cdbl(GetElement(sNo, " ", 1), 0);
	CEmail e = GetEmailByID(nMsgNo);
	if (!e.IsNull())
	{
		LogPrintf("\r\nFound Email %s", Mid(e.Body, 1, 100));
		std::string sReply = "+OK " + RoundToString(e.Body.length(), 0) + " octets\r\n" + e.Body + "\r\n" + "Message-Id: <" + e.GetHash().GetHex() + ">\r\n.\r\n";
		pop3_send(sReply);
	}
	else
	{
		LogPrintf("\r\nNotFound %f", 9007);
		pop3_send("+OK\r\n.\r\n");
		return;
	}
}

void pop3_process(std::string sInbound)
{
	  std::string sMyInbound(sInbound.c_str());

	  pop3_buffer += sMyInbound;
	  if (Contains(sInbound,"\n"))
	  {
		  // Process the pop3 command here
		  LogPrintf("\npop3_receive %s ", pop3_buffer);
		  if (pop3_buffer.find("QUIT") != std::string::npos)
		  {
			  pop3_QUIT();
		  }
		  else if (pop3_buffer.find("CAPA") != std::string::npos)
		  {
			  pop3_CAPA();
		  }
		  else if (pop3_buffer.find("USER") != std::string::npos)
		  {
			  pop3_USER(pop3_buffer);
		  }
		  else if (pop3_buffer.find("PASS") != std::string::npos)
		  {
			  pop3_PASS(pop3_buffer);
		  }
		  else if (pop3_buffer.find("STAT") != std::string::npos)
		  {
			  pop3_STAT();
		  }
		  else if (pop3_buffer.find("LIST") != std::string::npos)
		  {
			  pop3_LIST(pop3_buffer);
		  }
		  else if (pop3_buffer.find("UIDL") != std::string::npos)
		  {
			  pop3_LIST(pop3_buffer);
		  }
		  else if (pop3_buffer.find("RETR") != std::string::npos)
		  {
			  pop3_RETR(pop3_buffer);
		  }
		  else if (pop3_buffer.find("TOP") != std::string::npos)
		  {
			  pop3_TOP(pop3_buffer);
		  }
		  pop3_buffer = std::string();
	  }
}

void smtp_EHLO()
{
	std::string sReply = "250-biblepay Hello\r\n250-SIZE 0\r\n250-8BITTIME\r\n250 HELP\r\n250 AUTH LOGIN PLAIN\r\n";
	smtp_send(sReply);
}

bool smtp_Auth_User = false;
bool smtp_Auth_Pass = false;

void smtp_AUTH()
{
	// Base 64 Version of Auth
	std::string sAuth = "334 VXNlcm5hbWU6\r\n";
	smtp_Auth_User = true;
	smtp_Auth_Pass = false;
	smtp_send(sAuth);
}

std::string sSMTPUserName;
void smtp_AUTH_PASS(std::string sUserName)
{
	smtp_Auth_User = false;
	smtp_Auth_Pass = true;
	sSMTPUserName = sUserName;
	// Base 64 version of Auth
	std::string sAuth = "334 UGFzc3dvcmQ6\r\n";
	smtp_send(sAuth);
}

std::string sSMTPPassword;
void smtp_AUTH_COMPLETE(std::string sPassword)
{
	sSMTPPassword = sPassword;
	smtp_Auth_User = false;
	smtp_Auth_Pass = false;
	// Note that we don't actually use this, we use RSA keypairs and the private key is never revealed over the network!
	std::string sReply = "235 Authentication succeeded\r\n";
	smtp_send(sReply);
}

std::string msEmailFrom;
void smtp_MAIL_FROM(std::string sBuffer)
{
	msEmailFrom = GetElement(sBuffer, " ", 2);
	smtp_send("250 OK\r\n");
}

std::string msEmailTo;
void smtp_RCPT_TO(std::string sBuffer)
{
	msEmailTo = GetElement(sBuffer, " ", 2);
	if (fDebuggingEmail)
		LogPrintf("\nSMTP_TO %s", msEmailTo);
	smtp_send("250 OK\r\n");
}

void smtp_DATA()
{
	smtp_send("354 Enter message, ending with . on a line by itself\r\n");
	smtp_DATA_MODE = true;
}

void smtp_SENDMAIL(std::string sData)
{
	if (fDebuggingEmail && false)
		LogPrintf("\nSendMail %s", sData);
	// At this point we have enough to relay the object

	CEmail e;
	e.FromEmail = msEmailFrom;
	e.ToEmail = msEmailTo;

	// If the email is encrypted, this is where we apply the RSA key:

	std::string sPubPath =  GetSANDirectory4() + "pubkey.pub";
	std::string sPrivPath = GetSANDirectory4() + "privkey.priv";
	std::string sError;
	// Mission critical todo (spork_rsa needs to be enabled):
	/*
	std::string sEnc = RSA_Encrypt_String(sPubPath, sData ,sError);
	std::string sDec = RSA_Decrypt_String(sPrivPath, sEnc, sError);
	e.Body = sDec;

	*/

	e.Body = sData;
	
	e.nType = 1;
	e.nVersion = 1;
	e.nTime = GetAdjustedTime();
	e.Encrypted = false;

	uint256 eHash = e.GetHash();
	LogPrintf("\nEMAIL HASH %s", eHash.GetHex());

	// Charge user, then relay it

	UserRecord r = GetMyUserRecord();
	bool fPaid = false;
	if (r.AuthorizePayments)
	{
		fPaid = PayEmailFees(e);
	}
	
	if (fPaid && e.Body.length() < 3000000)
	{
		e.ProcessEmail();
		SendEmail(e);
		// Email is now on disk
		std::string sReply = "250 OK id=" + eHash.GetHex() + "\r\n";
		smtp_send(sReply);
	}
	else
	{
		std::string sNarr;
		if (!fPaid)
			sNarr += "UNPAID BIBLEPAY FEES. ";
		if (e.Body.length() > 3000000)
			sNarr += "MESSAGE TOO BIG (LIMIT TO 3,000,000 BYTES PLEASE). ";
		// The server has rejected the message due to its size, or because the user will not pay the fee.  "The recipients mailbox has exceeded its storage limit" is returned to the mail client here:
		std::string sReply = "422 REJECTED - " + sNarr + "\r\n";
		smtp_send(sReply);
	}

	smtp_close();
}

void smtp_process(std::string sInbound)
{
	std::string sMyInbound(sInbound.c_str());
	smtp_buffer += sMyInbound;
	if (smtp_DATA_MODE)
	{
		if (smtp_buffer.find("\r\n.\r\n") != std::string::npos || smtp_buffer.find("\n.\n") != std::string::npos)
		{
			smtp_DATA_MODE = false;
			smtp_SENDMAIL(smtp_buffer);
		}
		else if (smtp_buffer.find("QUIT") != std::string::npos)
		{
			// Reserved
			smtp_close();
		}
	}
	else if (Contains(sInbound, "\n"))
	{
		// Process the SMTP command here
		LogPrintf("\nsmtp_receive %s ", smtp_buffer);
		if (smtp_buffer.find("QUIT") != std::string::npos)
		{
			smtp_close();
		}
		else if (smtp_buffer.find("EHLO") != std::string::npos)
		{
			smtp_EHLO();
		}
		else if (smtp_buffer.find("AUTH") != std::string::npos)
		{
			smtp_AUTH();
		}
		else if (smtp_buffer.length() > 1 && smtp_Auth_User)
		{
			smtp_AUTH_PASS(smtp_buffer);
		}
		else if (smtp_buffer.length() > 1 && smtp_Auth_Pass)
		{
			smtp_AUTH_COMPLETE(smtp_buffer);
		}
		else if (smtp_buffer.find("MAIL FROM") != std::string::npos)
		{
			smtp_MAIL_FROM(smtp_buffer);
		}
		else if (smtp_buffer.find("RCPT TO") != std::string::npos)
		{
			smtp_RCPT_TO(smtp_buffer);
		}
		else if (smtp_buffer.find("DATA") != std::string::npos)
		{
			smtp_DATA();
		}
		smtp_buffer = std::string();
	}
}

void pop3_read_handler(const boost::system::error_code &ec, std::size_t bytes_transferred)
{
   if (ShutdownRequested())
		return;

  if (!ec)
  {
	  
	  if (pop3_connected)
	  {
		  std::string s1(std::begin(pop3_bytes), std::end(pop3_bytes));
	  	  pop3_process(s1);
	      pop3_bytes.fill('\0');
	 
          pop3_socket.async_read_some(buffer(pop3_bytes), pop3_read_handler);
      }
  }
  else
  {
	  	if ((boost::asio::error::eof == ec) || (boost::asio::error::connection_reset == ec))
	    {
            // handle the disconnect.
			pop3_close();
		}
		else
		{
			std::cout << "ASIO::" + ec.message() << std::endl;
		}
  }
}

void smtp_read_handler(const boost::system::error_code &ec, std::size_t bytes_transferred)
{
   if (ShutdownRequested())
		return;
   

    int64_t nElapsed = GetAdjustedTime() - smtp_activity;
    if (!ec)
    {
  	    if (smtp_connected)
	    {
	        std::string s1(std::begin(smtp_bytes), std::end(smtp_bytes));
		    smtp_process(s1);
	   	    smtp_bytes.fill('\0');
		    if (s1.length() > 1)
		  	  smtp_activity = GetAdjustedTime();
	   	    smtp_socket.async_read_some(buffer(smtp_bytes), smtp_read_handler);
  	    }
	    else
	    {
			// Reserved
	    }
  }
  else
  {
	  	if ((boost::asio::error::eof == ec) || (boost::asio::error::connection_reset == ec))
	    {
    		// handle the disconnect.
			std::cout << "smtp--shutting down the socket through hangup " << std::endl;
		    smtp_close();
		}
		else
		{
			// Display the system error:
			std::cout << "2ASO::" + ec.message() << std::endl;
		}
  }
}

void InitializeUser()
{
	UserRecord rec = GetMyUserRecord();
	msMyInternalEmailAddress = rec.InternalEmail;
	if (fDebuggingEmail)
		LogPrintf("\r\nSMTP::InitializeUser MyInternalEmailAddress=%s", msMyInternalEmailAddress);
}

void pop3_accept_handler(const boost::system::error_code &ec)
{
  if (!ec)
  {
	  UserRecord rec = GetMyUserRecord();
	  msMyInternalEmailAddress = rec.InternalEmail;
	  pop3_send("+OK POP3 server ready <" + msMyInternalEmailAddress + ">\n");
	  pop3_socket.async_read_some(buffer(pop3_bytes), pop3_read_handler);
  }
  else
  {
	  std::cout << "7009+" + ec.message() << std::endl;
  }
}

void smtp_accept_handler(const boost::system::error_code &ec)
{
	UserRecord rec = GetMyUserRecord();
	msMyInternalEmailAddress = rec.InternalEmail;
	smtp_Auth_User = false;
	smtp_Auth_Pass = false;
    smtp_DATA_MODE = false;
	smtp_activity = GetAdjustedTime();
	smtp_connected=true;
    
    if (!ec)
    {
	   smtp_send("220 biblepay - SMTP ready\n");
	   smtp_socket.async_read_some(buffer(smtp_bytes), smtp_read_handler);
    }
    else
    {
	  std::cout << "SMTP SYSTEM ERROR " << ec.message() << std::endl;
    }
}

void ThreadPOP3(CConnman& connman)
{
	// This is BiblePay's Decentralized version of the Pop3 protocol.
	// This allows BiblePay Core to deliver encrypted e-mails into your favorite E-mail client's inbox.
	// The message is encrypted up to the point when your e-mail Client asks for it, and, at that very point we decrypt it with your RSA key (in the local biblepaycore wallet).
	// As long as you have your PST file set up locally on your drive, and, you trust your e-mail client program, theoretically the message will be entirely secure end-to-end.
	// If you want even more security you can use the biblepay core wallet inbox.  You can also look into opensource e-mail clients like firebird if you do not trust outlook.

	while (1==1)
	{
		pop3_socket.close();

		pop3_connected = false;
		pop3_acceptor.listen();
		pop3_acceptor.async_accept(pop3_socket, pop3_accept_handler);
		pop3_connected = true;
		for (int i = 0; i < 10*60*1; i++)
		{
			MilliSleep(100);
			if (!pop3_connected || ShutdownRequested())
				  break;

			try
			  {
	    	      ioservice.poll();
			  }
			  catch(...)
			  {
		  		  LogPrintf("Exception %f", 7091);
				  break;
			  }
		}
		MilliSleep(1000);
		if (fDebuggingEmail)
			std::cout << "POP3 - Ready to Listen again ." << std::endl;
	}
}

//Request missing e-mails (Those less than 30 days old, paid for, in transactions, not on our hard drive)
void RequestMissingEmails()
{
	for (auto ii : mvApplicationCache) 
	{
		if (Contains(ii.first, "EMAIL"))
		{
			std::pair<std::string, int64_t> v = mvApplicationCache[ii.first];
			int64_t nTimestamp = v.second;
			std::string sTXID = GetElement(ii.first, "[-]", 1);
			uint256 hashInput = uint256S(sTXID);
			std::string sFileName = "email_" + hashInput.GetHex() + ".eml";
			std::string sTarget = GetSANDirectory4() + sFileName;
			int64_t nSz = GETFILESIZE(sTarget);
			if (nSz <= 0)
			{
				LogPrintf("\nSMTP::Requesting Missing Email %s ", sFileName);
				CEmailRequest erequest;
				erequest.RequestID = hashInput;
				bool nSent = false;
				g_connman->ForEachNode([&erequest, &nSent](CNode* pnode) 
				{
					erequest.RelayTo(pnode, *g_connman);
			    });
				LogPrintf("\nSMTP::RequestMissingEmails - Requesting %s %f ", sFileName, nSz);

			}
			else
			{
				LogPrintf("\nSMTP::RequestMissingEmails - Found %s %f ", sFileName, nSz);
				std::map<uint256, CEmail>::iterator mi = mapEmails.find(hashInput);
				if (mi == mapEmails.end())
				{
					CEmail e;
					e.EDeserialize(hashInput);
					uint256 MyHash = e.GetHash();
					e.Body = std::string();
					mapEmails.insert(std::make_pair(MyHash, e));
				}
			}
		}
	}
}


void ThreadSMTP(CConnman& connman)
{
	// This is BiblePay's decentralized version of SMTP.
	// This facilitates delivery of (your pre-created e-mail message) that is encrypted by biblepay (in contrast to forwarding the plaintext version over to your ISP).  In this case, the biblepay network becomes your ISP.
	// We encrypt the message with the destination recipient RSA public key first (before the message leaves biblepay).
	// Then we send the message over the biblepay network (encrypted).
	// Later, our decentralized POP3 protocol will handle the decryption and delivery.
	// You may also forward other non-biblepay e-mails into our SMTP server for non-encrypted delivery (as long as the fees are paid). 
	int64_t nTimer;
	while (1==1)
	{
		smtp_socket.close();
		smtp_connected = false;
		smtp_DATA_MODE = false;
	
		smtp_acceptor.listen();
		smtp_acceptor.async_accept(smtp_socket, smtp_accept_handler);
		smtp_connected=true;
		for (int i = 0; i < 10*60*1; i++)
		{
		
			  MilliSleep(100);
			  nTimer++;
			  if (nTimer % 1000 == 0)
			  {
				  // 2 minutes - TODO; change this to 5 minutes
				  RequestMissingEmails();
			  }
    	 	  if (!smtp_connected || ShutdownRequested())
				  break;
			  
			  try
			  {
     			  ioservice.poll();
			  }
			  catch(...)
			  {
				  LogPrintf("Exception %f", 7092);
  				  break;
			  }
		}
		MilliSleep(1000);
		if (fDebuggingEmail)
			std::cout << "SMTP - We have ended ." << std::endl;
	}
}

