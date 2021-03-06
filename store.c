//  store.c
//  ipdb / ipup / geod
//
//  Created by Dr. Rolf Jansen on 2016-07-10.
//  Copyright (c) 2016 projectworld.net. All rights reserved.
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
#include <string.h>

#include "binutils.h"
#include "store.h"


#pragma mark ••• AVL Tree of IPv4-Ranges •••

static int balanceIP4Node(IP4Node **node)
{
   int   change = 0;
   IP4Node *o = *node;
   IP4Node *p, *q;

   if (o->B == -2)
   {
      if (p = o->L)                    // make the static analyzer happy
         if (p->B == +1)
         {
            change = 1;                // double left-right rotation
            q      = p->R;             // left rotation
            p->R   = q->L;
            q->L   = p;
            o->L   = q->R;             // right rotation
            q->R   = o;
            o->B   = +(q->B < 0);
            p->B   = -(q->B > 0);
            q->B   = 0;
            *node  = q;
         }

         else
         {
            change = p->B;             // single right rotation
            o->L   = p->R;
            p->R   = o;
            o->B   = -(++p->B);
            *node  = p;
         }
   }

   else if (o->B == +2)
   {
      if (q = o->R)                    // make the static analyzer happy
         if (q->B == -1)
         {
            change = 1;                // double right-left rotation
            p      = q->L;             // right rotation
            q->L   = p->R;
            p->R   = q;
            o->R   = p->L;             // left rotation
            p->L   = o;
            o->B   = -(p->B > 0);
            q->B   = +(p->B < 0);
            p->B   = 0;
            *node  = p;
         }

         else
         {
            change = q->B;             // single left rotation
            o->R   = q->L;
            q->L   = o;
            o->B   = -(--q->B);
            *node  = q;
         }
   }

   return change != 0;
}


static int pickPrevIP4Node(IP4Node **node, IP4Node **exch)
{                                             // *exch on entry = parent node
   IP4Node *o = *node;                        // *exch on exit  = picked previous value node

   if (o->R)
   {
      *exch = o;
      int change = -pickPrevIP4Node(&o->R, exch);
      if (change)
         if (abs(o->B += change) > 1)
            return balanceIP4Node(node);
         else
            return o->B == 0;
      else
         return 0;
   }

   else if (o->L)
   {
      IP4Node *p = o->L;
      o->L = NULL;
      (*exch)->R = p;
      *exch = o;
      return p->B == 0;
   }

   else
   {
      (*exch)->R = NULL;
      *exch = o;
      return 1;
   }
}


static int pickNextIP4Node(IP4Node **node, IP4Node **exch)
{                                             // *exch on entry = parent node
   IP4Node *o = *node;                        // *exch on exit  = picked next value node

   if (o->L)
   {
      *exch = o;
      int change = +pickNextIP4Node(&o->L, exch);
      if (change)
         if (abs(o->B += change) > 1)
            return balanceIP4Node(node);
         else
            return o->B == 0;
      else
         return 0;
   }

   else if (o->R)
   {
      IP4Node *q = o->R;
      o->R = NULL;
      (*exch)->L = q;
      *exch = o;
      return q->B == 0;
   }

   else
   {
      (*exch)->L = NULL;
      *exch = o;
      return 1;
   }
}


IP4Node *findIP4Node(uint32_t ip, IP4Node  *node)
{
   if (node)
   {
      if (node->lo <= ip && ip <= node->hi)
         return node;

      else if (ip < node->lo)
         return findIP4Node(ip, node->L);

      else // (ip > node->hi)
         return findIP4Node(ip, node->R);
   }
   else
      return NULL;
}


IP4Node *findNet4Node(uint32_t lo, uint32_t hi, uint32_t cc, IP4Node  *node)
{
   if (node)
   {
      int ofs = (cc == node->cc);

      if (node->lo <= lo && lo-ofs <= node->hi || node->lo <= hi+ofs && hi <= node->hi || lo <= node->lo && node->hi <= hi)
         return node;

      else if (lo < node->lo)
         return findNet4Node(lo, hi, cc, node->L);

      else // ([lo|hi] > node->hi)
         return findNet4Node(lo, hi, cc, node->R);
   }
   else
      return NULL;
}


int addIP4Node(uint32_t lo, uint32_t hi, uint32_t cc, IP4Node **node)
{
   IP4Node *o = *node;

   if (o != NULL)
   {
      int change;

      if (lo < o->lo)
         change = -addIP4Node(lo, hi, cc, &o->L);

      else if (lo > o->lo)
         change = +addIP4Node(lo, hi, cc, &o->R);

      else // (lo == o->lo)               // this case must not happen !!!
         return 0;

      if (change)
         if (abs(o->B += change) > 1)
            return 1 - balanceIP4Node(node);
         else
            return o->B != 0;
      else
         return 0;
   }

   else // (o == NULL)                    // if the IP4Node is not in the tree
   {                                      // then add it into a new leaf
      if (o = allocate(sizeof(IP4Node), true))
      {
         o->lo = lo;
         o->hi = hi;
         o->cc = cc;
         *node = o;                       // report back the new node
         return 1;                        // add the weight of 1 leaf onto the balance
      }

      return 0;                           // Out of Memory situation, nothing changed
   }
}


int removeIP4Node(uint32_t ip, IP4Node **node)
{
   IP4Node *o = *node;

   if (o != NULL)
   {
      int change;

      if (ip < o->lo)
         change = +removeIP4Node(ip, &o->L);

      else if (ip > o->lo)
         change = -removeIP4Node(ip, &o->R);

      else // (o->lo <= ip && ip <= o->lo)
      {
         int      b = o->B;
         IP4Node *p = o->L;
         IP4Node *q = o->R;

         if (!p || !q)
         {
            deallocate(VPR(*node), false);
            *node = (p > q) ? p : q;
            return 1;                     // remove the weight of 1 leaf from the balance
         }

         else
         {
            if (b == -1)
            {
               if (!p->R)
               {
                  change = +1;
                  o      =  p;
                  o->R   =  q;
               }
               else
               {
                  change = +pickPrevIP4Node(&p, &o);
                  o->L   =  p;
                  o->R   =  q;
               }
            }

            else
            {
               if (!q->L)
               {
                  change = -1;
                  o      =  q;
                  o->L   =  p;
               }
               else
               {
                  change = -pickNextIP4Node(&q, &o);
                  o->L   =  p;
                  o->R   =  q;
               }
            }

            o->B = b;
            deallocate(VPR(*node), false);
            *node = o;
         }
      }

      if (change)
         if (abs(o->B += change) > 1)
            return balanceIP4Node(node);
         else
            return o->B == 0;
      else
         return 0;
   }

   else // (o == NULL)
      return 0;                           // not found -> recursively do nothing
}


void serializeIP4Tree(FILE *out, IP4Node *node)
{
   if (node)
   {
      if (node->L)
         serializeIP4Tree(out, node->L);

      IP4Set set = {node->lo, node->hi, node->cc};
      fwrite(set, sizeof(IP4Set), 1, out);

      if (node->R)
         serializeIP4Tree(out, node->R);
   }
}


void releaseIP4Tree(IP4Node *node)
{
   if (node)
   {
      if (node->L)
         releaseIP4Tree(node->L);

      if (node->R)
         releaseIP4Tree(node->R);

      deallocate(VPR(node), false);
   }
}


#pragma mark ••• AVL Tree of IPv6-Ranges •••

static int balanceIP6Node(IP6Node **node)
{
   int   change = 0;
   IP6Node *o = *node;
   IP6Node *p, *q;

   if (o->B == -2)
   {
      if (p = o->L)                    // make the static analyzer happy
         if (p->B == +1)
         {
            change = 1;                // double left-right rotation
            q      = p->R;             // left rotation
            p->R   = q->L;
            q->L   = p;
            o->L   = q->R;             // right rotation
            q->R   = o;
            o->B   = +(q->B < 0);
            p->B   = -(q->B > 0);
            q->B   = 0;
            *node  = q;
         }

         else
         {
            change = p->B;             // single right rotation
            o->L   = p->R;
            p->R   = o;
            o->B   = -(++p->B);
            *node  = p;
         }
   }

   else if (o->B == +2)
   {
      if (q = o->R)                    // make the static analyzer happy
         if (q->B == -1)
         {
            change = 1;                // double right-left rotation
            p      = q->L;             // right rotation
            q->L   = p->R;
            p->R   = q;
            o->R   = p->L;             // left rotation
            p->L   = o;
            o->B   = -(p->B > 0);
            q->B   = +(p->B < 0);
            p->B   = 0;
            *node  = p;
         }

         else
         {
            change = q->B;             // single left rotation
            o->R   = q->L;
            q->L   = o;
            o->B   = -(--q->B);
            *node  = q;
         }
   }

   return change != 0;
}


static int pickPrevIP6Node(IP6Node **node, IP6Node **exch)
{                                             // *exch on entry = parent node
   IP6Node *o = *node;                        // *exch on exit  = picked previous value node

   if (o->R)
   {
      *exch = o;
      int change = -pickPrevIP6Node(&o->R, exch);
      if (change)
         if (abs(o->B += change) > 1)
            return balanceIP6Node(node);
         else
            return o->B == 0;
      else
         return 0;
   }

   else if (o->L)
   {
      IP6Node *p = o->L;
      o->L = NULL;
      (*exch)->R = p;
      *exch = o;
      return p->B == 0;
   }

   else
   {
      (*exch)->R = NULL;
      *exch = o;
      return 1;
   }
}


static int pickNextIP6Node(IP6Node **node, IP6Node **exch)
{                                             // *exch on entry = parent node
   IP6Node *o = *node;                        // *exch on exit  = picked next value node

   if (o->L)
   {
      *exch = o;
      int change = +pickNextIP6Node(&o->L, exch);
      if (change)
         if (abs(o->B += change) > 1)
            return balanceIP6Node(node);
         else
            return o->B == 0;
      else
         return 0;
   }

   else if (o->R)
   {
      IP6Node *q = o->R;
      o->R = NULL;
      (*exch)->L = q;
      *exch = o;
      return q->B == 0;
   }

   else
   {
      (*exch)->L = NULL;
      *exch = o;
      return 1;
   }
}


IP6Node *findIP6Node(uint128t ip, IP6Node  *node)
{
   if (node)
   {
      if (le_u128(node->lo, ip) && le_u128(ip, node->hi))
         return node;

      else if (lt_u128(ip, node->lo))
         return findIP6Node(ip, node->L);

      else // (gt_u128(ip, node->hi))
         return findIP6Node(ip, node->R);
   }
   else
      return NULL;
}


IP6Node *findNet6Node(uint128t lo, uint128t hi, uint32_t cc, IP6Node  *node)
{
   if (node)
   {
      uint128t ofs = u64_to_u128t(cc == node->cc);

      if (le_u128(node->lo, lo) && le_u128(sub_u128(lo,ofs), node->hi) || le_u128(node->lo, add_u128(hi,ofs)) && le_u128(hi, node->hi) || le_u128(lo, node->lo) && le_u128(node->hi, hi))
         return node;

      else if (lt_u128(lo, node->lo))
         return findNet6Node(lo, hi, cc, node->L);

      else // (gt_u128([lo|hi], node->hi))
         return findNet6Node(lo, hi, cc, node->R);
   }
   else
      return NULL;
}


int addIP6Node(uint128t lo, uint128t hi, uint32_t cc, IP6Node **node)
{
   IP6Node *o = *node;

   if (o != NULL)
   {
      int change;

      if (lt_u128(lo, o->lo))
         change = -addIP6Node(lo, hi, cc, &o->L);

      else if (gt_u128(lo, o->lo))
         change = +addIP6Node(lo, hi, cc, &o->R);

      else // (eq_u128(lo, o->lo))               // this case must not happen !!!
         return 0;

      if (change)
         if (abs(o->B += change) > 1)
            return 1 - balanceIP6Node(node);
         else
            return o->B != 0;
      else
         return 0;
   }

   else // (o == NULL)                    // if the IP6Node is not in the tree
   {                                      // then add it into a new leaf
      if (o = allocate(sizeof(IP6Node), true))
      {
         o->lo = lo;
         o->hi = hi;
         o->cc = cc;
         *node = o;                       // report back the new node
         return 1;                        // add the weight of 1 leaf onto the balance
      }

      return 0;                           // Out of Memory situation, nothing changed
   }
}


int removeIP6Node(uint128t ip, IP6Node **node)
{
   IP6Node *o = *node;

   if (o != NULL)
   {
      int change;

      if (lt_u128(ip, o->lo))
         change = +removeIP6Node(ip, &o->L);

      else if (gt_u128(ip, o->lo))
         change = -removeIP6Node(ip, &o->R);

      else // (le_u128(o->lo, ip) && le_u128(ip, o->lo))
      {
         int      b = o->B;
         IP6Node *p = o->L;
         IP6Node *q = o->R;

         if (!p || !q)
         {
            deallocate(VPR(*node), false);
            *node = (p > q) ? p : q;
            return 1;                     // remove the weight of 1 leaf from the balance
         }

         else
         {
            if (b == -1)
            {
               if (!p->R)
               {
                  change = +1;
                  o      =  p;
                  o->R   =  q;
               }
               else
               {
                  change = +pickPrevIP6Node(&p, &o);
                  o->L   =  p;
                  o->R   =  q;
               }
            }

            else
            {
               if (!q->L)
               {
                  change = -1;
                  o      =  q;
                  o->L   =  p;
               }
               else
               {
                  change = -pickNextIP6Node(&q, &o);
                  o->L   =  p;
                  o->R   =  q;
               }
            }

            o->B = b;
            deallocate(VPR(*node), false);
            *node = o;
         }
      }

      if (change)
         if (abs(o->B += change) > 1)
            return balanceIP6Node(node);
         else
            return o->B == 0;
      else
         return 0;
   }

   else // (o == NULL)
      return 0;                           // not found -> recursively do nothing
}


void serializeIP6Tree(FILE *out, IP6Node *node)
{
   if (node)
   {
      if (node->L)
         serializeIP6Tree(out, node->L);

      IP6Set set = {node->lo, node->hi, node->cc};
      fwrite(set, sizeof(IP6Set), 1, out);

      if (node->R)
         serializeIP6Tree(out, node->R);
   }
}


void releaseIP6Tree(IP6Node *node)
{
   if (node)
   {
      if (node->L)
         releaseIP6Tree(node->L);

      if (node->R)
         releaseIP6Tree(node->R);

      deallocate(VPR(node), false);
   }
}


#pragma mark ••• AVL Tree of Country Codes •••

static int balanceCCNode(CCNode **node)
{
   int   change = 0;
   CCNode *o = *node;
   CCNode *p, *q;

   if (o->B == -2)
   {
      if (p = o->L)                    // make the static analyzer happy
         if (p->B == +1)
         {
            change = 1;                // double left-right rotation
            q      = p->R;             // left rotation
            p->R   = q->L;
            q->L   = p;
            o->L   = q->R;             // right rotation
            q->R   = o;
            o->B   = +(q->B < 0);
            p->B   = -(q->B > 0);
            q->B   = 0;
            *node  = q;
         }

         else
         {
            change = p->B;             // single right rotation
            o->L   = p->R;
            p->R   = o;
            o->B   = -(++p->B);
            *node  = p;
         }
   }

   else if (o->B == +2)
   {
      if (q = o->R)                    // make the static analyzer happy
         if (q->B == -1)
         {
            change = 1;                // double right-left rotation
            p      = q->L;             // right rotation
            q->L   = p->R;
            p->R   = q;
            o->R   = p->L;             // left rotation
            p->L   = o;
            o->B   = -(p->B > 0);
            q->B   = +(p->B < 0);
            p->B   = 0;
            *node  = p;
         }

         else
         {
            change = q->B;             // single left rotation
            o->R   = q->L;
            q->L   = o;
            o->B   = -(--q->B);
            *node  = q;
         }
   }

   return change != 0;
}


static int pickPrevCCNode(CCNode **node, CCNode **exch)
{                                             // *exch on entry = parent node
   CCNode *o = *node;                         // *exch on exit  = picked previous value node

   if (o->R)
   {
      *exch = o;
      int change = -pickPrevCCNode(&o->R, exch);
      if (change)
         if (abs(o->B += change) > 1)
            return balanceCCNode(node);
         else
            return o->B == 0;
      else
         return 0;
   }

   else if (o->L)
   {
      CCNode *p = o->L;
      o->L = NULL;
      (*exch)->R = p;
      *exch = o;
      return p->B == 0;
   }

   else
   {
      (*exch)->R = NULL;
      *exch = o;
      return 1;
   }
}


static int pickNextCCNode(CCNode **node, CCNode **exch)
{                                             // *exch on entry = parent node
   CCNode *o = *node;                         // *exch on exit  = picked next value node

   if (o->L)
   {
      *exch = o;
      int change = +pickNextCCNode(&o->L, exch);
      if (change)
         if (abs(o->B += change) > 1)
            return balanceCCNode(node);
         else
            return o->B == 0;
      else
         return 0;
   }

   else if (o->R)
   {
      CCNode *q = o->R;
      o->R = NULL;
      (*exch)->L = q;
      *exch = o;
      return q->B == 0;
   }

   else
   {
      (*exch)->L = NULL;
      *exch = o;
      return 1;
   }
}


CCNode *findCCNode(uint32_t cc, CCNode *node)
{
   if (node)
   {
      if (cc < node->cc)
         return findCCNode(cc, node->L);

      else if (cc > node->cc)
         return findCCNode(cc, node->R);

      else // (cc == node->cc)
         return node;
   }
   else
      return NULL;
}


int addCCNode(uint32_t cc, uint32_t ui, CCNode **node)
{
   CCNode *o = *node;

   if (o != NULL)
   {
      int change;

      if (cc < o->cc)
         change = -addCCNode(cc, ui, &o->L);

      else if (cc > o->cc)
         change = +addCCNode(cc, ui, &o->R);

      else // (cc == o->cc)               // already in the list, do nothing
         return 0;

      if (change)
         if (abs(o->B += change) > 1)
            return 1 - balanceCCNode(node);
         else
            return o->B != 0;
      else
         return 0;
   }

   else // (o == NULL)                    // if the CCNode is not in the tree
   {                                      // then add it into a new leaf
      if (o = allocate(sizeof(CCNode), true))
      {
         o->cc = cc;
         o->ui = ui;
         *node = o;                       // report back the new node
         return 1;                        // add the weight of 1 leaf onto the balance
      }

      return 0;                           // Out of Memory situation, nothing changed
   }
}


int removeCCNode(uint32_t cc, CCNode **node)
{
   CCNode *o = *node;

   if (o != NULL)
   {
      int change;

      if (cc < o->cc)
         change = +removeCCNode(cc, &o->L);

      else if (cc > o->cc)
         change = -removeCCNode(cc, &o->R);

      else // (cc == o->cc)
      {
         int     b = o->B;
         CCNode *p = o->L;
         CCNode *q = o->R;

         if (!p || !q)
         {
            deallocate(VPR(*node), false);
            *node = (p > q) ? p : q;
            return 1;                     // remove the weight of 1 leaf from the balance
         }

         else
         {
            if (b == -1)
            {
               if (!p->R)
               {
                  change = +1;
                  o      =  p;
                  o->R   =  q;
               }
               else
               {
                  change = +pickPrevCCNode(&p, &o);
                  o->L   =  p;
                  o->R   =  q;
               }
            }

            else
            {
               if (!q->L)
               {
                  change = -1;
                  o      =  q;
                  o->L   =  p;
               }
               else
               {
                  change = -pickNextCCNode(&q, &o);
                  o->L   =  p;
                  o->R   =  q;
               }
            }

            o->B = b;
            deallocate(VPR(*node), false);
            *node = o;
         }
      }

      if (change)
         if (abs(o->B += change) > 1)
            return balanceCCNode(node);
         else
            return o->B == 0;
      else
         return 0;
   }

   else // (o == NULL)
      return 0;                           // not found -> recursively do nothing
}


void releaseCCTree(CCNode *node)
{
   if (node)
   {
      if (node->L)
         releaseCCTree(node->L);

      if (node->R)
         releaseCCTree(node->R);

      deallocate(VPR(node), false);
   }
}


#pragma mark ••• Pseudo Hash Table of Country Codes •••


// Table creation and release
CCNode **createCCTable(void)
{
   return allocate(ccTableSize*sizeof(CCNode *), true);
}

void releaseCCTable(CCNode *table[])
{
   if (table)
   {
      for (uint32_t i = 0; i < ccTableSize; i++)
         releaseCCTree(table[i]);
      deallocate(VPR(table), false);
   }
}


// finding/storing/removing country codes

static inline uint32_t cci(uint32_t cc)
{
   return (cc = cce((uint16_t)cc) < ccTableSize) ? cc : 0;  // the resulting index MUST FIT into the hash table
}

CCNode *findCC(CCNode *table[], uint32_t cc)
{
   return findCCNode(cc, table[cci(cc)]);
}

void storeCC(CCNode *table[], char *ccui)
{
   int len = strvlen(ccui = trim(ccui));
   if (len >= 2)
   {
      uint16_t cc;
      int64_t  ui = 0;
      cc = *(uint16_t *)uppercase(ccui, 2);
      if (len > 2)
      {
         for (ccui += 2; *ccui && *ccui != '='; ccui++);
         if (*ccui)
            ui = strtol(ccui+1, NULL, 10);
      }

      addCCNode(cc, (0 < ui && ui < 4294967295) ? (uint32_t)ui : 0, &table[cci(cc)]);
   }
}

void removeCC(CCNode *table[], uint32_t cc)
{
   CCNode *node;
   uint32_t idx = cci(cc);
   if (node = table[idx])
      if (!node->L && !node->R)
         deallocate(VPR(table[idx]), false);
      else
         removeCCNode(cc, &table[idx]);
}
