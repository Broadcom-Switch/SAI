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
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/prctl.h>

#include <brcm_avl_api.h>
#include <brcm_syncdb_api.h>
#include <brcm_syncdb_msg.h>
#include <brcm_syncdb.h>

#ifdef L3_PERF
long long avl_int_lookup_usecs, avl_int_add_usecs;
#endif

static char _syncdb_command[SYNCDB_CMD_BUFF_SIZE];
/* All file paths */
char _syncdb_log_level_path[SYNCDB_FILE_PATH_BUFF_SIZE];
static char _syncdb_debug_path_base[SYNCDB_FILE_PATH_BUFF_SIZE];
static char _syncdb_debug_path[SYNCDB_FILE_PATH_BUFF_SIZE];
char _syncdb_nv_path_base[SYNCDB_FILE_PATH_BUFF_SIZE];
char _syncdb_nv_path[SYNCDB_FILE_PATH_BUFF_SIZE];
char _syncdb_sock_path_base[SYNCDB_FILE_PATH_BUFF_SIZE];
static char _syncdb_sock_path[SYNCDB_FILE_PATH_BUFF_SIZE];
static char _syncdb_db_file[SYNCDB_FILE_PATH_BUFF_SIZE];

/* syncdb Client Table.
*/
static clientTableNode_t *client_table [CLIENT_MAX_LIMIT];

/* Table Names. Used for debugging.
*/
static char *data_table_name [] =  {
  "Record",
  "AVL Tree"
};


/* Lookup table for various data tables.
** The table name is used as a hash index to perform the lookup.
*/
static dataTableNode_t *data_table_hash [TABLE_NAME_HASH_SIZE];

/* The sorted list of tables. The list is sorted alphabetically
** by the table name. This list is used for debugging.
*/
static dataTableNode_t *sorted_data_table_list;

/* Global debug statistics.
*/
static globalSyncdbStats_t syncdb_stats;

/* NSF Synchronization state variables.
*/
static int nsf_sync_enable_pending = 0; /* Agent requested to enable sync(1). */
static int nsf_sync_disable_pending = 0; /* Agent requested to disable sync(1). */

static nsf_state_t nsf_state;

static unsigned int max_sync_msg_size; /* Maximum sync message size */

static int to_agent_fd; /* Socket for sending to the SyncDB Agent */
static struct sockaddr_un agent_proc;

/* Forward function declarations.
*/
void syncdb_table_change_notify (dataTableNode_t *data_table,
                                 int exclude_client_id);
void syncdbNsfManagerDatabaseResync (void);
void syncdbNsfMsgParse (nsf_sync_msg_t *msg);

extern pid_t sai_pid;

/*********************************************************************
* @purpose  Utility function to convert 64-bit integers between
*           host and network order.
*           
* @param    value
*
* @returns  Swapped value (Only on little endian)
*
* @notes  
*       
* @end
*********************************************************************/
static unsigned long long syncdb_htonll (unsigned long long value)
{
  int num = 100;

  if(*(char *)&num == 100)
       return ((unsigned long long) (htonl(value & 0xFFFFFFFF)) << 32) | htonl(value >> 32);
  else 
       return value;
}
/**************************************************************************
*
* @purpose  Retrieve number of seconds since last reset
*
* @param    void
*
* @returns  Up Time in Seconds
*
* @comments    none.
*
* @end
*
*************************************************************************/
unsigned int syncdb_uptime(void)
{
  struct timespec tp;
  int rc;

  rc = clock_gettime(CLOCK_MONOTONIC, &tp);
  if (rc < 0)
  {
    return (0);
  }
  return(tp.tv_sec);
}

/*********************************************************************
* @purpose  Compare two client bit masks.
*           Return 1 if at least one client is set in both masks.
*           
* @param    None
*
* @returns  1 - At least one client is set in both masks.
*           0 - No Clients are set in common.
*
* @notes  
*       
* @end
*********************************************************************/
static int syncdb_client_mask_check (unsigned char *mask1,
                                     unsigned char *mask2
                                     )
{
  int i;

  for (i = 0; i < CLIENT_MASK_SIZE; i++)
  {
    if (mask1[i] & mask2[i])
    {
      return 1;
    }
  }

  return 0;
}

/*********************************************************************
* @purpose  Catch the SIGHUP signal. This signal tells the syncdb
* 	    to dump its debug information in the debug
* 	    directory.
*           
* @param    None
*
* @returns  None
*
* @notes  
*       
* @end
*********************************************************************/
void sighup_catch (int sig_num)
{
  int fd;
  struct sockaddr_un server;
  syncdbCmdMsg_t msg;

  memset (&msg, 0, sizeof (msg));

  fd = socket (AF_UNIX, SOCK_DGRAM, 0);
  if (fd < 0)
  {
    return;
  }

  memset(&server, 0, sizeof(struct sockaddr_un));
  server.sun_family = AF_UNIX;
  sprintf(_syncdb_sock_path, "%s%s", _syncdb_sock_path_base, SYNCDB_SERVER_SOCKET);
  strcpy(server.sun_path, _syncdb_sock_path);

  msg.message_type = SYNCDB_DEBUG;
  (void) sendto(fd, &msg, sizeof(msg), MSG_DONTWAIT,
             (struct sockaddr *) &server, sizeof(server));

  close (fd);
}
/*********************************************************************
* @purpose  Process the "Debug" command.
*           
* @param    None
*
* @returns  None
*
* @notes  
*       
* @end
*********************************************************************/
void syncdbCommandDebugHandle (void)
{
  FILE *fd;
  int i;
  dataTableNode_t *table_node;

  syncdb_stats.num_debug_commands++;

  /* Dump database information into the debug directory.
  ** Clean out the debug directory first.
  */
  sprintf(_syncdb_command, "%s%s", "rm -rf ", _syncdb_debug_path_base);
  system (_syncdb_command);
  sprintf(_syncdb_command, "%s%s", "mkdir ", _syncdb_debug_path_base);
  system (_syncdb_command);
#ifdef RAMDISK
  system ("mkdir " SYNCDB_TMP_ROOT "/debug/ramdisk");
#endif
  sprintf(_syncdb_debug_path, "%s%s", _syncdb_debug_path_base, "/nvram");
  sprintf(_syncdb_command, "%s%s", "mkdir ", _syncdb_debug_path);
  system (_syncdb_command);

  /* 
  ** Dump Global Status and Statistics.
  */
  sprintf(_syncdb_debug_path, "%s%s", _syncdb_debug_path_base, "/global");
  fd = fopen (_syncdb_debug_path, "w+");
  if (fd == 0)
  {
    return;
  }

  fprintf (fd, "Global SyncDB Status and Statistics.\n");

  fprintf (fd, "\nNumbed of Clients: %u\n",
           syncdb_stats.num_clients);
  fprintf (fd, "Maximum Supported Clients: %u\n", CLIENT_MAX_LIMIT);
  fprintf (fd, "Maximum Client ID: %u\n", CLIENT_MAX_ID);

  fprintf (fd, "\nTotal Tables: %u (%uKB)\n", 
                syncdb_stats.num_tables,
                syncdb_stats.num_tables_size / 1024);
  fprintf (fd, "Record Tables: %u (%uKB)\n", 
                syncdb_stats.num_record_tables,
                syncdb_stats.num_record_tables_size / 1024);
  fprintf (fd, "Storable Record Tables: %u (%uKB)\n", 
                syncdb_stats.num_storable_tables,
                syncdb_stats.num_storable_tables_size / 1024);
  fprintf (fd, "Queue Tables: %u (%uKB)\n", 
                syncdb_stats.num_queues,
                syncdb_stats.num_queues_size / 1024);
  fprintf (fd, "AVL Tables: %u (%uKB)\n", 
                syncdb_stats.num_avl_trees,
                syncdb_stats.num_avl_trees_size / 1024);

  fprintf (fd, "\nTotal Commands: %llu\n", 
                syncdb_stats.num_commands);
  fprintf (fd, "Total Messages from Syncdb Agent: %llu\n", 
                syncdb_stats.num_rx_agent_msgs);
  fprintf (fd, "Total Messages to Syncdb Agent: %llu\n", 
                syncdb_stats.num_tx_agent_msgs);
  fprintf (fd, "Get Commands: %llu\n", 
                syncdb_stats.num_get_commands);
  fprintf (fd, "Field Get Commands: %llu\n", 
                syncdb_stats.num_field_get_commands);
  fprintf (fd, "GetNext Commands: %llu\n", 
                syncdb_stats.num_getNext_commands);
  fprintf (fd, "GetNextChanged Commands: %llu\n", 
                syncdb_stats.num_getNextChanged_commands);
  fprintf (fd, "Insert Commands: %llu\n", 
                syncdb_stats.num_insert_commands);
  fprintf (fd, "Set Commands: %llu\n", 
                syncdb_stats.num_set_commands);
  fprintf (fd, "Delete Commands: %llu\n", 
                syncdb_stats.num_delete_commands);
  fprintf (fd, "NSF Sync Mode Commands: %llu\n", 
                syncdb_stats.num_nsf_sync_mode_commands);
  fprintf (fd, "Debug Commands: %llu\n", 
                syncdb_stats.num_debug_commands);

  fprintf (fd, "\nTable Change Notification Commands: %u\n", 
                  syncdb_stats.num_tableChangeNotify_commands);
  fprintf (fd, "Table Create Commands: %u\n", 
                  syncdb_stats.num_tableCreate_commands);
  fprintf (fd, "Table Delete Commands: %u\n", 
                  syncdb_stats.num_tableDelete_commands);
  fprintf (fd, "Table Store Commands: %u\n", 
                  syncdb_stats.num_tableStore_commands);
  fprintf (fd, "Table Status Commands: %u\n", 
                  syncdb_stats.num_tableStatus_commands);
#ifdef L3_PERF
  fprintf (fd, "Table Lookup Time: %d usecs\n", 
                  (int) (avl_int_lookup_usecs));
  fprintf (fd, "Table Add Time: %d usecs\n", 
                  (int) (avl_int_add_usecs));
#endif

  fprintf (fd, "Client Status Commands: %u\n", 
                  syncdb_stats.num_clientStatusGet_commands);

  fprintf (fd, "\nNSF Sync Sender: %u\n", 
                          nsf_state.sync_sender);
  fprintf (fd, "NSF Max Message Size: %u\n", 
                          max_sync_msg_size);
  fprintf (fd, "NSF TX Seq: %llu\n", 
                          nsf_state.tx_seq);
  fprintf (fd, "NSF Last TX Time: %u\n", 
                          nsf_state.last_tx_time);
  fprintf (fd, "NSF Last ACK Time: %u\n", 
                          nsf_state.last_ack_time);
  fprintf (fd, "NSF ACK Pending: %u\n", 
                          nsf_state.ack_pending);
  fprintf (fd, "NSF TX Buffer Head: %u\n", 
                          nsf_state.tx_buffer_head);
  fprintf (fd, "NSF TX Buffer Tail: %u\n", 
                          nsf_state.tx_buffer_tail);
  fprintf (fd, "NSF RX Seq: %llu\n", 
                          nsf_state.rx_seq);
  fclose (fd);

  /* 
  ** Dump Per-Client Status and Statistics.
  */
  sprintf(_syncdb_debug_path, "%s%s", _syncdb_debug_path_base, "/client");
  fd = fopen (_syncdb_debug_path, "w+");
  if (fd == 0)
  {
    return;
  }

  fprintf (fd, "Per-Client Status and Statistics\n");
  for (i = 0; i < CLIENT_MAX_LIMIT; i++)
  {
    if (client_table [i] == 0)
    {
      continue;
    }
    fprintf (fd, "\n----Internal Client ID: %u\n",
             client_table[i]->client_id);
    fprintf (fd, "Client Process ID: %d\n",
             client_table[i]->client_pid);
    fprintf (fd, "Client Description: %s\n",
             client_table[i]->client_description);

    /* .... */
  }

  fclose (fd);

  /* 
  ** Dump Per-Table Status and Statistics.
  */
  sprintf(_syncdb_debug_path, "%s%s", _syncdb_debug_path_base, "/table");
  fd = fopen (_syncdb_debug_path, "w+");
  if (fd == 0)
  {
    return;
  }
  fprintf (fd, "Per-Table Status and Statistics\n");

  table_node = sorted_data_table_list;
  while (table_node)
  {
    fprintf (fd, "\nTable Name: %s\n",
             table_node->table_name
             );
    fprintf (fd, "Table Version: %d\n",
             table_node->table_version
             );
    fprintf (fd, "Table Creation Flags: 0x%x\n",
             table_node->create_flags
             );
    fprintf (fd, "Table Type: %s (%d)\n",
             data_table_name[table_node->table_type],
             table_node->table_type
             );
    if (table_node->create_flags & SYNCDB_TABLE_FLAG_STORABLE)
    {
      fprintf (fd, "Storable Table Schema Size: %d\n",
               table_node->schema_size
               );
    }
    fprintf (fd, "Allocated Memory: %u (%uKB)\n",
             table_node->total_memory_size,
             table_node->total_memory_size / 1024);
    fprintf (fd, "Record Size: %u\n", table_node->record_size);
    if (table_node->table_type == TABLE_TYPE_AVL)
    {
      fprintf (fd, "Max Records: %u\n", table_node->max_records);
      fprintf (fd, "Number of Records: %u\n", table_node->num_records);
    }
    if (table_node->table_type == TABLE_TYPE_AVL)
        
    {
      fprintf (fd, "Max Non-Deleted Records: %u\n", table_node->max_non_deleted_records);
      fprintf (fd, "Number of Non-Deleted Records: %u\n", table_node->num_non_deleted_records);
      fprintf (fd, "Key Size: %d\n", table_node->key_size);
      fprintf (fd, "Number of purges: %d\n", table_node->num_purges);
    }
    fprintf (fd, "User Data Size: %u (%uKB)\n",
              table_node->max_records * table_node->record_size,
              (table_node->max_records * table_node->record_size) / 1024
               );

    fprintf (fd, "Number of Writes: %llu\n", table_node->num_writes);
    fprintf (fd, "Number of Reads: %llu\n", table_node->num_reads);

    fprintf (fd, "Clients registered for table change notificaiton:\n");
    for (i = 0; i < CLIENT_MAX_LIMIT; i++)
    {
      if (CLIENT_MASK_IS_SET(i, table_node->notify_clients_mask))
      {
        if (!(i % 10))
        {
          fprintf(fd, "\n");
        }
        if (client_table [i])
        {
          fprintf(fd, "%6d", client_table[i]->client_id);
        }
      }
    }
    fprintf(fd, "\n");


    /* .... */

    if (table_node->create_flags & SYNCDB_TABLE_FLAG_STORABLE)
    {
      if (table_node->create_flags & SYNCDB_TABLE_FLAG_NVRAM)
      {
        sprintf(_syncdb_debug_path, "%s%s", _syncdb_debug_path_base, "/nvram");
        syncdb_database_store (table_node, _syncdb_debug_path);
      } 
#ifdef RAMDISK
      else
      {
        syncdb_database_store (table_node, SYNCDB_TMP_ROOT "/debug/ramdisk");
      }
#endif
    }

    table_node = table_node->next_sorted;
  }
  fclose (fd);

  /* 
  ** Dump Content of the RAM Disk tables. 
  ** The tables are dumped in the debug directory as opposed to their normal 
  ** RAM disk location. 
  */
  /* syncdbJSONTableWrite (SYNCDB_TMP_ROOT "/debug/ramdisk", ...) */

  /* 
  ** Dump Content of the NVRAM Disk tables. 
  ** The tables are dumped in the debug directory as opposed to their normal 
  ** NVRAM location. 
  */
  /* syncdbJSONTableWrite (SYNCDB_TMP_ROOT "/debug/nvram", ...) */
#ifdef L3_PERF
  avl_int_lookup_usecs = avl_int_add_usecs = 0;
#endif

  syncdbDebugMsgLog();
  printf("Got Debug message...\n");
}

/*********************************************************************
* @purpose  Send a message to the client.
*           
* @param    client - Destination client for this message.
* @param    msg  - Message to send to the client
* @param    msg_size  - Message size 
*
* @returns  none
*
* @notes  
*       
* @end
*********************************************************************/
void syncdbMsgSend (clientTableNode_t *client,
                   syncdbCmdMsg_t *msg,
                   int msg_size
                   )
{
  int rc;

  do
  {
    rc = sendto (client->reply_socket_id,
                 msg,
                 msg_size,
                 0,
                 (struct sockaddr *) &client->client_socket_name,
                 sizeof (struct sockaddr_un)
                 );
  } while ((rc < 0) && (errno == EINTR));
}

/*********************************************************************
* @purpose  Remove the client.
*           
* @param    client_id
*
* @returns  None
*
* @notes  
*       
* @end
*********************************************************************/
void syncdb_client_cleanup (int client_id)
{
  dataTableNode_t *table;
  int client_index;
  avlTree_t *avl_tree;
  void *node;
  syncdbAvlNodeCtrl_t *node_ctrl;
  char new_node [SYNCDB_RECORD_MAX_SIZE];

  /* Convert the client_id to the client_table index.
  */
  client_index = client_id % CLIENT_MAX_LIMIT;

  /* If this client registered for notifications in any tables then remove
  ** the notification registration.
  */
  table = sorted_data_table_list;
  while (table)
  {
    if (CLIENT_MASK_IS_SET (client_index, table->notify_clients_mask))
    {
      CLIENT_MASK_CLEAR (client_index, table->notify_clients_mask);

      /* If this is an AVL tree then check whether any nodes are in delete-pending state.
      */
      if (table->table_type == TABLE_TYPE_AVL)
      {
        /* In case there is an entry with 0 key, perform an exact search first.
        */
        avl_tree = table->record_ptr;
        memset (new_node, 0, table->record_size);
        node = avlSearch(avl_tree, new_node, AVL_EXACT);
        if (node)
        {
          node_ctrl = (syncdbAvlNodeCtrl_t *) ((char *)node + table->record_size);
          if (node_ctrl->delete_pending)
          {
            CLIENT_MASK_CLEAR (client_index, node_ctrl->changed_mask);
            /* If the deleted client is the only client on which this entry is
            ** pending then remove the entry from the AVL tree.
            */
            if (((0 == node_ctrl->nsf_node_changed_flag) || !nsf_state.sync_sender) &&
                (0 == syncdb_client_mask_check (node_ctrl->changed_mask,
                                               table->notify_clients_mask)))
            {
              node = avlDeleteEntry (avl_tree,node);
              if (!node)
              {
                abort(); 
              }
              table->num_records--;
            }
          }
        }

        /* Now search through the rest of the table using GetNext
        */
        node = avlSearch(avl_tree, new_node, AVL_NEXT);
        while (node)
        {
          node_ctrl = (syncdbAvlNodeCtrl_t *) ((char *)node + table->record_size);
          if (node_ctrl->delete_pending)
          {
            CLIENT_MASK_CLEAR (client_index, node_ctrl->changed_mask);
            /* If the deleted client is the only client on which this entry is
            ** pending then remove the entry from the AVL tree.
            */
            if (((0 == node_ctrl->nsf_node_changed_flag) || !nsf_state.sync_sender) &&
                (0 == syncdb_client_mask_check (node_ctrl->changed_mask,
                                               table->notify_clients_mask)))
            {
              node = avlDeleteEntry (avl_tree,node);
              if (!node)
              {
                abort(); 
              }
              table->num_records--;
            }
          }
          node = avlSearch(avl_tree, node, AVL_NEXT);
        }
      }
    }
    table = table->next_sorted;
  }

  close (client_table[client_index]->event_socket_id);
  close (client_table[client_index]->reply_socket_id);
  unlink (client_table[client_index]->client_notify_socket_name.sun_path);
  unlink (client_table[client_index]->client_socket_name.sun_path);
  free (client_table[client_index]);
  client_table[client_index] = 0;
  syncdb_stats.num_clients--;
}

/*********************************************************************
* @purpose  Process the Client Registration command.
*           
* @param    msg - Client Registration Message
*
* @returns  None
*
* @notes  
*       
* @end
*********************************************************************/
void syncdbCommandClientRegisterHandle (syncdbClientRegisterMsg_t *msg)
{
  syncdbCmdMsg_t reply = {};
  int i;
  int sock_buf_size;

  /* The client ID to be assigned to the next registered client.
  */
  static int next_client_id = 1;
  int client_index;

  for (i = 0; i < CLIENT_MAX_LIMIT; i++)
  {
    client_index = next_client_id % CLIENT_MAX_LIMIT;
    if (client_table[client_index] == 0)
    {
      client_table[client_index] = malloc (sizeof(clientTableNode_t));
      memset (client_table[client_index], 0, sizeof (clientTableNode_t));
      memcpy (client_table[client_index]->client_description,
              msg->client_description,
              SYNCDB_MAX_STR_LEN);

      client_table[client_index]->client_socket_name.sun_family = AF_UNIX;
      strncpy (client_table[client_index]->client_socket_name.sun_path,
              msg->client_socket_name, 
               sizeof (client_table[client_index]->client_socket_name.sun_path) - 1);

      client_table[client_index]->client_notify_socket_name.sun_family = AF_UNIX;
      strncpy (client_table[client_index]->client_notify_socket_name.sun_path,
              msg->client_notify_socket_name,
              sizeof (client_table[client_index]->client_notify_socket_name.sun_path) - 1);

      client_table[client_index]->client_pid = msg->pid;
      client_table[client_index]->client_id = next_client_id;

      client_table[client_index]->reply_socket_id = socket (AF_UNIX, SOCK_DGRAM, 0);
      if (client_table[client_index]->reply_socket_id < 0)
      {
        abort();
      }

      client_table[client_index]->event_socket_id = socket (AF_UNIX, SOCK_DGRAM, 0);
      if (client_table[client_index]->event_socket_id < 0)
      {
        abort();
      }

      sock_buf_size = 1024; /* This is the Linux minimum size */
      if (setsockopt(client_table[client_index]->event_socket_id, 
                     SOL_SOCKET, SO_SNDBUF, 
                     &sock_buf_size, sizeof(sock_buf_size)))
      {
        abort ();
      }

      break;
    }
    next_client_id++;
    if (next_client_id >= CLIENT_MAX_ID)
    {
      next_client_id = 1;
    }
  }

  reply.message_type = SYNCDB_REPLY;
  if (i < CLIENT_MAX_LIMIT)
  {
    reply.client_id = next_client_id;
    reply.rc = SYNCDB_OK;

    syncdbMsgSend (client_table[client_index], &reply, sizeof (reply));
    syncdb_stats.num_clients++;
  } else
  {
    clientTableNode_t temp_client;

    /* Since we could not add a client, we need to create a temporary socket in
    ** order to send an error reply to the client. 
    */ 
    memset (&temp_client, 0, sizeof (clientTableNode_t));
    temp_client.client_socket_name.sun_family = AF_UNIX;
    strncpy (temp_client.client_socket_name.sun_path,
            msg->client_socket_name,
             sizeof (temp_client.client_socket_name.sun_path) - 1);
    temp_client.reply_socket_id = socket (AF_UNIX, SOCK_DGRAM, 0);
    if (temp_client.reply_socket_id < 0)
    {
      abort();
    }

    reply.client_id = 0;
    reply.rc = SYNCDB_MAX_CLIENTS;

    syncdbMsgSend (&temp_client, &reply, sizeof (reply));
    close (temp_client.reply_socket_id);
  }

 syncdbClientRegisterMsgLog (msg->pid, reply.client_id, reply.rc);
}

/*********************************************************************
* @purpose  Compute the hash index for the table name.
*           
* @param    table_name - The table name. The point must point to an
*                        allocated buffer of at least SYNCDB_TABLE_NAME_SIZE
*                        bytes. The unsued memory bust be set to 0.
*
* @returns  Hash Index.
*
* @notes  
*       
* @end
*********************************************************************/
int syncdb_table_name_hash (char *table_name)
{
  int i;
  int hash_index = 0;

  for (i = 0; i < SYNCDB_TABLE_NAME_SIZE; i+= 2)
  {
    hash_index = hash_index ^ *(unsigned short *) &table_name[i];
  }

  hash_index &= (TABLE_NAME_HASH_SIZE - 1);

  return hash_index;
}

/*********************************************************************
* @purpose  Find the specified data table.
*           
* @param    table_name - The table name. The pointer must point to an
*                        allocated buffer of at least SYNCDB_TABLE_NAME_SIZE
*                        bytes. The unsued memory bust be set to 0.
*
* @returns  Pointer to the table or 0 if table is not found.
*
* @notes  
*       
* @end
*********************************************************************/
dataTableNode_t *syncdb_table_find (char *table_name)
{
  int hash_index = 0;
  dataTableNode_t *table;

  hash_index = syncdb_table_name_hash (table_name);

  table = data_table_hash [hash_index];
  while (table)
  {
    if (0 == strncmp (table->table_name, table_name, SYNCDB_TABLE_NAME_SIZE))
    {
      break;
    }
    table = table->next_hash;
  }

  return table;
}

/*********************************************************************
* @purpose  Delete the specified data table.
*           
* @param    table_name - The table name. The pointer must point to an
*                        allocated buffer of at least SYNCDB_TABLE_NAME_SIZE
*                        bytes. The unsued memory bust be set to 0.
*
* @returns  Pointer to the deleted table or 0 if table is not found.
*
* @notes  
*       
* @end
*********************************************************************/
dataTableNode_t *syncdb_table_delete (char *table_name)
{
  int hash_index = 0;
  dataTableNode_t *table, *prev_table = 0, *tmp;

  hash_index = syncdb_table_name_hash (table_name);

  table = data_table_hash [hash_index];
  while (table)
  {
    if (0 == strncmp (table->table_name, table_name, SYNCDB_TABLE_NAME_SIZE))
    {
      /* Remove the table from hash.
      */
      if (table == data_table_hash [hash_index])
      {
        data_table_hash [hash_index] = table->next_hash;
      } else
      {
        /* The prev_table should always be a non-zero value at this point.
        ** Add this check for safety.
        */
        if (prev_table)
        {
          prev_table->next_hash = table->next_hash;
        }
      }

      /* Remove the table from the sorted list.
      */
      if (sorted_data_table_list == table)
      {
        sorted_data_table_list = table->next_sorted;
      } else
      {
        tmp = sorted_data_table_list;
        while (tmp->next_sorted)
        {
          if (0 == strncmp (tmp->next_sorted->table_name, table_name, SYNCDB_TABLE_NAME_SIZE))
          {
            tmp->next_sorted = tmp->next_sorted->next_sorted;
            break;
          }
          tmp = tmp->next_sorted;
        }
      }
      break;
    }
    prev_table = table;
    table = table->next_hash;
  }

  return table;
}

/*********************************************************************
* @purpose  Insert the new data table into the hash and the sorted list.
*           
* @param    data_table - The new table to be inserted into the list.
*
* @returns  SYNCDB_OK - Table is inserted.
* @returns  SYNCDB_DUPNAME - Insertion failed because another table
*                            with the same name already exists.
*
* @notes  
*       
* @end
*********************************************************************/
int syncdb_table_add (dataTableNode_t *data_table)
{
  int hash_index;
  dataTableNode_t *table_node;

  hash_index = syncdb_table_name_hash (data_table->table_name);

  /* Check whether there are any duplicate names
  */
  table_node = data_table_hash [hash_index];
  while (table_node)
  {
    if (0 == memcmp(data_table->table_name, table_node->table_name, SYNCDB_TABLE_NAME_SIZE))
    {
      return SYNCDB_DUPNAME;
    }
    table_node = table_node->next_hash;
  }

  /* Insert the new table into the hash
  */
  data_table->next_hash = data_table_hash [hash_index];
  data_table_hash[hash_index] = data_table;

  /* Insert the new table into the sorted table list.
  */
  if (!sorted_data_table_list || 
      (0 < memcmp(sorted_data_table_list->table_name, 
                  data_table->table_name, SYNCDB_TABLE_NAME_SIZE)))
  {
    data_table->next_sorted = sorted_data_table_list;
    sorted_data_table_list = data_table;
  } else
  {
    table_node = sorted_data_table_list;
    do
    {
      if (table_node->next_sorted &&
          (0 > memcmp(table_node->next_sorted->table_name,
                      data_table->table_name, SYNCDB_TABLE_NAME_SIZE)))
      {
        table_node = table_node->next_sorted;
      } else
      {
        data_table->next_sorted = table_node->next_sorted;
        table_node->next_sorted = data_table;
        break;
      }
    } while (1);
  }
  return SYNCDB_OK;
}

/*********************************************************************
* @purpose  Register for table change notifications.
*           
* @param    client - Client Entry.
* @param    msg - Message sent by the client.
*
* @returns  None
*
* @notes  
*       
* @end
*********************************************************************/
void syncdbCommandTableChangeNotifyHandle (clientTableNode_t *client,
                                        syncdbTableChangeNotifyMsg_t *msg)
{
  syncdbCmdMsg_t reply = {};
  dataTableNode_t *data_table;
  
  reply.message_type = SYNCDB_REPLY;
  reply.rc = SYNCDB_ERROR;
  syncdb_stats.num_tableChangeNotify_commands++;

  do
  {
    data_table = syncdb_table_find (msg->table_name);
    if (!data_table)
    {
      reply.rc = SYNCDB_NO_TABLE;
      break;
    }

    CLIENT_MASK_SET (client->client_id % CLIENT_MAX_LIMIT, data_table->notify_clients_mask);
    reply.rc = SYNCDB_OK;
  } while (0);

  syncdbTableChangeNotifyMsgLog (client->client_pid, client->client_id, msg->table_name, reply.rc);
  syncdbMsgSend (client, &reply, sizeof (reply));
}

/*********************************************************************
* @purpose  Get Client Table Status
*           
* @param    client - Client Entry.
* @param    request - Message sent by the client.
*
* @returns  None
*
* @notes  
*       
* @end
*********************************************************************/
void syncdbCommandClientStatusGetHandle (clientTableNode_t *client,
                                        syncdbClientStatus_t *request)
{
  syncdbCmdMsg_t *reply;
  unsigned char reply_cmd[SYNCDB_MSG_MAX_SIZE];
  syncdbClientStatus_t *client_status;
  clientTableNode_t *search_client;
  int client_id;
  
  reply = (syncdbCmdMsg_t *) reply_cmd;
  reply->message_type = SYNCDB_REPLY;
  reply->rc = SYNCDB_ERROR;
  syncdb_stats.num_clientStatusGet_commands++;
  client_id = request->client_id;

  do
  {
    search_client = client_table [client_id % CLIENT_MAX_LIMIT];
    if (!search_client)
    {
      reply->rc = SYNCDB_NOT_FOUND;
      break;
    }

    client_status = (syncdbClientStatus_t *) ((char *)reply + sizeof (syncdbCmdMsg_t));
    memset (client_status, 0, sizeof (syncdbClientStatus_t));

    client_status->client_id = search_client->client_id;
    memcpy (client_status->client_description, search_client->client_description, SYNCDB_MAX_STR_LEN);
    client_status->client_pid = search_client->client_pid;
    client_status->num_commands = search_client->num_commands;
    client_status->num_table_change_events = search_client->num_table_change_events;
    client_status->num_table_purges = search_client->num_table_purges;

    reply->rc = SYNCDB_OK;
  } while (0);

  syncdbClientStatusMsgLog (client->client_pid, client->client_id, client_id, reply->rc);
  syncdbMsgSend (client, reply, sizeof (syncdbCmdMsg_t) + sizeof (syncdbClientStatus_t));
}

/*********************************************************************
* @purpose  Get Data Table Status.
*           
* @param    client - Client Entry.
* @param    num_tables - Number of tables for which to retrieve the status.
* @param    table_list - Table list for which to retrieve status.
*
* @returns  None
*
* @notes  
*       
* @end
*********************************************************************/
void syncdbCommandTableStatusGetHandle (clientTableNode_t *client,
                                        int num_tables,
                                        syncdbDataTableStatus_t *table_list)
{
  syncdbCmdMsg_t *reply;
  dataTableNode_t *data_table;
  unsigned char reply_cmd[SYNCDB_MSG_MAX_SIZE];
  syncdbDataTableStatus_t *table_status;
  int i;
  int client_index;

  reply = (syncdbCmdMsg_t *) reply_cmd;
  
  reply->message_type = SYNCDB_REPLY;
  reply->rc = SYNCDB_OK;
  syncdb_stats.num_tableStatus_commands++;
  client_index = client->client_id % CLIENT_MAX_LIMIT;

  table_status = (syncdbDataTableStatus_t *) &reply_cmd [sizeof(syncdbCmdMsg_t)];
  for (i = 0; i < num_tables; i++)
  {
    memset (table_status, 0, sizeof (syncdbDataTableStatus_t));
    memcpy (table_status->table_name, table_list[i].table_name, SYNCDB_TABLE_NAME_SIZE);
    data_table = syncdb_table_find (table_list[i].table_name);

    if (data_table)
    {
      table_status->table_flags = data_table->create_flags;
      table_status->table_version = data_table->table_version;
      table_status->table_status |= SYNCDB_TABLE_STAT_EXISTS;
      table_status->table_type = data_table->table_type;
      table_status->num_elements = data_table->num_records;
      table_status->num_non_deleted_elements = data_table->num_non_deleted_records;
      if (CLIENT_MASK_IS_SET(client_index, data_table->table_changed_client_mask))
      {
        CLIENT_MASK_CLEAR (client_index, data_table->table_changed_client_mask);
        table_status->table_status |= SYNCDB_TABLE_STAT_CHANGED;
      }
      if (CLIENT_MASK_IS_SET(client_index, data_table->table_created_client_mask))
      {
        CLIENT_MASK_CLEAR (client_index, data_table->table_created_client_mask);
        table_status->table_status |= SYNCDB_TABLE_STAT_NEW_TABLE;
      }
      if (CLIENT_MASK_IS_SET(client_index, data_table->table_purged_client_mask))
      {
        CLIENT_MASK_CLEAR (client_index, data_table->table_purged_client_mask);
        table_status->table_status |= SYNCDB_TABLE_STAT_AVL_TREE_PURGED;
      }
    }
    table_status++;
  }

  syncdbTableStatusGetMsgLog (client->client_pid, client->client_id, num_tables, reply->rc);
  syncdbMsgSend (client, reply, 
                 sizeof (syncdbCmdMsg_t) + (num_tables * sizeof (syncdbDataTableStatus_t)));
}

/*********************************************************************
* @purpose  Process the AVL Table Create Command.
*           
* @param    client - Client Entry.
* @param    msg - Message sent by the client.
* @param    schema - Pointer to the start of the schema for storable
*                    tables.
*
* @returns  None
*
* @notes  
*       
* @end
*********************************************************************/
void syncdbCommandAvlTableCreateHandle (clientTableNode_t *client,
                                        syncdbTableCreateMsg_t *msg,
                                        char * schema
                                        )
{
  syncdbCmdMsg_t reply = {};
  dataTableNode_t *data_table;
  avlTree_t *avl_tree;
  avlTreeTables_t *tree_heap;
  void *node_heap;
  int  node_size;
  int  mem_size;
  char *mem_position;
  
  reply.message_type = SYNCDB_REPLY;
  reply.rc = SYNCDB_ERROR;
  syncdb_stats.num_tableCreate_commands++;

  do
  {
    /* Compute how much memory is needed for the AVL table. All data and control
    ** structures are allocated with a single malloc().
    */

    /* Each data node requires control information.
    */
    node_size = msg->node_size + sizeof (syncdbAvlNodeCtrl_t);
    mem_size = node_size * msg->max_elements;

    /* Need control structure for each node.
    */
    mem_size += (sizeof (avlTreeTables_t) * msg->max_elements);

    /* Need control structure for the tree.
    */
    mem_size += sizeof (avlTree_t);

    /* Need control structure for the syncdb table
    */
    mem_size += sizeof (dataTableNode_t);

    /* Need space for the schema. Only writable rables have a non-zero
    ** schema size.
    */
    mem_size += msg->schema_size;

    data_table = malloc (mem_size);
    if (!data_table)
    {
      abort();
    }

    memset (data_table, 0, sizeof (dataTableNode_t));
    strncpy (data_table->table_name,
             msg->table_name,
             SYNCDB_TABLE_NAME_SIZE - 1);

    /* Insert the table into the hash.
    */
    reply.rc = syncdb_table_add (data_table);
    if (reply.rc != SYNCDB_OK)
    {
      free (data_table);
      break;
    }

    /* Populate the data table attributes.
    */
    data_table->table_version = msg->table_version;
    data_table->create_flags = msg->flags;
    data_table->schema_size = msg->schema_size;
    data_table->max_records = msg->max_elements;
    data_table->max_non_deleted_records = msg->max_live_elements;
    data_table->record_size = msg->node_size;
    data_table->total_memory_size = mem_size;  
    data_table->key_size = msg->key_size;
    data_table->table_type = TABLE_TYPE_AVL;
    data_table->nsf_table_changed_flag = 1;
    data_table->nsf_table_purge_flag = 1;
    memset (data_table->table_created_client_mask, 0xff, CLIENT_MASK_SIZE);
    memset (data_table->table_changed_client_mask, 0xff, CLIENT_MASK_SIZE);

    mem_position = (char *) data_table + sizeof (dataTableNode_t);
    data_table->record_ptr = mem_position;

    /* If the table is writable then set the schema pointer.
    */
    if (data_table->create_flags & SYNCDB_TABLE_FLAG_STORABLE)
    {
      data_table->schema = ((char *)data_table) + (mem_size - data_table->schema_size);
      memcpy (data_table->schema, schema, data_table->schema_size);
    } else
    {
      data_table->schema = 0;
    }

    /* Set up the AVL tree pointers in the appropriate places in the allocated buffer.
    */
    avl_tree = (avlTree_t *) mem_position;
    mem_position += sizeof (avlTree_t);

    tree_heap = (avlTreeTables_t *) mem_position;
    mem_position += sizeof (avlTreeTables_t) * data_table->max_records;

    node_heap = mem_position;

    avlCreateAvlTreeProcLib (avl_tree, tree_heap, node_heap, 
                             data_table->max_records, 
                             node_size,
                             0x10,
                             data_table->key_size);


    /* Populate the tree from a file if needed.
    */
    if ((data_table->create_flags & SYNCDB_TABLE_FLAG_FILE_LOAD) &&
       (data_table->create_flags & SYNCDB_TABLE_FLAG_STORABLE))
    {
      syncdb_database_read (data_table);
    }

    syncdb_stats.num_avl_trees++;
    syncdb_stats.num_tables++;
    syncdb_stats.num_tables_size += mem_size;
    syncdb_stats.num_avl_trees_size += mem_size;
  } while (0);

  syncdbAvlTableCreateMsgLog (client->client_pid, client->client_id, msg->table_name, reply.rc);
  syncdbMsgSend (client, &reply, sizeof (reply));
}

/*********************************************************************
* @purpose  Process the Record Table Create Command.
*           
* @param    client - Client Entry.
* @param    msg - Message sent by the client.
* @param    schema - Pointer to the start of the schema for storable
*                    tables.
*
* @returns  None
*
* @notes  
*       
* @end
*********************************************************************/
void syncdbCommandRecordTableCreateHandle (clientTableNode_t *client,
                                        syncdbTableCreateMsg_t *msg,
                                        char * schema
                                        )
{
  syncdbCmdMsg_t reply = {};
  dataTableNode_t *data_table;
  int  mem_size;
  char *mem_position;
  
  reply.message_type = SYNCDB_REPLY;
  reply.rc = SYNCDB_ERROR;
  syncdb_stats.num_tableCreate_commands++;

  do
  {
    /* Compute how much memory is needed for the Record table. All data and control
    ** structures are allocated with a single malloc().
    */
    mem_size = msg->node_size;

    /* Need control structure for the syncdb table
    */
    mem_size += sizeof (dataTableNode_t);

    /* Need space for the schema. Only writable rables have a non-zero
    ** schema size.
    */
    mem_size += msg->schema_size;

    data_table = malloc (mem_size);
    if (!data_table)
    {
      abort();
    }

    memset (data_table, 0, sizeof (dataTableNode_t));
    strncpy (data_table->table_name,
             msg->table_name,
             SYNCDB_TABLE_NAME_SIZE - 1);

    /* Insert the table into the hash.
    */
    reply.rc = syncdb_table_add (data_table);
    if (reply.rc != SYNCDB_OK)
    {
      free (data_table);
      break;
    }

    /* Populate the data table attributes.
    */
    data_table->table_version = msg->table_version;
    data_table->create_flags = msg->flags;
    data_table->schema_size = msg->schema_size;
    data_table->max_records = 1;
    data_table->max_non_deleted_records = 1;
    data_table->record_size = msg->node_size;
    data_table->total_memory_size = mem_size;  
    data_table->key_size = 0;
    data_table->table_type = TABLE_TYPE_RECORD;
    data_table->nsf_table_changed_flag = 1;
    data_table->nsf_table_purge_flag = 0;  /* Ignored on this table type */
    memset (data_table->table_created_client_mask, 0xff, CLIENT_MASK_SIZE);
    memset (data_table->table_changed_client_mask, 0xff, CLIENT_MASK_SIZE);

    mem_position = (char *) data_table + sizeof (dataTableNode_t);
    data_table->record_ptr = mem_position;

    /* If the table is writable then set the schema pointer.
    */
    if (data_table->create_flags & SYNCDB_TABLE_FLAG_STORABLE)
    {
      data_table->schema = ((char *)data_table) + (mem_size - data_table->schema_size);
      memcpy (data_table->schema, schema, data_table->schema_size);
    } else
    {
      data_table->schema = 0;
    }

    /* Set all data to 0.
    */
    memset (data_table->record_ptr, 0, data_table->record_size);

    /* Populate the record from a file if needed.
    ** If a file does not exist then the record will be populated with 
    ** the defaults from the schema. 
    */
    if (data_table->create_flags & SYNCDB_TABLE_FLAG_FILE_LOAD)
    {
      syncdb_database_read (data_table);
    }

    syncdb_stats.num_record_tables++;
    syncdb_stats.num_tables++;
    syncdb_stats.num_tables_size += mem_size;
    syncdb_stats.num_record_tables_size += mem_size;
  } while (0);

  syncdbRecordTableCreateMsgLog (client->client_pid, client->client_id, msg->table_name, reply.rc);
  syncdbMsgSend (client, &reply, sizeof (reply));
}

/*********************************************************************
* @purpose  Process the Table Delete Command.
*           
* @param    client - Client Entry.
* @param    msg - Message sent by the client.
*
* @returns  None
*
* @notes  
*       
* @end
*********************************************************************/
void syncdbCommandTableDeleteHandle (clientTableNode_t *client,
                                        syncdbTableDeleteMsg_t *msg)
{
  syncdbCmdMsg_t reply = {};
  dataTableNode_t *data_table;
  
  reply.message_type = SYNCDB_REPLY;
  reply.rc = SYNCDB_ERROR;
  syncdb_stats.num_tableDelete_commands++;

  do
  {
    /* Delete the table from the table hash.
    ** If table is not found then return an error.
    */
    data_table = syncdb_table_delete (msg->table_name);
    if (!data_table)
    {
      reply.rc = SYNCDB_NO_TABLE;
      break;
    }

    /* Notify clients that something has changed.
    ** The syncdb does not wait for clients to acknowledge the table deletion 
    ** before deleting the datable and all its content. 
    ** The clients are expected to figure out that the table is deleted when 
    ** they get an error indication from syncdbTableStatusGet() or another 
    ** table API function. 
    */
    memset (data_table->table_changed_client_mask, 0xff, CLIENT_MASK_SIZE);
    syncdb_table_change_notify (data_table, client->client_id);


    if (data_table->table_type == TABLE_TYPE_AVL)
    {
      syncdb_stats.num_avl_trees--;
      syncdb_stats.num_avl_trees_size -= data_table->total_memory_size;
    }

    syncdb_stats.num_tables--;
    syncdb_stats.num_tables_size -= data_table->total_memory_size;

    free (data_table);
    reply.rc = SYNCDB_OK;
  } while (0);

  syncdbTableDeleteMsgLog (client->client_pid, client->client_id, msg->table_name, reply.rc);
  syncdbMsgSend (client, &reply, sizeof (reply));
}

/*********************************************************************
* @purpose  Process the Table Store Command.
*           
* @param    client - Client Entry.
* @param    msg - Message sent by the client.
*
* @returns  None
*
* @notes  
*       
* @end
*********************************************************************/
void syncdbCommandTableStoreHandle (clientTableNode_t *client,
                                        syncdbTableStoreMsg_t *msg)
{
  syncdbCmdMsg_t reply = {};
  dataTableNode_t *data_table;
  
  reply.message_type = SYNCDB_REPLY;
  reply.rc = SYNCDB_ERROR;
  syncdb_stats.num_tableStore_commands++;

  do
  {
    if (msg->all_tables)
    {
      data_table = 0;
    } else
    {
      /* If table is not found then return an error.
      */
      data_table = syncdb_table_find (msg->table_name);
      if (!data_table)
      {
        reply.rc = SYNCDB_NO_TABLE;
        break;
      }

      if (!(data_table->create_flags & SYNCDB_TABLE_FLAG_STORABLE))
      {
        reply.rc = SYNCDB_NO_TABLE;
        break;
      }
    }

    if (data_table)
    {
      if (data_table->create_flags & SYNCDB_TABLE_FLAG_NVRAM)
      {
        syncdb_database_store (data_table, _syncdb_nv_path);
      } 
#ifdef RAMDISK
      else
      {
        syncdb_database_store (data_table, SYNCDB_TMP_ROOT "/ramdisk");
      }
#endif
    } else
    {
      /* When saving all files, clear out the old directory content.
      */
#ifdef RAMDISK
      system ("rm -rf " SYNCDB_TMP_ROOT "/ramdisk");
#endif
      sprintf(_syncdb_command, "%s%s", "rm -rf ", _syncdb_nv_path);
      system (_syncdb_command);
#ifdef RAMDISK
      system ("mkdir " SYNCDB_TMP_ROOT "/ramdisk");
#endif
      sprintf(_syncdb_command, "%s%s", "mkdir ", _syncdb_nv_path);
      system (_syncdb_command);
      data_table = sorted_data_table_list;
      while (data_table)
      {
        if (data_table->create_flags & SYNCDB_TABLE_FLAG_STORABLE)
        {
          if (data_table->create_flags & SYNCDB_TABLE_FLAG_NVRAM)
          {
            syncdb_database_store (data_table, _syncdb_nv_path);
          } 
#ifdef RAMDISK
          else
          {
            syncdb_database_store (data_table, SYNCDB_TMP_ROOT "/ramdisk");
          }
#endif
        }
        data_table = data_table->next_sorted;
      }
    }

    /* If the nvram flag is set then archive files in permanent storage.
    */
    if (msg->nvram)
    {
      sprintf(_syncdb_command, "%s%s%s%s", "tar -zcf ", _syncdb_db_file, " ",
              _syncdb_nv_path);
      system (_syncdb_command);
      sprintf(_syncdb_command, "%s%s", "rm -rf ", _syncdb_nv_path);
      system (_syncdb_command);
    }
    sprintf(_syncdb_command, "%s%s%s", "rm -rf ", _syncdb_nv_path_base, "/tmp-schema");
    system (_syncdb_command);
    sprintf(_syncdb_command, "%s%s", "rm -rf ", _syncdb_debug_path_base);
    system (_syncdb_command);
    
    reply.rc = SYNCDB_OK;
  } while (0);

  syncdbTableStoreMsgLog (client->client_pid, 
                          client->client_id, 
                          msg->all_tables?"(All-Tables)":msg->table_name, 
                          reply.rc);
  syncdbMsgSend (client, &reply, sizeof (reply));
}

void syncdbTablePrint()
{
    dataTableNode_t *data_table;

    data_table = sorted_data_table_list;
    while (data_table)
    {
        printf("Table file %s/%s--data\n", _syncdb_nv_path, data_table->table_name);
        data_table = data_table->next_sorted;
    }
}

/*********************************************************************
* @purpose  Delete entries with delete-pending flag from the
*           AVL tree.
*           
* @param    avl_tree - Pointer to the AVL tree structure.
*
* @returns  None
*
* @notes  
*       
* @end
*********************************************************************/
void syncdb_purge_avl_tree (avlTree_t *avl_tree, 
                            void *new_node,
                            dataTableNode_t *data_table)
{
  syncdbAvlNodeCtrl_t *node_ctrl;
  void *node;

  /* Zero out the key. Perform an EXACT search in case there is an
  ** entry with zero key in the table.
  */
  memset (new_node, 0, data_table->record_size);
  node = avlSearch(avl_tree, new_node, AVL_EXACT);
  if (node)
  {
    node_ctrl = (syncdbAvlNodeCtrl_t *) ((char *)node + data_table->record_size);
    if (node_ctrl->delete_pending)
    {
      node = avlDeleteEntry (avl_tree,node);
      if (!node)
      {
        abort(); 
      }
      data_table->num_records--;
    }
  }

  /* Now search through the rest of the table using GetNext and delete all
  ** entries with delete-pending flag.
  */
  node = avlSearch(avl_tree, new_node, AVL_NEXT);
  while (node)
  {
    node_ctrl = (syncdbAvlNodeCtrl_t *) ((char *)node + data_table->record_size);
    if (node_ctrl->delete_pending)
    {
      node = avlDeleteEntry (avl_tree,node);
      if (!node)
      {
        abort(); 
      }
      data_table->num_records--;
    }
    node = avlSearch(avl_tree, node, AVL_NEXT);
  }
  memset (data_table->table_purged_client_mask, 0xff, CLIENT_MASK_SIZE);
  data_table->nsf_table_purge_flag = 1;
}

/*********************************************************************
* @purpose  Notify clients that registered for table changes
*           about the change.
*           
* @param    data_table - Table that changed.
* @param    exclude_client_id - Client that makes the changes
*                   is not notified about the changes.
*
* @returns  None
*
* @notes  
*       
* @end
*********************************************************************/
void syncdb_table_change_notify (dataTableNode_t *data_table,
                                 int exclude_client_id)
                            
{
  int i;
  static char buf[1024] = {};

  for (i = 0; i < CLIENT_MAX_LIMIT; i++)
  {
    if (!client_table[i] || (client_table[i]->client_id == exclude_client_id))
    {
      continue;
    }

    if (CLIENT_MASK_IS_SET (i, data_table->notify_clients_mask))
    {
      (void) sendto(client_table[i]->event_socket_id, 
                    buf, 
                    sizeof(buf), 
                    MSG_DONTWAIT,
                    (struct sockaddr *) &client_table[i]->client_notify_socket_name, 
                    sizeof(struct sockaddr_un));
    }
  }
}

/*********************************************************************
* @purpose  Process the "Insert" command.
*           
* @param    client - Client Record.
* @param    msg - Message sent by the client.
* @param    new_element - Message sent by the client.
*
* @returns  None
*
* @notes  This command is valid for AVL Trees. 
*       
* @end
*********************************************************************/
void syncdbCommandInsertHandle (clientTableNode_t *client,
                                syncdbGenericSetMsg_t *msg,
                                void *new_element)
{
  syncdbCmdMsg_t reply = {};
  dataTableNode_t *data_table;
  avlTree_t *avl_tree;
  void *node;
  unsigned char new_node [SYNCDB_RECORD_MAX_SIZE + sizeof (syncdbAvlNodeCtrl_t)];
  syncdbAvlNodeCtrl_t *node_ctrl;
#ifdef L3_PERF
  struct timeval cur_time1, cur_time2; 
  gettimeofday(&cur_time1, NULL);
#endif
  
  reply.message_type = SYNCDB_REPLY;
  reply.rc = SYNCDB_ERROR;
  syncdb_stats.num_insert_commands++;

  do
  {
    /* Make sure that the specified table exists.
    */
    data_table = syncdb_table_find (msg->table_name);
    if (!data_table)
    {
      reply.rc = SYNCDB_NO_TABLE;
      break;
    }

    /* The Insert command is applicable only to AVL trees.
    */
    if (data_table->table_type != TABLE_TYPE_AVL) 
    {
      break;
    }

    data_table->num_writes++;

    /* Verify that the record size is valid.
    */
    if (msg->size != data_table->record_size)
    {
      reply.rc = SYNCDB_SIZE;
      break;
    }
    avl_tree = data_table->record_ptr;
    node = avlSearch(avl_tree, new_element, AVL_EXACT);

    /* If the node does not already exist then try to create it.
    */
    if (!node)
    {
        if (data_table->num_non_deleted_records == data_table->max_non_deleted_records)
        {
          reply.rc = SYNCDB_FULL;
          break;
        }

        /* If the number of non-deleted elements in the table is less than maximum,
        ** but the table is full due to delete-pending entries then purge all 
        ** delete-pending entries from the table. 
        ** Notify the table users that the table has been purged. 
        ** The users will need to completely resycnrhonize with this table. 
        */
        if (data_table->num_records == data_table->max_records)
        {
          data_table->num_purges++;
          syncdb_purge_avl_tree (avl_tree, new_node, data_table);
        }

        memcpy (new_node, new_element, msg->size);
        node_ctrl = (syncdbAvlNodeCtrl_t *)((char *)new_node + msg->size);
        memset (node_ctrl, 0, sizeof (syncdbAvlNodeCtrl_t));
        memset (node_ctrl->changed_mask, 0xff, CLIENT_MASK_SIZE);
        if (data_table->create_flags & SYNCDB_TABLE_FLAG_NSF)
        {
          node_ctrl->nsf_node_changed_flag = 1;
        }

        node = avlInsertEntry(avl_tree,  new_node);
        if (node)
        {
          /* If the avlInsertEntry() failed then we have a code error.
          ** We already verified that the tree has room and the entry 
          ** is not duplicate.  
          */
          abort ();
        }
        data_table->num_non_deleted_records++;
        data_table->num_records++;
    } else
    {
        /* Handle the case where the specified entry is already in the AVL tree.
        */
        node_ctrl = (syncdbAvlNodeCtrl_t *)((char *)node + msg->size);

        /* If the node was in delete-pending state and we still have room in the table
        ** then clear the delete pending flag. 
        ** If there is no room in the table then return SYNCDB_FULL error.
        */
        if ((node_ctrl->delete_pending) &&
            (data_table->num_non_deleted_records == data_table->max_non_deleted_records))
        {
          reply.rc = SYNCDB_FULL;
          break;
        }
        if (node_ctrl->delete_pending)
        {
          node_ctrl->delete_pending = 0;
          data_table->num_non_deleted_records++;
        } else
        {
          /* If the table is created with the SYNCDB_TABLE_FLAG_EXISTS then return
          ** an error.
          */
          if (data_table->create_flags & SYNCDB_TABLE_FLAG_EXISTS)
          {
            reply.rc = SYNCDB_ENTRY_EXISTS;
            break;
          }
        }

        /* The node already exists. We just need to update it with the new data.
        */
        memcpy (node, new_element, msg->size);
        memset (node_ctrl->changed_mask, 0xff, CLIENT_MASK_SIZE);
        if (data_table->create_flags & SYNCDB_TABLE_FLAG_NSF)
        {
          node_ctrl->nsf_node_changed_flag = 1;
        }
     }
     memset (data_table->table_changed_client_mask, 0xff, CLIENT_MASK_SIZE);
     data_table->nsf_table_changed_flag = 1;
     syncdb_table_change_notify (data_table, client->client_id);
     reply.rc = SYNCDB_OK;
  } while (0);

  syncdbInsertMsgLog (client->client_pid, client->client_id, msg->table_name, reply.rc);
#ifdef L3_PERF
  gettimeofday(&cur_time2, NULL);
  avl_int_add_usecs += (cur_time2.tv_usec >= cur_time1.tv_usec) ?
      (cur_time2.tv_usec - cur_time1.tv_usec) :
      (1000000 - cur_time1.tv_usec) + cur_time2.tv_usec;
#endif
  syncdbMsgSend (client, &reply, sizeof (reply));
}

/*********************************************************************
* @purpose  Process the "Delete" command.
*           
* @param    client - Client Record.
* @param    msg - Message sent by the client.
* @param    new_element - Message sent by the client.
*
* @returns  None
*
* @notes  This command is valid for AVL Trees. 
*       
* @end
*********************************************************************/
void syncdbCommandDeleteHandle (clientTableNode_t *client,
                                syncdbGenericSetMsg_t *msg,
                                void *new_element)
{
  syncdbCmdMsg_t reply = {};
  dataTableNode_t *data_table;
  avlTree_t *avl_tree;
  void *node;
  syncdbAvlNodeCtrl_t *node_ctrl;
  int notification_needed;
  int i;
  unsigned char notification_mask [CLIENT_MASK_SIZE];
  
  reply.message_type = SYNCDB_REPLY;
  reply.rc = SYNCDB_ERROR;
  syncdb_stats.num_delete_commands++;

  do
  {
    /* Make sure that the specified table exists.
    */
    data_table = syncdb_table_find (msg->table_name);
    if (!data_table)
    {
      reply.rc = SYNCDB_NO_TABLE;
      break;
    }

    /* The Insert command is applicable only to AVL trees and queues.
    */
    if (data_table->table_type != TABLE_TYPE_AVL) 
    {
      break;
    }

    data_table->num_writes++;

    /* Verify that the record size is valid.
    */
    if (msg->size != data_table->record_size)
    {
      reply.rc = SYNCDB_SIZE;
      break;
    }
    avl_tree = data_table->record_ptr;
    node = avlSearch(avl_tree, new_element, AVL_EXACT);

    /* Verify that the node is in the table.
    */
    if (!node)
    {
      reply.rc = SYNCDB_NOT_FOUND;
      break;
    }

    node_ctrl = (syncdbAvlNodeCtrl_t *)((char *)node + msg->size);

    /* If the node is already in delete-pending state then return the
    ** "Not Found" error code to the caller. 
    */ 
    if (node_ctrl->delete_pending)
    {
      reply.rc = SYNCDB_NOT_FOUND;
      break;
    }

    /* If any applications are registered to be notified of table changes then
    ** mark the entry as delete-pending. The entry will be removed from the 
    ** tree when all applications aknowledge the delete event. 
    */
    memcpy (notification_mask, data_table->notify_clients_mask, CLIENT_MASK_SIZE);

    /* If the table is enabled for NSF then mark the entry for synchronization.
    */
    if (data_table->create_flags & SYNCDB_TABLE_FLAG_NSF)
    {
      node_ctrl->nsf_node_changed_flag = 1;
    }

    /* Remove the client which is calling the Delete function from the
    ** notification mask.
    */
    CLIENT_MASK_CLEAR (client->client_id % CLIENT_MAX_LIMIT, notification_mask);

    notification_needed = 0;
    for (i = 0; i < CLIENT_MASK_SIZE; i++)
    {
      notification_needed |= notification_mask[i];
    }

    if (notification_needed || (node_ctrl->nsf_node_changed_flag && nsf_state.sync_sender))
    {
      if (notification_needed)
      {
        memset (node_ctrl->changed_mask, 0xff, CLIENT_MASK_SIZE);
        CLIENT_MASK_CLEAR (client->client_id % CLIENT_MAX_LIMIT, node_ctrl->changed_mask);
      }
      node_ctrl->delete_pending = 1;

    } else
    {
      /* Delete the node from the AVL tree.
      */
      node = avlDeleteEntry (avl_tree, node);
      if (!node)
      {
        abort();  /* Not expecting this operation to fail */
      }
      data_table->num_records--;
    }
    data_table->num_non_deleted_records--;
    memset (data_table->table_changed_client_mask, 0xff, CLIENT_MASK_SIZE);
    data_table->nsf_table_changed_flag = 1;
    syncdb_table_change_notify (data_table, client->client_id);
    reply.rc = SYNCDB_OK;

  } while (0);

  syncdbDeleteMsgLog (client->client_pid, client->client_id, msg->table_name, reply.rc);
  syncdbMsgSend (client, &reply, sizeof (reply));
}

/*********************************************************************
* @purpose  Process the "Set" command.
*           
* @param    command - The type of Set Request.
* @param    client - Client Record.
* @param    msg - Message sent by the client.
* @param    new_element - Message sent by the client.
*
* @returns  None
*
* @notes  This command is valid for AVL Trees, Queues, and Records. 
*       
* @end
*********************************************************************/
void syncdbCommandSetHandle (syncdbMsg_t command,
                                clientTableNode_t *client,
                                syncdbGenericSetMsg_t *msg,
                                void *new_element)
{
  syncdbCmdMsg_t reply = {};
  dataTableNode_t *data_table;
  avlTree_t *avl_tree;
  void *node;
  syncdbAvlNodeCtrl_t *node_ctrl;
  
  reply.message_type = SYNCDB_REPLY;
  reply.rc = SYNCDB_ERROR;
  syncdb_stats.num_set_commands++;

  do
  {
    /* Make sure that the specified table exists.
    */
    data_table = syncdb_table_find (msg->table_name);
    if (!data_table)
    {
      reply.rc = SYNCDB_NO_TABLE;
      break;
    }

    /* The Insert command is applicable to AVL trees 
    ** and records.
    */
    if ((data_table->table_type != TABLE_TYPE_AVL) &&
        (data_table->table_type != TABLE_TYPE_RECORD))
    {
      break;
    }

    data_table->num_writes++;

    /* Verify that the record size is valid.
    */
    if (msg->size != data_table->record_size)
    {
      reply.rc = SYNCDB_SIZE;
      break;
    }

    if (data_table->table_type == TABLE_TYPE_AVL)
    {
      avl_tree = data_table->record_ptr;
      node = avlSearch(avl_tree, new_element, AVL_EXACT);

      /* If the record does not exist or the record is in delete-pending state
      ** then return an error. 
      */
      if (!node)
      {
        reply.rc = SYNCDB_NOT_FOUND;
        break;
      }
      node_ctrl = (syncdbAvlNodeCtrl_t *)((char *)node + msg->size);
      if (node_ctrl->delete_pending)
      {
        reply.rc = SYNCDB_NOT_FOUND;
        break;
      }

      /* Update the element with the new data.
      */
      if (command == SYNCDB_FIELD_SET)
      {
        /* Only the specified field is updated.
        */
        memcpy (((unsigned char *) node) + msg->field_offset, 
                ((unsigned char *) new_element) + msg->field_offset, 
                msg->field_size);
      } else
      {
        memcpy (node, new_element, msg->size);
      }
      memset (node_ctrl->changed_mask, 0xff, CLIENT_MASK_SIZE);

      /* If the table is enabled for NSF then mark the entry for synchronization.
      */
      if (data_table->create_flags & SYNCDB_TABLE_FLAG_NSF)
      {
        node_ctrl->nsf_node_changed_flag = 1;
      }

    } else if (data_table->table_type == TABLE_TYPE_RECORD)
    {
      if (command == SYNCDB_FIELD_SET)
      {
        /* Only the specified field is updated.
        */
        memcpy (((unsigned char *) data_table->record_ptr) + msg->field_offset, 
                ((unsigned char *) new_element) + msg->field_offset, 
                msg->field_size);
      } else
      {
        memcpy (data_table->record_ptr, new_element, msg->size);
      }
    }
    memset (data_table->table_changed_client_mask, 0xff, CLIENT_MASK_SIZE);
    data_table->nsf_table_changed_flag = 1;
    syncdb_table_change_notify (data_table, client->client_id);
    reply.rc = SYNCDB_OK;
  } while (0);

  if (command == SYNCDB_FIELD_SET)
  {
    syncdbFieldSetMsgLog (client->client_pid, client->client_id, msg->table_name, 
                          msg->field_offset, msg->field_size, reply.rc);
  } else
  {
    syncdbSetMsgLog (client->client_pid, client->client_id, msg->table_name, reply.rc);
  }
  syncdbMsgSend (client, &reply, sizeof (reply));
}

/*********************************************************************
* @purpose  Process the "Get/GetNext/GetNextChanged" command.
*           
* @param    command - The type of Get Request.
* @param    client - Client Record.
* @param    msg - Message sent by the client.
* @param    new_element - Memory where to copy the data.
*
* @returns  None
*
* @notes  This command is valid for AVL Trees, Queues, and Records. 
*       
* @end
*********************************************************************/
void syncdbCommandGenericGetHandle (syncdbMsg_t command,
                                    clientTableNode_t *client,
                                    syncdbGenericGetMsg_t *msg,
                                    void *new_element)
{
  syncdbCmdMsg_t *reply;
  dataTableNode_t *data_table;
  avlTree_t *avl_tree;
  void *node = 0;
  syncdbAvlNodeCtrl_t *node_ctrl;
  unsigned char reply_cmd[SYNCDB_MSG_MAX_SIZE];
  int client_index;
#ifdef L3_PERF
  struct timeval cur_time1, cur_time2; 
  gettimeofday(&cur_time1, NULL);
#endif

  reply = (syncdbCmdMsg_t *) reply_cmd;
  memset (reply, 0, sizeof (syncdbCmdMsg_t));

  reply->message_type = SYNCDB_REPLY;
  reply->rc = SYNCDB_ERROR;
  client_index = client->client_id % CLIENT_MAX_LIMIT;

  if (command == SYNCDB_GET) 
  {
    syncdb_stats.num_get_commands++;
  } else if (command == SYNCDB_FIELD_GET)
  {
    syncdb_stats.num_field_get_commands++;
  } else if (command == SYNCDB_GETNEXT)
  {
    syncdb_stats.num_getNext_commands++;
  } else if (command == SYNCDB_GETNEXT_CHANGED)
  {
    syncdb_stats.num_getNextChanged_commands++;
  }
  do
  {
    /* Make sure that the specified table exists.
    */
    data_table = syncdb_table_find (msg->table_name);
    if (!data_table)
    {
      reply->rc = SYNCDB_NO_TABLE;
      break;
    }

    /* The Get command is applicable only to AVL trees 
    ** and records. 
    ** The GetNext command is valid for AVL trees and queues. 
    ** The GetNextChanged command is valid 
    ** only for AVL trees. 
    */
    if (((command == SYNCDB_GET) || (command == SYNCDB_FIELD_GET)) &&
        (data_table->table_type != TABLE_TYPE_AVL) &&
        (data_table->table_type != TABLE_TYPE_RECORD))
    {
      break;
    } else if ((command == SYNCDB_GETNEXT) && 
               (data_table->table_type != TABLE_TYPE_AVL))
    {
      break;
    } else if ((command == SYNCDB_GETNEXT_CHANGED) &&
               (data_table->table_type != TABLE_TYPE_AVL))
    {
      break;
    }

    data_table->num_reads++;

    /* Verify that the record size is valid.
    */
    if (msg->size != data_table->record_size)
    {
      reply->rc = SYNCDB_SIZE;
      break;
    }

    if (data_table->table_type == TABLE_TYPE_AVL)
    {
      avl_tree = data_table->record_ptr;
      if ((command == SYNCDB_GET) || (command == SYNCDB_FIELD_GET))
      {
        node = avlSearch(avl_tree, new_element, AVL_EXACT);
      } else if (command == SYNCDB_GETNEXT)
      {
        node = avlSearch(avl_tree, new_element, AVL_NEXT);
      } else if (command == SYNCDB_GETNEXT_CHANGED)
      {
        node = avlSearch(avl_tree, new_element, AVL_NEXT);
        while (node)
        {
          node_ctrl = (syncdbAvlNodeCtrl_t *)((char *)node + msg->size);
          if (CLIENT_MASK_IS_SET(client_index, node_ctrl->changed_mask))
          {
            break;
          }
          node = avlSearch(avl_tree, node, AVL_NEXT);
        }
      } 

      /* If the record does not exist 
      ** then return an error. 
      */
      if (!node)
      {
        reply->rc = SYNCDB_NOT_FOUND;
        break;
      }
      node_ctrl = (syncdbAvlNodeCtrl_t *)((char *)node + msg->size);

      /* Read the data.
      */
      if (command == SYNCDB_FIELD_GET)
      {
        /* If requesting only part of the data, then set un-requested data to 0.
        */
        if (msg->field_offset)
        {
          memset (&reply_cmd[sizeof (syncdbCmdMsg_t)], 0, msg->field_offset);
        }
        memcpy (&reply_cmd[sizeof (syncdbCmdMsg_t) + msg->field_offset] , 
                ((unsigned char *) node) + msg->field_offset, 
                msg->field_size);
        if ((msg->field_offset + msg->field_size) < msg->size)
        {
          memset (&reply_cmd[sizeof (syncdbCmdMsg_t) + msg->field_offset + msg->field_size], 
                  0, 
                  msg->size - (msg->field_offset + msg->field_size));
        }
      } else
      {
        memcpy (&reply_cmd[sizeof (syncdbCmdMsg_t)], node, msg->size);
      }
      reply->msg.genericGetMsg.delete_pending = node_ctrl->delete_pending;

      /* Update the "changed_mask" and delete the record if appropriate.
      ** If the client set the "flags_unchanged" flag then do not clear the 
      ** changed_mask or delete the record. 
      */
      if (!msg->flags_unchanged)
      {
        CLIENT_MASK_CLEAR (client_index, node_ctrl->changed_mask);
        if (node_ctrl->delete_pending)
        {
          if (((0 == node_ctrl->nsf_node_changed_flag) || !nsf_state.sync_sender) &&
              (0 == syncdb_client_mask_check (node_ctrl->changed_mask,
                                             data_table->notify_clients_mask)))
          {
            /* Delete the node from the AVL tree.
            */
            node = avlDeleteEntry (avl_tree, node);
            if (!node)
            {
              abort();  /* Not expecting this operation to fail */
            }
            data_table->num_records--;
          }
        }
      }

    } else if (data_table->table_type == TABLE_TYPE_RECORD)
    {
      memcpy (&reply_cmd[sizeof (syncdbCmdMsg_t)], data_table->record_ptr, msg->size);
      reply->msg.genericGetMsg.delete_pending = 0;
    }
    reply->rc = SYNCDB_OK;
  } while (0);

  switch (command)
  {
     case SYNCDB_GET:
       syncdbGetMsgLog (client->client_pid, client->client_id, msg->table_name, reply->rc);
       break;
     case SYNCDB_FIELD_GET:
       syncdbFieldGetMsgLog (client->client_pid, client->client_id, msg->table_name, 
                             msg->field_offset, msg->field_size, reply->rc);
       break;
     case SYNCDB_GETNEXT:
       syncdbGetNextMsgLog (client->client_pid, client->client_id, msg->table_name, reply->rc);
       break;
     case SYNCDB_GETNEXT_CHANGED:
       syncdbGetNextChangedMsgLog (client->client_pid, client->client_id, msg->table_name, reply->rc);
       break;
     default:
       break;
  }
#ifdef L3_PERF
  gettimeofday(&cur_time2, NULL);
  avl_int_lookup_usecs += (cur_time2.tv_usec >= cur_time1.tv_usec) ?
      (cur_time2.tv_usec - cur_time1.tv_usec) :
      (1000000 - cur_time1.tv_usec) + cur_time2.tv_usec;
#endif
  syncdbMsgSend (client, reply, sizeof (syncdbCmdMsg_t) + msg->size);
}


/*********************************************************************
* @purpose  Process the "NsfSyncEnable" command.
*           
* @param    client - Client Record.
* @param    sync_mode - 1-Enable   0-Disable
* @param    max_msg_size - Maximum size of the sync message.
*
* @returns  None
*
* @notes  This command is valid for AVL Trees. 
*       
* @end
*********************************************************************/
void syncdbCommandNsfSyncEnableHandle (clientTableNode_t *client,
                                unsigned int sync_mode, 
                                unsigned int max_msg_size)
{
  syncdbCmdMsg_t reply = {};
  
  reply.message_type = SYNCDB_REPLY;
  reply.rc = SYNCDB_OK;
  syncdb_stats.num_nsf_sync_mode_commands++;


  if (sync_mode)
  {
    nsf_sync_enable_pending = 1;
    nsf_sync_disable_pending = 0;
    if (max_msg_size > SYNCDB_AGENT_MAX_MSG_SIZE)
    {
      max_sync_msg_size = SYNCDB_AGENT_MAX_MSG_SIZE;
    } else
    {
      max_sync_msg_size = max_msg_size;
    }
  } else
  {
    nsf_sync_enable_pending = 0;
    nsf_sync_disable_pending = 1;
  }

  syncdbMsgSend (client, &reply, sizeof (reply));
}

/*********************************************************************
* @purpose  Process commands received on the syncdb command socket.
*           
* @param    None
*
* @returns  None
*
* @notes  
*       
* @end
*********************************************************************/
void syncdbCommandProcess (unsigned char *cmd, int cmd_len)
{
  syncdbCmdMsg_t *msg = (syncdbCmdMsg_t *) cmd;
  clientTableNode_t *client = 0;
  int client_index;

  if (cmd_len < sizeof (syncdbMsg_t))
  {
    return;
  }

  /* All messages except DEBUG and CLIENT_REGISTER must have a valid client ID
  ** Verify that the client ID is valid.
  */
  if ((msg->message_type != SYNCDB_DEBUG) &&
      (msg->message_type != SYNCDB_CLIENT_REGISTER))
  {
    client_index = msg->client_id % CLIENT_MAX_LIMIT;
    client = client_table [client_index];
    if (!client)
    {
      return;
    }
    client->num_commands++;
  }

  switch (msg->message_type)
  {
    case SYNCDB_DEBUG:
         syncdbCommandDebugHandle ();
      break;
    case SYNCDB_CLIENT_REGISTER:
         syncdbCommandClientRegisterHandle (&msg->msg.registerMsg);
      break;
    case SYNCDB_CLIENT_STATUS:
         syncdbCommandClientStatusGetHandle (client,
         (syncdbClientStatus_t *) ((unsigned char *)msg + sizeof (syncdbCmdMsg_t)));
      break;
    case SYNCDB_TABLE_CHANGE_NOTIFY:
         syncdbCommandTableChangeNotifyHandle (client, &msg->msg.tableChangeNotifyMsg);
      break;
    case SYNCDB_TABLE_STATUS:
         syncdbCommandTableStatusGetHandle (client, msg->msg.tableStatusGetMsg.num_tables,
                     (syncdbDataTableStatus_t *) ((unsigned char *)msg + sizeof (syncdbCmdMsg_t))
                     );
      break;
    case SYNCDB_AVL_TABLE_CREATE:
         syncdbCommandAvlTableCreateHandle (client, &msg->msg.tableCreateMsg,
                                            (char *)msg + sizeof (syncdbCmdMsg_t));
      break;
    case SYNCDB_RECORD_TABLE_CREATE:
         syncdbCommandRecordTableCreateHandle (client, &msg->msg.tableCreateMsg,
                                            (char *)msg + sizeof (syncdbCmdMsg_t));
      break;
    case SYNCDB_TABLE_DELETE:
         syncdbCommandTableDeleteHandle (client, &msg->msg.tableDeleteMsg);
         break;
    case SYNCDB_TABLE_STORE:
         syncdbCommandTableStoreHandle (client, &msg->msg.tableStoreMsg);
         break;
    case SYNCDB_INSERT:
         syncdbCommandInsertHandle (client, 
                                    &msg->msg.genericSetMsg,
                                    (unsigned char *)msg + sizeof (syncdbCmdMsg_t)
                                    );
      break;
    case SYNCDB_DELETE:
         syncdbCommandDeleteHandle (client, 
                                    &msg->msg.genericSetMsg,
                                    (unsigned char *)msg + sizeof (syncdbCmdMsg_t)
                                    );
      break;
    case SYNCDB_SET:
    case SYNCDB_FIELD_SET:
         syncdbCommandSetHandle (msg->message_type, 
                                    client,
                                    &msg->msg.genericSetMsg,
                                    (unsigned char *)msg + sizeof (syncdbCmdMsg_t)
                                    );
      break;
    case SYNCDB_GET:
    case SYNCDB_FIELD_GET:
    case SYNCDB_GETNEXT:
    case SYNCDB_GETNEXT_CHANGED:
        syncdbCommandGenericGetHandle (msg->message_type,
                                client, 
                                &msg->msg.genericGetMsg,
                                (unsigned char *)msg + sizeof (syncdbCmdMsg_t)
                                );

      break;
     case SYNCDB_NSF_SYNC_ENABLE:
       syncdbCommandNsfSyncEnableHandle(client, 
                                        msg->msg.nsfSyncMsg.sync_mode,
                                        msg->msg.nsfSyncMsg.max_sync_msg_size);
       break;
    default:
      /* No Action */
      printf("%s %d - Unknown Command: %d\n",
             __FUNCTION__, __LINE__, msg->message_type);
      break;
  }
}

/*********************************************************************
*********************************************************************
** Non Stop Forwarding Synchronization Protocol.
*********************************************************************
*********************************************************************/

/*********************************************************************
* @purpose  Send a message to the SyncDB peer process via the
*           SyncDB Agent.
*           
* @param    buf - Pointer to the message buffer.
*
* @returns  None
*
* @notes  
*       
* @end
*********************************************************************/
void syncdbNsfMsgSend (unsigned char *buf)
{
  int rv;
  unsigned short msg_size;
  nsf_sync_msg_t *msg = (void *) buf;

  msg_size = htons (msg->msg_size);

  rv = sendto (to_agent_fd, buf, msg_size,
               0,
               (struct sockaddr *) &agent_proc,
               sizeof (struct sockaddr_un));
  if (0 != rv)
  {
  }
  syncdb_stats.num_tx_agent_msgs++;
}

/*********************************************************************
* @purpose  Create a message header.
*           
* @param    buf - Message buffer.
* @param    seq - Sequence number of this message.
* @param    msg_type - Message type.
* @param    first_trans - Flag indicating the first transaction
* @param    ack_request - Flag indicating the ACK request.
*
* @returns  Size of the message.
*
* @notes  
*       
* @end
*********************************************************************/
unsigned short syncdbNsfMsgInit (unsigned char *buf,
                              unsigned long long seq,
                              unsigned short msg_type,
                              unsigned char first_trans,
                              unsigned char ack_request)
{
  nsf_sync_msg_t *msg = (void *) buf;

  memset (msg, 0, sizeof (nsf_sync_msg_t));
  msg->first_trans = first_trans;
  msg->ack_request = ack_request;
  msg->msg_size = htons (sizeof(nsf_sync_msg_t));
  msg->msg_type = htons (msg_type);
  msg->seq = syncdb_htonll (seq);

  return sizeof(nsf_sync_msg_t);
}

/*********************************************************************
* @purpose  Handle State Transitions between Manager and
*           non-Manager.
*           
* @param    None
*
* @returns  None
*
* @notes  
*       
* @end
*********************************************************************/
void syncdbNsfStateChangeHandle (void)
{
  unsigned short msg_size;

  if (nsf_sync_disable_pending)
  {
    nsf_sync_disable_pending = 0;
    nsf_state.sync_sender = 0;

    nsf_state.tx_seq = 0;
    nsf_state.last_tx_time = 0;
    nsf_state.last_ack_time = 0;
    nsf_state.ack_pending = 0;

    nsf_state.tx_buffer_head = 0;
    nsf_state.tx_buffer_tail = 0;

    nsf_state.rx_seq = 0;
    return;
  }

  if (nsf_sync_enable_pending)
  {
    /* Mark the whole database to be resynchronized.
    */
    syncdbNsfManagerDatabaseResync ();

    nsf_sync_enable_pending = 0;
    nsf_state.sync_sender = 1;

    nsf_state.tx_seq = 1;
    nsf_state.last_tx_time = syncdb_uptime();
    nsf_state.last_ack_time = syncdb_uptime();

    nsf_state.tx_buffer_head = 0;
    nsf_state.tx_buffer_tail = 0;

    nsf_state.rx_seq = 0;

    msg_size = syncdbNsfMsgInit (nsf_state.tx_buffers[nsf_state.tx_buffer_head],
                                 nsf_state.tx_seq,
                                 SYNC_MSG_DATA,
                                 1,
                                 1);
    if (msg_size)
    {
    }
    syncdbNsfMsgSend (nsf_state.tx_buffers[nsf_state.tx_buffer_head]);
    nsf_state.tx_buffer_head = 1;
    nsf_state.ack_pending = 1;
    nsf_state.expected_ack_sequence = nsf_state.tx_seq;
  }
}
/*********************************************************************
* @purpose  Send an ACK request if unacked packets are pending.
*           
* @param    None
*
* @returns  None
*
* @notes  
*       
* @end
*********************************************************************/
void syncdbNsfAckRequest (void)
{
  unsigned int current_time;
  nsf_sync_msg_t ack_req_msg;

  /* If this node is not the manager then nothing to do.
  */
  if (!nsf_state.sync_sender)
  {
    return;
  }

  /* If there are no un-ACKed messages then there is nothing to do.
  */
  if (nsf_state.tx_buffer_head == nsf_state.tx_buffer_tail)
  {
    return;
  }

  current_time = syncdb_uptime();
  if ((current_time - nsf_state.last_tx_time) < SYNCDB_EMPTY_ACK_TIMEOUT)
  {
    return;
  }

  (void) syncdbNsfMsgInit ((unsigned char *) &ack_req_msg,
                               nsf_state.tx_seq,
                               SYNC_MSG_DATA,
                               0,
                               1);
  syncdbNsfMsgSend ((unsigned char *) &ack_req_msg);
  nsf_state.ack_pending = 1;
  nsf_state.expected_ack_sequence = nsf_state.tx_seq;
  nsf_state.last_ack_time = syncdb_uptime();
}

/*********************************************************************
* @purpose  Retransmit handler.
*           
* @param    None
*
* @returns  None
*
* @notes  
*       
* @end
*********************************************************************/
void syncdbNsfRetransmit (void)
{
  unsigned int current_time;
  int i;

  /* If this node is not the manager then nothing to do.
  */
  if (!nsf_state.sync_sender)
  {
    return;
  }

  /* If there are no ACKs pending then nothing to do.
  */
  if (!nsf_state.ack_pending)
  {
    return;
  }

  current_time = syncdb_uptime();

  if ((current_time - nsf_state.last_ack_time) < SYNCDB_ACK_TIMEOUT)
  {
    return;
  }

  i = nsf_state.tx_buffer_tail;
  while (i != nsf_state.tx_buffer_head)
  {
    syncdbNsfMsgSend (nsf_state.tx_buffers[i]);
    i++;
    if (i == SYNCDB_MAX_NSF_PENDING_BUFFERS)
    {
      i = 0;
    }
  }
  nsf_state.last_ack_time = current_time;
}

/*********************************************************************
* @purpose  Message handler on the Non-Manager
*           This device receives sync messages and updates the
*           database.
*           
* @param    buf - Message from the Manager.
*
* @returns  None
*
* @notes  
*       
* @end
*********************************************************************/
void syncdbNsfNonManagerRx (unsigned char *buf)
{
  nsf_sync_msg_t *msg = (void *) buf;
  nsf_sync_msg_t reply;
  unsigned long long rx_seq;
  unsigned short msg_size;
  unsigned int data_present = 0;

  rx_seq = syncdb_htonll (msg->seq);
  msg_size = ntohs (msg->msg_size);

  if (msg_size > sizeof(nsf_sync_msg_t))
  {
    /* There is data in this packet, so the sequence number
    ** on the received packet should have been incremented by one. 
    ** The ACK requests without data are sent with the same 
    ** sequence number as the previous data packet. 
    */ 
    data_present = 1;
  }

  /* If this is the first message then reset internal state and send an ACK.
  */
  if (msg->first_trans)
  {
    nsf_state.rx_seq = rx_seq;
    (void) syncdbNsfMsgInit ((unsigned char *) &reply,
                                 nsf_state.rx_seq,
                                 SYNC_MSG_ACK,
                                 0,
                                 0);
    syncdbNsfMsgSend ((unsigned char *) &reply);
    return;
  }

  /* If this message is out of sequence then send a MISS packet to the sender.
  */
  if (rx_seq > (nsf_state.rx_seq + data_present)) 
  {
    (void) syncdbNsfMsgInit ((unsigned char *) &reply,
                                 nsf_state.rx_seq,
                                 SYNC_MSG_MISS,
                                 0,
                                 0);
    syncdbNsfMsgSend ((unsigned char *) &reply);
    return;
  }

  /* If this message has the expected sequence number then process the
  ** information elements in the message. 
  ** Note that messages with smaller sequence than we already received are 
  ** not parsed. 
  */
  if ((rx_seq == (nsf_state.rx_seq + 1)) && data_present)
  {
    nsf_state.rx_seq++;

    /* Parse the message content....
    */
    syncdbNsfMsgParse (msg);
  }

  /* If the sender requested an ACK then send it.
  */
  if (msg->ack_request)
  {
    (void) syncdbNsfMsgInit ((unsigned char *) &reply,
                                 nsf_state.rx_seq,
                                 SYNC_MSG_ACK,
                                 0,
                                 0);
    syncdbNsfMsgSend ((unsigned char *) &reply);
    return;
  }
}

/*********************************************************************
* @purpose  Receive message from manager and update the database
*           on the backup manager.
*           
* @param    ie - Pointer to the information element header.
*
* @returns  None
*
* @notes  
*       
* @end
*********************************************************************/
void syncdbNsfMsgParse (nsf_sync_msg_t *msg)
{
  static unsigned char current_record [SYNCDB_RECORD_MAX_SIZE];

  nsf_ie_t *ie;
  unsigned short msg_size;
  unsigned char *buf = (unsigned char *) msg;
  unsigned int ie_offset;
  unsigned int payload_offset;
  dataTableNode_t *data_table;
  avlTree_t *avl_tree;
  void *node;

  msg_size = ntohs (msg->msg_size);
  if (msg_size <= sizeof(nsf_sync_msg_t))
  {
    /* No information elements.
    */
    return;
  }

  ie_offset = sizeof(nsf_sync_msg_t);
  msg_size -= sizeof (nsf_sync_msg_t);

  do
  {
    if (msg_size < sizeof (nsf_ie_t))
    {
      /* No room for IE. This should not happen.
      */
      break;
    }
    ie = (nsf_ie_t *) &buf[ie_offset];
    payload_offset = ie_offset + sizeof (nsf_ie_t);
    ie_offset += ntohs (ie->size);
    msg_size -= htons (ie->size);

    data_table = syncdb_table_find ((char *)ie->table_name);
    if (!data_table)
    {
      /* Data table specified by the manager is not found on the
      ** backup manager.  (Should not happen)
      */ 
      continue;
    }
    if (!(data_table->create_flags & SYNCDB_TABLE_FLAG_NSF))
    {
      /* Data table specified by the manager is not an NSF table
      ** on the backup manager.  (Should not happen)
      */ 
      continue;
    }
    if (ie->cmd == SYNC_IE_NOOP)
    {
      /* Nothing to do. */
    } else if (ie->cmd == SYNC_IE_DELETE)
    {
      /* Delete the specified element from the tree.
      */
      avl_tree = data_table->record_ptr;
      node = avlSearch(avl_tree, &buf[payload_offset], AVL_EXACT);
      if (node)
      {
        (void) avlDeleteEntry(avl_tree, node);
        data_table->num_records--;
        data_table->num_non_deleted_records--;
      }
    } else if (ie->cmd == SYNC_IE_PURGE)
    {
      /* Delete all elements from the tree.
      */
      avl_tree = data_table->record_ptr;
      avlPurgeAvlTree (avl_tree, data_table->max_records);
      data_table->num_records = 0;
      data_table->num_non_deleted_records = 0;
    } else if (ie->cmd == SYNC_IE_SET)
    {
      memcpy (&current_record [ntohl(ie->record_offset)],
              &buf[payload_offset], 
              ntohs (ie->segment_size));
      if (data_table->table_type == TABLE_TYPE_RECORD)
      {
        if (ie->last_seg)
        {
          memcpy (data_table->record_ptr, current_record, data_table->record_size);
        }
      } else if (data_table->table_type == TABLE_TYPE_AVL)
      {
        if (ie->last_seg)
        {
          /* Insert the element into the AVL Tree
          */ 
          avl_tree = data_table->record_ptr;
          node = avlSearch(avl_tree, current_record, AVL_EXACT);
          if (node)
          {
            memcpy (node, current_record, data_table->record_size);
          } else
          {
            syncdbAvlNodeCtrl_t *node_ctrl;

            node_ctrl = (syncdbAvlNodeCtrl_t *) &current_record[data_table->record_size];
            memset (node_ctrl, 0, sizeof(syncdbAvlNodeCtrl_t));
            node = avlInsertEntry(avl_tree, current_record);
            if (node)
            {
              abort ();
            }
            data_table->num_non_deleted_records++;
            data_table->num_records++;
          }

        }
      }
    } else
    {
      /* Unexpected message type */
    }
  } while (msg_size > 0); 
  


}

/*********************************************************************
* @purpose  Mark the whole database to be resynchronized.
*           
* @param    None
*
* @returns  None
*
* @notes  
*       
* @end
*********************************************************************/
void syncdbNsfManagerDatabaseResync (void)
{
  dataTableNode_t *data_table;

  data_table = sorted_data_table_list;
  while (data_table)
  {
    /* The flags are ignored for non-NSF tables.
    */
    data_table->nsf_table_changed_flag = 1;
    if (data_table->table_type == TABLE_TYPE_AVL)
    {
      data_table->nsf_table_purge_flag = 1; /* Applicable only to AVL trees */
    }
    data_table = data_table->next_sorted;
  }
}

/*********************************************************************
* @purpose  Populate the next message to send to the backup manager.
*           
* @param    buf_size - Number of bytes available in the buffer.
* @param    ie - Pointer to the information element header.
*
* @returns  Size of the information element created by this function
*           0 - No more data is available.
*
* @notes  
*       
* @end
*********************************************************************/
unsigned short syncdbNsfManagerNextMsgGet (unsigned short buf_size,
                                           nsf_ie_t *ie)
{
  /* Previous NextMsgGet() invocation finished the last table.
  ** Start sync from the first table 
  */ 
  static unsigned int first_table = 1; 

  /* Previous NextMsgGet() invocation finished the table.
  ** Search for the next table.
  */
  static unsigned int next_table = 0;  

  /* Previous NextMsgGet() invocation finished AVL node.
  ** Search for the next AVL node.
  */
  static unsigned int next_avl_node = 1;  

  static unsigned char current_table [SYNCDB_TABLE_NAME_SIZE] = {};
  static unsigned char current_record [SYNCDB_RECORD_MAX_SIZE];
  static unsigned int  current_record_offset = 0;
   
  dataTableNode_t *data_table;
  unsigned int max_payload_bytes;
  unsigned int bytes_remaining;
  unsigned short payload_size;
  unsigned short msg_size;
  unsigned char *payload;

  data_table = sorted_data_table_list;
  if (!first_table)
  {
    while (data_table)
    {
      if ((data_table->create_flags & SYNCDB_TABLE_FLAG_NSF) &&
           data_table->nsf_table_changed_flag &&
          (0 == memcmp (current_table, data_table->table_name, SYNCDB_TABLE_NAME_SIZE)))
      {
        if (next_table)
        {
          data_table->nsf_table_changed_flag = 0;
          data_table = data_table->next_sorted;
        }
        break;
      }
      data_table = data_table->next_sorted;
    }
    if (!data_table)
    {
      first_table = 1; /* Start the next search from the first table */
      next_table = 0; 
      return 0;  /* No NSF tables */
    }
  }
  if (first_table || next_table)
  {
    while (data_table)
    {
      if ((data_table->create_flags & SYNCDB_TABLE_FLAG_NSF) &&
           data_table->nsf_table_changed_flag)
      {
        break;
      }
      data_table = data_table->next_sorted;
    }
    if (!data_table)
    {
      first_table = 1; /* Start the next search from the first table */
      next_table = 0; 
      return 0;  /* No NSF tables */
    }

    memcpy (current_table, data_table->table_name, SYNCDB_TABLE_NAME_SIZE);
    first_table = 0;
    next_table = 0;
    current_record_offset = 0;
    if (data_table->table_type == TABLE_TYPE_RECORD)
    {
      memcpy (current_record, data_table->record_ptr, data_table->record_size);
    } else if (data_table->table_type == TABLE_TYPE_AVL)
    {
      /* If the purge flag is set then create a purge message.
      */
      if (data_table->nsf_table_purge_flag)
      {
        data_table->nsf_table_purge_flag = 0;
        msg_size = sizeof (nsf_ie_t);
        memset (ie, 0, sizeof (nsf_ie_t));
        ie->cmd = SYNC_IE_PURGE;
        ie->size = htons(msg_size);
        memcpy (ie->table_name, current_table, SYNCDB_TABLE_NAME_SIZE);
        next_avl_node = 1;

        return msg_size;
      }
    }
  } 
  if (next_avl_node && (data_table->table_type == TABLE_TYPE_AVL))
  {
    void *node;
    syncdbAvlNodeCtrl_t *node_ctrl;
    avlTree_t *avl_tree;
    char new_node [SYNCDB_RECORD_MAX_SIZE];

    /* Find the first AVL node in the table that contains
    ** the nsf_node_changed_flag. 
    */
    avl_tree = data_table->record_ptr;
    node_ctrl = (syncdbAvlNodeCtrl_t *)0;
    memset (new_node, 0, data_table->record_size);
    node = avlSearch(avl_tree, new_node, AVL_EXACT);
    if (!node)
    {
      node = avlSearch(avl_tree, new_node, AVL_NEXT);
    }
    if (node)
    {
      node_ctrl = (syncdbAvlNodeCtrl_t *) ((char *)node + data_table->record_size);
    }
    while (node && !node_ctrl->nsf_node_changed_flag)
    {
      node = avlSearch(avl_tree, node, AVL_NEXT);
      if (node)
      {
        node_ctrl = (syncdbAvlNodeCtrl_t *) ((char *)node + data_table->record_size);
      }
    }
    if (node)
    {
      /* Check if we need to send delete-pending message.
      */
      if (node_ctrl->delete_pending)
      {

        msg_size = data_table->key_size + sizeof (nsf_ie_t);

        payload = ((unsigned char *) ie) + sizeof (nsf_ie_t);
        max_payload_bytes = buf_size - sizeof (nsf_ie_t);
        if (data_table->key_size > max_payload_bytes)
        {
          /* Can't fit this message. Force sender to transmit
          ** the packet. Several NOOP messages might have to be 
          ** inserted. 
          */ 
          msg_size = sizeof (nsf_ie_t);
          memset (ie, 0, sizeof (nsf_ie_t));
          ie->cmd = SYNC_IE_NOOP;
          ie->size = htons(msg_size);
          memcpy (ie->table_name, current_table, SYNCDB_TABLE_NAME_SIZE);

          return msg_size;
        }
        memcpy (payload, (unsigned char *) node, data_table->key_size);

        memset (ie, 0, sizeof (nsf_ie_t));
        ie->cmd = SYNC_IE_DELETE;
        ie->size = htons(msg_size);
        ie->segment_size = htons(data_table->key_size);
        memcpy (ie->table_name, current_table, SYNCDB_TABLE_NAME_SIZE);

        /* Delete the delete-pending entry from the AVL tree if there
        ** are no clients waiting for notifications.
        */
        node_ctrl->nsf_node_changed_flag = 0;
        if (0 == syncdb_client_mask_check (node_ctrl->changed_mask,
                                               data_table->notify_clients_mask))
        {
           node = avlDeleteEntry (avl_tree,node);
           if (!node)
           {
             abort(); 
           }
           data_table->num_records--;
        }

        return msg_size;
      } else
      {
        memcpy (current_record, (unsigned char *) node, data_table->record_size);
        node_ctrl->nsf_node_changed_flag = 0;
      }
    } else
    {
      /* We searched the whole AVL tree and there are no records that need to
      ** be synched. Send the NOOP command to the backup manager. 
      ** The NOOP indicates that more IEs might be added to the message, while 
      ** significantly simplifying the logic in this function. 
      */ 
      msg_size = sizeof (nsf_ie_t);
      memset (ie, 0, sizeof (nsf_ie_t));
      ie->cmd = SYNC_IE_NOOP;
      ie->size = htons(msg_size);
      memcpy (ie->table_name, current_table, SYNCDB_TABLE_NAME_SIZE);
      next_table = 1;  /* Search the next table */
      next_avl_node = 1;

      return msg_size;
    }
  }

  /* At this point the data we need to sync is stored in the current_record buffer.
  ** Create the synchronization message.
  */

  max_payload_bytes = buf_size - sizeof (nsf_ie_t);
  bytes_remaining = data_table->record_size - current_record_offset;
  if (bytes_remaining > max_payload_bytes)
  {
    payload_size = max_payload_bytes;
  } else
  {
    payload_size = bytes_remaining;
  }

  msg_size = payload_size + sizeof (nsf_ie_t);
  payload = ((unsigned char *) ie) + sizeof (nsf_ie_t);
  memcpy (payload, &current_record[current_record_offset], payload_size);

  memset (ie, 0, sizeof (nsf_ie_t));
  ie->cmd = SYNC_IE_SET;
  ie->size = htons(msg_size);
  ie->segment_size = htons(payload_size);
  ie->record_offset = htonl (current_record_offset);
  if (current_record_offset == 0)
  {
    ie->first_seg = 1;
  } else
  {
    ie->first_seg = 0;
  }
  if ((current_record_offset + payload_size) >= data_table->record_size)
  {
    ie->last_seg = 1;
  } else
  {
    ie->last_seg = 0;
  }
  memcpy (ie->table_name, current_table, SYNCDB_TABLE_NAME_SIZE);

  current_record_offset += payload_size;

  if (data_table->table_type == TABLE_TYPE_AVL)
  {
    next_avl_node = 0;
  }
  if (current_record_offset >= data_table->record_size)
  {
    current_record_offset = 0;
    /* We reached the end of the record. For AVL trees move to the next
    ** AVL node. For tables move to the next table. 
    */ 
    if (data_table->table_type == TABLE_TYPE_RECORD)
    {
      next_table = 1;
    } else if (data_table->table_type == TABLE_TYPE_AVL)
    {
      next_avl_node = 1;
    }
  }
  return msg_size;
}


/*********************************************************************
* @purpose  Send sync data from manager to the backup manager.
*           
* @param    None
*
* @returns  None
*
* @notes  
*       
* @end
*********************************************************************/
void syncdbNsfManagerDataSend (void)
{
  nsf_sync_msg_t *msg;
  nsf_ie_t *ie;
  unsigned short msg_size;
  unsigned int msg_index;
  unsigned int next_msg_index;
  unsigned char ack_request;
  unsigned short bytes_left;
  unsigned short byte_index;
  unsigned int transmit_message;

  if (!nsf_state.sync_sender)
  {
    /* If this unit is not manager then nothing to do.
    */
    return;
  }

  do
  {
    bytes_left = max_sync_msg_size;
    byte_index = 0;
    msg_index = nsf_state.tx_buffer_head;
    next_msg_index = msg_index + 1;
    if (next_msg_index == SYNCDB_MAX_NSF_PENDING_BUFFERS)
    {
      next_msg_index = 0;
    }

    /* We leave one slot in the buffer queue empty. This is needed
    ** in order to differentiate between buffer-full and buffer-empty 
    ** condition. 
    */ 
    next_msg_index++;
    if (next_msg_index == SYNCDB_MAX_NSF_PENDING_BUFFERS)
    {
      next_msg_index = 0;
    }

    /* If the pending buffers list is full then request ACK in the next packet.
    */
    ack_request = 0;
    nsf_state.tx_seq++;
    if (next_msg_index == nsf_state.tx_buffer_tail)
    {
      ack_request = 1;
      nsf_state.ack_pending = 1;
      nsf_state.expected_ack_sequence = nsf_state.tx_seq;
    } 
    msg = (nsf_sync_msg_t *) nsf_state.tx_buffers[msg_index];
    msg_size = syncdbNsfMsgInit ((unsigned char *) msg,
                                 nsf_state.tx_seq,
                                 SYNC_MSG_DATA,
                                 0,
                                 ack_request);
    transmit_message = 0;
    do {
      byte_index += msg_size;
      bytes_left -= msg_size;
      if (bytes_left <= sizeof (nsf_ie_t))
      {
        /* No room left for data in this message
        */
        break;
      }
      ie = (nsf_ie_t *) &nsf_state.tx_buffers[msg_index][byte_index];
      msg_size = syncdbNsfManagerNextMsgGet(bytes_left, ie);
      if (msg_size)
      {
        msg->msg_size = htons(htons(msg->msg_size) + msg_size);
        /* We have some data, so need to send this message.
        */
        transmit_message = 1;
      }
    } while (msg_size); 
    
    if (transmit_message)
    {
      syncdbNsfMsgSend (nsf_state.tx_buffers[msg_index]);
      nsf_state.tx_buffer_head++;
      if (nsf_state.tx_buffer_head == SYNCDB_MAX_NSF_PENDING_BUFFERS)
      {
        nsf_state.tx_buffer_head = 0;
      }
      if (ack_request)
      {
        nsf_state.last_ack_time = syncdb_uptime();
      }
      nsf_state.last_tx_time = syncdb_uptime();
    } else
    {
      /* We didn't send the message, so reduce the sequence number
      ** which we preemptively incremented. 
      */ 
      nsf_state.tx_seq--;
    }
  } while (!ack_request && msg_size); 
  
}

/*********************************************************************
* @purpose  Message handler on the Manager
*           
* @param    buf - Message from the Non-Manager.
*
* @returns  None
*
* @notes  
*       
* @end
*********************************************************************/
void syncdbNsfManagerRx (unsigned char *buf)
{
  nsf_sync_msg_t *msg = (void *) buf;
  int i;


  /* If this is an ACK then update internal state.
  */
  if (msg->msg_type == ntohs(SYNC_MSG_ACK))
  {
    /* If this ACK is not for the last message that we sent, then ignore it.
    */
    if (nsf_state.expected_ack_sequence != syncdb_htonll(msg->seq))
    {
      return;
    }
    /* Only one ACK can be outstanding.
    */
    nsf_state.ack_pending = 0;
    nsf_state.tx_buffer_tail = nsf_state.tx_buffer_head;

    /* Check whether there are any additional packets to send.
    */
    syncdbNsfManagerDataSend ();

    return;
  }

  /* If we get a miss packet then retransmit all un-acknowledged
  ** buffers. 
  */
  if (msg->msg_type == ntohs(SYNC_MSG_MISS))
  {
    i = nsf_state.tx_buffer_tail;
    while (i != nsf_state.tx_buffer_head)
    {
      syncdbNsfMsgSend (nsf_state.tx_buffers[i]);
      i++;
      if (i == SYNCDB_MAX_NSF_PENDING_BUFFERS)
      {
        i = 0;
      }
    }
    return;
  }

  /* Data packets are not expected and are ignored.
  */
  return;
}

/*********************************************************************
* @purpose  Handle NSF synchronization with the backup manager.
*           
* @param    msg - Message from partner SyncDB or 0 if timer event. 
*
* @returns  None
*
* @notes  
*       
* @end
*********************************************************************/
void syncdbNsfProtocolHandle (unsigned char *msg) 
{
  static unsigned long last_client_check_time = 0;
  unsigned long current_time, delta_time;

  syncdbNsfStateChangeHandle ();

  current_time = syncdb_uptime();
  delta_time = current_time - last_client_check_time;
  if (delta_time >= 1)
  {
    last_client_check_time = current_time;

    /* Check if any retransmits are required.
    */
    syncdbNsfRetransmit ();

    /* Check if we have any un-ACKed data in the queue and there are no
    ** other packets to send. 
    */ 
    syncdbNsfAckRequest ();

    /* Check whether there are any additional packets to send.
    */
    if (!nsf_state.ack_pending)
    {
      syncdbNsfManagerDataSend ();
    }
  }

  if (!msg)
  {
    return;
  }

  if (nsf_state.sync_sender)
  {
    syncdbNsfManagerRx (msg);
  } else
  {
    syncdbNsfNonManagerRx (msg);
  }
}


/*********************************************************************
* @purpose  Wait on the syncdb command socket.
*           
* @param    None
*
* @returns  None
*
* @notes  
*       
* @end
*********************************************************************/
void syncdbCommandWait (char *path)
{

  struct sockaddr_un server;
  int command_fd;
  int agent_fd;
  struct sockaddr_un client;
  socklen_t addr_len = sizeof (client);
  fd_set rcv_set;
  struct timeval tv;
  int rc;
  unsigned char cmd[SYNCDB_MSG_MAX_SIZE];
  unsigned char agent_msg [SYNCDB_AGENT_MAX_MSG_SIZE];
  unsigned long last_client_check_time;
  unsigned long current_time, delta_time;
  int i;
  int num_sockets = 0;
 
  last_client_check_time = syncdb_uptime ();
  if (access(path, F_OK)) 
  {
    /* Create syncdb directory if it does not already exist.
    */
    sprintf(_syncdb_command, "%s%s", "mkdir ", path);
    system (_syncdb_command);
    sprintf(_syncdb_command, "%s%s", "chmod oug+w ", path);
    system (_syncdb_command);
  }
#ifdef RAMDISK
  if (access(SYNCDB_TMP_ROOT "/ramdisk", F_OK)) 
  {
    /* Create the ramdisk directory if it does not already exist.
    */
    system ("mkdir " SYNCDB_TMP_ROOT "/ramdisk");
  }
#endif
  /* Clean out the nvram directory and load files from
  ** the NVRAM archive if the archive exists.
  */
  strncpy(_syncdb_nv_path_base, path, sizeof(_syncdb_nv_path_base)-1);
  sprintf(_syncdb_nv_path, "%s%s", path, "/nvram");
  sprintf(_syncdb_command, "%s%s", "rm -rf ", _syncdb_nv_path);
  system (_syncdb_command);
  sprintf(_syncdb_command, "%s%s", "mkdir ", _syncdb_nv_path);
  system (_syncdb_command);
  sprintf(_syncdb_db_file, "%s%s", path, SYNCDB_NVRAM_ARCHIVE);
  if (0 == access(_syncdb_db_file, F_OK)) 
  {
    sprintf(_syncdb_command, "%s%s%s", "tar -zxf ", _syncdb_db_file, " -C /");
    system (_syncdb_command);
  }

  /* Clean out sockets and debug directories.
  */
  sprintf(_syncdb_sock_path_base, "%s%s", path, "/sockets");
  sprintf(_syncdb_debug_path_base, "%s%s", path, "/debug");
  sprintf(_syncdb_log_level_path, "%s%s", path, "/log_level");
  sprintf(_syncdb_command, "%s%s", "rm -rf ", _syncdb_sock_path_base);
  system (_syncdb_command);
  sprintf(_syncdb_command, "%s%s", "rm -rf ", _syncdb_debug_path_base);
  system (_syncdb_command);
  sprintf(_syncdb_command, "%s%s", "mkdir ", _syncdb_sock_path_base);
  system (_syncdb_command);
  sprintf(_syncdb_command, "%s%s", "mkdir ", _syncdb_debug_path_base);
  system (_syncdb_command);
  sprintf(_syncdb_command, "%s%s", "chmod oug+w ", _syncdb_sock_path_base);
  system (_syncdb_command);
  sprintf(_syncdb_command, "%s%s", "chmod oug+w ", _syncdb_debug_path_base);
  system (_syncdb_command);

  if ((command_fd = socket (AF_UNIX, SOCK_DGRAM, 0)) < 0)
  {
      perror ("socket");
      return;
  }

  /* Set the logging level.
  ** Useful for tracing startup activity when performing a warm reload.
  */
  syncdbLogLevelGet ();

  memset (&server, 0, sizeof (server));
  server.sun_family = AF_UNIX;
  sprintf(_syncdb_sock_path, "%s%s", _syncdb_sock_path_base, SYNCDB_SERVER_SOCKET);
  strcpy(server.sun_path, _syncdb_sock_path);
 
  if(bind(command_fd, (const struct sockaddr *) &server, sizeof(server)) < 0)
  {
   perror("bind");
   close(command_fd);
   return;
  }
  sprintf(_syncdb_command, "%s%s", "chmod oug+w ", _syncdb_sock_path);
  system (_syncdb_command);
 
  /* Create the socket for receiving messages from the SyncDB agent.
  */
  if ((agent_fd = socket (AF_UNIX, SOCK_DGRAM, 0)) < 0)
  {
      perror ("agent socket");
      close(command_fd);
      return;
  }
  memset (&server, 0, sizeof (server));
  server.sun_family = AF_UNIX;
  sprintf(_syncdb_sock_path, "%s%s", _syncdb_sock_path_base, SYNCDB_FROM_AGENT_SOCKET);
  strcpy(server.sun_path, _syncdb_sock_path);
 
  if(bind(agent_fd, (const struct sockaddr *) &server, sizeof(server)) < 0)
  {
   perror("agent bind");
   close(command_fd);
   close(agent_fd);
   return;
  }

  /* Create a socket for sending to the SyncDB Agent.
  */
  to_agent_fd = socket (AF_UNIX, SOCK_DGRAM, 0);
  if (to_agent_fd < 0)
  {
    perror ("to agent socket");
    close(command_fd);
    close(agent_fd);
    return;
  }
  memset(&agent_proc, 0, sizeof(struct sockaddr_un));
  agent_proc.sun_family = AF_UNIX;
  sprintf(_syncdb_sock_path, "%s%s", _syncdb_sock_path_base, SYNCDB_TO_AGENT_SOCKET);
  strcpy(agent_proc.sun_path, _syncdb_sock_path);


  #ifdef L3_PERF
  (void) signal (SIGHUP, sighup_catch);
  #else
  (void) signal (SIGHUP, SIG_IGN);
  #endif

  do {
    FD_ZERO (&rcv_set);
    if (command_fd > 0)
    {
        num_sockets = command_fd;
        FD_SET (command_fd, &rcv_set);
    }
    if (agent_fd > 0)
    {
      if (agent_fd > num_sockets)
      {
        num_sockets = agent_fd;
      }
      FD_SET (agent_fd, &rcv_set);
    }

    tv.tv_sec = 1;
    tv.tv_usec =0;

    rc = select (num_sockets + 1,&rcv_set, 0, 0, &tv);
    if (rc == 0)
    {
      /* If socket times out then check whether any NSF work needs to be done.
      */
      syncdbNsfProtocolHandle (0);
    }
    current_time = syncdb_uptime();
    delta_time = current_time - last_client_check_time;
    if (delta_time >= 5)
    {
      /* Check whether processes that registered clients are still active.
      ** If the process is not active then remove the client.
      */
      last_client_check_time = current_time;
      for (i = 0; i < CLIENT_MAX_LIMIT; i++)
      {
        if (client_table[i] && 
            (0 > kill (client_table[i]->client_pid, 0)))
        {
          syncdb_client_cleanup (client_table[i]->client_id);
        }
      }
    }
    if (rc <= 0)
    {
        continue;
    }


    if (FD_ISSET(command_fd, &rcv_set))
    {
      /* Read one datagram from the command socket
      */
      rc = recvfrom (command_fd, cmd, sizeof (cmd), 0,
                     (struct sockaddr *) &client, &addr_len);
      if (rc < 0)
      {
          perror ("recvfrom-cmd");
          continue;
      }

      syncdb_stats.num_commands++;

      syncdbCommandProcess (cmd, rc);
    }

    if (FD_ISSET(agent_fd, &rcv_set))
    {
      /* Read one datagram from the command socket
      */
      rc = recvfrom (agent_fd, agent_msg, sizeof (agent_msg), 0,
                     (struct sockaddr *) &client, &addr_len);
      if (rc < 0)
      {
          perror ("recvfrom-agent");
          continue;
      }

      syncdbNsfProtocolHandle (agent_msg);
      syncdb_stats.num_rx_agent_msgs++;
    }

  } while (1);
}

STATIC void
_brcm_syncdb_sig_handler(int sig)
{
    exit(0);
}


/*********************************************************************
* @purpose  Entry Point for the Synchronization Database program.
*           
* @param    None
*
* @returns  None
*
* @notes  
*       
* @end
*********************************************************************/
void syncdb_main (char *path)
{
  int i, rv;
  struct rlimit rlim;
#if 0
  l7proc_memtrack_init ();
  l7proc_crashlog_register ();
  l7proc_backtrace_register();
  l7proc_devshell_init();
#endif
  /* Increase the maximum number of open files to 8K.
  */
  struct sigaction siga;

  sigemptyset(&siga.sa_mask);
  siga.sa_flags = 0;
  siga.sa_handler = _brcm_syncdb_sig_handler;
  sigaction(SIGTERM, &siga, NULL);
  signal(SIGINT, SIG_IGN);

  rv = prctl(PR_SET_PDEATHSIG, SIGTERM);
  if (rv == -1) { perror(0); exit(1); }
  if (getppid() != sai_pid) { exit(1); }

  memset (&rlim, 0, sizeof(rlim));
  rlim.rlim_cur = (8*1024);
  rlim.rlim_max = (8*1024);
  (void) setrlimit (RLIMIT_NOFILE, &rlim);

  /* Allocate memory for NSF synchronization buffers.
  ** Note that the memory is not zeroed out, so on non-stacking platforms 
  ** the memory is never accessed and does not actually consume any 
  ** physical memory. 
  */
  for (i = 0; i < SYNCDB_MAX_NSF_PENDING_BUFFERS; i++)
  {
    nsf_state.tx_buffers[i] = malloc (SYNCDB_AGENT_MAX_MSG_SIZE);
  }
  nsf_state.record_buf = malloc (SYNCDB_RECORD_MAX_SIZE);
#ifdef L3_PERF
  avl_int_lookup_usecs = avl_int_add_usecs = 0;
#endif

  syncdbCommandWait(path);
}
