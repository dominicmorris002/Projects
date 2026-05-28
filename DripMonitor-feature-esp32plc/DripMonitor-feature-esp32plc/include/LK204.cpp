// LK204.cpp
#include "LK204.hpp"

// default I2C address is 0x28 despite manuals documenting 0x50 or 0x80
LK204::LK204(const struct i2c_dt_spec lcdDev) {
  _lcdDev = lcdDev;
}

// **************************************************************
// base commands
// **************************************************************
inline void LK204::lcdCmd(uint8_t cmd, uint8_t paramCnt, uint8_t params[]) {
  uint8_t bufsize = paramCnt + 2;
  uint8_t buf[bufsize];

  buf[0] = LCD_CMDPREFIX;
  buf[1] = cmd;
  for (uint8_t i = 0; i < paramCnt; i++) {
    buf[i+2] = params[i];
  }

  i2c_write_dt(&_lcdDev, buf, bufsize);
}

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(pkg_display, LOG_LEVEL_DBG);

void LK204::init(uint8_t cols, uint8_t rows) {
  _cols = cols;
  _rows = rows;

  // set display transmission protocol to i2c (0)
  // does not seem to have an effect but left in as it doesn't break anything
  uint8_t params[1] = {0};
  lcdCmd(LCD_TXPROTOCOL, 1, params);

  // manual states for I2C the display should have autotransmit off, this doesn't
  // seem to make any difference. This command is left in as it doesn't break anything
  lcdCmd(LCD_AUTOTRANSMITOFF);
}

// **************************************************************
// text commands
// **************************************************************
// clear the screen
void LK204::clear() {
  lcdCmd(LCD_CLEARSCREEN);
}

void LK204::setStartupScreen(const std::string &text) {
  uint8_t charArray[80];
  uint8_t i = 0;

  for (char c : text){
    charArray[i] = c;
    i++;
  }

  // fill with spaces otherwise previous characters remain
  for (; i < 80; i++) {
    charArray[i] = 0x20;
  }

  lcdCmd(LCD_CHANGESTARTUPSCREEN, 80, charArray);
}

// set cursor to the top left position
void LK204::home() {
  lcdCmd(LCD_SETCURSORHOME);
}

// set cursor to a specific position, top left is (0,0)
void LK204::setCursor(uint8_t col, uint8_t row) {
  if (col >= 0 && col <= _cols-1 && row >= 0 && row <= _rows-1) {
    uint8_t loc[2] = {uint8_t(col+1), uint8_t(row+1)};
    lcdCmd(LCD_SETCURSORPOS, 2, loc);
  }
}

// display contents will shift up one line when end of screen is reached
// default is ON
void LK204::autoscroll() {
  lcdCmd(LCD_AUTOSCROLLON);
}

// new text overwrites top line when end of screen is reached
void LK204::noAutoscroll() {
  lcdCmd(LCD_AUTOSCROLLOFF);
}

// text will wrap to the next consecutive line once a row becomes full
// default is ON
void LK204::linewrap() {
  lcdCmd(LCD_AUTOLINEWRAPON);
}

// text will skip one line when a row becomes full
// writing order is row 1, 3, 2, 4
void LK204::noLinewrap() {
  lcdCmd(LCD_AUTOLINEWRAPOFF);
}

// move cursor one position to the left
// obeys line wrap setting
void LK204::moveCursorBack() {
  lcdCmd(LCD_MOVECURSORBACK);
}

// move cursor one position to the right
// obeys line wrap setting
void LK204::moveCursorFwd() {
  lcdCmd(LCD_MOVECURSORFWD);
}

// displays an underline at current position
// can be used with blinking block
void LK204::cursor() {
  lcdCmd(LCD_UNDERLINECURSORON);
}

// removes underline at current position
void LK204::noCursor() {
  lcdCmd(LCD_UNDERLINECURSOROFF);
}

// displays a blinking block over current cursor position
// can be used with underline cursor
void LK204::blink() {
  lcdCmd(LCD_UNDERLINECURSORON);
}

// removes blinking block over current cursor position
void LK204::noBlink() {
  lcdCmd(LCD_UNDERLINECURSOROFF);
}

// writes text to the display
void LK204::write(const std::string &text) {
  uint8_t textLen = text.length();
  uint8_t charArray[textLen];
  uint8_t i = 0;
  uint8_t retries = 10;
  int ret;

  for (char c : text){
    charArray[i] = c;
    i++;
  }

  while (retries--){
    ret = i2c_write_dt(&_lcdDev, charArray, textLen);
    if (ret >= 0) {
        return;
    }
    k_sleep(K_MSEC(25));
  }
  
}

// writes single character to the display
void LK204::write(const uint8_t &chara) {
  uint8_t retries = 10;
  int ret;
  while (retries--){
    ret = i2c_write_dt(&_lcdDev, &chara, 1);
    if (ret >= 0) {
        return;
    }
    k_sleep(K_MSEC(25));
  }
}


// **************************************************************
// GPO / LED commands
// **************************************************************
void LK204::setLED(uint8_t led, ledColor color) {
  // LED colors are controlled by combination of two GPOs
  uint8_t gpo1[1] = {uint8_t(2 * led - 1)};
  uint8_t gpo2[1] = {uint8_t(2 * led)};

  // set GPOs
  lcdCmd((color & 1) ? LCD_GPOON : LCD_GPOOFF, 1, gpo1);
  lcdCmd((color & 2) ? LCD_GPOON : LCD_GPOOFF, 1, gpo2);

}

void LK204::setLEDStartup(uint8_t led, ledColor color) {
  // LED colors are controlled by combination of two GPOs
  uint8_t gpo1[2] = {uint8_t(2 * led - 1), uint8_t(color & 1)};
  uint8_t gpo2[2] = {uint8_t(2 * led),     uint8_t(color & 2)};

  // set first GPO
  lcdCmd(LCD_GPOSTARTSTATE, 2, gpo1);

  // set second GPO
  lcdCmd(LCD_GPOSTARTSTATE, 2, gpo2);
}


// **************************************************************
// Keypad commands
// **************************************************************
enum LK204::btns LK204::readButtons() {
  uint8_t button = 0;
  
  // button reads are possible without this command but sometimes bit-shift right without it
  lcdCmd(LCD_POLLKEYPRESS);

  i2c_read_dt(&_lcdDev, &button, 1);

  // clear MSB as it indicates if there are more key pressed queued (1) or empty (0)
  button &= ~(1UL << 7);

  return static_cast<btns>(button);
}

// display stores up to 10 key presses
void LK204::clearKeyBuffer() {
  lcdCmd(LCD_CLEARKEYBUFFER);
}

// set button debouce timer in increments of 6.554ms
// default value is 8, or approx. 52ms
void LK204::setKeyDebounceTime(uint8_t time) {
  // convert to array to pass
  uint8_t params[1] = {time};

  lcdCmd(LCD_SETKEYDEBOUNCE, 1, params);
}

// set button auto repeat mode
// hold mode transmits once while pressed
// typematic transmits immediately and then 5 times/sec after a 1 sec delay while held
// default is typematic
// manual has a seperate command to turn auto repeat mode off, but doesn't appear to be necessary
void LK204::setAutoRepeatMode(repeatMode mode) {
  // convert to array to pass
  uint8_t params[1] = {mode};

  lcdCmd(LCD_SETAUTOREPEATMODE, 1, params);
}

void LK204::keypadBacklightOff() {
  lcdCmd(LCD_KEYBACKLIGHTOFF);
}

// set keypad brightness
// default is 255
void LK204::setKeypadBacklightLevel(uint8_t level) {
  // convert to array to pass
  uint8_t params[1] = {level};

  lcdCmd(LCD_SETKEYBRIGHTNESS, 1, params);
}

// auto keypad backlight setting
// option to transmit or omit first keypress
// and options for no change to backlight, keypad and/or display
void LK204::setAutoBacklight(backlightMode mode) {
  // convert to array to pass
  uint8_t params[1] = {mode};

  lcdCmd(LCD_SETAUTOBACKLIGHT, 1, params);
}


// **************************************************************
// Display commands
// **************************************************************

// turn on backlight for length of time in minutes
// value of 0 leaves display on indefinitely
// if an inverse display is used this essentially turns on the text
void LK204::displayBacklightOnTime(uint8_t time) {
  // convert to array to pass
  uint8_t params[1] = {time};

  lcdCmd(LCD_BACKLIGHTON, 1, params);
}

// turn off backlight
void LK204::displayBacklightOff() {
  lcdCmd(LCD_BACKLIGHTOFF);
}

// set backlight level
// optionally permantly save level
void LK204::setDisplayBacklightLevel(uint8_t level, bool save) {
  // convert to array to pass
  uint8_t params[1] = {level};

  if (save) {
    lcdCmd(LCD_SETSAVEBRIGHTNESS, 1, params);
  } else {
    lcdCmd(LCD_SETBRIGHTNESS, 1, params);
  }
}

// set backlight color
// provide values for red, green, and blue 0-255
// default is white (255,255,255)
void LK204::setDisplayBacklightColor(uint8_t red, uint8_t green, uint8_t blue) {
  // convert to array to pass
  uint8_t params[3] = {red, green, blue};

  lcdCmd(LCD_SETBACKLIGHTCOLOR, 3, params);
}

// set contrast
// for inverse display this seets text brightness
// optionally permantly save level
// default is 128
void LK204::setDisplayContrast(uint8_t level, bool save) {
  // convert to array to pass
  uint8_t params[1] = {level};

  if (save) {
    lcdCmd(LCD_SETSAVECONTRAST, 1, params);
  } else {
    lcdCmd(LCD_SETCONTRAST, 1, params);
  }
}