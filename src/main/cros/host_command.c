/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Host command module for Chrome EC */

#include <string.h>

#include <platform.h>

#include "drivers/system.h"

#include "common/utils.h"

#include "flight/mixer.h"

#include "host_command.h"

#ifdef USE_CROS_EC

#define TASK_EVENT_CMD_PENDING TASK_EVENT_CUSTOM(1)

static struct host_cmd_handler_args *pending_cmd;
static struct host_cmd_handler_args *pending_rsp;

/*
 * Indicates that a 'slow' command has sent EC_RES_IN_PROGRESS but hasn't
 * sent a final status (i.e. it is in progress)
 */
static uint8_t command_pending;

/* The result of the last 'slow' operation */
static uint8_t saved_result = EC_RES_UNAVAILABLE;

/*
 * Host command args passed to command handler.  Static to keep it off the
 * stack.  Note this means we can handle only one host command at a time.
 */
static struct host_cmd_handler_args args0;

/* Current host command packet from host, for protocol version 3+ */
static struct host_packet *pkt0;

void host_send_response(struct host_cmd_handler_args *args)
{
	/*
	 *
	 * If we are in interrupt context, then we are handling a get_status
	 * response or an immediate error which prevented us from processing
	 * the command. Note we can't check for the GET_COMMS_STATUS command in
	 * args->command because the original command value has now been
	 * overwritten.
	 *
	 * When a EC_CMD_RESEND_RESPONSE arrives we will supply this response
	 * to that command.
	 */
	if (command_pending) {
		/*
		 * We previously got EC_RES_IN_PROGRESS.  This must be
		 * the completion of that command, so stash the result
		 * code.
		 */

		/*
		 * We don't support stashing response data, so mark the
		 * response as unavailable in that case.
		 */
		if (args->response_size != 0)
			saved_result = EC_RES_UNAVAILABLE;
		else
			saved_result = args->result;
		/*
		 * We can't send the response back to the host now
		 * since we already sent the in-progress response and
		 * the host is on to other things now.
		 */
		command_pending = 0;
		return;

	} else if (args->result == EC_RES_IN_PROGRESS) {
		command_pending = 1;
	}

	args->send_response(args);
}

void host_command_received(struct host_cmd_handler_args *args)
{
	/*
	 * TODO(crosbug.com/p/23806): should warn if we already think we're in
	 * a command.
	 */

	/*
	 * If this is the reboot command, reboot immediately.  This gives the
	 * host processor a way to unwedge the EC even if it's busy with some
	 * other command.
	 */
	if (args->command == EC_CMD_REBOOT) {
		stopPwmAllMotors();
		systemReset();

		/* Reset should never return; if it does, post an error */
		args->result = EC_RES_ERROR;
	}

	/* If hang detection is enabled, check stop on host command */
//	hang_detect_stop_on_host_command();

	if (args->result) {
		/* driver has signalled an error, prepare a response */
		pending_rsp = args;
	} else if (args->command == EC_CMD_GET_COMMS_STATUS) {
		args->result = host_command_process(args);
	} else {
		/* Save the command */
		pending_cmd = args;
	}
}

void host_packet_respond(struct host_cmd_handler_args *args)
{
	struct ec_host_response *r = (struct ec_host_response *)pkt0->response;
	uint8_t *out = (uint8_t *)pkt0->response;
	int csum = 0;
	int i;

	/* Clip result size to what we can accept */
	if (args->result) {
		/* Error results don't have data */
		args->response_size = 0;
	} else if (args->response_size > pkt0->response_max - sizeof(*r)) {
		/* Too much data */
		args->result = EC_RES_RESPONSE_TOO_BIG;
		args->response_size = 0;
	}

	/* Fill in response struct */
	r->struct_version = EC_HOST_RESPONSE_VERSION;
	r->checksum = 0;
	r->result = args->result;
	r->data_len = args->response_size;
	r->reserved = 0;

	/* Start checksum; this also advances *out to end of response */
	for (i = sizeof(*r); i > 0; i--)
		csum += *out++;

	/* Checksum response data, if any */
	for (i = args->response_size; i > 0; i--)
		csum += *out++;

	/* Write checksum field so the entire packet sums to 0 */
	r->checksum = (uint8_t)(-csum);

	pkt0->response_size = sizeof(*r) + r->data_len;
	pkt0->driver_result = args->result;
	pkt0->send_response(pkt0);
}

uint32_t host_request_expected_size(const struct ec_host_request *r)
{
	/* Check host request version */
	if (r->struct_version != EC_HOST_REQUEST_VERSION)
		return 0;

	/* Reserved byte should be 0 */
	if (r->reserved)
		return 0;

	return sizeof(*r) + r->data_len;
}

void host_packet_receive(struct host_packet *pkt)
{
	const struct ec_host_request *r =
		(const struct ec_host_request *)pkt->request;
	const uint8_t *in = (const uint8_t *)pkt->request;
	uint8_t *itmp = (uint8_t *)pkt->request_temp;
	int csum = 0;
	int i;

	/* Track the packet we're handling */
	pkt0 = pkt;

	/* If driver indicates error, don't even look at the data */
	if (pkt->driver_result) {
		args0.result = pkt->driver_result;
		goto host_packet_end;
	}

	if (pkt->request_size < sizeof(*r)) {
		/* Packet too small for even a header */
		args0.result = EC_RES_REQUEST_TRUNCATED;
		goto host_packet_end;
	}

	if (pkt->request_size > pkt->request_max) {
		/* Got a bigger request than the interface can handle */
		args0.result = EC_RES_REQUEST_TRUNCATED;
		goto host_packet_end;
	}

	/*
	 * Response buffer needs to be big enough for a header.  If it's not
	 * we can't even return an error packet.
	 */
	/* Start checksum and copy request header if necessary */
	if (pkt->request_temp) {
		/* Copy to temp buffer and checksum */
		for (i = sizeof(*r); i > 0; i--) {
			*itmp = *in++;
			csum += *itmp++;
		}
		r = (const struct ec_host_request *)pkt->request_temp;
	} else {
		/* Just checksum */
		for (i = sizeof(*r); i > 0; i--)
			csum += *in++;
	}

	if (r->struct_version != EC_HOST_REQUEST_VERSION) {
		/* Request header we don't know how to handle */
		args0.result = EC_RES_INVALID_HEADER;
		goto host_packet_end;
	}

	if (pkt->request_size < sizeof(*r) + r->data_len) {
		/*
		 * Packet too small for expected params.  Note that it's ok if
		 * the received packet data is too big; some interfaces may pad
		 * the data at the end (SPI) or may not know how big the
		 * received data is (LPC).
		 */
		args0.result = EC_RES_REQUEST_TRUNCATED;
		goto host_packet_end;
	}

	/* Copy request data and validate checksum */
	if (pkt->request_temp) {
		/* Params go in temporary buffer */
		args0.params = itmp;

		/* Copy request data and checksum */
		for (i = r->data_len; i > 0; i--) {
			*itmp = *in++;
			csum += *itmp++;
		}
	} else {
		/* Params read directly from request */
		args0.params = in;

		/* Just checksum */
		for (i = r->data_len; i > 0; i--)
			csum += *in++;
	}

	/* Validate checksum */
	if ((uint8_t)csum) {
		args0.result = EC_RES_INVALID_CHECKSUM;
		goto host_packet_end;
	}

	/* Set up host command handler args */
	args0.send_response = host_packet_respond;
	args0.command = r->command;
	args0.version = r->command_version;
	args0.params_size = r->data_len;
	args0.response = (struct ec_host_response *)(pkt->response) + 1;
	args0.response_max = pkt->response_max -
		sizeof(struct ec_host_response);
	args0.response_size = 0;
	args0.result = EC_RES_SUCCESS;

host_packet_end:
	host_command_received(&args0);
}

extern const struct host_command __hcmds[];
extern const struct host_command __hcmds_end[];

/**
 * Find a command by command number.
 *
 * @param command	Command number to find
 * @return The command structure, or NULL if no match found.
 */
#include "hex_leds.h"
static const struct host_command *find_host_command(int command)
{
	const struct host_command *l, *r, *m;
	uint32_t num;

/* Use binary search to locate host command handler */
	l = __hcmds;
	r = __hcmds_end - 1;

	while (1) {
		if (l > r)
			return NULL;

		num = r - l;
		m = l + (num / 2);

		if (m->command < command)
			l = m + 1;
		else if (m->command > command)
			r = m - 1;
		else {
			HEX_LED5_ON;
			return m;
		}
	}
}

bool host_command_update(timeUs_t currentTimeUs,
	timeDelta_t currentDeltaTimeUs)
{
	UNUSED(currentTimeUs);
	UNUSED(currentDeltaTimeUs);

	return !!(pending_cmd || pending_rsp);
}

void host_task_handler(timeUs_t currentTimeUs)
{
	UNUSED(currentTimeUs);

	if (pending_rsp) {
		host_send_response(pending_rsp);
		pending_rsp = NULL;
	}

	if (pending_cmd) {
		pending_cmd->result = host_command_process(pending_cmd);
		host_send_response(pending_cmd);
		pending_cmd = NULL;
	}
}

/*****************************************************************************/
/* Host commands */

static int host_command_hello(struct host_cmd_handler_args *args)
{
	const struct ec_params_hello *p = args->params;
	struct ec_response_hello *r = args->response;
	uint32_t d = p->in_data;

	r->out_data = d + 0x01020304;
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_HELLO,
		     host_command_hello,
		     EC_VER_MASK(0));

static int host_command_get_cmd_versions(struct host_cmd_handler_args *args)
{
	const struct ec_params_get_cmd_versions *p = args->params;
	const struct ec_params_get_cmd_versions_v1 *p_v1 = args->params;
	struct ec_response_get_cmd_versions *r = args->response;

	const struct host_command *cmd =
		(args->version == 1) ? find_host_command(p_v1->cmd) :
				       find_host_command(p->cmd);

	if (!cmd)
		return EC_RES_INVALID_PARAM;

	r->version_mask = cmd->version_mask;

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_CMD_VERSIONS,
		     host_command_get_cmd_versions,
		     EC_VER_MASK(0) | EC_VER_MASK(1));


enum ec_status host_command_process(struct host_cmd_handler_args *args)
{
	const struct host_command *cmd;
	int rv;

	cmd = find_host_command(args->command);
	if (!cmd)
		rv = EC_RES_INVALID_COMMAND;
	else if (!(EC_VER_MASK(args->version) & cmd->version_mask))
		rv = EC_RES_INVALID_VERSION;
	else
		rv = cmd->handler(args);

	return rv;
}

/* Returns current command status (busy or not) */
static int host_command_get_comms_status(struct host_cmd_handler_args *args)
{
	struct ec_response_get_comms_status *r = args->response;

	r->flags = command_pending ? EC_COMMS_STATUS_PROCESSING : 0;
	args->response_size = sizeof(*r);

	return 0;
}

DECLARE_HOST_COMMAND(EC_CMD_GET_COMMS_STATUS,
		     host_command_get_comms_status,
		     EC_VER_MASK(0));

/* Resend the last saved response */
static int host_command_resend_response(struct host_cmd_handler_args *args)
{
	/* Handle resending response */
	args->result = saved_result;
	args->response_size = 0;

	saved_result = EC_RES_UNAVAILABLE;

	return 0;
}

DECLARE_HOST_COMMAND(EC_CMD_RESEND_RESPONSE,
		     host_command_resend_response,
		     EC_VER_MASK(0));

/* Returns supported features. */
static int host_command_get_features(struct host_cmd_handler_args *args)
{
	struct ec_response_get_features *r = args->response;
	args->response_size = sizeof(*r);

	memset(r, 0, sizeof(*r));
	r->flags[0] = 0
#ifdef CONFIG_FW_LIMITED_IMAGE
		| EC_FEATURE_MASK_0(EC_FEATURE_LIMITED)
#endif
#ifdef CONFIG_FLASH
		| EC_FEATURE_MASK_0(EC_FEATURE_FLASH)
#endif
#ifdef CONFIG_FANS
		| EC_FEATURE_MASK_0(EC_FEATURE_PWM_FAN)
#endif
#ifdef CONFIG_PWM_KBLIGHT
		| EC_FEATURE_MASK_0(EC_FEATURE_PWM_KEYB)
#endif
#ifdef HAS_TASK_LIGHTBAR
		| EC_FEATURE_MASK_0(EC_FEATURE_LIGHTBAR)
#endif
#ifdef CONFIG_LED_COMMON
		| EC_FEATURE_MASK_0(EC_FEATURE_LED)
#endif
#ifdef HAS_TASK_MOTIONSENSE
		| EC_FEATURE_MASK_0(EC_FEATURE_MOTION_SENSE)
#endif
#ifdef HAS_TASK_KEYSCAN
		| EC_FEATURE_MASK_0(EC_FEATURE_KEYB)
#endif
#ifdef CONFIG_PSTORE
		| EC_FEATURE_MASK_0(EC_FEATURE_PSTORE)
#endif
#ifdef CONFIG_LPC
		| EC_FEATURE_MASK_0(EC_FEATURE_PORT80)
#endif
#ifdef CONFIG_TEMP_SENSOR
		| EC_FEATURE_MASK_0(EC_FEATURE_THERMAL)
#endif
/* Hack to uniquely identify Samus ec */
#if (defined CONFIG_BACKLIGHT_LID) || (defined CONFIG_BATTERY_SAMUS)
		| EC_FEATURE_MASK_0(EC_FEATURE_BKLIGHT_SWITCH)
#endif
#ifdef CONFIG_WIRELESS
		| EC_FEATURE_MASK_0(EC_FEATURE_WIFI_SWITCH)
#endif
#ifdef CONFIG_HOSTCMD_EVENTS
		| EC_FEATURE_MASK_0(EC_FEATURE_HOST_EVENTS)
#endif
#ifdef CONFIG_COMMON_GPIO
		| EC_FEATURE_MASK_0(EC_FEATURE_GPIO)
#endif
#ifdef CONFIG_I2C_MASTER
		| EC_FEATURE_MASK_0(EC_FEATURE_I2C)
#endif
#ifdef CONFIG_CHARGER
		| EC_FEATURE_MASK_0(EC_FEATURE_CHARGER)
#endif
#if (defined CONFIG_BATTERY) || (defined CONFIG_BATTERY_SMART)
		| EC_FEATURE_MASK_0(EC_FEATURE_BATTERY)
#endif
#ifdef CONFIG_BATTERY_SMART
		| EC_FEATURE_MASK_0(EC_FEATURE_SMART_BATTERY)
#endif
#ifdef CONFIG_AP_HANG_DETECT
		| EC_FEATURE_MASK_0(EC_FEATURE_HANG_DETECT)
#endif
#ifdef CONFIG_PMU_POWERINFO
		| EC_FEATURE_MASK_0(EC_FEATURE_PMU)
#endif
#ifdef CONFIG_HOSTCMD_PD
		| EC_FEATURE_MASK_0(EC_FEATURE_SUB_MCU)
#endif
#ifdef CONFIG_CHARGE_MANAGER
		| EC_FEATURE_MASK_0(EC_FEATURE_USB_PD)
#endif
#ifdef CONFIG_ACCEL_FIFO
		| EC_FEATURE_MASK_0(EC_FEATURE_MOTION_SENSE_FIFO)
#endif
#ifdef CONFIG_VSTORE
		| EC_FEATURE_MASK_0(EC_FEATURE_VSTORE)
#endif
#ifdef CONFIG_USB_MUX_VIRTUAL
		| EC_FEATURE_MASK_0(EC_FEATURE_USBC_SS_MUX_VIRTUAL)
#endif
#ifdef CONFIG_HOSTCMD_RTC
		| EC_FEATURE_MASK_0(EC_FEATURE_RTC)
#endif
#ifdef CONFIG_SPI_FP_PORT
		| EC_FEATURE_MASK_0(EC_FEATURE_FINGERPRINT)
#endif
		;
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_FEATURES,
		     host_command_get_features,
		     EC_VER_MASK(0));

#endif /* USE_CROS_EC */
