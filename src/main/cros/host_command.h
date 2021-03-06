/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Host command module for Chrome EC */

#pragma once

#include "common/time.h"

#include "ec_command.h"

/* Args for host command handler */
struct host_cmd_handler_args {
	/*
	 * The driver that receives the command sets up the send_response()
	 * handler. Once the command is processed this handler is called to
	 * send the response back to the host.
	 */
	void (*send_response)(struct host_cmd_handler_args *args);
	uint16_t command;      /* Command (e.g., EC_CMD_FLASH_GET_INFO) */
	uint8_t version;       /* Version of command (0-31) */

	const void *params; /* Input parameters */
	uint16_t params_size;  /* Size of input parameters in bytes */

	/*
	 * Pointer to output response data buffer.  On input to the handler,
	 * points to a buffer of size response_max.
	 */
	void *response;

	/* Maximum size of response buffer provided to command handler */
	uint16_t response_max;

	/*
	 * Size of data pointed to by response.  Defaults to 0, so commands
	 * which do not produce response data do not need to set this.
	 */
	uint16_t response_size;

	/*
	 * This is the result returned by command and therefore the status to
	 * be reported from the command execution to the host. The driver
	 * should set this to EC_RES_SUCCESS on receipt of a valid command.
	 * It is then passed back to the driver via send_response() when
	 * command execution is complete. The driver may still override this
	 * when sending the response back to the host if it detects an error
	 * in the response or in its own operation.
	 */
	enum ec_status result;
};

/* Args for host packet handler */
struct host_packet {
	/*
	 * The driver that receives the command sets up the send_response()
	 * handler. Once the command is processed this handler is called to
	 * send the response back to the host.
	 */
	void (*send_response)(struct host_packet *pkt);

	/* Input request data */
	const void *request;

	/*
	 * Input request temp buffer.  If this is non-null, the data has not
	 * been copied from here into the request buffer yet.  The host command
	 * handler should do so while verifying the command.  The interface
	 * can't do it, because it doesn't know how much to copy.
	 */
	void *request_temp;

	/*
	 * Maximum size of request the interface can handle, in bytes.  The
	 * buffers pointed to by *request and *request_temp must be at least
	 * this big.
	 */
	uint16_t request_max;

	/* Size of input request data, in bytes */
	uint16_t request_size;

	/* Pointer to output response data buffer */
	void *response;

	/* Maximum size of response buffer provided to command handler */
	uint16_t response_max;

	/* Size of output response data, in bytes */
	uint16_t response_size;

	/*
	 * Error from driver; if this is non-zero, host command handler will
	 * return a properly formatted error response packet rather than
	 * calling a command handler.
	 */
	enum ec_status driver_result;
};

/* Host command */
struct host_command {
	/*
	 * Handler for the command.  Args points to context for handler.
	 * Returns result status (EC_RES_*).
	 */
	int (*handler)(struct host_cmd_handler_args *args);
	/* Command code */
	int command;
	/* Mask of supported versions */
	int version_mask;
};

/**
 * Return a pointer to the memory-mapped buffer.
 *
 * This buffer is EC_MEMMAP_SIZE bytes long, is writable at any time, and the
 * host can read it at any time.
 *
 * @param offset        Offset within the range to return
 * @return pointer to the buffer at that offset
 */
uint8_t *host_get_memmap(int offset);

/**
 * Process a host command and return its response
 *
 * @param args	        Command handler args
 * @return resulting status
 */
enum ec_status host_command_process(struct host_cmd_handler_args *args);

/**
 * Send a response to the relevant driver for transmission
 *
 * Once command processing is complete, this is used to send a response
 * back to the host.
 *
 * @param args	Contains response to send
 */
void host_send_response(struct host_cmd_handler_args *args);

/**
 * Called by host interface module when a command is received.
 */
void host_command_received(struct host_cmd_handler_args *args);

/**
 * Return the expected host packet size given its header.
 *
 * Also does some sanity checking on the host request.
 *
 * @param r		Host request header
 * @return The expected packet size, or 0 if error.
 */
uint32_t host_request_expected_size(const struct ec_host_request *r);

/**
 * Handle a received host packet.
 *
 * @param packet	Host packet args
 */
void host_packet_receive(struct host_packet *pkt);

/**
 * functions called from scheduler to check for, and process commands.
 */
bool host_command_update(timeUs_t currentTimeUs,
	timeDelta_t currentDeltaTimeUs);
void host_task_handler(timeUs_t currentTimeUs);

#define HC_EXP(off, cmd) __host_cmd_##off##cmd
#define HC_EXP_STR(off, cmd) "__host_cmd_"#off#cmd

#define DECLARE_HOST_COMMAND(command, routine, version_mask)    \
	const struct host_command __keep HC_EXP(0x0000, command)        \
	__attribute__((section(".rodata.hcmds."HC_EXP_STR(0x0000, command)))) \
		= {routine, command, version_mask}
