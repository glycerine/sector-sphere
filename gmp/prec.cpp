/*****************************************************************************
Copyright (c) 2005 - 2010, The Board of Trustees of the University of Illinois.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

* Redistributions of source code must retain the above
  copyright notice, this list of conditions and the
  following disclaimer.

* Redistributions in binary form must reproduce the
  above copyright notice, this list of conditions
  and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of the University of Illinois
  nor the names of its contributors may be used to
  endorse or promote products derived from this
  software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*****************************************************************************/

/*****************************************************************************
written by
   Yunhong Gu, last updated 02/10/2010
*****************************************************************************/

#ifndef WIN32
   #include <sys/time.h>
   #include <time.h>
#else
   #include <windows.h>
#endif

#include <common.h>
#include <dhash.h>
#include <prec.h>

using namespace std;

CPeerRecord::CPeerRecord():
m_strIP(),
m_iPort(0),
m_iSession(0),
m_iID(0),
m_llTimeStamp(0),
m_iRTT(0),
m_iFlowWindow(0),
m_UDTSocket(UDT::INVALID_SOCK)
{
}

CPeerManagement::CPeerManagement()
{
   CGuard::createMutex(m_PeerRecLock);
}

CPeerManagement::~CPeerManagement()
{
   clearPR();
   CGuard::releaseMutex(m_PeerRecLock);
}

void CPeerManagement::insert(const string& ip, const int& port, const int& session, const int32_t& id, const int& rtt, const int& fw)
{
   CGuard recguard(m_PeerRecLock);

   if (rtt > 0)
   {
      map<string, int>::iterator t = m_mRTT.find(ip);
      if (t != m_mRTT.end())
         t->second = (t->second * 7 + rtt) >> 3;
      else
         m_mRTT[ip] = rtt;
   }

   CPeerRecord* pr = new CPeerRecord;
   pr->m_strIP = ip;
   pr->m_iPort = port;
   pr->m_iSession = session;
   pr->m_iID = id;
   pr->m_llTimeStamp = CTimer::getTime();
   pr->m_iFlowWindow = fw;

   //insert the message record to the recent records list, so to avoid repeated messages
   addRecentPR(*pr);

   set<CPeerRecord*, CFPeerRec>::iterator i = m_sPeerRec.find(pr);
   map<string, int>::iterator t = m_mRTT.find(ip);

   if (i != m_sPeerRec.end())
   {
      if (id > (*i)->m_iID)
         (*i)->m_iID = id;
      (*i)->m_iFlowWindow = fw;

      // adjust last updated time
      m_sPeerRecByTS.erase(*i);
      (*i)->m_llTimeStamp = CTimer::getTime();
      m_sPeerRecByTS.insert(*i);

      delete pr;
   }
   else
   {
      if (id > 0)
         pr->m_iID = id;
      else
         pr->m_iID = -1;

      m_sPeerRec.insert(pr);
      m_sPeerRecByTS.insert(pr);

      if (m_sPeerRecByTS.size() > m_uiRecLimit)
      {
         // delete oldest record
         set<CPeerRecord*, CFPeerRecByTS>::iterator j = m_sPeerRecByTS.begin();

         CPeerRecord* t = *j;

         // close the UDT connection if necessary
         if (t->m_UDTSocket != UDT::INVALID_SOCK)
            UDT::close(t->m_UDTSocket);

         m_sPeerRec.erase(t);
         m_sPeerRecByTS.erase(j);

         // BUG: when set reaches limit, this cause busy CPU loop
         /*
         bool delip = true;
         for (set<CPeerRecord*, CFPeerRec>::iterator k = m_sPeerRec.begin(); k != m_sPeerRec.end(); ++ k)
         {
            if ((*k)->m_strIP == t->m_strIP)
            {
               delip = false;
               break;
            }
         }

         if (delip)
         */

         m_mRTT.erase(t->m_strIP);

         delete t;
      }
   }
}

int CPeerManagement::getRTT(const string& ip)
{
   CGuard recguard(m_PeerRecLock);

   map<string, int>::iterator t = m_mRTT.find(ip);
   if (t != m_mRTT.end())
      return t->second;

   return -1;
}

void CPeerManagement::clearRTT(const string& ip)
{
   CGuard recguard(m_PeerRecLock);
   m_mRTT.erase(ip);
}

int CPeerManagement::flowControl(const string& ip, const int& port, const int& session)
{
   CPeerRecord pr;
   pr.m_strIP = ip;
   pr.m_iPort = port;
   pr.m_iSession = session;

   CGuard recguard(m_PeerRecLock);

   set<CPeerRecord*, CFPeerRec>::iterator i = m_sPeerRec.find(&pr);
   if (i == m_sPeerRec.end())
      return 0;

   int thresh = (*i)->m_iFlowWindow - int((CTimer::getTime() - (*i)->m_llTimeStamp) / 1000);

   if (thresh > 100)
   {
      #ifndef WIN32
         usleep(100000);
      #else
         Sleep(1);
      #endif
      return 100000;
   }

   if (thresh > 10)
   {
      #ifndef WIN32
         usleep(100000);
      #else
         Sleep(1);
      #endif
      return 10000;
   }

   return 0;
}

int32_t CPeerManagement::hash(const string& ip, const int& port, const int& session, const int32_t& id)
{
   char tmp[1024];
   sprintf(tmp, "%s%d%d%d", ip.c_str(), port, session, id);

   return DHash::hash(tmp, m_uiHashSpace);
}

int CPeerManagement::addRecentPR(const CPeerRecord& pr)
{
   int key = hash(pr.m_strIP, pr.m_iPort, pr.m_iSession, pr.m_iID);
   map<int, list<CPeerRecord> >::iterator i = m_mRecentRec.find(key);
   if (i == m_mRecentRec.end())
   {
      m_mRecentRec[key].clear();
      m_mRecentRec[key].push_back(pr);
   }
   else
   {
      int64_t ts = CTimer::getTime();
      while (!i->second.empty())
      {
         CPeerRecord& p = i->second.front();
         if (ts - p.m_llTimeStamp < 10 * 1000000)
            break;
         i->second.pop_front();
      }
      i->second.push_back(pr);
   }

   return 0;
}

void CPeerManagement::clearPR()
{
   CGuard recguard(m_PeerRecLock);

   for (map<int, list<CPeerRecord> >::iterator i = m_mRecentRec.begin(); i != m_mRecentRec.end(); ++ i)
   {
      i->second.clear();
   }
   m_mRecentRec.clear();

   for (set<CPeerRecord*, CFPeerRecByTS>::iterator i = m_sPeerRecByTS.begin(); i !=  m_sPeerRecByTS.end(); ++ i)
   {
      delete *i;
   }
   m_sPeerRecByTS.clear();

   m_sPeerRec.clear();
}

bool CPeerManagement::hit(const string& ip, const int& port, const int& session, const int32_t& id)
{
   CGuard recguard(m_PeerRecLock);

   int key = hash(ip, port, session, id);

   map<int, list<CPeerRecord> >::iterator i = m_mRecentRec.find(key);
   if (i != m_mRecentRec.end())
   {
      for (list<CPeerRecord>::iterator pr = i->second.begin(); pr != i->second.end(); ++ pr)
      {
         if ((ip == pr->m_strIP) && (port == pr->m_iPort) && (session == pr->m_iSession) && (id == pr->m_iID))
            return true;
      }
   }

   return false;
}

int CPeerManagement::setUDTSocket(const std::string& ip, const int& port, const UDTSOCKET& usock)
{
   CGuard recguard(m_PeerRecLock);

   CPeerRecord pr;
   pr.m_strIP = ip;
   pr.m_iPort = port;

   set<CPeerRecord*, CFPeerRec>::iterator i = m_sPeerRec.find(&pr);

   if (i != m_sPeerRec.end())
   {
      (*i)->m_UDTSocket = usock;
   }
   else
   {
      pr.m_llTimeStamp = CTimer::getTime();
      pr.m_UDTSocket = usock;

      m_sPeerRec.insert(&pr);
      m_sPeerRecByTS.insert(&pr);
   }

   return 0;
}

int CPeerManagement::getUDTSocket(const std::string& ip, const int& port, UDTSOCKET& usock)
{
   CGuard recguard(m_PeerRecLock);

   CPeerRecord pr;
   pr.m_strIP = ip;
   pr.m_iPort = port;

   set<CPeerRecord*, CFPeerRec>::iterator i = m_sPeerRec.find(&pr);

   if (i != m_sPeerRec.end())
   {
      usock = (*i)->m_UDTSocket;
      if (usock == UDT::INVALID_SOCK)
         return -1;

      // check current state; maybe the peer has closed this connection
      if (UDT::getsockstate(usock) != CONNECTED)
      {
         (*i)->m_UDTSocket = usock = UDT::INVALID_SOCK;
         return -1;
      }

      return 0;
   }

   return -1;
}
