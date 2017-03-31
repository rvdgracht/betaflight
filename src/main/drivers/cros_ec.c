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
#include "exti.h"
#include "io.h"
#include "io_impl.h"
#include "nvic.h"
#include "rcc.h"
#include "system.h"

#ifndef CROS_EC_SPI_DMA_CH_RX
#define CROS_EC_SPI_DMA_CH_RX		DMA1_Channel2
#endif

#ifndef CROS_EC_SPI_DMA_CH_TX
#define CROS_EC_SPI_DMA_CH_TX		DMA1_Channel3
#endif

#define DMA1_Channel2_IRQHandler	cros_ec_rx_dma_irq

//#define CROS_IRQ_DEBUG

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
	SPI_STATE_RECEIVING,

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

void cros_ec_dma_disable_rx(void)
{
	DMA_Cmd(CROS_EC_SPI_DMA_CH_RX, DISABLE);
}

void cros_ec_dma_disable_tx(void)
{
	DMA_Cmd(CROS_EC_SPI_DMA_CH_TX, DISABLE);
}

void cros_ec_dma_start_rx(unsigned count, void *memory)
{
	DMA_InitTypeDef DMA_InitStructure;
	DMA_StructInit(&DMA_InitStructure);
	DMA_InitStructure.DMA_PeripheralBaseAddr = CROS_EC_SPI_INSTANCE->DR;
	DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)memory;
	DMA_InitStructure.DMA_BufferSize = count;
	DMA_InitStructure.DMA_Priority = DMA_Priority_VeryHigh;
	DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
	DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
	DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;

	DMA_DeInit(CROS_EC_SPI_DMA_CH_RX);
	DMA_Init(CROS_EC_SPI_DMA_CH_RX, &DMA_InitStructure);

	/* Flush data in write buffer so that DMA can get the lastest data */
	asm volatile("dsb;");

	DMA_Cmd(CROS_EC_SPI_DMA_CH_RX, ENABLE);
}

void cros_ec_dma_prep_tx(unsigned count, void *memory)
{
	DMA_InitTypeDef DMA_InitStructure;
	DMA_StructInit(&DMA_InitStructure);
	DMA_InitStructure.DMA_PeripheralBaseAddr = CROS_EC_SPI_INSTANCE->DR;
	DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)memory;
	DMA_InitStructure.DMA_BufferSize = count;
	DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralDST;
	DMA_InitStructure.DMA_Priority = DMA_Priority_VeryHigh;
	DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
	DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
	DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;

	DMA_DeInit(CROS_EC_SPI_DMA_CH_TX);
	DMA_Init(CROS_EC_SPI_DMA_CH_TX, &DMA_InitStructure);

	/* Flush data in write buffer so that DMA can get the lastest data */
	asm volatile("dsb;");

	DMA_Cmd(CROS_EC_SPI_DMA_CH_TX, ENABLE);
}

/**
 * Wait until we have received a certain number of bytes
 *
 * Watch the DMA receive channel until it has the required number of bytes,
 * or a timeout occurs
 *
 * We keep an eye on the NSS line - if this goes high then the transaction is
 * over so there is no point in trying to receive the bytes.
 *
 * @param needed	Number of bytes that are needed
 * @return 0 if bytes received, -1 if we hit a timeout or NSS went high
 */
static int cros_ec_wait_for_bytes(uint32_t needed)
{
	struct cros_ec_spi_priv *priv = &cros_spi_priv;

	int done;
	uint32_t start = millis();

	while (1) {
		done = DMA_GetCurrDataCounter(CROS_EC_SPI_DMA_CH_RX);
		if ((sizeof(priv->in_msg) - done) >= needed)
			return 0;
		if (IORead(priv->spi_nss))
			return -1;
		if (millis() - start > SPI_CMD_RX_TIMEOUT_MS)
			return -1;
	}
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
	cros_ec_dma_prep_tx(sizeof(out_preamble) + pkt->response_size
		+ EC_SPI_PAST_END_LENGTH, priv->out_msg);

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
	cros_ec_dma_disable_tx();

	/*
	 * Read dummy bytes in case there are some pending; this prevents the
	 * receive DMA from getting that byte right when we start it.
	 */
	dummy = SPI_I2S_ReceiveData(CROS_EC_SPI_INSTANCE);

	/* Start DMA */
	cros_ec_dma_start_rx(sizeof(priv->in_msg), priv->in_msg);

	/* Ready to receive */
	priv->state = SPI_STATE_READY_TO_RX;
	tx_status(EC_SPI_OLD_READY);
}

//#define CROS_IRQ_DEBUG
static void cros_ec_nss_irq(extiCallbackRec_t *cb)
{
	struct cros_ec_spi_priv *priv = &cros_spi_priv;
	struct ec_host_request *r = (struct ec_host_request *)priv->in_msg;
	uint32_t pkt_size;

	UNUSED(cb);

	/* If not enabled, ignore glitches on NSS */
	if (priv->state == SPI_STATE_DISABLED)
		return;

#ifdef CROS_IRQ_DEBUG
	if (IORead(priv->spi_nss))
		IOHi(DEFIO_IO(PC0));
	else
		IOLo(DEFIO_IO(PC0));
	return;
#else
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
	priv->state = SPI_STATE_RECEIVING;
	tx_status(EC_SPI_RECEIVING);

	/* Wait for version, command, length bytes */
	if (cros_ec_wait_for_bytes(3))
		goto spi_event_error;

	if (priv->in_msg[0] != EC_HOST_REQUEST_VERSION)
		goto spi_event_error;

	/* Wait for the rest of the command header */
	if (cros_ec_wait_for_bytes(sizeof(*r)))
		goto spi_event_error;

	/*
	 * Check how big the packet should be.  We can't just wait to
	 * see how much data the host sends, because it will keep
	 * sending dummy data until we respond.
	 */
	pkt_size = host_request_expected_size(r);
	if (pkt_size == 0 || pkt_size > sizeof(priv->in_msg))
		goto spi_event_error;

	/* Wait for the packet data */
	if (cros_ec_wait_for_bytes(pkt_size))
		goto spi_event_error;

	spi_packet.send_response = cros_ec_send_response_packet;

	spi_packet.request = priv->in_msg;
	spi_packet.request_temp = NULL;
	spi_packet.request_max = sizeof(priv->in_msg);
	spi_packet.request_size = pkt_size;

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

spi_event_error:
	/* Error, timeout, or protocol we can't handle.  Ignore data. */
	tx_status(EC_SPI_RX_BAD_DATA);
	priv->state = SPI_STATE_RX_BAD;
#endif
}

void cros_ec_rx_dma_irq(void)
{
#ifdef CROS_IRQ_DEBUG
        IO_t led3 = IOGetByTag(IO_TAG(PC2));
	IOInit(led3, OWNER_FREE, 0);
	IOConfigGPIO(led3, IO_CONFIG(GPIO_Mode_Out_PP, GPIO_Speed_50MHz));
	IOHi(led3);
#endif
}

static void cros_ec_reinit(void)
{
	struct cros_ec_spi_priv *priv = &cros_spi_priv;
	volatile uint32_t dummy __attribute__((unused));

	/* Reset the SPI Peripheral to clear any existing weird states. */
	priv->state = SPI_STATE_DISABLED;
	SPI_I2S_DeInit(CROS_EC_SPI_INSTANCE);

	/* 40 MHz pin speed */
//	STM32_GPIO_OSPEEDR(GPIO_A) |= 0xff00;

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

#ifdef CROS_IRQ_DEBUG
        IO_t led1 = IOGetByTag(IO_TAG(PC0));
	IOInit(led1, OWNER_FREE, 0);
	IOConfigGPIO(led1, IO_CONFIG(GPIO_Mode_Out_PP, GPIO_Speed_50MHz));
	IOHi(led1);
#endif
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

	/* Setup the nss irq */
	EXTIHandlerInit(&priv->exti_nss, &cros_ec_nss_irq);
	EXTIConfig(priv->spi_nss, &priv->exti_nss, NVIC_BUILD_PRIORITY(1, 0),
		EXTI_Trigger_Rising_Falling);

	/* Setup the rx dma irq */
	priv->nvic_rx_dma.NVIC_IRQChannel = DMA1_Channel2_IRQn;
	priv->nvic_rx_dma.NVIC_IRQChannelPreemptionPriority = 1;
	priv->nvic_rx_dma.NVIC_IRQChannelSubPriority = 0;
//	priv->nvic_rx_dma.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&priv->nvic_rx_dma);

	cros_ec_reinit();
	return true;
}

#endif /* USE_RX_CROS_EC */
