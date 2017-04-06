
#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "common/time.h"
#include "common/utils.h"

#define PRT_EC_PKT_DATA_WORDS	14

#ifndef __packed
# define __packed __attribute__((packed))
#endif

#ifndef __keep
#define __keep __attribute__((used)) __attribute__((externally_visible))
#endif

enum prt_ec_result {
	EC_RES_SUCCESS = 200,
	EC_RES_NOT_READY,
	EC_RES_INVALID_COMMAND,
	EC_RES_INVALID_CHECKSUM,
	EC_RES_INVALID_PARAM,
	EC_RES_TIMEOUT,

	EC_RES_ERROR = 255,		/* Any other error */
};

/* Flag bit definitions */
#define PRT_EC_FLAG_ACKREQ		BIT(0)
#define PRT_EC_FLAG_ACKTGL		BIT(1)
#define PRT_EC_FLAG_ACK			BIT(2)
#define PRT_EC_FLAG_MOSI		BIT(3)

struct prt_ec_packet {
	/* Packet ID
	 *   0 - 200 command
	 * 200 - 255 response
	 */
	uint8_t id;

	/* Packet flags
	 * bits(s)
	 *   0		ack request
	 *   1		ack toggle
	 *   2          ack
	 *   3          mosi
	 *   4-7	reserved
	 */
	uint8_t flags;

	/* Packet data */
	uint16_t data[PRT_EC_PKT_DATA_WORDS];

	/* Packet crc */
	uint16_t crc;
} __packed;


struct host_packet {

	struct prt_ec_packet io_pkt;

	/*
	 * The driver that receives the command sets up the send_response()
	 * handler. Once the command is processed this handler is called to
	 * send the response back to the host.
	 */
	void (*send_response)(struct prt_ec_packet *pkt);
};

/* Host command */
struct host_command {
	/*
	 * Handler for the command.  Args points to context for handler.
	 * Returns result status (EC_RES_*).
	 */
	int (*handler)(uint16_t *data);

	/* Command code */
	int command;
};

/*
 * Functions
 */
bool host_cmd_is_ready(void);
int host_pkt_recieved(struct host_packet *pkt);

bool host_cmd_update(timeUs_t currentTimeUs, timeDelta_t currentDeltaTimeUs);
void host_cmd_task_handler(timeUs_t currentTimeUs);

#define DECLARE_HOST_COMMAND(command, routine)                          \
	const struct host_command __keep __host_cmd_##command           \
	__attribute__((section(".hcmds." #command))) = {routine, command}
