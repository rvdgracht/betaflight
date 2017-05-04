
#include <errno.h>
#include <string.h>

#include "prt_ec/crc16.h"

#include "buffer.h"
#include "cmd.h"

#define RX_BUFSIZE	8

static struct prt_ec_msg rx_msg_array[RX_BUFSIZE];
static PRT_EC_BUFFER_INIT(rx_msg_buffer, rx_msg_array, RX_BUFSIZE);

int host_pkt_recieved(struct prt_ec_pkt *pkt)
{
	if (!(pkt->flags & PRT_EC_FLAG_MOSI))
		return -1;

	if (pkt->crc != crc16((uint8_t *)pkt, sizeof(*pkt) - 2))
		return -2;

	return prt_ec_buffer_put_pkt(&rx_msg_buffer, pkt);
}


/**
 * Find a command by command number.
 *
 * @param command	Command number to find
 * @return The command structure, or NULL if no match found.
 */
extern const struct host_command __hcmds_start[];
extern const struct host_command __hcmds_end[];

static const struct host_command *find_host_command(int command)
{
	const struct host_command *cmd;

	for (cmd = __hcmds_start; cmd < __hcmds_end; cmd++) {
		if (command == cmd->command)
			return cmd;
	}
	return NULL;
}

static void host_cmd_process_one(void)
{
	const struct host_command *cmd;
	struct prt_ec_msg *msg;

	if (prt_ec_buffer_empty(&rx_msg_buffer))
		return;

	msg = rx_msg_buffer.tail;
	cmd = find_host_command(msg->id);
	if (cmd)
		cmd->handler(msg->data);

	prt_ec_buffer_inc_tail(&rx_msg_buffer);
}

bool host_cmd_update(timeUs_t currentTimeUs, timeDelta_t currentDeltaTimeUs)
{
	UNUSED(currentTimeUs);
	UNUSED(currentDeltaTimeUs);

	return (prt_ec_buffer_empty(&rx_msg_buffer) == false);
}

void host_cmd_task_handler(timeUs_t currentTimeUs)
{
	UNUSED(currentTimeUs);

	host_cmd_process_one();
}
