#include "./b-movie-hex.h"
#include <U8g2lib.h>
#include <Wire.h>

#define OLED_DATA  4
#define OLED_CLOCK 5
#define OLED_RESET U8X8_PIN_NONE

#define SCR_W      128
#define SCR_H      64

#define BEEPER     2
#define ENC_BTN    3
#define ENC_S1     10
#define ENC_S2     11

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, OLED_CLOCK, OLED_DATA, U8X8_PIN_NONE);

const char* todrawtext = (const char*)b_movie_txt;
//
// auto todrawtext     = "Тест! 123";
auto todrawtext_len    = strlen(todrawtext);

struct DrawLineResult
{
  uint8_t end_x, end_y;
  size_t drawn_text;
};

size_t getUTF8Len(const char* s)
{
  size_t l;
  for(l = 0; *s; s++)
    l += (*s - 0x80U >= 0x40) + (*s >= 0xf0);
  return l;
}

uint16_t utf8_to_unicode(const char* utf8, int* bytes_used)
{
  uint8_t c = utf8[0];
  if(c < 0x80)
  {
    *bytes_used = 1;
    return c;
  }
  if((c & 0xE0) == 0xC0)
  {
    *bytes_used = 2;
    return ((c & 0x1F) << 6) | (utf8[1] & 0x3F);
  }
  if((c & 0xF0) == 0xE0)
  {
    *bytes_used = 3;
    return ((c & 0x0F) << 12) | ((utf8[1] & 0x3F) << 6) | (utf8[2] & 0x3F);
  }
  *bytes_used = 1;
  return '?';
}

DrawLineResult drawTextLine(const char* text, const uint8_t x_start, const uint8_t y_start)
{
  const char* text_start = text;
  uint8_t x = x_start, y = y_start;

  do
  {
    int uni_byte_len = 0;
    x += u8g2.drawGlyph(x, y, utf8_to_unicode(text, &uni_byte_len));
    if(x >= SCR_W - 8)
    {
      y += u8g2.getMaxCharHeight();
      x = x_start;
      if(y > (SCR_H - u8g2.getMaxCharHeight()))
        break;
    }
    text += uni_byte_len;
  } while(*text && *text != '\n');

  return { .end_x = x, .end_y = y, .drawn_text = (size_t)(text - text_start) };
}

class EncoderHandler
{
public:
  EncoderHandler(uint8_t s1, uint8_t s2) : pin_s1(s1), pin_s2(s2) { last_used_s1 = digitalRead(pin_s1); };

  void update()
  {
    bool s1 = digitalRead(pin_s1);
    bool s2 = digitalRead(pin_s2);

    if(s1 == HIGH && last_used_s1 == LOW)
    {
      if(s2 == HIGH)
      {
        Serial.println("++");
        delta++;
      }
      else
      {
        Serial.println("--");
        delta--;
      }
    }

    last_used_s1 = s1;
  }
  int8_t getDelta() { return delta; };
  void clear() { delta = 0; }

private:
  uint8_t pin_s1, pin_s2;
  bool last_used_s1;
  bool last_active;
  int8_t delta = 0;
};

EncoderHandler enc_handler(ENC_S1, ENC_S2);

void setup()
{
  Wire.begin(OLED_DATA, OLED_CLOCK);
  Wire.setClock(400000);

  u8g2.begin();
  u8g2.setFont(u8g2_font_5x8_t_cyrillic);

  Serial.begin(115200);
  while(!Serial)
    ;
  Serial.println("BEGIN");

  pinMode(ENC_S1, INPUT);
  pinMode(ENC_S2, INPUT);
}

long long cur_pos = 0;

int scr_upd_bruh  = 0;

void loop()
{

  enc_handler.update();
  if(enc_handler.getDelta() != 0)
  {
    scr_upd_bruh = 0;
    if(enc_handler.getDelta() > 0)
    {
      if(cur_pos < todrawtext_len)
      {
        cur_pos = (long long)(strchr((const char*)todrawtext + cur_pos, '\n') - (const char*)todrawtext) + 1;
      }
    }
    else if(enc_handler.getDelta() < 0)
    {
      while(cur_pos > 0 && todrawtext[--cur_pos - 1] != '\n')
        ;
    }
  }
  enc_handler.clear();

  if(cur_pos < 0)
  {
    cur_pos = 0;
  }
  else if(cur_pos > todrawtext_len)
  {
    cur_pos = todrawtext_len;
  }

  if(scr_upd_bruh == -100000)
  {

    u8g2.clearBuffer();

    uint8_t y          = u8g2.getMaxCharHeight();
    size_t chars_drawn = cur_pos;
    do
    {
      auto end = drawTextLine((const char*)todrawtext + chars_drawn, 0, y);
      y        = end.end_y + u8g2.getMaxCharHeight();
      chars_drawn += end.drawn_text;
    } while(chars_drawn <= todrawtext_len && y < (SCR_H - u8g2.getMaxCharHeight()));

    char buff[32];
    sprintf(buff, "L %ld/%ld - %ld%%", (long)cur_pos, (long)todrawtext_len, (cur_pos * 100) / todrawtext_len);
    u8g2.drawStr(0, 64, buff);

    u8g2.sendBuffer();
  }
  scr_upd_bruh--;
}
