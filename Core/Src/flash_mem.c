/*
 * flash_mem.c
 *
 *  Created on: 01.10.2026
 *      Author: Yusuf Dertlioglu
 */

#include "flash_mem.h"
#include <string.h>
#include "main.h"
#include <string.h>

void Settings_Load(Settings_t *settings){
	memcpy(settings, (const void *)SETTINGS_FLASH_ADDR, sizeof(Settings_t));
}

void Settings_Save(Settings_t *settings){
	HAL_FLASH_Unlock();

	FLASH_EraseInitTypeDef erase;
	uint32_t sector_error;

	erase.TypeErase = FLASH_TYPEERASE_SECTORS;
	erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
	erase.Sector = FLASH_SECTOR_5;
	erase.NbSectors = 1;

	if(HAL_FLASHEx_Erase(&erase, &sector_error) != HAL_OK){
		HAL_FLASH_Lock();
		return;
	}

	const uint32_t *data = (const uint32_t*)settings;

	for(uint32_t i = 0; i < sizeof(Settings_t)/4; i++){
		if(HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, SETTINGS_FLASH_ADDR + i * 4, data[i]) != HAL_OK){
			HAL_FLASH_Lock();
			return;
		}
	}

	HAL_FLASH_Lock();
}
