#include <pebble.h>

// --- Message Keys ---
#define MESSAGE_KEY_TEMP_CURRENT 0
#define MESSAGE_KEY_CONDITIONS 1
#define MESSAGE_KEY_TEMP_HIGH 2
#define MESSAGE_KEY_TEMP_LOW 3
#define MESSAGE_KEY_UNITS 4
#define MESSAGE_KEY_TIME_FORMAT 6
#define MESSAGE_KEY_STEP_GOAL 7
#define MESSAGE_KEY_ACTIVE_GOAL 8
#define MESSAGE_KEY_IS_NIGHT 9
#define MESSAGE_KEY_UPDATE_FREQ 10
#define MESSAGE_KEY_UV_INDEX 11

// --- Persistence Keys ---
#define PERSIST_KEY_CUR 10
#define PERSIST_KEY_HI 11
#define PERSIST_KEY_LO 12
#define PERSIST_KEY_ICON 13
#define PERSIST_KEY_UV 14
#define PERSIST_KEY_IS_NIGHT 15

// --- UI Elements ---
static Window *s_main_window;
// COMMENTED OUT: s_weather_desc_layer
static TextLayer *s_time_layer, *s_date_layer, *s_hr_layer, *s_dist_layer /*, *s_weather_desc_layer */;
static Layer *s_weather_arc_layer, *s_goals_canvas;
static BitmapLayer *s_weather_icon_layer, *s_heart_layer;
static GBitmap *s_weather_bitmap = NULL, *s_heart_bitmap = NULL;
static Layer *s_battery_layer;

// --- State Variables ---
static int s_cur = 0, s_hi = 0, s_lo = 0, s_uv = 0;
static int s_step_goal = 10000, s_active_goal = 30, s_update_freq = 30, s_battery_level = 100;
static bool s_is_24h = false, s_connected = true, s_use_metric = false, s_is_night = false;
static char s_time_buf[10], s_cur_buf[8], s_hi_buf[8], s_lo_buf[8], s_hr_buf[8], s_dist_buf[16];
static char s_icon_code[4] = "01d";
// COMMENTED OUT: Description text buffer
// static char s_weather_desc_buf[20] = ""; 
static time_t s_weather_timestamp = 0;

// --- Heart Graph Variables ---
#define HR_HISTORY_SIZE 20
static int s_hr_history[HR_HISTORY_SIZE];
static Layer *s_hr_graph_layer;

// --- Memory-Safe Tuple Reader ---
static int get_int(Tuple *t) {
  if (t) {
    if (t->type == TUPLE_INT) {
      if (t->length == 1) return t->value->int8;
      if (t->length == 2) return t->value->int16;
      if (t->length == 4) return t->value->int32;
    } else if (t->type == TUPLE_UINT) {
      if (t->length == 1) return t->value->uint8;
      if (t->length == 2) return t->value->uint16;
      if (t->length == 4) return t->value->uint32;
    } else if (t->type == TUPLE_CSTRING) {
      return atoi(t->value->cstring);
    }
  }
  return 0;
}

// --- Persistence and Update Logic ---
static void request_weather() {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
    dict_write_uint8(iter, 0, 0); 
    app_message_outbox_send();
  }
}

static void save_weather_data() {
  persist_write_int(PERSIST_KEY_CUR, s_cur);
  persist_write_int(PERSIST_KEY_HI, s_hi);
  persist_write_int(PERSIST_KEY_LO, s_lo);
  persist_write_string(PERSIST_KEY_ICON, s_icon_code);
  persist_write_int(PERSIST_KEY_UV, s_uv);
  persist_write_bool(PERSIST_KEY_IS_NIGHT, s_is_night);
}

static void load_weather_data() {
  if (persist_exists(PERSIST_KEY_CUR)) {
    s_cur = persist_read_int(PERSIST_KEY_CUR);
    s_hi = persist_read_int(PERSIST_KEY_HI);
    s_lo = persist_read_int(PERSIST_KEY_LO);
    persist_read_string(PERSIST_KEY_ICON, s_icon_code, sizeof(s_icon_code));
    if (persist_exists(PERSIST_KEY_UV)) s_uv = persist_read_int(PERSIST_KEY_UV);
    if (persist_exists(PERSIST_KEY_IS_NIGHT)) s_is_night = persist_read_bool(PERSIST_KEY_IS_NIGHT);
  }
}

static void tap_handler(AccelAxisType axis, int32_t direction) {
  request_weather();
}

// --- Weather Arc Drawing ---
static void weather_arc_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_text_color(ctx, GColorBlack);
  
  snprintf(s_lo_buf, sizeof(s_lo_buf), "%d", s_lo);
  snprintf(s_hi_buf, sizeof(s_hi_buf), "%d", s_hi);
  graphics_draw_text(ctx, s_lo_buf, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GRect(15, 35, 35, 22), GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
  graphics_draw_text(ctx, s_hi_buf, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GRect(bounds.size.w - 50, 35, 35, 22), GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);
  
  snprintf(s_cur_buf, sizeof(s_cur_buf), "%d", s_cur);
  graphics_draw_text(ctx, s_cur_buf, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD), GRect(0, 15, bounds.size.w, 35), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  
  GRect arc_bounds = grect_inset(bounds, GEdgeInsets(14, 14, 28, 14));
  graphics_context_set_stroke_width(ctx, 4);
  graphics_context_set_stroke_color(ctx, GColorLightGray);
  graphics_draw_arc(ctx, arc_bounds, GOvalScaleModeFitCircle, DEG_TO_TRIGANGLE(260), DEG_TO_TRIGANGLE(460));
  
  if (s_connected) {
    int range = s_hi - s_lo;
    float percent = 0.5f; 
    
    if (range > 0) percent = (float)(s_cur - s_lo) / (float)range;
    if (percent < 0.0f) percent = 0.0f;
    if (percent > 1.0f) percent = 1.0f;
    
    int angle = 260 + (int)(percent * 200.0f);
    GPoint center = grect_center_point(&arc_bounds);
    int16_t radius = (arc_bounds.size.w / 2) - 8; 
    
    GPoint dot = {
      (int16_t)(center.x + (sin_lookup(DEG_TO_TRIGANGLE(angle)) * radius / TRIG_MAX_RATIO)), 
      (int16_t)(center.y - (cos_lookup(DEG_TO_TRIGANGLE(angle)) * radius / TRIG_MAX_RATIO))
    };
    
    GColor uv_color = GColorBlack; 
    if (!s_is_night && s_uv > 0) {
      if (s_uv <= 2) uv_color = GColorKellyGreen;
      else if (s_uv <= 5) uv_color = GColorYellow;
      else if (s_uv <= 7) uv_color = GColorOrange;
      else if (s_uv <= 10) uv_color = GColorRed;
      else uv_color = GColorPurple;
    }
    
    graphics_context_set_fill_color(ctx, uv_color);
    graphics_fill_circle(ctx, dot, 5); 
  }
}

// --- Goal Rings Drawing ---
static void goals_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  GRect draw_bounds = grect_inset(bounds, GEdgeInsets(3));
  int steps = (int)health_service_sum_today(HealthMetricStepCount);
  int active = (int)health_service_sum_today(HealthMetricActiveSeconds) / 60;
  
  graphics_context_set_stroke_width(ctx, 5);
  graphics_context_set_stroke_color(ctx, GColorLightGray);
  graphics_draw_arc(ctx, draw_bounds, GOvalScaleModeFitCircle, 0, TRIG_MAX_ANGLE);
  graphics_context_set_stroke_color(ctx, GColorKellyGreen);
  graphics_draw_arc(ctx, draw_bounds, GOvalScaleModeFitCircle, 0, (s_active_goal > 0) ? (active * TRIG_MAX_ANGLE) / s_active_goal : 0);
  graphics_context_set_stroke_color(ctx, GColorPictonBlue);
  graphics_draw_arc(ctx, grect_inset(draw_bounds, GEdgeInsets(7)), GOvalScaleModeFitCircle, 0, (s_step_goal > 0) ? (steps * TRIG_MAX_ANGLE) / s_step_goal : 0);
}

// --- Heart Rate Sparkline Drawing (Exaggerated & Colorized) ---
static void hr_graph_update_proc(Layer *layer, GContext *ctx) {
  #if defined(PBL_HEALTH)
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_stroke_width(ctx, 2);
  
  int step_x = bounds.size.w / HR_HISTORY_SIZE;
  
  for(int i = 0; i < HR_HISTORY_SIZE - 1; i++) {
    int y1 = bounds.size.h - ((s_hr_history[i] - 55) / 2);
    int y2 = bounds.size.h - ((s_hr_history[i+1] - 55) / 2);
    
    if(y1 < 2) { y1 = 2; } 
    if(y1 > bounds.size.h) { y1 = bounds.size.h; }
    if(y2 < 2) { y2 = 2; } 
    if(y2 > bounds.size.h) { y2 = bounds.size.h; }
    
    int avg_hr = (s_hr_history[i] + s_hr_history[i+1]) / 2;
    GColor line_color = GColorKellyGreen; 
    if (avg_hr < 70) line_color = GColorPictonBlue; 
    else if (avg_hr > 120) line_color = GColorRed; 
    else if (avg_hr > 95) line_color = GColorChromeYellow; 

    graphics_context_set_stroke_color(ctx, line_color);
    graphics_draw_line(ctx, GPoint(i * step_x, y1), GPoint((i+1) * step_x, y2));
  }
  #endif
}

static void update_theme_colors() {
  window_set_background_color(s_main_window, GColorWhite);
  text_layer_set_text_color(s_time_layer, GColorBlack);
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_date_layer, GColorBlack);
  text_layer_set_background_color(s_date_layer, GColorClear);
  text_layer_set_text_color(s_hr_layer, GColorBlack);
  text_layer_set_background_color(s_hr_layer, GColorClear);
  text_layer_set_text_color(s_dist_layer, GColorBlack);
  text_layer_set_background_color(s_dist_layer, GColorClear);
  bitmap_layer_set_background_color(s_weather_icon_layer, GColorClear);
  bitmap_layer_set_compositing_mode(s_weather_icon_layer, GCompOpSet);
  bitmap_layer_set_background_color(s_heart_layer, GColorClear);
  bitmap_layer_set_compositing_mode(s_heart_layer, GCompOpSet);
}

static void set_weather_icon(char *icon_code) {
  if (s_weather_bitmap) { gbitmap_destroy(s_weather_bitmap); s_weather_bitmap = NULL; }
  strncpy(s_icon_code, icon_code, sizeof(s_icon_code)); 
  
  uint32_t res_id = RESOURCE_ID_ICON_CLOUDY;
  // COMMENTED OUT: description variable
  // char *desc = "Loading";

  if (!s_connected) {
    res_id = RESOURCE_ID_ICON_HAZE;
    // desc = "Offline";
  } else {
    // Description assignments have been commented out to prevent unused variable compiler errors
    if (strcmp(icon_code, "01d") == 0) { res_id = RESOURCE_ID_ICON_CLEAR_DAY; /* desc = "Clear"; */ }
    else if (strcmp(icon_code, "01n") == 0) { res_id = RESOURCE_ID_ICON_CLEAR_NIGHT; /* desc = "Clear"; */ }
    else if (strcmp(icon_code, "02d") == 0) { res_id = RESOURCE_ID_ICON_CLOUDY_SUN; /* desc = "P. Cloudy"; */ }
    else if (strcmp(icon_code, "02n") == 0) { res_id = RESOURCE_ID_ICON_CLOUDY_NIGHT; /* desc = "P. Cloudy"; */ }
    else if (strcmp(icon_code, "03d") == 0 || strcmp(icon_code, "03n") == 0) { res_id = RESOURCE_ID_ICON_CLOUDY; /* desc = "Cloudy"; */ }
    else if (strcmp(icon_code, "04d") == 0 || strcmp(icon_code, "04n") == 0) { res_id = RESOURCE_ID_ICON_CLOUDY; /* desc = "Overcast"; */ }
    else if (strcmp(icon_code, "09d") == 0 || strcmp(icon_code, "09n") == 0) { res_id = RESOURCE_ID_ICON_RAIN; /* desc = "Showers"; */ }
    else if (strcmp(icon_code, "10d") == 0 || strcmp(icon_code, "10n") == 0) { res_id = RESOURCE_ID_ICON_RAIN; /* desc = "Rain"; */ }
    else if (strcmp(icon_code, "11d") == 0 || strcmp(icon_code, "11n") == 0) { res_id = RESOURCE_ID_ICON_THUNDER_SHOWER; /* desc = "Storm"; */ }
    else if (strcmp(icon_code, "13d") == 0 || strcmp(icon_code, "13n") == 0) { res_id = RESOURCE_ID_ICON_SNOW; /* desc = "Snow"; */ }
    else if (strcmp(icon_code, "50d") == 0 || strcmp(icon_code, "50n") == 0) { res_id = RESOURCE_ID_ICON_HAZE; /* desc = "Mist"; */ }
    // else { desc = "Weather"; } 
  }

  s_weather_bitmap = gbitmap_create_with_resource(res_id);
  bitmap_layer_set_bitmap(s_weather_icon_layer, s_weather_bitmap);
  
  // COMMENTED OUT: description assignment to layer
  /*
  strncpy(s_weather_desc_buf, desc, sizeof(s_weather_desc_buf));
  if (s_weather_desc_layer) {
    text_layer_set_text(s_weather_desc_layer, s_weather_desc_buf);
  }
  */
}

static void update_time(struct tm *tick_time, TimeUnits units_changed) {
  int hour = tick_time->tm_hour;
  if (!s_is_24h) { hour %= 12; if (hour == 0) hour = 12; }
  snprintf(s_time_buf, sizeof(s_time_buf), s_is_24h ? "%02d:%02d" : "%d:%02d", hour, tick_time->tm_min);
  text_layer_set_text(s_time_layer, s_time_buf);
  static char d_buf[16]; strftime(d_buf, sizeof(d_buf), "%a, %b %d", tick_time);
  text_layer_set_text(s_date_layer, d_buf);
  
  if (tick_time->tm_min % 30 == 0) request_weather();

  #if defined(PBL_HEALTH)
  for(int i = 0; i < HR_HISTORY_SIZE - 1; i++) {
    s_hr_history[i] = s_hr_history[i+1];
  }
  s_hr_history[HR_HISTORY_SIZE - 1] = (int)health_service_peek_current_value(HealthMetricHeartRateBPM);
  layer_mark_dirty(s_hr_graph_layer);
  #endif
}

static void update_health() {
  #if defined(PBL_HEALTH)
  int bpm = (int)health_service_peek_current_value(HealthMetricHeartRateBPM);
  
  snprintf(s_hr_buf, sizeof(s_hr_buf), "%d", bpm);
  text_layer_set_text(s_hr_layer, s_hr_buf);
  
  int dist_m = (int)health_service_sum_today(HealthMetricWalkedDistanceMeters);
  int dist_100th = s_use_metric ? (dist_m * 100) / 1000 : (dist_m * 100) / 1609;
  
  snprintf(s_dist_buf, sizeof(s_dist_buf), "%d.%01d", dist_100th / 100, (dist_100th % 100) / 10);
  text_layer_set_text(s_dist_layer, s_dist_buf);

  layer_mark_dirty(s_goals_canvas);
  #endif
}

static void battery_callback(BatteryChargeState state) {
  s_battery_level = state.charge_percent;
  layer_mark_dirty(s_battery_layer);
}

static void battery_update_proc(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, s_battery_level > 20 ? GColorKellyGreen : GColorRed);
  graphics_fill_rect(ctx, GRect(0, 0, (s_battery_level * b.size.w) / 100, b.size.h), 0, GCornerNone);
}

static void main_window_load(Window *window) {
  Layer *w_layer = window_get_root_layer(window);
  
  s_weather_icon_layer = bitmap_layer_create(GRect(15, 0, 64, 64));
  layer_add_child(w_layer, bitmap_layer_get_layer(s_weather_icon_layer));

  // COMMENTED OUT: description layer creation
  /*
  s_weather_desc_layer = text_layer_create(GRect(5, 44, 84, 18));
  text_layer_set_font(s_weather_desc_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
  text_layer_set_text_alignment(s_weather_desc_layer, GTextAlignmentCenter);
  text_layer_set_background_color(s_weather_desc_layer, GColorClear);
  layer_add_child(w_layer, text_layer_get_layer(s_weather_desc_layer));
  */
  
  s_weather_arc_layer = layer_create(GRect(105, 0, 90, 85));
  layer_set_update_proc(s_weather_arc_layer, weather_arc_update_proc);
  layer_add_child(w_layer, s_weather_arc_layer);
  
  s_date_layer = text_layer_create(GRect(0, 60, 200, 30));
  text_layer_set_font(s_date_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_alignment(s_date_layer, GTextAlignmentCenter);
  layer_add_child(w_layer, text_layer_get_layer(s_date_layer));
  
  s_time_layer = text_layer_create(GRect(0, 84, 200, 60));
  text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_ROBOTO_BOLD_SUBSET_49));
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
  layer_add_child(w_layer, text_layer_get_layer(s_time_layer));
  
  s_goals_canvas = layer_create(GRect(15, 142, 72, 72));
  layer_set_update_proc(s_goals_canvas, goals_update_proc);
  layer_add_child(w_layer, s_goals_canvas);

  s_dist_layer = text_layer_create(GRect(12, 160, 76, 35));
  text_layer_set_font(s_dist_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_alignment(s_dist_layer, GTextAlignmentCenter);
  layer_add_child(w_layer, text_layer_get_layer(s_dist_layer));

  s_heart_layer = bitmap_layer_create(GRect(120, 146, 32, 32));
  s_heart_bitmap = gbitmap_create_with_resource(RESOURCE_ID_ICON_HEART);
  bitmap_layer_set_bitmap(s_heart_layer, s_heart_bitmap);
  layer_add_child(w_layer, bitmap_layer_get_layer(s_heart_layer));

  s_hr_layer = text_layer_create(GRect(145, 140, 55, 35));
  text_layer_set_font(s_hr_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_alignment(s_hr_layer, GTextAlignmentCenter);
  layer_add_child(w_layer, text_layer_get_layer(s_hr_layer));

  s_hr_graph_layer = layer_create(GRect(120, 190, 64, 20));
  layer_set_update_proc(s_hr_graph_layer, hr_graph_update_proc);
  layer_add_child(w_layer, s_hr_graph_layer);

  // Initialize History safely
  #if defined(PBL_HEALTH)
  for(int i = 0; i < HR_HISTORY_SIZE; i++) {
    s_hr_history[i] = 60; // Fallback baseline
  }
  
  time_t end_time = time(NULL);
  time_t start_time = end_time - (HR_HISTORY_SIZE * SECONDS_PER_MINUTE);
  HealthMinuteData minute_data[HR_HISTORY_SIZE];
  
  uint32_t num_records = health_service_get_minute_history(minute_data, HR_HISTORY_SIZE, &start_time, &end_time);
  
  if (num_records > 0) {
    int offset = HR_HISTORY_SIZE - num_records;
    int last_valid_hr = 60;
    
    // Process returned records
    for (uint32_t i = 0; i < num_records; i++) {
      int hr = minute_data[i].heart_rate_bpm;
      if (hr > 0) { last_valid_hr = hr; }
      if ((offset + i) < HR_HISTORY_SIZE) {
        s_hr_history[offset + i] = last_valid_hr;
      }
    }
    
    // Pad any missing data at the front of the array with the oldest known heart rate
    int oldest_hr = minute_data[0].heart_rate_bpm > 0 ? minute_data[0].heart_rate_bpm : 60;
    for (int i = 0; i < offset; i++) {
      s_hr_history[i] = oldest_hr;
    }
  }
  
  // Guarantee the final graph point perfectly matches current live reading
  int current_hr = (int)health_service_peek_current_value(HealthMetricHeartRateBPM);
  if (current_hr > 0) {
    s_hr_history[HR_HISTORY_SIZE - 1] = current_hr;
  }
  #endif

  s_battery_layer = layer_create(GRect(0, 222, 200, 6));
  layer_set_update_proc(s_battery_layer, battery_update_proc);
  layer_add_child(w_layer, s_battery_layer);

  load_weather_data();
  set_weather_icon(s_icon_code);
  update_theme_colors();
  time_t now = time(NULL);
  update_time(localtime(&now), MINUTE_UNIT);
  update_health();
}

static void main_window_unload(Window *window) {
  if (s_weather_bitmap) gbitmap_destroy(s_weather_bitmap);
  if (s_heart_bitmap) gbitmap_destroy(s_heart_bitmap);
  // COMMENTED OUT: description layer destroy
  // text_layer_destroy(s_weather_desc_layer);
}

static void inbox_received_callback(DictionaryIterator *iter, void *ctx) {
  Tuple *t = dict_read_first(iter);
  while(t) {
    if(t->key == MESSAGE_KEY_TEMP_CURRENT) s_cur = get_int(t);
    else if(t->key == MESSAGE_KEY_TEMP_HIGH) s_hi = get_int(t);
    else if(t->key == MESSAGE_KEY_TEMP_LOW) s_lo = get_int(t);
    else if(t->key == MESSAGE_KEY_CONDITIONS) set_weather_icon(t->value->cstring);
    else if(t->key == MESSAGE_KEY_STEP_GOAL) s_step_goal = get_int(t);
    else if(t->key == MESSAGE_KEY_ACTIVE_GOAL) s_active_goal = get_int(t);
    else if(t->key == MESSAGE_KEY_UPDATE_FREQ) s_update_freq = get_int(t);
    else if(t->key == MESSAGE_KEY_UNITS) s_use_metric = (get_int(t) == 1);
    else if(t->key == MESSAGE_KEY_UV_INDEX) s_uv = get_int(t);
    else if(t->key == MESSAGE_KEY_IS_NIGHT) s_is_night = (get_int(t) == 1);
    t = dict_read_next(iter);
  }
  s_weather_timestamp = time(NULL);
  save_weather_data(); 
  layer_mark_dirty(s_weather_arc_layer);
}

static void init() {
  s_main_window = window_create();
  window_set_window_handlers(s_main_window, (WindowHandlers) { .load = main_window_load, .unload = main_window_unload });
  window_stack_push(s_main_window, true);
  tick_timer_service_subscribe(MINUTE_UNIT, update_time);
  battery_state_service_subscribe(battery_callback);
  battery_callback(battery_state_service_peek());
  accel_tap_service_subscribe(tap_handler);
  app_message_register_inbox_received(inbox_received_callback);
  app_message_open(256, 256);
}

int main(void) { init(); app_event_loop(); }