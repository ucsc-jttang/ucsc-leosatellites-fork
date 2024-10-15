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
#include<fstream>
#include <sstream>
#include<iostream>
#include <filesystem>
#include "LeoIpv4NetworkConfigurator.h"
namespace inet {
Define_Module(LeoIpv4NetworkConfigurator);

LeoIpv4NetworkConfigurator::~LeoIpv4NetworkConfigurator()
{
    nodeModules.clear();
    igraph_vector_int_destroy(&islVec); // @suppress("Function cannot be resolved")
}
void LeoIpv4NetworkConfigurator::initialize(int stage)
{
    if(stage == INITSTAGE_LOCAL){
        networkName = getParentModule()->getName();
        shellIndex = getAncestorPar("shellIndex");
        loadFiles = par("loadFiles");
        minLinkWeight = par("minLinkWeight");
        configureIsolatedNetworksSeparatly = par("configureIsolatedNetworksSeparatly").boolValue();
        numOfSats = getAncestorPar("numOfSats");
        numOfGS = getAncestorPar("numOfGS");
        satPerPlane = getAncestorPar("satsPerPlane");
        unsigned int planes = getAncestorPar("numOfPlanes");
        numOfPlanes = (int)std::ceil(((double)numOfSats/((double)planes*(double)satPerPlane))*(double)planes); //depending on number of satellites, all planes may not be filled
        enableInterSatelliteLinks = par("enableInterSatelliteLinks");
        if (enableInterSatelliteLinks){
            numOfISLs = numOfSats*2; // two per sat (Grid +)
        }
        else {
            numOfISLs = 0;
        }
        assignIDtoModules();
        numOfKPaths = par("numOfKPaths");
        currentInterval = 0;
        double altitude = getAncestorPar("alt");
        double inclination = getAncestorPar("incl");
        std::string altitudeStr = std::to_string(altitude);
        std::string inclinationStr = std::to_string(inclination);
        altitudeStr.erase ( altitudeStr.find_last_not_of('0'), std::string::npos );
        inclinationStr.erase ( inclinationStr.find_last_not_of('0') + 1, std::string::npos );
        filePrefix =  altitudeStr + "." + std::to_string(planes) + "." + std::to_string(satPerPlane) + "." + inclinationStr;
     }
}

void LeoIpv4NetworkConfigurator::loadConfiguration(simtime_t currentInterval)
{
    if(std::filesystem::is_directory(filePrefix) || std::filesystem::exists(filePrefix)) {
        std::string fName = filePrefix + "/" + currentInterval.str() + ".txt";
        std::ifstream file(fName);
        if (file.is_open()) {
            std::string line;
            while (std::getline(file, line)) {
                std::vector<int> array;
                std::stringstream ss(line);
                int temp;
                while (ss >> temp)
                    array.push_back(temp);
                cModule *nodeMod = nodeModules.find(array[0])->second;
                LeoIpv4* ipv4Mod = dynamic_cast<LeoIpv4 *>(nodeMod->getModuleByPath(".ipv4.ip"));
                ipv4Mod->addKNextHop(1, array[1], array[2]);
                ipv4Mod->addNextHop(array[1],array[2]);
            }
            file.close();
        }
    }
}

void LeoIpv4NetworkConfigurator::assignIDtoModules()
{
    for(int nodeNum = 0; nodeNum < numOfSats+numOfGS; nodeNum++){
        nodeModules[nodeNum] = getNodeModule(nodeNum);
        std::cout << getNodeModule(nodeNum)->getFullPath() << endl;
    }
//    const char* name1 = getNodeModule(625)->getFullName();
//    const char* name2 = getNodeModule(626)->getFullName();
//    const char* namen = getNodeModule(6)->getFullName();
//    std::string pathn = getNodeModule(6)->getFullPath();
//    std::string path1 = getNodeModule(625)->getFullPath();
    std::string path2 = getNodeModule(626)->getFullPath();

}

void LeoIpv4NetworkConfigurator::addToISLMobilityMap(SatelliteMobility* source, SatelliteMobility* destination)
{
    satelliteISLMobilityModules[source].push_back(source);
}

void LeoIpv4NetworkConfigurator::updateForwardingStates(simtime_t currentInterval)
{
    if(loadFiles){
        loadConfiguration(currentInterval);
    }
    else{
        generateTopologyGraph(currentInterval);
    }

}

void LeoIpv4NetworkConfigurator::establishInitialISLs()
{


    if (numOfISLs == 0) {
        fillNextHopInterfaceMap();
        igraph_vector_int_init(&islVec, 0);
    }
    else {

//        std::ofstream outfile;
//        outfile.open("sourceISLMods.txt",std::ios_base::app);
        int i = 0;

        //igraph_vector_int_init(&islVec, numOfISLs*2); //two vecs needed for each ISL
        igraph_vector_int_init(&islVec, 0);
        unsigned int islVecIterator = 0;
        for(int planeNum = 0; planeNum < numOfPlanes; planeNum++){
            unsigned int numOfSatsInPlane =  planeNum*satPerPlane+satPerPlane;
            if(numOfSats < numOfSatsInPlane){
                numOfSatsInPlane = numOfSats;
            }
            for(unsigned int satNum = planeNum*satPerPlane; satNum < numOfSatsInPlane; satNum++){
                cModule *satMod = nodeModules.find(satNum)->second; // get source satellite module
                SatelliteMobility* sourceSatMobility = dynamic_cast<SatelliteMobility*>(satMod->getSubmodule("mobility"));
                int destSatNumA = (satNum+1)%(satPerPlane*(planeNum+1));
                if(destSatNumA == 0){
                    destSatNumA = planeNum*satPerPlane; //If number is zero, must be start of orbital plane
                }
                if(destSatNumA < numOfSats){
                    cModule *destModA = nodeModules.find(destSatNumA)->second;
                    //VECTOR(islVec)[islVecIterator] = satNum; VECTOR(islVec)[islVecIterator+1] = destSatNumA;
                    igraph_vector_int_push_back(&islVec, satNum);
                    igraph_vector_int_push_back(&islVec, destSatNumA);

                    SatelliteMobility* destSatMobility = dynamic_cast<SatelliteMobility*>(destModA->getModuleByPath(".mobility"));
                    satelliteISLMobilityModules[sourceSatMobility].push_back(destSatMobility);
//                    outfile << "i=" << i << ":" << sourceSatMobility->getFullPath() << destSatMobility->getFullPath() <<  endl;
                    i++;

                    islVecIterator = islVecIterator + 2;
    //                for(int i = 0; i < satMod->gateSize("pppg$o"); i++){  //check each possible pppg gate
    //                    cGate* srcGate = satMod->gate("pppg$o", i);
    //                    if(srcGate->isConnected()){
    //                        cChannel *chan = srcGate->getChannel();
    //                        cModule* destModule = srcGate->getPathEndGate()->getOwnerModule()->getParentModule()->getParentModule();
    //                        //std::cout << "\n" << destModA->getFullName() << " - " << destModule->getFullName() << endl;
    //                        if(destModA == destModule){
    //                            std::string mobilityName = destModA->getModuleByPath(".mobility")->getNedTypeName();
    //                            double distance = 0;
    //                            if(mobilityName == "leosatellites.mobility.SatelliteMobility"){
    //                                SatelliteMobility* destSatMobility = dynamic_cast<SatelliteMobility*>(destModA->getModuleByPath(".mobility"));
    //                                satelliteISLMobilityModules[sourceSatMobility].push_back(destSatMobility);
    //                            }
    //                        }
    //                    }
    //                }
                }
//                outfile << endl;
                int destSatNumB = (satNum + satPerPlane);// % totalSats;
                if(destSatNumB < numOfSats){
                    cModule *destModB = nodeModules.find(destSatNumB)->second;
                    //VECTOR(islVec)[islVecIterator] = satNum; VECTOR(islVec)[islVecIterator+1] = destSatNumB;
                    igraph_vector_int_push_back(&islVec, satNum);
                    igraph_vector_int_push_back(&islVec, destSatNumB);

                    SatelliteMobility* destSatMobility = dynamic_cast<SatelliteMobility*>(destModB->getModuleByPath(".mobility"));
                    satelliteISLMobilityModules[sourceSatMobility].push_back(destSatMobility);
//                    outfile << "i=" << i << ":" << sourceSatMobility->getFullPath() << destSatMobility->getFullPath() <<  endl;
                    //islVecIterator = islVecIterator + 2;
    //                for(int i = 0; i < satMod->gateSize("pppg$o"); i++){  //check each possible pppg gate
    //                    cGate* srcGate = satMod->gate("pppg$o", i);
    //                    if(srcGate->isConnected()){
    //                        cChannel *chan = srcGate->getChannel();
    //                        cModule* destModule = srcGate->getPathEndGate()->getOwnerModule()->getParentModule()->getParentModule();
    //                        if(destModB == destModule){
    //                            std::string mobilityName = destModB->getModuleByPath(".mobility")->getNedTypeName();
    //                            double distance = 0;
    //                            if(mobilityName == "leosatellites.mobility.SatelliteMobility"){
    //                                SatelliteMobility* destSatMobility = dynamic_cast<SatelliteMobility*>(destModB->getModuleByPath(".mobility"));
    //                                satelliteISLMobilityModules[sourceSatMobility].push_back(destSatMobility);
    //                            }
    //                        }
    //                    }
    //                }
                }
            }
        }
//        outfile << endl;
        fillNextHopInterfaceMap();
    }
}

void LeoIpv4NetworkConfigurator::generateTopologyGraph(simtime_t currentInterval)
{
    if (!std::filesystem::is_directory(filePrefix) || !std::filesystem::exists(filePrefix)) {
        std::filesystem::create_directory(filePrefix);
    }

//    std::ofstream fout;
//    std::string fName = filePrefix + "/" + currentInterval.str() + ".txt";
//    fout.open(fName, std::ios_base::app);

    for (int nodeNum = 0; nodeNum < numOfSats+numOfGS; nodeNum++) {
        cModule* mod = nodeModules.find(nodeNum)->second;
        dynamic_cast<LeoIpv4 *>(mod->getModuleByPath(".ipv4.ip"))->clearNextHops(shellIndex);
    }
    std::chrono::high_resolution_clock::time_point fillVectorStartTime = std::chrono::high_resolution_clock::now();
    std::chrono::high_resolution_clock::time_point shortestPathEndTime, fillVectorEndTime;

    igraph_t constellationTopology;
    igraph_vector_int_t shortestPathVertexVec, shortestPathEdgesVec;
    igraph_vector_int_t islVecCopy;
    igraph_vector_int_init_copy(&islVecCopy, &islVec);
    //std::cout << "\nOG ISL VEC SIZE: " << igraph_vector_int_size(&islVec) << endl;
    igraph_vector_t weightsVec;
    igraph_vector_init(&weightsVec, 0);
    unsigned int islVecIterator = 0;
    unsigned int weightsVecIterator = 0;
//    for (auto iter = cbegin(satelliteISLMobilityModules); iter != cend(satelliteISLMobilityModules); ++iter) {

    for (int i = 0; i < nodeModules.size() - numOfGS; i++) {
        cModule* satMod = nodeModules.find(i)->second;
        SatelliteMobility* satMobility = dynamic_cast<SatelliteMobility*>(satMod->getSubmodule("mobility"));
        std::vector<SatelliteMobility*> mobVec = satelliteISLMobilityModules.find(satMobility)->second;


        for (SatelliteMobility* iter2 : mobVec) {
            //if(iter->first != iter2){
                //std::cout << "\nSOURCE SAT: " << iter->first->getParentModule()->getFullName() << "\nDEST SAT: " << iter2->getParentModule()->getFullName();
                double distance = satMobility->getDistance(iter2->getLatitude(), iter2->getLongitude(), iter2->getAltitude())*1000;
                double weight = (distance/299792458)*1000;
                std::string sourceName = satMobility->getFullPath();
                std::string destName = iter2->getFullPath();
                //VECTOR(weightsVec)[weightsVecIterator] = weight;
                igraph_vector_push_back(&weightsVec, weight);
//                if(weightsVecIterator == 2){
//                    std::cout << satMobility->getLatitude() << "," << satMobility->getLongitude() << "," << satMobility->getAltitude() << endl;
//                    std::cout << iter2->getLatitude() << "," << iter2->getLongitude() << "," << iter2->getAltitude() << endl;
//                }
                weightsVecIterator++;
                islVecIterator = islVecIterator + 2;
            //}
        }
    }
//    std::ofstream outfile3;
//    outfile3.open("weightsVec.txt",std::ios_base::app);
//    for (int i = 0; i < igraph_vector_size(&weightsVec); i++) {
//        outfile3 << "i=" << igraph_vector_get(&weightsVec, i) <<  endl;
//    }
//    outfile3 << endl;

//    What is this??
//    Check here for usage of ISLVec and gsTup. GsTup
    //std::cout << "\nOG WEIGHTS VEC SIZE: " << igraph_vector_size(&weightsVec) << endl;
    int numberOfGSLinks = groundStationLinks.size();
    for(int i = 0; i < numberOfGSLinks; i++){ //TODO USE ATTRIBUTE NAMES TO
        std::tuple<int, int, double> gsTup = groundStationLinks.front();
        groundStationLinks.pop();
        //VECTOR(islVecCopy)[islVecIterator] = std::get<0>(gsTup); VECTOR(islVecCopy)[islVecIterator+1] = std::get<1>(gsTup);
        igraph_vector_int_push_back(&islVecCopy, std::get<0>(gsTup));
        igraph_vector_int_push_back(&islVecCopy, std::get<1>(gsTup));
        //VECTOR(weightsVec)[weightsVecIterator] = std::get<2>(gsTup);
        igraph_vector_push_back(&weightsVec, std::get<2>(gsTup));
        islVecIterator = islVecIterator + 2;
        weightsVecIterator++;
    }

//    std::ofstream outfile4;
//    outfile4.open("weightsVec2.txt",std::ios_base::app);
//    for (int i = 0; i < igraph_vector_size(&weightsVec); i++) {
//        outfile4 << "i=" << igraph_vector_get(&weightsVec, i) <<  endl;
//    }
//    outfile4 << endl;

    //igraph_vector_int_resize(&islVecCopy, weightsVecIterator*2);
    //igraph_vector_resize(&weightsVec, weightsVecIterator);

    //std::cout << "\nISL VEC SIZE: " << igraph_vector_int_size(&islVecCopy) << endl;
    //std::cout << "\nWEIGHT VEC SIZE: " << igraph_vector_size(&weightsVec) << endl;
    igraph_empty(&constellationTopology, numOfSats+numOfGS, IGRAPH_UNDIRECTED);

    igraph_add_edges(&constellationTopology, &islVecCopy, 0);

    igraph_vector_int_init(&shortestPathVertexVec, 1);
    igraph_vector_int_init(&shortestPathEdgesVec, 1);
    fillVectorEndTime = std::chrono::high_resolution_clock::now();

    igraph_vector_int_list_t vertexPaths;
    igraph_vector_int_list_t edgePaths;

    igraph_vector_int_list_init(&vertexPaths, 1);
    igraph_vector_int_list_init(&edgePaths, 1);




//    std::ofstream outfile2;
//    outfile2.open("locations.txt",std::ios_base::app);
//    for(int nodeNum = 0; nodeNum < nodeModules.size()-2; nodeNum++){
//        cModule *satMod = nodeModules.find(nodeNum)->second;
//        SatelliteMobility* satMobility = dynamic_cast<SatelliteMobility*>(satMod->getSubmodule("mobility"));
//        double latitude = satMobility->getLatitude();
//        double longitude = satMobility->getLongitude();
//        double altitude = satMobility->getAltitude();
//        outfile2 << "i=" << nodeNum << ":" << latitude << "," << longitude << "," << altitude << endl;
//    }
//    outfile2 << endl;



    if(numOfKPaths <= 1){
        std::chrono::high_resolution_clock::time_point shortestPathStartTime = std::chrono::high_resolution_clock::now();
        int fmDuration = 0;
        int rDuration = 0;
        for(int sourceNodeNum = 0; sourceNodeNum <  numOfSats+numOfGS; sourceNodeNum++){
            igraph_get_all_shortest_paths_dijkstra(&constellationTopology, &vertexPaths, &edgePaths, /*nrgeo=*/ &shortestPathVertexVec, /*from=*/ sourceNodeNum, /*to=*/ igraph_vss_all(), /*weights=*/ &weightsVec, /*mode=*/ IGRAPH_ALL);
            cModule *sourceMod = nodeModules.find(sourceNodeNum)->second;
            LeoIpv4* srcIpv4Mod = dynamic_cast<LeoIpv4 *>(sourceMod->getModuleByPath(".ipv4.ip"));
            igraph_vector_int_t *path;
            for (int i = 0; i < igraph_vector_int_list_size(&vertexPaths); i++) {
//                if(i == 432){
//                    int ugh = 0;
//                }
                path = igraph_vector_int_list_get_ptr(&vertexPaths, i);
                int sourceNodeNum = igraph_vector_int_get(path, 0);
                int nextHopNodeNum = igraph_vector_int_get(path, 1);
                int destinationNodeNum = igraph_vector_int_get(path, igraph_vector_int_size(path)-1);
                //std::cout << "\nSOURCE NUM: " << sourceNodeNum << endl;
                cModule *nextHopMod = nodeModules.find(nextHopNodeNum)->second;
                cModule *destMod = nodeModules.find(destinationNodeNum)->second;
                //std::cout << "\nDEST NUM: " << nextHopMod->getFullName() << endl;
                //std::cout << "\nNEXT HOP NUM: " << destMod->getFullName() << endl;
                if(sourceNodeNum != destinationNodeNum){
                    int nextHopID = nextHopInterfaceMap.find(sourceMod)->second.find(nextHopMod)->second;
//                    if(sourceNodeNum == 432 && destinationNodeNum == 626){
//                        //This is outputting gs0 as next hop for 432 to 626
//                        std::cout << "\n NEXT HOP ID FOUND FOR " << sourceMod->getFullName() << "NextHop is" << nextHopMod->getFullName() << "For dest: " << destMod->getFullName()<< endl;
//                    }
                    IInterfaceTable* destIft = dynamic_cast<IInterfaceTable*>(destMod->getSubmodule("interfaceTable"));
                    for (size_t j = 0; j < destIft->getNumInterfaces(); j++) {
                        NetworkInterface *destinationIE = destIft->getInterface(j);
                        if (!destinationIE->isLoopback()){
                            srcIpv4Mod->addNextHop(destinationIE->getIpv4Address().getInt(),nextHopID);
                            std::string str1 = destMod->getFullName();
                            std::string str2 = nextHopMod->getFullName() + std::to_string(nextHopID);
                            //str2 = str2 + std::string(nextHopID);
                            srcIpv4Mod->addNextHopStr(str1, str2);
//                            if(shellIndex == 1 && sourceNodeNum >= numOfSats){
//                                int fuckme = 0;
//                            }
                            //10.1.40.107
                            Ipv4Address gs1 = Ipv4Address(10,1,40,107);

                            srcIpv4Mod->addKNextHop(shellIndex, destinationIE->getIpv4Address().getInt(), nextHopID);
                            if(shellIndex == 1 && sourceNodeNum == 432){
                                int nodenuming = 0;
                                std::string dest1 = destinationIE->getIpv4Address().str();
                                std::string gs1str = gs1.str();
                            }
//                          Where am I sending using 432?
                            if(shellIndex == 0 && sourceNodeNum == 432 && destinationIE->getIpv4Address().str() == gs1.str()){
                                int here = 0;
                            }
                            if(shellIndex == 1 && sourceNodeNum == 432 && destinationIE->getIpv4Address().str() == gs1.str()){
                                int here = 0;
                            }

//                            fout << sourceNodeNum << " " << destinationIE->getIpv4Address().getInt() << " " << nextHopID << "\n";
                        }
                    }
                }
            }
            igraph_vector_int_destroy(path);
            igraph_vector_int_list_clear(&vertexPaths);
            igraph_vector_int_list_clear(&edgePaths);
        }
    }
    else{
        for(int sourceNodeNum = 0; sourceNodeNum <  numOfSats+numOfGS; sourceNodeNum++){
            cModule *sourceMod = nodeModules.find(sourceNodeNum)->second;
            LeoIpv4* srcIpv4Mod = dynamic_cast<LeoIpv4 *>(sourceMod->getModuleByPath(".ipv4.ip"));
            for(int destNodeNum = 0; destNodeNum <  numOfSats+numOfGS; destNodeNum++){
                if(sourceNodeNum != destNodeNum){
                    igraph_get_k_shortest_paths(&constellationTopology, &weightsVec, &vertexPaths, &edgePaths, numOfKPaths, sourceNodeNum, destNodeNum, IGRAPH_ALL);
                    igraph_vector_int_t *path;
                    for (int i = 0; i < igraph_vector_int_list_size(&vertexPaths); i++) {
                        path = igraph_vector_int_list_get_ptr(&vertexPaths, i);
                        int sourceNodeNum = igraph_vector_int_get(path, 0);
                        int nextHopNodeNum = igraph_vector_int_get(path, 1);
                        int destinationNodeNum = igraph_vector_int_get(path, igraph_vector_int_size(path)-1);
                        cModule *nextHopMod = nodeModules.find(nextHopNodeNum)->second;
                        cModule *destMod = nodeModules.find(destinationNodeNum)->second;
                        if(sourceNodeNum != destinationNodeNum){
                            int nextHopID = nextHopInterfaceMap.find(sourceMod)->second.find(nextHopMod)->second;
                            IInterfaceTable* destIft = dynamic_cast<IInterfaceTable*>(destMod->getSubmodule("interfaceTable"));
                            for (size_t j = 0; j < destIft->getNumInterfaces(); j++) {
                                NetworkInterface *destinationIE = destIft->getInterface(j);
                                if (!destinationIE->isLoopback()){
                                    srcIpv4Mod->addNextHop(destinationIE->getIpv4Address().getInt(),nextHopID);
                                    srcIpv4Mod->addKNextHop(i+1, destinationIE->getIpv4Address().getInt(), nextHopID);
//                                    fout << sourceNodeNum << " " << destinationIE->getIpv4Address().getInt() << " " << nextHopID << "\n";
                                }
                            }
                        }
                    }
                    igraph_vector_int_destroy(path);
                    igraph_vector_int_list_clear(&vertexPaths);
                    igraph_vector_int_list_clear(&edgePaths);
                }
            }
        }
    }
    igraph_vector_int_list_destroy(&vertexPaths);
    igraph_vector_int_list_destroy(&edgePaths);

    while(!groundStationLinks.empty()) groundStationLinks.pop();
    igraph_destroy(&constellationTopology);
    igraph_vector_int_destroy(&islVecCopy);
    igraph_vector_destroy(&weightsVec);
    igraph_vector_int_destroy(&shortestPathVertexVec);
    igraph_vector_int_destroy(&shortestPathEdgesVec);
//    fout.close();
}
void LeoIpv4NetworkConfigurator::addNextHopInterface(cModule* source, cModule* destination, int interfaceID)
{
        nextHopInterfaceMap[source][destination] = interfaceID;

}

void LeoIpv4NetworkConfigurator::removeNextHopInterface(cModule* source, cModule* destination)
{
    nextHopInterfaceMap[source].erase(destination);
}

void LeoIpv4NetworkConfigurator::addGSLinktoTopologyGraph(int gsNum, int destNum, double weight)
{
    groundStationLinks.push(std::make_tuple(gsNum+numOfSats, destNum, weight));
    //groundStation[0] = index+numOfSats to get the node number. E.g. satellite[499] = 499, groundStation[0] = 500
}

cModule* LeoIpv4NetworkConfigurator::getNodeModule(int nodeNumber)
{
    cModule* mod;
    if(nodeNumber < numOfSats){
        std::string nodeName = std::string(networkName + "["+std::to_string(shellIndex)+"].satellite[" + std::to_string(nodeNumber) + "]");
        mod = getModuleByPath(nodeName.c_str());
    }
    else{
        std::string nodeName = std::string(networkName + "[0].groundStation[" + std::to_string(nodeNumber-numOfSats) + "]");
        mod = getModuleByPath(nodeName.c_str());
    }
    return mod;
}
int LeoIpv4NetworkConfigurator::getNextHopInterfaceID(cModule* sourceSatellite, cModule* nextHopSatellite)
{
    int interfaceID = -1;
    if(sourceSatellite == nextHopSatellite){
        return interfaceID;
    }

    IInterfaceTable* sourceIft = dynamic_cast<IInterfaceTable*>(sourceSatellite->getSubmodule("interfaceTable"));
    IInterfaceTable* nextHopIft = dynamic_cast<IInterfaceTable*>(nextHopSatellite->getSubmodule("interfaceTable"));

    for (int i = 0; i < sourceIft->getNumInterfaces(); i++) {
        NetworkInterface *srcIE = sourceIft->getInterface(i);
        if (!(srcIE->isPointToPoint()))
            continue;
        cGate* srcGateMod = sourceSatellite->gate(srcIE->getNodeOutputGateId());
        for (int j = 0; j < nextHopIft->getNumInterfaces(); j++) {
            NetworkInterface *nextHopIE = nextHopIft->getInterface(j);
            if (!(nextHopIE->isPointToPoint()))
                continue;
            cGate* nextHopGateMod = nextHopSatellite->gate(nextHopIE->getNodeInputGateId());
            if(srcGateMod->getPathEndGate() == nextHopGateMod->getPathEndGate()){
                return srcIE->getInterfaceId();
            }
        }
    }
    return interfaceID;
}

void LeoIpv4NetworkConfigurator::fillNextHopInterfaceMap()
{
    for(int nodeNum = 0; nodeNum < nodeModules.size(); nodeNum++){
        cModule *mod = nodeModules.find(nodeNum)->second;
        for(int nextHopNodeNum = 0; nextHopNodeNum < nodeModules.size(); nextHopNodeNum++){
            if(nodeNum != nextHopNodeNum){
                cModule *nextHopMod = nodeModules.find(nextHopNodeNum)->second;
                IInterfaceTable* sourceIft = dynamic_cast<IInterfaceTable*>(mod->getSubmodule("interfaceTable"));
                IInterfaceTable* nextHopIft = dynamic_cast<IInterfaceTable*>(nextHopMod->getSubmodule("interfaceTable"));

                for (int i = 0; i < sourceIft->getNumInterfaces(); i++) {
                    NetworkInterface *srcIE = sourceIft->getInterface(i);
                    if (!(srcIE->isPointToPoint()))
                        continue;
                    cGate* srcGateMod = mod->gate(srcIE->getNodeOutputGateId());
                    for (int j = 0; j < nextHopIft->getNumInterfaces(); j++) {
                        NetworkInterface *nextHopIE = nextHopIft->getInterface(j);
                        if (!(nextHopIE->isPointToPoint()))
                            continue;
                        cGate* nextHopGateMod = nextHopMod->gate(nextHopIE->getNodeInputGateId());
                        if(srcGateMod->getPathEndGate() == nextHopGateMod->getPathEndGate()){
                            nextHopInterfaceMap[mod][nextHopMod] = srcIE->getInterfaceId();
                        }
                    }
                }
            }
        }
    }
}

double LeoIpv4NetworkConfigurator::computeLinkWeight(Link *link, const char *metric, cXMLElement *parameters)
{
        return computeWiredLinkWeight(link, metric, nullptr);
}

double LeoIpv4NetworkConfigurator::computeWiredLinkWeight(Link *link, const char *metric, cXMLElement *parameters)
{
    //std::cout << "\n Metric: " << metric << endl;
    Topology::Link *linkOut = static_cast<Topology::Link *>(static_cast<Topology::Link *>(link));
    if (!strcmp(metric, "hopCount"))
        return 1;
    else if (!strcmp(metric, "delay")) {
        cDatarateChannel *transmissionChannel = dynamic_cast<cDatarateChannel *>(linkOut->getLinkOutLocalGate()->findTransmissionChannel());
        if (transmissionChannel != nullptr){
            return transmissionChannel->getDelay().dbl();
        }
        else
            return minLinkWeight;
    }
    else if (!strcmp(metric, "dataRate")) {
        cChannel *transmissionChannel = linkOut->getLinkOutLocalGate()->findTransmissionChannel();
        if (transmissionChannel != nullptr) {
            double dataRate = transmissionChannel->getNominalDatarate();
            return dataRate != 0 ? 1 / dataRate : minLinkWeight;
        }
        else
            return minLinkWeight;
    }
    else if (!strcmp(metric, "errorRate")) {
        cDatarateChannel *transmissionChannel = dynamic_cast<cDatarateChannel *>(linkOut->getLinkOutLocalGate()->findTransmissionChannel());
        if (transmissionChannel != nullptr) {
            InterfaceInfo *sourceInterfaceInfo = link->sourceInterfaceInfo;
            double bitErrorRate = transmissionChannel->getBitErrorRate();
            double packetErrorRate = 1.0 - pow(1.0 - bitErrorRate, sourceInterfaceInfo->networkInterface->getMtu());
            return minLinkWeight - log(1 - packetErrorRate);
        }
        else
            return minLinkWeight;
    }
    else
        throw cRuntimeError("Unknown metric");
}

// TODO If we set IPAddress in LeoChannelConstructor, do we need this? doubt it
//void LeoIpv4NetworkConfigurator::configureInterface(InterfaceInfo *interfaceInfo)
//{
//    EV_DETAIL << "Configuring network interface " << interfaceInfo->getFullPath() << ".\n";
//    InterfaceEntry *interfaceEntry = interfaceInfo->interfaceEntry;
//    Ipv4InterfaceData *interfaceData = interfaceEntry->getProtocolData<Ipv4InterfaceData>();
//    if (interfaceInfo->mtu != -1)
//        interfaceEntry->setMtu(interfaceInfo->mtu);
//    if (interfaceInfo->metric != -1)
//        interfaceData->setMetric(interfaceInfo->metric);
//    if (assignAddressesParameter) {
//        interfaceData->setIPAddress(Ipv4Address(interfaceInfo->address));
//        interfaceData->setNetmask(Ipv4Address(interfaceInfo->netmask));
//    }
//    // TODO: should we leave joined multicast groups first?
//    for (auto & multicastGroup : interfaceInfo->multicastGroups)
//        interfaceData->joinMulticastGroup(multicastGroup);
//}
}

