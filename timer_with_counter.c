#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>

#include <stdio.h>

#define DEFAULT_SECONDS (5U * 60U)
#define MAX_HOURS 99U
#define COUNT_PATH APP_DATA_PATH("count.bin")
#define COUNT_TMP_PATH APP_DATA_PATH("count.tmp")
#define COUNT_MAGIC 0x45585054U
#define SETTINGS_V2 0x00000002U

// Four short high beeps with vibration, like the linked countdown timer.
static const NotificationSequence sequence_timer_finished = {
    &message_display_backlight_on,
    &message_vibro_on,
    &message_note_c8,
    &message_delay_50,
    &message_sound_off,
    &message_delay_50,
    &message_note_c8,
    &message_delay_50,
    &message_sound_off,
    &message_delay_50,
    &message_note_c8,
    &message_delay_50,
    &message_sound_off,
    &message_delay_50,
    &message_note_c8,
    &message_delay_50,
    &message_sound_off,
    &message_vibro_off,
    NULL,
};

typedef enum {
    TimerReady,
    TimerRunning,
    TimerPaused,
    TimerFinished,
} TimerState;

typedef struct {
    FuriMessageQueue* queue;
    TimerState state;
    uint32_t remaining_ticks;
    uint32_t started_tick;
    uint32_t count;
    uint32_t duration_seconds;
    uint8_t selected_field;
    bool save_ok;
} ExperimentTimer;

static uint32_t timer_full_duration(const ExperimentTimer* app) {
    return furi_ms_to_ticks(app->duration_seconds * 1000U);
}

static uint32_t timer_remaining(const ExperimentTimer* app, uint32_t now) {
    if(app->state != TimerRunning) return app->remaining_ticks;
    uint32_t elapsed = now - app->started_tick; // unsigned subtraction handles tick wrap
    return elapsed >= app->remaining_ticks ? 0 : app->remaining_ticks - elapsed;
}

static void timer_read_settings(ExperimentTimer* app) {
    uint32_t data[4] = {0};
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool opened = storage_file_open(file, COUNT_PATH, FSAM_READ, FSOM_OPEN_EXISTING);
    size_t bytes_read = opened ? storage_file_read(file, data, sizeof(data)) : 0;
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    if(bytes_read >= 2 * sizeof(uint32_t) && data[0] == COUNT_MAGIC) {
        app->count = data[1];
        // Earlier versions stored either count only or count plus whole minutes.
        if(bytes_read == sizeof(data) && data[3] == SETTINGS_V2 &&
           data[2] >= 1 && data[2] <= MAX_HOURS * 3600U + 3599U) {
            app->duration_seconds = data[2];
        } else if(bytes_read == 3 * sizeof(uint32_t) && data[2] >= 1 && data[2] <= 60) {
            app->duration_seconds = data[2] * 60U;
        }
    }
}

static bool timer_write_settings(const ExperimentTimer* app) {
    const uint32_t data[4] = {COUNT_MAGIC, app->count, app->duration_seconds, SETTINGS_V2};
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool opened = storage_file_open(file, COUNT_TMP_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    bool written = opened && storage_file_write(file, data, sizeof(data)) == sizeof(data);
    bool closed = storage_file_close(file);
    storage_file_free(file);
    bool saved = written && closed &&
                 storage_common_rename(storage, COUNT_TMP_PATH, COUNT_PATH) == FSE_OK;
    furi_record_close(RECORD_STORAGE);
    return saved;
}

static void timer_draw(Canvas* canvas, void* context) {
    ExperimentTimer* app = context;
    uint32_t ticks = timer_remaining(app, furi_get_tick());
    uint32_t hz = furi_kernel_get_tick_frequency();
    uint32_t seconds = (ticks + hz - 1) / hz;
    char time_text[16];
    char count_text[32];

    snprintf(time_text, sizeof(time_text), "%02lu:%02lu:%02lu",
             (unsigned long)(seconds / 3600), (unsigned long)((seconds / 60) % 60),
             (unsigned long)(seconds % 60));
    snprintf(count_text, sizeof(count_text), "Completed: %lu", (unsigned long)app->count);

    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 9, count_text);

    canvas_set_font(canvas, FontBigNumbers);
    canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, time_text);
    if(app->state == TimerReady || app->state == TimerFinished) {
        static const uint8_t underline_x[3] = {17, 54, 89};
        static const uint8_t underline_width[3] = {24, 21, 24};
        canvas_draw_box(
            canvas, underline_x[app->selected_field], 43,
            underline_width[app->selected_field], 2);
    }

    canvas_set_font(canvas, FontSecondary);
    const char* status = "OK Start  L/R: field";
    if(app->state == TimerRunning) status = "Running | OK Pause";
    if(app->state == TimerPaused) status = "Paused | OK Resume";
    if(app->state == TimerFinished) status = "Done! OK Next run";
    canvas_draw_str(canvas, 2, 55, status);
    const char* hint = "Up/Down: adjust time";
    if(app->state == TimerRunning) hint = "Right: restart  Back: exit";
    if(app->state == TimerPaused) hint = "Hold OK: clear count";
    canvas_draw_str(canvas, 2, 63, app->save_ok ? hint : "Save failed: check SD card");
}

static void timer_input(InputEvent* event, void* context) {
    ExperimentTimer* app = context;
    furi_message_queue_put(app->queue, event, 0);
}

int32_t experiment_timer_main(void* context) {
    UNUSED(context);
    ExperimentTimer app = {
        .queue = furi_message_queue_alloc(8, sizeof(InputEvent)),
        .state = TimerReady,
        .duration_seconds = DEFAULT_SECONDS,
        .selected_field = 1,
        .save_ok = true,
    };
    timer_read_settings(&app);
    app.remaining_ticks = timer_full_duration(&app);

    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, timer_draw, &app);
    view_port_input_callback_set(view_port, timer_input, &app);
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);
    NotificationApp* notification = furi_record_open(RECORD_NOTIFICATION);

    bool running = true;
    while(running) {
        InputEvent event;
        if(furi_message_queue_get(app.queue, &event, furi_ms_to_ticks(100)) == FuriStatusOk) {
            if(event.type == InputTypeLong && event.key == InputKeyOk &&
               app.state != TimerRunning) {
                app.count = 0;
                app.save_ok = timer_write_settings(&app);
            } else if(event.type == InputTypeShort || event.type == InputTypeRepeat) {
                bool setting = app.state == TimerReady || app.state == TimerFinished;
                if(event.type == InputTypeShort && event.key == InputKeyBack) {
                    running = false;
                } else if(event.type == InputTypeShort && event.key == InputKeyOk) {
                    if(app.state == TimerRunning) {
                        app.remaining_ticks = timer_remaining(&app, furi_get_tick());
                        app.state = TimerPaused;
                    } else {
                        if(app.state == TimerFinished) {
                            app.remaining_ticks = timer_full_duration(&app);
                        }
                        app.started_tick = furi_get_tick();
                        app.state = TimerRunning;
                    }
                } else if(event.key == InputKeyRight) {
                    if(setting) {
                        app.selected_field = (app.selected_field + 1) % 3;
                    } else if(event.type == InputTypeShort) {
                        app.remaining_ticks = timer_full_duration(&app);
                        app.state = TimerReady;
                    }
                } else if(event.key == InputKeyLeft && setting) {
                    app.selected_field = (app.selected_field + 2) % 3;
                } else if((event.key == InputKeyUp || event.key == InputKeyDown) && setting) {
                    uint32_t step = app.selected_field == 0 ? 3600U :
                                    app.selected_field == 1 ? 60U : 1U;
                    uint32_t field_value = (app.duration_seconds / step) %
                                           (app.selected_field == 0 ? 100U : 60U);
                    uint32_t next = app.duration_seconds;
                    if(event.key == InputKeyUp &&
                       field_value < (app.selected_field == 0 ? MAX_HOURS : 59U)) {
                        next += step;
                    } else if(event.key == InputKeyDown && field_value > 0) {
                        next -= step;
                    }
                    if(next >= 1 && next != app.duration_seconds) {
                        app.duration_seconds = next;
                        app.remaining_ticks = timer_full_duration(&app);
                        app.state = TimerReady;
                        app.save_ok = timer_write_settings(&app);
                    }
                }
            }
        }

        if(app.state == TimerRunning && timer_remaining(&app, furi_get_tick()) == 0) {
            app.state = TimerFinished;
            app.remaining_ticks = 0;
            if(app.count < UINT32_MAX) app.count++;
            app.save_ok = timer_write_settings(&app);
            notification_message(notification, &sequence_timer_finished);
        }
        view_port_update(view_port);
    }

    view_port_enabled_set(view_port, false);
    gui_remove_view_port(gui, view_port);
    view_port_free(view_port);
    furi_message_queue_free(app.queue);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);
    return 0;
}
