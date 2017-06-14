
#include <stdbool.h>

#include "rx/rx.h"
#include "prt_ec/cmd.h"
#include "prt_ec/host_commands.h"

#if defined(USE_RX_PRT_EC) && defined(USE_PRT_EC_SPI)

#define PRT_EC_RX_CHANNELS	4

static struct prt_ec_rx {
	bool is_fresh;
	uint16_t data[PRT_EC_PKT_DATA_WORDS];
} prt_ec_rx_priv;

static uint8_t check_received(void)
{
	if (prt_ec_rx_priv.is_fresh) {
		prt_ec_rx_priv.is_fresh = false;
		return RX_FRAME_COMPLETE;
	}
	return RX_FRAME_PENDING;
}

static uint16_t get_raw_rx(const rxRuntimeConfig_t *rxRuntimeConfig, uint8_t channel)
{
	UNUSED(rxRuntimeConfig);
	return prt_ec_rx_priv.data[channel];
}

void prt_ec_rx_init(const rxConfig_t *rxConfig, rxRuntimeConfig_t *rxRuntimeConfig)
{
	UNUSED(rxConfig);

	rxRuntimeConfig->rcFrameStatusFn = check_received;
	rxRuntimeConfig->rxRefreshRate = 20000;
	rxRuntimeConfig->channelCount = PRT_EC_RX_CHANNELS;
	rxRuntimeConfig->rcReadRawFn = get_raw_rx;
}

static int prt_ec_handle_cmd_rc_data(void *data)
{
	struct host_cmd_set_rc_data *cmd_rc_data = data;
	int i;

	for (i = 0; i < PRT_EC_RX_CHANNELS; i++)
		prt_ec_rx_priv.data[i] = cmd_rc_data->channel[i];

	prt_ec_rx_priv.is_fresh = true;

	return 0;
}
DECLARE_HOST_COMMAND(EC_MSG_ID_SET_RC_DATA, prt_ec_handle_cmd_rc_data);


#endif /* USE_RX_PRT_EC */
