#include <limits> // GHOST_BACKOFF_V1 saturating timeout calculation
#include <functional> // QUEUE90_PROBE20_V1 self-check callbacks
// -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*-
//
// Copyright (c) 2008 University of Washington
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation;
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//

#include "rdma-smartflow-routing.h"
#include "assert.h"
#include <ns3/seq-ts-header.h>
#include <ns3/udp-header.h>
#include <ns3/ipv4-header.h>
#include "ns3/ppp-header.h"
#include "ns3/flow-id-tag.h"
#include <ns3/rdma-driver.h>
#include <ns3/switch-node.h>
namespace ns3
{

//--------------------------------初始化laps_plus_________________________________--------------------------------------------------
    bool RdmaSmartFlowRouting::enable_laps_plus = false;
    bool RdmaSmartFlowRouting::lcDiagDelayWeights=false,RdmaSmartFlowRouting::lcWaitAudit=false;
    std::set<uint32_t> RdmaSmartFlowRouting::lcWaitNodes;
    std::ofstream RdmaSmartFlowRouting::lcWaitStates,RdmaSmartFlowRouting::lcWaitNic,RdmaSmartFlowRouting::lcWaitChoices;
    std::map<uint32_t, Ptr<Node>> RdmaSmartFlowRouting::nodeIdToNodeMap;
    std::map<HostId2PathSeleKey, std::map<uint32_t,std::vector<record_utilization_rate>>>RdmaSmartFlowRouting::record_path_utilization_rate;
    std::map<uint32_t, std::map<uint32_t, std::vector<record_utilization_rate>>> RdmaSmartFlowRouting::record_all_port_utilization_rate;
    std::map<uint32_t, std::map<uint32_t,record_utilization_rate>> RdmaSmartFlowRouting::record_all_port_avg_utilization_rate;
    std::map<HostId2PathSeleKey,e2e_all_flow_dur >RdmaSmartFlowRouting::record_path_e2e_all_flow_dur;
    std::map<HostId2PathSeleKey,uint32_t> RdmaSmartFlowRouting::record_path_flow_num;
    std::vector<std::pair<HostId2PathSeleKey, uint32_t>> RdmaSmartFlowRouting:: sorted_path_flow_counts;
     std::map<HostId2PathSeleKey,cal > RdmaSmartFlowRouting:: record_path_cal;
     std::map<uint32_t, std::map<uint32_t,std::vector<record_queue_length>>> RdmaSmartFlowRouting:: record_all_port_queue_len;
     RelErrorStats RdmaSmartFlowRouting::s_relErrorStats;
     std::map<uint32_t, std::map<uint32_t,record_avg_queue_length>> RdmaSmartFlowRouting::record_all_port_AVG_queue_len;
     record_avg_queue_length  RdmaSmartFlowRouting::avg_port_length;
     record_ack_packet_delay RdmaSmartFlowRouting::m_record_ack_packet_delay;
     std::map<uint64_t,uint64_t > RdmaSmartFlowRouting::RecordAckSendTime;
     bool RdmaSmartFlowRouting::is_sim_finish=false;
   uint32_t RdmaSmartFlowRouting::choose_softmax=0; 
   uint64_t RdmaSmartFlowRouting::record_time=0;
  uint64_t RdmaSmartFlowRouting::sum_data_receive=0;
    uint64_t RdmaSmartFlowRouting::sum_data=0;
    uint64_t RdmaSmartFlowRouting::key=0;

    // 在类外部初始化静态成员变量
    std::vector<probeInfoEntry> RdmaSmartFlowRouting::m_prbInfoTable(0);
    std::map<std::string, reorder_entry_t> RdmaSmartFlowRouting::m_reorderTable;
    double RdmaSmartFlowRouting::laps_alpha = 1;
    std::uniform_real_distribution<double> RdmaSmartFlowRouting::rndGen(0.0, 1.0);
    std::default_random_engine RdmaSmartFlowRouting::generator(std::random_device{}());
    std::map<uint32_t,uint32_t> RdmaSmartFlowRouting::pathPair;
    std::map<uint32_t, std::vector<uint32_t>> RdmaSmartFlowRouting::m_pathDelayRecordTable;
    bool RdmaSmartFlowRouting::enableRecordPathlatency = false;
    
    void RdmaSmartFlowRouting::setPathPair(std::vector<PathData> &PIT)
    {
        std::map<std::string, uint32_t> m_nodeStr_to_pid;
        std::set<uint32_t> s_pids;
        for(auto & p : PIT){
            if(s_pids.find(p.pid) != s_pids.end()){
                std::cerr << "The repeated PID !" << std::endl;
                  std::exit (1);
            }
            std::vector<uint32_t> nodes = p.nodeIdSequence;
            std::string nodesStr = vectorTostring<uint32_t>(nodes);
            if(m_nodeStr_to_pid.find(nodesStr) != m_nodeStr_to_pid.end()){
                std::cerr << "The repeated Nodes for different Paths !" << std::endl;
                std::exit (1);
            }
            m_nodeStr_to_pid[nodesStr] = p.pid;
            

        }



        for (auto & p : PIT){
            if(pathPair.find(p.pid) != pathPair.end()){
                continue;
            }
            std::vector<uint32_t> rvs_nodes(p.nodeIdSequence.rbegin(), p.nodeIdSequence.rend());
            std::string rvs_nodes_str = vectorTostring<uint32_t>(rvs_nodes);
            auto it_rvs_path = m_nodeStr_to_pid.find(rvs_nodes_str);
            if(it_rvs_path == m_nodeStr_to_pid.end()){
                std::cerr << "Not Found the reverse path !" << std::endl;
                std::exit (1);
            }
            pathPair[p.pid] = it_rvs_path->second;
            pathPair[it_rvs_path->second] = p.pid;


        }
        // for (auto &pit : PIT)
        // {
        //     if (pathPair.find(pit.pid) == pathPair.end())
        //     {
        //         bool isFound = false;
        //         std::vector<uint32_t> selfNodes = pit.nodeIdSequence;
        //         for (auto & rPit : PIT)
        //         {
        //             if (rPit.pid == pit.pid)
        //             {
        //                 continue;
        //             }
        //             std::vector<uint32_t> peerNodes = rPit.nodeIdSequence;
        //             if (IsVectorReverse(selfNodes, peerNodes))
        //             {
        //                 NS_ASSERT_MSG(pathPair.find(rPit.pid) == pathPair.end(), "The pathPair should not exist");
        //                 pathPair[rPit.pid] = pit.pid;
        //                 pathPair[pit.pid] = rPit.pid;
        //                 // std::cout << "PathPair: " << rPit.pid << " <-> " << pit.pid << std::endl;
        //                 isFound = true;
        //             }

        //         }
        //         NS_ASSERT_MSG(isFound, "The pathPair should be found");
        //     }
        // }
    }


    NS_LOG_COMPONENT_DEFINE("RdmaSmartFlowRouting");

    NS_OBJECT_ENSURE_REGISTERED(RdmaSmartFlowRouting);

// ALPS_LINK_CAPACITY_V1

// FIRSTTEST_V1
bool RdmaSmartFlowRouting::firsttestTraceEnabled=false;
std::ofstream RdmaSmartFlowRouting::firsttestPathLog,RdmaSmartFlowRouting::firsttestAckQueueLog,RdmaSmartFlowRouting::firsttestCcLog;
// ACK_QUEUE_V1
bool RdmaSmartFlowRouting::lcPruneEnabled=false;
uint32_t RdmaSmartFlowRouting::lcDataQueue=3;
std::ofstream RdmaSmartFlowRouting::lcGuardLog,RdmaSmartFlowRouting::lcPruneLog;
// BANDWIDTH_WEIGHT_V1
uint32_t RdmaSmartFlowRouting::lcBwMode=0;
bool RdmaSmartFlowRouting::lcRecoveryTrace=false;
bool RdmaSmartFlowRouting::lcRateUseDelay=false,RdmaSmartFlowRouting::lcRateAudit=false;
std::ofstream RdmaSmartFlowRouting::lcRateSignals;
std::ofstream RdmaSmartFlowRouting::lcRecoveryEvents;
void RdmaSmartFlowRouting::TraceRecovery(const char *event,uint32_t node,uint32_t path,uint32_t flow,
        uint32_t seq,uint32_t bytes,uint32_t triggerFlow,uint32_t triggerSeq,uint64_t uid,uint32_t port) {
    if(lcRecoveryTrace)lcRecoveryEvents<<Simulator::Now().GetNanoSeconds()<<','<<event<<','<<node<<','<<path<<','<<flow<<','<<seq<<','<<bytes<<','<<triggerFlow<<','<<triggerSeq<<','<<uid<<','<<port<<'\n';
}

bool RdmaSmartFlowRouting::lcAllocationOnly=false;
double RdmaSmartFlowRouting::lcSmalltestFixedBranchProbability=-1.0;
bool RdmaSmartFlowRouting::lcBwAudit=false,RdmaSmartFlowRouting::lcDetailedLogs=true;
std::map<uint32_t,RdmaSmartFlowRouting::BwDecisionAudit> RdmaSmartFlowRouting::lcBwDecisions;
std::map<uint64_t,RdmaSmartFlowRouting::BwPathAudit> RdmaSmartFlowRouting::lcBwPaths;
std::map<std::pair<uint64_t,uint64_t>,RdmaSmartFlowRouting::BwTrafficAudit> RdmaSmartFlowRouting::lcBwTimeseries;

// EARLY_GHOST_V1: uses existing DATA transmit opportunities; no standalone timer traffic.
bool RdmaSmartFlowRouting::lcEarlyProbe=false,RdmaSmartFlowRouting::lcEarlyAudit=false;
uint64_t RdmaSmartFlowRouting::lcEarlyLeadNs=20000,RdmaSmartFlowRouting::lcEarlyIntervalNs=1000,
         RdmaSmartFlowRouting::lcEarlyTimeoutNs=1000000;
std::map<uint32_t,RdmaSmartFlowRouting::EarlyProbeAudit> RdmaSmartFlowRouting::lcEarlyStats;
std::ofstream RdmaSmartFlowRouting::lcEarlyLog,RdmaSmartFlowRouting::lcProbeBackoffLog;
bool RdmaSmartFlowRouting::LinkObservationAge(uint32_t link,uint64_t now,uint64_t &age) const {
    uint16_t q;if(!ReadLinkCapacity(link,now,q))return false;
    uint64_t round=now/lcObservationTickNs;
    uint16_t ticks=uint16_t(uint16_t(round)-m_linkStateTable.at(link).sampleEpoch);
    age=now-(round-ticks)*lcObservationTickNs;return true;
}


// CAPACITY_BASELINE_FLOOR_V1: no new link table or on-wire telemetry fields.
bool RdmaSmartFlowRouting::lcPathTimeoutBasePlusSwitch=false;

// ALPS_STRICT_RESTORE_V1: exact predicate from the original commented block.
bool RdmaSmartFlowRouting::alpsStrictPrune=false;
std::ofstream RdmaSmartFlowRouting::alpsStrictDataLog,RdmaSmartFlowRouting::alpsStrictEventLog;
bool RdmaSmartFlowRouting::IsAlpsPathPruned(const PathData &path,uint64_t now){
    const uint64_t reference=path.theoreticalSmallestLatencyInNs;
    const uint64_t generated=path.tsGeneration.GetNanoSeconds();
    return path.latency>=reference && now>=generated && now-generated<=reference;
}
bool RdmaSmartFlowRouting::AlpsStrictAllBlocked(const std::vector<PathData*> &paths,uint64_t now,uint64_t &retry){
    retry=0;if(paths.empty())return false;
    for(const auto *p:paths){
        if(!IsAlpsPathPruned(*p,now)){retry=0;return false;}
        uint64_t expiry=p->tsGeneration.GetNanoSeconds()+p->theoreticalSmallestLatencyInNs+1;
        retry=retry?std::min(retry,expiry):expiry;
    }
    NS_ABORT_MSG_IF(retry<=now,"Strict-prune retry must advance simulation time");return true;
}
bool RdmaSmartFlowRouting::RunAlpsStrictPruneSelfChecks(){
    bool ok=true;uint32_t count=0;auto ck=[&](bool v,const char *name){++count;if(!v){ok=false;std::cerr<<"ALPS_STRICT_FAIL "<<name<<std::endl;}};
    PathData a,b;a.theoreticalSmallestLatencyInNs=100;a.tsGeneration=NanoSeconds(50);a.latency=99;
    b=a;std::vector<PathData*> paths{&a,&b};uint64_t retry=0;
    ck(!IsAlpsPathPruned(a,100),"below_reference");a.latency=100;
    ck(IsAlpsPathPruned(a,100),"equality_prunes");a.latency=101;
    ck(IsAlpsPathPruned(a,150),"age_equal_reference_still_fresh");
    ck(!IsAlpsPathPruned(a,151),"age_exceeds_reference_reeligible");
    ck(!AlpsStrictAllBlocked(paths,100,retry) && retry==0,"one_eligible_does_not_gate");
    b.latency=200;b.tsGeneration=NanoSeconds(70);
    ck(AlpsStrictAllBlocked(paths,100,retry) && retry==151,"all_blocked_earliest_expiry");
    ck(!AlpsStrictAllBlocked(paths,151,retry),"timer_releases_old_observation");
    a.latency=99;a.tsGeneration=NanoSeconds(101);
    ck(!AlpsStrictAllBlocked(paths,101,retry),"new_low_feedback_recovers");
    a.latency=101;a.tsGeneration=NanoSeconds(101);
    ck(AlpsStrictAllBlocked(paths,101,retry) && retry==171,"new_high_feedback_keeps_gate");
    auto routing=CreateObject<RdmaSmartFlowRouting>();bool oldPlus=enable_laps_plus,oldStrict=alpsStrictPrune;auto oldChoice=choose_softmax;
    enable_laps_plus=true;choose_softmax=3;alpsStrictPrune=true;
    a.pid=1;b.pid=2;a.latency=90;a.tsGeneration=Time(0);b.latency=101;b.tsGeneration=Time(0);
    routing->m_pathBdpMap[1]=PathBdpInfo();routing->m_pathBdpMap[2]=PathBdpInfo();
    auto w=routing->CalPathWeightBasedOnDelay(paths,1);
    ck(w.size()==2 && w[0]==1 && w[1]==0,"pruning_and_normalization");
    a.latency=101;w=routing->CalPathWeightBasedOnDelay(paths,1);
    ck(w[0]==0 && w[1]==0,"all_zero_retained_for_predequeue_gate");
    alpsStrictPrune=false;w=routing->CalPathWeightBasedOnDelay(paths,1);
    ck(w[0]>0 && w[1]>0 && std::abs(w[0]+w[1]-1)<1e-9,"disabled_original_soft_weights");
    enable_laps_plus=oldPlus;choose_softmax=oldChoice;alpsStrictPrune=oldStrict;
    std::cout<<(ok?"ALPS_STRICT_SELF_CHECK_PASS":"ALPS_STRICT_SELF_CHECK_FAIL")<<" assertions="<<count<<std::endl;return ok;
}

uint64_t RdmaSmartFlowRouting::lcDataAckTimeoutNs=0;
bool RdmaSmartFlowRouting::lcPathTimeout=false;
uint32_t RdmaSmartFlowRouting::lcPathTimeoutPackets=20;
std::ofstream RdmaSmartFlowRouting::lcPathTimeoutLog,RdmaSmartFlowRouting::lcPathTimeoutThresholds;
bool RdmaSmartFlowRouting::lcSendPenalty=false;
std::ofstream RdmaSmartFlowRouting::lcSendPenaltyLog;
bool RdmaSmartFlowRouting::lcAlpsProbe=false;
std::ofstream RdmaSmartFlowRouting::lcAlpsProbeLog;
bool RdmaSmartFlowRouting::lcCapacityBaselineProbe=false;
double RdmaSmartFlowRouting::lcZeroBwMinProbability=0;
std::ofstream RdmaSmartFlowRouting::lcCapacityBaselineLog,RdmaSmartFlowRouting::lcZeroFloorLog;
std::map<uint32_t,RdmaSmartFlowRouting::ZeroFloorAudit> RdmaSmartFlowRouting::lcZeroFloorStats;
void RdmaSmartFlowRouting::ConfigureCapacityFloorAudit(const std::string &directory) {
    NS_ABORT_MSG_IF(lcZeroBwMinProbability<0 || lcZeroBwMinProbability>1 || !std::isfinite(lcZeroBwMinProbability),"Invalid zero-budget probability floor");
    if(lcBwMode && lcBwAudit){
        lcBwExpLog.open(directory+"/bandwidth-exponential-weights.csv");NS_ABORT_MSG_IF(!lcBwExpLog,"Cannot open bandwidth exponential audit");
        lcBwExpLog<<std::setprecision(17)<<"time_ns,node,path,fresh,cached,unknown,blocked,budget_gbps,probe_factor,beta,unit_gbps,log_score,raw_probability,final_probability,latency_weight_fallback,signed_link_min_gbps,nonnegative_grant_gbps,queue_score_mode\n";
    }
    if(CompactLinkState::queueScoreEnabled && lcBwAudit){
        lcQueueScoreLog.open(directory+"/queue-bandwidth-score.csv");NS_ABORT_MSG_IF(!lcQueueScoreLog,"Cannot write queue score");
        lcQueueScoreLog<<std::setprecision(17)<<"now_ns,uid,node,port,measured_residual_q,capacity_bps,queue_bytes,scaled_threshold_bytes,actual_threshold_bytes,horizon_ns,raw_score_q,encoded_word,decoded_score_q,positive_q,clipped\n";
    }
    if(lcLinkSendPenalty && lcBwAudit){
        lcLinkPenaltyLog.open(directory+"/link-send-penalty.csv");lcPathPenaltyLog.open(directory+"/path-send-penalty.csv");lcFeedbackCounterLog.open(directory+"/feedback-send-counter.csv");
        NS_ABORT_MSG_IF(!lcLinkPenaltyLog || !lcPathPenaltyLog || !lcFeedbackCounterLog,"Cannot write local send penalty audit");
        lcLinkPenaltyLog<<std::setprecision(17)<<"time_ns,node,counter_serial,link,known,observed_score_gbps,penalty_gbps,selection_score_gbps,contributors\n";
        lcPathPenaltyLog<<std::setprecision(17)<<"time_ns,node,counter_serial,path,fresh,cached,unknown,blocked,sent_count,feedback_budget_gbps,selection_budget_gbps,cached_observation_budget_gbps,max_link_penalty_gbps,signed_link_min_gbps,nonnegative_grant_gbps\n";
        lcFeedbackCounterLog<<"serial,time_ns,node,path,event,before,after\n";
    }
    if(!lcBwCc)return;
    if(lcSendPenalty && lcBwAudit){
        lcSendPenaltyLog.open(directory+"/bandwidth-send-penalty.csv");NS_ABORT_MSG_IF(!lcSendPenaltyLog,"Cannot open send penalty audit");
        lcSendPenaltyLog.precision(17);lcSendPenaltyLog<<"now_ns,node,path,sent_since_feedback,reference_ns,beta,log_factor,factor,prior_probability,raw_probability,final_probability,blocked,zero_budget\n";
    }
    if(lcCapacityBaselineProbe && lcEarlyAudit){
        lcCapacityBaselineLog.open(directory+"/capacity-baseline-probes.csv");
        NS_ABORT_MSG_IF(!lcCapacityBaselineLog,"Cannot open capacity baseline probe audit");
        lcCapacityBaselineLog<<"now_ns,node,path,last_capacity_update_ns,reference_ns,age_ns,reason\n";
    }
    if(lcZeroBwMinProbability>0 && lcBwAudit){
        lcZeroFloorLog.open(directory+"/zero-bandwidth-probabilities.csv");
        NS_ABORT_MSG_IF(!lcZeroFloorLog,"Cannot open zero-budget probability audit");
        lcZeroFloorLog<<"now_ns,node,path,blocked,fresh,cached,budget_q,probe_factor,zero_budget,pre_floor_probability,final_probability,effective_floor\n";
    }
}
void RdmaSmartFlowRouting::WriteCapacityFloorAudit(const std::string &directory) {
    if(lcLinkPenaltyLog)lcLinkPenaltyLog.flush();if(lcPathPenaltyLog)lcPathPenaltyLog.flush();if(lcFeedbackCounterLog)lcFeedbackCounterLog.flush();
    if(lcBwExpLog)lcBwExpLog.flush();if(lcQueueScoreLog)lcQueueScoreLog.flush();
    if(lcCapacityBaselineLog)lcCapacityBaselineLog.flush();
    if(lcZeroFloorLog)lcZeroFloorLog.flush();if(lcSendPenaltyLog)lcSendPenaltyLog.flush();
    if(lcBwCc && lcZeroBwMinProbability>0 && lcBwAudit){
        std::ofstream f(directory+"/zero-bandwidth-summary.csv");
        NS_ABORT_MSG_IF(!f,"Cannot write zero-budget summary");
        f<<"node,decisions,zero_path_visits,changed_decisions,capped_decisions\n";
        for(const auto &e:lcZeroFloorStats)f<<e.first<<','<<e.second.decisions<<','<<e.second.zeroPaths<<','<<e.second.changed<<','<<e.second.capped<<'\n';
    }
}
bool RdmaSmartFlowRouting::RecordPathCapacityUpdate(uint32_t pid,const LinkCapacityTag &tag,uint64_t previousBorn,uint64_t now) {
    if(!lcCapacityBaselineProbe || tag.overflow || now<tag.bornNs || now-tag.bornNs>lcTtlNs ||
       (previousBorn!=UINT64_MAX && tag.bornNs<=previousBorn))return false;
    auto it=m_nexthopSelTbl.find(pid);if(it==m_nexthopSelTbl.end())return false;
    const auto &path=it->second;
    if(path.portSequence.size()<2 || path.nodeIdSequence.size()!=path.portSequence.size()+1 ||
       tag.count!=path.portSequence.size()-1 || path.queueFeedbackBornNs!=tag.bornNs)return false;
    std::set<uint32_t> expected,observed;
    for(size_t j=1;j<path.portSequence.size();++j)expected.insert(MakeLinkId(path.nodeIdSequence[j],path.portSequence[j]));
    uint64_t tick=now/lcObservationTickNs;
    for(uint8_t i=0;i<tag.count;++i){const auto &v=tag.records[i];uint16_t delta=uint16_t(uint16_t(tick)-v.state.sampleEpoch);
        if(v.state.CapacityQ()==0xffff || !observed.insert(v.linkId).second || delta>tick || delta>=32768 ||
           now-(tick-delta)*lcObservationTickNs>lcTtlNs)return false;
    }
    if(expected!=observed)return false;
    // Receive time matches PIT tsGeneration semantics. Partial, duplicate and old ACKs do not renew it.
    m_earlyProbes[pid].lastCapacityUpdateNs=now;return true;
}
double RdmaSmartFlowRouting::ApplyZeroBudgetFloor(std::vector<double> &weights,const std::vector<bool> &zero,
        const std::vector<bool> &blocked,double requested) {
    NS_ABORT_MSG_IF(weights.size()!=zero.size() || weights.size()!=blocked.size(),"Invalid probability floor vectors");
    size_t k=0;for(size_t i=0;i<weights.size();++i)k+=zero[i] && !blocked[i];
    if(!k || requested<=0)return requested;
    const double floor=std::min(requested,1.0/k);
    if(floor*k>=1.0-1e-12){
        for(size_t i=0;i<weights.size();++i)weights[i]=zero[i] && !blocked[i]?1.0/k:0;
        return floor;
    }
    // Clamp only deficient zero-budget paths, then proportionally rescale the unfixed paths.
    std::vector<bool> fixed(weights.size(),false);double remaining=1.0;
    for(size_t pass=0;pass<=weights.size();++pass){
        double sum=0;size_t free=0;
        for(size_t i=0;i<weights.size();++i)if(!blocked[i] && !fixed[i]){sum+=weights[i];++free;}
        bool changed=false;
        for(size_t i=0;i<weights.size();++i)if(!blocked[i] && !fixed[i] && zero[i]){
            double candidate=sum>0?remaining*weights[i]/sum:(free?remaining/free:0);
            if(candidate+1e-12<floor){fixed[i]=true;remaining-=floor;changed=true;}
        }
        if(changed)continue;
        for(size_t i=0;i<weights.size();++i){
            if(blocked[i])weights[i]=0;
            else if(fixed[i])weights[i]=floor;
            else weights[i]=sum>0?remaining*weights[i]/sum:(free?remaining/free:0);
        }
        return floor;
    }
    NS_ABORT_MSG("Probability floor did not converge");return floor;
}

// GHOST_BACKOFF_V1: per-path retry wait; rate probing and DATA RTO are separate.
uint64_t RdmaSmartFlowRouting::ProbeTimeoutNs(const EarlyProbeState &p) {
    uint64_t limit=std::numeric_limits<uint64_t>::max();
    uint64_t rttGuard=p.rttNs>limit/4?limit:4*p.rttNs;
    uint64_t base=std::max(lcEarlyTimeoutNs,rttGuard);
    unsigned shift=lcBwCc?std::min<unsigned>(p.timeoutShift,6):0;
    return base>(limit>>shift)?limit:(base<<shift);
}

PathData *RdmaSmartFlowRouting::SelectEarlyProbe(const std::vector<PathData*> &paths,
        const std::vector<double> &eligible,uint64_t now) {
    NS_ABORT_MSG_IF(paths.size()!=eligible.size(),"Invalid early probe eligibility");
    const uint32_t node=m_node?m_node->GetId():m_nodeId;auto &a=lcEarlyStats[node];

    // ALPS_NATIVE_PROBE_V1: reuse the unchanged original expiry/cooldown/random selector.
    // pending, link TTL, cold-start priorities and source throttles cannot veto this decision.
    if(lcAlpsProbe){
        ++a.checks;auto candidates=paths;
        if(lcPathTimeout)candidates.erase(std::remove_if(candidates.begin(),candidates.end(),[&](PathData *p){auto it=m_earlyProbes.find(p->pid);return it!=m_earlyProbes.end() && it->second.unavailable;}),candidates.end());
        if(candidates.empty())return nullptr;
        PathData *chosen=CheckProbePathAmoungPitEntries(candidates);
        m_earlyReason=chosen?4:0;
        if(chosen && lcAlpsProbeLog){
            std::vector<PathData*> expired,allowed;
            get_the_expired_paths(candidates,expired);get_the_probe_paths(expired,allowed);
            uint64_t maximum=0,coolMaximum=0;
            for(auto p:paths)maximum=std::max(maximum,uint64_t(p->latency));
            for(auto p:expired)coolMaximum=std::max(coolMaximum,uint64_t(p->latency));
            lcAlpsProbeLog<<now<<','<<node<<','<<chosen->pid<<','<<2*maximum<<','<<2*coolMaximum<<','
                <<chosen->tsGeneration.GetNanoSeconds()<<','<<chosen->tsProbeLastSend.GetNanoSeconds()<<','
                <<expired.size()<<','<<allowed.size()<<',';
            for(size_t i=0;i<allowed.size();++i){if(i)lcAlpsProbeLog<<';';lcAlpsProbeLog<<allowed[i]->pid;}
            lcAlpsProbeLog<<'\n';
        }
        return chosen;
    }
    ++a.checks;
    if(!m_earlyStarted) {m_earlyStarted=true;m_earlyNextNs=now+(uint64_t(node)*2654435761ULL)%lcEarlyIntervalNs;}
    if(now<m_earlyNextNs){++a.rateLimited;return nullptr;}
    std::set<uint32_t> covered;
    for(auto &entry:m_earlyProbes) {
        auto &p=entry.second;
        if(!p.pending)continue;
        uint64_t wait=ProbeTimeoutNs(p);
        if(now>=p.sentNs && now-p.sentNs>=wait) {
            // GHOST_BACKOFF_V1: retry may be selected now; the next ACK wait doubles.
            unsigned previous=p.timeoutShift;
            if(lcBwCc){if(p.timeoutShift<6)++p.timeoutShift;++a.backoffTimeouts;if(previous==6)++a.backoffCapHits;}
            p.pending=false;++a.timeouts;
            if(lcBwCc && lcProbeBackoffLog)lcProbeBackoffLog<<now<<','<<node<<','<<entry.first<<",timeout,"<<previous<<','<<unsigned(p.timeoutShift)<<','<<wait<<','<<ProbeTimeoutNs(p)<<'\n';
            if(lcEarlyLog)lcEarlyLog<<"timeout,"<<now<<','<<node<<','<<entry.first<<",0,0,0\n";
            continue;
        }
        auto it=m_nexthopSelTbl.find(entry.first);if(it==m_nexthopSelTbl.end())continue;
        const auto &path=it->second;
        for(size_t j=1;j<path.portSequence.size();++j)covered.insert(MakeLinkId(path.nodeIdSequence[j],path.portSequence[j]));
    }
    PathData *best=nullptr;size_t bestNeed=0;uint64_t bestAge=0;uint32_t bestKind=0;
    uint64_t maxDelay=0;for(auto p:paths)maxDelay=std::max(maxDelay,uint64_t(p->latency));
    for(size_t i=0;i<paths.size();++i) {
        if(!(eligible[i]>0))continue;
        PathData *path=paths[i];auto state=m_earlyProbes.find(path->pid);
        if(state!=m_earlyProbes.end() && state->second.pending){++a.inflightSkipped;continue;}
        if(state!=m_earlyProbes.end() && now<state->second.nextNs)continue;
        if(lcCapacityBaselineProbe){
            const uint64_t last=state==m_earlyProbes.end()?UINT64_MAX:state->second.lastCapacityUpdateNs;
            const uint64_t reference=path->theoreticalSmallestLatencyInNs;
            const uint64_t limit=reference>UINT64_MAX/2?UINT64_MAX:2*reference;
            const bool cold=last==UINT64_MAX;
            const uint64_t age=!cold && now>=last?now-last:0;
            // Only this path's complete capacity ACK renews this clock; no shared-link dedup here.
            if(!cold && (!reference || now<last || age<=limit))continue;
            if(!best || (cold && bestKind!=1) || (cold==(bestKind==1) && age>bestAge) ||
               (cold==(bestKind==1) && age==bestAge && path->pid<best->pid)){
                best=path;bestAge=age;bestKind=cold?1:3;
            }
            continue;
        }
        uint64_t rtt=state==m_earlyProbes.end()?0:state->second.rttNs;
        uint64_t lead=std::min(lcTtlNs-lcWindowNs,std::max(lcEarlyLeadNs,rtt+2*lcWindowNs));
        size_t need=0,uncovered=0;uint64_t oldest=0;bool unknown=false,expired=false;
        bool ownAlarm=false;bool ownCongested=lcMaxLinkQueue && lcPathOnlyQueue && ReadPathQueueCongestion(*path,now,ownAlarm) && ownAlarm;
        for(size_t j=1;j<path->portSequence.size();++j) {
            uint32_t link=MakeLinkId(path->nodeIdSequence[j],path->portSequence[j]);uint64_t age=0;
            bool known=LinkObservationAge(link,now,age);
            // BANDWIDTH_CC_V1: probe congested links after the existing per-path cooldown.
            bool alarm=false;bool congested=lcBwCc && (lcMaxLinkQueue && lcPathOnlyQueue?ownCongested:(ReadLinkCongestion(link,now,alarm) && alarm)); // PATH_ONLY_QUEUE_V1
            if(!known || age+lead>=lcTtlNs || congested) {
                ++need;if(!covered.count(link))++uncovered;
                if(!known) {unknown=true;expired|=m_expiredLinksForAudit.count(link)>0;
                    auto it=m_linkStateTable.find(link);expired|=it!=m_linkStateTable.end() && it->second.availableQ!=0xffff;}
                oldest=std::max(oldest,known?age:lcTtlNs+1);
            }
        }
        // Another path's probe can refresh shared capacity, not this path's own queue maximum.
        if(ownCongested){need=std::max<size_t>(need,1);uncovered=std::max<size_t>(uncovered,1);}
        if(need && !uncovered){++a.sharedSkipped;continue;}
        // Preserve latency maintenance as a lower-priority reason, under the same guards.
        bool legacy=now>=uint64_t(path->tsGeneration.GetNanoSeconds()) &&
            now-uint64_t(path->tsGeneration.GetNanoSeconds())>2*maxDelay &&
            now-uint64_t(path->tsProbeLastSend.GetNanoSeconds())>2*maxDelay;
        uint32_t kind=need?(unknown?(expired?2:1):3):(legacy?4:0);
        if(!kind)continue;
        if(!best || uncovered>bestNeed || (uncovered==bestNeed && oldest>bestAge) ||
           (uncovered==bestNeed && oldest==bestAge && path->pid<best->pid)) {
            best=path;bestNeed=uncovered;bestAge=oldest;bestKind=kind;
        }
    }
    if(best && lcCapacityBaselineProbe && lcCapacityBaselineLog){
        auto state=m_earlyProbes.find(best->pid);uint64_t last=state==m_earlyProbes.end()?UINT64_MAX:state->second.lastCapacityUpdateNs;
        lcCapacityBaselineLog<<now<<','<<node<<','<<best->pid<<','<<last<<','<<best->theoreticalSmallestLatencyInNs<<','<<bestAge<<','<<bestKind<<'\n';
    }
    m_earlyReason=bestKind;return best;
}
void RdmaSmartFlowRouting::RecordEarlyProbeSent(uint32_t pid,uint64_t now) {
    if(!lcEarlyProbe && !lcEarlyAudit)return;
    uint32_t node=m_node?m_node->GetId():m_nodeId;auto &a=lcEarlyStats[node];
    uint32_t reason=lcEarlyProbe?m_earlyReason:0;
    if(!reason)++a.baselineSent;else if(reason==1)++a.coldSent;else if(reason==2)++a.expiredSent;
    else if(reason==3)++a.earlySent;else ++a.legacySent;
    if(lcEarlyProbe) {
        auto &p=m_earlyProbes[pid];
        if(lcAlpsProbe){
            // State tracks only the discount and ACK matching, never probe eligibility.
            p.pending=true;p.awaitingFresh=lcBwCc;p.sentNs=now;p.timeoutShift=0;p.nextNs=0;
        } else {
            NS_ABORT_MSG_IF(p.pending,"Duplicate in-flight ghost");
            p.pending=true;p.awaitingFresh=lcBwCc;p.sentNs=now;p.nextNs=now+std::max(lcEarlyIntervalNs,2*lcWindowNs);
            m_earlyNextNs=now+lcEarlyIntervalNs;
        }
    }
    if(lcEarlyLog)lcEarlyLog<<"send,"<<now<<','<<node<<','<<pid<<','<<reason<<",0,0\n";
}
bool RdmaSmartFlowRouting::CompleteEarlyProbe(uint32_t pid,uint64_t sent,uint64_t rtt,uint64_t now) {
    auto it=m_earlyProbes.find(pid);if(it==m_earlyProbes.end() || !it->second.pending || it->second.sentNs!=sent)return false;
    auto &p=it->second;
    unsigned oldShift=p.timeoutShift;uint64_t oldWait=ProbeTimeoutNs(p);
    if(lcBwCc && oldShift) {
        ++lcEarlyStats[m_node?m_node->GetId():m_nodeId].backoffResets;
    }
    p.timeoutShift=0;p.pending=false;p.rttNs=p.rttNs?(7*p.rttNs+rtt)/8:rtt;
    if(lcBwCc && oldShift && lcProbeBackoffLog)lcProbeBackoffLog<<now<<','<<(m_node?m_node->GetId():m_nodeId)<<','<<pid<<",ack_reset,"<<oldShift<<",0,"<<oldWait<<','<<ProbeTimeoutNs(p)<<'\n';
    if(lcBwCc)p.awaitingFresh=!HasPostProbeCapacity(pid,now,sent); // QUEUE90_PROBE20_V1
    p.nextNs=std::max(p.nextNs,now+lcEarlyIntervalNs);return true;
}
void RdmaSmartFlowRouting::ReceiveEarlyProbeAck(Ptr<Packet> packet) {
    if(!lcEarlyProbe && !lcEarlyAudit)return;
    Ipv4SmartFlowProbeTag probe;if(!packet->PeekPacketTag(probe))return;
    uint32_t pid,rtt;probe.get_path_info(pid,rtt);uint64_t now=Simulator::Now().GetNanoSeconds();
    uint32_t node=m_node?m_node->GetId():m_nodeId;auto &a=lcEarlyStats[node];++a.acks;
    bool matched=lcEarlyProbe && rtt<=now && CompleteEarlyProbe(pid,now-rtt,rtt,now);
    if(lcEarlyProbe){if(matched)++a.matched;else ++a.late;}
    if(lcEarlyLog)lcEarlyLog<<"ack,"<<now<<','<<node<<','<<pid<<",0,"<<rtt<<','<<matched<<'\n';
}
void RdmaSmartFlowRouting::RecordEarlyProbePhyTx(uint32_t node,Ptr<const Packet> p) {
    if(!lcEarlyAudit)return;
    CustomHeader ch(CustomHeader::L2_Header|CustomHeader::L3_Header|CustomHeader::L4_Header);p->PeekHeader(ch);
    Ipv4SmartFlowProbeTag tag;bool probe=p->PeekPacketTag(tag);auto &a=lcEarlyStats[node];
    if(ch.l3Prot==L3ProtType::UDP){if(probe){++a.probeTx;a.probeBytes+=p->GetSize();}else{++a.dataTx;a.dataBytes+=p->GetSize();}}
    else if(ch.l3Prot==L3ProtType::ACK || ch.l3Prot==L3ProtType::NACK){if(probe){++a.probeAckTx;a.probeAckBytes+=p->GetSize();}else a.dataAckBytes+=p->GetSize();}
}
void RdmaSmartFlowRouting::ConfigureEarlyProbeAudit(const std::string &directory) {
    NS_ABORT_MSG_IF(lcEarlyProbe && (!lcEnabled || !lcEarlyIntervalNs || lcWindowNs>=lcTtlNs || lcEarlyLeadNs>=lcTtlNs || lcEarlyTimeoutNs<lcTtlNs),"Invalid proactive probe configuration");
    if(!lcEarlyAudit)return;
    lcEarlyLog.open(directory+"/early-probe-events.csv");NS_ABORT_MSG_IF(!lcEarlyLog,"Cannot write early probe log");
    lcEarlyLog<<"event,time_ns,node,path,reason,rtt_ns,matched\n";
    if(lcAlpsProbe){lcAlpsProbeLog.open(directory+"/alps-probe-decisions.csv");NS_ABORT_MSG_IF(!lcAlpsProbeLog,"Cannot write original ALPS probe audit");
        lcAlpsProbeLog<<"time_ns,node,path,expiry_ns,cooldown_ns,last_update_ns,last_probe_ns,expired_count,candidate_count,candidates\n";}
    if(lcBwCc){lcProbeBackoffLog.open(directory+"/probe-backoff-events.csv");NS_ABORT_MSG_IF(!lcProbeBackoffLog,"Cannot open probe backoff log");
        lcProbeBackoffLog<<"time_ns,node,path,event,old_shift,new_shift,previous_timeout_ns,next_timeout_ns\n";}
}
void RdmaSmartFlowRouting::WriteEarlyProbeAudit(const std::string &directory) {
    WriteCapacityFloorAudit(directory);
    if(!lcEarlyAudit)return;
    std::ofstream f(directory+"/early-probe-summary.csv");NS_ABORT_MSG_IF(!f,"Cannot write probe summary");
    f<<"node,checks,baseline_sent,cold_sent,expired_sent,early_sent,legacy_sent,rate_limited,inflight_skipped,shared_skipped,timeouts,acks,matched,late,probe_records_accepted,probe_records_rejected,unknown_fallback,unknown_with_expired,probe_tx,probe_bytes,probe_ack_tx,probe_ack_bytes,data_tx,data_bytes,data_ack_bytes,backoff_timeouts,backoff_cap_hits,backoff_resets\n";
    for(const auto &e:lcEarlyStats){const auto &a=e.second;f<<e.first<<','<<a.checks<<','<<a.baselineSent<<','<<a.coldSent<<','<<a.expiredSent<<','<<a.earlySent<<','<<a.legacySent<<','<<a.rateLimited<<','<<a.inflightSkipped<<','<<a.sharedSkipped<<','<<a.timeouts<<','<<a.acks<<','<<a.matched<<','<<a.late<<','<<a.probeRecordsAccepted<<','<<a.probeRecordsRejected<<','<<a.unknownFallback<<','<<a.unknownWithExpired<<','<<a.probeTx<<','<<a.probeBytes<<','<<a.probeAckTx<<','<<a.probeAckBytes<<','<<a.dataTx<<','<<a.dataBytes<<','<<a.dataAckBytes<<','<<a.backoffTimeouts<<','<<a.backoffCapHits<<','<<a.backoffResets<<'\n';}
    lcEarlyLog.flush();if(lcAlpsProbeLog)lcAlpsProbeLog.flush();
}

bool RdmaSmartFlowRouting::RunAlpsNativeProbeSelfChecks(){
    lcEnabled=true;lcBwCc=true;lcEarlyProbe=true;lcAlpsProbe=true;lcEarlyAudit=false;
    lcCapacityBaselineProbe=true;lcWindowNs=5000;lcTtlNs=100000;
    auto src=CreateObject<RdmaSmartFlowRouting>();src->m_nodeId=0;
    std::map<uint32_t,PathData> pit;
    for(uint32_t i=1;i<=3;++i){auto &p=pit[i];p.pid=i;p.latency=i*10000;p.theoreticalSmallestLatencyInNs=1;
        p.tsGeneration=NanoSeconds(0);p.tsProbeLastSend=NanoSeconds(0);p.nodeIdSequence={100,i,99};p.portSequence={1,1};}
    src->set_PIT(pit);std::vector<PathData*> paths={src->lookup_PIT(1),src->lookup_PIT(2),src->lookup_PIT(3)};
    bool ok=true;unsigned checks=0;auto ck=[&](bool v,const char *name){++checks;if(!v){ok=false;std::cout<<"ALPS_NATIVE_PROBE_CASE_FAIL "<<name<<'\n';}};
    auto runAt=[&](uint64_t now,std::function<void()> action){
        auto invoke=+[](std::function<void()> *f){(*f)();};Simulator::Schedule(NanoSeconds(now-Simulator::Now().GetNanoSeconds()),invoke,&action);
        Simulator::Stop(NanoSeconds(now-Simulator::Now().GetNanoSeconds()+1));Simulator::Run();};
    ck(src->SelectEarlyProbe(paths,{1,1,1},0)==nullptr,"no_capacity_cold_start_override");
    runAt(60000,[&](){ck(src->SelectEarlyProbe(paths,{1,1,1},60000)==nullptr,"strict_expiry_boundary");});
    runAt(60001,[&](){
        src->m_earlyStarted=true;src->m_earlyNextNs=UINT64_MAX;
        for(auto p:paths){auto &s=src->m_earlyProbes[p->pid];s.pending=true;s.nextNs=UINT64_MAX;s.timeoutShift=6;}
        unsigned matched=0;
        for(unsigned seed=1;seed<=128;++seed){
            std::srand(seed);auto expected=src->CheckProbePathAmoungPitEntries(paths);int nextExpected=std::rand();
            std::srand(seed);auto actual=src->SelectEarlyProbe(paths,{0,0,0},60001);int nextActual=std::rand();
            matched+=(expected==actual && nextExpected==nextActual);
        }
        ck(matched==128,"exact_original_selection_and_RNG_128_seeds");
        paths[2]->tsGeneration=NanoSeconds(60000);paths[0]->tsProbeLastSend=NanoSeconds(20001);
        paths[1]->tsProbeLastSend=NanoSeconds(0);
        ck(src->SelectEarlyProbe(paths,{0,0,0},60001)==paths[1],"expired_subset_cooldown_40000_strict_boundary");
        src->update_PIT_after_probing(paths[1]);src->RecordEarlyProbeSent(2,60001);
        ck(src->m_earlyProbes[2].sentNs==60001 && src->m_earlyProbes[2].timeoutShift==0,"overlapping_probe_allowed_no_backoff");
        ck(src->ProbeCapacityFactor(2,60001)==0.5,"pending_weight_factor_half");
        ck(src->lcEarlyStats[0].timeouts==0 && src->lcEarlyStats[0].backoffTimeouts==0,"no_retry_timeout_side_effect");
    });
    runAt(60002,[&](){
        ck(src->SelectEarlyProbe(paths,{0,0,0},60002)==paths[0],"cooldown_finished_pending_cannot_veto");
        src->update_PIT_after_probing(paths[0]);src->RecordEarlyProbeSent(1,60002);
        ck(src->CompleteEarlyProbe(1,60002,100,60102),"matching_ack_state_preserved");
        ck(src->ProbeCapacityFactor(1,60102)==0.5,"invalid_capacity_keeps_half_factor");
        auto &state=src->m_linkStateTable[MakeLinkId(1,1)];state.availableQ=500;state.sampleEpoch=uint16_t(60100/lcObservationTickNs);
        ck(src->ProbeCapacityFactor(1,60102)==1.0,"fresh_capacity_releases_discount");
    });
    lcAlpsProbe=false;lcCapacityBaselineProbe=false;lcBwCc=false;lcEarlyProbe=false;Simulator::Destroy();
    std::cout<<(ok?"ALPS_NATIVE_PROBE_SELF_CHECK_PASS":"ALPS_NATIVE_PROBE_SELF_CHECK_FAIL")
        <<" assertions="<<checks<<",same_original_helpers,RNG_128_seeds,strict_expiry,expired_subset_cooldown,no_pending_gate,no_source_throttle,no_exponential_backoff,factor_half\n";
    return ok;
}

bool RdmaSmartFlowRouting::RunEarlyProbeSelfChecks() {
    lcEnabled=true;lcEarlyProbe=true;lcEarlyAudit=false;lcWindowNs=5000;lcTtlNs=100000;lcEarlyLeadNs=20000;lcEarlyIntervalNs=1000;lcEarlyTimeoutNs=1000000;
    Ptr<RdmaSmartFlowRouting> r=CreateObject<RdmaSmartFlowRouting>();r->m_nodeId=0;
    PathData a,b;a.pid=1;b.pid=2;a.nodeIdSequence={100,1,9,99};b.nodeIdSequence={100,2,9,99};
    a.portSequence=b.portSequence={1,1,1};a.latency=b.latency=10000;a.tsGeneration=b.tsGeneration=NanoSeconds(90000);
    r->m_nexthopSelTbl[1]=a;r->m_nexthopSelTbl[2]=b;std::vector<PathData*> paths={&a,&b};std::vector<double> eligible={1,1};
    auto set=[&](uint32_t node,uint64_t stamp){auto &v=r->m_linkStateTable[MakeLinkId(node,1)];v.availableQ=100;v.sampleEpoch=stamp/4;};
    set(1,20000);set(2,20000);set(9,20000);
    if(r->SelectEarlyProbe(paths,eligible,90000))return false; // Age 70us: not yet due.
    set(9,5000); // Age 85us: shared link becomes due before TTL.
    PathData *p=r->SelectEarlyProbe(paths,eligible,90000);if(!p || r->m_earlyReason!=3)return false;
    r->RecordEarlyProbeSent(p->pid,90000);
    if(r->SelectEarlyProbe(paths,eligible,92000))return false; // Pending path covers shared need.
    if(r->CompleteEarlyProbe(p->pid,89999,2000,92000))return false; // Old ACK cannot clear current request.
    if(!r->CompleteEarlyProbe(p->pid,90000,2000,92000))return false;
    set(9,92000);set(1,92000);set(2,92000);
    if(r->SelectEarlyProbe(paths,eligible,93000))return false; // Received link refresh reused by both paths.
    r->m_linkStateTable.erase(MakeLinkId(2,1));eligible={1,0};
    if(r->SelectEarlyProbe(paths,eligible,94000))return false; // Ineligible unknown path not probed.
    eligible={1,1};p=r->SelectEarlyProbe(paths,eligible,94000);if(!p || p->pid!=2 || r->m_earlyReason!=1)return false;
    r->RecordEarlyProbeSent(2,94000);
    if(r->SelectEarlyProbe(paths,eligible,94500))return false; // Source rate limit.
    std::vector<PathData*> only={&b};std::vector<double> oneWeight={1};
    if(r->SelectEarlyProbe(only,oneWeight,100000))return false;
    if(!r->SelectEarlyProbe(only,oneWeight,1094000) || lcEarlyStats[0].timeouts!=1)return false;
    std::cout<<"EARLY_GHOST_SELFTEST_PASS: pre-expiry, shared-link reuse, in-flight dedup, old ACK, zero eligibility, rate limit, timeout\n";
    return true;
}


bool RdmaSmartFlowRouting::RunProbeBackoffSelfChecks() {
    lcEnabled=true;lcBwCc=true;lcEarlyProbe=true;lcEarlyAudit=true;
    lcEarlyTimeoutNs=1000000;lcEarlyIntervalNs=1000;lcWindowNs=5000;lcTtlNs=100000;
    ConfigureEarlyProbeAudit("."); // Persist the synthetic retry sequence and reset transition.
    auto r=CreateObject<RdmaSmartFlowRouting>();r->m_nodeId=1000;r->m_earlyStarted=true;
    PathData a;a.pid=1;a.nodeIdSequence={1000,1,99};a.portSequence={1,1};r->m_nexthopSelTbl[1]=a;
    std::vector<PathData*> paths={&a};std::vector<double> eligible={1};bool ok=true;
    auto &state=r->m_earlyProbes[1];state.lastBudgetQ=400;
    r->RecordEarlyProbeSent(1,0);std::vector<uint64_t> observed={ProbeTimeoutNs(state)};
    std::vector<std::function<void()>> events(20);uint64_t sent=0;
    auto invoke=+[](std::function<void()> *f){(*f)();};
    for(unsigned k=0;k<8;++k) {
        unsigned shift=std::min(k,6u);uint64_t wait=1000000ULL<<shift;uint64_t deadline=sent+wait;
        events[2*k]=[&,k,wait](){
            uint64_t now=Simulator::Now().GetNanoSeconds();
            ok&=ProbeTimeoutNs(state)==wait && !r->SelectEarlyProbe(paths,eligible,now);
            ok&=lcEarlyStats[1000].timeouts==k && state.pending && r->ProbeCapacityFactor(1,now)==0.2;
        };
        events[2*k+1]=[&,k,deadline](){
            auto path=r->SelectEarlyProbe(paths,eligible,Simulator::Now().GetNanoSeconds());
            ok&=path && path->pid==1 && !state.pending && state.timeoutShift==std::min(k+1,6u);
            ok&=lcEarlyStats[1000].timeouts==k+1 && state.lastBudgetQ==400 && state.awaitingFresh;
            if(path)r->RecordEarlyProbeSent(1,deadline);
            observed.push_back(ProbeTimeoutNs(state));
        };
        Simulator::Schedule(NanoSeconds(deadline-1),invoke,&events[2*k]);
        Simulator::Schedule(NanoSeconds(deadline),invoke,&events[2*k+1]);sent=deadline;
    }
    uint64_t lastSent=sent;
    events[16]=[&](){
        ok&=!r->CompleteEarlyProbe(1,0,1,Simulator::Now().GetNanoSeconds());
        ok&=state.pending && state.timeoutShift==6; // old ACK cannot reset current round
    };
    events[17]=[&](){
        uint64_t now=Simulator::Now().GetNanoSeconds();
        ok&=r->CompleteEarlyProbe(1,lastSent,1000,now) && state.timeoutShift==0 && !state.pending;
        ok&=ProbeTimeoutNs(state)==1000000 && state.awaitingFresh && r->ProbeCapacityFactor(1,now)==0.2;
        ok&=lcEarlyStats[1000].backoffResets==1 && lcEarlyStats[1000].backoffCapHits==2;
    };
    events[18]=[&](){
        uint64_t now=Simulator::Now().GetNanoSeconds();auto &v=r->m_linkStateTable[MakeLinkId(1,1)];
        v.availableQ=400;v.sampleEpoch=uint16_t(now/lcObservationTickNs);
        ok&=r->ProbeCapacityFactor(1,now)==1.0 && !state.awaitingFresh && state.lastBudgetQ==400;
        EarlyProbeState huge;huge.rttNs=std::numeric_limits<uint64_t>::max();huge.timeoutShift=6;
        ok&=ProbeTimeoutNs(huge)==std::numeric_limits<uint64_t>::max();
    };
    Simulator::Schedule(NanoSeconds(lastSent+1),invoke,&events[16]);
    Simulator::Schedule(NanoSeconds(lastSent+1000),invoke,&events[17]);
    Simulator::Schedule(NanoSeconds(lastSent+2000),invoke,&events[18]);
    Simulator::Run();Simulator::Destroy();
    std::cout<<"GHOST_BACKOFF_WAIT_MS";for(uint64_t x:observed)std::cout<<' '<<x/1000000;std::cout<<'\n';
    std::cout<<(ok?"GHOST_BACKOFF_SELF_CHECK_PASS":"GHOST_BACKOFF_SELF_CHECK_FAIL")<<" strict_deadlines,no_duplicate,doubling,cap64,matching_ack_reset,old_ack_no_reset,capacity_discount_preserved,overflow_guard probe_state_bytes="<<sizeof(EarlyProbeState)<<" LST_bytes="<<sizeof(CompactLinkState)<<'\n';
    lcBwCc=false;lcEarlyProbe=false;return ok;
}

bool RdmaSmartFlowRouting::lcEnabled=false;
uint64_t RdmaSmartFlowRouting::lcObservationTickNs=4; // ACK_IMMEDIATE_V1
uint64_t RdmaSmartFlowRouting::lcWindowNs=10000;
uint64_t RdmaSmartFlowRouting::lcTtlNs=100000;
uint64_t RdmaSmartFlowRouting::lcStopNs=0;
std::ofstream RdmaSmartFlowRouting::lcSamplesLog, RdmaSmartFlowRouting::lcFeedbackLog;
std::ofstream RdmaSmartFlowRouting::lcStateLog, RdmaSmartFlowRouting::lcTxLog;
uint32_t RdmaSmartFlowRouting::MakeLinkId(uint32_t n,uint32_t p) {
    NS_ABORT_MSG_IF(n>65535 || p>65535,"Link ID encoding requires 16-bit node and port");
    return (n<<16)|p;
}
void RdmaSmartFlowRouting::ConfigureLinkCapacity(bool enabled,uint64_t window,uint64_t ttl,
                                                uint64_t stop,const std::string &directory) {
    NS_ABORT_MSG_IF(!window || ttl<window || ttl/lcObservationTickNs>=32767,"ACK_IMMEDIATE_V1: invalid window or observation TTL");
    lcEnabled=enabled;lcWindowNs=window;lcTtlNs=ttl;lcStopNs=stop;
    if(!enabled)return;
    NS_ABORT_MSG_IF(lcBwMode>2,"Invalid bandwidth weighting mode");
    if(lcDetailedLogs) {
    lcSamplesLog.open(directory+"/link-samples.csv");
    lcFeedbackLog.open(directory+"/link-feedback.csv");
    lcStateLog.open(directory+"/link-state.csv");
    lcTxLog.open(directory+"/link-transmits.csv");
    NS_ABORT_MSG_IF(!lcSamplesLog || !lcFeedbackLog || !lcStateLog || !lcTxLog,"Cannot open telemetry logs");
    lcSamplesLog << "node,port,window_start_ns,window_end_ns,capacity_bps,tx_bytes_total,delta_tx_bytes,tx_bps_raw,available_bps,queue_bytes,paused_mask,sample_seq,sample_epoch,available_q,valid,reason\n";
    lcFeedbackLog << "event,time_ns,node,path,packet_uid,link_id,sample_epoch,available_q,born_ns,overflow\n";
    lcStateLog << "event,time_ns,node,path,link_id,sample_epoch,available_q,reason\n";
    lcTxLog << "time_ns,node,port,packet_uid,bytes,tx_bytes_total\n";
    lcSamplesLog << std::setprecision(17);
    lcGuardLog.open(directory+"/link-guard-samples.csv");
    lcPruneLog.open(directory+"/link-pruning.csv");
    NS_ABORT_MSG_IF(!lcGuardLog || !lcPruneLog,"Cannot open link guard logs");
    lcGuardLog << "observation_ns,node,port,path,ack_uid,capacity_sample_ns,capacity_age_ns,available_q,queue_index,queue_bytes,threshold_bytes,congested,encoded_state,observation_epoch\n";
    lcPruneLog << "time_ns,node,event,blocked_paths,total_paths,path_ids\n";
    } else {
        lcSamplesLog.setstate(std::ios::failbit);lcFeedbackLog.setstate(std::ios::failbit);
        lcStateLog.setstate(std::ios::failbit);lcTxLog.setstate(std::ios::failbit);
        lcGuardLog.setstate(std::ios::failbit);lcPruneLog.setstate(std::ios::failbit);
    }

}
bool RdmaSmartFlowRouting::ReadLinkCapacity(uint32_t link,uint64_t now,uint16_t &q) const {
    auto it=m_linkStateTable.find(link);
    if(it==m_linkStateTable.end() || it->second.CapacityQ()==0xffff)return false;
    const uint64_t round=now/lcObservationTickNs;
    const uint16_t age=uint16_t(uint16_t(round)-it->second.sampleEpoch);
    if(age>round || age>=32768 || now-(round-age)*lcObservationTickNs>lcTtlNs)return false;
    q=it->second.CapacityQ();return true; // ACK_QUEUE_V1
}
bool RdmaSmartFlowRouting::AcceptLinkCapacity(const LinkCapacityRecord &r,uint32_t pid,uint64_t now) {
    std::string reason;
    auto path=m_nexthopSelTbl.find(pid);
    bool belongs=false;
    if(path!=m_nexthopSelTbl.end()) {
        const auto &p=path->second;
        for(size_t j=0;j<p.portSequence.size() && j+1<p.nodeIdSequence.size();++j)
            if(MakeLinkId(p.nodeIdSequence[j],p.portSequence[j])==r.linkId)belongs=true;
    }
    uint64_t round=now/lcObservationTickNs;
    uint16_t age=uint16_t(uint16_t(round)-r.state.sampleEpoch);
    if(!belongs)reason="not_on_forward_path";
    else if(age>round || age>=32768 || now-(round-age)*lcObservationTickNs>lcTtlNs)reason="stale_or_future";
    else {
        auto old=m_linkStateTable.find(r.linkId);
        if(old!=m_linkStateTable.end()) {
            uint16_t oldAge=uint16_t(uint16_t(round)-old->second.sampleEpoch);
            const bool oldFresh=oldAge<=round && oldAge<32768 && now-(round-oldAge)*lcObservationTickNs<=lcTtlNs;
            if(oldFresh) {
                uint16_t difference=uint16_t(r.state.sampleEpoch-old->second.sampleEpoch);
                if(difference==0 && lcMaxLinkQueue)reason=r.state.EncodedCapacityQ()==old->second.EncodedCapacityQ()?"duplicate":"same_epoch_conflict";
                else if(difference==0) {
                    if(r.state.availableQ==old->second.availableQ)reason="duplicate";
                    else if(r.state.EncodedCapacityQ()==old->second.EncodedCapacityQ() && r.state.Congested() && !old->second.Congested()) {
                        // ACK_IMMEDIATE_V1: unresolved same-4ns ordering retains congestion conservatively.
                    } else if(r.state.EncodedCapacityQ()==old->second.EncodedCapacityQ() && old->second.Congested())reason="same_tick_keep_congested";
                    else reason="same_epoch_conflict";
                }
                else if(difference>=32768)reason="older_epoch";
            }
        }
    }
    bool accepted=reason.empty();
    if(accepted) {auto &target=m_linkStateTable[r.linkId];
        if(lcMaxLinkQueue){bool alarm=target.Congested();uint16_t q=r.state.EncodedCapacityQ();target.availableQ=q==0xffff?(alarm?0xfffe:0xffff):uint16_t(q|(alarm?0x8000:0));target.sampleEpoch=r.state.sampleEpoch;}
        else target=r.state;if(lcEarlyAudit)m_expiredLinksForAudit.erase(r.linkId);reason=r.state.availableQ==0xffff?"new_invalid":"new_sample";}
    if(lcStateLog)lcStateLog << (accepted?"accept":"reject") << ',' << now << ',' << (m_node ? m_node->GetId() : m_nodeId) << ',' << pid << ','
        << r.linkId << ',' << r.state.sampleEpoch << ',' << r.state.availableQ << ',' << reason << '\n';
    return accepted;
}
void RdmaSmartFlowRouting::ExpireLinkCapacityEntries(uint64_t now) {
    uint64_t round=now/lcObservationTickNs;
    for(auto it=m_linkStateTable.begin();it!=m_linkStateTable.end();) {
        if(lcMaxLinkQueue && it->second.queueEpoch!=UINT16_MAX){uint64_t queueAge=0;
            if(!QueueEvidenceAge(it->second.queueEpoch,now,queueAge)){it->second.queueEpoch=UINT16_MAX;uint16_t q=it->second.EncodedCapacityQ();it->second.availableQ=q;}
        }
        uint16_t age=uint16_t(uint16_t(round)-it->second.sampleEpoch);
        if(age>round || age>=32768 || now-(round-age)*lcObservationTickNs>lcTtlNs) {
            uint64_t queueAge=0;if(lcMaxLinkQueue && QueueEvidenceAge(it->second.queueEpoch,now,queueAge)){uint16_t q=0xffff;it->second.availableQ=it->second.Congested()?0xfffe:q;++it;continue;}
            if(lcStateLog)lcStateLog << "expire," << now << ',' << (m_node ? m_node->GetId() : m_nodeId) << ",0," << it->first << ','
                 << it->second.sampleEpoch << ',' << it->second.availableQ << ",ttl\n";
            if(lcEarlyAudit)m_expiredLinksForAudit.insert(it->first); // EARLY_GHOST_V1: diagnostic provenance only
            it=m_linkStateTable.erase(it);
        } else ++it;
    }
}
void RdmaSmartFlowRouting::AgeLinkCapacityEntries() {
    uint64_t now=Simulator::Now().GetNanoSeconds();ExpireLinkCapacityEntries(now);
    if(lcEnabled && now+lcWindowNs<lcStopNs)
        m_linkCapacityAgingEvent=Simulator::Schedule(NanoSeconds(lcWindowNs),&RdmaSmartFlowRouting::AgeLinkCapacityEntries,this);
}
// ACK_PATH_QUEUE_MAX_V1: one complete ACK updates only its own path queue maximum.
bool RdmaSmartFlowRouting::AcceptPathQueueMaximum(uint32_t pid,const LinkCapacityTag &tag,uint64_t now) {
    if(!lcBwCc || !lcPathQueueMax || tag.overflow || tag.maxQueueBytes==UINT32_MAX || now<tag.bornNs || now-tag.bornNs>lcTtlNs)return false;
    auto it=m_nexthopSelTbl.find(pid);if(it==m_nexthopSelTbl.end())return false;auto &path=it->second;
    if(path.portSequence.size()<2 || path.nodeIdSequence.size()!=path.portSequence.size()+1 || tag.count!=path.portSequence.size()-1)return false;
    std::set<uint32_t> expected,observed;
    for(size_t j=1;j<path.portSequence.size();++j)expected.insert(MakeLinkId(path.nodeIdSequence[j],path.portSequence[j]));
    for(uint8_t i=0;i<tag.count;++i)if(!observed.insert(tag.records[i].linkId).second)return false;
    if(expected!=observed)return false;
    if(path.queueFeedbackBornNs!=UINT64_MAX) {
        if(tag.bornNs<path.queueFeedbackBornNs)return false;
        if(tag.bornNs==path.queueFeedbackBornNs && tag.maxQueueBytes<=path.maxEgressQueueBytes)return false;
    }
    path.maxEgressQueueBytes=tag.maxQueueBytes;path.queueFeedbackBornNs=tag.bornNs;
    if(lcPathQueueMaxLog)lcPathQueueMaxLog<<now<<','<<(m_node?m_node->GetId():m_nodeId)<<','<<pid<<','<<tag.maxQueueBytes<<','<<tag.bornNs<<','<<unsigned(tag.count)<<','<<(tag.maxQueueBytes>=lcQueueAlarmBytes)<<'\n';
    return true;
}
// MAX_LINK_QUEUE_V1: queue state stays in LST; an independent 2-byte epoch prevents false refresh.
uint32_t RdmaSmartFlowRouting::ScaleQueueBytes(uint32_t bytes,uint64_t rate){
    if(!rate)return UINT32_MAX;
    __uint128_t scaled=__uint128_t(bytes)*100000000000ULL/rate;
    return scaled>=UINT32_MAX?UINT32_MAX-1:uint32_t(scaled);
}
bool RdmaSmartFlowRouting::QueueEvidenceAge(uint16_t epoch,uint64_t now,uint64_t &age){
    if(epoch==UINT16_MAX)return false;
    uint64_t tick=now/lcObservationTickNs,delta=(tick%65535+65535-epoch)%65535;
    if(delta>tick || delta>=32767)return false;
    age=now-(tick-delta)*lcObservationTickNs;return age<=lcTtlNs;
}
bool RdmaSmartFlowRouting::AcceptMaxQueueEvidence(uint32_t link,bool alarm,uint64_t observed,uint64_t now){
    if(observed>now || now-observed>lcTtlNs)return false;
    auto &v=m_linkStateTable[link];uint64_t age=0;
    if(QueueEvidenceAge(v.queueEpoch,now,age)){
        uint64_t previous=now-age;
        if(observed<previous || (observed/lcObservationTickNs==previous/lcObservationTickNs && v.Congested() && !alarm))return false;
    }
    uint16_t q=v.EncodedCapacityQ();v.availableQ=q==0xffff?(alarm?0xfffe:0xffff):uint16_t(q|(alarm?0x8000:0));
    v.queueEpoch=uint16_t((observed/lcObservationTickNs)%65535);return true;
}
bool RdmaSmartFlowRouting::AcceptMaxQueueFeedback(uint32_t pid,const LinkCapacityTag &tag,uint64_t now){
    uint32_t node=m_node?m_node->GetId():m_nodeId;bool valid=true;
    if(tag.overflow || tag.maxQueueBytes==UINT32_MAX || tag.maxQueueLinkId==UINT32_MAX || now<tag.bornNs || now-tag.bornNs>lcTtlNs)valid=false;
    auto it=m_nexthopSelTbl.find(pid);if(it==m_nexthopSelTbl.end())valid=false;
    std::set<uint32_t> expected,observed;const LinkCapacityRecord *winner=nullptr;
    if(valid){const auto &p=it->second;
        if(p.portSequence.size()<2 || p.nodeIdSequence.size()!=p.portSequence.size()+1 || tag.count!=p.portSequence.size()-1)valid=false;
        else for(size_t j=1;j<p.portSequence.size();++j)expected.insert(MakeLinkId(p.nodeIdSequence[j],p.portSequence[j]));
    }
    uint64_t tick=now/lcObservationTickNs;
    for(uint8_t i=0;i<tag.count && valid;++i){const auto &v=tag.records[i];
        uint16_t delta=uint16_t(uint16_t(tick)-v.state.sampleEpoch);
        if(!observed.insert(v.linkId).second || delta>tick || delta>=32768)valid=false;
        else {uint64_t sample=(tick-delta)*lcObservationTickNs;if(now-sample>lcTtlNs || sample+lcObservationTickNs-1<tag.bornNs)valid=false;}
        if(v.linkId==tag.maxQueueLinkId)winner=&v;
    }
    valid&=expected==observed && winner!=nullptr;
    if(lcBwCcAudit){auto &a=lcMaxLinkQueueStats[node];++a.reports;if(!valid)++a.rejected;}
    bool alarm=tag.maxQueueBytes>=lcQueueAlarmBytes,updated=false;uint64_t stamp=0;
    if(valid){
        // A low maximum proves every sampled egress on this ACK was below threshold.
        // A high maximum gives no clearing evidence about the other links.
        for(uint8_t i=0;i<tag.count;++i){const auto &v=tag.records[i];if(alarm && v.linkId!=tag.maxQueueLinkId)continue;
            uint16_t delta=uint16_t(uint16_t(tick)-v.state.sampleEpoch);uint64_t sample=(tick-delta)*lcObservationTickNs;
            if(v.linkId==tag.maxQueueLinkId)stamp=sample;
            updated|=AcceptMaxQueueEvidence(v.linkId,alarm,sample,now);
        }
        auto &p=it->second;if(p.queueFeedbackBornNs==UINT64_MAX || tag.bornNs>p.queueFeedbackBornNs || (tag.bornNs==p.queueFeedbackBornNs && tag.maxQueueBytes>p.maxEgressQueueBytes)){
            p.maxEgressQueueBytes=tag.maxQueueBytes;p.queueFeedbackBornNs=tag.bornNs;
        }
    }
    if(lcMaxLinkQueueFeedbackLog)lcMaxLinkQueueFeedbackLog<<now<<','<<node<<','<<pid<<','<<tag.bornNs<<','<<tag.maxQueueBytes<<','<<tag.maxQueueLinkId<<','<<stamp<<','<<alarm<<','<<valid<<','<<updated<<','<<unsigned(tag.count)<<'\n';
    return updated;
}

bool RdmaSmartFlowRouting::ReadPathQueueCongestion(const PathData &path,uint64_t now,bool &congested) const {
    if(path.maxEgressQueueBytes==UINT32_MAX || path.queueFeedbackBornNs==UINT64_MAX || now<path.queueFeedbackBornNs || now-path.queueFeedbackBornNs>lcTtlNs)return false;
    congested=path.maxEgressQueueBytes>=lcQueueAlarmBytes;return true;
}

bool RdmaSmartFlowRouting::ReceiveLinkCapacityFeedback(Ptr<Packet> p) {
    if(!lcEnabled)return false;
    LinkCapacityTag tag;AckPathTag ack;
    if(!p->PeekPacketTag(tag) || !p->PeekPacketTag(ack))return false;
    uint64_t now=Simulator::Now().GetNanoSeconds();
    auto own=m_nexthopSelTbl.find(ack.GetPathId());
    uint64_t previousBorn=own==m_nexthopSelTbl.end()?UINT64_MAX:own->second.queueFeedbackBornNs;
    bool updated=lcMaxLinkQueue?false:AcceptPathQueueMaximum(ack.GetPathId(),tag,now);
    bool lifetimeValid=now>=tag.bornNs && now-tag.bornNs<32767*lcObservationTickNs;
    for(uint8_t i=0;i<tag.count;++i) {
        const auto &r=tag.records[i];
        if(lcFeedbackLog)lcFeedbackLog << "receive," << now << ',' << (m_node ? m_node->GetId() : m_nodeId) << ',' << ack.GetPathId() << ',' << p->GetUid()
            << ',' << r.linkId << ',' << r.state.sampleEpoch << ',' << r.state.availableQ << ',' << tag.bornNs << ',' << tag.overflow << '\n';
        bool accepted=false;
        if(!tag.overflow && lifetimeValid)accepted=AcceptLinkCapacity(r,ack.GetPathId(),now);
        else if(lcStateLog)lcStateLog << "reject," << now << ',' << (m_node ? m_node->GetId() : m_nodeId) << ',' << ack.GetPathId() << ',' << r.linkId << ','
            << r.state.sampleEpoch << ',' << r.state.availableQ << ',' << (tag.overflow?"tag_overflow":"packet_lifetime") << '\n';
        updated|=accepted;
        if(lcEarlyAudit){Ipv4SmartFlowProbeTag probe;if(p->PeekPacketTag(probe)) {
            auto &a=lcEarlyStats[m_node?m_node->GetId():m_nodeId];
            if(accepted)++a.probeRecordsAccepted;else ++a.probeRecordsRejected;
        }}
    }
    if(lcMaxLinkQueue)updated|=AcceptMaxQueueFeedback(ack.GetPathId(),tag,now);
    if(lcCapacityBaselineProbe)updated|=RecordPathCapacityUpdate(ack.GetPathId(),tag,previousBorn,now);
    if(m_linkCapacityAgingEvent.IsExpired() && now+lcWindowNs<lcStopNs)
        m_linkCapacityAgingEvent=Simulator::Schedule(NanoSeconds(lcWindowNs),&RdmaSmartFlowRouting::AgeLinkCapacityEntries,this);
    return updated;
}

// QUEUE90_PROBE20_V1: queue validity is independent of capacity-estimate validity.
bool RdmaSmartFlowRouting::ReadLinkCongestion(uint32_t link,uint64_t now,bool &congested) const {
    auto it=m_linkStateTable.find(link);if(it==m_linkStateTable.end())return false;
    if(lcMaxLinkQueue){uint64_t age;if(!QueueEvidenceAge(it->second.queueEpoch,now,age))return false;congested=it->second.Congested();return true;}
    if(it->second.availableQ==0xffff)return false;
    uint64_t round=now/lcObservationTickNs;uint16_t age=uint16_t(uint16_t(round)-it->second.sampleEpoch);
    if(age>round || age>=32768 || now-(round-age)*lcObservationTickNs>lcTtlNs)return false;
    congested=it->second.Congested();return true;
}
bool RdmaSmartFlowRouting::HasPostProbeCapacity(uint32_t pid,uint64_t now,uint64_t sent) const {
    auto it=m_nexthopSelTbl.find(pid);if(it==m_nexthopSelTbl.end())return false;
    const auto &p=it->second;
    for(size_t j=1;j<p.portSequence.size();++j) {
        uint32_t id=MakeLinkId(p.nodeIdSequence[j],p.portSequence[j]);uint64_t age=0;
        if(!LinkObservationAge(id,now,age) || now-age<sent)return false;
    }
    return p.portSequence.size()>=2;
}
double RdmaSmartFlowRouting::ProbeCapacityFactor(uint32_t pid,uint64_t now) {
    auto it=m_earlyProbes.find(pid);if(it==m_earlyProbes.end())return 1.0;
    auto &p=it->second;
    // A timeout does not silently restore a stale budget. Fresh complete feedback may release it.
    if(p.awaitingFresh && !p.pending && HasPostProbeCapacity(pid,now,p.sentNs))p.awaitingFresh=false;
    return p.pending || p.awaitingFresh?(lcAlpsProbe?0.5:0.2):1.0;
}

bool RdmaSmartFlowRouting::GetForwardSwitchSignedScore(uint32_t pid,uint64_t now,int32_t &score) const {
    auto it=m_nexthopSelTbl.find(pid);if(it==m_nexthopSelTbl.end())return false;
    const auto &p=it->second;if(p.nodeIdSequence.size()!=p.portSequence.size()+1 || p.portSequence.size()<2)return false;
    score=16382;
    for(size_t j=1;j<p.portSequence.size();++j){uint32_t id=MakeLinkId(p.nodeIdSequence[j],p.portSequence[j]);uint16_t q;
        if(!ReadLinkCapacity(id,now,q))return false;score=std::min(score,m_linkStateTable.at(id).SignedScoreQ());}
    return true;
}
bool RdmaSmartFlowRouting::GetForwardSwitchBottleneck(uint32_t pid,uint64_t now,uint16_t &q,uint32_t &link) const {
    auto it=m_nexthopSelTbl.find(pid);if(it==m_nexthopSelTbl.end())return false;
    const auto &p=it->second;
    if(p.nodeIdSequence.size()!=p.portSequence.size()+1 || p.portSequence.size()<2)return false;
    q=0xfffe;link=0;
    // Hop zero is the source NIC, outside switch telemetry; do not fabricate its free capacity.
    for(size_t j=1;j<p.portSequence.size();++j) {
        uint32_t id=MakeLinkId(p.nodeIdSequence[j],p.portSequence[j]);uint16_t value;
        if(!ReadLinkCapacity(id,now,value))return false;
        if(value<q) {q=value;link=id;}
    }
    return true;
}

// ACK_QUEUE_V1: all paths referencing the same directed link see the same fresh LST flag.
bool RdmaSmartFlowRouting::ApplyLinkCongestionPruning(const std::vector<PathData *> &paths,std::vector<double> &weights) {
    // BANDWIDTH_CC_V1: full mask, including all-blocked; the NIC handles probe-only pause.
    if(lcBwCc) {
        auto status=EvaluateBandwidthSelection(paths,Simulator::Now().GetNanoSeconds());
        if(!status.blockedCount)return false;
        double sum=0;size_t live=paths.size()-status.blockedCount;
        for(size_t i=0;i<paths.size();++i){if(status.blocked[i])weights[i]=0;else sum+=weights[i];}
        if(live)for(size_t i=0;i<paths.size();++i)if(!status.blocked[i])weights[i]=sum>0?weights[i]/sum:1.0/live;
        return true;
    }
    if(!lcEnabled || !lcPruneEnabled)return false;
    NS_ABORT_MSG_IF(paths.empty() || paths.size()!=weights.size(),"Invalid pruning input");
    uint64_t now=Simulator::Now().GetNanoSeconds();
    std::vector<bool> blocked(paths.size(),false);size_t count=0;std::ostringstream ids;
    for(size_t i=0;i<paths.size();++i) {
        const auto &path=*paths[i];
        for(size_t hop=1;hop<path.portSequence.size() && hop+1<path.nodeIdSequence.size();++hop) {
            uint32_t link=MakeLinkId(path.nodeIdSequence[hop],path.portSequence[hop]);uint16_t q;
            if(ReadLinkCapacity(link,now,q) && q==0 && m_linkStateTable.at(link).Congested()) {
                blocked[i]=true;++count;if(count>1)ids << ';';ids << path.pid;break;
            }
        }
    }
    if(!count)return false;
    // No alternate path: retain original ALPS rate/path control rather than returning all-zero weights.
    const bool all=count==paths.size();
    if(lcPruneLog)lcPruneLog << now << ',' << (m_node?m_node->GetId():m_nodeId) << ','
        << (all?"all_blocked_fallback":"prune") << ',' << count << ',' << paths.size() << ',' << ids.str() << '\n';
    if(all)return false;
    double sum=0;
    for(size_t i=0;i<weights.size();++i) {if(blocked[i])weights[i]=0;else sum+=weights[i];}
    for(size_t i=0;i<weights.size();++i)
        if(!blocked[i])weights[i]=sum>0?weights[i]/sum:1.0/(paths.size()-count);
    return true;
}



// BANDWIDTH_CC_V1. The shared-link budget is exactly zero when its residual is
// zero. It is therefore <= every nonnegative private bottleneck, including ties.
// A healthy shared link alone never proves the rest of a path healthy.

// QUEUE_DEFAULT26_V1: 26,000 B absolute alarm; old Kmax percentage remains explicitly selectable.
uint32_t RdmaSmartFlowRouting::lcQueueAlarmBytes=150000;
bool RdmaSmartFlowRouting::lcPathOnlyQueue=false; // Normal CLI opts in, legacy component fixtures remain off.
std::ofstream RdmaSmartFlowRouting::lcPathOnlyQueueLog;
bool RdmaSmartFlowRouting::lcMaxLinkQueue=false;
std::map<uint32_t,RdmaSmartFlowRouting::MaxLinkQueueAudit> RdmaSmartFlowRouting::lcMaxLinkQueueStats;
std::ofstream RdmaSmartFlowRouting::lcMaxLinkQueueHopLog,RdmaSmartFlowRouting::lcMaxLinkQueueFeedbackLog,RdmaSmartFlowRouting::lcMaxLinkQueuePathLog;
bool RdmaSmartFlowRouting::lcAlpsCongestion=false; // Legacy component fixtures opt in explicitly; normal CLI default true.
std::map<uint32_t,RdmaSmartFlowRouting::AlpsCongestionAudit> RdmaSmartFlowRouting::lcAlpsCongestionStats;
std::ofstream RdmaSmartFlowRouting::lcAlpsCongestionLog;
bool RdmaSmartFlowRouting::lcRequireZeroBw=false;
std::map<uint32_t,RdmaSmartFlowRouting::JointQueueAudit> RdmaSmartFlowRouting::lcJointQueueStats;
bool RdmaSmartFlowRouting::lcPathQueueMax=false; // Isolated old component fixtures; main enables the user default.
std::ofstream RdmaSmartFlowRouting::lcPathQueueMaxLog;
bool RdmaSmartFlowRouting::lcQueueMaxAll=false;
bool RdmaSmartFlowRouting::lcQueueSumAll=true;
uint32_t RdmaSmartFlowRouting::lcQueuePercent=100;
bool RdmaSmartFlowRouting::RunZeroBandwidthQueueSelfChecks() {
    lcEnabled=true;lcBwCc=true;lcPathQueueMax=true;lcRequireZeroBw=true;lcBwMode=2;lcTtlNs=100000;lcQueueAlarmBytes=26000;lcBwCcAudit=false;
    auto r=CreateObject<RdmaSmartFlowRouting>();r->m_nodeId=1000;
    PathData a,b,c;a.pid=1;b.pid=2;c.pid=3;
    a.nodeIdSequence={1000,1,3,7,99};a.portSequence={1,1,1,1};
    b.nodeIdSequence={1000,2,3,7,99};b.portSequence={2,1,1,1};
    c.nodeIdSequence={1000,4,5,7,99};c.portSequence={3,1,1,1};
    std::vector<PathData*> paths={&a,&b,&c};const uint64_t now=200000;
    auto load=[&](uint16_t q,uint32_t queue){
        r->m_linkStateTable.clear();for(uint32_t node:{1u,2u,3u,4u,5u,7u}){auto &v=r->m_linkStateTable[MakeLinkId(node,1)];v.availableQ=q;v.sampleEpoch=uint16_t(now/lcObservationTickNs);}
        for(auto p:paths){p->maxEgressQueueBytes=queue;p->queueFeedbackBornNs=now;}
    };
    bool ok=true;
    for(uint16_t q:{uint16_t(0),uint16_t(1)})for(uint32_t queue:{25999u,26000u}){
        load(q,queue);auto s=r->EvaluateBandwidthCongestion(paths,now);bool expected=q==0 && queue>=26000;
        ok&=s.blockedCount==(expected?3u:0u);std::cout<<"ZERO_QUEUE_CASE q="<<q<<" queue="<<queue<<" blocked="<<s.blockedCount<<'\n';
    }
    // Check the shortcut against the actual multi-group shared-link projection.
    unsigned combinations=0;for(unsigned mask=0;mask<64;++mask){load(100,26000);unsigned bit=0;
        for(uint32_t node:{1u,2u,3u,4u,5u,7u})r->m_linkStateTable[MakeLinkId(node,1)].availableQ=(mask&(1u<<bit++))?0:100;
        SharedQuotaResult grant;ok&=r->ComputeSharedQuota(paths,{true,true,true},now,grant);auto s=r->EvaluateBandwidthCongestion(paths,now);
        for(unsigned i=0;i<3;++i)ok&=s.blocked[i]==(grant.budgetQ[i]==0);++combinations;
    }
    for(uint16_t unknown:{uint16_t(0xffff),uint16_t(0xfffe)}){load(unknown,26000);auto s=r->EvaluateBandwidthCongestion(paths,now);ok&=s.blockedCount==0 && s.knownCount==0;}
    load(100,26000);r->m_linkStateTable.erase(MakeLinkId(1,1));ok&=r->EvaluateBandwidthCongestion(paths,now).blockedCount==0;
    r->m_linkStateTable[MakeLinkId(3,1)].availableQ=0;auto partial=r->EvaluateBandwidthCongestion(paths,now);ok&=partial.blocked[0] && partial.blocked[1] && !partial.blocked[2];
    load(0,26000);for(auto p:paths)p->queueFeedbackBornNs=now-lcTtlNs-1;ok&=r->EvaluateBandwidthCongestion(paths,now).blockedCount==0;
    load(0,26000);for(auto &e:r->m_linkStateTable)e.second.sampleEpoch=uint16_t((now-lcTtlNs-4)/lcObservationTickNs);ok&=r->EvaluateBandwidthCongestion(paths,now).blockedCount==0;
    load(100,26000);r->m_earlyProbes[1].lastBudgetQ=0;r->m_earlyProbes[1].pending=true;ok&=r->EvaluateBandwidthCongestion(paths,now).blockedCount==0;
    lcRequireZeroBw=false;ok&=r->EvaluateBandwidthCongestion(paths,now).blockedCount==3;
    lcRequireZeroBw=true;lcBwCcAudit=true;lcJointQueueStats.clear();load(100,26000);r->EvaluateBandwidthCongestion(paths,now);
    load(0,26000);r->EvaluateBandwidthCongestion(paths,now);auto audit=lcJointQueueStats[1000];ok&=audit.paths==6 && audit.queueWithoutZero==3 && audit.blocked==3;
    lcBwCcAudit=false;lcRequireZeroBw=false;lcPathQueueMax=false;Simulator::Destroy();
    std::cout<<(ok?"ZERO_BANDWIDTH_QUEUE_SELF_CHECK_PASS":"ZERO_BANDWIDTH_QUEUE_SELF_CHECK_FAIL")<<" truth_table,projection_combinations="<<combinations<<",unknown_not_zero,missing_link,fresh_zero_proof,TTL,cache_probe_not_zero,legacy_switch,audit\n";return ok;
}


bool RdmaSmartFlowRouting::RunPathOnlyZeroQueueSelfChecks(){
    lcEnabled=true;lcBwCc=true;lcBwMode=2;lcMaxLinkQueue=true;lcPathOnlyQueue=true;lcPathQueueMax=true;
    lcRequireZeroBw=true;lcAlpsCongestion=false;lcAllocationOnly=false;lcDynamicSmall=false;lcSizeAware=false;
    lcTtlNs=100000;lcWindowNs=5000;lcQueueAlarmBytes=100000;lcBwCcAudit=false;lcBwAudit=false;lcQuotaAudit=false;
    auto src=CreateObject<RdmaSmartFlowRouting>();src->m_nodeId=1000;
    PathData a,b,c;a.pid=1;b.pid=2;c.pid=3;a.nodeIdSequence={1000,1,9,99};b.nodeIdSequence={1000,2,9,99};c.nodeIdSequence={1000,3,99};
    a.portSequence=b.portSequence={1,1,1};c.portSequence={1,1};
    std::map<uint32_t,PathData> pit={{1,a},{2,b},{3,c}};src->set_PIT(pit);
    std::vector<PathData*> paths={src->lookup_PIT(1),src->lookup_PIT(2),src->lookup_PIT(3)};
    auto load=[&](uint32_t node,uint16_t q){auto &v=src->m_linkStateTable[MakeLinkId(node,1)];v.availableQ=q;v.sampleEpoch=0;};
    auto reset=[&](){src->m_linkStateTable.clear();src->m_earlyProbes.clear();for(auto node:{1,2,3,9})load(node,500);
        for(auto p:paths){p->maxEgressQueueBytes=100000;p->queueFeedbackBornNs=0;p->latency=999999;}};
    bool ok=true;unsigned checks=0,combinations=0;auto ck=[&](bool v,const char *name){++checks;if(!v){ok=false;std::cout<<"PATH_ONLY_ZERO_CASE_FAIL "<<name<<'\n';}};
    reset();ck(src->EvaluateBandwidthCongestion(paths,0).blockedCount==0,"positive_capacity_high_queue_not_pruned");
    load(1,0);auto state=src->EvaluateBandwidthCongestion(paths,0);ck(state.blocked==std::vector<bool>({true,false,false}),"private_zero_and_100KB");
    paths[0]->maxEgressQueueBytes=99999;ck(!src->EvaluateBandwidthCongestion(paths,0).blocked[0],"zero_below_100KB_not_pruned");
    reset();load(9,0);paths[1]->maxEgressQueueBytes=99999;ck(src->EvaluateBandwidthCongestion(paths,0).blocked==std::vector<bool>({true,false,false}),"shared_zero_own_queue_only");
    paths[1]->maxEgressQueueBytes=100000;ck(src->EvaluateBandwidthCongestion(paths,0).blocked==std::vector<bool>({true,true,false}),"second_path_requires_own_queue");
    paths[0]->maxEgressQueueBytes=0;ck(!src->EvaluateBandwidthCongestion(paths,0).blocked[0],"own_low_queue_recovers_even_with_zero");
    reset();load(1,0xffff);auto unknown=src->EvaluateBandwidthCongestion(paths,0);ck(!unknown.blocked[0]&&!unknown.known[0],"unknown_not_zero");
    load(9,0);ck(src->EvaluateBandwidthCongestion(paths,0).blocked[0],"fresh_zero_proof_survives_other_unknown");
    reset();src->m_linkStateTable.erase(MakeLinkId(1,1));ck(!src->EvaluateBandwidthCongestion(paths,0).blocked[0],"missing_not_zero");
    reset();src->m_earlyProbes[1].lastBudgetQ=0;src->m_earlyProbes[1].pending=true;ck(!src->EvaluateBandwidthCongestion(paths,0).blocked[0],"cache_and_probe_not_zero_evidence");
    reset();load(1,1);ck(!src->EvaluateBandwidthCongestion(paths,0).blocked[0],"small_positive_not_zero");
    reset();load(1,0);for(auto p:paths)p->queueFeedbackBornNs=100001;ck(!src->EvaluateBandwidthCongestion(paths,100001).blocked[0],"expired_zero_not_evidence");
    reset();load(1,0);auto &v=src->m_linkStateTable[MakeLinkId(1,1)];v.sampleEpoch=100004/4;
    ck(!src->EvaluateBandwidthCongestion(paths,100004).blocked[0],"expired_queue_not_evidence");
    reset();for(uint16_t q1:{0,1,500})for(uint16_t q2:{0,1,500})for(uint16_t q3:{0,1,500})for(uint16_t qs:{0,1,500}){
        load(1,q1);load(2,q2);load(3,q3);load(9,qs);SharedQuotaResult grant;
        bool computed=src->ComputeSharedQuota(paths,{true,true,true},0,grant);ok&=computed;
        auto z=src->EvaluateBandwidthCongestion(paths,0);
        if(computed)for(size_t i=0;i<3;++i)ok&=z.blocked[i]==(grant.budgetQ[i]==0);++combinations;
    }
    reset();load(1,0);paths[0]->maxEgressQueueBytes=99999;lcZeroBwMinProbability=.05;
    std::vector<double>w={.3,.3,.4};src->ApplyBottleneckBandwidthWeights(paths,w);ck(w[0]>=.05-1e-9,"real_allocator_zero_below_threshold_floor");
    paths[0]->maxEgressQueueBytes=100000;w={.3,.3,.4};src->ApplyBottleneckBandwidthWeights(paths,w);ck(w[0]==0,"real_allocator_joint_prune_beats_floor");
    reset();lcRequireZeroBw=false;ck(src->EvaluateBandwidthCongestion(paths,0).blockedCount==3,"pure_queue_compatibility_switch");
    lcRequireZeroBw=false;lcZeroBwMinProbability=0;lcBwCc=false;lcMaxLinkQueue=false;lcPathOnlyQueue=false;lcPathQueueMax=false;Simulator::Destroy();
    std::cout<<(ok?"PATH_ONLY_ZERO_SELF_CHECK_PASS":"PATH_ONLY_ZERO_SELF_CHECK_FAIL")<<" assertions="<<checks<<",projection_combinations="<<combinations<<",truth_table,own_queue,unknown,missing,TTL,cache,probe,zero_floor,compatibility\n";return ok;
}

bool RdmaSmartFlowRouting::RunPathOnlyQueueSelfChecks(){
    lcEnabled=true;lcBwCc=true;lcMaxLinkQueue=true;lcPathOnlyQueue=true;lcAlpsCongestion=true;lcPathQueueMax=true;lcRequireZeroBw=false;lcBwCcAudit=false;lcEarlyAudit=false;lcEarlyProbe=true;
    lcQueueAlarmBytes=26200;lcWindowNs=5000;lcTtlNs=100000;
    auto src=CreateObject<RdmaSmartFlowRouting>();src->m_nodeId=1000;
    PathData a,b,c;a.pid=1;b.pid=2;c.pid=3;
    a.nodeIdSequence={1000,1,9,99};a.portSequence={1,1,1};
    b.nodeIdSequence={1000,2,9,99};b.portSequence={1,1,1};
    c.nodeIdSequence={1000,1,98};c.portSequence={1,1};
    a.latency=b.latency=c.latency=10000;
    std::map<uint32_t,PathData> pit={{1,a},{2,b},{3,c}};src->set_PIT(pit);std::vector<PathData*> paths={src->lookup_PIT(1),src->lookup_PIT(2),src->lookup_PIT(3)};
    bool ok=true;unsigned checks=0;
    auto ck=[&](bool value,const char *name){++checks;if(!value){ok=false;std::cout<<"PATH_ONLY_QUEUE_CASE_FAIL "<<name<<'\n';}};
    auto send=[&](uint32_t pid,uint32_t maximum,uint64_t born,uint64_t now){
        LinkCapacityTag tag;tag.bornNs=born;tag.maxQueueBytes=maximum;auto &p=*src->lookup_PIT(pid);tag.maxQueueLinkId=MakeLinkId(p.nodeIdSequence[1],p.portSequence[1]);
        for(size_t j=1;j<p.portSequence.size();++j){LinkCapacityRecord v;v.linkId=MakeLinkId(p.nodeIdSequence[j],p.portSequence[j]);v.state.availableQ=500;v.state.sampleEpoch=uint16_t(born/lcObservationTickNs);tag.Append(v);}
        return src->AcceptMaxQueueFeedback(pid,tag,now);
    };
    send(1,30000,100,100);send(2,0,104,104);
    auto state=src->EvaluateBandwidthCongestion(paths,104);
    ck(state.blocked==std::vector<bool>({true,false,false}) && state.knownCount==2,"same_shared_link_no_linked_prune");
    src->AcceptMaxQueueEvidence(MakeLinkId(9,1),true,108,108);src->AcceptMaxQueueEvidence(MakeLinkId(1,1),true,108,108);
    ck(src->EvaluateBandwidthCongestion(paths,108).blocked==state.blocked,"LST_flags_do_not_mark_other_paths");
    send(2,0,112,112);ck(src->EvaluateBandwidthCongestion(paths,112).blocked[0],"other_path_low_does_not_restore_this_path");
    send(1,26199,116,116);ck(!src->EvaluateBandwidthCongestion(paths,116).blocked[0],"own_low_restores");
    send(1,30000,100,120);ck(!src->EvaluateBandwidthCongestion(paths,120).blocked[0],"old_high_cannot_override_new_low");
    send(2,26200,124,124);ck(src->EvaluateBandwidthCongestion(paths,124).blocked==std::vector<bool>({false,true,false}),"strict_boundary_at_26200");
    bool alarm=false;ck(src->ReadPathQueueCongestion(*paths[1],100124,alarm) && alarm,"TTL_boundary_valid");
    ck(!src->ReadPathQueueCongestion(*paths[1],100125,alarm),"expired_is_unknown");
    LinkCapacityTag bad;bad.bornNs=128;bad.maxQueueBytes=50000;bad.maxQueueLinkId=MakeLinkId(9,1);
    ck(!src->AcceptMaxQueueFeedback(1,bad,128) && !src->EvaluateBandwidthCongestion(paths,128).blocked[0],"partial_feedback_not_a_path_update");
    send(1,30000,132,132);send(2,0,136,136);
    for(auto p:paths)for(size_t j=1;j<p->portSequence.size();++j){auto &v=src->m_linkStateTable[MakeLinkId(p->nodeIdSequence[j],p->portSequence[j])];v.availableQ=500;v.sampleEpoch=uint16_t(140/lcObservationTickNs);}
    ck(src->SelectEarlyProbe(paths,{1,1,1},140)==paths[0],"own_pruned_path_probed_after_shared_low");
    auto &pb=src->m_earlyProbes[2];pb.pending=true;pb.sentNs=140;auto &pc=src->m_earlyProbes[3];pc.pending=true;pc.sentNs=140;
    ck(src->SelectEarlyProbe(paths,{1,1,1},144)==paths[0],"other_inflight_probes_cannot_cover_own_queue");
    src->m_earlyProbes[1].pending=true;src->m_earlyProbes[1].sentNs=144;
    ck(src->SelectEarlyProbe(paths,{1,1,1},148)==nullptr,"same_path_inflight_deduplicated");
    src->m_earlyProbes.clear();lcPathOnlyQueue=false;
    src->AcceptMaxQueueEvidence(MakeLinkId(9,1),true,152,152);
    ck(src->EvaluateBandwidthCongestion(paths,152).blocked[1],"legacy_shared_mode_switch");
    lcPathOnlyQueue=true;src->AcceptMaxQueueEvidence(MakeLinkId(9,1),false,156,156);
    ck(src->EvaluateBandwidthCongestion(paths,156).blocked==std::vector<bool>({true,false,false}),"LST_recovery_cannot_clear_own_high");
    lcPathOnlyQueue=false;lcMaxLinkQueue=false;lcAlpsCongestion=false;lcPathQueueMax=false;lcRequireZeroBw=false;lcEarlyProbe=false;Simulator::Destroy();
    std::cout<<(ok?"PATH_ONLY_QUEUE_SELF_CHECK_PASS":"PATH_ONLY_QUEUE_SELF_CHECK_FAIL")<<" assertions="<<checks<<",own_feedback_only,no_shared_mark_or_recovery,threshold,TTL,partial_old_reject,probe_scope,dedup,legacy_mode\n";return ok;
}

bool RdmaSmartFlowRouting::RunMaxLinkQueueSelfChecks(){
    lcEnabled=true;lcBwCc=true;lcMaxLinkQueue=true;lcAlpsCongestion=true;lcPathQueueMax=true;lcRequireZeroBw=true;lcBwCcAudit=false;
    lcQueueAlarmBytes=26200;lcWindowNs=5000;lcTtlNs=100000;lcStopNs=400000;lcEarlyProbe=false;lcEarlyAudit=false;
    auto host=CreateObject<Node>();auto a=CreateObject<SwitchNode>(),b=CreateObject<SwitchNode>(),c=CreateObject<SwitchNode>();auto src=CreateObject<RdmaSmartFlowRouting>();src->SetNode(host);
    PathData p,q,t;p.pid=1;q.pid=2;t.pid=3;
    p.nodeIdSequence={host->GetId(),a->GetId(),b->GetId(),999};p.portSequence={1,1,1};
    q.nodeIdSequence={host->GetId(),c->GetId(),b->GetId(),999};q.portSequence={1,1,1};
    t.nodeIdSequence={host->GetId(),a->GetId(),999};t.portSequence={1,2};
    std::map<uint32_t,PathData> pit={{1,p},{2,q},{3,t}};src->set_PIT(pit);std::vector<PathData*> paths={src->lookup_PIT(1),src->lookup_PIT(2),src->lookup_PIT(3)};
    std::map<uint32_t,Ptr<SwitchNode>> nodes={{a->GetId(),a},{b->GetId(),b},{c->GetId(),c}};
    uint32_t A=MakeLinkId(a->GetId(),1),B=MakeLinkId(b->GetId(),1),C=MakeLinkId(c->GetId(),1),A2=MakeLinkId(a->GetId(),2);
    bool ok=true;unsigned assertions=0;Ptr<Packet> oldLow;uint16_t savedEpoch=0;
    auto ck=[&](bool value,const char *what){++assertions;if(!value){ok=false;std::cout<<"MAX_LINK_QUEUE_CHECK_FAIL "<<Simulator::Now().GetNanoSeconds()<<' '<<what<<'\n';}};
    auto mask=[&](std::vector<bool> expected){return src->EvaluateBandwidthCongestion(paths,Simulator::Now().GetNanoSeconds()).blocked==expected;};
    using V=std::map<uint32_t,std::pair<uint32_t,uint64_t>>;
    auto ack=[&](uint32_t pid,const V &values){
        auto &path=pit.at(pid);auto packet=Create<Packet>(32);AckPathTag at;at.SetPathId(pid);packet->AddPacketTag(at);CustomHeader ch;ch.l3Prot=0xFD;
        for(size_t j=path.portSequence.size();j-->1;){auto sw=nodes.at(path.nodeIdSequence[j]);uint32_t port=path.portSequence[j],link=MakeLinkId(sw->GetId(),port);
            auto v=values.find(link);uint32_t bytes=v==values.end()?0:v->second.first;uint64_t rate=v==values.end()?100000000000ULL:v->second.second;
            auto &sample=sw->m_linkCapacitySamples[port];sample.baseline=true;sample.previousNs=Simulator::Now().GetNanoSeconds();sample.latest.linkId=link;sample.latest.state.availableQ=500;
            sw->m_outPort2BitRateMap[port]=rate;for(unsigned k=0;k<8;++k)sw->m_mmu->egress_bytes[port][k]=0;
            sw->m_mmu->egress_bytes[port][3]=bytes;sw->AppendLinkCapacityTelemetry(port,packet,ch);
        }return packet;
    };
    std::vector<std::pair<uint64_t,std::function<void()>>> events;
    events.push_back({100,[&](){auto packet=ack(1,{{A,{40000,200000000000ULL}},{B,{15000,50000000000ULL}}});LinkCapacityTag tag;packet->PeekPacketTag(tag);
        ck(tag.maxQueueBytes==30000 && tag.maxQueueLinkId==B,"heterogeneous_argmax");auto copy=packet->Copy();LinkCapacityTag rt;copy->PeekPacketTag(rt);ck(rt.maxQueueLinkId==B && rt.maxQueueBytes==30000 && rt.GetSerializedSize()==18+8*rt.count,"tag_roundtrip");
        src->ReceiveLinkCapacityFeedback(packet);ck(mask({true,true,false}),"shared_link_masks_all_users");}});
    events.push_back({200,[&](){oldLow=ack(1,{{A,{26199,100000000000ULL}}});src->ReceiveLinkCapacityFeedback(oldLow);ck(mask({false,false,false}),"below_boundary_recovers");}});
    events.push_back({300,[&](){src->ReceiveLinkCapacityFeedback(ack(1,{{A,{26200,100000000000ULL}}}));ck(mask({true,false,false}),"at_boundary_private_only_and_port_identity");}});
    events.push_back({400,[&](){src->ReceiveLinkCapacityFeedback(ack(2,{}));ck(mask({true,false,false}),"unrelated_feedback_cannot_clear_private_link");}});
    events.push_back({500,[&](){src->ReceiveLinkCapacityFeedback(ack(1,{{B,{30000,100000000000ULL}}}));bool high=false;ck(src->ReadLinkCongestion(A,500,high) && high,"new_max_does_not_clear_old_alarm");ck(mask({true,true,false}),"shared_alarm");savedEpoch=src->m_linkStateTable[B].queueEpoch;}});
    events.push_back({600,[&](){src->ReceiveLinkCapacityFeedback(ack(2,{{C,{40000,100000000000ULL}}}));ck(src->m_linkStateTable[B].queueEpoch==savedEpoch,"nonwinner_capacity_does_not_refresh_queue");ck(mask({true,true,false}),"nonwinner_is_not_zero");}});
    events.push_back({700,[&](){src->ReceiveLinkCapacityFeedback(oldLow);ck(mask({true,true,false}),"old_low_cannot_clear_new_alarm");}});
    events.push_back({800,[&](){src->ReceiveLinkCapacityFeedback(ack(1,{}));ck(mask({false,true,false}),"low_max_clears_only_observed_links");}});
    events.push_back({900,[&](){src->ReceiveLinkCapacityFeedback(ack(2,{}));ck(mask({false,false,false}),"low_max_bound_clears_shared_link");}});
    events.push_back({1000,[&](){auto packet=ack(1,{{B,{30000,100000000000ULL}}});LinkCapacityTag tag;packet->RemovePacketTag(tag);--tag.count;packet->AddPacketTag(tag);src->ReceiveLinkCapacityFeedback(packet);ck(mask({false,false,false}),"partial_feedback_rejected");}});
    events.push_back({1100,[&](){auto packet=ack(1,{{B,{30000,100000000000ULL}}});LinkCapacityTag tag;packet->RemovePacketTag(tag);tag.maxQueueLinkId=MakeLinkId(999,1);packet->AddPacketTag(tag);src->ReceiveLinkCapacityFeedback(packet);ck(mask({false,false,false}),"off_path_argmax_rejected");}});
    events.push_back({1200,[&](){ck(ScaleQueueBytes(13100,50000000000ULL)==26200 && ScaleQueueBytes(52400,200000000000ULL)==26200,"equivalent_boundary_rates");
        ck(ScaleQueueBytes(1,0)==UINT32_MAX && ScaleQueueBytes(UINT32_MAX,1)==UINT32_MAX-1,"invalid_and_saturated_scale");}});
    events.push_back({1300,[&](){auto packet=ack(1,{});LinkCapacityTag tag;packet->RemovePacketTag(tag);for(unsigned i=0;i<tag.count;++i)tag.records[i].state.availableQ=0;packet->AddPacketTag(tag);src->ReceiveLinkCapacityFeedback(packet);ck(mask({false,false,false}),"zero_capacity_not_a_queue_alarm");}});
    events.push_back({1400,[&](){auto packet=ack(1,{{B,{30000,100000000000ULL}}});LinkCapacityTag tag;packet->RemovePacketTag(tag);for(unsigned i=0;i<tag.count;++i)if(tag.records[i].linkId==B)tag.records[i].state.availableQ=0xffff;packet->AddPacketTag(tag);src->ReceiveLinkCapacityFeedback(packet);
        uint16_t cap=0;ck(!src->ReadLinkCapacity(B,1400,cap) && mask({true,true,false}),"queue_alarm_independent_of_capacity_validity");savedEpoch=src->m_linkStateTable[B].queueEpoch;}});
    events.push_back({1500,[&](){LinkCapacityRecord v;v.linkId=B;v.state.availableQ=1000;v.state.sampleEpoch=uint16_t(1500/lcObservationTickNs);src->AcceptLinkCapacity(v,1,1500);ck(src->m_linkStateTable[B].queueEpoch==savedEpoch && mask({true,true,false}),"capacity_only_preserves_queue_evidence_age");}});
    events.push_back({1600,[&](){src->AcceptMaxQueueEvidence(A2,true,1600,1600);ck(!src->AcceptMaxQueueEvidence(A2,false,1601,1601),"same_tick_retains_alarm");}});
    events.push_back({1608,[&](){ck(src->AcceptMaxQueueEvidence(A2,false,1608,1608),"later_tick_recovers");}});
    events.push_back({102000,[&](){LinkCapacityRecord v;v.linkId=B;v.state.availableQ=1000;v.state.sampleEpoch=uint16_t(102000/lcObservationTickNs);src->AcceptLinkCapacity(v,1,102000);src->ExpireLinkCapacityEntries(102000);
        bool high=false;uint16_t cap=0;ck(!src->ReadLinkCongestion(B,102000,high) && src->ReadLinkCapacity(B,102000,cap) && cap==1000,"queue_expires_independently_of_fresh_capacity");}});
    events.push_back({120000,[&](){src->ReceiveLinkCapacityFeedback(oldLow);ck(mask({false,false,false}),"expired_complete_ack_rejected");}});
    events.push_back({262144,[&](){ck(src->AcceptMaxQueueEvidence(B,true,262136,262144),"queue_epoch_wrap_accept");bool high=false;uint64_t age=0;ck(QueueEvidenceAge(src->m_linkStateTable[B].queueEpoch,262144,age) && age==8 && src->ReadLinkCongestion(B,262144,high) && high,"queue_epoch_wrap_read");}});
    events.push_back({262148,[&](){ck(src->AcceptMaxQueueEvidence(B,false,262148,262148),"queue_epoch_wrap_recovery");}});
    events.push_back({262152,[&](){ck(!src->AcceptMaxQueueEvidence(B,true,262136,262152),"older_alarm_cannot_override_recovery");}});
    events.push_back({370000,[&](){src->ExpireLinkCapacityEntries(370000);bool high=false;ck(!src->ReadLinkCongestion(B,370000,high),"aging_prevents_modulo_resurrection");}});
    auto invoke=+[](std::function<void()> *f){(*f)();};for(auto &e:events)Simulator::Schedule(NanoSeconds(e.first),invoke,&e.second);
    Simulator::Run();Simulator::Destroy();lcMaxLinkQueue=false;lcAlpsCongestion=false;lcPathQueueMax=false;lcRequireZeroBw=false;
    std::cout<<(ok?"MAX_LINK_QUEUE_SELF_CHECK_PASS":"MAX_LINK_QUEUE_SELF_CHECK_FAIL")<<" events="<<events.size()<<",assertions="<<assertions<<",heterogeneous_argmax,shared_private,port_identity,recovery,TTL,epoch_wrap,no_resurrection,unknown_capacity\n";return ok;
}

bool RdmaSmartFlowRouting::RunAlpsCongestionSelfChecks(){
    lcEnabled=true;lcBwCc=true;lcAlpsCongestion=true;lcBwCcAudit=false;lcAllocationOnly=false;lcPathQueueMax=true;lcRequireZeroBw=true;lcQueueAlarmBytes=200000;
    auto r=CreateObject<RdmaSmartFlowRouting>();r->m_nodeId=1000;
    PathData a,b,c;a.pid=1;b.pid=2;c.pid=3;
    a.nodeIdSequence={1000,1,99};b.nodeIdSequence={1000,2,99};c.nodeIdSequence={1000,3,99};a.portSequence=b.portSequence=c.portSequence={1,1};
    a.theoreticalSmallestLatencyInNs=10000;b.theoreticalSmallestLatencyInNs=20000;c.theoreticalSmallestLatencyInNs=30000;
    a.latency=10000;b.latency=20001;c.latency=29999;std::vector<PathData*> paths={&a,&b,&c};
    bool ok=true;auto first=r->EvaluateBandwidthCongestion(paths,0);ok&=first.blocked==std::vector<bool>({false,true,false}) && first.knownCount==3;
    for(auto p:paths){p->maxEgressQueueBytes=5000000;p->queueFeedbackBornNs=0;auto &v=r->m_linkStateTable[MakeLinkId(p->nodeIdSequence[1],1)];v.availableQ=0x8000;}
    ok&=r->EvaluateBandwidthCongestion(paths,0).blocked==first.blocked;
    r->m_linkStateTable.clear();ok&=r->EvaluateBandwidthCongestion(paths,1000000).blocked==first.blocked; // Original PIT delay is not given a new LST freshness rule.
    a.latency=10001;c.latency=30001;ok&=r->EvaluateBandwidthCongestion(paths,0).blockedCount==3;
    a.latency=10000;b.latency=20000;c.latency=30000;ok&=r->EvaluateBandwidthCongestion(paths,0).blockedCount==0;
    unsigned comparisons=0;for(int i=-1;i<=1;++i)for(int j=-1;j<=1;++j)for(int k=-1;k<=1;++k){
        a.latency=10000+i;b.latency=20000+j;c.latency=30000+k;auto s=r->EvaluateBandwidthCongestion(paths,0);unsigned original=0;
        for(auto p:paths)original+=p->latency>p->theoreticalSmallestLatencyInNs;ok&=s.blockedCount==original;++comparisons;
    }
    c.theoreticalSmallestLatencyInNs=0;auto unknown=r->EvaluateBandwidthCongestion(paths,0);ok&=!unknown.blocked[2] && !unknown.known[2] && unknown.knownCount==2;
    lcAlpsCongestion=false;lcRequireZeroBw=false;for(auto p:paths){p->maxEgressQueueBytes=200000;p->queueFeedbackBornNs=0;}ok&=r->EvaluateBandwidthCongestion(paths,0).blockedCount==3;
    lcAlpsCongestion=true;lcBwCcAudit=true;lcAlpsCongestionStats.clear();a.latency=10000;b.latency=20001;c.theoreticalSmallestLatencyInNs=30000;c.latency=29999;r->EvaluateBandwidthCongestion(paths,0);auto stats=lcAlpsCongestionStats[1000];ok&=stats.paths==3 && stats.blocked==1 && stats.known==3;
    lcBwCcAudit=false;lcAlpsCongestion=false;lcRequireZeroBw=false;lcPathQueueMax=false;Simulator::Destroy();
    std::cout<<(ok?"ALPS_CONGESTION_SELF_CHECK_PASS":"ALPS_CONGESTION_SELF_CHECK_FAIL")<<" original_comparison_vectors="<<comparisons<<",strict_boundary,independent_bandwidth_queue,legacy_mode,reference_required,original_PIT_lifetime,audit\n";return ok;
}

bool RdmaSmartFlowRouting::RunQueuePercentSelfChecks() {
    bool ok=true;lcEnabled=true;lcBwCc=true;lcTtlNs=100000;
    auto r=CreateObject<RdmaSmartFlowRouting>();r->m_nodeId=1000;
    PathData p;p.pid=1;p.nodeIdSequence={1000,1,99};p.portSequence={1,1};
    r->m_nexthopSelTbl[1]=p;std::vector<PathData*> paths={&p};
    for(uint32_t percent:{50u,90u}) {
        uint32_t boundary=1600000*percent/100;
        uint16_t low=CompactLinkState::QueuePercentWord(500,boundary-1,1600000,percent);
        uint16_t high=CompactLinkState::QueuePercentWord(500,boundary,1600000,percent);
        uint16_t invalid=CompactLinkState::QueuePercentWord(0xffff,boundary,1600000,percent);
        ok&=low==500 && high==uint16_t(0x8000|500) && invalid==0xfffe;
        ok&=CompactLinkState::QueuePercentWord(0,boundary-1,1600000,percent)==0;
        ok&=CompactLinkState::QueuePercentWord(500,100,0,percent)==500;
        auto &v=r->m_linkStateTable[MakeLinkId(1,1)];v.sampleEpoch=0;v.availableQ=low;
        ok&=r->EvaluateBandwidthCongestion(paths,0).blockedCount==0;
        v.availableQ=high;ok&=r->EvaluateBandwidthCongestion(paths,0).blockedCount==1;
        v.availableQ=invalid;ok&=r->EvaluateBandwidthCongestion(paths,0).blockedCount==1;
        std::cout<<"QUEUE_PERCENT_BOUNDARY percent="<<percent<<" bytes="<<boundary<<" before="<<low<<" at="<<high<<'\n';
    }
    ok&=sizeof(CompactLinkState)==4;
    std::cout<<(ok?"QUEUE_PERCENT_SELF_CHECK_PASS":"QUEUE_PERCENT_SELF_CHECK_FAIL")
        <<" boundary50,boundary90,positive_capacity_alarm,unknown_capacity_alarm,zero_below_threshold,endpoint_pruning LST_bytes="<<sizeof(CompactLinkState)<<'\n';
    lcEnabled=false;lcBwCc=false;return ok;
}

bool RdmaSmartFlowRouting::lcBwCc=false,RdmaSmartFlowRouting::lcBwCcAudit=false,RdmaSmartFlowRouting::lcBwCcUniformFallback=false;
std::ofstream RdmaSmartFlowRouting::lcBwCcLog;
void RdmaSmartFlowRouting::ConfigureBandwidthCc(const std::string &directory) {
    NS_ABORT_MSG_IF(lcBwCc && (!lcEnabled || lcBwMode!=2 || !lcEarlyProbe),
                    "Bandwidth CC requires telemetry, shared-quota mode 2, and recovery probes");
    if(!lcBwCcAudit)return;
    if(lcMaxLinkQueue){
        if(lcPathOnlyQueue){lcPathOnlyQueueLog.open(directory+"/path-only-queue-paths.csv");lcPathOnlyQueueLog<<"now_ns,node,path,max_scaled_bytes,observation_start_ns,known,blocked";
            if(lcRequireZeroBw)lcPathOnlyQueueLog<<",queue_known,capacity_complete,fresh_zero,zero_link,min_fresh_capacity_q";
            lcPathOnlyQueueLog<<'\n';NS_ABORT_MSG_IF(!lcPathOnlyQueueLog,"Cannot open path-only audit");}
        lcMaxLinkQueueHopLog.open(directory+"/maxlink-queue-hops.csv");lcMaxLinkQueueHopLog<<"now_ns,uid,path,link,raw_bytes,rate_bps,scaled_bytes,before_max,before_link,after_max,after_link\n";
        lcMaxLinkQueueFeedbackLog.open(directory+"/maxlink-queue-feedback.csv");lcMaxLinkQueueFeedbackLog<<"now_ns,node,path,born_ns,max_scaled_bytes,max_link,observation_ns,queue_alarm,valid,updated,hops\n";
        lcMaxLinkQueuePathLog.open(directory+"/maxlink-queue-paths.csv");lcMaxLinkQueuePathLog<<"now_ns,node,path,blocked,known,blocker,users_in_group\n";
        NS_ABORT_MSG_IF(!lcMaxLinkQueueHopLog || !lcMaxLinkQueueFeedbackLog || !lcMaxLinkQueuePathLog,"Cannot open max-link audit");
    }

    if(lcAlpsCongestion){lcAlpsCongestionLog.open(directory+"/alps-congestion-paths.csv");NS_ABORT_MSG_IF(!lcAlpsCongestionLog,"Cannot open ALPS congestion audit");lcAlpsCongestionLog<<"now_ns,node,path,latency_ns,reference_ns,generation_ns,congested\n";}
    if(lcPathQueueMax){lcPathQueueMaxLog.open(directory+"/path-queue-max.csv");NS_ABORT_MSG_IF(!lcPathQueueMaxLog,"Cannot open path queue maximum audit");lcPathQueueMaxLog<<"now_ns,node,path,max_queue_bytes,observation_start_ns,hops,queue_alarm\n";}
    lcBwCcLog.open(directory+"/bw-cc-events.csv");
    NS_ABORT_MSG_IF(!lcBwCcLog,"Cannot open bandwidth CC audit");
    lcBwCcLog<<"time_ns,node,flow,event,blocked_paths,total_paths,known_paths,old_rate_bps,new_rate_bps\n";
}

void RdmaSmartFlowRouting::ApplyProtocolUnavailable(const std::vector<PathData*> &paths,BandwidthCongestionResult &state) const {
    if(!lcPathTimeout)return;
    for(size_t i=0;i<paths.size();++i){auto it=m_earlyProbes.find(paths[i]->pid);
        if(it!=m_earlyProbes.end() && it->second.unavailable){
            if(!state.blocked[i]){state.blocked[i]=true;++state.blockedCount;}
            if(!state.known[i]){state.known[i]=true;++state.knownCount;}
        }
    }
}
RdmaSmartFlowRouting::BandwidthCongestionResult RdmaSmartFlowRouting::EvaluateBandwidthCongestion(
        const std::vector<PathData*> &paths,uint64_t now) const {
    BandwidthCongestionResult r;size_t n=paths.size();
    r.blocked.assign(n,false);r.known.assign(n,false);r.blocker.assign(n,0);
    r.privateQ.assign(n,-1);std::map<uint32_t,std::set<size_t>> users;
    if(lcBwCc && lcMaxLinkQueue && !lcAllocationOnly){
        uint32_t node=m_node?m_node->GetId():m_nodeId;auto &audit=lcMaxLinkQueueStats[node];bool sample=false;
        if(lcBwCcAudit){uint64_t bucket=now/20000;if(audit.lastSample!=bucket){audit.lastSample=bucket;sample=true;}}
        if(lcPathOnlyQueue){
            for(size_t i=0;i<n;++i){bool alarm=false;const auto &p=*paths[i];
                const bool queueKnown=ReadPathQueueCongestion(p,now,alarm);
                bool complete=true,hasFreshZero=false;uint32_t zeroLink=0;uint16_t minimum=0xffff;
                if(lcRequireZeroBw){
                    NS_ABORT_MSG_IF(p.nodeIdSequence.size()!=p.portSequence.size()+1 || p.portSequence.size()<2,"Invalid zero/queue path");
                    for(size_t j=1;j<p.portSequence.size();++j){uint32_t link=MakeLinkId(p.nodeIdSequence[j],p.portSequence[j]);uint16_t q;
                        bool fresh=ReadLinkCapacity(link,now,q);complete&=fresh;
                        if(fresh){minimum=std::min(minimum,q);if(q==0){hasFreshZero=true;if(!zeroLink)zeroLink=link;}}
                    }
                }
                // No reservations: a fresh zero resource forces this path's shared quota to zero.
                // Missing/expired capacity, cached budgets and probe discounts are not zero evidence.
                r.blocked[i]=queueKnown && alarm && (!lcRequireZeroBw || hasFreshZero);
                r.known[i]=queueKnown && (!lcRequireZeroBw || complete || hasFreshZero);
                r.knownCount+=r.known[i];r.blockedCount+=r.blocked[i];r.blocker[i]=r.blocked[i]?zeroLink:0;
                if(lcBwCcAudit){++audit.paths;audit.blocked+=r.blocked[i];audit.unknown+=!r.known[i];
                    if(lcRequireZeroBw){auto &a=lcJointQueueStats[node];++a.paths;a.queueHigh+=queueKnown && alarm;
                        a.observedZero+=hasFreshZero;a.blocked+=r.blocked[i];a.queueWithoutZero+=queueKnown && alarm && !hasFreshZero;
                        a.zeroWithoutQueue+=hasFreshZero && !(queueKnown && alarm);a.incompleteCapacity+=!complete;a.missingQueue+=!queueKnown;
                    }
                }
                if(sample && lcPathOnlyQueueLog){lcPathOnlyQueueLog<<now<<','<<node<<','<<p.pid<<','<<p.maxEgressQueueBytes<<','<<p.queueFeedbackBornNs<<','<<r.known[i]<<','<<r.blocked[i];
                    if(lcRequireZeroBw)lcPathOnlyQueueLog<<','<<queueKnown<<','<<complete<<','<<hasFreshZero<<','<<zeroLink<<','<<minimum;
                    lcPathOnlyQueueLog<<'\n';
                }
            }ApplyProtocolUnavailable(paths,r);return r;
        }
        for(size_t i=0;i<n;++i)for(size_t j=1;j<paths[i]->portSequence.size();++j)users[MakeLinkId(paths[i]->nodeIdSequence[j],paths[i]->portSequence[j])].insert(i);
        for(size_t i=0;i<n;++i){bool complete=true;
            for(size_t j=1;j<paths[i]->portSequence.size();++j){uint32_t link=MakeLinkId(paths[i]->nodeIdSequence[j],paths[i]->portSequence[j]);bool alarm=false;
                bool fresh=ReadLinkCongestion(link,now,alarm);complete&=fresh;
                if(fresh && alarm){r.blocked[i]=true;if(!r.blocker[i])r.blocker[i]=link;}
            }
            r.known[i]=r.blocked[i] || complete;r.knownCount+=r.known[i];r.blockedCount+=r.blocked[i];
            size_t use=r.blocker[i]?users[r.blocker[i]].size():0;
            if(lcBwCcAudit){++audit.paths;audit.blocked+=r.blocked[i];audit.sharedBlocked+=r.blocked[i] && use>1;audit.unknown+=!r.known[i];}
            if(sample && lcMaxLinkQueuePathLog)lcMaxLinkQueuePathLog<<now<<','<<node<<','<<paths[i]->pid<<','<<r.blocked[i]<<','<<r.known[i]<<','<<r.blocker[i]<<','<<use<<'\n';
        }ApplyProtocolUnavailable(paths,r);return r;
    }
    if(lcBwCc && lcAlpsCongestion && !lcAllocationOnly){
        uint32_t node=m_node?m_node->GetId():m_nodeId;bool sample=false;
        if(lcBwCcAudit){auto &a=lcAlpsCongestionStats[node];++a.calls;uint64_t bucket=now/20000;if(a.lastSample!=bucket){a.lastSample=bucket;sample=true;}}
        for(size_t i=0;i<n;++i){const auto &p=*paths[i];
            // Exact comparison used by original UpdateRateForLapsPlus.
            // Its PIT delay/probe maintenance remains unchanged; no LST TTL or zero-capacity gate.
            r.known[i]=p.theoreticalSmallestLatencyInNs>0;
            r.blocked[i]=r.known[i] && p.latency>p.theoreticalSmallestLatencyInNs;
            r.knownCount+=r.known[i];r.blockedCount+=r.blocked[i];
            if(lcBwCcAudit){auto &a=lcAlpsCongestionStats[node];++a.paths;a.known+=r.known[i];a.blocked+=r.blocked[i];a.unknown+=!r.known[i];}
            if(sample && lcAlpsCongestionLog)lcAlpsCongestionLog<<now<<','<<node<<','<<p.pid<<','<<p.latency<<','<<p.theoreticalSmallestLatencyInNs<<','<<p.tsGeneration.GetNanoSeconds()<<','<<r.blocked[i]<<'\n';
        }
        ApplyProtocolUnavailable(paths,r);return r;
    }

    for(size_t i=0;i<n;++i) {
        const auto &p=*paths[i];
        NS_ABORT_MSG_IF(p.nodeIdSequence.size()!=p.portSequence.size()+1 || p.portSequence.size()<2,"Invalid bandwidth CC path");
        for(size_t j=1;j<p.portSequence.size();++j)
            users[MakeLinkId(p.nodeIdSequence[j],p.portSequence[j])].insert(i);
    }
    for(size_t i=0;i<n;++i) {
        const auto &p=*paths[i];bool complete=true,privateComplete=true,hasPrivate=false,hasFreshZero=false;
        double privateMin=65535;
        for(size_t j=1;j<p.portSequence.size();++j) {
            uint32_t link=MakeLinkId(p.nodeIdSequence[j],p.portSequence[j]);uint16_t q;
            bool fresh=ReadLinkCapacity(link,now,q);complete&=fresh;
            hasFreshZero|=fresh && q==0; // Unknown/expired capacity is never a measured zero.
            bool exclusive=users.at(link).size()==1;
            if(exclusive){hasPrivate=true;privateComplete&=fresh;if(fresh)privateMin=std::min(privateMin,double(q));}
            // QUEUE90_PROBE20_V1: queue alarm is independent of a zero/positive/unknown bandwidth estimate.
            bool queueAlarm=false;bool queueFresh=ReadLinkCongestion(link,now,queueAlarm);
            if(!lcPathQueueMax && queueFresh && queueAlarm && !lcAllocationOnly) {
                // The uncapped allocation of this ZERO shared resource is 0;
                // never compare the already-min-capped final path grant to B_i.
                // Even an unknown B_i cannot be negative. Equality is a co-bottleneck.
                r.blocked[i]=true;if(!r.blocker[i])r.blocker[i]=link;
            }
        }
        bool pathQueueFresh=true;
        if(lcPathQueueMax && !lcAllocationOnly) {
            bool alarm=false;pathQueueFresh=ReadPathQueueCongestion(p,now,alarm);
            // Positive link capacities give strictly positive progressive-filling grants.
            // Thus, with endpoint reservations disabled, a zero projected bottleneck
            // is equivalent to a fresh zero on at least one traversed switch link.
            r.blocked[i]=pathQueueFresh && alarm && (!lcRequireZeroBw || hasFreshZero);
            if(lcRequireZeroBw && lcBwCcAudit){auto &a=lcJointQueueStats[m_node?m_node->GetId():m_nodeId];
                ++a.paths;a.queueHigh+=pathQueueFresh && alarm;a.observedZero+=hasFreshZero;a.blocked+=r.blocked[i];
                a.queueWithoutZero+=pathQueueFresh && alarm && !hasFreshZero;
                a.zeroWithoutQueue+=hasFreshZero && !(pathQueueFresh && alarm);
                a.incompleteCapacity+=!complete;a.missingQueue+=!pathQueueFresh;
            } // Never merge other ACKs' queue flags or cached/probe-discounted budgets here.
        }
        if(hasPrivate && privateComplete)r.privateQ[i]=privateMin;
        r.known[i]=r.blocked[i] || (complete && pathQueueFresh);
        r.blockedCount+=r.blocked[i];r.knownCount+=r.known[i];
    }
    ApplyProtocolUnavailable(paths,r);return r;
}

// NO_QUEUE_PRUNING_V1: keep queue feedback for rate control; alter only selection eligibility.
bool RdmaSmartFlowRouting::lcDisablePruning=false;
std::ofstream RdmaSmartFlowRouting::lcNoPruneDataLog;
RdmaSmartFlowRouting::BandwidthCongestionResult RdmaSmartFlowRouting::EvaluateBandwidthSelection(
        const std::vector<PathData*> &paths,uint64_t now) const {
    auto state=EvaluateBandwidthCongestion(paths,now);
    if(lcDisablePruning){
        std::fill(state.blocked.begin(),state.blocked.end(),false);
        std::fill(state.blocker.begin(),state.blocker.end(),0);state.blockedCount=0;
        ApplyProtocolUnavailable(paths,state); // Genuine ACK timeout safety is not queue pruning.
    }
    return state;
}
bool RdmaSmartFlowRouting::RunNoQueuePruningSelfChecks(){
    lcEnabled=true;lcBwCc=true;lcBwMode=2;lcTtlNs=100000;lcAllocationOnly=false;
    lcMaxLinkQueue=false;lcPathQueueMax=false;lcAlpsCongestion=false;lcPathTimeout=false;lcDisablePruning=false;
    lcBwAudit=false;lcQuotaAudit=false;lcEarlyProbe=false;lcSendPenalty=false;lcZeroBwMinProbability=.05;
    auto routing=CreateObject<RdmaSmartFlowRouting>();routing->m_nodeId=1000;
    PathData a,b,c;a.pid=1;b.pid=2;c.pid=3;
    a.nodeIdSequence={1000,1,9,99};a.portSequence={1,1,1};
    b.nodeIdSequence={1000,2,9,99};b.portSequence={2,1,1};
    c.nodeIdSequence={1000,3,8,99};c.portSequence={3,1,1};
    std::map<uint32_t,PathData> pit={{1,a},{2,b},{3,c}};routing->set_PIT(pit);
    std::vector<PathData*> paths={routing->lookup_PIT(1),routing->lookup_PIT(2),routing->lookup_PIT(3)};
    for(auto path:paths)for(size_t j=1;j<path->portSequence.size();++j)
        routing->m_linkStateTable[MakeLinkId(path->nodeIdSequence[j],path->portSequence[j])].availableQ=1000;
    uint32_t count=0;bool ok=true;auto ck=[&](bool v,const char *name){++count;if(!v){ok=false;std::cerr<<"NO_QUEUE_PRUNING_FAIL "<<name<<std::endl;}};
    auto shared=MakeLinkId(9,1);routing->m_linkStateTable[shared].availableQ=0x8000;
    auto raw=routing->EvaluateBandwidthCongestion(paths,0),selected=routing->EvaluateBandwidthSelection(paths,0);
    ck(raw.blockedCount==2 && selected.blocked==raw.blocked,"default_preserves_shared_pruning");
    lcDisablePruning=true;selected=routing->EvaluateBandwidthSelection(paths,0);
    ck(selected.blockedCount==0,"queue_pruning_removed");
    ck(routing->EvaluateBandwidthCongestion(paths,0).blockedCount==2,"raw_cc_signal_preserved");
    std::vector<double> w={.2,.3,.5};ck(!routing->ApplyLinkCongestionPruning(paths,w) && w==std::vector<double>({.2,.3,.5}),"first_mask_removed");
    routing->ApplyPathLocalBandwidthWeights(paths,w);
    ck(w[0]>0 && w[1]>0 && w[2]>0 && std::abs(w[0]+w[1]+w[2]-1)<1e-9,"allocator_retains_alarm_paths_and_floor");
    routing->m_linkStateTable[MakeLinkId(3,1)].availableQ=0x8000;
    ck(routing->EvaluateBandwidthCongestion(paths,0).blockedCount==3,"all_congested_still_requests_rate_decrease");
    ck(routing->EvaluateBandwidthSelection(paths,0).blockedCount==0,"no_queue_all_path_gate");
    lcPathTimeout=true;routing->m_earlyProbes[2].unavailable=true;
    selected=routing->EvaluateBandwidthSelection(paths,0);
    ck(selected.blocked==std::vector<bool>({false,true,false}),"protocol_timeout_remains_unavailable");
    routing->m_earlyProbes[2].unavailable=false;
    ck(routing->EvaluateBandwidthSelection(paths,0).blockedCount==0,"protocol_recovery_releases_path");
    lcDisablePruning=false;
    ck(routing->EvaluateBandwidthSelection(paths,0).blockedCount==3,"switch_off_restores_all_queue_masks");
    lcPathTimeout=false;lcBwCc=false;lcBwMode=0;Simulator::Destroy();
    std::cout<<(ok?"NO_QUEUE_PRUNING_SELF_CHECK_PASS":"NO_QUEUE_PRUNING_SELF_CHECK_FAIL")<<" assertions="<<count<<std::endl;return ok;
}

// FEEDBACK_RESUME_V1: reuse the existing probe cooldown and timeout, not lcWindowNs polling.
// SIZE_AWARE_V1: configurable endpoint budgets; switch service discipline is unchanged.
bool RdmaSmartFlowRouting::lcSizeAware=false,RdmaSmartFlowRouting::lcSizeReserve=false;
bool RdmaSmartFlowRouting::lcDynamicSmall=false;
uint64_t RdmaSmartFlowRouting::lcSmallTargetNs=50000;
std::ofstream RdmaSmartFlowRouting::lcDynamicSmallLog;
uint64_t RdmaSmartFlowRouting::PendingSmallBytes(Ptr<RdmaQueuePair> qp){
    if(!IsSmallTransfer(qp) || qp->IsFinishedConst())return 0;
    uint64_t sent=std::max(qp->snd_nxt,uint64_t(qp->m_irn.m_max_next_seq));
    uint64_t fresh=qp->m_size-std::min(qp->m_size,sent);
    return std::min(qp->m_size,fresh+uint64_t(qp->m_irn.m_sack.getLossyDataSize()));
}
uint64_t RdmaSmartFlowRouting::SmallDemandRate(uint64_t bytes){
    NS_ABORT_MSG_IF(!lcSmallTargetNs,"Small demand target must be positive");
    return uint64_t(std::ceil((long double)bytes*8*1000000000.0L/lcSmallTargetNs));
}
std::map<uint32_t,double> RdmaSmartFlowRouting::DynamicSmallLinkDemand() const{
    std::map<uint32_t,double> links;if(!lcDynamicSmall)return links;
    std::map<uint32_t,uint64_t> pathBytes;
    for(auto it=m_dynamicSmallFlows.begin();it!=m_dynamicSmallFlows.end();){
        auto qp=it->second;
        if(qp->IsFinishedConst()){it=m_dynamicSmallFlows.erase(it);continue;}
        uint64_t pending=PendingSmallBytes(qp);
        if(pending && qp->m_sizePinnedPath!=UINT32_MAX)pathBytes[qp->m_sizePinnedPath]+=pending;
        ++it;
    }
    for(const auto &v:pathBytes){
        auto p=m_nexthopSelTbl.find(v.first);if(p==m_nexthopSelTbl.end())continue;
        std::set<uint32_t> unique;
        for(size_t j=1;j<p->second.portSequence.size();++j){
            uint32_t link=MakeLinkId(p->second.nodeIdSequence[j],p->second.portSequence[j]);
            if(unique.insert(link).second)links[link]+=SmallDemandRate(v.second);
        }
    }
    if(lcDynamicSmallLog){
        std::set<uint32_t> all;for(const auto &v:pathBytes)all.insert(v.first);for(const auto &v:m_dynamicLastPathBytes)all.insert(v.first);
        for(uint32_t pid:all){uint64_t bytes=pathBytes.count(pid)?pathBytes.at(pid):0;
            auto old=m_dynamicLastPathBytes.find(pid);
            if(old==m_dynamicLastPathBytes.end() || old->second!=bytes){
                lcDynamicSmallLog<<Simulator::Now().GetNanoSeconds()<<','<<(m_node?m_node->GetId():m_nodeId)<<",path,"<<pid<<','<<bytes<<','<<SmallDemandRate(bytes)<<",0,"<<lcSmallTargetNs<<'\n';
                m_dynamicLastPathBytes[pid]=bytes;
            }
        }
    }
    return links;
}
uint64_t RdmaSmartFlowRouting::lcSmallFlowBytes=100000;
double RdmaSmartFlowRouting::lcSmallReservePercent=10;
std::ofstream RdmaSmartFlowRouting::lcSizeFlowLog,RdmaSmartFlowRouting::lcSizeLinkLog,RdmaSmartFlowRouting::lcSizeHostLog;
bool RdmaSmartFlowRouting::IsSmallTransfer(Ptr<RdmaQueuePair> qp){return qp->m_size<=lcSmallFlowBytes;}
void RdmaSmartFlowRouting::SetSmallReservePercent(double percent){
    NS_ABORT_MSG_IF(!std::isfinite(percent) || percent<=0 || percent>=100,"Mixed-flow reserve must be strictly between 0 and 100 percent");
    lcSmallReservePercent=percent;
}
uint64_t RdmaSmartFlowRouting::SizeClassRate(uint64_t capacity,bool small){
    if(!lcSizeReserve || lcDynamicSmall)return capacity;
    return std::max<uint64_t>(1,uint64_t(capacity*(small?lcSmallReservePercent:100-lcSmallReservePercent)/100.0));
}
double RdmaSmartFlowRouting::LargeClassHeadroom(uint64_t capacity,double smallRate,double largeRate,double otherRate){
    if(lcDynamicSmall)return std::max(0.0,double(capacity)-smallRate-largeRate-otherRate);
    // smallRate is already charged to its own class; never subtract it from the large budget twice.
    (void)smallRate;
    return std::max(0.0,double(SizeClassRate(capacity,false))-largeRate-otherRate);
}
uint32_t RdmaSmartFlowRouting::SelectSmallFlowIndex(Ptr<RdmaQueuePair> qp,const std::vector<PathData*> &paths,const std::vector<double> &weights){
    if(qp->m_sizePinnedPath==UINT32_MAX){uint32_t i=GetPathBasedOnWeight(weights);qp->m_sizePinnedPath=paths.at(i)->pid;}
    for(uint32_t i=0;i<paths.size();++i)if(paths[i]->pid==qp->m_sizePinnedPath)return i;
    NS_ABORT_MSG("Pinned small-flow path disappeared");return 0;
}
void RdmaSmartFlowRouting::RestrictPinnedFlowPaths(Ptr<RdmaQueuePair> qp,std::vector<PathData*> &paths) const{
    if(!lcSizeAware || !IsSmallTransfer(qp) || qp->m_sizePinnedPath==UINT32_MAX)return;
    for(auto p:paths)if(p->pid==qp->m_sizePinnedPath){paths={p};return;}
    NS_ABORT_MSG("Pinned small-flow path missing from candidate set");
}
bool RdmaSmartFlowRouting::RunDynamicSmallSelfChecks(){
    Irn::mode=Irn::Mode::NACK;
    lcSizeAware=true;lcSizeReserve=true;lcDynamicSmall=true;lcSmallFlowBytes=100000;lcSmallTargetNs=50000;
    if(SmallDemandRate(100000)!=16000000000ULL || SmallDemandRate(50000)!=8000000000ULL || SmallDemandRate(0)!=0)return false;
    auto a=CreateObject<RdmaQueuePair>(3,Ipv4Address(1),Ipv4Address(2),1,2);
    auto b=CreateObject<RdmaQueuePair>(3,Ipv4Address(1),Ipv4Address(2),3,4);
    auto large=CreateObject<RdmaQueuePair>(3,Ipv4Address(1),Ipv4Address(2),5,6);
    a->SetSize(100000);b->SetSize(50000);large->SetSize(100001);
    a->snd_nxt=a->snd_una=a->m_irn.m_max_next_seq=0;b->snd_nxt=b->snd_una=b->m_irn.m_max_next_seq=0;
    if(PendingSmallBytes(a)!=100000 || PendingSmallBytes(b)!=50000)return false;
    auto routing=CreateObject<RdmaSmartFlowRouting>();PathData p,q;
    p.pid=10;p.nodeIdSequence={1000,1,9,99};p.portSequence={1,1,1};
    q.pid=20;q.nodeIdSequence={1000,2,9,99};q.portSequence={1,1,1};
    a->m_sizePinnedPath=10;b->m_sizePinnedPath=20;routing->m_dynamicSmallFlows[1]=a;routing->m_dynamicSmallFlows[2]=b;
    routing->m_nexthopSelTbl[10]=p;routing->m_nexthopSelTbl[20]=q;
    auto demand=routing->DynamicSmallLinkDemand();
    if(demand[MakeLinkId(9,1)]!=24000000000.0 || demand[MakeLinkId(1,1)]!=16000000000.0)return false;
    for(auto path:{p,q})for(size_t j=1;j<path.portSequence.size();++j)routing->m_linkStateTable[MakeLinkId(path.nodeIdSequence[j],path.portSequence[j])].availableQ=1000;
    SharedQuotaResult projection;
    if(!routing->ComputeSharedQuota({&p,&q},{true,true},0,projection) || projection.capacityQ[MakeLinkId(9,1)]!=760 || projection.capacityQ[MakeLinkId(1,1)]!=840)return false;
    auto eq=CreateObject<RdmaEgressQueue>();eq->m_sizeClassCapacity=100000000000ULL;eq->m_qpGrp=CreateObject<RdmaQueuePairGroup>();eq->m_qpGrp->AddQp(a);
    if(eq->EffectiveSizeClassRate(large)!=84000000000ULL || eq->EffectiveSizeClassRate(a)!=100000000000ULL)return false;
    eq->CommitSizeClassData(large,1048,1000);if(eq->SizeClassNextAvail(large)!=NanoSeconds(1100))return false;
    a->snd_nxt=a->m_irn.m_max_next_seq=100000; // Sent, still awaiting ACK: no unsent reservation remains.
    if(PendingSmallBytes(a)!=0 || eq->EffectiveSizeClassRate(large)!=100000000000ULL || eq->SizeClassNextAvail(large)!=NanoSeconds(1084))return false;
    demand=routing->DynamicSmallLinkDemand();if(demand[MakeLinkId(9,1)]!=8000000000.0 || demand.count(MakeLinkId(1,1)))return false;
    b->snd_nxt=b->m_irn.m_max_next_seq=50000;if(!routing->DynamicSmallLinkDemand().empty())return false;
    if(LargeClassHeadroom(100000000000ULL,10000000000.0,20000000000.0,1000000000.0)!=69000000000.0)return false;
    lcSmallTargetNs=1;a->snd_nxt=a->m_irn.m_max_next_seq=0;if(eq->EffectiveSizeClassRate(large)!=0)return false;
    lcDynamicSmall=false;Simulator::Destroy();std::cout<<"DYNAMIC_SMALL_CHECK_PASS pending_bytes units shrink release path_incidence shared_once projection source_capacity no_small_ceiling clamp physical_feedback\n";return true;
}
bool RdmaSmartFlowRouting::RunSizeAwareSelfChecks(){
    lcSizeAware=true;lcSizeReserve=true;lcSmallFlowBytes=100000;SetSmallReservePercent(10);
    auto qp=CreateObject<RdmaQueuePair>(3,Ipv4Address(1),Ipv4Address(2),1,2);
    qp->SetSize(100000);if(!IsSmallTransfer(qp))return false;
    qp->SetSize(100001);if(IsSmallTransfer(qp))return false;
    qp->SetSize(100000);if(SizeClassRate(100000000000ULL,true)!=10000000000ULL || SizeClassRate(100000000000ULL,false)!=90000000000ULL)return false;
    if(std::abs(LargeClassHeadroom(100000000000ULL,1600000000.0,4000000000.0,800000000.0)-85200000000.0)>1)return false;
    if(LargeClassHeadroom(100000000000ULL,0,95000000000.0,0)!=0)return false;
    auto routing=CreateObject<RdmaSmartFlowRouting>();PathData a,b;a.pid=10;b.pid=20;std::vector<PathData*> paths={&a,&b};
    if(routing->SelectSmallFlowIndex(qp,paths,{1,0})!=0 || routing->SelectSmallFlowIndex(qp,paths,{0,1})!=0)return false;
    auto restricted=paths;routing->RestrictPinnedFlowPaths(qp,restricted);if(restricted.size()!=1 || restricted[0]->pid!=10)return false;
    auto eq=CreateObject<RdmaEgressQueue>();eq->m_sizeClassCapacity=100000000000ULL;
    eq->CommitSizeClassData(qp,1000,1000);if(eq->SizeClassNextAvail(qp)!=NanoSeconds(1800))return false;
    auto other=CreateObject<RdmaQueuePair>(3,Ipv4Address(1),Ipv4Address(3),1,2);other->SetSize(1000);
    if(eq->SizeClassNextAvail(other)!=NanoSeconds(1800))return false; // Same NIC shares the small quota.
    SetSmallReservePercent(5);if(SizeClassRate(100000000000ULL,true)!=5000000000ULL)return false;
    lcSizeReserve=false;other->m_nextAvail=NanoSeconds(50);if(eq->SizeClassNextAvail(other)!=NanoSeconds(50))return false;
    Ptr<Packet> packet=Create<Packet>();SizeClassTag tag;tag.value=1;packet->AddPacketTag(tag);SizeClassTag copy;if(!packet->PeekPacketTag(copy) || copy.value!=1)return false;
    Simulator::Destroy();std::cout<<"SIZE_AWARE_SELF_CHECK_PASS threshold pin no_reselection pinned_cc class_capacity no_double_charge clamp shared_host_pacer adjustable_a mode_off class_tag\n";return true;
}
bool RdmaSmartFlowRouting::lcFeedbackResume=true;
std::ofstream RdmaSmartFlowRouting::lcFeedbackResumeLog;
uint64_t RdmaSmartFlowRouting::NextBandwidthRecoveryNs(const std::vector<PathData*> &paths,uint64_t now) const {

    if(lcAlpsProbe){
        if(paths.empty())return now+1;
        uint64_t maximum=0,coolMaximum=0,next=UINT64_MAX;auto copy=paths;std::vector<PathData*> expired;
        for(auto p:paths)maximum=std::max(maximum,uint64_t(p->latency));
        const_cast<RdmaSmartFlowRouting*>(this)->get_the_expired_paths(copy,expired);
        for(auto p:expired)coolMaximum=std::max(coolMaximum,uint64_t(p->latency));
        // This is a recheck deadline only. The original selector makes the actual decision.
        if(expired.empty())coolMaximum=maximum;
        for(auto p:paths){
            uint64_t expiry=uint64_t(p->tsGeneration.GetNanoSeconds())+2*maximum+1;
            uint64_t cooldown=uint64_t(p->tsProbeLastSend.GetNanoSeconds())+2*coolMaximum+1;
            next=std::min(next,std::max(expiry,cooldown));
        }
        return std::max(now+1,next);
    }

    uint64_t next=std::numeric_limits<uint64_t>::max();std::set<uint32_t> covered;
    // A pending probe may cover links also used by another endpoint's candidate paths.
    for(const auto &entry:m_earlyProbes) {
        const auto &p=entry.second;if(!p.pending)continue;
        uint64_t due=p.sentNs+ProbeTimeoutNs(p);next=std::min(next,std::max(now+1,due));
        if(due<=now)continue;
        auto it=m_nexthopSelTbl.find(entry.first);if(it==m_nexthopSelTbl.end())continue;
        const auto &path=it->second;
        for(size_t j=1;j<path.portSequence.size();++j)covered.insert(MakeLinkId(path.nodeIdSequence[j],path.portSequence[j]));
    }
    for(auto path:paths) {
        auto it=m_earlyProbes.find(path->pid);
        if(it!=m_earlyProbes.end() && it->second.pending)continue;
        uint64_t rtt=it==m_earlyProbes.end()?0:it->second.rttNs;
        uint64_t lead=std::min(lcTtlNs-lcWindowNs,std::max(lcEarlyLeadNs,rtt+2*lcWindowNs));
        bool pathAlarm=false;bool ownCongested=lcMaxLinkQueue && lcPathOnlyQueue && ReadPathQueueCongestion(*path,now,pathAlarm) && pathAlarm;
        bool uncovered=ownCongested;
        for(size_t j=1;j<path->portSequence.size();++j) {
            uint32_t link=MakeLinkId(path->nodeIdSequence[j],path->portSequence[j]);uint64_t age=0;bool alarm=false;
            bool known=LinkObservationAge(link,now,age);
            bool congested=lcMaxLinkQueue && lcPathOnlyQueue?ownCongested:(ReadLinkCongestion(link,now,alarm) && alarm);
            if((!known || age+lead>=lcTtlNs || congested) && !covered.count(link)){uncovered=true;break;}
        }
        if(uncovered) {
            uint64_t due=std::max(now+1,m_earlyNextNs);
            if(it!=m_earlyProbes.end())due=std::max(due,it->second.nextNs);
            next=std::min(next,due);
        }
    }
    // Defensive bounded retry if a configuration leaves no maintenance candidate.
    return next==std::numeric_limits<uint64_t>::max()?now+lcEarlyIntervalNs:next;
}
bool RdmaSmartFlowRouting::RunFeedbackResumeSelfChecks() {
    lcEnabled=true;lcBwCc=true;lcEarlyProbe=true;lcTtlNs=100000;lcWindowNs=5000;
    lcEarlyLeadNs=20000;lcEarlyIntervalNs=1000;lcEarlyTimeoutNs=1000000;
    Ptr<RdmaSmartFlowRouting> r=CreateObject<RdmaSmartFlowRouting>();r->m_nodeId=100;
    PathData a,b;a.pid=1;a.nodeIdSequence={100,1,9};a.portSequence={1,1};
    b=a;b.pid=2;std::vector<PathData*> paths={&a,&b};
    r->m_nexthopSelTbl[1]=a;r->m_nexthopSelTbl[2]=b;
    r->m_linkStateTable[MakeLinkId(1,1)].availableQ=0x8000;
    r->m_earlyStarted=true;r->m_earlyNextNs=1000;
    auto &p=r->m_earlyProbes[1];p.pending=true;p.sentNs=0;p.nextNs=10000;
    if(r->NextBandwidthRecoveryNs(paths,1000)!=1000000)return false; // Shared need is covered: wait for ACK/timeout.
    p.pending=false;p.nextNs=10000;r->m_earlyProbes[2].nextNs=10000;
    if(r->NextBandwidthRecoveryNs(paths,1000)!=10000)return false; // Still congested ACK: use path cooldown.
    p.pending=true;p.timeoutShift=1;
    if(r->NextBandwidthRecoveryNs(paths,1000)!=2000000)return false; // Existing exponential backoff preserved.
    p.pending=false;p.nextNs=0;r->m_earlyNextNs=3500;
    if(r->NextBandwidthRecoveryNs(paths,1000)!=3500)return false; // Source probe rate limit preserved.
    Ptr<RdmaQueuePair> qp=CreateObject<RdmaQueuePair>(3,Ipv4Address(1),Ipv4Address(2),1,2);
    qp->m_nextAvail=NanoSeconds(1700);qp->m_lcRecoveryAt=NanoSeconds(10000);
    if(qp->LapsNextAvail()!=NanoSeconds(10000))return false;
    qp->m_lcRecoveryAt=Time(0); // Feedback wakes only recovery, not pacing.
    if(qp->LapsNextAvail()!=NanoSeconds(1700))return false;
    qp->m_lcRecoveryAt=NanoSeconds(1000);
    if(qp->LapsNextAvail()!=NanoSeconds(1700))return false;
    Simulator::Destroy();
    std::cout<<"FEEDBACK_RESUME_SELF_CHECK_PASS shared_pending cooldown backoff source_limit separate_pacer\n";
    return true;
}
Ptr<Packet> RdmaSmartFlowRouting::MakeBandwidthRecoveryProbe(Ptr<RdmaQueuePair> qp) {
    HostId2PathSeleKey key(lookup_SMT(Ipv4Address(qp->sip))->hostId,lookup_SMT(Ipv4Address(qp->dip))->hostId);
    auto paths=batch_lookup_PIT(lookup_PST(key)->paths);
    RestrictPinnedFlowPaths(qp,paths); // SIZE_AWARE_V1
    std::vector<double> eligible(paths.size(),1.0);
    auto path=SelectEarlyProbe(paths,eligible,Simulator::Now().GetNanoSeconds());
    if(!path)return nullptr;
    CustomHeader ch(CustomHeader::L2_Header|CustomHeader::L3_Header|CustomHeader::L4_Header);
    ch.sip=qp->sip.Get();ch.dip=qp->dip.Get();ch.udp.sport=qp->sport;ch.udp.dport=qp->dport;
    Ptr<Packet> unused;auto packet=construct_probe_packet(unused,ch);
    add_path_tag_by_path_id(packet,path->pid);add_probe_tag_by_path_id(packet,path->pid);
    Ipv4SmartFlowPathTag tag;packet->PeekPacketTag(tag);update_path_tag(packet,tag);
    update_PIT_after_probing(path);record_the_probing_info(path->pid);
    RecordEarlyProbeSent(path->pid,Simulator::Now().GetNanoSeconds());return packet;
}
bool RdmaSmartFlowRouting::RunBandwidthCcSelfChecks() {
    lcEnabled=true;lcBwCc=true;lcBwMode=2;lcPruneEnabled=true;lcTtlNs=100000;
    Ptr<RdmaSmartFlowRouting> r=CreateObject<RdmaSmartFlowRouting>();r->m_nodeId=1000;
    PathData a,b,c;a.pid=1;b.pid=2;c.pid=3;
    a.nodeIdSequence={1000,1,9,99};a.portSequence={1,1,1};
    b.nodeIdSequence={1000,2,9,99};b.portSequence={2,1,1};
    c.nodeIdSequence={1000,3,8,99};c.portSequence={3,1,1};
    std::vector<PathData*> paths={&a,&b,&c};
    for(auto p:paths)for(size_t j=1;j<p->portSequence.size();++j)
        r->m_linkStateTable[MakeLinkId(p->nodeIdSequence[j],p->portSequence[j])].availableQ=1000;
    auto shared=MakeLinkId(9,1),privateB=MakeLinkId(2,1);
    auto check=[&](uint32_t blocked,uint32_t known){auto v=r->EvaluateBandwidthCongestion(paths,0);return v.blockedCount==blocked && v.knownCount==known;};
    if(!check(0,3))return false;
    r->m_linkStateTable[shared].availableQ=0;if(!check(0,3))return false; // QUEUE90_PROBE20_V1: a busy link is not a queue alarm
    r->m_linkStateTable[shared].availableQ=0x8000;
    auto v=r->EvaluateBandwidthCongestion(paths,0);
    if(v.blocked!=std::vector<bool>({true,true,false}) || v.privateQ[0]!=1000 || v.privateQ[1]!=1000)return false;
    std::vector<double> w={.2,.3,.5};if(!r->ApplyLinkCongestionPruning(paths,w) || w!=std::vector<double>({0,0,1}))return false;
    r->m_linkStateTable[privateB].availableQ=0x8000;
    r->m_linkStateTable[shared].availableQ=500;
    v=r->EvaluateBandwidthCongestion(paths,0);
    if(v.blocked!=std::vector<bool>({false,true,false}))return false; // shared recovery is not path recovery
    r->m_linkStateTable[MakeLinkId(1,1)].availableQ=0x8000;
    r->m_linkStateTable[MakeLinkId(3,1)].availableQ=0x8000;
    w={.2,.3,.5};if(!r->ApplyLinkCongestionPruning(paths,w) || w!=std::vector<double>({0,0,0}))return false;
    r->m_linkStateTable.clear();if(!check(0,0))return false;
    if(CompactLinkState::QueueGuardWord(0,1600000,1600000,true)!=0x8000 ||
       CompactLinkState::QueueGuardWord(0,1599999,1600000)!=0 ||
       CompactLinkState::QueueGuardWord(1,2000000,1600000)!=1 ||
       CompactLinkState::QueueGuardWord(0xffff,2000000,1600000)!=0xffff)return false;
    lcBwCc=false;lcBwMode=0;lcPruneEnabled=false;Simulator::Destroy();
    std::cout<<"BANDWIDTH_CC_SELF_CHECK_PASS zero_without_queue,shared_zero_budget,private_bottleneck,shared_recovery_not_path_recovery,all_blocked_zero_weights,unknown_hold,queue_boundary\n";return true;
}

// SHARED_QUOTA_V1. Quantized capacity units are 0.1 Gbps; grants may be fractional.
bool RdmaSmartFlowRouting::lcQuotaAudit=false;
std::map<uint32_t,RdmaSmartFlowRouting::SharedQuotaAudit> RdmaSmartFlowRouting::lcQuotaStats;
std::set<std::pair<uint64_t,uint64_t>> RdmaSmartFlowRouting::lcQuotaSnapshotKeys;
std::ofstream RdmaSmartFlowRouting::lcQuotaPathLog,RdmaSmartFlowRouting::lcQuotaLinkLog;
bool RdmaSmartFlowRouting::ComputeSharedQuota(const std::vector<PathData*> &paths,
        const std::vector<bool> &eligible,uint64_t now,SharedQuotaResult &r,const std::map<uint32_t,double> *localPenaltyQ) const {
    if(paths.size()!=eligible.size() || paths.empty())return false;
    r=SharedQuotaResult();const size_t n=paths.size();size_t count=0;
    r.referenceQ.assign(n,0);r.budgetQ.assign(n,0);r.referenceKind.assign(n,0);
    std::vector<double> ownMinimum(n,0);std::vector<bool> hasPrivate(n,false);
    const auto smallDemand=DynamicSmallLinkDemand();
    for(size_t i=0;i<n;++i)if(eligible[i]) {
        ++count;const auto &p=*paths[i];
        if(p.nodeIdSequence.size()!=p.portSequence.size()+1 || p.portSequence.size()<2)return false;
        double low=65535;std::set<uint32_t> unique;
        for(size_t j=1;j<p.portSequence.size();++j) { // Source NIC excluded, as in existing telemetry.
            uint32_t link=MakeLinkId(p.nodeIdSequence[j],p.portSequence[j]);uint16_t q;
            if(!unique.insert(link).second || !ReadLinkCapacity(link,now,q))return false;
            if(lcDynamicSmall){auto demand=smallDemand.find(link);if(demand!=smallDemand.end()){
                uint64_t protectedQ=uint64_t(std::ceil(demand->second/100000000.0));
                q=protectedQ>=q?0:uint16_t(q-protectedQ);
            }} // One physical-link deduction before private/shared bottleneck projection.
            double cap=q;
            if(localPenaltyQ){auto it=localPenaltyQ->find(link);double delta=it==localPenaltyQ->end()?0:it->second;
                cap=std::max(0.0,double(m_linkStateTable.at(link).SignedScoreQ())-delta);}
            r.users[link].push_back(i);r.capacityQ[link]=cap;low=std::min(low,cap);
        }
        ownMinimum[i]=low;r.referenceQ[i]=65535;
    }
    if(!count)return true;
    for(const auto &e:r.users) {
        if(e.second.size()==1) {size_t i=e.second[0];hasPrivate[i]=true;
            r.referenceQ[i]=std::min(r.referenceQ[i],double(r.capacityQ.at(e.first)));}
        else {++r.shared;if(e.second.size()<count)++r.partial;}
    }
    std::vector<bool> active(n,false);size_t remaining=0;
    for(size_t i=0;i<n;++i)if(eligible[i]) {
        if(hasPrivate[i])r.referenceKind[i]=1;
        else {r.referenceQ[i]=ownMinimum[i];r.referenceKind[i]=2;++r.noPrivate;}
        if(r.referenceQ[i]>0){active[i]=true;++remaining;}
    }
    // Increase every still-active grant at speed referenceQ[i]. Freeze all users
    // of a tight link, then redistribute remaining capacity to the unfrozen set.
    while(remaining) {
        if(++r.iterations>n+1)return false;
        double step=1e100;
        for(size_t i=0;i<n;++i)if(active[i])step=std::min(step,std::max(0.0,r.referenceQ[i]-r.budgetQ[i])/r.referenceQ[i]);
        for(const auto &e:r.users) {
            double used=0,speed=0;
            for(size_t i:e.second){used+=r.budgetQ[i];if(active[i])speed+=r.referenceQ[i];}
            if(speed>0)step=std::min(step,std::max(0.0,double(r.capacityQ.at(e.first))-used)/speed);
        }
        if(!std::isfinite(step) || step<0)return false;
        for(size_t i=0;i<n;++i)if(active[i])r.budgetQ[i]+=step*r.referenceQ[i];
        std::vector<bool> freeze(n,false);
        for(size_t i=0;i<n;++i)if(active[i] && r.referenceQ[i]-r.budgetQ[i]<=1e-8*std::max(1.0,r.referenceQ[i]))freeze[i]=true;
        for(const auto &e:r.users) {
            double used=0;for(size_t i:e.second)used+=r.budgetQ[i];
            double cap=r.capacityQ.at(e.first);
            if(cap-used<=1e-8*std::max(1.0,cap))for(size_t i:e.second)if(active[i])freeze[i]=true;
        }
        size_t stopped=0;
        for(size_t i=0;i<n;++i)if(active[i] && freeze[i]){active[i]=false;--remaining;++stopped;}
        if(!stopped)return false;
    }
    for(const auto &e:r.users) {
        double used=0;for(size_t i:e.second)used+=r.budgetQ[i];
        r.worstExcessQ=std::max(r.worstExcessQ,used-r.capacityQ.at(e.first));
        if(used-r.capacityQ.at(e.first)>1e-7*std::max(1.0,double(r.capacityQ.at(e.first))))return false;
    }
    return true;
}
void RdmaSmartFlowRouting::ConfigureSharedQuotaAudit(const std::string &directory) {
    if(!lcQuotaAudit)return;
    lcQuotaPathLog.open(directory+"/shared-quota-paths.csv");lcQuotaLinkLog.open(directory+"/shared-quota-links.csv");
    NS_ABORT_MSG_IF(!lcQuotaPathLog || !lcQuotaLinkLog,"Cannot write shared quota audit");
    lcQuotaPathLog<<"time_ns,node,destination,path,reference_kind,reference_gbps,budget_gbps,normalized_budget,applied,total_budget_gbps,iterations,shared_links,partial_shared_links\n";
    lcQuotaLinkLog<<"time_ns,node,destination,link,capacity_gbps,allocated_gbps,path_users\n";
    lcQuotaPathLog<<std::setprecision(17);lcQuotaLinkLog<<std::setprecision(17);
}
void RdmaSmartFlowRouting::AuditSharedQuota(const std::vector<PathData*> &paths,const SharedQuotaResult &r,uint64_t now,bool applied) {
    if(!lcQuotaAudit)return;
    uint32_t node=m_node?m_node->GetId():m_nodeId;auto &a=lcQuotaStats[node];++a.feasible;
    a.partial+=r.partial>0;a.noPrivate+=r.noPrivate>0;
    double total=0;for(double x:r.budgetQ)total+=x;a.sumBudgetQ+=total;
    uint32_t dst=paths[0]->nodeIdSequence.back();uint64_t endpoint=(uint64_t(node)<<32)|dst;
    if(!lcQuotaSnapshotKeys.insert(std::make_pair(now/20000,endpoint)).second)return;
    for(size_t i=0;i<paths.size();++i)lcQuotaPathLog<<now<<','<<node<<','<<dst<<','<<paths[i]->pid<<','
        <<r.referenceKind[i]<<','<<r.referenceQ[i]/10<<','<<r.budgetQ[i]/10<<','<<(total?r.budgetQ[i]/total:0)<<','
        <<applied<<','<<total/10<<','<<r.iterations<<','<<r.shared<<','<<r.partial<<'\n';
    for(const auto &e:r.users) {
        double used=0;for(size_t i:e.second)used+=r.budgetQ[i];
        lcQuotaLinkLog<<now<<','<<node<<','<<dst<<','<<e.first<<','<<double(r.capacityQ.at(e.first))/10<<','<<used/10<<','<<e.second.size()<<'\n';
    }
}
void RdmaSmartFlowRouting::WriteSharedQuotaAudit(const std::string &directory) {
    if(!lcQuotaAudit)return;std::ofstream f(directory+"/shared-quota-summary.csv");
    NS_ABORT_MSG_IF(!f,"Cannot write shared quota summary");
    f<<"node,evaluations,applied,unknown_fallback,all_zero_fallback,no_eligible,invalid_fallback,partial_shared_feasible,no_private_reference_feasible,feasible_vectors,mean_budget_gbps\n";
    f<<std::setprecision(17);
    for(const auto &e:lcQuotaStats){const auto &a=e.second;f<<e.first<<','<<a.evaluations<<','<<a.applied<<','<<a.unknown<<','<<a.allZero<<','<<a.noEligible<<','<<a.invalid<<','<<a.partial<<','<<a.noPrivate<<','<<a.feasible<<','<<(a.feasible?double(a.sumBudgetQ/a.feasible/10):0)<<'\n';}
    lcQuotaPathLog.flush();lcQuotaLinkLog.flush();
}
bool RdmaSmartFlowRouting::RunSharedQuotaSelfChecks() {
    lcEnabled=true;lcBwMode=2;lcBwAudit=false;lcQuotaAudit=false;lcTtlNs=100000;
    Ptr<RdmaSmartFlowRouting> r=CreateObject<RdmaSmartFlowRouting>();r->m_nodeId=1000;
    PathData a,b,c;a.pid=1;b.pid=2;c.pid=3;std::vector<PathData*> paths={&a,&b,&c};
    for(size_t i=0;i<3;++i){paths[i]->nodeIdSequence={1000,uint32_t(i+1),9,99};paths[i]->portSequence={uint32_t(i+1),1,1};}
    auto load=[&](){r->m_linkStateTable.clear();for(auto p:paths){r->m_nexthopSelTbl[p->pid]=*p;for(size_t j=1;j<p->portSequence.size();++j)r->m_linkStateTable[MakeLinkId(p->nodeIdSequence[j],p->portSequence[j])].availableQ=1000;}};
    auto close=[](double x,double y){return std::abs(x-y)<1e-7;};
    load();r->m_linkStateTable[MakeLinkId(1,1)].availableQ=500;r->m_linkStateTable[MakeLinkId(2,1)].availableQ=300;
    r->m_linkStateTable[MakeLinkId(3,1)].availableQ=200;r->m_linkStateTable[MakeLinkId(9,1)].availableQ=400;
    SharedQuotaResult q;std::vector<bool> mask={true,true,true};std::vector<double> w={.7,.2,.1};
    if(!r->ComputeSharedQuota(paths,mask,0,q) || !close(q.budgetQ[0],200) || !close(q.budgetQ[1],120) || !close(q.budgetQ[2],80))return false;
    if(!r->ApplyBottleneckBandwidthWeights(paths,w) || !close(w[0],.5) || !close(w[1],.3) || !close(w[2],.2))return false;
    std::cout<<"COMMON_EXAMPLE_GBPS reference=50/30/20 shared=40 grant=20/12/8 probability=0.5/0.3/0.2\n";
    r->m_linkStateTable[MakeLinkId(9,1)].availableQ=1500;
    if(!r->ComputeSharedQuota(paths,mask,0,q) || !close(q.budgetQ[0],500) || !close(q.budgetQ[1],300) || !close(q.budgetQ[2],200))return false;
    // Intersecting shared groups: P1/P2 <=40, P2/P3 <=60, private reference 100 each.
    a.nodeIdSequence={1000,1,4,5,99};a.portSequence={1,1,1,1};
    b.nodeIdSequence={1000,2,4,5,6,7,99};b.portSequence={2,1,1,2,1,1};
    c.nodeIdSequence={1000,3,6,7,99};c.portSequence={3,1,1,1};load();
    r->m_linkStateTable[MakeLinkId(4,1)].availableQ=400;r->m_linkStateTable[MakeLinkId(6,1)].availableQ=600;
    if(!r->ComputeSharedQuota(paths,mask,0,q) || !close(q.budgetQ[0],200) || !close(q.budgetQ[1],200) || !close(q.budgetQ[2],400) || q.iterations!=2)return false;
    std::cout<<"OVERLAP_EXAMPLE_GBPS P1+P2<=40 P2+P3<=60 grant=20/20/40 probability=0.25/0.25/0.5\n";
    std::vector<PathData*> perm={&c,&a,&b};SharedQuotaResult permq;
    if(!r->ComputeSharedQuota(perm,mask,0,permq) || !close(permq.budgetQ[0],400) || !close(permq.budgetQ[1],200) || !close(permq.budgetQ[2],200))return false;
    // No private links: explicit standalone-path reference, no invented infinity/full capacity.
    a.nodeIdSequence={1000,9,99};a.portSequence={1,1};b=a;b.pid=2;c=a;c.pid=3;load();
    r->m_linkStateTable[MakeLinkId(9,1)].availableQ=600;
    if(!r->ComputeSharedQuota(paths,mask,0,q) || q.noPrivate!=3 || !close(q.budgetQ[0],200) || !close(q.budgetQ[1],200) || !close(q.budgetQ[2],200))return false;
    mask={false,true,true};r->m_linkStateTable[MakeLinkId(9,1)].availableQ=400;
    if(!r->ComputeSharedQuota(paths,mask,0,q) || q.budgetQ[0]!=0 || !close(q.budgetQ[1],200) || !close(q.budgetQ[2],200))return false;
    r->m_linkStateTable[MakeLinkId(9,1)].availableQ=0;
    if(!r->ComputeSharedQuota(paths,mask,0,q) || q.budgetQ!=std::vector<double>({0,0,0}))return false;
    w={.7,.2,.1};if(r->ApplyBottleneckBandwidthWeights(paths,w) || w!=std::vector<double>({.7,.2,.1}))return false;
    r->m_linkStateTable.clear();w={.7,.2,.1};if(r->ApplyBottleneckBandwidthWeights(paths,w) || w!=std::vector<double>({.7,.2,.1}))return false;
    // No sharing -> exactly ordinary per-path bottleneck proportions.
    for(size_t i=0;i<3;++i){paths[i]->pid=i+1;paths[i]->nodeIdSequence={1000,uint32_t(i+1),99};paths[i]->portSequence={uint32_t(i+1),1};}
    load();r->m_linkStateTable[MakeLinkId(1,1)].availableQ=100;r->m_linkStateTable[MakeLinkId(2,1)].availableQ=200;r->m_linkStateTable[MakeLinkId(3,1)].availableQ=300;
    w={.7,.2,.1};if(!r->ApplyBottleneckBandwidthWeights(paths,w) || !close(w[0],1.0/6) || !close(w[1],1.0/3) || !close(w[2],.5))return false;
    lcBwMode=0;Simulator::Destroy();std::cout<<"SHARED_QUOTA_SELFTEST_PASS common_exact,spare_capacity,intersecting_groups,redistribution,permutation,no_private_convention,zero_mask,all_zero,unknown,disjoint\n";return true;
}


// ZERO_BANDWIDTH_V1: unknown paths keep their own base probability mass.
// Complete paths share the remainder according to their projected shared quota.
std::map<uint32_t,RdmaSmartFlowRouting::PathLocalAudit> RdmaSmartFlowRouting::lcPathLocalStats;

// BW_SEND_PENALTY_V1: preserve raw capacity; add the ALPS send-work cost once.
void RdmaSmartFlowRouting::ApplyBandwidthSendPenalty(const std::vector<PathData*> &paths,
        std::vector<double> &weights,const std::vector<bool> &zero,const std::vector<bool> &blocked) {
    NS_ABORT_MSG_IF(paths.size()!=weights.size() || zero.size()!=weights.size() || blocked.size()!=weights.size(),"Invalid send-penalty vectors");
    uint64_t reference=0;for(auto p:paths)reference=std::max(reference,uint64_t(p->theoreticalSmallestLatencyInNs));
    NS_ABORT_MSG_IF(!reference,"Send penalty requires a positive path reference");
    const double beta=choose_softmax==0?0.2:5.0;auto prior=weights;
    std::vector<double> logScore(paths.size(),-std::numeric_limits<double>::infinity());
    double maximum=-std::numeric_limits<double>::infinity();
    for(size_t i=0;i<paths.size();++i)if(!blocked[i] && prior[i]>0){
        logScore[i]=std::log(prior[i])-beta*84.0*double(paths[i]->sent_pkt_num)/double(reference);
        maximum=std::max(maximum,logScore[i]);
    }
    NS_ABORT_MSG_IF(!std::isfinite(maximum),"No live probability for send penalty");
    double sum=0;
    for(size_t i=0;i<weights.size();++i){weights[i]=std::isfinite(logScore[i])?std::exp(logScore[i]-maximum):0;sum+=weights[i];}
    for(double &v:weights)v/=sum;
    auto raw=weights;
    if(lcZeroBwMinProbability>0)ApplyZeroBudgetFloor(weights,zero,blocked,lcZeroBwMinProbability);
    uint64_t now=Simulator::Now().GetNanoSeconds();uint32_t node=m_node?m_node->GetId():m_nodeId;
    if(lcSendPenaltyLog && lcZeroFloorStats[node].lastBucket!=now/20000){
        for(size_t i=0;i<paths.size();++i){
            double logFactor=-beta*84.0*double(paths[i]->sent_pkt_num)/double(reference);
            lcSendPenaltyLog<<now<<','<<node<<','<<paths[i]->pid<<','<<paths[i]->sent_pkt_num<<','<<reference<<','<<beta<<','
                <<logFactor<<','<<std::exp(logFactor)<<','<<prior[i]<<','<<raw[i]<<','<<weights[i]<<','<<blocked[i]<<','<<zero[i]<<'\n';
        }
    }
}

bool RdmaSmartFlowRouting::RunBandwidthSendPenaltySelfChecks(){
    bool ok=true;unsigned count=0;auto ck=[&](bool v,const char *name){++count;if(!v){ok=false;std::cout<<"BW_SEND_PENALTY_CASE_FAIL "<<name<<'\n';}};
    auto near=[](double a,double b){return std::fabs(a-b)<1e-9;};
    lcEnabled=true;lcBwCc=true;lcBwMode=2;lcSendPenalty=true;lcZeroBwMinProbability=.05;lcMaxLinkQueue=true;lcPathOnlyQueue=true;lcPathQueueMax=true;
    lcAlpsCongestion=false;lcRequireZeroBw=false;lcWindowNs=5000;lcTtlNs=100000;lcQueueAlarmBytes=100000;lcBwAudit=false;lcQuotaAudit=false;lcEarlyProbe=false;
    enable_laps_plus=true;choose_softmax=3;
    auto src=CreateObject<RdmaSmartFlowRouting>();src->m_nodeId=0;
    PathData a,b;a.pid=1;b.pid=2;a.latency=b.latency=10000;a.theoreticalSmallestLatencyInNs=b.theoreticalSmallestLatencyInNs=20000;
    a.nodeIdSequence={100,1,99};b.nodeIdSequence={100,2,99};a.portSequence=b.portSequence={1,1};
    a.maxEgressQueueBytes=b.maxEgressQueueBytes=0;a.queueFeedbackBornNs=b.queueFeedbackBornNs=0;
    std::map<uint32_t,PathData> pit={{1,a},{2,b}};src->set_PIT(pit);std::vector<PathData*>paths={src->lookup_PIT(1),src->lookup_PIT(2)};
    for(auto p:paths)src->m_pathBdpMap[p->pid]=PathBdpInfo();
    auto load=[&](uint32_t node,uint16_t q){auto &v=src->m_linkStateTable[MakeLinkId(node,1)];v.availableQ=q;v.sampleEpoch=0;};
    std::vector<double>w={.5,.5};src->ApplyBandwidthSendPenalty(paths,w,{false,false},{false,false});ck(near(w[0],.5),"zero_counter_identity");
    paths[0]->sent_pkt_num=100;const double g=std::exp(-2.1),expected=.95*g/(.95*g+.05);
    auto base=src->CalPathWeightBasedOnDelay(paths);ck(near(base[0],.5),"counter_removed_from_delay_prior");
    w={.95,.05};src->ApplyBandwidthSendPenalty(paths,w,{false,true},{false,false});ck(near(w[0],expected),"95_percent_stale_case_exploration_competes");
    load(1,1000);load(2,0);w=base;src->ApplyBottleneckBandwidthWeights(paths,w);ck(near(w[0],expected),"integrated_positive_budget_penalty");
    ck(src->m_earlyProbes[1].lastBudgetQ==1000 && src->m_earlyProbes[2].lastBudgetQ==0 && src->m_linkStateTable[MakeLinkId(1,1)].availableQ==1000,"raw_budget_and_lst_unchanged");
    src->m_linkStateTable.clear();w=base;src->ApplyBottleneckBandwidthWeights(paths,w);ck(near(w[0],expected),"cached_budget_same_penalty_once");
    src->m_earlyProbes.clear();w=base;src->ApplyBottleneckBandwidthWeights(paths,w);ck(near(w[0],g/(1+g)),"unknown_fallback_penalty_once");
    load(1,0);load(2,0);w=base;src->ApplyBottleneckBandwidthWeights(paths,w);ck(near(w[0],g/(1+g)),"all_zero_fallback_penalty_once");
    w={.05,.95};src->ApplyBandwidthSendPenalty(paths,w,{true,false},{false,false});ck(near(w[0],.05),"final_zero_budget_floor_restored");
    w={0,1};src->ApplyBandwidthSendPenalty(paths,w,{false,false},{true,false});ck(w[0]==0 && near(w[1],1),"pruned_path_never_revived");
    paths[0]->sent_pkt_num=paths[1]->sent_pkt_num=1000000000;w={.5,.5};src->ApplyBandwidthSendPenalty(paths,w,{false,false},{false,false});ck(near(w[0],.5)&&std::isfinite(w[1]),"log_normalization_large_counter");
    paths[0]->sent_pkt_num=paths[1]->sent_pkt_num=0;load(1,1000);load(2,1000);w={.5,.5};src->ApplyBottleneckBandwidthWeights(paths,w);ck(near(w[0],.5),"feedback_counter_reset_restores_weight");
    lcSendPenalty=false;paths[0]->sent_pkt_num=100;base=src->CalPathWeightBasedOnDelay(paths);ck(near(base[0],g/(1+g)),"flag_off_original_counter_retained");
    lcBwCc=false;Simulator::Destroy();std::cout<<(ok?"BW_SEND_PENALTY_SELF_CHECK_PASS":"BW_SEND_PENALTY_SELF_CHECK_FAIL")<<" assertions="<<count<<'\n';return ok;
}

// BW_EXP_ONLY_V1: overwrite the incoming vector; it is NOT a prior or a fallback.
double RdmaSmartFlowRouting::lcBwBeta=5.0;
double RdmaSmartFlowRouting::lcBwUnitGbps=100.0;
std::ofstream RdmaSmartFlowRouting::lcBwExpLog;
std::ofstream RdmaSmartFlowRouting::lcQueueScoreLog;
std::map<uint32_t,uint64_t> RdmaSmartFlowRouting::lcBwExpLastBucket;

// LINK_SEND_PENALTY_V1: source-local work since last feedback, not a strict outstanding-byte ledger.
bool RdmaSmartFlowRouting::lcLinkSendPenalty=false;
double RdmaSmartFlowRouting::lcLinkSendAlpha=1.0;
std::ofstream RdmaSmartFlowRouting::lcLinkPenaltyLog,RdmaSmartFlowRouting::lcPathPenaltyLog,RdmaSmartFlowRouting::lcFeedbackCounterLog;
std::map<uint32_t,uint64_t> RdmaSmartFlowRouting::lcFeedbackCounterSerial;
std::map<uint32_t,double> RdmaSmartFlowRouting::BuildLinkSendPenaltyQ() const {
    std::map<uint32_t,double> result;if(!lcLinkSendPenalty)return result;
    const uint32_t source=m_node?m_node->GetId():m_nodeId;
    const double horizon=8.0*double(lcQueueAlarmBytes)/1e11;
    NS_ABORT_MSG_IF(!(horizon>0),"Local send penalty requires threshold drain horizon");
    const double perPacketQ=lcLinkSendAlpha*8.0*1048.0/horizon/1e8;
    for(const auto &item:m_nexthopSelTbl){const auto &p=item.second;
        if(!p.sent_pkt_num || p.nodeIdSequence.empty() || p.nodeIdSequence.front()!=source)continue;
        NS_ABORT_MSG_IF(p.nodeIdSequence.size()!=p.portSequence.size()+1,"Invalid local DATA path");
        std::set<uint32_t> once;
        for(size_t j=1;j<p.portSequence.size();++j){uint32_t link=MakeLinkId(p.nodeIdSequence[j],p.portSequence[j]);
            if(once.insert(link).second)result[link]+=perPacketQ*double(p.sent_pkt_num);}
    }
    return result;
}
double RdmaSmartFlowRouting::PathLinkSendPenaltyQ(const PathData &p,const std::map<uint32_t,double> &penalties) const {
    double largest=0;for(size_t j=1;j<p.portSequence.size();++j){auto it=penalties.find(MakeLinkId(p.nodeIdSequence[j],p.portSequence[j]));if(it!=penalties.end())largest=std::max(largest,it->second);}
    return largest;
}
double RdmaSmartFlowRouting::PenalizedPathMinimumQ(const PathData &p,uint64_t now,const std::map<uint32_t,double> &penalties) const {
    double low=std::numeric_limits<double>::infinity();
    for(size_t j=1;j<p.portSequence.size();++j){uint32_t link=MakeLinkId(p.nodeIdSequence[j],p.portSequence[j]);uint16_t q;
        NS_ABORT_MSG_IF(!ReadLinkCapacity(link,now,q),"Fresh path lost a causal link observation");
        auto it=penalties.find(link);double delta=it==penalties.end()?0:it->second;
        low=std::min(low,double(m_linkStateTable.at(link).SignedScoreQ())-delta);}
    NS_ABORT_MSG_IF(!std::isfinite(low),"Empty local-penalty path");return low;
}
void RdmaSmartFlowRouting::RecordFeedbackSendCounter(uint32_t pid,const char *event,uint32_t before,uint32_t after){
    if(!lcLinkSendPenalty || !lcFeedbackCounterLog)return;
    uint32_t node=m_node?m_node->GetId():m_nodeId;uint64_t serial=++lcFeedbackCounterSerial[node];
    lcFeedbackCounterLog<<serial<<','<<Simulator::Now().GetNanoSeconds()<<','<<node<<','<<pid<<','<<event<<','<<before<<','<<after<<'\n';
}
void RdmaSmartFlowRouting::ResetFeedbackSendCounter(uint32_t pid){
    auto path=lookup_PIT(pid);RecordFeedbackSendCounter(pid,"feedback_reset",path->sent_pkt_num,0);path->sent_pkt_num=0;
}

bool RdmaSmartFlowRouting::ApplyPathLocalBandwidthWeights(const std::vector<PathData*> &paths,std::vector<double> &weights) {
    NS_ABORT_MSG_IF(paths.empty() || paths.size()!=weights.size(),"Invalid pure-bandwidth input");
    NS_ABORT_MSG_IF(!std::isfinite(lcBwBeta) || lcBwBeta<0 || !std::isfinite(lcBwUnitGbps) || lcBwUnitGbps<=0,"Invalid bandwidth exponential scale");
    const uint64_t now=Simulator::Now().GetNanoSeconds();const uint32_t node=m_node?m_node->GetId():m_nodeId;
    const auto status=EvaluateBandwidthSelection(paths,now);const size_t n=paths.size();
    std::vector<bool> fresh(n,false),cached(n,false),unknown(n,false),zero(n,false);
    std::vector<double> minimum(n,0),budget(n,0),factor(n,1),score(n,0),signedMinimum(n,0),grant(n,0);
    const auto localPenalty=BuildLinkSendPenaltyQ();
    std::vector<double> feedbackBudget(n,0),maxLocalPenalty(n,0);
    size_t freshCount=0,cachedCount=0,unknownCount=0,live=0,zeroCount=0;
    std::fill(weights.begin(),weights.end(),0.0);
    for(size_t i=0;i<n;++i)if(!status.blocked[i]){
        ++live;factor[i]=ProbeCapacityFactor(paths[i]->pid,now);uint16_t q;uint32_t link;
        if(GetForwardSwitchBottleneck(paths[i]->pid,now,q,link)){
            fresh[i]=true;minimum[i]=q;++freshCount;
            if(CompactLinkState::queueScoreEnabled){int32_t raw;NS_ABORT_MSG_IF(!GetForwardSwitchSignedScore(paths[i]->pid,now,raw),"Score freshness mismatch");signedMinimum[i]=raw;}
            else signedMinimum[i]=q;
        }else{
            const auto it=m_earlyProbes.find(paths[i]->pid);
            if(it!=m_earlyProbes.end() && std::isfinite(it->second.lastBudgetQ) && (CompactLinkState::queueScoreEnabled || it->second.lastBudgetQ>=0)){
                cached[i]=true;budget[i]=it->second.lastBudgetQ;++cachedCount;
            }else{unknown[i]=true;++unknownCount;} // No history => zero-bandwidth PRIOR, never a latency prior.
        }
        if(lcBwAudit){auto &a=lcBwPaths[(uint64_t(node)<<32)|paths[i]->pid];++a.eligible;
            if(fresh[i]){++a.known;a.bottleneckQSum+=minimum[i];a.zero+=minimum[i]==0;}}
    }
    auto &audit=lcPathLocalStats[node];
    if(lcBwAudit){++audit.decisions;audit.knownPaths+=freshCount;audit.cachedPaths+=cachedCount;audit.unknownPaths+=unknownCount;
        audit.zeroPaths+=status.blockedCount;audit.mixed+=(freshCount+cachedCount)>0 && unknownCount>0;audit.allZero+=!live;
        for(size_t i=0;i<n;++i)if(cached[i] && factor[i]==1)++audit.cachedOutsideProbe;
        auto &a=lcBwDecisions[node];++a.evaluations;if(live)++a.applied;else ++a.noEligible;}
    if(!live)return true;
    if(freshCount){
        SharedQuotaResult q;bool projected=lcBwMode==2 && ComputeSharedQuota(paths,fresh,now,q);
        if(lcBwMode==2 && !projected){if(lcBwAudit)++audit.invalidProjection;}
        SharedQuotaResult chosen=q;bool chosenProjected=projected;
        if(lcLinkSendPenalty){chosenProjected=ComputeSharedQuota(paths,fresh,now,chosen,&localPenalty);
            if(!chosenProjected && lcBwAudit)++audit.invalidProjection;}
        if(chosenProjected)AuditSharedQuota(paths,chosen,now,true);
        for(size_t i=0;i<n;++i)if(fresh[i]){
            double rawGrant=projected?q.budgetQ[i]:minimum[i];
            feedbackBudget[i]=CompactLinkState::queueScoreEnabled?std::min(rawGrant,signedMinimum[i]):rawGrant;
            m_earlyProbes[paths[i]->pid].lastBudgetQ=float(feedbackBudget[i]); // Cache only observation-based score.
            if(lcLinkSendPenalty)signedMinimum[i]=PenalizedPathMinimumQ(*paths[i],now,localPenalty);
            grant[i]=chosenProjected?chosen.budgetQ[i]:(lcLinkSendPenalty?std::max(0.0,signedMinimum[i]):minimum[i]);
            budget[i]=CompactLinkState::queueScoreEnabled?std::min(grant[i],signedMinimum[i]):grant[i];
        }
    }
    for(size_t i=0;i<n;++i)if(!status.blocked[i]){
        maxLocalPenalty[i]=PathLinkSendPenaltyQ(*paths[i],localPenalty);
        if(!fresh[i]){
            feedbackBudget[i]=budget[i];
            // Missing per-link information cannot support an exact reprojection: retain the last
            // observation-only path score, subtract the largest current link cost exactly once.
            if(lcLinkSendPenalty)budget[i]=feedbackBudget[i]-maxLocalPenalty[i];
        }
    }
    double total=0,maximum=-std::numeric_limits<double>::infinity();
    for(size_t i=0;i<n;++i)if(!status.blocked[i]){
        NS_ABORT_MSG_IF(!std::isfinite(budget[i]) || (!CompactLinkState::queueScoreEnabled && budget[i]<0) || !(factor[i]>0) || !std::isfinite(factor[i]),"Invalid bandwidth budget/probe factor");
        total+=budget[i];zero[i]=!unknown[i] && budget[i]==0;zeroCount+=zero[i];
        // budgetQ unit = 0.1 Gbps. Probe discount is an explicit pre-existing confidence factor.
        score[i]=lcBwBeta*(budget[i]/10.0)/lcBwUnitGbps+std::log(factor[i]);
        NS_ABORT_MSG_IF(!std::isfinite(score[i]),"Nonfinite bandwidth exponential score");
        maximum=std::max(maximum,score[i]);
        if(lcBwAudit && factor[i]<1)++audit.discountedPaths;
    }
    double sum=0;
    for(size_t i=0;i<n;++i)if(!status.blocked[i]){weights[i]=std::exp(score[i]-maximum);sum+=weights[i];}
    NS_ABORT_MSG_IF(!(sum>0) || !std::isfinite(sum),"Invalid bandwidth softmax sum");
    for(double &x:weights)x/=sum;
    const auto raw=weights;double effective=0;
    if(lcBwCc && lcZeroBwMinProbability>0)effective=ApplyZeroBudgetFloor(weights,zero,status.blocked,lcZeroBwMinProbability);
    // No all-zero special formula: exp(beta*0)=1, so un-discounted live paths are uniform.
    // No 84ns/send-count penalty: the only score signal above is measured/cached bandwidth.
    if(lcBwAudit){
        double unknownMass=0;for(size_t i=0;i<n;++i)if(unknown[i])unknownMass+=weights[i];audit.reservedUnknownMass+=unknownMass;
        auto &a=lcZeroFloorStats[node];++a.decisions;a.zeroPaths+=zeroCount;a.capped+=effective+1e-12<lcZeroBwMinProbability;
        bool changed=false;for(size_t i=0;i<n;++i)changed|=std::fabs(raw[i]-weights[i])>1e-12;a.changed+=changed;
        const uint64_t bucket=now/20000;
        if(bucket!=a.lastBucket){a.lastBucket=bucket;
            if(lcZeroFloorLog)for(size_t i=0;i<n;++i)lcZeroFloorLog<<now<<','<<node<<','<<paths[i]->pid<<','<<status.blocked[i]<<','<<fresh[i]<<','<<cached[i]<<','<<budget[i]<<','<<factor[i]<<','<<zero[i]<<','<<raw[i]<<','<<weights[i]<<','<<effective<<'\n';
        }
        auto it=lcBwExpLastBucket.find(node);
        if(lcBwExpLog && (it==lcBwExpLastBucket.end() || it->second!=bucket)){
            lcBwExpLastBucket[node]=bucket;
            if(lcLinkSendPenalty && lcLinkPenaltyLog){
                const uint64_t serial=lcFeedbackCounterSerial[node];std::set<uint32_t> candidateLinks;
                for(auto p:paths)for(size_t j=1;j<p->portSequence.size();++j)candidateLinks.insert(MakeLinkId(p->nodeIdSequence[j],p->portSequence[j]));
                for(uint32_t link:candidateLinks){uint16_t q;bool known=ReadLinkCapacity(link,now,q);
                    double base=known?double(m_linkStateTable.at(link).SignedScoreQ()):std::numeric_limits<double>::quiet_NaN();
                    auto it=localPenalty.find(link);double delta=it==localPenalty.end()?0:it->second;
                    std::string contributors;
                    for(const auto &entry:m_nexthopSelTbl){const auto &p=entry.second;if(!p.sent_pkt_num || p.nodeIdSequence.empty() || p.nodeIdSequence.front()!=node)continue;
                        bool uses=false;for(size_t j=1;j<p.portSequence.size();++j)uses|=MakeLinkId(p.nodeIdSequence[j],p.portSequence[j])==link;
                        if(uses){if(!contributors.empty())contributors+=';';contributors+=std::to_string(p.pid)+":"+std::to_string(p.sent_pkt_num);}}
                    lcLinkPenaltyLog<<now<<','<<node<<','<<serial<<','<<link<<','<<known<<','<<base/10<<','<<delta/10<<','<<(base-delta)/10<<','<<contributors<<'\n';
                }
                for(size_t i=0;i<n;++i){auto c=m_earlyProbes.find(paths[i]->pid);double saved=c==m_earlyProbes.end()?std::numeric_limits<double>::quiet_NaN():c->second.lastBudgetQ;
                    lcPathPenaltyLog<<now<<','<<node<<','<<serial<<','<<paths[i]->pid<<','<<fresh[i]<<','<<cached[i]<<','<<unknown[i]<<','<<status.blocked[i]<<','
                        <<paths[i]->sent_pkt_num<<','<<feedbackBudget[i]/10<<','<<budget[i]/10<<','<<saved/10<<','<<maxLocalPenalty[i]/10<<','<<signedMinimum[i]/10<<','<<grant[i]/10<<'\n';}
            }
            for(size_t i=0;i<n;++i)lcBwExpLog<<now<<','<<node<<','<<paths[i]->pid<<','<<fresh[i]<<','<<cached[i]<<','<<unknown[i]<<','<<status.blocked[i]<<','
                <<budget[i]/10.0<<','<<factor[i]<<','<<lcBwBeta<<','<<lcBwUnitGbps<<','<<score[i]<<','<<raw[i]<<','<<weights[i]<<",0,"<<signedMinimum[i]/10.0<<','<<grant[i]/10.0<<','<<CompactLinkState::queueScoreEnabled<<'\n';
        }
    }
    if(lcQuotaAudit){auto &a=lcQuotaStats[node];++a.evaluations;if(freshCount+cachedCount)++a.applied;else ++a.unknown;}
    return true;
}
bool RdmaSmartFlowRouting::RunPureBandwidthSelfChecks(){
    bool ok=true;unsigned count=0;auto ck=[&](bool v,const char *name){++count;if(!v){ok=false;std::cerr<<"PURE_BW_CASE_FAIL "<<name<<std::endl;}};
    auto near=[](double a,double b){return std::abs(a-b)<1e-10;};
    lcEnabled=true;lcBwMode=2;lcBwCc=true;lcBwAudit=false;lcQuotaAudit=false;lcEarlyProbe=false;lcPathTimeout=false;
    lcBwBeta=5;lcBwUnitGbps=100;lcZeroBwMinProbability=.05;lcTtlNs=100000;lcMaxLinkQueue=true;lcPathOnlyQueue=true;
    lcPathQueueMax=true;lcDisablePruning=true;lcAlpsProbe=true;lcSendPenalty=false;
    auto r=CreateObject<RdmaSmartFlowRouting>();r->m_nodeId=1000;
    PathData a,b,c;std::vector<PathData*>paths={&a,&b,&c};
    for(size_t i=0;i<3;++i){paths[i]->pid=i+1;paths[i]->nodeIdSequence={1000,uint32_t(i+1),9,99};paths[i]->portSequence={1,1,1};
        paths[i]->latency=(i+1)*1000;paths[i]->theoreticalSmallestLatencyInNs=(i+1)*10000;r->m_nexthopSelTbl[i+1]=*paths[i];}
    auto load=[&](uint32_t node,uint16_t q){auto &s=r->m_linkStateTable[MakeLinkId(node,1)];s.availableQ=q;s.sampleEpoch=0;};
    auto run=[&](std::vector<double> w){ck(r->ApplyBottleneckBandwidthWeights(paths,w),"handled_by_bandwidth");return w;};
    load(1,500);load(2,300);load(3,200);load(9,400);
    SharedQuotaResult quota;ck(r->ComputeSharedQuota(paths,{true,true,true},0,quota),"shared_quota_feasible");
    ck(near(quota.budgetQ[0],200)&&near(quota.budgetQ[1],120)&&near(quota.budgetQ[2],80),"shared_budget_20_12_8");
    auto w=run({.7,.2,.1});double den=std::exp(1)+std::exp(.6)+std::exp(.4);
    ck(near(w[0],std::exp(1)/den)&&near(w[1],std::exp(.6)/den)&&near(w[2],std::exp(.4)/den),"positive_bandwidth_exponential_exact");
    auto before=w;w=run({0,0,0});ck(w==before,"zero_input_vector_not_a_mask_or_delay_prior");
    w=run({.01,.98,.01});ck(w==before,"incoming_probability_independent");
    for(auto p:paths){p->latency=900000000;p->theoreticalSmallestLatencyInNs=1;p->sent_pkt_num=1000000000;}
    w=run({.99,.005,.005});ck(w==before,"latency_reference_and_send_count_independent");
    auto guard=r->CalPathWeightBasedOnDelay(paths);ck(guard==before,"legacy_delay_api_bypasses_to_bandwidth");
    r->m_linkStateTable.clear();w=run({.99,.005,.005});ck(w==before,"cached_budget_not_cached_probability");
    auto &probe=r->m_earlyProbes[1];probe.pending=true;w=run({1,0,0});
    ck(near(w[0],.5*std::exp(1)/(.5*std::exp(1)+std::exp(.6)+std::exp(.4))),"pending_probe_half_confidence");
    w=run({0,1,0});ck(near(w[0],.5*std::exp(1)/(.5*std::exp(1)+std::exp(.6)+std::exp(.4))),"probe_discount_not_compounded_in_cache");probe.pending=false;
    r->m_earlyProbes.clear();w=run({.99,.005,.005});ck(near(w[0],1.0/3)&&near(w[1],1.0/3),"cold_start_zero_prior_uniform");
    load(1,0);load(2,0);load(3,0);load(9,0);w=run({.99,.005,.005});
    ck(near(w[0],1.0/3)&&near(w[1],1.0/3),"valid_all_zero_natural_softmax");
    ck(r->m_earlyProbes[1].lastBudgetQ==0,"valid_zero_replaces_positive_history");
    r->m_linkStateTable.clear();r->m_earlyProbes.clear();
    r->m_earlyProbes[1].lastBudgetQ=1000;w=run({.01,.98,.01});
    ck(near(w[0],std::exp(5)/(std::exp(5)+2))&&near(w[1],1/(std::exp(5)+2)),"mixed_known_unknown_zero_prior");
    r->m_earlyProbes.clear();lcBwMode=1;load(1,100);load(2,200);load(3,300);load(9,1000);w=run({1,0,0});
    den=std::exp(.5)+std::exp(1)+std::exp(1.5);ck(near(w[0],std::exp(.5)/den)&&near(w[2],std::exp(1.5)/den),"mode1_also_exponential");
    lcBwBeta=0;w=run({1,0,0});ck(near(w[0],1.0/3),"beta_zero_uniform");
    lcBwBeta=5;lcBwUnitGbps=10;w=run({1,0,0});den=std::exp(5)+std::exp(10)+std::exp(15);
    ck(near(w[2],std::exp(15)/den),"explicit_bandwidth_unit");
    lcBwBeta=1000000;lcBwUnitGbps=100;w=run({1,0,0});ck(std::isfinite(w[0])&&near(w[2],1),"large_exponent_stable_normalization");
    lcBwBeta=5;lcZeroBwMinProbability=.05;load(1,0);load(2,1000);load(3,1000);w=run({1,0,0});ck(near(w[0],.05),"known_zero_floor_preserved");
    lcPathTimeout=true;r->m_earlyProbes[1].unavailable=true;w=run({1,0,0});ck(w[0]==0&&near(w[1]+w[2],1),"protocol_prune_overrides_floor");
    r->m_earlyProbes[2].unavailable=r->m_earlyProbes[3].unavailable=true;w=run({1,0,0});ck(w==std::vector<double>({0,0,0}),"all_unavailable_no_revival");
    lcPathTimeout=false;lcBwMode=2;r->m_linkStateTable.clear();r->m_earlyProbes.clear();
    r->m_earlyProbes[1].lastBudgetQ=400;r->m_earlyProbes[2].lastBudgetQ=300;
    std::function<void()> expired=[&](){auto v=run({.01,.98,.01});double d=std::exp(2)+std::exp(1.5)+1;
        ck(near(v[0],std::exp(2)/d)&&near(v[2],1/d),"expired_observation_uses_budget_or_zero_not_delay");};
    auto invoke=+[](std::function<void()> *f){(*f)();};Simulator::Schedule(NanoSeconds(100004),invoke,&expired);Simulator::Run();Simulator::Destroy();
    lcEnabled=false;lcBwCc=false;lcBwMode=0;lcDisablePruning=false;
    std::cout<<(ok?"PURE_BANDWIDTH_SELF_CHECK_PASS":"PURE_BANDWIDTH_SELF_CHECK_FAIL")<<" assertions="<<count<<" latency_weight_fallback=0 bandwidth_unit_gbps=100 beta=5"<<std::endl;return ok;
}


bool RdmaSmartFlowRouting::RunCapacityBaselineSelfChecks(){
    lcEnabled=true;lcBwCc=true;lcEarlyProbe=true;lcEarlyAudit=false;lcCapacityBaselineProbe=true;
    lcMaxLinkQueue=true;lcPathOnlyQueue=true;lcPathQueueMax=true;lcWindowNs=5000;lcTtlNs=100000;
    lcEarlyIntervalNs=1000;lcEarlyTimeoutNs=1000000;lcQueueAlarmBytes=100000;
    auto src=CreateObject<RdmaSmartFlowRouting>();src->m_nodeId=0;
    PathData a,b;a.pid=1;b.pid=2;a.nodeIdSequence={100,1,9,99};b.nodeIdSequence={100,2,9,99};
    a.portSequence=b.portSequence={1,1,1};a.latency=b.latency=999999;
    a.theoreticalSmallestLatencyInNs=20000;b.theoreticalSmallestLatencyInNs=10000;
    std::map<uint32_t,PathData> pit={{1,a},{2,b}};src->set_PIT(pit);std::vector<PathData*> paths={src->lookup_PIT(1),src->lookup_PIT(2)};
    bool ok=true;unsigned checks=0;auto ck=[&](bool value,const char *name){++checks;if(!value){ok=false;std::cout<<"CAPACITY_BASELINE_CASE_FAIL "<<name<<'\n';}};
    auto packet=[&](uint32_t pid,uint64_t born,bool partial=false,bool invalid=false){
        LinkCapacityTag tag;tag.bornNs=born;tag.maxQueueBytes=0;auto p=src->lookup_PIT(pid);
        tag.maxQueueLinkId=MakeLinkId(p->nodeIdSequence[1],p->portSequence[1]);
        for(size_t j=1;j<p->portSequence.size();++j){LinkCapacityRecord v;v.linkId=MakeLinkId(p->nodeIdSequence[j],p->portSequence[j]);v.state.availableQ=invalid?0xffff:100;v.state.sampleEpoch=born/4;tag.Append(v);}
        if(partial)--tag.count;auto out=Create<Packet>();AckPathTag ack;ack.SetPathId(pid);out->AddPacketTag(ack);out->AddPacketTag(tag);return out;
    };
    auto receive=[&](uint64_t now,Ptr<Packet> p){std::function<void()> event=[&](){src->ReceiveLinkCapacityFeedback(p);};auto invoke=+[](std::function<void()> *f){(*f)();};Simulator::Schedule(NanoSeconds(now-Simulator::Now().GetNanoSeconds()),invoke,&event);Simulator::Stop(NanoSeconds(now-Simulator::Now().GetNanoSeconds()+1));Simulator::Run();};
    auto first=packet(1,8000);receive(10000,first);ck(src->m_earlyProbes[1].lastCapacityUpdateNs==10000,"data_ack_complete_updates_own_clock");
    receive(12000,packet(2,11000));ck(src->m_earlyProbes[1].lastCapacityUpdateNs==10000,"shared_link_other_ack_does_not_reset_clock");
    ck(!src->SelectEarlyProbe({paths[1]},{1},32000),"at_two_reference_not_due");
    ck(src->SelectEarlyProbe({paths[1]},{1},32001)==paths[1],"own_shorter_reference_due");
    ck(!src->SelectEarlyProbe({paths[0]},{1},50000),"own_longer_reference_at_boundary");
    ck(src->SelectEarlyProbe({paths[0]},{1},50001)==paths[0],"strict_two_baseline_independent_of_latency");
    src->RecordEarlyProbeSent(1,50001);ck(!src->SelectEarlyProbe({paths[0]},{1},60000),"inflight_dedup");
    receive(61000,first);ck(src->m_earlyProbes[1].lastCapacityUpdateNs==10000,"old_duplicate_does_not_renew");
    receive(62000,packet(1,61000,true));ck(src->m_earlyProbes[1].lastCapacityUpdateNs==10000,"partial_does_not_renew");
    receive(63000,packet(1,62000,false,true));ck(src->m_earlyProbes[1].lastCapacityUpdateNs==10000,"unknown_capacity_does_not_renew");
    receive(68000,packet(1,67000));ck(src->m_earlyProbes[1].lastCapacityUpdateNs==68000,"probe_ack_complete_updates_clock");
    ck(src->CompleteEarlyProbe(1,50001,17999,68000),"matching_probe_completes");
    ck(!src->SelectEarlyProbe({paths[0]},{1},108000),"new_clock_boundary");
    ck(src->SelectEarlyProbe({paths[0]},{1},108001)==paths[0],"new_clock_due");
    auto cold=CreateObject<RdmaSmartFlowRouting>();cold->m_nodeId=0;std::map<uint32_t,PathData> coldPit={{1,a}};cold->set_PIT(coldPit);
    ck(cold->SelectEarlyProbe({cold->lookup_PIT(1)},{1},0)!=nullptr,"cold_start_probe");
    lcCapacityBaselineProbe=false;lcEarlyProbe=false;lcBwCc=false;lcMaxLinkQueue=false;lcPathOnlyQueue=false;lcPathQueueMax=false;Simulator::Destroy();
    std::cout<<(ok?"CAPACITY_BASELINE_SELF_CHECK_PASS":"CAPACITY_BASELINE_SELF_CHECK_FAIL")<<" assertions="<<checks<<",own_complete_ack,strict_boundary,per_path_baseline,cold,partial,unknown,old,dedup,recovery probe_state_bytes="<<sizeof(EarlyProbeState)<<'\n';return ok;
}
bool RdmaSmartFlowRouting::RunZeroBudgetFloorSelfChecks(){
    bool ok=true;unsigned checks=0;auto ck=[&](bool value,const char *name){++checks;if(!value){ok=false;std::cout<<"ZERO_FLOOR_CASE_FAIL "<<name<<'\n';}};
    auto close=[](double a,double b){return std::fabs(a-b)<1e-9;};
    std::vector<double>w={0,.4,.6};ApplyZeroBudgetFloor(w,{true,false,false},{false,false,false},.05);
    ck(close(w[0],.05)&&close(w[1],.38)&&close(w[2],.57),"one_zero_exact_final_floor");
    w={0,0,1};ApplyZeroBudgetFloor(w,{true,true,false},{false,false,false},.05);ck(close(w[0],.05)&&close(w[1],.05)&&close(w[2],.9),"two_zeros");
    w={0,.2,.8};ApplyZeroBudgetFloor(w,{true,false,false},{true,false,false},.05);ck(w[0]==0&&close(w[1],.2),"prune_wins");
    w={0,.1,.9};ApplyZeroBudgetFloor(w,{false,false,false},{false,false,false},.05);ck(w[0]==0&&close(w[1],.1),"unknown_not_zero");
    w={.2,.8};ApplyZeroBudgetFloor(w,{true,false},{false,false},.05);ck(close(w[0],.2)&&close(w[1],.8),"already_above_floor_unchanged");
    w={1};ApplyZeroBudgetFloor(w,{true},{false},.05);ck(w[0]==1,"single_zero_path");
    w=std::vector<double>(25,.04);double cap=ApplyZeroBudgetFloor(w,std::vector<bool>(25,true),std::vector<bool>(25,false),.05);ck(close(cap,.04)&&close(w[24],.04),"infeasible_floor_explicit_cap");
    lcEnabled=true;lcBwCc=true;lcBwMode=2;lcZeroBwMinProbability=.05;lcMaxLinkQueue=true;lcPathOnlyQueue=true;lcPathQueueMax=true;lcAlpsCongestion=false;
    lcQueueAlarmBytes=100000;lcWindowNs=5000;lcTtlNs=100000;lcBwAudit=false;lcQuotaAudit=false;
    auto src=CreateObject<RdmaSmartFlowRouting>();src->m_nodeId=0;PathData a,b;a.pid=1;b.pid=2;a.nodeIdSequence={100,1,99};b.nodeIdSequence={100,2,99};a.portSequence=b.portSequence={1,1};
    std::map<uint32_t,PathData> pit={{1,a},{2,b}};src->set_PIT(pit);std::vector<PathData*>paths={src->lookup_PIT(1),src->lookup_PIT(2)};
    auto load=[&](uint32_t node,uint16_t q){auto &v=src->m_linkStateTable[MakeLinkId(node,1)];v.availableQ=q;v.sampleEpoch=0;};
    load(1,0);load(2,1000);for(auto p:paths){p->maxEgressQueueBytes=99999;p->queueFeedbackBornNs=0;}
    src->m_earlyProbes[1].pending=true;src->m_earlyProbes[1].sentNs=0;
    w={.5,.5};src->ApplyBottleneckBandwidthWeights(paths,w);ck(close(w[0],.05)&&close(w[1],.95),"real_allocator_floor_after_probe_discount");
    paths[0]->maxEgressQueueBytes=100000;w={.5,.5};src->ApplyBottleneckBandwidthWeights(paths,w);ck(w[0]==0&&close(w[1],1),"real_allocator_queue_100KB_prunes");
    paths[0]->maxEgressQueueBytes=99999;load(1,1000);paths[0]->maxEgressQueueBytes=100000;ck(src->EvaluateBandwidthCongestion(paths,0).blocked[0],"positive_budget_queue_threshold_also_prunes");
    load(1,0);load(2,0);paths[0]->maxEgressQueueBytes=0;src->m_earlyProbes[1].pending=false;w={.5,.5};src->ApplyBottleneckBandwidthWeights(paths,w);ck(close(w[0],.5)&&close(w[1],.5),"all_zero_service");
    lcZeroBwMinProbability=0;lcBwCc=false;lcMaxLinkQueue=false;lcPathOnlyQueue=false;lcPathQueueMax=false;Simulator::Destroy();
    std::cout<<(ok?"ZERO_FLOOR_SELF_CHECK_PASS":"ZERO_FLOOR_SELF_CHECK_FAIL")<<" assertions="<<checks<<",normalization,multiple,probe_order,prune,unknown,all_zero,feasibility\n";return ok;
}

bool RdmaSmartFlowRouting::RunPathLocalBandwidthSelfChecks() {
    lcEnabled=true;lcBwCc=true;lcBwMode=2;lcBwAudit=false;lcQuotaAudit=false;lcTtlNs=100000;
    auto r=CreateObject<RdmaSmartFlowRouting>();r->m_nodeId=1000;
    PathData a,b,c,z;a.pid=1;b.pid=2;c.pid=3;z.pid=4;
    a.nodeIdSequence={1000,1,9,99};a.portSequence={1,1,1};
    b.nodeIdSequence={1000,2,9,99};b.portSequence={2,1,1};
    c.nodeIdSequence={1000,3,8,99};c.portSequence={3,1,1};
    z.nodeIdSequence={1000,4,99};z.portSequence={4,1};
    std::vector<PathData*> paths={&a,&b,&c,&z};
    for(auto p:paths){r->m_nexthopSelTbl[p->pid]=*p;
        for(size_t j=1;j<p->portSequence.size();++j)r->m_linkStateTable[MakeLinkId(p->nodeIdSequence[j],p->portSequence[j])].availableQ=1000;}
    r->m_linkStateTable[MakeLinkId(1,1)].availableQ=500;
    r->m_linkStateTable[MakeLinkId(9,1)].availableQ=600;
    r->m_linkStateTable.erase(MakeLinkId(3,1));r->m_linkStateTable[MakeLinkId(4,1)].availableQ=0x8000;
    auto close=[](double x,double y){return std::abs(x-y)<1e-10;};
    std::vector<double> w={.2,.3,.4,.1};
    if(!r->ApplyBottleneckBandwidthWeights(paths,w) || !close(w[0],5.0/27) || !close(w[1],10.0/27) || !close(w[2],4.0/9) || w[3]!=0)return false;
    std::cout<<"PATH_LOCAL_MIXED_PASS known=0.185185/0.370370 unknown=0.444444 zero=0\n";
    // Incomplete path with an unknown first hop and a known zero later is pruned, not restored by fallback.
    r->m_linkStateTable[MakeLinkId(8,1)].availableQ=0x8000;w={.2,.3,.4,.1};
    if(!r->ApplyBottleneckBandwidthWeights(paths,w) || !close(w[0],1.0/3) || !close(w[1],2.0/3) || w[2]!=0 || w[3]!=0)return false;
    r->m_linkStateTable[MakeLinkId(9,1)].availableQ=0x8000;w={.2,.3,.4,.1};
    if(!r->ApplyBottleneckBandwidthWeights(paths,w) || w!=std::vector<double>({0,0,0,0}))return false;
    r->m_linkStateTable[MakeLinkId(8,1)].availableQ=1000;w={.2,.3,.4,.1};
    if(!r->ApplyBottleneckBandwidthWeights(paths,w) || w!=std::vector<double>({0,0,1,0}))return false;
    // Expired LST values never become a fresh zero alarm; a prior valid path budget may survive.
    auto expired=r->EvaluateBandwidthCongestion(paths,100001);
    if(expired.blockedCount!=0 || expired.knownCount!=0)return false;
    r->m_linkStateTable.clear();w={.2,.3,.4,.1};
    if(!r->ApplyBottleneckBandwidthWeights(paths,w))return false;
    for(size_t i=0;i<4;++i)if(!close(w[i],std::vector<double>({1.0/6,1.0/3,.4,.1})[i]))return false;
    // A genuinely new endpoint has no historic budgets: retain the explicit cold-start policy.
    auto cold=CreateObject<RdmaSmartFlowRouting>();cold->m_nodeId=1001;
    for(auto p:paths)cold->m_nexthopSelTbl[p->pid]=*p;
    w={.2,.3,.4,.1};if(!cold->ApplyBottleneckBandwidthWeights(paths,w))return false;
    for(size_t i=0;i<4;++i)if(!close(w[i],std::vector<double>({.2,.3,.4,.1})[i]))return false;
    lcBwCc=false;lcBwMode=0;Simulator::Destroy();
    std::cout<<"PATH_LOCAL_SELF_CHECK_PASS mixed_shared_quota,zero_before_missing,zero_after_missing,all_zero_no_fallback,only_unknown_survives,expired_history,all_unknown_cold_start\n";return true;
}


bool RdmaSmartFlowRouting::RunQueue90Probe20SelfChecks() {
    lcEnabled=true;lcBwCc=true;lcEarlyProbe=true;lcBwMode=2;lcBwAudit=false;lcQuotaAudit=false;lcTtlNs=100000;
    auto close=[](double a,double b){return std::abs(a-b)<1e-6;};
    if(CompactLinkState::Queue90Word(500,1439999,1600000)!=500 ||
       CompactLinkState::Queue90Word(500,1440000,1600000)!=uint16_t(0x8000|500) ||
       CompactLinkState::Queue90Word(0,0,1600000)!=0 ||
       CompactLinkState::Queue90Word(0xffff,1440000,1600000)!=0xfffe)return false;
    CompactLinkState unknown;unknown.availableQ=0xfffe;if(!unknown.Congested() || unknown.CapacityQ()!=0xffff)return false;
    auto r=CreateObject<RdmaSmartFlowRouting>();r->m_nodeId=1000;
    PathData a,b;a.pid=1;a.nodeIdSequence={1000,1,99};a.portSequence={1,1};
    b.pid=2;b.nodeIdSequence={1000,2,99};b.portSequence={2,1};std::vector<PathData*> paths={&a,&b};
    r->m_nexthopSelTbl[1]=a;r->m_nexthopSelTbl[2]=b;
    auto load=[&](uint32_t id,uint16_t q){auto &s=r->m_linkStateTable[id];s.availableQ=q;s.sampleEpoch=uint16_t(Simulator::Now().GetNanoSeconds()/lcObservationTickNs);};
    uint32_t A=MakeLinkId(1,1),B=MakeLinkId(2,1);load(A,400);load(B,400);bool ok=true;
    std::vector<double> w={.5,.5};r->ApplyBottleneckBandwidthWeights(paths,w);ok&=close(w[0],.5);
    r->RecordEarlyProbeSent(1,0);w={.5,.5};r->ApplyBottleneckBandwidthWeights(paths,w);
    ok&=close(w[0],1.0/6) && close(w[1],5.0/6) && close(r->m_earlyProbes[1].lastBudgetQ,400);
    w={.5,.5};r->ApplyBottleneckBandwidthWeights(paths,w);ok&=close(w[0],1.0/6); // no exponential discount
    r->m_linkStateTable.erase(A);w={.5,.5};r->ApplyBottleneckBandwidthWeights(paths,w);
    ok&=close(w[0],1.0/6) && r->m_linkStateTable.at(B).CapacityQ()==400; // path cache, not global LST rewriting
    std::function<void()> complete=[&](){
        load(A,400);ok&=r->CompleteEarlyProbe(1,0,4,4);w={.5,.5};r->ApplyBottleneckBandwidthWeights(paths,w);ok&=close(w[0],.5);
        r->RecordEarlyProbeSent(1,4);ok&=!r->CompleteEarlyProbe(1,0,8,8) && r->m_earlyProbes[1].pending;
    };
    std::function<void()> invalid=[&](){
        r->m_linkStateTable.erase(A);ok&=r->CompleteEarlyProbe(1,4,4,8) && r->m_earlyProbes[1].awaitingFresh;
        w={.5,.5};r->ApplyBottleneckBandwidthWeights(paths,w);ok&=close(w[0],1.0/6);
        load(A,400);w={.5,.5};r->ApplyBottleneckBandwidthWeights(paths,w);ok&=close(w[0],.5);
        load(A,0xfffe);uint16_t q;bool congested=false;
        ok&=!r->ReadLinkCapacity(A,8,q) && r->ReadLinkCongestion(A,8,congested) && congested;
        auto state=r->EvaluateBandwidthCongestion(paths,8);ok&=state.blocked==std::vector<bool>({true,false});
        load(A,0);load(B,0);w={.5,.5};r->ApplyBottleneckBandwidthWeights(paths,w);ok&=close(w[0],.5) && close(w[1],.5);
    };
    auto invoke=+[](std::function<void()> *f){(*f)();};Simulator::Schedule(NanoSeconds(4),invoke,&complete);Simulator::Schedule(NanoSeconds(8),invoke,&invalid);
    Simulator::Run();Simulator::Destroy();lcBwCc=false;lcEarlyProbe=false;lcBwMode=0;
    std::cout<<(ok?"QUEUE90_PROBE20_SELF_CHECK_PASS":"QUEUE90_PROBE20_SELF_CHECK_FAIL")<<" boundary90,positive_capacity_alarm,unknown_capacity_alarm,probe20,no_repeat_discount,cached_budget,late_ack,invalid_ack,full_zero_service probe_state_bytes="<<sizeof(EarlyProbeState)<<" LST_bytes="<<sizeof(CompactLinkState)<<'\n';return ok;
}

bool RdmaSmartFlowRouting::RunBudgetFallbackSelfChecks() {
    lcEnabled=true;lcBwCc=true;lcEarlyProbe=true;lcBwMode=2;lcBwAudit=true;lcQuotaAudit=false;lcTtlNs=100000;
    auto close=[](double a,double b){return std::abs(a-b)<1e-6;};
    auto r=CreateObject<RdmaSmartFlowRouting>();r->m_nodeId=1000;
    PathData a,b;a.pid=1;a.nodeIdSequence={1000,1,99};a.portSequence={1,1};
    b.pid=2;b.nodeIdSequence={1000,2,99};b.portSequence={2,1};std::vector<PathData*> paths={&a,&b};
    r->m_nexthopSelTbl[1]=a;r->m_nexthopSelTbl[2]=b;
    uint32_t A=MakeLinkId(1,1),B=MakeLinkId(2,1);bool ok=true;
    auto load=[&](uint32_t id,uint16_t q){auto &s=r->m_linkStateTable[id];s.availableQ=q;
        s.sampleEpoch=uint16_t(Simulator::Now().GetNanoSeconds()/lcObservationTickNs);};
    auto apply=[&](double priorA,double priorB,double expectedA){
        std::vector<double> w={priorA,priorB};
        ok&=r->ApplyBottleneckBandwidthWeights(paths,w) && close(w[0],expectedA) && close(w[1],1-expectedA);
    };
    load(A,400);load(B,600);apply(.99,.01,.4);
    ok&=close(r->m_earlyProbes[1].lastBudgetQ,400) && !r->m_earlyProbes[1].pending;
    r->m_linkStateTable.erase(A);load(B,300);
    apply(.99,.01,4.0/7);apply(.01,.99,4.0/7); // history is bandwidth, never a saved probability
    load(B,100);apply(.01,.99,.8); // the denominator uses the other path's CURRENT budget
    load(B,300);r->RecordEarlyProbeSent(1,0);
    apply(.99,.01,4.0/19);apply(.01,.99,4.0/19);
    ok&=close(r->m_earlyProbes[1].lastBudgetQ,400); // no repeated 0.2 compounding
    auto &state=r->m_earlyProbes[1];state.pending=false;state.awaitingFresh=false;
    load(A,200);apply(.99,.01,.4); // current fresh value replaces history
    load(A,0);apply(.99,.01,0);r->m_linkStateTable.erase(A);apply(.99,.01,0); // zero is valid history
    state.lastBudgetQ=400;load(A,uint16_t(0x8000|200));apply(.99,.01,0); // a queue alarm overrides history
    load(A,400);load(B,600);apply(.99,.01,.4);
    std::function<void()> expired=[&](){
        uint64_t now=Simulator::Now().GetNanoSeconds();r->ExpireLinkCapacityEntries(now);
        ok&=r->m_linkStateTable.empty();load(B,300);apply(.01,.99,4.0/7);
        ok&=lcPathLocalStats[1000].cachedOutsideProbe>0;
    };
    auto invoke=+[](std::function<void()> *f){(*f)();};
    Simulator::Schedule(NanoSeconds(100004),invoke,&expired);Simulator::Run();Simulator::Destroy();
    // No historic observation exists at cold start. Keep that separate from a historical-budget fallback.
    auto cold=CreateObject<RdmaSmartFlowRouting>();cold->m_nodeId=1001;cold->m_nexthopSelTbl[1]=a;cold->m_nexthopSelTbl[2]=b;
    std::vector<double> w={.3,.7};cold->ApplyBottleneckBandwidthWeights(paths,w);ok&=close(w[0],.3) && close(w[1],.7);
    std::cout<<"BUDGET_FALLBACK_EXAMPLE history40_fresh30="<<4.0/7<<" probe20="<<4.0/19<<'\n';
    std::cout<<(ok?"BUDGET_FALLBACK_SELF_CHECK_PASS":"BUDGET_FALLBACK_SELF_CHECK_FAIL")
        <<" cached_without_probe,input_probability_independent,current_denominator,no_compounding,fresh_over_history,zero_history,queue_priority,ttl_expiry,cold_start_boundary LST_bytes="<<sizeof(CompactLinkState)<<" probe_state_bytes="<<sizeof(EarlyProbeState)<<'\n';
    lcEnabled=false;lcBwCc=false;lcEarlyProbe=false;lcBwMode=0;lcBwAudit=false;return ok;
}

// BANDWIDTH_WEIGHT_V1: endpoints read only causal LST feedback; no direct switch-state lookup.
bool RdmaSmartFlowRouting::ApplyBottleneckBandwidthWeights(const std::vector<PathData*> &paths,std::vector<double> &weights) {
    if(lcBwMode)return ApplyPathLocalBandwidthWeights(paths,weights); // BW_EXP_ONLY_V1

    // SMALLTEST_V1 oracle uses configured physical capacities, never switch runtime state.
    // It deliberately retains ALPS transport, pruning and probe behavior.
    if(lcSmalltestFixedBranchProbability>=0) {
        NS_ABORT_MSG_IF(paths.size()!=2 || weights.size()!=2,"Smalltest oracle requires two paths");
        bool branchSeen[2]={false,false};
        for(size_t i=0;i<paths.size();++i) {
            bool a=std::find(paths[i]->nodeIdSequence.begin(),paths[i]->nodeIdSequence.end(),1)!=paths[i]->nodeIdSequence.end();
            bool b=std::find(paths[i]->nodeIdSequence.begin(),paths[i]->nodeIdSequence.end(),2)!=paths[i]->nodeIdSequence.end();
            NS_ABORT_MSG_IF(a==b,"Unexpected smalltest path");branchSeen[a?0:1]=true;
        }
        NS_ABORT_MSG_IF(!branchSeen[0] || !branchSeen[1],"Missing smalltest branch");
        double sum=0;
        for(size_t i=0;i<paths.size();++i)if(weights[i]>0) {
            bool a=std::find(paths[i]->nodeIdSequence.begin(),paths[i]->nodeIdSequence.end(),1)!=paths[i]->nodeIdSequence.end();
            weights[i]=a?lcSmalltestFixedBranchProbability:1-lcSmalltestFixedBranchProbability;sum+=weights[i];
        }
        if(sum>0)for(double &w:weights)w/=sum;
        return sum>0;
    }

    // BW_EXP_ONLY_V1: every bandwidth mode owns its complete vector, including cold start and all-zero.
    return false; // ALPS baseline mode leaves its own vector unchanged.
}

void RdmaSmartFlowRouting::AuditBandwidthPathChoice(const std::vector<PathData*> &paths,
        const std::vector<double> &weights,uint32_t selected,uint32_t bytes) {
    if(!lcBwAudit)return;
    uint32_t node=m_node?m_node->GetId():m_nodeId;
    for(size_t i=0;i<paths.size();++i) {
        auto &a=lcBwPaths[(uint64_t(node)<<32)|paths[i]->pid];++a.visits;a.probabilitySum+=weights[i];
    }
    uint64_t key=(uint64_t(node)<<32)|paths[selected]->pid;auto &a=lcBwPaths[key];++a.dataPackets;a.dataBytes+=bytes;
    auto &bucket=lcBwTimeseries[std::make_pair(uint64_t(Simulator::Now().GetNanoSeconds()/20000),key)];
    ++bucket.packets;bucket.bytes+=bytes;
}
void RdmaSmartFlowRouting::WriteBandwidthAudit(const std::string &directory) {
    if(lcBwCcAudit && lcMaxLinkQueue){std::ofstream f(directory+"/maxlink-queue-summary.csv");f<<"node,path_checks,blocked,shared_blocked,unknown,reports,rejected\n";
        for(const auto &e:lcMaxLinkQueueStats){const auto &a=e.second;f<<e.first<<','<<a.paths<<','<<a.blocked<<','<<a.sharedBlocked<<','<<a.unknown<<','<<a.reports<<','<<a.rejected<<'\n';}
        lcMaxLinkQueueHopLog.flush();lcMaxLinkQueueFeedbackLog.flush();lcMaxLinkQueuePathLog.flush();if(lcPathOnlyQueueLog)lcPathOnlyQueueLog.flush();}
    if(lcBwCcAudit && lcAlpsCongestion){std::ofstream f(directory+"/alps-congestion-summary.csv");f<<"node,calls,path_checks,known,blocked,unknown\n";
        for(const auto &e:lcAlpsCongestionStats){const auto &a=e.second;f<<e.first<<','<<a.calls<<','<<a.paths<<','<<a.known<<','<<a.blocked<<','<<a.unknown<<'\n';}lcAlpsCongestionLog.flush();}

    if(lcBwCcAudit && lcRequireZeroBw){std::ofstream joint(directory+"/joint-condition-summary.csv");
        joint<<"node,path_checks,queue_high,observed_zero,blocked,queue_without_zero,zero_without_queue,incomplete_capacity,missing_queue\n";
        for(const auto &e:lcJointQueueStats){const auto &a=e.second;joint<<e.first<<','<<a.paths<<','<<a.queueHigh<<','<<a.observedZero<<','<<a.blocked<<','<<a.queueWithoutZero<<','<<a.zeroWithoutQueue<<','<<a.incompleteCapacity<<','<<a.missingQueue<<'\n';}
    }

    if(!lcBwAudit)return;
    if(lcBwCc || lcAllocationOnly) {
        std::ofstream local(directory+"/path-local-summary.csv");
        local<<"node,decisions,fresh_path_visits,unknown_path_visits,queue_blocked_path_visits,mixed_decisions,all_blocked_decisions,invalid_projection,mean_reserved_unknown_mass,cached_path_visits,discounted_path_visits,legacy_zero_budget_fallback_unused,cached_without_probe_visits\n";
        for(const auto &e:lcPathLocalStats){const auto &a=e.second;local<<e.first<<','<<a.decisions<<','<<a.knownPaths<<','<<a.unknownPaths<<','<<a.zeroPaths<<','<<a.mixed<<','<<a.allZero<<','<<a.invalidProjection<<','<<(a.decisions?double(a.reservedUnknownMass/a.decisions):0)<<','<<a.cachedPaths<<','<<a.discountedPaths<<','<<a.zeroBudgetFallback<<','<<a.cachedOutsideProbe<<'\n';}
    }
    std::ofstream decisions(directory+"/bw-decisions.csv"),paths(directory+"/bw-path-usage.csv"),series(directory+"/bw-path-timeseries.csv");
    NS_ABORT_MSG_IF(!decisions || !paths || !series,"Cannot write bandwidth allocation audits");
    decisions << "node,evaluations,baseline,applied,unknown_fallback,all_zero_fallback,no_eligible,equal_positive_bottlenecks\n";
    for(const auto &e:lcBwDecisions) {
        const auto &a=e.second;decisions << e.first << ',' << a.evaluations << ',' << a.baseline << ',' << a.applied
            << ',' << a.unknown << ',' << a.allZero << ',' << a.noEligible << ',' << a.equalPositive << '\n';
    }
    paths << "node,path,visits,eligible_visits,known_eligible_visits,zero_eligible_visits,mean_bottleneck_gbps,mean_target_probability,data_packets,data_packet_bytes\n";
    paths << std::setprecision(17);
    for(const auto &e:lcBwPaths) {
        const auto &a=e.second;paths << uint32_t(e.first>>32) << ',' << uint32_t(e.first) << ',' << a.visits
            << ',' << a.eligible << ',' << a.known << ',' << a.zero << ',' << (a.known?double(a.bottleneckQSum/a.known/10):0)
            << ',' << (a.visits?double(a.probabilitySum/a.visits):0) << ',' << a.dataPackets << ',' << a.dataBytes << '\n';
    }
    series << "bucket_start_ns,node,path,data_packets,data_packet_bytes\n";
    for(const auto &e:lcBwTimeseries)series << e.first.first*20000 << ',' << uint32_t(e.first.second>>32) << ','
        << uint32_t(e.first.second) << ',' << e.second.packets << ',' << e.second.bytes << '\n';
}
bool RdmaSmartFlowRouting::RunBandwidthWeightSelfChecks() {
    lcEnabled=true;lcBwMode=1;lcBwAudit=false;lcTtlNs=100000;
    Ptr<RdmaSmartFlowRouting> r=CreateObject<RdmaSmartFlowRouting>();r->m_nodeId=1000;
    PathData a,b,c;a.pid=1;b.pid=2;c.pid=3;std::vector<PathData*> paths={&a,&b,&c};
    for(unsigned i=0;i<3;++i) {
        paths[i]->nodeIdSequence={1000,i+1,99};paths[i]->portSequence={1,1};
        r->m_nexthopSelTbl[i+1]=*paths[i];r->m_linkStateTable[MakeLinkId(i+1,1)].availableQ=(i+1)*100;
    }
    auto close=[](double a,double b){return std::abs(a-b)<1e-12;};
    std::vector<double> w={.7,.2,.1};
    if(!r->ApplyBottleneckBandwidthWeights(paths,w) || !close(w[0],1.0/6) || !close(w[1],2.0/6) || !close(w[2],.5))return false;
    w={0,.5,.5};r->m_linkStateTable.erase(MakeLinkId(1,1));
    if(!r->ApplyBottleneckBandwidthWeights(paths,w) || w[0]!=0 || !close(w[1],.4) || !close(w[2],.6))return false;
    w={.7,.2,.1};if(r->ApplyBottleneckBandwidthWeights(paths,w) || w!=std::vector<double>({.7,.2,.1}))return false;
    for(unsigned i=1;i<=3;++i)r->m_linkStateTable[MakeLinkId(i,1)].availableQ=0;
    if(r->ApplyBottleneckBandwidthWeights(paths,w) || w!=std::vector<double>({.7,.2,.1}))return false;
    r->m_linkStateTable[MakeLinkId(2,1)].availableQ=200;r->m_linkStateTable[MakeLinkId(3,1)].availableQ=300;
    if(!r->ApplyBottleneckBandwidthWeights(paths,w) || w[0]!=0 || !close(w[1],.4))return false;
    for(unsigned i=0;i<3;++i) {
        paths[i]->nodeIdSequence={1000,i+1,4,99};paths[i]->portSequence={1,1,1};r->m_nexthopSelTbl[i+1]=*paths[i];
        r->m_linkStateTable[MakeLinkId(i+1,1)].availableQ=(i+1)*100;
    }
    r->m_linkStateTable[MakeLinkId(4,1)].availableQ=50;w={.7,.2,.1};
    if(!r->ApplyBottleneckBandwidthWeights(paths,w) || !close(w[0],1.0/3) || !close(w[1],1.0/3))return false;
    lcBwMode=0;w={.7,.2,.1};if(r->ApplyBottleneckBandwidthWeights(paths,w) || w!=std::vector<double>({.7,.2,.1}))return false;
    lcBwMode=1;w={0,0,0};if(r->ApplyBottleneckBandwidthWeights(paths,w) || w!=std::vector<double>({0,0,0}))return false;
    lcBwMode=0;Simulator::Destroy();
    std::cout << "BANDWIDTH_WEIGHT_SELF_CHECK_PASS proportional zero_mask unknown all_zero common_bottleneck mode_off no_eligible\n";
    return true;
}

bool RdmaSmartFlowRouting::RunLinkPruningSelfChecks() {
    lcEnabled=true;lcPruneEnabled=true;lcWindowNs=5000;lcTtlNs=100000;
    Ptr<RdmaSmartFlowRouting> r=CreateObject<RdmaSmartFlowRouting>();r->m_nodeId=10;
    PathData a,b,c;a.pid=0;a.nodeIdSequence={10,0,1,3,4,11};a.portSequence={1,2,2,3,1};
    b=a;b.pid=1;b.nodeIdSequence={10,0,2,3,4,11};b.portSequence={1,3,2,3,1};
    c.pid=2;c.nodeIdSequence={10,0,5,6,7,4,11};c.portSequence={1,4,2,2,2,1};
    std::vector<PathData*> paths={&a,&b,&c};std::vector<double> w={.2,.3,.5};
    uint32_t shared=MakeLinkId(3,3),common=MakeLinkId(4,1);
    r->m_linkStateTable[shared].availableQ=0; // Saturated alone must not prune.
    if(r->ApplyLinkCongestionPruning(paths,w) || w!=std::vector<double>({.2,.3,.5}))return false;
    r->m_linkStateTable[shared].availableQ=0x8000;
    if(!r->ApplyLinkCongestionPruning(paths,w) || w!=std::vector<double>({0,0,1}))return false;
    r->m_linkStateTable[common].availableQ=0x8000;w={.2,.3,.5};
    if(r->ApplyLinkCongestionPruning(paths,w) || w!=std::vector<double>({.2,.3,.5}))return false;
    r->m_linkStateTable[shared].availableQ=0xffff;r->m_linkStateTable[common].availableQ=20;
    if(r->ApplyLinkCongestionPruning(paths,w))return false; // Invalid is not congested.
    r->m_linkStateTable[shared].availableQ=0x8000;r->ExpireLinkCapacityEntries(105000);
    if(r->ApplyLinkCongestionPruning(paths,w))return false; // Stale flags cannot prune.
    lcPruneEnabled=false;Simulator::Destroy();
    std::cout << "LINK_PRUNING_SELF_CHECK_PASS zero_only shared_mask recovery_ttl all_blocked_fallback\n";
    return true;
}

bool RdmaSmartFlowRouting::RunLinkCapacitySelfChecks() {
    lcWindowNs=10000;lcTtlNs=30000;
    Ptr<RdmaSmartFlowRouting> r=CreateObject<RdmaSmartFlowRouting>();r->m_nodeId=100;
    PathData p;p.pid=1;p.nodeIdSequence={100,1,3,2};p.portSequence={1,2,4};r->m_nexthopSelTbl[1]=p;
    p.pid=2;p.nodeIdSequence={100,5,3,2};r->m_nexthopSelTbl[2]=p;
    LinkCapacityRecord a;a.linkId=MakeLinkId(3,4);a.state.sampleEpoch=uint16_t(100000/lcObservationTickNs);a.state.availableQ=0;
    uint16_t q=999;
    if(r->ReadLinkCapacity(a.linkId,100000,q))return false;
    if(!r->AcceptLinkCapacity(a,1,100000) || !r->ReadLinkCapacity(a.linkId,100000,q) || q!=0)return false;
    if(r->AcceptLinkCapacity(a,2,100001) || r->m_linkStateTable.size()!=1)return false;
    a.state.availableQ=12;if(r->AcceptLinkCapacity(a,1,100002))return false;
    a.state.sampleEpoch=uint16_t(99996/lcObservationTickNs);if(r->AcceptLinkCapacity(a,1,100003))return false;
    a.state.sampleEpoch=uint16_t(110000/lcObservationTickNs);if(!r->AcceptLinkCapacity(a,2,110000))return false;
    a.state.sampleEpoch=uint16_t(120000/lcObservationTickNs);a.state.availableQ=0xffff;
    if(!r->AcceptLinkCapacity(a,1,120000) || r->ReadLinkCapacity(a.linkId,120000,q))return false;
    a.state.sampleEpoch=uint16_t(110000/lcObservationTickNs);a.state.availableQ=20;if(r->AcceptLinkCapacity(a,1,120001))return false;
    r->ExpireLinkCapacityEntries(160000);if(!r->m_linkStateTable.empty())return false;
    if(r->AcceptLinkCapacity(a,1,160000))return false;
    a.state.sampleEpoch=65535;a.state.availableQ=500;
    if(!r->AcceptLinkCapacity(a,1,262140))return false;
    a.state.sampleEpoch=0;a.state.availableQ=400;
    if(!r->AcceptLinkCapacity(a,2,262144) || !r->ReadLinkCapacity(a.linkId,262145,q) || q!=400)return false;
    r->ExpireLinkCapacityEntries(302145);if(!r->m_linkStateTable.empty())return false;
    LinkCapacityTag tag;tag.bornNs=123;tag.Append(a);Ptr<Packet> packet=Create<Packet>(1000);packet->AddPacketTag(tag);
    LinkCapacityTag back;if(!packet->PeekPacketTag(back) || back.count!=1 || back.bornNs!=123 || back.records[0].state.availableQ!=400)return false;
    for(unsigned i=1;i<LinkCapacityTag::MAX_HOPS;++i)if(!tag.Append(a))return false;
    if(tag.Append(a) || !tag.overflow)return false;
    // ACK_IMMEDIATE_V1: flag changes within one estimator period, old feedback, ties and recovery.
    r->m_linkStateTable.clear();a.state.availableQ=0;a.state.sampleEpoch=25000;
    if(!r->AcceptLinkCapacity(a,1,100000))return false;
    a.state.availableQ=0x8000;a.state.sampleEpoch=25001;
    if(!r->AcceptLinkCapacity(a,2,100004) || !r->m_linkStateTable[a.linkId].Congested())return false;
    a.state.availableQ=0;a.state.sampleEpoch=25000;
    if(r->AcceptLinkCapacity(a,1,100008))return false;
    a.state.sampleEpoch=25003;if(!r->AcceptLinkCapacity(a,1,100012))return false;
    a.state.availableQ=0x8000;if(!r->AcceptLinkCapacity(a,2,100012))return false;
    a.state.availableQ=0;if(r->AcceptLinkCapacity(a,1,100013))return false;
    a.state.sampleEpoch=25004;if(!r->AcceptLinkCapacity(a,1,100016) || r->m_linkStateTable[a.linkId].Congested())return false;
    if(CompactLinkState::QueueGuardWord(0,1600000,1600000)!=0 ||
       CompactLinkState::QueueGuardWord(0,1600001,1600000)!=0x8000 ||
       CompactLinkState::QueueGuardWord(1,2000000,1600000)!=1 ||
       CompactLinkState::QueueGuardWord(0xffff,2000000,1600000)!=0xffff)return false;
    std::cout << "LINK_CAPACITY_SELF_CHECKS_PASS unknown,zero,duplicate,conflict,older,shared,invalid,expiry,wrap,tag,overflow\n";
    return true;
}


    RdmaSmartFlowRouting::RdmaSmartFlowRouting() : // m_ipv4(0),
                                                   m_alpha(0.2),
                                                   // Quantizing bits
                                                   m_Q(3),
                                                   // m_C(DataRate("10Gbps")),
                                                   m_agingTime(MilliSeconds(10)),
                                                   m_tdre(MicroSeconds(100)),
                                                   m_probeTimeInterval(PROBE_DEFAULT_INTERVAL_IN_NANOSECOND),
                                                   m_pathExpiredTimeThld(PROBE_PATH_EXPIRED_TIME_IN_NANOSECOND),
                                                   m_pathSelStrategy(PATH_SELECTION_SMALL_LATENCY_FIRST_OPTIMIZED_STRATEGY),
                                                   m_piggybackStrategy(PIGGY_BACK_SMALL_SENT_TIME_FIRST_STRATEGY),
                                                   m_pathSelNum(DEFAULT_PATH_SELECTION_NUM),
                                                   m_piggyLatencyCnt(PIGGY_BACK_DEFAULT_PATH_NUMBER),
                                                   m_enableFeedbackAllPathInfo(false),
                                                   m_enabledAllPacketFeedbackInfo(true)

    {
        NS_LOG_FUNCTION(this);
        m_pathSelTbl.clear();
        m_nexthopSelTbl.clear();
        m_vmVtepMapTbl.clear();
    }
    RdmaSmartFlowRouting::~RdmaSmartFlowRouting()
    {
        m_linkCapacityAgingEvent.Cancel();
        NS_LOG_FUNCTION(this);
    }

    bool cmp_pitEntry_in_increase_order_of_latency(const PathData *lhs, const PathData *rhs)
    {
        return lhs->latency < rhs->latency; // 升序排列
    }

    bool cmp_pitEntry_in_decrease_order_of_priority(const PathData *lhs, const PathData *rhs)
    {
        return lhs->priority > rhs->priority; // 降序排列
    }

    bool cmp_pitEntry_in_increase_order_of_Generation_time(const PathData *lhs, const PathData *rhs)
    {
        return lhs->tsGeneration < rhs->tsGeneration; // 升序排列
    }

    bool cmp_pitEntry_in_increase_order_of_congestion_Degree(const PathData *lhs, const PathData *rhs)
    {
        return lhs->pathDre < rhs->pathDre; // 升序排列
    }

    bool cmp_pitEntry_in_increase_order_of_Sent_time(const PathData *lhs, const PathData *rhs)
    {
        return lhs->tsLatencyLastSend < rhs->tsLatencyLastSend; // 升序排列
    }

    void RdmaSmartFlowRouting::SetSwitchInfo(bool isToR, uint32_t switch_id)
    {
        m_isToR = isToR;
        m_switch_id = switch_id;
        m_nodeId = switch_id;
    }
    void RdmaSmartFlowRouting::SetNode(Ptr<Node> node)
    {
        // m_server_node_id = server_node_id;
        // m_nodeId = server_node_id;
        m_node = node;
    }

    TypeId RdmaSmartFlowRouting::GetTypeId(void)
    {
        static TypeId tid = TypeId("ns3::RdmaSmartFlowRouting")
                                .SetParent<Object>()
                                .SetGroupName("Internet")
                                .AddConstructor<RdmaSmartFlowRouting>()
                                .AddAttribute("probeTimeInterval", " Probe can be send only exceeds this time interval",
                                              UintegerValue(PROBE_DEFAULT_INTERVAL_IN_NANOSECOND),
                                              MakeUintegerAccessor(&RdmaSmartFlowRouting::m_probeTimeInterval),
                                              MakeUintegerChecker<uint32_t>())
                                .AddAttribute("pathExpiredTimeThld", " Probe can be send only exceeds this time interval",
                                              UintegerValue(PROBE_PATH_EXPIRED_TIME_IN_NANOSECOND),
                                              MakeUintegerAccessor(&RdmaSmartFlowRouting::m_pathExpiredTimeThld),
                                              MakeUintegerChecker<uint32_t>())
                                .AddAttribute("pathSelStrategy", " how to select the path based on the latency",
                                              UintegerValue(PATH_SELECTION_SMALL_LATENCY_FIRST_OPTIMIZED_STRATEGY),
                                              MakeUintegerAccessor(&RdmaSmartFlowRouting::m_pathSelStrategy),
                                              MakeUintegerChecker<uint32_t>())
                                .AddAttribute("reorderFlag", " reorderFlag or not",
                                              UintegerValue(0),
                                              MakeUintegerAccessor(&RdmaSmartFlowRouting::m_reorderFlag),
                                              MakeUintegerChecker<uint32_t>())
                                .AddAttribute("piggybackStrategy", " how to take away the piggyback data",
                                              UintegerValue(PIGGY_BACK_SMALL_SENT_TIME_FIRST_STRATEGY),
                                              MakeUintegerAccessor(&RdmaSmartFlowRouting::m_piggybackStrategy),
                                              MakeUintegerChecker<uint32_t>())
                                .AddAttribute("probePathStrategy", " how to select the path to probe",
                                              UintegerValue(PROBE_SMALL_LATENCY_FIRST_STRATEGY),
                                              MakeUintegerAccessor(&RdmaSmartFlowRouting::m_probeStrategy),
                                              MakeUintegerChecker<uint32_t>())
                                .AddAttribute("flowletTimoutInNs", " flowlet Timout In Ns",
                                              UintegerValue(FLOWLET_DEFAULT_TIMEOUT_IN_NANOSECOND),
                                              MakeUintegerAccessor(&RdmaSmartFlowRouting::m_flowletTimoutInNs),
                                              MakeUintegerChecker<uint32_t>())
                                .AddAttribute("pathSelNum", " path selection number",
                                              UintegerValue(DEFAULT_PATH_SELECTION_NUM),
                                              MakeUintegerAccessor(&RdmaSmartFlowRouting::m_pathSelNum),
                                              MakeUintegerChecker<uint32_t>())
                                .AddAttribute("piggyLatencyCnt", " piggy back Latency count",
                                              UintegerValue(PIGGY_BACK_DEFAULT_PATH_NUMBER),
                                              MakeUintegerAccessor(&RdmaSmartFlowRouting::m_piggyLatencyCnt),
                                              MakeUintegerChecker<uint32_t>())
                                .AddAttribute("enableFeedbackAllPathInfo", " piggy Feed back all path info ",
                                              BooleanValue(false),
                                              MakeBooleanAccessor(&RdmaSmartFlowRouting::m_enableFeedbackAllPathInfo),
                                              MakeBooleanChecker())
                                .AddAttribute("enabledAllPacketFeedbackInfo",
                                              "all kind of packet feed back path info ",
                                              BooleanValue(true),
                                              MakeBooleanAccessor(&RdmaSmartFlowRouting::m_enabledAllPacketFeedbackInfo),
                                              MakeBooleanChecker())
                                .AddAttribute("enabledE2ELb",
                                              "lb is installed server",
                                              BooleanValue(false),
                                              MakeBooleanAccessor(&RdmaSmartFlowRouting::lb_isInstallSever),
                                              MakeBooleanChecker());

        return tid;
    }
    std::string RdmaSmartFlowRouting::ipv4Address2string(Ipv4Address addr)
    {
        std::ostringstream os;
        addr.Print(os);
        std::string addrStr = os.str();
        return addrStr;
    }
    hostIp2SMT_entry_t *RdmaSmartFlowRouting::lookup_SMT(const Ipv4Address &serverAddr)
    {
        // NS_LOG_INFO ("############ Fucntion: lookup_VMT() ############");
        // NS_LOG_INFO("VMT_KEY: svAddr=" << serverAddr);
        // hostIp2SMT_entry_t vmtEntry;

        std::map<Ipv4Address, hostIp2SMT_entry_t>::iterator it;
        it = m_vmVtepMapTbl.find(serverAddr);
        if (it == m_vmVtepMapTbl.end())
        {
            std::cout << "Error in lookup_SMT() since Cannot match any entry in SMT for the Key: (";
            std::cout << ipv4Address2string(serverAddr);
            std::cout << std::endl;
            return 0;
        }
        else
        {
            return &(it->second);
        }
    }
    std::string RdmaSmartFlowRouting::construct_target_string_strlen(uint32_t strLen, std::string c)
    {
        std::string result;
        for (uint32_t i = 0; i < strLen; ++i)
        {
            result = result + c;
        }
        return result;
    }

    uint32_t RdmaSmartFlowRouting::print_PIT()
    {
        uint32_t pitSize = m_nexthopSelTbl.size();
        uint32_t nodeID = m_switch_id;
        std::cout << "Node " << nodeID << " has a PIT of " << pitSize << " entries" << std::endl;
        std::cout << "Index" << construct_target_string_strlen(2, " ");
        std::cout << "Pid" << construct_target_string_strlen(3, " ");
        std::cout << "Priority" << construct_target_string_strlen(2, " ");
        std::cout << "Latency" << construct_target_string_strlen(3, " ");
        std::cout << "GenTime" << construct_target_string_strlen(3, " ");
        std::cout << "PbnTime" << construct_target_string_strlen(3, " ");
        std::cout << "SntTime" << construct_target_string_strlen(3, " ");
        std::cout << "NodeID" << construct_target_string_strlen(20, " ");
        std::cout << "PortID" << construct_target_string_strlen(1, " ");
        std::cout << std::endl;
        uint32_t pathIdx = 0;
        std::map<uint32_t, PathData>::iterator it;
        for (it = m_nexthopSelTbl.begin(); it != m_nexthopSelTbl.end(); it++)
        {
            std::cout << pathIdx << construct_target_string_strlen(5 + 2 - change2string(pathIdx).size(), " ");
            PathData *pstEntry = &(it->second);
            uint32_t pid = pstEntry->pid;
            std::cout << pid << construct_target_string_strlen(3 + 3 - change2string(pid).size(), " ");
            uint32_t priority = pstEntry->priority;
            std::cout << priority << construct_target_string_strlen(8 + 2 - change2string(priority).size(), " ");
            uint32_t latency = pstEntry->latency;
            std::cout << latency << construct_target_string_strlen(7 + 3 - change2string(latency).size(), " ");
            uint32_t GenTime = pstEntry->tsGeneration.GetNanoSeconds();
            std::cout << GenTime << construct_target_string_strlen(7 + 3 - change2string(GenTime).size(), " ");
            uint32_t PbnTime = pstEntry->tsProbeLastSend.GetNanoSeconds();
            std::cout << PbnTime << construct_target_string_strlen(7 + 3 - change2string(PbnTime).size(), " ");
            uint32_t SntTime = pstEntry->tsLatencyLastSend.GetNanoSeconds();
            std::cout << SntTime << construct_target_string_strlen(7 + 3 - change2string(SntTime).size(), " ");
            std::string nodeIdsStr = vectorTostring<uint32_t>(pstEntry->nodeIdSequence);
            std::cout << nodeIdsStr << construct_target_string_strlen(20 + 6 - change2string(nodeIdsStr).size(), " ");
            std::string portsStr = vectorTostring<uint32_t>(pstEntry->portSequence);
            std::cout << portsStr << construct_target_string_strlen(1, " ");
            std::cout << std::endl;
            pathIdx = pathIdx + 1;
        }
        return pathIdx;
    }

    uint32_t RdmaSmartFlowRouting::print_PST()
    {
        uint32_t nodeID = m_node->GetId();
        uint32_t pstSize = m_pathSelTbl.size();
        std::cout << "Node: **" << nodeID << "** has PST in smartFlow with **" << pstSize << "** entries" << std::endl;
        std::cout << "Index" << construct_target_string_strlen(5, " ");
        std::cout << "(SrcId, DstId)" << construct_target_string_strlen(5, " ");
        std::cout << "PathNum" << construct_target_string_strlen(5, " ");
        std::cout << "LastIdx" << construct_target_string_strlen(2, " ");
        std::cout << "highestIdx" << construct_target_string_strlen(2, " ");
        ;
        std::cout << "PathIDs" << construct_target_string_strlen(5, " ");
        ;
        std::cout << std::endl;
        uint32_t entryCnt = 0;
        std::map<HostId2PathSeleKey, pstEntryData>::const_iterator it;
        for (it = m_pathSelTbl.begin(); it != m_pathSelTbl.end(); it++)
        {
            std::cout << entryCnt << construct_target_string_strlen(5 + 5 - change2string(entryCnt).size(), " ");
            std::string srcTorId = std::to_string(it->first.selfHostId);
            std::string dstHostId = std::to_string(it->first.dstHostId);
            std::string pathKeyStr = "(" + srcTorId + ", " + dstHostId+ ")";
            std::cout << pathKeyStr << construct_target_string_strlen(5 + 24 - change2string(pathKeyStr).size(), " ");
            uint32_t curPathNum = it->second.pathNum;
            std::cout << curPathNum << construct_target_string_strlen(5 + 7 - change2string(curPathNum).size(), " ");
            uint32_t lastSelectedPathIdx = it->second.lastSelectedPathIdx;
            std::cout << lastSelectedPathIdx << construct_target_string_strlen(7 + 2 - change2string(lastSelectedPathIdx).size(), " ");
            uint32_t highestPriorityPathIdx = it->second.highestPriorityPathIdx;
            std::cout << highestPriorityPathIdx << construct_target_string_strlen(10 + 2 - change2string(highestPriorityPathIdx).size(), " ");
            std::string pathIdsStr = vectorTostring<uint32_t>(it->second.paths);
            std::cout << pathIdsStr << construct_target_string_strlen(1, " ");
            std::cout << std::endl;
            entryCnt = entryCnt + 1;
        }
        return entryCnt;
    }

    uint32_t RdmaSmartFlowRouting::print_SMT()
    {
        uint32_t nodeID = m_switch_id;
        uint32_t vmtSize = m_vmVtepMapTbl.size();
        std::cout << "Node: **" << nodeID << "** has SMT in smartFlow with **" << vmtSize << "** entries" << std::endl;
        std::cout << "Index" << construct_target_string_strlen(5, " ");
        std::cout << "Server Address" << construct_target_string_strlen(6, " ");
        std::cout << "ToR Address" << construct_target_string_strlen(1, " ");
        std::cout << std::endl;
        uint32_t entryCnt = 0;
        std::map<Ipv4Address, hostIp2SMT_entry_t>::const_iterator it;
        for (it = m_vmVtepMapTbl.begin(); it != m_vmVtepMapTbl.end(); it++)
        {
            std::cout << entryCnt << construct_target_string_strlen(5 + 5 - change2string(entryCnt).size(), " ");
            std::cout << ipv4Address2string(it->first) << construct_target_string_strlen(6 + 14 - ipv4Address2string(it->first).size(), " ");
            std::cout << it->second.torId << construct_target_string_strlen(1, " ");
            std::cout << it->second.hostId << construct_target_string_strlen(1, " ");
            std::cout << std::endl;
            entryCnt = entryCnt + 1;
        }
        return entryCnt;
    }

    /** CALLBACK: callback functions  */
    void RdmaSmartFlowRouting::DoSwitchSend(Ptr<Packet> p, CustomHeader &ch, uint32_t outDev, uint32_t qIndex)
    {
        m_switchSendCallback(p, ch, outDev, qIndex);
    }
    void RdmaSmartFlowRouting::DoSwitchSendToDev(Ptr<Packet> p, CustomHeader &ch)
    {
        m_switchSendToDevCallback(p, ch);
    }

    void RdmaSmartFlowRouting::SetSwitchSendCallback(SwitchSendCallback switchSendCallback)
    {
        m_switchSendCallback = switchSendCallback;
    }

    void RdmaSmartFlowRouting::SetSwitchSendToDevCallback(SwitchSendToDevCallback switchSendToDevCallback)
    {
        m_switchSendToDevCallback = switchSendToDevCallback;
    }

    bool RdmaSmartFlowRouting::output_packet_by_path_tag(Ptr<Packet> packet, CustomHeader &ch, uint32_t pg)
    {
        NS_LOG_FUNCTION(this);
        NS_ASSERT_MSG(packet != 0, "Packet is NULL");
        Ipv4SmartFlowPathTag pathTag;
        packet->PeekPacketTag(pathTag);
        uint32_t selectedPortId = get_egress_port_id_by_path_tag(pathTag);
        update_path_tag(packet, pathTag);
        DoSwitchSend(packet, ch, selectedPortId, pg);
        return true;
    }
    bool RdmaSmartFlowRouting::e2eLBSrc_output_packet(Ptr<E2ESrcOutPackets> &srcOutEntry)
    {
        NS_LOG_INFO("############ Fucntion: e2eLBSrc_output_packet ############");
        // std::cout << "Enter output_packet_by_path_tag()"<<std::endl;
        if (srcOutEntry == nullptr)
        {
            NS_LOG_INFO("srcOutEntry==nullptr");
            return false;
        }
        Ipv4SmartFlowPathTag pathTag;
        srcOutEntry->dataPacket->PeekPacketTag(pathTag);
        // uint32_t maxQLen = pathTag.GetDre();
        // uint32_t selectedPortId = get_egress_port_id_by_path_tag(pathTag);
        update_path_tag(srcOutEntry->dataPacket, pathTag);

        if (srcOutEntry->isProbe)
        {
            Ipv4SmartFlowPathTag probePathTag;
            srcOutEntry->probePacket->PeekPacketTag(probePathTag);
            update_path_tag(srcOutEntry->probePacket, probePathTag);
        }
        return true;
    }
    /*void RdmaSmartFlowRouting::add_conga_tag_by_pit_entry(Ptr<Packet> packet, PathData *pitEntry)
    {
        if (pitEntry == 0)
        {
            return;
        }

        Ipv4SmartFlowCongaTag congaTag;
        congaTag.SetPathId(pitEntry->pid);
        congaTag.SetDre(pitEntry->pathDre);
        packet->AddPacketTag(congaTag);
        // std::cout << "SrcSwitch PiggyBack congestion degree: " << "    ";
        // std::cout << "Node: " << get_node_id() << "    ";
        // std::cout << "Time: " << Simulator::Now().GetNanoSeconds() << "    ";
        // std::cout << "Pid: " << congaTag.get_path_id() << "    ";
        // std::cout << "QLen: " << congaTag.GetDre() << "    ";
        // std::cout << "\n";
    }*/

    void RdmaSmartFlowRouting::add_latency_tag_by_pit_entries(Ptr<Packet> packet, std::vector<PathData *> &pitEntries)
    {
        NS_LOG_FUNCTION(this << pitEntries.size() << m_piggyLatencyCnt << pitEntries.size());
        NS_ASSERT_MSG(packet != 0 && m_piggyLatencyCnt != 0, "Packet or m_piggyLatencyCnt is NULL");
        uint32_t pathNum = pitEntries.size();
        if (pathNum == 0)
        {
            NS_LOG_INFO("No need to add latency tag since pathNum=" << pathNum << ", m_piggyCnt=" << m_piggyLatencyCnt);
        }
        else
        {
            for (uint32_t i = 0; i < pathNum; i++)
            {
                if (i + 1 > m_piggyLatencyCnt)
                {
                    break;
                }
                if (i == 0)
                {
                    Ipv4SmartFlowLatencyTag<0> latencyTag;
                    latencyTag.set_data_by_pit_entry(pitEntries[i]);
                    packet->AddPacketTag(latencyTag);
                }
                else if (i == 1)
                {
                    Ipv4SmartFlowLatencyTag<1> latencyTag;
                    latencyTag.set_data_by_pit_entry(pitEntries[i]);
                    packet->AddPacketTag(latencyTag);
                }
                else if (i == 2)
                {
                    Ipv4SmartFlowLatencyTag<2> latencyTag;
                    latencyTag.set_data_by_pit_entry(pitEntries[i]);
                    packet->AddPacketTag(latencyTag);
                }
                else if (i == 3)
                {
                    Ipv4SmartFlowLatencyTag<3> latencyTag;
                    latencyTag.set_data_by_pit_entry(pitEntries[i]);
                    packet->AddPacketTag(latencyTag);
                }
                else if (i == 4)
                {
                    Ipv4SmartFlowLatencyTag<4> latencyTag;
                    latencyTag.set_data_by_pit_entry(pitEntries[i]);
                    packet->AddPacketTag(latencyTag);
                }
                else if (i == 5)
                {
                    Ipv4SmartFlowLatencyTag<5> latencyTag;
                    latencyTag.set_data_by_pit_entry(pitEntries[i]);
                    packet->AddPacketTag(latencyTag);
                }
                else if (i == 6)
                {
                    Ipv4SmartFlowLatencyTag<6> latencyTag;
                    latencyTag.set_data_by_pit_entry(pitEntries[i]);
                    packet->AddPacketTag(latencyTag);
                }
                else if (i == 7)
                {
                    Ipv4SmartFlowLatencyTag<7> latencyTag;
                    latencyTag.set_data_by_pit_entry(pitEntries[i]);
                    packet->AddPacketTag(latencyTag);
                }
                else if (i == 8)
                {
                    Ipv4SmartFlowLatencyTag<8> latencyTag;
                    latencyTag.set_data_by_pit_entry(pitEntries[i]);
                    packet->AddPacketTag(latencyTag);
                }
                else if (i == 9)
                {
                    Ipv4SmartFlowLatencyTag<9> latencyTag;
                    latencyTag.set_data_by_pit_entry(pitEntries[i]);
                    packet->AddPacketTag(latencyTag);
                }
            }
        }
        return;
    }

    void RdmaSmartFlowRouting::update_PIT_after_adding_path_tag(PathData *pitEntry)
    {
        NS_LOG_FUNCTION(this);
        NS_ASSERT_MSG(pitEntry != 0, "PiggyBackPitEntry is NULL");
        pitEntry->tsProbeLastSend = Simulator::Now();
    }

    void RdmaSmartFlowRouting::update_PST_after_adding_path_tag(pstEntryData *pstEntry, PathData *forwardPitEntry)
    {
        NS_LOG_INFO("############ Fucntion: update_PST_after_adding_path_tag() ############");
        if (m_pathSelStrategy == PATH_SELECTION_FLOWLET_STATEGY)
        {
            uint32_t pathCnt = pstEntry->paths.size();
            for (uint32_t i = 0; i < pathCnt; i++)
            {
                if (pstEntry->paths[i] == forwardPitEntry->pid)
                {
                    pstEntry->lastSelectedPathIdx = i;
                    pstEntry->lastSelectedTimeInNs = Simulator::Now().GetNanoSeconds();
                    return;
                }
            }
            NS_LOG_INFO("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!Error in update_PST_after_adding_path_tag due to flowlet strategy");
        }
        else if (m_pathSelStrategy == PATH_SELECTION_ROUND_ROBIN_FOR_PACKET_STRATEGY)
        {
            if (pstEntry->lastSelectedPathIdx == DEFAULT_PATH_INDEX)
            {
                pstEntry->lastSelectedPathIdx = 0;
            }
            else
            {
                pstEntry->lastSelectedPathIdx = (pstEntry->lastSelectedPathIdx + 1) % pstEntry->pathNum;
            }
        }
    }

    uint32_t RdmaSmartFlowRouting::update_PIT_after_piggybacking(std::vector<PathData *> &piggyBackPitEntries)
    {
        NS_LOG_FUNCTION(this << piggyBackPitEntries.size());
        uint32_t pathNum = piggyBackPitEntries.size();
        for (uint32_t i = 0; i < pathNum; i++)
        {
            NS_ASSERT_MSG(piggyBackPitEntries[i] != 0, "PiggyBackPitEntry is NULL");
            piggyBackPitEntries[i]->tsLatencyLastSend = Simulator::Now();
        }
        return pathNum;
    }

    Ipv4SmartFlowPathTag RdmaSmartFlowRouting::construct_path_tag(uint32_t selectedPathId)
    {
        NS_LOG_FUNCTION(this << selectedPathId);
        Ipv4SmartFlowPathTag pathTag;
        pathTag.SetPathId(selectedPathId);
        pathTag.set_hop_idx(0);
        Time now = Simulator::Now();
        pathTag.SetTimeStamp(now);
        pathTag.SetDre(0);
        return pathTag;
    }

    PathData *RdmaSmartFlowRouting::get_the_smallest_latency_path(std::vector<PathData *> &pitEntries)
    {
        NS_LOG_INFO("############ Fucntion: get_the_smallest_latency_path() ############");
        if (pitEntries.size() == 0)
        {
            return 0;
        }
        std::sort(pitEntries.begin(), pitEntries.end(), cmp_pitEntry_in_increase_order_of_latency);
        return pitEntries[0];
    }

    PathData *RdmaSmartFlowRouting::get_the_oldest_measured_path(std::vector<PathData *> &pitEntries)
    {
        // NS_LOG_INFO ("############ Fucntion: get_the_oldest_measured_path() ############");
        if (pitEntries.size() == 0)
        {
            return 0;
        }
        std::sort(pitEntries.begin(), pitEntries.end(), cmp_pitEntry_in_increase_order_of_Generation_time);
        return pitEntries[0];
    }

    PathData *RdmaSmartFlowRouting::get_the_random_path(std::vector<PathData *> &pitEntries)
    {
        NS_LOG_FUNCTION(this << pitEntries.size());
        if (pitEntries.size() == 0)
        {
            return 0;
        }
        PathData *bestPitEntry = pitEntries[std::rand() % pitEntries.size()];
        return bestPitEntry;
    }

    /* PathData *RdmaSmartFlowRouting::get_the_flowlet_path(pstEntryData *pstEntry, std::vector<PathData *> &pitEntries, Ptr<const Packet> packet, const Ipv4Header &header)
     {
         NS_LOG_INFO("get_the_flowlet_path()");
         std::string flowStr = hashing_flow_with_5_tuple_to_string(packet, header);
         NS_LOG_INFO("Flow INFO : " << flowStr);
         std::map<std::string, flet_entry_t>::iterator it;
         it = m_fletTbl.find(flowStr);
         PathData *bestPitEntry = 0;
         Time curTime = Simulator::Now();
         uint32_t curTimeInNs = curTime.GetNanoSeconds();
         uint32_t curSelectedPathIdx = std::rand() % pitEntries.size();
         if (it == m_fletTbl.end())
         {
             NS_LOG_INFO("New Flow");
             bestPitEntry = pitEntries[curSelectedPathIdx];
             flet_entry_t fletEnty = flet_entry_t();
             fletEnty.lastSelectedPitEntry = bestPitEntry;
             fletEnty.lastSelectedTimeInNs = curTimeInNs;
             m_fletTbl[flowStr] = fletEnty;
         }
         else
         {
             NS_LOG_INFO("Exist Flow");
             uint32_t gap = curTimeInNs - it->second.lastSelectedTimeInNs;
             NS_LOG_INFO("Flowlet Timout In Nano Second : " << m_flowletTimoutInNs << ", curGapInNs: " << gap);
             if (gap > m_flowletTimoutInNs)
             {
                 NS_LOG_INFO("New Flowlet, changes the path");
                 bestPitEntry = pitEntries[curSelectedPathIdx];
                 it->second.lastSelectedPitEntry = bestPitEntry;
                 it->second.lastSelectedTimeInNs = curTimeInNs;
             }
             else
             {
                 NS_LOG_INFO("Same Flowlet, maintain the previous path");
                 bestPitEntry = it->second.lastSelectedPitEntry;
                 it->second.lastSelectedTimeInNs = curTimeInNs;
             }
         }
         return bestPitEntry;
     }*/

    PathData *RdmaSmartFlowRouting::get_the_highest_priority_path(std::vector<PathData *> &pitEntries)
    {
        PathData *bestPitEntry = 0;
        std::sort(pitEntries.begin(), pitEntries.end(), cmp_pitEntry_in_decrease_order_of_priority);
        bestPitEntry = pitEntries[0];
        return bestPitEntry;
    }

    /*PathData *RdmaSmartFlowRouting::get_the_best_forwarding_path(std::vector<PathData *> &pitEntries, pstEntryData *pstEntry, Ptr<const Packet> packet, const Ipv4Header &header)
    {
        // NS_LOG_INFO ("############ Fucntion: get_the_best_forwarding_path() ############");
        PathData *bestPitEntry = 0;
        if (m_pathSelStrategy == PATH_SELECTION_SMALL_LATENCY_FIRST_STRATEGY)
        {
            bestPitEntry = get_the_smallest_latency_path(pitEntries); // smartflow
            // bestPitEntry->tsProbeLastSend = Simulator::Now();
        }
        else if (m_pathSelStrategy == PATH_SELECTION_RANDOM_STRATEGY)
        { // hashflow
            bestPitEntry = get_the_random_path(pitEntries);
        }
        else if (m_pathSelStrategy == PATH_SELECTION_ROUND_ROBIN_FOR_PACKET_STRATEGY)
        { // rbnflow
            bestPitEntry = get_the_round_robin_path_for_packet(pstEntry, pitEntries);
        }
        else if (m_pathSelStrategy == PATH_SELECTION_FLOW_HASH_STRATEGY)
        { // ecmp
            bestPitEntry = get_the_hashing_path(pitEntries, packet, header);
        }
        else if (m_pathSelStrategy == PATH_SELECTION_PRIORITY_FIRST_STRATEGY)
        { // minflow
            bestPitEntry = get_the_highest_priority_path(pitEntries);
        }
        else if (m_pathSelStrategy == PATH_SELECTION_FLOWLET_STATEGY)
        {
            bestPitEntry = get_the_flowlet_path(pstEntry, pitEntries, packet, header);
        }
        else if (m_pathSelStrategy == PATH_SELECTION_ROUND_ROBIN_FOR_FLOW_STRATEGY)
        {
            bestPitEntry = get_the_round_robin_path_for_flow(pstEntry, pitEntries, packet, header);
        }
        else if (m_pathSelStrategy == PATH_SELECTION_ROUND_ROBIN_FOR_HYBRID_STRATEGY)
        {
            bestPitEntry = get_the_round_robin_path_for_hybird(pstEntry, pitEntries, packet, header);
        }
        else if (m_pathSelStrategy == PATH_SELECTION_CONGA_STRATEGY)
        {

            bestPitEntry = get_the_least_congested_path(pstEntry, pitEntries, packet, header); // conga
        }
        return bestPitEntry;
    }*/

    std::vector<PathData *> RdmaSmartFlowRouting::get_the_best_piggyback_paths(std::vector<PathData *> &pitEntries)
    {
        NS_LOG_INFO("############ Fucntion: get_the_best_reverse_paths() ############");
        std::vector<PathData *> bestPitEntries;
        bestPitEntries.clear();
        // NS_LOG_INFO("The results after apply piggyback strategy=" << m_piggybackStrategy << " are:{0:latencyFirst},  {1:gentFirst}, {2:sentFirst}");
        if (m_piggybackStrategy == PIGGY_BACK_SMALL_LATENCY_FIRST_STRATEGY)
        {
            std::sort(pitEntries.begin(), pitEntries.end(), cmp_pitEntry_in_increase_order_of_latency);
        }
        else if (m_piggybackStrategy == PIGGY_BACK_SMALL_GENT_TIME_FIRST_STRATEGY)
        {
            std::sort(pitEntries.begin(), pitEntries.end(), cmp_pitEntry_in_increase_order_of_Generation_time);
        }
        else if (m_piggybackStrategy == PIGGY_BACK_SMALL_SENT_TIME_FIRST_STRATEGY)
        {
            std::sort(pitEntries.begin(), pitEntries.end(), cmp_pitEntry_in_increase_order_of_Sent_time);
        }
        else
        {
            std::cout << "Error in get_the_best_piggyback_paths() since m_probeStrategy=" << m_piggybackStrategy << std::endl;
            return bestPitEntries;
        }
        uint32_t allCnt = pitEntries.size();
        if (allCnt <= m_piggyLatencyCnt)
        {
            bestPitEntries.assign(pitEntries.begin(), pitEntries.end());
        }
        else
        {
            bestPitEntries.assign(pitEntries.begin(), pitEntries.begin() + m_piggyLatencyCnt);
        }
        return bestPitEntries;
    }

    uint32_t RdmaSmartFlowRouting::get_the_expired_paths(std::vector<PathData *> &allPitEntries, std::vector<PathData *> &expiredPitEntries)
    {
        NS_LOG_FUNCTION(this << allPitEntries.size());
        auto maxElement = std::max_element(allPitEntries.begin(), allPitEntries.end(),
                [](const PathData* lhs, const PathData* rhs) 
                {
                    return lhs->latency < rhs->latency;
                }
            );
        uint64_t expiredPeriodInNs = 2 * (*maxElement)->latency;
        expiredPitEntries.clear();
        uint64_t curTime = Simulator::Now().GetNanoSeconds();
        for (auto e : allPitEntries)
        {
            uint64_t Gentime = e->tsGeneration.GetNanoSeconds();
            NS_ASSERT_MSG(curTime >= Gentime, "GenTime cannot be larger than curtime");
            uint64_t timeGap = curTime - Gentime;
            if (timeGap > expiredPeriodInNs)
            {
                expiredPitEntries.push_back(e);
            }
        }
        return expiredPitEntries.size();
    }

    uint32_t RdmaSmartFlowRouting::get_the_potential_paths(PathData *bestPitEntry, std::vector<PathData *> &allPitEntries, std::vector<PathData *> &potentialPitEntries)
    {
        // NS_LOG_INFO ("############ Fucntion: get_the_potential_paths() ############");
        potentialPitEntries.clear();
        for (auto e : allPitEntries)
        {
            if (bestPitEntry->latency > e->theoreticalSmallestLatencyInNs)
            {
                potentialPitEntries.push_back(e);
            }
        }
        return potentialPitEntries.size();
    }

    uint32_t RdmaSmartFlowRouting::get_the_probe_paths(std::vector<PathData *> &expiredPitEntries, std::vector<PathData *> &probePitEntries)
    {
        NS_LOG_FUNCTION(this << expiredPitEntries.size());

        probePitEntries.clear();
        if (expiredPitEntries.size() == 0)
        {
            return 0;
        }

        auto maxElement = std::max_element(expiredPitEntries.begin(), expiredPitEntries.end(),
                [](const PathData* lhs, const PathData* rhs) 
                {
                    return lhs->latency < rhs->latency;
                }
            );
        uint64_t prbColdenPeriodInNs = 2 * (*maxElement)->latency;


        uint64_t curTime = Simulator::Now().GetNanoSeconds();
        for (auto e : expiredPitEntries)
        {
            uint64_t prbTime = e->tsProbeLastSend.GetNanoSeconds();
            NS_ASSERT_MSG(curTime >= prbTime, "ProbeTime cannot be larger than curtime");
            uint64_t timeGap = curTime - prbTime;
            if (timeGap > prbColdenPeriodInNs)
            {
                probePitEntries.push_back(e);
            }
        }
        return probePitEntries.size();
    }

    PathData *RdmaSmartFlowRouting::get_the_best_probing_path(std::vector<PathData *> &pitEntries)
    {
        NS_LOG_FUNCTION(this << pitEntries.size());
        // NS_LOG_INFO ("############ Fucntion: get_the_best_probing_path() ############");
        if (pitEntries.size() == 0)
        {
            return 0;
        }
        // if (m_probeStrategy == PROBE_SMALL_LATENCY_FIRST_STRATEGY)
        // {
        //     return get_the_smallest_latency_path(pitEntries);
        // }
        // else if (m_probeStrategy == PROBE_SMALL_GENERATION_TIME_FIRST_STRATEGY)
        // {
        //     return get_the_oldest_measured_path(pitEntries);
        // }
        // else if (m_probeStrategy == PROBE_RANDOM_STRATEGY)
        // { // hashflow
        //     return get_the_random_path(pitEntries);
        // }
        return get_the_random_path(pitEntries);
    }

    void RdmaSmartFlowRouting::initialize()
    {
        // highestPriorityPathIdx
        for (auto &pstEntry : m_pathSelTbl)
        {
            std::vector<PathData *> pitEntries = batch_lookup_PIT(pstEntry.second.paths);
            std::sort(pitEntries.begin(), pitEntries.end(), cmp_pitEntry_in_decrease_order_of_priority);
            uint32_t highestPriorityPathIdx = 0;
            for (uint32_t i = 1; i < pstEntry.second.paths.size(); i++)
            {
                if (pstEntry.second.paths[i] == pitEntries[0]->pid)
                {
                    highestPriorityPathIdx = i;
                    break;
                }
            }
            pstEntry.second.highestPriorityPathIdx = highestPriorityPathIdx;
            pstEntry.second.lastSelectedPathIdx = DEFAULT_PATH_INDEX;
            pstEntry.second.lastPiggybackPathIdx = 0;
        }
        for (auto &pitEntry : m_nexthopSelTbl)
        {
            pitEntry.second.theoreticalSmallestLatencyInNs = pitEntry.second.portSequence.size() * RDMA_LINK_LATENCY_IN_NANOSECOND;
            pitEntry.second.pathDre = 0; // to be modified
        }
        // m_nodeId
        m_nodeId = m_switch_id;
        for (size_t i = 1; i < CONGA_SW_PORT_NUM; i++)
        {
            m_XMap[i] = 0;
        }
        // lastSelectedPathIdx
    }

    std::vector<PathData *> RdmaSmartFlowRouting::get_the_newly_measured_paths(std::vector<PathData *> &pitEntries)
    {
        // NS_LOG_INFO ("############ Fucntion: get_the_newly_measured_paths() ############");
        std::vector<PathData *> candidatePaths;
        candidatePaths.clear();
        uint32_t pathNum = pitEntries.size();
        for (uint32_t i = 0; i < pathNum; i++)
        {
            Time tsGeneration = pitEntries[i]->tsGeneration;
            Time tsLatencyLastSend = pitEntries[i]->tsLatencyLastSend;
            if (tsGeneration.GetNanoSeconds() > tsLatencyLastSend.GetNanoSeconds())
            {
                candidatePaths.push_back(pitEntries[i]);
            }
        }
        return candidatePaths;
    }

    uint32_t RdmaSmartFlowRouting::install_PIT(std::map<uint32_t, PathData> &pit)
    {
        set_PIT(pit);
        return pit.size();
    }

    uint32_t RdmaSmartFlowRouting::install_PST(std::map<HostId2PathSeleKey, pstEntryData> &pst)
    {
        set_PST(pst);
        return pst.size();
    }

    uint32_t RdmaSmartFlowRouting::install_SMT(std::map<Ipv4Address, hostIp2SMT_entry_t> &vmt)
    {
        set_SMT(vmt);
        return vmt.size();
    }

    void RdmaSmartFlowRouting::update_PIT_by_latency_data(LatencyData &latencyData)
    {
        NS_LOG_FUNCTION(this);
        uint32_t pid = latencyData.latencyInfo.first;
        uint32_t newLatency = latencyData.latencyInfo.second;
        Time newLatencyGeneration = latencyData.tsGeneration;
        PathData *pitEntry = lookup_PIT(pid);
        // uint32_t oldLatency = pitEntry->latency;
        Time oldLatencyGeneration = pitEntry->tsGeneration;
        if (newLatencyGeneration > oldLatencyGeneration)
        {
            NS_LOG_INFO ("Old Info: " << pitEntry->toString());
            pitEntry->latency = newLatency;
            pitEntry->tsGeneration = newLatencyGeneration;
            NS_LOG_INFO ("New Info: " << pitEntry->toString());
        }
    }

    void RdmaSmartFlowRouting::update_PIT_by_latency_tag(Ptr<Packet> &packet)
    {
        NS_LOG_FUNCTION(this << m_piggyLatencyCnt);
        for (uint32_t i = 0; i < m_piggyLatencyCnt; i++)
        {
            if (i == 0)
            {
                Ipv4SmartFlowLatencyTag<0> latencyTag;
                if (packet->PeekPacketTag(latencyTag))
                {
                    LatencyData latencyData = latencyTag.GetLatencyData();
                    update_PIT_by_latency_data(latencyData);
                }
                else
                {
                    break;
                }
            }
            else if (i == 1)
            {
                Ipv4SmartFlowLatencyTag<1> latencyTag;
                if (packet->PeekPacketTag(latencyTag))
                {
                    LatencyData latencyData = latencyTag.GetLatencyData();
                    update_PIT_by_latency_data(latencyData);
                }
                else
                {
                    break;
                }
            }
            else if (i == 2)
            {
                Ipv4SmartFlowLatencyTag<2> latencyTag;
                if (packet->PeekPacketTag(latencyTag))
                {
                    LatencyData latencyData = latencyTag.GetLatencyData();
                    update_PIT_by_latency_data(latencyData);
                }
                else
                {
                    break;
                }
            }
            else if (i == 3)
            {
                Ipv4SmartFlowLatencyTag<3> latencyTag;
                if (packet->PeekPacketTag(latencyTag))
                {
                    LatencyData latencyData = latencyTag.GetLatencyData();
                    update_PIT_by_latency_data(latencyData);
                }
                else
                {
                    break;
                }
            }
            else if (i == 4)
            {
                Ipv4SmartFlowLatencyTag<4> latencyTag;
                if (packet->PeekPacketTag(latencyTag))
                {
                    LatencyData latencyData = latencyTag.GetLatencyData();
                    update_PIT_by_latency_data(latencyData);
                }
                else
                {
                    break;
                }
            }
            else if (i == 5)
            {
                Ipv4SmartFlowLatencyTag<5> latencyTag;
                if (packet->PeekPacketTag(latencyTag))
                {
                    LatencyData latencyData = latencyTag.GetLatencyData();
                    update_PIT_by_latency_data(latencyData);
                }
                else
                {
                    break;
                }
            }
            else if (i == 6)
            {
                Ipv4SmartFlowLatencyTag<6> latencyTag;
                if (packet->PeekPacketTag(latencyTag))
                {
                    LatencyData latencyData = latencyTag.GetLatencyData();
                    update_PIT_by_latency_data(latencyData);
                }
                else
                {
                    break;
                }
            }
            else if (i == 7)
            {
                Ipv4SmartFlowLatencyTag<7> latencyTag;
                if (packet->PeekPacketTag(latencyTag))
                {
                    LatencyData latencyData = latencyTag.GetLatencyData();
                    update_PIT_by_latency_data(latencyData);
                }
                else
                {
                    break;
                }
            }
            else if (i == 8)
            {
                Ipv4SmartFlowLatencyTag<8> latencyTag;
                if (packet->PeekPacketTag(latencyTag))
                {
                    LatencyData latencyData = latencyTag.GetLatencyData();
                    update_PIT_by_latency_data(latencyData);
                }
                else
                {
                    break;
                }
            }
            else if (i == 9)
            {
                Ipv4SmartFlowLatencyTag<9> latencyTag;
                if (packet->PeekPacketTag(latencyTag))
                {
                    LatencyData latencyData = latencyTag.GetLatencyData();
                    update_PIT_by_latency_data(latencyData);
                }
                else
                {
                    break;
                }
            }
        }
        return;
    }

    void RdmaSmartFlowRouting::update_PIT_by_path_tag(Ipv4SmartFlowPathTag &pathTag, PathData *&pitEntry)
    {
        NS_LOG_FUNCTION(this);
        Time curTime = Simulator::Now();
        Time oldTime = pathTag.GetTimeStamp();
        uint64_t newLatency = (uint64_t)(curTime.GetNanoSeconds() - oldTime.GetNanoSeconds());
        pitEntry->latency = newLatency;
        pitEntry->tsGeneration = curTime;
        return;
    }

    /*void RdmaSmartFlowRouting::update_PIT_by_conga_tag(Ipv4SmartFlowCongaTag &congaTag)
    {
        // NS_LOG_INFO ("############ Fucntion: update_PIT_by_path_tag() ############");
        uint32_t pid = congaTag.get_path_id();
        uint32_t pathDre = congaTag.GetDre();
        PathData *pitEntry = lookup_PIT(pid);
        // std::cout << "SrcSwitch Update congestion degree: " << "    ";
        // std::cout << "Node: " << get_node_id() << "    ";
        // std::cout << "Time: " << Simulator::Now().GetNanoSeconds() << "    ";
        // std::cout << "Pid: " << pid << "    ";
        // std::cout << "QLen: " << pitEntry->SetDre << " --> " << SetDre << "    ";
        // std::cout << "\n";
        // NS_LOG_INFO("Node " << m_nodeId << " updates PIT from piggyback");
        // std::cout << "Node " << m_nodeId << " updates PIT from piggyback" << std::endl;
        // pitEntry->print();
        pitEntry->pathDre = pathDre;
        pitEntry->tsGeneration = Simulator::Now();
        // pitEntry->print();

        return;
    }*/

    void RdmaSmartFlowRouting::update_PIT_by_probe_tag(Ipv4SmartFlowProbeTag &probeTag)
    {
        NS_LOG_FUNCTION(this);
        uint32_t pid, newLatency;
        probeTag.get_path_info(pid, newLatency);
        PathData *pitEntry = lookup_PIT(pid);
        pitEntry->latency = newLatency;
        pitEntry->tsGeneration = Simulator::Now();
        return;
    }

    void RdmaSmartFlowRouting::receive_probe_packet(Ipv4SmartFlowProbeTag &probeTag)
    {
        NS_LOG_INFO("############ Fucntion: receive_probe_packet() ############");
        update_PIT_by_probe_tag(probeTag);
        return;
    }
    
    Ptr<Packet> RdmaSmartFlowRouting::reply_probe_info(Ptr<Packet> &p, CustomHeader &ch)
    {
        Ipv4SmartFlowProbeTag probeTag;
        p->PeekPacketTag(probeTag);
        Ptr<Packet> replyPacket = construct_reply_probe_packet(p, ch);
        uint32_t pid = probeTag.GetPathId();
        PathData *pitEntry = lookup_PIT(pid);
        std::vector<PathData *> feedBackPitEntries;
        feedBackPitEntries.push_back(pitEntry);
        add_latency_tag_by_pit_entries(p, feedBackPitEntries);
        update_PIT_after_piggybacking(feedBackPitEntries);
        return replyPacket;
    }

    void RdmaSmartFlowRouting::update_path_tag(Ptr<Packet> &packet, Ipv4SmartFlowPathTag &pathTag)
    {
        NS_LOG_FUNCTION(this);
        uint32_t curHopIdx = pathTag.get_the_current_hop_index();
        pathTag.set_hop_idx(curHopIdx + 1);
        packet->ReplacePacketTag(pathTag);
        return;
    }

    Ptr<Packet> RdmaSmartFlowRouting::construct_probe_packet(Ptr<Packet> &pkt, CustomHeader &ch,bool protocol)
    {
        Ptr<Packet> probePacket = Create<Packet>(protocol?0:PROBE_DEFAULT_PKT_SIZE_IN_BYTE);

        /*Ptr<Packet> p = Create<Packet>(0);
        Ptr<UniformRandomVariable> x = CreateObject<UniformRandomVariable> ();
        PauseHeader pauseh((type == 0 ? m_pausetime : 0), m_queue->GetNBytes(qIndex), qIndex);
        p->AddHeader(pauseh);
        Ipv4Header ipv4h;  // Prepare IPv4 header
        ipv4h.SetProtocol(0xFE);
        ipv4h.SetSource(m_node->GetObject<Ipv4>()->GetAddress(m_ifIndex, 0).GetLocal());
        ipv4h.SetDestination(Ipv4Address("255.255.255.255"));
        ipv4h.SetPayloadSize(p->GetSize());
        ipv4h.SetTtl(1);
        ipv4h.SetIdentification(x->GetValue(0, 65536));
        p->AddHeader(ipv4h);
        AddHeader(p, 0x800);
        CustomHeader ch(CustomHeader::L2_Header | CustomHeader::L3_Header | CustomHeader::L4_Header);
        p->PeekHeader(ch);
        */

        // 添加 SeqTsHeader
        SeqTsHeader seqTs;
        seqTs.SetSeq(protocol?ch.udp.seq:0);if(protocol)seqTs.SetPG(ch.udp.pg); // 设置初始序列号
        probePacket->AddHeader(seqTs);
        // 添加 UdpHeader
        UdpHeader udpHeader;
        udpHeader.SetDestinationPort(ch.udp.dport); //
        udpHeader.SetSourcePort(ch.udp.sport);      //
        probePacket->AddHeader(udpHeader);

        /*Ipv4Header ipv4h;  // Prepare IPv4 header
        ipv4h.SetProtocol(0xFE);
        ipv4h.SetSource(m_node->GetObject<Ipv4>()->GetAddress(m_ifIndex, 0).GetLocal());
        ipv4h.SetDestination(Ipv4Address("255.255.255.255"));
        ipv4h.SetPayloadSize(p->GetSize());
        ipv4h.SetTtl(1);
        ipv4h.SetIdentification(x->GetValue(0, 65536));
        p->AddHeader(ipv4h);
        AddHeader(p, 0x800);*/

        // 添加 Ipv4Header
        Ipv4Header ipHeader;
        ipHeader.SetSource(Ipv4Address(ch.sip));
        ipHeader.SetDestination(Ipv4Address(ch.dip));
        ipHeader.SetProtocol(0x11); // UDP protocol number
        ipHeader.SetPayloadSize(probePacket->GetSize());
        ipHeader.SetTtl(64);
        ipHeader.SetTos(0);
        ipHeader.SetIdentification(50);
        probePacket->AddHeader(ipHeader);

        // 添加 PppHeader
        PppHeader ppp;
        ppp.SetProtocol(0x0021); // EtherToPpp(0x800), see point-to-point-net-device.cc
        probePacket->AddHeader(ppp);

        return probePacket;
    }
    Ptr<Packet> RdmaSmartFlowRouting::construct_reply_probe_packet(Ptr<Packet> &pkt, CustomHeader &ch)
    {
        Ptr<Packet> probePacket = Create<Packet>(PROBE_DEFAULT_PKT_SIZE_IN_BYTE);
        // 添加 SeqTsHeader
        SeqTsHeader seqTs;
        seqTs.SetSeq(0); // 设置初始序列号
        probePacket->AddHeader(seqTs);
        // 添加 UdpHeader
        UdpHeader udpHeader;
        udpHeader.SetDestinationPort(ch.udp.dport); //
        udpHeader.SetSourcePort(ch.udp.sport);      //
        probePacket->AddHeader(udpHeader);

        Ipv4Header ipHeader;
        ipHeader.SetSource(Ipv4Address(ch.dip));
        ipHeader.SetDestination(Ipv4Address(ch.sip));
        ipHeader.SetProtocol(0x11); // UDP protocol number
        ipHeader.SetPayloadSize(probePacket->GetSize());
        ipHeader.SetTtl(64);
        ipHeader.SetTos(0);
        ipHeader.SetIdentification(50);
        probePacket->AddHeader(ipHeader);

        // 添加 PppHeader
        PppHeader ppp;
        ppp.SetProtocol(0x0021); // EtherToPpp(0x800), see point-to-point-net-device.cc
        probePacket->AddHeader(ppp);

        return probePacket;
    }

    Ipv4SmartFlowProbeTag RdmaSmartFlowRouting::construct_probe_tag_by_path_id(uint32_t expiredPathId)
    {
        // NS_LOG_INFO ("############ Fucntion: construct_probe_tag_by_path_id() ############");
        Ipv4SmartFlowProbeTag probeTag;
        probeTag.Initialize(expiredPathId);
        return probeTag;
    }

    pstEntryData *RdmaSmartFlowRouting::lookup_PST(HostId2PathSeleKey &pstKey)
    {
        NS_LOG_FUNCTION(this << pstKey.to_string());
        std::map<HostId2PathSeleKey, pstEntryData>::iterator it = m_pathSelTbl.find(pstKey);
        NS_ASSERT_MSG(it != m_pathSelTbl.end(), "Cannot match any entry in PST for the Key " << pstKey.to_string());
        return &(it->second);
    }

    pdt_entry_t *RdmaSmartFlowRouting::lookup_PDT(HostId2PathSeleKey &pstKey)
    {
        NS_LOG_INFO("############ Fucntion: lookup_PDT() ############");
        auto it = m_pathDecTbl.find(pstKey);
        if (it == m_pathDecTbl.end())
        {
            NS_LOG_INFO("No matching entry in PDT for the following pstKey");
            // pstKey.print();
            return 0;
        }
        else
        {
            return &(it->second);
        }
    }

    PathData *RdmaSmartFlowRouting::lookup_PIT(uint32_t pieKey)
    {
        NS_LOG_FUNCTION(this);
        std::map<uint32_t, PathData>::iterator it = m_nexthopSelTbl.find(pieKey);
        NS_ASSERT_MSG(it != m_nexthopSelTbl.end(), "Cannot match any entry in PIT for the Key " << pieKey);
        return &(it->second);
    }
    std::vector<PathData *> RdmaSmartFlowRouting::batch_lookup_PIT(std::vector<uint32_t> &pids)
    {
        NS_LOG_FUNCTION(this <<  vectorTostring(pids));
        std::vector<PathData *> pitEntries(pids.size());
        for (uint32_t i = 0; i < pitEntries.size(); i++)
        {
            pitEntries[i] = lookup_PIT(pids[i]);
        }
        return pitEntries;
    }

    // uint32_t flowId = 0;
    // Hasher m_hasher;
    // m_hasher.clear();
    // TcpHeader tcpHeader;
    // packet->PeekHeader(tcpHeader);
    // std::ostringstream oss;
    // oss << tcpHeader.GetDestinationPort() << tcpHeader.GetSourcePort()//获取目标端口>获取源端口>
    //     << header.GetSource() << header.GetDestination() << header.GetProtocol();//获取源ip、目的ip、传输层协议
    // std::string data = oss.str();
    // flowId = m_hasher.GetHash32(data);
    // return flowId;

    /*void RdmaSmartFlowRouting::record_reorder_at_dst_tor(Ptr<Packet> &pkt, const Ipv4Header &header)
    {
        // NS_LOG_INFO ("############ Fucntion: check_reorder_info_in_dst_switch() ############");
        TcpHeader tcpHeader;
        if (pkt->PeekHeader(tcpHeader) == 0)
        {
            // NS_LOG_INFO("This Packet does not have TCP header");
            return;
        }
        std::string flowStr = hashing_flow_with_5_tuple_to_string(pkt, header);
        // NS_LOG_INFO("5-tuple Info of the packet :" << flowStr);

        uint32_t tcpHeaderLengthInByte = ((uint32_t)tcpHeader.GetLength()) * 4;
        uint32_t pktSizeInByte = pkt->GetSize();
        uint32_t tcpSeqNum = tcpHeader.GetSequenceNumber().GetValue();
        // uint32_t expectedSeqNum = tcpSeqNum + pktSizeInByte - tcpHeaderLengthInByte;
        uint32_t tcpPayloadInByte = pktSizeInByte - tcpHeaderLengthInByte;
        std::string flagStr = TcpHeader::FlagsToString(tcpHeader.GetFlags());

        // NS_LOG_INFO("Packet Size without ipv4 header but With TCP header in byte: " << pktSizeInByte);
        // NS_LOG_INFO("TCP Header Size in Byte: " << tcpHeaderLengthInByte);
        // NS_LOG_INFO("TCP Flags: " << flagStr);
        // NS_LOG_INFO("curSeqNum: " << tcpSeqNum);
        // NS_LOG_INFO("Packet Payload In Byte for TCP Header: " << tcpPayloadInByte);
        // NS_LOG_INFO("expectedSeqNum: " << expectedSeqNum);

        auto it = m_reorderTable.find(flowStr);
        if (flagStr == "SYN")
        {
            // NS_LOG_INFO("The First handshake packet of the new flow sent from the sender to the receiver");
            reorder_entry_t reorderEntry = reorder_entry_t();
            reorderEntry.flag = true;
            reorderEntry.seqs.clear();
            m_reorderTable[flowStr] = reorderEntry;
        }
        else if (flagStr == "SYN|ACK")
        {
            // NS_LOG_INFO("The Second handshake packet of the new flow sent from the receiver to the sender");
            reorder_entry_t reorderEntry = reorder_entry_t();
            reorderEntry.flag = false;
            reorderEntry.seqs.clear();
            m_reorderTable[flowStr] = reorderEntry;
        }
        else if (flagStr == "FIN|ACK")
        {
            // NS_LOG_INFO("The teardown packet");
            if (it == m_reorderTable.end())
            {
                std::cout << "Error in record_reorder_at_dst_tor() since the un-recorded flow" << std::endl;
            }
            else if (it->second.flag == false)
            {
                NS_LOG_INFO("The Second teardown packet from the receiver to the sender");
                // it->second.print();
            }
            else
            {
                // NS_LOG_INFO("The First teardown packet with normal payload from the sender to the receiver");
                it->second.seqs.push_back(tcpSeqNum);
                // it->second.print();
            }
        }
        else if (flagStr == "ACK")
        {
            // NS_LOG_INFO("The Mid packet of the flow");
            if (it == m_reorderTable.end())
            {
                std::cout << "Error in record_reorder_at_dst_tor() since the un-recorded flow" << std::endl;
            }
            else if (it->second.flag == false)
            {
                NS_LOG_INFO("From the receiver to the sender");
                // it->second.print();
            }
            else
            {
                if ((tcpPayloadInByte == 0) && (pktSizeInByte == 32) && (tcpSeqNum == 1))
                {
                    NS_LOG_INFO("The third handshake packet of the flow sent from the sender to the receiver");
                }
                else
                {
                    it->second.seqs.push_back(tcpSeqNum);
                }
                // it->second.print();
            }
        }
        return;
    }*/

    void RdmaSmartFlowRouting::receive_normal_packet(Ptr<Packet> &pkt, Ipv4SmartFlowPathTag &pathTag, PathData *&pitEntry)
    {
        NS_LOG_FUNCTION(this);
        update_PIT_by_path_tag(pathTag, pitEntry);
        update_PIT_by_latency_tag(pkt);
        return;
    }

    void RdmaSmartFlowRouting::add_probe_tag_by_path_id(Ptr<Packet> packet, uint32_t expiredPathId)
    {
        NS_LOG_FUNCTION(this << expiredPathId);
        Ipv4SmartFlowProbeTag probeTag = construct_probe_tag_by_path_id(expiredPathId);
        packet->AddPacketTag(probeTag);
        return;
    }

    void RdmaSmartFlowRouting::update_PIT_after_probing(PathData *pitEntry)
    {
        NS_LOG_FUNCTION(this);
        // std::cout << "Node " << m_node->GetId() << " Sends a Probe Data Packet at time " << Simulator::Now() << " pid " << pitEntry->pid << std::endl;
        //   pitEntry->print();
        pitEntry->tsProbeLastSend = Simulator::Now();
        // pitEntry->print();
        return;
    }

    void RdmaSmartFlowRouting::add_path_tag_by_path_id(Ptr<Packet> packet, uint32_t pid)
    {
        NS_LOG_FUNCTION(this << pid);
        Ipv4SmartFlowPathTag pathTag = construct_path_tag(pid);
        packet->AddPacketTag(pathTag);
        return;
    }

    uint32_t RdmaSmartFlowRouting::get_the_path_length_by_path_id(const uint32_t pathId, PathData *&pitEntry)
    {
        // NS_LOG_INFO ("############ Fucntion: get_the_path_length_by_path_id() ############");
        pitEntry = lookup_PIT(pathId);
        return pitEntry->portSequence.size();
    }

    bool RdmaSmartFlowRouting::reach_the_last_hop_of_path_tag(Ipv4SmartFlowPathTag &smartFlowTag, PathData *&pitEntry)
    {
        NS_LOG_FUNCTION(this);
        uint32_t pathId = smartFlowTag.get_path_id();
        uint32_t pathSize = get_the_path_length_by_path_id(pathId, pitEntry);
        uint32_t hopIdx = smartFlowTag.get_the_current_hop_index();
        NS_ASSERT_MSG(hopIdx <= pathSize, "The hopIdx is larger than the pathSize");
        if (hopIdx == pathSize)
        {
            NS_LOG_INFO("The packet reaches the last hop of Path " << pathId);
            return true;
        }
        else
        {
            // NS_LOG_INFO("Is " << hopIdx << "/" << pathSize << " hops");
            return false;
        }
    }

    bool RdmaSmartFlowRouting::exist_path_tag(Ptr<Packet> packet, Ipv4SmartFlowPathTag &pathTag)
    {
        NS_LOG_FUNCTION(this);
        if (packet->PeekPacketTag(pathTag))
        { // reaching the src ToR switch
            // NS_LOG_INFO("Path Tag indeed exits");
            uint32_t curHopIdx = pathTag.get_the_current_hop_index();
            uint32_t pathId = pathTag.get_path_id();
            NS_LOG_INFO("Packet " << packet->GetUid() << " is on the " << curHopIdx << "-th hop of Path " << pathId);
            return true;
        }
        // NS_LOG_INFO("Path Tag does not exit");
        return false;
    }

    bool RdmaSmartFlowRouting::exist_ack_tag(Ptr<Packet> packet, AckPathTag &ackTag)
    {
        NS_LOG_FUNCTION(this);
        if (packet->PeekPacketTag(ackTag))
        {
            NS_LOG_INFO("ACK Tag : pktID=" << packet->GetUid() << ",  flowID=" << ackTag.GetFlowId() << ", pathID= " << ackTag.GetPathId());
            return true;
        }
        return false;
    }

    bool RdmaSmartFlowRouting::exist_probe_tag(Ptr<Packet> packet, Ipv4SmartFlowProbeTag &probeTag)
    {
        // NS_LOG_INFO ("############ Fucntion: exist_probe_tag() ############");
        if (packet->PeekPacketTag(probeTag))
        { // reaching the src ToR switch
            // NS_LOG_INFO ("Probe Tag indeed exits");
            return true;
        }
        // NS_LOG_INFO ("Probe Tag does not exit");
        return false;
    }

    uint32_t RdmaSmartFlowRouting::get_egress_port_id_by_path_tag(Ipv4SmartFlowPathTag &smartFlowTag)
    {
        // NS_LOG_INFO ("############ Fucntion: get_egress_port_id_by_path_tag() ############");
        uint32_t pathId = smartFlowTag.get_path_id();
        uint32_t hopIdx = smartFlowTag.get_the_current_hop_index();
        PathData *pitEntry = lookup_PIT(pathId);
        NS_ASSERT_MSG(pitEntry != 0 && hopIdx < pitEntry->portSequence.size(), "The hopIdx is larger than the pathSize");
        return pitEntry->portSequence[hopIdx];
    }

    std::vector<PathData *> RdmaSmartFlowRouting::get_the_piggyback_pit_entries(uint32_t srcToRId, uint32_t dstHostId)
    {
        // NS_LOG_INFO ("############ Fucntion: get_the_piggyback_pit_entries() ############");
        HostId2PathSeleKey pstKey(srcToRId, dstHostId);
        pstEntryData *pstEntry = lookup_PST(pstKey);
        std::vector<PathData *> allPitEntries = batch_lookup_PIT(pstEntry->paths);
        std::vector<PathData *> freshPitEntries = get_the_newly_measured_paths(allPitEntries);
        std::vector<PathData *> piggybackPitEntries = get_the_best_piggyback_paths(freshPitEntries);
        return piggybackPitEntries;
    }

    /* std::vector<uint32_t> RdmaSmartFlowRouting::get_dre_of_egress_ports(std::vector<uint32_t> &ports)
     {
         uint32_t n = ports.size();
         std::vector<uint32_t> dres(n);
         for (size_t i = 0; i < n; i++)
         {
             auto it = m_XMap.find(ports[i]);
             if (it == m_XMap.end())
             {
                 std::cout << "Error in get_dre_of_egress_ports() with invalid portId: " << ports[i] << std::endl;
                 PrintDreTable();
                 return dres;
             }
             else
             {
                 uint32_t dre = QuantizingX(it->second);
                 dres[i] = dre;
             }
         }
         return dres;
     }*/

    uint32_t RdmaSmartFlowRouting::forward_normal_packet(Ptr<Packet> &p, CustomHeader &ch, uint32_t srcHostId, uint32_t dstHostId, uint32_t pg, Ptr<E2ESrcOutPackets> &srcOutEntry)
    {
        // NS_LOG_INFO("PST strategy=" << m_pathSelStrategy << "<1:min, 2:rbnp, 3:rnd, 4:lspray, 5:flet, 6:ecmp, 7:rbnf, 8:rbnh>");
        // ############ Function: forward_normal_packet() ############");

        if (m_pathSelStrategy == PATH_SELECTION_SMALL_LATENCY_FIRST_OPTIMIZED_STRATEGY)
        {

            std::vector<PathData *> forwardPitEntries;
            HostId2PathSeleKey forwarPstKey(srcHostId, dstHostId);
            pstEntryData *forwardPstEntry = lookup_PST(forwarPstKey);
            NS_LOG_INFO("forward PST Entry");
            // forwardPstEntry->print();

            uint32_t forwardPathNum = forwardPstEntry->pathNum;
            NS_LOG_INFO("m_pathSelNum, forwardPathNum=" << m_pathSelNum << ", " << forwardPathNum);
            if (m_pathSelNum >= forwardPathNum)
            {
                NS_LOG_INFO("There is too Less available forwarding paths");
                forwardPitEntries = batch_lookup_PIT(forwardPstEntry->paths);
            }
            else
            {
                NS_LOG_INFO("There is too Many available forwarding paths, so to select " << m_pathSelNum << " paths");
                uint32_t rndPathIdStartValue = std::rand() % (forwardPathNum - m_pathSelNum + 1);
                for (uint32_t i = 0; i < m_pathSelNum; i++)
                {
                    // uint32_t rndPathIdx = std::rand() % forwardPathNum;
                    //  std::cout << "The " << i << "-th path index is " << rndPathIdx << std::endl;
                    uint32_t rndPathId = forwardPstEntry->paths[rndPathIdStartValue];
                    PathData *rndPitEntry = lookup_PIT(rndPathId);
                    forwardPitEntries.push_back(rndPitEntry);
                    rndPathIdStartValue++;
                }
            }
            NS_LOG_INFO("forward PIT Entries");
            // for (auto e : forwardPitEntries) {
            //     e->print();
            // }

            forward_probe_packet_optimized(p, forwardPitEntries, ch, pg, srcOutEntry); // probing

            HostId2PathSeleKey reversePstKey(dstHostId, srcHostId);
            pstEntryData *reversePstEntry = lookup_PST(reversePstKey);
            NS_LOG_INFO("reverse PST Entry");
            // reversePstEntry->print();
            uint32_t reversePathNum = reversePstEntry->pathNum;
            NS_LOG_INFO("m_piggyLatencyCnt, reversePathNum=" << m_piggyLatencyCnt << ", " << reversePathNum);

            std::vector<PathData *> reversePitEntries;
            if (m_piggyLatencyCnt >= reversePathNum)
            {
                NS_LOG_INFO("There is too Less available reverse paths");
                reversePitEntries = batch_lookup_PIT(reversePstEntry->paths);
            }
            else
            {
                NS_LOG_INFO("There is too Many available reverse paths, so to select " << m_piggyLatencyCnt << " paths");
                for (uint32_t i = 0; i < m_piggyLatencyCnt; i++)
                {
                    uint32_t rndPathIdx = std::rand() % reversePathNum;
                    // std::cout << "The " << i << "-th path index is " << rndPathIdx << std::endl;
                    uint32_t rndPathId = reversePstEntry->paths[rndPathIdx];
                    PathData *rndPitEntry = lookup_PIT(rndPathId);
                    reversePitEntries.push_back(rndPitEntry);
                }
            }
            NS_LOG_INFO("reverse PIT Entries");
            // for (auto e : reversePitEntries) {
            //     e->print();
            // }
            std::vector<PathData *> feedBackPitEntries;
            if (m_enableFeedbackAllPathInfo)
            {
                feedBackPitEntries = reversePitEntries;
            }
            else
            {
                feedBackPitEntries = get_the_newly_measured_paths(reversePitEntries);
            }

            // std::vector<PathData *> freshrPitEntries = get_the_newly_measured_paths(reversePitEntries);
            NS_LOG_INFO("piggyback PIT Entries");
            // for (auto e : freshrPitEntries) {
            //     e->print();
            // }
            if (m_enabledAllPacketFeedbackInfo)
            {
                add_latency_tag_by_pit_entries(p, feedBackPitEntries);
                update_PIT_after_piggybacking(feedBackPitEntries); // piggybacking
            }
            else if (ch.l3Prot == 0xFC || ch.l3Prot == 0xFD) // ACK or NACK piggybacking
            {
                add_latency_tag_by_pit_entries(p, feedBackPitEntries);
                update_PIT_after_piggybacking(feedBackPitEntries);
            }

            NS_LOG_INFO("PIT Entries after piggybacking");
            // for (auto e : freshrPitEntries) {
            //     e->print();
            // }
            std::sort(forwardPitEntries.begin(), forwardPitEntries.end(), cmp_pitEntry_in_increase_order_of_latency);
            PathData *curBestForwardPitEntry = forwardPitEntries[0];
            NS_LOG_INFO("The cur Best Forward Pit Entry");
            // curBestForwardPitEntry->print();

            pdt_entry_t *bestForwadPitEntry = lookup_PDT(forwarPstKey);
            if (bestForwadPitEntry == 0)
            {
                // std::cout << "The new flow, to build a PDT entry" << std::endl;
                pdt_entry_t pdtEntry = pdt_entry_t();
                // pdtEntry.pid = curBestForwardPitEntry->pid;
                // pdtEntry.latency = curBestForwardPitEntry->latency;
                pdtEntry.pit = curBestForwardPitEntry;
                m_pathDecTbl[forwarPstKey] = pdtEntry;
                // m_pathDecTbl[forwarPstKey].print();
                bestForwadPitEntry = &m_pathDecTbl[forwarPstKey];
            }
            else if (curBestForwardPitEntry->latency < bestForwadPitEntry->pit->latency)
            {
                NS_LOG_INFO("The exist flow, the better Pid, to update pid and latency in PDT entry");
                // bestForwadPitEntry->print();
                bestForwadPitEntry->pit = curBestForwardPitEntry;
                // bestForwadPitEntry->latency = curBestForwardPitEntry->latency;
                // bestForwadPitEntry->print();
            }

            else
            {
                NS_LOG_INFO("The exist flow, the worse Pid, use the previous PDT entry");
            }
            uint32_t bestForwardPid = bestForwadPitEntry->pit->pid;
            NS_LOG_INFO("The final forward path Id " << bestForwardPid);
            // std::cout << "bestForwardPid: "<< bestForwardPid << std::endl;
            add_path_tag_by_path_id(p, bestForwardPid);
            if (IsE2ELb())
            {
                srcOutEntry->dataPacket = p;
                e2eLBSrc_output_packet(srcOutEntry);
            }
            else
            {
                output_packet_by_path_tag(p, ch, pg); // sending
            }
            return p->GetSize();
        }
        else
        {
            NS_LOG_INFO("m_pathSelStrategy != PATH_SELECTION_SMALL_LATENCY_FIRST_OPTIMIZED_STRATEGY");
            return 0;
        }
    }

    uint32_t RdmaSmartFlowRouting::forward_probe_packet_optimized(Ptr<Packet> pkt, std::vector<PathData *> &forwardPitEntries, CustomHeader &ch, uint32_t pg, Ptr<E2ESrcOutPackets> &srcOutEntry)
    {
        FlowIdTag flowIdTag;
        NS_LOG_INFO("############ Fucntion: forward_probe_packet_optimized() ############");
        if (m_pathSelStrategy != PATH_SELECTION_SMALL_LATENCY_FIRST_OPTIMIZED_STRATEGY)
        {
            NS_LOG_INFO("m_pathSelStrategy != PATH_SELECTION_SMALL_LATENCY_FIRST_OPTIMIZED_STRATEGY");
            return 0;
        }

        std::vector<PathData *> expiredPitEntries, probePitEntries, potentialPitEntries;
        get_the_expired_paths(forwardPitEntries, expiredPitEntries);
        get_the_probe_paths(expiredPitEntries, probePitEntries);
        PathData *bestProbingPitEntry = get_the_best_probing_path(probePitEntries);
        if (bestProbingPitEntry == 0)
        {
            std::cout << "No need to probe" << std::endl;
            return 0;
        }
        std::cout << "The probe path" << std::endl;
        // bestProbingPitEntry->print();

        Ptr<Packet> probePacket = construct_probe_packet(pkt, ch);
        std::cout << "probePacket" << std::endl;
        add_path_tag_by_path_id(probePacket, bestProbingPitEntry->pid);
        add_probe_tag_by_path_id(probePacket, bestProbingPitEntry->pid);
        CustomHeader probech(CustomHeader::L2_Header | CustomHeader::L3_Header | CustomHeader::L4_Header);
        if (probePacket->PeekHeader(probech) > 0)
        {
            NS_LOG_INFO("Extracted CustomHeader:");
            if (!IsE2ELb())
            {
                // probePacket->AddPacketTag(FlowIdTag(1));
            }

            if (!probePacket->PeekPacketTag(flowIdTag))
            {
                // 如果标签不存在，则添加
                flowIdTag.SetFlowId(1);
                probePacket->AddPacketTag(flowIdTag);
            }
            else
            {
                NS_LOG_INFO("probePacket FlowIdTag already exists on packet " << probePacket->GetUid());
            }

            // probePacket->AddPacketTag(FlowIdTag(1));
        }
        else
        {
            NS_LOG_WARN("Failed to extract CustomHeader from the packet.");
        }
        uint32_t probepg = 3;
        if (IsE2ELb())
        {
            srcOutEntry->probePacket = probePacket;
            srcOutEntry->isProbe = true;
        }
        else
        {
            output_packet_by_path_tag(probePacket, probech, probepg);
        }
        update_PIT_after_probing(bestProbingPitEntry);

        // bestProbingPitEntry->print();
        record_the_probing_info(bestProbingPitEntry->pid);
        return probePacket->GetSize();
    }

    // void RdmaSmartFlowRouting::RouteInput (Ptr<const Packet> p, const Ipv4Header &header, Ptr<const NetDevice> idev,
    //  UnicastForwardCallback ucb, MulticastForwardCallback mcb, LocalDeliverCallback lcb, ErrorCallback ecb) {
    bool RdmaSmartFlowRouting::RouteInput(Ptr<Packet> p, CustomHeader ch)
    {
        NS_LOG_FUNCTION(this << m_node->GetId()
                             << p->GetSize()
                             << Simulator::Now().GetMicroSeconds()
                        );
        NS_LOG_INFO("#Node: " << m_node->GetId() << ", "
                    <<"Time: " << Simulator::Now().GetNanoSeconds() << " ns, "
                    << "Size: " << p->GetSize() << " Bytes, " 
                    << "Receive PktID:" << p->GetUid() << " "
                    );
        Ipv4SmartFlowPathTag pathTag;
        bool IsHavePathTag = exist_path_tag(p, pathTag);
        NS_ASSERT_MSG(IsHavePathTag, "The path tag does not exist");
        bool ShouldUpForward = true;

        PathData *pitEntry;
        bool isReachDst = reach_the_last_hop_of_path_tag(pathTag, pitEntry);
        // uint64_t pktId 
        if (isReachDst)
        {   
            Ipv4SmartFlowProbeTag probeTag;
            if (exist_probe_tag(p, probeTag))
            {   
                // receive_probe_packet(probeTag);
                ShouldUpForward = true;
            }
            else
            {   
                receive_normal_packet(p, pathTag, pitEntry);
            }
        }
        else
        { 
            output_packet_by_path_tag(p, ch, ch.udp.pg);
            ShouldUpForward = false;
        }
        return ShouldUpForward;
    }

// 计算Softmax函数
std::vector<double> RdmaSmartFlowRouting::CalPathWeightBasedOnDelay(const std::vector<PathData *> paths, u_int32_t ch_seq) {
    // Defensive API boundary: bandwidth callers cannot accidentally obtain ALPS weights.
    if(lcBwMode && !lcDiagDelayWeights){std::vector<double> w(paths.size(),0);ApplyPathLocalBandwidthWeights(paths,w);return w;}

    NS_LOG_FUNCTION(this << paths.size());
    auto maxElement = std::max_element(paths.begin(), paths.end(),
            [](const PathData* lhs, const PathData* rhs) 
            {
                return lhs->theoreticalSmallestLatencyInNs < rhs->theoreticalSmallestLatencyInNs;
            }
    );
    uint64_t maxBastDelay = (*maxElement)->theoreticalSmallestLatencyInNs;
    NS_LOG_INFO("The targetDelay is " << maxBastDelay << " ns");
    std::vector<double> weights(paths.size());
    double sum_weights = 0.0;
    // bool is9604 = false;
    Time t = Simulator::Now();
//=========================================新增前10kB数据优待部分=====================================
     bool is_first_10KB=false;
     if(ch_seq!=1&&ch_seq<=10000){
        is_first_10KB = true;
     }
      // 如果是第一个10KB数据，则找出延迟最小的路径并将其权重设为1，其他路径权重设为0
      //if(RdmaSmartFlowRouting::choose_softmax==2)
      if(false)
      {
        if (is_first_10KB) {
        // 找到延迟最小的路径索引
        size_t minLatencyIndex = 0;
        uint32_t minLatency = paths[0]->latency+1004*paths[0]->portSequence.size()+84*paths[0]->sent_pkt_num;
        // std::cout<<" ========================================================="<<std::endl;
        //  std::cout<<" 前10kB数据,seq:"<<ch_seq<<std::endl;
        for (size_t i = 1; i < paths.size(); i++) {
           // std::cout << "paths["<<i<<"]->latency " << paths[i]->latency << std::endl;
            if ((paths[i]->latency+1004*paths[i]->portSequence.size()+84*paths[i]->sent_pkt_num) < minLatency) {
                minLatency = paths[i]->latency+1004*paths[i]->portSequence.size()+84*paths[i]->sent_pkt_num;
                minLatencyIndex = i;
            }
        }
        
        // 将最小延迟路径的权重设为1，其他路径权重设为0
        for (size_t i = 0; i < weights.size(); i++) {
            if (i == minLatencyIndex) {
              //  std::cout << "权重为1的最短路径："<<minLatencyIndex<<"，latency " << paths[minLatencyIndex]->latency << std::endl;
                weights[i] = 1.0;
            } else {
                weights[i] = 0.0;
            }
        }
        
        return weights;
    }
      }
    

     bool is_have_good_path=false;
     double max_weight=0.0;
  //===========================================================修改部分=====================================
       //std::cout<<"=========================================================="<<std::endl;
  for (size_t i = 0; i < weights.size(); i++)
        { 
            auto maps = m_pathBdpMap.find(paths[i]->pid);
            PathBdpInfo it11 = maps->second;
            double multiplier;
            switch (choose_softmax)
            {
            case 0:
                multiplier = 0.2;
                break;
            case 1:
                multiplier = 5.0;
                break;
            case 2:
                multiplier = 5.0;
                break;
            case 3:
                multiplier = 5.0;
                break;
            case 4:
                multiplier = 5.0;
                break;
             case 5:
                multiplier = 5.0;
                break;
            default:
                // 可选：处理非法 choose_softmax 值，例如设为默认 multiplier 或跳过
                multiplier = laps_alpha; // 或者 throw / continue / assert 等
                break;
            }
            //========================拥塞影响部分============
            //  double random_value = rndGen(generator);
            // double congestionpercent=paths[i]->m_maxCongestionPercent;
            // double c=std::max(1 - congestionpercent*congestionpercent*1.67 , 0.0);
            // if(random_value >c )
            // {//拥塞有概率不选路径
            //     weights[i] = 0;
            //     continue;
            // }
             //================================================
           

            if (enable_laps_plus)
            { 
            double ratio = -1.0 * (paths[i]->latency+(lcSendPenalty?0.0:84.0*paths[i]->sent_pkt_num))/ maxBastDelay * multiplier;

             //   double ratio = -1.0 * (paths[i]->latency+paths[i]->d_t*(Simulator::Now().GetNanoSeconds() - paths[i]->tsGeneration.GetNanoSeconds()) )/ maxBastDelay * multiplier;
            //std::cout<<"pid "<<paths[i]->pid<<",paths["<<i<<"]->latency " << paths[i]->latency << ", paths["<<i<<"]->sent_pkt_num " << paths[i]->sent_pkt_num<<",sum"<< paths[i]->latency+84*paths[i]->sent_pkt_num <<std::endl;
           
           
           
            weights[i] = std::exp(ratio);
            //weights[i] = std::pow(2.0, ratio);


                //    if ((paths[i]->latency < paths[i]->theoreticalSmallestLatencyInNs)
            //     || ((Simulator::Now().GetNanoSeconds()-paths[i]->tsGeneration.GetNanoSeconds()) >paths[i]->theoreticalSmallestLatencyInNs))
            //     {
            //         weights[i] = std::exp(ratio);
            //     }
            //     else
            //     {
            //         // std::cout<<"该路径不被选择，理由:路径实时时延："<<paths[i]->latency<<" ns, 路径基准时延："
            //         // <<paths[i]->theoreticalSmallestLatencyInNs<<" ns, 路径距离上次更新时间："<<
            //         // Simulator::Now().GetNanoSeconds()-paths[i]->tsGeneration.GetNanoSeconds()<<std::endl;
            //         weights[i] = 0;
            //     }
            


                
            }
            else{
                double ratio = -1.0 * paths[i]->latency / maxBastDelay * multiplier;
                weights[i] = std::exp(ratio);
            }

            if(alpsStrictPrune && IsAlpsPathPruned(*paths[i],Simulator::Now().GetNanoSeconds()))weights[i]=0;
            sum_weights += weights[i];
        }
        //===========================================================修改部分=====================================
    
    NS_ASSERT_MSG(sum_weights >= 0.0, "The sum_weights is zero");
    if (sum_weights == 0.0)
    {
        return weights;
    }
    
    for (size_t i = 0; i < weights.size(); i++)
    {          
        weights[i] /= sum_weights;
        if( weights[i]>max_weight){
            max_weight=weights[i];

        }
        if(weights[i]>0.9)
        {
            is_have_good_path=true;
        }
        NS_LOG_INFO("Path: " << paths[i]->pid << ", " <<
                    "Realtime Delay: " << paths[i]->latency << ", " <<
                    "Mininal Delay: " << paths[i]->theoreticalSmallestLatencyInNs << ", " <<
                    "Weight: " << weights[i]
                   );
     

    }
    // if(is_have_good_path)
    // {
    //     std::cout<<"=========================================================="<<std::endl;
    //       for (size_t i = 0; i < weights.size(); i++)
    // {          
    //     std::cout<<"Path: " << paths[i]->pid << ", " <<
    //                 "Realtime Delay: " << paths[i]->latency << ", " <<
    //                 "Mininal Delay: " << paths[i]->theoreticalSmallestLatencyInNs << ", " <<
    //                 "Weight: " << weights[i]<<std::endl;
    // }
      
    // }
    //===================================测量各部分最大权重占的比例==========================================
         if(max_weight>0.99){
            RdmaSmartFlowRouting::s_relErrorStats.RecordHighestWeightCount(99); 
         }else{
            RdmaSmartFlowRouting::s_relErrorStats.RecordHighestWeightCount((int)(max_weight*10)*10);
         }

    return weights;
}

uint32_t RdmaSmartFlowRouting::GetPathBasedOnWeight(const std::vector<double> & weights) 
{
    const double random_value = rndGen(generator);
    // std::cout << "Random Value: " << random_value << std::endl;
    NS_ASSERT_MSG(random_value >= 0.0 && random_value <= 1.0, "Wrong random value");
    NS_ASSERT_MSG(weights.size() > 0, "The weights is empty");
    
    // double sum_weights = 0.0;
    // for (size_t i = 0; i < weights.size(); i++)
    // {
    //     sum_weights += weights[i];
    // }
    // if (sum_weights == 0.0)
    // {
    //     return std::rand() % weights.size();
    // }
    
    double cumulative_weight = 0.0;
    for (size_t i = 0; i < weights.size(); ++i) 
    {
        cumulative_weight += weights[i];
        if (random_value < cumulative_weight) 
        {
            return i;
        }
    }
    return weights.size() - 1;
}


    void RdmaSmartFlowRouting::RouteOutput(Ptr<Packet> p, CustomHeader ch, Ptr<E2ESrcOutPackets> &srcOutEntry)
    {
        NS_LOG_FUNCTION(this << m_node->GetId()
                             << p->GetSize()
                             << Ipv4Address(ch.sip)
                             << Ipv4Address(ch.dip)
                             << Simulator::Now().GetMicroSeconds()
                        );

        Ipv4Address srcServerAddr = Ipv4Address(ch.sip);
        Ipv4Address dstServerAddr = Ipv4Address(ch.dip);
        uint32_t srcHostId = lookup_SMT(srcServerAddr)->hostId;
        uint32_t dstHostId = lookup_SMT(dstServerAddr)->hostId;

        NS_ASSERT_MSG(p != 0 && srcOutEntry != 0, "The packet or srcOutEntry is null");

        // forwarding
        srcOutEntry->dataPacket = ConstCast<Packet>(p);
        Ipv4SmartFlowPathTag pathTag;
        bool IsHavePathTag = exist_path_tag(srcOutEntry->dataPacket, pathTag);
        NS_ASSERT_MSG(!IsHavePathTag, "Should not have path tag");
        HostId2PathSeleKey pstKey(srcHostId, dstHostId);
        pstEntryData *pstEntry = lookup_PST(pstKey);
        std::vector<PathData *> pitEntries = batch_lookup_PIT(pstEntry->paths);
        std::vector<double> weights = CalPathWeightBasedOnDelay(pitEntries);
        uint32_t selPathIndex = GetPathBasedOnWeight(weights);
        NS_ASSERT_MSG(selPathIndex < pitEntries.size(), "The selected path index is out of range");
        uint32_t fPid = pitEntries[selPathIndex]->pid;
        add_path_tag_by_path_id(srcOutEntry->dataPacket, fPid);
        update_PIT_after_adding_path_tag(pitEntries[selPathIndex]);
        // piggybacking if ack
        if (srcOutEntry->isAck)
        {
            HostId2PathSeleKey reversePstKey(dstHostId, srcHostId);
            pstEntryData *reversePstEntry = lookup_PST(reversePstKey);
            NS_ASSERT_MSG(reversePstEntry != 0, "The reversePstEntry is null");
            std::vector<PathData *> reversePitEntries = batch_lookup_PIT(reversePstEntry->paths);
            add_latency_tag_by_pit_entries(srcOutEntry->dataPacket, reversePitEntries);
            update_PIT_after_piggybacking(reversePitEntries); // piggybacking
        }

        // probing
        std::vector<PathData *> expiredPitEntries, probePitEntries;
        get_the_expired_paths(pitEntries, expiredPitEntries);
        get_the_probe_paths(expiredPitEntries, probePitEntries);
        PathData *probePitEntry = get_the_best_probing_path(probePitEntries);
        NS_ASSERT_MSG((probePitEntries.size() != 0 && probePitEntry != 0 && fPid == probePitEntry->pid) || (probePitEntries.size() == 0 && probePitEntry == 0), "The probePitEntry is null");
        if (probePitEntry == 0)
        {
            srcOutEntry->probePacket = 0;
            srcOutEntry->isProbe = false;
        }
        else
        {
            srcOutEntry->isProbe = true;
            srcOutEntry->probePacket = construct_probe_packet(srcOutEntry->dataPacket, ch);
            add_path_tag_by_path_id(srcOutEntry->probePacket, probePitEntry->pid);
            add_probe_tag_by_path_id(srcOutEntry->probePacket, probePitEntry->pid);
            update_PIT_after_probing(probePitEntry);
            record_the_probing_info(probePitEntry->pid);
        }

        return;
    }



    void RdmaSmartFlowRouting::RouteOutputForDataPktOnSrcHostForLaps(Ptr<E2ESrcOutPackets> entry)
    {
        NS_LOG_FUNCTION(this << "Node: " << m_nodeId);
		NS_ASSERT_MSG(entry && entry->isData && !entry->isAck, "invalid entry");

		Ptr<Packet> p = entry->dataPacket;
		CustomHeader ch(CustomHeader::L2_Header | CustomHeader::L3_Header | CustomHeader::L4_Header);
        uint32_t size = p->PeekHeader(ch);
        NS_ASSERT_MSG(size > 0, "Failed to extract CustomHeader");
        Ipv4SmartFlowPathTag pathTag;
        bool IsHavePathTag = exist_path_tag(p, pathTag);
        NS_ASSERT_MSG(!IsHavePathTag, "Should not have path tag");

        // forwarding
        // std::cout << "11111111" << std::endl;

        Ipv4Address srcServerAddr = Ipv4Address(ch.sip);
        Ipv4Address dstServerAddr = Ipv4Address(ch.dip);
        // std::cout << ipv4Address2string(srcServerAddr) << " # " << ipv4Address2string(dstServerAddr) << std::endl;
        uint32_t srcHostId = lookup_SMT(srcServerAddr)->hostId;
        uint32_t dstHostId = lookup_SMT(dstServerAddr)->hostId;
        HostId2PathSeleKey pstKey(srcHostId, dstHostId);
        pstEntryData *pstEntry = lookup_PST(pstKey);
        std::vector<PathData *> pitEntries = batch_lookup_PIT(pstEntry->paths);

        // BW_EXP_ONLY_V1: independent ALPS and bandwidth allocation branches.
        std::vector<double> weights = (lcBwMode && !lcDiagDelayWeights)?std::vector<double>(pitEntries.size(),1.0):CalPathWeightBasedOnDelay(pitEntries,ch.udp.seq); // BW_EXP_ONLY_V1: no latency prior
        bool small=lcSizeAware && IsSmallTransfer(entry->lastQp);
        bool firstSmall=small && entry->lastQp->m_sizePinnedPath==UINT32_MAX;
        if(lcSizeAware){SizeClassTag old;p->RemovePacketTag(old);SizeClassTag cls;cls.value=small?1:2;p->AddPacketTag(cls);}
        ApplyLinkCongestionPruning(pitEntries,weights); // Existing physical queue eligibility; no small capacity filter.
        std::vector<double> probeEligible;if(lcEarlyProbe)probeEligible=lcBwCc?std::vector<double>(pitEntries.size(),1.0):weights;
        if(!small && !lcDiagDelayWeights)ApplyBottleneckBandwidthWeights(pitEntries,weights);

        uint32_t selPathIndex = small?SelectSmallFlowIndex(entry->lastQp,pitEntries,weights):GetPathBasedOnWeight(weights);
        if(lcDynamicSmall && small)m_dynamicSmallFlows[entry->lastQp->m_flow_id]=entry->lastQp;
        if(firstSmall && lcSizeFlowLog)lcSizeFlowLog<<Simulator::Now().GetNanoSeconds()<<','<<(m_node?m_node->GetId():m_nodeId)<<','<<entry->lastQp->m_flow_id<<','<<entry->lastQp->m_size<<",pin,"<<pitEntries[selPathIndex]->pid<<','<<lcSmallReservePercent<<'\n';
        NS_ASSERT_MSG(selPathIndex < pitEntries.size(), "The selected path index is out of range");
        uint32_t fPid = pitEntries[selPathIndex]->pid;
        if(lcWaitAudit && lcWaitNodes.count(m_nodeId)) {
            lcWaitChoices<<Simulator::Now().GetNanoSeconds()<<','<<m_nodeId<<','<<entry->lastQp->m_flow_id<<','<<ch.udp.seq<<','<<fPid<<','<<weights[selPathIndex]<<','<<pitEntries[selPathIndex]->latency<<','<<pitEntries[selPathIndex]->theoreticalSmallestLatencyInNs<<','<<entry->lastQp->laps.m_curRate.GetBitRate()<<'\n';
        }
        if(alpsStrictPrune){
            uint64_t now=Simulator::Now().GetNanoSeconds();uint32_t blocked=0;double sum=0,prunedMax=0;
            for(size_t i=0;i<pitEntries.size();++i){sum+=weights[i];if(IsAlpsPathPruned(*pitEntries[i],now)){++blocked;prunedMax=std::max(prunedMax,weights[i]);}}
            NS_ABORT_MSG_IF(prunedMax!=0 || std::abs(sum-1)>1e-9 || IsAlpsPathPruned(*pitEntries[selPathIndex],now) || weights[selPathIndex]<=0,"Strict-pruned DATA path selected or weights invalid");
            auto selected=pitEntries[selPathIndex];
            if(alpsStrictDataLog)alpsStrictDataLog<<now<<','<<m_nodeId<<','<<entry->lastQp->m_flow_id<<','<<fPid<<','<<ch.udp.seq<<','<<blocked<<','<<pitEntries.size()<<','<<selected->latency<<','<<selected->theoreticalSmallestLatencyInNs<<','<<selected->tsGeneration.GetNanoSeconds()<<','<<weights[selPathIndex]<<','<<sum<<','<<prunedMax<<'\n';
        }
        if(lcDisablePruning){
            auto raw=EvaluateBandwidthCongestion(pitEntries,Simulator::Now().GetNanoSeconds());
            BandwidthCongestionResult protocol;protocol.blocked.assign(pitEntries.size(),false);protocol.known.assign(pitEntries.size(),false);
            ApplyProtocolUnavailable(pitEntries,protocol);double sum=0;for(double weight:weights)sum+=weight;
            NS_ABORT_MSG_IF(protocol.blocked[selPathIndex] || weights[selPathIndex]<=0 || std::abs(sum-1)>1e-9,"Invalid DATA path in no-pruning mode");
            if(lcNoPruneDataLog)lcNoPruneDataLog<<Simulator::Now().GetNanoSeconds()<<','<<m_nodeId<<','<<entry->lastQp->m_flow_id<<','<<fPid<<','<<ch.udp.seq<<','<<raw.blockedCount<<','<<protocol.blockedCount<<','<<pitEntries.size()<<','<<raw.blocked[selPathIndex]<<','<<protocol.blocked[selPathIndex]<<','<<weights[selPathIndex]<<','<<sum<<'\n';
        }
        AuditBandwidthPathChoice(pitEntries,weights,selPathIndex,p->GetSize()); // BANDWIDTH_WEIGHT_V1

           //============记录每条路径发送数据量====================================
           //一条路径的排除
           if(pstEntry->paths.size()!=1){
            RdmaSmartFlowRouting:: record_path_cal[pstKey].record_path_send[fPid].data_byte += p->GetSize();
           RdmaSmartFlowRouting:: record_path_cal[pstKey].sum_data_byte += p->GetSize();
           RdmaSmartFlowRouting:: record_path_cal[pstKey].record_path_send[fPid].path_length = pitEntries[selPathIndex]->portSequence.size();
           }
         
     




        add_path_tag_by_path_id(entry->dataPacket, fPid);

//====================================增加对应端口的数据包记录，用来记录实际端口利用率==========
            uint32_t port_id = pitEntries[selPathIndex]->portSequence[0];
           // std::cout<<1<<std::endl;
            if(record_each_port_send_data[port_id].send_data_byte==0){
                record_each_port_send_data[port_id].time=Simulator::Now().GetNanoSeconds();
                  Ptr<RdmaDriver> rdma = m_node->GetObject<RdmaDriver>();
                  if(rdma==NULL){
                    std::cout<<"NULL"<<std::endl;
                  }
                record_each_port_send_data[port_id].port_rate = rdma->m_rdma->m_nic[port_id].dev->GetDataRate().GetBitRate();
                  
                

            }
           record_each_port_send_data[port_id].send_data_byte+=p->GetSize();
          // std::cout <<"端口"<<port_id<< " 发送数据包" <<record_each_port_send_data[port_id].send_data_byte<<std::endl;
    //====================================增加对应端口的数据包记录，用来记录实际端口利用率==========
        
        
           //==================================增 加 B D P======================================================================
           if (RdmaSmartFlowRouting::enable_laps_plus&&m_pathBdpMap.find(fPid) != m_pathBdpMap.end()) {
            m_pathBdpMap[fPid].currentBdp += p->GetSize();
            //std::cout << "当前BDP: " << m_pathBdpMap[fPid].currentBdp << "MAXBDP: " << m_pathBdpMap[fPid].maxBdp <<"，数据包大小"<< p->GetSize()<< std::endl;
           }

        NS_ASSERT_MSG(entry->lastQp, "The lastQp is null");
        if (!m_recordPathLantencyEvent.IsRunning() && enableRecordPathlatency)
        {
            // NS_LOG_INFO("ConWeave routing restarts aging event scheduling:" << m_switch_id << now);
            m_recordPathLantencyEvent = Simulator::Schedule(m_RecordTimeGap, &RdmaSmartFlowRouting::pathDelayRecord, this);
        }

        if (Irn::mode == Irn::Mode::NACK)
        {
            uint32_t payload_size = p->GetSize() - ch.GetSerializedSize();
            // entry->lastQp->m_irn.m_sack.appendOutstandingData(fPid, ch.udp.seq, payload_size);
			// NS_LOG_INFO("PktId: " << p->GetUid()  << " Type: DATA, Size: " << payload_size <<	" Pid: " << fPid);
            entry->latencyForDataPktInNs = pitEntries[selPathIndex]->latency;
            entry->pidForDataPkt = fPid;
        }
        update_PIT_after_adding_path_tag(pitEntries[selPathIndex]);

       //===============================记录该路径距离上次路径时延更新后发了多少包===================================
        pitEntries[selPathIndex]->sent_pkt_num++;
        RecordFeedbackSendCounter(fPid,"data_send",pitEntries[selPathIndex]->sent_pkt_num-1,pitEntries[selPathIndex]->sent_pkt_num);
         //std::cout<<"Data PktId: "<<p->GetUid()<<" Pid: "<<fPid<<" "<<pitEntries[selPathIndex]->sent_pkt_num<<std::endl;

        // probing
        PathData *prbeEntry = lcEarlyProbe?SelectEarlyProbe(pitEntries,probeEligible,Simulator::Now().GetNanoSeconds()):CheckProbePathAmoungPitEntries(pitEntries);
        if (prbeEntry != 0)
        {
            entry->isProbe = true;
            entry->probePacket = construct_probe_packet(entry->dataPacket, ch);
             //std::cout<<"PktId: "<<entry->probePacket->GetUid()<<" Pid: "<<prbeEntry->pid<<std::endl;
            add_path_tag_by_path_id(entry->probePacket, prbeEntry->pid);
            add_probe_tag_by_path_id(entry->probePacket, prbeEntry->pid);
			// NS_LOG_INFO("PktId: " << entry->probePacket->GetUid()  << " Type: Probe, Size: " << entry->probePacket->GetSize()-ch.GetSerializedSize() <<	" Pid: " << prbeEntry->pid);
            update_PIT_after_probing(prbeEntry);
            record_the_probing_info(prbeEntry->pid);
            RecordEarlyProbeSent(prbeEntry->pid,Simulator::Now().GetNanoSeconds()); // EARLY_GHOST_V1
        }else{
            entry->isProbe = false;
            entry->probePacket = NULL;
        }

        return;
    }


    void RdmaSmartFlowRouting::RouteOutputForAckPktOnSrcHostForLaps(Ptr<E2ESrcOutPackets> entry)
    {
        NS_LOG_FUNCTION(this << "Node: " << m_nodeId);
		NS_ASSERT_MSG(entry && !entry->isData && entry->isAck, "invalid entry");
        


		Ptr<Packet> p = entry->ackPacket;
		CustomHeader ch(CustomHeader::L2_Header | CustomHeader::L3_Header | CustomHeader::L4_Header);

        uint32_t size = p->PeekHeader(ch);
        NS_ASSERT_MSG(size > 0, "Failed to extract CustomHeader");
        Ipv4SmartFlowPathTag pathTag;

        bool IsHavePathTag = exist_path_tag(p, pathTag);
        NS_ASSERT_MSG(!IsHavePathTag, "Should not have path tag");
        AckPathTag ackTag;
        bool ishaveAckTag = exist_ack_tag(p, ackTag);
        NS_ASSERT_MSG((Irn::mode == Irn::Mode::NACK) ^ (!ishaveAckTag), "Should not have ack tag xor nack tag");
        // forwarding
        Ipv4Address srcServerAddr = Ipv4Address(ch.sip);
        Ipv4Address dstServerAddr = Ipv4Address(ch.dip);
        uint32_t srcHostId = lookup_SMT(srcServerAddr)->hostId;
        uint32_t dstHostId = lookup_SMT(dstServerAddr)->hostId;
        HostId2PathSeleKey pstKey(srcHostId, dstHostId);
        pstEntryData *pstEntry = lookup_PST(pstKey);
        std::vector<PathData *> pitEntries = batch_lookup_PIT(pstEntry->paths);
        NS_ASSERT_MSG(pitEntries.size() > 0, "The pitEntries is empty");
        uint32_t selPathIndex = 0;
        if (Irn::mode == Irn::Mode::NACK)
        {
            // std::vector<uint32_t> key = {ackTag.GetPathId(), ackTag.GetFlowId()};
            // size_t val = GetHashValue<uint32_t>(key);
            // selPathIndex = val % pitEntries.size();
        }
        else if (Irn::mode == Irn::Mode::IRN_OPT)
        {
            std::vector<double> weights = CalPathWeightBasedOnDelay(pitEntries);
            //std::cout<<"PIT size: "<<pitEntries.size()<<std::endl;
            selPathIndex = GetPathBasedOnWeight(weights);
        }
        else
        {
            NS_ASSERT_MSG(false, "The mode is invalid");
        }
          

        NS_ASSERT_MSG(selPathIndex < pitEntries.size(), "The selected path index is out of range");
        // uint32_t fPid = pitEntries[selPathIndex]->pid;
        auto it_rPatPid = pathPair.find(ackTag.GetPathId());
        NS_ASSERT_MSG(it_rPatPid != pathPair.end(), "The pathPair is not found");
        uint32_t fPid = it_rPatPid->second;
        // std::cout << "data Pid: " << ackTag.GetPathId() << ", ack pid: " << fPid << std::endl;
        add_path_tag_by_path_id(entry->ackPacket, fPid);
	    // NS_LOG_INFO("PktId: " << p->GetUid()  << " Type: ACK, Size: " << p->GetSize()-ch.GetSerializedSize() <<	" fPid: " << fPid << " rPid: " << ackTag.GetPathId());

        // probing
        // PathData *prbeEntry = CheckProbePathAmoungPitEntries(pitEntries);
        // if (prbeEntry != 0)
        // {
        //     entry->isProbe = true;
        //     entry->probePacket = construct_probe_packet(entry->ackPacket, ch);
        //     add_path_tag_by_path_id(entry->probePacket, prbeEntry->pid);
        //     add_probe_tag_by_path_id(entry->probePacket, prbeEntry->pid);
		// 	// NS_LOG_INFO("PktId: " << entry->probePacket->GetUid()  << " Type: Probe, Size: " << entry->probePacket->GetSize()-ch.GetSerializedSize() <<	" Pid: " << prbeEntry->pid);
        //     update_PIT_after_probing(prbeEntry);
        //     record_the_probing_info(prbeEntry->pid);
        // }else{
        //     entry->isProbe = false;
        //     entry->probePacket = NULL;
        // }
            entry->isProbe = false;
            entry->probePacket = NULL;
            //====================================增加对应端口的ACK包记录，用来记录实际端口利用率==========
             PathData *pitEntry =lookup_PIT(fPid);
            uint32_t port_id = pitEntry->portSequence[0];
           // std::cout<<1<<std::endl;
            if(record_each_port_send_data[port_id].send_data_byte==0){
                record_each_port_send_data[port_id].time=Simulator::Now().GetNanoSeconds();
                  Ptr<RdmaDriver> rdma = m_node->GetObject<RdmaDriver>();
                  if(rdma==NULL){
                    std::cout<<"NULL"<<std::endl;
                  }
                record_each_port_send_data[port_id].port_rate = rdma->m_rdma->m_nic[port_id].dev->GetDataRate().GetBitRate();
                   
                

            }
           record_each_port_send_data[port_id].send_data_byte+=p->GetSize();
           //std::cout<<"ACK PktId: "<<p->GetUid()<<" Pid: "<<fPid<<" "<<pitEntries[selPathIndex]->sent_pkt_num<<std::endl;
          //记录ACK发送时间
           RecordAckSendTime[p->GetUid()]=Simulator::Now().GetNanoSeconds();
           //std::cout <<"端口"<<port_id<< " 发送数据包" <<p->GetSize()<<std::endl;
    //====================================增加对应端口的数据包记录，用来记录实际端口利用率==========
        
        // // piggybacking
        // HostId2PathSeleKey reversePstKey(dstHostId, srcHostId);
        // pstEntryData *reversePstEntry = lookup_PST(reversePstKey);
        // NS_ASSERT_MSG(reversePstEntry != 0, "The reversePstEntry is null");
        // std::vector<PathData *> reversePitEntries = batch_lookup_PIT(reversePstEntry->paths);
        // add_latency_tag_by_pit_entries(p, reversePitEntries);
        // update_PIT_after_piggybacking(reversePitEntries);

        return;
    }

    PathData * RdmaSmartFlowRouting::CheckProbePathAmoungPitEntries(std::vector<PathData *> & pitEntries)
    {
        NS_LOG_FUNCTION(this << m_nodeId);
        std::vector<PathData *> expiredPitEntries, probePitEntries;
        get_the_expired_paths(pitEntries, expiredPitEntries);
        get_the_probe_paths(expiredPitEntries, probePitEntries);
        PathData *probePitEntry = get_the_best_probing_path(probePitEntries);
        NS_ASSERT_MSG((probePitEntries.size() != 0 && probePitEntry != 0) || (probePitEntries.size() == 0 && probePitEntry == 0), "The probePitEntry is null");
        return probePitEntry;
    }

    void RdmaSmartFlowRouting::pathDelayRecord()
    {

        for (auto it = m_nexthopSelTbl.begin(); it != m_nexthopSelTbl.end(); it++)
        {
            uint32_t pid = it->first;
            uint32_t latency = it->second.latency;
            m_pathDelayRecordTable[pid].push_back(latency);
        }
        m_recordPathLantencyEvent = Simulator::Schedule(m_RecordTimeGap, &RdmaSmartFlowRouting::pathDelayRecord, this);
    }

    void RdmaSmartFlowRouting::set_PST(std::map<HostId2PathSeleKey, pstEntryData> &pathSelTbl)
    {
        m_pathSelTbl.insert(pathSelTbl.begin(), pathSelTbl.end());
        // NS_LOG_INFO ("m_pathSelTbl size " << m_pathSelTbl.size());
        return;
    }

    std::map<HostId2PathSeleKey, pstEntryData> RdmaSmartFlowRouting::get_PST() const
    {
        return m_pathSelTbl;
    }

    void RdmaSmartFlowRouting::set_PIT(std::map<uint32_t, PathData> &nexthopSelTbl)
    {
        m_nexthopSelTbl.insert(nexthopSelTbl.begin(), nexthopSelTbl.end());
        // NS_LOG_INFO ("m_nexthopSelTbl size " << m_nexthopSelTbl.size());
        return;
    }

    std::map<uint32_t, PathData> RdmaSmartFlowRouting::get_PIT() const
    {
        return m_nexthopSelTbl;
    }

    bool RdmaSmartFlowRouting::insert_entry_to_PIT(PathData &pitEntry)
    {
        uint32_t pid = pitEntry.pid;
        auto it = m_nexthopSelTbl.find(pid);
        if (it == m_nexthopSelTbl.end())
        {
            m_nexthopSelTbl[pid] = pitEntry;
            return true;
        }
        else
        {
            // print_PIT();
            // pitEntry.print();
            NS_ASSERT_MSG(false, "The entry already exists in PIT");
            return false;
        }

    }

    bool RdmaSmartFlowRouting::insert_entry_to_SMT(hostIp2SMT_entry_t &smtEntry)
    {
        Ipv4Address key = smtEntry.hostIp;
        auto it = m_vmVtepMapTbl.find(key);
        if (it == m_vmVtepMapTbl.end())
        {
            m_vmVtepMapTbl[key] = smtEntry;
            return true;
        }
        else
        {
            NS_ASSERT_MSG(false, "The entry already exists in SMT");
            return false;
        }
    }


    bool RdmaSmartFlowRouting::insert_entry_to_PST(pstEntryData &pstEntry)
    {
        HostId2PathSeleKey pstKey = pstEntry.key;
        auto it = m_pathSelTbl.find(pstKey);
        if (it == m_pathSelTbl.end())
        {
            m_pathSelTbl[pstKey] = pstEntry;
            return true;
        }
        else
        {
            print_PST();
            NS_ASSERT_MSG(false, "The entry already exists in PST");
            return false;
        }
    }


    void RdmaSmartFlowRouting::set_SMT(std::map<Ipv4Address, hostIp2SMT_entry_t> &vmVtepMapTbl)
    {
        m_vmVtepMapTbl.insert(vmVtepMapTbl.begin(), vmVtepMapTbl.end());
        // NS_LOG_INFO ("m_vmVtepMapTbl size " << m_vmVtepMapTbl.size());
        return;
    }

    std::map<Ipv4Address, hostIp2SMT_entry_t> RdmaSmartFlowRouting::get_SMT() const
    {
        return m_vmVtepMapTbl;
    }

    void RdmaSmartFlowRouting::set_probing_interval(uint32_t probeTimeInterval)
    {
        m_probeTimeInterval = probeTimeInterval;
        return;
    }

    uint32_t RdmaSmartFlowRouting::get_probing_interval() const
    {
        return m_probeTimeInterval;
    }

    void RdmaSmartFlowRouting::set_path_expire_interval(uint32_t a)
    {
        m_pathExpiredTimeThld = a;
        return;
    }
    uint32_t RdmaSmartFlowRouting::get_path_expire_interval() const
    {
        return m_pathExpiredTimeThld;
    }

    void RdmaSmartFlowRouting::set_max_piggyback_path_number(uint32_t piggyLatencyCnt)
    {
        m_piggyLatencyCnt = piggyLatencyCnt;
        return;
    }

    uint32_t RdmaSmartFlowRouting::get_max_piggyback_path_number() const
    {
        return m_piggyLatencyCnt;
    }
    bool RdmaSmartFlowRouting::IsE2ELb(void)
    {
        return lb_isInstallSever;
    }
    void RdmaSmartFlowRouting::record_the_probing_info(uint32_t pathId)
    {
        if (pathId >= m_prbInfoTable.size())
        {
            m_prbInfoTable.resize(pathId + 1);
            m_prbInfoTable[pathId].pathId = pathId;
            m_prbInfoTable[pathId].probeCnt = 0;
        }
        m_prbInfoTable[pathId].probeCnt = m_prbInfoTable[pathId].probeCnt + 1;

        // auto it = ;
        // it = m_vmVtepMapTbl.find(serverAddr);
        // probeInfoEntry prbEntry;
        // prbEntry.nodeId = nodeId;
        // prbEntry.pathId = pathId;
        // prbEntry.sendTime = Simulator::Now().GetNanoSeconds();
        // // std::cout<<"%%%%%%%%%%%%%%%%NodeId:"<<prbEntry.pathId<<"\t PathId:"<<prbEntry.pathId<<"\t TimeStamp:"<<prbEntry.sendTime<<std::endl;
        // m_prbInfoTable.push_back(prbEntry);
    }

    //======================================检查是否BDPjun满了==========================

    uint32_t RdmaSmartFlowRouting::IsBDPAllFull(Ipv4Address srcServerAddr, Ipv4Address dstServerAddr)
    {
        if (!RdmaSmartFlowRouting::enable_laps_plus)
        {
            return false;
        }
        uint32_t srcHostId = lookup_SMT(srcServerAddr)->hostId;
        uint32_t dstHostId = lookup_SMT(dstServerAddr)->hostId;
        HostId2PathSeleKey pstKey(srcHostId, dstHostId);
        pstEntryData *pstEntry = lookup_PST(pstKey);
         uint32_t counts = 0;
        std::vector<uint32_t> pids = pstEntry->paths;
        for (uint32_t i = 0; i < pids.size(); i++)
        {
            auto maps = m_pathBdpMap.find(pids[i]);
            PathBdpInfo it = maps->second;
            if (it.currentBdp < it.maxBdp)
            {
                counts++;
            }
        }

        // if(counts <pids.size()){
        //     std::vector<PathData *> pitEntries =batch_lookup_PIT(pstEntry->paths);
        //     std::cout<<"================bdp有一个满了，降速================="<<std::endl;
        //     uint32_t sum_currentBdp = 0;
        //     uint32_t sum_maxBdp = 0;
        //     for(uint32_t i = 0; i < pids.size(); i++){
        //         auto maps = m_pathBdpMap.find(pids[i]);
        //         PathBdpInfo it = maps->second;
        //       std::cout<<"BDP:"<<it.currentBdp<<"， maxBdp "<<it.maxBdp<<"，使用率："<<(double)(it.currentBdp)/it.maxBdp
        //       <<"路径长度："<<pitEntries[i]->portSequence.size()<<",当前时延："<<pitEntries[i]->latency<<std::endl;
        //     sum_currentBdp += it.currentBdp;
        //     sum_maxBdp += it.maxBdp;
        //     }
        //     std::cout<<"总使用率："<<(double)(sum_currentBdp)/sum_maxBdp<<std::endl;
        // }

        if (counts <pids.size()&&counts>0)
        {   if(RdmaSmartFlowRouting::choose_softmax==1)
            return 2;//存在满BDP，降速
            else
            return 0;
        }
        else if (counts == pids.size())
        {
            return 1;//不 存在满BDP，加速
        }
        else
        {
            return 2;//BDP全部满了减速
        }
        
        
          
    }

    void RdmaSmartFlowRouting::RecordAllPortQueueLength()
{
    uint64_t currentTime = Simulator::Now().GetNanoSeconds();
    
    // 遍历所有节点
    for (const auto& nodePair : nodeIdToNodeMap) 
    {
        uint32_t nodeId = nodePair.first;
        Ptr<Node> node = nodePair.second;
        
        // 获取交换机节点
        Ptr<SwitchNode> swNode = DynamicCast<SwitchNode>(node);
        if (swNode == nullptr) {
            continue; // 跳过非交换机节点
        }
        
        // 遍历所有端口
        for (uint32_t portId = 1; portId <=swNode->GetNDevices(); portId++) 
        {
            uint32_t totalQueueLength = 0;
            
            // 计算所有优先级队列的总长度（以字节为单位）
            for (int priority = 0; priority < 8; priority++) {
                totalQueueLength += swNode->m_mmu->egress_bytes[portId][priority];
            }
            
            // 创建记录条目
            record_queue_length queueRecord;
            queueRecord.length = totalQueueLength;
            
            
            // 记录到静态变量中
            record_all_port_queue_len[nodeId][portId].push_back(queueRecord);
        }
    }
}

}
namespace ns3 {
bool RdmaSmartFlowRouting::RunQueueBandwidthScoreSelfChecks(){
    bool ok=true;uint32_t count=0;auto ck=[&](bool v,const char *name){++count;if(!v){ok=false;std::cerr<<"QUEUE_BW_SCORE_FAIL "<<name<<std::endl;}};
    auto near=[](double a,double b){return std::abs(a-b)<1e-9;};
    CompactLinkState::queueScoreEnabled=true;
    for(int32_t q:{-16382,-500,-1,0,1,1000,16382}){CompactLinkState a;a.availableQ=CompactLinkState::EncodeScoreQ(q);
        ck(a.SignedScoreQ()==q,"signed_round_trip");ck(a.CapacityQ()==std::max(0,q),"positive_projection");
        a.availableQ|=0x8000;ck(a.Congested()&&a.SignedScoreQ()==q,"queue_flag_preserves_payload");}
    ck(CompactLinkState().CapacityQ()==0xffff,"unknown_sentinel");CompactLinkState unknown;unknown.availableQ=0xfffe;ck(unknown.CapacityQ()==0xffff&&unknown.Congested(),"queue_only_unknown");
    ck(sizeof(CompactLinkState)==4&&sizeof(LinkCapacityRecord)==8&&sizeof(EndpointLinkState)==6,"no_link_state_growth");
    lcEnabled=true;lcBwCc=true;lcBwMode=2;lcBwAudit=false;lcQuotaAudit=false;lcBwBeta=5;lcBwUnitGbps=100;
    lcDisablePruning=true;lcPathTimeout=false;lcTtlNs=100000;lcMaxLinkQueue=true;lcPathOnlyQueue=true;lcPathQueueMax=true;lcQueueAlarmBytes=100000;lcZeroBwMinProbability=.05;lcAlpsProbe=true;
    auto r=CreateObject<RdmaSmartFlowRouting>();r->m_nodeId=1000;
    PathData a,b,c;std::vector<PathData*>p={&a,&b,&c};
    for(size_t i=0;i<p.size();++i){p[i]->pid=i+1;p[i]->nodeIdSequence={1000,uint32_t(i+1),9,99};p[i]->portSequence={1,1,1};r->m_nexthopSelTbl[i+1]=*p[i];}
    auto load=[&](uint32_t node,int32_t q){auto &s=r->m_linkStateTable[MakeLinkId(node,1)];s.availableQ=CompactLinkState::EncodeScoreQ(q);s.sampleEpoch=0;};
    load(1,500);load(2,300);load(3,200);load(9,400);SharedQuotaResult quota;
    ck(r->ComputeSharedQuota(p,{true,true,true},0,quota)&&near(quota.budgetQ[0],200)&&near(quota.budgetQ[1],120)&&near(quota.budgetQ[2],80),"shared_credit_once_20_12_8");
    load(1,-500);load(2,0);load(3,800);load(9,2000);
    std::vector<double>w={.9,.05,.05};r->ApplyBottleneckBandwidthWeights(p,w);
    ck(w[0]>0 && w[0]<.01 && near(w[1],.05) && w[2]>.94,"negative_not_zero_or_floor");
    ck(near(w[0]/w[2],std::exp(-6.5)),"negative_vs_positive_exponent_ratio");
    ck(r->m_earlyProbes[1].lastBudgetQ==-500,"signed_cache_saved");r->m_linkStateTable.clear();auto previous=w;w={1,0,0};r->ApplyBottleneckBandwidthWeights(p,w);ck(w==previous,"signed_cache_restores_no_delay");
    r->m_earlyProbes.clear();w={.9,.05,.05};r->ApplyBottleneckBandwidthWeights(p,w);ck(near(w[0],1.0/3),"nan_cache_cold_uniform");
    load(1,-1000);load(2,-500);load(3,-100);load(9,2000);w={1,0,0};r->ApplyBottleneckBandwidthWeights(p,w);
    ck(w[0]<w[1]&&w[1]<w[2],"all_negative_preserve_order");
    auto incoming=w;for(auto path:p){path->latency=999999;path->sent_pkt_num=999999;path->theoreticalSmallestLatencyInNs=1;}
    w={.99,.005,.005};r->ApplyBottleneckBandwidthWeights(p,w);ck(w==incoming,"no_latency_or_counter_mixing");
    auto id=MakeLinkId(1,1);r->AcceptMaxQueueEvidence(id,true,0,0);ck(r->m_linkStateTable[id].SignedScoreQ()==-1000,"alarm_keeps_signed");
    r->AcceptMaxQueueEvidence(id,false,4,4);ck(r->m_linkStateTable[id].SignedScoreQ()==-1000,"recovery_keeps_signed");
    LinkCapacityRecord rec;rec.linkId=id;rec.state.availableQ=CompactLinkState::EncodeScoreQ(-300);rec.state.sampleEpoch=2;
    ck(r->AcceptLinkCapacity(rec,1,8)&&r->m_linkStateTable[id].SignedScoreQ()==-300,"ACK_preserves_negative");
    rec.state.availableQ=CompactLinkState::EncodeScoreQ(-200);ck(!r->AcceptLinkCapacity(rec,1,8),"same_epoch_signed_conflict");
    rec.state.sampleEpoch=3;ck(r->AcceptLinkCapacity(rec,1,12)&&r->m_linkStateTable[id].SignedScoreQ()==-200,"newer_signed_update");
    CompactLinkState::queueScoreEnabled=false;lcEnabled=false;lcBwCc=false;lcBwMode=0;lcDisablePruning=false;Simulator::Destroy();
    std::cout<<(ok?"QUEUE_BW_SCORE_PASS":"QUEUE_BW_SCORE_FAIL")<<" assertions="<<count<<" LST_bytes="<<sizeof(CompactLinkState)<<" endpoint_value_bytes="<<sizeof(EndpointLinkState)<<" probe_state_bytes="<<sizeof(EarlyProbeState)<<std::endl;return ok;
}
}

namespace ns3 {
bool RdmaSmartFlowRouting::RunLinkSendPenaltySelfChecks(){
    bool ok=true;uint32_t count=0;auto ck=[&](bool v,const char *name){++count;if(!v){ok=false;std::cerr<<"LINK_SEND_PENALTY_FAIL "<<name<<std::endl;}};
    auto near=[](double a,double b){return std::abs(a-b)<1e-7;};
    CompactLinkState::queueScoreEnabled=true;lcLinkSendPenalty=true;lcEnabled=true;lcBwCc=true;lcBwMode=2;lcBwAudit=false;lcQuotaAudit=false;
    lcBwBeta=5;lcBwUnitGbps=100;lcDisablePruning=true;lcPathTimeout=false;lcTtlNs=100000;lcMaxLinkQueue=true;lcPathOnlyQueue=true;lcPathQueueMax=true;
    lcQueueAlarmBytes=100000;lcZeroBwMinProbability=.05;lcAlpsProbe=true;
    auto r=CreateObject<RdmaSmartFlowRouting>();r->m_nodeId=1000;
    std::map<uint32_t,PathData> pit;
    for(uint32_t i=1;i<=4;++i){PathData p;p.pid=i;p.nodeIdSequence={1000,i,9,8,i==4?88u:99u};p.portSequence={1,1,1,i==4?2u:1u};pit[i]=p;}
    PathData foreign=pit[1];foreign.pid=5;foreign.nodeIdSequence.front()=2000;foreign.sent_pkt_num=100000;pit[5]=foreign;r->set_PIT(pit);
    std::vector<PathData*>p={r->lookup_PIT(1),r->lookup_PIT(2),r->lookup_PIT(3)};
    for(auto path:p)for(size_t j=1;j<path->portSequence.size();++j){auto &v=r->m_linkStateTable[MakeLinkId(path->nodeIdSequence[j],path->portSequence[j])];v.availableQ=CompactLinkState::EncodeScoreQ(1000);}
    auto load=[&](uint32_t node,int32_t score){r->m_linkStateTable[MakeLinkId(node,1)].availableQ=CompactLinkState::EncodeScoreQ(score);};
    load(1,500);load(2,300);load(3,200);load(9,400);auto shared=MakeLinkId(9,1);
    std::vector<double>w={1,0,0};r->ApplyBottleneckBandwidthWeights(p,w);auto noWork=w;
    ck(near(r->m_earlyProbes[1].lastBudgetQ,200)&&near(r->m_earlyProbes[2].lastBudgetQ,120)&&near(r->m_earlyProbes[3].lastBudgetQ,80),"raw_shared_budget_20_12_8");
    p[0]->sent_pkt_num=10;p[1]->sent_pkt_num=20;r->lookup_PIT(4)->sent_pkt_num=7;
    auto penalties=r->BuildLinkSendPenaltyQ();
    ck(near(penalties.at(MakeLinkId(1,1)),104.8),"ten_packets_equal_10_48G");
    ck(near(penalties.at(MakeLinkId(2,1)),209.6),"node_port_identity_distinct");
    ck(near(penalties.at(shared),37*10.48),"shared_sum_includes_other_destination");
    ck(!penalties.count(MakeLinkId(1000,1)),"exclude_source_NIC");
    ck(near(penalties.at(shared),387.76),"foreign_sender_counter_excluded");
    SharedQuotaResult selection;
    ck(r->ComputeSharedQuota(p,{true,true,true},0,selection,&penalties),"penalized_projection_feasible");
    ck(near(selection.capacityQ.at(shared),12.24),"fractional_shared_capacity_preserved");
    ck(near(selection.budgetQ[0]+selection.budgetQ[1]+selection.budgetQ[2],12.24),"shared_penalty_applied_once_before_projection");
    auto original=r->m_linkStateTable;w={.9,.05,.05};r->ApplyBottleneckBandwidthWeights(p,w);auto once=w;
    ck(w!=noWork,"local_work_changes_weights");
    bool unchanged=true;for(const auto &e:original){const auto &v=r->m_linkStateTable.at(e.first);unchanged&=v.availableQ==e.second.availableQ&&v.sampleEpoch==e.second.sampleEpoch&&v.queueEpoch==e.second.queueEpoch;}
    ck(unchanged,"no_LST_writeback");
    ck(near(r->m_earlyProbes[1].lastBudgetQ,200)&&near(r->m_earlyProbes[2].lastBudgetQ,120)&&near(r->m_earlyProbes[3].lastBudgetQ,80),"cache_contains_observation_budget_not_penalty");
    w={1,0,0};r->ApplyBottleneckBandwidthWeights(p,w);ck(w==once,"repeated_evaluation_no_compounding");
    r->ResetFeedbackSendCounter(4);ck(r->lookup_PIT(4)->sent_pkt_num==0&&near(r->BuildLinkSendPenaltyQ().at(shared),30*10.48),"feedback_clears_only_its_path_contribution");
    load(9,2000);p[0]->sent_pkt_num=100;p[1]->sent_pkt_num=0;
    w={1,0,0};r->ApplyBottleneckBandwidthWeights(p,w);
    ck(w[0]<.05 && w[0]>0,"negative_selection_score_not_floored");
    ck(near(r->m_earlyProbes[1].lastBudgetQ,500),"negative_prediction_does_not_poison_cache");
    r->m_linkStateTable.clear();auto cached=r->m_earlyProbes[1].lastBudgetQ;w={1,0,0};r->ApplyBottleneckBandwidthWeights(p,w);auto cachedOnce=w;
    ck(r->m_earlyProbes[1].lastBudgetQ==cached,"stale_cache_not_overwritten");
    w={0,1,0};r->ApplyBottleneckBandwidthWeights(p,w);ck(w==cachedOnce,"stale_penalty_not_compounded");
    r->m_earlyProbes.clear();w={1,0,0};r->ApplyBottleneckBandwidthWeights(p,w);ck(std::isfinite(w[0])&&near(w[0]+w[1]+w[2],1),"unknown_prior_with_local_cost_no_delay");
    for(auto path:p)r->ResetFeedbackSendCounter(path->pid);
    ck(r->BuildLinkSendPenaltyQ().empty(),"all_feedback_resets_restore_zero_penalty");
    w={1,0,0};r->ApplyBottleneckBandwidthWeights(p,w);ck(near(w[0],1.0/3),"no_history_no_work_uniform_prior");
    // A zero observation minus fresh DATA work must be negative, not protected by the zero-score floor.
    for(auto path:p)for(size_t j=1;j<path->portSequence.size();++j)r->m_linkStateTable[MakeLinkId(path->nodeIdSequence[j],path->portSequence[j])].availableQ=CompactLinkState::EncodeScoreQ(2000);
    load(1,0);p[0]->sent_pkt_num=200;w={1,0,0};r->ApplyBottleneckBandwidthWeights(p,w);
    ck(w[0]<.05,"floor_uses_final_selection_zero_not_observation_zero");
    auto observed=r->m_earlyProbes[1].lastBudgetQ;r->ResetFeedbackSendCounter(1);w={1,0,0};r->ApplyBottleneckBandwidthWeights(p,w);
    ck(near(w[0],.05)&&observed==r->m_earlyProbes[1].lastBudgetQ,"feedback_removes_local_cost_preserves_zero_observation");
    lcLinkSendPenalty=false;CompactLinkState::queueScoreEnabled=false;lcEnabled=false;lcBwCc=false;lcBwMode=0;lcDisablePruning=false;Simulator::Destroy();
    std::cout<<(ok?"LINK_SEND_PENALTY_PASS":"LINK_SEND_PENALTY_FAIL")<<" assertions="<<count<<" packet_bytes=1048 horizon_ns=8000 LST_bytes="<<sizeof(EndpointLinkState)<<std::endl;return ok;
}
}

namespace ns3 {
bool RdmaSmartFlowRouting::RunLinkSendAlphaSelfChecks(){
    bool ok=true;uint32_t count=0;auto ck=[&](bool v,const char *name){++count;if(!v){ok=false;std::cerr<<"LINK_SEND_ALPHA_FAIL "<<name<<std::endl;}};
    auto near=[](double a,double b){return std::abs(a-b)<1e-8;};
    CompactLinkState::queueScoreEnabled=true;lcLinkSendPenalty=true;lcEnabled=true;lcBwCc=true;lcBwMode=2;lcBwAudit=false;lcQuotaAudit=false;
    lcBwBeta=5;lcBwUnitGbps=100;lcDisablePruning=true;lcPathTimeout=false;lcTtlNs=100000;lcMaxLinkQueue=true;lcPathOnlyQueue=true;lcPathQueueMax=true;
    lcQueueAlarmBytes=100000;lcZeroBwMinProbability=.05;lcAlpsProbe=true;
    auto r=CreateObject<RdmaSmartFlowRouting>();r->m_nodeId=1000;
    std::map<uint32_t,PathData> pit;
    for(uint32_t i=1;i<=3;++i){PathData p;p.pid=i;p.nodeIdSequence={1000,i,9,99};p.portSequence={1,1,1};p.sent_pkt_num=i==1?10:(i==2?20:0);pit[i]=p;}
    r->set_PIT(pit);std::vector<PathData*>p={r->lookup_PIT(1),r->lookup_PIT(2),r->lookup_PIT(3)};
    for(auto path:p)for(size_t j=1;j<path->portSequence.size();++j)r->m_linkStateTable[MakeLinkId(path->nodeIdSequence[j],path->portSequence[j])].availableQ=CompactLinkState::EncodeScoreQ(1000);
    const auto shared=MakeLinkId(9,1);r->m_linkStateTable[shared].availableQ=CompactLinkState::EncodeScoreQ(400);
    std::map<double,std::vector<double>> weights;
    for(double alpha:{0.0,0.5,1.0}){
        lcLinkSendAlpha=alpha;auto cost=r->BuildLinkSendPenaltyQ();
        ck(near(cost.at(MakeLinkId(1,1)),alpha*104.8),"private_gain_exact");
        ck(near(cost.at(shared),alpha*314.4),"shared_gain_exact");
        SharedQuotaResult quota;ck(r->ComputeSharedQuota(p,{true,true,true},0,quota,&cost),"gain_projection_valid");
        ck(near(quota.capacityQ.at(shared),400-alpha*314.4),"gain_before_shared_projection");
        std::vector<double>w={.7,.2,.1};r->ApplyBottleneckBandwidthWeights(p,w);weights[alpha]=w;
        ck(near(w[0]+w[1]+w[2],1),"gain_probability_normalized");
        ck(near(r->m_earlyProbes[1].lastBudgetQ,double(float(400.0/3))),"gain_does_not_contaminate_cache");
        auto again=w;r->ApplyBottleneckBandwidthWeights(p,again);ck(w==again,"gain_not_compounded");
    }
    lcLinkSendPenalty=false;std::vector<double>w={.9,.05,.05};r->ApplyBottleneckBandwidthWeights(p,w);
    ck(w==weights[0],"alpha_zero_equals_disabled_test32");
    ck(weights[.5]!=weights[1],"half_gain_exercises_different_allocation");
    lcLinkSendAlpha=1;lcLinkSendPenalty=false;CompactLinkState::queueScoreEnabled=false;lcEnabled=false;lcBwCc=false;lcBwMode=0;lcDisablePruning=false;Simulator::Destroy();
    std::cout<<(ok?"LINK_SEND_ALPHA_PASS":"LINK_SEND_ALPHA_FAIL")<<" assertions="<<count<<" alpha_values=0/0.5/1 per_packet_gbps=0/0.524/1.048"<<std::endl;return ok;
}
}

namespace ns3 {
bool RdmaSmartFlowRouting::lcQueueTargetCc=false;
bool RdmaSmartFlowRouting::lcDataAckCcOnly=false;
std::ofstream RdmaSmartFlowRouting::lcQueueTargetLog,RdmaSmartFlowRouting::lcCcFeedbackLog;
double RdmaSmartFlowRouting::QueueTargetPressureEpsilon(const PressureResult &state) {
    double epsilon=0;
    for(const auto &entry:state.links)if(entry.second.valid && entry.second.capacityBps)
        epsilon=std::max(epsilon,100000000.0/double(entry.second.capacityBps));
    return epsilon;
}
uint64_t RdmaSmartFlowRouting::QueueTargetReducedRate(uint64_t oldRate,double u,uint64_t minimum,double epsilon) {
    NS_ABORT_MSG_IF(!std::isfinite(u) || u<0 || !std::isfinite(epsilon) || epsilon<0 || minimum>oldRate,"Invalid queue-target input");
    if(u<=2.0+epsilon)return oldRate;
    const uint64_t proportional=uint64_t(std::ceil(double(oldRate)/(u-1.0)));
    const uint64_t half=oldRate/2+oldRate%2;
    return std::min(oldRate,std::max(minimum,std::max(half,proportional)));
}
bool RdmaSmartFlowRouting::RunQueueTargetCcSelfChecks() {
    uint32_t n=0;bool ok=true;auto ck=[&](bool v,const char *name){++n;if(!v){ok=false;std::cerr<<"QUEUE_TARGET_CC_FAIL "<<name<<std::endl;}};
    PressureResult state;PressureLink a;a.valid=true;a.capacityBps=100000000000ULL;state.links[1]=a;
    ck(std::fabs(QueueTargetPressureEpsilon(state)-.001)<1e-12,"100G_quantum");
    a.capacityBps=25000000000ULL;state.links[2]=a;a.valid=false;a.capacityBps=1000000000;state.links[3]=a;
    const double eps=QueueTargetPressureEpsilon(state);ck(std::fabs(eps-.004)<1e-12,"25G_max_valid_quantum");
    const uint64_t top=100000000000ULL,minimum=1000000000ULL;
    ck(QueueTargetReducedRate(top,2,minimum,eps)==top,"target_keeps_rate");
    ck(QueueTargetReducedRate(top,2+eps,minimum,eps)==top,"quantum_deadband_keeps_rate");
    ck(QueueTargetReducedRate(top,1.5,minimum,eps)==top,"draining_queue_no_decrease");
    ck(QueueTargetReducedRate(top,2.2,minimum,eps)==83333333334ULL,"moderate_excess");
    ck(QueueTargetReducedRate(top,2.5,minimum,eps)==66666666667ULL,"strong_excess");
    ck(QueueTargetReducedRate(top,3,minimum,eps)==top/2,"half_at_three");
    ck(QueueTargetReducedRate(top,9,minimum,eps)==top/2,"severe_half_cap");
    ck(QueueTargetReducedRate(101,4,1,eps)==51,"odd_integer_half");
    ck(QueueTargetReducedRate(1500000000,4,minimum,eps)==minimum,"absolute_floor");
    ck(QueueTargetReducedRate(minimum,4,minimum,eps)==minimum,"already_at_minimum");
    uint64_t previous=top;
    for(double u:{2.0,2.004,2.01,2.05,2.1,2.2,2.5,3.0,4.0,100.0}){
        auto next=QueueTargetReducedRate(top,u,minimum,eps);ck(next<=previous && next<=top && next>=top/2,"monotone_and_bounded");previous=next;
    }
    for(uint64_t rate:{1000000001ULL,3500000001ULL,30000000000ULL,100000000000ULL})
        for(double u:{2.005,2.1,2.7,3.0,5.0})ck(QueueTargetReducedRate(rate,u,minimum,eps)>=rate/2+rate%2,"never_more_than_half");
    std::cout<<(ok?"QUEUE_TARGET_CC_SELF_CHECK_PASS":"QUEUE_TARGET_CC_SELF_CHECK_FAIL")<<" assertions="<<n<<" target_U=2 no_new_QP_ACK_LST_fields=1"<<std::endl;return ok;
}
bool RdmaSmartFlowRouting::lcPressureCc=false;
std::ofstream RdmaSmartFlowRouting::lcPressureEvents,RdmaSmartFlowRouting::lcPressurePaths,RdmaSmartFlowRouting::lcPressureLinks;
double RdmaSmartFlowRouting::PressureFromRawScore(int32_t score,uint64_t capacity) {
    NS_ABORT_MSG_IF(!capacity,"Pressure requires a configured static link capacity");
    return std::max(0.0,2.0-double(score)*100000000.0/double(capacity));
}
double RdmaSmartFlowRouting::lcPressureGain=1.0;
uint64_t RdmaSmartFlowRouting::PressureRateWithGain(uint64_t oldRate,double u,uint64_t minimum,double gain) {
    NS_ABORT_MSG_IF(!std::isfinite(u) || u<=1 || minimum>oldRate || !std::isfinite(gain) || gain<=0 || gain>1,"Invalid pressure decrease/gain");
    uint64_t proportional=uint64_t(std::ceil(double(oldRate)/u));
    const uint64_t target=std::min(oldRate,std::max(minimum,std::max((oldRate+1)/2,proportional)));
    if(gain==1.0)return target; // Exact test41 path: no new floating-point rounding.
    // Whole-bps ceiling means the actual reduction never exceeds gain*(old-target).
    const long double candidate=static_cast<long double>(oldRate)-static_cast<long double>(gain)*static_cast<long double>(oldRate-target);
    const uint64_t smoothed=uint64_t(std::ceil(candidate));
    return std::min(oldRate,std::max(target,smoothed));
}
uint64_t RdmaSmartFlowRouting::PressureReducedRate(uint64_t oldRate,double u,uint64_t minimum) {
    return PressureRateWithGain(oldRate,u,minimum,lcPressureGain);
}
RdmaSmartFlowRouting::PressureResult RdmaSmartFlowRouting::EvaluateMinPathPressure(const std::vector<PathData*> &paths,uint64_t now) const {
    PressureResult out;out.leastU=std::numeric_limits<double>::infinity();out.evidenceNs=UINT64_MAX;
    for(auto path:paths){PressurePath p;p.pid=path->pid;
        auto protocol=m_earlyProbes.find(p.pid);p.unavailable=lcPathTimeout && protocol!=m_earlyProbes.end() && protocol->second.unavailable;
        if(p.unavailable){out.paths.push_back(p);continue;}
        ++out.active;bool shape=path->nodeIdSequence.size()==path->portSequence.size()+1 && path->portSequence.size()>=2;
        std::set<uint32_t> seen;
        if(shape)for(size_t j=1;j<path->portSequence.size();++j){ // Source NIC excluded, as in the existing switch telemetry.
            uint32_t id=MakeLinkId(path->nodeIdSequence[j],path->portSequence[j]);if(!seen.insert(id).second)continue;++p.links;
            auto found=out.links.find(id);
            if(found==out.links.end()){
                PressureLink l;auto node=nodeIdToNodeMap.find(id>>16);
                // Static topology configuration ONLY: never inspect remote queues or live counters here.
                if(node!=nodeIdToNodeMap.end()) {auto sw=DynamicCast<SwitchNode>(node->second);
                    if(sw){auto rate=sw->m_outPort2BitRateMap.find(id&65535);if(rate!=sw->m_outPort2BitRateMap.end())l.capacityBps=rate->second;}}
                uint16_t unused;
                if(CompactLinkState::queueScoreEnabled && l.capacityBps && ReadLinkCapacity(id,now,unused)){
                    auto state=m_linkStateTable.find(id);l.scoreQ=state->second.SignedScoreQ();
                    uint64_t tick=now/lcObservationTickNs;uint16_t age=uint16_t(uint16_t(tick)-state->second.sampleEpoch);
                    l.bornNs=(tick-age)*lcObservationTickNs;l.u=PressureFromRawScore(l.scoreQ,l.capacityBps);l.valid=true;
                }
                found=out.links.emplace(id,l).first;
            }
            const auto &l=found->second;if(!l.valid)continue;++p.fresh;
            if(!p.argmax || l.u>p.u || (l.u==p.u && l.bornNs>p.evidenceNs)){p.u=l.u;p.argmax=id;p.evidenceNs=l.bornNs;}
        }
        p.complete=shape && p.links && p.fresh==p.links;p.congested=p.fresh && p.u>1.0;
        // A fresh high link proves pressure even if another hop is missing; incomplete low values never prove health.
        out.complete+=p.complete;out.congested+=p.congested;out.anyHealthy|=p.complete && !p.congested;
        if(p.congested){out.leastU=std::min(out.leastU,p.u);out.evidenceNs=std::min(out.evidenceNs,p.evidenceNs);}
        out.paths.push_back(p);
    }
    out.allCongested=out.active && out.congested==out.active;
    if(!std::isfinite(out.leastU))out.leastU=0;if(out.evidenceNs==UINT64_MAX)out.evidenceNs=0;
    return out;
}
bool RdmaSmartFlowRouting::RunPressureCcSelfChecks(){
    uint32_t n=0;bool ok=true;auto ck=[&](bool v,const char *name){++n;if(!v){ok=false;std::cerr<<"PRESSURE_CC_FAIL "<<name<<std::endl;}};
    auto near=[](double a,double b){return std::abs(a-b)<1e-10;};
    ck(near(PressureFromRawScore(2000,100000000000ULL),0),"idle_100G");
    ck(near(PressureFromRawScore(1000,100000000000ULL),1),"full_empty_100G");
    ck(near(PressureFromRawScore(900,100000000000ULL),1.1),"10KB_full_100G");
    ck(near(PressureFromRawScore(0,100000000000ULL),2),"threshold_full_100G");
    ck(near(PressureFromRawScore(-500,100000000000ULL),2.5),"negative_signed_score");
    ck(near(PressureFromRawScore(50,25000000000ULL),1.8),"heterogeneous_25G");
    ck(PressureReducedRate(100000000000ULL,1.1,100000000)==90909090910ULL,"proportional_ceiling");
    ck(PressureReducedRate(100000000000ULL,2,100000000)==50000000000ULL,"half_at_two");
    ck(PressureReducedRate(100000000000ULL,5,100000000)==50000000000ULL,"never_more_than_half");
    ck(PressureReducedRate(150000000ULL,4,100000000)==100000000ULL,"absolute_floor");
    ck(PressureReducedRate(101,4,1)==51,"odd_integer_half_ceiling");
    CompactLinkState::queueScoreEnabled=true;lcEnabled=true;lcPathTimeout=true;lcTtlNs=100000;
    auto savedNodes=nodeIdToNodeMap;auto r=CreateObject<RdmaSmartFlowRouting>();
    auto a=CreateObject<SwitchNode>(),b=CreateObject<SwitchNode>(),c=CreateObject<SwitchNode>(),shared=CreateObject<SwitchNode>();
    for(auto sw:{a,b,c,shared}){nodeIdToNodeMap[sw->GetId()]=sw;sw->m_outPort2BitRateMap[1]=100000000000ULL;}
    PathData p1,p2,p3;std::vector<PathData*> paths={&p1,&p2,&p3};
    for(size_t i=0;i<3;++i){paths[i]->pid=i+1;paths[i]->nodeIdSequence={60000,uint32_t(i==0?a->GetId():i==1?b->GetId():c->GetId()),shared->GetId(),60001};paths[i]->portSequence={1,1,1};}
    auto put=[&](uint32_t node,int32_t score,uint64_t born=100){auto &x=r->m_linkStateTable[MakeLinkId(node,1)];x.availableQ=CompactLinkState::EncodeScoreQ(score);x.sampleEpoch=uint16_t(born/lcObservationTickNs);};
    put(a->GetId(),500);put(b->GetId(),700);put(c->GetId(),1200);put(shared->GetId(),1500);
    auto state=r->EvaluateMinPathPressure(paths,104);
    ck(state.active==3 && state.complete==3 && state.anyHealthy && !state.allCongested,"healthy_path_prevents_global_decrease");
    ck(state.links.size()==4,"shared_link_deduplicated_and_source_NIC_excluded");
    put(c->GetId(),900);state=r->EvaluateMinPathPressure(paths,104);
    ck(state.allCongested && near(state.leastU,1.1),"minimum_of_path_maxima");
    put(shared->GetId(),800);state=r->EvaluateMinPathPressure(paths,104);
    ck(state.allCongested && near(state.leastU,1.2),"common_shared_pressure_once");
    ck(state.evidenceNs==100,"winning_evidence_timestamp");
    put(shared->GetId(),1500);r->m_linkStateTable.erase(MakeLinkId(c->GetId(),1));state=r->EvaluateMinPathPressure(paths,104);
    ck(!state.anyHealthy && !state.allCongested,"unknown_not_healthy_or_congested");
    put(shared->GetId(),800);state=r->EvaluateMinPathPressure(paths,104);
    ck(state.allCongested && state.complete==2,"fresh_shared_high_proves_incomplete_path_pressure");
    r->m_earlyProbes[3].unavailable=true;state=r->EvaluateMinPathPressure(paths,104);
    ck(state.active==2 && state.paths[2].unavailable,"protocol_unavailable_not_candidate");
    r->m_earlyProbes[1].unavailable=r->m_earlyProbes[2].unavailable=true;state=r->EvaluateMinPathPressure(paths,104);
    ck(state.active==0 && !state.allCongested && !state.anyHealthy,"all_protocol_unavailable_holds");
    r->m_earlyProbes.clear();state=r->EvaluateMinPathPressure(paths,100104);
    ck(!state.allCongested && !state.anyHealthy && state.complete==0,"expired_evidence_not_reused");
    put(a->GetId(),1000,100200);put(b->GetId(),1000,100200);put(c->GetId(),1000,100200);put(shared->GetId(),1500,100200);state=r->EvaluateMinPathPressure(paths,100204);
    ck(state.anyHealthy && !state.allCongested,"pressure_one_boundary_holds");
    p1.sent_pkt_num=999999;p1.latency=UINT32_MAX;auto again=r->EvaluateMinPathPressure(paths,100204);
    ck(near(again.paths[0].u,state.paths[0].u),"no_local_send_or_delay_penalty_in_pressure");
    ck(sizeof(CompactLinkState)==4 && sizeof(LinkCapacityRecord)==8,"no_LST_or_ACK_record_growth");
    nodeIdToNodeMap=savedNodes;lcEnabled=false;lcPathTimeout=false;CompactLinkState::queueScoreEnabled=false;Simulator::Destroy();
    std::cout<<(ok?"PRESSURE_CC_SELF_CHECK_PASS":"PRESSURE_CC_SELF_CHECK_FAIL")<<" assertions="<<n<<" only_static_capacities_from_topology=1"<<std::endl;return ok;
}
}

namespace ns3 {
bool RdmaSmartFlowRouting::lcPressureQueueGate=false;
bool RdmaSmartFlowRouting::PressureFullQueueGate(const std::vector<PathData*> &paths,const PressureResult &pressure,
        uint64_t now,BandwidthCongestionResult &raw,uint32_t &high,uint32_t &known) const {
    NS_ABORT_MSG_IF(paths.size()!=pressure.paths.size(),"Queue gate candidate mismatch");
    raw=EvaluateBandwidthCongestion(paths,now);high=known=0;uint32_t active=0;
    for(size_t i=0;i<paths.size();++i)if(!pressure.paths[i].unavailable){++active;high+=raw.blocked[i];known+=raw.known[i];}
    NS_ABORT_MSG_IF(active!=pressure.active,"Queue gate active-path count mismatch");
    return active>0 && high==active && known==active;
}
bool RdmaSmartFlowRouting::RunPressureQueueGateSelfChecks(){
    bool ok=true;uint32_t n=0;auto ck=[&](bool v,const char *name){++n;if(!v){ok=false;std::cerr<<"PRESSURE_QUEUE_GATE_FAIL "<<name<<std::endl;}};
    auto savedNodes=nodeIdToNodeMap;
    CompactLinkState::queueScoreEnabled=true;lcEnabled=true;lcBwCc=true;lcMaxLinkQueue=true;lcPathOnlyQueue=false;
    lcPathTimeout=true;lcTtlNs=100000;lcBwCcAudit=false;lcAllocationOnly=false;lcAlpsCongestion=false;lcRequireZeroBw=false;
    auto r=CreateObject<RdmaSmartFlowRouting>();
    auto a=CreateObject<SwitchNode>(),b=CreateObject<SwitchNode>(),c=CreateObject<SwitchNode>(),shared=CreateObject<SwitchNode>();
    for(auto sw:{a,b,c,shared}){nodeIdToNodeMap[sw->GetId()]=sw;sw->m_outPort2BitRateMap[1]=100000000000ULL;}
    PathData p1,p2,p3;std::vector<PathData*> paths={&p1,&p2,&p3};
    for(size_t i=0;i<3;++i){auto p=paths[i];p->pid=i+1;p->nodeIdSequence={60000,uint32_t(i==0?a->GetId():i==1?b->GetId():c->GetId()),shared->GetId(),60001};p->portSequence={1,1,1};}
    auto put=[&](uint32_t node,int32_t score){auto &x=r->m_linkStateTable[MakeLinkId(node,1)];x.availableQ=CompactLinkState::EncodeScoreQ(score);x.sampleEpoch=uint16_t(100/lcObservationTickNs);};
    put(a->GetId(),900);put(b->GetId(),900);put(c->GetId(),900);put(shared->GetId(),1500);
    auto gate=[&](uint64_t now,uint32_t expectedHigh,uint32_t expectedKnown,bool expected,const char *name){
        auto pressure=r->EvaluateMinPathPressure(paths,now);BandwidthCongestionResult raw;uint32_t high=0,known=0;
        bool result=r->PressureFullQueueGate(paths,pressure,now,raw,high,known);
        ck(result==expected && high==expectedHigh && known==expectedKnown,name);return pressure;
    };
    auto pressure=gate(104,0,0,false,"unknown_queue_cannot_enable_decrease");
    ck(pressure.allCongested,"all_pressure_high_alone_not_enough");
    for(auto sw:{a,b,c,shared})r->AcceptMaxQueueEvidence(MakeLinkId(sw->GetId(),1),false,108,108);
    gate(108,0,3,false,"all_below_queue_threshold_holds");
    r->AcceptMaxQueueEvidence(MakeLinkId(a->GetId(),1),true,116,116);gate(116,1,3,false,"one_private_congested_holds");
    r->AcceptMaxQueueEvidence(MakeLinkId(b->GetId(),1),true,124,124);gate(124,2,3,false,"two_of_three_holds");
    r->AcceptMaxQueueEvidence(MakeLinkId(c->GetId(),1),true,132,132);gate(132,3,3,true,"all_private_queues_enable_gate");
    r->AcceptMaxQueueEvidence(MakeLinkId(c->GetId(),1),false,140,140);gate(140,2,3,false,"one_queue_recovered_closes_gate");
    r->AcceptMaxQueueEvidence(MakeLinkId(shared->GetId(),1),true,148,148);gate(148,3,3,true,"common_shared_queue_marks_all_once");
    r->AcceptMaxQueueEvidence(MakeLinkId(shared->GetId(),1),false,156,156);
    r->AcceptMaxQueueEvidence(MakeLinkId(b->GetId(),1),false,156,156);r->m_earlyProbes[3].unavailable=true;
    pressure=gate(156,1,2,false,"protocol_unavailable_is_not_queue_congestion");ck(pressure.active==2,"protocol_unavailable_excluded");
    r->m_earlyProbes[1].unavailable=r->m_earlyProbes[2].unavailable=true;gate(164,0,0,false,"no_active_path_cannot_enable_gate");
    r->m_earlyProbes.clear();gate(100200,0,0,false,"expired_queue_evidence_cannot_enable_gate");
    ck(sizeof(CompactLinkState)==4 && sizeof(LinkCapacityRecord)==8,"no_LST_or_ACK_growth");
    nodeIdToNodeMap=savedNodes;lcEnabled=false;lcBwCc=false;lcMaxLinkQueue=false;lcPathTimeout=false;CompactLinkState::queueScoreEnabled=false;Simulator::Destroy();
    std::cout<<(ok?"PRESSURE_QUEUE_GATE_SELF_CHECK_PASS":"PRESSURE_QUEUE_GATE_SELF_CHECK_FAIL")<<" assertions="<<n<<" original_raw_queue_predicate=1 protocol_state_not_queue=1"<<std::endl;return ok;
}
}

namespace ns3 {
bool RdmaSmartFlowRouting::lcQueueRecovery=false;
uint32_t RdmaSmartFlowRouting::CountHealthyQueuePaths(const PressureResult &pressure,const BandwidthCongestionResult &queue){
    NS_ABORT_MSG_IF(pressure.paths.size()!=queue.blocked.size() || pressure.paths.size()!=queue.known.size(),"Queue recovery candidate mismatch");
    uint32_t count=0;
    for(size_t i=0;i<pressure.paths.size();++i)
        if(!pressure.paths[i].unavailable && queue.known[i] && !queue.blocked[i])++count;
    return count;
}
RdmaSmartFlowRouting::QueuePressureAction RdmaSmartFlowRouting::SelectQueuePressureAction(
        const PressureResult &pressure,const BandwidthCongestionResult &queue,bool queueGate){
    if(!pressure.active)return QueuePressureAction::Hold;
    if(queueGate && pressure.allCongested)return QueuePressureAction::Decrease;
    if(CountHealthyQueuePaths(pressure,queue))return QueuePressureAction::Increase;
    return QueuePressureAction::Hold;
}
bool RdmaSmartFlowRouting::RunQueueRecoverySelfChecks(){
    bool ok=true;uint32_t n=0;auto ck=[&](bool v,const char *name){++n;if(!v){ok=false;std::cerr<<"QUEUE_RECOVERY_FAIL "<<name<<std::endl;}};
    using A=QueuePressureAction;PressureResult p;BandwidthCongestionResult q;
    p.active=3;p.paths.resize(3);p.allCongested=true;p.anyHealthy=false;
    q.blocked={true,true,false};q.known={true,true,true};
    ck(SelectQueuePressureAction(p,q,false)==A::Increase,"high_pressure_does_not_hide_fresh_low_queue_recovery");
    ck(CountHealthyQueuePaths(p,q)==1,"exactly_one_confirmed_low_path");
    q.known[2]=false;ck(SelectQueuePressureAction(p,q,false)==A::Hold,"unknown_not_healthy");
    q.known[2]=true;q.blocked[2]=true;ck(SelectQueuePressureAction(p,q,true)==A::Decrease,"all_queue_and_pressure_high_decrease");
    p.allCongested=false;p.anyHealthy=true;ck(SelectQueuePressureAction(p,q,true)==A::Hold,"low_pressure_cannot_override_all_high_queues");
    q.blocked[2]=false;ck(SelectQueuePressureAction(p,q,false)==A::Increase,"one_low_queue_sufficient");
    p.paths[2].unavailable=true;p.active=2;ck(SelectQueuePressureAction(p,q,false)==A::Hold,"protocol_unavailable_low_path_not_recovery");
    q.blocked[1]=false;ck(SelectQueuePressureAction(p,q,false)==A::Increase,"active_low_path_still_recovers");
    p.active=0;for(auto &x:p.paths)x.unavailable=true;ck(SelectQueuePressureAction(p,q,false)==A::Hold,"no_active_path_holds");
    // Exercise the existing directed-link freshness logic instead of treating absence as low queue.
    lcEnabled=true;lcBwCc=true;lcMaxLinkQueue=true;lcPathOnlyQueue=false;lcPathTimeout=true;lcTtlNs=100000;lcBwCcAudit=false;lcAllocationOnly=false;
    auto r=CreateObject<RdmaSmartFlowRouting>();PathData path;path.pid=1;path.nodeIdSequence={60000,1,2,60001};path.portSequence={1,1,1};std::vector<PathData*> paths={&path};
    PressureResult one;one.active=1;one.paths.resize(1);one.allCongested=true;
    auto action=[&](uint64_t now){auto raw=r->EvaluateBandwidthCongestion(paths,now);bool gate=raw.blockedCount==1 && raw.knownCount==1;return SelectQueuePressureAction(one,raw,gate);};
    ck(action(100)==A::Hold,"missing_all_links_not_recovery");
    r->AcceptMaxQueueEvidence(MakeLinkId(1,1),false,104,104);ck(action(104)==A::Hold,"partially_observed_low_path_not_recovery");
    r->AcceptMaxQueueEvidence(MakeLinkId(2,1),false,108,108);ck(action(108)==A::Increase,"complete_fresh_low_path_recovers");
    ck(action(100200)==A::Hold,"expired_low_path_not_recovery");
    r->AcceptMaxQueueEvidence(MakeLinkId(1,1),true,100204,100204);ck(action(100204)==A::Decrease,"fresh_high_proof_survives_missing_other_hop");
    ck(sizeof(CompactLinkState)==4 && sizeof(LinkCapacityRecord)==8,"no_ACK_or_LST_growth");
    lcEnabled=false;lcBwCc=false;lcMaxLinkQueue=false;lcPathTimeout=false;Simulator::Destroy();
    std::cout<<(ok?"QUEUE_RECOVERY_SELF_CHECK_PASS":"QUEUE_RECOVERY_SELF_CHECK_FAIL")<<" assertions="<<n<<" pressure_high_recovery_reachable=1 unknown_not_healthy=1"<<std::endl;return ok;
}
}

namespace ns3 {
bool RdmaSmartFlowRouting::RunPressureGainSelfChecks(){
    bool ok=true;uint32_t n=0;auto ck=[&](bool v,const char *name){++n;if(!v){ok=false;std::cerr<<"PRESSURE_GAIN_FAIL "<<name<<std::endl;}};
    ck(PressureRateWithGain(100000000000ULL,2.2,1000000000ULL,1)==50000000000ULL,"gain_one_preserves_original_half");
    ck(PressureRateWithGain(100000000000ULL,2.2,1000000000ULL,.2)==90000000000ULL,"100G_to_90G");
    ck(PressureRateWithGain(100000000000ULL,1.1,1000000000ULL,.2)==98181818182ULL,"small_pressure_small_reduction");
    ck(PressureRateWithGain(1000000000ULL,100,1000000000ULL,.2)==1000000000ULL,"runtime_minimum_unchanged");
    ck(PressureRateWithGain(3,2,1,.2)==3,"whole_bps_rounding_cannot_overdecrease");
    for(uint64_t rate:{1000000000ULL,1000000001ULL,33000000001ULL,100000000000ULL})
        for(double u:{1.000001,1.1,1.9,2.0,2.2,10.0}){
            uint64_t old=std::min(rate,std::max(uint64_t(1000000000ULL),std::max((rate+1)/2,uint64_t(std::ceil(double(rate)/u)))));
            const auto full=PressureRateWithGain(rate,u,1000000000ULL,1);
            const auto gentle=PressureRateWithGain(rate,u,1000000000ULL,.2);
            ck(full==old,"gain_one_exact_formula");
            ck(gentle>=old && gentle<=rate && gentle>=1000000000ULL,"bounded_target_and_rate");
            ck(static_cast<long double>(gentle)+1e-6L>=.9L*static_cast<long double>(rate),"maximum_ten_percent_per_action");
        }
    ck(sizeof(CompactLinkState)==4 && sizeof(LinkCapacityRecord)==8,"no_ACK_or_LST_growth");
    std::cout<<(ok?"PRESSURE_GAIN_SELF_CHECK_PASS":"PRESSURE_GAIN_SELF_CHECK_FAIL")<<" assertions="<<n<<" gamma_one_exact=1 gamma_point_two_max_drop=0.1"<<std::endl;return ok;
}
}
