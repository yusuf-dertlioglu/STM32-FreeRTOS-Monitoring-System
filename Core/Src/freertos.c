/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "i2c.h"
#include "ssd1306.h"
#include "ssd1306_fonts.h"
#include <stdio.h>
#include "bme280.h"
#include <stdlib.h>
#include "settings.h"
#include "flash_mem.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

typedef struct {
	float temp;
	float hum;
	uint16_t press_hpa;
	int16_t accelX;
	int16_t accelY;
	int16_t accelZ;
	uint8_t alarm_active; // 0 = Alles OK, 1 = ALARM!
}SensorData_t;

typedef struct {
	uint32_t last_activity_tick;// Für 10s Timeout
	float temp_alarm_limit; // z. B. 30.0 °C
	uint16_t vib_alarm_limit; // z. B. 5000 LSB
	uint8_t mpu_range_g[4]; // 2, 4 oder 8
	uint8_t mpu_range_index;
	uint8_t menu_cursor; // Aktuell gewählter Menüpunkt (0 bis MENU_ITEM_COUNT-1)
	uint8_t current_page;
	uint8_t flag;
	UIState_t ui_state;
}SystemData_t;

typedef struct {
	uint16_t magic_key;
	SystemData_t data;
}EEPROM_Layout_t;

SensorData_t g_sensor_data;
SystemData_t g_system_data;

uint8_t vibration_history[128]= {0};
uint8_t history_index= 0;

/* USER CODE END Variables */
/* Definitions for TaskDisplay */
osThreadId_t TaskDisplayHandle;
const osThreadAttr_t TaskDisplay_attributes = {
  .name = "TaskDisplay",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityBelowNormal,
};
/* Definitions for TaskSensor */
osThreadId_t TaskSensorHandle;
const osThreadAttr_t TaskSensor_attributes = {
  .name = "TaskSensor",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityHigh,
};
/* Definitions for I2CMutex */
osMutexId_t I2CMutexHandle;
const osMutexAttr_t I2CMutex_attributes = {
  .name = "I2CMutex"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

void MPU6050_SetFullScaleRange(uint8_t mpu_range_g){
	uint8_t range_addr;
	if(mpu_range_g== 2) range_addr= 0x00;
	if(mpu_range_g== 4) range_addr= 0x08;
	if(mpu_range_g== 8) range_addr= 0x10;
	if(mpu_range_g== 16) range_addr= 0x18;
	HAL_I2C_Mem_Write(&hi2c1, (0x68 << 1), 0x1C, I2C_MEMADD_SIZE_8BIT, &range_addr, 1, 100);
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
	uint32_t now = HAL_GetTick();
	static uint32_t last_press = 0;

	if (now - last_press < 200) return;
	last_press = now;

	// Aktivität registrieren (Timer zurücksetzen)
	g_system_data.last_activity_tick = now;

	// === TASTE 1 (Menü öffnen / Runter) ===
	if (GPIO_Pin == GPIO_PIN_0)
	{
	    if (g_system_data.ui_state == UI_STATE_MAIN_PAGES) {
	        // Aus der Hauptansicht ins Menü wechseln
	        g_system_data.ui_state = UI_STATE_MENU_MAIN;
	        g_system_data.menu_cursor = 0;
	    }
	    else if (g_system_data.ui_state == UI_STATE_MENU_MAIN) {
	        // Im Menü nach unten navigieren
	        g_system_data.menu_cursor = (g_system_data.menu_cursor + 1) % MENU_ITEM_COUNT;
	    }
	    else if (g_system_data.ui_state == UI_STATE_MENU_EDIT) {
	        // Wert verringern
	        if (g_system_data.menu_cursor == MENU_ITEM_TEMP_ALARM){
	        	if(g_system_data.temp_alarm_limit > 0.0f){
	        		g_system_data.temp_alarm_limit -= 1.0f;
	        	}
	        }
	        else if (g_system_data.menu_cursor == MENU_ITEM_VIB_ALARM){
	        	if(g_system_data.vib_alarm_limit > 500){
	        		g_system_data.vib_alarm_limit -= 500;
	        	}
	        	else{
	        		g_system_data.vib_alarm_limit = 3000;
	        	}
	        }
	        else if (g_system_data.menu_cursor == MENU_ITEM_MPU_RANGE){
	        	if(g_system_data.mpu_range_index > 0){
	        		g_system_data.mpu_range_index -= 1;
	        	}
	        	else{
	        		g_system_data.mpu_range_index = 3;
	        	}
	        }
	    }
	}

	// === TASTE 2 (Hoch) ===
	else if (GPIO_Pin == GPIO_PIN_1)
	{
	    if (g_system_data.ui_state == UI_STATE_MENU_MAIN) {
	        // Im Menü nach oben navigieren
	        if (g_system_data.menu_cursor == 0) g_system_data.menu_cursor = MENU_ITEM_COUNT - 1;
	        else g_system_data.menu_cursor--;
	    }
	    else if (g_system_data.ui_state == UI_STATE_MENU_EDIT) {
	        // Wert erhöhen
	        if (g_system_data.menu_cursor == MENU_ITEM_TEMP_ALARM){
	        	if(g_system_data.temp_alarm_limit <= 60.0f)
	        	g_system_data.temp_alarm_limit += 1.0f;
	        }
	        else if (g_system_data.menu_cursor == MENU_ITEM_VIB_ALARM){
	        	if(g_system_data.vib_alarm_limit < 3000){
	        		g_system_data.vib_alarm_limit += 500;
	        	}
	        	else{
	        		g_system_data.vib_alarm_limit = 500;
	        	}
	        }
	        else if (g_system_data.menu_cursor == MENU_ITEM_MPU_RANGE){
	        	if(g_system_data.mpu_range_index < 3){
	        		g_system_data.mpu_range_index += 1;
	        	}
	        	else{
	        		g_system_data.mpu_range_index = 0;
	        	}
	        }
	    }
	}

	   // === TASTE 3 (Auswählen / Bestätigen) ===
	 else if (GPIO_Pin == GPIO_PIN_4)
	 {
	     if (g_system_data.ui_state == UI_STATE_MENU_MAIN) {
	         if (g_system_data.menu_cursor == MENU_ITEM_EXIT) {
	        	 g_system_data.ui_state = UI_STATE_MAIN_PAGES;
	        	 g_system_data.menu_cursor= 0;
	        	 g_system_data.flag= 2;
	         } else {
	            g_system_data.ui_state = UI_STATE_MENU_EDIT; // Wert anpassen
	         }
	     }
	     else if (g_system_data.ui_state == UI_STATE_MENU_EDIT) {
	        // Bearbeiten abschließen und zurück ins Hauptmenü
	        g_system_data.ui_state = UI_STATE_MENU_MAIN;

	         // Falls MPU Range geändert wurde -> Register schreiben!
	         if (g_system_data.menu_cursor == MENU_ITEM_MPU_RANGE) {
	        	 g_system_data.flag = 1;
	         }
	     }
	  }

	else if (GPIO_Pin == GPIO_PIN_5){
		static uint32_t last_interrupt_time = 0;
		uint32_t current_time = HAL_GetTick();

		// Software-Entprellung: Reagiere nur, wenn der letzte Druck > 200 ms her ist
		if (current_time - last_interrupt_time > 200){
			// Weiterblättern: 0 -> 1 -> 2 -> 0 -> 1 ...
			g_system_data.current_page = (g_system_data.current_page + 1) % 3;

			last_interrupt_time = current_time;
		}
	}
}

void system_data_init(void) {

		g_system_data.ui_state= UI_STATE_MAIN_PAGES;
		g_system_data.current_page= 0;
		g_system_data.menu_cursor= 0;
		g_system_data.mpu_range_g[0]= 2;
		g_system_data.mpu_range_g[1]= 4;
		g_system_data.mpu_range_g[2]= 8;
		g_system_data.mpu_range_g[3]= 16;
		g_system_data.flag= 0;


		Settings_t settings;
		Settings_Load(&settings);
		if(settings.magic == SETTINGS_MAGIC){
			g_system_data.temp_alarm_limit = settings.temp_alarm_limit;
			g_system_data.vib_alarm_limit = settings.vib_alarm_limit;
			g_system_data.mpu_range_index = settings.mpu_range_index;
		}
		else{
			g_system_data.temp_alarm_limit = DEFAULT_TEMP_LIMIT;
			g_system_data.vib_alarm_limit = DEFAULT_VIB_LIMIT;
			g_system_data.mpu_range_index= 0;
		}

		g_system_data.last_activity_tick = HAL_GetTick();


}

void Draw_Menu_Page(void)
{
	char buf[32];

	ssd1306_SetCursor(0, 0);
	ssd1306_WriteString("--- SETTINGS ---", Font_7x10, White);

	//Scroll-Fenster berechnen
	uint8_t top_item= 0;
	if(g_system_data.menu_cursor >= 3){
		top_item= g_system_data.menu_cursor - 2; //Scrollt nach unten
	}

	//Die 3 sichtbaren Zeilen rendern
	for(uint8_t line= 0; line<3; line++){
		uint8_t item_idx = top_item + line;
		if(item_idx >= MENU_ITEM_COUNT) break; //Schutz vor Überlauf


		uint8_t y_pos= 16+ (line*16);
		char prefix = (g_system_data.menu_cursor== item_idx) ? '>' : ' ';


		switch(item_idx){
			case MENU_ITEM_TEMP_ALARM:
				snprintf(buf, sizeof(buf), "%cTemp Lim: %.0f C", prefix, g_system_data.temp_alarm_limit);
				break;

			case MENU_ITEM_VIB_ALARM:
				//snprintf(buf, sizeof(buf), "%cVib. Limit: %d", prefix, (int)g_system_data.vib_alarm_limit);
				uint16_t val = g_system_data.vib_alarm_limit;

				// Falls val aus irgendeinem Grund 0 ist, zur Sicherheit 2000 erzwingen zum Testen:
				if (val == 0) val = 2000;

				// Zerlegen in einzelne Ziffern (garantiert ohne sprintf-Fehler!)
				char d1 = '0' + ((val / 1000) % 10);
				char d2 = '0' + ((val / 100) % 10);
				char d3 = '0' + ((val / 10) % 10);
				char d4 = '0' + (val % 10);

				buf[0] = prefix;
				buf[1] = 'V';
				buf[2]= 'i';
				buf[3]= 'b';
				buf[4] = '.';
				buf[5] = 'L';
				buf[6] = 'i';
				buf[7] = 'm';
				buf[8] = 'i';
				buf[9] = 't';
				buf[10] = ':';
				buf[11] = ' ';
				buf[12] = d1;
				buf[13] = d2;
				buf[14] = d3;
				buf[15] = d4;
				buf[16] = '\0'; // String-Ende
				break;

			case MENU_ITEM_MPU_RANGE:
				snprintf(buf, sizeof(buf), "%cVib. Sens.: %dg", prefix, g_system_data.mpu_range_g[g_system_data.mpu_range_index]);
				break;

			case MENU_ITEM_EXIT:
				snprintf(buf, sizeof(buf), "%cSave & Exit", prefix);
				break;
		}

		ssd1306_SetCursor(0, y_pos);
		ssd1306_WriteString(buf, Font_7x10, White);

		// Wenn im EDIT-Modus: Blinkenden Hinweis anzeigen
		if (g_system_data.ui_state == UI_STATE_MENU_EDIT && g_system_data.menu_cursor == item_idx) {
		ssd1306_SetCursor(120, y_pos);
		ssd1306_WriteString("*", Font_7x10, White);
		}
	}
}

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);
void StartTask02(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

	system_data_init();

  /* USER CODE END Init */
  /* Create the mutex(es) */
  /* creation of I2CMutex */
  I2CMutexHandle = osMutexNew(&I2CMutex_attributes);

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of TaskDisplay */
  TaskDisplayHandle = osThreadNew(StartDefaultTask, NULL, &TaskDisplay_attributes);

  /* creation of TaskSensor */
  TaskSensorHandle = osThreadNew(StartTask02, NULL, &TaskSensor_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the TaskDisplay thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
	osDelay(500);

	// OLED & BME280 Initialisierung
	if(osMutexAcquire(I2CMutexHandle, 100)==osOK){
		ssd1306_Init();
		osMutexRelease(I2CMutexHandle);
	}

	char buf[32];

	for(;;)
	{
		if(osMutexAcquire(I2CMutexHandle, 20)==osOK){
			// --- Display aktualisieren ---
			ssd1306_Fill(Black);

			// 1. Timeout-Prüfung (10 Sekunden = 10000 ms)
			if(g_system_data.ui_state != UI_STATE_MAIN_PAGES)
				{
					if((HAL_GetTick() - g_system_data.last_activity_tick) > 10000)
					{
						// Automatisch zurück zur Hauptseite
						g_system_data.ui_state = UI_STATE_MAIN_PAGES;
						g_system_data.menu_cursor= 0;
					}
				}

			if (g_system_data.ui_state == UI_STATE_MAIN_PAGES){
				if(g_system_data.current_page == 0){

					// === SEITE 1: BME280 ===
					ssd1306_SetCursor(0, 0);
					ssd1306_WriteString("--- BME280 ---", Font_7x10, White);

					sprintf(buf, "Temp:  %.1f C", (float)g_sensor_data.temp);
					ssd1306_SetCursor(0, 16);
					ssd1306_WriteString(buf, Font_7x10, White);

					sprintf(buf, "Hum:   %.1f %%", (float)g_sensor_data.hum);
					ssd1306_SetCursor(0, 32);
					ssd1306_WriteString(buf, Font_7x10, White);

					sprintf(buf, "Press: %u hPa", g_sensor_data.press_hpa);
					ssd1306_SetCursor(0, 48);
					ssd1306_WriteString(buf, Font_7x10, White);
				}
				else if(g_system_data.current_page == 1){
					// === SEITE 2: MPU6050 ===

					ssd1306_SetCursor(0, 0);
					ssd1306_WriteString("--- MPU6050 ---", Font_7x10, White);

					sprintf(buf, "X: %d mg", g_sensor_data.accelX);
					ssd1306_SetCursor(0, 16);
					ssd1306_WriteString(buf, Font_7x10, White);

					sprintf(buf, "Y: %d mg", g_sensor_data.accelY);
					ssd1306_SetCursor(0, 32);
					ssd1306_WriteString(buf, Font_7x10, White);

					sprintf(buf, "Z: %d mg", g_sensor_data.accelZ);
					ssd1306_SetCursor(0, 48);
					ssd1306_WriteString(buf, Font_7x10, White);
				}
				else if(g_system_data.current_page == 2) // Seite 3 (0, 1, 2)
				{
					ssd1306_SetCursor(0, 0);
					ssd1306_WriteString("--- VIBRATION ---", Font_7x10, White);

					// Den Graph aus dem Ringpuffer zeichnen
					for (int i = 0; i < 127; i++){
						// Wir lesen den Puffer ringförmig ab, damit der aktuelle Wert immer rechts ist
						uint8_t idx1 = (history_index + i) % 128;
						uint8_t idx2 = (history_index + i + 1) % 128;

						int y1 = 55 - vibration_history[idx1]; // 0 ist oben, 63 ist unten
						int y2 = 55 - vibration_history[idx2];

						// Zeichnet eine Linie zwischen den Messpunkten
						ssd1306_Line(i, y1, i + 1, y2, White);
					}
				}

				if(g_sensor_data.alarm_active){
					ssd1306_SetCursor(75, 0);
					ssd1306_WriteString("!ALARM!", Font_7x10, White);
				}
			}
			else if(g_system_data.ui_state == UI_STATE_MENU_MAIN || g_system_data.ui_state == UI_STATE_MENU_EDIT){
				Draw_Menu_Page();
			}

			if(ssd1306_UpdateScreen()!= HAL_OK){
				HAL_I2C_DeInit(&hi2c1);
				HAL_I2C_Init(&hi2c1);
			}
			osMutexRelease(I2CMutexHandle);
		}
		osDelay(100);// Display Refresh-Rate (10 FPS)
	  }



  /* USER CODE END StartDefaultTask */
}

/* USER CODE BEGIN Header_StartTask02 */
/**
* @brief Function implementing the TaskSensor thread.
* @param argument: Not used
* @retval None
*/

#define TEMP_MAX_ALARM	28.0f	// °C
#define ACCEL_MAX_ALARM	1500	// mg
/* USER CODE END Header_StartTask02 */
void StartTask02(void *argument)
{
  /* USER CODE BEGIN StartTask02 */
	// MPU6050 & BME280 einmalig initialisieren (Bus sperren)
	if (osMutexAcquire(I2CMutexHandle, 100) == osOK)
	{
		bme280_init(&hi2c1, 1, 1, 1, 3, 4, 0);
		uint8_t pwr_mgmt = 0x00;
		HAL_I2C_Mem_Write(&hi2c1, (0x68 << 1), 0x6B, I2C_MEMADD_SIZE_8BIT, &pwr_mgmt, 1, 100);
		osMutexRelease(I2CMutexHandle);
	}
  /* Infinite loop */
  for(;;)
  {
	  //Temporäre lokale Variablen für die Messung
		float t= 0, h= 0;
		uint16_t p= 0;
	  int16_t ax_mg= 0, ay_mg= 0, az_mg= 0;

	  // --- I2C-Bus für Sensorabfrage reservieren ---
	  if (osMutexAcquire(I2CMutexHandle, 50) == osOK)
	  {
		  // 1. BME280 Werte auslesen
		  if(bme280_read_data(&t, &h, &p)!= HAL_OK){
			  HAL_I2C_DeInit(&hi2c1);
			  HAL_I2C_Init(&hi2c1);
		  }

		  if(g_system_data.flag == 1){
			  MPU6050_SetFullScaleRange(g_system_data.mpu_range_g[g_system_data.mpu_range_index]);
			  g_system_data.flag= 0;
		  }
		  if(g_system_data.flag == 2){
		  	  Settings_t settings;
		  	  settings.magic= SETTINGS_MAGIC;
		  	  settings.temp_alarm_limit= g_system_data.temp_alarm_limit;
		  	  settings.vib_alarm_limit= g_system_data.vib_alarm_limit;
		  	  settings.mpu_range_index= g_system_data.mpu_range_index;
		  	  Settings_Save(&settings);
		  	  g_system_data.flag= 0;
		  }

	  	  // 2. MPU6050 Werte auslesen
	  	  uint8_t Rec_Data[6];
	  	  HAL_I2C_Mem_Read(&hi2c1, (0x68 << 1), 0x3B, I2C_MEMADD_SIZE_8BIT, Rec_Data, 6, 100);

	  	  int16_t ax = (int16_t)(Rec_Data[0] << 8 | Rec_Data[1]);
	  	  int16_t ay = (int16_t)(Rec_Data[2] << 8 | Rec_Data[3]);
	  	  int16_t az = (int16_t)(Rec_Data[4] << 8 | Rec_Data[5]);

	  	  ax_mg= (int16_t)((int32_t)ax * 1000 / 16384);
	  	  ay_mg= (int16_t)((int32_t)ay * 1000 / 16384);
	  	  az_mg= (int16_t)((int32_t)az * 1000 / 16384);

	  	  osMutexRelease(I2CMutexHandle); // Bus sofort wieder freigeben

	  }

	  int32_t vibration_val= abs(az_mg);
	  uint8_t mapped_val= (vibration_val*60)/4000; // 16384 -> Skalierung für +-2g
	  if(mapped_val > 60) mapped_val= 60;

	  uint8_t is_alarm= 0;
	  if(t> g_system_data.temp_alarm_limit || abs(ax_mg) > g_system_data.vib_alarm_limit || abs(ay_mg) > g_system_data.vib_alarm_limit){
		  is_alarm= 1;
	  }

	  if (osMutexAcquire(I2CMutexHandle, 50) == osOK){

	  	  // 3. Globale Struktur thread-sicher aktualisieren
	  	  g_sensor_data.temp = t;
	  	  g_sensor_data.hum = h;
	  	  g_sensor_data.press_hpa = p;
	  	  g_sensor_data.accelX = ax_mg;
	  	  g_sensor_data.accelY = ay_mg;
	  	  g_sensor_data.accelZ = az_mg;
	  	  vibration_history[history_index]= mapped_val;
	  	  history_index= (history_index+1) % 128;
	  	  g_sensor_data.alarm_active = is_alarm;
	  	  osMutexRelease(I2CMutexHandle); // Bus sofort wieder freigeben
	  }

	  if(is_alarm){
		  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);
	  }
	  else{
		  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);
	  }

	  osDelay(100); //10 Hz Messrate

  }
  /* USER CODE END StartTask02 */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

