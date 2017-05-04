
#include "buffer.h"
#include "cmd.h"

#define TX_BUFSIZE	8

static struct prt_ec_msg tx_msg_array[TX_BUFSIZE];
static PRT_EC_BUFFER_INIT(tx_msg_buffer, tx_msg_array, TX_BUFSIZE);


int ec_push_msg(struct prt_ec_msg *msg)
{
	return prt_ec_buffer_put_msg(&tx_msg_buffer, msg);
}

int ec_pop_pkt(struct prt_ec_pkt *pkt)
{
	return prt_ec_buffer_pop_pkt(&tx_msg_buffer, pkt);
}
