/*********************************************************************
 *
 * Copyright: (c) 2017 Broadcom.
 * Broadcom Proprietary and Confidential. All rights reserved.
 *
 *********************************************************************/

#ifndef SYNCDB_H_INCLUDED
#define SYNCDB_H_INCLUDED

#include <sys/types.h>
#include <sys/un.h>

#include <brcm_syncdb_api.h>

/* NVRAM archive file name.
** All syncdb tables enabled for NVRAM archiving are combined into this archive.
*/
#define SYNCDB_NVRAM_ARCHIVE "/brcm_sai_syncdb.dat"

/* Maximum number of clients that can use the database.
** The value must be divisible by 8. 
** The value affects the bit mask size in each AVL table node. 
*/
#define CLIENT_MAX_LIMIT  (8 * 64)

/* Maximum client ID.
** This value must be the same or greater than CLIENT_MAX_LIMIT 
** and evenly divisible by the CLIENT_MAX_LIMIT. 
*/
#define CLIENT_MAX_ID ((65536 / CLIENT_MAX_LIMIT) * CLIENT_MAX_LIMIT)

/* Bit mask size containing a bit for each client.
*/
#define CLIENT_MASK_SIZE (CLIENT_MAX_LIMIT / 8)

/* Set a bit corresponding to the client ID in the client mask.
*/
#define CLIENT_MASK_SET(client,mask) \
        (((unsigned char *)(mask))[(client)/8] |= 1 << ((client) - 8*((client)/8)))

/* Clear a bit corresponding to the client ID in the client mask.
*/
#define CLIENT_MASK_CLEAR(client,mask) \
        (((unsigned char *)(mask))[(client)/8] &= ~(1 << ((client) - 8*((client)/8))))

/* Check whether specified client bit is set in the mask.
** Return 0 if not set and non-zero if set.
*/
#define CLIENT_MASK_IS_SET(client,mask) \
        ((((unsigned char *)(mask))[(client)/8]) & (1 << ((client) - 8*((client)/8))))

/* Hash table size of the data table names.
** This value must be a power of 2 and less or equal to (1 << 16).
*/
#define TABLE_NAME_HASH_SIZE (1 << 13)


/* Client Table Node.
*/
typedef struct 
{
  unsigned int client_id; /* Unique Client ID in the range from 0 to (CLIENT_MAX_LIMIT-1) */
  pid_t client_pid; /* Client Process ID */
  struct sockaddr_un client_socket_name; /* UNIX Domain Socket Name for the Client */
  struct sockaddr_un client_notify_socket_name; /* UNIX Domain Socket Name for the Client table changes */
  int reply_socket_id; /* Client Command Reply Socket ID */
  int event_socket_id; /* Client Event Notification Socket ID */
  unsigned char client_description[SYNCDB_MAX_STR_LEN]; /* User-Provided Client Description */

 /******************************************************** 
 ** The following information is used only for debugging. 
 ********************************************************/
  unsigned long long num_commands; /* Total number of commands issued by this client */

  /* Number of table change events sent by the syncdb process to this client.
  */
  unsigned long long num_table_change_events; 

  /* If a client is registered for AVL Table events, and an AVL table entry
  ** is deleted from the table before the client has the chance to process
  ** the delete notification, then syncdb sends an event to the client indicating 
  ** that the table has been purged and that full table resyncrhonization
  ** is required for this client. The counter keeps track of how many such table purges
  ** happened for this client.
  */
  unsigned long long num_table_purges;
  
} clientTableNode_t;

/* Do not change the table order.
*/
typedef enum {
    TABLE_TYPE_RECORD = 0,
    TABLE_TYPE_AVL,
    TABLE_TYPE_LAST   /* Must be the last field in the table */
} tableType_e;

/* Data Table Node.
*/
typedef struct dataTableNode_s
{
 char table_name [SYNCDB_TABLE_NAME_SIZE];
 unsigned char table_version;

 /* Flags passed on the create call.
 */
 unsigned int create_flags;

 tableType_e table_type; /* Record, Storable-Record, AVL Tree */

 /* The size of each record. This value must be less or 
 ** equal to SYNCDB_RECORD_MAX_SIZE.
 */
 int record_size;

 /* Number of records currently in the table.
 ** Always 1 for Record and Storable-Record.
 ** 0 to max_records for AVL trees.
 */
 int num_records;

 /* Number of records that are not pending for deletion.
 ** Valid only for AVL trees.
 */
 int num_non_deleted_records;

 /* Maximum number of records in the table. 
 ** Always 1 for Record and Storable-Record.
 */
 int max_records; 

 /* This attribute is applicable only to AVL trees.  
 ** The attribute specifies the maximum number of entries in the table that 
 ** are not pending for deletion.
 */
 int max_non_deleted_records;

 /* Pointer to the data record. 
 */
 void *record_ptr;

 /* Pointer for linking the nodes inside the hash lookup table.
 */
 struct dataTableNode_s *next_hash; 

 /* Clients registered for notifications when changes are made to 
 ** this table. The clients are represented as a bit mask indexed
 ** by the client ID.
 */
 unsigned char notify_clients_mask [CLIENT_MASK_SIZE];

 /* Per-Client mask indicating that this table has been created.
 ** The first access to syncdbTableStatusGet() clears the flag.
 */
 unsigned char table_created_client_mask [CLIENT_MASK_SIZE];

 /* Per client mask indicating that something changed in the table.
 ** The flag is set on Insert/Delete/Set command and cleared 
 ** on syncdbTableStatusGet(). 
 */
 unsigned char table_changed_client_mask [CLIENT_MASK_SIZE];

 /* Per client mask indicating that all delete-pending entries
 ** have been deleted from the AVL tree.
 */
 unsigned char table_purged_client_mask [CLIENT_MASK_SIZE];

 /* For NSF tables the flag indicating that the table needs to
 ** be synchronized to the backup manager. 
 */ 
 unsigned int nsf_table_changed_flag;

 /* For NSF AVL trees the flag indicates that the AVL tree needs to
 ** be purged on the backup manager 
 */ 
 unsigned int nsf_table_purge_flag;

 /* For writable tables this is the schema size.
 */
 unsigned int schema_size;

 /* For writable tables this is a pointer to the schema.
 */
 char *schema;


 /******************************************************** 
 ** The following information is used only for debugging. 
 ********************************************************/

 /* Pointer for linking the nodes inside the sorted list.
 */
 struct dataTableNode_s *next_sorted; 

 /* Number of read accesses to this table. 
 ** This includes get, getNext, getNextChanged operations.
 */
 unsigned long long num_reads; 
			 
 /* Number of write accesses to this table. 
 ** This includes Set, Insert, Delete operations.
 */
 unsigned long long num_writes;

 /* The overall memory size of this data table, including the control overhad.
 */
 unsigned int total_memory_size;

 /* For AVL tables this is the size of the index field.
 */
 int key_size;

 /* For AVL tables this is the number of times the table-purge action
 ** happened for this table. The table-purge action deletes all entries 
 ** from the table that are in delete-pending state. 
 */
 int num_purges;

} dataTableNode_t;

 /******************************************************* 
 ** Synchronization messages between Manager and 
 ** Backup managed in the Non-Stop-Forwarding environment. 
 ********************************************************/ 
#define SYNC_MSG_DATA  1
#define SYNC_MSG_ACK   2
#define SYNC_MSG_MISS  3

/* Sync message header.
*/
typedef struct 
{
    unsigned long long seq;
    unsigned short msg_size;
    unsigned short msg_type;  /* SYNC_MSG_DATA/ACK/MISS */
    unsigned char first_trans;  /* 0 or 1-First Transaction */
    unsigned char ack_request;  /* 0 or 1-Transmit ACK  */
    unsigned char pad1, pad2; /* Pad the header to a four-byte boundary. */
} nsf_sync_msg_t;

#define SYNC_IE_SET    1
#define SYNC_IE_DELETE 2
#define SYNC_IE_PURGE  3
#define SYNC_IE_NOOP   4

/* Information Element Header
*/
typedef struct
{
    unsigned short size;   /* Size of the whole IE */
    unsigned short segment_size; /* Size of the data in the IE */
    unsigned int   record_offset; /* Where to copy the data in the DB record */ 
    unsigned char  cmd;  /* SYNC_IE_SET/DELETE/PURGE */
    unsigned char  first_seg; /* 0 or 1-First Segment */
    unsigned char  last_seg; /* 0 or 1-Last Segment */
    unsigned char  pad1; 
    unsigned char  table_name [SYNCDB_TABLE_NAME_SIZE];
} nsf_ie_t;

/* NSF State Information.
*/

/* Number of buffers the sender can send before asking for ACK.
*/
#define SYNCDB_MAX_NSF_PENDING_BUFFERS  10

/* Number of seconds the sender waits for an ACK reply before
** retransmitting all unacked packets. 
*/ 
#define SYNCDB_ACK_TIMEOUT  5

/* Number of seconds the sender waits for additional data before
** sending an ACK request. 
*/ 
#define SYNCDB_EMPTY_ACK_TIMEOUT 1

typedef struct 
{
    unsigned int sync_sender;  /* 1-Sync Sender, 0-Sync Receiver */

    /* Sender State
    */
    unsigned long long tx_seq; /* Transmit Sequence Number */
    unsigned int last_tx_time; /* UP Time when last message was sent */
    unsigned int last_ack_time; /* UP Time when last ACK request was sent */
    unsigned int ack_pending; /* 1-waiting for ack */
    unsigned long long expected_ack_sequence; /* ACK reply should contain this number */

    /* Messages that have been sent, but not acknowledged.
    */
    unsigned char *tx_buffers[SYNCDB_MAX_NSF_PENDING_BUFFERS];
    unsigned int tx_buffer_head;
    unsigned int tx_buffer_tail;

    /* Receiver State
    */
    unsigned long long rx_seq; /* RX Sequence Number */
    unsigned char *record_buf; /* Temporary storage to reassemble the database record */
} nsf_state_t;

 /******************************************************** 
 ** The following information is used only for debugging. 
 ********************************************************/
typedef struct 
{
   /* Global Client Statistics.
   */
   unsigned int num_clients;

   /* Global Table Statistics.
   */
   unsigned int num_tables; /* Number of data tables */
   unsigned int num_record_tables; /* Number of data tables of type 'Record' */
   unsigned int num_storable_tables; /* Number of data tables of type 'Storable-Record' */
   unsigned int num_queues;  /* Number of data tables of type 'Queue' */
   unsigned int num_avl_trees; /* Number of data tables of type 'AVL Tree' */

   /* Number of allocated bytes for all tables and for each table type.
   ** The size includes control overhead.
   */
   unsigned int num_tables_size;
   unsigned int num_record_tables_size;
   unsigned int num_storable_tables_size;
   unsigned int num_queues_size;
   unsigned int num_avl_trees_size;

   /* This is the total number of commands received on the socket. 
   */
   unsigned long long num_commands;

   /* Total number of messages received from the syncdb agent and
   ** sent to the syncdb agent. 
   */ 
   unsigned long long num_rx_agent_msgs;
   unsigned long long num_tx_agent_msgs;

   /* The following statistics are counters for each command type.
   */
   unsigned long long num_get_commands;
   unsigned long long num_field_get_commands;
   unsigned long long num_getNext_commands;
   unsigned long long num_getNextChanged_commands;
   unsigned long long num_insert_commands;
   unsigned long long num_delete_commands;
   unsigned long long num_set_commands;
   unsigned long long num_nsf_sync_mode_commands;

   unsigned int num_tableCreate_commands;
   unsigned int num_tableDelete_commands;
   unsigned int num_tablePurge_commands;
   unsigned int num_tableStore_commands;
   unsigned int num_tableChangeNotify_commands;
   unsigned int num_tableStatus_commands;

   unsigned int num_clientRegister_commands;
   unsigned int num_clientStatusGet_commands;
   unsigned long long num_debug_commands;
}globalSyncdbStats_t;

/* AVL Record control data.
** This information is added to the end of each AVL node. 
*/
typedef struct
{
    /* This flag indicates that the entry has been deleted, but
    ** some applications have not processed the delete change 
    ** notification. 
    */
    int delete_pending; 

    /* Flag indicating that this node needs to be synchronized to
    ** the backup manager. 
    */ 
    int nsf_node_changed_flag;

    /* Clients that have not invoked a get, getNext, or getNextChanged function
    ** for this record since this record was added or changed.
    */
    unsigned char changed_mask [CLIENT_MASK_SIZE];
    void * next;  /* Needed by the AVL utility */
} syncdbAvlNodeCtrl_t;

void syncdb_main (char *path);

/* Utility Functions.
*/
void syncdb_database_store (dataTableNode_t *data_table,
                            char *file_name);
void syncdb_database_read (dataTableNode_t *data_table);

/* Debug Logging API functions.
*/
void syncdbLogLevelGet (void);
void syncdbDebugMsgLog (void);
void syncdbClientRegisterMsgLog (int pid, int client_id, int err_code);
void syncdbClientStatusMsgLog (int pid, 
                               int client_id, 
                               int target_client,
                               int err_code);
void syncdbTableChangeNotifyMsgLog (int pid, 
                               int client_id, 
                               char *table_name,
                               int err_code);
void syncdbTableStatusGetMsgLog (int pid, 
                               int client_id, 
                               int num_tables,
                               int err_code);
void syncdbAvlTableCreateMsgLog (int pid, 
                               int client_id, 
                               char *table_name,
                               int err_code);
void syncdbRecordTableCreateMsgLog (int pid, 
                               int client_id, 
                               char *table_name,
                               int err_code);
void syncdbTableDeleteMsgLog (int pid, 
                               int client_id, 
                               char *table_name,
                               int err_code);
void syncdbTableStoreMsgLog (int pid, 
                               int client_id, 
                               char *table_name,
                               int err_code);
void syncdbInsertMsgLog (int pid, 
                               int client_id, 
                               char *table_name,
                               int err_code);
void syncdbDeleteMsgLog (int pid, 
                               int client_id, 
                               char *table_name,
                               int err_code);
void syncdbSetMsgLog (int pid, 
                               int client_id, 
                               char *table_name,
                               int err_code);
void syncdbFieldSetMsgLog (int pid, 
                               int client_id, 
                               char *table_name,
                               int offset,
                               int size,
                               int err_code);

void syncdbGetMsgLog (int pid, 
                               int client_id, 
                               char *table_name,
                               int err_code);

void syncdbFieldGetMsgLog (int pid, 
                               int client_id, 
                               char *table_name,
                               int offset,
                               int size,
                               int err_code);

void syncdbGetNextMsgLog (int pid, 
                               int client_id, 
                               char *table_name,
                               int err_code);

void syncdbGetNextChangedMsgLog (int pid, 
                               int client_id, 
                               char *table_name,
                               int err_code);



#endif /* SYNCDB_H_INCLUDED */

