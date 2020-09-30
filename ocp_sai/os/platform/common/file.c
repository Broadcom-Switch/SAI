/*********************************************************************
 *
 * Copyright: (c) 2018 Broadcom.
 * Broadcom Proprietary and Confidential. All rights reserved.
 *
 *********************************************************************/

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>

/**************************************************************************
 * @purpose  Read line or specified number of bytes from the file descriptor.
 *
 * @param    fd       Open file descriptor.
 * @param    buffer   where to put data
 * @param    nbytes   amount of data to read, also return the read length
 *
 * @returns  0
 * @returns  -1
 *
 * @comments
 *
 * @end
 **************************************************************************/
int _sai_file_readline(int fd, char* buffer, int* nbytesp)
{
  unsigned int nbytes = *nbytesp;
  int byte_count, total_bytes;
  int  rc = 0;

  *nbytesp = total_bytes = 0;
  do
  {
    byte_count = read(fd, &buffer[total_bytes], 1);
    if (byte_count > 0)
    {
      if (buffer[total_bytes] == '\n')
      {
        buffer[total_bytes] = 0;
        while ((total_bytes > 0) && (buffer[total_bytes - 1] == '\r'))
        {
          buffer[total_bytes - 1] = 0;
          total_bytes -= 1;
        }
        break;
      }
      total_bytes += byte_count;
      continue;
    }
    rc = -1;
    break;
  }
  while ((byte_count > 0) && (total_bytes != nbytes));

  *nbytesp = total_bytes;
  return rc;
}

/*****************************************************************//**
* \brief Opens a file
*
* \param filename     [IN]  File to Open
* \param fd           [IN]  Pointer to file descriptor
*
* \return -1          if file does not exist
********************************************************************/
int _sai_fs_open(char *filename, int *fd)
{
  if (NULL == filename)
  {
    return -1;
  }
  if ((*fd = open(filename, O_RDWR | O_SYNC, 0644)) == -1)
  {
    return(-1);
  }

  return(0);
}

/*****************************************************************//**
* \brief closes a file opened by _sai_fs_open
*
* \param filedesc     [IN]  descriptor of file to close
*
* \return -1          if file already closed
* \return  0          Otherwise
********************************************************************/
int _sai_fs_close(int filedesc)
{
  if (close(filedesc) == -1)
  {
    printf("File close failed for descriptor %d \n", filedesc);
    return(-1);
  }
  sync();

  return(0);
}

/*****************************************************************//**
 * \brief convert all letters inside a buffer to lower case
 *
 * \param buf       [IN/OUT]  name of the buffer
 *
 * \return void
 * \note   This f(x) returns the same letter in the same buffer but all
 *         lower case, checking the buffer for empty string
 *
 ********************************************************************/
void convertStrToLowerCase(char *buf)
{
  char c_tmp;
  unsigned int i;
  unsigned int len; 

  len = strlen(buf);
  for (i = 0; i < len; i++) 
  {
    c_tmp = (char)  tolower((unsigned char)buf[i]);
    buf[i] = c_tmp;
  }
}

unsigned int platform_string_find(char *file, char *id)
{
  char buf[1024];
  int len, fd = -1;
  int rc;

  /* Identify this switch by searching for platform ID in the given platform file */
  memset(buf, 0, sizeof(buf));
  rc = _sai_fs_open(file, &fd);
  if (rc != 0)
  {
    return -1;
  }

  /* scan the entire file looking for hardcode value anywhere in any entry */
  while (rc == 0)
  {
    len = sizeof (buf);
    rc = _sai_file_readline(fd, buf, &len);
    if ((rc == 0) && (len > 0))
    {
      /* scan each line for a platform name */
      if (strstr(buf, id))
      {
        (void) _sai_fs_close(fd);
        return 0;
      }
    }
  }

  (void) _sai_fs_close(fd);
  return -1;
}
