/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef RDMA_SMARTFLOW_ROUTING_H
#define RDMA_SMARTFLOW_ROUTING_H

#include "ns3/ipv4-smartflow-tag.h"
#include "ns3/callback.h"
#include <stdexcept>
#include <algorithm>
#include <utility>
#include "ns3/uinteger.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/net-device.h"
#include "ns3/channel.h"
#include "ns3/node.h"
#include "ns3/ptr.h"
#include "ns3/boolean.h"
#include "ns3/nstime.h"
#include "ns3/event-id.h" // conga
#include "ns3/object.h"
#include "common-user-model.h"
#include "rdma-queue-pair.h"
#include <random>
#include <cstdlib>   // for std::llabs

#define DEFAULT_PATH_ID 999999
#define DEFAULT_PATH_INDEX 999999999
#define DEFAULT_PATH_SELECTION_NUM 2
#define RDMA_LINK_LATENCY_IN_NANOSECOND 1
#define PROBE_SMALL_LATENCY_FIRST_STRATEGY 0
#define PROBE_SMALL_GENERATION_TIME_FIRST_STRATEGY 1
#define PROBE_RANDOM_STRATEGY 2
#define PROBE_PATH_EXPIRED_TIME_IN_NANOSECOND 1000000
#define PROBE_DEFAULT_INTERVAL_IN_NANOSECOND 1000000
#define PROBE_DEFAULT_PKT_SIZE_IN_BYTE 1

#define PIGGY_BACK_SMALL_LATENCY_FIRST_STRATEGY 0
#define PIGGY_BACK_SMALL_GENT_TIME_FIRST_STRATEGY 1
#define PIGGY_BACK_SMALL_SENT_TIME_FIRST_STRATEGY 2
#define PIGGY_BACK_DEFAULT_PATH_NUMBER 15

#define PATH_SELECTION_PRIORITY_FIRST_STRATEGY 1
#define PATH_SELECTION_ROUND_ROBIN_FOR_PACKET_STRATEGY 2
#define PATH_SELECTION_RANDOM_STRATEGY 3
#define PATH_SELECTION_SMALL_LATENCY_FIRST_STRATEGY 4
#define PATH_SELECTION_FLOWLET_STATEGY 5
#define PATH_SELECTION_FLOW_HASH_STRATEGY 6
#define PATH_SELECTION_ROUND_ROBIN_FOR_FLOW_STRATEGY 7
#define PATH_SELECTION_ROUND_ROBIN_FOR_HYBRID_STRATEGY 8
#define PATH_SELECTION_SMALL_LATENCY_FIRST_OPTIMIZED_STRATEGY 9
#define PATH_SELECTION_CONGA_STRATEGY 10

#define FLOWLET_DEFAULT_TIMEOUT_IN_NANOSECOND 100000

#define CONGA_DRE_DECRESE_TIME_IN_US 160 // conga
#define CONGA_EXPIRED_TIME_IN_US 200     // conga
#define CONGA_SW_PORT_NUM 10             // conga

#define REORDER_SAMPLE_RATIO 1

namespace ns3
{

  class E2ESrcOutPackets : public SimpleRefCount<E2ESrcOutPackets>
  {
  public:
    Ptr<Packet> dataPacket = NULL;
    Ptr<Packet> probePacket = NULL;
    Ptr<Packet> ackPacket = NULL;
    uint64_t latencyForDataPktInNs = 0;
    uint32_t pidForDataPkt = 0;
    
    bool isData = false;
    bool isProbe = false;
    bool isAck = false;

    Ptr<RdmaQueuePair> lastQp = NULL;
    E2ESrcOutPackets(Ptr<Packet> dataPacket, bool Isprobe, Ptr<Packet> probePacket)
    {
      this->dataPacket = dataPacket;
      this->probePacket = probePacket;
      this->isProbe = Isprobe;

    }
    E2ESrcOutPackets() : dataPacket(NULL), probePacket(NULL), ackPacket(NULL), isData(false), isProbe(false), isAck(false), lastQp(NULL) {}
  };



//========================相对误差记录====================
struct RelErrorStats
{
    // 0.1% 精度
    static constexpr int32_t SCALE = 1000;

    // [-200%, +200%]
    static constexpr int32_t MIN_VAL = -500;
    static constexpr int32_t MAX_VAL =  500;
    static constexpr uint32_t BIN_NUM = MAX_VAL - MIN_VAL + 1;

    // 相对误差分布
    std::vector<uint64_t> ack_bins;
    std::vector<uint64_t> e2e_bins;
//=====路径跳数维度,统计跳数越大，实验测量误差越大-=========================
   struct HopDiffStats
{
    uint64_t sample_cnt = 0;

    int64_t ack_diff_sum  = 0;  // Σ(ack_ns - real_ns)
    int64_t data_diff_sum = 0;  // Σ(data_ns - real_ns)
    int64_t  measurement_delay= 0;
    int64_t  ACK_measurement_delay= 0;
};
// hop_count -> statistics
std::map<uint32_t, HopDiffStats> hop_diff_table;
inline void RecordHopDiff(uint32_t hop,
                          int64_t real_ns,
                          int64_t ack_ns,
                          int64_t data_ns)
{
    if (real_ns <= 0) return;

    auto &stat = hop_diff_table[hop];
    stat.sample_cnt++;

stat.ack_diff_sum  += std::llabs(ack_ns  - real_ns);
stat.data_diff_sum += std::llabs(data_ns - real_ns);
stat.measurement_delay+=data_ns + hop*1084;
stat.ACK_measurement_delay+=hop*1024;
//std::cout << "hop: " << hop << " real_ns: " << real_ns << " ack_ns: " << ack_ns << " data_ns: " << data_ns << " ack_diff: " << stat.ack_diff_sum << " data_diff: " << stat.data_diff_sum << std::endl;
}
void DumpHopDiffStats(const std::string &basePath)
{
    std::string file_name = basePath + "hop_diff.txt";
    FILE *file = fopen(file_name.c_str(), "w");
    if (file == nullptr)
    {
        perror("Error opening hop diff file");
        return;
    }

    // 每一行：hop_cnt sample_cnt avg_ack_abs_diff(ns) avg_data_abs_diff(ns)
    for (const auto &kv : hop_diff_table)
    {
        uint32_t hop = kv.first;
        const HopDiffStats &stat = kv.second;

        if (stat.sample_cnt == 0)
            continue;

        double ack_avg  = double(stat.ack_diff_sum)  / stat.sample_cnt;
        double data_avg = double(stat.data_diff_sum) / stat.sample_cnt;
        double measurement_delay_avg = stat.measurement_delay / stat.sample_cnt;
        double ACK_measurement_delay_avg = stat.ACK_measurement_delay / stat.sample_cnt;
        fprintf(file, "%u %lu %.3f %.3f %.3f %.3f\n",
                hop,
                stat.sample_cnt,
                ack_avg,
                data_avg,
                measurement_delay_avg,
               ACK_measurement_delay_avg);
    }

    fflush(file);
    fclose(file);
}

//=====路径跳数维度,统计跳数越大，实验测量误差越大-=========================

//=====拥塞度维度,拥塞度越大，实验测量误差越大-=========================

enum CongestionLevel
{
    C1  = 1,
    C3  = 3,
    C5  = 5,
    C7  = 7,
    C9 = 9,
    C11=11

};

struct CongDiffStats
{
    uint64_t sample_cnt = 0;

    int64_t ack_diff_sum  = 0;
    int64_t data_diff_sum = 0;
    int64_t  measurement_delay= 0;
    int64_t  ACK_measurement_delay= 0;
};

std::map<CongestionLevel, CongDiffStats> cong_diff_table;
inline void RecordCongestionDiff(
                                 int64_t baseline_ns,
                                 int64_t real_ns,
                                 int64_t ack_ns,
                                 int64_t data_ns)
{
    if (baseline_ns <= 0 || real_ns <= 0)
        return;

    double ratio = double(data_ns) / baseline_ns;

    CongestionLevel level;
    if (ratio < 3.0)
        level = C1;
    else if (ratio < 5.0)
        level = C3;
    else if (ratio < 7.0)
        level = C5;
    else if (ratio < 9.0)
        level = C7;
    else if (ratio < 11.0)
        level = C9;
    else
        level = C11;

    auto &stat = cong_diff_table[level];
    stat.sample_cnt++;

  stat.ack_diff_sum  += std::llabs(ack_ns  - real_ns);
stat.data_diff_sum += std::llabs(data_ns - real_ns);
stat.measurement_delay+=(data_ns + baseline_ns);
stat.ACK_measurement_delay+=int(baseline_ns/1000)*1024;
////std::cout << "level: " << level << " real_ns: " << real_ns << " ack_ns: " << ack_ns << " data_ns: " << data_ns << " ack_diff: " << stat.ack_diff_sum << " data_diff: " << stat.data_diff_sum << std::endl;
}

void DumpCongestionDiffStats(const std::string &basePath)
{
    std::string file_name = basePath + "congestion_diff.txt";
    FILE *file = fopen(file_name.c_str(), "w");
    if (file == nullptr)
    {
        perror("Error opening congestion diff file");
        return;
    }

    // 每一行：congestion_level sample_cnt avg_ack_abs_diff(ns) avg_data_abs_diff(ns)
    for (const auto &kv : cong_diff_table)
    {
        CongestionLevel level = kv.first;
        const CongDiffStats &stat = kv.second;

        if (stat.sample_cnt == 0)
            continue;

        double ack_avg  = double(stat.ack_diff_sum)  / stat.sample_cnt;
        double data_avg = double(stat.data_diff_sum) / stat.sample_cnt;
        double measurement_delay_avg = stat.measurement_delay / stat.sample_cnt;
         double ACK_measurement_delay_avg = stat.ACK_measurement_delay / stat.sample_cnt;
        fprintf(file, "%d %lu %.3f %.3f %.3f %.3f\n",
                static_cast<int>(level),
                stat.sample_cnt,
                ack_avg,
                data_avg,
                measurement_delay_avg,
                ACK_measurement_delay_avg
            );
    }

    fflush(file);
    fclose(file);
}

//=====拥塞度维度,拥塞度越大，实验测量误差越大-=========================
//=====记录最大权重分布在0.5-0.9的有多少-=========================

struct highest_weight_count
{
    uint64_t sample_cnt = 0;
};

std::map<uint32_t,highest_weight_count> highest_weight_interval_count;
uint32_t highest_weight_sum_count = 0;
inline void RecordHighestWeightCount(uint32_t interval)
    {

       highest_weight_interval_count[interval].sample_cnt++;

        highest_weight_sum_count++;
    }

void CoutHighestWeightPercent(void) const
{
  std::vector<std::pair<uint32_t, highest_weight_count>> sorted_items;
   for(const auto &kv : highest_weight_interval_count){
       sorted_items.push_back(kv);
   }
   std::sort(sorted_items.begin(), sorted_items.end(),
             [](const std::pair<uint32_t, highest_weight_count> &a, 
                const std::pair<uint32_t, highest_weight_count> &b) {
                 return a.first < b.first;
             });

   for(const auto &kv : sorted_items){
    std::cout<<"本次仿真每次喷洒时路径最大权重>"<<kv.first<<"%的概率, "<<(double)(kv.second.sample_cnt)/highest_weight_sum_count<<std::endl;
   }
}


//=====记录最大权重分布在0.5-0.9的有多少-=========================
    // 样本数：
    uint64_t sample_cnt = 0;
     uint64_t rejected_cnt = 0;
    RelErrorStats()
    {
        ack_bins.resize(BIN_NUM, 0);
        e2e_bins.resize(BIN_NUM, 0);
    }

    // inline void Record(int64_t real_ns,
    //                    int64_t ack_ns,
    //                    int64_t data_ns,int64_t baseline_delay)
    // {
    //     if (real_ns <= 0)
    //         return;

    //     // ACK 相对误差
    //     int64_t diff_ack = ack_ns - real_ns;
    //     int32_t rel_ack = static_cast<int32_t>(
    //         (diff_ack * SCALE) / real_ns + 0.5
    //     );

    //     if (rel_ack < MIN_VAL) rel_ack = MIN_VAL;
    //     if (rel_ack > MAX_VAL) rel_ack = MAX_VAL;

    //     ack_bins[rel_ack - MIN_VAL]++;

    //     // e2e 相对误差
    //     int64_t diff_e2e = data_ns - real_ns;
    //     int32_t rel_e2e = static_cast<int32_t>(
    //         (diff_e2e * SCALE) / real_ns + 0.5
    //     );

    //     if (rel_e2e < MIN_VAL) rel_e2e = MIN_VAL;
    //     if (rel_e2e > MAX_VAL) rel_e2e = MAX_VAL;

    //     e2e_bins[rel_e2e - MIN_VAL]++;

    //     sample_cnt++;
    // }

//==============分母由路径实际时延变为基准时延
inline void Record(int64_t real_ns,
                       int64_t ack_ns,
                       int64_t data_ns,int64_t baseline_delay)
    {
        if (baseline_delay <= 0)
            return;

        // ACK 相对误差
        int64_t diff_ack = ack_ns - real_ns;
        int32_t rel_ack = static_cast<int32_t>(
            (double)(diff_ack * 100) / baseline_delay + 0.5
        );

        if (rel_ack < MIN_VAL) rel_ack = MIN_VAL;
        if (rel_ack > MAX_VAL) rel_ack = MAX_VAL;

        ack_bins[rel_ack - MIN_VAL]++;

        // e2e 相对误差
        int64_t diff_e2e = data_ns - real_ns;
        int32_t rel_e2e = static_cast<int32_t>(
            (double)(diff_e2e * 100) / baseline_delay + 0.5
        );

        if (rel_e2e < MIN_VAL) rel_e2e = MIN_VAL;
        if (rel_e2e > MAX_VAL) rel_e2e = MAX_VAL;

        e2e_bins[rel_e2e - MIN_VAL]++;

        sample_cnt++;
    }


//     void Dump(const std::string &basePath) const
// {
//     std::string file_name = basePath + "relative_error.txt";
//     FILE *file = fopen(file_name.c_str(), "w");
//     if (file == nullptr)
//     {
//         perror("Error opening relative error file");
//         return;
//     }

//     // 第一行：追踪到的 ACK 样本总数
//     fprintf(file, "%lu\n", sample_cnt);

//     // 每一行：相对误差(%) ACK_count E2E_count
//     for (uint32_t i = 0; i < BIN_NUM; ++i)
//     {
//         int32_t rel = MIN_VAL + i;
//         fprintf(file, "%.1f %lu %lu\n",
//                 rel / 10.0,
//                 ack_bins[i],
//                 e2e_bins[i]);
//     }

//     fflush(file);
//     fclose(file);
// }
    void Dump(const std::string &basePath) const
{
    std::string file_name = basePath + "relative_error.txt";
    FILE *file = fopen(file_name.c_str(), "w");
    if (file == nullptr)
    {
        perror("Error opening relative error file");
        return;
    }

    // 第一行：追踪到的 ACK 样本总数
    fprintf(file, "%lu\n", sample_cnt);

    // 每一行：相对误差(%) ACK_count E2E_count
    for (uint32_t i = 0; i < BIN_NUM; ++i)
    {
        int32_t rel = MIN_VAL + i;
        fprintf(file, "%.1f %lu %lu\n",
                rel / 1.0,
                ack_bins[i],
                e2e_bins[i]);
    }

    fflush(file);
    fclose(file);
}

};
//================================================================================

struct record_utilization_rate{
    uint32_t time=0;
    //给平均带宽利用率使用
    uint64_t update_count=0;
    double utilization_rate=0;
};
struct record_queue_length{
      uint32_t length=0;
};

 struct e2e_all_flow_dur{
     uint64_t starttime=-1;
     //========
      uint64_t endtime=-1;
    };


    struct  record_path_send_data{
     uint64_t data_byte=0;
      uint32_t path_length=0;
    };
 struct cal{
     uint64_t sum_data_byte=0;
    /// 键值:pid
    std::map<uint32_t, record_path_send_data> record_path_send;    
    };

struct record_avg_queue_length{
      double now_avg_port_length=0;
      uint32_t count=0;

};
struct record_ack_packet_delay{
      uint32_t count=0;
      std::vector<uint32_t> ackdelay  = std::vector<uint32_t>(2600, 0);
      std::vector<uint32_t> packetdelay = std::vector<uint32_t>(2600, 0);
};


  class RdmaSmartFlowRouting : public Object
  {
    // friend class SwitchMmu;
    // friend class SwitchNode;

  public:
    RdmaSmartFlowRouting();
    virtual ~RdmaSmartFlowRouting();
    
    
    struct send_data{
     uint64_t time=0;
     //========
     uint64_t port_rate=-1;
      uint64_t send_data_byte=0;
    };
    
       //===========================每个节点记录各个端口定期的数据发送量用来计算带宽利用率=======================
     std::map<uint32_t, send_data> record_each_port_send_data;
     //两节点，路径id,利用率随时间变化数组；记录节点对之间所有路径利用率随时间变化
         static std::map<HostId2PathSeleKey, std::map<uint32_t,std::vector<record_utilization_rate>>>record_path_utilization_rate;
         //记录两点之间所有流最早开始时间和最晚完成时间。
         static std::map<HostId2PathSeleKey,e2e_all_flow_dur >record_path_e2e_all_flow_dur;

         //记录所有节点对之间产生流的数量，降序排列
         static std::map<HostId2PathSeleKey,uint32_t> record_path_flow_num;
          static std::vector<std::pair<HostId2PathSeleKey, uint32_t>> sorted_path_flow_counts;
//==========记录每个pst表中各路径上个发了多少数据，还记录了一个pst表里面相同长度路径的个数，以次来计算不同长度的路径被选择的概率怎么样，预期时一个pst中最短路径被选择的概率最高，还要除掉路径个数对结果的影响。
       static std::map<HostId2PathSeleKey,cal > record_path_cal;
       //记录当带宽利用率超过80%以上的端口队列平均长度
       static record_avg_queue_length avg_port_length;

         static std::map<uint64_t,uint64_t > RecordAckSendTime;

//===============================记录端口队列长度，每2us 记录一次===================
static std::map<uint32_t, std::map<uint32_t,std::vector<record_queue_length>>> record_all_port_queue_len;
//===========================用上面的数据计算平均队列长度============
static std::map<uint32_t, std::map<uint32_t,record_avg_queue_length>> record_all_port_AVG_queue_len;
//=======================================记录ACK和数据包测量延迟的变量====================
static record_ack_packet_delay m_record_ack_packet_delay;
    //-------------------------------------------------------------------启动laps_plus-----------------------------------------------------
     static std::map<uint32_t, std::map<uint32_t,std::vector<record_utilization_rate>>> record_all_port_utilization_rate;
     static std::map<uint32_t, std::map<uint32_t,record_utilization_rate>> record_all_port_avg_utilization_rate;
    static bool enable_laps_plus;
    static bool is_sim_finish;
    static u_int32_t choose_softmax;
     static uint64_t sum_data_receive;
    static uint64_t sum_data;
     static uint64_t key;
      static uint64_t  record_time;
    static std::vector<probeInfoEntry> m_prbInfoTable;
    static std::map<std::string, reorder_entry_t> m_reorderTable;
    static double laps_alpha;
    static std::uniform_real_distribution<double> rndGen;
    static std::default_random_engine generator;
    static std::map<uint32_t,uint32_t> pathPair;
    static void setPathPair(std::vector<PathData> &PIT);
    static std::map<uint32_t, std::vector<uint32_t>> m_pathDelayRecordTable;
    static bool enableRecordPathlatency;
    Time m_RecordTimeGap = MicroSeconds(50);
    EventId m_recordPathLantencyEvent;

    // static std::map<uint32_t, std>pathDelayRecordTable

  public:
    static TypeId GetTypeId(void);
    void pathDelayRecord();
    // virtual Ptr<Ipv4Route> RouteOutput (Ptr<Packet> p, const Ipv4Header &header, Ptr<NetDevice> oif, Socket::SocketErrno &sockerr);
    // virtual bool RouteInput  (Ptr<const Packet> p, const Ipv4Header &header, Ptr<const NetDevice> idev, UnicastForwardCallback ucb, MulticastForwardCallback mcb, LocalDeliverCallback lcb, ErrorCallback ecb);
    // Ptr<Ipv4Route> ConstructIpv4Route (uint32_t port, Ipv4Address &destAddress);
    bool RouteInput(Ptr<Packet> p, CustomHeader ch);
    void RouteOutput(Ptr<Packet> p, CustomHeader ch, Ptr<E2ESrcOutPackets> &SrcOutEntry); // E2E LB input
    bool e2eLBSrc_output_packet(Ptr<E2ESrcOutPackets> &SrcOutEntry);
      static void RecordAllPortQueueLength();
    void add_latency_tag_by_pit_entries(Ptr<Packet> packet, std::vector<PathData *> &pitEntries);
    void add_path_tag_by_path_id(Ptr<Packet> packet, uint32_t pid);
    void add_probe_tag_by_path_id(Ptr<Packet> packet, uint32_t expiredPathId);
    std::vector<PathData *> batch_lookup_PIT(std::vector<uint32_t> &pids);
    Ipv4SmartFlowPathTag construct_path_tag(uint32_t selectedPathId);
    Ptr<Packet> construct_probe_packet(Ptr<Packet> &pkt, CustomHeader &ch);
    Ptr<Packet> construct_reply_probe_packet(Ptr<Packet> &pkt, CustomHeader &ch);
    Ipv4SmartFlowProbeTag construct_probe_tag_by_path_id(uint32_t expiredPathId);
    bool exist_path_tag(Ptr<Packet> packet, Ipv4SmartFlowPathTag &pathTag);
    bool exist_probe_tag(Ptr<Packet> packet, Ipv4SmartFlowProbeTag &probeTag);
    bool exist_ack_tag(Ptr<Packet> packet, AckPathTag &ackTag);

    uint32_t forward_normal_packet(Ptr<Packet> &p, CustomHeader &ch, uint32_t srcToRId, uint32_t dstToRId, uint32_t pg, Ptr<E2ESrcOutPackets> &SrcOutEntry);
    // uint32_t forward_probe_packet(Ptr<Packet> pkt, std::vector<PathData *> &forwardPitEntries, PathData *bestPitEntry, const Ipv4Header &header, UnicastForwardCallback ucb, Ipv4Address dstServerAddr);
    uint32_t forward_probe_packet_optimized(Ptr<Packet> pkt, std::vector<PathData *> &forwardPitEntries, CustomHeader &ch, uint32_t pg, Ptr<E2ESrcOutPackets> &SrcOutEntry);

    uint32_t get_egress_port_id_by_path_tag(Ipv4SmartFlowPathTag &smartFlowTag);
    // uint32_t get_node_id(void) const;
    uint32_t get_max_piggyback_path_number() const;
    uint32_t get_path_expire_interval() const;
    std::map<uint32_t, PathData> get_PIT() const;
    uint32_t get_probing_interval() const;
    std::map<HostId2PathSeleKey, pstEntryData> get_PST() const;
    // PathData *get_the_best_forwarding_path(std::vector<PathData *> &pitEntries, pstEntryData *pstEntry, Ptr<const Packet> packet, const Ipv4Header &header);
    std::vector<PathData *> get_the_best_piggyback_paths(std::vector<PathData *> &pitEntries);
    uint32_t get_the_expired_paths(std::vector<PathData *> &allPitEntries, std::vector<PathData *> &expiredPitEntries);
    // PathData *get_the_flowlet_path(pstEntryData *pstEntry, std::vector<PathData *> &pitEntries, Ptr<const Packet> packet, const Ipv4Header &header);
    // PathData *get_the_hashing_path(std::vector<PathData *> &pitEntries, Ptr<const Packet> packet, const Ipv4Header &header);
    PathData *get_the_highest_priority_path(std::vector<PathData *> &pitEntries);
    std::vector<PathData *> get_the_newly_measured_paths(std::vector<PathData *> &pitEntries);
    PathData *get_the_oldest_measured_path(std::vector<PathData *> &pitEntries);
    uint32_t get_the_path_length_by_path_id(const uint32_t pathId, PathData *&pitEntry);
    std::vector<PathData *> get_the_piggyback_pit_entries(uint32_t srcToRId, uint32_t dstHostId);
    uint32_t get_the_potential_paths(PathData *bestPitEntry, std::vector<PathData *> &allPitEntries, std::vector<PathData *> &potentialPitEntries);
    uint32_t get_the_probe_paths(std::vector<PathData *> &expiredPitEntries, std::vector<PathData *> &probePitEntries);
    PathData *get_the_random_path(std::vector<PathData *> &pitEntries);
    // PathData *get_the_round_robin_path_for_packet(pstEntryData *pstEntry, std::vector<PathData *> &pitEntries);
    // PathData *get_the_round_robin_path_for_flow(pstEntryData *pstEntry, std::vector<PathData *> &pitEntries, Ptr<const Packet> packet, const Ipv4Header &header);
    // PathData *get_the_round_robin_path_for_hybird(pstEntryData *pstEntry, std::vector<PathData *> &pitEntries, Ptr<const Packet> packet, const Ipv4Header &header);
    // PathData *get_the_least_congested_path(pstEntryData *pstEntry, std::vector<PathData *> &pitEntries, Ptr<const Packet> packet, const Ipv4Header &header);

    // uint32_t get_the_routing_index();

    PathData *get_the_smallest_latency_path(std::vector<PathData *> &pitEntries);
    std::map<Ipv4Address, hostIp2SMT_entry_t> get_SMT() const;
    PathData *get_the_best_probing_path(std::vector<PathData *> &pitEntries);
    void initialize();
    uint32_t install_PIT(std::map<uint32_t, PathData> &pit);
    uint32_t install_PST(std::map<HostId2PathSeleKey, pstEntryData> &pst);
    uint32_t install_SMT(std::map<Ipv4Address, hostIp2SMT_entry_t> &SMT);
    PathData *lookup_PIT(uint32_t pieKey);
    pstEntryData *lookup_PST(HostId2PathSeleKey &pstKey);
    pdt_entry_t *lookup_PDT(HostId2PathSeleKey &pstKey);
    hostIp2SMT_entry_t *lookup_SMT(const Ipv4Address &serverAddr);
    uint32_t print_PIT();
    uint32_t print_PST();
    uint32_t print_SMT();
    std::string ipv4Address2string(Ipv4Address addr);
    std::string construct_target_string_strlen(uint32_t strLen, std::string c);
    bool output_packet_by_path_tag(Ptr<Packet> packet, CustomHeader &ch, uint32_t pg);

    bool reach_the_last_hop_of_path_tag(Ipv4SmartFlowPathTag &smartFlowTag, PathData *&pitEntry);

    void receive_normal_packet(Ptr<Packet> &pkt, Ipv4SmartFlowPathTag &pathTag, PathData *&pitEntry);
    void receive_probe_packet(Ipv4SmartFlowProbeTag &probeTag);
    void record_the_probing_info(uint32_t pathId);
    Ptr<Packet> reply_probe_info(Ptr<Packet> &p, CustomHeader &ch);
    // void record_reorder_at_dst_tor(Ptr<Packet> &pkt, const Ipv4Header &header);

    void set_max_piggyback_path_number(uint32_t piggyLatencyCnt);
    void set_path_expire_interval(uint32_t a);
    void set_PIT(std::map<uint32_t, PathData> &nexthopSelTbl);
    void set_probing_interval(uint32_t probeTimeInterval);
    void set_PST(std::map<HostId2PathSeleKey, pstEntryData> &pathSelTbl);
    void set_SMT(std::map<Ipv4Address, hostIp2SMT_entry_t> &vmVtepMapTbl);
    void update_path_tag(Ptr<Packet> &packet, Ipv4SmartFlowPathTag &smartFlowTag);
    uint32_t update_PIT_after_piggybacking(std::vector<PathData *> &piggyBackPitEntries);
    void update_PIT_after_probing(PathData *pitEntry);
    void update_PIT_by_latency_data(LatencyData &latencyData);
    void update_PIT_by_latency_tag(Ptr<Packet> &packet);
    void update_PIT_by_path_tag(Ipv4SmartFlowPathTag &pathTag, PathData *&pitEntry);
    void update_PIT_by_probe_tag(Ipv4SmartFlowProbeTag &probeTag);
    void update_PIT_after_adding_path_tag(PathData *forwardPitEntry);
    void update_PST_after_adding_path_tag(pstEntryData *pstEntry, PathData *forwardPitEntry);
    bool insert_entry_to_PIT(PathData &pitEntry);
    bool insert_entry_to_PST(pstEntryData &pstEntry);
    bool insert_entry_to_SMT(hostIp2SMT_entry_t &smtEntry);

    std::vector<double> CalPathWeightBasedOnDelay(const std::vector<PathData *> paths , u_int32_t ch_seq=1);
    uint32_t GetPathBasedOnWeight(const std::vector<double> & weights);


    // void add_conga_tag_by_pit_entry(Ptr<Packet> packet, PathData *pitEntry);
    // void update_PIT_by_conga_tag(Ipv4SmartFlowCongaTag &congaTag);
    std::vector<uint32_t> get_dre_of_egress_ports(std::vector<uint32_t> &ports);
    // uint32_t QuantizingX(uint32_t X);
    // uint32_t UpdateLocalDre(const Ipv4Header &header, Ptr<Packet> packet, uint32_t port);

    void SetSwitchInfo(bool isToR, uint32_t switch_id);
    void SetNode(Ptr<Node> node);
    bool IsE2ELb(void);

    // void DreEvent();
    // void AgingEvent();
    // void PrintDreTable();
    /*-----CALLBACK------*/
    void DoSwitchSend(Ptr<Packet> p, CustomHeader &ch, uint32_t outDev,
                      uint32_t qIndex);                      // TxToR and Agg/CoreSw
    void DoSwitchSendToDev(Ptr<Packet> p, CustomHeader &ch); // only at RxToR
    typedef Callback<void, Ptr<Packet>, CustomHeader &, uint32_t, uint32_t> SwitchSendCallback;
    typedef Callback<void, Ptr<Packet>, CustomHeader &> SwitchSendToDevCallback;
    void SetSwitchSendCallback(SwitchSendCallback switchSendCallback);                // set callback
    void SetSwitchSendToDevCallback(SwitchSendToDevCallback switchSendToDevCallback); // set callback

    void RouteOutputForAckPktOnSrcHostForLaps(Ptr<E2ESrcOutPackets> entry);
    void RouteOutputForDataPktOnSrcHostForLaps(Ptr<E2ESrcOutPackets> entry);
    PathData * CheckProbePathAmoungPitEntries(std::vector<PathData *> & pitEntries);
     //===========================================================新增部分====================================
		// BDP数据结构和管理
		struct PathBdpInfo {
			uint64_t currentBdp;  // 当前BDP占用
			uint64_t maxBdp;      // 最大BDP容量
			PathBdpInfo() : currentBdp(0), maxBdp(0) {}
		};
		// 路径BDP管理相关
		std::map<uint32_t, PathBdpInfo> m_pathBdpMap; // PID到BDP信息的映射
 uint32_t IsBDPAllFull(Ipv4Address srcServerAddr,Ipv4Address dstServerAddr);


static RelErrorStats s_relErrorStats;
//===========================================================新增部分====================================
   static std::map<uint32_t, Ptr<Node>> nodeIdToNodeMap;

  private:
    // callback
    SwitchSendCallback m_switchSendCallback;           // bound to SwitchNode::SwitchSend (for Request/UDP)
    SwitchSendToDevCallback m_switchSendToDevCallback; // bound to SwitchNode::SendToDevContinue (for Probe, Reply)
    // Ipv4 associated with this router
    // Ptr<Ipv4> m_ipv4;

    std::map<HostId2PathSeleKey, pstEntryData> m_pathSelTbl;
    std::map<std::string, flet_entry_t> m_fletTbl;
    std::map<std::string, rbn_entry_t> m_rbnTbl;
    std::map<uint32_t, PathData> m_nexthopSelTbl;
    std::map<Ipv4Address, hostIp2SMT_entry_t> m_vmVtepMapTbl;
    std::map<HostId2PathSeleKey, pdt_entry_t> m_pathDecTbl;

    uint32_t m_nodeId;
    uint32_t m_switch_id;
    Ptr<Node> m_node;
    uint32_t m_probeStrategy;
    uint32_t m_pathExpiredTimeThld;
    uint32_t m_pathSelNum;
    uint32_t m_pathSelStrategy; //
    uint32_t m_piggybackStrategy;
    uint32_t m_flowletTimoutInNs;
    bool m_enableFeedbackAllPathInfo;
    bool m_enabledAllPacketFeedbackInfo;
    bool m_isToR;

    bool lb_isInstallSever;
    EventId m_dreEvent;
    // Metric aging event
    EventId m_agingEvent;

    // Parameters
    // DRE
    std::map<uint32_t, uint32_t> m_XMap;
    double m_alpha;
    uint32_t m_Q;
    // DataRate m_C;
    Time m_agingTime;
    Time m_tdre;
    uint32_t m_probeTimeInterval;
    uint32_t m_piggyLatencyCnt;
    uint32_t m_reorderFlag;

    // Quantizing bits
  };

}

#endif /* RDMA_SMARTFLOW_ROUTING_H */
