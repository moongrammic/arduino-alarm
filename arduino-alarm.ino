/*
 * ЧАСЫ-БУДИЛЬНИК v2.1 (SSD1306 + ENCODER + EEPROM + PC SYNC)
 * Изменения:
 * - Драйвер экрана заменен на SSD1306
 * - Добавлено: Сохранение (EEPROM), Синхронизация с ПК, TimeLib
 */

 #include <Arduino.h>
 #include <U8g2lib.h>
 #include <ClickEncoder.h>
 #include <TimerOne.h>
 #include <TimeLib.h> // Установите "Time" by Michael Margolis через менеджер библиотек
 #include <EEPROM.h>
 
 // ================= НАСТРОЙКИ =================
 #define BUZZER_PIN 8
 #define LDR_PIN    A0
 #define ENC_A      2
 #define ENC_B      3
 #define ENC_BTN    4
 
 #define MAX_ALARMS 5
 #define EEPROM_KEY 123 // Ключ для проверки инициализации памяти
 
 // ================= ОБЪЕКТЫ =================
 
 // --- ЗАМЕНА ЭКРАНА ЗДЕСЬ ---
 // Было SH1106, стало SSD1306. Имя объекта оставил 'u8g2', чтобы не ломать остальной код.
 U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
 
 ClickEncoder encoder(ENC_A, ENC_B, ENC_BTN, 4);
 
 // ================= СТРУКТУРЫ =================
 struct Alarm {
   bool enabled;
   uint8_t hour;
   uint8_t minute;
   uint8_t days; // Битовая маска: 0-Пн ... 6-Вс
   bool triggered;
 };
 
 // ================= ПЕРЕМЕННЫЕ =================
 Alarm alarms[MAX_ALARMS];
 
 enum State { S_HOME, S_ALARM_LIST, S_ALARM_EDIT, S_TIME_SET, S_ALARM_RINGING };
 State currentState = S_HOME;
 
 // Интерфейс
 const char* mainMenuItems[2] = {"БУДИЛЬНИКИ", "ВРЕМЯ"};
 bool inMainMenu = false;      // находимся ли в главном меню
 int  menuSelection = 0;       // 0 или 1
 int alarmListIndex = 0;
 int editAlarmId = -1;
 
 // Буфер редактирования
 Alarm tempAlarm;
 int tempHour, tempMin, tempDay, tempMonth, tempYear;
 enum EditStep { STEP_HH, STEP_MM, STEP_DAYS, STEP_SAVE };
 EditStep currentStep;
 int currentDayEditIndex = 0; // 0=Пн...6=Вс
 
 // Звук
 bool isAlarmRinging = false;
 unsigned long ringStartTime = 0;
 
 // ================= ПРЕРЫВАНИЯ =================
 void timerIsr() {
   encoder.service();
 }
 
 // ================= EEPROM ЛОГИКА =================
 void saveAlarms() {
   EEPROM.put(0, EEPROM_KEY);
   int addr = sizeof(int);
   for (int i = 0; i < MAX_ALARMS; i++) {
     EEPROM.put(addr, alarms[i]);
     addr += sizeof(Alarm);
   }
 }
 
 void loadAlarms() {
   int key;
   EEPROM.get(0, key);
   if (key == EEPROM_KEY) {
     int addr = sizeof(int);
     for (int i = 0; i < MAX_ALARMS; i++) {
       EEPROM.get(addr, alarms[i]);
       alarms[i].triggered = false;
       addr += sizeof(Alarm);
     }
   } else {
     for (int i = 0; i < MAX_ALARMS; i++) {
       alarms[i] = {false, 0, 0, 0, false};
     }
     saveAlarms();
   }
 }
 
 // ================= ЛОГИКА ВРЕМЕНИ =================
 
 // Синхронизация с ПК. Ожидает строку вида: "SYYYYMMDDHHMMSS"
 // Например: S20251231235959
 void checkSerialSync() {
   if (Serial.available()) {
     char c = Serial.read();
     if (c == 'S') {
       String s = Serial.readStringUntil('\n');
       if (s.length() >= 14) {
         int Yr = s.substring(0,4).toInt();
         int Mo = s.substring(4,6).toInt();
         int Dy = s.substring(6,8).toInt();
         int Hr = s.substring(8,10).toInt();
         int Mn = s.substring(10,12).toInt();
         int Sc = s.substring(12,14).toInt();
 
         if (Yr > 2000) { // Простая проверка на мусор
           setTime(Hr, Mn, Sc, Dy, Mo, Yr);
         }
       }
     }
   }
 }
 
 // Проверка дня недели
 bool isAlarmDayActive(uint8_t alarmDays) {
   if (alarmDays == 0) return true; // Однократный
 
   int weekdayToday = weekday(); // TimeLib: 1=Sun, 2=Mon...
   // Конвертируем в нашу систему (0=Mon...6=Sun)
   int currentBitIndex = (weekdayToday == 1) ? 6 : (weekdayToday - 2);
 
   return (alarmDays & (1 << currentBitIndex));
 }
 
 void checkAlarms() {
   static int lastCheckedMin = -1;
   if (minute() == lastCheckedMin) return;
   lastCheckedMin = minute();
 
   for (int i = 0; i < MAX_ALARMS; i++) {
     // Сброс триггера, если время ушло
     if (alarms[i].triggered && (alarms[i].hour != hour() || alarms[i].minute != minute())) {
        alarms[i].triggered = false;
        if (alarms[i].days == 0) { // Однократный выключаем полностью
           alarms[i].enabled = false;
           saveAlarms();
        }
     }
 
     if (alarms[i].enabled && !alarms[i].triggered) {
       if (alarms[i].hour == hour() && alarms[i].minute == minute()) {
         if (isAlarmDayActive(alarms[i].days)) {
           alarms[i].triggered = true;
           startAlarm();
         }
       }
     }
   }
 }
 
 void startAlarm() {
   currentState = S_ALARM_RINGING;
   isAlarmRinging = true;
   ringStartTime = millis();
 }
 
 void stopAlarm() {
   isAlarmRinging = false;
   noTone(BUZZER_PIN);
   currentState = S_HOME;
 }
 
 void buzzerLogic() {
   if (!isAlarmRinging) return;
   if (millis() - ringStartTime > 60000) { stopAlarm(); return; }
 
   unsigned long t = millis() % 1000;
   if (t < 100 || (t > 200 && t < 300)) tone(BUZZER_PIN, 2000);
   else noTone(BUZZER_PIN);
 }
 
 // ================= ОТРИСОВКА =================
 
 void drawHome() {
   char buf[20];
 
   // Крупное время по центру
   u8g2.setFont(u8g2_font_logisoso32_tn);
   sprintf(buf, "%02d:%02d", hour(), minute());
   int w = u8g2.getStrWidth(buf);
   u8g2.drawStr((128 - w) / 2, 35, buf);
 
   // Дата под временем
   u8g2.setFont(u8g2_font_6x13_tr);
   sprintf(buf, "%02d.%02d.%04d", day(), month(), year());
   w = u8g2.getStrWidth(buf);
   u8g2.drawStr((128 - w) / 2, 52, buf);
 
   // Главное меню внизу
   if (inMainMenu) {
     u8g2.setDrawColor(1);
     u8g2.drawBox(0, 53, 128, 11); // затемняем низ
     u8g2.setDrawColor(0);
     u8g2.setFont(u8g2_font_6x12_tr);
 
     // Указатели слева/справа
     u8g2.drawStr(2, 63, "<");
     u8g2.drawStr(122, 63, ">");
 
     // Квадратик вокруг номера пункта
     u8g2.drawFrame(55, 53, 18, 11);
 
     char itemBuf[16];
     sprintf(itemBuf, "%d %s", menuSelection + 1, mainMenuItems[menuSelection]);
     u8g2.drawStr(60, 63, itemBuf);
     u8g2.setDrawColor(1);
   }
 }
 
 void drawAlarmList() {
   u8g2.setFont(u8g2_font_6x12_tr);
   u8g2.drawStr(0, 8, "СПИСОК БУДИЛЬНИКОВ");
   u8g2.drawHLine(0, 10, 128);
 
   char buf[30];
   if (alarmListIndex < MAX_ALARMS) {
     sprintf(buf, "Слот %d: %02d:%02d", alarmListIndex+1, alarms[alarmListIndex].hour, alarms[alarmListIndex].minute);
     u8g2.drawStr(10, 30, buf);
     if (alarms[alarmListIndex].enabled) u8g2.drawStr(10, 42, "[BKЛ]");
     else u8g2.drawStr(10, 42, "[ВЫКЛ]");
 
     u8g2.setCursor(10, 54);
     if (alarms[alarmListIndex].days == 0) u8g2.print("Однократно");
     else u8g2.print("По дням");
   } else {
     u8g2.drawStr(20, 35, "[ + ДОБАВИТЬ ]");
   }
   u8g2.drawStr(120, 35, ">");
   u8g2.drawStr(0, 35, "<");
 }
 
 void drawAlarmEdit() {
   u8g2.setFont(u8g2_font_6x12_tr);
   u8g2.drawStr(0, 8, "НАСТРОЙКА БУД.");
   u8g2.drawHLine(0, 10, 128);
 
   char buf[20];
   u8g2.setFont(u8g2_font_logisoso16_tn);
   sprintf(buf, "%02d:%02d", tempAlarm.hour, tempAlarm.minute);
   u8g2.drawStr(40, 35, buf);
 
   u8g2.setDrawColor(1);
   if (currentStep == STEP_HH) u8g2.drawFrame(38, 18, 24, 20);
   if (currentStep == STEP_MM) u8g2.drawFrame(70, 18, 24, 20);
 
   u8g2.setFont(u8g2_font_5x7_tr);
   if (currentStep == STEP_DAYS) {
     u8g2.drawStr(10, 50, "ДНИ: П В С Ч П С В");
     const char* dNames[] = {"П","В","С","Ч","П","С","В"};
     for(int i=0; i<7; i++) {
       int x = 30 + i*12;
       if (i == currentDayEditIndex) u8g2.drawFrame(x-2, 54, 10, 10);
       if (tempAlarm.days & (1<<i)) u8g2.drawBox(x, 56, 6, 6);
       else u8g2.drawStr(x, 62, dNames[i]);
     }
   }
 
   if (currentStep == STEP_SAVE) {
     u8g2.drawButtonUTF8(64, 55, U8G2_BTN_BW1|U8G2_BTN_HCENTER, 0, 1, 1, "СОХРАНИТЬ");
   }
 }
 
 void drawTimeSet() {
   u8g2.setFont(u8g2_font_6x12_tr);
   u8g2.drawStr(0, 8, "УСТАНОВКА ВРЕМЕНИ");
   char buf[20];
   u8g2.setFont(u8g2_font_logisoso16_tn);
   sprintf(buf, "%02d:%02d", tempHour, tempMin);
   u8g2.drawStr(40, 35, buf);
 
   u8g2.setFont(u8g2_font_6x12_tr);
   sprintf(buf, "%02d.%02d.%04d", tempDay, tempMonth, tempYear);
   u8g2.drawStr(35, 55, buf);
 
   int x = (currentStep == 0) ? 40 : 70;
   if(currentStep > 1) x = 35;
   u8g2.drawStr(x, (currentStep <= 1) ? 38 : 58, "^");
 }
 
 // ================= SETUP & LOOP =================
 void setup() {
   Serial.begin(9600);
 
   Timer1.initialize(1000);
   Timer1.attachInterrupt(timerIsr);
   encoder.setAccelerationEnabled(true);
 
   pinMode(BUZZER_PIN, OUTPUT);
   pinMode(LDR_PIN, INPUT);
   pinMode(ENC_A, INPUT_PULLUP);
   pinMode(ENC_B, INPUT_PULLUP);
   pinMode(ENC_BTN, INPUT_PULLUP);
 
   u8g2.begin();
   u8g2.enableUTF8Print();
 
   loadAlarms(); // Загрузка будильников
 
   // Старт с 01.01.2025, если не было синхронизации
   if(timeStatus() == timeNotSet) setTime(12, 0, 0, 1, 1, 2025);
 }
 
 void loop() {
   checkSerialSync();
   checkAlarms();
   buzzerLogic();
 
   // Автояркость
   static unsigned long lastLdr = 0;
   if (millis() - lastLdr > 500) {
     lastLdr = millis();
     int val = analogRead(LDR_PIN);
     u8g2.setContrast(map(val, 0, 1023, 10, 255));
   }
 
   int16_t val = encoder.getValue();
   ClickEncoder::Button b = encoder.getButton();
 
   switch (currentState) {
     case S_HOME:
       if (val != 0) {
         if (!inMainMenu) {
           inMainMenu = true; // первое вращение — открыть меню
         } else {
           menuSelection = (menuSelection + val + 2) % 2; // 0↔1
         }
       }
       if (b == ClickEncoder::Clicked) {
         if (inMainMenu) {
           // выбор пункта
           if (menuSelection == 0) {
             currentState = S_ALARM_LIST;
             alarmListIndex = 0;
           } else {
             currentState = S_TIME_SET;
             tempHour = hour(); tempMin = minute();
             tempDay = day(); tempMonth = month(); tempYear = year();
             currentStep = (EditStep)0;
           }
           inMainMenu = false; // выходим из меню после выбора
         } else {
           inMainMenu = true; // короткое нажатие просто открывает меню
         }
       }
       // долгое удержание закрывает меню
       if (b == ClickEncoder::Held) inMainMenu = false;
       break;
 
     case S_ALARM_LIST:
       if (val != 0) alarmListIndex = constrain(alarmListIndex + val, 0, MAX_ALARMS);
       if (b == ClickEncoder::Clicked) {
         if (alarmListIndex == MAX_ALARMS) { // Новый
           editAlarmId = -1;
           for(int i=0; i<MAX_ALARMS; i++) if(!alarms[i].enabled) { editAlarmId = i; break; }
           if(editAlarmId == -1) editAlarmId = 0;
           tempAlarm = {true, 7, 0, 0, false};
         } else {
           editAlarmId = alarmListIndex;
           tempAlarm = alarms[editAlarmId];
         }
         currentState = S_ALARM_EDIT;
         currentStep = STEP_HH;
       }
       if (b == ClickEncoder::Held) { currentState = S_HOME; inMainMenu = false; }
       break;
 
     case S_ALARM_EDIT:
       if (val != 0) {
         if(currentStep == STEP_HH) tempAlarm.hour = (tempAlarm.hour + val + 24) % 24;
         else if(currentStep == STEP_MM) tempAlarm.minute = (tempAlarm.minute + val * 5 + 60) % 60;
         else if(currentStep == STEP_DAYS) currentDayEditIndex = (currentDayEditIndex + val + 7) % 7;
       }
       if (b == ClickEncoder::Clicked) {
         if(currentStep == STEP_HH) currentStep = STEP_MM;
         else if(currentStep == STEP_MM) currentStep = STEP_DAYS;
         else if(currentStep == STEP_DAYS) tempAlarm.days ^= (1 << currentDayEditIndex);
         else if(currentStep == STEP_SAVE) {
           alarms[editAlarmId] = tempAlarm;
           saveAlarms();
           currentState = S_ALARM_LIST;
         }
       }
       if (currentStep == STEP_DAYS && b == ClickEncoder::DoubleClicked) currentStep = STEP_SAVE;
       if (b == ClickEncoder::Held) currentState = S_ALARM_LIST;
       break;
 
     case S_TIME_SET:
       if (val != 0) {
         if(currentStep == 0) tempHour = (tempHour + val + 24) % 24;
         if(currentStep == 1) tempMin = (tempMin + val + 60) % 60;
       }
       if (b == ClickEncoder::Clicked) {
         int s = (int)currentStep; s++; currentStep = (EditStep)s;
         if (s > 1) {
           setTime(tempHour, tempMin, 0, tempDay, tempMonth, tempYear);
           currentState = S_HOME;
         }
       }
       if (b == ClickEncoder::Held) currentState = S_HOME;
       break;
 
     case S_ALARM_RINGING:
       if (b == ClickEncoder::Clicked || b == ClickEncoder::Pressed) stopAlarm();
       break;
   }
 
   u8g2.firstPage();
   do {
     switch (currentState) {
       case S_HOME: drawHome(); break;
       case S_ALARM_LIST: drawAlarmList(); break;
       case S_ALARM_EDIT: drawAlarmEdit(); break;
       case S_TIME_SET: drawTimeSet(); break;
       case S_ALARM_RINGING:
         u8g2.setFont(u8g2_font_logisoso16_tr);
         u8g2.drawStr(10, 40, "!!! WAKE UP !!!");
         break;
     }
   } while (u8g2.nextPage());
 }
 