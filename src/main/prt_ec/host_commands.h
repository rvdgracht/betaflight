
#pragma once

#define PRT_EC_PROTOCOL_VERSION                 1

/*
 * Invalid ID.
 * Used as placeholder for internal and dummy messages.
 */
#define EC_MSG_ID_INVALID                       0x00


/*
 * Ask the EC for firmware information.
 */
#define EC_MSG_ID_REQ_INFO                      0x01
#define EC_MSG_ID_RES_INFO                      EC_MSG_ID_REQ_INFO
struct ec_responce_info {
        /* Protocol version */
        uint8_t protocol_version;

        /* Firmware version 2 byte major/minor (minor << 8 | major) */
        uint16_t fw_version;

        /* EC name including the terminating null byte ('\0') */
        char name[20];

        /* Reserved */
        uint8_t reserved;

        /* Features */
        struct {
                /* Digital inputs/outputs */
                uint32_t gpio:1;

                /* Analog input(s) */
                uint32_t ana_in:1;

                /* Analog output(s) */
                uint32_t ana_out:1;

                /* Pwm input(s) */
                uint32_t pwm_in:1;

                /* Pwm output(s) */
                uint32_t pwm_out:1;

                /* Motor(s) */
                uint32_t motor:1;

                /* TTY(s) */
                uint32_t ttys:1;

                /* Touchscreen */
                uint32_t touchscreen:1;
        } features;
} __packed;


/*
 * Ask the EC for information about its gpio feature.
 */
#define EC_MSG_ID_REQ_GPIO_INFO                 0x02
#define EC_MSG_ID_RES_GPIO_INFO                 EC_MSG_ID_REQ_GPIO_INFO
struct ec_responce_gpio_info {
        /* Number of exported GPIOS (max 96) */
        uint8_t count;

        /* Each bit represents a GPIO. (0 = input, 1 = output) */
        uint16_t direction[6];

        /* Each bit represents a GPIO. (0 = low, 1 = high) */
        uint16_t data[6];
} __packed;


/*
 * Get/Set remote GPIO states.
 */
#define EC_MSG_ID_REQ_GPIO_STATES               0x03
#define EC_MSG_ID_RES_GPIO_STATES               EC_MSG_ID_REQ_GPIO_STATES
struct ec_responce_gpio_states {
        /* Each bit represents a GPIO. (0 = low, 1 = high) */
        uint16_t data[6];
} __packed;
