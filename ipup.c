//  ipup.c
//  ipup
//
//  Created by Dr. Rolf Jansen on 2016-07-17.
//  Copyright © 2016 projectworld.net. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without modification,
//  are permitted provided that the following conditions are met:
//
//  1. Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//
//  2. Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS
//  OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
//  AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER
//  OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
//  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
//  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
//  IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
//  THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "binutils.h"
#include "store.h"


void usage(const char *executable)
{
   const char *r = executable + strvlen(executable);
   while (--r >= executable && *r != '/'); r++;
   printf("%s v1.1.1 ("SVNREV"), Copyright © 2016 Dr. Rolf Jansen\n\n", r);
   printf("Usage:\n\n");
   printf("1) look up the country code belonging to an IP address given by the last command line argument:\n\n");
   printf("   %s [-r bstfiles] [-h] <IP address>\n", r);
   printf("      <IP address>      IPv4 or IPv6 address of which the country code is to be looked up.\n\n");
   printf("      -h                Show these usage instructions.\n\n");
   printf("2) generate a sorted list of IP address/masklen pairs per country code, formatted as ipfw table construction directives:\n\n");
   printf("   %s -t CC:DD:.. | CC=nnnnn:DD=mmmmm:.. | \"\" [-n table number] [-v table value] [-x offset] [-p] [-4] [-6] [-r bstfiles]\n\n", r);
   printf("      -t CC:DD:..       Output all IP address/masklen pairs belonging to the listed countries, given by 2 letter\n");
   printf("         | CC=nnnnn:..  capital country codes, separated by colon. An empty CC list means any country code.\n");
   printf("           | \"\"         A table value can be assigned per country code in the following manner:\n");
   printf("                        -t BR=10000:DE=10100:US:CA:AU=10200. In the case of no assignment, no value\n");
   printf("                        or the global value defined by either the -v or the -x option is utilized.\n");
   printf("      -n table number   The ipfw table number between 0 and 65534 [default: 0].\n");
   printf("      -v table value    A global 32-bit unsigned value for all ipfw table entries [default: 0].\n");
   printf("      -x offset         Decimal encoded given CC and add it to the offset for computing the table value:\n");
   printf("                        value = offset + ((C1 - 'A')*26 + (C2 - 'A'))*10.\n");
   printf("      -p                Plain IP table generation, i.e. without ipfw table construction directives,\n");
   printf("                        and any -n, -v and -x flags are ignored in this mode.\n");
   printf("      -4                Process only the IPv4 address ranges.\n");
   printf("      -6                process only the IPv6 address ranges.\n\n");
   printf("   valid argument in usage forms 1+2:\n\n");
   printf("      -r bstfiles       Base path to the binary sorted tables (.v4 and .v6) with the consolidated IP ranges\n");
   printf("                        which were generated by the 'ipdb' tool [default: /usr/local/etc/ipdb/IPRanges/ipcc.bst].\n\n");
   printf("3) compute the encoded value of a country code (see -x flag above):\n\n");
   printf("   %s -q CC\n", r);
   printf("      -q CC             The country code to be encoded.\n\n");
}


CCNode **CCTable  = NULL;

static inline uint32_t ccv(uint16_t cc, int32_t toff)
{
   int64_t val = toff + cce(cc)*10;
   return (0 <= val && val <= 4294967295) ? (uint32_t)val : 0; // the result mut be a 32-bit unsigned value
}

int main(int argc, char *argv[])
{
   bool plainFlag = false,
        ccValFlag = false,
        only4Flag = false,
        only6Flag = false;

   int32_t  ch,
            rc    = 1,
            tnum  = 0,
            toff  = 0;
   uint32_t tval  = 0;

   char *ccList   = NULL,
        *bstfname = "/usr/local/etc/ipdb/IPRanges/ipcc.bst",   // actually 2 files *.v4 and *.v6
        *cmd      = argv[0],
        *lastopt  = "";

   while ((ch = getopt(argc, argv, "t:n:pv:x:46r:h:q:")) != -1)
   {
      switch (ch)
      {
         case 't':
            ccList = optarg;
            break;

         case 'n':
            tnum = (int32_t)strtol(optarg, NULL, 10);
            if (tnum < 0 || 65534 < tnum || tnum == 0 && errno == EINVAL)
            {
               lastopt = optarg;
               goto arg_err;
            }
            break;

         case 'p':
            plainFlag = true;
            break;

         case 'v':
            if (ccValFlag || (tval = (uint32_t)strtol(optarg, NULL, 10)) == 0 && errno == EINVAL)
            {
               lastopt = optarg;
               goto arg_err;
            }
            break;

         case 'x':
            if (tval || (toff = (int32_t)strtol(optarg, NULL, 10)) == 0 && errno == EINVAL)
            {
               lastopt = optarg;
               goto arg_err;
            }
            ccValFlag = true;
            break;

         case '4':
            if (only6Flag)
               goto arg_err;
            only4Flag = true;
            break;

         case '6':
            if (only4Flag)
               goto arg_err;
            only6Flag = true;
            break;

         case 'q':
            if (!optarg | strvlen(optarg) < 2)
            {
               lastopt = optarg;
               goto arg_err;
            }
            uppercase(optarg, 2);
            printf("%s encodes to %u\n", optarg, ccv(*(uint16_t *)optarg, 0));
            return 0;

         case 'r':
            bstfname = optarg;
            break;

         arg_err:
            printf("Incorrect argument:\n -%c %s, ...\n\n", ch, lastopt);
         default:
            rc = 1;
         case 'h':
            usage(cmd);
            return rc;
      }
   }

   argc -= optind;
   argv += optind;

   if (argc != 1 && !ccList)
   {
      printf("Wrong number of arguments:\n %s, ...\n\n", argv[0]);
      usage(cmd);
      return 1;
   }


   int    namelen = strvlen(bstfname);
   char  *inName  = strcpy(alloca(namelen+4), bstfname);
   FILE  *in;
   struct stat st;

   rc = 1;

//
// first usage form -- lookup the country code for a given IPv4 or IPv6 address
//
   if (ccList == NULL)
   {
      int      o;
      uint32_t ipv4;
      uint128t ipv6;
      if (ipv4 = ipv4_str2bin(argv[0]))
      {
         *(uint32_t *)&inName[namelen] = *(uint32_t *)".v4";
         if (stat(inName, &st) == noerr && st.st_size && (in = fopen(inName, "r")))
         {
            IP4Str ipstr_lo, ipstr_hi;
            IP4Set *sortedIP4Sets = allocate((ssize_t)st.st_size, false);
            if (sortedIP4Sets)
            {
               if (fread(sortedIP4Sets, (ssize_t)st.st_size, 1, in))
               {
                  if ((o = bisectionIP4Search(ipv4, sortedIP4Sets, (int)(st.st_size/sizeof(IP4Set)))) >= 0)
                     printf("%s in %s - %s in %s\n\n", argv[0], ipv4_bin2str(sortedIP4Sets[o][0], ipstr_lo), ipv4_bin2str(sortedIP4Sets[o][1], ipstr_hi), (char *)&sortedIP4Sets[o][2]);
                  else
                     printf("%s not found.\n\n", argv[0]);
                  rc = 0;
               }
               else
                  printf("IPv4 database file could not be loaded.\n\n");

               deallocate(VPR(sortedIP4Sets), false);
            }
            else
               printf("Not enough memory for loading the IPv4 database.\n\n");

            fclose(in);
         }
         else
            printf("IPv4 database file could not be found.\n\n");
      }

      else if (gt_u128(ipv6 = ipv6_str2bin(argv[0]), u64_to_u128t(0)))
      {
         *(uint32_t *)&inName[namelen] = *(uint32_t *)".v6";
         if (stat(inName, &st) == noerr && st.st_size && (in = fopen(inName, "r")))
         {
            IP6Str ipstr_lo, ipstr_hi;
            IP6Set *sortedIP6Sets = allocate((ssize_t)st.st_size, false);
            if (sortedIP6Sets)
            {
               if (fread(sortedIP6Sets, (ssize_t)st.st_size, 1, in))
               {
                  if ((o = bisectionIP6Search(ipv6, sortedIP6Sets, (int)(st.st_size/sizeof(IP6Set)))) >= 0)
                     printf("%s in %s - %s in %s\n\n", argv[0], ipv6_bin2str(sortedIP6Sets[o][0], ipstr_lo), ipv6_bin2str(sortedIP6Sets[o][1], ipstr_hi), (char *)&sortedIP6Sets[o][2]);
                  else
                     printf("%s not found.\n\n", argv[0]);
                  rc = 0;
               }
               else
                  printf("IPv6 database file could not be loaded.\n\n");

               deallocate(VPR(sortedIP6Sets), false);
            }
            else
               printf("Not enough memory for loading the IPv6 database.\n\n");

            fclose(in);
         }
         else
            printf("IPv6 database file could not be found.\n\n");
      }

      else
         printf("Invalid IP address.\n\n");
   }


//
// second usage form -- generate ipfw table construction directives
//
   else // (ccList != NULL)
   {
      if (CCTable = createCCTable())
      {
         int count = 0;
         char *ccui = ccList;
         while (*ccui)
         {
            int tl = taglen(ccui);
            if (ccui[tl] == ':')
               ccui[tl++] = '\0';
            storeCC(CCTable, ccui);
            ccui += tl;
         }

      //
      // IPv4 table generation
      //
         if (!only6Flag)
         {
            *(uint32_t *)&inName[namelen] = *(uint32_t *)".v4";
            if (stat(inName, &st) == noerr && st.st_size && (in = fopen(inName, "r")))
            {
               CCNode *ccn = NULL;
               IP4Str  ipstr;
               IP4Set *sortedIP4Sets = allocate((ssize_t)st.st_size, false);
               if (sortedIP4Sets)
               {
                  if (fread(sortedIP4Sets, (ssize_t)st.st_size, 1, in))
                  {
                     int i, n = (int)(st.st_size/sizeof(IP4Set));
                     for (i = 0; i < n; i++)
                     {
                        if (!*ccList || (ccn = findCC(CCTable, sortedIP4Sets[i][2])))
                        {
                           uint32_t ip = sortedIP4Sets[i][0];
                           uint32_t ui = (ccn) ? ccn->ui : 0;
                           int32_t  m;
                           do
                           {
                              m = intlb4_1p(sortedIP4Sets[i][1] - ip);
                              while (ip - (ip >> m << m))
                                 m--;

                              if (plainFlag)
                                 printf("%s/%d\n", ipv4_bin2str(ip, ipstr), 32 - m);
                              else if (ui != 0)
                                 printf("table %d add %s/%d %u\n", tnum, ipv4_bin2str(ip, ipstr), 32 - m, ui);
                              else if (tval != 0)
                                 printf("table %d add %s/%d %u\n", tnum, ipv4_bin2str(ip, ipstr), 32 - m, tval);
                              else if (ccValFlag)
                                 printf("table %d add %s/%d %u\n", tnum, ipv4_bin2str(ip, ipstr), 32 - m, ccv((uint16_t)sortedIP4Sets[i][2], toff));
                              else
                                 printf("table %d add %s/%d\n",    tnum, ipv4_bin2str(ip, ipstr), 32 - m);

                              count++;
                           }
                           while ((ip += (uint32_t)1<<m) < sortedIP4Sets[i][1]);
                        }
                     }

                     rc = 0;
                  }
                  else
                     printf("IPv4 database file could not be loaded.\n\n");

                  deallocate(VPR(sortedIP4Sets), false);
               }
               else
                  printf("Not enough memory for loading the IPv4 database.\n\n");

               fclose(in);
            }
            else
               printf("IPv4 database file could not be found.\n\n");
         }

      //
      // IPv6 table generation
      //
         if (!only4Flag)
         {
            *(uint32_t *)&inName[namelen] = *(uint32_t *)".v6";
            if (stat(inName, &st) == noerr && st.st_size && (in = fopen(inName, "r")))
            {
               CCNode *ccn = NULL;
               IP6Str  ipstr;
               IP6Set *sortedIP6Sets = allocate((ssize_t)st.st_size, false);
               if (sortedIP6Sets)
               {
                  if (fread(sortedIP6Sets, (ssize_t)st.st_size, 1, in))
                  {
                     int i, n = (int)(st.st_size/sizeof(IP6Set));
                     for (i = 0; i < n; i++)
                     {
                        if (!*ccList || (ccn = findCC(CCTable, *(uint32_t*)&sortedIP6Sets[i][2])))
                        {
                           uint128t ip = sortedIP6Sets[i][0];
                           uint32_t ui = (ccn) ? ccn->ui : 0;
                           int32_t  m;
                           do
                           {
                              m = intlb6_1p(sub_u128(sortedIP6Sets[i][1], ip));
                              while (gt_u128(sub_u128(ip, shl_u128(shr_u128(ip, m), m)), u64_to_u128t(0)))
                                 m--;

                              if (plainFlag)
                                 printf("%s/%d\n", ipv6_bin2str(ip, ipstr), 128 - m);
                              else if (ui != 0)
                                 printf("table %d add %s/%d %u\n", tnum, ipv6_bin2str(ip, ipstr), 128 - m, ui);
                              else if (tval != 0)
                                 printf("table %d add %s/%d %u\n", tnum, ipv6_bin2str(ip, ipstr), 128 - m, tval);
                              else if (ccValFlag)
                                 printf("table %d add %s/%d %u\n", tnum, ipv6_bin2str(ip, ipstr), 128 - m, ccv(*(uint16_t*)&sortedIP6Sets[i][2], toff));
                              else
                                 printf("table %d add %s/%d\n",    tnum, ipv6_bin2str(ip, ipstr), 128 - m);

                              count++;
                           }
                           while (lt_u128(ip = add_u128(ip, shl_u128(u64_to_u128t(1), m)), sortedIP6Sets[i][1]));
                        }
                     }

                     rc = 0;
                  }
                  else
                     printf("IPv6 database file could not be loaded.\n\n");

                  deallocate(VPR(sortedIP6Sets), false);
               }
               else
                  printf("Not enough memory for loading the IPv6 database.\n\n");

               fclose(in);
            }
            else
               printf("IPv6 database file could not be found.\n\n");
         }

         if (!count)
            printf("\n");

         releaseCCTable(CCTable);
      }
      else
         printf("Not enough memory.\n\n");
   }

   return rc;
}
