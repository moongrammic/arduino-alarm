#include <Wire.h>
#include <GyverOLED.h>

// --- ИНИЦИАЛИЗАЦИЯ ДИСПЛЕЯ (SSD1306 128x64 I2C) ---
GyverOLED<SSD1306_128x64, OLED_NO_BUFFER> oled;

// --- ПИНЫ ---
#define PIN_ENC_CLK 2
#define PIN_ENC_DT  3
#define PIN_ENC_SW  4
#define PIN_BUZZER  8
#define PIN_LDR     A0

// --- ПЕРЕМЕННЫЕ ВРЕМЕНИ ---
unsigned long lastScreenUpdate = 0; // Переменная для таймера экрана
unsigned long lastTick = 0;   // Для отсчета секунд
unsigned long lastSync = 0;   // Для авто-синхронизации (опционально)
int year, month, day, hour, minute, second;
bool timeSet = false;         // Получили ли мы время от ПК?

// --- СОСТОЯНИЯ ИНТЕРФЕЙСА ---
enum State {
  STATE_CLOCK,         // 0: Главное меню (Часы)
  STATE_ALARMS_LIST,   // 1: Список будильников
  STATE_SETTINGS_TIME, // 2: Настройка даты/времени
  STATE_ADD_ALARM,     // 3: Добавление/Редактирование будильника
  STATE_DELETE_ALARM   // 4: Подтверждение удаления
};

State currentState = STATE_CLOCK;

// --- СТРУКТУРА БУДИЛЬНИКА ---
struct Alarm {
  int hour;
  int minute;
  bool active;
  // Дни недели: Пн, Вт, Ср, Чт, Пт, Сб, Вс
  bool days[7];
};

// --- ПЕРЕМЕННЫЕ ДЛЯ БУДИЛЬНИКОВ ---
const int MAX_ALARMS = 5; // Максимальное количество будильников
Alarm alarms[MAX_ALARMS]; // Массив будильников
int activeAlarmCount = 0; // Сколько будильников активно
int currentFocus = 0;     // На каком элементе (будильнике) мы сейчас в списке
int editingAlarmIndex = -1; // Индекс редактируемого будильника (-1 = новый)

// --- СОСТОЯНИЕ БУДИЛЬНИКА ---
bool isAlarmRinging = false;
unsigned long buzzerTimer = 0;
bool buzzerState = false;

// --- ЭНКОДЕР ---
volatile int encoderPos = 0; // Обновляется прерыванием
int lastEncoderPos = 0;      // Предыдущее значение
unsigned long lastButtonPress = 0; // Антидребезг кнопки
unsigned long buttonPressStart = 0; // Для определения длинного нажатия
bool buttonPressed = false;

// --- ПЕРЕМЕННЫЕ ДЛЯ РЕДАКТИРОВАНИЯ БУДИЛЬНИКА ---
int editFocus = 0; // 0=активность, 1=час, 2=минута, 3-9=дни недели, 10=сохранить
Alarm alarmToEdit; // Временный будильник для редактирования

// Убираем меню, рисуем список вручную

// Обработчик прерывания энкодера
void readEncoder() {
  int dtValue = digitalRead(PIN_ENC_DT);
  if (dtValue == HIGH) {
    encoderPos++;
  } else {
    encoderPos--;
  }
}

void setup() {
  Serial.begin(9600);
  
  // Инициализация дисплея GyverOLED
  oled.init();
  oled.clear();
  oled.setScale(1);
  
  Wire.setClock(400000L);
  
  // Настройка пинов
  pinMode(PIN_ENC_CLK, INPUT);
  pinMode(PIN_ENC_DT, INPUT);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LDR, INPUT);
  
  // Прерывание для энкодера (на Mega пин 2 - это INT0)
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_CLK), readEncoder, RISING);

  // Показываем экран подключения
  oled.setCursor(0, 20);
  oled.print("Connecting to PC...");
  oled.update();
  
  // Инициализация будильников
  // Пример: 1 активный будильник на будние дни в 7:30
  alarms[0].hour = 7;
  alarms[0].minute = 30;
  alarms[0].active = true;
  alarms[0].days[0] = true; // Пн
  alarms[0].days[1] = true; // Вт
  alarms[0].days[2] = true; // Ср
  alarms[0].days[3] = true; // Чт
  alarms[0].days[4] = true; // Пт
  alarms[0].days[5] = false; // Сб
  alarms[0].days[6] = false; // Вс
  activeAlarmCount = 1;
  
  // Запрос времени при запуске
  requestTimeSync();
}

void loop() {
  // 1. ЧТЕНИЕ ДАННЫХ ИЗ SERIAL (Синхронизация)
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.startsWith("S")) {
      parseTime(input);
    }
  }

  // 2. ХОД ЧАСОВ
  if (millis() - lastTick >= 1000) {
    lastTick = millis();
    if (timeSet) {
      updateTime(); 
    }
  }

  // 3. ЛОГИКА (Энкодер и Будильник должны работать быстро)
  handleInterface();
  checkAlarm();

  // 4. ОТРИСОВКА (Только раз в 100 мс = 10 кадров в секунду)
  // Это уберет мерцание, так как мы не очищаем экран слишком часто
  if (millis() - lastScreenUpdate > 100) { 
    adjustBrightness(); // Проверяем яркость тоже не чаще раза в 0.1 сек
    drawScreen();
    lastScreenUpdate = millis();
  }
}

// --- ФУНКЦИИ ЛОГИКИ ---

void requestTimeSync() {
  Serial.println("GET_TIME"); // Отправляем команду вашему Python скрипту
  lastSync = millis();
}

void parseTime(String data) {
  // data придет вида: S20250520143005
  // Индексы: S(0) Y(1-4) M(5-6) D(7-8) H(9-10) M(11-12) S(13-14)
  if (data.length() >= 15) {
    year = data.substring(1, 5).toInt();
    month = data.substring(5, 7).toInt();
    day = data.substring(7, 9).toInt();
    hour = data.substring(9, 11).toInt();
    minute = data.substring(11, 13).toInt();
    second = data.substring(13, 15).toInt();
    timeSet = true;
  }
}

void updateTime() {
  second++;
  if (second >= 60) {
    second = 0;
    minute++;
    if (minute >= 60) {
      minute = 0;
      hour++;
      if (hour >= 24) {
        hour = 0;
        // Здесь можно добавить логику смены дня, но без RTC
        // проще синхронизироваться с ПК раз в сутки.
        requestTimeSync(); 
      }
    }
  }
}

void handleInterface() {
  // 1. ОБРАБОТКА НАЖАТИЯ КНОПКИ (с поддержкой длинного нажатия)
  bool buttonState = digitalRead(PIN_ENC_SW) == LOW;
  
  if (buttonState && !buttonPressed) {
    // Начало нажатия
    buttonPressed = true;
    buttonPressStart = millis();
  } else if (!buttonState && buttonPressed) {
    // Кнопка отпущена
    buttonPressed = false;
    unsigned long pressDuration = millis() - buttonPressStart;
    
    if (pressDuration > 50 && pressDuration < 2000) {
      // Короткое нажатие
      if (millis() - lastButtonPress > 300) {
        if (isAlarmRinging) {
          // Если звонит будильник - кнопка выключает его
          isAlarmRinging = false;
          noTone(PIN_BUZZER);
        } else {
          handleButtonPress(); // Отдельная функция для обработки кнопки
        }
        lastButtonPress = millis();
      }
    } else if (pressDuration >= 2000) {
      // Длинное нажатие (>= 2 секунды)
      handleLongButtonPress();
      lastButtonPress = millis();
    }
  } else if (buttonState && buttonPressed && (millis() - buttonPressStart > 2000)) {
    // Длинное нажатие (обработка прямо во время нажатия, если нужно)
    // Пока ничего не делаем, обработаем при отпускании
  }

  // 2. ОБРАБОТКА ВРАЩЕНИЯ
  if (encoderPos != lastEncoderPos) {
    int delta = encoderPos - lastEncoderPos;
    handleRotation(delta); // Отдельная функция для обработки вращения
    lastEncoderPos = encoderPos;
  }
}

// --- ОТДЕЛЬНАЯ ЛОГИКА ДЛЯ КНОПКИ ---
void handleButtonPress() {
  switch (currentState) {
    case STATE_CLOCK:
      // В главном меню нажатие переходит в список будильников
      currentState = STATE_ALARMS_LIST;
      currentFocus = 0; // Сброс фокуса на первый элемент
      break;
      
    case STATE_ALARMS_LIST:
      // Если фокус на "Добавить будильник" (+)
      if (currentFocus == activeAlarmCount) {
        // Инициализация нового будильника
        editingAlarmIndex = -1;
        alarmToEdit.hour = 7;
        alarmToEdit.minute = 0;
        alarmToEdit.active = true;
        for (int i = 0; i < 7; i++) {
          alarmToEdit.days[i] = false;
        }
        editFocus = 0; // Начинаем с активности
        currentState = STATE_ADD_ALARM;
      } else if (currentFocus < activeAlarmCount) {
        // Выбран существующий будильник, идем в режим редактирования
        editingAlarmIndex = currentFocus;
        alarmToEdit = alarms[currentFocus];
        editFocus = 0; // Начинаем с активности
        currentState = STATE_ADD_ALARM;
      }
      break;
      
    case STATE_ADD_ALARM:
      // В режиме добавления - переход к следующему полю
      // 0=Активность, 1=Час, 2=Минута, 3-9=Дни недели (7 дней), 10=Сохранить/Отменить
      if (editFocus < 9) {
        editFocus++;
      } else if (editFocus == 9) {
        // Переход к опции "Сохранить"
        editFocus = 10;
        currentFocus = 0; // 0 = Сохранить, 1 = Отменить
      } else if (editFocus == 10) {
        // На опции "Сохранить/Отменить" - нажатие выполняет действие
        if (currentFocus == 0) {
          // Сохранить будильник
          if (editingAlarmIndex == -1) {
            // Добавляем новый будильник
            if (activeAlarmCount < MAX_ALARMS) {
              alarms[activeAlarmCount] = alarmToEdit;
              activeAlarmCount++;
            }
          } else {
            // Обновляем существующий будильник
            alarms[editingAlarmIndex] = alarmToEdit;
          }
          currentState = STATE_ALARMS_LIST;
          editFocus = 0;
        } else {
          // Отменить редактирование
          currentState = STATE_ALARMS_LIST;
          editFocus = 0;
        }
      }
      break;

    case STATE_DELETE_ALARM:
      // Подтверждение удаления
      if (currentFocus == 0) {
        // Удаляем будильник
        if (editingAlarmIndex >= 0 && editingAlarmIndex < activeAlarmCount) {
          // Сдвигаем все будильники влево
          for (int i = editingAlarmIndex; i < activeAlarmCount - 1; i++) {
            alarms[i] = alarms[i + 1];
          }
          activeAlarmCount--;
        }
      }
      currentState = STATE_ALARMS_LIST;
      currentFocus = 0;
      break;
      
    case STATE_SETTINGS_TIME:
      // В настройках времени - возврат в главное меню
      currentState = STATE_CLOCK;
      break;
  }
  
  // После смены состояния, часто полезно сбросить энкодер, чтобы избежать случайного скролла
  encoderPos = 0; 
  lastEncoderPos = 0;
}

// Обработка длинного нажатия кнопки
void handleLongButtonPress() {
  switch (currentState) {
    case STATE_ALARMS_LIST:
      // Длинное нажатие в списке будильников = удаление выбранного будильника
      if (currentFocus < activeAlarmCount && activeAlarmCount > 0) {
        editingAlarmIndex = currentFocus;
        currentFocus = 0; // По умолчанию выбираем "YES"
        currentState = STATE_DELETE_ALARM;
      }
      break;
      
    case STATE_ADD_ALARM:
      // Длинное нажатие при редактировании = отмена и возврат в список
      currentState = STATE_ALARMS_LIST;
      editFocus = 0;
      break;
      
    default:
      // В других состояниях длинное нажатие ничего не делает
      break;
  }
  
  encoderPos = 0;
  lastEncoderPos = 0;
}

// --- ОТДЕЛЬНАЯ ЛОГИКА ДЛЯ ВРАЩЕНИЯ ---
void handleRotation(int delta) {
  switch (currentState) {
    case STATE_CLOCK:
      // В главном меню можно сразу перейти в Настройку времени
      if (delta > 0) {
        currentState = STATE_SETTINGS_TIME;
      }
      break;

    case STATE_ALARMS_LIST:
      // Скроллинг списка будильников
      currentFocus += delta;
      // Ограничение фокуса: от 0 до (количество будильников + кнопка "Добавить")
      int maxFocus = activeAlarmCount;
      if (activeAlarmCount < MAX_ALARMS) {
        maxFocus = activeAlarmCount; // Включая кнопку "Добавить"
      }
      currentFocus = constrain(currentFocus, 0, maxFocus);
      break;
      
    case STATE_ADD_ALARM:
      // Изменение значения в текущем поле фокуса
      if (editFocus == 0) { // Фокус на Активности (включен/выключен)
        // Переключаем активность будильника
        if (delta != 0) {
          alarmToEdit.active = !alarmToEdit.active;
        }
      } else if (editFocus == 1) { // Фокус на Часах
        alarmToEdit.hour += delta;
        alarmToEdit.hour = constrain(alarmToEdit.hour, 0, 23);
      } else if (editFocus == 2) { // Фокус на Минутах
        alarmToEdit.minute += delta;
        alarmToEdit.minute = constrain(alarmToEdit.minute, 0, 59);
      } else if (editFocus >= 3 && editFocus <= 9) { // Дни недели (3-9)
        int dayIndex = editFocus - 3;
        // Переключаем день только при вращении вперед (delta > 0)
        if (delta > 0) {
          alarmToEdit.days[dayIndex] = !alarmToEdit.days[dayIndex];
        }
      } else if (editFocus == 10) { // Фокус на кнопке "Сохранить/Отменить"
        // Вращение переключает между "Сохранить" (0) и "Отменить" (1)
        currentFocus += delta;
        currentFocus = constrain(currentFocus, 0, 1);
      }
      break;
      
    case STATE_SETTINGS_TIME:
      // Здесь можно добавить настройку времени через вращение
      break;
      
    case STATE_DELETE_ALARM:
      // Выбор: подтвердить или отменить
      currentFocus += delta;
      currentFocus = constrain(currentFocus, 0, 1);
      break;
  }
}

void checkAlarm() {
  // Проверка всех активных будильников (только если время установлено и секунды = 0)
  if (!isAlarmRinging && timeSet && second == 0) {
    // Получаем день недели (упрощенная версия, нужен правильный расчет)
    int dayOfWeek = getDayOfWeek(); // 0 = Пн, 6 = Вс
    
    for (int i = 0; i < activeAlarmCount; i++) {
      if (alarms[i].active && 
          hour == alarms[i].hour && 
          minute == alarms[i].minute &&
          alarms[i].days[dayOfWeek]) {
        isAlarmRinging = true;
        break;
      }
    }
  }

  // Звук будильника (пиканье)
  if (isAlarmRinging) {
    if (millis() - buzzerTimer > 500) { // Каждые 500мс
      buzzerTimer = millis();
      buzzerState = !buzzerState;
      if (buzzerState) tone(PIN_BUZZER, 2000); // 2kHz звук
      else noTone(PIN_BUZZER);
    }
  } else {
    noTone(PIN_BUZZER);
  }
}

// Вспомогательная функция для получения дня недели
// Использует алгоритм Зеллера для расчета дня недели
// Возвращает: 0=Понедельник, 1=Вторник, ..., 6=Воскресенье
int getDayOfWeek() {
  if (!timeSet) return 0; // Если время не установлено, возвращаем понедельник
  
  int y = year;
  int m = month;
  int d = day;
  
  // Алгоритм Зеллера (адаптирован для понедельника = 0)
  if (m < 3) {
    m += 12;
    y--;
  }
  
  int k = y % 100;
  int j = y / 100;
  int h = (d + (13 * (m + 1)) / 5 + k + k / 4 + j / 4 - 2 * j) % 7;
  
  // Преобразуем результат: 0=Суббота, 1=Воскресенье, ..., 6=Пятница
  // Нам нужно: 0=Понедельник, ..., 6=Воскресенье
  int dayOfWeek = (h + 5) % 7;
  return dayOfWeek;
}

void adjustBrightness() {
  int ldrValue = analogRead(PIN_LDR); // 0 (светло) - 1023 (темно) или наоборот, зависит от схемы
  // Для OLED SSD1306 настройка контраста
  // Обычно ldrValue: много света -> нужна высокая яркость
  // Мало света -> низкая яркость
  
  // Предположим схему: 5V -> LDR -> A0 -> 10k -> GND. 
  // Тогда Свет = высокое напряжение (большое значение). Темнота = малое.
  
  int contrast = map(ldrValue, 0, 1023, 0, 255);
  oled.setContrast(contrast);
}

void drawScreen() {
  oled.clear();
  
  switch (currentState) {
    case STATE_CLOCK:
      drawClockScreen();
      break;
      
    case STATE_ALARMS_LIST:
      drawAlarmListScreen();
      break;
      
    case STATE_SETTINGS_TIME:
      drawSettingsScreen();
      break;
      
    case STATE_ADD_ALARM:
      drawAddAlarmScreen();
      break;
      
    case STATE_DELETE_ALARM:
      drawDeleteAlarmScreen();
      break;
  }
  
  oled.update();
}

// Функция отрисовки главного экрана (часы)
void drawClockScreen() {
  oled.home();
  oled.setScale(4);
  if (!timeSet) {
    oled.print("--:--");
  } else {
    if (hour < 10) oled.print("0");
    oled.print(hour);
    oled.print(":");
    if (minute < 10) oled.print("0");
    oled.print(minute);
  }

  // Показываем секунды мелким шрифтом
  oled.setScale(1);
  oled.setCursor(85, 5);
  oled.print(":");
  if (second < 10) oled.print("0");
  oled.print(second);

  // Показываем дату
  oled.setCursor(30, 50);
  if (timeSet) {
    if (day < 10) oled.print("0");
    oled.print(day);
    oled.print(".");
    if (month < 10) oled.print("0");
    oled.print(month);
    oled.print(".");
    oled.print(year);
  } else {
    oled.print("--.--.----");
  }

  // Показываем количество активных будильников
  oled.setCursor(0, 40);
  oled.print("Alarms: ");
  int activeCount = 0;
  for (int i = 0; i < activeAlarmCount; i++) {
    if (alarms[i].active) activeCount++;
  }
  oled.print(activeCount);
  oled.print("/");
  oled.print(activeAlarmCount);

  // Индикация будильника
  if (isAlarmRinging) {
    oled.setCursor(20, 25);
    oled.print("!!! WAKE UP !!!");
  }
}

// Функция отрисовки списка будильников
void drawAlarmListScreen() {
  oled.setScale(1);
  oled.setCursor(0, 0);
  oled.print("ALARMS:");
  
  // Рисуем существующие будильники
  for (int i = 0; i < activeAlarmCount; i++) {
    int y = 10 + i * 10;
    
    // Рисуем рамку, если текущий будильник в фокусе
    if (i == currentFocus) {
      oled.rect(0, y - 1, 128, 10, OLED_STROKE);
    }
    
    // Статус активности
    oled.setCursor(2, y);
    if (alarms[i].active) {
      oled.print("[*]");
    } else {
      oled.print("[ ]");
    }
    
    // Время будильника
    oled.setCursor(22, y);
    if (alarms[i].hour < 10) oled.print("0");
    oled.print(alarms[i].hour);
    oled.print(":");
    if (alarms[i].minute < 10) oled.print("0");
    oled.print(alarms[i].minute);
    
    // Дни недели (краткая форма)
    oled.setCursor(65, y);
    const char dayChars[] = "MTWTFSS";
    for (int d = 0; d < 7; d++) {
      if (alarms[i].days[d]) {
        oled.print(dayChars[d]);
      } else {
        oled.print(" ");
      }
    }
  }
  
  // Кнопка "Добавить будильник"
  int addY = 10 + activeAlarmCount * 10;
  if (activeAlarmCount < MAX_ALARMS) {
    if (currentFocus == activeAlarmCount) {
      oled.rect(0, addY - 1, 128, 11, OLED_STROKE);
    }
    oled.setCursor(2, addY);
    oled.print("[+] Add Alarm");
  }
}

// Функция отрисовки экрана добавления/редактирования будильника
void drawAddAlarmScreen() {
  oled.setScale(1);
  oled.setCursor(0, 0);
  if (editingAlarmIndex == -1) {
    oled.print("NEW ALARM:");
  } else {
    oled.print("EDIT ALARM:");
  }
  
  if (editFocus == 0) {
    // Активность
    oled.setCursor(0, 15);
    oled.print(">Active: ");
    if (alarmToEdit.active) {
      oled.print("ON ");
    } else {
      oled.print("OFF");
    }
  } else if (editFocus == 1 || editFocus == 2) {
    // Редактирование времени с рамкой фокуса
    oled.setScale(4);
    
    // Часы
    int hourX = 10;
    int hourY = 10;
    oled.setCursor(hourX, hourY);
    if (alarmToEdit.hour < 10) oled.print("0");
    oled.print(alarmToEdit.hour);
    
    // Разделитель
    oled.setCursor(55, hourY);
    oled.print(":");
    
    // Минуты
    int minX = 65;
    int minY = 10;
    oled.setCursor(minX, minY);
    if (alarmToEdit.minute < 10) oled.print("0");
    oled.print(alarmToEdit.minute);
    
    // Рамка фокуса вокруг активного поля
    if (editFocus == 1) {
      // Рамка вокруг часов (x, y, width, height)
      oled.rect(8, 8, 47, 37, OLED_STROKE);
    } else if (editFocus == 2) {
      // Рамка вокруг минут (x, y, width, height)
      oled.rect(63, 8, 47, 37, OLED_STROKE);
    }
    
    // Дни недели внизу
    oled.setScale(1);
    oled.setCursor(0, 55);
    const char dayChars[] = "MTWTFSS";
    for (int i = 0; i < 7; i++) {
      if (alarmToEdit.days[i]) {
        oled.print(dayChars[i]);
      } else {
        oled.print("-");
      }
      oled.print(" ");
    }
  } else if (editFocus >= 3 && editFocus <= 9) {
    // Показываем дни недели с выделением
    oled.setScale(1);
    oled.setCursor(0, 15);
    const char* dayNames[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    for (int i = 0; i < 7; i++) {
      int y = 15 + (i % 4) * 12;
      int x = (i < 4) ? 0 : 64;
      
      oled.setCursor(x, y);
      if (editFocus == i + 3) {
        oled.print(">");
      } else {
        oled.print(" ");
      }
      oled.print(dayNames[i]);
      oled.print(": ");
      if (alarmToEdit.days[i]) {
        oled.print("ON");
      } else {
        oled.print("OFF");
      }
    }
  } else if (editFocus == 10) {
    // Экран подтверждения сохранения
    oled.setScale(1);
    oled.setCursor(0, 20);
    oled.print("Save changes?");
    
    oled.setCursor(0, 40);
    if (currentFocus == 0) oled.print(">");
    else oled.print(" ");
    oled.print("YES - Save");
    
    oled.setCursor(0, 50);
    if (currentFocus == 1) oled.print(">");
    else oled.print(" ");
    oled.print("NO - Cancel");
  }
  
  // Подсказка (только если не на экране сохранения и не редактируем время)
  if (editFocus < 10 && editFocus != 1 && editFocus != 2) {
    oled.setScale(1);
    oled.setCursor(0, 55);
    oled.print("Rotate  Btn=Next  Hold=Cancel");
  }
}

// Функция отрисовки экрана настроек времени
void drawSettingsScreen() {
  oled.setScale(1);
  oled.setCursor(0, 0);
  oled.print("TIME SETTINGS:");
  
  oled.setCursor(0, 15);
  oled.print("Sync from PC only");
  
  oled.setCursor(0, 25);
  oled.print("Current time:");
  
  oled.setScale(2);
  oled.setCursor(20, 35);
  if (timeSet) {
    if (hour < 10) oled.print("0");
    oled.print(hour);
    oled.print(":");
    if (minute < 10) oled.print("0");
    oled.print(minute);
  } else {
    oled.print("--:--");
  }
  
  oled.setScale(1);
  oled.setCursor(0, 55);
  oled.print("Press Btn to return");
}

// Функция отрисовки экрана подтверждения удаления
void drawDeleteAlarmScreen() {
  oled.setScale(1);
  oled.setCursor(0, 0);
  oled.print("DELETE ALARM?");
  
  if (editingAlarmIndex >= 0 && editingAlarmIndex < activeAlarmCount) {
    oled.setCursor(0, 15);
    if (alarms[editingAlarmIndex].hour < 10) oled.print("0");
    oled.print(alarms[editingAlarmIndex].hour);
    oled.print(":");
    if (alarms[editingAlarmIndex].minute < 10) oled.print("0");
    oled.print(alarms[editingAlarmIndex].minute);
  }
  
  oled.setCursor(0, 35);
  if (currentFocus == 0) oled.print(">");
  else oled.print(" ");
  oled.print("YES - Delete");
  
  oled.setCursor(0, 45);
  if (currentFocus == 1) oled.print(">");
  else oled.print(" ");
  oled.print("NO - Cancel");
}