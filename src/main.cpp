#define LGFX_AUTODETECT // Autodetect board
#define LGFX_USE_V1     // set to use new version of library

// #define LV_CONF_INCLUDE_SIMPLE

#include <LovyanGFX.hpp> // main library
#include <lvgl.h>
#include "lv_conf.h"

static LGFX lcd; // declare display variable

/*** Setup screen resolution for LVGL ***/
static const uint16_t screenWidth = 480;
static const uint16_t screenHeight = 320;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[screenWidth * 10];
static lv_obj_t *kb;
static lv_obj_t *ta;
static lv_obj_t *scr_PREV;
static lv_obj_t *scr_start;
static lv_obj_t *scr_settings;
static lv_obj_t *scr_calibration_zero;
static lv_obj_t *scr_calibration_force;

// Variables for loadcell
#include <Preferences.h>
#include <hx711_zp.h>

#define HX711_dout 4
#define HX711_sck 2
#define MSG_NEW_FORCE_MEASURED 1

HX711 loadcell;
float reading = 0;
Preferences preferences;

/*** Menu Structure ***/
#include "MenuClass.h"
MenuClass menuuu;

/*** Function declaration ***/
void display_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p);
void touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data);
void lv_screen_start(void);
void kb_event_cb(lv_obj_t *keyboard, lv_event_t e);
void kb_create(void);
void ta_event_cb(lv_event_t *e);
void create_screen_start();
void create_screen_settings();
void create_screen_calibration_zero();

void setup(void)
{
  Serial.begin(115200); /* prepare for possible serial debug */

  lcd.init(); // Initialize LovyanGFX
  lv_init();  // Initialize lvgl

  // Setting display to landscape
  if (lcd.width() < lcd.height())
    lcd.setRotation(lcd.getRotation() ^ 1);

  /* LVGL : Setting up buffer to use for display */
  lv_disp_draw_buf_init(&draw_buf, buf, NULL, screenWidth * 10);

  /*** LVGL : Setup & Initialize the display device driver ***/
  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = screenWidth;
  disp_drv.ver_res = screenHeight;
  disp_drv.flush_cb = display_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  /*** LVGL : Setup & Initialize the input device driver ***/
  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = touchpad_read;
  lv_indev_drv_register(&indev_drv);

  /*** Initialize loadcell***/
  loadcell.begin(HX711_dout, HX711_sck);
  // loadcell.set_scale(preferences.getFloat("sfactor", 2.0F));
  // loadcell.set_zeropoint_offset(preferences.getLong("zeropoint", 1));
  loadcell.set_scale(2.0F);
  loadcell.set_zeropoint_offset(1);
  Serial.println("Loadcell initialized");

  create_screen_start();
  create_screen_settings();
  create_screen_calibration_zero();
  lv_scr_load(scr_calibration_zero);
}

void loop()
{
  lv_timer_handler(); /* let the GUI do its work */
  loadcell.read();    // get the current data from the loadcell
  lv_msg_send(MSG_NEW_FORCE_MEASURED, NULL);
  lcd.setCursor(10, screenHeight - 10);
  lcd.printf("Force: %9.2f", loadcell.get_last_reading_zeroed());

  // lcd.getTouch(&x, &y)
}

/*** Display callback to flush the buffer to screen ***/
void display_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);

  lcd.startWrite();
  lcd.setAddrWindow(area->x1, area->y1, w, h);
  lcd.pushColors((uint16_t *)&color_p->full, w * h, true);
  lcd.endWrite();

  lv_disp_flush_ready(disp);
}

/*** Touchpad callback to read the touchpad ***/
void touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data)
{
  uint16_t touchX, touchY;
  bool touched = lcd.getTouch(&touchX, &touchY);

  if (!touched)
  {
    data->state = LV_INDEV_STATE_REL;
  }
  else
  {
    data->state = LV_INDEV_STATE_PR;

    /*Set the coordinates*/
    data->point.x = touchX;
    data->point.y = touchY;

    // Serial.printf("Touch (x,y): (%03d,%03d)\n",touchX,touchY );
  }
}

static void to_scr_start_handler(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_CLICKED)
    lv_scr_load(scr_start);
}

static void to_scr_calibration_zero_handler(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_CLICKED)
    lv_scr_load(scr_calibration_zero);
}

/* Settings button event handler */
static void to_scr_settings_handler(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_CLICKED)
    lv_scr_load(scr_settings);
}

static void createBackButton(lv_obj_t *scr)
{
  // Back Button
  lv_obj_t *btn_up = lv_btn_create(scr);
  lv_obj_add_event_cb(btn_up, to_scr_start_handler, LV_EVENT_ALL, NULL);
  lv_obj_set_size(btn_up, 50, 50);               /*Set its size*/
  lv_obj_align(btn_up, LV_ALIGN_TOP_LEFT, 0, 0); // Center and 0 from the top

  lv_obj_t *label = lv_label_create(btn_up);
  lv_label_set_text(label, "<<");
  lv_obj_center(label);
}

// https://forum.lvgl.io/t/multiple-windows/3850

void label_forceRaw_change_event(lv_event_t *e)
{
  lv_obj_t *label = lv_event_get_target(e);
  Serial.println("Force Update msg");
  lv_label_set_text_fmt(label, "Rohwert: %06.0f", loadcell.get_last_reading());
}

void label_forceRawZero_change_event(lv_event_t *e)
{
  lv_obj_t *label = lv_event_get_target(e);
  Serial.println("Force Update msg");
  lv_label_set_text_fmt(label, "Genullt: %06.0f", loadcell.get_last_reading_zeroed());
}

void label_forceCal_change_event(lv_event_t *e)
{
  lv_obj_t *label = lv_event_get_target(e);
  Serial.println("Force Update msg");
  lv_label_set_text_fmt(label, "Kraft: %06.0f N", loadcell.get_cal_force());
}

void calibrate_zero_event(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_CLICKED)
    loadcell.set_zeropoint_offset_current();
}

void calibrate_force_event(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_CLICKED)
    loadcell.set_scale_current(50);
}

void create_screen_calibration_zero()
{
  scr_calibration_zero = lv_obj_create(NULL);

  lv_obj_set_style_bg_color(scr_calibration_zero, lv_color_black(), LV_STATE_DEFAULT);

  lv_obj_t *label;
  lv_obj_t *btn;

  label = lv_label_create(scr_calibration_zero);
  lv_label_set_text(label, "Kalibrierung");
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 10);

  btn = lv_btn_create(scr_calibration_zero);
  lv_obj_add_event_cb(btn, calibrate_zero_event, LV_EVENT_ALL, NULL);
  lv_obj_set_size(btn, 150, 60);
  lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 70, 40);
  label = lv_label_create(btn);
  lv_label_set_text(label, "Nullpunkt");
  lv_obj_center(label);

  label = lv_label_create(scr_calibration_zero);
  lv_label_set_text(label, "Maschine ohne Kraft,\naber mit allen Anbauteilen\nstehen lassen.");
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, 230, 40);

  btn = lv_btn_create(scr_calibration_zero);
  lv_obj_add_event_cb(btn, calibrate_force_event, LV_EVENT_ALL, NULL);
  lv_obj_set_size(btn, 150, 60);
  lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 70, 120);
  label = lv_label_create(btn);
  lv_label_set_text(label, "Kraft");
  lv_obj_center(label);

  label = lv_label_create(scr_calibration_zero);
  lv_label_set_text(label, "Maschine mit definierter\nKraft belasten.");
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, 230, 130);

  label = lv_label_create(scr_calibration_zero);
  lv_label_set_text(label, "Messwerte:");
  lv_obj_align(label, LV_ALIGN_LEFT_MID, 70, 40);

  label = lv_label_create(scr_calibration_zero);
  lv_label_set_text(label, "Messwert roh");
  lv_obj_align(label, LV_ALIGN_LEFT_MID, 70, 60);
  lv_obj_add_event_cb(label, label_forceRaw_change_event, LV_EVENT_MSG_RECEIVED, NULL);
  lv_msg_subscribe_obj(MSG_NEW_FORCE_MEASURED, label, NULL);

  label = lv_label_create(scr_calibration_zero);
  lv_label_set_text(label, "Messwert zero");
  lv_obj_align(label, LV_ALIGN_LEFT_MID, 70, 80);
  lv_obj_add_event_cb(label, label_forceRawZero_change_event, LV_EVENT_MSG_RECEIVED, NULL);
  lv_msg_subscribe_obj(MSG_NEW_FORCE_MEASURED, label, NULL);

  label = lv_label_create(scr_calibration_zero);
  lv_label_set_text(label, "Messwert kalibriert");
  lv_obj_align(label, LV_ALIGN_LEFT_MID, 70, 100);
  lv_obj_add_event_cb(label, label_forceCal_change_event, LV_EVENT_MSG_RECEIVED, NULL);
  lv_msg_subscribe_obj(MSG_NEW_FORCE_MEASURED, label, NULL);

  createBackButton(scr_calibration_zero);

  // https://docs.lvgl.io/latest/en/html/widgets/textarea.html
  // ta = lv_textarea_create(lv_scr_act());
  // lv_obj_align(ta, LV_ALIGN_TOP_MID, 50, 200);
  // lv_obj_set_size(ta, 200, 80);
  // lv_obj_add_state(ta, LV_STATE_FOCUSED);
  // lv_obj_add_event_cb(ta, ta_event_cb, LV_EVENT_ALL, NULL);
}

void create_screen_settings()
{
  scr_settings = lv_obj_create(NULL);

  lv_obj_set_style_bg_color(scr_settings, lv_color_black(), LV_STATE_DEFAULT);

  lv_obj_t *label;

  // Calibrate Button
  lv_obj_t *btn_up = lv_btn_create(scr_settings);
  lv_obj_add_event_cb(btn_up, to_scr_calibration_zero_handler, LV_EVENT_ALL, NULL);
  lv_obj_set_size(btn_up, 200, 50);              /*Set its size*/
  lv_obj_align(btn_up, LV_ALIGN_TOP_MID, 0, 20); // Center and 0 from the top

  label = lv_label_create(btn_up);
  lv_label_set_text(label, "Kalibrieren");
  lv_obj_center(label);

  createBackButton(scr_settings);
}

void create_screen_start(void)
{
  scr_start = lv_obj_create(NULL);

  lv_obj_set_style_bg_color(scr_start, lv_color_black(), LV_STATE_DEFAULT);

  lv_obj_t *label;

  /*** Create simple label and show version ***/
  label = lv_label_create(scr_start);                // full screen as the parent
  lv_label_set_text(label, "Seilrissmaschine V1.0"); // set label text
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 20);      // Center and 0 from the top

  // Start Button
  lv_obj_t *btn_up = lv_btn_create(scr_start);
  lv_obj_add_event_cb(btn_up, to_scr_start_handler, LV_EVENT_ALL, NULL);
  lv_obj_set_size(btn_up, 200, 200);              /*Set its size*/
  lv_obj_align(btn_up, LV_ALIGN_RIGHT_MID, 0, 0); // Center and 0 from the top
  lv_obj_set_style_bg_color(btn_up, lv_color_hex3(0x5f5), LV_PART_MAIN);

  label = lv_label_create(btn_up);
  lv_label_set_text(label, "Messung");
  lv_obj_center(label);
  lv_obj_set_style_text_color(btn_up, lv_color_black(), LV_PART_MAIN);

  // Settings Button
  lv_obj_t *btn_settings = lv_btn_create(scr_start);
  lv_obj_add_event_cb(btn_settings, to_scr_settings_handler, LV_EVENT_ALL, NULL);
  lv_obj_set_size(btn_settings, 200, 50);               /*Set its size*/
  lv_obj_align(btn_settings, LV_ALIGN_TOP_MID, 0, 200); // Center and 0 from the top

  label = lv_label_create(btn_settings);
  lv_label_set_text(label, "Einstellungen");
  lv_obj_center(label);
}

/*** KEYBOARD ***/

void kb_event_cb(lv_obj_t *keyboard, lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_CANCEL)
  {
    lv_obj_set_height(lv_scr_act(), LV_VER_RES);
    lv_keyboard_set_textarea(kb, NULL);
    lv_obj_del(kb);
    kb = NULL;
  }
  if (code == LV_EVENT_READY)
  {
    ta = lv_keyboard_get_textarea(kb);
    const char *str = lv_textarea_get_text(ta);
    /*Normally do something with data here*/

    /**
     * Restore Cont to original size and delete the KB
     */
    lv_keyboard_set_textarea(kb, NULL);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  }
}

void kb_create(void)
{
  kb = lv_keyboard_create(lv_scr_act());
  lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);
}

void ta_event_cb(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  ta = lv_event_get_target(e);
  if (code == LV_EVENT_FOCUSED)
  {
    kb_create();
    lv_keyboard_set_textarea(kb, ta);
    lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
  }

  if (code == LV_EVENT_DEFOCUSED)
  {
    lv_keyboard_set_textarea(kb, NULL);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  }
}
