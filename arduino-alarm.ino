#include <U8g2lib.h>
#include <Wire.h>

#define BEEPER  DD2
#define ENC_BTN DD3
#define ENC_S1  DD4
#define ENC_S2  DD5

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// Variables are first 4 bytes, so its easy to empty just by casting a pointer to uint32_t and
// setting it to 0, like a free optimization
struct EncoderInput
{
  bool keyHeld      = false;
  bool keyPressed   = false;
  bool turnedLeft   = false;
  bool turnedRight  = false;
  bool last_state_a = false;
  bool last_active  = false;
  bool last_key     = false;
};

int position        = 0;
int lpp             = 0;
int upd_timeout     = 10000;
int upd_timer       = 0;

bool screen_updated = false;
bool update_screen  = false;

bool change_time    = false;

long time           = 0;
unsigned long long previousMillis;

struct EncoderInput encoderInput;

void handleEncoder(struct EncoderInput& write_to)
{
  *(uint32_t*)(&write_to) = 0; // a little optimization :3
  bool b                  = !digitalRead(ENC_S1);
  bool a                  = !digitalRead(ENC_S2);
  write_to.keyHeld        = digitalRead(ENC_BTN);
  write_to.keyPressed     = write_to.keyHeld && (write_to.keyHeld != write_to.last_key);
  write_to.last_key       = write_to.keyHeld;

  if(a || b)
  {
    if(a && b)
    {
      if(!write_to.last_active)
      {
        if(write_to.last_state_a)
        {
          write_to.turnedRight = true;
        }
        else
        {
          write_to.turnedLeft = true;
        }
      }
      write_to.last_active = true;
    }
    write_to.last_state_a = a;
  }
  else
  {
    write_to.last_active = false;
  }
}

#define MENU_ENTRY_UNKNOWN 0x0
#define MENU_ENTRY_NUMBER  0x1

template <typename T> class BasicMenuEntry;

class Menu
{
  BasicMenuEntry<void>* m_entries; // We dont know types of these entries, we can later cast them to
                                   // appropriate types by getting their first byte
  uint8_t entry_count;
};

template <typename T> class BasicMenuEntry
{
  uint8_t type;
  uint8_t data_count;
  T* m_data;
};

class MenuEntryNumber : public BasicMenuEntry<int>
{
};

void dispUpdate()
{
  u8g2.clearBuffer();
  u8g2.drawStr(0, 15, String(position).c_str());
  u8g2.drawStr(32, 15, String(time).c_str());
  u8g2.sendBuffer();
}

void setup()
{
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

void loop()
{
  unsigned long currentMillis = millis(); // Get the current time

  // Check if the interval has passed since the last count
  if(currentMillis - previousMillis >= 1000)
  {
    previousMillis = currentMillis;
    time++;
    update_screen = true;
  }

  if(lpp != position)
  {
    lpp = position;
    Serial.println(position);
  }
  handleEncoder(encoderInput);

  if(encoderInput.keyPressed)
  {
    change_time = !change_time;
  }

  if(change_time)
  {
    if(encoderInput.turnedRight)
    {
      time++;
    }
    else if(encoderInput.turnedLeft)
    {
      time--;
    }
  }

  if(update_screen)
  {
    upd_timer      = upd_timeout;
    screen_updated = false;
    update_screen  = 0;
  }

  if(upd_timer <= 0)
  {
    if(!screen_updated)
    {
      dispUpdate();
      screen_updated = true;
    }
  }
  else
  {
    upd_timer--;
  }
}
