#include <set>
#include <tuple>
#include "ns3/flow-id-tag.h"
#include "ns3/int-header.h"
/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
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
 */

#undef PGO_TRAINING
#define PATH_TO_PGO_CONFIG "path_to_pgo_config"

#include <iostream>
#include <cmath>
#include <fstream>
#include <unordered_map>
#include <time.h>
#include "ns3/core-module.h"
#include "ns3/qbb-helper.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/applications-module.h"
#include "ns3/internet-module.h"
#include "ns3/global-route-manager.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/packet.h"
#include "ns3/error-model.h"
#include <ns3/rdma.h>
#include <ns3/rdma-client.h>
#include <ns3/rdma-client-helper.h>
#include <ns3/rdma-driver.h>
#include <ns3/switch-node.h>
#include <ns3/sim-setting.h>
#include <ns3/userdefinedfunction.h>
#include "ns3/trace-helper.h"

using namespace ns3;
using namespace std;

NS_LOG_COMPONENT_DEFINE("CongestionControlSimulator");


// ALPS_LINK_CAPACITY_V1: audit observes completions; it does not choose paths or rates.
// FCT_CAUSE_OBSERVER_V1: reads packets through existing trace sources only.
// No tags, RNG calls, Simulator::Schedule calls, routing or transport writes.
static std::ofstream lcFctPacketEvents;
static std::map<std::tuple<uint32_t,uint32_t,uint32_t>,uint32_t> lcFctSmallFlows;
static void FctPacketObserver(uint32_t node,uint32_t port,bool tx,Ptr<const Packet> p) {
    CustomHeader ch(CustomHeader::L2_Header|CustomHeader::L3_Header|CustomHeader::L4_Header);
    p->PeekHeader(ch);
    const bool data=ch.l3Prot==L3ProtType::UDP;
    if(!data && ch.l3Prot!=L3ProtType::ACK && ch.l3Prot!=L3ProtType::NACK)return;
    auto key=data?std::make_tuple(ch.sip,ch.dip,uint32_t(ch.udp.sport)):
                  std::make_tuple(ch.dip,ch.sip,uint32_t(ch.ack.dport));
    auto it=lcFctSmallFlows.find(key);if(it==lcFctSmallFlows.end())return;
    Ipv4SmartFlowProbeTag probe;if(p->PeekPacketTag(probe))return;
    Ipv4SmartFlowPathTag tag;AckPathTag ack;
    uint32_t pid=0xffffffff;
    if(data && p->PeekPacketTag(tag))pid=tag.get_path_id();
    if(!data && p->PeekPacketTag(ack))pid=ack.GetPathId();
    lcFctPacketEvents << Simulator::Now().GetNanoSeconds() << ',' << it->second << ','
        << p->GetUid() << ',' << (tx?"tx":"rx") << ',' << node << ',' << port << ','
        << (data?"data":"ack") << ',' << (data?ch.udp.seq:ch.ack.seq) << ','
        << p->GetSize() << ',' << pid << '\n';
}
static void StartFctPacketObserver(global_variable_t &v,const std::string &directory) {
    lcFctPacketEvents.open(directory+"/fct-small-packet-events.csv");
    NS_ABORT_MSG_IF(!lcFctPacketEvents,"Cannot open FCT observation trace");
    lcFctPacketEvents << "time_ns,flow,uid,event,node,port,type,seq,bytes,path\n";
    for(const auto &f:v.genFlows)if(f.byteCnt<=100000)
        lcFctSmallFlows[std::make_tuple(f.srcAddr.Get(),f.dstAddr.Get(),f.srcPort)]=f.idx;
    for(uint32_t i=0;i<v.allNodes.GetN();++i) {
        Ptr<Node> n=v.allNodes.Get(i);
        for(uint32_t j=0;j<n->GetNDevices();++j) {
            Ptr<QbbNetDevice> d=DynamicCast<QbbNetDevice>(n->GetDevice(j));if(!d)continue;
            d->TraceConnectWithoutContext("PhyTxBegin",MakeBoundCallback(&FctPacketObserver,n->GetId(),d->GetIfIndex(),true));
            d->TraceConnectWithoutContext("MacRx",MakeBoundCallback(&FctPacketObserver,n->GetId(),d->GetIfIndex(),false));
        }
    }
}
static std::ofstream lcFlowEvents;
static std::set<uint32_t> lcPlanned,lcFinished;
static void LinkCapacityFlowComplete(Ptr<RdmaQueuePair> qp) {
    NS_ABORT_MSG_IF(!lcPlanned.count(qp->m_flow_id) || !lcFinished.insert(qp->m_flow_id).second,"Unexpected/duplicate flow completion");
    lcFlowEvents << "finish," << Simulator::Now().GetNanoSeconds() << ',' << qp->m_flow_id << ',' << qp->m_size << ','
       << qp->startTime.GetNanoSeconds() << ',' << (Simulator::Now()-qp->startTime).GetNanoSeconds() << '\n';
}
static void StartLinkCapacityFlowAudit(global_variable_t &v,const std::string &directory) {
    std::ofstream plan(directory+"/flow-plan.csv");lcFlowEvents.open(directory+"/flow-events.csv");
    NS_ABORT_MSG_IF(!plan || !lcFlowEvents,"Cannot open flow audit");
    plan << "flow,source,destination,port,start_ns,bytes\n";
    lcFlowEvents << "event,time_ns,flow,bytes,start_ns,fct_ns\n";
    for(const auto &f:v.genFlows) {
        NS_ABORT_MSG_IF(!lcPlanned.insert(f.idx).second,"Duplicate planned flow");
        plan << f.idx << ',' << f.srcNode->GetId() << ',' << f.dstNode->GetId() << ',' << f.srcPort << ','
          << Seconds(f.startTimeInSec).GetNanoSeconds() << ',' << f.byteCnt << '\n';
    }
    for(uint32_t i=0;i<v.svNodes.GetN();++i) {
        Ptr<RdmaDriver> d=v.svNodes.Get(i)->GetObject<RdmaDriver>();
        if(d)d->TraceConnectWithoutContext("QpComplete",MakeCallback(&LinkCapacityFlowComplete));
    }
}
static std::ofstream lcCalibrationTrace;
static void LinkCapacityCalibrationTrace(uint32_t node,uint32_t port,Ptr<const Packet> p) {
    lcCalibrationTrace << Simulator::Now().GetNanoSeconds() << ',' << node << ',' << port << ','
        << p->GetUid() << ',' << p->GetSize() << '\n';
}
static void SendLinkCapacityCalibrationFrame(Ptr<QbbNetDevice> dev) {
    CustomHeader h(CustomHeader::L2_Header|CustomHeader::L3_Header|CustomHeader::L4_Header);
    h.l3Prot=L3ProtType::NACK;h.pppProto=0x21;h.sip=0x0b000001;h.dip=0x0b000101;
    h.ack.sport=1;h.ack.dport=2;h.ack.flags=0;h.ack.pg=0;h.ack.seq=0;h.ack.irnNack=0;h.ack.irnNackSize=0;
    Ptr<Packet> p=Create<Packet>(1048-h.GetSerializedSize());p->AddHeader(h);
    FlowIdTag flow;flow.SetFlowId(0);p->AddPacketTag(flow);
    dev->SwitchSend(0,p,h);
}
static void PauseLinkCapacityCalibrationQueue(Ptr<QbbNetDevice> dev) { dev->PauseLinkCapacityCalibrationQueue(0); }
static int RunLinkCapacityCalibration(const std::string &directory) {
    IntHeader::mode=IntHeader::NONE;
    RdmaSmartFlowRouting::ConfigureLinkCapacity(true,10000,100000,600000,directory);
    Ptr<SwitchNode> a=CreateObject<SwitchNode>(),b=CreateObject<SwitchNode>();
    a->m_linkCapacityCalibrationSink=b->m_linkCapacityCalibrationSink=true;
    NodeContainer nodes;nodes.Add(a);nodes.Add(b);InternetStackHelper internet;internet.Install(nodes);
    QbbHelper helper;helper.SetDeviceAttribute("DataRate",DataRateValue(DataRate("100Gbps")));
    helper.SetChannelAttribute("Delay",TimeValue(MicroSeconds(1)));
    NetDeviceContainer devices=helper.Install(a,b);
    Ptr<QbbNetDevice> da=DynamicCast<QbbNetDevice>(devices.Get(0)),db=DynamicCast<QbbNetDevice>(devices.Get(1));
    lcCalibrationTrace.open(directory+"/independent-phy-transmits.csv");
    lcCalibrationTrace << "time_ns,node,port,packet_uid,bytes\n";
    da->TraceConnectWithoutContext("PhyTxBegin",MakeBoundCallback(&LinkCapacityCalibrationTrace,a->GetId(),da->GetIfIndex()));
    db->TraceConnectWithoutContext("PhyTxBegin",MakeBoundCallback(&LinkCapacityCalibrationTrace,b->GetId(),db->GetIfIndex()));
    a->StartLinkCapacitySampling();b->StartLinkCapacitySampling();
    for(uint64_t t=100000;t<200000;t+=105)Simulator::Schedule(NanoSeconds(t),&SendLinkCapacityCalibrationFrame,da);
    for(uint64_t t=250000;t<350000;t+=60)Simulator::Schedule(NanoSeconds(t),&SendLinkCapacityCalibrationFrame,da);
    for(uint64_t t=430000;t<500000;t+=105)Simulator::Schedule(NanoSeconds(t),&SendLinkCapacityCalibrationFrame,da);
    for(uint64_t t=125000;t<175000;t+=420)Simulator::Schedule(NanoSeconds(t),&SendLinkCapacityCalibrationFrame,db);
    Simulator::Schedule(NanoSeconds(450000),&PauseLinkCapacityCalibrationQueue,da);
    Simulator::Schedule(NanoSeconds(480000),&QbbNetDevice::ResumeLinkCapacityCalibrationQueue,da,0u);
    Simulator::Stop(NanoSeconds(600000));Simulator::Run();
    a->StopLinkCapacitySampling();b->StopLinkCapacitySampling();lcCalibrationTrace.flush();
    Simulator::Destroy();std::cout << "LINK_CAPACITY_CALIBRATION_DONE actual_qbb_transmissions\n";return 0;
}


// FIRSTTEST_V1: ground truth stays in this observer, never in the sender's control input.
struct FirsttestPort {
    Ptr<SwitchNode> sw;Ptr<QbbNetDevice> device;uint32_t peer;
};
static std::vector<FirsttestPort> firsttestPorts;
static std::ofstream firsttestQueueLog;
static void FirsttestPollQueues() {
    uint64_t now=Simulator::Now().GetNanoSeconds();
    for(const auto &entry:firsttestPorts)
        firsttestQueueLog << now << ',' << entry.sw->GetId() << ',' << entry.device->GetIfIndex() << ',' << entry.peer << ','
            << entry.device->GetQueue()->GetNBytesTotal() << ',' << unsigned(entry.device->GetLinkCapacityPausedMask()) << '\n';
    if(now+100<6000000)Simulator::Schedule(NanoSeconds(100),&FirsttestPollQueues);
}
static int RunFirsttest(int argc,char **argv) {
    uint32_t firsttestQueuePercent=100,firsttestQueueAlarmBytes=100000;bool firsttestPathOnlyQueue=true,firsttestMaxLinkQueue=true,firsttestAlpsCongestion=false,firsttestRequireZeroBw=true,firsttestPathQueueMax=true,firsttestQueueMaxAll=false,firsttestQueueSumAll=true; // PATH_QUEUE_MAX150_V1
    bool firsttestAlpsProbe=true,firsttestCapacityBaselineProbe=false;double firsttestZeroBwMinProbability=.05;
    bool firsttestBwCc=false,firsttestGateFixture=false; // BANDWIDTH_CC_V1 test fixture only
    bool firsttest=true,trace=true,prune=false,bwAudit=false;uint32_t seed=7,bwMode=0;uint64_t windowNs=10000; // BANDWIDTH_WEIGHT_V1
    std::string scenario="shared",directory="/ALPS-link-capacity/firsttest/runs/shared-seed7";
    CommandLine command;
    command.AddValue("firsttest","Run controlled shared/private/bypass experiment",firsttest);
    command.AddValue("firsttestAlpsProbe","Use original ALPS probe timing, random selection and cooldown; pending factor .5",firsttestAlpsProbe);
    command.AddValue("firsttestCapacityBaselineProbe","Probe after two own path reference delays without complete capacity ACK",firsttestCapacityBaselineProbe);
    command.AddValue("firsttestZeroBwMinProbability","Final zero-budget path probability floor",firsttestZeroBwMinProbability);
    command.AddValue("firsttestQueueAlarmBytes","Absolute queue alarm bytes; default 100000",firsttestQueueAlarmBytes);
    command.AddValue("firsttestPathOnlyQueue","Prune only the path whose own complete ACK queue maximum crosses threshold",firsttestPathOnlyQueue);
    command.AddValue("firsttestMaxLinkQueue","Max normalized queue link feedback and shared-link pruning",firsttestMaxLinkQueue);
    command.AddValue("firsttestAlpsCongestion","Use original ALPS latency comparison for path congestion",firsttestAlpsCongestion);
    command.AddValue("firsttestRequireZeroBw","Require fresh zero bottleneck plus path queue alarm",firsttestRequireZeroBw);
    command.AddValue("firsttestPathQueueMax","ACK running maximum of forward-egress occupancy along its source route",firsttestPathQueueMax);
    command.AddValue("firsttestQueueMaxAll","Maximum individual queue among eight; full bandwidth CC; priority over sum",firsttestQueueMaxAll);
    command.AddValue("firsttestQueueSumAll","Sum all eight egress queues; default true",firsttestQueueSumAll);
    command.AddValue("firsttestQueuePercent","Legacy Kmax percentage if absolute bytes are zero; default 100",firsttestQueuePercent);
    command.AddValue("firsttestBwCc","Use optional bandwidth CC in controlled test",firsttestBwCc);
    command.AddValue("firsttestGateFixture","Inject temporary LST alarms to verify DATA pause/probe recovery; not a performance experiment",firsttestGateFixture);
    command.AddValue("firsttestCase","isolated, shared, or private",scenario);
    command.AddValue("firsttestDirectory","Existing output directory",directory);
    command.AddValue("firsttestSeed","Seed for existing ALPS packet selection",seed);
    command.AddValue("firsttestTrace","Enable queue and original-ALPS observation logs",trace);
    command.AddValue("firsttestWindowNs","Link measurement and LST epoch interval",windowNs);
    command.AddValue("firsttestPrune","Enable zero-headroom AND existing-ECN-kmax path mask",prune);
    command.AddValue("firsttestBwMode","0=delay, 1=bottleneck headroom",bwMode);
    command.AddValue("firsttestBwAudit","Audit actual DATA allocation",bwAudit);
    command.Parse(argc,argv);
    NS_ABORT_MSG_IF(firsttestQueuePercent==0 || firsttestQueuePercent>100,"Invalid queue threshold percentage");
    RdmaSmartFlowRouting::lcAlpsProbe=firsttestBwCc && firsttestAlpsProbe;
    RdmaSmartFlowRouting::lcCapacityBaselineProbe=firsttestBwCc && firsttestCapacityBaselineProbe && !firsttestAlpsProbe;
    RdmaSmartFlowRouting::lcZeroBwMinProbability=firsttestBwCc?firsttestZeroBwMinProbability:0;
    RdmaSmartFlowRouting::lcQueuePercent=firsttestQueuePercent;
    RdmaSmartFlowRouting::lcQueueAlarmBytes=firsttestQueueAlarmBytes;
    RdmaSmartFlowRouting::lcPathOnlyQueue=firsttestPathOnlyQueue;
    RdmaSmartFlowRouting::lcMaxLinkQueue=firsttestMaxLinkQueue;
    RdmaSmartFlowRouting::lcAlpsCongestion=firsttestAlpsCongestion;
    RdmaSmartFlowRouting::lcRequireZeroBw=firsttestRequireZeroBw;
    RdmaSmartFlowRouting::lcPathQueueMax=firsttestPathQueueMax;
    RdmaSmartFlowRouting::lcQueueMaxAll=firsttestQueueMaxAll;
    RdmaSmartFlowRouting::lcQueueSumAll=firsttestQueueSumAll;
    NS_ABORT_MSG_IF(scenario!="isolated" && scenario!="shared" && scenario!="private","Invalid firsttest scenario");
    global_variable_t v;v.configFileName="/file-in-ctr/inputFiles/C00003/CONFIG_DCQCN.txt";
    parse_default_configures(&v);
    v.lbsName="e2elaps";v.ccMode="Laps";v.enable_laps_plus=true;v.choose_softmax=3;
    v.simStartTimeInSec=0;v.simEndTimeInSec=.006;v.flowLunchEndTimeInSec=.0007;
    v.outputFileDir=directory+"/";v.fileIdx="firsttest";
    std::string inputs="/ALPS-link-capacity/firsttest/inputs/"+scenario+"/";
    v.topoFileName=inputs+"TOPO.txt";v.pitFile=inputs+"PIT.txt";v.pstFile=inputs+"PST.txt";
    v.enableFctMonitor=true;v.enableQlenMonitor=false;v.enablePfcMonitor=false;
    v.enableLLMWorkLoadTest=false;v.enableLLMAFlowRateChange=false;v.enableKvCache=false;
    std::srand(seed);RngSeedManager::SetSeed(seed);RdmaSmartFlowRouting::generator.seed(seed);
    load_default_configures(&v);
    if(firsttestBwCc)bwMode=2;
    RdmaSmartFlowRouting::lcBwMode=bwMode;RdmaSmartFlowRouting::lcBwAudit=bwAudit; // BANDWIDTH_WEIGHT_V1
    RdmaSmartFlowRouting::ConfigureLinkCapacity(true,windowNs,100000,6000000,directory);
    RdmaSmartFlowRouting::lcBwCc=firsttestBwCc;RdmaSmartFlowRouting::lcBwCcAudit=firsttestBwCc;
    RdmaSmartFlowRouting::lcEarlyProbe=firsttestBwCc;RdmaSmartFlowRouting::lcEarlyAudit=firsttestBwCc;
    RdmaSmartFlowRouting::ConfigureCapacityFloorAudit(directory);
    RdmaSmartFlowRouting::ConfigureEarlyProbeAudit(directory);
    RdmaSmartFlowRouting::ConfigureBandwidthCc(directory);
    RdmaSmartFlowRouting::lcPruneEnabled=prune;RdmaSmartFlowRouting::lcDataQueue=v.flowGroupPrio; // ACK_QUEUE_V1
    RdmaSmartFlowRouting::firsttestTraceEnabled=trace;
    if(trace) {
        RdmaSmartFlowRouting::firsttestPathLog.open(directory+"/path-delay-updates.csv");
        RdmaSmartFlowRouting::firsttestAckQueueLog.open(directory+"/ack-queue-observations.csv");
        RdmaSmartFlowRouting::firsttestCcLog.open(directory+"/cc-evaluations.csv");
        RdmaSmartFlowRouting::firsttestPathLog << "time_ns,node,flow,path,ack_uid,delay_ns,threshold_ns,probe\n";
        RdmaSmartFlowRouting::firsttestAckQueueLog << "time_ns,node,port,path,ack_uid,queue_bytes\n";
        RdmaSmartFlowRouting::firsttestCcLog << "time_ns,node,flow,congested_paths,total_paths,rate_bps,action\n";
    }
    create_topology_rdma(&v);assign_node_addresses(&v);calculate_paths_for_servers(&v);
    install_routing_entries(&v);Ipv4GlobalRoutingHelper::PopulateRoutingTables();config_switch(&v);
    // ACK_QUEUE_V1: snapshot actual MMU settings, including the existing port-count buffer scaling.
    std::ofstream mmu(directory+"/runtime-mmu.csv");
    mmu << "node,port,buffer_bytes,data_queue,kmin_bytes,kmax_bytes,pfc_shared_bytes\n";
    for(uint32_t n=0;n<v.swNodes.GetN();++n) {
        auto sw=DynamicCast<SwitchNode>(v.swNodes.Get(n));
        for(uint32_t port=1;port<sw->GetNDevices();++port)
            mmu << sw->GetId() << ',' << port << ',' << sw->m_mmu->buffer_size << ',' << v.flowGroupPrio << ','
                << sw->m_mmu->kmin[port] << ',' << sw->m_mmu->kmax[port] << ',' << sw->m_mmu->GetPfcThreshold(port) << '\n';
    }
    mmu.close();
    for(uint32_t i=0;i<v.swNodes.GetN();++i)DynamicCast<SwitchNode>(v.swNodes.Get(i))->StartLinkCapacitySampling();
    std::ofstream ports(directory+"/runtime-ports.csv");ports << "node,port,peer,capacity_bps\n";
    for(uint32_t n=0;n<v.allNodes.GetN();++n) {
        auto node=v.allNodes.Get(n);
        for(uint32_t port=1;port<node->GetNDevices();++port) {
            auto dev=DynamicCast<QbbNetDevice>(node->GetDevice(port));if(!dev)continue;
            auto channel=dev->GetChannel();uint32_t peer=channel->GetDevice(0)->GetNode()->GetId();
            if(peer==node->GetId())peer=channel->GetDevice(1)->GetNode()->GetId();
            ports << node->GetId() << ',' << port << ',' << peer << ',' << dev->GetDataRate().GetBitRate() << '\n';
            if(trace && ((n==3 && peer==4)||(n==0 && peer==1)||(n==5 && peer==6)||(n==4 && peer==11)))
                firsttestPorts.push_back({DynamicCast<SwitchNode>(node),dev,peer});
        }
    }
    ports.close();
    if(trace) {
        NS_ABORT_MSG_IF(firsttestPorts.size()!=4,"Missing watched egress");
        firsttestQueueLog.open(directory+"/queue-trace.csv");
        firsttestQueueLog << "time_ns,node,port,peer,queue_bytes,paused_mask\n";FirsttestPollQueues();
    }
    v.genFlows.push_back(generate_single_rdma_flow(1,v.allNodes.Get(10),v.allNodes.Get(11),12001,firsttestGateFixture?100000:20000000,.0001,
        v.flowGroupPrio,v.maxBdpInByte,v.maxRttInNs));
    v.genFlows.push_back(generate_single_rdma_flow(2,v.allNodes.Get(12),v.allNodes.Get(13),12002,firsttestGateFixture?100000:4000000,.0006,
        v.flowGroupPrio,v.maxBdpInByte,v.maxRttInNs));
    v.flowCount=2;v.totalFlowSizeInByte=firsttestGateFixture?200000:24000000;RdmaSmartFlowRouting::sum_data=v.totalFlowSizeInByte; // ZERO_BANDWIDTH_V1 test size only
    uint32_t fgSource=10,fgDestination=11,bgSource=12,bgDestination=13;
    RdmaSmartFlowRouting::record_path_flow_num[HostId2PathSeleKey(fgSource,fgDestination)]=1;
    RdmaSmartFlowRouting::record_path_flow_num[HostId2PathSeleKey(bgSource,bgDestination)]=1;
    RdmaSmartFlowRouting::sorted_path_flow_counts.assign(RdmaSmartFlowRouting::record_path_flow_num.begin(),
        RdmaSmartFlowRouting::record_path_flow_num.end());
    QbbNetDevice::flowPacketSenGap.resize(3,std::vector<uint64_t>(1000,0));
    install_rdma_flows_on_nodes(&v);StartLinkCapacityFlowAudit(v,directory);
    // BANDWIDTH_CC_V1 fixture: alarm injection is explicitly synthetic; subsequent ACKs are real.
    std::vector<std::function<void()>> gateEvents(10);
    if(firsttestGateFixture) {
        NS_ABORT_MSG_IF(!firsttestBwCc,"Gate fixture requires firsttestBwCc");
        auto routing=v.allNodes.Get(10)->GetObject<RdmaDriver>()->m_rdma->m_E2ErdmaSmartFlowRouting;
        uint32_t sourceId=10,destinationId=11;HostId2PathSeleKey fixtureKey(sourceId,destinationId);
        auto paths=routing->batch_lookup_PIT(routing->lookup_PST(fixtureKey)->paths);
        for(size_t k=0;k<gateEvents.size();++k) {
            gateEvents[k]=[routing,paths]() {
                uint64_t now=Simulator::Now().GetNanoSeconds();
                for(auto path:paths)for(size_t j=1;j<path->portSequence.size();++j) {
                    LinkCapacityRecord record;record.linkId=RdmaSmartFlowRouting::MakeLinkId(path->nodeIdSequence[j],path->portSequence[j]);
                    record.state.availableQ=0x8000;record.state.sampleEpoch=uint16_t(now/RdmaSmartFlowRouting::lcObservationTickNs);
                    routing->AcceptLinkCapacity(record,path->pid,now);
                }
            };
            auto invoke=+[](std::function<void()> *f){(*f)();};
            Simulator::Schedule(NanoSeconds(105000+200*k),invoke,&gateEvents[k]);
        }
    }
    Simulator::Stop(Seconds(v.simEndTimeInSec));Simulator::Run();
    for(uint32_t i=0;i<v.swNodes.GetN();++i)DynamicCast<SwitchNode>(v.swNodes.Get(i))->StopLinkCapacitySampling();
    lcFlowEvents.flush();firsttestQueueLog.flush();
    std::ofstream completion(directory+"/completion.csv");completion << "planned,finished,complete\n"
        << lcPlanned.size() << ',' << lcFinished.size() << ',' << (lcPlanned.size()==lcFinished.size()) << '\n';completion.close();
    if(firsttestBwCc) {
        RdmaSmartFlowRouting::lcBwCcLog.flush();
        RdmaSmartFlowRouting::WriteEarlyProbeAudit(directory);
    }
    if(firsttestGateFixture) {
        // ZERO_BANDWIDTH_V1 fixture: an already-in-flight probe may supply recovery.
        std::ifstream log(directory+"/bw-cc-events.csv");std::string line;uint64_t probes=0,pauses=0,firstPause=uint64_t(-1),returned=0;
        while(std::getline(log,line)) {
            probes+=line.find(",pause_probe,")!=std::string::npos;
            if(line.find(",pause_")!=std::string::npos){++pauses;firstPause=std::min<uint64_t>(firstPause,std::stoull(line.substr(0,line.find(','))));}
        }
        std::ifstream ackLog(directory+"/early-probe-events.csv");
        while(std::getline(ackLog,line))if(line.find("ack,")==0) {
            size_t endTime=line.find(',',4),endNode=line.find(',',endTime+1);
            if(line.substr(endTime+1,endNode-endTime-1)=="10" && std::stoull(line.substr(4,endTime-4))>=firstPause)++returned;
        }
        NS_ABORT_MSG_IF(!pauses || (!probes && !returned) || lcFinished.size()!=2,"DATA pause/probe recovery fixture failed");
        std::cout<<"BANDWIDTH_CC_GATE_FIXTURE_PASS pause_events="<<pauses<<" new_pause_probes="<<probes<<" returned_after_pause="<<returned<<" completed_flows="<<lcFinished.size()<<" injected_LST_not_performance_evidence\n";
    }
    RdmaSmartFlowRouting::WriteBandwidthAudit(directory); // BANDWIDTH_WEIGHT_V1
    save_egress_ports_loadinfo(&v);sim_finish(&v);Simulator::Destroy();
    std::cout << "FIRSTTEST_DONE case=" << scenario << " seed=" << seed << " finished=" << lcFinished.size() << "/2\n";
    return lcFinished.size()==2?0:3;
}


// SMALLTEST_V1: read-only packet observer; no packet, RNG, rate or event changes.
static std::ofstream smalltestPackets;
static std::map<std::pair<uint32_t,uint32_t>,Ptr<QbbNetDevice>> smalltestDevices;
static void SmalltestPacketObserver(uint32_t node,uint32_t port,bool tx,Ptr<const Packet> p) {
    CustomHeader ch(CustomHeader::L2_Header|CustomHeader::L3_Header|CustomHeader::L4_Header);p->PeekHeader(ch);
    bool data=ch.l3Prot==L3ProtType::UDP;
    if(!data && ch.l3Prot!=L3ProtType::ACK && ch.l3Prot!=L3ProtType::NACK)return;
    if((data?uint32_t(ch.udp.sport):uint32_t(ch.ack.dport))!=12001)return;
    Ipv4SmartFlowProbeTag probe;if(p->PeekPacketTag(probe))return;
    uint32_t path=0xffffffff;Ipv4SmartFlowPathTag t;AckPathTag a;
    if(data && p->PeekPacketTag(t))path=t.get_path_id();
    if(!data && p->PeekPacketTag(a))path=a.GetPathId();
    auto dev=smalltestDevices.at(std::make_pair(node,port));
    uint32_t queue=node<4?dev->GetQueue()->GetNBytesTotal():0;
    NS_ABORT_MSG_IF(data && p->GetSize()<ch.GetSerializedSize(),"Bad smalltest payload length");
    smalltestPackets<<Simulator::Now().GetNanoSeconds()<<','<<p->GetUid()<<','<<(tx?"tx":"rx")<<','
        <<node<<','<<port<<','<<(data?"data":(ch.l3Prot==L3ProtType::ACK?"ack":"nack"))<<','
        <<(data?ch.udp.seq:ch.ack.seq)<<','<<(data?p->GetSize()-ch.GetSerializedSize():0)<<','
        <<p->GetSize()<<','<<path<<','<<queue<<','<<unsigned(dev->GetLinkCapacityPausedMask())<<'\n';
}
static int RunSmalltest(int argc,char **argv) {
    bool enabled=true,trace=true;uint32_t seed=7;std::string mode="original",scenario="20-80";
    std::string directory="/ALPS-link-capacity/smalltest/runs/default";
    CommandLine cmd;cmd.AddValue("smalltest","Controlled capacity-matching microexperiment",enabled);
    cmd.AddValue("smalltestMode","original, bandwidth, or oracle",mode);
    cmd.AddValue("smalltestCase","20-80 or 80-20",scenario);
    cmd.AddValue("smalltestSeed","ALPS RNG seed",seed);
    cmd.AddValue("smalltestTrace","Read-only packet trace",trace);
    cmd.AddValue("smalltestDirectory","Existing output directory",directory);cmd.Parse(argc,argv);
    NS_ABORT_MSG_IF(mode!="original" && mode!="bandwidth" && mode!="oracle","Bad smalltest mode");
    NS_ABORT_MSG_IF(scenario!="20-80" && scenario!="80-20","Bad smalltest capacity case");
    global_variable_t v;v.configFileName="/file-in-ctr/inputFiles/C00003/CONFIG_DCQCN.txt";parse_default_configures(&v);
    v.lbsName="e2elaps";v.ccMode="Laps";v.enable_laps_plus=true;v.choose_softmax=3;
    v.simStartTimeInSec=0;v.simEndTimeInSec=.020;v.flowLunchEndTimeInSec=.0002;
    v.outputFileDir=directory+"/";v.fileIdx="smalltest";
    std::string inputs="/ALPS-link-capacity/smalltest/inputs/"+scenario+"/";
    v.topoFileName=inputs+"TOPO.txt";v.pitFile=inputs+"PIT.txt";v.pstFile=inputs+"PST.txt";
    v.enableFctMonitor=true;v.enableQlenMonitor=false;v.enablePfcMonitor=false;
    v.enableLLMWorkLoadTest=false;v.enableLLMAFlowRateChange=false;v.enableKvCache=false;
    std::srand(seed);RngSeedManager::SetSeed(seed);RdmaSmartFlowRouting::generator.seed(seed);load_default_configures(&v);
    RdmaSmartFlowRouting::lcBwMode=mode=="bandwidth"?2:0;
    RdmaSmartFlowRouting::lcBwAudit=true;RdmaSmartFlowRouting::lcQuotaAudit=mode=="bandwidth";
    RdmaSmartFlowRouting::lcDetailedLogs=false;
    RdmaSmartFlowRouting::ConfigureLinkCapacity(mode=="bandwidth",5000,100000,20000000,directory);
    RdmaSmartFlowRouting::lcPruneEnabled=false;RdmaSmartFlowRouting::lcBwCc=false;
    RdmaSmartFlowRouting::lcBwCcAudit=false;RdmaSmartFlowRouting::lcEarlyProbe=false;RdmaSmartFlowRouting::lcEarlyAudit=false;
    RdmaSmartFlowRouting::lcDataQueue=v.flowGroupPrio;
    RdmaSmartFlowRouting::lcSmalltestFixedBranchProbability=mode=="oracle"?(scenario=="20-80"?.2:.8):-1;
    RdmaSmartFlowRouting::firsttestTraceEnabled=true;
    RdmaSmartFlowRouting::firsttestPathLog.open(directory+"/path-delay-updates.csv");
    RdmaSmartFlowRouting::firsttestAckQueueLog.open(directory+"/ack-queue-observations.csv");
    RdmaSmartFlowRouting::firsttestCcLog.open(directory+"/cc-evaluations.csv");
    RdmaSmartFlowRouting::firsttestPathLog<<"time_ns,node,flow,path,ack_uid,delay_ns,threshold_ns,probe\n";
    RdmaSmartFlowRouting::firsttestAckQueueLog<<"time_ns,node,port,path,ack_uid,queue_bytes\n";
    RdmaSmartFlowRouting::firsttestCcLog<<"time_ns,node,flow,congested_paths,total_paths,rate_bps,action\n";
    // The stock CONFIG has 10/25/100G entries. Extend its linear 100G ECN scaling.
    NS_ABORT_MSG_IF(!v.ecnParaMap.count(100),"Missing reference ECN parameters");
    for(uint32_t rate:{20u,80u}) {
        v.ecnParaMap[rate]=v.ecnParaMap.at(100);
        v.ecnParaMap[rate].kminInKb=v.ecnParaMap.at(100).kminInKb*rate/100;
        v.ecnParaMap[rate].kmaxInKb=v.ecnParaMap.at(100).kmaxInKb*rate/100;
    }
    create_topology_rdma(&v);assign_node_addresses(&v);calculate_paths_for_servers(&v);
    install_routing_entries(&v);Ipv4GlobalRoutingHelper::PopulateRoutingTables();config_switch(&v);
    std::ofstream ports(directory+"/runtime-ports.csv"),mmu(directory+"/runtime-mmu.csv");
    ports<<"node,port,peer,capacity_bps\n";mmu<<"node,port,buffer_bytes,data_queue,kmin_bytes,kmax_bytes,pfc_shared_bytes\n";
    if(trace){smalltestPackets.open(directory+"/packet-events.csv");smalltestPackets<<"time_ns,uid,event,node,port,type,seq,payload_bytes,wire_bytes,path,queue_bytes,paused_mask\n";}
    for(uint32_t n=0;n<v.allNodes.GetN();++n) {
        auto node=v.allNodes.Get(n);auto sw=DynamicCast<SwitchNode>(node);
        for(uint32_t port=1;port<node->GetNDevices();++port) {
            auto dev=DynamicCast<QbbNetDevice>(node->GetDevice(port));if(!dev)continue;
            auto channel=dev->GetChannel();uint32_t peer=channel->GetDevice(0)->GetNode()->GetId();
            if(peer==node->GetId())peer=channel->GetDevice(1)->GetNode()->GetId();
            ports<<node->GetId()<<','<<port<<','<<peer<<','<<dev->GetDataRate().GetBitRate()<<'\n';
            if(sw)mmu<<node->GetId()<<','<<port<<','<<sw->m_mmu->buffer_size<<','<<v.flowGroupPrio<<','
                <<sw->m_mmu->kmin[port]<<','<<sw->m_mmu->kmax[port]<<','<<sw->m_mmu->GetPfcThreshold(port)<<'\n';
            if(trace) {
                smalltestDevices[std::make_pair(node->GetId(),port)]=dev;
                dev->TraceConnectWithoutContext("PhyTxBegin",MakeBoundCallback(&SmalltestPacketObserver,node->GetId(),port,true));
                dev->TraceConnectWithoutContext("MacRx",MakeBoundCallback(&SmalltestPacketObserver,node->GetId(),port,false));
            }
        }
        if(sw)sw->StartLinkCapacitySampling();
    }
    v.genFlows.push_back(generate_single_rdma_flow(1,v.allNodes.Get(4),v.allNodes.Get(5),12001,10000000,.0001,
        v.flowGroupPrio,v.maxBdpInByte,v.maxRttInNs));
    v.flowCount=1;v.totalFlowSizeInByte=10000000;RdmaSmartFlowRouting::sum_data=v.totalFlowSizeInByte;
    uint32_t src=4,dst=5;RdmaSmartFlowRouting::record_path_flow_num[HostId2PathSeleKey(src,dst)]=1;
    RdmaSmartFlowRouting::sorted_path_flow_counts.assign(RdmaSmartFlowRouting::record_path_flow_num.begin(),RdmaSmartFlowRouting::record_path_flow_num.end());
    QbbNetDevice::flowPacketSenGap.resize(2,std::vector<uint64_t>(1000,0));
    install_rdma_flows_on_nodes(&v);StartLinkCapacityFlowAudit(v,directory);
    Simulator::Stop(Seconds(v.simEndTimeInSec));Simulator::Run();
    for(uint32_t i=0;i<v.swNodes.GetN();++i)DynamicCast<SwitchNode>(v.swNodes.Get(i))->StopLinkCapacitySampling();
    lcFlowEvents.flush();smalltestPackets.flush();RdmaSmartFlowRouting::firsttestPathLog.flush();
    RdmaSmartFlowRouting::firsttestAckQueueLog.flush();RdmaSmartFlowRouting::firsttestCcLog.flush();
    std::ofstream completion(directory+"/completion.csv");completion<<"planned,finished,complete\n"<<lcPlanned.size()<<','<<lcFinished.size()<<','<<(lcFinished.size()==lcPlanned.size())<<'\n';
    RdmaSmartFlowRouting::WriteBandwidthAudit(directory);save_egress_ports_loadinfo(&v);sim_finish(&v);Simulator::Destroy();
    std::cout<<"SMALLTEST_DONE mode="<<mode<<" case="<<scenario<<" seed="<<seed<<" complete="<<lcFinished.size()<<"/1\n";
    return lcFinished.size()==1?0:3;
}


// SMALLTEST_DRAGONFLY_V1: observational counters, never read by allocation/transport.
struct FlowPathAuditStats {
    uint64_t uniqueTx=0,allTx=0,uniqueRx=0,txPackets=0,rxPackets=0,outOfOrder=0;
    uint64_t firstTx=uint64_t(-1),lastTx=0,firstRx=uint64_t(-1),lastRx=0;
};
struct FlowPathAuditSeen {
    uint64_t txFront=0,rxFront=0;std::map<uint64_t,uint32_t> txPending,rxPending;
};
static std::map<std::tuple<uint32_t,uint32_t,uint32_t>,uint32_t> flowPathAuditKeys;
static std::map<std::pair<uint32_t,uint32_t>,FlowPathAuditStats> flowPathAuditStats;
static std::map<uint32_t,FlowPathAuditSeen> flowPathAuditSeen;
static bool FlowPathAuditNew(std::map<uint64_t,uint32_t> &pending,uint64_t &front,uint64_t seq,uint32_t len) {
    if(seq<front || pending.count(seq))return false;
    pending[seq]=len;
    for(;;){auto i=pending.find(front);if(i==pending.end())break;front+=i->second;pending.erase(i);}
    return true;
}
static void FlowPathAuditPacket(uint32_t node,bool tx,Ptr<const Packet> p) {
    CustomHeader ch(CustomHeader::L2_Header|CustomHeader::L3_Header|CustomHeader::L4_Header);p->PeekHeader(ch);
    if(ch.l3Prot!=L3ProtType::UDP)return;
    Ipv4SmartFlowProbeTag probe;if(p->PeekPacketTag(probe))return;
    auto i=flowPathAuditKeys.find(std::make_tuple(ch.sip,ch.dip,uint32_t(ch.udp.sport)));
    if(i==flowPathAuditKeys.end())return;
    Ipv4SmartFlowPathTag tag;NS_ABORT_MSG_IF(!p->PeekPacketTag(tag),"Untagged audited DATA");
    NS_ABORT_MSG_IF(p->GetSize()<=ch.GetSerializedSize(),"Empty audited DATA payload");
    uint32_t bytes=p->GetSize()-ch.GetSerializedSize();uint64_t now=Simulator::Now().GetNanoSeconds();
    auto &a=flowPathAuditStats[std::make_pair(i->second,tag.get_path_id())];auto &seen=flowPathAuditSeen[i->second];
    if(tx) {
        ++a.txPackets;a.allTx+=bytes;a.firstTx=std::min(a.firstTx,now);a.lastTx=now;
        if(FlowPathAuditNew(seen.txPending,seen.txFront,ch.udp.seq,bytes))a.uniqueTx+=bytes;
    } else {
        ++a.rxPackets;bool reordered=ch.udp.seq>seen.rxFront;
        if(FlowPathAuditNew(seen.rxPending,seen.rxFront,ch.udp.seq,bytes)) {
            a.uniqueRx+=bytes;a.firstRx=std::min(a.firstRx,now);a.lastRx=now;a.outOfOrder+=reordered;
        }
    }
}
static void StartFlowPathAudit(global_variable_t &v) {
    for(const auto &f:v.genFlows)flowPathAuditKeys[std::make_tuple(f.srcAddr.Get(),f.dstAddr.Get(),f.srcPort)]=f.idx;
    for(uint32_t i=0;i<v.svNodes.GetN();++i) {
        auto n=v.svNodes.Get(i);
        for(uint32_t j=0;j<n->GetNDevices();++j) {
            auto d=DynamicCast<QbbNetDevice>(n->GetDevice(j));if(!d)continue;
            d->TraceConnectWithoutContext("PhyTxBegin",MakeBoundCallback(&FlowPathAuditPacket,n->GetId(),true));
            d->TraceConnectWithoutContext("MacRx",MakeBoundCallback(&FlowPathAuditPacket,n->GetId(),false));
        }
    }
}
static void WriteFlowPathAudit(const std::string &directory) {
    std::ofstream f(directory+"/flow-path-payload.csv"),s(directory+"/flow-payload-progress.csv");
    NS_ABORT_MSG_IF(!f || !s,"Cannot open flow/path payload counters");
    f<<"flow,path,unique_tx_payload,all_tx_payload,unique_rx_payload,tx_packets,rx_packets,first_tx_ns,last_tx_ns,first_rx_ns,last_rx_ns,out_of_order_unique_rx_packets\n";
    for(const auto &i:flowPathAuditStats) {const auto &a=i.second;f<<i.first.first<<','<<i.first.second<<','<<a.uniqueTx<<','<<a.allTx<<','<<a.uniqueRx<<','
        <<a.txPackets<<','<<a.rxPackets<<','<<a.firstTx<<','<<a.lastTx<<','<<a.firstRx<<','<<a.lastRx<<','<<a.outOfOrder<<'\n';}
    s<<"flow,contiguous_tx_payload,contiguous_rx_payload,tx_pending_ranges,rx_pending_ranges\n";
    for(const auto &i:flowPathAuditSeen)s<<i.first<<','<<i.second.txFront<<','<<i.second.rxFront<<','<<i.second.txPending.size()<<','<<i.second.rxPending.size()<<'\n';
}


// RECOVERY_DIAG_V1: wire DATA/ACK/probe events on hosts only. Never changes packets or RNG.
static std::ofstream recoveryPackets;
static void RecoveryPacketObserver(uint32_t node,bool tx,Ptr<const Packet> p) {
    CustomHeader ch(CustomHeader::L2_Header|CustomHeader::L3_Header|CustomHeader::L4_Header);p->PeekHeader(ch);
    bool data=ch.l3Prot==L3ProtType::UDP;
    if(!data && ch.l3Prot!=L3ProtType::ACK && ch.l3Prot!=L3ProtType::NACK)return;
    auto key=data?std::make_tuple(ch.sip,ch.dip,uint32_t(ch.udp.sport)):std::make_tuple(ch.dip,ch.sip,uint32_t(ch.ack.dport));
    auto i=flowPathAuditKeys.find(key);uint32_t flow=i==flowPathAuditKeys.end()?0xffffffff:i->second;
    Ipv4SmartFlowProbeTag probe;bool isProbe=p->PeekPacketTag(probe);
    Ipv4SmartFlowPathTag t;AckPathTag a;uint32_t path=0xffffffff;
    if(data && p->PeekPacketTag(t))path=t.get_path_id();
    if(!data && p->PeekPacketTag(a))path=a.GetPathId();
    recoveryPackets<<Simulator::Now().GetNanoSeconds()<<','<<p->GetUid()<<','<<(data?(tx?"data_tx":"data_rx"):(tx?"ack_tx":"ack_rx"))<<','
        <<node<<','<<flow<<','<<path<<','<<(data?ch.udp.seq:ch.ack.seq)<<','
        <<(data?p->GetSize()-ch.GetSerializedSize():0)<<','<<p->GetSize()<<','<<isProbe<<','
        <<(data?0:ch.ack.irnNack)<<','<<(data?0:ch.ack.irnNackSize)<<'\n';
}
static void StartRecoveryObserver(global_variable_t &v) {
    for(uint32_t i=0;i<v.svNodes.GetN();++i) {
        auto n=v.svNodes.Get(i);
        for(uint32_t j=0;j<n->GetNDevices();++j) {
            auto dev=DynamicCast<QbbNetDevice>(n->GetDevice(j));if(!dev)continue;
            dev->TraceConnectWithoutContext("PhyTxBegin",MakeBoundCallback(&RecoveryPacketObserver,n->GetId(),true));
            dev->TraceConnectWithoutContext("MacRx",MakeBoundCallback(&RecoveryPacketObserver,n->GetId(),false));
        }
    }
}

int main(int argc, char *argv[])
{
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--lcPartialTxSelfTest=1")return SwitchNode::RunPartialTxAccountingSelfChecks()?0:2;
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--lcLinkSendAlphaSelfTest=1")return RdmaSmartFlowRouting::RunLinkSendAlphaSelfChecks()?0:2;
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--lcLinkSendPenaltySelfTest=1")return RdmaSmartFlowRouting::RunLinkSendPenaltySelfChecks()?0:2;
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--lcQueueBwScoreSelfTest=1")return (SwitchNode::RunQueueScoreArithmeticSelfChecks() && RdmaSmartFlowRouting::RunQueueBandwidthScoreSelfChecks())?0:2;
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--lcPureBwSelfTest=1")return RdmaSmartFlowRouting::RunPureBandwidthSelfChecks()?0:2;
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--lcNoPruneSelfTest=1")return RdmaSmartFlowRouting::RunNoQueuePruningSelfChecks()?0:2;
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--alpsStrictSelfTest=1")return RdmaSmartFlowRouting::RunAlpsStrictPruneSelfChecks()?0:2;
    // LogComponentEnable("RdmaHw", LOG_LEVEL_INFO);
    // LogComponentEnable("SwitchNode", LOG_LEVEL_INFO);
    // LogComponentEnable("ConWeaveRouting", LOG_LEVEL_INFO);
    //  LogComponentEnable("RdmaSmartFlowRouting", LOG_LEVEL_INFO);
    LogComponentEnable("userdefinedfunction", LOG_LEVEL_INFO);
    // LogComponentEnable("CongestionControlSimulator", LOG_LEVEL_INFO);
    // LogComponentEnable("QbbNetDevice", LOG_LEVEL_INFO);
    // LogComponentEnable ("BEgressQueue", LOG_LEVEL_INFO);


    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--lcDynamicSmallSelfTest=1")return RdmaSmartFlowRouting::RunDynamicSmallSelfChecks()?0:2;
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--lcSizeAwareSelfTest=1")return RdmaSmartFlowRouting::RunSizeAwareSelfChecks()?0:2;
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--lcFeedbackResumeSelfTest=1")return RdmaSmartFlowRouting::RunFeedbackResumeSelfChecks()?0:2;
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--lcQueuePercentSelfTest=1")return RdmaSmartFlowRouting::RunQueuePercentSelfChecks()?0:2; // QUEUE_PERCENT_V1
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--lcBudgetFallbackSelfTest=1")return RdmaSmartFlowRouting::RunBudgetFallbackSelfChecks()?0:2; // BUDGET_FALLBACK_V1
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--lcProbeBackoffSelfTest=1")return RdmaSmartFlowRouting::RunProbeBackoffSelfChecks()?0:2; // GHOST_BACKOFF_V1
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--lcQueueProbeSelfTest=1")return RdmaSmartFlowRouting::RunQueue90Probe20SelfChecks()?0:2; // QUEUE90_PROBE20_V1
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--lcPathLocalSelfTest=1")return RdmaSmartFlowRouting::RunPathLocalBandwidthSelfChecks()?0:2; // ZERO_BANDWIDTH_V1
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--lcBwCcSelfTest=1")return RdmaSmartFlowRouting::RunBandwidthCcSelfChecks()?0:2; // BANDWIDTH_CC_V1
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--lcQuotaSelfTest=1")return RdmaSmartFlowRouting::RunSharedQuotaSelfChecks()?0:2; // SHARED_QUOTA_V1
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--lcPathTimeoutSelfTest=1")return RdmaHw::RunPathTimeoutSelfChecks()?0:2;
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--lcQueueTargetCcSelfTest=1")return RdmaSmartFlowRouting::RunQueueTargetCcSelfChecks()?0:2;
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--lcDataAckOnlyRecoverySelfTest=1"){RdmaSmartFlowRouting::lcDataAckCcOnly=true;return RdmaHw::RunPathTimeoutSelfChecks()?0:2;}
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--lcPressureGainSelfTest=1")return RdmaSmartFlowRouting::RunPressureGainSelfChecks()?0:2;
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--lcQueueRecoverySelfTest=1")return RdmaSmartFlowRouting::RunQueueRecoverySelfChecks()?0:2;
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--lcPressureQueueGateSelfTest=1")return RdmaSmartFlowRouting::RunPressureQueueGateSelfChecks()?0:2;
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--lcPressureCcSelfTest=1")return RdmaSmartFlowRouting::RunPressureCcSelfChecks()?0:2;
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--lcSendPenaltySelfTest=1")return RdmaSmartFlowRouting::RunBandwidthSendPenaltySelfChecks()?0:2;
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--lcAlpsProbeSelfTest=1")return RdmaSmartFlowRouting::RunAlpsNativeProbeSelfChecks()?0:2;
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--lcEarlySelfTest=1")return RdmaSmartFlowRouting::RunEarlyProbeSelfChecks()?0:2; // EARLY_GHOST_V1
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--lcBwSelfTest=1")return RdmaSmartFlowRouting::RunBandwidthWeightSelfChecks()?0:2; // BANDWIDTH_WEIGHT_V1
    // ACK_IMMEDIATE_V1: real ACK-append path fixture; no synthetic performance claims.
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--lcPathOnlyZeroSelfTest=1")return RdmaSmartFlowRouting::RunPathOnlyZeroQueueSelfChecks()?0:2;
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--lcCapacityBaselineSelfTest=1")return RdmaSmartFlowRouting::RunCapacityBaselineSelfChecks()?0:2;
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--lcZeroFloorSelfTest=1")return RdmaSmartFlowRouting::RunZeroBudgetFloorSelfChecks()?0:2;
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--lcPathOnlyQueueSelfTest=1")return RdmaSmartFlowRouting::RunPathOnlyQueueSelfChecks()?0:2;
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--lcMaxLinkQueueSelfTest=1")return RdmaSmartFlowRouting::RunMaxLinkQueueSelfChecks()?0:2;
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--lcAlpsCongestionSelfTest=1")return RdmaSmartFlowRouting::RunAlpsCongestionSelfChecks()?0:2;
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--lcZeroBandwidthQueueSelfTest=1")return RdmaSmartFlowRouting::RunZeroBandwidthQueueSelfChecks()?0:2;
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--lcAckPathQueueMaxSelfTest=1") {
        std::string directory=".";for(int j=1;j<argc;++j)if(std::string(argv[j]).find("--lcDirectory=")==0)directory=std::string(argv[j]).substr(14);
        RdmaSmartFlowRouting::ConfigureLinkCapacity(true,5000,100000,10000,directory);return SwitchNode::RunAckPathQueueMaxSelfChecks()?0:2;
    }
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--lcAckImmediateSelfTest=1") {
        std::string directory=".";
        for(int j=1;j<argc;++j)if(std::string(argv[j]).find("--lcDirectory=")==0)directory=std::string(argv[j]).substr(14);
        RdmaSmartFlowRouting::ConfigureLinkCapacity(true,5000,100000,10000,directory);
        return SwitchNode::RunAckImmediateSelfChecks()?0:2;
    }
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--lcPruneSelfTest=1")return RdmaSmartFlowRouting::RunLinkPruningSelfChecks()?0:2; // ACK_QUEUE_V1

    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--lcQpInitSelfTest=1") {
        auto q=CreateObject<RdmaQueuePair>(uint16_t(3),Ipv4Address("11.0.0.1"),Ipv4Address("11.0.0.2"),uint16_t(12001),uint16_t(12001));
        bool ok=q->lastPktSize==0 && q->m_nextAvail==NanoSeconds(0);
        std::cout<<(ok?"QP_INIT_SELFTEST_PASS":"QP_INIT_SELFTEST_FAIL")<<" lastPktSize="<<q->lastPktSize<<'\n';return ok?0:2;
    }
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--smalltest=1")return RunSmalltest(argc,argv);
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--firsttest=1")return RunFirsttest(argc,argv);
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--lcSelfTest=1")
        return RdmaSmartFlowRouting::RunLinkCapacitySelfChecks()?0:2;
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--lcCalibrate=1") {
        std::string directory=".";
        for(int j=1;j<argc;++j)if(std::string(argv[j]).find("--lcDirectory=")==0)directory=std::string(argv[j]).substr(14);
        return RunLinkCapacityCalibration(directory);
    }
    // EARLY_GHOST_V1
    uint32_t lcQueueAlarmBytes=100000;bool lcPathOnlyQueue=true,lcMaxLinkQueue=true,lcAlpsCongestion=false,lcRequireZeroBw=true,lcPathQueueMax=true,lcQueueMaxAll=false,lcQueueSumAll=true; // PATH_QUEUE_MAX150_V1
    uint32_t lcQueuePercent=100; // QUEUE_DEFAULT26_V1: absolute alarm uses 100%
    bool lcBwCc=false,lcBwCcAudit=false,lcBwCcUniformFallback=false; // BANDWIDTH_CC_V1
    bool lcFeedbackResume=true; // FEEDBACK_RESUME_V1: effective only with bandwidth CC
    bool lcSizeAware=false,lcSizeReserve=false;uint64_t lcSmallFlowBytes=100000;double lcSmallReservePercent=10; // SIZE_AWARE_V1
    bool lcDynamicSmall=false;uint64_t lcSmallTargetNs=50000;
    bool lcQuotaAudit=false; // SHARED_QUOTA_V1
    bool lcPartialTxAccounting=false;
    bool lcDisablePruning=false;
    bool alpsStrictPrune=false;
    uint64_t lcDataAckTimeoutNs=0;
    bool lcPathTimeoutBasePlusSwitch=false;
    bool lcPathTimeout=false;uint32_t lcPathTimeoutPackets=20;
    bool lcQueueBwScore=false,lcLinkSendPenalty=false;
    double lcBwBeta=5.0,lcBwUnitGbps=100.0,lcLinkSendAlpha=1.0;
    bool lcSendPenalty=false;
    bool lcAlpsProbe=true,lcCapacityBaselineProbe=false;double lcZeroBwMinProbability=.05;
    bool lcEarlyProbe=false,lcEarlyAudit=false;uint64_t lcEarlyLeadNs=20000,lcEarlyIntervalNs=1000,lcEarlyTimeoutNs=1000000;
    bool lcQueueTargetCc=false,lcDataAckCcOnly=false;
    bool lcPressureCc=false,lcPressureQueueGate=false,lcQueueRecovery=false;double lcPressureGain=1.0;
    bool lcDiagDelayWeights=false,lcWaitAudit=false;std::string lcWaitNodes="30,34";
    bool lcFctTrace=false,lcAllocationOnly=false,lcFlowPathAudit=false,lcRecoveryTrace=false,lcRateUseDelay=false,lcRateAudit=false;
    bool lcEnabled=false,lcAudit=false,lcPrune=false,lcBwAudit=false,lcDetailedLogs=true;uint32_t lcBwMode=0; // BANDWIDTH_WEIGHT_V1
    uint64_t lcWindowNs=10000,lcTtlNs=100000;
    uint32_t lcSeed=7;
    std::string lcDirectory=".";
    global_variable_t varMap;
    varMap.configFileName = "/file-in-ctr/inputFiles/C00003/CONFIG_DCQCN.txt";
    std::cout << "*******************************Parse the Default Configures*****************************************" << std::endl;
    parse_default_configures(&varMap);
    std::cout << "*******************************Parse the Input Parameters*****************************************" << std::endl;
    CommandLine cmd;
    cmd.AddValue("configFileName", "configFileName", varMap.configFileName);
    cmd.AddValue("topoFileName", "topoFileName", varMap.topoFileName);
    cmd.AddValue("inputFileDir", "input File Directory", varMap.inputFileDir);
    cmd.AddValue("outputFileDir", "output File Directory", varMap.outputFileDir);
    cmd.AddValue("fileIdx", "fileIdx", varMap.fileIdx);
    cmd.AddValue("simStartTimeInSec", "simStartTimeInSec", varMap.simStartTimeInSec);
    cmd.AddValue("simEndTimeInSec", "simEndTimeInSec", varMap.simEndTimeInSec);
    cmd.AddValue("flowLunchStartTimeInSec", "flowLunchStartTimeInSec", varMap.flowLunchStartTimeInSec);
    cmd.AddValue("flowLunchEndTimeInSec", "flowLunchEndTimeInSec", varMap.flowLunchEndTimeInSec);
    cmd.AddValue("qlenMonitorIntervalInNs", "qlenMonitorIntervalInNs", varMap.qlenMonitorIntervalInNs);
    cmd.AddValue("lbsName", "Name of the load balancing solution.", varMap.lbsName);
    cmd.AddValue("flowletTimoutInUs", "The time out of the flowlet in microsecond.", varMap.flowletTimoutInUs);
    cmd.AddValue("loadRatio", "The load ratio of the server workload.", varMap.loadRatio);
    cmd.AddValue("loadRatioShift", "The load AdjustFacror of the server workload.", varMap.loadRatioShift);
    cmd.AddValue("workloadFile", "The load source of the workload.", varMap.workLoadFileName);
    cmd.AddValue("patternFile", "The server run pattern.", varMap.patternFile);
    cmd.AddValue("SMTFile", "init The LAPS LB SMT map.", varMap.smtFile);
    cmd.AddValue("PITFile", "init The LAPS LB PIT map.", varMap.pitFile);
    cmd.AddValue("PSTFile", "init The LAPS LB PST map.", varMap.pstFile);
    cmd.AddValue("ccMode", "congestion control algorithm.", varMap.ccMode);
    //-------------------------------------------------------------------------添加控制台输入--------------------------------------------------
    //cmd.AddValue("enable_laps2", "控制LAPS修改版是否启用.", varMap.enable_laps2);
    cmd.AddValue("screenDisplayInNs", "creen display interval in Ns.", varMap.screenDisplayInNs);
    cmd.AddValue("enablePfcMonitor", "trace Pfc packets or not.", varMap.enablePfcMonitor);
    cmd.AddValue("enableFctMonitor", "trace Fct or not", varMap.enableFctMonitor);
    cmd.AddValue("enableQlenMonitor", "trace queue length or not.", varMap.enableQlenMonitor);
    cmd.AddValue("rdmaAppStartPort", "minimal port for rdma client.", varMap.appStartPort);
    cmd.AddValue("enableQbbTrace", "trace the packet event on node's all Qbb netdevices.", varMap.enableQbbTrace);
    cmd.AddValue("testPktNum", "The number of packets to test.", varMap.testPktNum);
    cmd.AddValue("enableFlowCongestTest", "creat network congestion Test", varMap.enableFlowCongestTest);
    cmd.AddValue("enableLLMWorkLoadTest", "creat LLM Work Load Test", varMap.enableLLMWorkLoadTest);

    cmd.AddValue("lcQuotaAudit","Audit shared-budget vectors and every resource constraint",lcQuotaAudit);
    cmd.AddValue("lcPartialTxAccounting","Count cross-window packets by serialized fraction, carrying the remaining fraction forward",lcPartialTxAccounting);
    cmd.AddValue("lcDisablePruning","Disable queue-driven path masks and all-path pause, preserve original bandwidth rate control",lcDisablePruning);
    cmd.AddValue("alpsStrictPrune","Restore original commented latency/freshness pruning, with pre-dequeue all-path gate",alpsStrictPrune);
    cmd.AddValue("lcDataAckTimeoutNs","Fixed DATA ACK deadline in ns; zero keeps per-path formula; 8000000 reproduces 8ms magnitude",lcDataAckTimeoutNs);
    cmd.AddValue("lcPathTimeoutBasePlusSwitch","Use DATA/ACK static RTT plus 20-packet allowance per forward switch egress",lcPathTimeoutBasePlusSwitch);
    cmd.AddValue("lcPathTimeout","Path-specific ACK deadline, unavailable state and FIFO ghost recovery",lcPathTimeout);
    cmd.AddValue("lcPathTimeoutPackets","DATA packet allowance per hop for the new timeout threshold",lcPathTimeoutPackets);
    cmd.AddValue("lcLinkSendAlpha","Source-local send-work gain in [0,1]; 0=zero local penalty, 1=test33",lcLinkSendAlpha);
    cmd.AddValue("lcLinkSendPenalty","Predict source-local link work since last ACK using 1048B packets and the queue drain horizon",lcLinkSendPenalty);
    cmd.AddValue("lcQueueBwScore","Signed queue-adjusted bandwidth score; threshold drain horizon, no extra wait",lcQueueBwScore);
    cmd.AddValue("lcBwBeta","Nonnegative beta for exp(beta*budgetGbps/unitGbps); independent of ALPS choose_softmax",lcBwBeta);
    cmd.AddValue("lcBwUnitGbps","Bandwidth unit in the exponential; default 100 Gbps",lcBwUnitGbps);
    cmd.AddValue("lcSendPenalty","Apply the original 84ns send-counter cost once to bandwidth probabilities",lcSendPenalty);
    cmd.AddValue("lcAlpsProbe","Original ALPS expiry/cooldown/random probing; pending factor .5; default true",lcAlpsProbe);
    cmd.AddValue("lcCapacityBaselineProbe","Probe if complete capacity feedback is older than twice this path reference",lcCapacityBaselineProbe);
    cmd.AddValue("lcZeroBwMinProbability","Final probability floor for zero-budget non-pruned paths; default .05",lcZeroBwMinProbability);
    cmd.AddValue("lcEarlyProbe","Maintain LST ahead of expiry using native ghost slots",lcEarlyProbe);
    cmd.AddValue("lcEarlyAudit","Probe events, physical source transmit counters and expiry fallback audit",lcEarlyAudit);
    cmd.AddValue("lcEarlyLeadNs","Minimum lead; enlarged to measured probe RTT plus two estimator windows",lcEarlyLeadNs);
    cmd.AddValue("lcEarlyIntervalNs","Minimum interval between any two source ghost sends",lcEarlyIntervalNs);
    cmd.AddValue("lcEarlyTimeoutNs","Probe retry timeout floor; also at least four smoothed RTTs",lcEarlyTimeoutNs);
    cmd.AddValue("lcRateUseDelay","Ablation: original delay predicate for total rate only",lcRateUseDelay);
    cmd.AddValue("lcRateAudit","Read-only logging of delay and queue rate predicates",lcRateAudit);
    cmd.AddValue("lcRecoveryTrace","Read-only DATA ACK loss-decision and RTO diagnosis",lcRecoveryTrace);
    cmd.AddValue("lcFlowPathAudit","Read-only unique DATA payload accounting per flow and path",lcFlowPathAudit);
    cmd.AddValue("lcAllocationOnly","Use current per-path cached bandwidth allocator with original ALPS CC and probes",lcAllocationOnly);
    cmd.AddValue("lcFctTrace","Read-only packet timestamps for flows <=100000 bytes",lcFctTrace);
    cmd.AddValue("lcEnabled","Enable capacity measurement/feedback/LST only",lcEnabled);
    cmd.AddValue("lcQueueAlarmBytes","Independent queue alarm bytes; default 100000; 0 enables legacy Kmax percentage",lcQueueAlarmBytes);
    cmd.AddValue("lcPathOnlyQueue","Own complete ACK queue maximum only; no shared queue propagation",lcPathOnlyQueue);
    cmd.AddValue("lcMaxLinkQueue","Maximum rate-normalized forward-egress queue and shared-link pruning",lcMaxLinkQueue);
    cmd.AddValue("lcAlpsCongestion","Original ALPS latency>path reference for pruning, rate and gate; default false",lcAlpsCongestion);
    cmd.AddValue("lcRequireZeroBw","Require fresh zero bottleneck AND path queue alarm; default true",lcRequireZeroBw);
    cmd.AddValue("lcPathQueueMax","ACK running max of source-route forward-egress total queue; default true",lcPathQueueMax);
    cmd.AddValue("lcQueueMaxAll","Maximum individual egress queue; full bandwidth CC; takes priority over sum",lcQueueMaxAll);
    cmd.AddValue("lcQueueSumAll","Sum all eight egress queues for queue alarms; default true",lcQueueSumAll);
    cmd.AddValue("lcQueuePercent","Legacy Kmax percentage only when absolute alarm bytes are zero; default 100",lcQueuePercent);
    cmd.AddValue("lcBwCc","Use queue alarm, historical-budget fallback and 20% retained capacity while probing",lcBwCc);
    cmd.AddValue("lcSizeAware","Small flows pin a delay-weighted path; large flows retain shared bandwidth spraying",lcSizeAware);
    cmd.AddValue("lcSizeReserve","Enable endpoint class pacing and class-specific bandwidth accounting",lcSizeReserve);
    cmd.AddValue("lcSmallFlowBytes","Small transfer payload cutoff; default 100000 B",lcSmallFlowBytes);
    cmd.AddValue("lcSmallReservePercent","Small-class capacity percentage; fixed per run, no feedback controller",lcSmallReservePercent);
    cmd.AddValue("lcDynamicSmall","Dynamic endpoint path protection from pending small bytes",lcDynamicSmall);
    cmd.AddValue("lcSmallTargetNs","Pending work conversion horizon, not an FCT deadline",lcSmallTargetNs);
    cmd.AddValue("lcFeedbackResume","Wake paused DATA on ACK feedback; false replays old 5us polling",lcFeedbackResume);
    cmd.AddValue("lcBwCcAudit","Record bandwidth CC rate actions and probe-only pauses",lcBwCcAudit);
    cmd.AddValue("lcBwCcUniformFallback","Diagnostic only: uniform fallback for missing/all-zero budgets",lcBwCcUniformFallback);
    cmd.AddValue("lcPrune","Enable optional shared-link path masking",lcPrune);
    cmd.AddValue("lcBwMode","0=delay, 1=independent path minimum, 2=shared quota progressive filling",lcBwMode);
    cmd.AddValue("lcBwAudit","Audit actual DATA path choices and fallback coverage",lcBwAudit);
    cmd.AddValue("lcDetailedLogs","Write detailed per-packet telemetry logs",lcDetailedLogs);
    cmd.AddValue("lcAudit","Record planned and completed flows in both comparison groups",lcAudit);
    cmd.AddValue("lcWindowNs","Switch measurement window in nanoseconds",lcWindowNs);
    cmd.AddValue("lcTtlNs","Maximum measurement age in nanoseconds",lcTtlNs);
    cmd.AddValue("lcDirectory","Existing output directory for capacity logs",lcDirectory);
    cmd.AddValue("lcSeed","Matched audit seed (used only with lcAudit)",lcSeed);
    // Expose the same existing switches as laps_share/executableFiles/C00001/main.cc.
    cmd.AddValue("enable_laps_plus","Enable the existing ALPS branch",varMap.enable_laps_plus);
    cmd.AddValue("choose_softmax","Existing ALPS weighting variant",varMap.choose_softmax);
    cmd.AddValue("enbaleRateTrace","Existing rate trace switch",varMap.enbaleRateTrace);
    cmd.AddValue("enableProbeTrace","Existing probe trace switch",varMap.enableProbeTrace);
    cmd.AddValue("enablePathDelayTraceForLaps","Existing path-delay trace switch",varMap.enablePathDelayTraceForLaps);
    cmd.AddValue("lcQueueTargetCc","TEST44: reference pressure to the existing target queue; no new telemetry",lcQueueTargetCc);
    cmd.AddValue("lcDataAckCcOnly","TEST44: probe ACKs refresh state but only DATA ACKs actuate total rate",lcDataAckCcOnly);
    cmd.AddValue("lcPressureGain","TEST42: decrease-target relaxation, 1 preserves test41; .2 limits actual decrease to 10 percent",lcPressureGain);
    cmd.AddValue("lcQueueRecovery","TEST41: allow original rate increase on a fresh complete below-threshold queue path",lcQueueRecovery);
    cmd.AddValue("lcPressureQueueGate","TEST40: require original raw all-active-path queue congestion before pressure decrease",lcPressureQueueGate);
    cmd.AddValue("lcPressureCc","TEST39: all active paths pressured; least path controls proportional decrease capped at half",lcPressureCc);
    cmd.AddValue("lcDiagDelayWeights","Explicit ALPS allocator arm only; retain identical bandwidth measurement/probes/control",lcDiagDelayWeights);
    cmd.AddValue("lcWaitAudit","Passive source wait-state and NIC timeline",lcWaitAudit);
    cmd.AddValue("lcWaitNodes","Comma separated source node IDs for passive wait diagnosis",lcWaitNodes);
    cmd.Parse(argc, argv);
    NS_ABORT_MSG_IF(lcPressureCc && (!lcEnabled || !lcQueueBwScore || !lcBwCc || lcBwMode!=2 || lcRateUseDelay || lcDiagDelayWeights || !lcDisablePruning || lcSizeAware),"Pressure pilot requires unchanged full-bandwidth allocation and no queue pruning");
    NS_ABORT_MSG_IF(lcPressureQueueGate && (!lcPressureCc || !lcMaxLinkQueue || lcPathOnlyQueue || lcAlpsCongestion),"Queue gate requires original shared-link raw queue predicate and pressure mode");
    NS_ABORT_MSG_IF(lcQueueRecovery && !lcPressureQueueGate,"Queue recovery requires the unchanged full-path queue decrease gate");
    NS_ABORT_MSG_IF(!std::isfinite(lcPressureGain) || lcPressureGain<=0 || lcPressureGain>1,"Pressure gain must be finite and in (0,1]");
    NS_ABORT_MSG_IF(lcPressureGain!=1.0 && (!lcPressureCc || !lcPressureQueueGate || !lcQueueRecovery),"Gentle decrease requires unchanged test41 controller");
    NS_ABORT_MSG_IF((lcQueueTargetCc || lcDataAckCcOnly) && (!lcPressureCc || !lcPressureQueueGate || !lcQueueRecovery || lcPressureGain!=1.0),"TEST44 requires test41 configuration and gain one");
    RdmaSmartFlowRouting::lcQueueTargetCc=lcQueueTargetCc;RdmaSmartFlowRouting::lcDataAckCcOnly=lcDataAckCcOnly;
    if(lcQueueTargetCc){
        RdmaSmartFlowRouting::lcQueueTargetLog.open(lcDirectory+"/queue-target-decisions.csv");
        NS_ABORT_MSG_IF(!RdmaSmartFlowRouting::lcQueueTargetLog,"Cannot open queue-target diagnostics");
        RdmaSmartFlowRouting::lcQueueTargetLog<<"time_ns,node,flow,raw_u,referenced_v,epsilon,old_rate_bps,candidate_rate_bps,applied,target_before,target_after,stage_before,stage_after,next_dec_before,next_dec_after,last_dec_before,last_dec_after,next_inc_before,next_inc_after\n";
    }
    if(lcQueueTargetCc || lcDataAckCcOnly){
        RdmaSmartFlowRouting::lcCcFeedbackLog.open(lcDirectory+"/cc-feedback-source.csv");
        NS_ABORT_MSG_IF(!RdmaSmartFlowRouting::lcCcFeedbackLog,"Cannot open ACK actuator diagnostics");
        RdmaSmartFlowRouting::lcCcFeedbackLog<<"time_ns,node,flow,source,executed,rate_before,rate_after\n";
    }
    std::cout<<"QUEUE_TARGET_CC_CONFIG enabled="<<lcQueueTargetCc<<" data_ack_only="<<lcDataAckCcOnly<<" target_U=2 dynamic_quantum_deadband=1 no_new_QP_ACK_LST_fields=1"<<std::endl;
    RdmaSmartFlowRouting::lcPressureGain=lcPressureGain;
    RdmaSmartFlowRouting::lcQueueRecovery=lcQueueRecovery;
    RdmaSmartFlowRouting::lcPressureQueueGate=lcPressureQueueGate;
    RdmaSmartFlowRouting::lcPressureCc=lcPressureCc;
    if(lcPressureCc){
        RdmaSmartFlowRouting::lcPressureEvents.open(lcDirectory+"/pressure-rate-events.csv");
        RdmaSmartFlowRouting::lcPressurePaths.open(lcDirectory+"/pressure-rate-paths.csv");
        RdmaSmartFlowRouting::lcPressureLinks.open(lcDirectory+"/pressure-rate-links.csv");
        NS_ABORT_MSG_IF(!RdmaSmartFlowRouting::lcPressureEvents || !RdmaSmartFlowRouting::lcPressurePaths || !RdmaSmartFlowRouting::lcPressureLinks,"Cannot open pressure controller audit");
        RdmaSmartFlowRouting::lcPressureEvents<<"time_ns,node,flow,event,active_paths,complete_paths,congested_paths,any_healthy,least_u,old_rate_bps,new_rate_bps,next_dec_ns,next_inc_ns,evidence_ns,last_dec_ns";
        if(lcPressureQueueGate)RdmaSmartFlowRouting::lcPressureEvents<<",queue_gate_passed,queue_active_paths,queue_congested_paths,queue_known_paths";
        if(lcQueueRecovery)RdmaSmartFlowRouting::lcPressureEvents<<",queue_healthy_paths";
        RdmaSmartFlowRouting::lcPressureEvents<<'\n';
        RdmaSmartFlowRouting::lcPressurePaths<<"time_ns,node,flow,path,unavailable,links,fresh,complete,congested,max_u,argmax_link,evidence_ns";
        if(lcPressureQueueGate)RdmaSmartFlowRouting::lcPressurePaths<<",raw_queue_congested,raw_queue_known";
        RdmaSmartFlowRouting::lcPressurePaths<<'\n';
        RdmaSmartFlowRouting::lcPressureLinks<<"time_ns,node,flow,link,valid,capacity_bps,raw_score_q,u,born_ns\n";
    }
    std::cout<<"PRESSURE_CC_CONFIG enabled="<<lcPressureCc<<" path=max_link flow=min_path all_active_paths_must_be_high=1 single_decrease_limit=0.5 existing_cooldown=1 fresh_post_decrease_evidence=1 raw_score_only=1"<<std::endl;
    NS_ABORT_MSG_IF(lcDiagDelayWeights && (!lcBwCc || lcBwMode!=2 || !lcDisablePruning || lcSizeAware),"Delay allocator ablation requires unchanged full-mode supporting mechanisms");
    RdmaSmartFlowRouting::lcDiagDelayWeights=lcDiagDelayWeights;RdmaSmartFlowRouting::lcWaitAudit=lcWaitAudit;
    if(lcWaitAudit){
        std::istringstream nodes(lcWaitNodes);std::string item;
        while(std::getline(nodes,item,','))RdmaSmartFlowRouting::lcWaitNodes.insert(uint32_t(std::stoul(item)));
        RdmaSmartFlowRouting::lcWaitStates.open(lcDirectory+"/wait-states.csv");
        RdmaSmartFlowRouting::lcWaitNic.open(lcDirectory+"/wait-nic-transmits.csv");
        RdmaSmartFlowRouting::lcWaitChoices.open(lcDirectory+"/wait-path-decisions.csv");
        NS_ABORT_MSG_IF(!RdmaSmartFlowRouting::lcWaitStates || !RdmaSmartFlowRouting::lcWaitNic || !RdmaSmartFlowRouting::lcWaitChoices,"Cannot open wait audit");
        RdmaSmartFlowRouting::lcWaitStates<<"time_ns,node,port,event,flow,pending_bytes,finished,rate_bps,target_rate_bps,next_avail_ns,effective_next_ns,recovery_at_ns,inflight_bytes,window_bytes,window_blocked,irn_allowed,pg,pfc_paused,nic_busy,nic_owner_flow,nic_kind,nic_busy_until_ns,ack_queue_packets,path_count,unavailable_paths,next_decrease_ns,next_increase_ns,snd_nxt,snd_una,max_next_seq,lossy_bytes,next_event_ns\n";
        RdmaSmartFlowRouting::lcWaitNic<<"start_ns,end_ns,node,port,flow,kind,wire_bytes,uid\n";
        RdmaSmartFlowRouting::lcWaitChoices<<"time_ns,node,flow,seq,path,selected_probability,latency_ns,reference_ns,rate_bps\n";
    }
    std::cout<<"TEST38_ABLATION delay_allocator="<<lcDiagDelayWeights<<" delay_rate="<<lcRateUseDelay<<" wait_audit="<<lcWaitAudit<<std::endl;
    NS_ABORT_MSG_IF(lcBwMode && !lcEnabled,"Bandwidth weighting requires link feedback");
    NS_ABORT_MSG_IF(lcAllocationOnly && (!lcEnabled || lcBwMode!=2 || lcBwCc || lcEarlyProbe || lcPrune),"Allocation-only test must preserve original CC/probes");
    NS_ABORT_MSG_IF(lcSendPenalty && (!lcBwCc || lcBwMode!=2 || lcSizeAware || !varMap.enable_laps_plus),"Send penalty pilot requires full bandwidth ALPS mode and no size split");
    NS_ABORT_MSG_IF(lcPathTimeout && (!lcBwCc || lcBwMode!=2 || !lcEarlyProbe || lcSizeAware || !lcPathTimeoutPackets),"Path timeout requires full bandwidth mode and no size split");
    NS_ABORT_MSG_IF(lcPathTimeoutBasePlusSwitch && !lcPathTimeout,"Corrected timeout formula requires the path-timeout mode");
    NS_ABORT_MSG_IF(lcDataAckTimeoutNs && !lcPathTimeout,"Fixed DATA ACK budget requires timeout protocol mode");
    NS_ABORT_MSG_IF(alpsStrictPrune && (!varMap.enable_laps_plus || lcBwCc || lcBwMode || lcEnabled || lcSizeAware || lcPathTimeout),"Original strict-pruning control requires original ALPS allocation and control");
    NS_ABORT_MSG_IF(lcDisablePruning && (!lcBwCc || lcBwMode!=2 || lcAllocationOnly || alpsStrictPrune),"No-pruning ablation requires full bandwidth mode with unchanged rate control");
    NS_ABORT_MSG_IF(lcPartialTxAccounting && (lcSizeAware || lcSizeReserve || lcDynamicSmall),"Partial TX accounting pilot does not combine with class-specific reservation counters");
    SwitchNode::lcPartialTxAccounting=lcPartialTxAccounting;
    std::cout<<"PARTIAL_TX_ACCOUNTING_CONFIG enabled="<<lcPartialTxAccounting<<" actual_serialization_duration=1 all_wire_packets=1 carry_across_windows=1"<<std::endl;
    RdmaSmartFlowRouting::lcDisablePruning=lcDisablePruning;
    if(lcDisablePruning){
        RdmaSmartFlowRouting::lcNoPruneDataLog.open(lcDirectory+"/bandwidth-no-prune-data.csv");
        RdmaSmartFlowRouting::lcNoPruneDataLog<<"time_ns,node,flow,path,seq,raw_congested,protocol_unavailable,paths,selected_raw_congested,selected_protocol_unavailable,selected_weight,sum_weight\n";
        NS_ABORT_MSG_IF(!RdmaSmartFlowRouting::lcNoPruneDataLog,"Cannot open no-pruning DATA audit");
    }
    std::cout<<"NO_QUEUE_PRUNING_CONFIG enabled="<<lcDisablePruning<<" raw_cc_unchanged=1 protocol_timeout_retained=1"<<std::endl;
    RdmaSmartFlowRouting::alpsStrictPrune=alpsStrictPrune;
    if(alpsStrictPrune){
        RdmaSmartFlowRouting::alpsStrictDataLog.open(lcDirectory+"/alps-strict-data.csv");
        RdmaSmartFlowRouting::alpsStrictDataLog<<"time_ns,node,flow,path,seq,blocked,paths,delay_ns,reference_ns,generation_ns,selected_weight,sum_weight,pruned_weight_max\n";
        RdmaSmartFlowRouting::alpsStrictEventLog.open(lcDirectory+"/alps-strict-events.csv");
        RdmaSmartFlowRouting::alpsStrictEventLog<<"time_ns,node,flow,event,retry_ns,pacing_next_ns,snd_nxt\n";
        NS_ABORT_MSG_IF(!RdmaSmartFlowRouting::alpsStrictDataLog || !RdmaSmartFlowRouting::alpsStrictEventLog,"Cannot open strict-pruning audit");
    }
    std::cout<<"ALPS_STRICT_RESTORE_CONFIG enabled="<<alpsStrictPrune<<" original_predicate=1 original_reference_unchanged=1 feedback_resume=1 age_expiry_resume=1"<<std::endl;
    RdmaSmartFlowRouting::lcDataAckTimeoutNs=lcDataAckTimeoutNs;
    RdmaSmartFlowRouting::lcPathTimeoutBasePlusSwitch=lcPathTimeoutBasePlusSwitch;
    std::cout<<"PATH_TIMEOUT_FORMULA_CONFIG enabled="<<lcPathTimeout<<" baseline_plus_switch_allowance="<<lcPathTimeoutBasePlusSwitch<<" allowance_packets="<<lcPathTimeoutPackets<<std::endl;
    RdmaSmartFlowRouting::lcPathTimeout=lcPathTimeout;RdmaSmartFlowRouting::lcPathTimeoutPackets=lcPathTimeoutPackets;
    if(lcPathTimeout){
        RdmaSmartFlowRouting::lcPathTimeoutLog.open(lcDirectory+"/path-timeout-events.csv");
        RdmaSmartFlowRouting::lcPathTimeoutLog<<"event,time_ns,node,path,token,unavailable,pending,queued,shift,sent_ns,next_ns,delay_threshold_ns,ack_timeout_ns,detail,lost";
        if(lcDataAckTimeoutNs)RdmaSmartFlowRouting::lcPathTimeoutLog<<",data_ack_timeout_ns";
        RdmaSmartFlowRouting::lcPathTimeoutLog<<'\n';
        RdmaSmartFlowRouting::lcPathTimeoutThresholds.open(lcDirectory+"/path-timeout-thresholds.csv");
        RdmaSmartFlowRouting::lcPathTimeoutThresholds<<"path,source,hops,packet_bytes,packet_count,delay_threshold_ns,ack_bytes,ack_static_ns,ack_timeout_ns,legacy_reference_ns\n";
    }
    std::cout<<"DATA_ACK_TIMEOUT_CONFIG fixed_ns="<<lcDataAckTimeoutNs<<" oldest_unacknowledged_clock=1 ghost_wait_unchanged=1"<<std::endl;
    NS_ABORT_MSG_IF(lcBwMode && lcSendPenalty,"Pure bandwidth weighting forbids the ALPS 84ns send-counter penalty; set lcSendPenalty=0");
    NS_ABORT_MSG_IF(lcBwMode && (!std::isfinite(lcBwBeta) || lcBwBeta<0 || !std::isfinite(lcBwUnitGbps) || lcBwUnitGbps<=0),"Invalid bandwidth exponential scale");
    NS_ABORT_MSG_IF(lcBwMode && lcSizeAware,"Pure-bandwidth pilot does not combine the former latency-based small-flow selector");
    NS_ABORT_MSG_IF(lcQueueBwScore && (!lcBwCc || lcBwMode!=2 || !lcMaxLinkQueue || !lcPathQueueMax || !lcQueueAlarmBytes || lcSizeAware || lcAllocationOnly || lcRequireZeroBw),"Queue score requires full bandwidth mode and queue-only raw control");
    NS_ABORT_MSG_IF(lcLinkSendPenalty && !lcQueueBwScore,"Link send penalty requires signed queue-bandwidth scores");
    NS_ABORT_MSG_IF(!std::isfinite(lcLinkSendAlpha) || lcLinkSendAlpha<0 || lcLinkSendAlpha>1,"Local send penalty alpha must be finite and in [0,1]");
    RdmaSmartFlowRouting::lcLinkSendAlpha=lcLinkSendAlpha;
    RdmaSmartFlowRouting::lcLinkSendPenalty=lcLinkSendPenalty;
    std::cout<<"LINK_SEND_PENALTY_CONFIG enabled="<<lcLinkSendPenalty<<" alpha="<<lcLinkSendAlpha<<" packet_bytes=1048 horizon_ns="<<(8.0*lcQueueAlarmBytes/1e11*1e9)<<" counters=since_feedback all_local_destinations=1 no_observation_writeback=1"<<std::endl;
    CompactLinkState::queueScoreEnabled=lcQueueBwScore;
    std::cout<<"QUEUE_BANDWIDTH_SCORE_CONFIG enabled="<<lcQueueBwScore<<" horizon_ns="<<(8.0*lcQueueAlarmBytes/1e11*1e9)<<" threshold_scaled_bytes="<<lcQueueAlarmBytes<<" signed_score=1 LST_bytes="<<sizeof(CompactLinkState)<<std::endl;
    RdmaSmartFlowRouting::lcBwBeta=lcBwBeta;RdmaSmartFlowRouting::lcBwUnitGbps=lcBwUnitGbps;
    std::cout<<"BANDWIDTH_EXPONENTIAL_CONFIG enabled="<<(lcBwMode!=0)<<" beta="<<lcBwBeta<<" unit_gbps="<<lcBwUnitGbps<<" latency_weight_fallback=0 send_latency_penalty=0 unknown_prior_budget_gbps=0"<<std::endl;
    RdmaSmartFlowRouting::lcSendPenalty=lcSendPenalty;
    std::cout<<"BW_SEND_PENALTY_CONFIG enabled="<<lcSendPenalty<<" tau_ns=84 beta_from_choose_softmax=1 counter_once=1 raw_budget_unchanged=1"<<std::endl;
    RdmaSmartFlowRouting::lcAllocationOnly=lcAllocationOnly;
    NS_ABORT_MSG_IF(lcRecoveryTrace && !lcFlowPathAudit,"Recovery diagnosis requires flow identity audit");
    RdmaSmartFlowRouting::lcRecoveryTrace=lcRecoveryTrace;
    NS_ABORT_MSG_IF(lcRateUseDelay && !lcBwCc,"Rate-predicate ablation requires full bandwidth mode");
    RdmaSmartFlowRouting::lcRateUseDelay=lcRateUseDelay;RdmaSmartFlowRouting::lcRateAudit=lcRateAudit;
    if(lcRateAudit) {
        RdmaSmartFlowRouting::lcRateSignals.open(lcDirectory+"/rate-signal-comparison.csv");
        RdmaSmartFlowRouting::lcRateSignals<<"time_ns,node,flow,event,delay_congested,queue_congested,queue_known,total_paths,old_rate_bps,new_rate_bps,selected_signal\n";
    }

    if(lcRecoveryTrace) {
        RdmaSmartFlowRouting::lcRecoveryEvents.open(lcDirectory+"/recovery-events.csv");
        RdmaSmartFlowRouting::lcRecoveryEvents<<"time_ns,event,node,path,flow,seq,bytes,trigger_flow,trigger_seq,uid,port\n";
        recoveryPackets.open(lcDirectory+"/recovery-packets.csv");
        recoveryPackets<<"time_ns,uid,event,node,flow,path,seq,payload_bytes,wire_bytes,probe,ack_data_seq,ack_data_bytes\n";
    }

    update_EST(varMap.paraMap, "loadRatio", varMap.loadRatio);

    std::cout << "*******************************Load the Default Configures*****************************************" << std::endl;
    load_default_configures(&varMap);
    RdmaSmartFlowRouting::lcBwMode=lcBwMode;RdmaSmartFlowRouting::lcBwAudit=lcBwAudit;RdmaSmartFlowRouting::lcDetailedLogs=lcDetailedLogs; // BANDWIDTH_WEIGHT_V1
    RdmaSmartFlowRouting::ConfigureLinkCapacity(lcEnabled,lcWindowNs,lcTtlNs,
        Seconds(varMap.simEndTimeInSec).GetNanoSeconds(),lcDirectory);
    RdmaSmartFlowRouting::lcQuotaAudit=lcQuotaAudit;RdmaSmartFlowRouting::ConfigureSharedQuotaAudit(lcDirectory); // SHARED_QUOTA_V1
    RdmaSmartFlowRouting::lcAlpsProbe=lcEarlyProbe && lcAlpsProbe;
    RdmaSmartFlowRouting::lcCapacityBaselineProbe=lcBwCc && lcCapacityBaselineProbe && !lcAlpsProbe;
    RdmaSmartFlowRouting::lcZeroBwMinProbability=lcBwCc?lcZeroBwMinProbability:0;
    RdmaSmartFlowRouting::lcEarlyProbe=lcEarlyProbe;RdmaSmartFlowRouting::lcEarlyAudit=lcEarlyAudit;
    RdmaSmartFlowRouting::lcEarlyLeadNs=lcEarlyLeadNs;RdmaSmartFlowRouting::lcEarlyIntervalNs=lcEarlyIntervalNs;
    RdmaSmartFlowRouting::lcEarlyTimeoutNs=lcEarlyTimeoutNs;RdmaSmartFlowRouting::ConfigureEarlyProbeAudit(lcDirectory);
    NS_ABORT_MSG_IF(lcQueuePercent==0 || lcQueuePercent>100,"Invalid queue threshold percentage");
    RdmaSmartFlowRouting::lcQueuePercent=lcQueuePercent;
    RdmaSmartFlowRouting::lcQueueAlarmBytes=lcQueueAlarmBytes;
    NS_ABORT_MSG_IF(lcBwCc && lcPathQueueMax && !lcQueueAlarmBytes,"Path queue maximum requires a positive absolute byte threshold");
    NS_ABORT_MSG_IF(lcBwCc && lcRequireZeroBw && (!lcPathQueueMax || lcDynamicSmall),"Joint zero/queue predicate requires path maximum and no dynamic reservations");
    NS_ABORT_MSG_IF(lcMaxLinkQueue && lcBwCc && (!lcPathQueueMax || !lcQueueAlarmBytes),"Max-link mode requires path ACK collection and positive threshold");
    RdmaSmartFlowRouting::lcPathOnlyQueue=lcPathOnlyQueue;
    RdmaSmartFlowRouting::lcMaxLinkQueue=lcMaxLinkQueue;
    RdmaSmartFlowRouting::lcAlpsCongestion=lcAlpsCongestion;
    RdmaSmartFlowRouting::lcRequireZeroBw=lcRequireZeroBw;
    RdmaSmartFlowRouting::lcPathQueueMax=lcPathQueueMax;
    RdmaSmartFlowRouting::lcQueueMaxAll=lcQueueMaxAll;
    RdmaSmartFlowRouting::lcQueueSumAll=lcQueueSumAll;
    std::cout<<"QUEUE_ALARM_CONFIG bytes="<<lcQueueAlarmBytes<<" sum_all="<<lcQueueSumAll
             <<" percent="<<(lcQueueAlarmBytes?100:lcQueuePercent)<<" legacy_percent="<<lcQueuePercent
             <<" require_zero="<<lcRequireZeroBw<<" max_all="<<lcQueueMaxAll<<" path_max="<<lcPathQueueMax<<" mode="<<(lcBwCc && lcPathQueueMax?"ack_path_max_egress":(lcBwCc && lcQueueMaxAll?"max_queue":(lcQueueSumAll?"sum_queues":"data_queue")))<<std::endl;
    std::cout<<"CONGESTION_SIGNAL_CONFIG mode="<<(lcBwCc && lcMaxLinkQueue?(lcPathOnlyQueue?"path_only_scaled_queue":"max_scaled_link_queue"):(lcBwCc && lcAlpsCongestion?"alps_latency_reference":"legacy_queue_capacity"))<<" byte_threshold_active="<<(lcBwCc && (lcMaxLinkQueue || !lcAlpsCongestion))<<" zero_gate_active="<<(lcBwCc && lcRequireZeroBw && (lcMaxLinkQueue?lcPathOnlyQueue:!lcAlpsCongestion))<<std::endl;
    RdmaSmartFlowRouting::lcBwCc=lcBwCc;RdmaSmartFlowRouting::lcBwCcAudit=lcBwCcAudit;
    NS_ABORT_MSG_IF(lcBwCc && lcCapacityBaselineProbe && (!lcEarlyProbe || !lcPathQueueMax),"Baseline capacity probing requires probes and complete path ACK feedback");
    RdmaSmartFlowRouting::ConfigureCapacityFloorAudit(lcDirectory);
    std::cout<<"CAPACITY_FLOOR_CONFIG baseline_probe="<<RdmaSmartFlowRouting::lcCapacityBaselineProbe<<" zero_final_probability="<<(lcBwCc?lcZeroBwMinProbability:0)<<" queue_bytes="<<lcQueueAlarmBytes<<std::endl;
    std::cout<<"ALPS_PROBE_CONFIG enabled="<<RdmaSmartFlowRouting::lcAlpsProbe<<" pending_factor="<<(RdmaSmartFlowRouting::lcAlpsProbe?0.5:0.2)<<" trigger=max_current_delay_x2 cooldown=max_expired_delay_x2 selection=std_rand_mod retry=cooldown_only"<<std::endl;
    RdmaSmartFlowRouting::lcFeedbackResume=lcFeedbackResume;
    NS_ABORT_MSG_IF(lcSizeAware && (!lcEnabled || lcBwMode!=2 || !lcBwCc),"Size-aware experiment requires full shared-bandwidth feedback mode");
    NS_ABORT_MSG_IF(lcSizeReserve && !lcSizeAware,"Class reservation requires size-aware classification");
    NS_ABORT_MSG_IF(!lcSmallFlowBytes,"Small-flow cutoff must be positive");
    NS_ABORT_MSG_IF(lcDynamicSmall && (!lcSizeReserve || !lcSmallTargetNs),"Dynamic protection requires size reservation and a positive horizon");
    RdmaSmartFlowRouting::lcDynamicSmall=lcDynamicSmall;RdmaSmartFlowRouting::lcSmallTargetNs=lcSmallTargetNs;
    RdmaSmartFlowRouting::lcSizeAware=lcSizeAware;RdmaSmartFlowRouting::lcSizeReserve=lcSizeReserve;
    RdmaSmartFlowRouting::lcSmallFlowBytes=lcSmallFlowBytes;RdmaSmartFlowRouting::SetSmallReservePercent(lcSmallReservePercent);
    if(lcSizeAware){
        auto &flow=RdmaSmartFlowRouting::lcSizeFlowLog;flow.open(lcDirectory+"/size-flow-pins.csv");flow<<"time_ns,node,flow,bytes,event,path,a_percent\n";
        auto &link=RdmaSmartFlowRouting::lcSizeLinkLog;link.open(lcDirectory+"/size-link-samples.csv");link<<"time_ns,node,port,capacity_bps,small_budget_bps,large_budget_bps,small_bps,large_bps,other_bps,large_available_bps,valid,a_percent\n";
        auto &host=RdmaSmartFlowRouting::lcSizeHostLog;host.open(lcDirectory+"/size-host-transmits.csv");host<<"time_ns,node,flow,class,bytes,budget_bps,next_class_ns"<<(lcDynamicSmall?",pending_small_bytes,protection_bps":"")<<'\n';
        if(lcDynamicSmall){auto &dyn=RdmaSmartFlowRouting::lcDynamicSmallLog;dyn.open(lcDirectory+"/dynamic-small-paths.csv");dyn<<"time_ns,node,scope,key,pending_bytes,protection_bps,large_rate_bps,target_ns\n";}
        NS_ABORT_MSG_IF(!flow || !link || !host,"Cannot open size-aware audit logs");
    }
    NS_ABORT_MSG_IF(lcBwCc && lcFeedbackResume && !lcEarlyProbe,"Feedback resume requires independent recovery probes");
    if(lcBwCcAudit && lcFeedbackResume) {
        RdmaSmartFlowRouting::lcFeedbackResumeLog.open(lcDirectory+"/feedback-resume.csv");
        RdmaSmartFlowRouting::lcFeedbackResumeLog<<"time_ns,node,flow,event,blocked,total,pacing_ns,retry_ns,previous_retry_ns,snd_nxt\n";
    }
    RdmaSmartFlowRouting::lcBwCcUniformFallback=lcBwCcUniformFallback;
    RdmaSmartFlowRouting::ConfigureBandwidthCc(lcDirectory); // BANDWIDTH_CC_V1
    RdmaSmartFlowRouting::lcPruneEnabled=lcPrune;RdmaSmartFlowRouting::lcDataQueue=varMap.flowGroupPrio; // ACK_IMMEDIATE_V1
    if(lcAudit) {
        std::srand(lcSeed);RngSeedManager::SetSeed(lcSeed);
        RdmaSmartFlowRouting::generator.seed(lcSeed);
    }

    std::cout << "-------------------------------Create The Topology----------------------------------------" << std::endl;
    create_topology_rdma(&varMap);
    std::cout << "-------------------------------Assign The Addresses----------------------------------------" << std::endl;
    // assign_addresses(varMap.allNodes, varMap.addr2node);
    assign_node_addresses(&varMap);
    std::cout << "-------------------------------Calculate The Paths----------------------------------------" << std::endl;
    calculate_paths_for_servers(&varMap);
    std::cout << "-------------------------------Install The Routing Table----------------------------------------" << std::endl;

    // install_routing_entries_without_Pathtable(&varMap);
    install_routing_entries(&varMap);
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    // print_node_routing_tables(&varMap, 5);
    // print_node_routing_tables(&varMap, 19);
    std::cout << "-------------------------------Configure The Switch----------------------------------------" << std::endl;
    config_switch(&varMap);
    if(lcEnabled)for(uint32_t i=0;i<varMap.swNodes.GetN();++i)
        DynamicCast<SwitchNode>(varMap.swNodes.Get(i))->StartLinkCapacitySampling();

    std::cout << "-------------------------------Install The Application----------------------------------------" << std::endl;
    // install_rdma_client_on_node(&varMap, 20, 24);
    // install_rdma_client_on_node(&varMap, 20, 21, 1, 2000000000, varMap.appStartPort);
    // install_rdma_client_on_node(&varMap, 20, 30, 1, 2000000000, varMap.appStartPort + 1);
    /*for (size_t i = 0; i < 200; i += 4)
    {
        install_rdma_client_on_node(&varMap, 5, 7, 1, 20000000, varMap.appStartPort);
        install_rdma_client_on_node(&varMap, 5, 8, 1, 20000000, varMap.appStartPort + i + 1);
        install_rdma_client_on_node(&varMap, 6, 8, 1, 20000000, varMap.appStartPort + i + 2);
        install_rdma_client_on_node(&varMap, 6, 7, 1, 20000000, varMap.appStartPort + i + 3);
    }*/

    /* for (size_t i = 0; i < 200; i += 1)
     {
         install_rdma_client_on_node(&varMap, 5, 7, 1, 20000, varMap.appStartPort + i);
     }*/
    // install_rdma_client_on_node(&varMap, 5, 7, 1, 3000, varMap.appStartPort);

    // install_rdma_client_on_node(&varMap, 5, 8, 1, 2000, varMap.appStartPort + 1);
    // install_rdma_client_on_node(&varMap, 37, 57, 1, 2000000000, varMap.appStartPort + 2);
    // install_rdma_client_on_node(&varMap, 6, 7, 1, 20000000000, varMap.appStartPort + 3);
    node_install_rdma_application(&varMap);
    if(lcAudit)StartLinkCapacityFlowAudit(varMap,lcDirectory);
    if(lcFlowPathAudit)StartFlowPathAudit(varMap);
    if(lcRecoveryTrace)StartRecoveryObserver(varMap);
    if(lcFctTrace)StartFctPacketObserver(varMap,lcDirectory);
    if(lcEarlyAudit)for(uint32_t i=0;i<varMap.svNodes.GetN();++i) {
        Ptr<Node> n=varMap.svNodes.Get(i);
        for(uint32_t j=0;j<n->GetNDevices();++j){Ptr<QbbNetDevice> d=DynamicCast<QbbNetDevice>(n->GetDevice(j));if(d)
            d->TraceConnectWithoutContext("PhyTxBegin",MakeBoundCallback(&RdmaSmartFlowRouting::RecordEarlyProbePhyTx,n->GetId()));}
    }
    std::cout << "-------------------------------Monitor The queue Len----------------------------------------" << std::endl;
    monitor_special_port_qlen(&varMap, 0, 1, 0);
    std::cout << "-------------------------------Monitor The qBB Device----------------------------------------" << std::endl;
    set_QBB_trace(&varMap);
    std::cout << "-------------------------------Start the Simulation----------------------------------------" << std::endl;
    Simulator::Stop(Seconds(varMap.simEndTimeInSec));
    NS_LOG_INFO("Run Simulation.");
    Simulator::Run();
    if(lcEnabled)for(uint32_t i=0;i<varMap.swNodes.GetN();++i)
        DynamicCast<SwitchNode>(varMap.swNodes.Get(i))->StopLinkCapacitySampling();
    if(lcAudit) {
        lcFlowEvents.flush();
        std::ofstream summary(lcDirectory+"/completion.csv");
        summary << "planned,finished,complete\n" << lcPlanned.size() << ',' << lcFinished.size() << ','
          << (lcPlanned.size()==lcFinished.size() && !lcPlanned.empty()) << '\n';
    }

    RdmaSmartFlowRouting::WriteBandwidthAudit(lcDirectory); // BANDWIDTH_WEIGHT_V1
    RdmaSmartFlowRouting::WriteSharedQuotaAudit(lcDirectory); // SHARED_QUOTA_V1
    RdmaSmartFlowRouting::WriteEarlyProbeAudit(lcDirectory); // EARLY_GHOST_V1
    if(lcFctTrace)lcFctPacketEvents.flush();
    if(lcFlowPathAudit)WriteFlowPathAudit(lcDirectory);
    if(lcRecoveryTrace){recoveryPackets.flush();RdmaSmartFlowRouting::lcRecoveryEvents.flush();}
    if(lcRateAudit)RdmaSmartFlowRouting::lcRateSignals.flush();
    if(lcPressureCc){RdmaSmartFlowRouting::lcPressureEvents.flush();RdmaSmartFlowRouting::lcPressurePaths.flush();RdmaSmartFlowRouting::lcPressureLinks.flush();}
    if(lcWaitAudit){RdmaSmartFlowRouting::lcWaitStates.flush();RdmaSmartFlowRouting::lcWaitNic.flush();RdmaSmartFlowRouting::lcWaitChoices.flush();}
    save_egress_ports_loadinfo(&varMap);
    sim_finish(&varMap);
    Simulator::Destroy();
    std::cout << "-------------------------------Finish The Simulation----------------------------------------" << std::endl;
    return 0;
}
