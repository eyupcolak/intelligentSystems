#include "contiki.h"
#include "lib/list.h"
#include "lib/memb.h"
#include "lib/random.h"
#include "net/rime/rime.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "dev/button-sensor.h"

#define INFINITE 1000000.0
/* This is the structure of broadcast messages. */
struct broadcast_message {
  uint8_t seqno;
};

/* This is the structure of unicast ping messages. */
struct netflood_message {
  uint8_t type;
};

/* These are the types of unicast messages that we can send. */
enum {
  NETFLOOD_TYPE_TOPOLOGY,
  NETFLOOD_TYPE_ENERGY
};
typedef struct NodeAdjecent{
	uint8_t nodeId;
	uint8_t* adjecentVector;
	uint8_t vectorSize;
	struct NodeAdjecent *next;
};

struct NodeAdjecent* listTopology;
struct NodeAdjecent* topologyCurrentNode;
uint8_t topologyNodeCount=1;
static int currentNode;
static float**  topologyMatrix;
/* This structure holds information about neighbors. */
struct neighbor {
  /* The ->next pointer is needed since we are placing these on a
     Contiki list. */
  struct neighbor *next;

  /* The ->addr field holds the Rime address of the neighbor. */
  linkaddr_t addr;

  /* The ->last_rssi and ->last_lqi fields hold the Received Signal
     Strength Indicator (RSSI) and CC2420 Link Quality Indicator (LQI)
     values that are received for the incoming broadcast packets. */
  uint16_t last_rssi, last_lqi;

  /* Each broadcast packet contains a sequence number (seqno). The
     ->last_seqno field holds the last sequenuce number we saw from
     this neighbor. */
  uint8_t last_seqno;

  /* The ->avg_gap contains the average seqno gap that we have seen
     from this neighbor. */
  uint32_t avg_seqno_gap;

};

/* This #define defines the maximum amount of neighbors we can remember. */
#define MAX_NEIGHBORS 16

/* This MEMB() definition defines a memory pool from which we allocate
   neighbor entries. */
MEMB(neighbors_memb, struct neighbor, MAX_NEIGHBORS);

/* The neighbors_list is a Contiki list that holds the neighbors we
   have seen thus far. */
LIST(neighbors_list);

/* These hold the broadcast and unicast structures, respectively. */
static struct broadcast_conn broadcast;

/* These two defines are used for computing the moving average for the
   broadcast sequence number gaps. */
#define SEQNO_EWMA_UNITY 0x100
#define SEQNO_EWMA_ALPHA 0x040

/*---------------------------------------------------------------------------*/
/* We first declare our two processes. */
PROCESS(broadcast_process, "Broadcast process");

/* The AUTOSTART_PROCESSES() definition specifices what processes to
   start when this module is loaded. We put both our processes
   there. */
AUTOSTART_PROCESSES(&broadcast_process);
/*---------------------------------------------------------------------------*/

static int8_t rimeAddressToNodeId(linkaddr_t addr) {
	return (addr.u8[0]+(addr.u8[1]*10))-1;
}
static uint8_t* getNeighbourArrayFromList() {

	if (list_length(neighbors_list) > 0) {
		struct neighbor *n;
	int i;
		uint8_t* neighbourArray = (uint8_t *) malloc(list_length(neighbors_list) * sizeof(uint8_t));
		n = list_head(neighbors_list);
		neighbourArray[0] = rimeAddressToNodeId(n->addr);
		for (i = 1; i < list_length(neighbors_list); i++) {
			n = list_item_next(n);
			if (n != NULL) {
				neighbourArray[i] = rimeAddressToNodeId(n->addr);
			}
		}
		return neighbourArray;
	}
	return NULL;
}
static int checkListContainsNodeInfo(uint8_t nodeID){
	struct NodeAdjecent* nodePtr;
	nodePtr=listTopology;
	while(nodePtr!=NULL){
		if (nodePtr->nodeId==nodeID) {
			return 1;
		}
		nodePtr=nodePtr->next;
	}
	return 0;
}

static float** generateTopologyMatrix(){
	int i,j;
	/*Allocate space for Matrix*/
	float **mat = (float **) malloc(topologyNodeCount* sizeof(float*));
	for ( i = 0; i < topologyNodeCount; i++){
			mat[i] = (float *) malloc(topologyNodeCount * sizeof(float));
	}
	for (i = 0; i < topologyNodeCount; ++i) {
		for (j = 0; j < topologyNodeCount; ++j) {
			mat[i][j]=INFINITE;
		}
	}
	struct NodeAdjecent* nodePtr;
	nodePtr=listTopology;
	while(nodePtr!=NULL){
		for (i = 0; i < nodePtr->vectorSize; ++i) {
			mat[nodePtr->nodeId][nodePtr->adjecentVector[i]]=3.0;
		}
		nodePtr=nodePtr->next;
	}
	return mat;
}



/* This function is called whenever a broadcast message is received. */
static void
broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from)
{
  struct neighbor *n;
  struct broadcast_message *m;
  uint8_t seqno_gap;

  /* The packetbuf_dataptr() returns a pointer to the first data byte
     in the received packet. */
  m = packetbuf_dataptr();

  /* Check if we already know this neighbor. */
  for(n = list_head(neighbors_list); n != NULL; n = list_item_next(n)) {

    /* We break out of the loop if the address of the neighbor matches
       the address of the neighbor from which we received this
       broadcast message. */
    if(rimeaddr_cmp(&n->addr, from)) {
      break;
    }
  }

  /* If n is NULL, this neighbor was not found in our list, and we
     allocate a new struct neighbor from the neighbors_memb memory
     pool. */
  if(n == NULL) {
    n = memb_alloc(&neighbors_memb);

    /* If we could not allocate a new neighbor entry, we give up. We
       could have reused an old neighbor entry, but we do not do this
       for now. */
    if(n == NULL) {
      return;
    }

    /* Initialize the fields. */
    rimeaddr_copy(&n->addr, from);
    n->last_seqno = m->seqno - 1;
    n->avg_seqno_gap = SEQNO_EWMA_UNITY;

    /* Place the neighbor on the neighbor list. */
    list_add(neighbors_list, n);
  }

  /* We can now fill in the fields in our neighbor entry. */
  n->last_rssi = packetbuf_attr(PACKETBUF_ATTR_RSSI);
  n->last_lqi = packetbuf_attr(PACKETBUF_ATTR_LINK_QUALITY);

  /* Compute the average sequence number gap we have seen from this neighbor. */
  seqno_gap = m->seqno - n->last_seqno;
  n->avg_seqno_gap = (((uint32_t)seqno_gap * SEQNO_EWMA_UNITY) *
                      SEQNO_EWMA_ALPHA) / SEQNO_EWMA_UNITY +
                      ((uint32_t)n->avg_seqno_gap * (SEQNO_EWMA_UNITY -
                                                     SEQNO_EWMA_ALPHA)) /
    SEQNO_EWMA_UNITY;

  /* Remember last seqno we heard. */
  n->last_seqno = m->seqno;

  /* Print out a message. */
  printf("broadcast message received from %d.%d with seqno %d, RSSI %u, LQI %u, avg seqno gap %d.%02d\n",
         from->u8[0], from->u8[1],
         m->seqno,
         packetbuf_attr(PACKETBUF_ATTR_RSSI),
         packetbuf_attr(PACKETBUF_ATTR_LINK_QUALITY),
         (int)(n->avg_seqno_gap / SEQNO_EWMA_UNITY),
         (int)(((100UL * n->avg_seqno_gap) / SEQNO_EWMA_UNITY) % 100));
}
/* This is where we define what function to be called when a broadcast
   is received. We pass a pointer to this structure in the
   broadcast_open() call below. */
static const struct broadcast_callbacks broadcast_call = {broadcast_recv};
/*---------------------------------------------------------------------------*/
/* This function is called for every incoming unicast packet. */
static void
trickle_recv(struct trickle_conn *c){
	struct netflood_message msg;
	void* packetBufPointer = packetbuf_dataptr();
	memcpy(&msg, packetBufPointer, sizeof(struct netflood_message));
	packetBufPointer += sizeof(struct netflood_message);
	struct NodeAdjecent* nodePtr=listTopology;
	if (msg.type == NETFLOOD_TYPE_TOPOLOGY) {
		int8_t senderNode;
		memcpy(&senderNode, packetBufPointer, sizeof(uint8_t));
		if (!checkListContainsNodeInfo(senderNode)) {
			uint8_t neighbourCount;
			struct NodeAdjecent* newNode;
			memcpy(&neighbourCount, packetBufPointer, sizeof(uint8_t));
			packetBufPointer += sizeof(uint8_t);
			uint8_t* neighbourArray=(uint8_t *)malloc(neighbourCount*sizeof(uint8_t));
			printf("NODE COUNT : %d :\n"  ,neighbourCount);
			memcpy(neighbourArray, packetBufPointer,
					neighbourCount * sizeof(uint8_t));
			int i;
			printf("NEIGHBOURS :\n");
			for (i= 0; i< neighbourCount; ++i) {
				printf("%d----",neighbourArray[i]);
			}
			printf("\n");
			/*add new node info to list*/
			newNode=malloc(sizeof(struct NodeAdjecent));
			newNode->adjecentVector=neighbourArray;
			newNode->nodeId=senderNode;
			newNode->vectorSize=neighbourCount;
			topologyCurrentNode->next=newNode;
			topologyCurrentNode=newNode;
			topologyNodeCount++;
			printf("TOPOLOGY NODE COUNT :%d\n",topologyNodeCount);
			while(nodePtr!=NULL){
				printf("NODE-ID : %d-- Neighbours :" ,nodePtr->nodeId+1);
				for (i = 0; i < nodePtr->vectorSize; ++i) {
					printf("----%d",nodePtr->adjecentVector[i]+1);
				}
				printf("\n");
				nodePtr=nodePtr->next;
			}
			printf("\n");



			printf("broadcast message received from %d:  DENEME : '%d'\n",
							senderNode+1, msg);
					printf("RECIEVED\n");
		}
	}
}
static int count=0;
const static struct trickle_callbacks trickle_call = {trickle_recv};
static struct trickle_conn trickle;
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(broadcast_process, ev, data)
{
  static struct etimer et;
  static uint8_t seqno;
  struct broadcast_message msg;
  struct netflood_message msg2;

  PROCESS_EXITHANDLER(broadcast_close(&broadcast);)

  PROCESS_BEGIN();

  broadcast_open(&broadcast, 129, &broadcast_call);
  trickle_open(&trickle, 5, 145, &trickle_call);
  currentNode = rimeAddressToNodeId(linkaddr_node_addr);
  while(count<5) {

    /* Send a broadcast every 16 - 32 seconds */
    etimer_set(&et, CLOCK_SECOND * 16 + random_rand() % (CLOCK_SECOND * (16+currentNode)));

    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
    printf("COUNT: %d\n",count);
    msg.seqno = seqno;
    packetbuf_copyfrom(&msg, sizeof(struct broadcast_message));
    broadcast_send(&broadcast);
    seqno++;
    count++;

  }
  seqno=0;
  count=0;

  /*initalize topology data model*/
  listTopology=malloc(sizeof(struct NodeAdjecent));
  listTopology->adjecentVector=getNeighbourArrayFromList();
  listTopology->nodeId=currentNode;
  listTopology->vectorSize=list_length(neighbors_list);
  topologyCurrentNode=listTopology;
  printf("vectorSize: %d\n",listTopology->vectorSize);
  printf("vectorSize2: %d\n",list_length(neighbors_list));

	struct NodeAdjecent* nodePtr;
	int i,j;
	float c;
//			printf("NEIGHBOURS :\n");
//			for (i= 0; i< listTopology->vectorSize; ++i) {
//				printf("%d----",listTopology->adjecentVector[i]);
//			}
//			printf("\n");

  SENSORS_ACTIVATE(button_sensor);

  if(currentNode==1) {

    /* Send a broadcast every 16 - 32 seconds */
    etimer_set(&et, CLOCK_SECOND * 40*currentNode );

    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
//	    PROCESS_WAIT_EVENT_UNTIL(ev == sensors_event &&
//				     data == &button_sensor);
    printf("COUNT: %d\n",count);
    msg2.type = NETFLOOD_TYPE_TOPOLOGY;
    uint8_t neighbourCount=listTopology->vectorSize;
    packetbuf_clear();
	void* bufferPointer = packetbuf_dataptr();
	/*first message*/
	memcpy(bufferPointer, &msg2, sizeof(struct netflood_message));
	bufferPointer += sizeof(struct netflood_message);
	/*orginator node */
	memcpy(bufferPointer, &currentNode, sizeof(uint8_t));
	/*neighbour count*/
	printf("Neighbour Count : %d :\n",neighbourCount);
	memcpy(bufferPointer, &neighbourCount, sizeof(uint8_t));
	bufferPointer += sizeof(uint8_t);
	/*neighbour array*/
	memcpy(bufferPointer, &listTopology->adjecentVector[0],listTopology->vectorSize*sizeof(uint8_t));
	printf("listTopology->vectorSize : %d :\n",listTopology->vectorSize);
			printf("NEIGHBOURS :\n");
			for (i= 0; i< neighbourCount; ++i) {
				printf("%d----",listTopology->adjecentVector[i]);
			}
			printf("\n");
	packetbuf_set_datalen((sizeof(uint8_t)*(neighbourCount +2 ))+sizeof(struct netflood_message));

	trickle_send(&trickle);
    seqno++;
    count++;

  }
  printf("WAITING PERIOD FOR ALL INCOMING MESSAGES \n");
  etimer_set(&et, CLOCK_SECOND * 120 + random_rand() % (CLOCK_SECOND * 15));

   PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
   printf("WAITING PERIOD OVER ,CREATING TOPOLOGY \n");
   topologyMatrix=generateTopologyMatrix();
   printf("TOPOLOGY NODE COUNT :%d\n",topologyNodeCount);
   printf("TOPOLOGY MATRIX :\n");

	for (i = 0; i <topologyNodeCount; ++i) {
			for (j = 0; j < topologyNodeCount; ++j) {
				c = topologyMatrix[i][j];
				printf("------%ld.%03u", (long) c,
									(unsigned) ((c - floor(c)) * 1000));
			}
			printf("\n");
		}

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/

