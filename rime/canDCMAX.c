#include "contiki.h"
#include "net/rime.h"
#include "random.h"
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
#define ENERGY_SHARE_PRESICION 0.99;

typedef union {
	float xy[2];
} NodeCoordinate;

struct NodeEnergy {
  rimeaddr_t addr;
  int energyLevel;
};
typedef struct NodeEnergy NodeEnergy;

struct route_path {
	struct route_path *next;
	rimeaddr_t addr;
};
typedef struct route_path route_path;
/*---------------------------------------------------------------------------*/
PROCESS(example_trickle_process, "Trickle example");
AUTOSTART_PROCESSES(&example_trickle_process);
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

static float bateryLevel = 10.0;
static float lastSharedEnergyLevel=10.0;
static float localDistance = 3.0;
static int currentNode;
static NodeCoordinate nodeCoordinateArray[MAX];
static float GlobalEnergyMatrix[MAX];

static int roundCount;
/*Routing methods*/

static void initializeDistanceVector(){

	nodeCoordinateArray[0].xy[0]=0;
	nodeCoordinateArray[0].xy[1]=0;

	nodeCoordinateArray[1].xy[0]=0;
    nodeCoordinateArray[1].xy[1]=33;

    nodeCoordinateArray[2].xy[0]=0;
	nodeCoordinateArray[2].xy[1]=66;

	nodeCoordinateArray[3].xy[0]=33;
    nodeCoordinateArray[3].xy[1]=0;
	nodeCoordinateArray[4].xy[0]=33;
	nodeCoordinateArray[4].xy[1]=33;
	nodeCoordinateArray[5].xy[0]=33;
    nodeCoordinateArray[5].xy[1]=66;

	nodeCoordinateArray[6].xy[0]=66;
    nodeCoordinateArray[6].xy[1]=0;
	nodeCoordinateArray[7].xy[0]=66;
	nodeCoordinateArray[7].xy[1]=33;
	nodeCoordinateArray[8].xy[0]=66;
    nodeCoordinateArray[8].xy[1]=66;
}
static float getDistanceBetweenTwoNode(int x,int y){
	float firstPart=(nodeCoordinateArray[x].xy[0]-nodeCoordinateArray[y].xy[0]);
	firstPart=powf(firstPart,2);
	float secondPart=(nodeCoordinateArray[x].xy[1]-nodeCoordinateArray[y].xy[1]);
	secondPart=powf(secondPart,2);
	float result;
	result=powf(firstPart+secondPart,0.5);

return result;

}

static void shortpath(float cost[][MAX], int *preced, float *distance,
		int startPoint,int destinationNode) {

	int selected[MAX] = { 0 };
	int current = startPoint, i, k;
	float smalldist, newdist, dc;
	for (i = 0; i < MAX; i++)
		distance[i] = INFINITE;
	selected[current] = 1;
	distance[startPoint] = 0;
	int visitedNodeCount = 0;
	float c = -1;
//	printf("GLOBAL ENERGIES :  %d\n", current);
//	for (i = 0; i < MAX; ++i) {
//		c = GlobalEnergyMatrix[i];
//		printf("%ld.%03u\n", (long) c, (unsigned) ((c - floor(c)) * 1000));
//	}
	float nodeDistanceVector[MAX]={0};
	for (i = 0; i < MAX; i++)
			nodeDistanceVector[i]=getDistanceBetweenTwoNode(i,destinationNode);
	float baseDistance=nodeDistanceVector[currentNode];

	while (visitedNodeCount < MAX) {
		printf("CURRENT :  %d\n", current);
		smalldist = INFINITE;
		dc = distance[current];

		printf("DISTANCE -CURRENT :");
		printf("%ld.%03u\n", (long) dc, (unsigned) ((dc - floor(dc)) * 1000));
		for (i = 0; i < MAX; i++) {
			if (selected[i] == 0) {
				if (nodeDistanceVector[i] <= baseDistance) {
					newdist = dc + cost[current][i];
					if (newdist - distance[i] < 0) {
						distance[i] = newdist;
						preced[i] = current;
					}
					if (distance[i] - smalldist < 0) {
						smalldist = distance[i];
						k = i;
					}
				}
			}

		}
//		for (i = 0; i < MAX; ++i) {
//			printf("---  %d", preced[i]);
//		}
		current = k;
		selected[current] = 1;
		visitedNodeCount++;

		printf("\n");
	}
}
/*Energy Operation*/
static void printffloat(float c) {
	printf("%ld.%03u\n", (long) c, (unsigned) ((c - floor(c)) * 1000));
}

static float drainBatery(float energyCost) {
	printf("ENERGY LEVEL:");
	printffloat(bateryLevel);
	bateryLevel = bateryLevel - energyCost;
	return bateryLevel;
}

static void energyInfoToPacketBuf() {
	printf("SENT ENERGY LEVEL:");
	printffloat(bateryLevel);
	packetbuf_copyfrom(&bateryLevel, sizeof(bateryLevel));
}

static float calculateEnegeryByDistance(float distance) {
	float unitDataTransferCost = (powf(distance, 3.0)) * 0.001;
	if ((unitDataTransferCost - 0.001) < 0) {
		unitDataTransferCost = 0.001;
	}
	return unitDataTransferCost;
}
static void updateEnegeryWeightMatrix(int i, float currentEnergyOf_i,
		float weight[][MAX], float distance[][MAX], int messageLength) {
	/*calculate  alfa_ij()*/
	float alfa_ij = 1 - (currentEnergyOf_i / 10.0);
	int j;
	for (j = 0; j < MAX; j++) {
		if (i != j) {

			float unitDataTransferCost = calculateEnegeryByDistance(
					distance[i][j]);
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
	printf("FINISH \n ");
}
static int validateShareEnergy(){
	float energyShareLimit=lastSharedEnergyLevel*ENERGY_SHARE_PRESICION;
	if (bateryLevel>0 && bateryLevel-energyShareLimit<0) {
		return 1;
	}else {
		return 0;
	}

}
static void initializeWeightMatrix(float weight[][MAX], float distance[][MAX],
		int messageLength) {

	int i;
	for (i = 0; i < MAX; i++) {
		updateEnegeryWeightMatrix(i, 10.0, weight, distance, messageLength);
	}
	printf("FINISH-3 \n ");
}

static rimeaddr_t getRimeAddressOfNode(int nodeId) {
	nodeId += 1;
	rimeaddr_t addr;
	addr.u8[1] = nodeId / 10;
	addr.u8[0] = nodeId % 10;
	return addr;
}

static int8_t rimeAddressToNodeId(rimeaddr_t addr) {
	return (addr.u8[0] + (addr.u8[1] * 10)) - 1;
}

static void broadcast_recv(struct broadcast_conn *c, const rimeaddr_t *from) {

	if (!rimeaddr_cmp(from, &rimeaddr_node_addr)) {
		float energyLevel;
		if (packetbuf_datalen() == sizeof(energyLevel)) {
			memcpy(&energyLevel, packetbuf_dataptr(), sizeof(energyLevel));
			printf("broadcast message received from %d.%d", from->u8[0],
					from->u8[1]);
			printf("ENEGERY LEVEL");
			printffloat(energyLevel);
			/*update weight matrix with new energylevel of i */
			updateEnegeryWeightMatrix(rimeAddressToNodeId(*from), energyLevel,
					weight, distance, 1);
			GlobalEnergyMatrix[rimeAddressToNodeId(*from)] = energyLevel;
			/*Drain local broadcastig energy*/
			drainBatery(calculateEnegeryByDistance(localDistance));
		} else {
			printf("ERORRR BROADCAST ENERGY\n");

		}

	}
}

static void fillForwardPathVector(int8_t* forwardPath, int destinationNode) {
	int preced[MAX], j;
	float distance_djikstra[MAX];
	/*at the begining no one can reach anybodyl*/
	for (j = 0; j < MAX; ++j) {
		preced[j] = UNREACHABLE;
	}

//	printf("PRECEDED:\n");
	shortpath(weight, preced, distance_djikstra, currentNode,destinationNode);
//	for (j = 0; j < MAX; ++j) {
//		printf("---  %d", preced[j]);
//	}
	printf("\n");
	int root = destinationNode;
	int previousNode = -1;
	while (1) {
//		printf("%d---->", root);
		if (root == currentNode) {
			forwardPath[0] = root;
			forwardPath[1] = previousNode;
			forwardPath[2] = destinationNode;
			break;
		} else if (root == -1) {
			forwardPath[0] = -1;
			break;
		}
		previousNode = root;
		root = preced[root];
	}

}

/*Send DataPackage*/
static int8_t forwardPathToPackageBuffer(int destinationNode,
		uint16_t currentHop) {
	int8_t forwardArray[71] = { 0 };
	fillForwardPathVector(forwardArray,destinationNode);
	int index=68;
		if (forwardArray[0]!=-1) {
			packetbuf_clear();
			void* bufferPointer = packetbuf_dataptr();
			/** Current Hop* */
			memcpy(bufferPointer, &currentHop, sizeof(uint16_t));
			bufferPointer += sizeof(uint16_t);
			/*****/
			int k;
			for (k = 0; k < MAX; ++k) {
				printf("--->%d",forwardArray[k]);
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
static void recv_uc(struct unicast_conn *c, const rimeaddr_t *from) {
	printf("RECIEVE OK! \n");
	rimeaddr_t nextHopAddress;
	uint16_t currentHop;
	void* bufferPointer = packetbuf_dataptr();

	memcpy(&currentHop, bufferPointer, sizeof(uint16_t));
	printf("Current Hop: %d \n", currentHop);
	bufferPointer = bufferPointer + sizeof(uint16_t);
	int8_t forwardArray2[3] = { 0 };
	memcpy(&forwardArray2[0], bufferPointer, sizeof(forwardArray2));

	int i;
			printf("FORWARD RECIEVED: ");
			for (i = 0; i < 3; ++i) {
				printf("---%d",forwardArray2[i]);
			}
			printf("\n");

	//next-hop and destination equals then message recieved
	if (forwardArray2[1] == forwardArray2[2]) {
		printf("Message comming from multi hop forwarding");
		/*get address of message orginator*/
		nextHopAddress = getRimeAddressOfNode(forwardArray2[0]);
		printf("Message comming from multi hop forwarding: ID- %d %d\n",
				nextHopAddress.u8[1], nextHopAddress.u8[0]);

	} else {
		currentHop = currentHop + 1;
		int8_t nextHopId = forwardPathToPackageBuffer(forwardArray2[2],
				currentHop);
		if (nextHopId != -1) {
			nextHopAddress = getRimeAddressOfNode(nextHopId);
		}
		printf("next Hope Address: %d %d\n", nextHopAddress.u8[1],
				nextHopAddress.u8[0]);
		rimeaddr_t addr;
		addr.u8[0] = nextHopAddress.u8[0];
		addr.u8[1] = nextHopAddress.u8[1];
		if (!rimeaddr_cmp(&addr, &rimeaddr_node_addr)) {
			printf("FORWARRRDING \n");
			unicast_send(c, &addr);
			printf("SEND OK \n");
			drainBatery(
					calculateEnegeryByDistance(
							distance[currentNode][nextHopId]));
		}
	}
}

static const struct unicast_callbacks unicast_callbacks = { recv_uc };
static struct unicast_conn uc;

static const struct broadcast_callbacks broadcast_call = { broadcast_recv };
static struct broadcast_conn broadcast;

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(example_trickle_process, ev, data) {
	PROCESS_EXITHANDLER(broadcast_close(&broadcast);)
	PROCESS_BEGIN();
		unicast_open(&uc, 146, &unicast_callbacks);
		broadcast_open(&broadcast, 129, &broadcast_call);
		roundCount = 0;
		static struct etimer et;
		SENSORS_ACTIVATE(button_sensor);
		float c = -1;
		/*getNodeId*/
		printf("START NODE\n");
		currentNode = rimeAddressToNodeId(rimeaddr_node_addr);
		printf("%d\n", currentNode);
		initializeWeightMatrix(weight, distance, 1);
		/*node to node distances*/
		initializeDistanceVector();
		int i, j, k;
		for (i = 0; i < MAX; ++i) {
			GlobalEnergyMatrix[i] = 10.0;
		}
		printf("FINISH-4 \n ");
		void* packetPointerStart = packetbuf_dataptr();
		static uint16_t seqNo = 1;
		while (1) {
			rimeaddr_t addr;
//    PROCESS_WAIT_EVENT_UNTIL(ev == sensors_event &&
//			     data == &button_sensor);
			etimer_set(&et,
					CLOCK_SECOND * 4 + random_rand() % (CLOCK_SECOND * 4));

			PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

			if (currentNode == 0) {
				/*Send forward package first */
				/*write path to packetpuf*/
				printf("ENERGIES :\n");
				for (i = 0; i < MAX; ++i) {
					c = GlobalEnergyMatrix[i];
					printf("%ld.%03u\n", (long) c,
							(unsigned) ((c - floor(c)) * 1000));
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

				printf("TEST-CLICK-1 \n ");
				addr.u8[0] = 9;
				addr.u8[1] = 0;
				int8_t nodeId = forwardPathToPackageBuffer(
						rimeAddressToNodeId(addr), 1);
				printf("TEST-CLICK-2 \n ");
				if (nodeId != -1 && !rimeaddr_cmp(&addr, &rimeaddr_node_addr)) {
					drainBatery(
							calculateEnegeryByDistance(
									distance[currentNode][nodeId]));
					printf("node-id %d\n", nodeId);
					addr = getRimeAddressOfNode(nodeId);
					unicast_send(&uc, &addr);
				}
				printf("TEST-CLICK-2 \n ");

			}
			/*clear packetBuf to ensure that all information about forwarding was deleted.*/
			if (validateShareEnergy()) {
				packetbuf_clear();
				/*write energy information to packetBuf*/
				energyInfoToPacketBuf();
				/*then send new energy level*/
				broadcast_send(&broadcast);
				lastSharedEnergyLevel=bateryLevel;
				drainBatery(calculateEnegeryByDistance(localDistance));
				printf("broadcast message sent\n");
				roundCount = 0;
				seqNo = seqNo + 1;
			}
			roundCount = roundCount + 1;
		}
	PROCESS_END();
}
/*---------------------------------------------------------------------------*/
