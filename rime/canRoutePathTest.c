/*
 * Copyright (c) 2007, Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 *
 */

/**
 * \file
 *         Testing the broadcast layer in Rime
 * \author
 *         Adam Dunkels <adam@sics.se>
 */

#include "contiki.h"
#include "net/rime.h"
#include "random.h"


#include "dev/button-sensor.h"

#include "dev/leds.h"
#include <stdio.h>
//#include <arpa/inet.h>
/*---------------------------------------------------------------------------*/
PROCESS(example_broadcast_process, "Broadcast example");
AUTOSTART_PROCESSES(&example_broadcast_process);
/*---------------------------------------------------------------------------*/
struct energyData{
	int energyLevel;
};
typedef struct energyData EnergyData;

static void
broadcast_recv(struct broadcast_conn *c, const rimeaddr_t *from)
{
	uint16_t code;
	memcpy(&code, packetbuf_dataptr(), sizeof(code));

	 if(packetbuf_datalen() == sizeof(int)) {
	   printf("broadcast message received from %d.%d: %d\n",
	          from->u8[0], from->u8[1], code);
	 } else {
	   printf("invalid broadcast size: %d\n", packetbuf_datalen());
	 }
}
static const struct broadcast_callbacks broadcast_call = {broadcast_recv};
static struct broadcast_conn broadcast;

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(example_broadcast_process, ev, data)
{

  PROCESS_EXITHANDLER(broadcast_close(&broadcast);)


  PROCESS_BEGIN();
 //uint16_t energyLevel=100;
  //uint16_t *energyPtr=&energyLevel;
  //uint16_t networkFormat=htons(energyLevel);


  static uint16_t x=100;
  broadcast_open(&broadcast, 129, &broadcast_call);
  SENSORS_ACTIVATE(button_sensor);
  while(1) {

    /* Delay 2-4 seconds */
    /*etimer_set(&et, CLOCK_SECOND * 4 + random_rand() % (CLOCK_SECOND * 4));

    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));*/
	PROCESS_WAIT_EVENT_UNTIL(ev == sensors_event && data == &button_sensor);
	printf("Button clicked\n");
	packetbuf_copyfrom(&x, sizeof(x));
    broadcast_send(&broadcast);
    printf("broadcast message sent\n");
    x=x-1;
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
