
#include <stdbool.h>
#include <string.h>

#include "rx/rx.h"
#include "prt_ec/cmd.h"

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

static int prt_ec_handle_rx_cmd(uint16_t *data)
{
	memcpy(&prt_ec_rx_priv.data, data, PRT_EC_RX_CHANNELS * 2);
	prt_ec_rx_priv.is_fresh = true;

	return EC_RES_SUCCESS;
}

DECLARE_HOST_COMMAND(0x64, prt_ec_handle_rx_cmd);

#endif /* USE_RX_PRT_EC */
