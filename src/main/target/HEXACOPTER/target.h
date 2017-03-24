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

#pragma once

#define TARGET_BOARD_IDENTIFIER "HEXA"

#define LED0                    PD2 // Blue
#define LED0_INVERTED

// MPU-9150
#define USE_EXTI
#define MPU_INT_EXTI            PA13 // Requires internal pullup
#define EXTI15_10_CALLBACK_HANDLER_COUNT 1
#define USE_MPU_DATA_READY_SIGNAL

#define GYRO
#define USE_GYRO_MPU6050
#define GYRO_MPU6050_ALIGN      CW0_DEG

#define ACC
#define USE_ACC_MPU6050
#define ACC_MPU6050_ALIGN       CW0_DEG

#define MAG
#define USE_MAG_AK8975
#define MAG_AK8975_ALIGN        CW180_DEG_FLIP

// LPS331AP
//#define BARO
//#define USE_BARO_MS5611
//#define USE_BARO_BMP280

// UARTS
#define USE_UART1	// Debug uart
#define UART1_TX_PIN            PA9
#define UART1_RX_PIN            PA10
#define SERIAL_PORT_COUNT       1
//#define AVOID_UART2_FOR_PWM_PPM
//#define USE_ESCSERIAL
//#define ESCSERIAL_TIMER_TX_HARDWARE 0 // PWM 1

#define USE_I2C
#define USE_I2C_DEVICE_2
#define I2C_DEVICE              (I2CDEV_2)
#define I2C2_SCL                PB10
#define I2C2_SDA                PB11

#define USE_SPI
#define USE_SPI_DEVICE_1

#define USE_RX_SPI_SLAVE
#define RX_SPI_INSTANCE         SPI1

//#define RX_IRQ_PIN		PA8
#define SPI1_NSS_PIN		PA15
#define SPI1_SCK_PIN            PB3
#define SPI1_MISO_PIN           PB4
#define SPI1_MOSI_PIN           PB5

#define DEFAULT_RX_FEATURE      FEATURE_RX_SPI

#if 0	// SPI

#define USE_RX_V202
#define RX_SPI_DEFAULT_PROTOCOL RX_SPI_NRF24_V202_250K	// Adjust..


#endif




// Just in case..
#ifdef USE_RX_SPI
# undef USE_RX_SPI
#endif

#ifdef USE_PWM
# undef USE_PWM
#endif

#ifdef USE_PPM
# undef USE_PPM
#endif

#ifdef SERIAL_RX
# undef SERIAL_RX
#endif







#define TARGET_DEFAULT_MIXER	MIXER_HEX6

//#define SONAR
//#define SONAR_ECHO_PIN          PB1
//#define SONAR_TRIGGER_PIN       PA2

#define TARGET_IO_PORTA		0xbfcf
#define TARGET_IO_PORTB		0xffff
#define TARGET_IO_PORTC		0x03ff
#define TARGET_IO_PORTD		0x0004

#define USABLE_TIMER_CHANNEL_COUNT 6
#define USED_TIMERS             (TIM_N(1) | TIM_N(3) | TIM_N(4) | TIM_N(5) | TIM_N(8))
