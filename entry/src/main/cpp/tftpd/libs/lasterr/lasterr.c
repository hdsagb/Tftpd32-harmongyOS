//////////////////////////////////////////////////////
//
// Projet TFTPD32.  Mars 2000 Ph.jounin
// File lasterr.c:   function LastErrorText
// HarmonyOS 适配：FormatMessage → strerror（同签名）
//
// source released under European Union Public License
//
//////////////////////////////////////////////////////

#include "headers.h"
#include "lasterr.h"


// Function LastErrorText
// A wrapper for FormatMessage : retrieve the message text for a system-defined error
char *LastErrorText (void)
{
static char szLastErrorText [512];

   snprintf (szLastErrorText, sizeof szLastErrorText, "%s", strerror (GetLastError ()));
return szLastErrorText;
} // LastErrorText
