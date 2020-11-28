#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/netanim-module.h"
#include "ns3/tcp-westwood.h"
#include "ns3/internet-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/packet-sink.h"
#include "ns3/tcp-hybla.h"
#include "ns3/tcp-congestion-ops.h"
#include "ns3/traced-value.h"
#include "ns3/tcp-yeah.h"
#include "ns3/log.h"
#include "ns3/tcp-scalable.h"
#include "ns3/sequence-number.h"
#include "ns3/traced-value.h"
#include "ns3/drop-tail-queue.h"
#include "ns3/enum.h"
#include <string>
#include <fstream>
#include <cstdlib>
#include <map>


typedef uint32_t uint;

using namespace ns3;
using namespace std;

#define ERROR 0.00001

NS_LOG_COMPONENT_DEFINE ("MyApp");

class MyApp: public Application {
	private:
		virtual void StartApplication(void);
		virtual void StopApplication(void);

		void ScheduleTx(void);
		void SendPacket(void);

		Ptr<Socket>     m_Socket;
		Address         m_Peer;
		uint32_t        m_PacketSize;
		uint32_t        m_NPackets;
		DataRate        m_DataRate;
		EventId         m_SendEvent;
		bool            m_Running;
		uint32_t        m_PacketsSent;

	public:
		MyApp();
		virtual ~MyApp();

		void Setup(Ptr<Socket> socket, Address address, uint packetSize, uint nPackets, DataRate dataRate);
		void recv(int numBytesRcvd);

};

MyApp::MyApp(): m_Socket(0),
		    m_Peer(),
		    m_PacketSize(0),
		    m_NPackets(0),
		    m_DataRate(0),
		    m_SendEvent(),
		    m_Running(false),
		    m_PacketsSent(0) {
}

MyApp::~MyApp() {
	m_Socket = 0;
}

void MyApp::Setup(Ptr<Socket> socket, Address address, uint packetSize, uint nPackets, DataRate dataRate) {
	m_Socket = socket;
	m_Peer = address;
	m_PacketSize = packetSize;
	m_NPackets = nPackets;
	m_DataRate = dataRate;
}

void MyApp::StartApplication() {
	m_Running = true;
	m_PacketsSent = 0;
	m_Socket->Bind();
	m_Socket->Connect(m_Peer);
	SendPacket();
}

void MyApp::StopApplication() {
	m_Running = false;
	if(m_SendEvent.IsRunning()) {
		Simulator::Cancel(m_SendEvent);
	}
	if(m_Socket) {
		m_Socket->Close();
	}
}

void MyApp::SendPacket() {
	Ptr<Packet> packet = Create<Packet>(m_PacketSize);
	m_Socket->Send(packet);

	if(++m_PacketsSent < m_NPackets) {
		ScheduleTx();
	}
}

void MyApp::ScheduleTx() {
	if (m_Running) {
		Time tNext(Seconds(m_PacketSize*8/static_cast<double>(m_DataRate.GetBitRate())));
		m_SendEvent = Simulator::Schedule(tNext, &MyApp::SendPacket, this);

	}
}

////////////////////////////////////////////////////////////////////////////
std::map<uint, uint> packetsDropped;
std::map<Address, double> TotalBytesAtSink;
std::map<std::string, double> mapBytesReceivedIPV4, Throughput;

static void packetDrop(FILE* stream, double current_time, uint myId) {
	if(packetsDropped.find(myId) == packetsDropped.end()) {
		packetsDropped[myId] = 0;
	}
	packetsDropped[myId]++;
}

static void CongestionWindowUpdater(FILE *stream, double current_time, uint oldCwnd, uint newCwnd) {
    fprintf(stream, "%s  %s\n",(std::to_string(  Simulator::Now ().GetSeconds () - current_time)).c_str()   , (std::to_string(newCwnd )).c_str() );

	// *stream->GetStream() << Simulator::Now ().GetSeconds () - current_time << "\t" << newCwnd << std::endl;
}

void ThroughtPutData(FILE *stream, double current_time, std::string context, Ptr<const Packet> p, Ptr<Ipv4> ipv4, uint interface) {
	double timeNow = Simulator::Now().GetSeconds();

    if(Throughput.find(context) == Throughput.end())
		Throughput[context] = 0;
	if(mapBytesReceivedIPV4.find(context) == mapBytesReceivedIPV4.end())
		mapBytesReceivedIPV4[context] = 0;

	mapBytesReceivedIPV4[context] += p->GetSize();
	double curSpeed = (((mapBytesReceivedIPV4[context] * 8.0) / 1024)/(timeNow-current_time));
    fprintf(stream, "%s  %s\n",(std::to_string( timeNow-current_time).c_str())  , (std::to_string(curSpeed )).c_str() );

    if(Throughput[context] < curSpeed)
			Throughput[context] = curSpeed;
}


void GoodPutData(FILE *stream, double current_time, std::string context, Ptr<const Packet> p, const Address& addr){
	double timeNow = Simulator::Now().GetSeconds();

	if(TotalBytesAtSink.find(addr) == TotalBytesAtSink.end())
		TotalBytesAtSink[addr] = 0;
	TotalBytesAtSink[addr] += p->GetSize();
	//getting goodput by calculating the average data transfer rate
    double speed = (((TotalBytesAtSink[addr] * 8.0) / 1024)/(timeNow-current_time));
    fprintf(stream, "%s  %s\n",(std::to_string( timeNow-current_time).c_str() ) , (std::to_string(speed )).c_str() );

    

}

Ptr<Socket> WestwoodFlow(Address sinkAddress, 
					uint sinkPort, 
					Ptr<Node> hostNode, 
					Ptr<Node> sinkNode, 
					double current_time, 
					double stopTime,
					uint packetSize,
					uint totalPackets,
					string dataRate,
					double appStartTime,
					double appStopTime) {


    Config::SetDefault ("ns3::TcpWestwood::ProtocolType", EnumValue (TcpWestwood::WESTWOODPLUS));
    Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(TcpWestwood::GetTypeId()));
	
	PacketSinkHelper packetSinkHelper("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), sinkPort));
	ApplicationContainer sinkApps = packetSinkHelper.Install(sinkNode);
	sinkApps.Start(Seconds(current_time));
	sinkApps.Stop(Seconds(stopTime));

	Ptr<Socket> TcpSocket = Socket::CreateSocket(hostNode, TcpSocketFactory::GetTypeId());

    //Run the app to get the data
	Ptr<MyApp> app = CreateObject<MyApp>();
	app->Setup(TcpSocket, sinkAddress, packetSize, totalPackets, DataRate(dataRate));
	hostNode->AddApplication(app);
	app->SetStartTime(Seconds(appStartTime));
	app->SetStopTime(Seconds(appStopTime));

	return TcpSocket;
}

Ptr<Socket> YeahFlow(Address sinkAddress, 
					uint sinkPort, 
					Ptr<Node> hostNode, 
					Ptr<Node> sinkNode, 
					double current_time, 
					double stopTime,
					uint packetSize,
					uint totalPackets,
					string dataRate,
					double appStartTime,
					double appStopTime) {

    Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(TcpYeah::GetTypeId()));
	
	PacketSinkHelper packetSinkHelper("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), sinkPort));
	ApplicationContainer sinkApps = packetSinkHelper.Install(sinkNode);
	sinkApps.Start(Seconds(current_time));
	sinkApps.Stop(Seconds(stopTime));

	Ptr<Socket> TcpSocket = Socket::CreateSocket(hostNode, TcpSocketFactory::GetTypeId());

    //Run the app to get the data
	Ptr<MyApp> app = CreateObject<MyApp>();
	app->Setup(TcpSocket, sinkAddress, packetSize, totalPackets, DataRate(dataRate));
	hostNode->AddApplication(app);
	app->SetStartTime(Seconds(appStartTime));
	app->SetStopTime(Seconds(appStopTime));

	return TcpSocket;
}


Ptr<Socket> HyblaFlow(Address sinkAddress, 
					uint sinkPort, 
					Ptr<Node> hostNode, 
					Ptr<Node> sinkNode, 
					double current_time, 
					double stopTime,
					uint packetSize,
					uint totalPackets,
					string dataRate,
					double appStartTime,
					double appStopTime) {


    Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(TcpYeah::GetTypeId()));
	
	PacketSinkHelper packetSinkHelper("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), sinkPort));
	ApplicationContainer sinkApps = packetSinkHelper.Install(sinkNode);
	sinkApps.Start(Seconds(current_time));
	sinkApps.Stop(Seconds(stopTime));

	Ptr<Socket> TcpSocket = Socket::CreateSocket(hostNode, TcpSocketFactory::GetTypeId());

    //Run the app to get the data
	Ptr<MyApp> app = CreateObject<MyApp>();
	app->Setup(TcpSocket, sinkAddress, packetSize, totalPackets, DataRate(dataRate));
	hostNode->AddApplication(app);
	app->SetStartTime(Seconds(appStartTime));
	app->SetStopTime(Seconds(appStopTime));

	return TcpSocket;
}

int main(){
    //
//          H1////////// 				  //////////H4
//	  					//				//
//		    H2-----------R1-----------R2 -----------H5
//						//			  ///
//			H3/////////					///////////	H6
//
//
    uint totalPackets = 1000000;
	//given in the ques to take packet size = 1.3 KB
	uint packetSize = 1.3*1024;		
	double elapsed_time = 1000;
	double current_time = 0;
	uint port = 7000;
	double errorP = ERROR;
    Ptr<RateErrorModel> em = CreateObjectWithAttributes<RateErrorModel> ("ErrorRate", DoubleValue (errorP));

	//Mode: Whether to use Bytes (see MaxBytes) or Packets (see MaxPackets) as the maximum queue size metric. 
	PointToPointHelper Con_HTR, Con_RTR;
	std::cout << "Establishing the connection's transfer rates" << endl;
    Con_HTR.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
	Con_HTR.SetChannelAttribute("Delay", StringValue("20ms"));
	Con_HTR.SetQueue("ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue (QueueSize("250000B")));
	Con_RTR.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
	Con_RTR.SetChannelAttribute("Delay", StringValue("50ms"));
	Con_RTR.SetQueue("ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue (QueueSize("62500B")));

    NodeContainer nodes;
    nodes.Create(8);
    NodeContainer h1r1 = NodeContainer (nodes.Get (0), nodes.Get (3)); 
	std::cout << "Concatenating H1 with R1" << endl;
    NodeContainer h2r1 = NodeContainer (nodes.Get (1), nodes.Get (3));
    std::cout << "Concatenating H2 with R1" << endl;
	NodeContainer h3r1 = NodeContainer (nodes.Get (2), nodes.Get (3));
    std::cout << "Concatenating H3 with R1" << endl;
	NodeContainer h4r2 = NodeContainer (nodes.Get (5), nodes.Get (4));
    std::cout << "Concatenating H4 with R2" << endl;
	NodeContainer h5r2 = NodeContainer (nodes.Get (6), nodes.Get (4));
    std::cout << "Concatenating H5 with R2" << endl;
	NodeContainer h6r2 = NodeContainer (nodes.Get (7), nodes.Get (4));
    std::cout << "Concatenating H6 with R2" << endl;
	NodeContainer r1r2 = NodeContainer (nodes.Get (3), nodes.Get (4));
	std::cout << "Concatenating R1 with R2" << endl;

    InternetStackHelper internet;
    internet.Install (nodes);    
    std:: cout << endl;
    std::cout << "Installing connections in the network (Dumbbell Topology)" << endl;
    NetDeviceContainer n_h1r1 = Con_HTR.Install (h1r1);
    NetDeviceContainer n_h2r1 = Con_HTR.Install (h2r1);
    NetDeviceContainer n_h3r1 = Con_HTR.Install (h3r1);
    NetDeviceContainer n_h4r2 = Con_HTR.Install (h4r2);
    NetDeviceContainer n_h5r2 = Con_HTR.Install (h5r2);
    NetDeviceContainer n_h6r2 = Con_HTR.Install (h6r2);
    NetDeviceContainer n_r1r2 = Con_HTR.Install (r1r2);

	// Error Handling if connection is having trouble
    n_h1r1.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em));
    n_h2r1.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em));
    n_h3r1.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em));
    n_h4r2.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em));
    n_h5r2.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em));
    n_h6r2.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em));

    std:: cout << endl;
    std::cout << "Assigning IP addresses" << std::endl;
    Ipv4AddressHelper ipv4;
    ipv4.SetBase ("102.115.1.0", "255.255.255.0");
    Ipv4InterfaceContainer i_h1r1 = ipv4.Assign (n_h1r1);
	std::cout << "H1 address : 102.115.1.0" << endl;
    ipv4.SetBase ("102.115.2.0", "255.255.255.0");
    Ipv4InterfaceContainer i_h2r1 = ipv4.Assign (n_h2r1);
	std::cout << "H2 address : 102.115.2.0" << endl;
    ipv4.SetBase ("102.115.3.0", "255.255.255.0");
    Ipv4InterfaceContainer i_h3r1 = ipv4.Assign (n_h3r1);
	std::cout << "H3 address : 102.115.3.0" << endl;
    ipv4.SetBase ("102.115.5.0", "255.255.255.0");
    Ipv4InterfaceContainer i_h4r2 = ipv4.Assign (n_h4r2);
	std::cout << "H4 address : 102.115.5.0" << endl;
    ipv4.SetBase ("102.115.6.0", "255.255.255.0");
    Ipv4InterfaceContainer i_h5r2 = ipv4.Assign (n_h5r2);
	std::cout << "H5 address : 102.115.6.0" << endl;
    ipv4.SetBase ("102.115.7.0", "255.255.255.0");
    Ipv4InterfaceContainer i_h6r2 = ipv4.Assign (n_h6r2);
	std::cout << "H6 address : 102.115.7.0" << endl;
    ipv4.SetBase ("102.115.4.0", "255.255.255.0");
    Ipv4InterfaceContainer i_r1r2 = ipv4.Assign (n_r1r2);
	std::cout << "R1 address : 102.115.4.0" << endl;
	std::cout << endl;
    //////////////////////////////////////////////////////////////////////////////

	

    AsciiTraceHelper asciiTraceHelper;

    FILE * Data1,*CN_YeahTCP,*ThroughPut_YeahTCP,*GoodPut_YeahTCP;
    Data1 = fopen ("q2_Yeah_TCP_H1_H4_file_q1.txt","w");
    CN_YeahTCP = fopen ("q2_Yeah_TCP_H1_H4_Congestion_data_q1.txt","w");
    ThroughPut_YeahTCP = fopen ("q2_Yeah_TCP_H1_H4_Throughput_data_q1.txt","w");
    GoodPut_YeahTCP = fopen ("q2_Yeah_TCP_H1_H4_Goodput_data_q1.txt","w"); 

    //tcpYeah stimulation
	std::cout << "Establishing TCP YeAH-TCP Simulation" << endl;
	Ptr<Socket> TcpYeahSocket = YeahFlow(InetSocketAddress( i_h4r2.GetAddress(0), port), port, nodes.Get(0), nodes.Get(5), current_time, current_time+elapsed_time, packetSize, totalPackets, "50Mbps", current_time, current_time+elapsed_time);
	
	// Measure PacketSinks
	string sinkYeah1 = "/NodeList/5/ApplicationList/0/$ns3::PacketSink/Rx";
	Config::Connect(sinkYeah1, MakeBoundCallback(&GoodPutData, GoodPut_YeahTCP, current_time));

	string sinkYeah2 = "/NodeList/5/$ns3::Ipv4L3Protocol/Rx";
	Config::Connect(sinkYeah2, MakeBoundCallback(&ThroughtPutData, ThroughPut_YeahTCP, current_time));

    TcpYeahSocket->TraceConnectWithoutContext("Drop", MakeBoundCallback (&packetDrop, Data1, current_time, 1));
    TcpYeahSocket->TraceConnectWithoutContext("CongestionWindow", MakeBoundCallback (&CongestionWindowUpdater, CN_YeahTCP, current_time));


    // //increment start TIme
	current_time += elapsed_time;
	cout << endl;
    FILE * Data2,*CN_Hybla,*ThroughPut_Hybla,*GoodPut_Hybla;
    Data2 = fopen ("q2_Hybla_H2_H5_file_q1.txt","w");
    CN_Hybla = fopen ("q2_Hybla_H2_H5_Congestion_data_q1.txt","w");
    ThroughPut_Hybla = fopen ("q2_Hybla_H2_H5_Throughput_data_q1.txt","w");
    GoodPut_Hybla = fopen ("q2_Hybla_H2_H5_Goodput_data_q1.txt","w"); 
	//tcpHybla Simulation
	std::cout << "Establishing TCP Hybla Simulation" << endl;

	Ptr<Socket> TcpHyblaSocket = HyblaFlow(InetSocketAddress(i_h5r2.GetAddress(0), port), port, nodes.Get(1), nodes.Get(6), current_time+30, current_time+elapsed_time+30, packetSize, totalPackets, "50Mbps", current_time, current_time+elapsed_time);
	// Measure PacketSinks
	string sinkHybla1 = "/NodeList/6/ApplicationList/0/$ns3::PacketSink/Rx";
	Config::Connect(sinkHybla1, MakeBoundCallback(&GoodPutData, GoodPut_Hybla, current_time));

	string sinkHybla2 = "/NodeList/6/$ns3::Ipv4L3Protocol/Rx";
	Config::Connect(sinkHybla2, MakeBoundCallback(&ThroughtPutData, ThroughPut_Hybla, current_time));

	TcpHyblaSocket->TraceConnectWithoutContext("Drop", MakeBoundCallback (&packetDrop, Data2, current_time, 2));
    TcpHyblaSocket->TraceConnectWithoutContext("CongestionWindow", MakeBoundCallback (&CongestionWindowUpdater, CN_Hybla, current_time));

    //increment start TIme
	current_time += elapsed_time;
	cout << endl;
    // //westWood Plus Stimulation

    FILE * Data3,*CN_WestWood,*ThroughPut_Westwood,*GoodPut_Westwood;
   	Data3 = fopen ("q2_Westwood_H3_H6_file_q1.txt","w");
    CN_WestWood = fopen ("q2_Westwood_H3_H6_Congestion_data_q1.txt","w");
    ThroughPut_Westwood = fopen ("q2_Westwood_H3_H6_Throughput_data_q1.txt","w");
    GoodPut_Westwood = fopen ("q2_Westwood_H3_H6_Goodput_data_q1.txt","w"); 
	
	std::cout << "Establishing TCP Westwood+ Simuation" << endl;
	Ptr<Socket> westwoodPSocket = WestwoodFlow(InetSocketAddress(i_h6r2.GetAddress(0), port), port, nodes.Get(2), nodes.Get(7), current_time+30, current_time+elapsed_time+30, packetSize, totalPackets, "50Mbps", current_time, current_time+elapsed_time);

	// Measure PacketSinks
	string sinkWestwood1 = "/NodeList/7/ApplicationList/0/$ns3::PacketSink/Rx";
	Config::Connect(sinkWestwood1, MakeBoundCallback(&GoodPutData, GoodPut_Westwood, current_time));

	string sinkWestwood2 = "/NodeList/7/$ns3::Ipv4L3Protocol/Rx";
	Config::Connect(sinkWestwood2, MakeBoundCallback(&ThroughtPutData, ThroughPut_Westwood, current_time));

    westwoodPSocket->TraceConnectWithoutContext("Drop", MakeBoundCallback (&packetDrop, Data3, current_time, 3));
    westwoodPSocket->TraceConnectWithoutContext("CongestionWindow", MakeBoundCallback (&CongestionWindowUpdater, CN_WestWood, current_time));

    //increment start TIme
	current_time += elapsed_time;
	cout << endl;
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();
	std::cout << "Running the flow monitor for analysing the code" << endl;

	Ptr<FlowMonitor> flowmon;
	FlowMonitorHelper flowmonHelper;
	flowmon = flowmonHelper.InstallAll();
	Simulator::Stop(Seconds(current_time+elapsed_time+30));
	Simulator::Run();
	flowmon->CheckForLostPackets();

	//Ptr<OutputStreamWrapper> streamTP = asciiTraceHelper.CreateFileStream("application_6_a.tp");
	Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmonHelper.GetClassifier());
	std::map<FlowId, FlowMonitor::FlowStats> stats = flowmon->GetFlowStats();

	for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin(); i != stats.end(); ++i) {
		Ipv4FlowClassifier::FiveTuple tempClassifier = classifier->FindFlow (i->first);

        if (tempClassifier.sourceAddress=="102.115.1.1"){
		    // *Data1->GetStream() << "Tcp Yeah Flow " << i->first  << " (" << (tempClassifier.sourceAddress) << " -> " << tempClassifier.destinationAddress << ")\n";
		    fprintf(Data1, "TCP Yeah flow %s (102.115.1.1 -> 102.115.5.1)\n",(std::to_string( i->first)).c_str());
            fprintf(Data1, "Total Packet Lost : %s\n",(std::to_string( i->second.lostPackets)).c_str() );
            fprintf(Data1, "Packet Lost due to Buffer Overflow : %s\n",(std::to_string( packetsDropped[1] )).c_str() );
            fprintf(Data1, "Packet Lost due to Congestion : %s\n",(std::to_string( i->second.lostPackets - packetsDropped[1] )).c_str() );
            fprintf(Data1, "Maximum throughput (in kbps) : %s\n",(std::to_string( Throughput["/NodeList/5/$ns3::Ipv4L3Protocol/Rx"] )).c_str() );
            fprintf(Data1, "Total Packets transmitted : %s\n",(std::to_string( totalPackets )).c_str() );
            fprintf(Data1, "Packets Successfully Transferred : %s\n",(std::to_string(  totalPackets- i->second.lostPackets )).c_str() );
            fprintf(Data1, "Percentage of packet loss (total) : %s\n",(std::to_string( double(i->second.lostPackets*100)/double(totalPackets) )).c_str() );
		    fprintf(Data1, "Percentage of packet loss (due to Buffer Overflow) : %s\n",(std::to_string(  double(packetsDropped[1]*100)/double(totalPackets))).c_str() );
		    fprintf(Data1, "Percentage of packet loss (due to Congestion) : %s\n",(std::to_string( double((i->second.lostPackets - packetsDropped[1])*100)/double(totalPackets))).c_str() );
		}
        else if(tempClassifier.sourceAddress=="102.115.2.1"){
            fprintf(Data2, "TCP Hybla flow %s (102.115.2.1 -> 102.115.6.1)\n",(std::to_string( i->first)).c_str());
            fprintf(Data2, "Total Packet Lost : %s\n",(std::to_string( i->second.lostPackets)).c_str() );
            fprintf(Data2, "Packet Lost due to Buffer Overflow : %s\n",(std::to_string( packetsDropped[2] )).c_str() );
            fprintf(Data2, "Packet Lost due to Congestion : %s\n",(std::to_string( i->second.lostPackets - packetsDropped[2] )).c_str() );
            fprintf(Data2, "Maximum throughput (in kbps) : %s\n",(std::to_string( Throughput["/NodeList/6/$ns3::Ipv4L3Protocol/Rx"] )).c_str() );
            fprintf(Data2, "Total Packets transmitted : %s\n",(std::to_string( totalPackets )).c_str() );
            fprintf(Data2, "Packets Successfully Transferred : %s\n",(std::to_string(  totalPackets- i->second.lostPackets )).c_str() );
            fprintf(Data2, "Percentage of packet loss (total) : %s\n",(std::to_string( double(i->second.lostPackets*100)/double(totalPackets) )).c_str() );
		    fprintf(Data2, "Percentage of packet loss (due to Buffer Overflow) : %s\n",(std::to_string(  double(packetsDropped[2]*100)/double(totalPackets))).c_str() );
		    fprintf(Data2, "Percentage of packet loss (due to Congestion) : %s\n",(std::to_string( double((i->second.lostPackets - packetsDropped[2])*100)/double(totalPackets))).c_str() );
        }
        else if(tempClassifier.sourceAddress=="102.115.3.1"){
            fprintf(Data3, "TCP Westwood+ flow %s (102.115.3.1 -> 102.115.7.1)\n",(std::to_string( i->first)).c_str());
            fprintf(Data3, "Total Packet Lost : %s\n",(std::to_string( i->second.lostPackets)).c_str() );
            fprintf(Data3, "Packet Lost due to Buffer Overflow : %s\n",(std::to_string( packetsDropped[3] )).c_str() );
            fprintf(Data3, "Packet Lost due to Congestion : %s\n",(std::to_string( i->second.lostPackets - packetsDropped[3] )).c_str() );
            fprintf(Data3, "Maximum throughput (in kbps) : %s\n",(std::to_string( Throughput["/NodeList/7/$ns3::Ipv4L3Protocol/Rx"] )).c_str() );
            fprintf(Data3, "Total Packets transmitted : %s\n",(std::to_string( totalPackets )).c_str() );
            fprintf(Data3, "Packets Successfully Transferred : %s\n",(std::to_string(  totalPackets- i->second.lostPackets )).c_str() );
            fprintf(Data3, "Percentage of packet loss (total) : %s\n",(std::to_string( double(i->second.lostPackets*100)/double(totalPackets) )).c_str() );
		    fprintf(Data3, "Percentage of packet loss (due to Buffer Overflow) : %s\n",(std::to_string(  double(packetsDropped[3]*100)/double(totalPackets))).c_str() );
		    fprintf(Data3, "Percentage of packet loss (due to Congestion) : %s\n",(std::to_string( double((i->second.lostPackets - packetsDropped[3])*100)/double(totalPackets))).c_str() );
        }
	}
	//flowmon->SerializeToXmlFile("application_6_a.flowmon", true, true);
	std::cout << "Simulation Ended" << std::endl;
	Simulator::Destroy();
    return 0;
}