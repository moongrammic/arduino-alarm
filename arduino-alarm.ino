#include <U8g2lib.h>
#include <Wire.h>

#define BEEPER        DD2
#define ENC_BTN       DD3
#define ENC_S1        DD4
#define ENC_S2        DD5
#define ENC_HOLD_TIME 10000
#define CHAR_WIDTH    12

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
  int16_t timer     = 0;
};

struct MenuState
{
  uint8_t cur_menu = 0;
  uint8_t cur_line = 0;
};

int position         = 0;
int lpp              = 0;
uint16_t upd_timeout = 10000;
int upd_timer        = 0;

bool screen_updated  = false;
bool update_screen   = false;

bool change_time     = false;

long time            = 0;
unsigned long long previousMillis;

struct EncoderInput encoderInput;
struct MenuState menuState;

void handleEncoder(struct EncoderInput& write_to)
{
  *(uint32_t*)(&write_to) = 0; // a little optimization :3
  bool b                  = !digitalRead(ENC_S1);
  bool a                  = !digitalRead(ENC_S2);
  bool inp_btn            = digitalRead(ENC_BTN);
  write_to.keyPressed     = inp_btn && (inp_btn != write_to.last_key);

  if(inp_btn)
  {
    if(write_to.timer == 0)
    {
      if(!write_to.keyPressed)
      {
        write_to.keyHeld = true;
      }
      else
      {
        write_to.timer = ENC_HOLD_TIME;
      }
    }
    else
    {
      write_to.timer--;
    }
  }
  else
  {
    write_to.timer = 0;
  }

  write_to.last_key = inp_btn;

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

class Menu;
class BasicMenuEntry;

class Menu
{
public:
  Menu(const char* name, uint8_t entry_count) : m_name(name), m_entry_count(entry_count)
  {
    m_entries  = (BasicMenuEntry*)malloc(sizeof(BasicMenuEntry*)); // Allocate space for pointers
    m_name_len = strlen(m_name);
  }
  void handleInput(EncoderInput& input, MenuState& state) {}
  const char* m_name        = "null";
  uint8_t m_name_len        = 0;
  BasicMenuEntry* m_entries = nullptr; // We dont know types of these entries, we can later cast
                                       // them to appropriate types by getting their first byte
  uint8_t m_entry_count     = 0;
};

class BasicMenuEntry
{
public:
  BasicMenuEntry() : m_type(0) {}
  uint8_t m_type = 0;
};

class MenuEntryNumber : public BasicMenuEntry
{
public:
  MenuEntryNumber(uint8_t data_count, int defaults[]) : m_data_count(data_count)
  {
    m_data = (int*)malloc(sizeof(int) * data_count);
    for(uint8_t i = 0; i < data_count; i++)
    {
      m_data[i] = defaults[i];
    }
  }
  uint8_t m_data_count = 0;
  int* m_data;
};

constexpr uint8_t menu_count = 2;
Menu* menus                  = (Menu*)malloc(sizeof(Menu*) * menu_count);

void drawHead()
{
  uint16_t x = 0;
  for(uint8_t i = 0; i < menu_count; i++)
  {
    const char* name       = menus[i].m_name;
    const uint8_t name_len = menus[i].m_name_len;
    if(menuState.cur_menu == i)
    {
      u8g2.setDrawColor(255);
      u8g2.drawBox(x, 0, name_len * CHAR_WIDTH - 3, CHAR_WIDTH + 1);
      u8g2.setDrawColor(0);
      u8g2.drawStr(x + 1, CHAR_WIDTH, name);
    }
    else
    {
      u8g2.setDrawColor(0);
      u8g2.drawBox(x, 0, name_len * CHAR_WIDTH - 3, CHAR_WIDTH + 1);
      u8g2.setDrawColor(255);
      u8g2.drawStr(x + 1, CHAR_WIDTH, name);
      Serial.println(name);
    }
    x += name_len * CHAR_WIDTH;
  }
}

void dispUpdate()
{
  u8g2.clearBuffer();
  if(encoderInput.keyHeld)
  {
    drawHead();
  }
  u8g2.sendBuffer();
}

void handleMenus()
{

  if(encoderInput.keyHeld)
  {
    if(encoderInput.turnedRight)
    {
      menuState.cur_menu++;
      menuState.cur_line = 0;
    }
    else if(encoderInput.turnedLeft)
    {
      menuState.cur_menu--;
      menuState.cur_line = 0;
    }
    menuState.cur_menu %= menu_count;
  }

  Menu& curMenu = menus[menuState.cur_menu];

  curMenu.handleInput(encoderInput, menuState);
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

  // Build menus
  int zerofill[]        = { 0, 0, 0, 0, 0, 0, 0, 0, 0 }; // A bit cringe but why the fuck not
  menus[0]              = Menu("MAIN", 0);
  menus[1]              = Menu("ST", 0);
  menus[1].m_entries[0] = MenuEntryNumber(2, zerofill); // Time
  menus[1].m_entries[1] = MenuEntryNumber(3, zerofill); // Date
}

void loop()
{
  unsigned long currentMillis = millis(); // Get the current time

  // Check if the interval has passed since the last count
  if(currentMillis - previousMillis >= 1000)
  {
    previousMillis = currentMillis;
    // time++;
    update_screen  = true;
  }

  if(lpp != position)
  {
    lpp = position;
    Serial.println(position);
  }
  handleEncoder(encoderInput);

  handleMenus();

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
