
#include <platform.h>

#ifdef TARGET_CONFIG

#include "common/axis.h"

#include "drivers/pwm_output.h"

#include "sensors/compass.h"

#include "flight/mixer.h"

void targetConfiguration(void)
{
	compassConfigMutable()->mag_hardware = MAG_AK8975;
	motorConfigMutable()->dev.motorPwmProtocol = PWM_TYPE_STANDARD;
}
#endif
