
#include <stdbool.h>

#include <platform.h>

#ifdef TARGET_CONFIG

#include "common/axis.h"
#include "drivers/pwm_output.h"
#include "sensors/compass.h"

#include "config/config_master.h"

void targetConfiguration(master_t *config)
{
	config->compassConfig.mag_hardware = MAG_AK8975;
	config->motorConfig.motorPwmProtocol = PWM_TYPE_STANDARD;
}
#endif
