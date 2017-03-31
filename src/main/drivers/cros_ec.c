/*
 * This file is part of Cleanflight.
 *
 * Cleanflight is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Cleanflight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Cleanflight.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <platform.h>

#ifdef USE_CROS_EC

#ifndef CROS_EC_SPI_INSTANCE
#error "CROS_EC_SPI_INSTANCE is not defined"
#endif

#include "build/build_config.h"
#include "config/feature.h"
#include "fc/config.h"
#include "cros/ec_command.h"
#include "cros/host_command.h"

#include "bus_spi.h"
#include "dma.h"
#include "exti.h"
#include "io.h"
#include "io_impl.h"
#include "nvic.h"
#include "rcc.h"
#include "system.h"

#ifndef CROS_EC_SPI_DMA_CH_RX
#define CROS_EC_SPI_DMA_CH_RX		DMA1_Channel2
#endif

#ifndef CROS_EC_SPI_DMA_CH_RX_IRQN
#define CROS_EC_SPI_DMA_CH_RX_IRQN	DMA1_Channel2_IRQn
#endif

#ifndef CROS_EC_SPI_DMA_CH_TX
#define CROS_EC_SPI_DMA_CH_TX		DMA1_Channel3
#endif

/*
 * Timeout to wait for SPI request packet
 *
 * This affects the slowest SPI clock we can support.  A delay of 8192 us
 * permits a 512-byte request at 500 KHz, assuming the master starts sending
 * bytes as soon as it asserts chip select.
 */
#define SPI_CMD_RX_TIMEOUT_MS 82

/*
 * Max data size for a version 3 request/response packet.  This is big enough
 * to handle a request/response header, flash write offset/size, and 512 bytes
 * of flash data.
 */
#define SPI_MAX_REQUEST_SIZE 0x220
#define SPI_MAX_RESPONSE_SIZE 0x220

/*
 * The AP blindly clocks back bytes over the SPI interface looking for a
 * framing byte.  So this preamble must always precede the actual response
 * packet.  Search for "spi-frame-header" in U-boot to see how that's
 * implemented.
 *
 * The preamble must be 32-bit aligned so that the response buffer is also
 * 32-bit aligned.
 */
static const uint8_t out_preamble[4] = {
	EC_SPI_PROCESSING,
	EC_SPI_PROCESSING,
	EC_SPI_PROCESSING,
	EC_SPI_FRAME_START,  /* This is the byte which matters */
};

/*
 * Space allocation of the past-end status byte (EC_SPI_PAST_END) in the out_msg
 * buffer. This seems to be dynamic because the F0 family needs to send it 4
 * times in order to make sure it actually stays at the repeating byte after DMA
 * ends.
 */
#define EC_SPI_PAST_END_LENGTH 1

static struct host_packet spi_packet;

enum spi_state {
	/* SPI not enabled (initial state, and when chipset is off) */
	SPI_STATE_DISABLED = 0,

	/* Setting up receive DMA */
	SPI_STATE_PREPARE_RX,

	/* Ready to receive next request */
	SPI_STATE_READY_TO_RX,

	/* Receiving request */
	SPI_STATE_RECEIVING_HDR,
	SPI_STATE_RECEIVING_PAY,

	/* Processing request */
	SPI_STATE_PROCESSING,

	/* Sending response */
	SPI_STATE_SENDING,

	/*
	 * Received bad data - transaction started before we were ready, or
	 * packet header from host didn't parse properly.  Ignoring received
	 * data.
	 */
	SPI_STATE_RX_BAD,
};


static struct cros_ec_spi_priv {
	// Flags
	enum spi_state state;
	int setup_transaction_later;

	// Buffers
	uint8_t in_msg[SPI_MAX_REQUEST_SIZE] __aligned(4);
	uint8_t out_msg[SPI_MAX_RESPONSE_SIZE + sizeof(out_preamble) +
		EC_SPI_PAST_END_LENGTH] __aligned(4);

	// IRQs
	NVIC_InitTypeDef nvic_rx_dma;
	extiCallbackRec_t exti_nss;

	// DMAs
	DMA_InitTypeDef dma_rx_cfg;
	DMA_InitTypeDef dma_tx_cfg;

	// IOs
	IO_t spi_nss;
	IO_t spi_miso;
	IO_t spi_mosi;
	IO_t spi_sck;
} cros_spi_priv;

static void cros_ec_reinit(void);

/**
 * Sends a byte over SPI without DMA
 *
 * This is mostly used when we want to relay status bytes to the AP while we're
 * recieving the message and we're thinking about it.
 *
 * @note It may be sent 0, 1, or >1 times, depending on whether the host clocks
 * the bus or not. Basically, the EC is saying "if you ask me what my status is,
 * you'll get this value.  But you're not required to ask, or you can ask
 * multiple times."
 *
 * @param byte	status byte to send, one of the EC_SPI_* #defines from
 *		ec_commands.h
 */
static void tx_status(uint8_t byte)
{
	SPI_I2S_SendData(CROS_EC_SPI_INSTANCE, byte);
}

void cros_ec_dma_start_rx(uint16_t count, uint32_t offset)
{
	struct cros_ec_spi_priv *priv = &cros_spi_priv;

	/* Disable the DMA channel */
	DMA_Cmd(CROS_EC_SPI_DMA_CH_RX, DISABLE);

	/* Set bytes to receive */
	DMA_SetCurrDataCounter(CROS_EC_SPI_DMA_CH_RX, count);

	/* Set position in the receive buffer */
	CROS_EC_SPI_DMA_CH_RX->CMAR = (uint32_t)(priv->in_msg + offset);

	/* Flush data when starting a new transfer */
	if (offset == 0) {
		asm volatile("dsb;");
	}

	/* Enable the DMA channel */
	DMA_Cmd(CROS_EC_SPI_DMA_CH_RX, ENABLE);
}

void cros_ec_dma_start_tx(uint32_t count)
{
	/* Disable the DMA channel */
	DMA_Cmd(CROS_EC_SPI_DMA_CH_TX, DISABLE);

	/* Set bytes to transmit */
	DMA_SetCurrDataCounter(CROS_EC_SPI_DMA_CH_TX, count);

	asm volatile("dsb;");

	/* Enable the DMA channel */
	DMA_Cmd(CROS_EC_SPI_DMA_CH_TX, ENABLE);
}

/*
 * If a setup_for_transaction() was postponed, call it now.
 * Note that setup_for_transaction() cancels Tx DMA.
 */
static void cros_ec_check_setup_transaction_later(void)
{
	struct cros_ec_spi_priv *priv = &cros_spi_priv;

	if (priv->setup_transaction_later) {
		cros_ec_reinit(); /* Fix for bug chrome-os-partner:31390 */
		/*
		 * 'state' is set to SPI_STATE_READY_TO_RX. Somehow AP
		 * de-asserted the SPI NSS during the handler was running.
		 * Thus, the pending result will be dropped anyway.
		 */
	}
}

/**
 * Called to send a response back to the host.
 *
 * Some commands can continue for a while. This function is called by
 * host_command when it completes.
 *
 */
static void cros_ec_send_response_packet(struct host_packet *pkt)
{
	struct cros_ec_spi_priv *priv = &cros_spi_priv;

	/*
	 * If we're not processing, then the AP has already terminated the
	 * transaction, and won't be listening for a response.
	 */
	if (priv->state != SPI_STATE_PROCESSING)
		return;

	/* state == SPI_STATE_PROCESSING */

	/* Append our past-end byte, which we reserved space for. */
	((uint8_t *)pkt->response)[pkt->response_size + 0] = EC_SPI_PAST_END;

	/* Transmit the reply */
	cros_ec_dma_start_tx(sizeof(out_preamble) + pkt->response_size +
		EC_SPI_PAST_END_LENGTH);

	/*
	 * Before the state is set to SENDING, any CS de-assertion would
	 * set setup_transaction_later to 1.
	 */
	priv->state = SPI_STATE_SENDING;
	cros_ec_check_setup_transaction_later();
}

/*
 * Get ready to receive a message from the master.
 *
 * Set up our RX DMA and disable our TX DMA. Set up the data output so that
 * we will send preamble bytes.
 */
static void cros_ec_setup_transaction(void)
{
	struct cros_ec_spi_priv *priv = &cros_spi_priv;
	volatile uint8_t dummy __attribute__((unused));

	/* clear this as soon as possible */
	priv->setup_transaction_later = 0;

	/* Not ready to receive yet */
	tx_status(EC_SPI_NOT_READY);

	/* We are no longer actively processing a transaction */
	priv->state = SPI_STATE_PREPARE_RX;

	/* Stop sending response, if any */
	DMA_Cmd(CROS_EC_SPI_DMA_CH_TX, DISABLE);

	/*
	 * Read dummy bytes in case there are some pending; this prevents the
	 * receive DMA from getting that byte right when we start it.
	 */
	dummy = SPI_I2S_ReceiveData(CROS_EC_SPI_INSTANCE);

	/* Start DMA */
	cros_ec_dma_start_rx(sizeof(struct ec_host_request), 0);

	/* Ready to receive */
	priv->state = SPI_STATE_READY_TO_RX;
	tx_status(EC_SPI_OLD_READY);
}

static void cros_ec_nss_irq(extiCallbackRec_t *cb)
{
	struct cros_ec_spi_priv *priv = &cros_spi_priv;

	UNUSED(cb);

	/* If not enabled, ignore glitches on NSS */
	if (priv->state == SPI_STATE_DISABLED)
		return;

	/* Check chip select.  If it's high, the AP ended a transaction. */
	if (IORead(priv->spi_nss)) {
		/*
		 * If the buffer is still used by the host command, postpone
		 * the DMA rx setup.
		 */
		if (priv->state == SPI_STATE_PROCESSING) {
			priv->setup_transaction_later = 1;
			return;
		}

		/* Set up for the next transaction */
		cros_ec_reinit(); /* Fix for bug chrome-os-partner:31390 */
		return;
	}

	/* Chip select is low = asserted */
	if (priv->state != SPI_STATE_READY_TO_RX) {
		/*
		 * AP started a transaction but we weren't ready for it.
		 * Tell AP we weren't ready, and ignore the received data.
		 */
		tx_status(EC_SPI_NOT_READY);
		priv->state = SPI_STATE_RX_BAD;
		return;
	}

	/* We're now inside a transaction */
	priv->state = SPI_STATE_RECEIVING_HDR;
	tx_status(EC_SPI_RECEIVING);
}

void cros_ec_rx_dma_irq(struct dmaChannelDescriptor_s *cd)
{
	struct cros_ec_spi_priv *priv = &cros_spi_priv;
	struct ec_host_request *r = (struct ec_host_request *)priv->in_msg;

	UNUSED(cd);

	if (priv->state == SPI_STATE_RECEIVING_HDR) {
		if (r->struct_version != EC_HOST_REQUEST_VERSION)
			goto spi_event_error;

		/* Reconfigure dma ASAP to pull in the remainder */
		cros_ec_dma_start_rx(r->data_len, sizeof(*r));

		priv->state = SPI_STATE_RECEIVING_PAY;
		return;

	} else if (priv->state == SPI_STATE_RECEIVING_PAY) {

		spi_packet.send_response = cros_ec_send_response_packet;

		spi_packet.request = priv->in_msg;
		spi_packet.request_temp = NULL;
		spi_packet.request_max = sizeof(priv->in_msg);
		spi_packet.request_size = sizeof(*r) + r->data_len;

		/* Response must start with the preamble */
		memcpy(priv->out_msg, out_preamble, sizeof(out_preamble));
		spi_packet.response = priv->out_msg + sizeof(out_preamble);
		/* Reserve space for the preamble and trailing past-end byte */
		spi_packet.response_max = sizeof(priv->out_msg)
			- sizeof(out_preamble) - EC_SPI_PAST_END_LENGTH;
		spi_packet.response_size = 0;

		spi_packet.driver_result = EC_RES_SUCCESS;

		/* Move to processing state */
		priv->state = SPI_STATE_PROCESSING;
		tx_status(EC_SPI_PROCESSING);

		host_packet_receive(&spi_packet);
		return;
	}

spi_event_error:
	/* Error, timeout, or protocol we can't handle.  Ignore data. */
	tx_status(EC_SPI_RX_BAD_DATA);
	priv->state = SPI_STATE_RX_BAD;
}

static void cros_ec_reinit(void)
{
	struct cros_ec_spi_priv *priv = &cros_spi_priv;
	volatile uint32_t dummy __attribute__((unused));

	/* Reset the SPI Peripheral to clear any existing weird states. */
	priv->state = SPI_STATE_DISABLED;
	SPI_I2S_DeInit(CROS_EC_SPI_INSTANCE);

	/* Enable clocks to SPI1 module */
	RCC_ClockCmd(RCC_APB2(SPI1), ENABLE);

	/* Delay 1 APB clock cycle after the clock is enabled */
	dummy = DMA1->ISR;

	/* Re-initialize SPI */
	SPI_InitTypeDef spiInit;
	SPI_StructInit(&spiInit);
	SPI_Init(CROS_EC_SPI_INSTANCE, &spiInit);

	/* Disable hardware crc checking */
	SPI_CalculateCRC(CROS_EC_SPI_INSTANCE, DISABLE);

	/* Enable rx/tx DMA and get ready to receive our first transaction. */
	SPI_I2S_DMACmd(CROS_EC_SPI_INSTANCE,
		SPI_I2S_DMAReq_Tx | SPI_I2S_DMAReq_Rx, ENABLE);

	/* Enable the SPI peripheral */
	SPI_Cmd(CROS_EC_SPI_INSTANCE, ENABLE);

	/* Enable interrupts on NSS */
	EXTIEnable(priv->spi_nss, true);

	/* Set up for next transaction */
	cros_ec_setup_transaction();
}

bool cros_ec_init(void)
{
	struct cros_ec_spi_priv *priv = &cros_spi_priv;

	// This driver only works on SPI1
	if (CROS_EC_SPI_INSTANCE != SPI1)
		return false;

	priv->spi_nss = IOGetByTag(IO_TAG(SPI1_NSS_PIN));
	priv->spi_sck = IOGetByTag(IO_TAG(SPI1_SCK_PIN));
	priv->spi_miso = IOGetByTag(IO_TAG(SPI1_MISO_PIN));
	priv->spi_mosi = IOGetByTag(IO_TAG(SPI1_MOSI_PIN));

	/* Set SPI pins to alternate function */
	IOConfigGPIO(priv->spi_nss, IO_CONFIG(GPIO_Mode_IPU, GPIO_Speed_50MHz));
	IOConfigGPIO(priv->spi_sck, IO_CONFIG(GPIO_Mode_IN_FLOATING, GPIO_Speed_50MHz));
	IOConfigGPIO(priv->spi_miso, IO_CONFIG(GPIO_Mode_AF_PP, GPIO_Speed_50MHz));
	IOConfigGPIO(priv->spi_mosi, IO_CONFIG(GPIO_Mode_IPU, GPIO_Speed_50MHz));

	/* Configure RX DMA channel */
	DMA_StructInit(&priv->dma_rx_cfg);
	priv->dma_rx_cfg.DMA_PeripheralBaseAddr = CROS_EC_SPI_INSTANCE->DR;
	priv->dma_rx_cfg.DMA_MemoryBaseAddr = (uint32_t)priv->in_msg;
	priv->dma_rx_cfg.DMA_Priority = DMA_Priority_VeryHigh;
	priv->dma_rx_cfg.DMA_MemoryInc = DMA_MemoryInc_Enable;
	priv->dma_rx_cfg.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
	priv->dma_rx_cfg.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
	DMA_DeInit(CROS_EC_SPI_DMA_CH_RX);
	DMA_Init(CROS_EC_SPI_DMA_CH_RX, &priv->dma_rx_cfg);
	DMA_ITConfig(CROS_EC_SPI_DMA_CH_RX, DMA_IT_TC, ENABLE);

	/* Configure TX DMA channel */
	DMA_DeInit(CROS_EC_SPI_DMA_CH_TX);
	DMA_StructInit(&priv->dma_tx_cfg);
	priv->dma_tx_cfg.DMA_PeripheralBaseAddr = CROS_EC_SPI_INSTANCE->DR;
	priv->dma_tx_cfg.DMA_MemoryBaseAddr = (uint32_t)priv->out_msg;
	priv->dma_tx_cfg.DMA_DIR = DMA_DIR_PeripheralDST;
	priv->dma_tx_cfg.DMA_Priority = DMA_Priority_High;
	priv->dma_tx_cfg.DMA_MemoryInc = DMA_MemoryInc_Enable;
	priv->dma_tx_cfg.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
	priv->dma_tx_cfg.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
	DMA_DeInit(CROS_EC_SPI_DMA_CH_TX);
	DMA_Init(CROS_EC_SPI_DMA_CH_TX, &priv->dma_tx_cfg);

	/* Setup the nss irq */
	EXTIHandlerInit(&priv->exti_nss, &cros_ec_nss_irq);
	EXTIConfig(priv->spi_nss, &priv->exti_nss, NVIC_BUILD_PRIORITY(1, 0),
		EXTI_Trigger_Rising_Falling);

	/* Setup the rx dma irq */
	dmaInit(DMA1_CH2_HANDLER, OWNER_CROS_EC, 0);
	dmaSetHandler(DMA1_CH2_HANDLER, cros_ec_rx_dma_irq, NVIC_BUILD_PRIORITY(1, 0), NULL);

	cros_ec_reinit();
	return true;
}

#endif /* USE_RX_CROS_EC */
