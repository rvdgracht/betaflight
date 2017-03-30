
#include <platform.h>

#ifdef TARGET_CONFIG

#include "common/axis.h"
#include "sensors/compass.h"

void targetConfiguration(void)
{
	compassConfigMutable()->mag_hardware = MAG_AK8975;
}
#endif
