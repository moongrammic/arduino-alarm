#include <U8g2lib.h>
#include <Wire.h>

#define BEEPER        DD2
#define ENC_BTN       DD3
#define ENC_S1        DD4
#define ENC_S2        DD5
#define ENC_HOLD_TIME 10000

#define SCR_WIDTH     128
#define SCR_HEIGHT    64
#define CHAR_WIDTH    9
#define CHAR_HEIGHT   12
#define CHAR_MARGIN   2
#define ENTRY_HEIGHT  CHAR_HEIGHT + CHAR_MARGIN * 2

U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);

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
  uint8_t cur_menu = 1;
  uint8_t cur_line = -1;
  bool locked      = false;
};

constexpr uint16_t upd_timeout = 10000;
uint16_t upd_timer             = 0;

bool screen_updated            = false;
bool update_screen             = false;

long time                      = 3660;
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

typedef uint8_t MenuEntryType;
class Menu;
class BasicMenuEntry
{
public:
  BasicMenuEntry() : m_type(0) {}
  void Draw(uint8_t ypos, uint8_t color) {
    // oled.setDrawColor(color);
    // oled.drawBox(0, ypos, 128, ENTRY_HEIGHT);
  };
  MenuEntryType m_type = MENU_ENTRY_UNKNOWN;
};

class MenuEntryNumber : public BasicMenuEntry
{
public:
  MenuEntryNumber(uint8_t data_count, const char* name)
      : BasicMenuEntry(), m_data_count(data_count), m_name(name)
  {
    m_type = 1;
    m_data = (int*)malloc(sizeof(int) * data_count);
    for(uint8_t i = 0; i < data_count; i++)
    {
      m_data[i] = 0;
    }
  }
  void Draw(uint8_t ypos, uint8_t color)
  {
    oled.setDrawColor(color);
    oled.drawBox(0, ypos, 128, ENTRY_HEIGHT);
    oled.setDrawColor(255 - color);
    oled.drawStr(16, ypos + CHAR_MARGIN + CHAR_HEIGHT, m_name);
  };
  uint8_t m_data_count = 0;
  const char* m_name;
  int* m_data;
};

class Menu
{
public:
  Menu(const char* name, uint8_t entry_count) : m_name(name), m_entry_count(entry_count)
  {
    m_entries  = (BasicMenuEntry*)malloc(sizeof(BasicMenuEntry*)
                                         * m_entry_count); // Allocate space for pointers
    m_name_len = strlen(m_name);
  }
  void handleInput(EncoderInput& input, MenuState& state)
  {
    if(!state.locked)
    {
      if(encoderInput.turnedRight)
      {
        menuState.cur_line++;
      }
      else if(encoderInput.turnedLeft)
      {
        menuState.cur_line--;
      }
      if(menuState.cur_line != -1 && menuState.cur_line > m_entry_count)
      {
        menuState.cur_line = -1;
      }
    }
  }
  void drawEntries()
  {

    if(m_entries == nullptr)
    {
      oled.setDrawColor(255);
      oled.drawStr(16, 64, "No entr");
      return;
    }
    for(uint8_t i = 0; i < m_entry_count; i++)
    {
      BasicMenuEntry* entry    = (m_entries + i);
      const MenuEntryType type = entry->m_type;
      char c[2]                = "0";
      c[0] += type;
      oled.setDrawColor(255);
      oled.drawStr(16, 64, c);
      switch(type)
      {
        case MENU_ENTRY_NUMBER:
          ((MenuEntryNumber*)entry)->Draw(i * ENTRY_HEIGHT, 255);
          break;
        default:
          entry->Draw(i * ENTRY_HEIGHT, 255);
      }
    }
  }
  const char* m_name        = "null";
  uint8_t m_name_len        = 0;
  BasicMenuEntry* m_entries = nullptr; // We dont know types of these entries, we can later cast
                                       // them to appropriate types by getting their first byte
  uint8_t m_entry_count     = 0;
};

constexpr uint8_t menu_count = 2;
Menu* menus                  = (Menu*)malloc(sizeof(Menu*) * menu_count);

void drawTextBox(const uint8_t x, const uint8_t y, const uint8_t color, const char* text,
                 const uint8_t text_length)
{
  oled.setDrawColor(255 - color);
  oled.drawBox(x, y, text_length * CHAR_WIDTH + CHAR_MARGIN * 2, CHAR_HEIGHT + CHAR_MARGIN * 2);
  oled.setDrawColor(color);
  oled.drawStr(x + CHAR_MARGIN, y + CHAR_MARGIN + CHAR_HEIGHT, text);
}
void drawHead()
{
  uint16_t x                     = 0;
  constexpr uint16_t text_height = CHAR_HEIGHT + CHAR_MARGIN;
  oled.setDrawColor(0);
  oled.drawBox(0, 0, SCR_WIDTH, text_height + CHAR_MARGIN);

  for(uint8_t i = 0; i < menu_count; i++)
  {
    const char* name       = menus[i].m_name;
    const uint8_t name_len = menus[i].m_name_len;

    drawTextBox(x, 0, menuState.cur_menu == i ? 0 : 255, name, name_len);
    oled.setDrawColor(255);
    oled.drawLine(0, text_height + CHAR_MARGIN, SCR_WIDTH, text_height + CHAR_MARGIN);
    x += (name_len * CHAR_WIDTH) + CHAR_MARGIN * 2;
  }
}

void drawMainMenu(class Menu& cur_menu)
{
  { // Time draw scope
    oled.setFont(u8g2_font_profont29_tr);
    oled.setDrawColor(255);
    oled.drawBox(0, 12, 24 * 4, 32);
    oled.setDrawColor(0);
    constexpr uint32_t secs_day = 86400;
    constexpr uint32_t secs_hrs = 3600;
    constexpr uint32_t secs_min = 60;

    uint32_t hrs                = (time % secs_day / secs_hrs);
    uint32_t mins               = ((time % secs_day) % secs_hrs) / secs_min;

    char to_prt[6]              = "\0\0:\0\0";
    to_prt[1]                   = (hrs % 10) + '0';
    to_prt[0]                   = (hrs / 10 % 10) + '0';
    to_prt[4]                   = (mins % 10) + '0';
    to_prt[3]                   = (mins / 10 % 10) + '0';

    oled.drawStr(6, 34, to_prt);

    oled.setFont(u8g2_font_profont17_mf);
  }
  { // Icons draw scope
  }
}

void drawSetTime(class Menu& cur_menu) { cur_menu.drawEntries(); }

void dispUpdate()
{
  oled.clearBuffer();
  Menu& curMenu = menus[menuState.cur_menu];

  switch(menuState.cur_menu)
  {
    case 0:
      drawMainMenu(curMenu);
      break;
    case 1:
      drawSetTime(curMenu);
      break;
    default:
      menuState.cur_menu = 0;
  }

  if(encoderInput.keyHeld)
  {
    drawHead();
  }

  oled.sendBuffer();
}

void handleMenus()
{
  switch(menuState.cur_menu)
  {
    case 0:
      break;
    case 1:
      break;
    default:
      menuState.cur_menu = 0;
  }

  if(menuState.locked)
  {
  }
  else
  {
    if(encoderInput.keyHeld)
    {
      if(encoderInput.turnedRight)
      {
        menuState.cur_menu++;
        menuState.cur_line = -1;
      }
      else if(encoderInput.turnedLeft)
      {
        menuState.cur_menu--;
        menuState.cur_line = -1;
      }
      menuState.cur_menu %= menu_count;
    }
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

  // Build menus
  // int zerofill[]        = { 0, 0, 0, 0, 0, 0, 0, 0, 0 }; // A bit cringe but why the fuck not
  // menus[0]              = Menu("MAIN", 0);
  menus[1]              = Menu("ST", 1);
  menus[1].m_entries[0] = MenuEntryNumber(2, ""); // MenuEntryNumber(2, "Time"); // Time
  // menus[1].m_entries[1] = MenuEntryNumber(3, zerofill, "Date"); // Date

  oled.begin();
  oled.setFont(u8g2_font_profont17_mf);
}

void loop()
{
  unsigned long currentMillis = millis(); // Get the current time

  // Check if the interval has passed since the last count
  if(currentMillis - previousMillis >= 1000)
  {
    previousMillis = currentMillis;
    time += 30;
    update_screen = true;
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
