/*********************************************************************
 *
 * Copyright: (c) 2017 Broadcom.
 * Broadcom Proprietary and Confidential. All rights reserved.
 *
 *********************************************************************/
 
#ifndef SYNCDB_MSG_H_INCLUDED
#define SYNCDB_MSG_H_INCLUDED

#include <sys/types.h>
#include <brcm_syncdb_api.h>

#define SYNCDB_CMD_BUFF_SIZE 512
#define SYNCDB_FILE_PATH_BUFF_SIZE 256

/* The syncdb process command socket name.
*/
#define SYNCDB_SERVER_SOCKET "/syncdb-server-sock"

/* The socket for sending from the SyncDB process to the
** Syncdb agent running in the swithdrvr process. 
*/ 
#define SYNCDB_TO_AGENT_SOCKET "/syncdb-to-agent-sock"

/* The socket for sending from the SyncDB agent to the
** Syncdb process. 
*/ 
#define SYNCDB_FROM_AGENT_SOCKET "/syncdb-from-agent-sock"

/* Maximum size of the command message on the socket.
*/
#define SYNCDB_MSG_MAX_SIZE (SYNCDB_RECORD_MAX_SIZE + sizeof(syncdbCmdMsg_t))

typedef enum 
{
   SYNCDB_REPLY = 0,
   SYNCDB_DEBUG = 1,
   SYNCDB_CLIENT_REGISTER = 2,
   SYNCDB_CLIENT_STATUS = 3,
   SYNCDB_AVL_TABLE_CREATE = 4,
   SYNCDB_QUEUE_TABLE_CREATE = 5,
   SYNCDB_RECORD_TABLE_CREATE = 6,
   SYNCDB_STORABLE_RECORD_TABLE_CREATE = 7,
   SYNCDB_TABLE_DELETE = 8, 
   SYNCDB_TABLE_CHANGE_NOTIFY = 9,
   SYNCDB_TABLE_STATUS = 10,
   SYNCDB_TABLE_STORE = 11,
   SYNCDB_INSERT = 12,
   SYNCDB_DELETE = 13,
   SYNCDB_GET = 14,
   SYNCDB_FIELD_GET = 15,
   SYNCDB_GETNEXT = 16,
   SYNCDB_GETNEXT_CHANGED = 17,
   SYNCDB_SET = 18,
   SYNCDB_FIELD_SET = 19,
   SYNCDB_NSF_SYNC_ENABLE = 20
} syncdbMsg_t;

/* Message structure definitions.
*/

/* Client Registration Message.
*/
typedef struct 
{
    pid_t pid;
    char client_socket_name [SYNCDB_MAX_STR_LEN];
    char client_notify_socket_name [SYNCDB_MAX_STR_LEN];
    char client_description [SYNCDB_MAX_STR_LEN];

} syncdbClientRegisterMsg_t ;

/* Table Creation Message.
*/
typedef struct 
{
    char table_name [SYNCDB_TABLE_NAME_SIZE];
    unsigned int table_version;
    unsigned int max_elements; /* Only meaningful for AVL and Queue */
    unsigned int max_live_elements; /* Only meaningful for AVL */
    unsigned int node_size;
    unsigned int key_size; /* Only meaningful for AVL */
    unsigned int flags;
    unsigned int schema_size;
} syncdbTableCreateMsg_t;

/* Table Change Notification Message
*/
typedef struct 
{
    char table_name [SYNCDB_TABLE_NAME_SIZE];
} syncdbTableChangeNotifyMsg_t;

/* Table Status Get Message
*/
typedef struct 
{
    int num_tables;
} syncdbTableStatusGetMsg_t;

/* Message used for Set/Insert/Delete commands.
*/
typedef struct 
{
    char table_name [SYNCDB_TABLE_NAME_SIZE];
    unsigned int size;
    unsigned int field_offset;
    unsigned int field_size;
} syncdbGenericSetMsg_t;

/* Message used for Get/GetNext/GetNextChanged commands.
** The message is used for requests and replies.
*/
typedef struct 
{
    char table_name [SYNCDB_TABLE_NAME_SIZE];
    unsigned int size;
    unsigned int field_offset;
    unsigned int field_size;
    int flags_unchanged;
    int delete_pending; /* Only used for replies */
} syncdbGenericGetMsg_t;

/* Table Delete Message
*/
typedef struct 
{
    char table_name [SYNCDB_TABLE_NAME_SIZE];
} syncdbTableDeleteMsg_t;

/* Table Store Message
*/
typedef struct 
{
    char table_name [SYNCDB_TABLE_NAME_SIZE];
    unsigned int all_tables;
    unsigned int nvram;
} syncdbTableStoreMsg_t;

/* NSF Sync Mode Message.
*/
typedef struct 
{
    unsigned int sync_mode;
    unsigned int max_sync_msg_size;
} syncdbNsfSyncModeMsg_t;

/* Message header for communicating between the clients and the syncdb server.
*/
typedef struct
{
  syncdbMsg_t message_type;
  unsigned int client_id; /* Valid only for requests and client register replies */
  int rc;     /* Valid only for reply messages */
  union 
  {
      syncdbClientRegisterMsg_t registerMsg;
      syncdbTableCreateMsg_t tableCreateMsg;
      syncdbGenericSetMsg_t genericSetMsg;
      syncdbGenericGetMsg_t genericGetMsg;
      syncdbTableChangeNotifyMsg_t tableChangeNotifyMsg;
      syncdbTableStatusGetMsg_t tableStatusGetMsg;
      syncdbTableDeleteMsg_t tableDeleteMsg;
      syncdbTableStoreMsg_t tableStoreMsg;
      syncdbNsfSyncModeMsg_t nsfSyncMsg;
  }msg;
} syncdbCmdMsg_t;


#endif /* SYNCDB_MSG_H_INCLUDED */

