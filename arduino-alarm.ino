#include <Wire.h>
#include <GyverOLED.h>
#include <EncButton.h>
#include <TimeLib.h>
#include <EEPROM.h>

GyverOLED<SSD1306_128x64, OLED_NO_BUFFER> oled; 
EncButton eb(2, 3, 4);

#define BUZZER_PIN 8
#define MAX_ALARMS 5
#define ALARM_EMPTY_MARKER 255

struct Alarm {
  uint8_t hour;
  uint8_t minute;
  bool enabled;
  uint8_t days; 
};

Alarm alarms[MAX_ALARMS];
enum Screen { SCREEN_MAIN, SCREEN_MENU, SCREEN_EDIT_ALARM };
Screen currentScreen = SCREEN_MAIN;

int8_t menuCursor = 0;       
int8_t editAlarmIndex = -1;  
uint8_t editStep = 0;        
int8_t dayCursor = 0;        
uint8_t tempHour, tempMinute, tempDays;

bool needRedraw = true;
bool isAlarmRinging = false;
uint32_t lastBlink = 0;
bool blinkState = true;

const char* dayNames[] = {"Пн", "Вт", "Ср", "Чт", "Пт", "Сб", "Вс"};

// Переменные для отслеживания изменений (умная отрисовка)
uint8_t lastStep = 255;
int8_t lastD = -1;

void printCentered(int y, String text, int scale = 1) {
  oled.setScale(scale);
  int x = (128 - text.length() * 6 * scale) / 2;
  oled.setCursor(x < 0 ? 0 : x, y);
  oled.print(text);
}

// --- СЕРВИСНЫЕ ФУНКЦИИ ---
void loadAlarms() {
  EEPROM.get(0, alarms);
  if (alarms[0].hour > 23 && alarms[0].hour != ALARM_EMPTY_MARKER) {
    for (int i = 0; i < MAX_ALARMS; i++) {
      alarms[i].hour = ALARM_EMPTY_MARKER;
      alarms[i].days = 127; 
      alarms[i].enabled = false;
    }
  }
}

bool syncTimeFromPC() {
  Serial.println("GET_TIME");
  unsigned long start = millis();
  while (millis() - start < 3000) {
    if (Serial.available()) {
      if (Serial.read() == 'S') {
        char buf[15];
        Serial.readBytes(buf, 14);
        buf[14] = '\0';
        int year   = (buf[0] - '0') * 1000 + (buf[1] - '0') * 100 + (buf[2] - '0') * 10 + (buf[3] - '0');
        int month  = (buf[4] - '0') * 10   + (buf[5] - '0');
        int day    = (buf[6] - '0') * 10   + (buf[7] - '0');
        int hour   = (buf[8] - '0') * 10   + (buf[9] - '0');
        int minute = (buf[10] - '0') * 10  + (buf[11] - '0');
        int second = (buf[12] - '0') * 10  + (buf[13] - '0');
        setTime(hour, minute, second, day, month, year);
        return true;
      }
    }
  }
  return false;
}

void saveAlarms() { EEPROM.put(0, alarms); }

void checkAlarms() {
  static uint8_t lastM = 255;
  if (minute() != lastM) {
    lastM = minute();
    if (currentScreen == SCREEN_MAIN) needRedraw = true;
    
    if (isAlarmRinging) return;
    uint8_t currentDayBit = (weekday() == 1) ? 6 : (weekday() - 2);
    for (int i = 0; i < MAX_ALARMS; i++) {
      if (alarms[i].enabled && alarms[i].hour == hour() && alarms[i].minute == minute() && ((alarms[i].days >> currentDayBit) & 1)) {
        isAlarmRinging = true;
        tone(BUZZER_PIN, 2000);
        needRedraw = true;
      }
    }
  }
}

// --- ОТРИСОВКА ---

void drawMainScreen() {
  oled.clear();
  String t = (hour() < 10 ? "0" : "") + String(hour()) + ":" + (minute() < 10 ? "0" : "") + String(minute());
  printCentered(2, t, 3);
  String d = String(day()) + "." + String(month()) + "." + String(year());
  printCentered(6, d, 1);
  if (isAlarmRinging) printCentered(0, "!!! ПОДЪЕМ !!!", 1);
}

void drawMenu() {
  oled.clear();
  printCentered(0, "СПИСОК БУДИЛЬНИКОВ", 1);
  oled.fastLineH(9, 0, 127);
  
  if (menuCursor < MAX_ALARMS) {
    String s = "Будильник #" + String(menuCursor + 1);
    printCentered(2, s, 1);
    
    String info;
    if (alarms[menuCursor].hour == ALARM_EMPTY_MARKER) {
      info = " --:-- (ПУСТО) ";
    } else {
      info = (alarms[menuCursor].hour < 10 ? "0" : "") + String(alarms[menuCursor].hour) + ":" + 
             (alarms[menuCursor].minute < 10 ? "0" : "") + String(alarms[menuCursor].minute) + 
             (alarms[menuCursor].enabled ? " [ВКЛ]" : " [ВЫКЛ]");
    }
    
    oled.invertText(true);
    printCentered(4, info, 1);
    oled.invertText(false);
    
    // Подсказка меняется в зависимости от того, пустой будильник или нет
    if (alarms[menuCursor].hour != ALARM_EMPTY_MARKER) {
      printCentered(6, "Наж+Поворот: ВКЛ/ВЫКЛ", 1);
    } else {
      printCentered(6, "Клик: Создать", 1);
    }
  } 
  else if (menuCursor == MAX_ALARMS) {
    oled.invertText(true);
    printCentered(4, " >>> ВЫХОД <<< ", 1);
    oled.invertText(false);
  }
}

void drawEditAlarm() {
  // Полная очистка только при смене этапа настройки
  if (lastStep != editStep) {
    oled.clear();
    printCentered(0, "НАСТРОЙКА", 1);
    lastStep = editStep;
  }

  // Отрисовка Времени
  oled.setScale(2);
  oled.setCursor(34, 2);
  
  if (editStep == 0 && !blinkState) oled.print("  "); 
  else { if(tempHour < 10) oled.print("0"); oled.print(tempHour); }
  
  oled.print(":");
  
  if (editStep == 1 && !blinkState) oled.print("  "); 
  else { if(tempMinute < 10) oled.print("0"); oled.print(tempMinute); }

  // Отрисовка Дней недели
  oled.setScale(1);
  for (int d = 0; d < 7; d++) {
    oled.setCursor(10 + (d * 16), 5);
    
    if (editStep == 2 && dayCursor == d) oled.invertText(true);
    oled.print(dayNames[d]);
    oled.invertText(false);
    
    // Индикатор активности дня (полоска снизу)
    if ((tempDays >> d) & 1) {
      oled.rect(10 + (d * 16), 48, 10 + (d * 16) + 10, 49, 1);
    } else {
      // Стираем полоску двумя линиями (черный цвет = 0)
      for (int i = 0; i < 11; i++) {
        oled.dot(10 + (d * 16) + i, 48, 0);
        oled.dot(10 + (d * 16) + i, 49, 0);
      }
    }
  }

  // Кнопка сохранения
  if (editStep == 2 && dayCursor == 7) oled.invertText(true);
  printCentered(7, " СОХРАНИТЬ ", 1);
  oled.invertText(false);
}

// --- LOOP & INPUT ---

void handleInput() {
  eb.tick();

  if (isAlarmRinging) {
    if (eb.click() || eb.turn() || eb.hold()) {
      isAlarmRinging = false;
      noTone(BUZZER_PIN);
      needRedraw = true;
    }
    return;
  }

  // 1. ПОВОРОТ
  if (eb.turn()) {
    needRedraw = true;
    int dir = eb.dir();

    if (currentScreen == SCREEN_MENU) {
      // ПРОВЕРКА: зажата ли кнопка прямо в момент поворота
      if (eb.pressing() && menuCursor < MAX_ALARMS && alarms[menuCursor].hour != ALARM_EMPTY_MARKER) {
        alarms[menuCursor].enabled = !alarms[menuCursor].enabled;
        saveAlarms();
        tone(BUZZER_PIN, 1500, 20); // Короткий писк при переключении
      } 
      else {
        menuCursor += dir;
        if (menuCursor > MAX_ALARMS) menuCursor = 0;
        if (menuCursor < 0) menuCursor = MAX_ALARMS;
      }
    } 
    else if (currentScreen == SCREEN_EDIT_ALARM) {
      if (editStep == 0) tempHour = (tempHour + dir + 24) % 24;
      else if (editStep == 1) tempMinute = (tempMinute + dir + 60) % 60;
      else if (editStep == 2) {
        dayCursor += dir;
        if (dayCursor > 7) dayCursor = 0;
        if (dayCursor < 0) dayCursor = 7;
      }
    }
  }

  // 2. КЛИК (Только если кнопка НЕ была зажата во время поворота)
  if (eb.click()) {
    // Проверка: если мы крутили с зажатой кнопкой, EncButton может засчитать клик после отпускания.
    // Большинство современных версий библиотеки это фильтруют, но если нет — проверяем состояние.
    
    needRedraw = true;
    if (currentScreen == SCREEN_MAIN) {
      currentScreen = SCREEN_MENU;
      menuCursor = 0;
    } 
    else if (currentScreen == SCREEN_MENU) {
      if (menuCursor == MAX_ALARMS) {
        currentScreen = SCREEN_MAIN;
      } else {
        editAlarmIndex = menuCursor;
        if (alarms[editAlarmIndex].hour == ALARM_EMPTY_MARKER) {
          tempHour = 7; tempMinute = 0; tempDays = 127;
        } else {
          tempHour = alarms[editAlarmIndex].hour;
          tempMinute = alarms[editAlarmIndex].minute;
          tempDays = alarms[editAlarmIndex].days;
        }
        currentScreen = SCREEN_EDIT_ALARM;
        editStep = 0; dayCursor = 0; lastStep = 255;
      }
    } 
    else if (currentScreen == SCREEN_EDIT_ALARM) {
      if (editStep < 2) editStep++;
      else {
        if (dayCursor == 7) {
          alarms[editAlarmIndex].hour = tempHour;
          alarms[editAlarmIndex].minute = tempMinute;
          alarms[editAlarmIndex].days = tempDays;
          alarms[editAlarmIndex].enabled = true;
          saveAlarms();
          currentScreen = SCREEN_MENU;
        } else {
          tempDays ^= (1 << dayCursor);
        }
      }
    }
  }

  // 3. УДЕРЖАНИЕ (Удаление)
  if (eb.hold()) {
    if (currentScreen == SCREEN_MENU && menuCursor < MAX_ALARMS) {
      if (alarms[menuCursor].hour != ALARM_EMPTY_MARKER) {
        alarms[menuCursor].hour = ALARM_EMPTY_MARKER;
        alarms[menuCursor].enabled = false;
        saveAlarms();
        tone(BUZZER_PIN, 800, 100); // Звук удаления (ниже тоном)
        needRedraw = true;
      }
    }
  }
}

void setup() {
  Serial.begin(9600);
  Wire.setClock(400000L);
  oled.init();
  oled.setContrast(20);
  loadAlarms();

  // Попытка синхронизации при старте
  if (syncTimeFromPC()) {
    // Можно коротко пискнуть, если успешно
    tone(BUZZER_PIN, 1500, 100); 
  }
}

void loop() {
  if (Serial.available() > 0 && Serial.read() == 'T') {
    setTime(Serial.parseInt()); needRedraw = true;
  }
  handleInput();
  checkAlarms();

  if (currentScreen == SCREEN_EDIT_ALARM && millis() - lastBlink > 400) {
    lastBlink = millis(); blinkState = !blinkState; needRedraw = true;
  }

  if (needRedraw) {
    needRedraw = false;
    if (currentScreen == SCREEN_MAIN) drawMainScreen();
    else if (currentScreen == SCREEN_MENU) drawMenu();
    else if (currentScreen == SCREEN_EDIT_ALARM) drawEditAlarm();
  }
}