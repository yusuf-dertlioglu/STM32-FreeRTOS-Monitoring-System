/*
 * flash_mem.h
 *
 *  Created on: 01.10.2026
 *      Author: Yusuf Dertlioglu
 */

#include <stdint.h>

#define SETTINGS_FLASH_ADDR 0x08020000
#define SETTINGS_MAGIC 0xDEADBEEF

typedef struct
{
	uint32_t magic;
	float temp_alarm_limit;
	uint16_t vib_alarm_limit;
	uint8_t mpu_range_index;
	uint32_t crc;
} Settings_t;


void Settings_Load(Settings_t *settings);
void Settings_Save(Settings_t *settings);

#ifndef INC_FLASH_MEM_H_
#define INC_FLASH_MEM_H_



#endif /* INC_FLASH_MEM_H_ */
