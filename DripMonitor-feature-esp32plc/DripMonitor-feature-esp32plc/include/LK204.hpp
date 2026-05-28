// LK204.h
#ifndef LK204_H
#define LK204_H

#include <string>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>

// command prefix
#define   LCD_CMDPREFIX             0xFE

// command prefix
#define   LCD_TXPROTOCOL            0xA0

// text commands
#define   LCD_CLEARSCREEN           0x58
#define   LCD_CHANGESTARTUPSCREEN   0x40
#define   LCD_AUTOSCROLLON          0x51
#define   LCD_AUTOSCROLLOFF         0x52
#define   LCD_AUTOLINEWRAPON        0x43
#define   LCD_AUTOLINEWRAPOFF       0x44
#define   LCD_SETCURSORPOS          0x47
#define   LCD_SETCURSORHOME         0x48
#define   LCD_MOVECURSORBACK        0x4C
#define   LCD_MOVECURSORFWD         0x4D
#define   LCD_UNDERLINECURSORON     0x4A
#define   LCD_UNDERLINECURSOROFF    0x4B
#define   LCD_BLOCKCURSORON         0x53
#define   LCD_BLOCKCURSOROFF        0x54

// GPO commands
#define   LCD_GPOON                 0x57
#define   LCD_GPOOFF                0x56
#define   LCD_GPOSTARTSTATE         0xC3

// Keypad commands
#define   LCD_AUTOTRANSMITOFF       0x4F // auto transmit always off for I2C
#define   LCD_POLLKEYPRESS          0x26 // not used
#define   LCD_CLEARKEYBUFFER        0x45
#define   LCD_SETKEYDEBOUNCE        0x55
#define   LCD_SETAUTOREPEATMODE     0x7E
#define   LCD_AUTOREPEATMODEOFF     0x60 // not used
#define   LCD_KEYBACKLIGHTOFF       0x9B
#define   LCD_SETKEYBRIGHTNESS      0x9C
#define   LCD_SETAUTOBACKLIGHT      0x9D

// Display commands
#define   LCD_BACKLIGHTON           0x42
#define   LCD_BACKLIGHTOFF          0x46
#define   LCD_SETBRIGHTNESS         0x99
#define   LCD_SETSAVEBRIGHTNESS     0x98
#define   LCD_SETBACKLIGHTCOLOR     0x82
#define   LCD_SETCONTRAST           0x50
#define   LCD_SETSAVECONTRAST       0x91

class LK204 {
public:

  enum ledColor {
    LED_OFF = 0,
    LED_RED,
    LED_GREEN,
    LED_YELLOW
  };

  // key down enumeration
  // manual states key up should generate values as well, does not seem to function this way
  enum btns {
    NONE = 0,

    BTN_TOPSELECT = 65,
    BTN_UP,
    BTN_RIGHT,
    BTN_LEFT,
    BTN_CTRSELECT,

    BTN_BTMSELECT = 71,
    BTN_DOWN,

    // must use HOLD for auto repeate mode to receive releases
    BTN_TOPSELECT_RELEASE = 97,
    BTN_UP_RELEASE,
    BTN_RIGHT_RELEASE,
    BTN_LEFT_RELEASE,
    BTN_CTRSELECT_RELEASE,

    BTN_BTMSELECT_RELEASE = 103,
    BTN_DOWN_RELEASE
  };

  enum repeatMode {
    TYPEMATIC = 0,
    HOLD
  };

  enum backlightMode {
    // transmit first keypress options
    TRANSMIT_NOCHANGE = 0,
    TRANSMIT_LIGHT_KEYPAD,
    TRANSMIT_LIGHT_DISPLAY,
    TRANSMIT_LIGHT_BOTH,

    // omit first keypress options
    OMIT_NOCHANGE = 8,
    OMIT_LIGHT_KEYPAD,
    OMIT_LIGHT_DISPLAY,
    OMIT_LIGHT_BOTH,
  };

  LK204(const struct i2c_dt_spec lcdDev);
  void init(uint8_t cols, uint8_t rows);
  void clear();
  void setStartupScreen(const std::string &text);
  void home();
  void setCursor(uint8_t col, uint8_t row);
  void autoscroll();
  void noAutoscroll();
  void linewrap();
  void noLinewrap();
  void moveCursorBack();
  void moveCursorFwd();
  void cursor();
  void noCursor();
  void blink();
  void noBlink();
  void write(const std::string &text);
  void write(const uint8_t &chara);
  void setLED(uint8_t led, ledColor color);
  void setLEDStartup(uint8_t led, ledColor color);
  enum btns readButtons();
  void clearKeyBuffer();
  void setKeyDebounceTime(uint8_t time);
  void setAutoRepeatMode(repeatMode mode);
  void keypadBacklightOff();
  void setKeypadBacklightLevel(uint8_t level);
  void setAutoBacklight(backlightMode mode);
  void displayBacklightOnTime(uint8_t time);
  void displayBacklightOff();
  void setDisplayBacklightLevel(uint8_t level, bool save);
  void setDisplayBacklightColor(uint8_t red, uint8_t green, uint8_t blue);
  void setDisplayContrast(uint8_t level, bool save);

private:
  struct i2c_dt_spec _lcdDev;
  uint8_t _lcdAddr;
  uint8_t _cols;
  uint8_t _rows;
  inline void lcdCmd(uint8_t cmd, uint8_t paramCnt = 0, uint8_t params[] = nullptr);
};

#endif
