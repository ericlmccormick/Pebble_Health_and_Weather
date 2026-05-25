#include <pebble.h> // Includes the standard Pebble SDK library required for all Pebble apps

// --- Message Keys ---
// These define the integer keys used to communicate with the index.js file.
// They must match the keys configured in your package.json.
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
// These define the storage slots used to save data locally on the watch.
// This allows the watchface to display data immediately upon loading before the phone connects.
#define PERSIST_KEY_CUR 10
#define PERSIST_KEY_HI 11
#define PERSIST_KEY_LO 12
#define PERSIST_KEY_ICON 13
#define PERSIST_KEY_UV 14
#define PERSIST_KEY_IS_NIGHT 15

// --- UI Elements ---
// Pointers to the various layers that make up the visual interface.
static Window *s_main_window; // The main application window
static TextLayer *s_time_layer, *s_date_layer, *s_hr_layer, *s_dist_layer; // Layers for displaying text
static Layer *s_weather_arc_layer, *s_goals_canvas; // Custom drawing layers for arcs and rings
static BitmapLayer *s_weather_icon_layer, *s_heart_layer; // Layers for displaying static images
static GBitmap *s_weather_bitmap = NULL, *s_heart_bitmap = NULL; // The actual image data loaded into memory
static Layer *s_battery_layer; // Custom drawing layer for the battery bar

// --- State Variables ---
// Variables that hold the current data values displayed on the screen.
static int s_cur = 0, s_hi = 0, s_lo = 0, s_uv = 0; // Weather metrics
static int s_step_goal = 10000, s_active_goal = 30, s_update_freq = 30, s_battery_level = 100; // Goals and system metrics
static bool s_is_24h = false, s_connected = true, s_use_metric = false, s_is_night = false; // Toggles and system states

// Character buffers used to format integers into text strings for the TextLayers.
static char s_time_buf[10], s_cur_buf[8], s_hi_buf[8], s_lo_buf[8], s_hr_buf[8], s_dist_buf[16];
static char s_icon_code[4] = "01d"; // Stores the OpenWeatherMap icon code
static time_t s_weather_timestamp = 0; // Tracks the last time weather was updated

// --- Heart Graph Variables ---
#define HR_HISTORY_SIZE 20 // The number of data points in the heart rate graph
static int s_hr_history[HR_HISTORY_SIZE]; // Array storing the last 20 heart rate readings
static Layer *s_hr_graph_layer; // Custom drawing layer for the sparkline graph

// --- Memory-Safe Tuple Reader ---
// A helper function to safely extract an integer from a Pebble Dictionary Tuple,
// regardless of whether the JavaScript sent it as an 8-bit, 16-bit, 32-bit, or string type.
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
      return atoi(t->value->cstring); // Converts a string to an integer
    }
  }
  return 0; // Returns 0 if the tuple is missing or unreadable
}

// --- Persistence and Update Logic ---

// Sends a blank message to the phone to trigger the index.js file to fetch new weather.
static void request_weather() {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
    dict_write_uint8(iter, 0, 0); 
    app_message_outbox_send();
  }
}

// Writes the current weather variables into the watch's permanent local storage.
static void save_weather_data() {
  persist_write_int(PERSIST_KEY_CUR, s_cur);
  persist_write_int(PERSIST_KEY_HI, s_hi);
  persist_write_int(PERSIST_KEY_LO, s_lo);
  persist_write_string(PERSIST_KEY_ICON, s_icon_code);
  persist_write_int(PERSIST_KEY_UV, s_uv);
  persist_write_bool(PERSIST_KEY_IS_NIGHT, s_is_night);
}

// Reads saved weather variables from local storage when the watchface first launches.
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

// Handler triggered when the user flicks their wrist. Forces a weather update.
static void tap_handler(AccelAxisType axis, int32_t direction) {
  request_weather();
}

// --- Custom Drawing Functions ---

// Draws the top-right weather complication (Temperatures and UV Arc)
static void weather_arc_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer); // Gets the size of the drawing area
  graphics_context_set_text_color(ctx, GColorBlack); // Sets text color
  
  // Format and draw the Low and High temperatures
  snprintf(s_lo_buf, sizeof(s_lo_buf), "%d", s_lo);
  snprintf(s_hi_buf, sizeof(s_hi_buf), "%d", s_hi);
  graphics_draw_text(ctx, s_lo_buf, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GRect(15, 35, 35, 22), GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
  graphics_draw_text(ctx, s_hi_buf, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GRect(bounds.size.w - 50, 35, 35, 22), GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);
  
  // Format and draw the Current temperature in the center
  snprintf(s_cur_buf, sizeof(s_cur_buf), "%d", s_cur);
  graphics_draw_text(ctx, s_cur_buf, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD), GRect(0, 15, bounds.size.w, 35), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  
  // Defines the boundary for the background arc
  GRect arc_bounds = grect_inset(bounds, GEdgeInsets(14, 14, 28, 14));
  graphics_context_set_stroke_width(ctx, 4); // Arc thickness
  graphics_context_set_stroke_color(ctx, GColorLightGray); // Arc color
  // Draws the static background arc from 260 degrees to 460 degrees (a 200-degree sweep)
  graphics_draw_arc(ctx, arc_bounds, GOvalScaleModeFitCircle, DEG_TO_TRIGANGLE(260), DEG_TO_TRIGANGLE(460));
  
  // If Bluetooth is connected, calculate and draw the UV dot's position on the arc
  if (s_connected) {
    int range = s_hi - s_lo; // Total temperature range for the day
    float percent = 0.5f; // Default dot placement to the middle
    
    // Calculates where the current temp sits within today's range as a percentage
    if (range > 0) percent = (float)(s_cur - s_lo) / (float)range;
    if (percent < 0.0f) percent = 0.0f; // Clamp to 0% minimum
    if (percent > 1.0f) percent = 1.0f; // Clamp to 100% maximum
    
    // Maps the percentage to an angle along the 200-degree arc
    int angle = 260 + (int)(percent * 200.0f);
    GPoint center = grect_center_point(&arc_bounds); // Center of the arc
    int16_t radius = (arc_bounds.size.w / 2) - 8; // Distance from center to draw the dot
    
    // Trigonometry to find the exact X/Y coordinate on the arc based on the calculated angle
    GPoint dot = {
      (int16_t)(center.x + (sin_lookup(DEG_TO_TRIGANGLE(angle)) * radius / TRIG_MAX_RATIO)), 
      (int16_t)(center.y - (cos_lookup(DEG_TO_TRIGANGLE(angle)) * radius / TRIG_MAX_RATIO))
    };
    
    // Determine the dot's color based on the UV Index
    GColor uv_color = GColorBlack; 
    if (!s_is_night && s_uv > 0) {
      if (s_uv <= 2) uv_color = GColorKellyGreen;
      else if (s_uv <= 5) uv_color = GColorYellow;
      else if (s_uv <= 7) uv_color = GColorOrange;
      else if (s_uv <= 10) uv_color = GColorRed;
      else uv_color = GColorPurple;
    }
    
    // Draw the dot at the calculated coordinate
    graphics_context_set_fill_color(ctx, uv_color);
    graphics_fill_circle(ctx, dot, 5); 
  }
}

// Draws the bottom-left Activity Rings (Steps and Active Minutes)
static void goals_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  GRect draw_bounds = grect_inset(bounds, GEdgeInsets(3));
  
  // Retrieve today's health metrics from the Pebble HealthService
  int steps = (int)health_service_sum_today(HealthMetricStepCount);
  int active = (int)health_service_sum_today(HealthMetricActiveSeconds) / 60; // Convert seconds to minutes
  
  graphics_context_set_stroke_width(ctx, 5); // Ring thickness
  
  // Draw the background outer ring
  graphics_context_set_stroke_color(ctx, GColorLightGray);
  graphics_draw_arc(ctx, draw_bounds, GOvalScaleModeFitCircle, 0, TRIG_MAX_ANGLE);
  
  // Draw the Active Minutes progress ring (Outer Ring)
  graphics_context_set_stroke_color(ctx, GColorKellyGreen);
  graphics_draw_arc(ctx, draw_bounds, GOvalScaleModeFitCircle, 0, (s_active_goal > 0) ? (active * TRIG_MAX_ANGLE) / s_active_goal : 0);
  
  // Draw the Steps progress ring (Inner Ring, inset by 7 pixels)
  graphics_context_set_stroke_color(ctx, GColorPictonBlue);
  graphics_draw_arc(ctx, grect_inset(draw_bounds, GEdgeInsets(7)), GOvalScaleModeFitCircle, 0, (s_step_goal > 0) ? (steps * TRIG_MAX_ANGLE) / s_step_goal : 0);
}

// Draws the bottom-right Heart Rate Sparkline graph
static void hr_graph_update_proc(Layer *layer, GContext *ctx) {
  #if defined(PBL_HEALTH) // Only compile if the watch supports Pebble Health
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_stroke_width(ctx, 2); // Line thickness
  
  int step_x = bounds.size.w / HR_HISTORY_SIZE; // Distance in pixels between each point on the X axis
  
  // Loop through the history array and draw line segments between points
  for(int i = 0; i < HR_HISTORY_SIZE - 1; i++) {
    // Calculates the Y coordinate. 55 is subtracted as a baseline, and it's divided by 2 to scale it to the layer height.
    int y1 = bounds.size.h - ((s_hr_history[i] - 55) / 2);
    int y2 = bounds.size.h - ((s_hr_history[i+1] - 55) / 2);
    
    // Clamp values so lines don't draw outside the layer boundaries
    if(y1 < 2) { y1 = 2; } 
    if(y1 > bounds.size.h) { y1 = bounds.size.h; }
    if(y2 < 2) { y2 = 2; } 
    if(y2 > bounds.size.h) { y2 = bounds.size.h; }
    
    // Calculate average HR of this specific line segment to assign a color
    int avg_hr = (s_hr_history[i] + s_hr_history[i+1]) / 2;
    GColor line_color = GColorKellyGreen; // Default: Normal
    if (avg_hr < 70) line_color = GColorPictonBlue; // Resting
    else if (avg_hr > 120) line_color = GColorRed; // High
    else if (avg_hr > 95) line_color = GColorChromeYellow; // Elevated

    graphics_context_set_stroke_color(ctx, line_color);
    
    // Draw the segment from point A to point B
    graphics_draw_line(ctx, GPoint(i * step_x, y1), GPoint((i+1) * step_x, y2));
  }
  #endif
}

// Ensures proper contrast for all text and backgrounds
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
  bitmap_layer_set_compositing_mode(s_weather_icon_layer, GCompOpSet); // Ensures transparency
  bitmap_layer_set_background_color(s_heart_layer, GColorClear);
  bitmap_layer_set_compositing_mode(s_heart_layer, GCompOpSet); // Ensures transparency
}

// Maps the OpenWeatherMap icon code string to a specific Pebble image resource
static void set_weather_icon(char *icon_code) {
  // If an icon already exists in memory, destroy it to prevent memory leaks before loading a new one
  if (s_weather_bitmap) { gbitmap_destroy(s_weather_bitmap); s_weather_bitmap = NULL; }
  strncpy(s_icon_code, icon_code, sizeof(s_icon_code)); 
  
  uint32_t res_id = RESOURCE_ID_ICON_CLOUDY; // Default fallback icon

  if (!s_connected) {
    res_id = RESOURCE_ID_ICON_HAZE; // Show haze if Bluetooth is disconnected
  } else {
    // String matching to assign the correct icon ID
    if (strcmp(icon_code, "01d") == 0) { res_id = RESOURCE_ID_ICON_CLEAR_DAY; }
    else if (strcmp(icon_code, "01n") == 0) { res_id = RESOURCE_ID_ICON_CLEAR_NIGHT; }
    else if (strcmp(icon_code, "02d") == 0) { res_id = RESOURCE_ID_ICON_CLOUDY_SUN; }
    else if (strcmp(icon_code, "02n") == 0) { res_id = RESOURCE_ID_ICON_CLOUDY_NIGHT; }
    else if (strcmp(icon_code, "03d") == 0 || strcmp(icon_code, "03n") == 0) { res_id = RESOURCE_ID_ICON_CLOUDY; }
    else if (strcmp(icon_code, "04d") == 0 || strcmp(icon_code, "04n") == 0) { res_id = RESOURCE_ID_ICON_CLOUDY; }
    else if (strcmp(icon_code, "09d") == 0 || strcmp(icon_code, "09n") == 0) { res_id = RESOURCE_ID_ICON_RAIN; }
    else if (strcmp(icon_code, "10d") == 0 || strcmp(icon_code, "10n") == 0) { res_id = RESOURCE_ID_ICON_RAIN; }
    else if (strcmp(icon_code, "11d") == 0 || strcmp(icon_code, "11n") == 0) { res_id = RESOURCE_ID_ICON_THUNDER_SHOWER; }
    else if (strcmp(icon_code, "13d") == 0 || strcmp(icon_code, "13n") == 0) { res_id = RESOURCE_ID_ICON_SNOW; }
    else if (strcmp(icon_code, "50d") == 0 || strcmp(icon_code, "50n") == 0) { res_id = RESOURCE_ID_ICON_HAZE; }
  }

  // Load the newly determined icon into memory and set it to the layer
  s_weather_bitmap = gbitmap_create_with_resource(res_id);
  bitmap_layer_set_bitmap(s_weather_icon_layer, s_weather_bitmap);
}

// Called every minute to update the clock and shift data structures
static void update_time(struct tm *tick_time, TimeUnits units_changed) {
  int hour = tick_time->tm_hour;
  if (!s_is_24h) { hour %= 12; if (hour == 0) hour = 12; } // Convert 24hr to 12hr format if needed
  
  // Format the time string
  snprintf(s_time_buf, sizeof(s_time_buf), s_is_24h ? "%02d:%02d" : "%d:%02d", hour, tick_time->tm_min);
  text_layer_set_text(s_time_layer, s_time_buf);
  
  // Format the date string (e.g., "Mon, Jan 01")
  static char d_buf[16]; strftime(d_buf, sizeof(d_buf), "%a, %b %d", tick_time);
  text_layer_set_text(s_date_layer, d_buf);
  
  // Request fresh weather data from the phone every 30 minutes
  if (tick_time->tm_min % 30 == 0) request_weather();

  // Shift the graph history array left by one index to make room for the new reading
  #if defined(PBL_HEALTH)
  for(int i = 0; i < HR_HISTORY_SIZE - 1; i++) {
    s_hr_history[i] = s_hr_history[i+1];
  }
  // Add the current live reading to the end of the array
  s_hr_history[HR_HISTORY_SIZE - 1] = (int)health_service_peek_current_value(HealthMetricHeartRateBPM);
  // Tell the OS to redraw the graph layer
  layer_mark_dirty(s_hr_graph_layer);
  #endif
}

// Retrieves and formats live health statistics (Called regularly)
static void update_health() {
  #if defined(PBL_HEALTH)
  // Fetch current BPM
  int bpm = (int)health_service_peek_current_value(HealthMetricHeartRateBPM);
  snprintf(s_hr_buf, sizeof(s_hr_buf), "%d", bpm);
  text_layer_set_text(s_hr_layer, s_hr_buf);
  
  // Fetch total distance walked today in meters
  int dist_m = (int)health_service_sum_today(HealthMetricWalkedDistanceMeters);
  // Convert to kilometers or miles based on user settings, multiplied by 100 to preserve decimals without floats
  int dist_100th = s_use_metric ? (dist_m * 100) / 1000 : (dist_m * 100) / 1609;
  
  // Format the string to include the decimal (e.g., "3.4")
  snprintf(s_dist_buf, sizeof(s_dist_buf), "%d.%01d", dist_100th / 100, (dist_100th % 100) / 10);
  text_layer_set_text(s_dist_layer, s_dist_buf);

  // Trigger a redraw of the activity rings to reflect new steps/distance
  layer_mark_dirty(s_goals_canvas);
  #endif
}

// Updates the state variable when the battery percentage changes
static void battery_callback(BatteryChargeState state) {
  s_battery_level = state.charge_percent;
  layer_mark_dirty(s_battery_layer); // Triggers the draw function below
}

// Draws the thin battery indicator line at the bottom of the screen
static void battery_update_proc(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  // Turns red if battery is 20% or below
  graphics_context_set_fill_color(ctx, s_battery_level > 20 ? GColorKellyGreen : GColorRed);
  // Draws a rectangle whose width is a percentage of the total screen width
  graphics_fill_rect(ctx, GRect(0, 0, (s_battery_level * b.size.w) / 100, b.size.h), 0, GCornerNone);
}

// --- App Lifecycle Functions ---

// Called once when the app starts. Initializes all UI layers and memory.
static void main_window_load(Window *window) {
  Layer *w_layer = window_get_root_layer(window);
  
  // Top-Left: Static Weather Icon Layer
  s_weather_icon_layer = bitmap_layer_create(GRect(15, 0, 64, 64));
  layer_add_child(w_layer, bitmap_layer_get_layer(s_weather_icon_layer));
  
  // Top-Right: Custom Arc Drawing Layer
  s_weather_arc_layer = layer_create(GRect(105, 0, 90, 85));
  layer_set_update_proc(s_weather_arc_layer, weather_arc_update_proc); // Assign drawing function
  layer_add_child(w_layer, s_weather_arc_layer);
  
  // Center: Date Text
  s_date_layer = text_layer_create(GRect(0, 60, 200, 30));
  text_layer_set_font(s_date_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_alignment(s_date_layer, GTextAlignmentCenter);
  layer_add_child(w_layer, text_layer_get_layer(s_date_layer));
  
  // Center: Time Text
  s_time_layer = text_layer_create(GRect(0, 84, 200, 60));
  text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_ROBOTO_BOLD_SUBSET_49));
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
  layer_add_child(w_layer, text_layer_get_layer(s_time_layer));
  
  // Bottom-Left: Activity Rings Layer
  s_goals_canvas = layer_create(GRect(15, 142, 72, 72));
  layer_set_update_proc(s_goals_canvas, goals_update_proc); // Assign drawing function
  layer_add_child(w_layer, s_goals_canvas);

  // Bottom-Left: Walked Distance Text
  s_dist_layer = text_layer_create(GRect(12, 160, 76, 35));
  text_layer_set_font(s_dist_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_alignment(s_dist_layer, GTextAlignmentCenter);
  layer_add_child(w_layer, text_layer_get_layer(s_dist_layer));

  // Bottom-Right: Static Heart Icon
  s_heart_layer = bitmap_layer_create(GRect(120, 146, 32, 32));
  s_heart_bitmap = gbitmap_create_with_resource(RESOURCE_ID_ICON_HEART);
  bitmap_layer_set_bitmap(s_heart_layer, s_heart_bitmap);
  layer_add_child(w_layer, bitmap_layer_get_layer(s_heart_layer));

  // Bottom-Right: Heart Rate Text
  s_hr_layer = text_layer_create(GRect(145, 140, 55, 35));
  text_layer_set_font(s_hr_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_alignment(s_hr_layer, GTextAlignmentCenter);
  layer_add_child(w_layer, text_layer_get_layer(s_hr_layer));

  // Bottom-Right: Sparkline Graph Layer
  s_hr_graph_layer = layer_create(GRect(120, 190, 64, 20));
  layer_set_update_proc(s_hr_graph_layer, hr_graph_update_proc); // Assign drawing function
  layer_add_child(w_layer, s_hr_graph_layer);

  // Initialize the Graph History Array
  #if defined(PBL_HEALTH)
  for(int i = 0; i < HR_HISTORY_SIZE; i++) {
    s_hr_history[i] = 60; // Set fallback baseline for array elements
  }
  
  // Calculate timestamps to request the last 20 minutes of background data from OS
  time_t end_time = time(NULL);
  time_t start_time = end_time - (HR_HISTORY_SIZE * SECONDS_PER_MINUTE);
  HealthMinuteData minute_data[HR_HISTORY_SIZE];
  
  // Fetch the data from Pebble HealthService
  uint32_t num_records = health_service_get_minute_history(minute_data, HR_HISTORY_SIZE, &start_time, &end_time);
  
  // If data was returned, process it into the local drawing array
  if (num_records > 0) {
    int offset = HR_HISTORY_SIZE - num_records; // Calculate gap if fewer than 20 records exist
    int last_valid_hr = 60;
    
    // Loop through the returned records
    for (uint32_t i = 0; i < num_records; i++) {
      int hr = minute_data[i].heart_rate_bpm;
      if (hr > 0) { last_valid_hr = hr; } // Update valid HR (ignores 0s caused by taking watch off)
      if ((offset + i) < HR_HISTORY_SIZE) {
        s_hr_history[offset + i] = last_valid_hr; // Insert into drawing array
      }
    }
    
    // Pad any missing older data with the oldest valid record so the line doesn't drop to 0
    int oldest_hr = minute_data[0].heart_rate_bpm > 0 ? minute_data[0].heart_rate_bpm : 60;
    for (int i = 0; i < offset; i++) {
      s_hr_history[i] = oldest_hr;
    }
  }
  
  // Explicitly fetch the immediate live reading to cap the end of the graph perfectly
  int current_hr = (int)health_service_peek_current_value(HealthMetricHeartRateBPM);
  if (current_hr > 0) {
    s_hr_history[HR_HISTORY_SIZE - 1] = current_hr;
  }
  #endif

  // Bottom Edge: Battery Bar Layer
  s_battery_layer = layer_create(GRect(0, 222, 200, 6));
  layer_set_update_proc(s_battery_layer, battery_update_proc);
  layer_add_child(w_layer, s_battery_layer);

  // Initial State Setup
  load_weather_data(); // Attempt to load saved data
  set_weather_icon(s_icon_code); // Set initial icon
  update_theme_colors(); // Apply UI colors
  time_t now = time(NULL);
  update_time(localtime(&now), MINUTE_UNIT); // Trigger initial time format
  update_health(); // Trigger initial health format
}

// Called when the app closes. Destroys elements to prevent memory leaks.
static void main_window_unload(Window *window) {
  if (s_weather_bitmap) gbitmap_destroy(s_weather_bitmap);
  if (s_heart_bitmap) gbitmap_destroy(s_heart_bitmap);
}

// Callback triggered whenever a message (like weather updates) is received from index.js via Bluetooth
static void inbox_received_callback(DictionaryIterator *iter, void *ctx) {
  Tuple *t = dict_read_first(iter); // Start reading the dictionary
  while(t) {
    // Match the incoming key to our defined constants and update the corresponding state variable
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
    t = dict_read_next(iter); // Move to next item in dictionary
  }
  s_weather_timestamp = time(NULL); // Note the update time
  save_weather_data(); // Persist the new data to local storage
  layer_mark_dirty(s_weather_arc_layer); // Force the watch to redraw the weather arc with new data
}

// Primary initialization sequence for the watchface
static void init() {
  s_main_window = window_create(); // Allocate window
  // Link load/unload lifecycle handlers
  window_set_window_handlers(s_main_window, (WindowHandlers) { .load = main_window_load, .unload = main_window_unload });
  window_stack_push(s_main_window, true); // Push window to the screen
  
  // Subscribe to system services
  tick_timer_service_subscribe(MINUTE_UNIT, update_time); // Call update_time every minute
  battery_state_service_subscribe(battery_callback); // Listen for battery changes
  battery_callback(battery_state_service_peek()); // Get immediate battery state
  accel_tap_service_subscribe(tap_handler); // Listen for wrist flicks
  app_message_register_inbox_received(inbox_received_callback); // Listen for Bluetooth messages
  app_message_open(256, 256); // Open Bluetooth buffer (256 bytes in/out)
}

// Program entry point
int main(void) { 
  init(); 
  app_event_loop(); // Enter the main waiting loop for system events
}