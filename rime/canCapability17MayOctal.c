/**
 * \author cnygn
 */

#include "contiki.h"
#include "net/rime/rime.h"
#include "random.h"
#include "leds.h"
#include "dev/button-sensor.h"

#include "dev/leds.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

/*---------------------------------------------------------------------------*/
PROCESS(example_broadcast_process, "Broadcast example");
AUTOSTART_PROCESSES(&example_broadcast_process);
/*---------------------------------------------------------------------------*/

static int delayTime;

struct taskMessage {
	int8_t nodeNo;
	int8_t delay;
};
typedef struct taskMessage taskMessage;

/*capability message format*/
struct messageFormat {
	int8_t messageCode;
	int8_t clusterNo;
	int8_t* skillPtr;
	taskMessage* taskMessagePtr;

};
typedef struct messageFormat messageFormat;
typedef struct member {
	int8_t memberNodeId;
	int8_t skillList[3];
	int8_t matchSkillcount;
	int8_t selectedForCandidate;
	float residualEnergy;
	struct member* next;
} member;
typedef struct workCandidate {
	int8_t nodeArray[3];
	int8_t delay;
	float residualPeriod;
	struct workCandidate* next;
} workCandidate;
/**/

static int8_t currentNode;
static int clusterNo;
static int targetId;
static int8_t clusterHeadNo;

taskMessage senderTask;
static int packageLength;
static int residualEnergy = 1000;
static int workPeriodLength = 9;
static int subWorkPeriodLength = 3;
static int skillCount = 3;
static workCandidate* candidateLstStartPtr;
static member* memberPtr;
/*member p*/
static member* memberGroupPtr[3];
static int messageSendCost = 10;
static int sensingCost = 2;
static int messageSendCostFloat = 10.00;
static int chChange = 0;
static int chChangeTreshold = 800;
static int8_t workLoopCount = 5;
static int isNodeLive = 1;
static int newClusterHead = -1;
static int sinkID=10;
/*3 capability üzerinden olusturuldu.*/
/**NON UNIFORM */
static int8_t nodeCapabilityArray[50][3] = {
		{ 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 },
		{ 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 },
		{ 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 },
		{ 0, 0, 1 }, { 1, 1, 1 }, { 0, 0, 1 },
		{ 1, 0, 0 }, { 1, 1, 0 }, { 1, 1, 1 }, { 0, 0, 1 }, { 1, 0, 0 }, { 1, 1, 0 }, { 1, 1, 1 }, { 1, 0, 1 }, { 1, 1, 0 }, { 1, 1, 0 }, { 1, 1, 1 }, { 0, 0, 1 }, { 1, 0, 0 }, { 1, 1, 0 },
		{ 1, 1, 1 }, { 0, 0, 1 }, { 1, 0, 0 }, { 1, 1, 0 }, { 1, 1, 1 }, { 0, 0, 1 }, { 1, 0, 0 }, { 1, 1, 0 }, { 1, 1, 1 }, { 0, 0, 1 }, { 1, 0, 0 }, { 1, 1, 0 }, { 1, 1, 1 }, { 1, 0, 1 },
		{ 1, 0, 0 }, { 1, 1, 0 }, { 1, 1, 1 }, { 0, 0, 1 }, { 1, 0, 0 }, { 1, 1, 0 }, { 1, 1, 1 }, { 0, 0, 1 }, { 1, 0, 0 }, { 1, 1, 0 } };
/**UNIFORM */
/*static int8_t nodeCapabilityArray[20][3] = {
 { 1, 1, 1 }, { 1, 1, 1 }, { 1, 1, 1 }, { 1, 1, 1 },
 { 1, 1, 1 }, { 1, 1, 1 }, { 1, 1, 1 }, { 1, 1, 1 },
 { 1, 1, 1 }, { 1, 1, 1 }, { 1, 1, 1 }, { 1, 1, 1 },
 { 1, 1, 1 }, { 1, 1, 1 }, { 1, 1, 1 }, { 1, 1, 1 },
 { 1, 1, 1 }, { 1, 1, 1 }, { 1, 1, 1 }, { 1, 1, 1 } };*/

static int8_t targetRequiredSensorArray[4][3] = { { 1, 1, 1 }, { 1, 1, 1 }, { 1, 1, 1 }, { 1, 1, 1 } };

static int8_t rimeAddressToNodeId(linkaddr_t addr) {
	return (addr.u8[0] + (addr.u8[1] * 10)) - 1;
}
static int8_t getMatchedSkillCount(int8_t nodeId, int8_t targetId) {
	int8_t matchCount = 0;
	int i;
	for (i = 0; i < 3; ++i) {
		if (nodeCapabilityArray[nodeId][i] == 1 && targetRequiredSensorArray[targetId][i] == 1) {
			matchCount++;
		}
	}
	return matchCount;
}
static void printffloat(float c) {
	//printf("%ld.%03u\n", (long) c, (unsigned) ((c - floor(c)) * 1000));
}
void printCandit() {
	printf("#####CANDIDATES------------------------------------------------\n");
	int i;
	workCandidate* candidatePtr = candidateLstStartPtr;
	while (candidatePtr != NULL) {
		printf("canidates : ");
		for (i = 0; i < 3; ++i) {
			if (candidatePtr->nodeArray[i] != -1) {
				printf("- %d -", candidatePtr->nodeArray[i]);
			}

		}
		printf("\n");
		printf("residual period ");
		printffloat(candidatePtr->residualPeriod);
		candidatePtr = candidatePtr->next;
	}
}
static int addCandidateToList(workCandidate* newCandidate) {
	/*DESC. order linked list*/
	if (newCandidate == NULL || newCandidate->residualPeriod < 1) {
		return 0;
	}
	newCandidate->next = NULL;
	if (candidateLstStartPtr == NULL) {
		candidateLstStartPtr = newCandidate;
		candidateLstStartPtr->next = NULL;
		return 1;
	}
	int candidateHead = newCandidate->nodeArray[0];
	int i;

	//TODO : sucsess se silme partini gec int succsess = 0;
	workCandidate* currentMemberPtr = candidateLstStartPtr;
	workCandidate* previousMemberPtr = NULL;
	while (currentMemberPtr != NULL) {
		for (i = 0; i < 3; ++i) {
			if (currentMemberPtr->nodeArray[i] == candidateHead) {
				//daha oncede bir candidate icinde kullanilmistir.
				if (newCandidate->residualPeriod > currentMemberPtr->residualPeriod) {
					//delete old candidate
					previousMemberPtr->next = currentMemberPtr->next;
					free(currentMemberPtr);
					break;
				} else {
					/*eski aday var ama yenide daha yuksek enerjili o yuzden yeni eklenmez*/
					return 0;
				}
			}
		}
		previousMemberPtr = currentMemberPtr;
		currentMemberPtr = currentMemberPtr->next;
	}
	currentMemberPtr = candidateLstStartPtr;
	previousMemberPtr = NULL;
	while (currentMemberPtr != NULL) {
		if (newCandidate->residualPeriod <= currentMemberPtr->residualPeriod) {
			previousMemberPtr = currentMemberPtr;
			currentMemberPtr = currentMemberPtr->next;
			/*son elemana denk gelindiyse*/
			if (currentMemberPtr == NULL) {
				previousMemberPtr->next = newCandidate;
				break;
			}
		} else {
			if (previousMemberPtr == NULL) {
				newCandidate->next = currentMemberPtr;
				candidateLstStartPtr = newCandidate;
			} else {
				newCandidate->next = previousMemberPtr->next;
				previousMemberPtr->next = newCandidate;
			}
			break;
		}
	}
	return 1;
}
static void flagNodeAsSelected(int8_t* nodeArray) {
	int i, j;
	int allFound = 0;
	int candidateNodeCount = 0;
	for (i = 0; i < 3; ++i) {
		if (nodeArray[i] > -1) {

			candidateNodeCount++;
		}
	}
	for (i = 0; i < 3; ++i) {
		member* offset = memberGroupPtr[i];
		if (offset != NULL) {
			while (offset != NULL) {
				for (j = 0; j < 3; ++j) {
					if (offset->memberNodeId == nodeArray[j]) {
						offset->selectedForCandidate = 1;
						allFound++;
						if (allFound == candidateNodeCount) {
							return;
						}
					}
				}
				offset = offset->next;
			}
		}

	}
}
static void setResidualPeriodForCandidate(workCandidate* candidatePtr) {
	int i;
	float candidateC = 0.0;
	if (candidatePtr != NULL) {
		for (i = 0; i < 3; ++i) {
			if (candidatePtr->nodeArray[i] != -1) {
				candidateC++;
			}
		}
		if (candidateC > 0) {
			candidatePtr->residualPeriod = candidatePtr->residualPeriod / (messageSendCostFloat * candidateC);
		} else {
			candidatePtr->residualPeriod = 0.0;
		}
	}
}
static void initializeWorkCanidateNodeList(workCandidate* candidate) {
	int8_t i;
	for (i = 0; i < 3; ++i) {
		candidate->nodeArray[i] = -1;
	}

}

static void generateCandidateList() {
	if (candidateLstStartPtr == NULL) {
		/*min required skill iceren candidatelerden baslanarak , residual energy based scheduling yapilacak*/
		int i, j, counter;
		int selectedCandidateCount = 0;
		int totalSelectedCandidateCount = 0;
		member* coupleMemberPtr;
		int8_t tempSkillArray[3];
		int8_t skillTotal;
		int matchCount;
		int matchIndex;
		int selecterNodeOffset;

		workCandidate* previousCandidate;
		workCandidate* currentCandidate;
		/*this part search for best matches*/
		/*eger ilk turda yeterli sayida candidate bulunamazsa handicap la 1 tur daha yapilir.*/
		for (i = 2; i > -1; i--) {
			member* memberPtrStart = memberGroupPtr[i];
			while (memberPtrStart != NULL) {
				if (memberPtrStart->selectedForCandidate == 0) {
					selecterNodeOffset = 1;
					//TODO : memberPtrStart->nodeSelectedForCandidate == 0 ayni dugumun daha kotu kombinasyonlarda kullanilmasini engeller
					//burasi acilisa nodeSelectedForCandidate ==1 olan node daha onceki adaylarda kendini iceren canidate i bulup bunu canidate listten silmeli eger
					//enerjisi daha dusukse degilse bu node icin bulunan canidate ,candidate liste eklenmeyecek
					//eksik skill varsa tamamlamla
					currentCandidate = NULL;
					previousCandidate = NULL;
					if (memberPtrStart->matchSkillcount < skillCount) {
						matchIndex = 0;
						while (matchIndex <= i) {
							memcpy(tempSkillArray, memberPtrStart->skillList, sizeof(memberPtrStart->skillList));
							skillTotal = memberPtrStart->matchSkillcount;
							coupleMemberPtr = memberGroupPtr[matchIndex];
							if (matchIndex == i) {
								counter = 0;
								while (counter < selecterNodeOffset) {
									counter++;
									coupleMemberPtr = coupleMemberPtr->next;
								}
							}
							/*select candidate with highest energy*/
							while (coupleMemberPtr != NULL) {
								if (coupleMemberPtr->memberNodeId != memberPtrStart->memberNodeId && coupleMemberPtr->selectedForCandidate == 0) {
									matchCount = 0;
									/*compare skills*/
									for (j = 0; j < skillCount; ++j) {
										if (coupleMemberPtr->skillList[j] == 1 && !tempSkillArray[j] && coupleMemberPtr->skillList[j]) {
											tempSkillArray[j] = coupleMemberPtr->skillList[j];
											matchCount++;
											skillTotal++;
										}
									}
									if (matchCount > 0) {
										if (currentCandidate == NULL) {
											currentCandidate = malloc(sizeof(workCandidate));
											initializeWorkCanidateNodeList(currentCandidate);
											currentCandidate->nodeArray[0] = memberPtrStart->memberNodeId;
											currentCandidate->residualPeriod = memberPtrStart->residualEnergy;
											currentCandidate->nodeArray[1] = coupleMemberPtr->memberNodeId;
											currentCandidate->residualPeriod += coupleMemberPtr->residualEnergy;
											//aday adaylari listesine ekle
										} else {
											currentCandidate->nodeArray[2] = coupleMemberPtr->memberNodeId;
											currentCandidate->residualPeriod += coupleMemberPtr->residualEnergy;
										}
									}
								}
								if (skillTotal == skillCount) {
									setResidualPeriodForCandidate(currentCandidate);
									break;
								}
								coupleMemberPtr = coupleMemberPtr->next;
							}
							//bu gruptan hic bir node secilemedi.
							if (skillTotal != skillCount) {
								free(currentCandidate);
							} else {
								if (previousCandidate != NULL) {
									if (currentCandidate->residualPeriod > previousCandidate->residualPeriod) {
										free(previousCandidate);
										previousCandidate = currentCandidate;
									} else {
										free(currentCandidate);
									}
								} else {
									previousCandidate = currentCandidate;
								}
								currentCandidate = NULL;
							}
							matchIndex++;
						}
						if (addCandidateToList(previousCandidate)) {
							selectedCandidateCount++;
							totalSelectedCandidateCount++;
							//elemanlari isaretle ve listeyi temizle
							flagNodeAsSelected(previousCandidate->nodeArray);
						}
					} else {
						currentCandidate = malloc(sizeof(workCandidate));
						currentCandidate->residualPeriod = memberPtrStart->residualEnergy / messageSendCost;
						initializeWorkCanidateNodeList(currentCandidate);
						currentCandidate->nodeArray[0] = memberPtrStart->memberNodeId;
						if (addCandidateToList(currentCandidate)) {
							selectedCandidateCount++;
							totalSelectedCandidateCount++;
						}
					}
				}
				memberPtrStart = memberPtrStart->next;
				selecterNodeOffset++;
			}

		}
	}
}

static void* writeTaskListToBuffer(void* bufferPtr) {
	/*worker selection*/
	generateCandidateList();
	workCandidate* candidatePtr = candidateLstStartPtr;
	taskMessage* messagePtr;
	int selectedNodeCount = 0;
	int i;
	int periodTotal = 0;
	/*suan kac elemean secilecegini bilmiyoruz ama mesajlasma deseninde en basta kac eleman oldugu var o yuzden en basta
	 * secilecek eleman sayisini yazmak icin yer hazirladik.
	 * */
	void* bufferPtrStart = bufferPtr;
	bufferPtr = bufferPtr + sizeof(int8_t);
	packageLength += sizeof(int8_t);
	while (candidatePtr != NULL && periodTotal <= workPeriodLength - subWorkPeriodLength) {
		for (i = 0; i < 3; ++i) {
			if (candidatePtr->nodeArray[i] > -1) {
				messagePtr = malloc(sizeof(taskMessage));
				messagePtr->delay = periodTotal;
				messagePtr->nodeNo = candidatePtr->nodeArray[i];
				memcpy(bufferPtr, messagePtr, sizeof(taskMessage));
				bufferPtr = bufferPtr + sizeof(taskMessage);
				selectedNodeCount++;
				packageLength += sizeof(taskMessage);
			}
		}
		periodTotal += subWorkPeriodLength;
		candidatePtr = candidatePtr->next;
	}
	if (selectedNodeCount == 0) {
		/*hata target izlenemiyor.*/
		printf("ALAN IZLENEMIYOR\n");
		//exit(1);
	} else {
		memcpy(bufferPtrStart, &selectedNodeCount, sizeof(int8_t));
	}
	return bufferPtr;
}
static void writeMessageToPackageBuffer(int messageCode) {
	/*clean packet buffer*/
	packetbuf_clear();
	packageLength = 0;
	void* bufferPointer = packetbuf_dataptr();
	int8_t* skillPtr = nodeCapabilityArray[currentNode];

	memcpy(bufferPointer, &clusterNo, sizeof(int8_t));
	bufferPointer = bufferPointer + sizeof(int8_t);
	packageLength += sizeof(int8_t);
	memcpy(bufferPointer, &messageCode, sizeof(int8_t));
	bufferPointer = bufferPointer + sizeof(int8_t);
	packageLength += sizeof(int8_t);

	if (messageCode == 1) {
		/*CA MESSAGE ENCODING*/
		memcpy(bufferPointer, &clusterHeadNo, sizeof(int8_t));
		bufferPointer = bufferPointer + sizeof(int8_t);
		packageLength += sizeof(int8_t);

		memcpy(bufferPointer, skillPtr, 3 * sizeof(int8_t));
		bufferPointer = bufferPointer + 3 * sizeof(int8_t);
		packageLength = packageLength + 3 * sizeof(int8_t);
		memcpy(bufferPointer, &residualEnergy, sizeof(int));
		packageLength += sizeof(int);
//		printf("Package Length =  %d\n", packageLength);
//		printf("CAPABILITY EXCHANGE MESSAGE SEND\n");
	} else if (messageCode == 2) {
		memcpy(bufferPointer, &currentNode, sizeof(int8_t));
		bufferPointer = bufferPointer + sizeof(int8_t);
		packageLength += sizeof(int8_t);

	} else if (messageCode == 3) {
		/*WA MESSAGE ENCODING*/
		bufferPointer = writeTaskListToBuffer(bufferPointer);
	} else if (messageCode == 4) {
		memcpy(bufferPointer, &newClusterHead, sizeof(int8_t));
		bufferPointer = bufferPointer + sizeof(int8_t);
		packageLength += sizeof(int8_t);

	}
	/*MESSAGE CODE =4 CC- CH CHANGE MESSAGE ENCODING*/
	/*MESSAGE CODE =5 AC- AREA COVERAGE MESSAGE ENCODING*/
	/*MESSAGE CODE =6 ND- NODE DEATH MESSAGE ENCODING*/
	/*MESSAGE CODE =7 CHALIVE MESSAGE */
	packetbuf_set_datalen(packageLength);
}
static void clearMemory() {
	/*clear all unused memory to prevent memory leaks*/
	int i;
	for (i = 0; i < 3; ++i) {
		member* memberPtrStart = memberGroupPtr[i];
		while (memberPtrStart != NULL) {
			member* deletePtr = memberPtrStart;
			memberPtrStart = memberPtrStart->next;
			free(deletePtr);
		}
		memberGroupPtr[i] = NULL;
	}
	workCandidate* canidateDeletePtr = candidateLstStartPtr;
	while (candidateLstStartPtr != NULL) {
		canidateDeletePtr = candidateLstStartPtr;
		candidateLstStartPtr = candidateLstStartPtr->next;
		free(canidateDeletePtr);
	}
	memberPtr = NULL;
}
static void addMemberWithResidualEnergyOrder(member* newMember, int index) {
	/*DESC. order linked list*/
	member* memberPtrStart = memberGroupPtr[index];
	if (newMember == memberPtrStart || newMember == NULL) {
		return;
	} else {
		member* currentMemberPtr = memberPtrStart;
		member* previousMemberPtr = NULL;
		while (currentMemberPtr != NULL) {
			if (newMember->residualEnergy <= currentMemberPtr->residualEnergy) {
				previousMemberPtr = currentMemberPtr;
				currentMemberPtr = currentMemberPtr->next;
				/*son elemana denk gelindiyse*/
				if (currentMemberPtr == NULL) {
					previousMemberPtr->next = newMember;
					break;
				}
			} else {
				if (previousMemberPtr == NULL) {
					newMember->next = currentMemberPtr;
					memberGroupPtr[index] = newMember;
				} else {
					newMember->next = previousMemberPtr->next;
					previousMemberPtr->next = newMember;
				}
				break;
			}
		}
	}
	return;
}
static int checkMemberListContainsTheNode(int nodeId) {
	int i;
	for (i = 0; i < 3; ++i) {
		member* memberChecker = memberGroupPtr[i];
		while (memberChecker != NULL) {
			if (memberChecker->memberNodeId == nodeId) {
				return 1;
			}
			memberChecker = memberChecker->next;
		}
	}
	return 0;
}
static void printMemberList() {
	int memberCount = 0;
	int i;
	for (i = 0; i < 3; ++i) {
		member* offset = memberGroupPtr[i];
		if (offset != NULL) {
			printf("%d.matched skill group----------------------\n", i + 1);
			while (offset != NULL) {
				printf("NodeId: %d ---->", offset->memberNodeId);
				printf("residual Energy ---->");
				printffloat(offset->residualEnergy);
				printf("%.2f\n", offset->residualEnergy);
				printf("Skill List ----> ");
				int i;
				for (i = 0; i < 3; ++i) {
					printf("%d - ", offset->skillList[i]);
				}
				printf("\n");
				offset = offset->next;
				memberCount++;
			}
		}
		printf("CLUSTER MEMBER COUNT: %d\n", memberCount);
	}
}
static int8_t selectNewClusterHead() {
	int i;
	float maxResidualEnergy=0;
	member* result;
	for (i = 2; i > -1; i--) {
		member* offset = memberGroupPtr[i];
		if (offset != NULL) {
			while (offset != NULL) {
				if (offset->residualEnergy >maxResidualEnergy) {
					result=offset;
					maxResidualEnergy=result->residualEnergy;
				} else {
					offset = offset->next;
				}

			}
		}
	}

	// mark member as selected to not include work list
	result->selectedForCandidate=1;
	return result->memberNodeId;
}


static void recv_general(const linkaddr_t *from){
	int8_t senderNodeId = rimeAddressToNodeId(*from);
	void* ptr = packetbuf_dataptr();
	/*validate if message coming from same cluster*/
	int8_t senderClusterNo;
	int8_t senderMessageCode;
	int8_t senderSkillArray[3];
	int8_t senderClusterHeadNo;
	int8_t senderElementCount;
	int8_t senderNewClusterHead;
	int senderResidualEnergy;
	memcpy(&senderClusterNo, ptr, sizeof(int8_t));
	ptr = ptr + sizeof(int8_t);
	if (senderClusterNo != clusterNo) {
		/*HATALI*/
		return;
	} else {
		memcpy(&senderMessageCode, ptr, sizeof(int8_t));
		ptr = ptr + sizeof(int8_t);
		if (senderMessageCode == 1 && clusterHeadNo == -2) {

			memcpy(&senderClusterHeadNo, ptr, sizeof(int8_t));
			printf("CAPABILITY MESSAGE COME from : %d -- SENDER CH : %d	\n",senderNodeId,senderClusterHeadNo);
			//printf("senderClusterHeadNo : %d	\n", senderClusterHeadNo);
			ptr = ptr + sizeof(int8_t);
			if (senderClusterHeadNo == currentNode) {
				//printf("DECODING --- CAPABILITIES\n");
				//printf("Package Length =  %d\n", packageLength);
				/*CA  message*/
				memcpy(&senderSkillArray[0], ptr, 3 * sizeof(int8_t));
				ptr = ptr + 3 * sizeof(int8_t);
				int8_t senderMatchedSkillCount = getMatchedSkillCount(senderNodeId, targetId);

				if (senderSkillArray != NULL) {
					if (memberGroupPtr[senderMatchedSkillCount - 1] == NULL) {
						memberPtr = malloc(sizeof(member));
						memberGroupPtr[senderMatchedSkillCount - 1] = memberPtr;
					} else {
						if (!checkMemberListContainsTheNode(senderNodeId)) {
							memberPtr = malloc(sizeof(member));
						} else {
							return;
						}
					}
					memberPtr->memberNodeId = senderNodeId;
					int i = 0;
					memcpy(&senderResidualEnergy, ptr, sizeof(int));
					ptr = ptr + (sizeof(int));
					memberPtr->residualEnergy = senderResidualEnergy;
					memberPtr->matchSkillcount = getMatchedSkillCount(senderNodeId, targetId);
					memberPtr->selectedForCandidate = 0;
					for (i = 0; i < 3; ++i) {
						memberPtr->skillList[i] = senderSkillArray[i];
					}
					memberPtr->next = NULL;
					addMemberWithResidualEnergyOrder(memberPtr, senderMatchedSkillCount - 1);
					printf("ResidualEnergy After Adding :%d\n",residualEnergy);
				}
			}
			residualEnergy = residualEnergy - (messageSendCost / 2);
		} else if (senderMessageCode == 2 && clusterHeadNo == -1) {
			//printf("CH NOOOOO :%d\n", clusterHeadNo);
			//printf("HEAD SELECTION MESSAGE\n");
			/*clusterHead is senders itself ,nothing to do*/
			memcpy(&clusterHeadNo, ptr, sizeof(int8_t));
			if (clusterHeadNo != currentNode) {
				ptr = ptr + sizeof(int8_t);
				//printf("CLUSTER HEAD SELECTED :  %d\n: ", clusterHeadNo);
				leds_on(LEDS_GREEN);
			} else {
				clusterHeadNo = -1;
			}
			residualEnergy = residualEnergy - (messageSendCost / 2);
		} else if (senderMessageCode == 3 && senderNodeId == clusterHeadNo) {
			memcpy(&senderElementCount, ptr, sizeof(int8_t));
			ptr = ptr + sizeof(int8_t);
			//printf("Element Count : ");
			//printf("%d\n", senderElementCount);
			//printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
			int i = 0;
			for (i = 0; i < senderElementCount; ++i) {
				memcpy(&senderTask, ptr, sizeof(taskMessage));
				//printf("%d.row \NODE NO : %d  DELAY : %d \n", i, senderTask.nodeNo, senderTask.delay);
				if (senderTask.nodeNo == currentNode) {
					delayTime = senderTask.delay;
				}
				ptr = ptr + sizeof(taskMessage);
			}
			//printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
			residualEnergy = residualEnergy - (messageSendCost / 2);

		} else if ((senderMessageCode == 4 && senderNodeId == clusterHeadNo)) {
			//CH change message
			leds_off(LEDS_GREEN);
			memcpy(&senderNewClusterHead, ptr, sizeof(int8_t));
			if (senderNewClusterHead == currentNode) {
				clusterHeadNo = -2;
				leds_on(LEDS_RED);
				clearMemory();
				delayTime = -1;
			} else {
				clusterHeadNo = senderNewClusterHead;
				leds_on(LEDS_GREEN);
			}
			residualEnergy = residualEnergy - (messageSendCost / 2);
		}else if (senderMessageCode == 5 && currentNode==sinkID) {
			printf("COVERAGE MESSAGE COME from : %d -- SENDER CH : %d	\n",senderNodeId,senderClusterHeadNo);
		}else if (senderMessageCode == 6) {
			//CH change message
			leds_off(LEDS_ALL);
			printf("NODE DEAD \n");
			isNodeLive = 0;
			residualEnergy = residualEnergy - (messageSendCost / 2);
		} else if (senderMessageCode == 7) {
			//No response from CH
			chChange = 0;
			leds_on(LEDS_GREEN);
			//member olmaya devam et
			residualEnergy = residualEnergy - (messageSendCost / 2);
		} else {
			/*HATALI*/
			printf("wrong message format \n");
			return;
		}
	}
}


static void recv_uc(struct unicast_conn *c, const linkaddr_t *from) {
	printf("unicast message received from %d.%d\n", from->u8[0], from->u8[1]);
	recv_general(from);
}

static void broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from) {
	recv_general(from);
}

static const struct unicast_callbacks unicast_callbacks = { recv_uc };
static struct unicast_conn uc;

static const struct broadcast_callbacks broadcast_call = { broadcast_recv };
static struct broadcast_conn broadcast;
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(example_broadcast_process, ev, data) {

	PROCESS_EXITHANDLER(broadcast_close(&broadcast)
	;
	)

	PROCESS_BEGIN()
		;
		unicast_open(&uc, 146, &unicast_callbacks);
		broadcast_open(&broadcast, 129, &broadcast_call);

		currentNode = rimeAddressToNodeId(linkaddr_node_addr);

		linkaddr_t addr;

		currentNode = rimeAddressToNodeId(linkaddr_node_addr);
		clusterNo = currentNode / 10;
		targetId = clusterNo;
		static int count = 0;
		static int tourCount = 0;
		static int chWaitTime;
		static struct etimer et;
		static struct etimer smallTimer;
		static struct etimer fixedTimer;
		static struct etimer repeatTimer;
		static int i;
		static int8_t matchedSkillCount;
		static int energyExhausted = 0;
		static int workPeriodCount = 0;
		static struct timer rxtimer;

		if (currentNode == sinkID) {
			while (1) {
				etimer_set(&fixedTimer, CLOCK_SECOND * 10000);
				PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&fixedTimer));
			}
		} else {
			chChange = 1;
			/*period zamanlayicilari*/
			matchedSkillCount = getMatchedSkillCount(currentNode, targetId);

			/***********CLUSTER HEAD SELECTION PERIOD *******************/
			etimer_set(&fixedTimer, CLOCK_SECOND * 35);
			if (chChange) {
				leds_off(LEDS_ALL);
				clusterHeadNo = -1;
				printf("CH ADVERTISEMENT PERIOD START --  matched skill count %d\n", matchedSkillCount);
				chWaitTime = ((currentNode % 10) * 3) + 3;
				printf("CH wait Time :%d\n", chWaitTime);
				etimer_set(&et, CLOCK_SECOND * chWaitTime);
				PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
				printf("CLUSTER HEAD :%d\n", clusterHeadNo);
				if (clusterHeadNo == -1 && residualEnergy >= chChangeTreshold) {
					/*cluster head advertisement message*/
					count = 0;
					if (clusterHeadNo == -1) {
						while (count < 2) {
							writeMessageToPackageBuffer(2);
							broadcast_send(&broadcast);
							residualEnergy = residualEnergy - messageSendCost;
							printf("CH ADVERTISEMENT SENT\n");
							clusterHeadNo = -2;
							leds_on(LEDS_RED);
							count++;
							etimer_set(&smallTimer, CLOCK_SECOND);
							PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&smallTimer));
						}
					}
					printf("CH WAITING FOR PREPARE PERIOD\n");
				} else {
					printf("WORKER WAITING FOR PREPARE PERIOD CH=%d\n", clusterHeadNo);
				}
			}
			PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&fixedTimer));
			printf("CH SELECTION PART FINISHH\n");
			/***********CLUSTER HEAD SELECTION PERIOD FINISH*******************/
			/*********************************************************************************/
			/*********************************************************************************/

			/***********CLUSTER HEAD SELECTION PERIOD FINISH*******************/
			/*********************************************************************************/
			/*********************************************************************************/
			while (residualEnergy > 0) {
				/*initial setup*/
				clearMemory();
				delayTime = -1;
				/**********PREPARE PERIOD ********************/
				etimer_set(&fixedTimer, CLOCK_SECOND * 55);
				printf("\n");
				printf("PREPARE PERIOD -- Cluster Head :  %d --- residual energy : %d \n", clusterHeadNo, residualEnergy);
				if (clusterHeadNo == -2) {
					printf("CLUSTER HEAD WAITING\n");
					/*clusterHead 50 sn boyunca gelen cap mesajlarını dinler.*/
					etimer_set(&et, CLOCK_SECOND * 40);
					PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
					if (memberPtr != NULL) {
						printMemberList();
						newClusterHead = -1;
						if (residualEnergy < chChangeTreshold) {
							chChange = 1;
							newClusterHead = selectNewClusterHead();
							printf("New ClusterHead : %d\n", newClusterHead);
						}
						/*Determine Task and Send*/
						count = 0;
						while (count < 3) {
							etimer_set(&smallTimer, 2 * CLOCK_SECOND);
							PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&smallTimer));
							writeMessageToPackageBuffer(3);
							broadcast_send(&broadcast);
							residualEnergy = residualEnergy - messageSendCost;
							count++;
						}
						if (newClusterHead > -1) {
							/*CH energy reached the critical point change CH*/
							count = 0;
							chChange = 0;
							while (count < 2) {
								writeMessageToPackageBuffer(4);
								broadcast_send(&broadcast);
								residualEnergy = residualEnergy - messageSendCost;
								printf("CH CHANGE MESSAGE SENT\n");
								clusterHeadNo = -2;
								leds_off(LEDS_RED);
								leds_on(LEDS_GREEN);
								count++;
								etimer_set(&smallTimer, 2 * CLOCK_SECOND);
								PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&smallTimer));
							}
							clusterHeadNo = newClusterHead;
							newClusterHead = -1;
						}
					} else {
						/*CH has no member*/
						leds_off(LEDS_RED);
						clusterHeadNo = -1;
					}
					PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&fixedTimer));
					/*Hiç elemanı olmayan cluster head ler ya bir gruba katılacak yada bu turu uyuyarak tamamlayacak*/
				} else {
					linkaddr_t addr;
					/*send cap message*/

					etimer_set(&et, (currentNode * 3) * CLOCK_SECOND);

					PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
					addr.u8[0] = clusterHeadNo % 10 + 1;
					addr.u8[1] = clusterHeadNo / 10;
					if (!linkaddr_cmp(&addr, &linkaddr_node_addr)) {
						writeMessageToPackageBuffer(1);
						unicast_send(&uc, &addr);
						etimer_set(&smallTimer, CLOCK_SECOND);
						PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&smallTimer));
						writeMessageToPackageBuffer(1);
						addr.u8[0] = clusterHeadNo % 10 + 1;
						addr.u8[1] = clusterHeadNo / 10;
						unicast_send(&uc, &addr);
					}
					PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&fixedTimer));
				}
				/*********PREPARE PERIOD FINISH********************/
				/*********************************************************************************/

				/*********************************************************************************/
				/**********STEADY PERIOD START ********************/
				for (i = 0; i < workLoopCount; ++i) {
					printf("STEADY PERIOD ----> Delay Time: %d \n", delayTime);
					etimer_set(&fixedTimer, CLOCK_SECOND * workPeriodLength + 3);
					if (delayTime >= 0) {
						if (residualEnergy >= ((sensingCost * matchedSkillCount))) {
							etimer_set(&smallTimer, delayTime * CLOCK_SECOND);
							printf("WORKING NOWWW \n");
							PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&smallTimer));
							leds_on(LEDS_BLUE);
							etimer_set(&et, CLOCK_SECOND * subWorkPeriodLength);
							PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
							leds_off(LEDS_BLUE);
							residualEnergy = residualEnergy - (sensingCost * matchedSkillCount);
							workPeriodCount++;
						} else {
							energyExhausted = 1;
							break;
						}
					}
					PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&fixedTimer));
					printf("STEADY PERIOD FINISH Residual Energy ---> %d \n", residualEnergy);
				}
				if (energyExhausted == 1) {
					break;
				}
				/***********STEADY PERIOD FINISH***************************************************/
				/*********************************************************************************/

				/*********************************************************************************/
				/****CHANGE CH ?? and SHARE INFO WITH SINK ********************************/
				printf("INFO CHANGE PERIOD START----- %.2f\n", residualEnergy);
				etimer_set(&fixedTimer, CLOCK_SECOND * 10);
				if (clusterHeadNo == -2) {
					addr.u8[0] = sinkID % 10 + 1;
					addr.u8[1] = sinkID / 10;
					writeMessageToPackageBuffer(5);
					unicast_send(&uc, &addr);
					printf("Unicast Sent to Sink \n");
					etimer_set(&smallTimer, CLOCK_SECOND * 2);
					PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&smallTimer));
					addr.u8[0] = sinkID % 10 + 1;
					addr.u8[1] = sinkID / 10;
					writeMessageToPackageBuffer(5);
					unicast_send(&uc, &addr);
					printf("Unicast Sent to Sink \n");
					residualEnergy = residualEnergy - messageSendCost * 6;
				}
				PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&fixedTimer));
				printf("INFO CHANGE PERIOD END\n");
				tourCount++;
				/****CHANGE CH ?? and SHARE INFO WITH SINK ********************************/
			}
			printf("NODE DEAD -- Delay Time : %d --- Work Period Count : %d \n", delayTime, workPeriodCount);
			leds_off(LEDS_ALL);
		}

	PROCESS_END();
}
/*---------------------------------------------------------------------------*/

