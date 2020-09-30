/*********************************************************************
 *
 * Copyright: (c) 2017 Broadcom.
 * Broadcom Proprietary and Confidential. All rights reserved.
 *
 *********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/select.h>
#include <dlfcn.h>

#include <brcm_syncdb_api.h>
#include <brcm_syncdb_msg.h>

/* Socket ID used to communicate with the syncdb process.
*/
static int syncdb_socket = -1;

/* dyncdb UNIX domain socket info.
*/
struct sockaddr_un syncdb_server;

/* When this code is linked with the pthread library, these functions are
** used to lock and unlock the mutex.
*/
static int (*func_pthread_mutex_lock)() = 0;
static int (*func_pthread_mutex_unlock)() = 0;
static int (*func_pthread_mutex_init)() = 0;

/* Flag indicating whether pThreads are supported.
*/
static int syncdb_pthread = 0;

/*********************************************************************
*
* @purpose Lock syncdb Client semaphore.
* 
* @param   client - SyncDB Client Handle
*
* @returns None
*
* @notes   None
*
* @end
*
*********************************************************************/
static void syncdb_sema_take (syncdbClientHandle_t *client)
{
  int rc;

  if (!syncdb_pthread)
  {
      return;
  }

  rc = func_pthread_mutex_lock (&client->client_lock);
  if (rc < 0)
  {
      perror ("pthread_mutex_lock()");
      abort ();
  }
}

/*********************************************************************
*
* @purpose Unlock syncdb Client semaphore.
* 
* @param   client - SyncDB Client Handle
*
* @returns None
*
* @notes   None
*
* @end
*
*********************************************************************/
static void syncdb_sema_give (syncdbClientHandle_t *client)
{
    int rc;

    if (!syncdb_pthread)
    {
        return;
    }

    rc = func_pthread_mutex_unlock (&client->client_lock);
    if (rc < 0)
    {
        perror ("pthread_mutex_unlock()");
        abort ();
    }
}

/*********************************************************************
*
* @purpose Create syncdb Client semaphore.
* 
* @param   client - SyncDB Client Handle
*
* @returns None
*
* @notes   None
*
* @end
*
*********************************************************************/
static void syncdb_sema_create (syncdbClientHandle_t *client)
{
    int rc;

    if (!syncdb_pthread)
    {
        return;
    }

    rc = func_pthread_mutex_init (&client->client_lock, 0);
    if (rc < 0)
    {
        perror ("pthread_mutex_init");
        abort ();
    }
}

/*********************************************************************
*
* @purpose Determine whether the code is linked with pthreads
*          and resolve pthread mutex function pointers if needed.
*
* @param   none
*
* @returns None
*
* @notes   
*
* @end
*
*********************************************************************/
static int syncdb_pthread_check (void)
{
    func_pthread_mutex_init = dlsym (RTLD_NEXT, "pthread_mutex_init");
    func_pthread_mutex_lock = dlsym (RTLD_NEXT, "pthread_mutex_lock");
    func_pthread_mutex_unlock = dlsym (RTLD_NEXT, "pthread_mutex_unlock");

    if (!func_pthread_mutex_init ||
        !func_pthread_mutex_lock ||
        !func_pthread_mutex_unlock
        )
    {
        return 0;
    }

    return 1;
}

/*********************************************************************
* @purpose  Send a message to the syncdb server.
*           
* @param    socket - Socket ID on which to send this message.
* @param    server - Message destination.
* @param    msg  - Message to send to the client
* @param    msg_size  - Message size 
*
* @returns  0 - Success
* @returns  -1 - Send Failed
*
* @notes  
*       
* @end
*********************************************************************/
static int syncdb_msg_send (int socket,
                             struct sockaddr_un *server,
                             syncdbCmdMsg_t *msg,
                             int msg_size
                             )
{
  int rc;

  do
  {
    rc = sendto (socket,
                 msg,
                 msg_size,
                 0,
                 (struct sockaddr *) server,
                 sizeof (struct sockaddr_un)
                 );
    if ((rc < 0) && (errno != EINTR))
    {
     perror("syncdb_msg_send - sendto - Retry in 1 second...");
     sleep (1);
     rc = sendto (socket,
                 msg,
                 msg_size,
                 0,
                 (struct sockaddr *) server,
                 sizeof (struct sockaddr_un)
                 );
    }
  } while ((rc < 0) && (errno == EINTR));

  return rc;
}

/*********************************************************************
* @purpose  Receive a message from the syncdb server.
*           
* @param    socket - Socket ID on which to send this message.
* @param    msg  - Message to send to the client
* @param    msg_size  - Message size 
*
* @returns  0 - Success
* @returns  -1 - Receive Failed
*
* @notes  
*       
* @end
*********************************************************************/
static int syncdb_msg_receive (int socket,
                             syncdbCmdMsg_t *msg,
                             int msg_size
                             )
{
  int rc;
  
  /*
  ** If syncdb dies then all clients are killed anyway, so don't need 
  ** to worry about hanging on recvfrom(). 
  */ 
  do
  {
    rc = recvfrom (socket, msg, msg_size, 0, 0, 0);
  } while ((rc < 0) && (errno == EINTR));

  return rc;
}

/*********************************************************************
* @purpose  Register a new client with the syncdb.
*           
* @param    client_name (Input) - Descriptive name for this client.
* @param    client_id (Output) - ID used on subsequent transactions.
*           The client_id contains the command socket, the notification
*           socket and unique syncdb client ID.
*
* @returns  SYNCDB_OK - Client is registered.
* @returns  SYNCDB_ERROR - Can't create socket.
* @returns  SYNCDB_MAX_CLIENTS - Too many clients already registered.
*
* @notes   There is no corresponding function to de-register clients.
*          The syncdb automatically deregisters clients whose process
*          is terminated.
* 
*          The client_id.client_id returned parameter is a unique
*          identifier for the client within the system. The client ID
*          is an integer between 0 and 65535. The client IDs are
*          assigned in the incrementing order. When the maximum value
*          is reached the client ID is wrapped to 0. This means that
*          client IDs are not reused immediately.
* 
*          The list of active clients 
*          is maintained by syncdb in the internal AVL
*          table "syncdb-client-table". The applications can register
*          to be notified about changes to this table and perform
*          Get/GetNext/GetNextChanged operations on that table.
*          The client table in conjunction with the client ID re-use
*          policy allows applications to detect when any other application
*          fails.
*       
*          Multiple clients may register with the same client_name.
* 
*          The function may be called from a process or a pthread.
*          Multiple registrations by the same process or by the same
*          pthread are allowed.
* 
*          The return client handle includes socket descriptors.
*          The returned client handle can only be used by the process 
*          that registered with the syncdb. A pthread can pass the
*          handle to another pthread, but only one pthread can invoke
*          a syncdb API call on the same client handle at any one time.
* 
* @end
*********************************************************************/
int syncdbClientRegister (char *client_name,
                          syncdbClientHandle_t  *client_id, char *path)
{
 int rc;
 int fd1, fd2;
 syncdbCmdMsg_t msg, reply;
 struct sockaddr_un server;
 static int pthread_check_done = 0;
 char sock_path[SYNCDB_FILE_PATH_BUFF_SIZE];

 if (!pthread_check_done)
 {
     syncdb_pthread = syncdb_pthread_check ();
     pthread_check_done = 1;
 }

 memset (client_id, 0, sizeof (syncdbClientHandle_t));
 syncdb_sema_create(client_id);

 /* Create the socket to send messages to the syncdb process.
 */
 syncdb_socket = socket (AF_UNIX, SOCK_DGRAM, 0);
 if (syncdb_socket < 0)
 {
   return SYNCDB_ERROR;
 }

 /* Create socket to receive messages from the syncdb Process.
 */
 fd1 = socket (AF_UNIX, SOCK_DGRAM, 0);
 if (fd1 < 0)
 {
   return SYNCDB_ERROR;
 }

 /* Create socket to receive table change notifications from the syncdb Process.
 */
 fd2 = socket (AF_UNIX, SOCK_DGRAM, 0);
 if (fd2 < 0)
 {
   close (fd1);
   return SYNCDB_ERROR;
 }

 memset (&msg, 0, sizeof (msg));

 msg.msg.registerMsg.pid = getpid();

 /* Construct the socket name files.
 */
 sprintf (msg.msg.registerMsg.client_socket_name,
          "%s/sockets/client-cmd-sock-%d-%d", path,
          msg.msg.registerMsg.pid, fd1);
 sprintf (msg.msg.registerMsg.client_notify_socket_name,
          "%s/sockets/client-notify-sock-%d-%d", path,
          msg.msg.registerMsg.pid, fd2);

 /* Bind to the client sockets.
 */
 memset(&server, 0, sizeof(struct sockaddr_un));
 server.sun_family = AF_UNIX;
 strncpy(server.sun_path, msg.msg.registerMsg.client_socket_name, sizeof (server.sun_path) - 1);

 if(bind(fd1, (const struct sockaddr *) &server, sizeof(server)) < 0)
 {
  close(fd1);
  close(fd2);
  perror("bind-1");
  return SYNCDB_ERROR;
 }

 memset(&server, 0, sizeof(struct sockaddr_un));
 server.sun_family = AF_UNIX;
 strncpy(server.sun_path, msg.msg.registerMsg.client_notify_socket_name,
         sizeof (server.sun_path) - 1);

 if(bind(fd2, (const struct sockaddr *) &server, sizeof(server)) < 0)
 {
  close(fd1);
  close(fd2);
  perror("bind-2");
  return SYNCDB_ERROR;
 }

 /* Send the Client Registration message to syncdb
 */

 memset(&syncdb_server, 0, sizeof(struct sockaddr_un));
 syncdb_server.sun_family = AF_UNIX;
 sprintf(sock_path, "%s%s%s", path, "/sockets", SYNCDB_SERVER_SOCKET);
 strcpy(syncdb_server.sun_path, sock_path);

 msg.message_type = SYNCDB_CLIENT_REGISTER;
 snprintf (msg.msg.registerMsg.client_description, 
           sizeof (msg.msg.registerMsg.client_description) - 1,
           "%s",
           client_name
           );

 syncdb_sema_take (client_id);
 rc = syncdb_msg_send(syncdb_socket, &syncdb_server, &msg, sizeof(msg));
 if (rc < 0) 
 {
     perror("sendto");
     close(fd1);
     close(fd2);
     (void) unlink (msg.msg.registerMsg.client_socket_name);
     (void) unlink (msg.msg.registerMsg.client_notify_socket_name);
     syncdb_sema_give (client_id);
     return SYNCDB_ERROR;
 }


 /* Receive reply from the syncdb.
 */
 rc = syncdb_msg_receive (fd1, &reply, sizeof(reply));
 syncdb_sema_give (client_id);
 if (rc < 0) 
 {
     perror("recvfrom");
     close(fd1);
     close(fd2);
     (void) unlink (msg.msg.registerMsg.client_socket_name);
     (void) unlink (msg.msg.registerMsg.client_notify_socket_name);
     return SYNCDB_ERROR;
 }

 if (reply.rc != SYNCDB_OK) 
 {
     close(fd1);
     close(fd2);
     (void) unlink (msg.msg.registerMsg.client_socket_name);
     (void) unlink (msg.msg.registerMsg.client_notify_socket_name);
     return SYNCDB_MAX_CLIENTS;
 }

 client_id->cmd_socket = fd1;
 client_id->notify_socket = fd2;
 client_id->client_id = reply.client_id;


 return SYNCDB_OK;
}

/*********************************************************************
* @purpose  Check whether any change notifications are pending
*           for this client.
*           
* @param    client_id (input) - Handle returned by syncdbClientRegister()
* @param    timeout_secs (input) - How many seconds to wait.
*                       0 - Do not wait. Return immediately.
*                       < 0 - Wait forever.
*                       > 0 - Wait specified number of seconds.
*
* @returns  SYNCDB_OK - Change notifications are pending.
* @returns  SYNCDB_ERROR - No pending notifications.
*
* @notes    This function checks the event socket and returns
*           immediately or waits for an event.

*           If the user application needs to handle other socket
*           events then it can explicitely add the
*           client_id->notify_socket to the select() statement which
*           it uses to wait for other sockets.
*           The syncdbTableChaneCheck() can then be called with
*           0 timeout to check for events and drain the event
*           socket.
* 
*           If the process receives a signal, and the wait is
*           with a timeout then the function exits.
* 
*           If the wait is "forever" then signals do not
*           cause the function to exit.
* 
* @end
*********************************************************************/
int syncdbTableChangeCheck (syncdbClientHandle_t  *client_id,
                          int  timeout_secs)
{
  struct timeval tv;
  fd_set rcv_set;
  int rc;
  char buf[1];

  if (!client_id)
  {
      return SYNCDB_ERROR;
  }


  do
  {
      FD_ZERO (&rcv_set);
      FD_SET (client_id->notify_socket, &rcv_set);

      if (timeout_secs >= 0)
      {
          tv.tv_sec = timeout_secs;
          tv.tv_usec = 0;
          rc = select (client_id->notify_socket + 1,&rcv_set, 0, 0, &tv);
          break;
      } else if (timeout_secs < 0)
      {
          rc = select (client_id->notify_socket + 1,&rcv_set, 0, 0, 0);
          if ((rc < 0) && (errno == EINTR))
          {
              continue;
          }
          break;
      } 
  } while (1);

  /* If data is detected on the socket then drain the socket.
  */
  if (rc > 0)
  {
      do
      {
          rc = recvfrom (client_id->notify_socket, 
                         buf, sizeof(buf), MSG_DONTWAIT, 0, 0);
      } while (rc >= 0);
      return SYNCDB_OK;
  }

  return SYNCDB_ERROR;
}

/*********************************************************************
* @purpose  Notify this client about changes to the specified table.
*           
* @param    client_id (input) - Handle returned by syncdbClientRegister()
* @param    table_name  - Unique name for the table.
*
* @returns  SYNCDB_OK - Table notification is enabled.
* @returns  SYNCDB_ERROR - Table notification is not enabled.
* @returns  SYNCDB_NO_NAME - Specified table does not exist.
*
* @notes    The function works for every table type.
*           Note that there is no API to disable table notifications.
*           When a process that created the client ID dies, the
*           syncdb automatically disables table change notifications
*           for that client.
* 
*           The table changes made after the registration cause events to
*           be generated to the client. To make sure that the client
*           does not miss any events, the client should read the
*           content of whole table after registering for
*           notifications.
* 
*           The change notifications are NOT sent to the client which
*           is making the table change.
* 
* @end
*********************************************************************/
int syncdbTableChangeNotify (syncdbClientHandle_t  *client_id,
                          char  *table_name)
{
    syncdbCmdMsg_t msg, reply;
    int rc;

    if (!client_id || !table_name) 
    {
        return SYNCDB_ERROR;
    }

    memset (&msg, 0, sizeof (msg));
    msg.message_type = SYNCDB_TABLE_CHANGE_NOTIFY;
    msg.client_id = client_id->client_id;
    strncpy (msg.msg.tableChangeNotifyMsg.table_name,
             table_name,
             SYNCDB_TABLE_NAME_SIZE-1);

    syncdb_sema_take (client_id);
    rc = syncdb_msg_send(syncdb_socket, &syncdb_server, &msg, sizeof(msg));
    if (rc < 0) 
    {
        syncdb_sema_give (client_id);
        return SYNCDB_ERROR;
    }

    rc = syncdb_msg_receive (client_id->cmd_socket, &reply, sizeof(reply));
    syncdb_sema_give (client_id);
    if (rc < 0) 
    {
        return SYNCDB_ERROR;
    }

    return reply.rc;
}

/*********************************************************************
* @purpose  Get client information for the specified client ID.
*           
* @param    client_id (input) - Handle returned by syncdbClientRegister()
* @param    client_status (input/output)  - Information about this client.
*
* @returns  SYNCDB_OK - Status is returned.
* @returns  SYNCDB_ERROR - The status request failed.
* @returns  SYNCDB_NOT_FOUND - The client does not exist.
*
* @notes    The caller must set the client_status->client_id to the
*           syncdb client ID for which to retrieve status.
* 
* @end
*********************************************************************/
int syncdbClientStatusGet (syncdbClientHandle_t  *client_id,
                          syncdbClientStatus_t *client_status)
{
    unsigned char *buf;
    syncdbCmdMsg_t *msg, *reply;
    int rc;
    syncdbClientStatus_t *status;

    if (!client_id || !client_status) 
    {
        return SYNCDB_ERROR;
    }

    buf = malloc(SYNCDB_MSG_MAX_SIZE);
    msg = (syncdbCmdMsg_t *) buf;
    reply = (syncdbCmdMsg_t *) buf;

    memset (msg, 0, sizeof (syncdbCmdMsg_t));
    msg->message_type = SYNCDB_CLIENT_STATUS;
    msg->client_id = client_id->client_id;
    status = (syncdbClientStatus_t *) &buf[sizeof(syncdbCmdMsg_t)];
    status->client_id = client_status->client_id; 

    syncdb_sema_take (client_id);
    rc = syncdb_msg_send(syncdb_socket, &syncdb_server, msg, 
                         sizeof(syncdbCmdMsg_t) + sizeof (syncdbClientStatus_t));
    if (rc < 0) 
    {
        free (buf);
        syncdb_sema_give (client_id);
        return SYNCDB_ERROR;
    }

    rc = syncdb_msg_receive (client_id->cmd_socket, reply, 
                             sizeof(syncdbCmdMsg_t) + sizeof(syncdbClientStatus_t));
    syncdb_sema_give (client_id);
    if (rc < 0) 
    {
        free (buf);
        return SYNCDB_ERROR;
    }

    status = (syncdbClientStatus_t *) &buf[sizeof(syncdbCmdMsg_t)];
    if (reply->rc == SYNCDB_OK)
    {
        memcpy (client_status, status, sizeof (syncdbClientStatus_t));
    }

    rc = reply->rc;
    free (buf);
    return rc;
}

/*********************************************************************
* @purpose  Get Table Status as it pertains to the specified client.
*           
* @param    client_id (input) - Handle returned by syncdbClientRegister()
* @param    num_tables  - Number of tables in the list.
* @param    table_list  - List of tables. 
*
* @returns  SYNCDB_OK - Status is returned.
* @returns  SYNCDB_ERROR - The status request failed.
* @returns  SYNCDB_SIZE - Too many tables in the table_list.
*
* @notes    The SYNCDB_OK means that the status command returned
*           information. The return code should not be used as an
*           indication that specified table or tables exist. The caller
*           needs to check the status for each table to see if
*           it is present.
* 
*           The maximum size of the table_list array must be less
*           or equal to SYNCDB_RECORD_MAX_SIZE. Otherwise the
*           function returns the SYNCDB_SIZE error.
* 
* @end
*********************************************************************/
int syncdbTableStatusGet (syncdbClientHandle_t  *client_id,
                          int num_tables,
                          syncdbDataTableStatus_t *table_list)
{
    unsigned char *buf;
    syncdbCmdMsg_t *msg, *reply;
    int rc;
    syncdbDataTableStatus_t *table_status;
    int i;

    if (!client_id || !num_tables || !table_list) 
    {
        return SYNCDB_ERROR;
    }

    if ((num_tables * sizeof(syncdbDataTableStatus_t)) > SYNCDB_RECORD_MAX_SIZE)
    {
        return SYNCDB_SIZE;
    }

    buf = malloc (SYNCDB_MSG_MAX_SIZE);
    msg = (syncdbCmdMsg_t *) buf;
    reply = (syncdbCmdMsg_t *) buf;

    memset (msg, 0, sizeof (syncdbCmdMsg_t));
    msg->message_type = SYNCDB_TABLE_STATUS;
    msg->client_id = client_id->client_id;
    msg->msg.tableStatusGetMsg.num_tables = num_tables;

    table_status = (syncdbDataTableStatus_t *) &buf[sizeof(syncdbCmdMsg_t)];
    for (i = 0; i < num_tables; i++)
    {
        memset (table_status, 0, sizeof (syncdbDataTableStatus_t));
        strncpy (table_status->table_name,
                 table_list[i].table_name,
                 SYNCDB_TABLE_NAME_SIZE-1);
        table_status++;
    }

    syncdb_sema_take (client_id);
    rc = syncdb_msg_send(syncdb_socket, &syncdb_server, msg, 
                         sizeof(syncdbCmdMsg_t) + (num_tables * sizeof(syncdbDataTableStatus_t)));
    if (rc < 0) 
    {
        free (buf);
        syncdb_sema_give (client_id);
        return SYNCDB_ERROR;
    }

    rc = syncdb_msg_receive (client_id->cmd_socket, reply, 
                             sizeof(syncdbCmdMsg_t) + (num_tables * sizeof(syncdbDataTableStatus_t)));
    syncdb_sema_give (client_id);
    if (rc < 0) 
    {
        free (buf);
        return SYNCDB_ERROR;
    }

    table_status = (syncdbDataTableStatus_t *) &buf[sizeof(syncdbCmdMsg_t)];
    if (reply->rc == SYNCDB_OK)
    {
        for (i = 0; i < num_tables; i++)
        {
            table_list[i].table_status = table_status->table_status;
            table_list[i].table_version = table_status->table_version;
            table_list[i].table_type = table_status->table_type;
            table_list[i].num_elements = table_status->num_elements;
            table_list[i].num_non_deleted_elements = table_status->num_non_deleted_elements;
            table_status++;
        }
    }

    rc = reply->rc;
    free (buf);
    return rc;
}

/*********************************************************************
* @purpose  Store specified table or all tables into the file system.
*           
* @param    client_id (input) - Handle returned by syncdbClientRegister()
* @param    table_name  - Name of the table to be written to the file.
*                         NULL - Store all tables to the file system.
* @param    nvram - 1 - Save tables in NVRAM.
*                   0 - Save tables only in the RAM disk.
*
* @returns  SYNCDB_OK - Specified table(s) is written to file.
* @returns  SYNCDB_ERROR - Error.
* @returns  SYNCDB_NO_TABLE - Specified table does not exist.
*
* @notes   Only storable tables are written to the file system.
*          The storable tables are created with the SYNCDB_TABLE_FLAG_STORABLE
*          property.
*          If the "nvram" flag is set to 1, then tables with the
*          SYNCDB_TABLE_FLAG_NVRAM are saved in NVRAM.
* 
* @end
*********************************************************************/
int syncdbTableStore (syncdbClientHandle_t  *client_id,
                          char *table_name,
                          unsigned int nvram)
{
    syncdbCmdMsg_t msg, reply;
    int rc;

    if (!client_id) 
    {
        return SYNCDB_ERROR;
    }

    memset (&msg, 0, sizeof (syncdbCmdMsg_t));
    msg.message_type = SYNCDB_TABLE_STORE;
    msg.client_id = client_id->client_id;
    if (!table_name)
    {
        msg.msg.tableStoreMsg.all_tables = 1;
    } else
    {
        strncpy (msg.msg.tableDeleteMsg.table_name,
                 table_name,
                 SYNCDB_TABLE_NAME_SIZE-1);
    }
    if (nvram)
    {
        msg.msg.tableStoreMsg.nvram = 1;
    }


    syncdb_sema_take (client_id);
    rc = syncdb_msg_send(syncdb_socket, &syncdb_server, &msg, 
                         sizeof(syncdbCmdMsg_t));
    if (rc < 0) 
    {
        syncdb_sema_give (client_id);
        return SYNCDB_ERROR;
    }

    rc = syncdb_msg_receive (client_id->cmd_socket, &reply, 
                             sizeof(syncdbCmdMsg_t));
    syncdb_sema_give (client_id);
    if (rc < 0) 
    {
        return SYNCDB_ERROR;
    }

    return reply.rc;
}

/*********************************************************************
* @purpose  Tell SyncDB that it should start or stop data sync
*           with the backup manager.
*           
* @param    client_id (input) - Handle returned by syncdbClientRegister()
* @param    sync_mode - 1 - Enable Sync. 0 - Disable Sync.
* @param    max_msg_size - Maximum number of bytes in the sync message.
*
* @returns  SYNCDB_OK - Command is successful.
* @returns  SYNCDB_ERROR - Error.
*
* @notes    
* 
* @end
*********************************************************************/
int syncdbNsfModeSet (syncdbClientHandle_t  *client_id,
                          unsigned int sync_mode,
                          unsigned int max_msg_size)
{
    syncdbCmdMsg_t msg, reply;
    int rc;

    if (!client_id) 
    {
        return SYNCDB_ERROR;
    }

    memset (&msg, 0, sizeof (syncdbCmdMsg_t));
    msg.message_type = SYNCDB_NSF_SYNC_ENABLE;
    msg.client_id = client_id->client_id;
    msg.msg.nsfSyncMsg.sync_mode = sync_mode;
    msg.msg.nsfSyncMsg.max_sync_msg_size = max_msg_size;

    syncdb_sema_take (client_id);
    rc = syncdb_msg_send(syncdb_socket, &syncdb_server, &msg, 
                         sizeof(syncdbCmdMsg_t));
    if (rc < 0) 
    {
        syncdb_sema_give (client_id);
        return SYNCDB_ERROR;
    }

    rc = syncdb_msg_receive (client_id->cmd_socket, &reply, 
                             sizeof(syncdbCmdMsg_t));
    syncdb_sema_give (client_id);
    if (rc < 0) 
    {
        return SYNCDB_ERROR;
    }

    return reply.rc;
}


/*********************************************************************
* @purpose  Delete specified table or all tables.
*           
* @param    client_id (input) - Handle returned by syncdbClientRegister()
* @param    table_name  - Name of the table to be deleted.
*
* @returns  SYNCDB_OK - Specified table is deleted.
* @returns  SYNCDB_ERROR - Error.
* @returns  SYNCDB_NO_TABLE - Specified table does not exist.
*
* @notes    
* 
* @end
*********************************************************************/
int syncdbTableDelete (syncdbClientHandle_t  *client_id,
                          char *table_name)
{
    syncdbCmdMsg_t msg, reply;
    int rc;

    if (!client_id || !table_name) 
    {
        return SYNCDB_ERROR;
    }

    memset (&msg, 0, sizeof (syncdbCmdMsg_t));
    msg.message_type = SYNCDB_TABLE_DELETE;
    msg.client_id = client_id->client_id;
    strncpy (msg.msg.tableDeleteMsg.table_name,
             table_name,
             SYNCDB_TABLE_NAME_SIZE-1);

    syncdb_sema_take (client_id);
    rc = syncdb_msg_send(syncdb_socket, &syncdb_server, &msg, 
                         sizeof(syncdbCmdMsg_t));
    if (rc < 0) 
    {
        syncdb_sema_give (client_id);
        return SYNCDB_ERROR;
    }

    rc = syncdb_msg_receive (client_id->cmd_socket, &reply, 
                             sizeof(syncdbCmdMsg_t));
    syncdb_sema_give (client_id);
    if (rc < 0) 
    {
        return SYNCDB_ERROR;
    }

    return reply.rc;
}

/*********************************************************************
* @purpose  Create AVL Table.
*           
* @param    client_id (input) - Handle returned by syncdbClientRegister()
* @param    table_name  - Unique name for this table.
* @param    table_version - Version of this table. 
* @param    max_elements  - Maximum number of nodes in the AVL tree.
* @param    max_live_elements  - Maximum number of nodes in the AVL tree
*                         that are not pending for removal. This value must
*                         be greater than 0 and less or equal to max_elements.
* @param    node_size  - User data number of bytes in each table element.
*                        The size includes the key field.
* @param    key_size  - Number of bytes in the key element. The first
*                       'key_size' bytes are treated as the key.
* @param    flags - Table Creation Flags
*                    SYNCDB_TABLE_FLAG_STORABLE - Schema is present.
*                    SYNCDB_TABLE_FLAG_FILE_LOAD - Load the table from file if file exists.
*                    SYNCDB_TABLE_FLAG_NVRAM - Copy to NVRAM when storing the table.
* @param    schema  - JSON schema create using the syncdbUtilSchemaCreate().
*                     Must be set to 0 for non-storable tables. Must be non-zero
*                     for storable tables.
* @param    schema_size  - JSON schema size returned by the syncdbUtilSchemaCreate().
*                      Must be 0 for non-storable tables.
*
* @returns  SYNCDB_OK - The table is created.
* @returns  SYNCDB_ERROR - Can't create the table.
* @returns  SYNCDB_DUPNAME - The table is not created because another
*                            table already exists with the same name.
*
* @notes   
* 
* @end
*********************************************************************/
int syncdbAvlTableCreate (syncdbClientHandle_t  *client_id,
                          char  *table_name,
                          unsigned int   table_version,
                          unsigned int   max_elements,
                          unsigned int   max_live_elements,
                          unsigned int   node_size,
                          unsigned int   key_size,
                          unsigned int   flags,
                          char * schema,
                          unsigned int   schema_size)
{
    unsigned char *buf;
    syncdbCmdMsg_t *msg;
    syncdbCmdMsg_t reply;
    int rc;

    if (!client_id || !table_name || (syncdb_socket < 0)) 
    {
        return SYNCDB_ERROR;
    }

    if ((max_elements == 0) || (max_live_elements == 0) || 
        (max_live_elements > max_elements)) 
    {
        return SYNCDB_ERROR;
    }

    if ((node_size == 0) || (key_size == 0) ||
        (key_size > node_size))
    {
        return SYNCDB_ERROR;
    }

    if (flags & SYNCDB_TABLE_FLAG_STORABLE)
    {
        if (!schema || !schema_size || (schema_size > SYNCDB_JSON_MAX_SCHEMA_SIZE))
        {
            return SYNCDB_ERROR;
        }
    } else
    {
        if (schema || schema_size || 
            (flags & SYNCDB_TABLE_FLAG_FILE_LOAD) ||
            (flags & SYNCDB_TABLE_FLAG_NVRAM))
        {
            return SYNCDB_ERROR;
        }
    }

    buf = malloc (SYNCDB_MSG_MAX_SIZE);
    msg = (syncdbCmdMsg_t *) buf;
    memset (msg, 0, sizeof (syncdbCmdMsg_t));
    msg->message_type = SYNCDB_AVL_TABLE_CREATE;
    msg->client_id = client_id->client_id;
    strncpy (msg->msg.tableCreateMsg.table_name,
             table_name,
             SYNCDB_TABLE_NAME_SIZE-1);
    msg->msg.tableCreateMsg.key_size = key_size;
    msg->msg.tableCreateMsg.node_size = node_size;
    msg->msg.tableCreateMsg.max_elements = max_elements;
    msg->msg.tableCreateMsg.max_live_elements = max_live_elements;
    msg->msg.tableCreateMsg.flags = flags;
    msg->msg.tableCreateMsg.schema_size = schema_size;
    msg->msg.tableCreateMsg.table_version = table_version;

    /* If schema is present then copy it into the message.
    */
    if (schema_size)
    {
        memcpy (&buf[sizeof(syncdbCmdMsg_t)], schema, schema_size);
    }

    syncdb_sema_take (client_id);
    rc = syncdb_msg_send(syncdb_socket, &syncdb_server, msg, 
                         sizeof(syncdbCmdMsg_t) + schema_size);
    if (rc < 0) 
    {
        free (buf);
        syncdb_sema_give (client_id);
        return SYNCDB_ERROR;
    }

    rc = syncdb_msg_receive (client_id->cmd_socket, &reply, sizeof(reply));
    syncdb_sema_give (client_id);
    if (rc < 0) 
    {
        free (buf);
        return SYNCDB_ERROR;
    }

    free (buf);
    return reply.rc;
}

/*********************************************************************
* @purpose  Create a Record Table.
*           
* @param    client_id (input) - Handle returned by syncdbClientRegister()
* @param    table_name  - Unique name for this table.
* @param    table_version - Version of this table. 
* @param    node_size  - User data number of bytes in the table element.
* @param    flags - Table Creation Flags
*                    SYNCDB_TABLE_FLAG_STORABLE - Schema is present.
*                    SYNCDB_TABLE_FLAG_FILE_LOAD - Load the table from file if file exists.
*                    SYNCDB_TABLE_FLAG_NVRAM - Copy to NVRAM when storing the table.
* @param    schema  - JSON schema create using the syncdbUtilSchemaCreate().
*                     Must be set to 0 for non-storable tables. Must be non-zero
*                     for storable tables.
* @param    schema_size  - JSON schema size returned by the syncdbUtilSchemaCreate().
*                      Must be 0 for non-storable tables.
*
* @returns  SYNCDB_OK - The table is created.
* @returns  SYNCDB_ERROR - Can't create the table.
* @returns  SYNCDB_DUPNAME - The table is not created because another
*                            table already exists with the same name.
*
* @notes   
* 
* @end
*********************************************************************/
int syncdbRecordTableCreate (syncdbClientHandle_t  *client_id,
                          char  *table_name,
                          unsigned int   table_version,
                          unsigned int   node_size,
                          unsigned int   flags,
                          char * schema,
                          unsigned int   schema_size)
{
    unsigned char *buf;
    syncdbCmdMsg_t *msg;
    syncdbCmdMsg_t reply;
    int rc;

    if (!client_id || !table_name || (syncdb_socket < 0)) 
    {
        return SYNCDB_ERROR;
    }

    if (node_size == 0) 
    {
        return SYNCDB_ERROR;
    }

    if (flags & SYNCDB_TABLE_FLAG_STORABLE)
    {
        if (!schema || !schema_size || (schema_size > SYNCDB_JSON_MAX_SCHEMA_SIZE))
        {
            return SYNCDB_ERROR;
        }
    } else
    {
        if (schema || schema_size || 
            (flags & SYNCDB_TABLE_FLAG_FILE_LOAD) ||
            (flags & SYNCDB_TABLE_FLAG_NVRAM))
        {
            return SYNCDB_ERROR;
        }
    }

    buf = malloc (SYNCDB_MSG_MAX_SIZE);
    msg = (syncdbCmdMsg_t *) buf;
    memset (msg, 0, sizeof (syncdbCmdMsg_t));
    msg->message_type = SYNCDB_RECORD_TABLE_CREATE;
    msg->client_id = client_id->client_id;
    strncpy (msg->msg.tableCreateMsg.table_name,
             table_name,
             SYNCDB_TABLE_NAME_SIZE-1);
    msg->msg.tableCreateMsg.node_size = node_size;
    msg->msg.tableCreateMsg.flags = flags;
    msg->msg.tableCreateMsg.schema_size = schema_size;
    msg->msg.tableCreateMsg.table_version = table_version;

    /* If schema is present then copy it into the message.
    */
    if (schema_size)
    {
        memcpy (&buf[sizeof(syncdbCmdMsg_t)], schema, schema_size);
    }

    syncdb_sema_take (client_id);
    rc = syncdb_msg_send(syncdb_socket, &syncdb_server, msg, 
                         sizeof(syncdbCmdMsg_t) + schema_size);
    if (rc < 0) 
    {
        free (buf);
        syncdb_sema_give (client_id);
        return SYNCDB_ERROR;
    }

    rc = syncdb_msg_receive (client_id->cmd_socket, &reply, sizeof(reply));
    syncdb_sema_give (client_id);
    if (rc < 0) 
    {
        free (buf);
        return SYNCDB_ERROR;
    }

    free (buf);
    return reply.rc;
}

/*********************************************************************
* @purpose  Send a Set/Insert/Delete Command to syncdb.
*           
* @param    client_id (input) - Handle returned by syncdbClientRegister()
* @param    table_name  - Unique name for this table.
* @param    element  - Pointer to the data element to be inserted.
* @param    size  - Number of bytes in the element.
* @param    field_offset  - Field offset for the syncdbFieldSet() command.
* @param    field_size  - field size for the syncdbFieldSet() command.
* @param    command  - The type of command.
*
* @returns  SYNCDB_OK - Command is successful.
* @returns  SYNCDB_ERROR - Unspecified error.
* @returns  Other command-specific error codes.
*
* @notes   This utility function is used by Set/Insert/Delete APIs.
*          See the API description for the exact error code list.
* 
* @end
*********************************************************************/
static int syncdb_generic_set (syncdbClientHandle_t  *client_id,
                          char  *table_name,
                          void *element,
                          unsigned int   size,
                          unsigned int field_offset,
                          unsigned int field_size,
                          int command)
{
  unsigned char *buf;
  syncdbCmdMsg_t *msg;
  void *element_ptr;
  syncdbCmdMsg_t reply;
  int rc;

    if (!client_id || !table_name || !element) 
    {
        return SYNCDB_ERROR;
    }

    if (!size) 
    {
        return SYNCDB_ERROR;
    }

    buf = malloc(SYNCDB_MSG_MAX_SIZE);
    msg = (syncdbCmdMsg_t *) buf;
    element_ptr = buf + sizeof (syncdbCmdMsg_t);
    memset (msg, 0, sizeof (syncdbCmdMsg_t));
    msg->client_id = client_id->client_id;
    msg->message_type = command;
    strncpy (msg->msg.genericSetMsg.table_name,
             table_name,
             SYNCDB_TABLE_NAME_SIZE-1);
    msg->msg.genericSetMsg.size = size;
    memcpy (element_ptr, element, size);
    msg->msg.genericSetMsg.field_offset = field_offset;
    msg->msg.genericSetMsg.field_size = field_size;

    syncdb_sema_take (client_id);
    rc = syncdb_msg_send(syncdb_socket, &syncdb_server, 
                         msg, sizeof(syncdbCmdMsg_t) + size);
    if (rc < 0) 
    {
        free (buf);
        syncdb_sema_give (client_id);
        return SYNCDB_ERROR;
    }

    rc = syncdb_msg_receive (client_id->cmd_socket, &reply, sizeof(reply));
    syncdb_sema_give (client_id);
    if (rc < 0) 
    {
        free (buf);
        return SYNCDB_ERROR;
    }

    free (buf);
    return reply.rc;
}


/*********************************************************************
* @purpose  Insert a new entry into the AVL table.
*           
* @param    client_id (input) - Handle returned by syncdbClientRegister()
* @param    table_name  - Unique name for this table.
* @param    element  - Pointer to the data element to be inserted.
* @param    size  - Number of bytes in the element.
*
* @returns  SYNCDB_OK - Entry is inserted.
* @returns  SYNCDB_ERROR - Unspecified error.
* @returns  SYNCDB_NO_TABLE - Specified table does not exist.
* @returns  SYNCDB_FULL - Specified table is already full.
* @returns  SYNCDB_SIZE - The specified size is invalid.
* @returns  SYNCDB_ENTRY_EXISTS - The specified entry already exists and
*                           the SYNCDB_TABLE_FLAG_EXISTS is enabled for
*                           the table.
*
* @notes   The Insert operation is supported only on AVL trees.
*          If the specified table is not an AVL then
*          the SYCNDB_ERROR error code is returned.
* 
*          The 'size' must be exactly the same as the
*          record size specified on table creation.
*          If the size is invalid then SYNCDB_SIZE error code
*          is returned.
* 
*          If the element with the same key already exists in 
*          the tree then the existing element is updated with the new content.
*          If the table is created with the SYNCDB_TABLE_FLAG_EXISTS then
*          the syncdbInsert() fails for an entry with the duplicate
*          key and returns the SYNCDB_ENTRY_EXISTS error.
* 
* @end
*********************************************************************/
int syncdbInsert (syncdbClientHandle_t  *client_id,
                          char  *table_name,
                          void *element,
                          unsigned int   size)
{
    return syncdb_generic_set (client_id, 
                               table_name, 
                               element,
                               size, 
                               0,0,
                               SYNCDB_INSERT);
}

/*********************************************************************
* @purpose  Delete an entry from the AVL table.
*           
* @param    client_id (input) - Handle returned by syncdbClientRegister()
* @param    table_name  - Unique name for this table.
* @param    element  - Pointer to the data element to be deleted.
* @param    size  - Number of bytes in the element.
*
* @returns  SYNCDB_OK - Entry is deleted.
* @returns  SYNCDB_ERROR - Unspecified error.
* @returns  SYNCDB_NO_TABLE - Specified table does not exist.
* @returns  SYNCDB_SIZE - The specified size is invalid.
* @returns  SYNCDB_NOT_FOUND - Entry is not found in the table.
*
* @notes   The Delete operation is supported only on AVL trees.
*          If the specified table is not an AVL tree then
*          the SYCNDB_ERROR error code is returned.
* 
*          The 'size' must be exactly the same as the
*          record size specified on table creation.
*          If the size is invalid then SYNCDB_SIZE error code
*          is returned.
* 
* @end
*********************************************************************/
int syncdbDelete (syncdbClientHandle_t  *client_id,
                          char  *table_name,
                          void *element,
                          unsigned int   size)
{
  return syncdb_generic_set (client_id, 
                             table_name, 
                             element,
                             size, 
                             0,0,
                             SYNCDB_DELETE);
}


/*********************************************************************
* @purpose  Set an entry in the table.
*           
* @param    client_id (input) - Handle returned by syncdbClientRegister()
* @param    table_name  - Unique name for this table.
* @param    element  - Pointer to the data buffer to be set in the table. 
* @param    size  - Number of bytes in the element.
*
* @returns  SYNCDB_OK - Entry is modified.
* @returns  SYNCDB_ERROR - Unspecified error.
* @returns  SYNCDB_NO_TABLE - Specified table does not exist.
* @returns  SYNCDB_SIZE - The specified size is invalid.
* @returns  SYNCDB_NOT_FOUND - Entry is not found in the table.
*
* @notes   This command works for Records and AVL Trees.
*          For AVL Trees the command modifies the element which
*          matches the key.
*          For Records the command modifies the whole record.
* 
*          For AVL Trees and Records the 'size' must be exactly the
*          same as the record size specified on table creation.
* 
*          If the size is invalid then SYNCDB_SIZE error code
*          is returned.
* 
* @end
*********************************************************************/
int syncdbSet (syncdbClientHandle_t  *client_id,
                          char  *table_name,
                          void *element,
                          unsigned int   size)
{
  return syncdb_generic_set (client_id, 
                             table_name, 
                             element,
                             size, 
                             0,0,
                             SYNCDB_SET);
}

/*********************************************************************
* @purpose  Set the specified field in an entry in the table.
*           The remaining fields in the entry are unchanged.
*           
* @param    client_id (input) - Handle returned by syncdbClientRegister()
* @param    table_name  - Unique name for this table.
* @param    element (Input) - Caller must pass a buffer where
*                   the data is located.
* @param    size  - Number of bytes in the element.
* @param    field_offset  - Start of the field to set in the table.
*                           The value represents the number of bytes from the
*                           start of the element.
* @param    field_size  - Size of the field to set.
*
* @returns  SYNCDB_OK - Entry is modified.
* @returns  SYNCDB_ERROR - Unspecified error.
* @returns  SYNCDB_NO_TABLE - Specified table does not exist.
* @returns  SYNCDB_SIZE - The specified size is invalid.
* @returns  SYNCDB_NOT_FOUND - Entry is not found in the table.
*
* @notes   This command works for Records and AVL Trees.
*          For AVL Trees the command modifies the element which
*          matches the key.
* 
*          The command is useful when multiple processes modify
*          different fields in the same record.
* 
*          The 'size' must be exactly the
*          same as the record size specified on the table creation.
*          If the size is invalid then SYNCDB_SIZE error code
*          is returned.
* 
*          The field offset plus the field size must be less or
*          equal to the "size".
*          Note that multiple adjacent fields can be set
*          at the same time. The whole record can be set
*          by setting the "field_offset" to 0 and "field_size" to
*          "size".
* 
* @end
*********************************************************************/
int syncdbFieldSet (syncdbClientHandle_t  *client_id,
                          char  *table_name,
                          void *element,
                          unsigned int   size,
                          unsigned int field_offset,
                          unsigned int field_size)
{
    if ((field_offset + field_size) > size)
    {
        return SYNCDB_ERROR;
    }

    return syncdb_generic_set (client_id, 
                             table_name, 
                             element,
                             size, 
                             field_offset,
                             field_size,
                             SYNCDB_FIELD_SET);
}

/*********************************************************************
* @purpose  Send a Get/GetNext/GetNextChanged Command to syncdb.
*           
* @param    client_id (input) - Handle returned by syncdbClientRegister()
* @param    table_name  - Unique name for this table.
* @param    element  - Pointer to the data element to be inserted.
* @param    size  - Number of bytes in the element.
* @param    field_offset  - Start of the field to retrieve from the table.
*                           The value represents the number of bytes from the
*                           start of the table.
* @param    field_size  - Size of the field to get.
* @param    flags_unchanged - Do not clear the notification-pending and the
*                             delete-pending flags.
* @param    delete_pending (Output) - For AVL Trees this indicates
*                           whether the record is pending for
*                           deletion. The field is unused for
*                           records and queues. This pointer may be
*                           passed in as 0.
* @param    command  - The type of command.
*
* @returns  SYNCDB_OK - Command is successful.
* @returns  SYNCDB_ERROR - Unspecified error.
* @returns  Other command-specific error codes.
*
* @notes   This utility function is used by Get/GetNext/GetNextChanged APIs.
*          See the API description for the exact error code list.
* 
* @end
*********************************************************************/
static int syncdb_generic_get (syncdbClientHandle_t  *client_id,
                          char  *table_name,
                          void *element,
                          unsigned int   size,
                          unsigned int field_offset,
                          unsigned int field_size,
                          int flags_unchanged,
                          int *delete_pending,
                          int command)
{
  unsigned char *buf;
  syncdbCmdMsg_t *msg;
  void *element_ptr;
  syncdbCmdMsg_t *reply;
  int rc;
  unsigned char *user_data;

    if (!client_id || !table_name || !element) 
    {
        return SYNCDB_ERROR;
    }

    if (!size) 
    {
        return SYNCDB_ERROR;
    }

    buf = malloc (SYNCDB_MSG_MAX_SIZE);
    msg = (syncdbCmdMsg_t *) buf;
    element_ptr = buf + sizeof (syncdbCmdMsg_t);
    reply = (syncdbCmdMsg_t *) buf;
    memset (msg, 0, sizeof (syncdbCmdMsg_t));
    msg->client_id = client_id->client_id;
    msg->message_type = command;
    strncpy (msg->msg.genericGetMsg.table_name,
             table_name,
             SYNCDB_TABLE_NAME_SIZE-1);
    msg->msg.genericGetMsg.size = size;
    memcpy (element_ptr, element, size);
    msg->msg.genericGetMsg.field_offset = field_offset;
    msg->msg.genericGetMsg.field_size = field_size;
    msg->msg.genericGetMsg.flags_unchanged = flags_unchanged;

    syncdb_sema_take (client_id);
    rc = syncdb_msg_send(syncdb_socket, &syncdb_server, 
                         msg, sizeof(syncdbCmdMsg_t) + size);
    if (rc < 0) 
    {
        free (buf);
        syncdb_sema_give (client_id);
        return SYNCDB_ERROR;
    }

    rc = syncdb_msg_receive (client_id->cmd_socket, reply, sizeof(syncdbCmdMsg_t) + size);
    syncdb_sema_give (client_id);
    if (rc < 0) 
    {
        free (buf);
        return SYNCDB_ERROR;
    }

    /* If the Get operation is successful then copy the returned data.
    */
    if (delete_pending)
    {
        *delete_pending = 0;
    }
    if (reply->rc == SYNCDB_OK)
    {
        if (delete_pending)
        {
            *delete_pending = reply->msg.genericGetMsg.delete_pending;
        }
        user_data = (unsigned char *)reply + sizeof (syncdbCmdMsg_t);
        memcpy (element, user_data, size);
    }

    rc = reply->rc;
    free (buf);
    return rc;
}

/*********************************************************************
* @purpose  Get an entry from the table.
*           
* @param    client_id (input) - Handle returned by syncdbClientRegister()
* @param    table_name  - Unique name for this table.
* @param    element (Input/Output) - Caller must pass a buffer where
*                   the data is written. The buffer must be big
*                   enough to contain 'size' bytes.
* @param    size  - Number of bytes in the element.
* @param    delete_pending (Output) - For AVL Trees this indicates
*                           whether the record is pending for
*                           deletion. The field is unused for
*                           records and queues.
*
* @returns  SYNCDB_OK - Entry is found. Need to check delete_pending
*                       flag for AVL trees.
* @returns  SYNCDB_ERROR - Unspecified error.
* @returns  SYNCDB_NO_TABLE - Specified table does not exist.
* @returns  SYNCDB_SIZE - The specified size is invalid.
* @returns  SYNCDB_NOT_FOUND - Entry is not found in the table.
*
* @notes   This command works for Records and AVL Trees.
*          For AVL Trees the command retrieves the element which
*          matches the key.
*          For Records the command retrieves the whole record.
* 
*          The 'size' must be exactly the
*          same as the record size specified on the table creation.
*          If the size is invalid then SYNCDB_SIZE error code
*          is returned.
* 
* @end
*********************************************************************/
int syncdbGet (syncdbClientHandle_t  *client_id,
                          char  *table_name,
                          void *element,
                          unsigned int   size,
                          int *delete_pending)
{
  return syncdb_generic_get (client_id, 
                             table_name, 
                             element,
                             size, 
                             0,0,0,
                             delete_pending,
                             SYNCDB_GET);
}

/*********************************************************************
* @purpose  Get the specified field in an entry from the table.
*           
* @param    client_id (input) - Handle returned by syncdbClientRegister()
* @param    table_name  - Unique name for this table.
* @param    element (Input/Output) - Caller must pass a buffer where
*                   the data is written. The buffer must be big
*                   enough to contain 'size' bytes.
* @param    size  - Number of bytes in the element.
* @param    field_offset  - Start of the field to retrieve from the table.
*                           The value represents the number of bytes from the
*                           start of the table.
* @param    field_size  - Size of the field to get.
* @param    flags_unchanged - Do not clear the notification-pending and the
*                             delete-pending flags.
* @param    delete_pending (Output) - For AVL Trees this indicates
*                           whether the record is pending for
*                           deletion. The field is unused for
*                           records and queues.
*
* @returns  SYNCDB_OK - Entry is found. Need to check delete_pending
*                       flag for AVL trees.
* @returns  SYNCDB_ERROR - Unspecified error.
* @returns  SYNCDB_NO_TABLE - Specified table does not exist.
* @returns  SYNCDB_SIZE - The specified size is invalid.
* @returns  SYNCDB_NOT_FOUND - Entry is not found in the table.
*
* @notes   This command works for Records and AVL Trees.
*          For AVL Trees the command retrieves the element which
*          matches the key.
* 
*          The 'size' must be exactly the
*          same as the record size specified on the table creation.
*          If the size is invalid then SYNCDB_SIZE error code
*          is returned.
* 
*          The field offset plus the field size must be less or
*          equal to the "size".
*          Note that multiple adjacent fields can be retrieved
*          at the same time. The whole record can be retrieved
*          by setting the "field_offset" to 0 and "field_size" to
*          "size".
* 
*          The function may be used to retrieve data without
*          clearing the change notification and delete-pending flags
*          by setting the "flags_unchanged" parameter to 1.
* 
* @end
*********************************************************************/
int syncdbFieldGet (syncdbClientHandle_t  *client_id,
                          char  *table_name,
                          void *element,
                          unsigned int   size,
                          unsigned int field_offset,
                          unsigned int field_size,
                          int  flags_unchanged,
                          int *delete_pending)
{
    if ((field_offset + field_size) > size)
    {
        return SYNCDB_ERROR;
    }

    return syncdb_generic_get (client_id, 
                             table_name, 
                             element,
                             size, 
                             field_offset,
                             field_size,
                             flags_unchanged,
                             delete_pending,
                             SYNCDB_FIELD_GET);
}

/*********************************************************************
* @purpose  Get the Next entry from the table.
*           
* @param    client_id (input) - Handle returned by syncdbClientRegister()
* @param    table_name  - Unique name for this table.
* @param    element (Input/Output) - Caller must pass a buffer where
*                   the data is written. The buffer must be big
*                   enough to contain 'size' bytes.
* @param    size  - Number of bytes in the element.
* @param    delete_pending (Output) - For AVL Trees this indicates
*                           whether the record is pending for
*                           deletion. The field is unused for
*                           records and queues.
*
* @returns  SYNCDB_OK - Entry is found.
* @returns  SYNCDB_ERROR - Unspecified error.
* @returns  SYNCDB_NO_TABLE - Specified table does not exist.
* @returns  SYNCDB_SIZE - The specified size is invalid.
* @returns  SYNCDB_NOT_FOUND - Entry is not found in the table.
*
* @notes   This command works only for AVL Trees.
*          The command does not work for other table types.
*          For AVL Trees the command retrieves the element which
*          matches the next higher key.
* 
*          The 'size' must be exactly the
*          same as the record size specified on the table creation.
*          If the size is invalid then SYNCDB_SIZE error code
*          is returned.
* 
* @end
*********************************************************************/
int syncdbGetNext (syncdbClientHandle_t  *client_id,
                          char  *table_name,
                          void *element,
                          unsigned int   size,
                          int *delete_pending)
{
  return syncdb_generic_get (client_id, 
                             table_name, 
                             element,
                             size, 
                             0,0,0,
                             delete_pending,
                             SYNCDB_GETNEXT);
}

/*********************************************************************
* @purpose  Get the Next Changed entry from the table.
*           
* @param    client_id (input) - Handle returned by syncdbClientRegister()
* @param    table_name  - Unique name for this table.
* @param    element (Input/Output) - Caller must pass a buffer where
*                   the data is written. The buffer must be big
*                   enough to contain 'size' bytes.
* @param    size  - Number of bytes in the element.
* @param    delete_pending (Output) - For AVL Trees this indicates
*                           whether the record is pending for
*                           deletion. The field is unused for
*                           records and queues.
*
* @returns  SYNCDB_OK - Entry is found.
* @returns  SYNCDB_ERROR - Unspecified error.
* @returns  SYNCDB_NO_TABLE - Specified table does not exist.
* @returns  SYNCDB_SIZE - The specified size is invalid.
* @returns  SYNCDB_NOT_FOUND - Entry is not found in the table.
*
* @notes   This command works only for AVL Trees.
*          The command does not work for other table types.
*          For AVL Trees the command retrieves the element which
*          matches the next higher key and which has changed since
*          this client performed the last Get/GetNext/GetNextChanged
*          operation for this record.
* 
*          The 'size' must be exactly the
*          same as the record size specified on the table creation.
*          If the size is invalid then SYNCDB_SIZE error code
*          is returned.
* 
* @end
*********************************************************************/
int syncdbGetNextChanged (syncdbClientHandle_t  *client_id,
                          char  *table_name,
                          void *element,
                          unsigned int   size,
                          int *delete_pending)
{
  return syncdb_generic_get (client_id, 
                             table_name, 
                             element,
                             size, 
                             0,0,0,
                             delete_pending,
                             SYNCDB_GETNEXT_CHANGED);
}

/*********************************************************************
* @purpose  Perform sanity checking on the schema node.
*           
* @param    num_nodes  - Number of nodes in the element_node array.
* @param    element_node - Element descriptor nodes.
* @param    check_node - Schema node that we need to check.
* @param    data_element_size - Data record size. Used for error checking.
* @param    schema_error - Detailed schema error code.
* 
* 
* @returns  SYNCDB_OK - Entry is found.
* @returns  SYNCDB_SCHEMA_ERROR - Error in schema.
*
* @notes    
* 
* @end
*********************************************************************/
static int syncdb_json_node_validate (
                            unsigned int num_nodes,
                            syncdbJsonNode_t *element_node,
                            syncdbJsonNode_t *check_node,
                            unsigned int data_element_size,
                            syncdbSchemaError_e *schema_error
                            )
{
    int i;
    unsigned long long int_val;
    unsigned int start, end;
    unsigned int zero_offset_found;
    unsigned int last_schema_byte;
    unsigned int smallest_gap;

    if ((check_node->data_offset + check_node->data_size) > data_element_size)
    {
        *schema_error = SYNCDB_SCHEMA_TOO_BIG;
        return SYNCDB_SCHEMA_ERROR;
    }

    if (!check_node->data_size)
    {
        *schema_error = SYNCDB_SCHEMA_ZERO_SIZE;
        return SYNCDB_SCHEMA_ERROR;
    }

    if (check_node->data_type == SYNCDB_JSON_NUMBER)
    {
        if ((check_node->data_size != 1) && (check_node->data_size != 2) &&
            (check_node->data_size != 4) && (check_node->data_size != 8))
        {
            *schema_error = SYNCDB_SCHEMA_INT_SIZE;
            return SYNCDB_SCHEMA_ERROR;
        }

        int_val =  check_node->val.default_number;
        if (check_node->data_size == 1)
        {
            if (int_val >= (1 << 8))
            {
                *schema_error = SYNCDB_SCHEMA_INT_OVERFLOW;
                return SYNCDB_SCHEMA_ERROR;
            }
        }
        if (check_node->data_size == 2)
        {
            if (int_val >= (1 << 16))
            {
                *schema_error = SYNCDB_SCHEMA_INT_OVERFLOW;
                return SYNCDB_SCHEMA_ERROR;
            }
        }
        if (check_node->data_size == 4)
        {
            if (int_val >= (1LL << 32))
            {
                *schema_error = SYNCDB_SCHEMA_INT_OVERFLOW;
                return SYNCDB_SCHEMA_ERROR;
            }
        }

    }

    if (check_node->data_type == SYNCDB_JSON_STRING)
    {
        /* The default string, including the zero terminating character
        ** must fit into the string buffer.
        */
        if (check_node->data_size < (strlen(check_node->val.default_string) + 1))
        {
            *schema_error = SYNCDB_SCHEMA_STRING_OVERFLOW;
            return SYNCDB_SCHEMA_ERROR;
        }
    }

    zero_offset_found = 0;
    last_schema_byte = 0;
    if (num_nodes == 1)
    {
        smallest_gap = 0;
    } else
    {
        smallest_gap = data_element_size; 
    }
    for (i = 0; i < num_nodes; i++)
    {
        if (element_node[i].data_offset == 0)
        {
            zero_offset_found = 1;
        }

        if ((element_node[i].data_offset + element_node[i].data_size) > last_schema_byte)
        {
            last_schema_byte = element_node[i].data_offset + element_node[i].data_size;
        }

        if (check_node == &element_node[i])
        {
            /* Skip the node we are checking.
            */
            continue;
        }
        start = element_node[i].data_offset;
        end = element_node[i].data_offset + element_node[i].data_size;

        if ((start < check_node->data_offset) &&
            ((check_node->data_offset - end) < smallest_gap))
        {
            smallest_gap = check_node->data_offset - end;
        }
        if (check_node->data_offset == 0)
        {
            smallest_gap = 0;
        }

        if ((check_node->data_offset >= start) &&
            (check_node->data_offset < end))
        {
            *schema_error = SYNCDB_SCHEMA_OVERLAP;
            return SYNCDB_SCHEMA_ERROR;
        }
        if (((check_node->data_offset + check_node->data_size) > start) &&
            ((check_node->data_offset + check_node->data_size) <= end))
        {
            *schema_error = SYNCDB_SCHEMA_OVERLAP;
            return SYNCDB_SCHEMA_ERROR;
        }

        if (0 == strcmp(check_node->data_name, element_node[i].data_name))
        {
            *schema_error = SYNCDB_SCHEMA_DUP_NAME;
            return SYNCDB_SCHEMA_ERROR;
        }

    }

    if (!zero_offset_found)
    {
        *schema_error = SYNCDB_SCHEMA_NO_ZERO_OFFSET;
        return SYNCDB_SCHEMA_ERROR;
    }

    if ((last_schema_byte < data_element_size) &&
        (data_element_size - last_schema_byte) >= 8)
    {
        *schema_error = SYNCDB_SCHEMA_TOO_SHORT;
        return SYNCDB_SCHEMA_ERROR;
    }

    if (smallest_gap  >= 8)
    {
        *schema_error = SYNCDB_SCHEMA_GAP;
        return SYNCDB_SCHEMA_ERROR;
    }

    return SYNCDB_OK;
}

/*********************************************************************
* @purpose  Encode the schema into JSON format.
*           
* @param    num_nodes  - Number of nodes in the element_node array.
* @param    element_node - Element descriptor nodes.
* @param    schema_buf - Buffer in which the JSON schema is returned.
* @param    buf_size - Number of bytes in the schema_buf.
* @param    schema_size (output) - Current schema size.
* @param    data_element_size - Data record size. Used for error checking.
* @param    schema_error - Detailed schema error code.
* 
* 
* @returns  SYNCDB_OK - Entry is found.
* @returns  SYNCDB_ERROR - Error in input parameters or element nodes.
* @returns  SYNCDB_SIZE - JSON schema does not fit into schema_buf buffer.
* @returns  SYNCDB_SCHEMA_ERROR - Error in schema.
*
* @notes    
* 
* @end
*********************************************************************/
static int syncdb_json_object_encode (
                            unsigned int num_nodes,
                            syncdbJsonNode_t *element_node,
                            char *schema_buf,
                            unsigned int buf_size,
                            unsigned int *schema_size,
                            unsigned int data_element_size,
                            syncdbSchemaError_e *schema_error
                            )
{
  int len;
  int i;
  int rc;
  static char *data_type_str[] = {"","number","string","array"};
  char indent_buf [] = {"\t"};
  char indent_prev[] = {""};

  *schema_error = SYNCDB_SCHEMA_OK;

  for (i = 0; i < num_nodes; i++)
  {
      if ((element_node[i].data_type < SYNCDB_JSON_NUMBER) ||
          (element_node[i].data_type > SYNCDB_JSON_ARRAY))
      {
          *schema_error = SYNCDB_SCHEMA_TYPE;
          return SYNCDB_SCHEMA_ERROR;
      }

      len = snprintf (&schema_buf[*schema_size], 
                     buf_size - *schema_size,
                     "%s{\n"
                     "%s\"name\"  :\"%s\",\n"
                     "%s\"type\"  :\"%s\",\n"
                     "%s\"offset\":%d,\n"
                     "%s\"size\"  :%d,\n",
                     indent_prev,
                     indent_buf,
                     element_node[i].data_name,
                     indent_buf,
                     data_type_str [element_node[i].data_type],
                     indent_buf,
                     element_node[i].data_offset,
                     indent_buf,
                     element_node[i].data_size
                     );
      *schema_size += len;
      if (*schema_size > buf_size)
      {
          return SYNCDB_SIZE;
      }

      rc = syncdb_json_node_validate (num_nodes, element_node,
                                      &element_node[i], data_element_size,
                                      schema_error);
      if (rc != SYNCDB_OK)
      {
          return rc;
      }

      switch (element_node[i].data_type)
      {
      case SYNCDB_JSON_NUMBER:
          len = snprintf (&schema_buf[*schema_size], 
                         buf_size - *schema_size,
                          "%s\"deflt\" :\"%llu\"\n"
                          "%s}",
                          indent_buf,
                          element_node[i].val.default_number,
                          indent_prev);
          *schema_size += len;
          if (*schema_size > buf_size)
          {
              return SYNCDB_SIZE;
          }
          break;
      case SYNCDB_JSON_ARRAY:
          len = snprintf (&schema_buf[*schema_size], 
                         buf_size - *schema_size,
                          "%s\"deflt\" :\"0\"\n"
                          "%s}",
                          indent_buf,
                          indent_prev);
          *schema_size += len;
          if (*schema_size > buf_size)
          {
              return SYNCDB_SIZE;
          }
          break;
      case SYNCDB_JSON_STRING:
          len = snprintf (&schema_buf[*schema_size], 
                         buf_size - *schema_size,
                          "%s\"deflt\" :\"%s\"\n"
                          "%s}",
                          indent_buf,
                          element_node[i].val.default_string,
                          indent_prev);
          *schema_size += len;
          if (*schema_size > buf_size)
          {
              return SYNCDB_SIZE;
          }
          break;
      }
      if ((i+1) < num_nodes)
      {
          len = snprintf (&schema_buf[*schema_size], 
                         buf_size - *schema_size,
                          ",\n");
      } else
      {
          len = snprintf (&schema_buf[*schema_size], 
                         buf_size - *schema_size,
                          "\n");
      }
      *schema_size += len;
      if (*schema_size > buf_size)
      {
          return SYNCDB_SIZE;
      }
  }

  return SYNCDB_OK;
}
/*********************************************************************
* @purpose  Generate storable record schema.
*           
* @param    element_node - Element descriptor nodes.
* @param    node_schema_size - Size of the element node array.
* @param    schema_buf - Buffer in which the JSON schema is returned.
*                   The caller must allocate space for this buffer.
* @param    buf_size - Number of bytes in the schema_buf.
*                   The maximum buffer size is SYNCDB_JSON_MAX_SCHEMA_SIZE
* @param    schema_size (output) - Actual schema length.
* @param    data_element_size - Data record size. Used for error checking.
* @param    schema_error - Detailed schema error code.
* 
* 
* 
* @returns  SYNCDB_OK - Entry is found.
* @returns  SYNCDB_ERROR - Error in input parameters or element nodes.
* @returns  SYNCDB_SIZE - JSON schema does not fit into schema_buf buffer.
* @returns  SYNCDB_SCHEMA_ERROR - Error in schema.
*
* @notes    If an error is detected while generating the schema,
*           the caller can print out the content of the schema_buf
*           to see approximately where the error is found. The
*           schema_buf contains generated schema up to the point
*           where the error was found.
* 
* @end
*********************************************************************/
int syncdbUtilSchemaCreate (syncdbJsonNode_t *element_node,
                            unsigned int node_schema_size, 
                            char *schema_buf,
                            unsigned int buf_size,
                            unsigned int *schema_size,
                            unsigned int data_element_size,
                            syncdbSchemaError_e *schema_error
                            )
{
  int rc;
  int len;
  int num_nodes;

    if (!node_schema_size || !element_node || !data_element_size ||
        !schema_buf || !buf_size || !schema_size || !schema_error)
    {
        return SYNCDB_ERROR;
    }

    if (buf_size > SYNCDB_JSON_MAX_SCHEMA_SIZE)
    {
        return SYNCDB_ERROR;
    }

    num_nodes = node_schema_size / sizeof (syncdbJsonNode_t);
    if ((num_nodes * sizeof(syncdbJsonNode_t)) != node_schema_size) 
    {
        return SYNCDB_ERROR;
    }

    memset (schema_buf, 0, buf_size);
    *schema_size = 0;
    *schema_error = SYNCDB_SCHEMA_OK;
    buf_size--; /* Make sure that the schema string is zero-terminated */

    len = snprintf (&schema_buf[*schema_size], 
                   buf_size - *schema_size,
                   "{\n"
                   "\"schema\": {\n"
                   "\t\"node\": [\n");
    *schema_size += len;
    if (*schema_size > buf_size)
    {
        return SYNCDB_SIZE;
    }

    rc = syncdb_json_object_encode (num_nodes,
                                    element_node, 
                                    schema_buf, 
                                    buf_size, 
                                    schema_size,
                                    data_element_size,
                                    schema_error
                                    );

    if (rc == SYNCDB_OK)
    {
        len = snprintf (&schema_buf[*schema_size], 
                       buf_size - *schema_size,
                        "\t]\n"
                        "}\n"
                        "}\n"
                        );
        *schema_size += len;
        if (*schema_size > buf_size)
        {
            return SYNCDB_SIZE;
        }
    }

    return rc;
}
