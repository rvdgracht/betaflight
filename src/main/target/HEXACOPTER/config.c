
#include <stdbool.h>

#include <platform.h>

#ifdef TARGET_CONFIG

#include "common/axis.h"
#include "drivers/pwm_output.h"
#include "flight/failsafe.h"
#include "sensors/compass.h"

#include "config/config_master.h"

void targetConfiguration(master_t *config)
{
	config->compassConfig.mag_hardware = MAG_AK8975;
	config->motorConfig.motorPwmProtocol = PWM_TYPE_STANDARD;

	/* Failsafe, 10 seconds to land or regain control */
	config->failsafeConfig.failsafe_off_delay = 100;
	config->failsafeConfig.failsafe_procedure = FAILSAFE_PROCEDURE_AUTO_LANDING;
	config->failsafeConfig.failsafe_throttle = 1500;
}
#endif
