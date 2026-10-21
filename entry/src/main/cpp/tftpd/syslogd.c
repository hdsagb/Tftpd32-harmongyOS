//////////////////////////////////////////////////////
//
// Projet TFTPD32.  Mai 98 Ph.jounin
// File syslogd.c:  Syslog
//
// source released under European Union Public License
//
//////////////////////////////////////////////////////
//
// HarmonyOS 适配：无 Windows 命名管道，PipeSyslogMsg 相关代码保留但不生效
// （m_hPipe 始终为 INVALID_HANDLE_VALUE）。UDP 监听 + 文件日志逻辑不变。
//
//////////////////////////////////////////////////////

#include "headers.h"
#include "threading.h"


static HANDLE hSysLogFile = INVALID_HANDLE_VALUE;


///////////////////////////////////////////////////////
// Open the log file
// uses sSettings, modifies hSysLogFile
///////////////////////////////////////////////////////
void SyslogCreateLogFile (LPSTR szLogFile)
{
   if (hSysLogFile !=INVALID_HANDLE_VALUE)
       CloseHandle (hSysLogFile);

   // if file already exists append at end of file
   if (szLogFile != NULL  && szLogFile[0]!=0)
   {
       hSysLogFile = CreateFile (szLogFile,
                                 GENERIC_WRITE,
                                 FILE_SHARE_READ,
                                 NULL,
                                 OPEN_ALWAYS,
                                 FILE_ATTRIBUTE_ARCHIVE | FILE_FLAG_SEQUENTIAL_SCAN ,
                                 NULL);
       // seek end of file
       //if (hSysLogFile==INVALID_HANDLE_VALUE)
       //           MY_WARNING ("can not open Syslog File");
       SetFilePointer (hSysLogFile, 0, 0, FILE_END);
 }
} // SyslogOpenLog




// check that received message match the form '<....?.?>'
// may be this must be an option
BOOL CheckSyslogMsg (char *SyslogTxt, int nSize)
{
int Ark;
   if (nSize < 5)  return FALSE;
   if (    SyslogTxt[0] != '<'
       && (SyslogTxt[4] != '>'  || SyslogTxt[5] != '>' || SyslogTxt[6] != '>')
      )  return FALSE;

   SyslogTxt[nSize] = 0;
   for (Ark=0 ;  Ark<nSize ;  Ark++)
     if (! __isascii (SyslogTxt[Ark]))  SyslogTxt[Ark]='.';

return TRUE;
} // CheckSyslogMsg




/////////////////////////////
// Background window 
//
void SyslogProc (void * param)
{
SOCKET   sSyslogListenSocket=INVALID_SOCKET;
char     szSyslogBuf[SYSLOG_MAXMSG+1]; // Buffer 
int             Rc;
SOCKADDR_STORAGE sSock;
socklen_t       nDummy;
HANDLE m_hPipe=INVALID_HANDLE_VALUE;  /* HarmonyOS 无命名管道，保留占位 */
struct S_SyslogMsg msg;

   (void) m_hPipe;

   sSyslogListenSocket  = tThreads[TH_SYSLOG].skt;
   SyslogCreateLogFile (sSettings.szSyslogFile);

   tThreads [TH_SYSLOG].bInit = TRUE;  // inits OK

   while ( tThreads[TH_SYSLOG].gRunning )
   {
      nDummy = sizeof sSock;
      // get the message, checks its format and display it
      Rc = recvfrom (sSyslogListenSocket, szSyslogBuf, SYSLOG_MAXMSG, 
                     0, (struct sockaddr *) & sSock, & nDummy);

      // something received and format OK
      if (Rc>0  && CheckSyslogMsg (szSyslogBuf, Rc) )
      {

		  // who sent the UDP message
		  getnameinfo ( (LPSOCKADDR) & sSock, sizeof sSock, 
		                 msg.from, sizeof msg.from, 
				         NULL, 0,
				         NI_NUMERICHOST );          
		  if ( sSock.ss_family == AF_INET6 
			 && IN6_IS_ADDR_V4MAPPED ( & (* (struct sockaddr_in6 *) & sSock ).sin6_addr ) )
          {
		  	  memmove (msg.from, msg.from + sizeof ("::ffff:") - 1, strlen (msg.from + sizeof ("::ffff:") -1) +1 );        
		  }
		  msg.from[sizeof msg.from - 1] = 0; // probably paranoid


		  if (hSysLogFile != INVALID_HANDLE_VALUE)
		  {
			  struct tm *newtime;
			  time_t tm;
			  DWORD Dummy;
			  char szTxt[SYSLOG_MAXMSG + 1 + 30];
			  time(&tm);
			  newtime = localtime(&tm);
			  // copy in file the string without the new-line
			  wsprintf(szTxt,"%24.24s;%s; %s\r\n",asctime(newtime),msg.from,szSyslogBuf);
			  WriteFile(hSysLogFile, szTxt, lstrlen(szTxt), &Dummy, NULL);
		  } // log in file

          lstrcpy (msg.txt, szSyslogBuf); 
          SendMsgRequest (   C_SYSLOG, 
			               & msg, 
						     1 + Rc + sizeof msg.from, 
							 FALSE,	  	    // don't block thread until msg sent
							 FALSE );		// if no GUI return
      }
   } // loop

   /* 关闭日志文件并复位，支持服务停/启循环 */
   if (hSysLogFile != INVALID_HANDLE_VALUE)
   {
       CloseHandle (hSysLogFile);
       hSysLogFile = INVALID_HANDLE_VALUE;
   }
   LogToMonitor ("End of Syslog thread\n");
_endthread ();
} // SyslogDProc
