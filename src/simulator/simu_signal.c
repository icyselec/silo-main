#include <stdlib.h>

#include "../include/define.h"
#include "../include/wire.h"
#include "../include/node/node.h"
#include "../include/simulator/simulator.h"
#include "../include/simulator/simulator_in.h"
#include "../include/simulator/thread.h"

void SimuSendSignal(SENDFORM dest, SIGNAL signal) { dest.node->sent[dest.port] = signal; }
void SimuWakeUpNode(NODE * node) { node->simu->simu.sentlist[node->ndid] = true; }

void SimuTransfer(WIRE wire, SIGNAL signal) {
	for (DEFT_WORD i = 0; i < wire.size; i++) {
		SimuSendSignal(wire.send[i].node->send[wire.send[i].port], signal);
		SimuWakeUpNode(wire.send[i].node);
	}
}
