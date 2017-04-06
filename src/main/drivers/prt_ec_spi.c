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

#include "build/build_config.h"
#include "config/feature.h"
#include "fc/config.h"

#include "prt_ec/host_cmd.h"

#include "dma.h"
#include "io.h"
#include "io_impl.h"
#include "nvic.h"
#include "rcc.h"
#include "system.h"

#include "hex_leds.h"

static struct prt_ec_packet pkt_in;
//static struct prt_ec_packet pkt_out;

static struct host_packet spi_pkt;

void prt_ec_prep_tx_pkt(struct prt_ec_packet *pkt)
{
//	memcpy(pkt_out, spi_pkt->io_pkt, sizeof(pkt_out));
	UNUSED(pkt);
}

void prt_ec_rx_dma_irq(struct dmaChannelDescriptor_s *cd)
{
	DMA_CLEAR_FLAG(cd, DMA_IT_TCIF);

	// Drop cmd packages when busy without notice
	if (host_cmd_is_ready()) {
		memcpy(&spi_pkt.io_pkt, &pkt_in, sizeof(pkt_in));
		host_pkt_recieved(&spi_pkt);
	}

#if 0
	int ret;

	if (host_cmd_get_state() != HOST_CMD_READY) {
		spi_pkt.io_pkt.id = EC_RES_NOT_READY;
		ret = -EBUSY;
		goto reply;
	}

	memcpy(&spi_pkt.io_pkt, &pkt_in, sizeof(pkt_in));
	host_pkt_recieved(&spi_pkt);
reply:
	if (pkt_in.flags & PRT_EC_FLAG_ACKREQ) {
		prt_ec_prep_tx_pkt(&spi_pkt);
	}
#endif
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
	dmaSetHandler(DMA1_CH2_HANDLER, prt_ec_rx_dma_irq, NVIC_BUILD_PRIORITY(1, 0), NULL);

	spi_pkt.send_response = prt_ec_prep_tx_pkt;

	/* Enable RX DMA */
	DMA_ITConfig(DMA1_Channel2, DMA_IT_TC | DMA_IT_TC, ENABLE);

	/* Enable the SPI peripheral */
	SPI1->CR1 |= (1 << 11) | (1 << 6);

	SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Rx | SPI_I2S_DMAReq_Tx, ENABLE);

	DMA_Cmd(DMA1_Channel2, ENABLE);


	return true;
}

#endif /* USE_PRT_EC_SPI */
