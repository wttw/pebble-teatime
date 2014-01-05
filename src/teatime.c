#include <pebble.h>

static Window *window;
static Window *timer_window;
static MenuLayer *menu_layer;
static TextLayer *name_layer;
static TextLayer *content_layer;
static Layer *progress_layer;
static BitmapLayer *image_layer;

static int num_entries;
static int selected_entry;
static time_t time_start;
static int bar_width;
static int bar_total;

static int saved_current;

static AppTimer *timer;
static GBitmap *image;

#define STORAGE_VERSION 1

#define STORE_BASE 123400000
#define STORE_VERSION 1 + STORE_BASE
#define STORE_CURRENT 2 + STORE_BASE
#define STORE_SIZE 3 + STORE_BASE
#define STORE_ENTRY 4 + STORE_BASE

#define PROGRESS_MARGIN 2
#define MAX_ENTRIES 15

typedef struct tea_entry {
  uint8_t quarts_low;
  uint8_t quarts_high;
  char name[20];
  char content[30];
} __attribute__((__packed__)) tea_entry;

tea_entry entries[MAX_ENTRIES];

static uint16_t menu_get_num_rows_callback(MenuLayer *menu, uint16_t section_index, void *data)
{
  return num_entries;
}

static void menu_draw_row_callback(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data)
{
  tea_entry *t = entries + cell_index->row;
  menu_cell_basic_draw(ctx, cell_layer, t->name, t->content, NULL);
}

static void menu_select_callback(MenuLayer *menu, MenuIndex *cell_index, void *data)
{
  selected_entry = cell_index->row;
  window_stack_push(timer_window, true);
}

static void progress_paint_callback(Layer *layer, GContext *ctx)
{
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_draw_rect(ctx, bounds);
  /* Checks */
  int h = bounds.size.h - PROGRESS_MARGIN * 2;
  int mark_low = entries[selected_entry].quarts_low;
  int mark_high = entries[selected_entry].quarts_high;

  graphics_fill_rect(ctx, GRect(PROGRESS_MARGIN, PROGRESS_MARGIN, bar_width, h), 0, GCornerNone);

  for(int i=1; i<mark_high; ++i) {
    int x = PROGRESS_MARGIN + (bar_total * i) / mark_high;
    if(x >= bar_width) { /* Why draw it, if unneeded */
      int size = (i == mark_low) ? 0 : ((i % 4) ? 7 * h / 16 : h / 4);
      graphics_draw_line(ctx, GPoint(x, size+2), GPoint(x, bounds.size.h - size - 3));
    }
  }
}

static void timer_callback(void *data)
{
  time_t now;
  uint16_t nowms;
  time_ms(&now, &nowms);
  int mark_high = entries[selected_entry].quarts_high;
  
  int elapsedms = nowms + 1000 * (now - time_start);

  bar_width = (bar_total * elapsedms) / (mark_high * 15000);
  if(bar_width >= bar_total) {
    bar_width = bar_total;
    timer = 0;
    vibes_short_pulse();
  } else {
    int nextms = ((bar_width+1) * mark_high * 15000) / bar_total - elapsedms;
    if(nextms <= 0) {
      nextms = 1;
    }
    int mark_low = entries[selected_entry].quarts_low;
    if(elapsedms < mark_low * 15000 && nextms + elapsedms >= mark_low * 15000) {
      vibes_short_pulse();
    }
    app_timer_register(nextms, timer_callback, 0);
  }

  layer_mark_dirty(progress_layer);
}

static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  menu_layer = menu_layer_create(bounds);

  menu_layer_set_callbacks(menu_layer, 0, (MenuLayerCallbacks){
      .get_num_rows = menu_get_num_rows_callback,
        .draw_row = menu_draw_row_callback,
        .select_click = menu_select_callback
    });

  menu_layer_set_click_config_onto_window(menu_layer, window);
  layer_add_child(window_layer, menu_layer_get_layer(menu_layer));
}

static void window_unload(Window *window) {
  menu_layer_destroy(menu_layer);
}

static void timer_window_load(Window *window) {
  window_set_background_color(window, GColorBlack);
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_frame(window_layer);

  image = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_TEA);
  image_layer = bitmap_layer_create(bounds);
  bitmap_layer_set_bitmap(image_layer, image);
  bitmap_layer_set_alignment(image_layer, GAlignCenter);
  bitmap_layer_set_compositing_mode(image_layer, GCompOpAssignInverted);
  layer_add_child(window_layer, bitmap_layer_get_layer(image_layer));

  name_layer = text_layer_create(GRect(0, 10, bounds.size.w, 28));
  text_layer_set_font(name_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_alignment(name_layer, GTextAlignmentCenter);
  text_layer_set_background_color(name_layer, GColorBlack);
  text_layer_set_text_color(name_layer, GColorWhite);
  layer_add_child(window_layer, text_layer_get_layer(name_layer));

  content_layer = text_layer_create(GRect(0, 36, bounds.size.w, 28));
  text_layer_set_font(content_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(content_layer, GTextAlignmentCenter);
  text_layer_set_background_color(content_layer, GColorBlack);
  text_layer_set_text_color(content_layer, GColorWhite);
  layer_add_child(window_layer, text_layer_get_layer(content_layer));

  bar_total = bounds.size.w - 10 - 2 * PROGRESS_MARGIN;
  progress_layer = layer_create(GRect(5, 100, bounds.size.w-10, 40));
  layer_set_update_proc(progress_layer, progress_paint_callback);
  layer_add_child(window_layer, progress_layer);

  
}

static void timer_window_appear(Window *window) {
  tea_entry *entry = entries + selected_entry;
  text_layer_set_text(name_layer, entry->name);
  text_layer_set_text(content_layer, entry->content);
  bar_width = 0;
  time_ms(&time_start, 0); /* use instead of time() in case they ever differ */
  timer = app_timer_register(1, timer_callback, 0);
}

static void timer_window_disappear(Window *window) {
  if(timer) {
    app_timer_cancel(timer);
    timer = 0;
  }
}

static void timer_window_unload(Window *window) {
  text_layer_destroy(name_layer);
  text_layer_destroy(content_layer);
  layer_destroy(progress_layer);
  bitmap_layer_destroy(image_layer);
  gbitmap_destroy(image);
}

static void message_received(DictionaryIterator *iter, void *context)
{
  Tuple *tp = dict_find(iter, 0);
  if(!tp || tp->type != TUPLE_CSTRING) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Invalid message");
    return;
  }
  if(tp->value->cstring[0] == 'u') {
    /* Single config entry */
    int idx;
    int count;

    tp = dict_find(iter, 1);
    if(!tp) { return; }
    idx = tp->value->int32;

    if(idx >= MAX_ENTRIES) {
      return;
    }

    tp = dict_find(iter, 2);
    if(!tp) { return; }
    count = tp->value->int32;

    tp = dict_find(iter, 3);
    if(!tp) { return; }
    strncpy(entries[idx].name, tp->value->cstring, sizeof(entries[0].name));

    tp = dict_find(iter, 4);
    if(!tp) { return; }
    strncpy(entries[idx].content, tp->value->cstring, sizeof(entries[0].content));

    tp = dict_find(iter, 5);
    if(!tp) { return; }
    int lo = tp->value->int32;
    
    tp = dict_find(iter, 6);
    if(!tp) { return; }
    int hi = tp->value->int32;
    if(lo > hi) {
      hi = lo;
    }
    entries[idx].quarts_low = lo;
    entries[idx].quarts_high = hi;

    if(idx >= num_entries || idx+1 == count) {
      num_entries = idx + 1;
    }
    menu_layer_reload_data(menu_layer);
    if(saved_current >=0 && saved_current < num_entries) {
      menu_layer_set_selected_index(menu_layer, (MenuIndex){.row=saved_current, .section=0}, MenuRowAlignCenter, false);
    }
    layer_mark_dirty(menu_layer_get_layer(menu_layer));
  }
}

static void message_dropped(AppMessageResult reason, void *context)
{
  /*  APP_LOG(APP_LOG_LEVEL_DEBUG, "Message dropped %d", reason); */
}

static void init(void) {
  num_entries = 0;
  for(int i=0; i<MAX_ENTRIES; ++i) {
    strcpy(entries[i].name, "invalid");
    strcpy(entries[i].content, "invalid");
    entries[i].quarts_low = 4;
    entries[i].quarts_high = 4;
  }
#ifdef USE_PERSIST
  int ret;
  if(!persist_exists(STORE_VERSION) ||
     STORAGE_VERSION != persist_read_int(STORE_VERSION)) {
    // FIXME - pull from app, instead
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Setting app config");
    ret = persist_write_int(STORE_VERSION, STORAGE_VERSION);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "STORE_VERSION: %d", ret);
    num_entries = sizeof(default_entries) / sizeof(default_entries[0]);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "num_entries here is %d", num_entries);
    ret = persist_write_int(STORE_SIZE, num_entries);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "STORE_SIZE: %d", ret);

    for(int i=0; i<num_entries; ++i) {
      persist_write_data(STORE_ENTRY+i, default_entries+i, sizeof(tea_entry));
    }
  }
  num_entries = persist_read_int(STORE_SIZE);
  entries = malloc(num_entries * sizeof(tea_entry));
  for(int i=0; i<num_entries; ++i) {
    persist_read_data(STORE_ENTRY+i, entries+i, sizeof(tea_entry));
  }
#else
  saved_current = -1;
  if(persist_exists(STORE_CURRENT)) {
    saved_current = persist_read_int(STORE_CURRENT);
  }
  /*  APP_LOG(APP_LOG_LEVEL_DEBUG, "Reading config from phone"); */
  app_message_register_inbox_received(message_received);
  app_message_register_inbox_dropped(message_dropped);
  app_message_open(app_message_inbox_size_maximum(), 100);
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  dict_write_cstring(iter, 0, "c");
  app_message_outbox_send();
#endif

  
  window = window_create();
  window_set_window_handlers(window, (WindowHandlers) {
      .load = window_load,
        .unload = window_unload,
  });

  timer_window = window_create();
  window_set_window_handlers(timer_window, (WindowHandlers) {
      .load = timer_window_load,
        .appear = timer_window_appear,
        .disappear = timer_window_disappear,
        .unload = timer_window_unload,
  });

  window_stack_push(window, true);
}

static void deinit(void) {
  window_stack_pop_all(false);
  window_destroy(window);
  window_destroy(timer_window);
  persist_write_int(STORE_CURRENT, selected_entry);
}

int main(void) {
  init();


  app_event_loop();
  deinit();
}
