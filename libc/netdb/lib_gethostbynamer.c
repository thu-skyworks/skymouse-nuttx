/****************************************************************************
 * libc/netdb/lib_gethostbynamer.c
 *
 *   Copyright (C) 2015 Gregory Nutt. All rights reserved.
 *   Author: Gregory Nutt <gnutt@nuttx.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <errno.h>

#include <arpa/inet.h>

#include <nuttx/net/dnsclient.h>

#include "lib_internal.h"

#ifdef CONFIG_LIBC_NETDB

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Type Definitions
 ****************************************************************************/

/* This is the layout of the caller provided memory area */

struct hostent_info_s
{
  FAR char *hi_addrlist[2];
  char hi_data[1];
};

/****************************************************************************
 * Private functions
 ****************************************************************************/

/****************************************************************************
 * Name: lib_numeric_address
 *
 * Description:
 *   Check if the name is a numeric IP address. In this case, simply copy
 *   name into the h_name field and its struct in_addr equivalent into the
 *   h_addr_list[0] field of the returned hostent structure.
 *
 * Input paramters:
 *   stream - File stream of the opened hosts file with the file pointer
 *     positioned at the beginning of the next host entry.
 *   host - Caller provided location to return the host data.
 *   buf - Caller provided buffer to hold string data associated with the
 *     host data.
 *   buflen - The size of the caller-provided buffer
 *
 * Returned Value:
 *   Zero (0) is returned if the name is an numeric IP address.
 *
 ****************************************************************************/

static int lib_numeric_address(FAR const char *name, FAR struct hostent *host,
                               FAR char *buf, size_t buflen)
{
  FAR struct hostent_info_s *info;
  FAR char *ptr;
  size_t addrlen;
  int namelen;
  int ret;

  /* Verify that we have a buffer big enough to get started (it still may not
   * be big enough).
   */

 if (buflen <= sizeof(struct hostent_info_s))
   {
     return -ERANGE;
   }

  info    = (FAR struct hostent_info_s *)buf;
  ptr     = info->hi_data;
  buflen -= (sizeof(struct hostent_info_s) - 1);

  memset(host, 0, sizeof(struct hostent));
  memset(info, 0, sizeof(struct hostent_info_s));

  /* If the address contains a colon, then it might be a numeric IPv6 */

  if (strchr(name, ':') != NULL)
    {
      /* Make sure that space remains to hold the IPv6 address */

      addrlen = sizeof(struct in6_addr);
      if (buflen < addrlen)
        {
          return -ERANGE;
        }

      ret = inet_pton(AF_INET6, name, ptr);
      if (ret < 0)
        {
          /* Conversion failed.  Must not be a IPv6 address */

          return 1;
        }

      host->h_addrtype  = AF_INET6;
    }
  /* If the address contains a colon, then it might be a numeric IPv6 */

  else if (strchr(name, '.') != NULL)
    {
      /* Make sure that space remains to hold the IPv4 address */

      addrlen = sizeof(struct in_addr);
      if (buflen < addrlen)
        {
          return -ERANGE;
        }

      ret = inet_pton(AF_INET, name, ptr);
      if (ret < 0)
        {
          /* Conversion failed.  Must not be an IPv4 address */

          return 1;
        }

      host->h_addrtype  = AF_INET;
    }

  /* No colon?  No period?  Can't be a numeric address */

  else
    {
      return 1;
    }

  info->hi_addrlist[0] = ptr;
  host->h_addr_list    = info->hi_addrlist;
  host->h_length       = addrlen;

  ptr                 += addrlen;
  buflen              -= addrlen;

  /* And copy name */

  namelen = strlen(name);
  if (addrlen + namelen + 1 > buflen)
    {
      return -ERANGE;
    }

  strncpy(ptr, name, buflen);
  return 0;
}

/****************************************************************************
 * Name: lib_dns_lookup
 *
 * Description:
 *   Try to look-up the host name from the DNS server
 *
 * Input paramters:
 *   name - The name of the host to find.
 *   host - Caller provided location to return the host data.
 *   buf - Caller provided buffer to hold string data associated with the
 *     host data.
 *   buflen - The size of the caller-provided buffer
 *
 * Returned Value:
 *   Zero (0) is returned if the DNS lookup was successful.
 *
 ****************************************************************************/

#ifdef CONFIG_NETDB_DNSCLIENT
static int lib_dns_lookup(FAR const char *name, FAR struct hostent *host,
                          FAR char *buf, size_t buflen)
{
  FAR struct hostent_info_s *info;
  FAR char *ptr;
  size_t addrlen;
  int addrtype;
  int namelen;
  int ret;

  /* Verify that we have a buffer big enough to get started (it still may not
   * be big enough).
   */

 if (buflen <= sizeof(struct hostent_info_s))
   {
     return -ERANGE;
   }

  /* Initialize buffers */

  info    = (FAR struct hostent_info_s *)buf;
  ptr     = info->hi_data;
  buflen -= (sizeof(struct hostent_info_s) - 1);

  memset(host, 0, sizeof(struct hostent));
  memset(info, 0, sizeof(struct hostent_info_s));

  /* Try to get the host address using the DNS name server */

#ifdef CONFIG_NETDB_DNSCLIENT_IPv6
  addrlen  = sizeof(struct in6_addr);
  addrtype = AF_INET6;
  ret      = dns_gethostip(name, (FAR struct in6_addr *)ptr);
#else
  addrlen  = sizeof(struct in6_addr);
  addrtype = AF_INET;
  ret      = dns_gethostip(name, (FAR in_addr_t *)ptr);
#endif

  /* Was the DNS lookup successful? */

  if (ret >= 0)
    {
      /* Yes.. Return the address that we obtained from the DNS name server. */

      info->hi_addrlist[0] = ptr;
      host->h_addr_list    = info->hi_addrlist;
      host->h_addrtype     = addrtype;
      host->h_length       = addrlen;

      ptr                 += addrlen;
      buflen              -= addrlen;

      /* And copy name */

      namelen = strlen(name);
      if (addrlen + namelen + 1 > buflen)
        {
          return -ERANGE;
        }

      strncpy(ptr, name, buflen);
      return OK;
    }

  return ret;
}
#endif /* CONFIG_NETDB_DNSCLIENT */

/****************************************************************************
 * Name: lib_hostfile_lookup
 *
 * Description:
 *   Try to look-up the host name from the network host file
 *
 * Input paramters:
 *   name - The name of the host to find.
 *   host - Caller provided location to return the host data.
 *   buf - Caller provided buffer to hold string data associated with the
 *     host data.
 *   buflen - The size of the caller-provided buffer
 *
 * Returned Value:
 *   Zero (0) is returned if the DNS lookup was successful.
 *
 ****************************************************************************/

#ifdef CONFIG_NETDB_HOSTFILE
static int lib_hostfile_lookup(FAR const char *name, FAR struct hostent *host,
                               FAR char *buf, size_t buflen, int *h_errnop)
{
  FAR FILE *stream;
  int herrnocode;
  int nread;

  /* Search the hosts file for a match */

  stream = fopen(CONFIG_NETDB_HOSTCONF_PATH, "r");
  if (stream == NULL)
    {
      int errcode = errno;

      ndbg("ERROR:  Failed to open the hosts file %s: %d\n",
           CONFIG_NETDB_HOSTCONF_PATH, errcode);
      UNUSED(errcode);

      herrnocode = NO_RECOVERY;
      goto errorout_with_herrnocode;
    }

  /* Loop reading entries from the hosts file until a match is found or
   * until we hit the end-of-file.
   */

  do
    {
      /* Read the next entry from the hosts file */

      nread = lib_parse_hostfile(stream, host, buf, buflen);
      if (nread < 0)
        {
          /* Possible errors:
           *     ERANGE - Buffer not big enough
           *     ESPIPE - End of file (or possibly a read error).
           *     EAGAIN - Error parsing the line (E.g., missing hostname)
           */

          if (nread == -ESPIPE)
            {
              nread = 0;
            }
          else if (nread != -EAGAIN)
            {
              herrnocode = NO_RECOVERY;
              goto errorout_with_stream;
            }
        }
      else if (nread > 0)
        {
          /* We successfully read the entry */

          nvdbg("Comparing %s to %s\n", name, host->h_name);

          /* Check for a host name match */

          if (strcmp(name, host->h_name) == 0)
            {
               /* We have a match */

               fclose(stream);
               return OK;
            }

          /* For a match with any host alias */

          if (host->h_aliases != NULL)
            {
              FAR char **alias;

              for (alias = host->h_aliases; *alias != NULL; alias++)
                {
                  /* Check for a host alias match */

                  if (strcmp(name, *alias) == 0)
                    {
                      /* We have a match */

                      fclose(stream);
                      return OK;
                    }
                }
            }
        }
    }
  while (nread != 0);

  /* We get here when the end of the hosts file is encountered without
   * finding the hostname.
   */

  herrnocode = HOST_NOT_FOUND;

errorout_with_stream:
  fclose(stream);

errorout_with_herrnocode:
  if (h_errnop)
    {
      *h_errnop = herrnocode;
    }

 return ERROR;
}
#endif /* CONFIG_NETDB_HOSTFILE */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: gethostname_r
 *
 * Description:
 *   The gethostbyname_r() function returns a structure of type hostent for
 *   the given host name. Here name is either a hostname, or an IPv4 address
 *   in standard dot notation (as for inet_addr(3)), or an IPv6 address in
 *   colon (and possibly dot) notation.
 *
 *   If name is an IPv4 or IPv6 address, no lookup is performed and
 *   gethostbyname_r() simply copies name into the h_name field
 *   and its struct in_addr equivalent into the h_addr_list[0] field of the
 *   returned hostent structure.
 *
 *   gethostname_r() is *not* POSIX but is similar to a Glibc extension and is
 *   used internally by NuttX to implement the POSIX gethostname().
 *
 * Input Parameters:
 *   name - The name of the host to find.
 *   host - Caller provided location to return the host data.
 *   buf - Caller provided buffer to hold string data associated with the
 *     host data.
 *   buflen - The size of the caller-provided buffer
 *   h_errnop - There h_errno value returned in the event of a failure.
 *
 * Returned Value:
 *   Zero (OK) is returned on success, -1 (ERROR) is returned on a failure
 *   with the returned h_errno value provided the reason for the failure.
 *
 ****************************************************************************/

int gethostbyname_r(FAR const char *name, FAR struct hostent *host,
                    FAR char *buf, size_t buflen, int *h_errnop)
{
#ifdef CONFIG_NETDB_DNSCLIENT
  int ret;
#endif

  DEBUGASSERT(name != NULL && host != NULL && buf != NULL);

  /* Make sure that the h_errno has a non-error code */

  if (h_errnop)
    {
      *h_errnop = 0;
    }

  /* Check for a numeric hostname */

  if (lib_numeric_address(name, host, buf, buflen) == 0)
    {
      /* Yes.. we are done */

      return OK;
    }

  /* Try to find the name in the HOSTALIASES environment variable */
  /* REVISIT: Not implemented */

#ifdef CONFIG_NETDB_DNSCLIENT
  /* Try to get the host address using the DNS name server */

  ret = lib_dns_lookup(name, host, buf, buflen);
  if (ret >= 0)
    {
      /* Successful DNS lookup! */

      return OK;
    }
#endif /* CONFIG_NETDB_DNSCLIENT */

#ifdef CONFIG_NETDB_HOSTFILE
  /* Search the hosts file for a match */

  return lib_hostfile_lookup(name, host, buf, buflen, h_errnop);

#else
  /* The host file file is not supported.  The host name mapping was not
   * found from any lookup heuristic
   */

  if (h_errnop)
    {
      *h_errnop = HOST_NOT_FOUND;
    }

 return ERROR;
#endif
}

#endif /* CONFIG_LIBC_NETDB */
