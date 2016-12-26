#include "contiki.h"
#include "net/rime.h"
#include "random.h"
#include "net/rime/netflood.h"
#include "dev/button-sensor.h"
#include "dev/leds.h"
#include <math.h>
#include <stdio.h>


#define MAX 9
#define INFINITE 1000000.0
#define FULL_BATERY 10.0;
#define LAMBDA 10000.0;
#define LOCAL_DISTANCE 3.0;
#define UNREACHABLE -1;
#define ENERGY_SHARE_PRESICION 0.98;

/*Node Enerji seviyelerini tutan struct*/
struct NodeEnergy {
  rimeaddr_t addr;
  int energyLevel;
};
typedef struct NodeEnergy NodeEnergy;

/*forward path icin kullanilan struct*/
struct route_path {
	struct route_path *next;
	rimeaddr_t addr;
};
typedef struct route_path route_path;
/*---------------------------------------------------------------------------*/
PROCESS(cmax_process, "cmax example");
AUTOSTART_PROCESSES(&cmax_process);
/*---------------------------------------------------------------------------*/

/*********GLOBAL VARIABLES*****************/

/*1-network topology and distance matrix*/
static float distance[MAX][MAX]={
		{INFINITE,3.0      ,INFINITE,3.0     ,INFINITE,INFINITE	,INFINITE,INFINITE,INFINITE},
		{3.0     ,INFINITE,3.0     ,INFINITE,3.0     ,INFINITE	,INFINITE,INFINITE,INFINITE},
		{INFINITE,3.0     ,INFINITE,INFINITE,INFINITE,3.0     	,INFINITE,INFINITE,INFINITE},
		{3.0     ,INFINITE,INFINITE,INFINITE,3.0     ,INFINITE	,3.0     ,INFINITE,INFINITE},
		{INFINITE,3.0     ,INFINITE,3.0     ,INFINITE,3.0     	,INFINITE,3.0     ,INFINITE},
		{INFINITE,INFINITE,3.0     ,INFINITE,3.0     ,INFINITE	,INFINITE,INFINITE,3.0     },
		{INFINITE,INFINITE,INFINITE,3.0     ,INFINITE,INFINITE	,INFINITE,3.0     ,INFINITE},
		{INFINITE,INFINITE,INFINITE,INFINITE,3.0     ,INFINITE	,3.0     ,INFINITE,3.0     },
		{INFINITE,INFINITE,INFINITE,INFINITE,INFINITE,3.0     	,INFINITE,3.0     ,INFINITE}
	};
/*2-Enegery weight matrix*/
static float weight[MAX][MAX] = { 0 };

/*initial batery level*/
static float bateryLevel=10.0;
static float lastSharedEnergyLevel=10.0;
static float localDistance=3.0;
static int currentNode;
/*just for printing energy matrix*/
static float GlobalEnergyMatrix[MAX];
static int totalMessageCount=0;
static float startBateryLevel=10.0;
/*Routing methods*/
/*Djikstra kisa yol algoritmasi*/
static void shortpath(float cost[][MAX], int *preced, float *distance,
		int startPoint) {

	int selected[MAX] = { 0 };
	int current = startPoint, i, k;
	float smalldist, newdist,dc;
	for (i = 0; i < MAX; i++)
		distance[i] = INFINITE;
	selected[current] = 1;
	distance[startPoint] = 0;
	int visitedNodeCount = 0;
	float c=-1;
	printf("GLOBAL ENERGIES :  %d\n",current);
	for (i = 0; i < MAX; ++i) {
			c=GlobalEnergyMatrix[i];
			             		printf("%ld.%03u\n",  (long)c,
			             		(unsigned)((c-floor(c))*1000));
			}

	while (visitedNodeCount < MAX) {
		smalldist = INFINITE;
		dc = distance[current];
		for (i = 0; i < MAX; i++) {
			if (selected[i] == 0) {
				newdist = dc + cost[current][i];
				if (newdist-distance[i] <0 ) {
					distance[i] = newdist;
					preced[i] = current;
				}
				if (distance[i]-smalldist < 0) {
					smalldist = distance[i];
					k = i;
				}
			}

		}
//		for (i = 0; i < MAX; ++i) {
//				printf("---  %d",preced[i]);
//			}
		current = k;
		selected[current] = 1;
		visitedNodeCount++;

		printf("\n");
	}
}
/*Energy Operation*/
static void printffloat(float c)
{
	printf("%ld.%03u\n",  (long)c,
	(unsigned)((c-floor(c))*1000));
}

/*enerji seviyesini paketbuf a yazan method*/
static void energyInfoToPacketBuf(){
	printf("SENT ENERGY LEVEL:");
	printffloat(bateryLevel);
	packetbuf_copyfrom(&bateryLevel, sizeof(bateryLevel));
}
/*mesafeye gore enerji maliyetini hesaplayan method*/
static float calculateEnegeryByDistance(float distance){
	float unitDataTransferCost = (powf(distance, 3.0)) * 0.001;
	if ((unitDataTransferCost - 0.001) < 0) {
		unitDataTransferCost = 0.001;
	}
	return unitDataTransferCost;
}
/*weight matrisinini update eden method*/
static void updateEnegeryWeightMatrix(int i, float currentEnergyOf_i,float weight[][MAX], float distance[][MAX], int messageLength) {
	/*calculate  alfa_ij()*/
	float alfa_ij = 1 - (currentEnergyOf_i / startBateryLevel);
	int j;
	for (j = 0; j < MAX; j++) {
		if (i != j) {

			float unitDataTransferCost = calculateEnegeryByDistance(distance[i][j]);
			/*check if there is enough energy to send data ,if there is not then update it INFINITE*/
			if (((currentEnergyOf_i / messageLength) - unitDataTransferCost)
					< 0) {
				weight[i][j] = INFINITE;
			} else {
				weight[i][j] = (powf(10000.0, alfa_ij) - 1)
						* (unitDataTransferCost);
				if (weight[i][j] - INFINITE > 0) {
					weight[i][j] = INFINITE;
				}

			}
		}
	}
	printf("weight update\n");
}
/*enerji seviyesini maliyet kadar dusuren method*/
static float drainBatery(float energyCost){
	printf("ENERGY LEVEL:");
	printffloat(bateryLevel);
	bateryLevel=bateryLevel-energyCost;
	GlobalEnergyMatrix[currentNode]=bateryLevel;
	updateEnegeryWeightMatrix(currentNode,bateryLevel,weight,distance,1);
	return bateryLevel;
}

/*enerji bilgisinin paylasilmasi icin -esik degerinin altına inilip inilmedigini kontrol eden method*/
static int validateShareEnergy(){
	float energyShareLimit=lastSharedEnergyLevel*ENERGY_SHARE_PRESICION;
	if (bateryLevel>0 && bateryLevel-energyShareLimit<0) {
		return 1;
	}else {
		return 0;
	}

}
/*weight matrisini baslangictaki pil seviyelerine gore update eden method*/
static void initializeWeightMatrix(float weight[][MAX], float distance[][MAX],
		int messageLength) {

	int i;
	for (i = 0; i < MAX; i++) {
		updateEnegeryWeightMatrix(i, startBateryLevel, weight, distance, messageLength);
	}
}

static rimeaddr_t getRimeAddressOfNode(int nodeId) {
	nodeId+=1;
	rimeaddr_t addr;
	addr.u8[1] = nodeId / 10;
	addr.u8[0] = nodeId % 10;
	return addr;
}

static int8_t rimeAddressToNodeId(rimeaddr_t addr) {
	return (addr.u8[0]+(addr.u8[1]*10))-1;
}


static int
netflood_received(struct netflood_conn *c, const rimeaddr_t *from,
		     const rimeaddr_t *originator, uint8_t seqno, uint8_t hops)
{
	if (!rimeaddr_cmp(originator, &rimeaddr_node_addr)) {
		float energyLevel;
		if (packetbuf_datalen()==sizeof(energyLevel)) {
			memcpy(&energyLevel, packetbuf_dataptr(), sizeof(energyLevel));
			  printf("broadcast SENDER  %d.%d",
					  from->u8[0],  from->u8[1]);
				  printf("broadcast message received from %d.%d",
						  originator->u8[0],  originator->u8[1]);
				  printf("ENEGERY LEVEL");
				  printffloat(energyLevel);
				  /*update weight matrix with new energylevel of i */
				  updateEnegeryWeightMatrix(rimeAddressToNodeId(*originator),energyLevel,weight,distance,1);
				  GlobalEnergyMatrix[rimeAddressToNodeId(*originator)]=energyLevel;
				  /*Drain local broadcastig energy*/
				  drainBatery(calculateEnegeryByDistance(localDistance));
		  return 1;
		}else {
			printf("ERORRR BROADCAST ENERGY\n");
			return 0;
		}

	} else {
		return 0;
	}
}

/*Send DataPackage*/
static int8_t forwardPathToPackageBuffer(int destinationNode){
	int preced[MAX],j;
	float distance_djikstra[MAX];
	/*at the begining no one can reach anybodyl*/
	for (j = 0; j < MAX; ++j) {
		preced[j] = UNREACHABLE;
	}

	printf("PRECEDED:\n");
	shortpath(weight, preced, distance_djikstra, currentNode);
	for (j = 0; j < MAX; ++j) {
		printf("---  %d",preced[j]);
	}
	printf("\n");
	int root = destinationNode;
		int8_t reverseArray[71];
		int index = 70;
		reverseArray[index] = root;
		index--;
		printf("%d---->", root);
		do {
			root = preced[root];
			if (root == currentNode) {
				reverseArray[index] = root;
				printf("%d", root);
				break;
			} else if (root == -1 || index==-1) {
				reverseArray[70]=-1;
				break;
			}
			reverseArray[index] = root;
			index--;
			printf("%d---->", root);
		} while (1);
		printf("\n");
		if (reverseArray[70]!=-1) {
			packetbuf_clear();
			void* bufferPointer = packetbuf_dataptr();
			/** Current Hop* */
			static uint16_t currentHop = 1;
			memcpy(bufferPointer, &currentHop, sizeof(uint16_t));
			bufferPointer += sizeof(uint16_t);
			/*****/
			int8_t forwardArray[71] = { 0 };
			int copyIndex=71-index;
			memcpy(forwardArray,&reverseArray[index],71-index);
			forwardArray[copyIndex] = -1;
			int k;
			for (k = 0; k < MAX; ++k) {
				printf("--->%d",forwardArray[k]+1);
			}
			printf("\n");

			memcpy(bufferPointer, &forwardArray[0], sizeof(forwardArray));
			packetbuf_set_datalen(sizeof(uint16_t)+sizeof(forwardArray));
			return forwardArray[1];

		}else {
			printf("Path Not Found");
			return -1;
		}

}
/*recieve DataPackage or forward it*/
static void
recv_uc(struct unicast_conn *c, const rimeaddr_t *from)
{
	rimeaddr_t nextHopAddress;
		uint16_t currentHop;
		void* bufferPointer = packetbuf_dataptr();

		memcpy(&currentHop, bufferPointer, sizeof(uint16_t));
		printf("Current Hop: %d \n", currentHop);
		bufferPointer = bufferPointer + sizeof(uint16_t);
		int8_t forwardArray2[71] = { 0 };
		memcpy(&forwardArray2[0], bufferPointer, sizeof(forwardArray2));



		if (forwardArray2[currentHop+1]==-1) {
			printf("Message comming from multi hop forwarding");
			/*get address of message orginator*/
			nextHopAddress=getRimeAddressOfNode(forwardArray2[0]);
		    printf("Message comming from multi hop forwarding: ID- %d %d\n", nextHopAddress.u8[1],
							nextHopAddress.u8[0]);
		    totalMessageCount=totalMessageCount+1;
		    printf("TOTAL MESSAGE COUNT : %d\n",totalMessageCount);

		} else {
			nextHopAddress=getRimeAddressOfNode(forwardArray2[currentHop+1]);
			printf("next Hope Address: %d %d\n", nextHopAddress.u8[1],
					nextHopAddress.u8[0]);
		    currentHop += 1;
			/*Data Package Operations*/
			void* bufferPointer = packetbuf_dataptr();
			/* Current Hop*/
			memcpy(bufferPointer, &currentHop, sizeof(uint16_t));
			bufferPointer += sizeof(uint16_t);
			rimeaddr_t addr;
			addr.u8[0] = nextHopAddress.u8[0];
			addr.u8[1] = nextHopAddress.u8[1];
			if (!rimeaddr_cmp(&addr, &rimeaddr_node_addr)) {
				printf("FORWARRRDING \n");
				unicast_send(c, &addr);
				drainBatery(calculateEnegeryByDistance(distance[currentNode][forwardArray2[currentHop]]));
			}
		}
}

static const struct unicast_callbacks unicast_callbacks = {recv_uc};
static struct unicast_conn uc;

const static struct netflood_callbacks netflood_call= {netflood_received,NULL,NULL};
static struct netflood_conn netflood;

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(cmax_process, ev, data) {
	PROCESS_EXITHANDLER(netflood_close(&netflood);)
	PROCESS_BEGIN()
		;
		unicast_open(&uc, 146, &unicast_callbacks);
		netflood_open(&netflood, 5, 145, &netflood_call);
		static struct etimer et;
		SENSORS_ACTIVATE(button_sensor);
		float c = -1;
		/*getNodeId*/
		currentNode = rimeAddressToNodeId(rimeaddr_node_addr);

		initializeWeightMatrix(weight, distance, 1);
		printf("%d\n", currentNode);
		int i, j, k;
		for (i = 0; i < MAX; ++i) {
			GlobalEnergyMatrix[i] = startBateryLevel;
		}

		void* packetPointerStart = packetbuf_dataptr();
		static uint16_t seqNo=1;
		while (1) {
			rimeaddr_t addr;
//    PROCESS_WAIT_EVENT_UNTIL(ev == sensors_event &&
//			     data == &button_sensor);
			etimer_set(&et,
					CLOCK_SECOND * 4 + random_rand() % (CLOCK_SECOND * 4));

			PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

			if (currentNode==0) {
				/*Send forward package first */
				/*write path to packetpuf*/
				printf("ENERGIES :\n");
				for (i = 0; i < MAX; ++i) {
								c=GlobalEnergyMatrix[i];
								             		printf("%ld.%03u\n",  (long)c,
								             		(unsigned)((c-floor(c))*1000));
								};
				printf("WEIGHT MATRIX :\n");
				for (k = 0; k < MAX; ++k) {
					for (j = 0; j < MAX; ++j) {
						c = weight[k][j];
						printf("------%ld.%03u", (long) c,
											(unsigned) ((c - floor(c)) * 1000));
					}
					printf("\n");
				}
				addr.u8[0] = 9;
				addr.u8[1] = 0;
				int8_t nodeId = forwardPathToPackageBuffer(
						rimeAddressToNodeId(addr));
				if (nodeId != -1 && !rimeaddr_cmp(&addr, &rimeaddr_node_addr)) {
					drainBatery(calculateEnegeryByDistance(distance[currentNode][nodeId]));
					printf("node-id %d\n", nodeId);
					addr = getRimeAddressOfNode(nodeId);
					unicast_send(&uc, &addr);
				}
			}
			/*clear packetBuf to ensure that all information about forwarding was deleted.*/
			if(validateShareEnergy()){
			/*write energy information to packetBuf*/
			energyInfoToPacketBuf();
			/*then send new energy level*/
			netflood_send(&netflood,seqNo);
			lastSharedEnergyLevel=bateryLevel;
			printf("broadcast message sent\n");
			drainBatery(calculateEnegeryByDistance(localDistance));
			seqNo=seqNo+1;
			}
		}
	PROCESS_END();
}
/*---------------------------------------------------------------------------*/
