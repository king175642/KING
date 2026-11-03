#include <functional> // Path timeout protocol self-check callbacks.
#include <ns3/simulator.h>
#include <ns3/seq-ts-header.h>
#include <ns3/udp-header.h>
#include <ns3/ipv4-header.h>
#include "ns3/ppp-header.h"
#include "ns3/boolean.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "ns3/data-rate.h"
#include "ns3/pointer.h"
#include "rdma-hw.h"
#include "ppp-header.h"
#include "qbb-header.h"
#include "cn-header.h"
#include "ns3/flow-id-num-tag.h"
#include "flow-stat-tag.h"
#include "ns3/callback.h"
#include <set>
#include <time.h>
#include <ns3/switch-node.h>
#include <ns3/rdma-driver.h>
namespace ns3
{
	// bool RdmaHw::isIrnEnabled = false;
	NS_OBJECT_ENSURE_REGISTERED(RdmaHw);
	NS_LOG_COMPONENT_DEFINE("RdmaHw");
	std::map<uint32_t, std::map<uint32_t, PlbRecordEntry>> RdmaHw::m_plbRecordOutInf;
	std::map<uint32_t, std::map<std::string, std::map<uint64_t, RecordCcmodeOutEntry>>> RdmaHw::ccmodeOutInfo;
	std::map<uint32_t, std::map<std::string, std::map<uint32_t, LostPacketEntry>>> RdmaHw::m_lossPacket;
	std::map<std::string, std::string> RdmaHw::m_recordQpSen;
	std::map<std::string, std::vector<RecordFlowRateEntry_t>> RdmaHw::m_qpRatechange;
	uint32_t RdmaHw::flowComplteNum;
	std::map<uint32_t, std::vector<RecordFlowRateEntry_t>> RdmaHw::recordRateMap;		  //
	std::map<uint32_t, std::vector<RecordPathDelayEntry_t>> RdmaHw::recordPathDelayMap;		  //
	std::map<uint32_t, uint64_t> RdmaHw::pidToThDelay;		  //
	std::map<uint32_t, pstEntryData *> RdmaHw::flowToPstEntry;
	uint32_t RdmaHw::qpFlowIndex;
	std::map<uint32_t, QpRecordEntry> RdmaHw::m_recordQpExec;
	std::map<uint32_t, uint64_t> RdmaHw::RecordPacketHop;

	bool RdmaHw::enablePathDelayRecord = false;

	void RdmaHw::insertRateRecord(uint32_t flowId, uint64_t curRateInMbps)
	{
		if (RdmaHw::enableRateRecord == false)
		{
			return;
		}
		auto it = RdmaHw::recordRateMap.find(flowId);
		if (it == RdmaHw::recordRateMap.end())
		{
			recordRateMap[flowId].push_back(RecordFlowRateEntry_t(curRateInMbps, Simulator::Now().GetNanoSeconds(), 0));
		}
		else
		{
			std::vector<RecordFlowRateEntry_t> &pathDelayVec = it->second;
			uint64_t prev_rate_in_Mbps = pathDelayVec.back().rateInMbps;
			uint64_t prev_time_in_ns = pathDelayVec.back().startTimeInNs;
			pathDelayVec.back().durationInNs = Simulator::Now().GetNanoSeconds() - prev_time_in_ns;
			if (curRateInMbps != prev_rate_in_Mbps)
			{
				pathDelayVec.push_back(RecordFlowRateEntry_t(curRateInMbps, Simulator::Now().GetNanoSeconds(), 0));
			}
		}
	}

	void RdmaHw::insertPathDelayRecord(uint32_t pid, uint64_t pathDelayInNs)
	{
		if (RdmaHw::enablePathDelayRecord == false)
		{
			return;
		}
		auto it = RdmaHw::recordPathDelayMap.find(pid);
		if (it == RdmaHw::recordPathDelayMap.end())
		{
			recordPathDelayMap[pid].push_back(RecordPathDelayEntry_t(pathDelayInNs, Simulator::Now().GetNanoSeconds(), 0));
		}
		else
		{
			std::vector<RecordPathDelayEntry_t> &pathDelayVec = it->second;
			uint64_t prev_delay_in_ns = pathDelayVec.back().delayInNs;
			uint64_t prev_time_in_ns = pathDelayVec.back().startTimeInNs;
			pathDelayVec.back().durationInNs = Simulator::Now().GetNanoSeconds() - prev_time_in_ns;
			if (pathDelayInNs != prev_delay_in_ns)
			{
				pathDelayVec.push_back(RecordPathDelayEntry_t(pathDelayInNs, Simulator::Now().GetNanoSeconds(), 0));
			}
		}
	}

	void RdmaHw::printRateRecordToFile(std::string fileName)
	{
		if (RdmaHw::enableRateRecord == false)
		{
			return;
		}

		FILE *fileHandle = fopen(fileName.c_str(), "w");
		if (fileHandle == NULL)
		{
			std::cout << "Error for Cannot open file " << fileName << std::endl;
			return;
		}

		for (auto &flowRateEntry : RdmaHw::recordRateMap)
		{
			uint32_t flowId = flowRateEntry.first;
			fprintf(fileHandle, "FlowId %u ", flowId);
			std::vector<RecordFlowRateEntry_t> &rateVec = flowRateEntry.second;
			for (auto &rateEntry : rateVec)
			{
				fprintf(fileHandle, "%s ", rateEntry.to_string().c_str());
			}
			fprintf(fileHandle, "\n");
		}
		fflush(fileHandle);
		fclose(fileHandle);
	}

	void RdmaHw::printPathDelayRecordToFile(std::string fileName)
	{
		if (RdmaHw::enablePathDelayRecord == false)
		{
			return;
		}

		FILE *fileHandle = fopen(fileName.c_str(), "w");
		if (fileHandle == NULL)
		{
			std::cout << "Error for Cannot open file " << fileName << std::endl;
			return;
		}

		for (auto &pathDelayEntry : RdmaHw::recordPathDelayMap)
		{
			uint32_t pid = pathDelayEntry.first;
			fprintf(fileHandle, "%u %lu ", pid, pidToThDelay[pid]);
			std::vector<RecordPathDelayEntry_t> &pathDelayVec = pathDelayEntry.second;
			for (auto &pathDelay : pathDelayVec)
			{
				fprintf(fileHandle, "%s ", pathDelay.to_string().c_str());
			}
			fprintf(fileHandle, "\n");
		}
		fflush(fileHandle);
		fclose(fileHandle);
	}

	bool RdmaHw::enableRateRecord = true;
	std::map<uint32_t, Ptr<RdmaQueuePair>> m_flowId2Qp;
	TypeId RdmaHw::GetTypeId(void)
	{
		static TypeId tid = TypeId("ns3::RdmaHw")
								.SetParent<Object>()
								.AddAttribute("MinRate",
											  "Minimum rate of a throttled flow",
											  DataRateValue(DataRate("100Mb/s")),
											  MakeDataRateAccessor(&RdmaHw::m_minRate),
											  MakeDataRateChecker())
								.AddAttribute("Mtu",
											  "Mtu.",
											  UintegerValue(1000),
											  MakeUintegerAccessor(&RdmaHw::m_mtu),
											  MakeUintegerChecker<uint32_t>())
								.AddAttribute("CcMode",
											  "which mode of CC Algorithm is running on Server",
											  EnumValue(CongestionControlMode::CC_NONE),
											  MakeEnumAccessor(&RdmaHw::m_cc_mode),
											  MakeEnumChecker(CongestionControlMode::DCQCN_MLX, "Dcqcn_mlx",
															  CongestionControlMode::HPCC_PINT, "Hpcc_pint",
															  CongestionControlMode::DCTCP, "Dctcp",
															  CongestionControlMode::TIMELY, "Timely",
															  CongestionControlMode::HPCC, "Hpcc",
															  CongestionControlMode::CC_LAPS, "Laps",
															  CongestionControlMode::CC_NONE, "None"))
								.AddAttribute("NACKInterval",
											  "The NACK Generation interval",
											  DoubleValue(500.0),
											  MakeDoubleAccessor(&RdmaHw::m_nack_interval),
											  MakeDoubleChecker<double>())
								.AddAttribute("L2ChunkSize",
											  "Layer 2 chunk size. Disable chunk mode if equals to 0.",
											  UintegerValue(0),
											  MakeUintegerAccessor(&RdmaHw::m_chunk),
											  MakeUintegerChecker<uint32_t>())
								.AddAttribute("L2AckInterval",
											  "Layer 2 Ack intervals. Disable ack if equals to 0.",
											  UintegerValue(0),
											  MakeUintegerAccessor(&RdmaHw::m_ack_interval),
											  MakeUintegerChecker<uint32_t>())
								.AddAttribute("L2BackToZero",
											  "Layer 2 go back to zero transmission.",
											  BooleanValue(false),
											  MakeBooleanAccessor(&RdmaHw::m_backto0),
											  MakeBooleanChecker())
								.AddAttribute("EwmaGain",
											  "Control gain parameter which determines the level of rate decrease",
											  DoubleValue(1.0 / 16),
											  MakeDoubleAccessor(&RdmaHw::m_g),
											  MakeDoubleChecker<double>())
								.AddAttribute("RateOnFirstCnp",
											  "the fraction of rate on first CNP",
											  DoubleValue(1.0),
											  MakeDoubleAccessor(&RdmaHw::m_rateOnFirstCNP),
											  MakeDoubleChecker<double>())
								.AddAttribute("ClampTargetRate",
											  "Clamp target rate.",
											  BooleanValue(false),
											  MakeBooleanAccessor(&RdmaHw::m_EcnClampTgtRate),
											  MakeBooleanChecker())
								.AddAttribute("RPTimer",
											  "The rate increase timer at RP in microseconds",
											  DoubleValue(1500.0),
											  MakeDoubleAccessor(&RdmaHw::m_rpgTimeReset),
											  MakeDoubleChecker<double>())
								.AddAttribute("RateDecreaseInterval",
											  "The interval of rate decrease check",
											  DoubleValue(4.0),
											  MakeDoubleAccessor(&RdmaHw::m_rateDecreaseInterval),
											  MakeDoubleChecker<double>())
								.AddAttribute("FastRecoveryTimes",
											  "The rate increase timer at RP",
											  UintegerValue(5),
											  MakeUintegerAccessor(&RdmaHw::m_rpgThreshold),
											  MakeUintegerChecker<uint32_t>())
								.AddAttribute("AlphaResumInterval",
											  "The interval of resuming alpha",
											  DoubleValue(55.0),
											  MakeDoubleAccessor(&RdmaHw::m_alpha_resume_interval),
											  MakeDoubleChecker<double>())
								.AddAttribute("RateAI",
											  "Rate increment unit in AI period",
											  DataRateValue(DataRate("5Mb/s")),
											  MakeDataRateAccessor(&RdmaHw::m_rai),
											  MakeDataRateChecker())
								.AddAttribute("RateHAI",
											  "Rate increment unit in hyperactive AI period",
											  DataRateValue(DataRate("50Mb/s")),
											  MakeDataRateAccessor(&RdmaHw::m_rhai),
											  MakeDataRateChecker())
								.AddAttribute("VarWin",
											  "Use variable window size or not",
											  BooleanValue(false),
											  MakeBooleanAccessor(&RdmaHw::m_var_win),
											  MakeBooleanChecker())
								.AddAttribute("FastReact",
											  "Fast React to congestion feedback",
											  BooleanValue(true),
											  MakeBooleanAccessor(&RdmaHw::m_fast_react),
											  MakeBooleanChecker())
								.AddAttribute("MiThresh",
											  "Threshold of number of consecutive AI before MI",
											  UintegerValue(5),
											  MakeUintegerAccessor(&RdmaHw::m_miThresh),
											  MakeUintegerChecker<uint32_t>())
								.AddAttribute("TargetUtil",
											  "The Target Utilization of the bottleneck bandwidth, by default 95%",
											  DoubleValue(0.95),
											  MakeDoubleAccessor(&RdmaHw::m_targetUtil),
											  MakeDoubleChecker<double>())
								.AddAttribute("UtilHigh",
											  "The upper bound of Target Utilization of the bottleneck bandwidth, by default 98%",
											  DoubleValue(0.98),
											  MakeDoubleAccessor(&RdmaHw::m_utilHigh),
											  MakeDoubleChecker<double>())
								.AddAttribute("RateBound",
											  "Bound packet sending by rate, for test only",
											  BooleanValue(true),
											  MakeBooleanAccessor(&RdmaHw::m_rateBound),
											  MakeBooleanChecker())
								.AddAttribute("MultiRate",
											  "Maintain multiple rates in HPCC",
											  BooleanValue(true),
											  MakeBooleanAccessor(&RdmaHw::m_multipleRate),
											  MakeBooleanChecker())
								.AddAttribute("SampleFeedback",
											  "Whether sample feedback or not",
											  BooleanValue(false),
											  MakeBooleanAccessor(&RdmaHw::m_sampleFeedback),
											  MakeBooleanChecker())
								.AddAttribute("TimelyAlpha",
											  "Alpha of TIMELY",
											  DoubleValue(0.875),
											  MakeDoubleAccessor(&RdmaHw::m_tmly_alpha),
											  MakeDoubleChecker<double>())
								.AddAttribute("TimelyBeta",
											  "Beta of TIMELY",
											  DoubleValue(0.8),
											  MakeDoubleAccessor(&RdmaHw::m_tmly_beta),
											  MakeDoubleChecker<double>())
								.AddAttribute("TimelyTLow",
											  "TLow of TIMELY (ns)",
											  UintegerValue(50000),
											  MakeUintegerAccessor(&RdmaHw::m_tmly_TLow),
											  MakeUintegerChecker<uint64_t>())
								.AddAttribute("TimelyTHigh",
											  "THigh of TIMELY (ns)",
											  UintegerValue(500000),
											  MakeUintegerAccessor(&RdmaHw::m_tmly_THigh),
											  MakeUintegerChecker<uint64_t>())
								.AddAttribute("TimelyMinRtt",
											  "MinRtt of TIMELY (ns)",
											  UintegerValue(20000),
											  MakeUintegerAccessor(&RdmaHw::m_tmly_minRtt),
											  MakeUintegerChecker<uint64_t>())
								.AddAttribute("DctcpRateAI",
											  "DCTCP's Rate increment unit in AI period",
											  DataRateValue(DataRate("1000Mb/s")),
											  MakeDataRateAccessor(&RdmaHw::m_dctcp_rai),
											  MakeDataRateChecker())
								.AddAttribute("PintSmplThresh",
											  "PINT's sampling threshold in rand()%65536",
											  UintegerValue(65536),
											  MakeUintegerAccessor(&RdmaHw::pint_smpl_thresh),
											  MakeUintegerChecker<uint32_t>())
								.AddAttribute("L2Timeout", "Sender's timer of waiting for the ack",
											  TimeValue(MilliSeconds(4)), MakeTimeAccessor(&RdmaHw::m_waitAckTimeout),
											  MakeTimeChecker())
								.AddAttribute("enableStartFlowRateChange", "", BooleanValue(false),
											  MakeBooleanAccessor(&RdmaHw::m_initFlowRateChange),
											  MakeBooleanChecker())
								.AddAttribute("E2ELb", "E2E Load balancing algorithm.",
											  EnumValue(LB_Solution::LB_NONE),
											  MakeEnumAccessor(&RdmaHw::m_lbSolution),
											  MakeEnumChecker(LB_Solution::LB_E2ELAPS, "e2elaps",
															  LB_Solution::LB_PLB, "plb"));
		return tid;
	}

	RdmaHw::RdmaHw()
	{
		m_E2ErdmaSmartFlowRouting = CreateObject<RdmaSmartFlowRouting>();
		m_outstanding_data.clear();
	}

	void RdmaHw::SetNode(Ptr<Node> node)
	{
		m_node = node;
	}
	void RdmaHw::Setup(QpCompleteCallback cb)
	{
		if (m_lbSolution == LB_Solution::LB_E2ELAPS)
		{
			NS_ASSERT_MSG(m_E2ErdmaSmartFlowRouting != NULL, "E2ErdmaSmartFlowRouting is NULL");
			m_E2ErdmaSmartFlowRouting->SetNode(m_node);
		}
		if (ENABLE_CCMODE_TEST)
		{
			std::map<std::string, std::map<uint64_t, RecordCcmodeOutEntry>> ccmodeflowOutInfo;
			ccmodeOutInfo[m_node->GetId()] = ccmodeflowOutInfo;
		}

		for (uint32_t i = 0; i < m_nic.size(); i++)
		{
			Ptr<QbbNetDevice> dev = m_nic[i].dev;
			if (dev == NULL)
				continue;
			// share data with NIC
			dev->m_rdmaEQ->m_qpGrp = m_nic[i].qpGrp;
			// setup callback
			dev->m_rdmaReceiveCb = MakeCallback(&RdmaHw::Receive, this);
			dev->m_rdmaLinkDownCb = MakeCallback(&RdmaHw::SetLinkDown, this);
			dev->m_rdmaPktSent = MakeCallback(&RdmaHw::PktSent, this);
			dev->m_rdmaLbPktSent = MakeCallback(&RdmaHw::LBPktSent, this);
			//===============================新增回调==========================================================
			dev->m_updateRateForLapsBasedOnBDPCb = MakeCallback(&RdmaHw::UpdateRateForLapsBasedOnBDP, this);
			//===============================新增回调==========================================================
			dev->m_rdmaOutStanding_cb = MakeCallback(&RdmaHw::AppendOutStandingDataPerPath, this);
			dev->m_rtoSetCb = MakeCallback(&RdmaHw::SetTimeoutForLaps, this);
			dev->m_rdmaGetE2ELapsLBouting = MakeCallback(&RdmaHw::GetE2ELapsLBouting, this);
			dev->m_plbTableDataCb = MakeCallback(&RdmaHw::GetFlowIdRehashNum, this);
			dev->m_lbSolution = m_lbSolution;
			// config NIC
			dev->m_rdmaEQ->m_rdmaGetNxtPkt = MakeCallback(&RdmaHw::GetNxtPacket, this);
		}
		// setup qp complete callback
		m_qpCompleteCallback = cb;
	}

	uint32_t RdmaHw::GetNicIdxOfQp(Ptr<RdmaQueuePair> qp)
	{
		/*if (Irn::mode == Irn::Mode::NACK)
		{
			return 1;
		}*/
		auto &v = m_rtTable[qp->dip.Get()];
		if (v.size() > 0)
		{
			// std::cout << " TX qp NicIdx: " << v[qp->GetHash() % v.size()] << std::endl;
			return v[qp->GetHash() % v.size()];
		}
		else
		{
			std::cout << "Node ** " << m_node->GetId() << " ** has " << m_rtTable.size() << " entries and " << m_nic.size() << " nics" << std::endl;
			for (auto &i : m_rtTable)
			{
				std::cout << "m_rtTable[" << i.first << "] = [";
				for (auto &j : i.second)
				{
					std::cout << j << " ";
				}
				std::cout << "]" << std::endl;
			}

			NS_ASSERT_MSG(false, "We assume at least one NIC is alive");
			exit(1);
		}
	}
	uint64_t RdmaHw::GetQpKey(uint32_t dip, uint16_t sport, uint16_t dport, uint16_t pg)
	{
		// return ((uint64_t)dip << 32) | ((uint64_t)sport << 16) | (uint64_t)dport | (uint64_t)pg;
		return ((uint64_t)dip << 32) | ((uint64_t)sport << 16) | (uint64_t)dport;

	}
	Ptr<RdmaQueuePair> RdmaHw::GetQp(uint64_t key)
	{
		auto it = m_qpMap.find(key);
		if (it != m_qpMap.end())
			return it->second;
		return NULL;
	}
	void RecordQPrate(Ptr<RdmaQueuePair> qp, Ptr<RdmaQueuePair> qp1, DataRate oldRate, DataRate newRate)
	{
		Time now = Simulator::Now();
		uint32_t curRateInMBs = newRate.GetBitRate() / 1000000 / 8;

		std::string flowId = qp1->GetStringHashValueFromQp();
		auto it = RdmaHw::m_qpRatechange.find(flowId);
		if (it == RdmaHw::m_qpRatechange.end())
		{
			RdmaHw::m_qpRatechange[flowId].push_back(RecordFlowRateEntry_t(curRateInMBs, now.GetNanoSeconds(), 0));
		}
		else
		{
			std::vector<RecordFlowRateEntry_t> &rateEntry = it->second;
			uint64_t prev_rate_in_MBs = rateEntry.back().rateInMbps;
			uint64_t prev_time_in_ns = rateEntry.back().startTimeInNs;
			rateEntry.back().durationInNs = now.GetNanoSeconds() - prev_time_in_ns;
			if (curRateInMBs != prev_rate_in_MBs)
			{
				rateEntry.push_back(RecordFlowRateEntry_t(curRateInMBs, now.GetNanoSeconds(), 0));
			}
		}

		return;
	}
	void RdmaHw::AddQueuePair(uint64_t size, uint16_t pg, Ipv4Address sip, Ipv4Address dip, uint16_t sport, uint16_t dport, uint32_t win, uint64_t baseRtt, int32_t flowId, Callback<void> notifyAppFinish)
	{
		// create qp
		Ptr<RdmaQueuePair> qp = CreateObject<RdmaQueuePair>(pg, sip, dip, sport, dport);
		qp->SetSize(size);
		qp->SetWin(win);
		qp->SetBaseRtt(baseRtt);
		qp->SetVarWin(m_var_win);
		qp->SetFlowId(flowId);
		qp->SetAppNotifyCallback(notifyAppFinish);
		qp->SetTimeout(m_waitAckTimeout);
		qp->TraceConnectWithoutContext("RateChange", MakeBoundCallback(&RecordQPrate, qp));
		// qp->TraceConnectWithoutContext("RateChange", MakeMemberCallback(this, &RdmaHw::RecordQPrate))
		//  PLB init
		if (m_lbSolution == LB_Solution::LB_PLB)
		{
			std::string flowIdHash = qp->GetStringHashValueFromQp();
			m_plbtable[flowIdHash].congested_rounds = 0;
		}
		if (Irn::mode == Irn::Mode::IRN_OPT)
		{
			qp->m_irn.m_bdp = win;
		}

		// add qp
		uint32_t nic_idx = GetNicIdxOfQp(qp);
		m_nic[nic_idx].qpGrp->AddQp(qp);

		uint64_t key = GetQpKey(dip.Get(), sport, dport, pg);
		m_qpMap[key] = qp;

		// set init variables
		DataRate m_bps = m_nic[nic_idx].dev->GetDataRate();
		qp->TraceRate(m_bps);
		if (false)
		{

			qp->m_rate = m_bps / flowPerHost;
			// std::cout << "FLOW InitRATE:" << qp->m_rate.GetBitRate() << std::endl;
		}
		else
		{
			qp->m_rate = m_bps;
			// std::cout << "InitRATE:" << qp->m_rate.GetBitRate() << std::endl;
		}

		qp->m_max_rate = m_bps;
		if (m_cc_mode == CongestionControlMode::DCQCN_MLX)
		{
			qp->mlx.m_targetRate = m_bps;
		}
		else if (m_cc_mode == CongestionControlMode::HPCC)
		{
			qp->hp.m_curRate = m_bps;
			if (m_multipleRate)
			{
				for (uint32_t i = 0; i < IntHeader::maxHop; i++)
					qp->hp.hopState[i].Rc = m_bps;
			}
		}
		else if (m_cc_mode == CongestionControlMode::TIMELY)
		{
			qp->tmly.m_curRate = m_bps;
		}
		else if (m_cc_mode == CongestionControlMode::HPCC_PINT)
		{
			qp->hpccPint.m_curRate = m_bps;
		}
		else if (m_cc_mode == CongestionControlMode::CC_LAPS)
		{
			qp->laps.m_curRate = m_bps;
			qp->laps.m_tgtRate = m_bps;
			qp->laps.m_incStage = 0;
			qp->laps.m_nxtRateDecTimeInNs = Simulator::Now().GetNanoSeconds();
			qp->laps.m_nxtRateIncTimeInNs = Simulator::Now().GetNanoSeconds();
		}
		else if (m_cc_mode == CongestionControlMode::DCTCP)
		{
			// no need init
		}
		else
		{
			std::cout << "Unknown CC mode" << std::endl;
			exit(1);
		}
		qpFlowIndex++;
		// std::cout << "Time " << Simulator::Now().GetNanoSeconds() << " nodeID:" << m_node->GetId() << " FlowIndex " << qpFlowIndex << " nic_idx " << nic_idx << std::endl;
		//   Notify Nic
		m_nic[nic_idx].dev->NewQp(qp);
	}

	Time RdmaHw::getNxtAvailTimeForQp(uint32_t flowId)
	{
		Time t = Simulator::GetMaximumSimulationTime();
		auto it = flowToPstEntry.find(flowId);
		if (it == flowToPstEntry.end())
		{
			std::cerr << "flowId " << flowId << " is not in flowToPstEntry" << std::endl;
			exit(1);
			return t;
		}
		else
		{
			pstEntryData *pstEntry = it->second;
			std::vector<PathData *> pitEntries = m_E2ErdmaSmartFlowRouting->batch_lookup_PIT(pstEntry->paths);
			if (pitEntries.size() == 0)
			{
				std::cerr << "flowId " << flowId << " has no available path" << std::endl;
				exit(1);
				return t;
			}
			for (auto &pitEntry : pitEntries)
			{
				t = std::min(t, pitEntry->nextAvailableTime);
			}
		}
		t = std::max(t, Simulator::Now());
		return t;
	}

	bool RdmaHw::isPathAvailable(uint32_t flowId)
	{
		auto it = flowToPstEntry.find(flowId);
		if (it == flowToPstEntry.end())
		{
			std::cerr << "flowId " << flowId << " is not in flowToPstEntry" << std::endl;
			exit(1);
			return false;
		}
		else
		{
			pstEntryData *pstEntry = it->second;
			std::vector<PathData *> pitEntries = m_E2ErdmaSmartFlowRouting->batch_lookup_PIT(pstEntry->paths);
			if (pitEntries.size() == 0)
			{
				std::cerr << "flowId " << flowId << " has no available path" << std::endl;
				exit(1);
				return false;
			}
			Time t = Simulator::Now();
			for (auto &pitEntry : pitEntries)
			{
				if (pitEntry->nextAvailableTime <= t)
				{
					return true;
				}
			}
		}
		return false;
	}

	void RdmaHw::AddQueuePairForLaps(uint64_t size, uint16_t pg, Ipv4Address sip, Ipv4Address dip, uint16_t sport, uint16_t dport, uint32_t win, uint64_t baseRtt, int32_t flowId, Callback<void> notifyAppFinish)
	{
		NS_LOG_FUNCTION(this << "timeInNs " << Simulator::Now().GetNanoSeconds()
							 << " flowId " << flowId
							 << "size " << size
							 << " pg " << pg
							 << " sip " << sip
							 << " dip " << dip
							 << " sport " << sport
							 << " dport " << dport
							 << " winInBytes " << win
							 << " rttInNs " << baseRtt
							 << " enableVarWin " << m_var_win

		);
		// std::cout << "AddQueuePairForLaps sip " << ipv4Address2string(sip);
		// std::cout << " dip " << ipv4Address2string(dip) << std::endl;
		//  create qp
		NS_ASSERT_MSG(flowId >= 0, "Flow ID should be non-negative");
		NS_ASSERT_MSG(m_cc_mode == CongestionControlMode::CC_LAPS, "Called only when LAPS is enabled");
		NS_ASSERT_MSG(Irn::mode == Irn::Mode::IRN_OPT || Irn::mode == Irn::Mode::NACK, "Called only when LAPS is enabled");
     

		Ptr<RdmaQueuePair> qp = CreateObject<RdmaQueuePair>(pg, sip, dip, sport, dport);
		qp->SetSize(size);
		qp->SetVarWin(m_var_win);
		qp->SetFlowId(flowId);
		qp->SetAppNotifyCallback(notifyAppFinish);
		qp->m_node_id = m_node->GetId();
		qp->m_rtoSetCb = MakeCallback(&RdmaHw::SetTimeoutForLaps, this);
		qp->m_irn.m_bdp = win;
		qp->m_irn.m_sack.m_outstanding_data.clear();
		qp->m_irn.m_sack.m_lossy_data.clear();
		m_minRate = DataRate("1000Mb/s");

		qp->TraceConnectWithoutContext("RateChange", MakeBoundCallback(&RecordQPrate, qp));
		// add qp
		uint32_t nic_idx = GetNicIdxOfQp(qp);
		m_nic[nic_idx].qpGrp->AddQp(qp);
		uint64_t key = GetQpKey(dip.Get(), sport, dport, pg);
		NS_ASSERT_MSG(m_qpMap.find(key) == m_qpMap.end(), "Flow cannot be initialized twice");
		if (m_qpMap.find(key) != m_qpMap.end())
		{
			std::cout << "Flow cannot be initialized twice" << std::endl;
			exit(1);
		}

		if (m_flowId2Qp.find(flowId) != m_flowId2Qp.end())
		{
			std::cout << " xxxxxFlow cannot be initialized twice" << std::endl;
			exit(1);
		}

		m_qpMap[key] = qp;
		m_flowId2Qp[flowId] = qp;
		// set init variables
		DataRate m_bps = m_nic[nic_idx].dev->GetDataRate();
		//std::cout<<"m_bps:"<<m_bps<<std::endl;
		qp->m_max_rate = m_bps;
		if (false)
		{

			qp->laps.m_curRate = m_bps / flowPerHost;
			qp->laps.m_tgtRate = m_bps / flowPerHost;
		}
		else
		{
			qp->laps.m_curRate = m_bps;
			qp->laps.m_tgtRate = m_bps;
		}

		qp->laps.m_incStage = 0;
		qp->laps.m_nxtRateDecTimeInNs = 0;
		qp->laps.m_nxtRateIncTimeInNs = 0;
		qp->m_cb_getRtoTimeForPath = MakeCallback(&RdmaHw::GetRtoTimeForPath, this);
		qp->m_cb_cancelRtoForPath = MakeCallback(&RdmaHw::CancelRtoForPath, this);
		qp->m_cb_isPathsValid = MakeCallback(&RdmaHw::isPathAvailable, this);
		qp->m_cb_getNxtAvailTimeForQp = MakeCallback(&RdmaHw::getNxtAvailTimeForQp, this);
		// PLB init
		if (m_lbSolution == LB_Solution::LB_PLB)
		{
			std::string flowIdHash = qp->GetStringHashValueFromQp();
			m_plbtable[flowIdHash].congested_rounds = 0;
		}
		Ipv4Address srcServerAddr = Ipv4Address(sip);
		Ipv4Address dstServerAddr = Ipv4Address(dip);
		uint32_t srcHostId = m_E2ErdmaSmartFlowRouting->lookup_SMT(srcServerAddr)->hostId;
		uint32_t dstHostId = m_E2ErdmaSmartFlowRouting->lookup_SMT(dstServerAddr)->hostId;
		HostId2PathSeleKey pstKey(srcHostId, dstHostId);
				//===============================记录两点所有流最早开始时间和最晚结束时间=========

		if(RdmaSmartFlowRouting::record_path_e2e_all_flow_dur.find(pstKey) == RdmaSmartFlowRouting::record_path_e2e_all_flow_dur.end()){
			RdmaSmartFlowRouting::record_path_e2e_all_flow_dur[pstKey].starttime = Simulator::Now().GetNanoSeconds();
		
	}
		
        
		//===================================================================
		pstEntryData *pstEntry = m_E2ErdmaSmartFlowRouting->lookup_PST(pstKey);
		flowToPstEntry[flowId] = pstEntry;
		std::vector<PathData *> pitEntries = m_E2ErdmaSmartFlowRouting->batch_lookup_PIT(pstEntry->paths);
		NS_ASSERT_MSG(pitEntries.size() > 0, "The pitEntries is empty");
		std::sort(pitEntries.begin(), pitEntries.end(), [](const PathData *lhs, const PathData *rhs)
				  {
					  return lhs->theoreticalSmallestLatencyInNs > rhs->theoreticalSmallestLatencyInNs; // 降序排列
				  });



	 //=================================================================*0.85看看·========================
		qp->laps.m_tgtDelayInNs = (pitEntries[0]->theoreticalSmallestLatencyInNs);
       

		qp->SetBaseRtt(qp->laps.m_tgtDelayInNs * 2);

		uint64_t winInByte = uint64_t(1.0 * qp->laps.m_tgtDelayInNs  * m_bps.GetBitRate() / 8 / 1000000000lu);
		qp->SetWin(winInByte);
       //std::cout<<"winInByte"<<winInByte<<",qp->laps.m_tgtDelayInNs"<<qp->laps.m_tgtDelayInNs<<std::endl;
		// std::cout << "Time " << Simulator::Now().GetNanoSeconds() << " Flow " << flowId << " maxRateInGbps " << qp->m_max_rate.GetBitRate() / 1000000000lu << " target delay in us " << 1.0*qp->laps.m_tgtDelayInNs/1000 << " winInByte " << qp->m_win << " baseRtt " << qp->m_baseRtt << std::endl;
		if(winInByte > 429490000){
			std::cout << "Flow " << flowId << " maxRateInGbps " << qp->m_max_rate.GetBitRate() / 1000000000lu << " target delay " << qp->laps.m_tgtDelayInNs << " winInByte " << qp->m_win << std::endl;
		}
		m_nic[nic_idx].dev->NewQp(qp);
	}

	void RdmaHw::DeleteQueuePair(Ptr<RdmaQueuePair> qp)
	{
		// remove qp from the m_qpMap
		uint64_t key = GetQpKey(qp->dip.Get(), qp->sport, qp->dport, qp->m_pg);
	if(RdmaSmartFlowRouting::enable_laps_plus==0){
		m_finishedQpMap[key] = 0;
	}
		 
		m_qpMap.erase(key);
	}

	uint64_t RdmaHw::GetRxQpKey(uint32_t dip, uint16_t dport, uint16_t sport, uint16_t pg)
	{																									 // Receiver perspective
		// return ((uint64_t)dip << 32) | ((uint64_t)pg << 16) | ((uint64_t)sport << 16) | (uint64_t)dport; // srcIP, srcPort
		return ((uint64_t)dip << 32) | ((uint64_t)sport << 16) | (uint64_t)dport; // srcIP, srcPort

	}

	Ptr<RdmaRxQueuePair> RdmaHw::GetRxQp(uint32_t sip, uint32_t dip, uint16_t sport, uint16_t dport, uint16_t pg, bool create)
	{
		uint64_t key = GetRxQpKey(dip, dport, sport, pg);
		auto it_active_qp = m_rxQpMap.find(key);
		if (it_active_qp != m_rxQpMap.end())
			return it_active_qp->second;
		if (create)
		{
			// create new rx qp
			NS_LOG_INFO("create new rx qp");
			Ptr<RdmaRxQueuePair> q = CreateObject<RdmaRxQueuePair>();
			// init the qp
			q->sip = sip;
			q->dip = dip;
			q->sport = sport;
			q->dport = dport;
			q->m_ecn_source.qIndex = pg;
			q->m_flow_id = -1;
			q->m_sack.socketId = -1;
			// store in map
			m_rxQpMap[key] = q;
			return q;
		}
		return NULL;
	}

	Ptr<RdmaRxQueuePair> RdmaHw::InitRxQp(uint32_t sip, uint32_t dip, uint16_t sport, uint16_t dport, uint16_t pg, int32_t flowId)
	{
		NS_ASSERT_MSG(flowId >= 0, "Flow ID should be non-negative");
		uint64_t key = GetRxQpKey(dip, dport, sport, pg);
		NS_ASSERT_MSG(m_rxQpMap.find(key) == m_rxQpMap.end(), "Flow cannot be initialized twice");
		if(m_rxQpMap.find(key) != m_rxQpMap.end())
		std::cout<<"Flow cannot be initialized twice"<<std::endl;
		// if()
		// std::cout << 
		NS_ASSERT_MSG(m_finishedQpMap.find(key) == m_finishedQpMap.end(), "Flow cannot be initialized after finished");

		Ptr<RdmaRxQueuePair> q = CreateObject<RdmaRxQueuePair>();
		q->sip = sip;
		q->dip = dip;
		q->sport = sport;
		q->dport = dport;
		q->m_ecn_source.qIndex = pg;
		q->m_flow_id = flowId;
		q->m_sack.socketId = flowId;
		m_rxQpMap[key] = q;
		return q;
	}

	uint32_t RdmaHw::GetNicIdxOfRxQp(Ptr<RdmaRxQueuePair> q)
	{
		/*if (m_rtTable.size() == 0 && m_nic.size() == 1)
		{
			return 0;
		}*/

		auto &v = m_rtTable[q->dip];
		if (v.size() > 0)
		{
			// std::cout << " Receive  qp NicIdx: " << v[q->GetHash() % v.size()] << std::endl;
			return v[q->GetHash() % v.size()];
		}
		else
		{
			NS_ASSERT_MSG(false, "We assume at least one NIC is alive");
			exit(1);
		}
	}
	void RdmaHw::DeleteRxQp(uint32_t dip, uint16_t dport, uint16_t sport, uint16_t pg)
	{
		uint64_t key = GetRxQpKey(dip, dport, sport, pg);
		NS_ASSERT_MSG(m_finishedQpMap.find(key) == m_finishedQpMap.end(), "Flow cannot be finished twice"); // should not be already existing
		m_finishedQpMap[key] = key;
		NS_ASSERT_MSG(m_rxQpMap.find(key) != m_rxQpMap.end(), "cannot find the Flow to finish"); // should not be already existing
		m_rxQpMap.erase(key);
	}
	/*bool RdmaHw::UpdateLostPacket(uint32_t sepNum, std::string flowId)
	{
		for (auto it = m_packetLost[m_node->GetId()].begin(); it != m_packetLost[m_node->GetId()].end(); it++)
		{
			if (it->second != sepNum)
			{
				NS_LOG_INFO("FLOW " << flowId << " Lost packet seqnum " << sepNum);

				Time now = Simulator::Now();
				std::map<uint64_t, RecordCcmodeOutEntry> m_saveEntry;
				if (ccmodeOutInfo[m_node->GetId()][flowId].find(now.GetMicroSeconds()) != ccmodeOutInfo[m_node->GetId()][flowId].end())
				{

					ccmodeOutInfo[m_node->GetId()][flowId][now.GetMicroSeconds()].currlossPacketSeq = sepNum;
				}
				else
				{
					m_saveEntry[now.GetMicroSeconds()].currlossPacketSeq = sepNum;
					ccmodeOutInfo[m_node->GetId()][flowId] = m_saveEntry;
				}

				// ccmodeOutInfo[m_node->GetId()][flowId] = std::paie(now.GetMicroSeconds(), saveEntry);
				m_packetLost[m_node->GetId()][flowId] = sepNum;
				return true;
			}
		}
		return false;
	}*/
	bool RdmaHw::LostPacketTest(uint32_t sepNum, std::string flowId)
	{
		// mtu*packetNum
		Time now = Simulator::Now();
		if (m_manualDropSeqMap1[sepNum])
		{
			// Drop
			Time now = Simulator::Now();
			// std::string flowId = std::to_string(ch.sip) + "#" + std::to_string(ch.dip) + "#" + std::to_string(ch.udp.sport);
			LostPacketEntry lostEntry;
			lostEntry.currlossPacketSeq = sepNum;
			lostEntry.lostNum += 1;
			m_lossPacket[m_node->GetId()][flowId][now.GetMicroSeconds()] = lostEntry;
			NS_LOG_INFO("FLOW " << flowId << " Lost packet seqnum " << sepNum);
			m_manualDropSeqMap1[sepNum] = false;
			return true;
		}

		/*if (sepNum == 200 * 1000)
		{
			if (m_packetLost[m_node->GetId()].find(flowId) == m_packetLost[m_node->GetId()].end())
			{

				NS_LOG_INFO("FLOW " << flowId << " Lost packet seqnum " << sepNum);
				RecordCcmodeOutEntry saveEntry;
				saveEntry.currlossPacketSeq = sepNum;
				Time now = Simulator::Now();
				ccmodeOutInfo[m_node->GetId()][flowId][now.GetMicroSeconds()].currlossPacketSeq = sepNum;
				m_packetLost[m_node->GetId()][flowId] = sepNum;
				return 1;
			}
			else
			{
				return UpdateLostPacket(sepNum, flowId);
			}
		}
		if (sepNum == 300 * 1000)
		{
			return UpdateLostPacket(sepNum, flowId);
		}
		if (sepNum == 301 * 1000)
		{
			return UpdateLostPacket(sepNum, flowId);
		}
		if (sepNum == 10000 * 1000)
		{
			return UpdateLostPacket(sepNum, flowId);
		}*/
		return false;
	}
	int RdmaHw::ReceiveUdp(Ptr<Packet> p, CustomHeader &ch)
	{
		NS_LOG_FUNCTION(this);
		uint8_t ecnbits = ch.GetIpv4EcnBits();
		NS_LOG_INFO("ecnbits : " << ecnbits);
		uint32_t payload_size = p->GetSize() - ch.GetSerializedSize();
		NS_LOG_INFO("payload_size : " << payload_size);
		Time now = Simulator::Now();

		if (ENABLE_CCMODE_TEST)
		{

			std::string flowId = ipv4Address2string(Ipv4Address(ch.sip)) + "#" + ipv4Address2string(Ipv4Address(ch.dip)) + "#" + std::to_string(ch.udp.sport);
			if (ccmodeOutInfo[m_node->GetId()].find(flowId) == ccmodeOutInfo[m_node->GetId()].end())
			{
				std::map<uint64_t, RecordCcmodeOutEntry> m_flowSaveEntry;
				ccmodeOutInfo[m_node->GetId()][flowId] = m_flowSaveEntry;
			}
		}
		if (ENABLE_LOSS_PACKET_TEST)
		{

			std::string flowId = ipv4Address2string(Ipv4Address(ch.sip)) + "#" + ipv4Address2string(Ipv4Address(ch.dip)) + "#" + std::to_string(ch.udp.sport);
			if (LostPacketTest(ch.udp.seq, flowId))
			{
				return 1;
			}
		}
		int32_t flowId = -1;
		FlowIDNUMTag fit;
		if (p->PeekPacketTag(fit))
		{
			flowId = fit.GetId();
		}
		else
		{
			std::cerr << "Rx cannot find the flowId Tag on Packet" << std::endl;
			exit(1);
		}
		// RecordPacketHop;
		PacketHopTag packetHopTag;
		p->PeekPacketTag(packetHopTag);
		uint32_t hopNum = packetHopTag.GetHopId();
		if (RecordPacketHop.find(hopNum + 1) != RecordPacketHop.end())
		{
			RecordPacketHop[hopNum + 1]++;
		}
		else
		{
			RecordPacketHop[hopNum + 1] = 1;
		}

		auto it = m_recordQpExec.find(flowId);
		if (it == m_recordQpExec.end())
		{
			std::cerr << "Flow " << flowId << " is not in m_recordQpExec" << std::endl;
			exit(1);
		}
		it->second.receSizeInbyte += p->GetSize() - ch.GetSerializedSize();;
		it->second.recePacketNum++;

		Ptr<RdmaRxQueuePair> rxQp = GetRxQp(ch.dip, ch.sip, ch.udp.dport, ch.udp.sport, ch.udp.pg, false);
		if (rxQp == NULL)
		{
			if (checkRxQpFinishedOnDstHost(ch))
			{
				return 1;
			}
			rxQp = InitRxQp(ch.dip, ch.sip, ch.udp.dport, ch.udp.sport, ch.udp.pg, -1);
			rxQp->m_sack.socketId = flowId;
		}

		// std::string flowId = rxQp->GetStringHashValueFromQp();
		// std::string flowId = ipv4Address2string(Ipv4Address(ch.sip)) + "#" + ipv4Address2string(Ipv4Address(ch.dip)) + "#" + std::to_string(ch.udp.sport);
		
		if (ecnbits != 0)
		{
			rxQp->m_ecn_source.ecnbits |= ecnbits;
			rxQp->m_ecn_source.qfb++;
		}
		rxQp->m_ecn_source.total++;
		rxQp->m_milestone_rx = m_ack_interval;

		// Ipv4SmartFlowProbeTag probeTag;
		// bool findProPacket = p->PeekPacketTag(probeTag);
		// if (rxQp->m_flow_id < 0)
		// {
		// 	FlowIDNUMTag fit;
		// 	if (p->PeekPacketTag(fit))
		// 	{
		// 		rxQp->m_flow_id = fit.GetId();
		// 		rxQp->m_sack.socketId = fit.GetId();
		// 	}
		// 	else if (findProPacket)
		// 	{
		// 		NS_LOG_INFO("Node " << m_node->GetId() << " HW Receive ProbePacket " << "Drop");
		// 		return 1;
		// 	}
		// 	else
		// 	{
		// 		std::cout << "Rx cannot find the flow id" << std::endl;
		// 		exit(1);
		// 	}
		// }

		bool isEnableCnp = false;
		int x = ReceiverCheckSeq(ch.udp.seq, rxQp, payload_size, isEnableCnp);
		if (ENABLE_CCMODE_TEST)
		{

			std::string flowId = ipv4Address2string(Ipv4Address(ch.dip)) + "#" + ipv4Address2string(Ipv4Address(ch.sip)) + "#" + std::to_string(ch.udp.sport);
			if (ccmodeOutInfo[m_node->GetId()].find(flowId) == ccmodeOutInfo[m_node->GetId()].end())
			{
				std::map<uint64_t, RecordCcmodeOutEntry> m_flowSaveEntry;
				ccmodeOutInfo[m_node->GetId()][flowId] = m_flowSaveEntry;
			}

			RecordCcmodeOutEntry m_saveRecordEntry;
			if (ccmodeOutInfo[m_node->GetId()][flowId].find(now.GetMicroSeconds()) != ccmodeOutInfo[m_node->GetId()][flowId].end())
			{
				m_saveRecordEntry = ccmodeOutInfo[m_node->GetId()][flowId][now.GetMicroSeconds()];
			}
			if (ecnbits)
			{
				m_saveRecordEntry.m_cnt_cnpByEcn = m_cnt_cnpByEcn + 1;
			}
			if (isEnableCnp)
			{
				m_saveRecordEntry.m_cnt_Cnp = m_cnt_cnpByOoo + 1;
			}
			ccmodeOutInfo[m_node->GetId()][flowId][now.GetMicroSeconds()] = m_saveRecordEntry;
		}

		if (x == ReceiverSequenceCheckResult::ACTION_ACK ||
			x == ReceiverSequenceCheckResult::ACTION_NACK ||
			x == ReceiverSequenceCheckResult::ACTION_NACK_PSEUDO)
		{ // generate ACK or NACK
			qbbHeader seqh;
			seqh.SetSeq(rxQp->ReceiverNextExpectedSeq);
			seqh.SetPG(ch.udp.pg);
			seqh.SetSport(ch.udp.dport);
			seqh.SetDport(ch.udp.sport);
			seqh.SetIntHeader(ch.udp.ih);

			if (ecnbits || isEnableCnp){
            m_cnt_Cnp++;
            if (ecnbits) m_cnt_cnpByEcn++;
            if (isEnableCnp) m_cnt_cnpByOoo++;
            seqh.SetCnp();
			}


			Ptr<Packet> newp = Create<Packet>(std::max(60 - 14 - 20 - (int)seqh.GetSerializedSize(), 0));
			newp->AddHeader(seqh);

			Ipv4Header head; // Prepare IPv4 header
			head.SetDestination(Ipv4Address(ch.sip));
			head.SetSource(Ipv4Address(ch.dip));
			head.SetProtocol(x == ReceiverSequenceCheckResult::ACTION_ACK ? L3ProtType::ACK : L3ProtType::NACK); // ack=0xFC nack=0xFD
			head.SetTtl(64);
			head.SetPayloadSize(newp->GetSize());
			head.SetIdentification(rxQp->m_ipid++);

			newp->AddHeader(head);
			AddHeader(newp, 0x800); // Attach PPP header
			// send
			uint32_t nic_idx = GetNicIdxOfRxQp(rxQp);
			m_nic[nic_idx].dev->RdmaEnqueueHighPrioQ(newp);
			m_nic[nic_idx].dev->TriggerTransmit();
		}
		return 0;
	}

bool RdmaHw::checkQpFinishedOnDstHost(const CustomHeader &ch)
{
	uint64_t key = GetQpKey(ch.sip, ch.udp.dport, ch.udp.sport, ch.udp.pg);
	auto it_complete_qp = m_finishedQpMap.find(key);
	if (it_complete_qp != m_finishedQpMap.end())
	{
		it_complete_qp->second += 1;
		return true;
	}
	else
	{
		return false;
	}
}

bool RdmaHw::checkRxQpFinishedOnDstHost(const CustomHeader &ch)
{
	NS_LOG_FUNCTION(this << "Node=" << m_node->GetId());
	// NS_ASSERT_MSG(Irn::mode == Irn::Mode::IRN_OPT || Irn::mode == Irn::Mode::NACK, "IRN_OPT or NACK should be enabled");
	// NS_ASSERT_MSG(ch.l3Prot == L3ProtType::UDP, "Not UDP packet");
	uint64_t key = GetRxQpKey(ch.sip, ch.udp.sport, ch.udp.dport, ch.udp.pg);
	auto it_complete_qp = m_finishedQpMap.find(key);
	if (it_complete_qp != m_finishedQpMap.end())
	{
		// it_complete_qp->second += 1;
		return true;
	}
	else
	{
		return false;
	}
}

void RdmaHw::AppendOutStandingDataPerPath(uint32_t pathId, OutStandingDataEntry  e)
{
    RdmaSmartFlowRouting::TraceRecovery("append",m_node->GetId(),pathId,e.flow_id,e.seq,e.size,0,0,0,0);
	NS_LOG_FUNCTION(this << "Node=" << m_node->GetId());
    std::list<OutStandingDataEntry> data;
	data.clear();
	//m_outstanding_data[pathId].push_back(e);
	//std::map<uint32_t, std::list<OutStandingDataEntry>> m_outstanding_data;
	// if (e.flow_id==3816){
	// 	std::cout<<"***************seq:"<<e.seq<<" e.byte:"<<e.size<<std::endl;
	// }
	if (m_outstanding_data.find(pathId)!=m_outstanding_data.end()){
		m_outstanding_data[pathId].push_back(e);
	}else{
     data.push_back(e);
	 m_outstanding_data[pathId]=data;
	}
	// if (e.flow_id == 421 && pathId == 26320)
	// {
	// 	std::cout << "pathId " << pathId;
	// 	for (auto it = m_outstanding_data[pathId].begin(); it != m_outstanding_data[pathId].end(); it++)
	// 	{
	// 		std::cout << " " << it->to_string();
	// 	}
	// }
    if(RdmaSmartFlowRouting::lcPathTimeout)m_outstanding_data[pathId].back().sentNs=Simulator::Now().GetNanoSeconds();
	SetTimeoutForLapsPerPath(pathId);
}



Ptr<Packet> RdmaHw::ConstructAckForUDP(ReceiverSequenceCheckResult state, const CustomHeader &ch, Ptr<RdmaRxQueuePair> rxQp, uint32_t udpPayloadSize)
{

	NS_LOG_FUNCTION(this << "Node=" << m_node->GetId());	
	NS_ASSERT_MSG(!(Irn::mode == Irn::Mode::IRN_OPT) || !(Irn::mode == Irn::Mode::IRN), "IRN and IRN_O shouldn't be enabled at the same time");
	NS_LOG_INFO("Node " << m_node->GetId() << " Reply ACK with " << "ackSeq=" << rxQp->ReceiverNextExpectedSeq << ", nackSeq=" << ch.udp.seq << ", nackLen=" << udpPayloadSize);
	qbbHeader seqh;
	Ipv4Header head; // Prepare IPv4 header

	if (Irn::mode == Irn::Mode::NACK)
	{
		seqh.SetIrnNack(ch.udp.seq);
		seqh.SetIrnNackSize(udpPayloadSize);
		head.SetProtocol(L3ProtType::NACK);
	}
	else if (Irn::mode == Irn::Mode::IRN_OPT)
	{
		head.SetProtocol(L3ProtType::NACK);
		if (state == ReceiverSequenceCheckResult::ACTION_NACK)
		{
			seqh.SetIrnNack(ch.udp.seq);
			seqh.SetIrnNackSize(udpPayloadSize);
		}
		else if (state == ReceiverSequenceCheckResult::ACTION_ACK || state == ReceiverSequenceCheckResult::ACTION_NACK_PSEUDO)
		{
			seqh.SetIrnNack((uint16_t) 0); 
			seqh.SetIrnNackSize(0);
		}
		else
		{
			return NULL;
		}
	}
	else if (Irn::mode == Irn::Mode::IRN)
	{
		if (state == ReceiverSequenceCheckResult::ACTION_NACK)
		{
			seqh.SetIrnNack(ch.udp.seq);
			seqh.SetIrnNackSize(udpPayloadSize);
			head.SetProtocol(L3ProtType::NACK); 
		}
		else if (state == ReceiverSequenceCheckResult::ACTION_ACK || state == ReceiverSequenceCheckResult::ACTION_NACK_PSEUDO)
		{
			seqh.SetIrnNack((uint16_t) 0); 
			seqh.SetIrnNackSize(0);
			head.SetProtocol(L3ProtType::ACK);
		}
		else
		{
			return NULL;
		}
	}
	else if (Irn::mode == Irn::Mode::GBN)
	{
			seqh.SetIrnNack((uint16_t) 0); 
			seqh.SetIrnNackSize(0);
			head.SetProtocol(L3ProtType::ACK); 
	}
	else
	{
		NS_ASSERT_MSG(false, "Invalid Irn::mode");
		return NULL;
	}
	

	Ptr<Packet> ackPkt = NULL;
	if (Irn::mode == Irn::Mode::IRN_OPT || Irn::mode == Irn::Mode::NACK)
	{
	 	ackPkt = Create<Packet>(0);
	}
	else
	{
		ackPkt = Create<Packet>(std::max(60 - 14 - 20 - (int)seqh.GetSerializedSize(), 0));
	}
	
	seqh.SetSeq(rxQp->ReceiverNextExpectedSeq);
	seqh.SetPG(ch.udp.pg);
	seqh.SetSport(ch.udp.dport);
	seqh.SetDport(ch.udp.sport);
	seqh.SetIntHeader(ch.udp.ih);

	head.SetDestination(Ipv4Address(ch.sip));
	head.SetSource(Ipv4Address(ch.dip));
	head.SetTtl(64);
	head.SetPayloadSize(ackPkt->GetSize());
	head.SetIdentification(rxQp->m_ipid++);

	ackPkt->AddHeader(seqh);
	ackPkt->AddHeader(head);
	AddHeader(ackPkt, 0x800); // Attach PPP header
	
	return ackPkt;

}


Ptr<Packet> RdmaHw::ConstructAckForProbe(const CustomHeader &ch)
{

	NS_LOG_FUNCTION(this << "Node=" << m_node->GetId());	
	NS_LOG_INFO("Node " << m_node->GetId() << " Reply Probe");
	qbbHeader seqh;
	Ipv4Header head; // Prepare IPv4 header

	if (Irn::mode == Irn::Mode::NACK)
	{
		seqh.SetIrnNack(ch.udp.seq);
		seqh.SetIrnNackSize(0);
		head.SetProtocol(L3ProtType::NACK);
	}
	// else if (Irn::mode == Irn::Mode::IRN_OPT)
	// {
	// 	head.SetProtocol(L3ProtType::NACK);
	// 	if (state == ReceiverSequenceCheckResult::ACTION_NACK)
	// 	{
	// 		seqh.SetIrnNack(ch.udp.seq);
	// 		seqh.SetIrnNackSize(udpPayloadSize);
	// 	}
	// 	else if (state == ReceiverSequenceCheckResult::ACTION_ACK || state == ReceiverSequenceCheckResult::ACTION_NACK_PSEUDO)
	// 	{
	// 		seqh.SetIrnNack((uint16_t) 0); 
	// 		seqh.SetIrnNackSize(0);
	// 	}
	// 	else
	// 	{
	// 		return NULL;
	// 	}
	// }
	// else if (Irn::mode == Irn::Mode::IRN)
	// {
	// 	if (state == ReceiverSequenceCheckResult::ACTION_NACK)
	// 	{
	// 		seqh.SetIrnNack(ch.udp.seq);
	// 		seqh.SetIrnNackSize(udpPayloadSize);
	// 		head.SetProtocol(L3ProtType::NACK); 
	// 	}
	// 	else if (state == ReceiverSequenceCheckResult::ACTION_ACK || state == ReceiverSequenceCheckResult::ACTION_NACK_PSEUDO)
	// 	{
	// 		seqh.SetIrnNack((uint16_t) 0); 
	// 		seqh.SetIrnNackSize(0);
	// 		head.SetProtocol(L3ProtType::ACK);
	// 	}
	// 	else
	// 	{
	// 		return NULL;
	// 	}
	// }
	// else if (Irn::mode == Irn::Mode::GBN)
	// {
	// 		seqh.SetIrnNack((uint16_t) 0); 
	// 		seqh.SetIrnNackSize(0);
	// 		head.SetProtocol(L3ProtType::ACK); 
	// }
	else
	{
		NS_ASSERT_MSG(false, "Invalid Irn::mode");
		return NULL;
	}
	

	Ptr<Packet> ackPkt = NULL;
	if (Irn::mode == Irn::Mode::IRN_OPT || Irn::mode == Irn::Mode::NACK)
	{
	 	ackPkt = Create<Packet>(0);
	}
	else
	{
		ackPkt = Create<Packet>(std::max(60 - 14 - 20 - (int)seqh.GetSerializedSize(), 0));
	}
	
	seqh.SetSeq(0);
	seqh.SetPG(ch.udp.pg);
	seqh.SetSport(ch.udp.dport);
	seqh.SetDport(ch.udp.sport);
	seqh.SetIntHeader(ch.udp.ih);

	head.SetDestination(Ipv4Address(ch.sip));
	head.SetSource(Ipv4Address(ch.dip));
	head.SetTtl(64);
	head.SetPayloadSize(ackPkt->GetSize());
	head.SetIdentification(0);

	ackPkt->AddHeader(seqh);
	ackPkt->AddHeader(head);
	AddHeader(ackPkt, 0x800); // Attach PPP header
	
	return ackPkt;

}



int RdmaHw::ReceiveUdpOnDstHostForLaps(Ptr<Packet> p, CustomHeader &ch)
{
	NS_LOG_FUNCTION (this << "Node=" << m_node->GetId());
	NS_ASSERT_MSG(Irn::mode == Irn::Mode::IRN_OPT || Irn::mode == Irn::Mode::NACK, "IRN_OPT or NACK should be enabled");
	NS_ASSERT_MSG(p && ch.l3Prot == L3ProtType::UDP, "Not UDP packet");
	Ipv4SmartFlowProbeTag probeTag;
	if(p->PeekPacketTag(probeTag)){
		return ReceiveProbeDataOnDstHostForLaps(p, ch);
	}

	uint32_t payload_size = p->GetSize() - ch.GetSerializedSize();

	NS_LOG_INFO("#Node: " << m_node->GetId() << ", Time:" << Simulator::Now() << ", Receive Packet with Type: DATA" << ", PktId:" << p->GetUid() << ", Size=" << payload_size);
	if (payload_size > 1000)
	{

		std::cout << "#Node: " << m_node->GetId() << ", Time:" << Simulator::Now() << ", Receive Packet with Type: DATA" << ", PktId:" << p->GetUid() << ", Size=" << payload_size << std::endl;
	}
	int32_t flowId = -1;
	FlowIDNUMTag fit;
	if (p->PeekPacketTag(fit))
	{
		flowId = fit.GetId();
	}
	else
	{
		std::cerr << "Rx cannot find the flowId Tag on Packet" << std::endl;
		exit(1);
	}
	static bool check = true;
	if ( flowId == 3681&&check){
		check=false;
		uint64_t key = GetRxQpKey(ch.sip, ch.udp.sport, ch.udp.dport, ch.udp.pg);
		RdmaSmartFlowRouting::key=key;
		std::cout << "Time: " 							<< Simulator::Now().GetNanoSeconds() 	<< ", ";
		std::cout << "Self_Key: " 						<< key 	<< ", ";
		std::cout << "Reason: " 					  	<< "Receive Data Packet"							<< ", ";
		std::cout << "[FlowID, sip, sport,dport] = [" 	<< flowId << ", " << ch.sip << ", " << ch.udp.sport << ", " << ch.udp.dport << "]\n";
		
	}else{
		if(RdmaSmartFlowRouting::key!=0
		&&GetRxQpKey(ch.sip, ch.udp.sport, ch.udp.dport, ch.udp.pg)==RdmaSmartFlowRouting::key
		&&flowId!=3681){
			std::cout<<"Time: " 							<< Simulator::Now().GetNanoSeconds() 	<< ", ";
			std::cout << "Self_Key: " 						<< RdmaSmartFlowRouting::key 	<< ", ";
			std::cout << "Reason: " 					  	<< "发现key值冲突的不同流"							<< ", ";
			std::cout << "[FlowID, sip, sport,dport] = [" 	<< flowId << ", " << ch.sip << ", " << ch.udp.sport << ", " << ch.udp.dport << "]\n";
		}
	}

	if (checkRxQpFinishedOnDstHost(ch))	{

		//if ( flowId == 3681)
		//{
			uint64_t key = GetRxQpKey(ch.sip, ch.udp.sport, ch.udp.dport, ch.udp.pg);
			uint64_t key_2 = m_finishedQpMap[key];
			Ipv4SmartFlowPathTag pathTag2;
			bool IsHavePathTag = p->PeekPacketTag(pathTag2);
			std::cout << "Time: " 							<< Simulator::Now().GetNanoSeconds() 	<< ", ";
			std::cout << "Self_Key: " 						<< key 	<< ", ";
			std::cout << "Other_Key: "	 					<< key_2 	<< ", ";
			std::cout << "PathID: "    						<< pathTag2.get_path_id() 								 	<< ", ";
			std::cout << "Reason: " 					  	<< "Reach Dst But The RxQp is wrongly finished"							<< ", ";
			std::cout << "[FlowID, PktSeq, Payload] = [" 	<< flowId << ", " << ch.udp.seq << ", " <<payload_size << "]\n";
		//}
		return 1; 
	}


	PacketHopTag packetHopTag;
	p->PeekPacketTag(packetHopTag);

	// if ( flowId == 177259)
	// {
	// 	Ipv4SmartFlowPathTag pathTag2;
	// 	bool IsHavePathTag = p->PeekPacketTag(pathTag2);
	// 	std::cout << "Time: " 							<< Simulator::Now().GetNanoSeconds() 	<< ", ";
	// 	std::cout << "PathID: "    						<< pathTag2.get_path_id() 								 	<< ", ";
	// 	std::cout << "Reason: " 					  	<< "Reach Dst"							<< ", ";
	// 	std::cout << "[FlowID, PktSeq, Payload] = [" 	<< flowId << ", " << ch.udp.seq << ", " <<payload_size << "]\n";
	// }

	uint32_t hopNum = packetHopTag.GetHopId();
	if (RecordPacketHop.find(hopNum + 1) != RecordPacketHop.end())
	{
		RecordPacketHop[hopNum + 1]++;
	}
	else
	{
		RecordPacketHop[hopNum + 1] = 1;
	}
	auto it = m_recordQpExec.find(flowId);
	if (it == m_recordQpExec.end())
	{
		std::cerr << "Flow " << flowId << " is not in m_recordQpExec" << std::endl;
		exit(1);
	}
	it->second.receSizeInbyte += p->GetSize() - ch.GetSerializedSize();;
	it->second.recePacketNum++;
	RdmaSmartFlowRouting::sum_data_receive+= p->GetSize() - ch.GetSerializedSize();
		// ====添加进度检查和输出========================================
	static int last_reported_interval = -1; // 记录上次报告的区间 (-1 表示未报告过)
	if (RdmaSmartFlowRouting::sum_data > 0) { // 确保分母不为0
		double progress_ratio = (double)RdmaSmartFlowRouting::sum_data_receive / RdmaSmartFlowRouting::sum_data;
		int current_percentage = (int)(progress_ratio * 100);
		int current_interval = current_percentage / 10; // 计算当前处于哪个10%区间 (0-10%为区间0, 10-20%为区间1, 等等)
		
		// 如果进入了新的10%区间，则输出提示信息
		if (current_interval > last_reported_interval) {
			last_reported_interval = current_interval;
			int interval_start = current_interval * 10;
			int interval_end = (current_interval + 1) * 10;
			if (interval_end > 100) interval_end = 100; // 确保不超过100%
			// 获取当前真实时间
			time_t now = time(0);
			char* time_str = ctime(&now);
			// 去除换行符
			time_str[strlen(time_str)-1] = '\0';
			std::cout << "[NOW Time:" << time_str << "] 数据接收进度已到: " << interval_start << "%" <<std::endl;
		}
	}///===========================================================================

	Ptr<RdmaRxQueuePair> rxQp = GetRxQp(ch.dip, ch.sip, ch.udp.dport, ch.udp.sport, ch.udp.pg, false);
	if (rxQp == NULL)
	{
		// FlowIDNUMTag fit;
		// bool IsHaveTag = p->PeekPacketTag(fit);
		// NS_ASSERT_MSG(IsHaveTag, "Rx cannot find the flow id");
		rxQp = InitRxQp(ch.dip, ch.sip, ch.udp.dport, ch.udp.sport, ch.udp.pg, flowId);
	}
	rxQp->m_milestone_rx = m_ack_interval;

	bool isEnableCnp = false;
	ReceiverSequenceCheckResult x = ReceiverCheckSeq(ch.udp.seq, rxQp, payload_size, isEnableCnp);
	Ptr<Packet> ackPkt = ConstructAckForUDP(x, ch, rxQp, payload_size);
	if (Irn::mode == Irn::Mode::NACK)
	{
		NS_ASSERT_MSG(ackPkt, "Ack packet should be generated");
		Ipv4SmartFlowPathTag pathTag;
		bool IsHavePathTag = p->PeekPacketTag(pathTag);
		NS_ASSERT_MSG(IsHavePathTag, "Path tag should be attached on UDP packet");
		int64_t ts = Simulator::Now().GetNanoSeconds() - pathTag.GetTimeStamp().GetNanoSeconds();
		NS_ASSERT_MSG(ts >= 0, "Timestamp should be non-negative");
		AckPathTag ackTag;
		ackTag.SetPathId(pathTag.get_path_id());
		ackTag.SetFlowId(rxQp->m_flow_id);
		ackTag.SetDelay(ts);
		ackPkt->AddPacketTag(ackTag);
	}

	if (ackPkt)
	{
        // ACK_QUEUE_V1: receiver adds no DATA telemetry; switches annotate reverse ACKs.

		uint32_t nic_idx = GetNicIdxOfRxQp(rxQp);

		m_nic[nic_idx].dev->RdmaEnqueueHighPrioQ(ackPkt);
		m_nic[nic_idx].dev->TriggerTransmit();
	}
	return 0;
}


int RdmaHw::ReceiveProbeDataOnDstHostForLaps(Ptr<Packet> p, CustomHeader &ch)
{
	NS_LOG_FUNCTION (this << "Node=" << m_node->GetId());
	NS_ASSERT_MSG(Irn::mode == Irn::Mode::IRN_OPT || Irn::mode == Irn::Mode::NACK, "IRN_OPT or NACK should be enabled");
	NS_ASSERT_MSG(p && ch.l3Prot == L3ProtType::UDP, "Not UDP packet");
	Ptr<Packet> ackPkt = NULL;
	Ipv4SmartFlowProbeTag probeTag;
	bool isProbe = p->PeekPacketTag(probeTag);
	NS_ASSERT_MSG(isProbe, "Probe packet should be received here");
	ackPkt = ConstructAckForProbe(ch);
	ackPkt->AddPacketTag(probeTag);

	Ipv4SmartFlowPathTag pathTag;
	bool IsHavePathTag = p->PeekPacketTag(pathTag);
	NS_ASSERT_MSG(IsHavePathTag, "Path tag should be attached on UDP packet");
	int64_t ts = Simulator::Now().GetNanoSeconds() - pathTag.GetTimeStamp().GetNanoSeconds();
	NS_ASSERT_MSG(ts >= 0, "Timestamp should be non-negative");


	AckPathTag ackTag;
	ackTag.SetPathId(pathTag.get_path_id());
	ackTag.SetDelay(ts);
	ackPkt->AddPacketTag(ackTag);
	// std::cout << "Node " << m_node->GetId() << " Receive Probe Data Packet for path " << pathTag.get_path_id() << std::endl;

	if (ackPkt)
	{
		// m_nic[1].dev->RdmaEnqueueHighPrioQ(ackPkt);
		// m_nic[1].dev->TriggerTransmit();
		// Ptr<RdmaRxQueuePair> rxQp = GetRxQp(ch.dip, ch.sip, ch.udp.dport, ch.udp.sport, ch.udp.pg, false);
		// uint32_t nic_idx = GetNicIdxOfRxQp(rxQp);
		m_nic[1].dev->RdmaEnqueueHighPrioQ(ackPkt);
		m_nic[1].dev->TriggerTransmit();
	}
	return 0;
}




	int RdmaHw::ReceiveCnp(Ptr<Packet> p, CustomHeader &ch)
	{
		NS_LOG_FUNCTION(this);
		// QCN on NIC
		// This is a Congestion signal
		// Then, extract data from the congestion packet.
		// We assume, without verify, the packet is destinated to me
		uint32_t qIndex = ch.cnp.qIndex;
		if (qIndex == 1)
		{ // DCTCP
			std::cout << "TCP--ignore\n";
			return 0;
		}
		// uint16_t udpport = ch.cnp.fid; // corresponds to the sport
		// uint8_t ecnbits = ch.cnp.ecnBits;
		// uint16_t qfb = ch.cnp.qfb;
		// uint16_t total = ch.cnp.total;

		// uint32_t i;
		//  get qp


    NS_ASSERT(ch.cnp.fid == ch.udp.dport);
    uint16_t udpport = ch.cnp.fid;  // corresponds to the sport (CNP's dport)
    uint16_t sport = ch.udp.sport;  // corresponds to the dport (CNP's sport)
    uint8_t ecnbits = ch.cnp.ecnBits;
    uint16_t qfb = ch.cnp.qfb;
    uint16_t total = ch.cnp.total;

		uint32_t i;
    uint64_t key = GetQpKey(ch.sip, udpport, sport, qIndex);
		Ptr<RdmaQueuePair> qp = GetQp(key);

		if (qp == NULL)
		{
			auto it = m_finishedQpMap.find(key);
			if (it != m_finishedQpMap.end())
			{
				it->second += 1;
				return 1;
			}
			else
			{
				std::cout << "ERROR: " << "node:" << m_node->GetId() << " cannot find the qp for " << (ch.l3Prot == 0xFC ? "ACK" : "NACK") << "\n";
				exit(1);
			}
		}



		// get nic
		uint32_t nic_idx = GetNicIdxOfQp(qp);
		Ptr<QbbNetDevice> dev = m_nic[nic_idx].dev;

		if (qp->m_rate == 0) // lazy initialization
		{
			qp->TraceRate(dev->GetDataRate());
			qp->m_rate = dev->GetDataRate();
			if (m_cc_mode == CongestionControlMode::DCQCN_MLX)
			{
				qp->mlx.m_targetRate = dev->GetDataRate();
			}
			else if (m_cc_mode == CongestionControlMode::HPCC)
			{
				qp->hp.m_curRate = dev->GetDataRate();
				if (m_multipleRate)
				{
					for (uint32_t i = 0; i < IntHeader::maxHop; i++)
						qp->hp.hopState[i].Rc = dev->GetDataRate();
				}
			}
			else if (m_cc_mode == CongestionControlMode::TIMELY)
			{
				qp->tmly.m_curRate = dev->GetDataRate();
			}
			else if (m_cc_mode == CongestionControlMode::HPCC_PINT)
			{
				qp->hpccPint.m_curRate = dev->GetDataRate();
			}
		}
		return 0;
	}

	void RdmaHw::HandleTimeout(Ptr<RdmaQueuePair> qp, Time rto) {
    // Assume Outstanding Packets are lost
    // std::cerr << "Timeout on qp=" << qp << std::endl;
    if (qp->IsFinished()) {
        return;
    }
	NS_LOG_INFO("rto timeInNS" << rto.GetNanoSeconds());
	if (m_lbSolution == LB_Solution::LB_PLB)
	{
		plb_update_state_upon_rto(qp);
	}

	uint32_t nic_idx = GetNicIdxOfQp(qp);
	Ptr<QbbNetDevice> dev = m_nic[nic_idx].dev;

	if (Irn::mode == Irn::Mode::IRN_OPT)
	{
		qp->RecoverQueueUponTimeout();
		dev->TriggerTransmit();
		return;
	}

	// IRN: disable timeouts when PFC is enabled to prevent spurious retransmissions
	if (Irn::mode == Irn::Mode::IRN && dev->IsPfcEnabled())
		return;
	if (m_cnt_timeout.find(qp->m_flow_id) == m_cnt_timeout.end())
		m_cnt_timeout[qp->m_flow_id] = 0;
	m_cnt_timeout[qp->m_flow_id]++;

	if (Irn::mode == Irn::Mode::IRN)
		qp->m_irn.m_recovery = true;
	if (ENABLE_LOSS_PACKET_TEST)
	{
		Time now = Simulator::Now();
		LostPacketEntry m_saveRecordEntry;
		std::string flowId = ipv4Address2string(qp->sip) + "#" + ipv4Address2string(qp->dip) + "#" + std::to_string(qp->sport);
		if (m_lossPacket[m_node->GetId()][flowId].find(now.GetMicroSeconds()) != m_lossPacket[m_node->GetId()][flowId].end())
		{
			m_saveRecordEntry = m_lossPacket[m_node->GetId()][flowId][now.GetMicroSeconds()];
		}
		m_saveRecordEntry.snd_nxt = qp->snd_nxt;
		m_saveRecordEntry.snd_una = qp->snd_una;
		m_saveRecordEntry.RTO = rto.GetMicroSeconds();
		m_lossPacket[m_node->GetId()][flowId][now.GetMicroSeconds()] = m_saveRecordEntry;
		NS_LOG_INFO("RTO " << rto.GetMicroSeconds() << " FLOW " << flowId << " snd_nxt " << qp->snd_nxt << " <-snd_una " << qp->snd_una);
	}

		



	RecoverQueue(qp);
	dev->TriggerTransmit();
}


// 	void RdmaHw::HandleTimeoutForLaps(Ptr<RdmaQueuePair> qp) {
// 		NS_ASSERT_MSG(Irn::mode == Irn::Mode::IRN_OPT, "LAPS should be enabled");
//     if (qp->IsFinished()) { return; }
//     uint32_t nic_idx = GetNicIdxOfQp(qp);
//     Ptr<QbbNetDevice> dev = m_nic[nic_idx].dev;
// 		qp->RecoverQueueUponTimeout();
// 		dev->TriggerTransmit();		
// }





	int RdmaHw::ReceiveAck(Ptr<Packet> p, CustomHeader &ch)
	{
		NS_LOG_FUNCTION(this);
		uint16_t qIndex = ch.ack.pg;
		uint16_t port = ch.ack.dport;
    uint16_t sport = ch.ack.sport;  // dport for this host (sport of ACK packet)
		uint32_t seq = ch.ack.seq;
		uint8_t cnp = (ch.ack.flags >> qbbHeader::FLAG_CNP) & 1;
		// int i;
    uint64_t key = GetQpKey(ch.sip, port, sport, qIndex);
		Ptr<RdmaQueuePair> qp = GetQp(key);
		if (qp == NULL)
		{
			auto it = m_finishedQpMap.find(key);
			if (it != m_finishedQpMap.end())
			{
				NS_LOG_INFO("FLOW is finished and ack is useless");
				return 1;
			}
			else
			{
				std::cout << "ERROR: " << "node:" << m_node->GetId() << " cannot find the flow for " << (ch.l3Prot == 0xFC ? "ACK" : "NACK") << std::endl;
				exit(1);
			}
		}
		// std::string flowId = ipv4Address2string(Ipv4Address(ch.dip)) + "#" + ipv4Address2string(Ipv4Address(ch.sip)) + "#" + std::to_string(ch.ack.sport);
		// m_recordQpExec[qp->m_flow_id].receAckInbyte += p->GetSize();
		m_recordQpExec[qp->m_flow_id].receAckPacketNum++;

		uint32_t nic_idx = GetNicIdxOfQp(qp);
		Ptr<QbbNetDevice> dev = m_nic[nic_idx].dev;
		if (m_ack_interval == 0) {
			std::cout << "ERROR: shouldn't receive ack\n";
			exit(1);
		}else	{
			if (Irn::mode == Irn::Mode::IRN_OPT && ch.ack.irnNackSize != 0)
			{
				qp->m_irn.m_sack.sack(ch.ack.irnNack, ch.ack.irnNackSize);
				qp->m_irn.m_sack.discardUpTo(qp->snd_una);
			}
			
			if (!m_backto0)
			{
				qp->Acknowledge(seq);
			}
			else
			{
				uint32_t goback_seq = seq / m_chunk * m_chunk;
				qp->Acknowledge(goback_seq);
			}

			if (Irn::mode == Irn::Mode::IRN) {
					// handle NACK
					NS_ASSERT(ch.l3Prot == L3ProtType::NACK); // no pure ack in Irn
					// for bdp-fc calculation update m_irn_maxAck
					if (seq > qp->m_irn.m_highest_ack) qp->m_irn.m_highest_ack = seq;

					if (ch.ack.irnNackSize != 0) {
							// ch.ack.irnNack contains the seq triggered this NACK
							qp->m_irn.m_sack.sack(ch.ack.irnNack, ch.ack.irnNackSize);
					}

					uint32_t firstSackSeq, firstSackLen;
					if (qp->m_irn.m_sack.peekFrontBlock(&firstSackSeq, &firstSackLen)) {
							if (qp->snd_una == firstSackSeq) {
									qp->snd_una += firstSackLen;
							}
					}

					qp->m_irn.m_sack.discardUpTo(qp->snd_una);

					if (qp->snd_nxt < qp->snd_una) {
							qp->snd_nxt = qp->snd_una;
					}
					// if (qp->irn.m_sack.IsEmpty())  { //
					if (qp->m_irn.m_recovery && qp->snd_una >= qp->m_irn.m_recovery_seq) {
							qp->m_irn.m_recovery = false;
					}
			} else {
					if (qp->snd_nxt < qp->snd_una) {
							qp->snd_nxt = qp->snd_una;
					}
			}
			Time now = Simulator::Now();
			std::ostringstream oss;
			int32_t gap = qp->m_size - qp->snd_una;
			oss << "snd_una:" << qp->snd_una << " qpsize:" << qp->m_size << " gap " << gap << " currTime " << now.GetNanoSeconds();

			m_recordQpSen[qp->GetStringHashValueFromQp()] = oss.str();
			if (qp->IsFinished())
			{
				QpComplete(qp);
				return 0; // ying added;
			}
		}
    
    if (qp->GetOnTheFly() > 0) {
        if (qp->m_retransmit.IsRunning()){
					qp->m_retransmit.Cancel();
				}
				qp->m_retransmit = Simulator::Schedule(qp->GetRto(m_mtu), &RdmaHw::HandleTimeout, this, qp, qp->GetRto(m_mtu));
    }
		
		if(Irn::mode == Irn::Mode::IRN_OPT){

		}
    else if (Irn::mode == Irn::Mode::IRN)
		{
			if (ch.ack.irnNackSize != 0) {						
					if (!qp->m_irn.m_recovery) {
							qp->m_irn.m_recovery_seq = qp->snd_nxt;
							RecoverQueue(qp);
							qp->m_irn.m_recovery = true;
					}
			} else {
					if (qp->m_irn.m_recovery) {
							qp->m_irn.m_recovery = false;
					}
			}

    } else if (ch.l3Prot == L3ProtType::NACK) { // NACK
		if (ENABLE_LOSS_PACKET_TEST)
		{
			Time now = Simulator::Now();
			LostPacketEntry m_saveRecordEntry;
			std::string flowId = ipv4Address2string(qp->sip) + "#" + ipv4Address2string(qp->dip) + "#" + std::to_string(qp->sport);
			if (m_lossPacket[m_node->GetId()][flowId].find(now.GetMicroSeconds()) != m_lossPacket[m_node->GetId()][flowId].end())
			{
				m_saveRecordEntry = m_lossPacket[m_node->GetId()][flowId][now.GetMicroSeconds()];
			}
			m_saveRecordEntry.snd_nxt = qp->snd_nxt;
			m_saveRecordEntry.snd_una = qp->snd_una;
			m_lossPacket[m_node->GetId()][flowId][now.GetMicroSeconds()] = m_saveRecordEntry;
			NS_LOG_INFO("NACK " << " FLOW " << flowId << " snd_nxt " << qp->snd_nxt << " <-snd_una " << qp->snd_una);
		}
		RecoverQueue(qp);
		}


		// handle cnp
		if (cnp)
		{
			if (m_cc_mode == CongestionControlMode::DCQCN_MLX)
			{ // mlx version
				cnp_received_mlx(qp);
			}
		}

		if (m_cc_mode == CongestionControlMode::HPCC)
		{
			HandleAckHp(qp, p, ch);
		}
		else if (m_cc_mode == CongestionControlMode::TIMELY)
		{
			HandleAckTimely(qp, p, ch);
		}
		else if (m_cc_mode == CongestionControlMode::DCTCP)
		{
			if (m_lbSolution == LB_Solution::LB_PLB)
			{
				PLBHandleAckDctcp(qp, p, ch);
			}
			else
			{
				HandleAckDctcp(qp, p, ch);
			}
		}
		else if (m_cc_mode == CongestionControlMode::HPCC_PINT)
		{
			HandleAckHpPint(qp, p, ch);
		}else if (m_cc_mode == CongestionControlMode::CC_LAPS)
		{
			HandleAckLaps(qp, p, ch);
		}

		
		// ACK may advance the on-the-fly window, allowing more packets to send
		dev->TriggerTransmit();
		return 0;
	}

	bool RdmaHw::checkOutstandingDataAndUpdateLossyData(uint32_t pid, uint32_t flowId, uint32_t seq, uint16_t size){
		NS_LOG_FUNCTION(this);
		NS_LOG_INFO("OutStanding Data On path " << pid << "are as follows: ");
		auto it = m_outstanding_data.find(pid);
		NS_ASSERT_MSG(it != m_outstanding_data.end(), "Invalid path id");
		std::list<OutStandingDataEntry> & dataList = it->second;
		NS_ASSERT_MSG(dataList.size() > 0, "Time " << Simulator::Now().GetNanoSeconds()<< ", Invalid outstanding data for PathID " << pid << " FlowID " << flowId);
		// for(auto & it2 : dataList){
		// 	NS_LOG_INFO(it2.to_string());
		// }

        if(RdmaSmartFlowRouting::lcPathTimeout){
            bool exists=std::any_of(dataList.begin(),dataList.end(),[&](const OutStandingDataEntry &e){return e.size && e.flow_id==flowId && e.seq==seq && e.size==size;});
            if(!exists){TracePathTimeout("late_data_ack",pid,seq);return false;}
        }
		bool valid = false;
		bool lossy = false;
		auto it2 = dataList.begin();
		// if (pid == 26320)
		// {
		// 	std::cout << "Pid " << pid << ", flowId " << flowId << " " << " seq " << seq << " size " << size << std::endl;
   	// 	std::cout << "Outstanding Data ";
		// bool isFound = false;
		// for (auto it3 = dataList.begin(); it3 != dataList.end(); it3++)
		// {
		// 	if(it3->flow_id == flowId && it3->seq == seq && it3->size == size){
		// 		isFound = true;
		// 		break;
		// 	}
		// }
		// if (!isFound)
		// {
		// 	return false;
		// }

		
		// 	std::cout << std::endl;

		// }

		
		while (it2 != dataList.end())
		{
            if(RdmaSmartFlowRouting::lcPathTimeout && it2->size==0){it2=dataList.erase(it2);continue;}
			if(it2->flow_id == flowId && it2->seq == seq && it2->size == size){
                RdmaSmartFlowRouting::TraceRecovery("ack_match",m_node->GetId(),pid,it2->flow_id,it2->seq,it2->size,flowId,seq,0,0);
				//=================================新增逻辑，减少BDP===================================================
				if (RdmaSmartFlowRouting::enable_laps_plus)
				{
					decreaseBDPForPath(pid, it2->size);
				}
				it2 = dataList.erase(it2);
				valid = true;
				break;
			}
			else
			{
				auto it3 = m_flowId2Qp.find(it2->flow_id);
				NS_ASSERT_MSG(it3 != m_flowId2Qp.end(), "Invalid flow id");
				auto qp = it3->second;
				// if(qp->m_flow_id==3816){
				// 	std::cout<<"itseq:"<<it2->seq<<" itsize:"<<it2->size<<std::endl;
				// }
				RdmaSmartFlowRouting::TraceRecovery("ack_skip_mark",m_node->GetId(),pid,it2->flow_id,it2->seq,it2->size,flowId,seq,0,0);
				qp->m_irn.m_sack.m_lossy_data.emplace_back(it2->seq, it2->size);
			// if (   it2->flow_id == 195533
			// 	|| it2->flow_id == 195537 
			// 	|| it2->flow_id == 197957
			// 	|| it2->flow_id == 193101
			// 	|| it2->flow_id == 329013
			// 	|| it2->flow_id == 195541
			// 	|| it2->flow_id == 193105
			// )
			// {
			// 	std::cout << "Time: " 							<< Simulator::Now().GetNanoSeconds() 	<< ", ";
			// 	std::cout << "PathID: "    						<< pid 								 	<< ", ";
			// 	std::cout << "Reason: " 					  	<< "Lossy"							<< ", ";
			// 	std::cout << "[FlowID, PktSeq, Payload] = [" 	<< it2->flow_id << ", " << it2->seq << ", " << it2->size << "]\n";
			// }
                
				// if(qp->m_flow_id==3816){
				// 	std::cout<<"1111seq:"<<qp->m_irn.m_sack.m_lossy_data.back().first<<" 11111itsize:"<<qp->m_irn.m_sack.m_lossy_data.back().second<<std::endl;
				// }
				// std::cout << "Time " << Simulator::Now().GetNanoSeconds() << " FlowID " << qp->m_flow_id << " Rate " <<  1.0*qp->laps.m_curRate.GetBitRate()/1000000000 << " Gbps ";
				// std::cout << ", Lossy data for PathID " << pid << " with " << it2->to_string() << std::endl;
				NS_LOG_INFO ("LossyData: flowId=" << flowId << ", seq=[" << it2->seq << ", " << it2->size << ")");
				// if (qp->m_size - qp->snd_una < 1000)
				// {
				// 	std::cout << "Time" << Simulator::Now().GetNanoSeconds() << " LossyData: flowId=" << flowId << ", seq=[" << it2->seq << ", " << it2->size << ")" << " QPSIZE:" << qp->m_size << " senduna:" << qp->snd_una << " sendNext:" << qp->snd_nxt << std::endl;
				// }
				//=================================新增逻辑，减少BDP===================================================
				if (RdmaSmartFlowRouting::enable_laps_plus)
				{
					decreaseBDPForPath(pid, it2->size);
				}

				it2 = it->second.erase(it2);
				lossy = true;
			}
		}
		NS_ASSERT_MSG(valid, "Time " << Simulator::Now().GetNanoSeconds()<< ", Invalid outstanding data for PathID " << pid << " FlowID " << flowId << " Seq " << seq);
		// if (!valid)
		// {
		// 	std::cerr << "Time " << Simulator::Now().GetNanoSeconds() << ", Invalid outstanding data for PathID " << pid << " FlowID " << flowId << " Seq " << seq << std::endl;
		// 	exit(1);
		// }
		
        if(RdmaSmartFlowRouting::lcPathTimeout){ArmPathTimeout(pid);return lossy;}
		if (dataList.size() == 0)
		{
			CancelRtoPerPath(pid);
		}
		else
		{
			SetTimeoutForLapsPerPath(pid);
		}
		
		return lossy;
	}




	int RdmaHw::ReceiveAckForLaps(Ptr<Packet> p, CustomHeader &ch)
	{
		NS_LOG_FUNCTION(this);
		NS_ASSERT_MSG(p && ch.l3Prot == L3ProtType::NACK, "Not NACK packet");
		NS_ASSERT_MSG(Irn::mode == Irn::Mode::IRN_OPT || Irn::mode == Irn::Mode::NACK, "TRN_OPTIMIZED should be enabled");
		NS_ASSERT_MSG(m_cc_mode == CongestionControlMode::CC_LAPS, "LAPS CC should be enabled");

			Ipv4SmartFlowProbeTag probeTag;
			bool findProPacket = p->PeekPacketTag(probeTag);
			if (findProPacket)
			{
				return ReceiveProbeAckForLaps(p, ch);
			}

        if(RdmaSmartFlowRouting::lcEnabled && m_E2ErdmaSmartFlowRouting->ReceiveLinkCapacityFeedback(p)) {
            if(RdmaSmartFlowRouting::lcBwCc && RdmaSmartFlowRouting::lcFeedbackResume)
                for(auto &nic:m_nic)if(nic.dev)Simulator::ScheduleNow(&QbbNetDevice::NotifyBandwidthFeedback,nic.dev);
        } // FEEDBACK_RESUME_V1: ScheduleNow finishes normal ACK/CC processing first.
		uint16_t qIndex = ch.ack.pg;
		uint16_t port = ch.ack.dport;
    uint16_t sport = ch.ack.sport;  // dport for this host (sport of ACK packet)
		uint32_t seq = ch.ack.seq;
		uint8_t cnp = (ch.ack.flags >> qbbHeader::FLAG_CNP) & 1;
		// int i;
		uint64_t key = GetQpKey(ch.sip, port, sport, qIndex);
		Ptr<RdmaQueuePair> qp = GetQp(key);
		if (qp == NULL)
		{
			if (checkQpFinishedOnDstHost(ch))	{ return 1; }
			std::cerr << "ERROR: " << "node:" << m_node->GetId() << " cannot find the flow \n";
		}


		uint32_t f_pid = 0;
		if (Irn::mode == Irn::Mode::NACK)
		{
			AckPathTag ackTag;
			bool ishaveAckTag = p->PeekPacketTag(ackTag);
			NS_ASSERT_MSG(ishaveAckTag, "Path tag should be attached on ACK packet");
			f_pid = ackTag.GetPathId();
			uint64_t delayInNs = ackTag.GetDelay();
			PathData * pitEntry = m_E2ErdmaSmartFlowRouting->lookup_PIT(f_pid);
			NS_ASSERT_MSG(pitEntry != NULL, "Invalid path id");
//====================时延更新，重置================================================
			pitEntry->sent_pkt_num=0;
			pitEntry->d_t=(double)(delayInNs - pitEntry->latency)/(Simulator::Now().GetNanoSeconds()-pitEntry->tsGeneration.GetNanoSeconds());

			pitEntry->latency = delayInNs;
			//std::cout << "更新后的时延"<< " Latency " << delayInNs << " ns" << std::endl;
			pitEntry->tsGeneration = Simulator::Now();

            // FIRSTTEST_V1
            if(RdmaSmartFlowRouting::firsttestTraceEnabled)
                RdmaSmartFlowRouting::firsttestPathLog << Simulator::Now().GetNanoSeconds() << ',' << m_node->GetId() << ','
                    << qp->m_flow_id << ',' << f_pid << ',' << p->GetUid() << ',' << delayInNs << ','
                    << pitEntry->theoreticalSmallestLatencyInNs << ',' << 0 << '\n';


			
			//std::cout << "收到数据包ACK时延更新，重置" << std::endl;
  
			// if (pitEntry->latency <= pitEntry->theoreticalSmallestLatencyInNs)
			// {
			// 	pitEntry->nextAvailableTime = Simulator::Now();
			// }
			// else
			// {
			// 	pitEntry->nextAvailableTime = Simulator::Now() + NanoSeconds(pitEntry->latency - pitEntry->theoreticalSmallestLatencyInNs);
			// }

			insertPathDelayRecord(pitEntry->pid, pitEntry->latency);
			// pitEntry->print();
			NS_LOG_INFO("#Node " << m_node->GetId() << " receive ACK with ExpSeq=" << seq << ", NackSeq=" << ch.ack.irnNack << ", NackSize=" << ch.ack.irnNackSize << ", PathId=" << f_pid);
		}
		

			// m_irn.m_sack.checkOutstandingDataAndUpdateLossyData(fpid, nackSeq);
		RdmaSmartFlowRouting::TraceRecovery("ack_process",m_node->GetId(),f_pid,qp->m_flow_id,ch.ack.irnNack,ch.ack.irnNackSize,0,0,p->GetUid(),0);
		checkOutstandingDataAndUpdateLossyData(f_pid, qp->m_flow_id, ch.ack.irnNack, ch.ack.irnNackSize);

		if (!m_backto0)
		{
			qp->AcknowledgeForLaps(seq, ch.ack.irnNack, ch.ack.irnNackSize,f_pid);
		}
		else
		{
			uint32_t goback_seq = seq / m_chunk * m_chunk;
			qp->AcknowledgeForLaps(goback_seq, ch.ack.irnNack, ch.ack.irnNackSize,f_pid);
		}

		// qp->CheckAndUpdateQpStateForLaps();

		Time now = Simulator::Now();
		std::ostringstream oss;
		int32_t gap = qp->m_size - qp->snd_una;
		oss << "snd_una:" << qp->snd_una << " qpsize:" << qp->m_size << " gap " << gap << " currTime " << now.GetNanoSeconds();

		m_recordQpSen[qp->GetStringHashValueFromQp()] = oss.str();
		if (qp->IsFinished())
		{
			QpComplete(qp);
			return 0; // ying added;
		}
		

    // if (qp->GetOnTheFlyForLaps() > 0) {
    //     if (qp->m_retransmit.IsRunning()){
		// 			qp->m_retransmit.Cancel();
		// 		}
		// 		qp->m_retransmit = Simulator::Schedule(NanoSeconds(qp->m_baseRtt), &RdmaHw::HandleTimeoutForLaps, this, qp);
    // }
		

		HandleAckLaps(qp, p, ch);
		
		uint32_t nic_idx = GetNicIdxOfQp(qp);
		Ptr<QbbNetDevice> dev = m_nic[nic_idx].dev;
		dev->TriggerTransmit();
		return 0;
	}

	int RdmaHw::ReceiveProbeAckForLaps(Ptr<Packet> p, CustomHeader &ch)
	{
			NS_LOG_FUNCTION(this);
            // ACK_IMMEDIATE_V1: probe ACKs refresh shared-link state as well as path latency.
            bool updated=RdmaSmartFlowRouting::lcEnabled && m_E2ErdmaSmartFlowRouting->ReceiveLinkCapacityFeedback(p);
            if(RdmaSmartFlowRouting::lcPathTimeout && ch.ack.irnNack>0)return ReceivePathTimeoutProbeAck(p,ch);
            m_E2ErdmaSmartFlowRouting->ReceiveEarlyProbeAck(p); // EARLY_GHOST_V1
            // A matched probe also changes the maintenance deadline when its queue remains congested.
            if(RdmaSmartFlowRouting::lcBwCc && RdmaSmartFlowRouting::lcFeedbackResume && (updated || RdmaSmartFlowRouting::lcEarlyProbe))
                for(auto &nic:m_nic)if(nic.dev)Simulator::ScheduleNow(&QbbNetDevice::NotifyBandwidthFeedback,nic.dev);
			Ipv4SmartFlowProbeTag probeTag;
			bool findProPacket = p->PeekPacketTag(probeTag);
			NS_ASSERT_MSG(findProPacket, "Probe packet should be attached on ACK packet");
			AckPathTag ackTag;
			bool ishaveAckTag = p->PeekPacketTag(ackTag);
			NS_ASSERT_MSG(ishaveAckTag, "Path tag should be attached on ACK packet");
			uint32_t f_pid = ackTag.GetPathId();
			uint64_t delayInNs = ackTag.GetDelay();
			PathData * pitEntry = m_E2ErdmaSmartFlowRouting->lookup_PIT(f_pid);
			NS_ASSERT_MSG(pitEntry != NULL, "Invalid path id");
			// std::cout << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!Node " << m_node->GetId() << " Receive Probe ACK Packet for path " << f_pid << std::endl;
			//  pitEntry->print();
			pitEntry->d_t=(double)(delayInNs - pitEntry->latency)/(Simulator::Now().GetNanoSeconds()-pitEntry->tsGeneration.GetNanoSeconds());
			pitEntry->latency = delayInNs;

			pitEntry->tsGeneration = Simulator::Now();

            // FIRSTTEST_V1
            if(RdmaSmartFlowRouting::firsttestTraceEnabled)
                RdmaSmartFlowRouting::firsttestPathLog << Simulator::Now().GetNanoSeconds() << ',' << m_node->GetId() << ','
                    << 0 << ',' << f_pid << ',' << p->GetUid() << ',' << delayInNs << ','
                    << pitEntry->theoreticalSmallestLatencyInNs << ',' << 1 << '\n';


			//====================时延更新，重置================================================
			pitEntry->sent_pkt_num=0;
			//std::cout << "Node " << m_node->GetId() << " Receive Probe ACK Packet for path " << f_pid << std::endl;
			// if (pitEntry->latency <= pitEntry->theoreticalSmallestLatencyInNs)
			// {
			// 	pitEntry->nextAvailableTime = Simulator::Now();
			// }
			// else
			// {
			// 	pitEntry->nextAvailableTime = Simulator::Now() + NanoSeconds(pitEntry->latency - pitEntry->theoreticalSmallestLatencyInNs);
			// }
			insertPathDelayRecord(pitEntry->pid, pitEntry->latency);
			// pitEntry->print();


        // BANDWIDTH_CC_V1: update active flows sharing these endpoint paths.
        if(RdmaSmartFlowRouting::lcBwCc)for(const auto &entry:m_qpMap) {
            auto qp=entry.second;
            if(qp->sip.Get()==ch.dip && qp->dip.Get()==ch.sip && !qp->IsFinished())UpdateRateForLapsPlus(qp,ch);
        }
		return 0;
	}


	int RdmaHw::ReceiveForLaps(Ptr<Packet> p, CustomHeader &ch)
	{
		NS_LOG_FUNCTION(this);
		NS_ASSERT_MSG(Irn::mode == Irn::Mode::IRN_OPT || Irn::mode == Irn::Mode::NACK, "IRN_OPT or NACK should be enabled");

		if (ch.l3Prot == L3ProtType::UDP)
		{

			ReceiveUdpOnDstHostForLaps(p, ch);
		}
		else if (ch.l3Prot == L3ProtType::NACK)
		{ 

			//-------------------------------------------------------改动部分----计算ACK带回来的时延--记得要加enable_LAPS2限定------------------------------------------------------------------
			if (m_lbSolution == LB_Solution::LB_E2ELAPS) {
              if (m_E2ErdmaSmartFlowRouting->enable_laps_plus == true) {

				Ipv4SmartFlowPathTag pathTag;
				bool IsHavePathTag = p->PeekPacketTag(pathTag);
				NS_ASSERT_MSG(IsHavePathTag, "Path tag should be attached on ACK packet");
				//计算计算ACK带回来的时延
				int64_t ack_caculated_delay = Simulator::Now().GetNanoSeconds() - pathTag.GetTimeStamp().GetNanoSeconds();
				NS_ASSERT_MSG(ack_caculated_delay >= 0, "Timestamp should be non-negative");
				//导出看看数据包/探测包走到终点的时延
				AckPathTag acktag;
				bool IsHaveACKPathTag = p->PeekPacketTag(acktag);
				NS_ASSERT_MSG(IsHaveACKPathTag, "Path tag should be attached on ACK packet");

				//看看是否为数据包ACK
				Ipv4SmartFlowProbeTag probeTag;
				bool findProPacket = p->PeekPacketTag(probeTag);
				if (findProPacket) {
					//std::cout << std::endl << "探测包 ";
					//std::cout << "ACK到达终点." << "   数据包到达终点所用时间:" << acktag.GetDelay() << "    ACK带回来的估计时延（还没计算本地出端口长度）：" << ack_caculated_delay << std::endl;

				}
				else
					{//是数据包ACK
					
                     uint32_t pathid= acktag.GetPathId();
                     PathData *pitentry= m_E2ErdmaSmartFlowRouting->lookup_PIT(pathid);
					 uint32_t baseline_delay=pitentry->portSequence.size()*(1000+84);
					 uint32_t realdelay= baseline_delay;
					
					uint32_t sum_queue_delay=0;
					 for(int i=1;i<pitentry->portSequence.size();i++){
                          Ptr<Node> Node=RdmaSmartFlowRouting::nodeIdToNodeMap[pitentry->nodeIdSequence[i]];
						  Ptr<SwitchNode> swNode = DynamicCast<SwitchNode>(Node);
						   uint32_t queue_length_bytes=0;
						   for (int prio = 0; prio < 8; prio++) {
                                  queue_length_bytes += swNode->m_mmu->egress_bytes[pitentry->portSequence[i]][prio];
                             }
				//std::cout << "  数据比特总量：" << sum_pkt << std::endl << std::endl << std::endl;
				//得到出端口的链路速率
				uint64_t current_port_rate = swNode->m_outPort2BitRateMap[pitentry->portSequence[i]];
				uint64_t total_bits = queue_length_bytes * 8;
				//  计算时延（纳秒）
				uint64_t queuing_delay_ns = (current_port_rate > 0)
					? (total_bits * 1000000000ULL) / current_port_rate
					: 0; // 防止除零

                      sum_queue_delay+=queuing_delay_ns;
					 }
					 realdelay+=sum_queue_delay;

					 //================ACK里放的是数据包测量的时延===============================
					int64_t data_caculated_delay=acktag.GetDelay();
                    
                    uint32_t path_hop=pitentry->portSequence.size();
					uint32_t  baseline_path_delay=path_hop*1004;
					uint32_t    throushold_delay=baseline_path_delay+21*84*path_hop;
					//记录时延的两种测量方式的误差
					 RdmaSmartFlowRouting::s_relErrorStats.Record(realdelay,ack_caculated_delay, data_caculated_delay,throushold_delay);
					 RdmaSmartFlowRouting::s_relErrorStats.RecordHopDiff(path_hop,realdelay,ack_caculated_delay,data_caculated_delay);
					  RdmaSmartFlowRouting::s_relErrorStats.RecordCongestionDiff(baseline_delay,realdelay,ack_caculated_delay, data_caculated_delay);
					



					 //=======================记录ACK，数据包测量所需要的总时间/基准时延的离散分布，用于画PMF图=========================
					  //目前以不堵车的情况估计ACK返回所需要的时间，后续可以进行实际测量，估计结果大差不差
					 
					 
					  //=================2.19  修改，分母从1004变为ACK_measurent_delay,改为测量测量延迟相对基准时延的倍率=======================
					 uint32_t ACK_measurent_delay=path_hop*1004;
					 
					 if(RdmaSmartFlowRouting::RecordAckSendTime.find(p->GetUid())==RdmaSmartFlowRouting::RecordAckSendTime.end()){
                        std::cout<<"ACK到达终点."<<"  但是没有记录发送时间"<<std::endl;
					 }
					 else{auto it = RdmaSmartFlowRouting::RecordAckSendTime.find(p->GetUid());
						ACK_measurent_delay=Simulator::Now().GetNanoSeconds()- it->second;
					   //std::cout<<"ACK到达终点."<<"    ACK返回所花时间："<<ACK_measurent_delay<<"基准时延 "<<baseline_path_delay<<std::endl;
                       RdmaSmartFlowRouting::RecordAckSendTime.erase(it);
					}


                    RdmaSmartFlowRouting::m_record_ack_packet_delay.ackdelay[(int)((double)(ACK_measurent_delay)/baseline_path_delay*10+0.5)]++;
					uint32_t packet_idx=(int)((double)(data_caculated_delay+ACK_measurent_delay)/baseline_path_delay*10+0.5);
					//超过14的就记在149倍位置
					if(packet_idx>=RdmaSmartFlowRouting::m_record_ack_packet_delay.packetdelay.size()){
						packet_idx=RdmaSmartFlowRouting::m_record_ack_packet_delay.packetdelay.size()-1;
					}
					RdmaSmartFlowRouting::m_record_ack_packet_delay.packetdelay[packet_idx]++;
                    RdmaSmartFlowRouting::m_record_ack_packet_delay.count++;
                    //=======================记录ACK，数据包测量所需要的总时间的离散分布，用于画PMF图=========================
					  



					 //std::cout<<"真实时延："<<realdelay;

                    // std::cout << "，ACK到达终点." << "   数据包到达终点所用时间:" << acktag.GetDelay() << "    ACK带回来的估计时延（还没计算本地出端口长度）：" << ack_caculated_delay << std::endl;
					}

				//这里就是替换ACK中AckPathTag的时延，之后对时延的操作就可以复用了
				acktag.SetDelay(ack_caculated_delay);
				p->ReplacePacketTag(acktag);

              //=============================更新各端口带宽利用率============================================
               //已经搬走了
              //=============================更新各端口带宽利用率============================================
			}
			

		}
			//-------------------------------------------------------改动部分结束---------------------------------------------------------



			ReceiveAckForLaps(p, ch);
		}
		else
		{ 
			NS_ASSERT_MSG(false, "unknown packet type");
		}
		return 0;
	}


	int RdmaHw::Receive(Ptr<Packet> p, CustomHeader &ch)
	{    
		//============新增更新带宽利用率部分========================================================
		 UpdatePortUtilization( p,ch);


		NS_LOG_FUNCTION(this);
		if (Irn::mode == Irn::Mode::IRN_OPT || Irn::mode == Irn::Mode::NACK)
		{
			// std::cout << "xxxxxxxxxxxxxxxxxx" << std::endl;
			return ReceiveForLaps(p, ch);
		}

		if (ch.l3Prot == L3ProtType::UDP)
		{ // UDP
			ReceiveUdp(p, ch);
		}
		else if (ch.l3Prot == L3ProtType::CNP)
		{ // CNP
			ReceiveCnp(p, ch);
		}
		else if (ch.l3Prot == L3ProtType::NACK)
		{ 
			ReceiveAck(p, ch);
		}
		else if (ch.l3Prot == L3ProtType::ACK)
		{ // ACK
			ReceiveAck(p, ch);
		}
		return 0;
	}

	// int RdmaHw::ReceiverCheckSeq(uint32_t seq, Ptr<RdmaRxQueuePair> q, uint32_t size)
	// {
  //   NS_LOG_FUNCTION (this << "[" << seq << ", " << seq+size << ")");

	// 	uint32_t expected = q->ReceiverNextExpectedSeq;

	// 	if (seq == expected  || (seq < expected && seq + size >= expected) ) // receive all or saome expected data
	// 	{
	// 		q->ReceiverNextExpectedSeq = expected + size;
	// 		if (q->ReceiverNextExpectedSeq >= static_cast<uint32_t>(q->m_milestone_rx))
	// 		{
	// 			q->m_milestone_rx += m_ack_interval;
	// 			return 1; // Generate ACK
	// 		}
	// 		else if (q->ReceiverNextExpectedSeq % m_chunk == 0)
	// 		{
	// 			return 1;
	// 		}
	// 		else
	// 		{
	// 			return 5;
	// 		}
	// 	}
	// 	else if (seq > expected)
	// 	{
	// 		// Generate NACK
	// 		if (Simulator::Now() >= q->m_nackTimer || q->m_lastNACK != expected)
	// 		{
	// 			q->m_nackTimer = Simulator::Now() + MicroSeconds(m_nack_interval);
	// 			q->m_lastNACK = expected;
	// 			if (m_backto0)
	// 			{
	// 				q->ReceiverNextExpectedSeq = q->ReceiverNextExpectedSeq / m_chunk * m_chunk;
	// 			}
	// 			return 2;
	// 		}
	// 		else
	// 			return 4;
	// 	}
	// 	else
	// 	{
	// 		// Duplicate.
	// 		return 3;
	// 	}
	// }

	
/**
 * @brief Check sequence number when UDP DATA is received
 *
 * @return int (Cnp)
 * ACTION_ERROR=0: should not reach here
 * -----------------------------Go-Back-N---------------------------
 * 1. ACTION_ACK=1: generate ACK
 * Case 1: expected && (expSeq>milestone || expSeq%chunk=0);
 * Case 2: useless
 * 2. ACTION_NACK = 2: generate NACK
 * Case 1: OoO && (nack-expired || expSeq!=lastNack); Cnp =true
 * 3. ACTION_NACKED=4: OoO, but skip to send NACK as it is already NACKed.
 * Case 1: OoO && (nacked && expSeq=lastNack)
 * 4. ACTION_NO_REPLY = 5
 * Case 1: expected && (expSeq<=milestone && expSeq%chunk!=0); 

 * -----------------------------Improved-RoCE-Network---------------------------
 * 1. ACTION_NACK = 2: still in loss recovery of IRN
 * Case 1: expected || useless && block exists;
 * Case 2: OoO && (packet-newly || nack-expired); Cnp = true
 * 2. ACTION_NACKED=4: OoO, but skip to send NACK as it is already NACKed.
 * Case 1: OoO && duplicated && nacked;
 * 3. ACTION_NACK_PSEUDO = 6:  NACK but functionality is ACK (indicating all packets are received)
 * Case 1: expected || useless && no block; 
 */
ReceiverSequenceCheckResult RdmaHw::ReceiverCheckSeq(uint32_t seq, Ptr<RdmaRxQueuePair> q, uint32_t size, bool &cnp) {
    NS_LOG_FUNCTION (this << seq << seq+size);
		if (Irn::mode == Irn::Mode::IRN_OPT || Irn::mode == Irn::Mode::NACK)
		{
			return ReceiverCheckSeqForLaps(seq, q, size, cnp);
		}
		
    uint32_t expected = q->ReceiverNextExpectedSeq;
    NS_LOG_INFO("expectedSeq=" << expected << ", m_milestone_rx=" << q->m_milestone_rx);
    if (seq == expected || (seq < expected && seq + size >= expected)) { // Receive all or some expected packet.
        if (Irn::mode == Irn::Mode::IRN) { // 如果开启了IRN，即选择性重传
            if (q->m_milestone_rx < seq + size) { //如果收到了的数据超过了m_milestone_rx，更新m_milestone_rx
                q->m_milestone_rx = seq + size; // 更新已经收到的最大序列号
            }else{
                NS_LOG_LOGIC ("The receiverd packet is NOT with the largest sequence.");
            }
            q->ReceiverNextExpectedSeq += size - (expected - seq); // 更新下一个应该收到的序列号，即最小的序列号
            NS_LOG_INFO ("Advance the ReceiverNextExpectedSeq to " << q->ReceiverNextExpectedSeq);
            uint32_t firstSackSeq, firstSackLen;
            bool isSackExist = q->m_sack.peekFrontBlock(&firstSackSeq, &firstSackLen); // Incremental but non-contiguous blocks.
            if (isSackExist) {
                NS_LOG_INFO("First block : [" << firstSackSeq << ", " << firstSackSeq + firstSackLen << ")");
                if ((firstSackSeq <= q->ReceiverNextExpectedSeq) && ((firstSackSeq + firstSackLen) >= q->ReceiverNextExpectedSeq) ){ // 如果sack中第一个块含有期望收到的数据
                    q->ReceiverNextExpectedSeq += (firstSackLen - (q->ReceiverNextExpectedSeq - firstSackSeq)); // found bug when q->ReceiverNextExpectedSeq > (sack_seq + sack_len) 
                    NS_LOG_INFO ("Advance the ReceiverNextExpectedSeq to " << q->ReceiverNextExpectedSeq);
                }else{
                    NS_LOG_INFO ("Maintain the ReceiverNextExpectedSeq.");
                }
            }else{
                NS_LOG_INFO ("Non-Exist block! Maintain the ReceiverNextExpectedSeq.");
            }

            size_t progress = q->m_sack.discardUpTo(q->ReceiverNextExpectedSeq);
            NS_LOG_INFO ("Removed blocks' length : " << progress); 
            if (q->m_sack.IsEmpty()) { // 没有乱序报文。即没有提前到达的报文
                NS_LOG_LOGIC ("No Out-Of-Order blocks");
                NS_LOG_INFO ("peseudo-NACK but functionality is ACK (indicating all packets are received)");
                return ReceiverSequenceCheckResult::ACTION_NACK_PSEUDO;  // This generates NACK, but actually functions as an ACK (indicates all packet has been received)
            } else {
            			q->m_nackTimer = Simulator::Now() + MicroSeconds(m_nack_interval);
                	return ReceiverSequenceCheckResult::ACTION_NACK;  // 有提前到达的，且<expected, first_arrived>之间有缺失的块
                // should we put nack timer here
								// if (Simulator::Now() < q->m_nackTimer) {
                // 	return ReceiverSequenceCheckResult::NACKED;  // don't need to send nack yet
								// }else{
            		// 	q->m_nackTimer = Simulator::Now() + MicroSeconds(m_nack_interval);
                // 	return ReceiverSequenceCheckResult::IRN_RECOVERY;  // 有提前到达的，且<expected, first_arrived>之间有缺失的块
								// }

            }
        }

        q->ReceiverNextExpectedSeq += size - (expected - seq);
        NS_LOG_INFO ("Advance ReceiverNextExpectedSeq: " << q->ReceiverNextExpectedSeq);
        if (q->ReceiverNextExpectedSeq >= q->m_milestone_rx) {
            q->m_milestone_rx += m_ack_interval;  // if ack_interval is small (e.g., 1), condition is meaningless，控制接收方生成 ACK频率。
            NS_LOG_LOGIC("Increase milestone by ack_interval : " << q->m_milestone_rx);
            return ReceiverSequenceCheckResult::ACTION_ACK; 
        } else if (q->ReceiverNextExpectedSeq % m_chunk == 0) {
            NS_LOG_LOGIC("ReceiverNextExpectedSeq % m_chunk == 0, generate ACK");
            NS_LOG_INFO ("m_chunk : " << m_chunk);
            return ReceiverSequenceCheckResult::ACTION_ACK;
        } else {
            NS_LOG_LOGIC("NOT generate ACK");
            return ReceiverSequenceCheckResult::ACTION_NO_REPLY;
        }
    } else if (seq > expected) {
        if (Irn::mode == Irn::Mode::IRN) {
            if (q->m_milestone_rx < seq + size){
                q->m_milestone_rx = seq + size;
                NS_LOG_INFO ("Increase m_milestone_rx to " << q->m_milestone_rx);
            }
            // if seq is already nacked, check for nacktimer
            if (q->m_sack.blockExists(seq, size) && Simulator::Now() < q->m_nackTimer) {
                NS_LOG_LOGIC ("This block is alreadly nacked.");
                return ReceiverSequenceCheckResult::ACTION_NACKED;  // don't need to send nack yet
            }
            NS_LOG_LOGIC("Set the NACK timer");
            q->m_nackTimer = Simulator::Now() + MicroSeconds(m_nack_interval);
            q->m_sack.sack(seq, size);  // set SACK
            NS_ASSERT(q->m_sack.discardUpTo(expected) == 0);  // SACK blocks must be larger than expected
            NS_LOG_LOGIC ("Mark the CNP upon out-of-order");
            cnp = true;    // XXX: out-of-order should accompany with CNP (?) TODO: Check on CX6
            return ReceiverSequenceCheckResult::ACTION_NACK;      // generate SACK
        }

        NS_LOG_LOGIC ("Using the Go-Back-N mechanism.");
		if (Irn::mode == Irn::Mode::GBN)
		{

			if (Simulator::Now() >= q->m_nackTimer || q->m_lastNACK != expected)
			{ // new NACK
				NS_LOG_LOGIC("generate NACK.");
				q->m_nackTimer = Simulator::Now() + MicroSeconds(m_nack_interval);
				q->m_lastNACK = expected;
				if (m_backto0)
				{
					q->ReceiverNextExpectedSeq = q->ReceiverNextExpectedSeq / m_chunk * m_chunk;
				}
				NS_LOG_LOGIC("Mark the CNP upon out-of-order");
				cnp = true; // XXX: out-of-order should accompany with CNP (?) TODO: Check on CX6
				return ReceiverSequenceCheckResult::ACTION_NACK;
			}
			else
			{
				// skip to send NACK
				NS_LOG_LOGIC("This block is duplicate AND last NACK is NOT expired.");
				NS_LOG_LOGIC("Skip to send NACK");
				return ReceiverSequenceCheckResult::ACTION_NACKED;
			}
		}
	}
	else
	{
		NS_LOG_INFO("Duplicate happens!");
		if (Irn::mode == Irn::Mode::IRN)
		{
			NS_LOG_LOGIC("Using the IRN mechanism.");
			// if (q->ReceiverNextExpectedSeq - 1 == q->m_milestone_rx) {
			// 	return 6; // This generates NACK, but actually functions as an ACK (indicates all
			// packet has been received)
			// }
			if (q->m_sack.IsEmpty())
			{
				return ReceiverSequenceCheckResult::ACTION_NACK_PSEUDO;
			}
			else
			{
				// should we put nack timer here
				// if (Simulator::Now() < q->m_nackTimer) {
				// return ReceiverSequenceCheckResult::ACTION_NACKED;  // don't need to send nack yet
				// }else{
				// q->m_nackTimer = Simulator::Now() + MicroSeconds(m_nack_interval);
				return ReceiverSequenceCheckResult::ACTION_NACK; // 有提前到达的，且<expected, first_arrived>之间有缺失的块
																 // }
			}
		}
		// Duplicate.
		NS_LOG_LOGIC("Using the Go-Back-N mechanism.");
		NS_LOG_INFO("generate ACK");
		return ReceiverSequenceCheckResult::ACTION_ACK; // According to IB Spec C9-110
														/**
														 * IB Spec C9-110
														 * A responder shall respond to all duplicate requests in PSN order;
														 * i.e. the request with the (logically) earliest PSN shall be executed first. If,
														 * while responding to a new or duplicate request, a duplicate request is received
														 * with a logically earlier PSN, the responder shall cease responding
														 * to the original request and shall begin responding to the duplicate request
														 * with the logically earlier PSN.
														 */
	}
}


ReceiverSequenceCheckResult RdmaHw::ReceiverCheckSeqForLaps(uint32_t seq, Ptr<RdmaRxQueuePair> q, uint32_t size, bool &cnp) {
    NS_LOG_FUNCTION (this << seq << seq+size);
		NS_ASSERT_MSG(Irn::mode == Irn::Mode::IRN_OPT || Irn::mode == Irn::Mode::NACK, "This function is only for LAPS");
    NS_LOG_INFO("Node " << m_node->GetId() << " receives data packet with : RecvSeq = " << seq << ", size = " << size);
		if (Irn::mode == Irn::Mode::NACK)
		{
			return ReceiverSequenceCheckResult::ACTION_NACK;
		}
		
		uint32_t expected = q->ReceiverNextExpectedSeq;
    if (seq == expected || (seq < expected && seq + size >= expected))
		{
			q->ReceiverNextExpectedSeq += size - (expected - seq);
			NS_LOG_INFO ("Node " << m_node->GetId() << " Advance expSeq from " << expected << " to " << q->ReceiverNextExpectedSeq);
			uint32_t firstSackSeq, firstSackLen;
			bool isSackExist = q->m_sack.peekFrontBlock(&firstSackSeq, &firstSackLen); // Incremental but non-contiguous blocks.
			if (isSackExist)
			{
				if ((firstSackSeq <= q->ReceiverNextExpectedSeq) && ((firstSackSeq + firstSackLen) >= q->ReceiverNextExpectedSeq) )
				{
						q->ReceiverNextExpectedSeq += (firstSackLen - (q->ReceiverNextExpectedSeq - firstSackSeq)); // found bug when q->ReceiverNextExpectedSeq > (sack_seq + sack_len) 
						NS_LOG_INFO ("Node " << m_node->GetId() << " Advance expSeq from " << expected << " to " << q->ReceiverNextExpectedSeq);
				}
			}
			q->m_sack.discardUpTo(q->ReceiverNextExpectedSeq);
			if (q->m_sack.IsEmpty())
			{
				return ReceiverSequenceCheckResult::ACTION_NACK_PSEUDO; 
			}
			else
			{
				return ReceiverSequenceCheckResult::ACTION_NACK;  
			}
    }
		else if (seq > expected)
		{
			if (q->m_sack.blockExists(seq, size))
			{
					return ReceiverSequenceCheckResult::ACTION_NACKED;
			}
			q->m_sack.sack(seq, size);
			NS_ASSERT(q->m_sack.discardUpTo(expected) == 0);
			return ReceiverSequenceCheckResult::ACTION_NACK;
        
    }
		else
		{
			if (q->m_sack.IsEmpty())
			{
				return ReceiverSequenceCheckResult::ACTION_NACK_PSEUDO; 
			}
			else
			{
				return ReceiverSequenceCheckResult::ACTION_NACK;
			}
    }
}










	void RdmaHw::AddHeader(Ptr<Packet> p, uint16_t protocolNumber)
	{
		PppHeader ppp;
		ppp.SetProtocol(EtherToPpp(protocolNumber));
		p->AddHeader(ppp);
	}
	uint16_t RdmaHw::EtherToPpp(uint16_t proto)
	{
		switch (proto)
		{
		case 0x0800:
			return 0x0021; // IPv4
		case 0x86DD:
			return 0x0057; // IPv6
		default:
			NS_ASSERT_MSG(false, "PPP Protocol number not defined!");
		}
		return 0;
	}



	void RdmaHw::RecoverQueue(Ptr<RdmaQueuePair> qp)
	{
		if (Irn::mode == Irn::Mode::IRN_OPT)
		{
			qp->RecoverQueue();
			return ;
		}
		
		qp->snd_nxt = qp->snd_una;
	}

	void RdmaHw::QpComplete(Ptr<RdmaQueuePair> qp)
	{
		NS_ASSERT(!m_qpCompleteCallback.IsNull());
		if (m_cc_mode == 1)
		{
			Simulator::Cancel(qp->mlx.m_eventUpdateAlpha);
			Simulator::Cancel(qp->mlx.m_eventDecreaseRate);
			Simulator::Cancel(qp->mlx.m_rpTimer);
		}
		flowComplteNum += 1;
		// This callback will log info
		// It may also delete the rxQp on the receiver
		m_qpCompleteCallback(qp);

		qp->m_notifyAppFinish();

		// delete the qp
		DeleteQueuePair(qp);
	}

	void RdmaHw::SetLinkDown(Ptr<QbbNetDevice> dev)
	{
		printf("RdmaHw: node:%u a link down\n", m_node->GetId());
	}

	void RdmaHw::AddTableEntry(Ipv4Address &dstAddr, uint32_t intf_idx)
	{
		uint32_t dip = dstAddr.Get();
		m_rtTable[dip].push_back(intf_idx);
	}

	void RdmaHw::ClearTable()
	{
		m_rtTable.clear();
	}

	void RdmaHw::RedistributeQp()
	{
		// clear old qpGrp
		for (uint32_t i = 0; i < m_nic.size(); i++)
		{
			if (m_nic[i].dev == NULL)
				continue;
			m_nic[i].qpGrp->Clear();
		}

		// redistribute qp
		for (auto &it : m_qpMap)
		{
			Ptr<RdmaQueuePair> qp = it.second;
			uint32_t nic_idx = GetNicIdxOfQp(qp);
			m_nic[nic_idx].qpGrp->AddQp(qp);
			// Notify Nic
			m_nic[nic_idx].dev->ReassignedQp(qp);
		}
	}

	Ptr<Packet> RdmaHw::GetNxtPacket(Ptr<RdmaQueuePair> qp)
	{
		NS_LOG_FUNCTION(this);
		uint32_t payload_size = 0;
        
		// if(qp->m_flow_id==3816){
		// 	std::cout <<" before Time" << Simulator::Now().GetNanoSeconds()<<" Next:"<<qp->snd_nxt<<" una:"<<qp->snd_una<<std::endl;
		// }
		//  if(qp->m_flow_id==3816){
		// std::cout<<"lost size:"<<qp->m_irn.m_sack.getLossyDataSize()<<std::endl;
		//  }
		payload_size = qp->GetBytesLeft();
		if (Irn::mode == Irn::Mode::NACK)
		{
			qp->CheckAndUpdateQpStateForLaps();
			//if qp->m_irn.m_lossy_data
		}
        // if(qp->m_flow_id==3816){
		// 	std::cout <<" mid Time" << Simulator::Now().GetNanoSeconds()<<" Next:"<<qp->snd_nxt<<" una:"<<qp->snd_una<<std::endl;
		// }
		if (m_mtu < payload_size)
		{
			payload_size = m_mtu;
		}


		// if (qp->m_irn.m_sack.getFirstLossyDataSize() > 0)
		// {
		// 	qp->m_irn.m_sack.m_lossy_data.pop_front();
		// }
		uint32_t seq = (uint32_t)qp->snd_nxt;
		qp->m_phyTxNPkts += 1;
		qp->m_phyTxBytes += payload_size;

		Ptr<Packet> p = Create<Packet>(payload_size);
		// add SeqTsHeader
		SeqTsHeader seqTs;
		seqTs.SetSeq(seq);
		seqTs.SetPG(qp->m_pg);
		p->AddHeader(seqTs);
		// add udp header
		UdpHeader udpHeader;
		udpHeader.SetDestinationPort(qp->dport);
		udpHeader.SetSourcePort(qp->sport);
		p->AddHeader(udpHeader);
        

        

		// add ipv4 header
		Ipv4Header ipHeader;
		ipHeader.SetSource(qp->sip);
		ipHeader.SetDestination(qp->dip);
		ipHeader.SetProtocol(0x11);
		ipHeader.SetPayloadSize(p->GetSize());
		ipHeader.SetTtl(64);
		ipHeader.SetTos(0);
		ipHeader.SetIdentification(qp->m_ipid);
		p->AddHeader(ipHeader);
		// add ppp header
		PppHeader ppp;
		ppp.SetProtocol(0x0021); // EtherToPpp(0x800), see point-to-point-net-device.cc
		p->AddHeader(ppp);

        CustomHeader ch(CustomHeader::L2_Header | CustomHeader::L3_Header | CustomHeader::L4_Header);
		p->PeekHeader(ch);
		
		// if (ch.udp.seq > qp->m_size)
		// {
		// 	std::cout << "Time" << Simulator::Now().GetNanoSeconds() << " ch.udp.seq:" << ch.udp.seq << " seq:"<<seq<<" qpsize"<< qp->m_size<<" sndNxt"<<qp->snd_nxt<<"flowId"<<qp->m_flow_id<<std::endl;
		// }
		

    // attach Stat Tag
    uint8_t packet_pos = UINT8_MAX;
    {
        FlowIDNUMTag fint;
        if (!p->PeekPacketTag(fint)) {
            fint.SetId(qp->m_flow_id);
            fint.SetFlowSize(qp->m_size);
            p->AddPacketTag(fint);
        }
        FlowStatTag fst;
        uint64_t size = qp->m_size;
        if (!p->PeekPacketTag(fst)) {
            if (size < m_mtu && qp->snd_nxt + payload_size >= qp->m_size) {
                fst.SetType(FlowStatTag::FLOW_START_AND_END);
            } else if (qp->snd_nxt + payload_size >= qp->m_size) {
                fst.SetType(FlowStatTag::FLOW_END);
            } else if (qp->snd_nxt == 0) {
                fst.SetType(FlowStatTag::FLOW_START);
            } else {
                fst.SetType(FlowStatTag::FLOW_NOTEND);
            }
            packet_pos = fst.GetType();
            fst.setInitiatedTime(Simulator::Now().GetSeconds());
            p->AddPacketTag(fst);
        }
    }


    // if (Irn::mode == Irn::Mode::IRN) {
    //     if (qp->m_irn.m_max_seq < qp->snd_nxt)
		// 			qp->m_irn.m_max_seq = qp->snd_nxt;
    // }

		// update state
		// qp->snd_nxt += payload_size;
		// qp->m_ipid++;

	if (Irn::mode == Irn::Mode::IRN_OPT)
	{
		qp->snd_nxt += payload_size;
		qp->m_irn.m_max_next_seq = qp->m_irn.m_max_next_seq < qp->snd_nxt ? qp->snd_nxt : qp->m_irn.m_max_next_seq;
		qp->m_irn.m_max_seq = qp->m_irn.m_max_seq < qp->snd_nxt ? qp->snd_nxt : qp->m_irn.m_max_seq;
		qp->m_ipid++;
	}
	else if (Irn::mode == Irn::Mode::IRN)
	{
		if (qp->m_irn.m_max_seq < seq)
		{
			qp->m_irn.m_max_seq = seq;
		}
		// update state
		qp->snd_nxt += payload_size;
		qp->m_ipid++;
	}
	else if (Irn::mode == Irn::Mode::NACK)
	{
		
		qp->snd_nxt += payload_size;
		// if(qp->m_flow_id==3816){
		// 	std::cout <<" after Time" << Simulator::Now().GetNanoSeconds()<<" Next:"<<qp->snd_nxt<<std::endl;
		// }
		qp->m_irn.m_max_next_seq = qp->m_irn.m_max_next_seq < qp->snd_nxt ? qp->snd_nxt : qp->m_irn.m_max_next_seq;
		qp->m_ipid++;
	}
	else if (Irn::mode == Irn::Mode::GBN)
	{
		qp->snd_nxt += payload_size;
		qp->m_ipid++;
	}

	else
	{
		NS_ASSERT_MSG(false, "Unknown IRN mode");
	}
		// return
		return p;
	}

		void RdmaHw::SetTimeoutForLaps(Ptr<RdmaQueuePair> qp, uint32_t pid, Time timeInNs) 
	{
		NS_ASSERT_MSG(Irn::mode == Irn::Mode::NACK, "LAPS::NACK should be enabled");
    if (qp->IsFinished()) { return; }
		std::pair<uint32_t, uint32_t> key = std::make_pair(qp->m_flow_id, pid);
		auto it = m_rtoEvents.find(key);
		if (it != m_rtoEvents.end())
		{
			if (it->second.IsRunning ())
			{
				// std::cout << "Time " << Simulator::Now().GetNanoSeconds();
				// std::cout << ", FlowID " << qp->m_flow_id;
				// std::cout << ", PathID " << pid;
				// std::cout << ", **RESET** RTO from " << it->second.GetTs();
				// std::cout << " to " << Simulator::Now().GetNanoSeconds() + timeInNs.GetNanoSeconds();
				// std::cout << std::endl;
				NS_LOG_INFO("Cancel the timeout event that should be triggered at " << it->second.GetTs());
				it->second.Cancel();
			}
			m_rtoEvents[key] = Simulator::Schedule(timeInNs, &RdmaHw::HandleTimeoutForLaps, this, qp, pid);
			NS_LOG_INFO("Reset a exist timeout event that should be triggered at " <<m_rtoEvents[key].GetTs());
		}
		else
		{
			m_rtoEvents[key] = Simulator::Schedule(timeInNs, &RdmaHw::HandleTimeoutForLaps, this, qp, pid);
			NS_LOG_INFO("Initial a new timeout event that should be triggered at " <<m_rtoEvents[key].GetTs());
			// std::cout << "Time " << Simulator::Now().GetNanoSeconds();
			// std::cout << ", FlowID " << qp->m_flow_id;
			// std::cout << ", PathID " << pid;
			// std::cout << " Initial RTO at " << m_rtoEvents[key].GetTs();
			// std::cout << std::endl;
		}

	}


// PATH_TIMEOUT_PROTOCOL_V1: keep the in-flight ledger until a FIFO ghost ACK supplies evidence.
void RdmaHw::TracePathTimeout(const char *event,uint32_t pid,uint64_t detail,uint32_t lost) {
    auto routing=m_E2ErdmaSmartFlowRouting;auto path=routing->lookup_PIT(pid);auto &state=routing->m_earlyProbes[pid];
    if(RdmaSmartFlowRouting::lcPathTimeoutLog)RdmaSmartFlowRouting::lcPathTimeoutLog
        <<event<<','<<Simulator::Now().GetNanoSeconds()<<','<<m_node->GetId()<<','<<pid<<','<<state.protocolToken<<','
        <<state.unavailable<<','<<state.pending<<','<<state.queued<<','<<unsigned(state.timeoutShift)<<','
        <<state.sentNs<<','<<state.nextNs<<','<<path->protocolDelayThresholdNs<<','<<path->protocolAckTimeoutNs<<','<<detail<<','<<lost<<'\n';
}
void RdmaHw::WakePathTimeoutNic() {
    for(auto &nic:m_nic)if(nic.dev)nic.dev->TriggerTransmit();
}
void RdmaHw::ArmPathTimeout(uint32_t pid) {
    auto routing=m_E2ErdmaSmartFlowRouting;auto &state=routing->m_earlyProbes[pid];
    if(state.unavailable)return; // DATA ACK progress cannot overwrite a pending ghost timer.
    auto &event=m_rtoEventsPerPath[pid];event.Cancel();
    auto it=m_outstanding_data.find(pid);if(it==m_outstanding_data.end())return;
    uint64_t oldest=UINT64_MAX;for(const auto &entry:it->second)if(entry.size)oldest=std::min(oldest,entry.sentNs);
    if(oldest==UINT64_MAX)return;
    auto path=routing->lookup_PIT(pid);NS_ABORT_MSG_IF(!path->protocolAckTimeoutNs,"Missing path-specific timeout threshold");
    uint64_t now=Simulator::Now().GetNanoSeconds(),deadline=oldest+path->protocolAckTimeoutNs+1;
    // A newer DATA packet never postpones the deadline of the oldest unacknowledged one.
    event=Simulator::Schedule(NanoSeconds(deadline>now?deadline-now:1),&RdmaHw::HandleTimeoutForLapsPerPath,this,pid);
}
void RdmaHw::EnterPathTimeout(uint32_t pid) {
    auto routing=m_E2ErdmaSmartFlowRouting;auto &state=routing->m_earlyProbes[pid];
    if(!state.unavailable){
        auto &entries=m_outstanding_data[pid];auto first=std::find_if(entries.begin(),entries.end(),[](const OutStandingDataEntry &e){return e.size>0;});
        if(first==entries.end())return;
        state.unavailable=true;state.protocolFlow=first->flow_id;state.timeoutShift=0;state.pending=false;state.queued=true;state.nextNs=Simulator::Now().GetNanoSeconds();
        TracePathTimeout("data_timeout",pid,first->sentNs);
    }else{
        if(state.pending){
            if(state.timeoutShift<62)++state.timeoutShift;
            state.pending=false;TracePathTimeout("ghost_timeout",pid);
        }
        state.queued=true;state.nextNs=Simulator::Now().GetNanoSeconds();TracePathTimeout("retry_ready",pid);
    }
    WakePathTimeoutNic();
}
Ptr<Packet> RdmaHw::MakePathTimeoutProbe(uint32_t port,const bool *paused) {
    if(!RdmaSmartFlowRouting::lcPathTimeout)return nullptr;
    auto routing=m_E2ErdmaSmartFlowRouting;
    for(auto &item:routing->m_earlyProbes){auto &state=item.second;
        if(!state.unavailable || !state.queued || state.pending)continue;
        auto path=routing->lookup_PIT(item.first);if(path->portSequence.empty() || path->portSequence[0]!=port)continue;
        auto found=m_flowId2Qp.find(state.protocolFlow);NS_ABORT_MSG_IF(found==m_flowId2Qp.end(),"Missing protocol flow context");auto qp=found->second;
        if(paused && paused[qp->m_pg])continue; // Ghost uses the DATA priority to preserve per-path FIFO.
        for(const auto &entry:m_outstanding_data[item.first])if(entry.size){auto q=m_flowId2Qp.at(entry.flow_id);NS_ABORT_MSG_IF(q->m_pg!=qp->m_pg,"FIFO ghost requires one DATA priority per path");}
        if(++m_pathTimeoutToken==0)++m_pathTimeoutToken;state.protocolToken=m_pathTimeoutToken;
        CustomHeader ch(CustomHeader::L2_Header|CustomHeader::L3_Header|CustomHeader::L4_Header);
        ch.sip=qp->sip.Get();ch.dip=qp->dip.Get();ch.udp.sport=qp->sport;ch.udp.dport=qp->dport;ch.udp.pg=qp->m_pg;ch.udp.seq=state.protocolToken;
        Ptr<Packet> unused;auto packet=routing->construct_probe_packet(unused,ch,true);
        routing->add_path_tag_by_path_id(packet,item.first);routing->add_probe_tag_by_path_id(packet,item.first);
        Ipv4SmartFlowPathTag tag;packet->PeekPacketTag(tag);routing->update_path_tag(packet,tag);
        state.pending=true;state.queued=false;state.sentNs=Simulator::Now().GetNanoSeconds();
        uint64_t base=path->protocolAckTimeoutNs,limit=UINT64_MAX/4;
        uint64_t wait=state.timeoutShift>=62 || base>(limit>>state.timeoutShift)?limit:(base<<state.timeoutShift);
        state.nextNs=state.sentNs+wait+1;
        m_rtoEventsPerPath[item.first].Cancel();m_rtoEventsPerPath[item.first]=Simulator::Schedule(NanoSeconds(wait+1),&RdmaHw::HandleTimeoutForLapsPerPath,this,item.first);
        // Zero-size entries are FIFO probe barriers, not retransmittable DATA.
        OutStandingDataEntry marker(state.protocolFlow,state.protocolToken,0);marker.sentNs=state.sentNs;m_outstanding_data[item.first].push_back(marker);
        routing->update_PIT_after_probing(path);TracePathTimeout("ghost_send",item.first,wait);return packet;
    }
    return nullptr;
}
int RdmaHw::ReceivePathTimeoutProbeAck(Ptr<Packet> packet,CustomHeader &ch) {
    auto routing=m_E2ErdmaSmartFlowRouting;AckPathTag ack;NS_ABORT_MSG_IF(!packet->PeekPacketTag(ack),"Missing ghost ACK path");
    uint32_t pid=ack.GetPathId(),token=ch.ack.irnNack;auto &state=routing->m_earlyProbes[pid];auto &entries=m_outstanding_data[pid];
    auto marker=std::find_if(entries.begin(),entries.end(),[&](const OutStandingDataEntry &e){return e.size==0 && e.seq==token;});
    if(marker==entries.end()){TracePathTimeout("duplicate_ghost_ack",pid,token);return 0;}
    uint32_t lost=0;
    // The forward ghost shares DATA FIFO priority; earlier received DATA ACKs precede this ACK.
    // Missing DATA before this barrier is now evidence of loss. Never touch entries after it.
    for(auto it=entries.begin();it!=marker;){
        if(it->size){auto qp=m_flowId2Qp.at(it->flow_id);qp->m_irn.m_sack.m_lossy_data.emplace_back(it->seq,it->size);
            decreaseBDPForPath(pid,it->size);RdmaSmartFlowRouting::TraceRecovery("ghost_loss_mark",m_node->GetId(),pid,it->flow_id,it->seq,it->size,0,token,0,0);++lost;}
        it=entries.erase(it);
    }
    entries.erase(marker);
    const bool current=state.unavailable && state.pending && token==state.protocolToken;
    TracePathTimeout(current?"ghost_ack":"old_ghost_ack",pid,ack.GetDelay(),lost);
    if(current){
        m_rtoEventsPerPath[pid].Cancel();state.pending=false;
        auto path=routing->lookup_PIT(pid);path->latency=ack.GetDelay();path->tsGeneration=Simulator::Now();path->sent_pkt_num=0;
        if(ack.GetDelay()<=path->protocolDelayThresholdNs){
            state.unavailable=false;state.queued=false;state.awaitingFresh=false;state.timeoutShift=0;state.nextNs=0;TracePathTimeout("path_recovered",pid,ack.GetDelay());ArmPathTimeout(pid);
        }else{
            state.timeoutShift=0;state.nextNs=Simulator::Now().GetNanoSeconds()+path->protocolAckTimeoutNs+1;
            m_rtoEventsPerPath[pid]=Simulator::Schedule(NanoSeconds(path->protocolAckTimeoutNs+1),&RdmaHw::HandleTimeoutForLapsPerPath,this,pid);
            TracePathTimeout("probe_delay_high",pid,ack.GetDelay());
        }
        for(const auto &entry:m_qpMap){auto qp=entry.second;if(qp->sip.Get()==ch.dip && qp->dip.Get()==ch.sip && !qp->IsFinished())UpdateRateForLapsPlus(qp,ch);}
    }
    WakePathTimeoutNic();return 0;
}

bool RdmaHw::RunPathTimeoutSelfChecks() {
    Irn::mode=Irn::Mode::NACK;
    RdmaSmartFlowRouting::lcPathTimeout=true;RdmaSmartFlowRouting::lcEnabled=true;RdmaSmartFlowRouting::lcBwCc=true;RdmaSmartFlowRouting::lcBwMode=2;
    RdmaSmartFlowRouting::lcMaxLinkQueue=true;RdmaSmartFlowRouting::lcPathOnlyQueue=false;RdmaSmartFlowRouting::enable_laps_plus=true;
    auto node=CreateObject<Node>();auto routing=CreateObject<RdmaSmartFlowRouting>();routing->SetNode(node);auto hw=CreateObject<RdmaHw>();hw->m_node=node;hw->m_E2ErdmaSmartFlowRouting=routing;
    PathData p;p.pid=1;p.nodeIdSequence={node->GetId(),100,101};p.portSequence={1,1};p.protocolDelayThresholdNs=20000;p.protocolAckTimeoutNs=25000;p.theoreticalSmallestLatencyInNs=30000;
    std::map<uint32_t,PathData> pit={{1,p}};routing->set_PIT(pit);routing->m_pathBdpMap[1].currentBdp=2096;
    auto qp=CreateObject<RdmaQueuePair>(3,Ipv4Address("10.0.0.1"),Ipv4Address("10.0.0.2"),10,20);qp->m_flow_id=1;qp->SetSize(100000);m_flowId2Qp[1]=qp;
    auto &state=routing->m_earlyProbes[1];bool ok=true;unsigned count=0;
    auto ck=[&](bool v,const char *name){++count;if(!v){ok=false;std::cout<<"PATH_TIMEOUT_CASE_FAIL "<<name<<'\n';}};
    auto runAt=[&](uint64_t now,std::function<void()> action){auto invoke=+[](std::function<void()> *f){(*f)();};
        Simulator::Schedule(NanoSeconds(now-Simulator::Now().GetNanoSeconds()),invoke,&action);Simulator::Stop(NanoSeconds(now-Simulator::Now().GetNanoSeconds()+1));Simulator::Run();};
    auto ack=[&](uint32_t token,uint64_t delay){auto packet=Create<Packet>();AckPathTag tag;tag.SetPathId(1);tag.SetDelay(delay);packet->AddPacketTag(tag);CustomHeader ch;ch.ack.irnNack=token;return hw->ReceivePathTimeoutProbeAck(packet,ch);};
    auto probe=[&](){bool paused[8]={};return hw->MakePathTimeoutProbe(1,paused);};
    hw->AppendOutStandingDataPerPath(1,OutStandingDataEntry(1,0,1000));
    ck(hw->m_rtoEventsPerPath[1].GetTs()==25001,"first_data_deadline");
    runAt(10000,[&](){hw->AppendOutStandingDataPerPath(1,OutStandingDataEntry(1,1000,1000));ck(hw->m_rtoEventsPerPath[1].GetTs()==25001,"new_data_does_not_postpone_oldest");});
    runAt(25000,[&](){ck(!state.unavailable,"strict_deadline_boundary");});
    runAt(25002,[&](){
        ck(state.unavailable && state.queued && hw->m_outstanding_data[1].size()==2 && qp->m_irn.m_sack.m_lossy_data.empty(),"timeout_blocks_but_keeps_data_ledger");
        bool paused[8]={};paused[3]=true;ck(!hw->MakePathTimeoutProbe(1,paused) && state.queued,"ghost_respects_data_priority_pfc");
        auto packet=probe();CustomHeader ch(CustomHeader::L2_Header|CustomHeader::L3_Header|CustomHeader::L4_Header);packet->PeekHeader(ch);
        ck(ch.udp.pg==3 && ch.udp.seq==state.protocolToken && state.pending && hw->m_outstanding_data[1].back().size==0,"ghost_fifo_priority_and_barrier");
        hw->checkOutstandingDataAndUpdateLossyData(1,1,0,1000);ack(state.protocolToken,10000);
        ck(!state.unavailable && hw->m_outstanding_data[1].empty() && qp->m_irn.m_sack.m_lossy_data.size()==1 && qp->m_irn.m_sack.m_lossy_data.front().first==1000,"ghost_ack_marks_only_missing_data_before_barrier");
        ck(routing->m_pathBdpMap[1].currentBdp==0,"barrier_does_not_consume_bdp");qp->m_irn.m_sack.m_lossy_data.clear();
    });
    runAt(30000,[&](){routing->m_pathBdpMap[1].currentBdp=1048;hw->AppendOutStandingDataPerPath(1,OutStandingDataEntry(1,2000,1000));});
    runAt(55002,[&](){probe();uint32_t token=state.protocolToken;hw->checkOutstandingDataAndUpdateLossyData(1,1,2000,1000);ack(token,15000);
        ck(!state.unavailable && qp->m_irn.m_sack.m_lossy_data.empty(),"delayed_data_ack_avoids_false_retransmission");
        ack(token,10000);ck(!state.unavailable,"duplicate_ghost_ack_is_harmless");});
    runAt(60000,[&](){routing->m_pathBdpMap[1].currentBdp=1048;hw->AppendOutStandingDataPerPath(1,OutStandingDataEntry(1,3000,1000));});
    uint32_t oldToken=0;
    runAt(85002,[&](){probe();oldToken=state.protocolToken;ck(state.nextNs==110003,"initial_probe_wait_is_path_ack_budget");});
    runAt(110004,[&](){
        ck(state.unavailable && state.queued && !state.pending && state.timeoutShift==1 && qp->m_irn.m_sack.m_lossy_data.empty(),"ghost_timeout_keeps_data_and_doubles_wait");
        probe();ck(state.nextNs==160005,"exponential_backoff_next_probe");
        uint32_t latest=state.protocolToken;ack(oldToken,10000);ck(state.unavailable && state.pending && state.protocolToken==latest,"old_probe_ack_cannot_restore_current_round");
        auto link=RdmaSmartFlowRouting::MakeLinkId(100,1);routing->AcceptMaxQueueEvidence(link,false,110004,110004);
        auto result=routing->EvaluateBandwidthCongestion({routing->lookup_PIT(1)},110004);
        ck(result.blocked[0],"shared_link_low_feedback_cannot_clear_path_timeout");
        ack(latest,25000);ck(state.unavailable && !state.pending && state.nextNs==135005,"high_latency_probe_keeps_path_unavailable");
    });
    runAt(135006,[&](){probe();ack(state.protocolToken,10000);ck(!state.unavailable && state.timeoutShift==0,"valid_low_probe_restores_and_resets_backoff");});
    auto &entries=hw->m_outstanding_data[1];entries.clear();RdmaSmartFlowRouting::lcPathTimeout=false;
    hw->AppendOutStandingDataPerPath(1,OutStandingDataEntry(1,4000,1000));ck(hw->m_rtoEventsPerPath[1].GetTs()==Simulator::Now().GetNanoSeconds()+8000000,"disabled_mode_retains_original_8ms_timer");
    Simulator::Destroy();m_flowId2Qp.erase(1);std::cout<<(ok?"PATH_TIMEOUT_SELF_CHECK_PASS":"PATH_TIMEOUT_SELF_CHECK_FAIL")<<" assertions="<<count<<" outstanding_entry_bytes="<<sizeof(OutStandingDataEntry)<<" path_bytes="<<sizeof(PathData)<<" probe_state_bytes="<<sizeof(RdmaSmartFlowRouting::EarlyProbeState)<<'\n';return ok;
}

		void RdmaHw::SetTimeoutForLapsPerPath(uint32_t pid) 
	{
		NS_ASSERT_MSG(Irn::mode == Irn::Mode::NACK, "LAPS::NACK should be enabled");
        if(RdmaSmartFlowRouting::lcPathTimeout){ArmPathTimeout(pid);return;}
		// Time timeInNs = GetRtoTimeForPath(pid) * 200;
		Time timeInNs = MilliSeconds(8);
		auto it = m_rtoEventsPerPath.find(pid);
		if (it != m_rtoEventsPerPath.end())
		{
			if (it->second.IsRunning ())
			{
				// std::cout << "Time " << Simulator::Now().GetNanoSeconds();
				// std::cout << ", PathID " << pid;
				// std::cout << ", **RESET** RTO from " << it->second.GetTs();
				// std::cout << " to " << Simulator::Now().GetNanoSeconds() + timeInNs.GetNanoSeconds();
				// std::cout << std::endl;
				NS_LOG_INFO("Cancel the timeout event that should be triggered at " << it->second.GetTs());
				it->second.Cancel();
			}
			m_rtoEventsPerPath[pid] = Simulator::Schedule(timeInNs, &RdmaHw::HandleTimeoutForLapsPerPath, this, pid);
			NS_LOG_INFO("Reset a exist timeout event that should be triggered at " <<m_rtoEventsPerPath[pid].GetTs());
		}
		else
		{
			m_rtoEventsPerPath[pid] = Simulator::Schedule(timeInNs, &RdmaHw::HandleTimeoutForLapsPerPath, this, pid);
			NS_LOG_INFO("Initial a new timeout event that should be triggered at " <<m_rtoEventsPerPath[pid].GetTs());
			// std::cout << "Time " << Simulator::Now().GetNanoSeconds();
			// std::cout << ", PathID " << pid;
			// std::cout << " Initial RTO at " << m_rtoEventsPerPath[pid].GetTs();
			// std::cout << std::endl;
		}

	}


	void RdmaHw::CancelRtoForPath(Ptr<RdmaQueuePair> qp, uint32_t pathId)
	{
		NS_ASSERT_MSG(Irn::mode == Irn::Mode::NACK, "LAPS::NACK should be enabled");
		std::pair<uint32_t, uint32_t> key = std::make_pair(qp->m_flow_id, pathId);
		auto it = m_rtoEvents.find(key);
		NS_ASSERT_MSG(it != m_rtoEvents.end(), "RTO event should exist");

		if (it->second.IsRunning ())
		{
			NS_LOG_INFO("Cancel the exist timeout event that should be triggered at " << it->second.GetTs());
			// std::cout << "Time " << Simulator::Now().GetNanoSeconds();
			// std::cout << ", FlowID " << qp->m_flow_id;
			// std::cout << ", PathID " << pathId;
			// std::cout << ", **CANCEL** RTO that should be triggered at " << it->second.GetTs();
			// std::cout << std::endl;
			it->second.Cancel();
		}

	}

	void RdmaHw::CancelRtoPerPath(uint32_t pathId)
	{
		NS_ASSERT_MSG(Irn::mode == Irn::Mode::NACK, "LAPS::NACK should be enabled");

		auto it = m_rtoEventsPerPath.find(pathId);
		NS_ASSERT_MSG(it != m_rtoEventsPerPath.end(), "RTO event should exist");

		if (it->second.IsRunning ())
		{
			NS_LOG_INFO("Cancel the exist timeout event that should be triggered at " << it->second.GetTs());
			// std::cout << "Time " << Simulator::Now().GetNanoSeconds();
			// std::cout << ", PathID " << pathId;
			// std::cout << ", **CANCEL** RTO that should be triggered at " << it->second.GetTs();
			// std::cout << std::endl;
			it->second.Cancel();
		}

	}


	Time RdmaHw::GetRtoTimeForPath(uint32_t pathId)
	{
		PathData * pitEntry = m_E2ErdmaSmartFlowRouting->lookup_PIT(pathId);
		NS_ASSERT_MSG(pitEntry != NULL, "PIT entry should exist called by GetRtoTimeForPath");
		return NanoSeconds(pitEntry->latency * 2);
	}

	void RdmaHw::HandleTimeoutForLaps(Ptr<RdmaQueuePair> qp, uint32_t pid) 
	{
		NS_ASSERT_MSG(Irn::mode == Irn::Mode::NACK, "LAPS::NACK should be enabled");
    if (qp->IsFinished()) { return; }
    uint32_t nic_idx = GetNicIdxOfQp(qp);
    Ptr<QbbNetDevice> dev = m_nic[nic_idx].dev;
		RdmaSmartFlowRouting::TraceRecovery("rto_qp",m_node->GetId(),pid,qp->m_flow_id,0,0,0,0,0,0);
		qp->m_irn.m_sack.handleRto(pid);
		dev->TriggerTransmit();		
	}

	void RdmaHw::HandleTimeoutForLapsPerPath(uint32_t pid) 
	{
        if(RdmaSmartFlowRouting::lcPathTimeout){EnterPathTimeout(pid);return;}
        RdmaSmartFlowRouting::TraceRecovery("rto_fire",m_node->GetId(),pid,0,0,0,0,0,0,0);
		NS_ASSERT_MSG(Irn::mode == Irn::Mode::NACK, "LAPS::NACK should be enabled");
		auto it = m_outstanding_data.find(pid);
		NS_ASSERT_MSG(it != m_outstanding_data.end(), "Outstanding data should exist called by HandleTimeoutForLapsPerPath");
		//std::cout << "=====================Path " << pid << " RTO Timeout Handling Start=====================" << std::endl;
		std::list<OutStandingDataEntry> & dataList = it->second;
		std::map<uint32_t, bool> reTxNic;
		auto it2 = dataList.begin();
		bool prefixPrinted = false;
		while (it2 != dataList.end())
		{
			uint32_t flowId = it2->flow_id;
			uint32_t seq = it2->seq;
			uint16_t size = it2->size;
			//std::cout << "FlowID " << flowId <<", PathID=" << pid << ", segment=[" << seq << ", " << seq+size << "] timeout retransmit" << std::endl;
			auto & qp = m_flowId2Qp[flowId];
			NS_ASSERT_MSG(qp != NULL, "QP should exist called by HandleTimeoutForLapsPerPath");
			RdmaSmartFlowRouting::TraceRecovery("rto_mark",m_node->GetId(),pid,flowId,seq,size,0,0,0,0);
			qp->m_irn.m_sack.m_lossy_data.emplace_back(seq, size);
			uint32_t nic_idx = GetNicIdxOfQp(qp);
			reTxNic[nic_idx] = true;
			if (!prefixPrinted)
			{
				// std::cout << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!Time " << Simulator::Now().GetNanoSeconds() << ", Path " << pid << " enter RTO timeout" << std::endl;
				prefixPrinted = true;
			}


            //=================================新增逻辑，减少BDP===================================================
				if (RdmaSmartFlowRouting::enable_laps_plus)
				{
					decreaseBDPForPath(pid, it2->size);
				}
			//std::cout << "FlowID " << flowId <<", PathID=" << pid << ", segment=[" << seq << ", " << seq+size << "]" << std::endl;
			it2 = it->second.erase(it2);
		}
		NS_ASSERT_MSG(reTxNic.size() > 0, "Retransmit NIC should exist called by HandleTimeoutForLapsPerPath");
		NS_ASSERT_MSG(dataList.size() == 0, "Outstanding data should be empty called by HandleTimeoutForLapsPerPath");

		for (auto & it3 : reTxNic)
		{
			Ptr<QbbNetDevice> dev = m_nic[it3.first].dev;
			dev->TriggerTransmit();
		}
	}

	Ptr<RdmaSmartFlowRouting> RdmaHw::GetE2ELapsLBouting()
	{
		return m_E2ErdmaSmartFlowRouting;
	}

	void RdmaHw::PktSent(Ptr<RdmaQueuePair> qp, Ptr<Packet> pkt, Time interframeGap)
	{
		qp->lastPktSize = pkt->GetSize();
		UpdateNextAvail(qp, interframeGap, pkt->GetSize());
	}
	void RdmaHw::LBPktSent(Ptr<RdmaQueuePair> qp, uint32_t pkt_size, Time interframeGap)
	{
		qp->lastPktSize = pkt_size;
		UpdateNextAvail(qp, interframeGap, pkt_size);
	}
	void RdmaHw::UpdateNextAvail(Ptr<RdmaQueuePair> qp, Time interframeGap, uint32_t pkt_size)
	{
		Time sendingTime;
		// std::cout << "Seconds(qp->m_rate.CalculateTxTime(pkt_size)): " << Seconds(qp->m_rate.CalculateTxTime(pkt_size)) <<"，初始化速率："<<qp->m_rate <<std::endl;
		 if (m_cc_mode == CongestionControlMode::CC_LAPS&&RdmaSmartFlowRouting::enable_laps_plus)
         {
        // LAPS 特定的发送时间计算逻辑
        // 在LAPS模式下，使用laps.m_curRate而不是m_rate
        sendingTime = interframeGap + Seconds(qp->laps.m_curRate.CalculateTxTime(pkt_size));
        // std::cout << "LAPS: Seconds(qp->laps.m_curRate.CalculateTxTime(pkt_size)): " << Seconds(qp->laps.m_curRate.CalculateTxTime(pkt_size)) <<"，LAPS当前速率："<<qp->laps.m_curRate <<std::endl;
        }
    else  if (m_rateBound){
			sendingTime = interframeGap + Seconds(qp->m_rate.CalculateTxTime(pkt_size));
          // std::cout << "Seconds(qp->m_rate.CalculateTxTime(pkt_size)): " << Seconds(qp->m_rate.CalculateTxTime(pkt_size)) <<"，初始化速率："<<qp->m_rate <<std::endl;
		
		}
			
		else{
			sendingTime = interframeGap + Seconds(qp->m_max_rate.CalculateTxTime(pkt_size));
			// std::cout << "Seconds(qp->m_max_rate.CalculateTxTime(pkt_size)): " << Seconds(qp->m_rate.CalculateTxTime(pkt_size)) <<"，初始化速率："<<qp->m_rate <<std::endl;
		

		}
			
		qp->m_nextAvail = Simulator::Now() + sendingTime;
		if (qp->m_nextAvail > Seconds(15))
		{
			std::cout << "sendingTime:" << sendingTime.GetSeconds() << " interframeGap:" << interframeGap.GetSeconds() << " rate:" << qp->m_rate.GetBitRate() << " maxRate:" << qp->m_max_rate.GetBitRate() << std::endl;
		}
		
	}

	void RdmaHw::ChangeRate(Ptr<RdmaQueuePair> qp, DataRate new_rate)
	{
#if 1
		Time sendingTime = Seconds(qp->m_rate.CalculateTxTime(qp->lastPktSize));
		Time new_sendintTime = Seconds(new_rate.CalculateTxTime(qp->lastPktSize));
		qp->m_nextAvail = qp->m_nextAvail + new_sendintTime - sendingTime;
		// update nic's next avail event
		uint32_t nic_idx = GetNicIdxOfQp(qp);
		m_nic[nic_idx].dev->UpdateNextAvail(qp->m_nextAvail);
#endif

		// change to new rate
		qp->TraceRate(new_rate);
		qp->m_rate = new_rate;
	}

#define PRINT_LOG 0
	/******************************
	 * Mellanox's version of DCQCN
	 *****************************/
	void RdmaHw::UpdateAlphaMlx(Ptr<RdmaQueuePair> q)
	{
#if PRINT_LOG
// std::cout << Simulator::Now() << " alpha update:" << m_node->GetId() << ' ' << q->mlx.m_alpha << ' ' << (int)q->mlx.m_alpha_cnp_arrived << '\n';
// printf("%lu alpha update: %08x %08x %u %u %.6lf->", Simulator::Now().GetTimeStep(), q->sip.Get(), q->dip.Get(), q->sport, q->dport, q->mlx.m_alpha);
#endif
		if (q->mlx.m_alpha_cnp_arrived)
		{
			q->mlx.m_alpha = (1 - m_g) * q->mlx.m_alpha + m_g; // binary feedback
		}
		else
		{
			q->mlx.m_alpha = (1 - m_g) * q->mlx.m_alpha; // binary feedback
		}
#if PRINT_LOG
// printf("%.6lf\n", q->mlx.m_alpha);
#endif
		q->mlx.m_alpha_cnp_arrived = false; // clear the CNP_arrived bit
		ScheduleUpdateAlphaMlx(q);
	}
	void RdmaHw::ScheduleUpdateAlphaMlx(Ptr<RdmaQueuePair> q)
	{
		q->mlx.m_eventUpdateAlpha = Simulator::Schedule(MicroSeconds(m_alpha_resume_interval), &RdmaHw::UpdateAlphaMlx, this, q);
	}

	void RdmaHw::cnp_received_mlx(Ptr<RdmaQueuePair> q)
	{
		q->mlx.m_alpha_cnp_arrived = true;	  // set CNP_arrived bit for alpha update
		q->mlx.m_decrease_cnp_arrived = true; // set CNP_arrived bit for rate decrease
		if (q->mlx.m_first_cnp)
		{
			// init alpha
			q->mlx.m_alpha = 1;
			q->mlx.m_alpha_cnp_arrived = false;
			// schedule alpha update
			ScheduleUpdateAlphaMlx(q);
			// schedule rate decrease
			ScheduleDecreaseRateMlx(q, 1); // add 1 ns to make sure rate decrease is after alpha update
			// set rate on first CNP
			q->TraceRate(m_rateOnFirstCNP * q->m_rate);
			q->mlx.m_targetRate = q->m_rate = m_rateOnFirstCNP * q->m_rate;
			q->mlx.m_first_cnp = false;
		}
	}

	void RdmaHw::CheckRateDecreaseMlx(Ptr<RdmaQueuePair> q)
	{
		ScheduleDecreaseRateMlx(q, 0);
		if (q->mlx.m_decrease_cnp_arrived)
		{
#if PRINT_LOG
			printf("%lu rate dec: %08x %08x %u %u (%0.3lf %.3lf)->", Simulator::Now().GetTimeStep(), q->sip.Get(), q->dip.Get(), q->sport, q->dport, q->mlx.m_targetRate.GetBitRate() * 1e-9, q->m_rate.GetBitRate() * 1e-9);
#endif
			bool clamp = true;
			if (!m_EcnClampTgtRate)
			{
				if (q->mlx.m_rpTimeStage == 0)
					clamp = false;
			}
			if (clamp)
				q->mlx.m_targetRate = q->m_rate;
			if (ENABLE_CCMODE_TEST)
			{
				uint64_t currrate = q->m_rate.GetBitRate() / 8 / 1000000;
				Time now = Simulator::Now();
				RecordCcmodeOutEntry m_saveRecordEntry;
				std::string flowId = ipv4Address2string(q->sip) + "#" + ipv4Address2string(q->dip) + "#" + std::to_string(q->sport);
				if (ccmodeOutInfo[m_node->GetId()][flowId].find(now.GetMicroSeconds()) != ccmodeOutInfo[m_node->GetId()][flowId].end())
				{
					m_saveRecordEntry = ccmodeOutInfo[m_node->GetId()][flowId][now.GetMicroSeconds()];
				}
				m_saveRecordEntry.currdatarate = currrate;
				m_saveRecordEntry.nextdatarate = std::max(m_minRate, q->m_rate * (1 - q->mlx.m_alpha / 2)).GetBitRate() / 8 / 1000000;
				ccmodeOutInfo[m_node->GetId()][flowId][now.GetMicroSeconds()] = m_saveRecordEntry;
			}
			q->TraceRate(std::max(m_minRate, q->m_rate * (1 - q->mlx.m_alpha / 2)));
			q->m_rate = std::max(m_minRate, q->m_rate * (1 - q->mlx.m_alpha / 2));
            insertRateRecord(q->m_flow_id, q->m_rate.GetBitRate()/1000000/8);
            //std::cout << "RdmaHw::CheckRateDecreaseMlx: " << Simulator::Now().GetNanoSeconds() <<  std::endl;
			
			// reset rate increase related things
			q->mlx.m_rpTimeStage = 0;
			q->mlx.m_decrease_cnp_arrived = false;
			Simulator::Cancel(q->mlx.m_rpTimer);
			q->mlx.m_rpTimer = Simulator::Schedule(MicroSeconds(m_rpgTimeReset), &RdmaHw::RateIncEventTimerMlx, this, q);
#if PRINT_LOG
			printf("(%.3lf %.3lf)\n", q->mlx.m_targetRate.GetBitRate() * 1e-9, q->m_rate.GetBitRate() * 1e-9);
#endif
		}
	}
	void RdmaHw::ScheduleDecreaseRateMlx(Ptr<RdmaQueuePair> q, uint32_t delta)
	{
		q->mlx.m_eventDecreaseRate = Simulator::Schedule(MicroSeconds(m_rateDecreaseInterval) + NanoSeconds(delta), &RdmaHw::CheckRateDecreaseMlx, this, q);
	}

	void RdmaHw::RateIncEventTimerMlx(Ptr<RdmaQueuePair> q)
	{
		q->mlx.m_rpTimer = Simulator::Schedule(MicroSeconds(m_rpgTimeReset), &RdmaHw::RateIncEventTimerMlx, this, q);
		RateIncEventMlx(q);
		q->mlx.m_rpTimeStage++;
	}
	void RdmaHw::RateIncEventMlx(Ptr<RdmaQueuePair> q)
	{
		// check which increase phase: fast recovery, active increase, hyper increase
		if (q->mlx.m_rpTimeStage < m_rpgThreshold)
		{ // fast recovery
			FastRecoveryMlx(q);
		}
		else if (q->mlx.m_rpTimeStage == m_rpgThreshold)
		{ // active increase
			ActiveIncreaseMlx(q);
		}
		else
		{ // hyper increase
			HyperIncreaseMlx(q);
		}
	}

	void RdmaHw::FastRecoveryMlx(Ptr<RdmaQueuePair> q)
	{
#if PRINT_LOG
		printf("%lu fast recovery: %08x %08x %u %u (%0.3lf %.3lf)->", Simulator::Now().GetTimeStep(), q->sip.Get(), q->dip.Get(), q->sport, q->dport, q->mlx.m_targetRate.GetBitRate() * 1e-9, q->m_rate.GetBitRate() * 1e-9);
#endif
		q->TraceRate((q->m_rate / 2) + (q->mlx.m_targetRate / 2));
		q->m_rate = (q->m_rate / 2) + (q->mlx.m_targetRate / 2);
		insertRateRecord(q->m_flow_id, q->m_rate.GetBitRate()/1000000/8);
		//std::cout << "RdmaHw::FastRecoveryMlx: " << Simulator::Now().GetNanoSeconds() << "   "<<q->m_flow_id<< std::endl;
#if PRINT_LOG
		printf("(%.3lf %.3lf)\n", q->mlx.m_targetRate.GetBitRate() * 1e-9, q->m_rate.GetBitRate() * 1e-9);
#endif
	}
	void RdmaHw::ActiveIncreaseMlx(Ptr<RdmaQueuePair> q)
	{
#if PRINT_LOG
		printf("%lu active inc: %08x %08x %u %u (%0.3lf %.3lf)->", Simulator::Now().GetTimeStep(), q->sip.Get(), q->dip.Get(), q->sport, q->dport, q->mlx.m_targetRate.GetBitRate() * 1e-9, q->m_rate.GetBitRate() * 1e-9);
#endif
		// get NIC
		uint32_t nic_idx = GetNicIdxOfQp(q);
		Ptr<QbbNetDevice> dev = m_nic[nic_idx].dev;
		// increate rate
		q->mlx.m_targetRate += m_rai;
		if (q->mlx.m_targetRate > dev->GetDataRate())
			q->mlx.m_targetRate = dev->GetDataRate();
		q->TraceRate((q->m_rate / 2) + (q->mlx.m_targetRate / 2));
		q->m_rate = (q->m_rate / 2) + (q->mlx.m_targetRate / 2);
		insertRateRecord(q->m_flow_id, q->m_rate.GetBitRate()/1000000/8);
#if PRINT_LOG
		printf("(%.3lf %.3lf)\n", q->mlx.m_targetRate.GetBitRate() * 1e-9, q->m_rate.GetBitRate() * 1e-9);
#endif
	}
	void RdmaHw::HyperIncreaseMlx(Ptr<RdmaQueuePair> q)
	{
#if PRINT_LOG
		printf("%lu hyper inc: %08x %08x %u %u (%0.3lf %.3lf)->", Simulator::Now().GetTimeStep(), q->sip.Get(), q->dip.Get(), q->sport, q->dport, q->mlx.m_targetRate.GetBitRate() * 1e-9, q->m_rate.GetBitRate() * 1e-9);
#endif
		// get NIC
		uint32_t nic_idx = GetNicIdxOfQp(q);
		Ptr<QbbNetDevice> dev = m_nic[nic_idx].dev;
		// increate rate
		q->mlx.m_targetRate += m_rhai;
		if (q->mlx.m_targetRate > dev->GetDataRate())
			q->mlx.m_targetRate = dev->GetDataRate();
		q->TraceRate((q->m_rate / 2) + (q->mlx.m_targetRate / 2));
		q->m_rate = (q->m_rate / 2) + (q->mlx.m_targetRate / 2);
		insertRateRecord(q->m_flow_id, q->m_rate.GetBitRate()/1000000/8);
#if PRINT_LOG
		printf("(%.3lf %.3lf)\n", q->mlx.m_targetRate.GetBitRate() * 1e-9, q->m_rate.GetBitRate() * 1e-9);
#endif
	}

	/***********************
	 * High Precision CC
	 ***********************/
	void RdmaHw::HandleAckHp(Ptr<RdmaQueuePair> qp, Ptr<Packet> p, CustomHeader &ch)
	{
		uint32_t ack_seq = ch.ack.seq;
		// update rate
		if (ack_seq > qp->hp.m_lastUpdateSeq)
		{ // if full RTT feedback is ready, do full update
			UpdateRateHp(qp, p, ch, false);
		}
		else
		{ // do fast react
			FastReactHp(qp, p, ch);
		}
	}

	void RdmaHw::UpdateRateHp(Ptr<RdmaQueuePair> qp, Ptr<Packet> p, CustomHeader &ch, bool fast_react)
	{
		uint32_t next_seq = qp->snd_nxt;
#if PRINT_LOG
		bool print = !fast_react || true;
#endif
		if (qp->hp.m_lastUpdateSeq == 0)
		{ // first RTT
			qp->hp.m_lastUpdateSeq = next_seq;
			// store INT
			IntHeader &ih = ch.ack.ih;
			NS_ASSERT(ih.nhop <= IntHeader::maxHop);
			for (uint32_t i = 0; i < ih.nhop; i++)
				qp->hp.hop[i] = ih.hop[i];
#if PRINT_LOG
			if (print)
			{
				printf("%lu %s %08x %08x %u %u [%u,%u,%u]", Simulator::Now().GetTimeStep(), fast_react ? "fast" : "update", qp->sip.Get(), qp->dip.Get(), qp->sport, qp->dport, qp->hp.m_lastUpdateSeq, ch.ack.seq, next_seq);
				for (uint32_t i = 0; i < ih.nhop; i++)
					printf(" %u %lu %lu", ih.hop[i].GetQlen(), ih.hop[i].GetBytes(), ih.hop[i].GetTime());
				printf("\n");
			}
#endif
		}
		else
		{
			// check packet INT
			IntHeader &ih = ch.ack.ih;
			if (ih.nhop <= IntHeader::maxHop)
			{
				double max_c = 0;
// bool inStable = false;
#if PRINT_LOG
				if (print)
					printf("%lu %s %08x %08x %u %u [%u,%u,%u]", Simulator::Now().GetTimeStep(), fast_react ? "fast" : "update", qp->sip.Get(), qp->dip.Get(), qp->sport, qp->dport, qp->hp.m_lastUpdateSeq, ch.ack.seq, next_seq);
#endif
				// check each hop
				double U = 0;
				uint64_t dt = 0;
				bool updated[IntHeader::maxHop] = {false}, updated_any = false;
				NS_ASSERT(ih.nhop <= IntHeader::maxHop);
				for (uint32_t i = 0; i < ih.nhop; i++)
				{
					if (m_sampleFeedback)
					{
						if (ih.hop[i].GetQlen() == 0 && fast_react)
							continue;
					}
					updated[i] = updated_any = true;
#if PRINT_LOG
					if (print)
						printf(" %u(%u) %lu(%lu) %lu(%lu)", ih.hop[i].GetQlen(), qp->hp.hop[i].GetQlen(), ih.hop[i].GetBytes(), qp->hp.hop[i].GetBytes(), ih.hop[i].GetTime(), qp->hp.hop[i].GetTime());
#endif
					uint64_t tau = ih.hop[i].GetTimeDelta(qp->hp.hop[i]);
					;
					double duration = tau * 1e-9;
					double txRate = (ih.hop[i].GetBytesDelta(qp->hp.hop[i])) * 8 / duration;
					double u = txRate / ih.hop[i].GetLineRate() + (double)std::min(ih.hop[i].GetQlen(), qp->hp.hop[i].GetQlen()) * qp->m_max_rate.GetBitRate() / ih.hop[i].GetLineRate() / qp->m_win;
#if PRINT_LOG
					if (print)
						printf(" %.3lf %.3lf", txRate, u);
#endif
					if (!m_multipleRate)
					{
						// for aggregate (single R)
						if (u > U)
						{
							U = u;
							dt = tau;
						}
					}
					else
					{
						// for per hop (per hop R)
						if (tau > qp->m_baseRtt)
							tau = qp->m_baseRtt;
						qp->hp.hopState[i].u = (qp->hp.hopState[i].u * (qp->m_baseRtt - tau) + u * tau) / double(qp->m_baseRtt);
					}
					qp->hp.hop[i] = ih.hop[i];
				}

				DataRate new_rate;
				int32_t new_incStage;
				DataRate new_rate_per_hop[IntHeader::maxHop];
				int32_t new_incStage_per_hop[IntHeader::maxHop];
				if (!m_multipleRate)
				{
					// for aggregate (single R)
					if (updated_any)
					{
						if (dt > qp->m_baseRtt)
							dt = qp->m_baseRtt;
						qp->hp.u = (qp->hp.u * (qp->m_baseRtt - dt) + U * dt) / double(qp->m_baseRtt);
						max_c = qp->hp.u / m_targetUtil;

						if (max_c >= 1 || qp->hp.m_incStage >= m_miThresh)
						{
							new_rate = qp->hp.m_curRate / max_c + m_rai;
							new_incStage = 0;
						}
						else
						{
							new_rate = qp->hp.m_curRate + m_rai;
							new_incStage = qp->hp.m_incStage + 1;
						}
						if (new_rate < m_minRate)
							new_rate = m_minRate;
						if (new_rate > qp->m_max_rate)
							new_rate = qp->m_max_rate;
#if PRINT_LOG
						if (print)
							printf(" u=%.6lf U=%.3lf dt=%u max_c=%.3lf", qp->hp.u, U, dt, max_c);
#endif
#if PRINT_LOG
						if (print)
							printf(" rate:%.3lf->%.3lf\n", qp->hp.m_curRate.GetBitRate() * 1e-9, new_rate.GetBitRate() * 1e-9);
#endif
					}
				}
				else
				{
					// for per hop (per hop R)
					new_rate = qp->m_max_rate;
					for (uint32_t i = 0; i < ih.nhop; i++)
					{
						if (updated[i])
						{
							double c = qp->hp.hopState[i].u / m_targetUtil;
							if (c >= 1 || qp->hp.hopState[i].incStage >= m_miThresh)
							{
								new_rate_per_hop[i] = qp->hp.hopState[i].Rc / c + m_rai;
								new_incStage_per_hop[i] = 0;
							}
							else
							{
								new_rate_per_hop[i] = qp->hp.hopState[i].Rc + m_rai;
								new_incStage_per_hop[i] = qp->hp.hopState[i].incStage + 1;
							}
							// bound rate
							if (new_rate_per_hop[i] < m_minRate)
								new_rate_per_hop[i] = m_minRate;
							if (new_rate_per_hop[i] > qp->m_max_rate)
								new_rate_per_hop[i] = qp->m_max_rate;
							// find min new_rate
							if (new_rate_per_hop[i] < new_rate)
								new_rate = new_rate_per_hop[i];
#if PRINT_LOG
							if (print)
								printf(" [%u]u=%.6lf c=%.3lf", i, qp->hp.hopState[i].u, c);
#endif
#if PRINT_LOG
							if (print)
								printf(" %.3lf->%.3lf", qp->hp.hopState[i].Rc.GetBitRate() * 1e-9, new_rate.GetBitRate() * 1e-9);
#endif
						}
						else
						{
							if (qp->hp.hopState[i].Rc < new_rate)
								new_rate = qp->hp.hopState[i].Rc;
						}
					}
#if PRINT_LOG
					printf("\n");
#endif
				}
				if (updated_any)
					ChangeRate(qp, new_rate);
				if (!fast_react)
				{
					if (updated_any)
					{
						qp->hp.m_curRate = new_rate;
						qp->hp.m_incStage = new_incStage;
					}
					if (m_multipleRate)
					{
						// for per hop (per hop R)
						for (uint32_t i = 0; i < ih.nhop; i++)
						{
							if (updated[i])
							{
								qp->hp.hopState[i].Rc = new_rate_per_hop[i];
								qp->hp.hopState[i].incStage = new_incStage_per_hop[i];
							}
						}
					}
				}
			}
			if (!fast_react)
			{
				if (next_seq > qp->hp.m_lastUpdateSeq)
					qp->hp.m_lastUpdateSeq = next_seq; //+ rand() % 2 * m_mtu;
			}
		}
	}

	void RdmaHw::FastReactHp(Ptr<RdmaQueuePair> qp, Ptr<Packet> p, CustomHeader &ch)
	{
		if (m_fast_react)
			UpdateRateHp(qp, p, ch, true);
	}

	/**********************
	 * TIMELY
	 *********************/
	void RdmaHw::HandleAckTimely(Ptr<RdmaQueuePair> qp, Ptr<Packet> p, CustomHeader &ch)
	{
		uint32_t ack_seq = ch.ack.seq;
		// update rate
		if (ack_seq > qp->tmly.m_lastUpdateSeq)
		{ // if full RTT feedback is ready, do full update
			UpdateRateTimely(qp, p, ch, false);
		}
		else
		{ // do fast react
			FastReactTimely(qp, p, ch);
		}
	}
	void RdmaHw::UpdateRateTimely(Ptr<RdmaQueuePair> qp, Ptr<Packet> p, CustomHeader &ch, bool us)
	{
		uint32_t next_seq = qp->snd_nxt;
		uint64_t rtt = Simulator::Now().GetTimeStep() - ch.ack.ih.ts;
#if PRINT_LOG
		bool print = !us;
#endif
		if (qp->tmly.m_lastUpdateSeq != 0)
		{ // not first RTT
			int64_t new_rtt_diff = (int64_t)rtt - (int64_t)qp->tmly.lastRtt;
			double rtt_diff = (1 - m_tmly_alpha) * qp->tmly.rttDiff + m_tmly_alpha * new_rtt_diff;
			double gradient = rtt_diff / m_tmly_minRtt;
			bool inc = false;
			double c = 0;
#if PRINT_LOG
			if (print)
				printf("%lu node:%u rtt:%lu rttDiff:%.0lf gradient:%.3lf rate:%.3lf", Simulator::Now().GetTimeStep(), m_node->GetId(), rtt, rtt_diff, gradient, qp->tmly.m_curRate.GetBitRate() * 1e-9);
#endif
			if (rtt < m_tmly_TLow)
			{
				inc = true;
			}
			else if (rtt > m_tmly_THigh)
			{
				c = 1 - m_tmly_beta * (1 - (double)m_tmly_THigh / rtt);
				inc = false;
			}
			else if (gradient <= 0)
			{
				inc = true;
			}
			else
			{
				c = 1 - m_tmly_beta * gradient;
				if (c < 0)
					c = 0;
				inc = false;
			}
			if (inc)
			{
				if (qp->tmly.m_incStage < 5)
				{
					qp->TraceRate(qp->tmly.m_curRate + m_rai);
					qp->m_rate = qp->tmly.m_curRate + m_rai;
				}
				else
				{
					qp->TraceRate(qp->tmly.m_curRate + m_rhai);
					qp->m_rate = qp->tmly.m_curRate + m_rhai;
				}
				if (qp->m_rate > qp->m_max_rate)
				{

					qp->TraceRate(qp->m_max_rate);
					qp->m_rate = qp->m_max_rate;
				}
				if (!us)
				{
					qp->tmly.m_curRate = qp->m_rate;
					qp->tmly.m_incStage++;
					qp->tmly.rttDiff = rtt_diff;
				}
			}
			else
			{
				qp->TraceRate(std::max(m_minRate, qp->tmly.m_curRate * c));
				qp->m_rate = std::max(m_minRate, qp->tmly.m_curRate * c);
				if (!us)
				{
					qp->tmly.m_curRate = qp->m_rate;
					qp->tmly.m_incStage = 0;
					qp->tmly.rttDiff = rtt_diff;
				}
			}
#if PRINT_LOG
			if (print)
			{
				printf(" %c %.3lf\n", inc ? '^' : 'v', qp->m_rate.GetBitRate() * 1e-9);
			}
#endif
		}
		if (!us && next_seq > qp->tmly.m_lastUpdateSeq)
		{
			qp->tmly.m_lastUpdateSeq = next_seq;
			// update
			qp->tmly.lastRtt = rtt;
		}
	}
	void RdmaHw::FastReactTimely(Ptr<RdmaQueuePair> qp, Ptr<Packet> p, CustomHeader &ch)
	{
	}

	void RdmaHw::plb_update_state_upon_rto(Ptr<RdmaQueuePair> qp)
	{
		NS_LOG_INFO("*****plb_update_state_upon_rto******");
		std::string flowId = qp->GetStringHashValueFromQp();
		PlbEntry *plb_entry = lookup_PlbEntry(flowId);
		Ptr<UniformRandomVariable> x = CreateObject<UniformRandomVariable>();
		uint32_t pauseTimeInseconds = x->GetValue(0, PLB_SUSPEND_RTO_SEC) + PLB_SUSPEND_RTO_SEC; //(1-2)*PLB_SUSPEND_RTO_SEC
		plb_entry->pause_until = Simulator::Now() + Seconds(pauseTimeInseconds);
		/* Reset PLB state upon RTO, since an RTO causes a PlbRehash call
		 * that may switch this connection to a path with completely different
		 * congestion characteristics.
		 */
		plb_entry->congested_rounds = 0;
	}
	void RdmaHw::plbCheckRehash(Ptr<RdmaQueuePair> qp)
	{
		NS_LOG_INFO("*****plbCheckRehash******");
		Time now = Simulator::Now();
		std::string flowId = qp->GetStringHashValueFromQp();
		NS_LOG_INFO("stringhash is " << flowId << "Nowtime: " << now.GetSeconds());
		PlbEntry *plb_entry = lookup_PlbEntry(flowId);
		bool can_idle_rehash = (plb_entry->congested_rounds >= IDLE_REHASH_ROUNDS) && (qp->GetOnTheFly() == 0);
		bool can_force_rehash = plb_entry->congested_rounds >= PLB_REHASH_ROUNDS;
		bool IsNotPause = !(plb_entry->pause_until.GetSeconds() && 1);
		NS_LOG_INFO("plb_entry->pause_until.GetSeconds(): " << plb_entry->pause_until.GetSeconds());
		if (plb_entry->pause_until.GetSeconds() && (now.GetSeconds() >= plb_entry->pause_until.GetSeconds()))
		{
			IsNotPause = true;
			plb_entry->pause_until = Seconds(0);
		}
		NS_LOG_INFO("PLB status: " << "can_idle_rehash: " << can_idle_rehash << " can_force_rehash: " << can_force_rehash << " IsNotPause: " << IsNotPause);
		NS_LOG_INFO("can_idle_rehash: plb_entry->congested_rounds: " << plb_entry->congested_rounds << " qp->GetOnTheFly(): " << qp->GetOnTheFly());
		PlbRecordEntry record_outinfo;
		record_outinfo.flowID = flowId;
		record_outinfo.congested_rounds = plb_entry->congested_rounds;
		record_outinfo.pkts_in_flight = qp->GetOnTheFly();
		//IsNotPause = true;
		if (IsNotPause && (can_idle_rehash || can_force_rehash))
		{
			Ptr<UniformRandomVariable> x = CreateObject<UniformRandomVariable>();
			// qp->flowRandNum = x->GetValue(0, 65536);
			plb_entry->randomNum = x->GetValue(0, 65536);
			plb_entry->congested_rounds = 0;
			NS_LOG_INFO("UpdateRehash,randomNum: " << plb_entry->randomNum);
			record_outinfo.randomNum=plb_entry->randomNum;
		}
		m_plbRecordOutInf[m_node->GetId()][now.GetMilliSeconds()] = record_outinfo;
		return;
	}
	uint32_t RdmaHw::GetFlowIdRehashNum(std::string flowId)
	{
		// NS_LOG_INFO ("############ Fucntion: lookup_PLB() ############");

		std::map<std::string, PlbEntry>::iterator it;
		it = m_plbtable.find(flowId);
		if (it == m_plbtable.end())
		{
			size_t last_hash_pos = flowId.find_last_of('#');
			if (last_hash_pos == std::string::npos)
			{
				throw std::invalid_argument("No '#' found in the string.");
			}
			std::string portNumberStr = flowId.substr(last_hash_pos + 1);
			int portNumber = std::stoi(portNumberStr);
			// FLOW START PORT NUM IS 666
			if (portNumber > 665)
			{
				// std::cout << "Error in lookup_PLB() since Cannot match any entry in PLB TABLE for the Key: (";
				// std::cout << flowId;
			}

			return 0;
		}
		else
		{
			return it->second.randomNum;
		}
	}

	PlbEntry *RdmaHw::lookup_PlbEntry(std::string flowId)
	{
		// NS_LOG_INFO ("############ Fucntion: lookup_PLB() ############");

		std::map<std::string, PlbEntry>::iterator it;
		it = m_plbtable.find(flowId);
		if (it == m_plbtable.end())
		{
			// std::cout << "Error in lookup_PLB() since Cannot match any entry in PLB TABLE for the Key: (";
			// std::cout << flowId;
			NS_LOG_INFO("******cannt find any entry flowId: " << flowId);
			return 0;
		}
		else
		{
			return &(it->second);
		}
	}
	void RdmaHw::PlbUpdateState(Ptr<RdmaQueuePair> qp)
	{
		NS_LOG_INFO("******PlbUpdateState******" << std::endl);
		std::string flowId = qp->GetStringHashValueFromQp();
		NS_LOG_INFO("stringhash is " << flowId << std::endl);
		PlbEntry *plb_entry = lookup_PlbEntry(flowId);
		NS_LOG_INFO("qp->dctcp.m_ecnCnt: " << qp->dctcp.m_ecnCnt << " ,qp->dctcp.m_batchSizeOfAlpha: " << qp->dctcp.m_batchSizeOfAlpha);
		double frac = std::min(1.0, double(qp->dctcp.m_ecnCnt) / qp->dctcp.m_batchSizeOfAlpha);
		if (frac >= 0.5)
		{
			plb_entry->congested_rounds += 1;
		}
		else
		{
			plb_entry->congested_rounds = 0;
		}
		return;
	}

    /**********************
	 * PLB_DCTCP
	 *********************/
	void RdmaHw::PLBHandleAckDctcp(Ptr<RdmaQueuePair> qp, Ptr<Packet> p, CustomHeader &ch)
	{
		uint32_t ack_seq = ch.ack.seq;
		uint8_t cnp = (ch.ack.flags >> qbbHeader::FLAG_CNP) & 1;
		bool new_batch = false;

		// update alpha
		qp->dctcp.m_ecnCnt += (cnp > 0);
		if (ack_seq > qp->dctcp.m_lastUpdateSeq)
		{ // if full RTT feedback is ready, do alpha update
#if PRINT_LOG
			printf("%lu %s %08x %08x %u %u [%u,%u,%u] %.3lf->", Simulator::Now().GetTimeStep(), "alpha", qp->sip.Get(), qp->dip.Get(), qp->sport, qp->dport, qp->dctcp.m_lastUpdateSeq, ch.ack.seq, qp->snd_nxt, qp->dctcp.m_alpha);
#endif
			new_batch = true;
			if (qp->dctcp.m_lastUpdateSeq == 0)
			{ // first RTT
				qp->dctcp.m_lastUpdateSeq = qp->snd_nxt;
				qp->dctcp.m_batchSizeOfAlpha = qp->snd_nxt / m_mtu + 1;
			}
			else
			{
				PlbUpdateState(qp);
				plbCheckRehash(qp);
				double frac = std::min(1.0, double(qp->dctcp.m_ecnCnt) / qp->dctcp.m_batchSizeOfAlpha);
				qp->dctcp.m_alpha = (1 - m_g) * qp->dctcp.m_alpha + m_g * frac;
				qp->dctcp.m_lastUpdateSeq = qp->snd_nxt;
				qp->dctcp.m_ecnCnt = 0;
				qp->dctcp.m_batchSizeOfAlpha = (qp->snd_nxt - ack_seq) / m_mtu + 1;
#if PRINT_LOG
				printf("%.3lf F:%.3lf", qp->dctcp.m_alpha, frac);
#endif
			}
#if PRINT_LOG
			printf("\n");

#endif
		}

		// check cwr exit
		if (qp->dctcp.m_caState == 1)
		{
			if (ack_seq > qp->dctcp.m_highSeq)
				qp->dctcp.m_caState = 0;
		}

		// check if need to reduce rate: ECN and not in CWR
		if (cnp && qp->dctcp.m_caState == 0)
		{
#if PRINT_LOG
			printf("%lu %s %08x %08x %u %u %.3lf->", Simulator::Now().GetTimeStep(), "rate", qp->sip.Get(), qp->dip.Get(), qp->sport, qp->dport, qp->m_rate.GetBitRate() * 1e-9);
#endif
			qp->TraceRate(std::max(m_minRate, qp->m_rate * (1 - qp->dctcp.m_alpha / 2)));
			qp->m_rate = std::max(m_minRate, qp->m_rate * (1 - qp->dctcp.m_alpha / 2));
			insertRateRecord(qp->m_flow_id, qp->m_rate.GetBitRate()/1000000/8);
#if PRINT_LOG
			printf("%.3lf\n", qp->m_rate.GetBitRate() * 1e-9);
#endif
			qp->dctcp.m_caState = 1;
			qp->dctcp.m_highSeq = qp->snd_nxt;
		}

		// additive inc
		if (qp->dctcp.m_caState == 0 && new_batch)
		{
			qp->TraceRate(std::min(qp->m_max_rate, qp->m_rate + m_dctcp_rai));
			qp->m_rate = std::min(qp->m_max_rate, qp->m_rate + m_dctcp_rai);
			insertRateRecord(qp->m_flow_id, qp->m_rate.GetBitRate()/1000000/8);
		}
	}

	/**********************
	 * DCTCP
	 *********************/
	void RdmaHw::HandleAckDctcp(Ptr<RdmaQueuePair> qp, Ptr<Packet> p, CustomHeader &ch)
	{
		uint32_t ack_seq = ch.ack.seq;
		uint8_t cnp = (ch.ack.flags >> qbbHeader::FLAG_CNP) & 1;
		bool new_batch = false;

		// update alpha
		qp->dctcp.m_ecnCnt += (cnp > 0);
		if (ack_seq > qp->dctcp.m_lastUpdateSeq)
		{ // if full RTT feedback is ready, do alpha update
#if PRINT_LOG
			printf("%lu %s %08x %08x %u %u [%u,%u,%u] %.3lf->", Simulator::Now().GetTimeStep(), "alpha", qp->sip.Get(), qp->dip.Get(), qp->sport, qp->dport, qp->dctcp.m_lastUpdateSeq, ch.ack.seq, qp->snd_nxt, qp->dctcp.m_alpha);
#endif
			new_batch = true;
			if (qp->dctcp.m_lastUpdateSeq == 0)
			{ // first RTT
				qp->dctcp.m_lastUpdateSeq = qp->snd_nxt;
				qp->dctcp.m_batchSizeOfAlpha = qp->snd_nxt / m_mtu + 1;
			}
			else
			{
				PlbUpdateState(qp);
				plbCheckRehash(qp);
				double frac = std::min(1.0, double(qp->dctcp.m_ecnCnt) / qp->dctcp.m_batchSizeOfAlpha);
				qp->dctcp.m_alpha = (1 - m_g) * qp->dctcp.m_alpha + m_g * frac;
				qp->dctcp.m_lastUpdateSeq = qp->snd_nxt;
				qp->dctcp.m_ecnCnt = 0;
				qp->dctcp.m_batchSizeOfAlpha = (qp->snd_nxt - ack_seq) / m_mtu + 1;
#if PRINT_LOG
				printf("%.3lf F:%.3lf", qp->dctcp.m_alpha, frac);
#endif
			}
#if PRINT_LOG
			printf("\n");

#endif
		}

		// check cwr exit
		if (qp->dctcp.m_caState == 1)
		{
			if (ack_seq > qp->dctcp.m_highSeq)
				qp->dctcp.m_caState = 0;
		}

		// check if need to reduce rate: ECN and not in CWR
		if (cnp && qp->dctcp.m_caState == 0)
		{
#if PRINT_LOG
			printf("%lu %s %08x %08x %u %u %.3lf->", Simulator::Now().GetTimeStep(), "rate", qp->sip.Get(), qp->dip.Get(), qp->sport, qp->dport, qp->m_rate.GetBitRate() * 1e-9);
#endif
			qp->TraceRate(std::max(m_minRate, qp->m_rate * (1 - qp->dctcp.m_alpha / 2)));
			qp->m_rate = std::max(m_minRate, qp->m_rate * (1 - qp->dctcp.m_alpha / 2));
			insertRateRecord(qp->m_flow_id, qp->m_rate.GetBitRate()/1000000/8);
#if PRINT_LOG
			printf("%.3lf\n", qp->m_rate.GetBitRate() * 1e-9);
#endif
			qp->dctcp.m_caState = 1;
			qp->dctcp.m_highSeq = qp->snd_nxt;
		}

		// additive inc
		if (qp->dctcp.m_caState == 0 && new_batch)
		{
			qp->TraceRate(std::min(qp->m_max_rate, qp->m_rate + m_dctcp_rai));
			qp->m_rate = std::min(qp->m_max_rate, qp->m_rate + m_dctcp_rai);
			insertRateRecord(qp->m_flow_id, qp->m_rate.GetBitRate()/1000000/8);
		}
	}

	/*********************
	 * HPCC-PINT
	 ********************/
	void RdmaHw::SetPintSmplThresh(double p)
	{
		pint_smpl_thresh = (uint32_t)(65536 * p);
	}
	void RdmaHw::HandleAckHpPint(Ptr<RdmaQueuePair> qp, Ptr<Packet> p, CustomHeader &ch)
	{
		uint32_t ack_seq = ch.ack.seq;
		if ((uint32_t)(rand() % 65536) >= pint_smpl_thresh)
			return;
		// update rate
		if (ack_seq > qp->hpccPint.m_lastUpdateSeq)
		{ // if full RTT feedback is ready, do full update
			UpdateRateHpPint(qp, p, ch, false);
		}
		else
		{ // do fast react
			UpdateRateHpPint(qp, p, ch, true);
		}
	}

	void RdmaHw::UpdateRateHpPint(Ptr<RdmaQueuePair> qp, Ptr<Packet> p, CustomHeader &ch, bool fast_react)
	{
		uint32_t next_seq = qp->snd_nxt;
		if (qp->hpccPint.m_lastUpdateSeq == 0)
		{ // first RTT
			qp->hpccPint.m_lastUpdateSeq = next_seq;
		}
		else
		{
			// check packet INT
			IntHeader &ih = ch.ack.ih;
			double U = Pint::decode_u(ih.GetPower());

			DataRate new_rate;
			int32_t new_incStage;
			double max_c = U / m_targetUtil;

			if (max_c >= 1 || qp->hpccPint.m_incStage >= m_miThresh)
			{
				new_rate = qp->hpccPint.m_curRate / max_c + m_rai;
				new_incStage = 0;
			}
			else
			{
				new_rate = qp->hpccPint.m_curRate + m_rai;
				new_incStage = qp->hpccPint.m_incStage + 1;
			}
			if (new_rate < m_minRate)
				new_rate = m_minRate;
			if (new_rate > qp->m_max_rate)
				new_rate = qp->m_max_rate;
			ChangeRate(qp, new_rate);
			if (!fast_react)
			{
				qp->hpccPint.m_curRate = new_rate;
				qp->hpccPint.m_incStage = new_incStage;
			}
			if (!fast_react)
			{
				if (next_seq > qp->hpccPint.m_lastUpdateSeq)
					qp->hpccPint.m_lastUpdateSeq = next_seq; //+ rand() % 2 * m_mtu;
			}
		}
	}

	int64_t RdmaHw::IncreaseRateForLaps(Ptr<RdmaQueuePair> qp, uint64_t nxtIncTimeInNs)
	{
		NS_LOG_FUNCTION(this);
		NS_ASSERT_MSG(qp->laps.m_curRate <= qp->laps.m_tgtRate, "The current rate is larger than the target rate");
		Time oldSendingTime = Seconds(qp->laps.m_curRate.CalculateTxTime(qp->lastPktSize));
		if (qp->laps.m_incStage > CcLaps::maxIncStage)
		{
			qp->laps.m_tgtRate = std::min(qp->m_max_rate, qp->laps.m_tgtRate * 2);
			qp->laps.m_tgtRate = std::max(m_minRate, qp->laps.m_tgtRate);
			// qp->laps.m_incStage = 0;
		}
		// qp->laps.m_curRate = 0.5 * (qp->laps.m_curRate + qp->laps.m_tgtRate);
		DataRate tmp_minRate = DataRate("1000Mb/s");
		qp->laps.m_curRate = std::max(qp->laps.m_curRate + tmp_minRate, 0.5 * (qp->laps.m_tgtRate + qp->laps.m_curRate));
		qp->laps.m_curRate = std::min(qp->m_max_rate, qp->laps.m_curRate);
		
		qp->laps.m_tgtRate = std::max(qp->laps.m_curRate, qp->laps.m_tgtRate);

		qp->laps.m_nxtRateIncTimeInNs = Simulator::Now().GetNanoSeconds() + nxtIncTimeInNs;
		qp->laps.m_incStage++;
		Time newSendingTime = Seconds(qp->laps.m_curRate.CalculateTxTime(qp->lastPktSize));
		int64_t timeGap = newSendingTime.GetNanoSeconds() - oldSendingTime.GetNanoSeconds();
		NS_ASSERT_MSG(timeGap <= 0, "The timeGap is positive");
		NS_LOG_INFO("curRateInMbps: " << qp->laps.m_curRate.GetBitRate()/1000000 << " tgtRateInMbps: " << qp->laps.m_tgtRate.GetBitRate()/1000000);
		// if (RdmaHw::enableRateRecord)
		// {
		// 	RecordFlowRateEntry_t recordFlowRateEntry;
		// 	recordFlowRateEntry.timeInNs = Simulator::Now().GetNanoSeconds();
		// 	recordFlowRateEntry.curRateInMbps = qp->laps.m_curRate.GetBitRate()/1000000/8;
		// 	recordFlowRateEntry.tgtRateInMbps = qp->laps.m_tgtRate.GetBitRate()/1000000/8;
		// 	recordFlowRateEntry.reason = "IdleLatency";
		// 	recordFlowRateEntry.incStage = qp->laps.m_incStage;
		// 	recordRateMap[qp->m_flow_id].push_back(recordFlowRateEntry);
		// }
		return timeGap;
	}

	int64_t RdmaHw::DecreaseRateForLaps(Ptr<RdmaQueuePair> qp, uint64_t nxtDecTimeInNs)
	{
		NS_LOG_FUNCTION(this);
		NS_ASSERT_MSG(qp->laps.m_curRate <= qp->laps.m_tgtRate, "The current rate is larger than the target rate");
		qp->laps.m_nxtRateDecTimeInNs = Simulator::Now().GetNanoSeconds() + nxtDecTimeInNs;
		Time oldSendingTime = Seconds(qp->laps.m_curRate.CalculateTxTime(qp->lastPktSize));
		qp->laps.m_tgtRate = std::max(m_minRate, qp->laps.m_curRate);
		qp->laps.m_curRate = std::max(m_minRate, qp->laps.m_curRate / 2);
		qp->laps.m_incStage = 0;
		Time newSendingTime = Seconds(qp->laps.m_curRate.CalculateTxTime(qp->lastPktSize));
		int64_t timeGap = newSendingTime.GetNanoSeconds() - oldSendingTime.GetNanoSeconds();
		NS_ASSERT_MSG(timeGap >= 0, "The timeGap is negative");
		NS_LOG_INFO("curRateInMbps: " << qp->laps.m_curRate.GetBitRate()/1000000 << " tgtRateInMbps: " << qp->laps.m_tgtRate.GetBitRate()/1000000);
		// if (RdmaHw::enableRateRecord)
		// {
		// 	RecordFlowRateEntry_t recordFlowRateEntry;
		// 	recordFlowRateEntry.timeInNs = Simulator::Now().GetNanoSeconds();
		// 	recordFlowRateEntry.curRateInMbps = qp->laps.m_curRate.GetBitRate()/1000000/8;
		// 	recordFlowRateEntry.tgtRateInMbps = qp->laps.m_tgtRate.GetBitRate()/1000000/8;
		// 	recordFlowRateEntry.reason = "largeLatency";
		// 	recordRateMap[qp->m_flow_id].push_back(recordFlowRateEntry);
		// }
		

		return timeGap;
	}

	void RdmaHw::UpdateNxtQpAvailTimeForLaps(Ptr<RdmaQueuePair> qp, int64_t timeGap)
	{
		NS_LOG_FUNCTION(this << timeGap);
		NS_ASSERT_MSG(Irn::mode == Irn::Mode::IRN_OPT || Irn::mode == Irn::Mode::NACK, "The mode is not IRN_OPT or IRN_OPT");
		qp->m_nextAvail = qp->m_nextAvail + NanoSeconds(timeGap);;
		uint32_t nic_idx = GetNicIdxOfQp(qp);
		m_nic[nic_idx].dev->UpdateNextAvail(qp->m_nextAvail);
	}


	void RdmaHw::UpdateRateForLaps(Ptr<RdmaQueuePair> qp, CustomHeader &ch)
	{
		NS_LOG_FUNCTION(this << "flowId" << qp->m_flow_id);
		Ipv4Address srcServerAddr = Ipv4Address(ch.dip);
		Ipv4Address dstServerAddr = Ipv4Address(ch.sip);
		uint32_t srcHostId = m_E2ErdmaSmartFlowRouting->lookup_SMT(srcServerAddr)->hostId;
		uint32_t dstHostId = m_E2ErdmaSmartFlowRouting->lookup_SMT(dstServerAddr)->hostId;
		HostId2PathSeleKey pstKey(srcHostId, dstHostId);
		pstEntryData *pstEntry = m_E2ErdmaSmartFlowRouting->lookup_PST(pstKey);
		std::vector<PathData *> pitEntries = m_E2ErdmaSmartFlowRouting->batch_lookup_PIT(pstEntry->paths);

		uint32_t congPathCnt = 0;
		uint32_t curMaxDelayInNs = 0;
		uint64_t tgtDelayInNs = qp->laps.m_tgtDelayInNs;
		for (size_t i = 0; i < pitEntries.size(); i++)
		{
			if (pitEntries[i]->latency > tgtDelayInNs)
			{
				congPathCnt++;
			}
			curMaxDelayInNs = std::max(curMaxDelayInNs, pitEntries[i]->latency);
		}
		
		NS_ASSERT_MSG(pitEntries.size() > 0, "The pitEntries is empty");

		// auto maxElement = std::max_element(pitEntries.begin(), pitEntries.end(),
		// 				[](const PathData* lhs, const PathData* rhs) 
		// 				{
		// 						return lhs->theoreticalSmallestLatencyInNs < rhs->theoreticalSmallestLatencyInNs;
		// 				}
		// 		);
		// uint64_t curMaxDelayInNs = (*maxElement)->theoreticalSmallestLatencyInNs;
		uint64_t curTimeInNs = Simulator::Now().GetNanoSeconds();

		if (congPathCnt == pitEntries.size())
		{
			if (qp->laps.m_nxtRateDecTimeInNs < curTimeInNs)
			{
				// std::cout << "Decrease rate for LAPS since  curMaxDelayInNs " << curMaxDelayInNs << " tgtDelayInNs " << tgtDelayInNs << std::endl;
				// for (size_t i = 0; i < pitEntries.size(); i++)
				// {
				// 	pitEntries[i]->print();
				// }

				NS_LOG_INFO("Decrease rate for LAPS");
				int64_t timeGap = DecreaseRateForLaps(qp, curMaxDelayInNs*2);
				insertRateRecord(qp->m_flow_id, qp->laps.m_curRate.GetBitRate()/1000000/8);
				UpdateNxtQpAvailTimeForLaps(qp, timeGap);
			}
		}
		else
		{
			if (qp->laps.m_nxtRateIncTimeInNs < curTimeInNs)
			{
				int64_t timeGap = IncreaseRateForLaps(qp, tgtDelayInNs*2);
				UpdateNxtQpAvailTimeForLaps(qp, timeGap);
   			insertRateRecord(qp->m_flow_id, qp->laps.m_curRate.GetBitRate()/1000000/8);
			}
		}
	}
	void RdmaHw::UpdateRateForLapsPlus(Ptr<RdmaQueuePair> qp, CustomHeader &ch)
	{
		NS_LOG_FUNCTION(this << "flowId" << qp->m_flow_id);
		Ipv4Address srcServerAddr = Ipv4Address(ch.dip);
		Ipv4Address dstServerAddr = Ipv4Address(ch.sip);
		uint32_t srcHostId = m_E2ErdmaSmartFlowRouting->lookup_SMT(srcServerAddr)->hostId;
		uint32_t dstHostId = m_E2ErdmaSmartFlowRouting->lookup_SMT(dstServerAddr)->hostId;
		HostId2PathSeleKey pstKey(srcHostId, dstHostId);
		pstEntryData *pstEntry = m_E2ErdmaSmartFlowRouting->lookup_PST(pstKey);
		std::vector<PathData *> pitEntries = m_E2ErdmaSmartFlowRouting->batch_lookup_PIT(pstEntry->paths);

		m_E2ErdmaSmartFlowRouting->RestrictPinnedFlowPaths(qp,pitEntries); // SIZE_AWARE_V1
		uint64_t oldBandwidthRate=qp->laps.m_curRate.GetBitRate(); uint32_t bandwidthKnownCount=0; // ZERO_BANDWIDTH_V1 audit
		uint32_t congPathCnt = 0;
		uint32_t curMaxDelayInNs = 0;
		uint64_t tgtDelayInNs = qp->laps.m_tgtDelayInNs;

        // RATE_SIGNAL_ABLATION_V1: log both predicates without feeding counterfactual values into control.
        uint32_t auditDelayCount=0,auditQueueCount=0,auditKnownCount=0;
        if(RdmaSmartFlowRouting::lcRateAudit && RdmaSmartFlowRouting::lcBwCc) {
            auto auditState=m_E2ErdmaSmartFlowRouting->EvaluateBandwidthCongestion(pitEntries,Simulator::Now().GetNanoSeconds());
            auditQueueCount=auditState.blockedCount;auditKnownCount=auditState.knownCount;
            for(auto path:pitEntries)auditDelayCount+=path->latency>path->theoreticalSmallestLatencyInNs;
        }
        auto logRateSignal=[&](const char *event) {
            if(RdmaSmartFlowRouting::lcRateAudit)RdmaSmartFlowRouting::lcRateSignals<<Simulator::Now().GetNanoSeconds()<<','
                <<m_node->GetId()<<','<<qp->m_flow_id<<','<<event<<','<<auditDelayCount<<','<<auditQueueCount<<','
                <<auditKnownCount<<','<<pitEntries.size()<<','<<oldBandwidthRate<<','<<qp->laps.m_curRate.GetBitRate()<<','
                <<(RdmaSmartFlowRouting::lcRateUseDelay?"delay":"queue")<<'\n';
        };
        // Replace only the total-rate predicate. Path eligibility, pause/probes and weights stay unchanged.
        if(RdmaSmartFlowRouting::lcBwCc && !RdmaSmartFlowRouting::lcRateUseDelay) {
            auto status=m_E2ErdmaSmartFlowRouting->EvaluateBandwidthCongestion(pitEntries,Simulator::Now().GetNanoSeconds());
            congPathCnt=status.blockedCount;bandwidthKnownCount=status.knownCount;
            // ZERO_BANDWIDTH_V1: unknown paths do not veto known positive paths.
            // If no known positive path exists and some are unknown, hold rather than assuming health.
            if(status.knownCount==status.blockedCount && status.blockedCount!=pitEntries.size()){logRateSignal("hold_unknown");return;}
        } else {
		for (size_t i = 0; i < pitEntries.size(); i++)
		{    //if (pitEntries[i]->latency >tgtDelayInNs)
			if (pitEntries[i]->latency > pitEntries[i]->theoreticalSmallestLatencyInNs)
			{
				congPathCnt++;
			}
			curMaxDelayInNs = std::max(curMaxDelayInNs, pitEntries[i]->latency);
		}
		        }

		NS_ASSERT_MSG(pitEntries.size() > 0, "The pitEntries is empty");

		// auto maxElement = std::max_element(pitEntries.begin(), pitEntries.end(),
		// 				[](const PathData* lhs, const PathData* rhs) 
		// 				{
		// 						return lhs->theoreticalSmallestLatencyInNs < rhs->theoreticalSmallestLatencyInNs;
		// 				}
		// 		);
		// uint64_t curMaxDelayInNs = (*maxElement)->theoreticalSmallestLatencyInNs;
		uint64_t curTimeInNs = Simulator::Now().GetNanoSeconds();

        // FIRSTTEST_V1: copy predicates to the log; original control statements below are untouched.
        if(RdmaSmartFlowRouting::firsttestTraceEnabled) {
            const char *action=(congPathCnt==pitEntries.size())?
                (qp->laps.m_nxtRateDecTimeInNs<curTimeInNs?"decrease":"hold"):
                (qp->laps.m_nxtRateIncTimeInNs<curTimeInNs?"increase":"hold");
            RdmaSmartFlowRouting::firsttestCcLog << curTimeInNs << ',' << m_node->GetId() << ',' << qp->m_flow_id << ','
                << congPathCnt << ',' << pitEntries.size() << ',' << qp->laps.m_curRate.GetBitRate() << ',' << action << '\n';
        }


		if (congPathCnt == pitEntries.size())
		{
			if (qp->laps.m_nxtRateDecTimeInNs < curTimeInNs)
			{
				// std::cout << "Decrease rate for LAPS since  curMaxDelayInNs " << curMaxDelayInNs << " tgtDelayInNs " << tgtDelayInNs << std::endl;
				// for (size_t i = 0; i < pitEntries.size(); i++)
				// {
				// 	pitEntries[i]->print();
				// }

				NS_LOG_INFO("Decrease rate for LAPS");
				int64_t timeGap = DecreaseRateForLaps(qp, tgtDelayInNs*2);
				insertRateRecord(qp->m_flow_id, qp->laps.m_curRate.GetBitRate()/1000000/8);
				UpdateNxtQpAvailTimeForLaps(qp, timeGap);
			}
		}
		else
		{
			if (qp->laps.m_nxtRateIncTimeInNs < curTimeInNs)
			{//std::cout << "increase rate for LAPS since  curMaxDelayInNs " << curMaxDelayInNs << " tgtDelayInNs " << tgtDelayInNs << std::endl;
				
				int64_t timeGap = IncreaseRateForLaps(qp, tgtDelayInNs);
				UpdateNxtQpAvailTimeForLaps(qp, timeGap);
   			insertRateRecord(qp->m_flow_id, qp->laps.m_curRate.GetBitRate()/1000000/8);
			}
		}
        if(RdmaSmartFlowRouting::lcBwCc && RdmaSmartFlowRouting::lcBwCcLog && oldBandwidthRate!=qp->laps.m_curRate.GetBitRate()) {
            RdmaSmartFlowRouting::lcBwCcLog<<Simulator::Now().GetNanoSeconds()<<','<<m_node->GetId()<<','<<qp->m_flow_id<<','<<(qp->laps.m_curRate.GetBitRate()<oldBandwidthRate?"decrease":"increase")<<','<<congPathCnt<<','<<pitEntries.size()<<','<<bandwidthKnownCount<<','<<oldBandwidthRate<<','<<qp->laps.m_curRate.GetBitRate()<<'\n';
        }
        logRateSignal("evaluated");
	}


	void RdmaHw::HandleAckLaps(Ptr<RdmaQueuePair> qp, Ptr<Packet> p, CustomHeader &ch)
	{
		NS_LOG_FUNCTION(this << "flowId" << qp->m_flow_id);
		// m_E2ErdmaSmartFlowRouting->update_PIT_by_latency_tag(p);
		if(RdmaSmartFlowRouting::enable_laps_plus){
        UpdateRateForLapsPlus(qp, ch);
		}
		else{
		UpdateRateForLaps(qp, ch);
		}
	}


		//=================================新增函数，初始化BDP=========================================================

	void RdmaHw::InitializePathBdpForAllPaths(std::vector<Ptr<Node>> allNodes1)
	{
		if (!RdmaSmartFlowRouting::enable_laps_plus)
			return; // 只在启用laps_plus时生效
		NS_LOG_FUNCTION(this);
		NS_LOG_INFO("Initializing path BDP for all paths");

		// 遍历所有PST条目来初始化路径BDP
		for (const auto &pstPair : m_E2ErdmaSmartFlowRouting->get_PST())
		{
			const pstEntryData &pstEntry = pstPair.second;
			std::vector<uint32_t> pathsCopy = pstEntry.paths;
			std::vector<PathData *> pitEntries = m_E2ErdmaSmartFlowRouting->batch_lookup_PIT(pathsCopy);

			if (pitEntries.empty())
				continue;

			// 对PIT条目按延迟进行排序，找到最大延迟
			std::sort(pitEntries.begin(), pitEntries.end(), [](const PathData *lhs, const PathData *rhs)
					  {
						  return lhs->theoreticalSmallestLatencyInNs > rhs->theoreticalSmallestLatencyInNs; // 降序排列
					  });

			// 使用最大延迟作为基准
			uint64_t maxLatencyInNs = pitEntries[0]->theoreticalSmallestLatencyInNs;
			double delayInSec = maxLatencyInNs; // 不延迟转换为秒

			// 为该PST中的所有路径初始化BDP
			for (auto pitEntry : pitEntries)
			{

				uint32_t pid = pitEntry->pid;
				uint64_t minBandwidthBytesPerSec = 200000000000;

				std::vector<uint32_t> nodeIdSequence = pitEntry->nodeIdSequence;
				std::vector<uint32_t> portSequence = pitEntry->portSequence;

				// 遍历路径上的每个链路，找到最小带宽
				for (size_t i = 0; i < portSequence.size() && i < nodeIdSequence.size() - 1; i++)
				{
					uint32_t nodeId = nodeIdSequence[i]; // 节点ID
					uint32_t portId = portSequence[i];	 // 端口ID

					// 通过节点ID和端口ID获取对应的QbbNetDevice并获取带宽
					// 需要找到对应节点的对应端口设备

					Ptr<Node> node = allNodes1[nodeId];

					if (node && node->GetNDevices() > portId)
					{
						Ptr<NetDevice> netDevice = node->GetDevice(portId);
						Ptr<QbbNetDevice> qbbDev = DynamicCast<QbbNetDevice>(netDevice);
						if (qbbDev)
						{

							uint64_t linkBandwidthBps = qbbDev->GetDataRate().GetBitRate(); // 获取带宽，单位bps

							uint64_t linkBandwidthBytesPerSec = linkBandwidthBps / 8; // 转换为B/s

							// 更新最小带宽
							if (linkBandwidthBytesPerSec < minBandwidthBytesPerSec)
							{
								minBandwidthBytesPerSec = linkBandwidthBytesPerSec;
							}
						}
					}
				}
				// std::cout<<"node"<<nodeIdSequence[0]<<",pid:"<<pid<<",minBandwidthBytesPerSec:"<<minBandwidthBytesPerSec<<std::endl;

				// 检查该路径是否已初始化过
				uint64_t bdp = (uint64_t)((minBandwidthBytesPerSec / 1000000000.0) * (delayInSec)); 

				if (pid == 28672 || pid == 28671 || pid == 28645)
					std::cout << "node" << nodeIdSequence[0] << ",pid:" << pid << ",BDP:" << bdp << ",延迟：" << delayInSec+1000 * portSequence.size()  << std::endl;

				if (m_E2ErdmaSmartFlowRouting->m_pathBdpMap.find(pid) == m_E2ErdmaSmartFlowRouting->m_pathBdpMap.end())
				{
					m_E2ErdmaSmartFlowRouting->m_pathBdpMap[pid].maxBdp = bdp;
					m_E2ErdmaSmartFlowRouting->m_pathBdpMap[pid].currentBdp = 0; // 初始时路径BDP为0
					NS_LOG_INFO("Initialize BDP for path " << pid << ", maxBdp: " << bdp << ", using max delay: " << maxLatencyInNs);
				}
			}
		}

		NS_LOG_INFO("All path BDPs initialized");
	}

	void RdmaHw::decreaseBDPForPath(uint32_t pathId, uint16_t size)
	{
		if (!RdmaSmartFlowRouting::enable_laps_plus)
		{
			return;
		}
		m_E2ErdmaSmartFlowRouting->m_pathBdpMap[pathId].currentBdp -= (size+48);
		//std::cout << "decrease BDP for path " << pathId << ", currentBdp: " << m_E2ErdmaSmartFlowRouting->m_pathBdpMap[pathId].currentBdp << ", size: " << size << std::endl;
		if(m_E2ErdmaSmartFlowRouting->m_pathBdpMap[pathId].currentBdp < 0)
		std::cout << "ERROR ,BDP < 0" << std::endl;
		else
		if(m_E2ErdmaSmartFlowRouting->m_pathBdpMap[pathId].currentBdp==0&&size!=1000){
			//std::cout << "decrease BDP for path " << pathId << ", currentBdp: " << m_E2ErdmaSmartFlowRouting->m_pathBdpMap[pathId].currentBdp << ", size: " << size << std::endl;
		}
		
	}


		void RdmaHw::UpdateRateForLapsBasedOnBDP(Ptr<RdmaQueuePair> qp,uint32_t increaserate){
		NS_LOG_FUNCTION(this << "flowId" << qp->m_flow_id);
		Ipv4Address srcServerAddr = Ipv4Address(qp->sip);
		Ipv4Address dstServerAddr = Ipv4Address(qp->dip);
		uint32_t srcHostId = m_E2ErdmaSmartFlowRouting->lookup_SMT(srcServerAddr)->hostId;
		uint32_t dstHostId = m_E2ErdmaSmartFlowRouting->lookup_SMT(dstServerAddr)->hostId;
		HostId2PathSeleKey pstKey(srcHostId, dstHostId);
		pstEntryData *pstEntry = m_E2ErdmaSmartFlowRouting->lookup_PST(pstKey);
		std::vector<PathData *> pitEntries = m_E2ErdmaSmartFlowRouting->batch_lookup_PIT(pstEntry->paths);

		uint32_t congPathCnt = 0;
		uint32_t curMaxDelayInNs = 0;
		uint64_t tgtDelayInNs = qp->laps.m_tgtDelayInNs;
		for (size_t i = 0; i < pitEntries.size(); i++)
		{//=======改动 1.15========
			// if (pitEntries[i]->m_maxCongestionPercent > 0.6)
			// {
			// 	congPathCnt++;
			// }
			curMaxDelayInNs = std::max(curMaxDelayInNs, pitEntries[i]->latency);
		}

		NS_ASSERT_MSG(pitEntries.size() > 0, "The pitEntries is empty");

		uint64_t curTimeInNs = Simulator::Now().GetNanoSeconds();
           //increaserate=2，0为减速，1为加速，
		if (increaserate==2||increaserate==0)
		{
			if (qp->laps.m_nxtRateDecTimeInNs < curTimeInNs)
			{
				
				NS_LOG_INFO("Decrease rate for LAPS");
				int64_t timeGap = DecreaseRateForLaps(qp,  tgtDelayInNs/4);
				insertRateRecord(qp->m_flow_id, qp->laps.m_curRate.GetBitRate() / 1000000 / 8);
				UpdateNxtQpAvailTimeForLaps(qp, timeGap);
			}
		}
		else if(increaserate==1)
		{
			if (qp->laps.m_nxtRateIncTimeInNs < curTimeInNs)
			{
				int64_t timeGap = IncreaseRateForLaps(qp,tgtDelayInNs/2 );
				UpdateNxtQpAvailTimeForLaps(qp, timeGap);
				insertRateRecord(qp->m_flow_id, qp->laps.m_curRate.GetBitRate() / 1000000 / 8);
			}
		}
	}

	 void RdmaHw::UpdatePortUtilization(Ptr<Packet> p, CustomHeader &ch){
		//=============================更新各端口带宽利用率============================================
                  //==============每10us统计一次============
			  if(Simulator::Now().GetNanoSeconds()-RdmaSmartFlowRouting::record_time>50000){
				RdmaSmartFlowRouting::record_time=Simulator::Now().GetNanoSeconds();
				for (const auto& pair : RdmaSmartFlowRouting::nodeIdToNodeMap) 
                   {
                    uint32_t nodeId = pair.first;
                     Ptr<Node> node = pair.second;
					 if(node->GetNodeType() == 0){
						//服务器部分处理逻辑
                        //node->m_E2ErdmaSmartFlowRouting->record_each_port_send_data
                       

						//continue;
					  }
                      Ptr<SwitchNode> swNode = DynamicCast<SwitchNode>(node);
					   Ptr<RdmaDriver> rdma = node->GetObject<RdmaDriver>();
                           //========================================================注意，为了使用Ptr<RdmaDriver> rdma =============
                           //========================================================我在rdmahw里面调用了头文件RdmaDriver，已经造成了循环调用===========
						   //========================================================不过可以正常编译就暂时不管了


                   for ( auto& pair_1 : node->GetNodeType() == 0? rdma->m_rdma->m_E2ErdmaSmartFlowRouting->record_each_port_send_data:swNode->m_mmu->m_SmartFlowRouting->record_each_port_send_data) 
                   {//std::cout<<1<<std::endl;
					uint32_t portId = pair_1.first;
					//对应端口没有数据记录
                    // if(pair_1.second.send_data_byte==0){
					// 	continue;
					// }
					//std::cout<<2<<std::endl;
					record_utilization_rate port_utilization_rate;
					port_utilization_rate.time=Simulator::Now().GetNanoSeconds();
					//获取经过时间
					uint64_t duration_time=Simulator::Now().GetNanoSeconds()-pair_1.second.time;
					//计算利用率
					//std::cout<<3<<std::endl;
					 double utilization;
					if(pair_1.second.port_rate==-1){
						utilization=0;
					}else{
						utilization=(double)(pair_1.second.send_data_byte*8*1000000000)/duration_time/pair_1.second.port_rate;
					}

                     

					port_utilization_rate.utilization_rate= utilization;

					RdmaSmartFlowRouting::record_all_port_utilization_rate[nodeId][portId].push_back(port_utilization_rate);
					//===================更新带宽平均利用率==========================================
					//粗略将没用的丢弃,看看效果
					
							 double avg_utilization_rate=RdmaSmartFlowRouting::record_all_port_avg_utilization_rate[nodeId][portId].utilization_rate;
					 uint32_t update_count=RdmaSmartFlowRouting::record_all_port_avg_utilization_rate[nodeId][portId].update_count;
                    RdmaSmartFlowRouting::record_all_port_avg_utilization_rate[nodeId][portId].utilization_rate=(double)( avg_utilization_rate*update_count + utilization)/(update_count+1);
					RdmaSmartFlowRouting::record_all_port_avg_utilization_rate[nodeId][portId].update_count++;
					

					//添加计算交换机高负载端口平均队列长度 ---二月六号
					if(RdmaSmartFlowRouting::record_all_port_queue_len[nodeId][portId].size()!=0&&node->GetNodeType() != 0 ){
						uint32_t sum_queue_len=0;
                       uint32_t size1=RdmaSmartFlowRouting::record_all_port_queue_len[nodeId][portId].size();
					   uint32_t count=RdmaSmartFlowRouting::record_all_port_AVG_queue_len[nodeId][portId].count;

						for(uint32_t i=0;i<size1;i++){
							sum_queue_len+=RdmaSmartFlowRouting::record_all_port_queue_len[nodeId][portId][i].length;
						}

                         double avg_queue_len=(double)(count*(RdmaSmartFlowRouting::record_all_port_AVG_queue_len[nodeId][portId].now_avg_port_length)+sum_queue_len)/(count+size1);
                         RdmaSmartFlowRouting::record_all_port_AVG_queue_len[nodeId][portId].now_avg_port_length=avg_queue_len;
						 RdmaSmartFlowRouting::record_all_port_AVG_queue_len[nodeId][portId].count+=size1;
						 //std::cout<<nodeId<<" "<<portId<<" "<<RdmaSmartFlowRouting::record_all_port_AVG_queue_len[nodeId][portId].now_avg_port_length<<std::endl;
                     
					}
					
				
					//std::cout<<"时间 "<<Simulator::Now().GetNanoSeconds()<<"，节点 "<<nodeId<<"，端口 "<<portId<<"，平均利用率 "<<RdmaSmartFlowRouting::record_all_port_avg_utilization_rate[nodeId][portId].utilization_rate<<std::endl;
					//===================更新带宽平均利用率==========================================
					if(nodeId==1&&portId==1){
						//std::cout<<"时间 "<<Simulator::Now().GetNanoSeconds()<<"，节点 "<<nodeId<<"，端口 "<<portId<<"，利用率 "<<utilization<<std::endl;
					
					}
					
					//端口记录重置数据
					// std::cout<<4<<std::endl;
					pair_1.second.send_data_byte=0;
					pair_1.second.time=Simulator::Now().GetNanoSeconds();    
                   
				   }	//std::cout<<"时间 "<<Simulator::Now().GetNanoSeconds()<<"，节点 "<<nodeId<<std::endl;
					
                  }
				  //清除已经使用过的数据
				  RdmaSmartFlowRouting::record_all_port_queue_len.clear();
				   
                  //================================更新路径利用率============================
				  if(RdmaSmartFlowRouting::enable_laps_plus){ 
					  //前1个流最多的节点对
					  for(uint32_t i=0;i<1;i++){
				  HostId2PathSeleKey pstKey=RdmaSmartFlowRouting::sorted_path_flow_counts[i].first;
				  uint32_t srcHostId=pstKey.selfHostId;
				  uint32_t dstHostId=pstKey.dstHostId;
				  //可以用循环记录多个节点之间的路径的利用率
		
				Ptr<Node> srcnode =RdmaSmartFlowRouting::nodeIdToNodeMap[srcHostId];
			
				 Ptr<RdmaDriver> rdma = srcnode->GetObject<RdmaDriver>();
                pstEntryData *pstEntry =rdma->m_rdma->m_E2ErdmaSmartFlowRouting-> lookup_PST(pstKey);
               std::vector<PathData *> pitEntries = rdma->m_rdma->m_E2ErdmaSmartFlowRouting->batch_lookup_PIT(pstEntry->paths);
        
			   for (auto &pitEntry : pitEntries) { 
                    double utilization=0.0;
					for (uint32_t i = 0; i < pitEntry->portSequence.size(); i++) { 
						uint32_t portId = pitEntry->portSequence[i];
						u_int32_t nodeId = pitEntry->nodeIdSequence[i];
							// 检查容器是否为空，避免访问空容器的back()
						if (!RdmaSmartFlowRouting::record_all_port_utilization_rate[nodeId][portId].empty()) {
							record_utilization_rate now_port = RdmaSmartFlowRouting::record_all_port_utilization_rate[nodeId][portId].back();
							if (now_port.utilization_rate > utilization) {
								utilization = now_port.utilization_rate;
							}
						} else {
							// 如果容器为空，可以设置默认值或跳过处理
							NS_LOG_WARN("Warning: record_all_port_utilization_rate is empty for nodeId " << nodeId << " portId " << portId);
							// 可以选择设置默认利用率值为0或其他合适的默认值
						}
					}
				
					record_utilization_rate path_utilization_rate;
					path_utilization_rate.time=Simulator::Now().GetNanoSeconds();
					path_utilization_rate.utilization_rate=utilization;
			
					RdmaSmartFlowRouting::record_path_utilization_rate[pstKey][pitEntry->pid].push_back(path_utilization_rate);
					if(utilization>1.01)
               std::cout<<"时间 "<<Simulator::Now().GetNanoSeconds()<<"，路径 "<<pitEntry->pid<<"，利用率 "<<utilization<<std::endl;
				}




				  }	
				  }



			  }
				 

              //=============================更新各端口带宽利用率============================================
	 }




}
