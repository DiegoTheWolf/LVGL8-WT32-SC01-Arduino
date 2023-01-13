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
static lv_obj_t *scr_calibration;
static lv_obj_t *scr_measurement;
static lv_obj_t *scr_measurement_live;
static lv_obj_t *scr_measurement_end;

// Variables for loadcell
#include <Preferences.h>
#include <hx711_zp.h>

#define HX711_dout 4
#define HX711_sck 2
#define MSG_NEW_FORCE_MEASURED 1

HX711 loadcell;
float cal_value = 1;
Preferences preferences; // https://randomnerdtutorials.com/esp32-save-data-permanently-preferences/
#define PREF_SCALE "scale"
#define PREF_ZERO "zero"

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
void create_screen_calibration();
void create_screen_measurement();
void create_screen_measurement_live();
void create_screen_measurement_end();

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
  cal_value = loadcell.get_scale();

  /*** Preferences ***/
  preferences.begin("srm-app", false);
  loadcell.set_scale(preferences.getFloat(PREF_SCALE, 1.0F));
  loadcell.set_zeropoint_offset(preferences.getFloat(PREF_ZERO, 0));

  /*** Screens***/
  create_screen_start();
  create_screen_settings();
  create_screen_calibration();
  lv_scr_load(scr_start);
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

#pragma region screen base functions

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

#pragma endregion

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
    lv_scr_load(scr_calibration);
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
  lv_obj_set_size(btn_up, 50, 40);               /*Set its size*/
  lv_obj_align(btn_up, LV_ALIGN_TOP_LEFT, 0, 0); // Center and 0 from the top

  lv_obj_t *label = lv_label_create(btn_up);
  lv_label_set_text(label, "<<");
  lv_obj_center(label);
}

#pragma region Calibration

void label_forceRaw_change_event(lv_event_t *e)
{
  lv_obj_t *label = lv_event_get_target(e);
  lv_label_set_text_fmt(label, "Rohwert: %06.0f", loadcell.get_last_reading());
}

void label_forceRawZero_change_event(lv_event_t *e)
{
  lv_obj_t *label = lv_event_get_target(e);
  lv_label_set_text_fmt(label, "Genullt: %06.0f", loadcell.get_last_reading_zeroed());
}

void label_forceCal_change_event(lv_event_t *e)
{
  lv_obj_t *label = lv_event_get_target(e);
  lv_label_set_text_fmt(label, "Kraft: %06.0f N", loadcell.get_cal_force());
}

void calibrate_zero_event(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  if (code != LV_EVENT_CLICKED)
    return;
  loadcell.set_zeropoint_offset_current();
  preferences.putFloat(PREF_ZERO, loadcell.get_zeropoint_offset());
}

void calibrate_force_event(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  if (code != LV_EVENT_CLICKED)
    return;
  loadcell.set_scale_current(cal_value);
  preferences.putFloat(PREF_SCALE, loadcell.get_scale());
}

void calValue_changed_event(lv_event_t *e)
{
  lv_obj_t *ta_ = lv_event_get_target(e);
  const char *txt = lv_textarea_get_text(ta_);
  cal_value = static_cast<float>(*txt);
}

void create_screen_calibration()
{
  scr_calibration = lv_obj_create(NULL);

  lv_obj_set_style_bg_color(scr_calibration, lv_color_black(), LV_STATE_DEFAULT);

  lv_obj_t *label;
  lv_obj_t *btn;

  label = lv_label_create(scr_calibration);
  lv_label_set_text(label, "Kalibrierung");
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 10);

  btn = lv_btn_create(scr_calibration);
  lv_obj_add_event_cb(btn, calibrate_zero_event, LV_EVENT_ALL, NULL);
  lv_obj_set_size(btn, 150, 60);
  lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 70, 40);
  label = lv_label_create(btn);
  lv_label_set_text(label, "Nullpunkt");
  lv_obj_center(label);

  label = lv_label_create(scr_calibration);
  lv_label_set_text(label, "Maschine ohne Kraft,\naber mit allen Anbauteilen\nstehen lassen.");
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, 230, 40);

  btn = lv_btn_create(scr_calibration);
  lv_obj_add_event_cb(btn, calibrate_force_event, LV_EVENT_ALL, NULL);
  lv_obj_set_size(btn, 100, 60);
  lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 70, 120);
  label = lv_label_create(btn);
  lv_label_set_text(label, "Kraft");
  lv_obj_center(label);

  label = lv_label_create(scr_calibration);
  lv_label_set_text(label, "Maschine mit\nKraft belasten.");
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, 170, 130);

  lv_obj_t *ta = lv_textarea_create(scr_calibration);
  lv_obj_align(ta, LV_ALIGN_TOP_LEFT, 300, 130);
  lv_obj_set_size(ta, 60, 60);
  lv_textarea_set_one_line(ta, true);
  lv_textarea_set_accepted_chars(ta, "0123456789.");
  char result[8];
  dtostrf(loadcell.get_scale(), 6, 2, result);
  lv_textarea_set_text(ta, result);
  // lv_obj_add_state(ta, LV_STATE_DEFAULT);
  lv_obj_add_event_cb(ta, ta_event_cb, LV_EVENT_ALL, NULL);
  lv_obj_add_event_cb(ta, calValue_changed_event, LV_EVENT_VALUE_CHANGED, NULL);

  label = lv_label_create(scr_calibration);
  lv_label_set_text(label, "Messwerte:");
  lv_obj_align(label, LV_ALIGN_LEFT_MID, 70, 40);

  label = lv_label_create(scr_calibration);
  lv_label_set_text(label, "Messwert roh");
  lv_obj_align(label, LV_ALIGN_LEFT_MID, 70, 60);
  lv_obj_add_event_cb(label, label_forceRaw_change_event, LV_EVENT_MSG_RECEIVED, NULL);
  lv_msg_subscribe_obj(MSG_NEW_FORCE_MEASURED, label, NULL);

  label = lv_label_create(scr_calibration);
  lv_label_set_text(label, "Messwert zero");
  lv_obj_align(label, LV_ALIGN_LEFT_MID, 70, 80);
  lv_obj_add_event_cb(label, label_forceRawZero_change_event, LV_EVENT_MSG_RECEIVED, NULL);
  lv_msg_subscribe_obj(MSG_NEW_FORCE_MEASURED, label, NULL);

  label = lv_label_create(scr_calibration);
  lv_label_set_text(label, "Messwert kalibriert");
  lv_obj_align(label, LV_ALIGN_LEFT_MID, 70, 100);
  lv_obj_add_event_cb(label, label_forceCal_change_event, LV_EVENT_MSG_RECEIVED, NULL);
  lv_msg_subscribe_obj(MSG_NEW_FORCE_MEASURED, label, NULL);

  createBackButton(scr_calibration);

  // https://docs.lvgl.io/latest/en/html/widgets/textarea.html
}

#pragma endregion

#pragma region Measurement

void start_measurement_event(lv_event_t *e)
{
  // TODO:
}

void goto_startposition_event(lv_event_t *e)
{
  // TODO:
}

void maxForce_changed_event(lv_event_t *e)
{
  lv_obj_t *ta_ = lv_event_get_target(e);
  const char *txt = lv_textarea_get_text(ta_);
  cal_value = static_cast<float>(*txt);
}

void minForce_changed_event(lv_event_t *e)
{
  lv_obj_t *ta_ = lv_event_get_target(e);
  const char *txt = lv_textarea_get_text(ta_);
  cal_value = static_cast<float>(*txt);
}

void maxTime_changed_event(lv_event_t *e)
{
  lv_obj_t *ta_ = lv_event_get_target(e);
  const char *txt = lv_textarea_get_text(ta_);
  cal_value = static_cast<float>(*txt);
}

void create_screen_measurement()
{
  scr_measurement = lv_obj_create(NULL);

  lv_obj_set_style_bg_color(scr_measurement, lv_color_black(), LV_STATE_DEFAULT);

  lv_obj_t *label;
  lv_obj_t *btn;
  lv_obj_t *ta;

  label = lv_label_create(scr_measurement);
  lv_label_set_text(label, "Messung");
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 10);

  btn = lv_btn_create(scr_measurement);
  lv_obj_add_event_cb(btn, start_measurement_event, LV_EVENT_ALL, NULL);
  lv_obj_set_size(btn, 100, 70);
  lv_obj_align(btn, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
  lv_obj_set_style_bg_color(btn, lv_color_hex3(0x0F0), LV_STATE_DEFAULT);
  label = lv_label_create(btn);
  lv_label_set_text(label, "START");
  lv_obj_center(label);

  btn = lv_btn_create(scr_measurement);
  lv_obj_add_event_cb(btn, goto_startposition_event, LV_EVENT_ALL, NULL);
  lv_obj_set_size(btn, 100, 70);
  lv_obj_align(btn, LV_ALIGN_BOTTOM_LEFT, 10, 10);
  lv_obj_set_style_bg_color(btn, lv_color_hex3(0x99F), LV_STATE_DEFAULT);
  label = lv_label_create(btn);
  lv_label_set_text(label, "Startposition");
  lv_obj_center(label);

  // IDEA: Dropdown with known ropes. Values stored in _preferences_. Screen for new entries.

  label = lv_label_create(scr_measurement);
  lv_label_set_recolor(label, true);
  lv_label_set_text(label, "Kraft-#99ff99 blue#Minimum");
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, 10, 60);

  ta = lv_textarea_create(scr_measurement);
  lv_obj_align(ta, LV_ALIGN_TOP_LEFT, 150, 60);
  lv_obj_set_size(ta, 50, 50);
  lv_textarea_set_one_line(ta, true);
  lv_textarea_set_accepted_chars(ta, "0123456789.");
  lv_textarea_set_text(ta, "100");
  lv_obj_add_event_cb(ta, ta_event_cb, LV_EVENT_ALL, NULL);
  lv_obj_add_event_cb(ta, maxForce_changed_event, LV_EVENT_VALUE_CHANGED, NULL);

  label = lv_label_create(scr_measurement);
  lv_label_set_recolor(label, true);
  lv_label_set_text(label, "Kraft-#ff9999 red#Maximum");
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, 10, 120);

  ta = lv_textarea_create(scr_measurement);
  lv_obj_align(ta, LV_ALIGN_TOP_LEFT, 150, 120);
  lv_obj_set_size(ta, 50, 50);
  lv_textarea_set_one_line(ta, true);
  lv_textarea_set_accepted_chars(ta, "0123456789.");
  lv_textarea_set_text(ta, "200");
  lv_obj_add_event_cb(ta, ta_event_cb, LV_EVENT_ALL, NULL);
  lv_obj_add_event_cb(ta, minForce_changed_event, LV_EVENT_VALUE_CHANGED, NULL);

  label = lv_label_create(scr_measurement);
  lv_label_set_recolor(label, true);
  lv_label_set_text(label, "Zeit-#9999ff red#Maximum");
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, 10, 180);

  ta = lv_textarea_create(scr_measurement);
  lv_obj_align(ta, LV_ALIGN_TOP_LEFT, 150, 180);
  lv_obj_set_size(ta, 50, 50);
  lv_textarea_set_one_line(ta, true);
  lv_textarea_set_accepted_chars(ta, "0123456789.");
  lv_textarea_set_text(ta, "40"); // 5 mm/s @ 200 mm = 40s
  lv_obj_add_event_cb(ta, ta_event_cb, LV_EVENT_ALL, NULL);
  lv_obj_add_event_cb(ta, maxTime_changed_event, LV_EVENT_VALUE_CHANGED, NULL);
}

#pragma endregion

#pragma region MeasurementLive

void stop_measurement_event(lv_event_t *e)
{
  // TODO:
}

void label_forceMeasurement_change_event(lv_event_t *e)
{
  lv_obj_t *label = lv_event_get_target(e);
  lv_label_set_text_fmt(label, "%04.0f N", loadcell.get_cal_force());
}

void create_screen_measurement_live()
{
  scr_measurement_live = lv_obj_create(NULL);

  lv_obj_set_style_bg_color(scr_measurement_live, lv_color_hex3(0x070), LV_STATE_DEFAULT);

  lv_obj_t *label;
  lv_obj_t *btn;
  lv_obj_t *ta;

  label = lv_label_create(scr_measurement);
  lv_label_set_text(label, "Messung LIVE");
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 10);

  btn = lv_btn_create(scr_measurement);
  lv_obj_add_event_cb(btn, stop_measurement_event, LV_EVENT_ALL, NULL);
  lv_obj_set_size(btn, 150, 300);
  lv_obj_align(btn, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
  lv_obj_set_style_bg_color(btn, lv_color_hex3(0xF00), LV_STATE_DEFAULT);
  label = lv_label_create(btn);
  lv_label_set_text(label, "STOP");
  lv_obj_center(label);

  label = lv_label_create(scr_calibration);
  lv_label_set_text(label, "100");
  lv_obj_align(label, LV_ALIGN_BOTTOM_LEFT, -10, 10);
  lv_obj_add_event_cb(label, label_forceMeasurement_change_event, LV_EVENT_MSG_RECEIVED, NULL);
  lv_msg_subscribe_obj(MSG_NEW_FORCE_MEASURED, label, NULL);

  lv_obj_t *meter;
  lv_obj_set_size(meter, 300, 300);
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, 10, 10);

  /*Add a scale first*/
  lv_meter_scale_t *scale = lv_meter_add_scale(meter);
  lv_meter_set_scale_ticks(meter, scale, 50, 2, 10, lv_palette_main(LV_PALETTE_GREY));
  lv_meter_set_scale_major_ticks(meter, scale, 10, 4, 15, lv_color_black(), 10);
  lv_meter_set_scale_range(meter, scale, 0, 100, 320, 110);

  lv_meter_indicator_t *indic;

  /*Add a green arc after the min value*/
  indic = lv_meter_add_arc(meter, scale, 3, lv_palette_main(LV_PALETTE_GREEN), 0);
  lv_meter_set_indicator_start_value(meter, indic, 0); // TODO: min value
  lv_meter_set_indicator_end_value(meter, indic, 20);  // TODO: end value
  // TODO: "end value" = MAX value + 25% ????

  /*Make the tick lines blue at the start of the scale*/
  indic = lv_meter_add_scale_lines(meter, scale, lv_palette_main(LV_PALETTE_GREEN), lv_palette_main(LV_PALETTE_GREEN),
                                   false, 0);
  lv_meter_set_indicator_start_value(meter, indic, 0); // TODO: min value
  lv_meter_set_indicator_end_value(meter, indic, 20);  // TODO: end value

  /*Add a red arc before the max value*/
  indic = lv_meter_add_arc(meter, scale, 3, lv_palette_main(LV_PALETTE_RED), 0);
  lv_meter_set_indicator_start_value(meter, indic, 80); // TODO: min value - 20%
  lv_meter_set_indicator_end_value(meter, indic, 100);  // TODO: min value

  /*Make the tick lines red at the end of the scale*/
  indic = lv_meter_add_scale_lines(meter, scale, lv_palette_main(LV_PALETTE_RED), lv_palette_main(LV_PALETTE_RED), false,
                                   0);
  lv_meter_set_indicator_start_value(meter, indic, 80); // TODO: min value - 20%
  lv_meter_set_indicator_end_value(meter, indic, 100);  // TODO: min value

  /*Add a needle line indicator*/
  indic = lv_meter_add_needle_line(meter, scale, 4, lv_color_black(), -10);

  // TODO: set value of needle in message
  // lv_meter_set_indicator_value(meter, indic, v);
}

#pragma endregion

#pragma region MeasurementEnd

void finish_btn_event(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  if (code != LV_EVENT_CLICKED)
    return;
  lv_scr_load(scr_start);
}

void create_screen_measurement_end()
{
  scr_measurement_end = lv_obj_create(NULL);

  lv_obj_set_style_bg_color(scr_measurement_end, lv_color_hex3(0xF00), LV_STATE_DEFAULT);

  lv_obj_t *label;
  lv_obj_t *btn;

  label = lv_label_create(scr_measurement_end);
  lv_label_set_text(label, "VALUE"); // TODO: Ergebnis der Messung
  lv_obj_align(label, LV_ALIGN_CENTER, 0, -40);
  // TODO: riesig

  btn = lv_btn_create(scr_measurement_end);
  lv_obj_add_event_cb(btn, finish_btn_event, LV_EVENT_ALL, NULL);
  lv_obj_set_size(btn, 150, 40);
  lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex3(0x070), LV_STATE_DEFAULT);
  label = lv_label_create(btn);
  lv_label_set_text(label, "Fertig");
  lv_obj_center(label);
}

#pragma endregion

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

#pragma region Keyboard

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

#pragma endregion keyboard