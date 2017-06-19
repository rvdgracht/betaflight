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

#ifdef USE_PRT_EC_SPI

#include "dma.h"
#include "nvic.h"
#include "rcc.h"

#include "drivers/io.h"
#include "fc/config.h"
#include "prt_ec/cmd.h"
#include "prt_ec/crc16.h"

static struct prt_ec_pkt pkt_in;
static struct prt_ec_pkt pkt_out;

static int host_cmd_toggle = -1;
static bool host_cmd_queued;

static bool ec_cmd_toggle;
static bool ec_cmd_acked;

static int handle_rx(bool *ack_rx)
{
	int ret;

	if (!(pkt_in.flags & PRT_EC_FLAG_MOSI))
		return -1;

	if (pkt_in.crc != crc16((uint8_t *)&pkt_in, sizeof(pkt_in) - 2))
		return -2;

	if (pkt_in.flags & PRT_EC_FLAG_ACK && !ec_cmd_acked) {
		/* Flip out toggle bit if we've received an expected ack. */
		ec_cmd_acked = true;
		ec_cmd_toggle = !ec_cmd_toggle;
	}

	if (pkt_in.flags & PRT_EC_FLAG_ACKREQ) {
		/* We've received a CMD (ACK) message */
		bool toggle = !!(pkt_in.flags & PRT_EC_FLAG_ACKTGL);

		if (toggle != host_cmd_toggle) {
			host_cmd_queued = false;

			/* This is NOT a resend. */
			ret = host_pkt_recieved(&pkt_in);
			if (ret == 0) {
				host_cmd_queued = true;
				ack_rx = true;
			}
		} else {
			/*
			 * We're received a resend.
			 * Resend ack if we've already pushed this upstream.
			 */
			if (host_cmd_queued)
				ack_rx = true;
		}

		host_cmd_toggle = toggle;
	} else {
		/* We've received a RT data message */
		host_pkt_recieved(&pkt_in);
	}

	return 0;
}

static void handle_tx(bool ack)
{
	if (ec_pop_pkt(&pkt_out) != 0) {
		/* We have no packet to send, make it a dummy one. */
		pkt_out.id = EC_MSG_ID_INVALID;
		pkt_out.flags = 0;
	} else {
		/* Set our toggle bit if required. */
		if ((pkt_out.flags & PRT_EC_FLAG_ACKREQ) && ec_cmd_toggle) {
			pkt_out.flags |= PRT_EC_FLAG_ACKTGL;
			ec_cmd_acked = false;
		}
	}

	/* Ack the received message if requested. */
	if (ack)
		pkt_out.flags |= PRT_EC_FLAG_ACK;

	/* Update the CRC */
	pkt_out.crc = crc16((uint8_t *)&pkt_out, sizeof(pkt_out) - 2);
}

void prt_ec_xfer_done_irq(struct dmaChannelDescriptor_s *cd)
{
	int ret;
	bool ack_rx = false;

	/* Wait until transceive complete.
	 * This checks the state flag as well as follows the
	 * procedure on the Reference Manual (RM0008 rev 16
	 * Section 25.3.9 page 722)
	 */

	while(!(SPI1->SR & SPI_I2S_FLAG_TXE));
	while(SPI1->SR & SPI_I2S_FLAG_BSY);

	/*
	 * First, parse the incoming message.
	 */

	handle_rx(&ack_rx);

	/*
	 * Second, prepare our outgoing message for the next xfer.
	 */

	handle_tx(ack_rx);


	DMA_CLEAR_FLAG(cd, DMA_IT_TCIF);
}

bool prt_ec_spi_init(void)
{
	DMA_InitTypeDef rx_dma;

	IO_t spi_nss = IOGetByTag(IO_TAG(SPI1_NSS_PIN));
	IO_t spi_sck = IOGetByTag(IO_TAG(SPI1_SCK_PIN));
	IO_t spi_miso = IOGetByTag(IO_TAG(SPI1_MISO_PIN));
	IO_t spi_mosi = IOGetByTag(IO_TAG(SPI1_MOSI_PIN));

	/* Set SPI pins to alternate function */
	IOConfigGPIO(spi_nss, IO_CONFIG(GPIO_Mode_IN_FLOATING, GPIO_Speed_50MHz));
	IOConfigGPIO(spi_sck, IO_CONFIG(GPIO_Mode_IN_FLOATING, GPIO_Speed_50MHz));
	IOConfigGPIO(spi_miso, IO_CONFIG(GPIO_Mode_AF_PP, GPIO_Speed_50MHz));
	IOConfigGPIO(spi_mosi, IO_CONFIG(GPIO_Mode_IN_FLOATING, GPIO_Speed_50MHz));

	/* Configure SPI1 peripheral */
	RCC_ClockCmd(RCC_APB2(SPI1), ENABLE);
	SPI_I2S_DeInit(SPI1);

	/* Configure RX DMA channel */
	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

	DMA_DeInit(DMA1_Channel2);
	DMA_StructInit(&rx_dma);
	rx_dma.DMA_PeripheralBaseAddr = (uint32_t)&SPI1->DR;
	rx_dma.DMA_MemoryBaseAddr = (uint32_t)&pkt_in;
	rx_dma.DMA_BufferSize = sizeof(pkt_in) / 2;
	rx_dma.DMA_DIR = DMA_DIR_PeripheralSRC;
	rx_dma.DMA_Mode = DMA_Mode_Circular;
	rx_dma.DMA_Priority = DMA_Priority_VeryHigh;
	rx_dma.DMA_MemoryInc = DMA_MemoryInc_Enable;
	rx_dma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
	rx_dma.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
	rx_dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
	DMA_Init(DMA1_Channel2, &rx_dma);

	/* Configure RX DMA IRQ */
	dmaInit(DMA1_CH2_HANDLER, OWNER_PRT_EC, 0);
	dmaSetHandler(DMA1_CH2_HANDLER, prt_ec_xfer_done_irq, NVIC_BUILD_PRIORITY(1, 0), NULL);

	/* Enable RX DMA */
	DMA_ITConfig(DMA1_Channel2, DMA_IT_TC | DMA_IT_TC, ENABLE);

	/* Enable the SPI peripheral */
	SPI1->CR1 |= (1 << 11) | (1 << 6);

	SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Rx | SPI_I2S_DMAReq_Tx, ENABLE);

	DMA_Cmd(DMA1_Channel2, ENABLE);


	return true;
}

#endif /* USE_PRT_EC_SPI */
