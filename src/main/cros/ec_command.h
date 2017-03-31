
#pragma once

/* Command version mask */
#define EC_VER_MASK(version) (1UL << (version))

/*****************************************************************************/
/*
 * Byte codes returned by EC over SPI interface.
 *
 * These can be used by the AP to debug the EC interface, and to determine
 * when the EC is not in a state where it will ever get around to responding
 * to the AP.
 *
 * Example of sequence of bytes read from EC for a current good transfer:
 *   1. -                  - AP asserts chip select (CS#)
 *   2. EC_SPI_OLD_READY   - AP sends first byte(s) of request
 *   3. -                  - EC starts handling CS# interrupt
 *   4. EC_SPI_RECEIVING   - AP sends remaining byte(s) of request
 *   5. EC_SPI_PROCESSING  - EC starts processing request; AP is clocking in
 *                           bytes looking for EC_SPI_FRAME_START
 *   6. -                  - EC finishes processing and sets up response
 *   7. EC_SPI_FRAME_START - AP reads frame byte
 *   8. (response packet)  - AP reads response packet
 *   9. EC_SPI_PAST_END    - Any additional bytes read by AP
 *   10 -                  - AP deasserts chip select
 *   11 -                  - EC processes CS# interrupt and sets up DMA for
 *                           next request
 *
 * If the AP is waiting for EC_SPI_FRAME_START and sees any value other than
 * the following byte values:
 *   EC_SPI_OLD_READY
 *   EC_SPI_RX_READY
 *   EC_SPI_RECEIVING
 *   EC_SPI_PROCESSING
 *
 * Then the EC found an error in the request, or was not ready for the request
 * and lost data.  The AP should give up waiting for EC_SPI_FRAME_START,
 * because the EC is unable to tell when the AP is done sending its request.
 */

/*
 * Framing byte which precedes a response packet from the EC.  After sending a
 * request, the AP will clock in bytes until it sees the framing byte, then
 * clock in the response packet.
 */
#define EC_SPI_FRAME_START    0xec

/*
 * Padding bytes which are clocked out after the end of a response packet.
 */
#define EC_SPI_PAST_END       0xed

/*
 * EC is ready to receive, and has ignored the byte sent by the AP.  EC expects
 * that the AP will send a valid packet header (starting with
 * EC_COMMAND_PROTOCOL_3) in the next 32 bytes.
 */
#define EC_SPI_RX_READY       0xf8

/*
 * EC has started receiving the request from the AP, but hasn't started
 * processing it yet.
 */
#define EC_SPI_RECEIVING      0xf9

/* EC has received the entire request from the AP and is processing it. */
#define EC_SPI_PROCESSING     0xfa

/*
 * EC received bad data from the AP, such as a packet header with an invalid
 * length.  EC will ignore all data until chip select deasserts.
 */
#define EC_SPI_RX_BAD_DATA    0xfb

/*
 * EC received data from the AP before it was ready.  That is, the AP asserted
 * chip select and started clocking data before the EC was ready to receive it.
 * EC will ignore all data until chip select deasserts.
 */
#define EC_SPI_NOT_READY      0xfc

/*
 * EC was ready to receive a request from the AP.  EC has treated the byte sent
 * by the AP as part of a request packet, or (for old-style ECs) is processing
 * a fully received packet but is not ready to respond yet.
 */
#define EC_SPI_OLD_READY      0xfd


#ifndef __packed
#define __packed __attribute__((packed))
#endif

#ifndef __aligned
#define __aligned(x) __attribute__((aligned(x)))
#endif

#define __ec_align1 __packed
#define __ec_align2 __packed __aligned(2)
#define __ec_align4 __packed __aligned(4)

/* Host command response codes */
enum ec_status {
	EC_RES_SUCCESS = 0,
	EC_RES_INVALID_COMMAND = 1,
	EC_RES_ERROR = 2,
	EC_RES_INVALID_PARAM = 3,
	EC_RES_ACCESS_DENIED = 4,
	EC_RES_INVALID_RESPONSE = 5,
	EC_RES_INVALID_VERSION = 6,
	EC_RES_INVALID_CHECKSUM = 7,
	EC_RES_IN_PROGRESS = 8,		/* Accepted, command in progress */
	EC_RES_UNAVAILABLE = 9,		/* No response available */
	EC_RES_TIMEOUT = 10,		/* We got a timeout */
	EC_RES_OVERFLOW = 11,		/* Table / data overflow */
	EC_RES_INVALID_HEADER = 12,     /* Header contains invalid data */
	EC_RES_REQUEST_TRUNCATED = 13,  /* Didn't get the entire request */
	EC_RES_RESPONSE_TOO_BIG = 14,   /* Response was too big to handle */
	EC_RES_BUS_ERROR = 15,          /* Communications bus error */
	EC_RES_BUSY = 16                /* Up but too busy.  Should retry */
};

/*
 * Value written to legacy command port / prefix byte to indicate protocol
 * 3+ structs are being used.  Usage is bus-dependent.
 */
#define EC_COMMAND_PROTOCOL_3 0xda

#define EC_HOST_REQUEST_VERSION 3

/* Version 3 request from host */
struct __packed __aligned(4) ec_host_request {
	/* Structure version (=3)
	 *
	 * EC will return EC_RES_INVALID_HEADER if it receives a header with a
	 * version it doesn't know how to parse.
	 */
	uint8_t struct_version;

	/*
	 * Checksum of request and data; sum of all bytes including checksum
	 * should total to 0.
	 */
	uint8_t checksum;

	/* Command code */
	uint16_t command;

	/* Command version */
	uint8_t command_version;

	/* Unused byte in current protocol version; set to 0 */
	uint8_t reserved;

	/* Length of data which follows this header */
	uint16_t data_len;
};

#define EC_HOST_RESPONSE_VERSION 3

/* Version 3 response from EC */
struct __ec_align4 ec_host_response {
	/* Structure version (=3) */
	uint8_t struct_version;

	/*
	 * Checksum of response and data; sum of all bytes including checksum
	 * should total to 0.
	 */
	uint8_t checksum;

	/* Result code (EC_RES_*) */
	uint16_t result;

	/* Length of data which follows this header */
	uint16_t data_len;

	/* Unused bytes in current protocol version; set to 0 */
	uint16_t reserved;
};


/*
 * Hello.  This is a simple command to test the EC is responsive to
 * commands.
 */
#define EC_CMD_HELLO 0x0001

struct __ec_align4 ec_params_hello {
	uint32_t in_data;  /* Pass anything here */
};

struct __ec_align4 ec_response_hello {
	uint32_t out_data;  /* Output will be in_data + 0x01020304 */
};

/* Read versions supported for a command */
#define EC_CMD_GET_CMD_VERSIONS 0x0008

struct __ec_align1 ec_params_get_cmd_versions {
	uint8_t cmd;      /* Command to check */
};

struct __ec_align2 ec_params_get_cmd_versions_v1 {
	uint16_t cmd;     /* Command to check */
};

struct __ec_align4 ec_response_get_cmd_versions {
	/*
	 * Mask of supported versions; use EC_VER_MASK() to compare with a
	 * desired version.
	 */
	uint32_t version_mask;
};

/*
 * Check EC communications status (busy). This is needed on i2c/spi but not
 * on lpc since it has its own out-of-band busy indicator.
 *
 * lpc must read the status from the command register. Attempting this on
 * lpc will overwrite the args/parameter space and corrupt its data.
 */
#define EC_CMD_GET_COMMS_STATUS		0x0009

/* Avoid using ec_status which is for return values */
enum ec_comms_status {
	EC_COMMS_STATUS_PROCESSING	= 1 << 0,	/* Processing cmd */
};

struct __ec_align4 ec_response_get_comms_status {
	uint32_t flags;		/* Mask of enum ec_comms_status */
};

/* List the features supported by the firmware */
#define EC_CMD_GET_FEATURES  0x000D

/* Supported features */
enum ec_feature_code {
	/*
	 * This image contains a limited set of features. Another image
	 * in RW partition may support more features.
	 */
	EC_FEATURE_LIMITED = 0,
	/*
	 * Commands for probing/reading/writing/erasing the flash in the
	 * EC are present.
	 */
	EC_FEATURE_FLASH = 1,
	/*
	 * Can control the fan speed directly.
	 */
	EC_FEATURE_PWM_FAN = 2,
	/*
	 * Can control the intensity of the keyboard backlight.
	 */
	EC_FEATURE_PWM_KEYB = 3,
	/*
	 * Support Google lightbar, introduced on Pixel.
	 */
	EC_FEATURE_LIGHTBAR = 4,
	/* Control of LEDs  */
	EC_FEATURE_LED = 5,
	/* Exposes an interface to control gyro and sensors.
	 * The host goes through the EC to access these sensors.
	 * In addition, the EC may provide composite sensors, like lid angle.
	 */
	EC_FEATURE_MOTION_SENSE = 6,
	/* The keyboard is controlled by the EC */
	EC_FEATURE_KEYB = 7,
	/* The AP can use part of the EC flash as persistent storage. */
	EC_FEATURE_PSTORE = 8,
	/* The EC monitors BIOS port 80h, and can return POST codes. */
	EC_FEATURE_PORT80 = 9,
	/*
	 * Thermal management: include TMP specific commands.
	 * Higher level than direct fan control.
	 */
	EC_FEATURE_THERMAL = 10,
	/* Can switch the screen backlight on/off */
	EC_FEATURE_BKLIGHT_SWITCH = 11,
	/* Can switch the wifi module on/off */
	EC_FEATURE_WIFI_SWITCH = 12,
	/* Monitor host events, through for example SMI or SCI */
	EC_FEATURE_HOST_EVENTS = 13,
	/* The EC exposes GPIO commands to control/monitor connected devices. */
	EC_FEATURE_GPIO = 14,
	/* The EC can send i2c messages to downstream devices. */
	EC_FEATURE_I2C = 15,
	/* Command to control charger are included */
	EC_FEATURE_CHARGER = 16,
	/* Simple battery support. */
	EC_FEATURE_BATTERY = 17,
	/*
	 * Support Smart battery protocol
	 * (Common Smart Battery System Interface Specification)
	 */
	EC_FEATURE_SMART_BATTERY = 18,
	/* EC can detect when the host hangs. */
	EC_FEATURE_HANG_DETECT = 19,
	/* Report power information, for pit only */
	EC_FEATURE_PMU = 20,
	/* Another Cros EC device is present downstream of this one */
	EC_FEATURE_SUB_MCU = 21,
	/* Support USB Power delivery (PD) commands */
	EC_FEATURE_USB_PD = 22,
	/* Control USB multiplexer, for audio through USB port for instance. */
	EC_FEATURE_USB_MUX = 23,
	/* Motion Sensor code has an internal software FIFO */
	EC_FEATURE_MOTION_SENSE_FIFO = 24,
	/* Support temporary secure vstore */
	EC_FEATURE_VSTORE = 25,
	/* EC decides on USB-C SS mux state, muxes configured by host */
	EC_FEATURE_USBC_SS_MUX_VIRTUAL = 26,
	/* EC has RTC feature that can be controlled by host commands */
	EC_FEATURE_RTC = 27,
	/* The MCU exposes a Fingerprint sensor */
	EC_FEATURE_FINGERPRINT = 28,
};

#define EC_FEATURE_MASK_0(event_code) (1UL << (event_code % 32))
#define EC_FEATURE_MASK_1(event_code) (1UL << (event_code - 32))
struct __ec_align4 ec_response_get_features {
	uint32_t flags[2];
};

/*
 * Reboot NOW
 *
 * This command will work even when the EC LPC interface is busy, because the
 * reboot command is processed at interrupt level.  Note that when the EC
 * reboots, the host will reboot too, so there is no response to this command.
 *
 * Use EC_CMD_REBOOT_EC to reboot the EC more politely.
 */
#define EC_CMD_REBOOT 0x00D1  /* Think "die" */


/*
 * Check EC communications status (busy). This is needed on i2c/spi but not
 * on lpc since it has its own out-of-band busy indicator.
 *
 * lpc must read the status from the command register. Attempting this on
 * lpc will overwrite the args/parameter space and corrupt its data.
 */
#define EC_CMD_GET_COMMS_STATUS		0x0009

