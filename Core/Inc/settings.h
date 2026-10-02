/*
 * settings.h
 *
 *  Created on: 29.09.2026
 *      Author: Yusuf Dertlioglu
 */

#define EEPROM_MAGIC_KEY 0x4142 // "AB" - Bestätigt, dass EEPROM initialisiert ist
#define EEPROM_START_ADDR 0x00

// Standard-Startwerte
#define DEFAULT_TEMP_LIMIT 28.0f
#define DEFAULT_VIB_LIMIT 1500
#define DEFAULT_MPU_RANGE 2 // 2g

typedef enum {
	UI_STATE_MAIN_PAGES, // Normale 3 Seiten (Sensoren / Graph)
	UI_STATE_MENU_MAIN, // Hauptmenü (Auswahl der Punkte)
	UI_STATE_MENU_EDIT // Wert anpassen (z. B. Limit ändern)
} UIState_t;

typedef enum {
	MENU_ITEM_TEMP_ALARM, // Temp Alarm-Schwelle (°C)
	MENU_ITEM_VIB_ALARM, // Vibrations-Schwelle
	MENU_ITEM_MPU_RANGE, // MPU6050 Scale (2g, 4g, 8g)
	MENU_ITEM_EXIT, // Menü verlassen
	MENU_ITEM_COUNT // Anzahl der Punkte (für Modulo/Limits)
} MenuItem_t;

#ifndef INC_SETTINGS_H_
#define INC_SETTINGS_H_



#endif /* INC_SETTINGS_H_ */
