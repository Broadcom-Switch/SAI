/*********************************************************************
 *
 * Copyright: (c) 2017 Broadcom.
 * Broadcom Proprietary and Confidential. All rights reserved.
 *
 *********************************************************************/

#ifndef SYNCDB_API_H_INCLUDED
#define SYNCDB_API_H_INCLUDED

#include <pthread.h>

/* Maximum size of the message buffer between the SyncDB process and the
** SyncDB Agent. The actual message size may be smaller, and is limited by 
** the transport message size. 
**/ 
#define SYNCDB_AGENT_MAX_MSG_SIZE (8000)

/* Maximum size of the text string that uniquely identifies 
** a database table. The size includes the 0 terminator. 
** This value must be divisible by 2. 
*/
#define SYNCDB_TABLE_NAME_SIZE  64

/* Maximum length for string fields used by syncdb. This includes
** client names, file names and various descriptions.
*/
#define SYNCDB_MAX_STR_LEN  256

/* Maximum size of a single data record. In case of AVL trees and Queues
** this is the maximum size of each AVL or Queue node.
*/
#define SYNCDB_RECORD_MAX_SIZE (1024*64)

/* Supported Data Table Types.
*/
#define SYNCDB_TABLE_TYPE_RECORD 0
#define SYNCDB_TABLE_TYPE_STORABLE_RECORD 1
#define SYNCDB_TABLE_TYPE_AVL_TREE 2
#define SYNCDB_TABLE_TYPE_QUEUE 3

/* Return Codes from the syncdb API functions.
*/
#define SYNCDB_OK   (0)
#define SYNCDB_ERROR (-1)        /* General Error */
#define SYNCDB_MAX_CLIENTS (-2)  /* Reached maximum number of clients */
#define SYNCDB_DUPNAME (-3) /* Table exists with duplicate name */
#define SYNCDB_NO_TABLE (-4) /* Specified table does not exist */
#define SYNCDB_FULL (-5) /* Specified table is full */
#define SYNCDB_SIZE (-6) /* Specified record size is invalid for this table. */
#define SYNCDB_NOT_FOUND (-7) /* Specified record is not in the table. */
#define SYNCDB_SCHEMA_ERROR (-8) /* Schema validation error */
#define SYNCDB_ENTRY_EXISTS (-9) /* AVL entry with the specified key already exists */

/********  Data Table Status.
** The status is a bit mask returned by the syncdbTableStatusGet() function. 
** The status is maintained per client.  
********/

/* Specified Table Exist
** If this bit is not set to 1 then all other bits are meaningless.
*/
#define SYNCDB_TABLE_STAT_EXISTS (1 << 0) 

/* Changes pending for this client. (Cleared on Read)
** This bit is set whenever something changes in the table. The bit is cleared 
** when the syncdbTableStatusGet() function is invoked by the client for the table. 
** When a client gets a table change notification, this bit can be used to determine 
** which table has changed. However the client does not need to be registered for 
** change notifications. The bit is set for all clients when the table changes. 
*/ 
#define SYNCDB_TABLE_STAT_CHANGED (1 << 1) 

/* AVL Tree Purged (Cleared on Read)
** This bit indicates that all delete-pending entries have been removed from the 
** AVL tree since the last call to the syncdbTableStatusGet() function. 
** This means that some record delete notifcations may have been missed by this client. 
** The client needs to completely resynchronize with the AVL tree if it needs to know 
** the exact content. 
** If the table is not an AVL tree then this bit is always set to 0. 
*/
#define SYNCDB_TABLE_STAT_AVL_TREE_PURGED (1 << 2) 

/* New Table (Cleared on Read)
** This bit indicates that the syncdbTableStatusGet() function is called for 
** the first time since the table has been created. 
** This flag can be used to detect that a table has been deleted and recreated. 
** Normally applications do not delete tables, even when they are restarted. If 
** a table needs to be deleted then all applications using this table should be 
** restarted.  
*/
#define SYNCDB_TABLE_STAT_NEW_TABLE (1 << 3) 

/********  Data Table Creation Flags
** The flags specified on the table create call.
** The flags is a bit mask returned by the syncdbTableStatusGet() function. 
********/
/* 
** This table can be stored in a file. 
** When this flag is set the table creation function must be given the schema and 
** the schema size.  
*/ 
#define SYNCDB_TABLE_FLAG_STORABLE (1 << 0)

/* This table is copied to NVRAM when the table-store command is received.
** The flag is allowed only if the SYNCDB_TABLE_FLASG_STORABLE is also set.
*/
#define SYNCDB_TABLE_FLAG_NVRAM (1 << 1)

/* If this flag is enabled then the syncdb tries to populate the table
** from a file when creating the table. 
** The flag is allowed only if the SYNCDB_TABLE_FLAG_STORABLE is also set. 
*/
#define SYNCDB_TABLE_FLAG_FILE_LOAD (1 << 2)


/* When this flag is set, the syncdbInsert() function returns an error if an
** entry with the specified key already exists. When this flag is not set the 
** old entry is overwriten with the new content. 
*/
#define SYNCDB_TABLE_FLAG_EXISTS (1 << 3)

/* When this flag is set, the content of the table is synchronized with the backup
** manager. On non-stacking platform the flag has no effect. 
** Applications that use SyncDB for the Non Stop Forwarding feature should store 
** the data in the datble using the big-endian format.  
*/ 
#define SYNCDB_TABLE_FLAG_NSF (1 << 4)

/* syncdb Client Handle.
*/
typedef struct 
{
    int cmd_socket;
    int notify_socket;
    int client_id;
    pthread_mutex_t client_lock;
} syncdbClientHandle_t;

/* Data Table Status
*/
typedef struct
{
    char table_name [SYNCDB_TABLE_NAME_SIZE];

    /* The version of the table. This information can be used by processes
    ** to decide whether the table is compatible with the process.
    */
    unsigned int table_version;

    /* Flags specified on table creation. SYCNDB_TABLE_FLAG_STORABLE and others...
    */
    unsigned int table_flags;

    /* Bit Mask. See description for SYNCDB_TABLE_STAT_EXISTS and others...
    */
    unsigned int table_status; 

    /* If the table exists then this specifies the table type.
    ** The types are SYNCDB_TABLE_TYPE_RECORD,... 
    */
    int table_type; 

    /* Number of records in the table. 
    ** Valid only for Queues and AVL Trees 
    */ 
    int num_elements; 

    /* Number of records that are not in delete-pending state.
    ** This parameter is valid only for AVL trees.
    */
    int num_non_deleted_elements;
} syncdbDataTableStatus_t;

/* Client Table Status.
*/
typedef struct 
{
    int client_id; /* syncdb client ID of the client for which to retrieve the status */
    char client_description [SYNCDB_MAX_STR_LEN];
    int  client_pid;  /* Linux Process ID for this client */
    int  num_commands; /* Total number of commands issued by this client */ 
    int  num_table_change_events; /* Total number of table-change notifications sent to the client */
    int  num_table_purges; /* Number of AVL tree purge events sent to the client */
} syncdbClientStatus_t;

/******************************** 
** Storable Record Schema 
********************************/ 

/* The AVL trees and records can be stored in a file system using JSON notation.
** In order to store information in the file system the table creator must tell syncdb how 
** the data is formatted. Also to facilitate data format changes the table creator must 
** tell syncdb the default values of fields that are not present in the file. 
*/

/* The schema for storable records is also defined using JSON format.
** This compiler constant defines the maximum length of the schema. 
** The constant may be changed to be smaller than the maximum record size, but 
** never bigger. 
*/
#define SYNCDB_JSON_MAX_SCHEMA_SIZE  (SYNCDB_RECORD_MAX_SIZE)

/* The syncdb supports the following JSON data types:
** SYNCDB_JSON_NUMBER - A numeric value. Syncdb supports only unsigned integers.
** SYNCDB_JSON_STRING - Zero-terminated array of characters.
** SYNCDB_JSON_ARRAY - Syncdb supports only arrays of bytes. The default value is 0.
**  
** SYNCDB Data Type Limitations: 
** The syncdb supports only a subset of JSON data types. 
** The most important limitation is the limited support for arays. Arrays are difficult 
** to handle during data format migration, so should be avoided when defining storable records.
** Syncdb does support arrays of bytes. The table creator cannot provide default values for
** the byte arrays. The default for array content is always zero.
**  
** The syncdb supports only unsigned integer values for SYNCDB_JSON_NUMBER type. 
** The supported integer sizes are 1, 2, 4, and 8 bytes. 
** Floating point numbers and exponential notation is not supported. 
** Signed integers are also not supported. If a signed integer is used in the node 
** definition then the data migration may not work properly. 
** 
** The syncdb also does not support "true", "false", and "null" values. These can be represented 
** with an integer. 
*/
typedef enum
{
  SYNCDB_JSON_NUMBER = 1,
  SYNCDB_JSON_STRING = 2,
  SYNCDB_JSON_ARRAY = 3
} syncdbJsonDataType_e;

typedef struct syncdbJsonNode_s
{
    syncdbJsonDataType_e data_type;

    /* The uniqe identifier for this variable. The data_name is used to
    ** match fields when performing version migration. All names within 
    ** the element must be unique. A good policy is to use the fully 
    ** qualified C field name, such as "element.key.k1" 
    ** There is no limit on the size of the name field, other than the 
    ** overall schema size limit. 
    */
    char *data_name;

    /* Offset of this element from the beginning of the data buffer.
    ** Note that if an element is a member of a structure within another 
    ** structure then the offset of the nested structure needs to be added 
    ** to the offset of the variable. 
    */
    unsigned int data_offset; 

    /* The size of this data element in bytes.
    */
    unsigned int  data_size; 

    union {
        unsigned long long default_number; /* Default value for SYNCDB_JSON_NUMBER object. */
        char *default_string; /* Default value for SYNCDB_JSON_STRING object */
    } val;

} syncdbJsonNode_t;

/* The following errors may be reported during schema validation.
*/
typedef enum 
{
    SYNCDB_SCHEMA_OK = 0,   /* No error */
    SYNCDB_SCHEMA_TOO_BIG = 1, /* Schema node offset plus size is larger than data element */
    SYNCDB_SCHEMA_OVERLAP = 2, /* Schema nodes overlap each other. */
    SYNCDB_SCHEMA_TOO_SHORT = 3, /* Data element has 8 or more bytes beyond last schema node */
    SYNCDB_SCHEMA_GAP = 4, /* Two schema nodes have an 8 or more byte gap between them */
    SYNCDB_SCHEMA_INT_OVERFLOW = 5, /* Default value for an integer does not fit into size */
    SYNCDB_SCHEMA_STRING_OVERFLOW = 6, /* Default value for a string does not fit into size */
    SYNCDB_SCHEMA_INT_SIZE = 7, /* Integer size is not 1, 2, 4, or 8 */
    SYNCDB_SCHEMA_TYPE = 8, /* Data type is not Number, String or Array. */
    SYNCDB_SCHEMA_NO_ZERO_OFFSET = 9, /* No node with offset set to 0. */
    SYNCDB_SCHEMA_ZERO_SIZE = 10, /* Found an element with size equal to 0 */
    SYNCDB_SCHEMA_DUP_NAME = 11 /* The element has the same name as another element in the schema */
} syncdbSchemaError_e;

/******************************** 
** syncdb API Functions 
*********************************/ 
int syncdbUtilSchemaCreate (syncdbJsonNode_t *element_node,
                            unsigned int node_schema_size, 
                            char *schema_buf,
                            unsigned int buf_size,
                            unsigned int *schema_size,
                            unsigned int data_element_size,
                            syncdbSchemaError_e *schema_error
                            );

int syncdbClientRegister (char *client_name,
                          syncdbClientHandle_t  *client_id, char *path);

int syncdbClientStatusGet (syncdbClientHandle_t  *client_id,
                          syncdbClientStatus_t *client_status);

int syncdbTableStore (syncdbClientHandle_t  *client_id,
                          char *table_name,
                          unsigned int nvram);

int syncdbTableChangeCheck (syncdbClientHandle_t  *client_id,
                          int  timeout_secs);

int syncdbTableChangeNotify (syncdbClientHandle_t  *client_id,
                          char  *table_name);

int syncdbTableStatusGet (syncdbClientHandle_t  *client_id,
                          int num_tables,
                          syncdbDataTableStatus_t *table_list);

int syncdbAvlTableCreate (syncdbClientHandle_t  *client_id,
                          char  *table_name,
                          unsigned int   table_version,
                          unsigned int   max_elements,
                          unsigned int   max_live_elements,
                          unsigned int   node_size,
                          unsigned int   key_size,
                          unsigned int   flags,
                          char * schema,
                          unsigned int   schema_size);

int syncdbRecordTableCreate (syncdbClientHandle_t  *client_id,
                          char  *table_name,
                          unsigned int   table_version,
                          unsigned int   node_size,
                          unsigned int   flags,
                          char * schema,
                          unsigned int   schema_size);
 
int syncdbTableDelete (syncdbClientHandle_t  *client_id,
                          char *table_name);

int syncdbInsert (syncdbClientHandle_t  *client_id,
                          char  *table_name,
                          void *element,
                          unsigned int   size);

int syncdbDelete (syncdbClientHandle_t  *client_id,
                          char  *table_name,
                          void *element,
                          unsigned int   size);

int syncdbSet (syncdbClientHandle_t  *client_id,
                          char  *table_name,
                          void *element,
                          unsigned int   size);

int syncdbFieldSet (syncdbClientHandle_t  *client_id,
                          char  *table_name,
                          void *element,
                          unsigned int   size,
                          unsigned int field_offset,
                          unsigned int field_size);

int syncdbGet (syncdbClientHandle_t  *client_id,
                          char  *table_name,
                          void *element,
                          unsigned int   size,
                          int *delete_pending);

int syncdbFieldGet (syncdbClientHandle_t  *client_id,
                          char  *table_name,
                          void *element,
                          unsigned int   size,
                          unsigned int field_offset,
                          unsigned int field_size,
                          int  flags_unchanged,
                          int *delete_pending);

int syncdbGetNext (syncdbClientHandle_t  *client_id,
                          char  *table_name,
                          void *element,
                          unsigned int   size,
                          int *delete_pending);
int syncdbGetNextChanged (syncdbClientHandle_t  *client_id,
                          char  *table_name,
                          void *element,
                          unsigned int   size,
                          int *delete_pending);
int syncdbNsfModeSet (syncdbClientHandle_t  *client_id,
                          unsigned int sync_mode,
                          unsigned int max_msg_size);

#endif /* SYNCDB_API_H_INCLUDED */

