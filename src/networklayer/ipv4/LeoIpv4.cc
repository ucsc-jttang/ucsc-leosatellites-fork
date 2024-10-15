//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
// 
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/.
// 
#include "LeoIpv4.h"

namespace inet {

Define_Module(LeoIpv4);

LeoIpv4::LeoIpv4()
{
}

LeoIpv4::~LeoIpv4()
{
//    for (auto it : nextHops)
//        delete it.second;
}

void LeoIpv4::initialize(int stage)
{
    Ipv4::initialize(stage);
    if (stage == INITSTAGE_LOCAL) {
        WATCH_MAP(nextHops);
        WATCH_MAP(nextHopsStr);
        const char* parentNodeName = this->getParentModule()->getParentModule()->getFullName();
        std::hash<std::string> hasher;
        auto hashed = hasher(parentNodeName);
        rng.seed(hashed);
    }
}

void LeoIpv4::addKNextHop(int k, uint32_t destinationAddr, uint32_t nextInterfaceID){
    kNextHops[k][destinationAddr] = nextInterfaceID;
}

void LeoIpv4::addNextHop(uint32_t destinationAddr, uint32_t nextInterfaceID){
    nextHops[destinationAddr] = nextInterfaceID;
}

void LeoIpv4::addNextHopStr(std::string destinationAddr, std::string nextInterfaceID){
    nextHopsStr[destinationAddr] = nextInterfaceID;
}

void LeoIpv4::clearNextHops(int shellIndex=0){
//    if(shellIndex == 0){
//        kNextHops.clear();
//    } else {
        kNextHops[shellIndex].clear();
//    }
    nextHopsStr.clear();
    nextHops.clear();
}

int LeoIpv4::getShellIndex(){
    int rows = sizeof kNextHops / sizeof kNextHops[0];
    if (rows > 1) {
        return rows;
    }
    return -1;
}

void LeoIpv4::routeUnicastPacket(Packet *packet)
{
    //std::cout << "Routing Unicast Packet: " << packet->str() << endl;
    const NetworkInterface *fromIE = getSourceInterface(packet);
    const NetworkInterface *destIE = getDestInterface(packet);
    bool hopFound = false;
    // Initial Syn packet does not have either source or dest interface set
    Ipv4Address nextHopAddress = getNextHop(packet);

    const auto& ipv4Header = packet->peekAtFront<Ipv4Header>();
    Ipv4Address destAddr = ipv4Header->getDestAddress();
    EV_INFO << "Routing " << packet << " with destination = " << destAddr << ", ";
    //std::cout << "Routing " << packet << " with destination = " << destAddr << ", ";
    // if output port was explicitly requested, use that, otherwise use Ipv4 routing
    if (destIE) {
        EV_DETAIL << "using manually specified output interface " << destIE->getInterfaceName() << "\n";
        // and nextHopAddr remains unspecified
        //if (!nextHopAddress.isUnspecified()) {
            // do nothing, next hop address already specified
        //}
//        // special case ICMP reply
//        else if (destIE->isBroadcast()) {
//            // if the interface is broadcast we must search the next hop
//            const Ipv4Route *re = rt->findBestMatchingRoute(destAddr);
//            int interfaceID = nextHops[destAddr.getInt()];
//            if (interfaceID && interfaceID == destIE->getInterfaceId()) {
//                packet->addTagIfAbsent<NextHopAddressReq>()->setNextHopAddress(re->getGateway());
//            }
//        }
    }
    else {
        // use Ipv4 routing (lookup in routing table)
//        std::cout << "\nFinding best matching route for: " << destAddr.str() << endl;
        //const Ipv4Route *re = rt->findBestMatchingRoute(destAddr);
        int shell = -1;
        auto it = kNextHops.begin();
        //Currently using rng seeded with unique groundstation name
        if(kNextHops.size() > 1){
            shell = rng()%kNextHops.size();
        } else {
            shell = it->first;
        }

        int interfaceID = kNextHops[shell][destAddr.getInt()];

//        int interfaceID = kNextHops[1][destAddr.getInt()];

        int testinterfaceID0 = kNextHops[0][destAddr.getInt()];
        int testinterfaceID1 = kNextHops[1][destAddr.getInt()];
        // Simulator Limitation:shell0 interfaces cannot be reached by shell1 interfaces
        //if you are returning a packet to sender, you must send it back on the same shell path
        if (!interfaceID ){
             for(auto it = kNextHops.begin(); it != kNextHops.end(); ++it){
                 auto i = std::distance(kNextHops.begin(), it);
                 int potentialInterfaceId = kNextHops[i][destAddr.getInt()];
                 if(potentialInterfaceId){
                     interfaceID=potentialInterfaceId;
                     break;
                 }
             }
        }

        if (interfaceID) {
            //std::cout << "Adding route with the following dest ID: " << interfaceID << endl;
            //std::cout << "Gateway: " << re->getGateway() << endl;
            packet->addTagIfAbsent<InterfaceReq>()->setInterfaceId(interfaceID);
            hopFound = true;
            //packet->addTagIfAbsent<NextHopAddressReq>()->setNextHopAddress(re->getGateway());
        }
        else{
            std::cout << "\ndestAddr: " << destAddr.str() << endl;
            std::cout << "\ndestAddr (int): " << destAddr.getInt() << endl;
            std::cout << "\nInterface ID not found!: ID " << interfaceID << " at time: " << simTime() << endl;

        }
    }

    if (!hopFound) {    // no route found
        EV_WARN << "unroutable, sending ICMP_DESTINATION_UNREACHABLE, dropping packet\n";
        std::cout << "unroutable, sending ICMP_DESTINATION_UNREACHABLE, dropping packet\n";
        numUnroutable++;
        PacketDropDetails details;
        details.setReason(NO_ROUTE_FOUND);
        emit(packetDroppedSignal, packet, &details);
        sendIcmpError(packet, fromIE ? fromIE->getInterfaceId() : -1, ICMP_DESTINATION_UNREACHABLE, 0);
    }
    else {    // fragment and send
        //std::cout << "\n Packet being routed!!" << endl;
        if (fromIE != nullptr) {
            if (datagramForwardHook(packet) != INetfilter::IHook::ACCEPT)
                return;
        }

        routeUnicastPacketFinish(packet);
    }
}

void LeoIpv4::stop()
{
    Ipv4::stop();
    nextHops.clear();
}
}
