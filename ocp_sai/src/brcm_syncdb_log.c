/*********************************************************************
 *
 * Copyright: (c) 2017 Broadcom.
 * Broadcom Proprietary and Confidential. All rights reserved.
 *
 *********************************************************************/

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include <brcm_syncdb.h>

extern char _syncdb_log_level_path[];

/* Maximum length of the log entry.
*/
#define LOG_MSG_SIZE 128

static int syncdb_log_level = 0;

/*********************************************************************
* @purpose  Get the number of milliseconds since the system booted.
*           
* @param    None
*
* @returns  None
*
* @notes  
*       
* @end
*********************************************************************/
static unsigned long long upTimeMillisecondsGet(void)
{
  struct timespec tp;
  int rc;

  rc = clock_gettime (CLOCK_MONOTONIC, &tp);
  if (rc < 0)
  {
      return 0;
  }

  return ((tp.tv_sec * 1000) + (tp.tv_nsec / 1000000));
}

/*********************************************************************
* @purpose  Get the current logging level.
*           
* @param    None
*
* @returns  None
*
* @notes  
*       
* @end
*********************************************************************/
void syncdbLogLevelGet (void)
{
  int fd;
  char buf[20];
  int rc;
  int log_level;

  syncdb_log_level = 0;

  fd = open (_syncdb_log_level_path, O_RDONLY);
  if (fd < 0) 
  {
      return;
  }

  memset (buf, 0, sizeof (buf));
  rc = read (fd, buf, sizeof(buf) - 1);
  (void) close (fd);
  if (rc < 0) 
  {
      return;
  }

  log_level = atoi (buf);

  if (log_level < 0) 
  {
      return;
  }
 
  syncdb_log_level = log_level; 
}

/*********************************************************************
* @purpose  Append the loge entry to the log file.
*           
* @param    log_entry - String containing the log entry.
*
* @returns  None
*
* @notes  
*       
* @end
*********************************************************************/
static void syncdbEventLog (char *log_entry)
{
  int fd;

  fd = open ("/tmp/syncdb.log", O_WRONLY | O_APPEND | O_CREAT, 0777);
  if (fd < 0) 
  {
      return;
  }
  (void) write (fd, log_entry, strlen (log_entry));

  (void) close (fd);

}

/*********************************************************************
* @purpose  Log Debug message in the debug log.
*           
* @param    None
*
* @returns  None
*
* @notes    This function also updates the logging level.
*       
* @end
*********************************************************************/
void syncdbDebugMsgLog (void)
{
  char log_entry[LOG_MSG_SIZE];

    syncdbLogLevelGet ();
    if (!syncdb_log_level) 
    {
        return;
    }

    memset (log_entry, 0, sizeof (log_entry));
    snprintf (log_entry, LOG_MSG_SIZE - 1, 
              "%llu Debug Log-Level:%d\n", 
              upTimeMillisecondsGet(),
              syncdb_log_level);
    log_entry [LOG_MSG_SIZE - 2] = '\n'; /* Make sure we got a new-line */

    syncdbEventLog (log_entry);
}

/*********************************************************************
* @purpose  Log Client-Register message in the debug log.
*           
* @param    pid - Process ID of the requester.
* @param    client_id - Assigned client ID.
* @param    err_code - Error Code returned by the syncdb.
*
* @returns  None
*
* @notes  
*       
* @end
*********************************************************************/
void syncdbClientRegisterMsgLog (int pid, int client_id, int err_code)
{
  char log_entry[LOG_MSG_SIZE];

    if (!syncdb_log_level) 
    {
        return;
    }

    memset (log_entry, 0, sizeof (log_entry));
    snprintf (log_entry, LOG_MSG_SIZE - 1, 
              "%llu P:%d C:%d Client-Register rc:%d\n", 
              upTimeMillisecondsGet(),
              pid,
              client_id,
              err_code);
    log_entry [LOG_MSG_SIZE - 2] = '\n'; /* Make sure we got a new-line */

    syncdbEventLog (log_entry);
}

/*********************************************************************
* @purpose  Log Client-Status message in the debug log.
*           
* @param    pid - Process ID of the requester.
* @param    client_id - Assigned client ID.
* @param    target_client - Client whose status is requested.
* @param    err_code - Error Code returned by the syncdb.
*
* @returns  None
*
* @notes 
*       
* @end
*********************************************************************/
void syncdbClientStatusMsgLog (int pid, 
                               int client_id, 
                               int target_client,
                               int err_code)
{
  char log_entry[LOG_MSG_SIZE];

    if (!syncdb_log_level) 
    {
        return;
    }

    memset (log_entry, 0, sizeof (log_entry));
    snprintf (log_entry, LOG_MSG_SIZE - 1, 
              "%llu P:%d C:%d Client-Status Target:%d rc:%d\n", 
              upTimeMillisecondsGet(),
              pid,
              client_id,
              target_client,
              err_code);
    log_entry [LOG_MSG_SIZE - 2] = '\n'; /* Make sure we got a new-line */

    syncdbEventLog (log_entry);
}

/*********************************************************************
* @purpose  Log Table-Change-Notify message in the debug log.
*           
* @param    pid - Process ID of the requester.
* @param    client_id - Assigned client ID.
* @param    table_name - Table name for which notification is requested.
* @param    err_code - Error Code returned by the syncdb.
*
* @returns  None
*
* @notes 
*       
* @end
*********************************************************************/
void syncdbTableChangeNotifyMsgLog (int pid, 
                               int client_id, 
                               char *table_name,
                               int err_code)
{
  char log_entry[LOG_MSG_SIZE];

    if (!syncdb_log_level) 
    {
        return;
    }

    memset (log_entry, 0, sizeof (log_entry));
    snprintf (log_entry, LOG_MSG_SIZE - 1, 
              "%llu P:%d C:%d Tab-Change-Notify Tab:%s rc:%d\n", 
              upTimeMillisecondsGet(),
              pid,
              client_id,
              table_name,
              err_code);
    log_entry [LOG_MSG_SIZE - 2] = '\n'; /* Make sure we got a new-line */

    syncdbEventLog (log_entry);
}

/*********************************************************************
* @purpose  Log Table-Status-Get message in the debug log.
*           
* @param    pid - Process ID of the requester.
* @param    client_id - Assigned client ID.
* @param    num_tables - Number of tables for which to get status.
* @param    err_code - Error Code returned by the syncdb.
*
* @returns  None
*
* @notes 
*       
* @end
*********************************************************************/
void syncdbTableStatusGetMsgLog (int pid, 
                               int client_id, 
                               int num_tables,
                               int err_code)
{
  char log_entry[LOG_MSG_SIZE];

    if (!syncdb_log_level) 
    {
        return;
    }

    memset (log_entry, 0, sizeof (log_entry));
    snprintf (log_entry, LOG_MSG_SIZE - 1, 
              "%llu P:%d C:%d Tab-Status-Get Num-Tables:%d rc:%d\n", 
              upTimeMillisecondsGet(),
              pid,
              client_id,
              num_tables,
              err_code);
    log_entry [LOG_MSG_SIZE - 2] = '\n'; /* Make sure we got a new-line */

    syncdbEventLog (log_entry);
}



/*********************************************************************
* @purpose  Log Avl-Table-Create message in the debug log.
*           
* @param    pid - Process ID of the requester.
* @param    client_id - Assigned client ID.
* @param    table_name - Created table name.
* @param    err_code - Error Code returned by the syncdb.
*
* @returns  None
*
* @notes 
*       
* @end
*********************************************************************/
void syncdbAvlTableCreateMsgLog (int pid, 
                               int client_id, 
                               char *table_name,
                               int err_code)
{
  char log_entry[LOG_MSG_SIZE];

    if (!syncdb_log_level) 
    {
        return;
    }

    memset (log_entry, 0, sizeof (log_entry));
    snprintf (log_entry, LOG_MSG_SIZE - 1, 
              "%llu P:%d C:%d Avl-Tab-Create Tab:%s rc:%d\n", 
              upTimeMillisecondsGet(),
              pid,
              client_id,
              table_name,
              err_code);
    log_entry [LOG_MSG_SIZE - 2] = '\n'; /* Make sure we got a new-line */

    syncdbEventLog (log_entry);
}

/*********************************************************************
* @purpose  Log Record-Table-Create message in the debug log.
*           
* @param    pid - Process ID of the requester.
* @param    client_id - Assigned client ID.
* @param    table_name - Created table name.
* @param    err_code - Error Code returned by the syncdb.
*
* @returns  None
*
* @notes 
*       
* @end
*********************************************************************/
void syncdbRecordTableCreateMsgLog (int pid, 
                               int client_id, 
                               char *table_name,
                               int err_code)
{
  char log_entry[LOG_MSG_SIZE];

    if (!syncdb_log_level) 
    {
        return;
    }

    memset (log_entry, 0, sizeof (log_entry));
    snprintf (log_entry, LOG_MSG_SIZE - 1, 
              "%llu P:%d C:%d Rec-Tab-Create Tab:%s rc:%d\n", 
              upTimeMillisecondsGet(),
              pid,
              client_id,
              table_name,
              err_code);
    log_entry [LOG_MSG_SIZE - 2] = '\n'; /* Make sure we got a new-line */

    syncdbEventLog (log_entry);
}

/*********************************************************************
* @purpose  Log Table-Delete message in the debug log.
*           
* @param    pid - Process ID of the requester.
* @param    client_id - Assigned client ID.
* @param    table_name - Table name to be deleted.
* @param    err_code - Error Code returned by the syncdb.
*
* @returns  None
*
* @notes 
*       
* @end
*********************************************************************/
void syncdbTableDeleteMsgLog (int pid, 
                               int client_id, 
                               char *table_name,
                               int err_code)
{
  char log_entry[LOG_MSG_SIZE];

    if (!syncdb_log_level) 
    {
        return;
    }

    memset (log_entry, 0, sizeof (log_entry));
    snprintf (log_entry, LOG_MSG_SIZE - 1, 
              "%llu P:%d C:%d Tab-Delete Tab:%s rc:%d\n", 
              upTimeMillisecondsGet(),
              pid,
              client_id,
              table_name,
              err_code);
    log_entry [LOG_MSG_SIZE - 2] = '\n'; /* Make sure we got a new-line */

    syncdbEventLog (log_entry);
}

/*********************************************************************
* @purpose  Log Table-Store message in the debug log.
*           
* @param    pid - Process ID of the requester.
* @param    client_id - Assigned client ID.
* @param    table_name - Table name for which save id requested.
* @param    err_code - Error Code returned by the syncdb.
*
* @returns  None
*
* @notes 
*       
* @end
*********************************************************************/
void syncdbTableStoreMsgLog (int pid, 
                               int client_id, 
                               char *table_name,
                               int err_code)
{
  char log_entry[LOG_MSG_SIZE];

    if (!syncdb_log_level) 
    {
        return;
    }

    memset (log_entry, 0, sizeof (log_entry));
    snprintf (log_entry, LOG_MSG_SIZE - 1, 
              "%llu P:%d C:%d Tab-Store Tab:%s rc:%d\n", 
              upTimeMillisecondsGet(),
              pid,
              client_id,
              table_name,
              err_code);
    log_entry [LOG_MSG_SIZE - 2] = '\n'; /* Make sure we got a new-line */

    syncdbEventLog (log_entry);
}

/*********************************************************************
* @purpose  Log Insert message in the debug log.
*           
* @param    pid - Process ID of the requester.
* @param    client_id - Assigned client ID.
* @param    table_name - Table name into which an element is inserted
* @param    err_code - Error Code returned by the syncdb.
*
* @returns  None
*
* @notes 
*       
* @end
*********************************************************************/
void syncdbInsertMsgLog (int pid, 
                               int client_id, 
                               char *table_name,
                               int err_code)
{
  char log_entry[LOG_MSG_SIZE];

    if (!syncdb_log_level) 
    {
        return;
    }

    memset (log_entry, 0, sizeof (log_entry));
    snprintf (log_entry, LOG_MSG_SIZE - 1, 
              "%llu P:%d C:%d Insert Tab:%s rc:%d\n", 
              upTimeMillisecondsGet(),
              pid,
              client_id,
              table_name,
              err_code);
    log_entry [LOG_MSG_SIZE - 2] = '\n'; /* Make sure we got a new-line */

    syncdbEventLog (log_entry);
}

/*********************************************************************
* @purpose  Log Delete message in the debug log.
*           
* @param    pid - Process ID of the requester.
* @param    client_id - Assigned client ID.
* @param    table_name - Table name from which an element is deleted
* @param    err_code - Error Code returned by the syncdb.
*
* @returns  None
*
* @notes 
*       
* @end
*********************************************************************/
void syncdbDeleteMsgLog (int pid, 
                               int client_id, 
                               char *table_name,
                               int err_code)
{
  char log_entry[LOG_MSG_SIZE];

    if (!syncdb_log_level) 
    {
        return;
    }

    memset (log_entry, 0, sizeof (log_entry));
    snprintf (log_entry, LOG_MSG_SIZE - 1, 
              "%llu P:%d C:%d Delete Tab:%s rc:%d\n", 
              upTimeMillisecondsGet(),
              pid,
              client_id,
              table_name,
              err_code);
    log_entry [LOG_MSG_SIZE - 2] = '\n'; /* Make sure we got a new-line */

    syncdbEventLog (log_entry);
}

/*********************************************************************
* @purpose  Log Set message in the debug log.
*           
* @param    pid - Process ID of the requester.
* @param    client_id - Assigned client ID.
* @param    table_name - Table name in which to set.
* @param    err_code - Error Code returned by the syncdb.
*
* @returns  None
*
* @notes 
*       
* @end
*********************************************************************/
void syncdbSetMsgLog (int pid, 
                               int client_id, 
                               char *table_name,
                               int err_code)
{
  char log_entry[LOG_MSG_SIZE];

    if (!syncdb_log_level) 
    {
        return;
    }

    memset (log_entry, 0, sizeof (log_entry));
    snprintf (log_entry, LOG_MSG_SIZE - 1, 
              "%llu P:%d C:%d Set Tab:%s rc:%d\n", 
              upTimeMillisecondsGet(),
              pid,
              client_id,
              table_name,
              err_code);
    log_entry [LOG_MSG_SIZE - 2] = '\n'; /* Make sure we got a new-line */

    syncdbEventLog (log_entry);
}

/*********************************************************************
* @purpose  Log Field-Set message in the debug log.
*           
* @param    pid - Process ID of the requester.
* @param    client_id - Assigned client ID.
* @param    table_name - Table name in which to set.
* @param    offset - Offset of the field which to set.
* @param    size - Size of the field which to set.
* @param    err_code - Error Code returned by the syncdb.
*
* @returns  None
*
* @notes 
*       
* @end
*********************************************************************/
void syncdbFieldSetMsgLog (int pid, 
                               int client_id, 
                               char *table_name,
                               int offset,
                               int size,
                               int err_code)
{
  char log_entry[LOG_MSG_SIZE];

    if (!syncdb_log_level) 
    {
        return;
    }

    memset (log_entry, 0, sizeof (log_entry));
    snprintf (log_entry, LOG_MSG_SIZE - 1, 
              "%llu P:%d C:%d Fld-Set Tab:%s O:%d S:%d rc:%d\n", 
              upTimeMillisecondsGet(),
              pid,
              client_id,
              table_name,
              offset,
              size,
              err_code);
    log_entry [LOG_MSG_SIZE - 2] = '\n'; /* Make sure we got a new-line */

    syncdbEventLog (log_entry);
}


/*********************************************************************
* @purpose  Log Get message in the debug log.
*           
* @param    pid - Process ID of the requester.
* @param    client_id - Assigned client ID.
* @param    table_name - Table name from which to get.
* @param    err_code - Error Code returned by the syncdb.
*
* @returns  None
*
* @notes 
*       
* @end
*********************************************************************/
void syncdbGetMsgLog (int pid, 
                               int client_id, 
                               char *table_name,
                               int err_code)
{
  char log_entry[LOG_MSG_SIZE];

    if (!syncdb_log_level) 
    {
        return;
    }

    memset (log_entry, 0, sizeof (log_entry));
    snprintf (log_entry, LOG_MSG_SIZE - 1, 
              "%llu P:%d C:%d Get Tab:%s rc:%d\n", 
              upTimeMillisecondsGet(),
              pid,
              client_id,
              table_name,
              err_code);
    log_entry [LOG_MSG_SIZE - 2] = '\n'; /* Make sure we got a new-line */

    syncdbEventLog (log_entry);
}

/*********************************************************************
* @purpose  Log Field-Get message in the debug log.
*           
* @param    pid - Process ID of the requester.
* @param    client_id - Assigned client ID.
* @param    table_name - Table name from which to get.
* @param    offset - Offset of the field which to get.
* @param    size - Size of the field which to get.
* @param    err_code - Error Code returned by the syncdb.
*
* @returns  None
*
* @notes 
*       
* @end
*********************************************************************/
void syncdbFieldGetMsgLog (int pid, 
                               int client_id, 
                               char *table_name,
                               int offset,
                               int size,
                               int err_code)
{
  char log_entry[LOG_MSG_SIZE];

    if (!syncdb_log_level) 
    {
        return;
    }

    memset (log_entry, 0, sizeof (log_entry));
    snprintf (log_entry, LOG_MSG_SIZE - 1, 
              "%llu P:%d C:%d Fld-Get Tab:%s O:%d S:%d rc:%d\n", 
              upTimeMillisecondsGet(),
              pid,
              client_id,
              table_name,
              offset,
              size,
              err_code);
    log_entry [LOG_MSG_SIZE - 2] = '\n'; /* Make sure we got a new-line */

    syncdbEventLog (log_entry);
}

/*********************************************************************
* @purpose  Log GetNext message in the debug log.
*           
* @param    pid - Process ID of the requester.
* @param    client_id - Assigned client ID.
* @param    table_name - Table name from which to get.
* @param    err_code - Error Code returned by the syncdb.
*
* @returns  None
*
* @notes 
*       
* @end
*********************************************************************/
void syncdbGetNextMsgLog (int pid, 
                               int client_id, 
                               char *table_name,
                               int err_code)
{
  char log_entry[LOG_MSG_SIZE];

    if (!syncdb_log_level) 
    {
        return;
    }

    memset (log_entry, 0, sizeof (log_entry));
    snprintf (log_entry, LOG_MSG_SIZE - 1, 
              "%llu P:%d C:%d Get-Next Tab:%s rc:%d\n", 
              upTimeMillisecondsGet(),
              pid,
              client_id,
              table_name,
              err_code);
    log_entry [LOG_MSG_SIZE - 2] = '\n'; /* Make sure we got a new-line */

    syncdbEventLog (log_entry);
}
/*********************************************************************
* @purpose  Log GetNextChanged message in the debug log.
*           
* @param    pid - Process ID of the requester.
* @param    client_id - Assigned client ID.
* @param    table_name - Table name from which to get.
* @param    err_code - Error Code returned by the syncdb.
*
* @returns  None
*
* @notes 
*       
* @end
*********************************************************************/
void syncdbGetNextChangedMsgLog (int pid, 
                               int client_id, 
                               char *table_name,
                               int err_code)
{
  char log_entry[LOG_MSG_SIZE];

    if (!syncdb_log_level) 
    {
        return;
    }

    memset (log_entry, 0, sizeof (log_entry));
    snprintf (log_entry, LOG_MSG_SIZE - 1, 
              "%llu P:%d C:%d Get-Next-Changed Tab:%s rc:%d\n", 
              upTimeMillisecondsGet(),
              pid,
              client_id,
              table_name,
              err_code);
    log_entry [LOG_MSG_SIZE - 2] = '\n'; /* Make sure we got a new-line */

    syncdbEventLog (log_entry);
}

