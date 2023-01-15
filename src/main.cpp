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
#define MSG_TIME_IN_TEST 2

/*** Loadcell ***/
HX711 loadcell;
float cal_value = 1;
Preferences preferences; // https://randomnerdtutorials.com/esp32-save-data-permanently-preferences/
#define PREF_SCALE "scale"
#define PREF_ZERO "zero"

/*** Measurement Data ***/
float mes_set_minForce = 1000;
float mes_set_maxForce = 2500;
float mes_set_maxtime = 60;
float mes_maxForce = 0;
uint32_t mes_timeAtStart = 0;
float mes_timeSinceStart = 0;
#define METER_REDBAR_SIZE_MIN 0.8
// break detection
#define FORCE_DROP_FOR_BREAK 0.8
#define MIN_FORCE_FOR_BREAK_DETECTION 100
bool hasMinForceReached = false;
#define BREAK_DETECTION_MIN_POINTS 5
byte breakDetectionCount = 0;

/*** Additional Fonts***/
LV_FONT_DECLARE(UbuntuMono_16);
LV_FONT_DECLARE(UbuntuMono_36);
LV_FONT_DECLARE(UbuntuMono_72);
LV_FONT_DECLARE(UbuntuMono_200);

/*** round-time measurement ***/
#define RTT_TIMES_AVG 10
uint32_t roundTripTime = 0;
uint32_t roundTripTime_avg[RTT_TIMES_AVG];
uint32_t roundTripTime_sum;
byte roundTripTime_index;

/*** Motor control ***/;
enum motor_states
{
  MOTOR_NONE,
  MOTOR_PULL,
  MOTOR_PUSH,
  MOTOR_TESTING,
  MOTOR_ENDOFTEST,
  MOTOR_COAST,
  MOTOR_BREAK,
  MOTOR_GOTOSTART,
  MOTOR_STARTPOSITION,
};
uint8_t motor_state;
uint8_t last_motor_state = MOTOR_NONE;
#define MOTOR_1 33
#define MOTOR_2 32
#define STARTPOS_SWITCH 12
#define LED_DATA 27

/*** Function declaration ***/
void display_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p);
void touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data);
void lv_screen_start(void);
// void kb_event_cb(lv_obj_t *keyboard, lv_event_t e);
void kb_event(lv_event_t *e);
void ta_event_cb(lv_event_t *e);
void create_screen_start();
void create_screen_settings();
void create_screen_calibration();
void create_screen_measurement();
void create_screen_measurement_live();
void create_screen_measurement_end(); // will be build on purpose with values

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

  /*** MOTOR ***/
  pinMode(MOTOR_1, OUTPUT);
  pinMode(MOTOR_2, OUTPUT);
  pinMode(LED_DATA, OUTPUT);

  /*** Preferences ***/
  preferences.begin("srm-app", false);
  loadcell.set_scale(preferences.getFloat(PREF_SCALE, 1.0F));
  loadcell.set_zeropoint_offset(preferences.getFloat(PREF_ZERO, 0));

  /*** Screens***/
  create_screen_start();
  create_screen_settings();
  create_screen_calibration();
  create_screen_measurement();
  lv_scr_load(scr_start);
  motor_state = MOTOR_COAST;
}

void controlMotor()
{
  // if (digitalRead(STARTPOS_SWITCH) == HIGH)
  //   motor_state = MOTOR_STARTPOSITION;

  if (last_motor_state != motor_state)
  {
    last_motor_state = motor_state;
    switch (motor_state)
    {
    case MOTOR_PULL:
      digitalWrite(MOTOR_1, HIGH);
      digitalWrite(MOTOR_2, LOW);
      break;
    case MOTOR_GOTOSTART:
    case MOTOR_PUSH:
      digitalWrite(MOTOR_1, LOW);
      digitalWrite(MOTOR_2, HIGH);
      break;
    case MOTOR_BREAK:
      digitalWrite(MOTOR_1, HIGH);
      digitalWrite(MOTOR_2, HIGH);
      break;
    case MOTOR_ENDOFTEST:
      create_screen_measurement_end();

    case MOTOR_STARTPOSITION:
    case MOTOR_COAST:
    default:
      digitalWrite(MOTOR_1, LOW);
      digitalWrite(MOTOR_1, LOW);
      break;
    }
  }
}

String motor_state_str()
{
  switch (motor_state)
  {
  case MOTOR_NONE:
    return "NONE      ";
  case MOTOR_PULL:
    return "PULL      ";
  case MOTOR_PUSH:
    return "PUSH      ";
  case MOTOR_TESTING:
    return "TESTING   ";
  case MOTOR_ENDOFTEST:
    return "ENDOFTEST ";
  case MOTOR_BREAK:
    return "BREAK     ";
  case MOTOR_STARTPOSITION:
    return "STARTPOS  ";
  case MOTOR_COAST:
    return "COAST     ";
  case MOTOR_GOTOSTART:
    return "GOTO START";
  default:
    return ">> ERROR <<";
  }
}

byte nextIndex(byte index, byte limit)
{
  if (index + 1 >= limit)
    return 0;
  return index + 1;
}

void resetTest()
{
  mes_maxForce = 0;
  hasMinForceReached = false;
  mes_timeSinceStart = 0;
  mes_timeAtStart = 0;
  motor_state = MOTOR_COAST;
}

void endTest()
{
  motor_state = MOTOR_ENDOFTEST;
  create_screen_measurement_end();
  lv_scr_load(scr_measurement_end);
}

void loop()
{

  // lvgl & message handling
  lv_timer_handler(); /* let the GUI do its work */
  loadcell.read();    // get the current data from the loadcell
  if (mes_maxForce < loadcell.get_cal_force())
    mes_maxForce = loadcell.get_cal_force();
  lv_msg_send(MSG_NEW_FORCE_MEASURED, NULL);

  /** Detect break **/
  if (motor_state == MOTOR_TESTING)
  {
    // start of testing
    if (mes_timeAtStart == 0)
    {
      mes_timeAtStart = millis();
    }

    mes_timeSinceStart = (millis() - mes_timeAtStart) / 1000.0;
    lv_msg_send(MSG_TIME_IN_TEST, NULL);
    if (mes_timeSinceStart > mes_set_maxtime)
    {
      // time overdue --> abort
      endTest();
    }

    // overcome minimum force to rule out noise
    if (loadcell.get_cal_force() > MIN_FORCE_FOR_BREAK_DETECTION)
    {
      hasMinForceReached = true;
    }
    // low force triggers break detection
    if (hasMinForceReached && loadcell.get_cal_force() < mes_maxForce * (1 - FORCE_DROP_FOR_BREAK))
    {
      breakDetectionCount++;
    }
    else
    { // break detection is reset, when a higher force is measured
      breakDetectionCount = 0;
    }

    // end the test, if the break detection is saturated
    if (breakDetectionCount >= BREAK_DETECTION_MIN_POINTS)
    {
      // end of test
      endTest();
    }
  }
  else
  {
    // print status only, when we have time
    uint32_t t = millis() - roundTripTime;
    roundTripTime = millis();
    roundTripTime_index = nextIndex(roundTripTime_index, RTT_TIMES_AVG);
    roundTripTime_avg[roundTripTime_index] = t;
    roundTripTime_sum += t;
    roundTripTime_sum -= roundTripTime_avg[nextIndex(roundTripTime_index, RTT_TIMES_AVG)];
    lcd.setCursor(400, screenHeight - 10);
    lcd.printf("RTT: %04d", roundTripTime_sum / RTT_TIMES_AVG);
    lcd.setCursor(10, screenHeight - 10);
    lcd.printf("Force: %7.2f", loadcell.get_cal_force());
  }

  controlMotor();

  // print motor status
  lcd.setCursor(120, screenHeight - 10);
  lcd.printf("Motor: %s", motor_state_str().c_str());
}

/*** Display callback to flush the buffer to screen ***/
void display_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);

  lcd.startWrite();
  lcd.setAddrWindow(area->x1, area->y1, w, h);
  // lcd.pushColors((uint16_t *)&color_p->full, w * h, true);
  lcd.pushPixels((uint16_t *)&color_p->full, w * h, true);
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

static void to_scr_calibration_handler(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_CLICKED)
    lv_scr_load(scr_calibration);
}

static void to_scr_settings_handler(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_CLICKED)
    lv_scr_load(scr_settings);
}

static void to_scr_measurement_handler(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_CLICKED)
    lv_scr_load(scr_measurement);
}

static void stop_motor_handler(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_CLICKED)
    motor_state = MOTOR_BREAK;
}

static void createStandardButtons(lv_obj_t *scr, bool back = true, bool stop = true)
{
  lv_obj_t *btn;
  lv_obj_t *label;
  if (back)
  {
    // Back Button
    btn = lv_btn_create(scr);
    lv_obj_add_event_cb(btn, to_scr_start_handler, LV_EVENT_ALL, NULL);
    lv_obj_set_size(btn, 50, 40);               /*Set its size*/
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 0, 0); // Center and 0 from the top

    label = lv_label_create(btn);
    lv_label_set_text(label, "<<");
    lv_obj_center(label);
  }

  if (stop)
  {
    // Stop Button
    btn = lv_btn_create(scr);
    lv_obj_add_event_cb(btn, stop_motor_handler, LV_EVENT_ALL, NULL);
    lv_obj_set_size(btn, 50, 40); /*Set its size*/
    lv_obj_set_style_bg_color(btn, lv_color_hex3(0xF00), LV_STATE_DEFAULT);
    lv_obj_align(btn, LV_ALIGN_TOP_RIGHT, 0, 0); // Center and 0 from the top

    label = lv_label_create(btn);
    lv_obj_set_style_text_color(label, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_36, LV_STATE_DEFAULT);
    lv_label_set_text(label, "X");
    lv_obj_center(label);
  }
}

void create_screen_start(void)
{
  scr_start = lv_obj_create(NULL);

  lv_obj_set_style_bg_color(scr_start, lv_color_black(), LV_STATE_DEFAULT);

  createStandardButtons(scr_start, false, true);

  lv_obj_t *label;
  lv_obj_t *btn;

  /*** Create simple label and show version ***/
  label = lv_label_create(scr_start);                // full screen as the parent
  lv_label_set_text(label, "Seilrissmaschine V1.0"); // set label text
  lv_obj_align(label, LV_ALIGN_TOP_MID, -10, 10);    // Center and 0 from the top
  lv_obj_set_style_text_font(label, &lv_font_montserrat_36, LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(label, lv_color_hex3(0x070), LV_STATE_DEFAULT);

  // Measurement Button
  btn = lv_btn_create(scr_start);
  lv_obj_add_event_cb(btn, to_scr_measurement_handler, LV_EVENT_ALL, NULL);
  lv_obj_set_size(btn, 200, 200);
  lv_obj_align(btn, LV_ALIGN_RIGHT_MID, -10, 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex3(0x5f5), LV_PART_MAIN);

  label = lv_label_create(btn);
  lv_label_set_text(label, "Messung\nstarten");
  lv_obj_center(label);
  lv_obj_set_style_text_color(btn, lv_color_black(), LV_PART_MAIN);

  // Kalibrieren Button
  btn = lv_btn_create(scr_start);
  lv_obj_add_event_cb(btn, to_scr_calibration_handler, LV_EVENT_ALL, NULL);
  lv_obj_set_style_bg_color(btn, lv_color_hex3(0x555), LV_STATE_DEFAULT);
  lv_obj_set_size(btn, 200, 90);
  lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 10, 60);

  label = lv_label_create(btn);
  lv_label_set_text(label, "Kalibrierung");
  lv_obj_center(label);

  // Settings Button
  btn = lv_btn_create(scr_start);
  lv_obj_add_event_cb(btn, to_scr_settings_handler, LV_EVENT_ALL, NULL);
  lv_obj_set_size(btn, 200, 90);
  lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 10, 180);

  label = lv_label_create(btn);
  lv_label_set_text(label, "Einstellungen");
  lv_obj_center(label);
}

void label_forceRaw_change_event(lv_event_t *e)
{
  lv_obj_t *label = lv_event_get_target(e);
  lv_label_set_text_fmt(label, "%06.0f", loadcell.get_last_reading());
}

void label_forceRawZero_change_event(lv_event_t *e)
{
  lv_obj_t *label = lv_event_get_target(e);
  lv_label_set_text_fmt(label, "%06.0f", loadcell.get_last_reading_zeroed());
}

void label_forceCal_change_event(lv_event_t *e)
{
  lv_obj_t *label = lv_event_get_target(e);
  lv_label_set_text_fmt(label, "%06.0f N", loadcell.get_cal_force());
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
  lv_obj_t *ta = lv_event_get_target(e);
  const char *txt = lv_textarea_get_text(ta);
  cal_value = atof(txt);
}

void create_screen_calibration()
{
  scr_calibration = lv_obj_create(NULL);

  lv_obj_set_style_bg_color(scr_calibration, lv_color_black(), LV_STATE_DEFAULT);

  lv_obj_t *label;
  lv_obj_t *btn;
  lv_obj_t *ta;

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
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, 180, 130);

  ta = lv_textarea_create(scr_calibration);
  lv_obj_align(ta, LV_ALIGN_TOP_LEFT, 310, 130);
  lv_obj_set_size(ta, 100, 60);
  lv_textarea_set_one_line(ta, true);
  lv_textarea_set_accepted_chars(ta, "0123456789.");
  // lv_textarea_set_text(ta, "100");
  lv_obj_add_event_cb(ta, ta_event_cb, LV_EVENT_ALL, NULL);
  lv_obj_add_event_cb(ta, calValue_changed_event, LV_EVENT_VALUE_CHANGED, NULL);

  label = lv_label_create(scr_calibration);
  lv_label_set_text(label, "N");
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, 415, 145);

  // Messwert roh
  label = lv_label_create(scr_calibration);
  lv_label_set_text(label, "Rohwert:");
  lv_obj_align(label, LV_ALIGN_LEFT_MID, 70, 50);

  label = lv_label_create(scr_calibration);
  lv_obj_set_style_text_font(label, &UbuntuMono_16, LV_STATE_DEFAULT);
  lv_obj_align(label, LV_ALIGN_LEFT_MID, 160, 50);
  lv_obj_add_event_cb(label, label_forceRaw_change_event, LV_EVENT_MSG_RECEIVED, NULL);
  lv_msg_subscribe_obj(MSG_NEW_FORCE_MEASURED, label, NULL);

  // Messwert zero
  label = lv_label_create(scr_calibration);
  lv_label_set_text(label, "Roh-genullt:");
  lv_obj_align(label, LV_ALIGN_LEFT_MID, 70, 80);

  label = lv_label_create(scr_calibration);
  lv_obj_set_style_text_font(label, &UbuntuMono_36, LV_STATE_DEFAULT);
  lv_obj_align(label, LV_ALIGN_LEFT_MID, 70, 103);
  lv_obj_add_event_cb(label, label_forceRawZero_change_event, LV_EVENT_MSG_RECEIVED, NULL);
  lv_msg_subscribe_obj(MSG_NEW_FORCE_MEASURED, label, NULL);

  // Messwert kalibriert
  label = lv_label_create(scr_calibration);
  lv_label_set_text(label, "Kraft:");
  lv_obj_align(label, LV_ALIGN_RIGHT_MID, -20, 80);

  label = lv_label_create(scr_calibration);
  lv_obj_set_style_text_font(label, &UbuntuMono_36, LV_STATE_DEFAULT);
  lv_obj_align(label, LV_ALIGN_RIGHT_MID, -20, 100);
  lv_obj_add_event_cb(label, label_forceCal_change_event, LV_EVENT_MSG_RECEIVED, NULL);
  lv_msg_subscribe_obj(MSG_NEW_FORCE_MEASURED, label, NULL);

  createStandardButtons(scr_calibration);

  // https://docs.lvgl.io/latest/en/html/widgets/textarea.html
}

void motor_push_event(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSING)
    motor_state = MOTOR_PUSH;
  if (code == LV_EVENT_RELEASED)
    motor_state = MOTOR_BREAK;
}

void motor_pull_event(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSING)
    motor_state = MOTOR_PULL;
  if (code == LV_EVENT_RELEASED)
    motor_state = MOTOR_BREAK;
}

void start_measurement_event(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_LONG_PRESSED)
  {

    // for (int i = 0; i < lv_obj_get_child_cnt(scr_measurement); i++)
    // {
    //   lv_obj_t *child = lv_obj_get_child(scr_measurement, i);
    //   Serial.print(i);
    //   Serial.print("\t");
    //   Serial.println(lv_obj_check_type(child, &lv_textarea_class));
    // }

    lv_obj_t *ta;
    const char *txt;
    // Kraftmaximum
    ta = lv_obj_get_child(scr_measurement, 8);
    txt = lv_textarea_get_text(ta);
    mes_set_minForce = atof(txt);

    // Kraftminimum
    ta = lv_obj_get_child(scr_measurement, 11);
    txt = lv_textarea_get_text(ta);
    mes_set_maxForce = atof(txt);

    // Zeitmaximum
    ta = lv_obj_get_child(scr_measurement, 14);
    txt = lv_textarea_get_text(ta);
    mes_set_maxtime = atof(txt);

    create_screen_measurement_live();
    lv_scr_load(scr_measurement_live);
    motor_state = MOTOR_TESTING;
  }
}

void goto_startposition_event(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_CLICKED)
    motor_state = MOTOR_GOTOSTART;
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

  createStandardButtons(scr_measurement);

  lv_obj_t *label;
  lv_obj_t *btn;
  lv_obj_t *ta;

  // TODO: tare

  label = lv_label_create(scr_measurement);
  lv_label_set_text(label, "Messung");
  lv_obj_set_style_text_font(label, &lv_font_montserrat_36, LV_STATE_DEFAULT);
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 0);

  // Button START
  btn = lv_btn_create(scr_measurement);
  lv_obj_add_event_cb(btn, start_measurement_event, LV_EVENT_ALL, NULL);
  lv_obj_set_size(btn, 190, 100);
  lv_obj_align(btn, LV_ALIGN_BOTTOM_RIGHT, 0, -10);
  lv_obj_set_style_bg_color(btn, lv_color_hex3(0x0F0), LV_STATE_DEFAULT);
  label = lv_label_create(btn);
  lv_label_set_text(label, "START\n(lange druecken)");
  lv_obj_set_style_text_color(label, lv_color_black(), LV_STATE_DEFAULT);
  lv_obj_center(label);

  // Button Startposition
  btn = lv_btn_create(scr_measurement);
  lv_obj_add_event_cb(btn, goto_startposition_event, LV_EVENT_ALL, NULL);
  lv_obj_set_size(btn, 190, 100);
  lv_obj_align(btn, LV_ALIGN_BOTTOM_LEFT, 0, -10);
  lv_obj_set_style_bg_color(btn, lv_color_hex3(0x99F), LV_STATE_DEFAULT);
  label = lv_label_create(btn);
  lv_label_set_text(label, "Startposition");
  lv_obj_set_style_text_color(label, lv_color_black(), LV_STATE_DEFAULT);
  lv_obj_center(label);

  // Button PULL
  btn = lv_btn_create(scr_measurement);
  lv_obj_add_event_cb(btn, motor_pull_event, LV_EVENT_ALL, NULL);
  lv_obj_set_size(btn, 90, 47);
  lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_set_style_bg_color(btn, lv_color_hex3(0xCFC), LV_STATE_DEFAULT);
  label = lv_label_create(btn);
  lv_label_set_text(label, "Spannen");
  lv_obj_set_style_text_color(label, lv_color_black(), LV_STATE_DEFAULT);
  lv_obj_center(label);

  // Button PUSH
  btn = lv_btn_create(scr_measurement);
  lv_obj_add_event_cb(btn, motor_push_event, LV_EVENT_ALL, NULL);
  lv_obj_set_size(btn, 90, 47);
  lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -63);
  lv_obj_set_style_bg_color(btn, lv_color_hex3(0xCCF), LV_STATE_DEFAULT);
  label = lv_label_create(btn);
  lv_label_set_text(label, "Loesen");
  lv_obj_set_style_text_color(label, lv_color_black(), LV_STATE_DEFAULT);
  lv_obj_center(label);

  // IDEA: Dropdown with known ropes. Values stored in _preferences_. Screen for new entries.

  int16_t x;
  int16_t y;
  const int16_t y0 = 50;
  const int16_t mx1 = 25; // right
  const int16_t my1 = 18; // down
  const int16_t mx2 = 81; // right
  const int16_t my2 = 12; // down

  // Kraftminimum
  x = 10;
  y = y0;
  label = lv_label_create(scr_measurement);
  lv_label_set_recolor(label, true);
  lv_label_set_text(label, "Kraft-#99ff99 Minimum#");
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, x, y);

  x += mx1;
  y += my1;
  ta = lv_textarea_create(scr_measurement);
  lv_obj_align(ta, LV_ALIGN_TOP_LEFT, x, y);
  lv_obj_set_size(ta, 80, 50);
  lv_textarea_set_one_line(ta, true);
  lv_textarea_set_accepted_chars(ta, "0123456789.");
  lv_textarea_set_text(ta, "1000");
  lv_obj_add_event_cb(ta, ta_event_cb, LV_EVENT_ALL, NULL);
  lv_obj_add_event_cb(ta, maxForce_changed_event, LV_EVENT_VALUE_CHANGED, NULL);

  x += mx2;
  y += my2;
  label = lv_label_create(scr_measurement);
  lv_label_set_recolor(label, true);
  lv_label_set_text(label, "N");
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, x, y);

  // Kraftmaximum
  x = 0;
  y = y0;
  label = lv_label_create(scr_measurement);
  lv_label_set_recolor(label, true);
  lv_label_set_text(label, "Kraft-#ff9999 Maximum#");
  lv_obj_align(label, LV_ALIGN_TOP_MID, x, y);

  x += 0;
  y += my1;
  ta = lv_textarea_create(scr_measurement);
  lv_obj_align(ta, LV_ALIGN_TOP_MID, x, y);
  lv_obj_set_size(ta, 80, 50);
  lv_textarea_set_one_line(ta, true);
  lv_textarea_set_accepted_chars(ta, "0123456789.");
  lv_textarea_set_text(ta, "2000");
  lv_obj_add_event_cb(ta, ta_event_cb, LV_EVENT_ALL, NULL);
  lv_obj_add_event_cb(ta, minForce_changed_event, LV_EVENT_VALUE_CHANGED, NULL);

  x += 47;
  y += my2;
  label = lv_label_create(scr_measurement);
  lv_label_set_recolor(label, true);
  lv_label_set_text(label, "N");
  lv_obj_align(label, LV_ALIGN_TOP_MID, x, y);

  // Zeitmaximum
  x = -10;
  y = y0;
  label = lv_label_create(scr_measurement);
  lv_label_set_recolor(label, true);
  lv_label_set_text(label, "Zeit-#9999ff Maximum#");
  lv_obj_align(label, LV_ALIGN_TOP_RIGHT, x, y);

  x -= mx1 + 15;
  y += my1;
  ta = lv_textarea_create(scr_measurement);
  lv_obj_align(ta, LV_ALIGN_TOP_RIGHT, x, y);
  lv_obj_set_size(ta, 50, 50);
  lv_textarea_set_one_line(ta, true);
  lv_textarea_set_accepted_chars(ta, "0123456789.");
  lv_textarea_set_text(ta, "40"); // 5 mm/s @ 200 mm = 40s
  lv_obj_add_event_cb(ta, ta_event_cb, LV_EVENT_ALL, NULL);
  lv_obj_add_event_cb(ta, maxTime_changed_event, LV_EVENT_VALUE_CHANGED, NULL);

  x -= -25;
  y += my2;
  label = lv_label_create(scr_measurement);
  lv_label_set_recolor(label, true);
  lv_label_set_text(label, "s");
  lv_obj_align(label, LV_ALIGN_TOP_RIGHT, x, y);
}

void stop_measurement_event(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_CLICKED)
  {
    motor_state = MOTOR_BREAK;
    lv_scr_load(scr_measurement);
  }
}

void label_testTime_change_event(lv_event_t *e)
{
  lv_obj_t *label = lv_event_get_target(e);
  lv_label_set_text_fmt(label, "%4.1fs", mes_timeSinceStart);
}

void label_forceMeasurement_change_event(lv_event_t *e)
{
  lv_obj_t *label = lv_event_get_target(e);
  lv_label_set_text_fmt(label, "%4.0fN", loadcell.get_cal_force());
}

void label_maxForceMeasurement_change_event(lv_event_t *e)
{
  lv_obj_t *label = lv_event_get_target(e);
  lv_label_set_text_fmt(label, "%4.0fN", mes_maxForce);
}

void meter_forceMeasurement_change_event(lv_event_t *e)
{
  lv_obj_t *meter = lv_event_get_target(e);
  lv_meter_indicator_t *indic = (lv_meter_indicator_t *)lv_event_get_user_data(e);
  lv_meter_set_indicator_value(meter, indic, loadcell.get_cal_force());
}

void create_screen_measurement_live()
{
  scr_measurement_live = lv_obj_create(NULL);

  lv_obj_set_style_bg_color(scr_measurement_live, lv_color_hex3(0x070), LV_STATE_DEFAULT);

  lv_obj_t *label;
  lv_obj_t *btn;

  btn = lv_btn_create(scr_measurement_live);
  lv_obj_add_event_cb(btn, stop_measurement_event, LV_EVENT_ALL, NULL);
  lv_obj_set_size(btn, 150, 150);
  lv_obj_align(btn, LV_ALIGN_TOP_RIGHT, -10, 10);
  lv_obj_set_style_bg_color(btn, lv_color_hex3(0xF00), LV_STATE_DEFAULT);
  label = lv_label_create(btn);
  lv_label_set_text(label, "STOP");
  lv_obj_set_style_text_font(label, &lv_font_montserrat_36, LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(label, lv_color_black(), LV_STATE_DEFAULT);
  lv_obj_center(label);

  // Label for showing the testing time
  label = lv_label_create(scr_measurement_live);
  lv_obj_set_style_text_font(label, &UbuntuMono_36, LV_STATE_DEFAULT);
  lv_obj_align(label, LV_ALIGN_RIGHT_MID, -10, 20);
  lv_obj_add_event_cb(label, label_testTime_change_event, LV_EVENT_MSG_RECEIVED, NULL);
  lv_msg_subscribe_obj(MSG_TIME_IN_TEST, label, NULL);

  // Label for showing the MAX-force
  label = lv_label_create(scr_measurement_live);
  lv_obj_set_style_text_font(label, &UbuntuMono_36, LV_STATE_DEFAULT);
  lv_obj_align(label, LV_ALIGN_RIGHT_MID, -10, 65);
  lv_obj_add_event_cb(label, label_maxForceMeasurement_change_event, LV_EVENT_MSG_RECEIVED, NULL);
  lv_msg_subscribe_obj(MSG_NEW_FORCE_MEASURED, label, NULL);

  // Label for showing the force
  label = lv_label_create(scr_measurement_live);
  lv_obj_set_style_text_font(label, &UbuntuMono_72, LV_STATE_DEFAULT);
  lv_obj_align(label, LV_ALIGN_RIGHT_MID, -10, 110);
  lv_obj_add_event_cb(label, label_forceMeasurement_change_event, LV_EVENT_MSG_RECEIVED, NULL);
  lv_msg_subscribe_obj(MSG_NEW_FORCE_MEASURED, label, NULL);

  /*** METER ***/
  lv_obj_t *meter = lv_meter_create(scr_measurement_live);
  lv_obj_set_size(meter, 300, 300);
  lv_obj_align(meter, LV_ALIGN_TOP_LEFT, 5, 5);

  /*Add a scale first*/
  lv_meter_scale_t *scale = lv_meter_add_scale(meter);
  lv_meter_set_scale_ticks(meter, scale, 50, 2, 10, lv_palette_main(LV_PALETTE_GREY));
  lv_meter_set_scale_major_ticks(meter, scale, 10, 4, 15, lv_color_black(), 15);
  lv_meter_set_scale_range(meter, scale, 0, mes_set_maxForce, 320, 110);

  lv_meter_indicator_t *indic;

  /*Add a green arc after the min value*/
  indic = lv_meter_add_arc(meter, scale, 3, lv_palette_main(LV_PALETTE_GREEN), 0);
  lv_meter_set_indicator_start_value(meter, indic, mes_set_minForce);
  lv_meter_set_indicator_end_value(meter, indic, mes_set_maxForce);

  /*Make the tick lines green after the min force*/
  indic = lv_meter_add_scale_lines(meter, scale, lv_palette_main(LV_PALETTE_GREEN), lv_palette_main(LV_PALETTE_GREEN), false, 0);
  lv_meter_set_indicator_start_value(meter, indic, mes_set_minForce);
  lv_meter_set_indicator_end_value(meter, indic, mes_set_maxForce);

  /*Add a red arc before the max value*/
  indic = lv_meter_add_arc(meter, scale, 3, lv_palette_main(LV_PALETTE_RED), 0);
  lv_meter_set_indicator_start_value(meter, indic, mes_set_minForce * METER_REDBAR_SIZE_MIN);
  lv_meter_set_indicator_end_value(meter, indic, mes_set_minForce);

  /*Make the tick lines red at the end of the scale*/
  indic = lv_meter_add_scale_lines(meter, scale, lv_palette_main(LV_PALETTE_RED), lv_palette_main(LV_PALETTE_RED), false, 0);
  lv_meter_set_indicator_start_value(meter, indic, mes_set_minForce * METER_REDBAR_SIZE_MIN);
  lv_meter_set_indicator_end_value(meter, indic, mes_set_minForce);

  /*Make the arc RED and FAT breakpoint threshhold */
  indic = lv_meter_add_arc(meter, scale, 5, lv_palette_main(LV_PALETTE_RED), 5);
  lv_meter_set_indicator_start_value(meter, indic, MIN_FORCE_FOR_BREAK_DETECTION - 10);
  lv_meter_set_indicator_end_value(meter, indic, MIN_FORCE_FOR_BREAK_DETECTION + 10);

  /*Add a needle line indicator*/
  indic = lv_meter_add_needle_line(meter, scale, 6, lv_color_black(), -10);

  /*Subscribe to Force Change event*/
  lv_msg_subscribe_obj(MSG_NEW_FORCE_MEASURED, meter, NULL);
  lv_obj_add_event_cb(meter, meter_forceMeasurement_change_event, LV_EVENT_MSG_RECEIVED, indic);
}

void finish_btn_event(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  if (code != LV_EVENT_CLICKED)
    return;
  resetTest();
  lv_scr_load(scr_start);
}

void create_screen_measurement_end()
{
  scr_measurement_end = lv_obj_create(NULL);

  lv_obj_t *label;
  lv_obj_t *btn;

  if (mes_maxForce >= mes_set_minForce)
  {
    // successful test -- GREEN
    lv_obj_set_style_bg_color(scr_measurement_end, lv_color_hex3(0x0F0), LV_STATE_DEFAULT);
  }
  else
  {
    // unsuccessful test -- RED
    lv_obj_set_style_bg_color(scr_measurement_end, lv_color_hex3(0xF00), LV_STATE_DEFAULT);
  }

  label = lv_label_create(scr_measurement_end);
  lv_obj_set_style_text_font(label, &UbuntuMono_200, LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(label, lv_color_black(), LV_STATE_DEFAULT);
  lv_label_set_text_fmt(label, "%.0fN", mes_maxForce);
  lv_obj_align(label, LV_ALIGN_CENTER, 0, -60);

  label = lv_label_create(scr_measurement_end);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_36, LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(label, lv_color_black(), LV_STATE_DEFAULT);
  lv_label_set_text_fmt(label, "Mindestwert: %.0f N", mes_set_minForce);
  lv_obj_align(label, LV_ALIGN_CENTER, 0, 50);

  btn = lv_btn_create(scr_measurement_end);
  lv_obj_add_event_cb(btn, finish_btn_event, LV_EVENT_ALL, NULL);
  lv_obj_set_size(btn, 400, 60);
  lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_set_style_bg_color(btn, lv_color_hex3(0x555), LV_STATE_DEFAULT);
  label = lv_label_create(btn);
  lv_label_set_text(label, "Fertig");
  lv_obj_center(label);
}

void create_screen_settings()
{
  scr_settings = lv_obj_create(NULL);

  lv_obj_set_style_bg_color(scr_settings, lv_color_black(), LV_STATE_DEFAULT);

  lv_obj_t *label;

  label = lv_label_create(scr_settings);
  lv_label_set_text(label, "ToDo ...");
  lv_obj_center(label);

  createStandardButtons(scr_settings);
}

// void kb_event_cb(lv_obj_t *keyboard, lv_event_t *e)
// {
//   lv_event_code_t code = lv_event_get_code(e);
//   if (code == LV_EVENT_CANCEL)
//   {
//     lv_obj_set_height(lv_scr_act(), LV_VER_RES);
//     lv_keyboard_set_textarea(kb, NULL);
//     lv_obj_del(kb);
//     kb = NULL;
//   }
//   if (code == LV_EVENT_READY)
//   {
//     ta = lv_keyboard_get_textarea(kb);
//     const char *str = lv_textarea_get_text(ta);
//     /*Normally do something with data here*/

//     /**
//      * Restore Cont to original size and delete the KB
//      */
//     lv_keyboard_set_textarea(kb, NULL);
//     lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
//   }
// }

void kb_create()
{
  kb = lv_keyboard_create(lv_scr_act());
  lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);
  lv_obj_add_event_cb(kb, kb_event, LV_EVENT_ALL, NULL);
}

void kb_close()
{
  lv_keyboard_set_textarea(kb, NULL);
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
}

void kb_event(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_READY)
  {
    lv_obj_t *ta = lv_keyboard_get_textarea(kb);
    lv_obj_clear_state(ta, LV_STATE_FOCUSED);

    kb_close();
  }
}

void ta_event_cb(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *ta = lv_event_get_target(e);
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
