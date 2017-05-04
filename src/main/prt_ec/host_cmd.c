
#include <errno.h>
#include <string.h>

#include "prt_ec/crc16.h"

#include "cmd.h"

enum host_cmd_state {
	HOST_CMD_READY = 0,
	HOST_CMD_RECEIVED,
	HOST_CMD_PROCESSING,
	HOST_CMD_PROCESSED,
};


struct host_packet *active_pkt;
static enum host_cmd_state state;

bool host_cmd_is_ready(void)
{
	return !!(state == HOST_CMD_READY);
}

int host_pkt_recieved(struct host_packet *host_pkt)
{
	struct prt_ec_packet *pkt = &host_pkt->io_pkt;

	if (!(pkt->flags & PRT_EC_FLAG_MOSI)) {
		/* We received our own transmission or noting */
		pkt->id = EC_RES_ERROR;
		return -1;
	}

	if (pkt->crc != crc16((uint8_t *)pkt, sizeof(*pkt) - 2)) {
		/* Packet has invalid crc */
		pkt->id = EC_RES_INVALID_CHECKSUM;
		return -1;
	}

	active_pkt = host_pkt;
	state = HOST_CMD_RECEIVED;
	return 0;
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

static enum prt_ec_result host_cmd_process(void)
{
	const struct host_command *cmd;
	struct prt_ec_packet *pkt = &active_pkt->io_pkt;

	cmd = find_host_command(pkt->id);
	if (!cmd)
		return EC_RES_INVALID_COMMAND;

	return cmd->handler(pkt->data);
}

bool host_cmd_update(timeUs_t currentTimeUs, timeDelta_t currentDeltaTimeUs)
{
	UNUSED(currentTimeUs);
	UNUSED(currentDeltaTimeUs);

	return !!(state == HOST_CMD_RECEIVED);
}

void host_cmd_task_handler(timeUs_t currentTimeUs)
{
	struct prt_ec_packet *pkt = &active_pkt->io_pkt;

	UNUSED(currentTimeUs);

	state = HOST_CMD_PROCESSING;

	pkt->id = host_cmd_process();

//	state = HOST_CMD_PROCESSED;
	state = HOST_CMD_READY;
}
