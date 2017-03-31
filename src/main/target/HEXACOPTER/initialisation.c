
#include "platform.h"

#include "drivers/io.h"

#include "hex_leds.h"

void targetPreInit(void)
{
	//FIXME Enable 5V here

	GPIO_PinRemapConfig(GPIO_Remap_SPI1, ENABLE);

	for (int i = 0; i < 6; i++) {
		IO_t io = IOGetByTag(DEFIO_TAG_MAKE(DEFIO_GPIOID__C, i));
		IOInit(io, OWNER_LED, RESOURCE_INDEX(i));
		IOConfigGPIO(io, IOCFG_OUT_PP);
		IOLo(io);
	}

	HEX_LED1_OFF;
	HEX_LED2_OFF;
	HEX_LED3_OFF;
	HEX_LED4_OFF;
	HEX_LED5_OFF;
	HEX_LED6_OFF;
}
