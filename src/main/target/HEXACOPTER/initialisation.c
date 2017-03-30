
#include "platform.h"

void targetPreInit(void)
{
	//FIXME Enable 5V here

	GPIO_PinRemapConfig(GPIO_Remap_SPI1, ENABLE);
}
