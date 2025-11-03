#include <U8g2lib.h>
#include <Wire.h>

#define BEEPER DD2
#define ENC_BTN DD3
#define ENC_S1 DD4
#define ENC_S2 DD5

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);
  Serial.println("Start test!");

  pinMode(ENC_S1, INPUT);
  pinMode(ENC_S2, INPUT);
  pinMode(ENC_BTN, INPUT);
  pinMode(BEEPER, OUTPUT);

  u8g2.begin();
  u8g2.setFont(u8g2_font_ncenB10_tr);
}

int position = 0;
int lpp = 0;
int upd_timeout = 10000;
int upd_timer = 0;

bool screen_updated = false;
bool update_screen = false;

bool last_active = false;
bool last_state_a = false;
bool last_key = false;

bool change_time = false;

long time = 0;
unsigned long long previousMillis;

void loop() {
  unsigned long currentMillis = millis(); // Get the current time

  // Check if the interval has passed since the last count
  if (currentMillis - previousMillis >= 1000) {
    previousMillis = currentMillis;
    time++;
    update_screen = true;
  }

  // put your main code here, to run repeatedly:
  bool inc=false,dec=false;
  bool b = !digitalRead(ENC_S1);
  bool a = !digitalRead(ENC_S2);
  bool cur_key = digitalRead(ENC_BTN);
  bool key_onpress = cur_key!=last_key;
  if (a||b){
      if (a && b) {
        if (!last_active){
          update_screen = true;
          if(last_state_a) {
            inc = true;
          } else {
            dec = true;
          }
        }
        last_active = true;
      }
      last_state_a = a;
  } else {
    last_active = false;
  }
  if(lpp!=position){
    lpp=position;
    Serial.println(position);
  }


  if(key_onpress) {change_time=!change_time;}

  if (change_time) {
    if(inc){
      time++;
    }else if (dec) {
      time--;
    }
  }

  if(update_screen) {
    upd_timer = upd_timeout;
    screen_updated = false;
    update_screen = 0;
  }

  if(upd_timer<=0) {
      if (!screen_updated) {
        dispUpdate();
        screen_updated = true;
      }
  } else {
    upd_timer--;
  }
  last_key = cur_key;
}

void dispUpdate() {
  u8g2.clearBuffer();
  u8g2.drawStr(0, 15, String(position).c_str());
  u8g2.drawStr(32, 15, String(time).c_str());
  u8g2.sendBuffer();
}
