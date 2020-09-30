/*********************************************************************
 *
 * Copyright: (c) 2017 Broadcom.
 * Broadcom Proprietary and Confidential. All rights reserved.
 *
 *********************************************************************/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>

#include <brcm_syncdb_msg.h>
#include <brcm_syncdb.h>
#include <brcm_syncdb_api.h>
#include <brcm_avl_api.h>

typedef struct 
{
  syncdbJsonDataType_e type;
  char *value;     /* Pointer to the object value (String or Array objects) */
  unsigned int size; /* Detected size of the string or array object */
  unsigned long long int_value; /* Returned value for object of type "Number" */
} jsonObjectDescr_t;

typedef struct jsonSchemaNode_s
{
    syncdbJsonNode_t schema_node;
    struct jsonSchemaNode_s *next;
} jsonSchemaNode_t;

extern char _syncdb_nv_path_base[];
extern char _syncdb_nv_path[];

/*********************************************************************
* @purpose  Read a character from an open file.
*           
* @param    fd - File descriptor
* @param    next_char - Character retrieved from the file.
*
* @returns  SYNCDB_OK - Character read.
* @returns  SYNCDB_ERROR - Reached end of file.
*
* @notes  
*       
* @end
*********************************************************************/
int syncdb_char_get (int fd,
                        unsigned char *next_char)
{
  ssize_t size;

  do {
      size = read (fd, next_char, 1);
      if (size == 1) 
      {
          return SYNCDB_OK;
      }
      if (size == 0) 
      {
          return SYNCDB_ERROR;
      }
      if (size < 0) 
      {
          if (errno != EINTR) 
          {
              perror ("read");
              return SYNCDB_ERROR;
          }
      }
  } while (1);
}

/*********************************************************************
* @purpose  Write Buffer to File
*           
* @param    fd - File descriptor
* @param    buf - Buffer to write to file.
* @param    buf_size - Number of bytes to write to file.
*
* @returns  None
*
* @notes  The function writes to the file until the specified
*         number of bytes is written. The function handles
*         interruprs received during write.
*       
* @end
*********************************************************************/
static void syncdb_file_write (int fd,
                        char *buf,
                        unsigned int buf_size)
{
  ssize_t size;
  unsigned int len = 0;

  while (len < buf_size) 
  {
      size = write (fd, &buf[len], buf_size - len);
      if (size == 0) 
      {
          abort (); /* Should never happen */
      }
      if (size < 0) 
      {
          if (errno == EINTR) 
          {
              continue;
          } else
          {
              perror ("write");
              abort ();
          }
      }
      len += size;
  }
}

/*********************************************************************
* @purpose  Open Database File
*           
* @param    data_table - The database to write to the file.
* @param    dir_name - Directory where to store the database.
* @param    file_type - "schema" or "data".
* @param    create - Flag indicating that the file should be
*                    created (or re-created).
*
* @returns  File descriptor to the open file.
*           The file_type is appended to the file name.
*
* @notes  
*       
* @end
*********************************************************************/
static int syncdb_file_open (dataTableNode_t *data_table,
                            char *dir_name,
                                    char *file_type,
                                    int create)
{
  char schema_file_name [SYNCDB_MAX_STR_LEN];
  int size;
  int fd;

  memset (schema_file_name, 0, SYNCDB_MAX_STR_LEN);
  size = snprintf (schema_file_name, SYNCDB_MAX_STR_LEN - 1,
                   "%s/%s--%s", 
                   dir_name, data_table->table_name, file_type);
  if (size > (SYNCDB_MAX_STR_LEN - 1)) 
  {
      abort ();
  }

  if (create) 
  {
      fd = open (schema_file_name, O_CREAT | O_RDWR | O_TRUNC, 0777);
  } else
  {
      fd = open (schema_file_name, O_RDONLY);
  }

  if (fd < 0)
  {
      abort ();
  }

  return fd;
}

/*********************************************************************
* @purpose  Read a file until specified character is found.
*           
* @param    fd - Open file descriptor.
* @param    key - Character to search in the file.
*
* @returns  SYNCDB_OK - Character found.
* @returns  SYNCDB_ERROR - Reached end of file without finding the key.
*
* @notes 
* 
* @end
*********************************************************************/
static int syncdb_json_char_find (int fd, unsigned char key)
{
  int rc;
  unsigned char next_char;

  do {
      rc = syncdb_char_get (fd, &next_char);
      if (rc != SYNCDB_OK) 
      {
          return SYNCDB_ERROR;
      }
      if (next_char == key) 
      {
          return SYNCDB_OK;
      }
  } while (1);
}

/*********************************************************************
* @purpose  Get JSON object from the file.
*           
* @param    num - Number of elements in the object.
* @param    json_obj - Object descriptor. The caller must supply the
*                object type for each object and must set all other
*                fields to 0.
* @param    fd - Open file descriptor. The file location must
*                be sometime before the '{' character that
*                marks the beginning of the object.
*
* @returns  SYNCDB_OK - Memory allocated for objects.
* @returns  SYNCDB_ERROR - No object found, memory not allocated.
*
* @notes  The function allocates dynamic memory and attaches it
*         to the json_obj elements. If the function returns
*         SYNCDB_OK then the caller must free the dynamic memory.
* 
* @end
*********************************************************************/
static int syncdb_json_object_get (int num,
                                   jsonObjectDescr_t *json_obj,
                                   int fd)
{
  int i, j;
  int rc = SYNCDB_ERROR;
  unsigned char int_buf[32];
  unsigned char next_char;

    for (i = 0; i < num; i++)
    {
        rc = syncdb_json_char_find (fd, ':');
        if (rc != SYNCDB_OK)
        {
            break;
        }
        if (json_obj[i].type == SYNCDB_JSON_NUMBER)
        {
            memset (int_buf, 0, sizeof (int_buf));
            for (j = 0; j < sizeof (int_buf); j++)
            {
                rc = syncdb_char_get (fd, &int_buf[j]);
                if (rc != SYNCDB_OK)
                {
                    break;
                }
                if (int_buf[j] == ',')
                {
                    /* Found end of the numeric field.
                    */
                    int_buf[j] = 0;
                    break;
                }
            }
            if ((rc != SYNCDB_OK) || (j == sizeof(int_buf)))
            {
                /* If we go an error while reading the file or
                ** the numeric field cannot fit into the int_buf then 
                ** abort with an error. 
                */
                rc = SYNCDB_ERROR;
                break;
            }
            /* Convert the numeric field into a number.
            */
            json_obj[i].int_value = atoll ((char *)int_buf);
        } else
        {
            rc = syncdb_json_char_find (fd, '"');
            if (rc != SYNCDB_OK)
            {
                break;
            }
            json_obj[i].size = 0;
            do
            {
                json_obj[i].value = realloc (json_obj[i].value, json_obj[i].size + 1);
                rc = syncdb_char_get (fd, &next_char);
                if (rc != SYNCDB_OK)
                {
                    break;
                }
                if (next_char == '"')
                {
                    /* Found end of string.
                    */
                    json_obj[i].value[json_obj[i].size] = 0;
                    break;
                }

                json_obj[i].value[json_obj[i].size] = next_char;
                json_obj[i].size++;
            } while (1);
            if (rc != SYNCDB_OK)
            {
                break;
            }
        }
    }
    if (i == num)
    {
        /* We did not break early, so the everything must be OK...
        */
        rc = SYNCDB_OK;
    }

    if (rc == SYNCDB_ERROR)
    {
        /* Free any dynamic memory allocated for this descriptor.
        */
        for (i = 0; i < num; i++)
        {
            if (json_obj[i].value)
            {
                free (json_obj[i].value);
                json_obj[i].value = 0;
            }
        }
    }

    return rc;
}

/*********************************************************************
* @purpose  Get JSON Schema
*           
* @param    fd - Open file descriptor for the JSON schema.
*
* @returns  Pointer to the schema.
* @returns  0 - Schema error, memory not allocated.
*
* @notes  The function allocates a linked list of schema nodes.
*         The caller must invoke syncdb_schema_free() in order
*         to free that memory.
* 
* @end
*********************************************************************/
static jsonSchemaNode_t *syncdb_schema_get (int fd)
{
  jsonSchemaNode_t *schema_node_list = 0;
  jsonSchemaNode_t *next_node = 0;
  int rc;
  jsonObjectDescr_t schema[5]; 

    rc = syncdb_json_char_find (fd, '[');
    if (rc != SYNCDB_OK) 
    {
        printf("Can't find '[' in schema.\n");
        return 0;
    }

    do {
        memset (schema, 0, sizeof (schema));
        schema[0].type = SYNCDB_JSON_STRING; /* name */
        schema[1].type = SYNCDB_JSON_STRING; /* type */
        schema[2].type = SYNCDB_JSON_NUMBER; /* offset */
        schema[3].type = SYNCDB_JSON_NUMBER; /* size */
        schema[4].type = SYNCDB_JSON_STRING; /* default */

        rc = syncdb_json_object_get (5, schema, fd);
        if (rc != SYNCDB_OK) 
        {
            return schema_node_list;
        }

        if (schema_node_list == 0) 
        {
            next_node = malloc (sizeof (jsonSchemaNode_t));
            schema_node_list = next_node;
        } else
        {
            next_node->next = malloc (sizeof(jsonSchemaNode_t));
            next_node = next_node->next;
        }
        memset (next_node, 0, sizeof (jsonSchemaNode_t));
        next_node->schema_node.data_name = schema[0].value;
        next_node->schema_node.data_offset = schema[2].int_value;
        next_node->schema_node.data_size = schema[3].int_value;

        if (0 == strcmp(schema[1].value, "number")) 
        {
            next_node->schema_node.data_type = SYNCDB_JSON_NUMBER;
            next_node->schema_node.val.default_number = atoll(schema[4].value);
            free (schema[4].value);
        } else if (0 == strcmp(schema[1].value, "string")) 
        {
            next_node->schema_node.data_type = SYNCDB_JSON_STRING;
            next_node->schema_node.val.default_string = schema[4].value;
        } else
        {
            next_node->schema_node.data_type = SYNCDB_JSON_ARRAY;

            /* Arrays don't have meaningful default values.
            */
            free (schema[4].value);
        }
        free (schema[1].value);

        /* At this point all dynamically allocated blocks in the schema[] array
        ** have either been freed or assigned to the next_node structure. The elements 
        ** assigned to the next_node will be freed later. 
        */
#if 0
        printf("Name: %s\n", next_node->schema_node.data_name);
        printf("Type: %d\n", next_node->schema_node.data_type);
        printf("Offset: %d\n", next_node->schema_node.data_offset);
        printf("Size: %d\n", next_node->schema_node.data_size);
        switch (next_node->schema_node.data_type)
        {
        case SYNCDB_JSON_NUMBER:
            printf("Default: %llu\n", next_node->schema_node.val.default_number);
            break;
        case SYNCDB_JSON_STRING:
            printf("Default: %s\n", next_node->schema_node.val.default_string);
            break;
        case SYNCDB_JSON_ARRAY:
            printf("Default: None\n");
            break;
        }
#endif
        
    } while (1);
}

/*********************************************************************
* @purpose  Free JSON Schema
*           
* @param    schema - Linked list of schema nodes.
*
* @returns  none
*
* @notes  The function frees schema nodes and any dynamic memory
*         attached to the schema nodes.
* 
* @end
*********************************************************************/
static void syncdb_schema_free (jsonSchemaNode_t *schema)
{
  jsonSchemaNode_t *tmp;

    while (schema)
    {
        free (schema->schema_node.data_name);
        if (schema->schema_node.data_type == SYNCDB_JSON_STRING)
        {
            free (schema->schema_node.val.default_string);
        }
        tmp = schema;
        schema = schema->next;
        free (tmp);
    }
}

/*********************************************************************
* @purpose  Write a record in the JSON file.
*           
* @param    node - Pointer to the data record.
* @param    fd - File descriptor pointing to the open data file.
* @param    schema - Schema for the table.
*
* @returns  None
*
* @notes  
*       
* @end
*********************************************************************/
void syncdb_record_store (unsigned char *node,
                       int fd,
                       jsonSchemaNode_t *schema)
{
  int i;
  char int_buf[32];
  unsigned long long int_val;

    syncdb_file_write (fd,"{\n", 2);
    syncdb_file_write (fd,"\"node\": [\n", 10);

    while (schema)
    {
        syncdb_file_write (fd,"\t{\n", 3);

        syncdb_file_write (fd, "\t\"name\": ", 9);
        syncdb_file_write (fd, "\"", 1);
        i = 0;
        while (schema->schema_node.data_name[i])
        {
            syncdb_file_write (fd, &schema->schema_node.data_name[i], 1);
            i++;
        }
        syncdb_file_write (fd, "\",\n", 3);
        syncdb_file_write (fd, "\t\"data\": ", 9);
        syncdb_file_write (fd, "\"", 1);
        switch (schema->schema_node.data_type)
        {
        case SYNCDB_JSON_NUMBER:
            if (schema->schema_node.data_size == 1)
            {
                int_val = (unsigned long long) 
                    (*(unsigned char *) &node[schema->schema_node.data_offset]);
            } else if (schema->schema_node.data_size == 2)
            {
                int_val = (unsigned long long) 
                    (*(unsigned short *) &node[schema->schema_node.data_offset]);
            } else if (schema->schema_node.data_size == 4)
            {
                int_val = (unsigned long long) 
                    (*(uint32_t *) &node[schema->schema_node.data_offset]);
            } else
            {
                int_val = (unsigned long long) 
                    (*(unsigned long long *) &node[schema->schema_node.data_offset]);
            }
            snprintf (int_buf, sizeof(int_buf), "%llu", int_val);
            i = 0;
            while (int_buf[i])
            {
                syncdb_file_write (fd, &int_buf[i], 1);
                i++;
            }
            break;

        case SYNCDB_JSON_STRING:
            i = 0;
            while (node[schema->schema_node.data_offset + i])
            {
                syncdb_file_write (fd, (char *)&node[schema->schema_node.data_offset + i], 1);
                i++;
            }
            break;

        case SYNCDB_JSON_ARRAY:
            for (i = 0; i < schema->schema_node.data_size; i++)
            {
                sprintf (int_buf, "%3u ", node[schema->schema_node.data_offset + i]);
                syncdb_file_write (fd, int_buf, 4);
            }
            break;
        }
        syncdb_file_write (fd, "\"\n", 2);

        syncdb_file_write (fd,"\t}", 2);
        schema = schema->next;
        if (schema)
        {
            syncdb_file_write (fd,",\n", 2);
        } else
        {
            syncdb_file_write (fd,"\n", 1);
        }
    }

    syncdb_file_write (fd,"]}", 2);
}

/*********************************************************************
* @purpose  Write AVL tree to the database.
*           
* @param    data_table - The database to write to the file.
* @param    fd - File descriptor pointing to the open data file.
* @param    schema - Schema for the table.
*
* @returns  None
*
* @notes  
*       
* @end
*********************************************************************/
void syncdb_avl_store (dataTableNode_t *data_table,
                       int fd,
                       jsonSchemaNode_t *schema)
{
    avlTree_t *avl_tree;
    char element [SYNCDB_RECORD_MAX_SIZE];
    void *node;

    syncdb_file_write (fd,"{\n", 2);
    syncdb_file_write (fd,"\"avl_tree\": [\n", 14);

    avl_tree = data_table->record_ptr;
    memset (element, 0, data_table->record_size);

    node = avlSearch(avl_tree, element, AVL_EXACT);
    if (!node)
    {
        node = avlSearch(avl_tree, element, AVL_NEXT);
    }
    if (node)
    {
        do
        {
            syncdb_record_store (node, fd, schema);
            node = avlSearch(avl_tree, node, AVL_NEXT);
            if (node)
            {
                syncdb_file_write (fd,",\n", 2);
            } else
            {
                syncdb_file_write (fd,"\n", 1);
            }
        } while (node);
    }
    syncdb_file_write (fd,"]\n", 2);
    syncdb_file_write (fd,"}\n", 2);
}

/*********************************************************************
* @purpose  Find node in the schema that matches the name.
*           
* @param    name - Name of the node.
* @param    schema_list - Linked list of schema nodes.
*
* @returns  Pointer to the schema node.
* @returns  0 - Matching schema is not found.
*
* @notes  
*       
* @end
*********************************************************************/
static jsonSchemaNode_t *syncdb_schema_search (unsigned char *name,
                       jsonSchemaNode_t *schema_list)
{
 jsonSchemaNode_t *tmp;

 tmp = schema_list;
 do
 {
     if (0 == strcmp ((char *)name, tmp->schema_node.data_name))
     {
         return tmp;
     }
     tmp = tmp->next;
 } while (tmp);

 return 0;
}
/*********************************************************************
* @purpose  Encode a number into the data element.
*           
* @param    data_ptr - Start of memory where to copy the number.
* @param    value - Integer value to write to the memory.
* @param    size - Interger size (1, 2, 4, or 8).
*
* @returns  None
*
* @notes  
*       
* @end
*********************************************************************/
static void syncdb_number_encode (unsigned char *data_ptr,
                                  unsigned long long value,
                                  unsigned int size)
{
  unsigned char val_1;
  unsigned short val_2;
  uint32_t val_4;

  if (size == 1)
  {
      val_1 = (unsigned char) value;
      *(unsigned char *) data_ptr = val_1;
  } else if (size == 2)
  {
      val_2 = (unsigned short) value;
      *(unsigned short *) data_ptr = val_2;
  } else if (size == 4)
  {
      val_4 = (uint32_t) value;
      *(uint32_t *) data_ptr = val_4; 
  } else
  {
      *(unsigned long long *) data_ptr = value;
  }
}

/*********************************************************************
* @purpose  Encode an array into the data element.
*           
* @param    data_ptr - Start of memory where to copy the array.
* @param    value - Pointer to a string containing the encoded array.
* @param    size - Number of bytes in the array. 
*
* @returns  None
*
* @notes  
*       
* @end
*********************************************************************/
static void syncdb_array_encode (unsigned char *data_ptr,
                                  unsigned char *value,
                                  unsigned int size)
{
  int i;
  int char_index;
  int len;

  i = 0;
  char_index = 0;
  len = strlen ((char *)value);
  do
  {
    data_ptr[i] = (unsigned char) atoi((char *)&value[char_index]);
    char_index += 4;
    i++;
    if ((i >= size) || (char_index >= len))
    {
        return;
    }
  } while (1);
}

/*********************************************************************
* @purpose  Set the data field to the default value specified
*           in the schema.
*           
* @param    node - Pointer to the start of the record.
* @param    schema - Schema for the data field.
*
* @returns  None
*
* @notes  
*       
* @end
*********************************************************************/
static void syncdb_node_default_set (unsigned char *node,
                       jsonSchemaNode_t *schema)
{
    switch (schema->schema_node.data_type)
    {
    case SYNCDB_JSON_NUMBER:
        syncdb_number_encode (&node[schema->schema_node.data_offset], 
                              schema->schema_node.val.default_number,
                              schema->schema_node.data_size);

        break;
    case SYNCDB_JSON_STRING:
        strncpy ((char *)&node[schema->schema_node.data_offset], 
                schema->schema_node.val.default_string,
                 schema->schema_node.data_size - 1);
        break;
    case SYNCDB_JSON_ARRAY:
        memset (&node[schema->schema_node.data_offset],
                0,
                schema->schema_node.data_size);
        break;
    }
}

/*********************************************************************
* @purpose  Migrate a number from the old schema to the new 
*           schema.
*           
* @param    node - Pointer to the start of the record.
* @param    schema - New schema for the data field.
* @param    value - String containing the numeric value.
*
* @returns  none
* 
* @notes  The size of numeric fields can be changed between
*         schema versions. If the new schema data size is bigger
*         than the old schema data size then
*         the numeric data is simply copied to the new schema.
*         If the new data size is smaller than the old
*         data size then if the numeric value fits into the
*         new data size then it is copied to the new schema,
*         otherwise the default value is used for the data.
*       
* @end
*********************************************************************/
static void syncdb_number_migrate (unsigned char *node,
                                   jsonSchemaNode_t *schema,
                                   unsigned char *value)
{
    unsigned long long int_val;
    int use_default = 0;

    sscanf ((char *)value, "%llu", &int_val);
    if (schema->schema_node.data_size == 1)
    {
        if (int_val >= (1 << 8))
        {
            use_default = 1;
        } 
    } else if (schema->schema_node.data_size == 2)
    {
        if (int_val >= (1 << 16))
        {
            use_default = 1;
        } 
    } else if (schema->schema_node.data_size == 4)
    {
        if (int_val >= (1LL << 32))
        {
            use_default = 1;
        } 
    } 
    if (use_default)
    {
        syncdb_number_encode (&node[schema->schema_node.data_offset], 
                              schema->schema_node.val.default_number,
                              schema->schema_node.data_size);
    } else
    {
        syncdb_number_encode (&node[schema->schema_node.data_offset], 
                              int_val,
                              schema->schema_node.data_size);
    }
}

/*********************************************************************
* @purpose  Migrate a string from the old schema to the new 
*           schema.
*           
* @param    node - Pointer to the start of the record.
* @param    schema - New schema for the data field.
* @param    value - String containing the string value.
*
* @returns  None
*
* @notes   The string length in the new schema may change from
*          the old schema. If the new schema string length is longer
*          than the old schema then the string is simply copied
*          to the new schema. If the new schema length is shorter,
*          but the string fits then the string is copied.
*          If the string, including the zero termination character,
*          does not fit into the new schema then the default
*          value is used.
*       
* @end
*********************************************************************/
static void syncdb_string_migrate (unsigned char *node,
                                   jsonSchemaNode_t *schema,
                                   unsigned char *value)
{
   int len;

   len = strlen ((char *)value);
   if (len < schema->schema_node.data_size )
   {
       strncpy ((char *)&node[schema->schema_node.data_offset],
                (char *)value,
                schema->schema_node.data_size - 1);
   } else
   {
       strncpy ((char *)&node[schema->schema_node.data_offset],
                schema->schema_node.val.default_string,
                schema->schema_node.data_size - 1);
   }
}

/*********************************************************************
* @purpose  Migrate an array from the old schema to the new 
*           schema.
*           
* @param    node - Pointer to the start of the record.
* @param    schema - New schema for the data field.
* @param    value - String containing the array value.
*
* @returns  None
*
* @notes   The array size in the new schema may be different from
*          the old schema. If the new array is shorter then
*          only the elements that fit into the new array are
*          copied from the old array. If the new array is longer then
*          the unused elements in the new array are set to 0.
*       
* @end
*********************************************************************/
static void syncdb_array_migrate (unsigned char *node,
                                   jsonSchemaNode_t *schema,
                                   unsigned char *value)
{
    /* Copy the array into the node.
    ** Since the node is zeroed out before being passed into this 
    ** function, any unused elements are left as zeroes. 
    */
    syncdb_array_encode (&node[schema->schema_node.data_offset],
                         value,
                         schema->schema_node.data_size);
}

/*********************************************************************
* @purpose  Read a record from the JSON file.
*           
* @param    node - Pointer to the data record.
* @param    fd - File descriptor pointing to the open data file.
* @param    old_schema - Schema for the file.
* @param    new_schema - Schema for the new database.
*
* @returns  SYNCDB_OK - Got record from file.
* @returns  SYNCDB_ERROR - End of file or some other error.
*
* @notes  
*       
* @end
*********************************************************************/
int syncdb_record_read (unsigned char *node,
                       int fd,
                       jsonSchemaNode_t *old_schema,
                       jsonSchemaNode_t *new_schema)
{
  jsonObjectDescr_t node_data[2]; 
  int rc;
  unsigned char next_char;
  jsonSchemaNode_t *old_node_schema, *new_node_schema;

      rc = syncdb_json_char_find (fd, '[');
      if (rc != SYNCDB_OK) 
      {
          return SYNCDB_ERROR;
      }

      do
      {
          /* Read characters from the file until we find a '{', ']' or
          ** end of file. The '{' character means that we need to read another record. 
          ** The ']' or end of file terminates the record. 
          */
          do
          {
              rc = syncdb_char_get (fd, &next_char);
              if (rc != SYNCDB_OK)
              {
                  /* End of file.
                  */
                  return SYNCDB_ERROR; 
              }
              if (next_char == ']')
              {
                  /* End of record.
                  ** Check whether there are any fields in the new schema that are not 
                  ** present in the old schema. Set default values for those fields. 
                  */
                  new_node_schema = new_schema;
                  while (new_node_schema)
                  {
                      old_node_schema = syncdb_schema_search ((unsigned char *)new_node_schema->schema_node.data_name,
                                                              old_schema);
                      if (!old_node_schema)
                      {
                          syncdb_node_default_set (node, new_node_schema);
                      }
                      new_node_schema = new_node_schema->next;
                  }

                  return SYNCDB_OK;  
              }
          } while (next_char != '{');

          memset (node_data, 0, sizeof (node_data));
          node_data[0].type = SYNCDB_JSON_STRING; /* name */
          node_data[1].type = SYNCDB_JSON_STRING; /* data */

          rc = syncdb_json_object_get (2, node_data, fd);
          if (rc != SYNCDB_OK)
          {
              /* No more objects...
              */
              return SYNCDB_ERROR;
          }

          do
          {
#if 0
              printf("Name: %s\n", node_data[0].value);
              printf("Data: %s\n", node_data[1].value);
#endif

              /* Find schema records for this data node in the old schema and
              ** the new schema. 
              */
              old_node_schema = syncdb_schema_search ((unsigned char *)node_data[0].value, old_schema);
              new_node_schema = syncdb_schema_search ((unsigned char *)node_data[0].value, new_schema);
              if (!old_node_schema || !new_node_schema)
              {
                  /* If schema is missing for this field then skip the field.
                  */
                  break;
              }

              /* If the new and old schema data types are not the same then
              ** data migration is not possible.
              */
              if (old_node_schema->schema_node.data_type != new_node_schema->schema_node.data_type)
              {
                  /* Initialize the data field to the default value specified in the new
                  ** schema.
                  */
                  syncdb_node_default_set (node, new_node_schema);
                  break;
              }

              switch (old_node_schema->schema_node.data_type)
              {
              case SYNCDB_JSON_NUMBER:
                  syncdb_number_migrate ((unsigned char *)node, new_node_schema, (unsigned char *)node_data[1].value);
                  break;
              case SYNCDB_JSON_STRING:
                  syncdb_string_migrate ((unsigned char *)node, new_node_schema, (unsigned char *)node_data[1].value);
                  break;
              case SYNCDB_JSON_ARRAY:
                  syncdb_array_migrate ((unsigned char *)node, new_node_schema, (unsigned char *)node_data[1].value);
                  break;
              }
          } while (0);

          free (node_data[0].value);
          free (node_data[1].value);

      } while (1);

}

/*********************************************************************
* @purpose  Read AVL tree from the database.
*           
* @param    data_table - The database to populate from a file.
* @param    fd - File descriptor pointing to the open data file.
* @param    old_schema - Schema used for the file.
* @param    new_schema - Schema used for the new database format.
*
* @returns  None
*
* @notes  
*       
* @end
*********************************************************************/
void syncdb_avl_read (dataTableNode_t *data_table,
                       int fd,
                       jsonSchemaNode_t *old_schema,
                       jsonSchemaNode_t *new_schema)
{
  int rc;
  unsigned char node [SYNCDB_RECORD_MAX_SIZE];
  unsigned char *inserted_node;
  avlTree_t *avl_tree;
  syncdbAvlNodeCtrl_t *node_ctrl;

    avl_tree = data_table->record_ptr;

    /* Point to the first node in the data file.
    */
    rc = syncdb_json_char_find (fd, '[');
    if (rc != SYNCDB_OK) 
    {
        printf("Can't find '[' in data file. (table name = %s)\n",
               data_table->table_name);
        return;
    }

    do
    {
        memset (node, 0, data_table->record_size);

        rc = syncdb_record_read (node, fd, old_schema, new_schema);
        if (rc != SYNCDB_OK)
        {
            /* Reached end of data file.
            */
            break; 
        }

        if (data_table->num_non_deleted_records == data_table->max_non_deleted_records)
        {
            /* Table is full. Stop reading from the file.
            */
          break;
        }

        node_ctrl = (syncdbAvlNodeCtrl_t *)((char *)node + data_table->record_size);
        memset (node_ctrl, 0, sizeof (syncdbAvlNodeCtrl_t));
        memset (node_ctrl->changed_mask, 0xff, CLIENT_MASK_SIZE);

        inserted_node = avlInsertEntry(avl_tree,  node);
        if (inserted_node)
        {
            /* Found a duplicate entry in the data table.
            ** This should not happen.
            */
            printf("Duplicate AVL Entry... (table name = %s)\n",
                   data_table->table_name);
            break;
        }
        data_table->num_non_deleted_records++;
        data_table->num_records++;
    } while (1);
}

/*********************************************************************
* @purpose  Write Database to a JSON file.
*           
* @param    data_table - The database to write to the file.
* @param    dir_name - Directory where to store the database.
*
* @returns  None
*
* @notes  
*       
* @end
*********************************************************************/
void syncdb_database_store (dataTableNode_t *data_table,
                            char *dir_name)
{
    int fd;
    jsonSchemaNode_t *schema;

    fd = syncdb_file_open (data_table, dir_name, "schema", 1);
    if (fd < 0)
    {
        perror ("Can't open schema file for writing.");
        abort ();
    }
    syncdb_file_write (fd, data_table->schema, data_table->schema_size);
    close (fd);

    /* Re-open the schema file for reading.
    */
    fd = syncdb_file_open (data_table, dir_name, "schema", 0);
    if (fd < 0)
    {
        perror ("Can't open schema file for reading.");
        abort ();
    }

    /* Parse the schema.
    */
    schema = syncdb_schema_get (fd); 
    close (fd);
    if (!schema) 
    {
        printf("Schema Error.\n");
        return;
    }

    /* Open the database content file.
    */
    fd = syncdb_file_open (data_table, dir_name, "data", 1);
    if (fd < 0)
    {
        perror ("Can't open data file for writing.");
        abort ();
    }
    if (data_table->table_type == TABLE_TYPE_AVL)
    {
        syncdb_avl_store (data_table, fd, schema);
    } else if (data_table->table_type == TABLE_TYPE_RECORD)
    {
        syncdb_record_store (data_table->record_ptr, fd, schema);
    }
    close (fd);

    syncdb_schema_free (schema);
}

/*********************************************************************
* @purpose  Populate the Database from a JSON file.
*           
* @param    data_table - The database to read from the file.
*
* @returns  None
*
* @notes  
*       
* @end
*********************************************************************/
void syncdb_database_read (dataTableNode_t *data_table)
{
    int fd;
    jsonSchemaNode_t *old_schema, *new_schema;
    char *dir_name;
    char tmp_path[SYNCDB_FILE_PATH_BUFF_SIZE];

    if (data_table->create_flags & SYNCDB_TABLE_FLAG_NVRAM)
    {
        dir_name = _syncdb_nv_path;
    }
#ifdef RAMDISK
    else
    {
        dir_name = SYNCDB_TMP_ROOT "/ramdisk";
    }
#endif

    /* Create a temporary schema file for the new database.
    */
    sprintf(tmp_path, "%s%s", _syncdb_nv_path_base, "/tmp-schema");
    fd = open (tmp_path, O_CREAT | O_RDWR | O_TRUNC, 0777);
    if (fd < 0)
    {
        perror ("Can't open temporary schema file.");
        abort ();
    }

    syncdb_file_write (fd, data_table->schema, data_table->schema_size);
    close (fd);

    /* Re-open the schema file for reading.
    */
    sprintf(tmp_path, "%s%s", _syncdb_nv_path_base, "/tmp-schema");
    fd = open (tmp_path, O_RDONLY, 0777);
    if (fd < 0)
    {
        perror ("Can't open temporary schema file for reading.");
        abort ();
    }

    /* Parse the new schema.
    */
    new_schema = syncdb_schema_get (fd); 
    close (fd);
    if (!new_schema) 
    {
        printf("New Schema Error.\n");
        return;
    }

    /* Open the schema for the stored file.
    */
    fd = syncdb_file_open (data_table, dir_name, "schema", 0);
    if (fd < 0)
    {
        /* The schema for this database does not exist.
        ** For AVL trees there is nothing else to do. 
        ** For records load the default values from the schema into the record. 
        */
        if (data_table->table_type == TABLE_TYPE_RECORD)
        {
            old_schema = new_schema;
            while (old_schema)
            {
                syncdb_node_default_set (data_table->record_ptr, old_schema);
                old_schema = old_schema->next;
            }
        }
        syncdb_schema_free (new_schema);
        return;
    }
    old_schema = syncdb_schema_get (fd); 
    close (fd);

    if (!old_schema)
    {
        printf("Old Schema Error.\n");
        syncdb_schema_free (new_schema);
        return;
    }

    /* Open the database content file.
    */
    fd = syncdb_file_open (data_table, dir_name, "data", 0);
    if (data_table->table_type == TABLE_TYPE_AVL)
    {
        syncdb_avl_read (data_table, fd, old_schema, new_schema);
    } else if (data_table->table_type == TABLE_TYPE_RECORD)
    {
        (void) syncdb_record_read (data_table->record_ptr, fd, old_schema, new_schema);
    }
    close (fd);

    syncdb_schema_free (old_schema);
    syncdb_schema_free (new_schema);
}

