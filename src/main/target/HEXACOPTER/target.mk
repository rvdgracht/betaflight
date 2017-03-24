F1_TARGETS += $(TARGET)
FLASH_SIZE = 128
FEATURES = ONBOARDFLASH

TARGET_SRC = \
	drivers/accgyro_mpu.c           \
	drivers/accgyro_mpu6050.c       \
	drivers/compass_ak8975.c
