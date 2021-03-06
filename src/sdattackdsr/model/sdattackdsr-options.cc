/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2011 Yufei Cheng
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Yufei Cheng   <yfcheng@ittc.ku.edu>
 *
 * James P.G. Sterbenz <jpgs@ittc.ku.edu>, director
 * ResiliNets Research Group  http://wiki.ittc.ku.edu/resilinets
 * Information and Telecommunication Technology Center (ITTC)
 * and Department of Electrical Engineering and Computer Science
 * The University of Kansas Lawrence, KS USA.
 *
 * Work supported in part by NSF FIND (Future Internet Design) Program
 * under grant CNS-0626918 (Postmodern Internet Architecture),
 * NSF grant CNS-1050226 (Multilayer Network Resilience Analysis and Experimentation on GENI),
 * US Department of Defense (DoD), and ITTC at The University of Kansas.
 */

#define NS_LOG_APPEND_CONTEXT                                   \
  if (GetObject<Node> ()) { std::clog << "[node " << GetObject<Node> ()->GetId () << "] "; }

#include <list>
#include <ctime>
#include <map>
#include "ns3/ptr.h"
#include "ns3/log.h"
#include "ns3/assert.h"
#include "ns3/fatal-error.h"
#include "ns3/node.h"
#include "ns3/uinteger.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/udp-header.h"
#include "ns3/pointer.h"
#include "ns3/node-list.h"
#include "ns3/object-vector.h"
#include "ns3/ipv4-l3-protocol.h"
#include "ns3/ipv4-interface.h"
#include "ns3/ipv4-header.h"
#include "ns3/ipv4-address.h"
#include "ns3/ipv4-route.h"
#include "ns3/icmpv4-l4-protocol.h"
#include "ns3/ip-l4-protocol.h"
#include "sdattackdsr-option-header.h"
#include "sdattackdsr-options.h"
#include "sdattackdsr-rcache.h"

u_int8_t ackcount;

#define fname "sdroute.txt"

using namespace std;
std::ofstream outputfile(fname);

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("SdattackdsrOptions");

namespace sdattackdsr {

NS_OBJECT_ENSURE_REGISTERED (SdattackdsrOptions);

TypeId SdattackdsrOptions::GetTypeId ()
{
  static TypeId tid = TypeId ("ns3::sdattackdsr::SdattackdsrOptions")
    .SetParent<Object> ()
    .SetGroupName ("Sdattackdsr")
    .AddAttribute ("OptionNumber", "The Sdattackdsr option number.",
                   UintegerValue (0),
                   MakeUintegerAccessor (&SdattackdsrOptions::GetOptionNumber),
                   MakeUintegerChecker<uint8_t> ())
    .AddTraceSource ("Drop",
                     "Packet dropped.",
                     MakeTraceSourceAccessor (&SdattackdsrOptions::m_dropTrace),
                     "ns3::Packet::TracedCallback")
    .AddTraceSource ("Rx",
                     "Receive DSR packet.",
                     MakeTraceSourceAccessor (&SdattackdsrOptions::m_rxPacketTrace),
                     "ns3::sdattackdsr::SdattackdsrOptionSRHeader::TracedCallback")
  ;
  return tid;
}

SdattackdsrOptions::SdattackdsrOptions ()
{
  NS_LOG_FUNCTION_NOARGS ();
}

SdattackdsrOptions::~SdattackdsrOptions ()
{
  NS_LOG_FUNCTION_NOARGS ();
}

void SdattackdsrOptions::SetNode (Ptr<Node> node)
{
  NS_LOG_FUNCTION (this << node);
  m_node = node;
}

Ptr<Node> SdattackdsrOptions::GetNode () const
{
  NS_LOG_FUNCTION_NOARGS ();
  return m_node;
}

bool SdattackdsrOptions::ContainAddressAfter (Ipv4Address ipv4Address, Ipv4Address destAddress, std::vector<Ipv4Address> &nodeList)
{
  NS_LOG_FUNCTION (this << ipv4Address << destAddress);
  std::vector<Ipv4Address>::iterator it = find (nodeList.begin (), nodeList.end (), destAddress);

  for (std::vector<Ipv4Address>::iterator i = it; i != nodeList.end (); ++i)
    {
      if ((ipv4Address == (*i)) && ((*i) != nodeList.back ()))
        {
          return true;
        }
    }
  return false;
}

std::vector<Ipv4Address>
SdattackdsrOptions::CutRoute (Ipv4Address ipv4Address, std::vector<Ipv4Address> &nodeList)
{
  NS_LOG_FUNCTION (this << ipv4Address);
  std::vector<Ipv4Address>::iterator it = find (nodeList.begin (), nodeList.end (), ipv4Address);
  std::vector<Ipv4Address> cutRoute;
  for (std::vector<Ipv4Address>::iterator i = it; i != nodeList.end (); ++i)
    {
      cutRoute.push_back (*i);
    }
  return cutRoute;
}

Ptr<Ipv4Route> SdattackdsrOptions::SetRoute (Ipv4Address nextHop, Ipv4Address srcAddress)
{
  NS_LOG_FUNCTION (this << nextHop << srcAddress);
  m_ipv4Route = Create<Ipv4Route> ();
  m_ipv4Route->SetDestination (nextHop);
  m_ipv4Route->SetGateway (nextHop);
  m_ipv4Route->SetSource (srcAddress);
  return m_ipv4Route;
}

bool SdattackdsrOptions::ReverseRoutes (std::vector<Ipv4Address> & vec)
{
  NS_LOG_FUNCTION (this);
  std::vector<Ipv4Address> vec2 (vec);
  vec.clear ();    // To ensure vec is empty before start
  for (std::vector<Ipv4Address>::reverse_iterator ri = vec2.rbegin (); ri
       != vec2.rend (); ++ri)
    {
      vec.push_back (*ri);
    }

  if ((vec.size () == vec2.size ()) && (vec.front () == vec2.back ()))
    {
      return true;
    }
  return false;
}

Ipv4Address SdattackdsrOptions::SearchNextHop (Ipv4Address ipv4Address, std::vector<Ipv4Address>& vec)
{
  NS_LOG_FUNCTION (this << ipv4Address);
  Ipv4Address nextHop;
  NS_LOG_DEBUG ("the vector size " << vec.size ());
  if (vec.size () == 2)
    {
      NS_LOG_DEBUG ("The two nodes are neighbors");
      nextHop = vec[1];
      return nextHop;
    }
  else
    {
      if (ipv4Address == vec.back ())
        {
          NS_LOG_DEBUG ("We have reached to the final destination " << ipv4Address << " " << vec.back ());
          return ipv4Address;
        }
      for (std::vector<Ipv4Address>::const_iterator i = vec.begin (); i != vec.end (); ++i)
        {
          if (ipv4Address == (*i))
            {
              nextHop = *(++i);
              return nextHop;
            }
        }
    }
  NS_LOG_DEBUG ("next hop address not found, route corrupted");
  Ipv4Address none = "0.0.0.0";
  return none;
}

Ipv4Address SdattackdsrOptions::ReverseSearchNextHop (Ipv4Address ipv4Address, std::vector<Ipv4Address>& vec)
{
  NS_LOG_FUNCTION (this << ipv4Address);
  Ipv4Address nextHop;
  if (vec.size () == 2)
    {
      NS_LOG_DEBUG ("The two nodes are neighbors");
      nextHop = vec[0];
      return nextHop;
    }
  else
    {
      for (std::vector<Ipv4Address>::reverse_iterator ri = vec.rbegin (); ri != vec.rend (); ++ri)
        {
          if (ipv4Address == (*ri))
            {
              nextHop = *(++ri);
              return nextHop;
            }
        }
    }
  NS_LOG_DEBUG ("next hop address not found, route corrupted");
  Ipv4Address none = "0.0.0.0";
  return none;
}

Ipv4Address SdattackdsrOptions::ReverseSearchNextTwoHop  (Ipv4Address ipv4Address, std::vector<Ipv4Address>& vec)
{
  NS_LOG_FUNCTION (this << ipv4Address);
  Ipv4Address nextTwoHop;
  NS_LOG_DEBUG ("The vector size " << vec.size ());
  NS_ASSERT (vec.size () > 2);
  for (std::vector<Ipv4Address>::reverse_iterator ri = vec.rbegin (); ri != vec.rend (); ++ri)
    {
      if (ipv4Address == (*ri))
        {
          nextTwoHop = *(ri + 2);
          return nextTwoHop;
        }
    }
  NS_FATAL_ERROR ("next hop address not found, route corrupted");
  Ipv4Address none = "0.0.0.0";
  return none;
}

void SdattackdsrOptions::PrintVector (std::vector<Ipv4Address>& vec)
{
  NS_LOG_FUNCTION (this);
  /*
   * Check elements in a route vector
   */
  if (!vec.size ())
    {
      NS_LOG_DEBUG ("The vector is empty");
    }
  else
    {
      NS_LOG_DEBUG ("Print all the elements in a vector");
      for (std::vector<Ipv4Address>::const_iterator i = vec.begin (); i != vec.end (); ++i)
        {
          NS_LOG_DEBUG ("The ip address " << *i);
        }
    }
}

bool SdattackdsrOptions::IfDuplicates (std::vector<Ipv4Address>& vec, std::vector<Ipv4Address>& vec2)
{
  NS_LOG_FUNCTION (this);
  for (std::vector<Ipv4Address>::const_iterator i = vec.begin (); i != vec.end (); ++i)
    {
      for (std::vector<Ipv4Address>::const_iterator j = vec2.begin (); j != vec2.end (); ++j)
        {
          if ((*i) == (*j))
            {
              return true;
            }
          else
            {
              continue;
            }
        }
    }
  return false;
}

bool SdattackdsrOptions::CheckDuplicates (Ipv4Address ipv4Address, std::vector<Ipv4Address>& vec)
{
  NS_LOG_FUNCTION (this << ipv4Address);
  for (std::vector<Ipv4Address>::const_iterator i = vec.begin (); i != vec.end (); ++i)
    {
      if ((*i) == ipv4Address)
        {
          return true;
        }
      else
        {
          continue;
        }
    }
  return false;
}

void SdattackdsrOptions::RemoveDuplicates (std::vector<Ipv4Address>& vec)
{
  NS_LOG_FUNCTION (this);
  //Remove duplicate ip address from the route if any, should not happen with normal behavior nodes
  std::vector<Ipv4Address> vec2 (vec); // declare vec2 as a copy of the vec
  PrintVector (vec2); // Print all the ip address in the route
  vec.clear (); // clear vec
  for (std::vector<Ipv4Address>::const_iterator i = vec2.begin (); i != vec2.end (); ++i)
    {
      if (vec.empty ())
        {
          vec.push_back (*i);
          continue;
        }
      else
        {
          for (std::vector<Ipv4Address>::iterator j = vec.begin (); j != vec.end (); ++j)
            {
              if ((*i) == (*j))
                {
                  if ((j + 1) != vec.end ())
                    {
                      vec.erase (j + 1, vec.end ());   // Automatic shorten the route
                      break;
                    }
                  else
                    {
                      break;
                    }
                }
              else if (j == (vec.end () - 1))
                {
                  vec.push_back (*i);
                  break;
                }
              else
                {
                  continue;
                }
            }
        }
    }
}

uint32_t
SdattackdsrOptions::GetIDfromIP (Ipv4Address address)
{
  NS_LOG_FUNCTION (this << address);
  int32_t nNodes = NodeList::GetNNodes ();
  for (int32_t i = 0; i < nNodes; ++i)
    {
      Ptr<Node> node = NodeList::GetNode (i);
      Ptr<Ipv4> ipv4 = node->GetObject<Ipv4> ();
      if (ipv4->GetAddress (1, 0).GetLocal () == address)
        {
          return i;
        }
    }
  return 255;
}

Ptr<Node> SdattackdsrOptions::GetNodeWithAddress (Ipv4Address ipv4Address)
{
  NS_LOG_FUNCTION (this << ipv4Address);
  int32_t nNodes = NodeList::GetNNodes ();
  for (int32_t i = 0; i < nNodes; ++i)
    {
      Ptr<Node> node = NodeList::GetNode (i);
      Ptr<Ipv4> ipv4 = node->GetObject<Ipv4> ();
      int32_t ifIndex = ipv4->GetInterfaceForAddress (ipv4Address);
      if (ifIndex != -1)
        {
          return node;
        }
    }
  return 0;
}

NS_OBJECT_ENSURE_REGISTERED (SdattackdsrOptionPad1);

TypeId SdattackdsrOptionPad1::GetTypeId ()
{
  static TypeId tid = TypeId ("ns3::sdattackdsr::SdattackdsrOptionPad1")
    .SetParent<SdattackdsrOptions> ()
    .SetGroupName ("Sdattackdsr")
    .AddConstructor<SdattackdsrOptionPad1> ()
  ;
  return tid;
}

SdattackdsrOptionPad1::SdattackdsrOptionPad1 ()
{
  NS_LOG_FUNCTION_NOARGS ();
}

SdattackdsrOptionPad1::~SdattackdsrOptionPad1 ()
{
  NS_LOG_FUNCTION_NOARGS ();
}

uint8_t SdattackdsrOptionPad1::GetOptionNumber () const
{
  NS_LOG_FUNCTION_NOARGS ();

  return OPT_NUMBER;
}

uint8_t SdattackdsrOptionPad1::Process (Ptr<Packet> packet, Ptr<Packet> sdattackdsrP, Ipv4Address ipv4Address, Ipv4Address source, Ipv4Header const& ipv4Header, uint8_t protocol, bool& isPromisc, Ipv4Address promiscSource)
{
  NS_LOG_FUNCTION (this << packet << sdattackdsrP << ipv4Address << source << ipv4Header << (uint32_t)protocol << isPromisc);
  Ptr<Packet> p = packet->Copy ();
  SdattackdsrOptionPad1Header pad1Header;
  p->RemoveHeader (pad1Header);

  isPromisc = false;

  return pad1Header.GetSerializedSize ();
}

NS_OBJECT_ENSURE_REGISTERED (SdattackdsrOptionPadn);

TypeId SdattackdsrOptionPadn::GetTypeId ()
{
  static TypeId tid = TypeId ("ns3::sdattackdsr::SdattackdsrOptionPadn")
    .SetParent<SdattackdsrOptions> ()
    .SetGroupName ("Sdattackdsr")
    .AddConstructor<SdattackdsrOptionPadn> ()
  ;
  return tid;
}

SdattackdsrOptionPadn::SdattackdsrOptionPadn ()
{
  NS_LOG_FUNCTION_NOARGS ();
}

SdattackdsrOptionPadn::~SdattackdsrOptionPadn ()
{
  NS_LOG_FUNCTION_NOARGS ();
}

uint8_t SdattackdsrOptionPadn::GetOptionNumber () const
{
  NS_LOG_FUNCTION_NOARGS ();
  return OPT_NUMBER;
}

uint8_t SdattackdsrOptionPadn::Process (Ptr<Packet> packet, Ptr<Packet> sdattackdsrP, Ipv4Address ipv4Address, Ipv4Address source, Ipv4Header const& ipv4Header, uint8_t protocol, bool& isPromisc, Ipv4Address promiscSource)
{
  NS_LOG_FUNCTION (this << packet << sdattackdsrP << ipv4Address << source << ipv4Header << (uint32_t)protocol << isPromisc);

  Ptr<Packet> p = packet->Copy ();
  SdattackdsrOptionPadnHeader padnHeader;
  p->RemoveHeader (padnHeader);

  isPromisc = false;

  return padnHeader.GetSerializedSize ();
}

NS_OBJECT_ENSURE_REGISTERED (SdattackdsrOptionRreq);

TypeId SdattackdsrOptionRreq::GetTypeId ()
{
  static TypeId tid = TypeId ("ns3::sdattackdsr::SdattackdsrOptionRreq")
    .SetParent<SdattackdsrOptions> ()
    .SetGroupName ("Sdattackdsr")
    .AddConstructor<SdattackdsrOptionRreq> ()
  ;
  return tid;
}

TypeId SdattackdsrOptionRreq::GetInstanceTypeId () const
{
  return GetTypeId ();
}

SdattackdsrOptionRreq::SdattackdsrOptionRreq ()
{
  NS_LOG_FUNCTION_NOARGS ();
}

SdattackdsrOptionRreq::~SdattackdsrOptionRreq ()
{
  NS_LOG_FUNCTION_NOARGS ();
}

uint8_t SdattackdsrOptionRreq::GetOptionNumber () const
{
  NS_LOG_FUNCTION_NOARGS ();

  return OPT_NUMBER;
}

uint8_t SdattackdsrOptionRreq::Process (Ptr<Packet> packet, Ptr<Packet> sdattackdsrP, Ipv4Address ipv4Address, Ipv4Address source, Ipv4Header const& ipv4Header, uint8_t protocol, bool& isPromisc, Ipv4Address promiscSource)
{
  u_int8_t malicious = 6;

  NS_LOG_FUNCTION (this << packet << sdattackdsrP << ipv4Address << source << ipv4Header <<
     (uint32_t)protocol << isPromisc);
  // Fields from IP header
  Ipv4Address srcAddress = ipv4Header.GetSource ();
  /*
   * \ when the ip source address is equal to the address of our own, this is request packet originated
   * \ by the node itself, discard it
   */
  if (source == ipv4Address)
    {
      NS_LOG_DEBUG ("Discard the packet since it was originated from same source address");
      m_dropTrace (packet); // call the drop trace to show in the tracing
      return 0;
    }
  /*
   * Get the node associated with the ipv4 address and get several objects from the node and leave for further use
   */
   //ブラックホール攻撃
   if (malicious == GetIDfromIP (ipv4Address))
   {
     NS_LOG_DEBUG ("im malicious node ! Discard the packet ");
     m_dropTrace (packet);
   }

  Ptr<Node> node = GetNodeWithAddress (ipv4Address);
  Ptr<sdattackdsr::SdattackdsrRouting> sdattackdsr = node->GetObject<sdattackdsr::SdattackdsrRouting> ();

  Ptr<Packet> p = packet->Copy (); // Note: The packet here doesn't contain the fixed size sdattackdsr header
  /*
   * \brief Get the number of routers' address field before removing the header
   * \peek the packet and get the value
   */
  uint8_t buf[2];
  p->CopyData (buf, sizeof(buf));
  uint8_t numberAddress = (buf[1] - 6) / 4;
  NS_LOG_DEBUG ("The number of Ip addresses " << (uint32_t)numberAddress);
  if (numberAddress >= 255)
    {
      NS_LOG_DEBUG ("Discard the packet, malformed header since two many ip addresses in route");
      m_dropTrace (packet); // call the drop trace to show in the tracing
      return 0;
    }

  /*
   * Create the sdattackdsr rreq header
   */
  SdattackdsrOptionRreqHeader rreq;
  /*
   * Set the number of addresses with the value from peek data and remove the rreq header
   */
  rreq.SetNumberAddress (numberAddress);
  // Remove the route request header
  p->RemoveHeader (rreq);
  // Verify the option length
  uint8_t length = rreq.GetLength ();
  if (length % 2 != 0)
    {
      NS_LOG_LOGIC ("Malformed header. Drop!");
      m_dropTrace (packet); // call drop trace
      return 0;
    }
  // Check the rreq id for verifying the request id
  uint16_t requestId = rreq.GetId ();
  // The target address is where we want to send the data packets
  Ipv4Address targetAddress = rreq.GetTarget ();
  Ipv4Address fakeAddress = Ipv4Address("10.1.1.55");
  // Get the node list and source address from the route request header
  std::vector<Ipv4Address> mainVector = rreq.GetNodesAddresses ();
  std::vector<Ipv4Address> nodeList (mainVector);
  // Get the real source address of this request, it will be used when checking if we have received the save
  // route request before or not
  Ipv4Address sourceAddress = nodeList.front ();
  PrintVector (nodeList);
  /*
   * Construct the sdattackdsr routing header for later use
   */
  SdattackdsrRoutingHeader sdattackdsrRoutingHeader;
  sdattackdsrRoutingHeader.SetNextHeader (protocol);
  sdattackdsrRoutingHeader.SetMessageType (1);
  sdattackdsrRoutingHeader.SetSourceId (GetIDfromIP (source));
  sdattackdsrRoutingHeader.SetDestId (255);

  // check whether we have received this request or not, if not, it will save the request in the table for
  // later use, if not found, return false, and push the newly received source request entry in the cache

  // Get the TTL value, this is used to test if the packet will be forwarded or not
  uint8_t ttl = ipv4Header.GetTtl ();
  bool dupRequest = false;  // initialize the duplicate request check value
  if (ttl)
    {
      // if the ttl value is not 0, then this request will be forwarded, then we need to
      // save it in the source entry
      dupRequest = sdattackdsr->FindSourceEntry (sourceAddress, targetAddress, requestId);
    }
  /*
   * Before processing the route request, we need to check two things
   * 1. if this is the exact same request we have just received, ignore it
   * 2. if our address is already in the path list, ignore it
   * 3. otherwise process further
   */

  if (dupRequest)
    {
      // We have received this same route request before, not forwarding it now
      NS_LOG_LOGIC ("Duplicate request. Drop!");
      m_dropTrace (packet); // call drop trace
      return 0;
    }

  else if (CheckDuplicates (ipv4Address, nodeList))
    {
      /*
       * if the route contains the node address already, drop the request packet
       */
      m_dropTrace (packet);    // call drop trace
      NS_LOG_DEBUG ("Our node address is already seen in the route, drop the request");
      return 0;
    }
  else
    {
      // A node ignores all RREQs received from any node in its blacklist
      SdattackdsrRouteCacheEntry toPrev;
      bool isRouteInCache = sdattackdsr->LookupRoute (targetAddress,
                                              toPrev);
      SdattackdsrRouteCacheEntry::IP_VECTOR ip = toPrev.GetVector (); // The route from our own route cache to dst
      PrintVector (ip);
      std::vector<Ipv4Address> saveRoute (nodeList);
      PrintVector (saveRoute);
      bool areThereDuplicates = IfDuplicates (ip,
                                              saveRoute);
      /*
       *  When the reverse route is created or updated, the following actions on the route are also carried out:
       *  3. the next hop in the routing table becomes the node from which the  RREQ was received
       *  4. the hop count is copied from the Hop Count in the RREQ message;
       */

      //  A node generates a RREP if either:
      //  (i)  it is itself the destination,
      /*
       * The target address equal to our own ip address
       */
      NS_LOG_DEBUG ("The target address over here " << targetAddress << " and the ip address " << ipv4Address << " and the source address " << mainVector[0]);

      //偽造ルート作成
      if (GetIDfromIP (ipv4Address) == malicious)
      {
        Ipv4Address nextHop; // 使用するネクストホップアドレスを宣言する
        if (nodeList.size () == 1)
          {
            NS_LOG_DEBUG ("These two nodes are neighbors");
            //std::cout << "これらは隣接ノードです.Error\n";
            m_finalRoute.clear ();
            /// TODOはsrcAddressをソースに変更しました。どちらにしてもかまいません。後で確認してください
            m_finalRoute.push_back (source);     // リクエストの発信者のアドレスをプッシュバックする
            m_finalRoute.push_back (fakeAddress);
            m_finalRoute.push_back (ipv4Address);    // 自分の住所を押し戻す
            nextHop = srcAddress;
          }
        else
          {
            std::vector<Ipv4Address> changeRoute (nodeList);
            changeRoute.push_back (ipv4Address);    // 自分の住所を押し戻す

/*
            for(int i =0; i < (int)changeRoute.size();i++){
              std::cout << changeRoute[i] <<"\n";
            }
*/

            m_finalRoute.clear ();              // 明確なルートベクトルを取得する
            std::cout << "現在のノードは　" << ipv4Address << '\n';
            /*
            std::cout << "Mノード：偽ルートを作成" << '\n';
            std::cout << "ソースノードは　" << mainVector[0] << '\n';
            std::cout << "現在のノードは　" << ipv4Address << '\n';
            std::cout << "宛先ノードは　" << targetAddress << '\n';
            std::cout << "/-----偽ルート-----/" << '\n';
            */


            for (std::vector<Ipv4Address>::iterator i = changeRoute.begin (); i != changeRoute.end (); ++i)
              {
              //  std::cout << *i << '\n';

                /*悪意のあるノードから宛先ノードまでを省略した偽RREPを作成する
                */

              //  m_finalRoute.push_back (fakeAddress);
                  m_finalRoute.push_back (*i);


              }
              //std::cout << "最後のプッシュ" << '\n';
              m_finalRoute.push_back (fakeAddress);
            m_finalRoute.push_back (targetAddress);  // 出発地から目的地までの完全なルートを取得する


            outputfile << "/finalroute/　idはipv4addressの末尾から-1した数" << '\n';
            for (std::vector<Ipv4Address>::iterator i = m_finalRoute.begin (); i != m_finalRoute.end (); ++i)
              {
                outputfile << *i << '\n';
              }
            PrintVector (m_finalRoute);

            /*
            std::cout << "/----------/" << '\n';

            std::cout << "m_finalRoute の数" << '\n'<< m_finalRoute.size() << '\n';
            std::cout << "changeRoute　の数" <<'\n'<< changeRoute.size() << '\n';


            std::cout << "/--------/" << '\n';
            */

            nextHop = ReverseSearchNextHop (ipv4Address, m_finalRoute); // get the next hop
          }

        SdattackdsrOptionRrepHeader rrep;
        rrep.SetNodesAddress (m_finalRoute);     // ルート応答ヘッダーにノードアドレスを設定します
        NS_LOG_DEBUG ("The nextHop address " << nextHop);
        Ipv4Address replyDst = m_finalRoute.front ();

        //std::cout << "/* replyDst = */
        ////"<< replyDst << '\n';
        /*
         *この部分は、パケットにsdattackdsrヘッダーを追加し、ルート応答パケットを送信します
         */

        SdattackdsrRoutingHeader sdattackdsrRoutingHeader;
        sdattackdsrRoutingHeader.SetNextHeader (protocol);
        sdattackdsrRoutingHeader.SetMessageType (1);
        //RREPの宛先ノードをターゲットにする。SetSourceId
        sdattackdsrRoutingHeader.SetSourceId (GetIDfromIP (targetAddress));
        sdattackdsrRoutingHeader.SetDestId (GetIDfromIP (replyDst));

        // Set the route for route reply
        SetRoute (nextHop, ipv4Address);

        uint8_t length = rrep.GetLength ();  // タイプヘッダーを除くrrepヘッダーの長さを取得します
        sdattackdsrRoutingHeader.SetPayloadLength (length + 2);
        sdattackdsrRoutingHeader.AddSdattackdsrOption (rrep);
        Ptr<Packet> newPacket = Create<Packet> ();
        newPacket->AddHeader (sdattackdsrRoutingHeader);
        sdattackdsr->ScheduleInitialReply (newPacket, ipv4Address, nextHop, m_ipv4Route);

        /*
         * rreq発信元へのルートエントリを作成し、ルートキャッシュに保存します。ルートを逆にする必要もあります
         */
         //std::cout << "finalroute" << '\n';

        PrintVector (m_finalRoute);
        if (ReverseRoutes (m_finalRoute))
          {

            PrintVector (m_finalRoute);
            Ipv4Address dst = m_finalRoute.back ();
            bool addRoute = false;
            if (numberAddress > 0)
              {
                SdattackdsrRouteCacheEntry toSource (

                  /*IP_VECTOR=*/
                m_finalRoute, /*dst=*/
                                                            dst, /*expire time=*/ ActiveRouteTimeout);
               if (sdattackdsr->IsLinkCache ())
                  {
                    addRoute = sdattackdsr->AddRoute_Link (m_finalRoute, ipv4Address);
                  }
                else
                  {
                    addRoute = sdattackdsr->AddRoute (toSource);
                  }
              }
            else
              {
                NS_LOG_DEBUG ("Abnormal RouteRequest");
                return 0;
              }

            if (addRoute)
              {
                /*
                 * dstへのルートを見つけ、ソースルートオプションヘッダーを構築します
                 */
             SdattackdsrOptionSRHeader sourceRoute;
                NS_LOG_DEBUG ("The route length " << m_finalRoute.size ());
                sourceRoute.SetNodesAddress (m_finalRoute);

                /// TODO !!!!!!!!!!!!!!
                   ///この部分について考えてみましょう。ルートを追加しました。
                   ///安定性を今すぐ上げる必要はないか?????
                // if (sdattackdsr->IsLinkCache ())
                //   {
                //     sdattackdsr->UseExtends (m_finalRoute);
                //   }
                sourceRoute.SetSegmentsLeft ((m_finalRoute.size () - 2));
                // The salvage value here is 0
                sourceRoute.SetSalvage (0);
                Ipv4Address nextHop = SearchNextHop (ipv4Address, m_finalRoute); // Get the next hop address
                NS_LOG_DEBUG ("The nextHop address " << nextHop);

                if (nextHop == "0.0.0.0")
                  {
                    sdattackdsr->PacketNewRoute (sdattackdsrP, ipv4Address, dst, protocol);
                    return 0;
                  }
                SetRoute (nextHop, ipv4Address);

                /*
                 * 送信バッファからデータパケットを送信します
                 */
                // std::cout <<"nexthop = " << nextHop << '\n';
             sdattackdsr->SendPacketFromBuffer (sourceRoute, nextHop, protocol);
                // //データパケットを送信した後、宛先のルート要求タイマーをキャンセルします
                sdattackdsr->CancelRreqTimer (dst, true);

              //  std::cout << "/* ipv4Address */" << ipv4Address << '\n';

              }
            else
              {
                NS_LOG_DEBUG ("The route is failed to add in cache");
                return 0;
              }
          }
        else
          {
            NS_LOG_DEBUG ("Unable to reverse route");
            return 0;
          }
        isPromisc = false;
      //  std::cout << "malicios serialized" << '\n';

        return rreq.GetSerializedSize ();
      }

      if (targetAddress == ipv4Address)
        {
          Ipv4Address nextHop; // Declare the next hop address to use
          if (nodeList.size () == 1)
            {
              NS_LOG_DEBUG ("These two nodes are neighbors");
              m_finalRoute.clear ();
              /// TODO has changed the srcAddress to source, should not matter either way, check later
              m_finalRoute.push_back (source);     // push back the request originator's address
              m_finalRoute.push_back (ipv4Address);    // push back our own address
              nextHop = srcAddress;
            }
          else
            {
              std::vector<Ipv4Address> changeRoute (nodeList);
              changeRoute.push_back (ipv4Address);    // push back our own address
              m_finalRoute.clear ();              // get a clear route vector
              for (std::vector<Ipv4Address>::iterator i = changeRoute.begin (); i != changeRoute.end (); ++i)
                {
                  m_finalRoute.push_back (*i);  // Get the full route from source to destination
                }
              PrintVector (m_finalRoute);
              nextHop = ReverseSearchNextHop (ipv4Address, m_finalRoute); // get the next hop
            }

          SdattackdsrOptionRrepHeader rrep;
          rrep.SetNodesAddress (m_finalRoute);     // Set the node addresses in the route reply header
          NS_LOG_DEBUG ("The nextHop address " << nextHop);
          Ipv4Address replyDst = m_finalRoute.front ();
          /*
           * This part add sdattackdsr header to the packet and send route reply packet
           */
          SdattackdsrRoutingHeader sdattackdsrRoutingHeader;
          sdattackdsrRoutingHeader.SetNextHeader (protocol);
          sdattackdsrRoutingHeader.SetMessageType (1);
          sdattackdsrRoutingHeader.SetSourceId (GetIDfromIP (ipv4Address));
          sdattackdsrRoutingHeader.SetDestId (GetIDfromIP (replyDst));
          // Set the route for route reply
          SetRoute (nextHop, ipv4Address);

          uint8_t length = rrep.GetLength ();  // Get the length of the rrep header excluding the type header
          sdattackdsrRoutingHeader.SetPayloadLength (length + 2);
          sdattackdsrRoutingHeader.AddSdattackdsrOption (rrep);
          Ptr<Packet> newPacket = Create<Packet> ();
          newPacket->AddHeader (sdattackdsrRoutingHeader);
          sdattackdsr->ScheduleInitialReply (newPacket, ipv4Address, nextHop, m_ipv4Route);
          /*
           * Create the route entry to the rreq originator and save it to route cache, also need to reverse the route
           */
          PrintVector (m_finalRoute);
          if (ReverseRoutes (m_finalRoute))
            {
              PrintVector (m_finalRoute);
              Ipv4Address dst = m_finalRoute.back ();
              bool addRoute = false;
              if (numberAddress > 0)
                {
                  SdattackdsrRouteCacheEntry toSource (/*IP_VECTOR=*/ m_finalRoute, /*dst=*/
                                                              dst, /*expire time=*/ ActiveRouteTimeout);
                  if (sdattackdsr->IsLinkCache ())
                    {
                      addRoute = sdattackdsr->AddRoute_Link (m_finalRoute, ipv4Address);
                    }
                  else
                    {
                      addRoute = sdattackdsr->AddRoute (toSource);
                    }
                }
              else
                {
                  NS_LOG_DEBUG ("Abnormal RouteRequest");
                  return 0;
                }

              if (addRoute)
                {
                  /*
                   * Found a route to the dst, construct the source route option header
                   */
                  SdattackdsrOptionSRHeader sourceRoute;
                  NS_LOG_DEBUG ("The route length " << m_finalRoute.size ());
                  sourceRoute.SetNodesAddress (m_finalRoute);

                  /// TODO !!!!!!!!!!!!!!
                  /// Think about this part, we just added the route,
                  /// probability no need to increase stability now?????
                  // if (sdattackdsr->IsLinkCache ())
                  //   {
                  //     sdattackdsr->UseExtends (m_finalRoute);
                  //   }
                  sourceRoute.SetSegmentsLeft ((m_finalRoute.size () - 2));
                  // The salvage value here is 0
                  sourceRoute.SetSalvage (0);
                  Ipv4Address nextHop = SearchNextHop (ipv4Address, m_finalRoute); // Get the next hop address
                  NS_LOG_DEBUG ("The nextHop address " << nextHop);

                  if (nextHop == "0.0.0.0")
                    {
                      sdattackdsr->PacketNewRoute (sdattackdsrP, ipv4Address, dst, protocol);
                      return 0;
                    }
                  SetRoute (nextHop, ipv4Address);
                  /*
                   * Send the data packet from the send buffer
                   */
                  sdattackdsr->SendPacketFromBuffer (sourceRoute, nextHop, protocol);
                  // Cancel the route request timer for destination after sending the data packet
                  sdattackdsr->CancelRreqTimer (dst, true);
                }
              else
                {
                  NS_LOG_DEBUG ("The route is failed to add in cache");
                  return 0;
                }
            }
          else
            {
              NS_LOG_DEBUG ("Unable to reverse route");
              return 0;
            }
          isPromisc = false;
          return rreq.GetSerializedSize ();
        }

      /*
       * (ii) or it has an active route to the destination, send reply based on request header and route cache,
       *      need to delay based on a random value from d = H * (h - 1 + r), which can avoid possible route
       *      reply storm. Also, verify if two vectors do not contain duplicates (part of the route to the
       *      destination from route cache and route collected so far). If so, do not use the route found
       *      and forward the route request.
       */
      else if (isRouteInCache && !areThereDuplicates)
        {
          m_finalRoute.clear ();                // Clear the final route vector
          /**
           * push back the intermediate node address from the source to this node
           */
          for (std::vector<Ipv4Address>::iterator i = saveRoute.begin (); i != saveRoute.end (); ++i)
            {
              m_finalRoute.push_back (*i);
            }
          /**
           * push back the route vector we found in our route cache to destination, including this node's address
           */
          for (std::vector<Ipv4Address>::iterator j = ip.begin (); j != ip.end (); ++j)
            {
              m_finalRoute.push_back (*j);
            }
          /*
           * Create the route entry to the rreq originator and save it to route cache, also need to reverse the route
           */
          bool addRoute = false;
          std::vector<Ipv4Address> reverseRoute (m_finalRoute);

          if (ReverseRoutes (reverseRoute))
            {
              saveRoute.push_back (ipv4Address);
              ReverseRoutes (saveRoute);
              Ipv4Address dst = saveRoute.back ();
              NS_LOG_DEBUG ("This is the route save in route cache");
              PrintVector (saveRoute);

              SdattackdsrRouteCacheEntry toSource (/*IP_VECTOR=*/ saveRoute, /*dst=*/ dst, /*expire time=*/ ActiveRouteTimeout);
              NS_ASSERT (saveRoute.front () == ipv4Address);
              // Add the route entry in the route cache
              if (sdattackdsr->IsLinkCache ())
                {
                  addRoute = sdattackdsr->AddRoute_Link (saveRoute, ipv4Address);
                }
              else
                {
                  addRoute = sdattackdsr->AddRoute (toSource);
                }

              if (addRoute)
                {
                  NS_LOG_LOGIC ("We have added the route and search send buffer for packet with destination " << dst);
                  /*
                   * Found a route the dst, construct the source route option header
                   */
                  SdattackdsrOptionSRHeader sourceRoute;
                  PrintVector (saveRoute);

                  sourceRoute.SetNodesAddress (saveRoute);
                  // if (sdattackdsr->IsLinkCache ())
                  //   {
                  //     sdattackdsr->UseExtends (saveRoute);
                  //   }
                  sourceRoute.SetSegmentsLeft ((saveRoute.size () - 2));
                  uint8_t salvage = 0;
                  sourceRoute.SetSalvage (salvage);
                  Ipv4Address nextHop = SearchNextHop (ipv4Address, saveRoute);     // Get the next hop address
                  NS_LOG_DEBUG ("The nextHop address " << nextHop);

                  if (nextHop == "0.0.0.0")
                    {
                      sdattackdsr->PacketNewRoute (sdattackdsrP, ipv4Address, dst, protocol);
                      return 0;
                    }
                  SetRoute (nextHop, ipv4Address);
                  /*
                   * Schedule the packet retry
                   */
                  sdattackdsr->SendPacketFromBuffer (sourceRoute, nextHop, protocol);
                  // Cancel the route request timer for destination
                  sdattackdsr->CancelRreqTimer (dst, true);
                }
              else
                {
                  NS_LOG_DEBUG ("The route is failed to add in cache");
                  return 0;
                }
            }
          else
            {
              NS_LOG_DEBUG ("Unable to reverse the route");
              return 0;
            }

          /*
           * Need to first pin down the next hop address before removing duplicates
           */
          Ipv4Address nextHop = ReverseSearchNextHop (ipv4Address, m_finalRoute);
          /*
           * First remove the duplicate ip address to automatically shorten the route, and then reversely
           * search the next hop address
           */
          // Set the route
          SetRoute (nextHop, ipv4Address);

          uint16_t hops = m_finalRoute.size ();
          SdattackdsrOptionRrepHeader rrep;
          rrep.SetNodesAddress (m_finalRoute);         // Set the node addresses in the route reply header
          // Get the real source of the reply
          Ipv4Address realSource = m_finalRoute.back ();
          PrintVector (m_finalRoute);
          NS_LOG_DEBUG ("This is the full route from " << realSource << " to " << m_finalRoute.front ());
          /*
           * This part add sdattackdsr header to the packet and send route reply packet
           */
          SdattackdsrRoutingHeader sdattackdsrRoutingHeader;
          sdattackdsrRoutingHeader.SetNextHeader (protocol);
          sdattackdsrRoutingHeader.SetMessageType (1);
          sdattackdsrRoutingHeader.SetSourceId (GetIDfromIP (realSource));
          sdattackdsrRoutingHeader.SetDestId (255);

          uint8_t length = rrep.GetLength ();      // Get the length of the rrep header excluding the type header
          sdattackdsrRoutingHeader.SetPayloadLength (length + 2);
          sdattackdsrRoutingHeader.AddSdattackdsrOption (rrep);
          Ptr<Packet> newPacket = Create<Packet> ();
          newPacket->AddHeader (sdattackdsrRoutingHeader);
          sdattackdsr->ScheduleCachedReply (newPacket, ipv4Address, nextHop, m_ipv4Route, hops);
          isPromisc = false;
          return rreq.GetSerializedSize ();
        }
      /*
       * (iii) no route in any type has been found
       */
      else
        {
          mainVector.push_back (ipv4Address);
          NS_ASSERT (mainVector.front () == source);
          NS_LOG_DEBUG ("Print out the main vector");
          PrintVector (mainVector);
          rreq.SetNodesAddress (mainVector);

          Ptr<Packet> errP = p->Copy ();
          if (errP->GetSize ())
            {
              NS_LOG_DEBUG ("Error header included");
              SdattackdsrOptionRerrUnreachHeader rerr;
              p->RemoveHeader (rerr);
              Ipv4Address errorSrc = rerr.GetErrorSrc ();
              Ipv4Address unreachNode = rerr.GetUnreachNode ();
              Ipv4Address errorDst = rerr.GetErrorDst ();

              if ((errorSrc == srcAddress) && (unreachNode == ipv4Address))
                {
                  NS_LOG_DEBUG ("The error link back to work again");
                  uint16_t length = rreq.GetLength ();
                  NS_LOG_DEBUG ("The RREQ header length " <<  length);
                  sdattackdsrRoutingHeader.AddSdattackdsrOption (rreq);
                  sdattackdsrRoutingHeader.SetPayloadLength (length + 2);
                }
              else
                {
                  sdattackdsr->DeleteAllRoutesIncludeLink (errorSrc, unreachNode, ipv4Address);

                  SdattackdsrOptionRerrUnreachHeader newUnreach;
                  newUnreach.SetErrorType (1);
                  newUnreach.SetErrorSrc (errorSrc);
                  newUnreach.SetUnreachNode (unreachNode);
                  newUnreach.SetErrorDst (errorDst);
                  newUnreach.SetSalvage (rerr.GetSalvage ()); // Set the value about whether to salvage a packet or not
                  uint16_t length = rreq.GetLength () + newUnreach.GetLength ();
                  NS_LOG_DEBUG ("The RREQ and newUnreach header length " <<  length);
                  sdattackdsrRoutingHeader.SetPayloadLength (length + 4);
                  sdattackdsrRoutingHeader.AddSdattackdsrOption (rreq);
                  sdattackdsrRoutingHeader.AddSdattackdsrOption (newUnreach);
                }
            }
          else
            {
              uint16_t length = rreq.GetLength ();
              NS_LOG_DEBUG ("The RREQ header length " <<  length);
              sdattackdsrRoutingHeader.AddSdattackdsrOption (rreq);
              sdattackdsrRoutingHeader.SetPayloadLength (length + 2);
            }
          // Get the TTL value
          uint8_t ttl = ipv4Header.GetTtl ();
          /*
          * Decrease the TTL value in the packet tag by one, this tag will go to ip layer 3 send function
          * and drop packet when TTL value equals to 0
          */
          NS_LOG_DEBUG ("The ttl value here " << (uint32_t)ttl);
          if (ttl)
            {
              Ptr<Packet> interP = Create<Packet> ();
              SocketIpTtlTag tag;
              if (malicious == GetIDfromIP (ipv4Address))
              {
                tag.SetTtl (ttl + 2000);
              }else
              {
              tag.SetTtl (ttl - 1);
            }
              interP->AddPacketTag (tag);
              interP->AddHeader (sdattackdsrRoutingHeader);
              sdattackdsr->ScheduleInterRequest (interP);
              isPromisc = false;
            }
          return rreq.GetSerializedSize ();
        }
    }
  //unreachable:  return rreq.GetSerializedSize ();
}

NS_OBJECT_ENSURE_REGISTERED (SdattackdsrOptionRrep);

TypeId SdattackdsrOptionRrep::GetTypeId ()
{
  static TypeId tid = TypeId ("ns3::sdattackdsr::SdattackdsrOptionRrep")
    .SetParent<SdattackdsrOptions> ()
    .SetGroupName ("Sdattackdsr")
    .AddConstructor<SdattackdsrOptionRrep> ()
  ;
  return tid;
}

SdattackdsrOptionRrep::SdattackdsrOptionRrep ()
{
  NS_LOG_FUNCTION_NOARGS ();
}

SdattackdsrOptionRrep::~SdattackdsrOptionRrep ()
{
  NS_LOG_FUNCTION_NOARGS ();
}

TypeId SdattackdsrOptionRrep::GetInstanceTypeId () const
{
  return GetTypeId ();
}

uint8_t SdattackdsrOptionRrep::GetOptionNumber () const
{
  NS_LOG_FUNCTION_NOARGS ();

  return OPT_NUMBER;
}

uint8_t SdattackdsrOptionRrep::Process (Ptr<Packet> packet, Ptr<Packet> sdattackdsrP, Ipv4Address ipv4Address, Ipv4Address source, Ipv4Header const& ipv4Header, uint8_t protocol, bool& isPromisc, Ipv4Address promiscSource)
{
  NS_LOG_FUNCTION (this << packet << sdattackdsrP << ipv4Address << source << ipv4Header << (uint32_t)protocol << isPromisc);

  Ptr<Packet> p = packet->Copy ();

  // Get the number of routers' address field
  uint8_t buf[2];
  p->CopyData (buf, sizeof(buf));
  uint8_t numberAddress = (buf[1] - 2) / 4;

  SdattackdsrOptionRrepHeader rrep;
  rrep.SetNumberAddress (numberAddress);  // Set the number of ip address in the header to reserver space for deserialize header
  p->RemoveHeader (rrep);

  Ptr<Node> node = GetNodeWithAddress (ipv4Address);
  Ptr<sdattackdsr::SdattackdsrRouting> sdattackdsr = node->GetObject<sdattackdsr::SdattackdsrRouting> ();

  NS_LOG_DEBUG ("The next header value " << (uint32_t)protocol);

  std::vector<Ipv4Address> nodeList = rrep.GetNodesAddress ();
  /**
   * Get the destination address, which is the last element in the nodeList
   */
  Ipv4Address targetAddress = nodeList.front ();
  // If the RREP option has reached to the destination
  if (targetAddress == ipv4Address)
    {
      RemoveDuplicates (nodeList); // This is for the route reply from intermediate node since we didn't remove
                                   // duplicate there
      if (nodeList.size () == 0)
        {
          NS_LOG_DEBUG ("The route we have contains 0 entries");
          return 0;
        }
      /**
       * Get the destination address for the data packet, which is the last element in the nodeList
       */
      Ipv4Address dst = nodeList.back ();
      /**
       * Add the newly found route to the route cache
       * The route looks like:
       * \\ "srcAddress" + "intermediate node address" + "targetAddress"
       */
      SdattackdsrRouteCacheEntry toDestination (/*IP_VECTOR=*/ nodeList, /*dst=*/ dst, /*expire time=*/ ActiveRouteTimeout);
      NS_ASSERT (nodeList.front () == ipv4Address);
      bool addRoute = false;
      if (sdattackdsr->IsLinkCache ())
        {
          addRoute = sdattackdsr->AddRoute_Link (nodeList, ipv4Address);
        }
      else
        {
          addRoute = sdattackdsr->AddRoute (toDestination);
        }

      if (addRoute)
        {
          NS_LOG_DEBUG ("We have added the route and search send buffer for packet with destination " << dst);
          /**
           * Found a route the dst, construct the source route option header
           */
          SdattackdsrOptionSRHeader sourceRoute;
          NS_LOG_DEBUG ("The route length " << nodeList.size ());
          sourceRoute.SetNodesAddress (nodeList);
          sourceRoute.SetSegmentsLeft ((nodeList.size () - 2));
          sourceRoute.SetSalvage (0);
          Ipv4Address nextHop = SearchNextHop (ipv4Address, nodeList); // Get the next hop address
          NS_LOG_DEBUG ("The nextHop address " << nextHop);
          if (nextHop == "0.0.0.0")
            {
              sdattackdsr->PacketNewRoute (sdattackdsrP, ipv4Address, dst, protocol);
              return 0;
            }
          PrintVector (nodeList);
          SetRoute (nextHop, ipv4Address);
          // Cancel the route request timer for destination
          sdattackdsr->CancelRreqTimer (dst, true);
          /**
           * Schedule the packet retry
           */
          sdattackdsr->SendPacketFromBuffer (sourceRoute, nextHop, protocol);
        }
      else
        {
          NS_LOG_DEBUG ("Failed to add the route");
          return 0;
        }
    }
  else
    {
      uint8_t length = rrep.GetLength () - 2; // The get length - 2 is to get aligned for the malformed header check
      NS_LOG_DEBUG ("The length of rrep option " << (uint32_t)length);

      if (length % 2 != 0)
        {
          NS_LOG_LOGIC ("Malformed header. Drop!");
          m_dropTrace (packet);
          return 0;
        }
      PrintVector (nodeList);
      /*
       * This node is only an intermediate node, but it needs to save the possible route to the destination when cutting the route
       */
      std::vector<Ipv4Address> routeCopy = nodeList;
      std::vector<Ipv4Address> cutRoute = CutRoute (ipv4Address, nodeList);
      PrintVector (cutRoute);
      if (cutRoute.size () >= 2)
        {
          Ipv4Address dst = cutRoute.back ();
          NS_LOG_DEBUG ("The route destination after cut " << dst);
          SdattackdsrRouteCacheEntry toDestination (/*IP_VECTOR=*/ cutRoute, /*dst=*/ dst, /*expire time=*/ ActiveRouteTimeout);
          NS_ASSERT (cutRoute.front () == ipv4Address);
          bool addRoute = false;
          if (sdattackdsr->IsLinkCache ())
            {
              addRoute = sdattackdsr->AddRoute_Link (nodeList, ipv4Address);
            }
          else
            {
              addRoute = sdattackdsr->AddRoute (toDestination);
            }
          if (addRoute)
            {
              sdattackdsr->CancelRreqTimer (dst, true);
            }
          else
            {
              NS_LOG_DEBUG ("The route not added");
            }
        }
      else
        {
          NS_LOG_DEBUG ("The route is corrupted");
        }
      /*
       * Reverse search the vector for next hop address
       */
      Ipv4Address nextHop = ReverseSearchNextHop (ipv4Address, routeCopy);
      NS_ASSERT (routeCopy.back () == source);
      PrintVector (routeCopy);
      NS_LOG_DEBUG ("The nextHop address " << nextHop << " and the source in the route reply " << source);
      /*
       * Set the route entry we will use to send reply
       */
      SetRoute (nextHop, ipv4Address);
      /*
       * This part add sdattackdsr routing header to the packet and send reply
       */
      SdattackdsrRoutingHeader sdattackdsrRoutingHeader;
      sdattackdsrRoutingHeader.SetNextHeader (protocol);

      length = rrep.GetLength ();    // Get the length of the rrep header excluding the type header
      NS_LOG_DEBUG ("The reply header length " << (uint32_t)length);
      sdattackdsrRoutingHeader.SetPayloadLength (length + 2);
      sdattackdsrRoutingHeader.SetMessageType (1);
      sdattackdsrRoutingHeader.SetSourceId (GetIDfromIP (source));
      sdattackdsrRoutingHeader.SetDestId (GetIDfromIP (targetAddress));
      sdattackdsrRoutingHeader.AddSdattackdsrOption (rrep);
      Ptr<Packet> newPacket = Create<Packet> ();
      newPacket->AddHeader (sdattackdsrRoutingHeader);
      sdattackdsr->SendReply (newPacket, ipv4Address, nextHop, m_ipv4Route);
      isPromisc = false;
    }
  return rrep.GetSerializedSize ();
}

NS_OBJECT_ENSURE_REGISTERED (SdattackdsrOptionSR);

TypeId SdattackdsrOptionSR::GetTypeId ()
{
  static TypeId tid = TypeId ("ns3::sdattackdsr::SdattackdsrOptionSR")
    .SetParent<SdattackdsrOptions> ()
    .SetGroupName ("Sdattackdsr")
    .AddConstructor<SdattackdsrOptionSR> ()
  ;
  return tid;
}

SdattackdsrOptionSR::SdattackdsrOptionSR ()
{
  NS_LOG_FUNCTION_NOARGS ();
}

SdattackdsrOptionSR::~SdattackdsrOptionSR ()
{
  NS_LOG_FUNCTION_NOARGS ();
}

TypeId SdattackdsrOptionSR::GetInstanceTypeId () const
{
  return GetTypeId ();
}

uint8_t SdattackdsrOptionSR::GetOptionNumber () const
{
  NS_LOG_FUNCTION_NOARGS ();
  return OPT_NUMBER;
}

uint8_t SdattackdsrOptionSR::Process (Ptr<Packet> packet, Ptr<Packet> sdattackdsrP, Ipv4Address ipv4Address, Ipv4Address source, Ipv4Header const& ipv4Header, uint8_t protocol, bool& isPromisc, Ipv4Address promiscSource)
{
  NS_LOG_FUNCTION (this << packet << sdattackdsrP << ipv4Address << source << ipv4Address << ipv4Header << (uint32_t)protocol << isPromisc);
  Ptr<Packet> p = packet->Copy ();
  // Get the number of routers' address field
  uint8_t buf[2];
  p->CopyData (buf, sizeof(buf));
  uint8_t numberAddress = (buf[1] - 2) / 4;
  SdattackdsrOptionSRHeader sourceRoute;
  sourceRoute.SetNumberAddress (numberAddress);
  p->RemoveHeader (sourceRoute);

  // The route size saved in the source route
  std::vector<Ipv4Address> nodeList = sourceRoute.GetNodesAddress ();
  uint8_t segsLeft = sourceRoute.GetSegmentsLeft ();
  uint8_t salvage = sourceRoute.GetSalvage ();
  /*
   * Get the node from IP address and get the DSR extension object
   */
  Ptr<Node> node = GetNodeWithAddress (ipv4Address);
  Ptr<sdattackdsr::SdattackdsrRouting> sdattackdsr = node->GetObject<sdattackdsr::SdattackdsrRouting> ();
  /*
   * Get the source and destination address from ipv4 header
   */
  Ipv4Address srcAddress = ipv4Header.GetSource ();
  Ipv4Address destAddress = ipv4Header.GetDestination ();

  // Get the node list destination
  Ipv4Address destination = nodeList.back ();
  /*
   * If it's a promiscuous receive data packet,
   * 1. see if automatic route shortening possible or not
   * 2. see if it is a passive acknowledgment
   */
  if (isPromisc)
    {
      NS_LOG_LOGIC ("We process promiscuous receipt data packet");
      if (ContainAddressAfter (ipv4Address, destAddress, nodeList))
        {
          NS_LOG_LOGIC ("Send back the gratuitous reply");
          sdattackdsr->SendGratuitousReply (source, srcAddress, nodeList, protocol);
        }

      uint16_t fragmentOffset = ipv4Header.GetFragmentOffset ();
      uint16_t identification = ipv4Header.GetIdentification ();

      if (destAddress != destination)
        {
          NS_LOG_DEBUG ("Process the promiscuously received packet");
          bool findPassive = false;
          int32_t nNodes = NodeList::GetNNodes ();
          for (int32_t i = 0; i < nNodes; ++i)
            {
              NS_LOG_DEBUG ("Working with node " << i);

              Ptr<Node> node = NodeList::GetNode (i);
              Ptr<sdattackdsr::SdattackdsrRouting> sdattackdsrNode = node->GetObject<sdattackdsr::SdattackdsrRouting> ();
              // The source and destination addresses here are the real source and destination for the packet
              findPassive = sdattackdsrNode->PassiveEntryCheck (packet, source, destination, segsLeft, fragmentOffset, identification, false);
              if (findPassive)
                {
                  break;
                }
            }

          if (findPassive)
            {
              NS_LOG_DEBUG ("We find one previously received passive entry");
              /*
               * Get the node from IP address and get the DSR extension object
               * the srcAddress would be the source address from ip header
               */
              PrintVector (nodeList);

              NS_LOG_DEBUG ("promisc source " << promiscSource);
              Ptr<Node> node = GetNodeWithAddress (promiscSource);
              Ptr<sdattackdsr::SdattackdsrRouting> sdattackdsrSrc = node->GetObject<sdattackdsr::SdattackdsrRouting> ();
              sdattackdsrSrc->CancelPassiveTimer (packet, source, destination, segsLeft);
            }
          else
            {
              NS_LOG_DEBUG ("Saved the entry for further use");
              sdattackdsr->PassiveEntryCheck (packet, source, destination, segsLeft, fragmentOffset, identification, true);
            }
        }
      /// Safely terminate promiscuously received packet
      return 0;
    }
  else
    {
      /*
       * Get the number of address from the source route header
       */
      uint8_t length = sourceRoute.GetLength ();
      uint8_t nextAddressIndex;
      Ipv4Address nextAddress;

      // Get the option type value
      uint32_t size = p->GetSize ();
      uint8_t *data = new uint8_t[size];
      p->CopyData (data, size);
      uint8_t optionType = 0;
      optionType = *(data);
      /// When the option type is 160, means there is ACK request header after the source route, we need
      /// to send back acknowledgment
      if (optionType == 160)
        {
          NS_LOG_LOGIC ("Remove the ack request header and add ack header to the packet");
          // Here we remove the ack packet to the previous hop
          SdattackdsrOptionAckReqHeader ackReq;
          p->RemoveHeader (ackReq);
          uint16_t ackId = ackReq.GetAckId ();
          /*
           * Send back acknowledgment packet to the earlier hop
           * If the node list is not empty, find the previous hop from the node list,
           * otherwise, use srcAddress
           */
          Ipv4Address ackAddress = srcAddress;
          if (!nodeList.empty ())
            {
              if (segsLeft > numberAddress)   // The segmentsLeft field should not be larger than the total number of ip addresses
                {
                  NS_LOG_LOGIC ("Malformed header. Drop!");
                  m_dropTrace (packet);
                  return 0;
                }
              // -fstrict-overflow sensitive, see bug 1868
              if (numberAddress - segsLeft < 2)   // The index is invalid
                {
                  NS_LOG_LOGIC ("Malformed header. Drop!");
                  m_dropTrace (packet);
                  return 0;
                }
              ackAddress = nodeList[numberAddress - segsLeft - 2];
            }
          m_ipv4Route = SetRoute (ackAddress, ipv4Address);
          NS_LOG_DEBUG ("Send back ACK to the earlier hop " << ackAddress << " from us " << ipv4Address);
          sdattackdsr->SendAck (ackId, ackAddress, source, destination, protocol, m_ipv4Route);
        }
      /*
       * After send back ACK, check if the segments left value has turned to 0 or not, if yes, update the route entry
       * and return header length
       */
      if (segsLeft == 0)
        {
          NS_LOG_DEBUG ("This is the final destination");
          isPromisc = false;
          return sourceRoute.GetSerializedSize ();
        }

      if (length % 2 != 0)
        {
          NS_LOG_LOGIC ("Malformed header. Drop!");
          m_dropTrace (packet);
          return 0;
        }

      if (segsLeft > numberAddress) // The segmentsLeft field should not be larger than the total number of ip addresses
        {
          NS_LOG_LOGIC ("Malformed header. Drop!");
          m_dropTrace (packet);
          return 0;
        }

      SdattackdsrOptionSRHeader newSourceRoute;
      newSourceRoute.SetSegmentsLeft (segsLeft - 1);
      newSourceRoute.SetSalvage (salvage);
      newSourceRoute.SetNodesAddress (nodeList);
      nextAddressIndex = numberAddress - segsLeft;
      nextAddress = newSourceRoute.GetNodeAddress (nextAddressIndex);
      NS_LOG_DEBUG ("The next address of source route option " << nextAddress << " and the nextAddressIndex: " << (uint32_t)nextAddressIndex << " and the segments left : " << (uint32_t)segsLeft);
      /*
       * Get the target Address in the node list
       */
      Ipv4Address targetAddress = nodeList.back ();
      Ipv4Address realSource = nodeList.front ();
      /*
       * Search the vector for next hop address
       */
      Ipv4Address nextHop = SearchNextHop (ipv4Address, nodeList);
      PrintVector (nodeList);

      if (nextHop == "0.0.0.0")
        {
          NS_LOG_DEBUG ("Before new packet " << *sdattackdsrP);
          sdattackdsr->PacketNewRoute (sdattackdsrP, realSource, targetAddress, protocol);
          return 0;
        }

      if (ipv4Address == nextHop)
        {
          NS_LOG_DEBUG ("We have reached the destination");
          newSourceRoute.SetSegmentsLeft (0);
          return newSourceRoute.GetSerializedSize ();
        }
      // Verify the multicast address, leave it here for now
      if (nextAddress.IsMulticast () || destAddress.IsMulticast ())
        {
          m_dropTrace (packet);
          return 0;
        }
      // Set the route and forward the data packet
      SetRoute (nextAddress, ipv4Address);
      NS_LOG_DEBUG ("sdattackdsr packet size " << sdattackdsrP->GetSize ());
      sdattackdsr->ForwardPacket (sdattackdsrP, newSourceRoute, ipv4Header, realSource, nextAddress, targetAddress, protocol, m_ipv4Route);
    }
  return sourceRoute.GetSerializedSize ();
}

NS_OBJECT_ENSURE_REGISTERED (SdattackdsrOptionRerr);

TypeId SdattackdsrOptionRerr::GetTypeId ()
{
  static TypeId tid = TypeId ("ns3::sdattackdsr::SdattackdsrOptionRerr")
    .SetParent<SdattackdsrOptions> ()
    .SetGroupName ("Sdattackdsr")
    .AddConstructor<SdattackdsrOptionRerr> ()
  ;
  return tid;
}

SdattackdsrOptionRerr::SdattackdsrOptionRerr ()
{
  NS_LOG_FUNCTION_NOARGS ();
}

SdattackdsrOptionRerr::~SdattackdsrOptionRerr ()
{
  NS_LOG_FUNCTION_NOARGS ();
}

TypeId SdattackdsrOptionRerr::GetInstanceTypeId () const
{
  return GetTypeId ();
}

uint8_t SdattackdsrOptionRerr::GetOptionNumber () const
{
  NS_LOG_FUNCTION_NOARGS ();
  return OPT_NUMBER;
}

uint8_t SdattackdsrOptionRerr::Process (Ptr<Packet> packet, Ptr<Packet> sdattackdsrP, Ipv4Address ipv4Address, Ipv4Address source, Ipv4Header const& ipv4Header, uint8_t protocol, bool& isPromisc, Ipv4Address promiscSource)
{
  NS_LOG_FUNCTION (this << packet << sdattackdsrP << ipv4Address << source << ipv4Header << (uint32_t)protocol << isPromisc);
  Ptr<Packet> p = packet->Copy ();
  uint32_t size = p->GetSize ();
  uint8_t *data = new uint8_t[size];
  p->CopyData (data, size);
  uint8_t errorType = *(data + 2);
  /*
   * Get the node from Ip address and get the sdattackdsr extension object
   */
  Ptr<Node> node = GetNodeWithAddress (ipv4Address);
  Ptr<sdattackdsr::SdattackdsrRouting> sdattackdsr = node->GetObject<sdattackdsr::SdattackdsrRouting> ();
  /*
   * The error serialized size
   */
  uint32_t rerrSize;
  NS_LOG_DEBUG ("The error type value here " << (uint32_t)errorType);
  if (errorType == 1) // unreachable ip address
    {
      /*
       * Remove the route error header from the packet, and get the error type
       */
      SdattackdsrOptionRerrUnreachHeader rerrUnreach;
      p->RemoveHeader (rerrUnreach);
      /*
       * Get the error destination address
       */
      Ipv4Address unreachAddress = rerrUnreach.GetUnreachNode ();
      Ipv4Address errorSource = rerrUnreach.GetErrorSrc ();

      NS_LOG_DEBUG ("The error source is " <<  rerrUnreach.GetErrorDst () << "and the unreachable node is " << unreachAddress);
      /*
       * Get the serialized size of the rerr header
       */
      rerrSize = rerrUnreach.GetSerializedSize ();
      /*
       * Delete all the routes including the unreachable node address from the route cache
       */
      Ptr<Node> node = GetNodeWithAddress (ipv4Address);
      sdattackdsr->DeleteAllRoutesIncludeLink (errorSource, unreachAddress, ipv4Address);

      Ptr<Packet> newP = p->Copy ();
      uint32_t serialized = DoSendError (newP, rerrUnreach, rerrSize, ipv4Address, protocol);
      return serialized;
    }
  else
    {
      /*
       * Two other type of error headers:
       * 1. flow state not supported type-specific information
       * 2. unsupported option with option number
       */
      /*
       * Remove the route error header from the packet, and get the error type
       */
      SdattackdsrOptionRerrUnsupportHeader rerrUnsupport;
      p->RemoveHeader (rerrUnsupport);
      rerrSize = rerrUnsupport.GetSerializedSize ();

      NS_UNUSED (rerrSize);
      /// \todo This is for the other two error options, not supporting for now
      // uint32_t serialized = DoSendError (p, rerrUnsupport, rerrSize, ipv4Address, protocol);
      uint32_t serialized = 0;
      return serialized;
    }
}

uint8_t SdattackdsrOptionRerr::DoSendError (Ptr<Packet> p, SdattackdsrOptionRerrUnreachHeader &rerr, uint32_t rerrSize, Ipv4Address ipv4Address, uint8_t protocol)
{
  // Get the number of routers' address field
  uint8_t buf[2];
  p->CopyData (buf, sizeof(buf));
  uint8_t numberAddress = (buf[1] - 2) / 4;

  // Here remove the source route header and schedule next hop error transmission
  NS_LOG_DEBUG ("The number of addresses " << (uint32_t)numberAddress);
  SdattackdsrOptionSRHeader sourceRoute;
  sourceRoute.SetNumberAddress (numberAddress);
  p->RemoveHeader (sourceRoute);
  NS_ASSERT (p->GetSize () == 0);
  /*
   * Get the node from ip address and the sdattackdsr extension object
   */
  Ptr<Node> node = GetNodeWithAddress (ipv4Address);
  Ptr<sdattackdsr::SdattackdsrRouting> sdattackdsr = node->GetObject<sdattackdsr::SdattackdsrRouting> ();
  /*
   * Get the segments left field and the next address
   */
  uint8_t segmentsLeft = sourceRoute.GetSegmentsLeft ();
  uint8_t length = sourceRoute.GetLength ();
  uint8_t nextAddressIndex;
  Ipv4Address nextAddress;
  /*
   * Get the route size and the error target address
   */
  std::vector<Ipv4Address> nodeList = sourceRoute.GetNodesAddress ();
  Ipv4Address targetAddress = nodeList.back ();
  /*
   * The total serialized size for both the rerr and source route headers
   */
  uint32_t serializedSize = rerrSize + sourceRoute.GetSerializedSize ();

  if (length % 2 != 0)
    {
      NS_LOG_LOGIC ("Malformed header. Drop!");
      m_dropTrace (p);
      return 0;
    }

  if (segmentsLeft > numberAddress)
    {
      NS_LOG_LOGIC ("Malformed header. Drop!");
      m_dropTrace (p);
      return 0;
    }
  /*
   * When the error packet has reached to the destination
   */
  if (segmentsLeft == 0 && targetAddress == ipv4Address)
    {
      NS_LOG_INFO ("This is the destination of the error, send error request");
      sdattackdsr->SendErrorRequest (rerr, protocol);
      return serializedSize;
    }

  // Get the next Router Address
  SdattackdsrOptionSRHeader newSourceRoute;
  newSourceRoute.SetSegmentsLeft (segmentsLeft - 1);
  nextAddressIndex = numberAddress - segmentsLeft;
  nextAddress = sourceRoute.GetNodeAddress (nextAddressIndex);
  newSourceRoute.SetSalvage (sourceRoute.GetSalvage ());
  newSourceRoute.SetNodesAddress (nodeList);
  nextAddress = newSourceRoute.GetNodeAddress (nextAddressIndex);

  /// to test if the next address is multicast or not
  if (nextAddress.IsMulticast () || targetAddress.IsMulticast ())
    {
      m_dropTrace (p);
      return serializedSize;
    }

  // Set the route entry
  SetRoute (nextAddress, ipv4Address);
  sdattackdsr->ForwardErrPacket (rerr, newSourceRoute, nextAddress, protocol, m_ipv4Route);
  return serializedSize;
}

NS_OBJECT_ENSURE_REGISTERED (SdattackdsrOptionAckReq);

TypeId SdattackdsrOptionAckReq::GetTypeId ()
{
  static TypeId tid = TypeId ("ns3::sdattackdsr::SdattackdsrOptionAckReq")
    .SetParent<SdattackdsrOptions> ()
    .SetGroupName ("Sdattackdsr")
    .AddConstructor<SdattackdsrOptionAckReq> ()
  ;
  return tid;
}

SdattackdsrOptionAckReq::SdattackdsrOptionAckReq ()
{
  NS_LOG_FUNCTION_NOARGS ();
}

SdattackdsrOptionAckReq::~SdattackdsrOptionAckReq ()
{
  NS_LOG_FUNCTION_NOARGS ();
}

TypeId SdattackdsrOptionAckReq::GetInstanceTypeId () const
{
  return GetTypeId ();
}

uint8_t SdattackdsrOptionAckReq::GetOptionNumber () const
{
  NS_LOG_FUNCTION_NOARGS ();
  return OPT_NUMBER;
}

uint8_t SdattackdsrOptionAckReq::Process (Ptr<Packet> packet, Ptr<Packet> sdattackdsrP, Ipv4Address ipv4Address, Ipv4Address source, Ipv4Header const& ipv4Header, uint8_t protocol, bool& isPromisc, Ipv4Address promiscSource)
{
  NS_LOG_FUNCTION (this << packet << sdattackdsrP << ipv4Address << source << ipv4Header << (uint32_t)protocol << isPromisc);
  /*
   * Current implementation of the ack request header processing is coded in source route header processing
   */
  /*
   * Remove the ack request header
   */
  Ptr<Packet> p = packet->Copy ();
  SdattackdsrOptionAckReqHeader ackReq;
  p->RemoveHeader (ackReq);
  /*
   * Get the node with ip address and get the sdattackdsr extension and reoute cache objects
   */
  Ptr<Node> node = GetNodeWithAddress (ipv4Address);
  Ptr<sdattackdsr::SdattackdsrRouting> sdattackdsr = node->GetObject<sdattackdsr::SdattackdsrRouting> ();

  NS_LOG_DEBUG ("The next header value " << (uint32_t)protocol);

  return ackReq.GetSerializedSize ();
}

NS_OBJECT_ENSURE_REGISTERED (SdattackdsrOptionAck);

TypeId SdattackdsrOptionAck::GetTypeId ()
{
  static TypeId tid = TypeId ("ns3::sdattackdsr::SdattackdsrOptionAck")
    .SetParent<SdattackdsrOptions> ()
    .SetGroupName ("Sdattackdsr")
    .AddConstructor<SdattackdsrOptionAck> ()
  ;
  return tid;
}

SdattackdsrOptionAck::SdattackdsrOptionAck ()
{
  NS_LOG_FUNCTION_NOARGS ();
}

SdattackdsrOptionAck::~SdattackdsrOptionAck ()
{
  NS_LOG_FUNCTION_NOARGS ();
}

TypeId SdattackdsrOptionAck::GetInstanceTypeId () const
{
  return GetTypeId ();
}

uint8_t SdattackdsrOptionAck::GetOptionNumber () const
{
  NS_LOG_FUNCTION_NOARGS ();
  return OPT_NUMBER;
}

uint8_t SdattackdsrOptionAck::Process (Ptr<Packet> packet, Ptr<Packet> sdattackdsrP, Ipv4Address ipv4Address, Ipv4Address source, Ipv4Header const& ipv4Header, uint8_t protocol, bool& isPromisc, Ipv4Address promiscSource)
{
  ackcount++;
  printf("ackcount=%d\n",ackcount );
  NS_LOG_FUNCTION (this << packet << sdattackdsrP << ipv4Address << source << ipv4Header << (uint32_t)protocol << isPromisc);
  /*
   * Remove the ACK header
   */
  Ptr<Packet> p = packet->Copy ();
  SdattackdsrOptionAckHeader ack;
  p->RemoveHeader (ack);
  /*
   * Get the ACK source and destination address
   */
  Ipv4Address realSrc = ack.GetRealSrc ();
  Ipv4Address realDst = ack.GetRealDst ();
  uint16_t ackId = ack.GetAckId ();
  /*
   * Get the node with ip address and get the sdattackdsr extension and route cache objects
   */
  Ptr<Node> node = GetNodeWithAddress (ipv4Address);
  Ptr<sdattackdsr::SdattackdsrRouting> sdattackdsr = node->GetObject<sdattackdsr::SdattackdsrRouting> ();
  sdattackdsr->UpdateRouteEntry (realDst);
  /*
   * Cancel the packet retransmit timer when receiving the ack packet
   */
  sdattackdsr->CallCancelPacketTimer (ackId, ipv4Header, realSrc, realDst);
  return ack.GetSerializedSize ();
}

} // namespace sdattackdsr
} // namespace ns3
