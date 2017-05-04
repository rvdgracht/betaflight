
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

/*
 * Invalid ID.
 * Used as placeholder for internal and dummy messages.
 */
#define EC_MSG_ID_INVALID		0x00

/*
 * Flag bit definitions
 */
#define PRT_EC_FLAG_ACKREQ              BIT(0)
#define PRT_EC_FLAG_ACKTGL              BIT(1)
#define PRT_EC_FLAG_ACK                 BIT(2)
#define PRT_EC_FLAG_MOSI                BIT(3)

/*
 * Low-level protocol packet
 */
struct prt_ec_pkt {
        /* Packet id */
        uint8_t id;

        /* Packet flags
         * bits(s)
         *   0          ack request
         *   1          ack toggle
         *   2          ack
         *   3          mosi
         *   4-7        reserved
         */
        uint8_t flags;

        /* Packet data */
        uint16_t data[PRT_EC_PKT_DATA_WORDS];

        /* Packet crc */
        uint16_t crc;
} __packed;

/*
 * Upper level message
 */
struct prt_ec_msg {
        /* ID of the message (EC_MSG_ID_...) */
        uint8_t id;

        /* Request an ack for this message */
        uint8_t ackreq;

        /*
         * Responce message ID
         * set to EC_MSG_ID_INVALID if not interested in a responce.
         */
        uint8_t rid;

        /* Message payload */
        uint16_t data[PRT_EC_PKT_DATA_WORDS];
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
 * Host Functions
 */
int host_pkt_recieved(struct prt_ec_pkt *pkt);
bool host_cmd_update(timeUs_t currentTimeUs, timeDelta_t currentDeltaTimeUs);
void host_cmd_task_handler(timeUs_t currentTimeUs);


/*
 * EC Functions
 */
int ec_push_msg(struct prt_ec_msg *msg);
int ec_pop_pkt(struct prt_ec_pkt *pkt);

#define DECLARE_HOST_COMMAND(command, routine)                          \
	const struct host_command __keep __host_cmd_##command           \
	__attribute__((section(".hcmds." #command))) = {routine, command}

