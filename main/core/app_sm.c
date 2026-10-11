/*
 * Application state machine (event-driven, table-based). See app_sm.h for the diagram.
 */
#include <stddef.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "bsp/esp-bsp.h"
#include "main.h"
#include "services/bt_svc.h"
#include "services/ota_svc.h"
#include "services/time_svc.h"
#include "ui/launcher.h"
#include "ui/quick_panel.h"
#include "ui/statusbar.h"
#include "ui/ui_common.h"
#include "app_registry.h"
#include "app_sm.h"

static const char *TAG = "app_sm";

#define APP_SM_QUEUE_LEN        16
#define APP_SM_TASK_PRIO        4
#define APP_SM_TICK_PERIOD_US   (1000 * 1000)

typedef struct {
    app_event_t event;
    int arg;
} app_msg_t;

// Return false to cancel the transition (stay in the current state)
typedef bool (*app_action_t)(const app_msg_t *msg);

typedef struct {
    app_state_t  state;     // current state (APP_STATE_ANY = every state)
    app_event_t  event;     // event received
    app_state_t  next;      // state after the transition (same state or APP_STATE_ANY = stay)
    app_action_t action;    // optional work done on the transition
} app_transition_t;

static bool open_app(const app_msg_t *msg);
static bool close_app(const app_msg_t *msg);
static bool tick(const app_msg_t *msg);
static bool wifi_up(const app_msg_t *msg);
static bool wifi_down(const app_msg_t *msg);
static bool ota_progress(const app_msg_t *msg);
static bool bt_changed(const app_msg_t *msg);
static bool bt_message(const app_msg_t *msg);
static bool forward_to_app(const app_msg_t *msg);

// The whole behaviour of the app in one table, first match wins. Events not listed are ignored.
static const app_transition_t transitions[] = {
    { APP_STATE_BOOT, APP_EVT_INIT_DONE,      APP_STATE_HOME,  NULL },
    { APP_STATE_BOOT, APP_EVT_INIT_FAILED,    APP_STATE_ERROR, NULL },
    { APP_STATE_HOME, APP_EVT_APP_OPEN,       APP_STATE_APP,   open_app },
    { APP_STATE_HOME, APP_EVT_TICK,           APP_STATE_HOME,  tick },
    { APP_STATE_APP,  APP_EVT_APP_CLOSE,      APP_STATE_HOME,  close_app },
    { APP_STATE_APP,  APP_EVT_TICK,           APP_STATE_APP,   tick },
    { APP_STATE_ANY,  APP_EVT_WIFI_UP,        APP_STATE_ANY,   wifi_up },
    { APP_STATE_ANY,  APP_EVT_WIFI_DOWN,      APP_STATE_ANY,   wifi_down },
    { APP_STATE_ANY,  APP_EVT_WIFI_SCAN_DONE, APP_STATE_ANY,   forward_to_app },
    { APP_STATE_ANY,  APP_EVT_TIME_SYNCED,    APP_STATE_ANY,   forward_to_app },
    { APP_STATE_ANY,  APP_EVT_OTA_PROGRESS,   APP_STATE_ANY,   ota_progress },
    { APP_STATE_ANY,  APP_EVT_WEATHER_UPDATED, APP_STATE_ANY,  forward_to_app },
    { APP_STATE_ANY,  APP_EVT_WEATHER_SEARCH_DONE, APP_STATE_ANY, forward_to_app },
    { APP_STATE_ANY,  APP_EVT_BT_CHANGED,     APP_STATE_ANY,   bt_changed },
    { APP_STATE_ANY,  APP_EVT_BT_MESSAGE,     APP_STATE_ANY,   bt_message },
};

static const char *const state_names[APP_STATE_COUNT] = {
    [APP_STATE_BOOT]  = "BOOT",
    [APP_STATE_HOME]  = "HOME",
    [APP_STATE_APP]   = "APP",
    [APP_STATE_ERROR] = "ERROR",
};

static const char *const event_names[APP_EVT_COUNT] = {
    [APP_EVT_INIT_DONE]      = "INIT_DONE",
    [APP_EVT_INIT_FAILED]    = "INIT_FAILED",
    [APP_EVT_APP_OPEN]       = "APP_OPEN",
    [APP_EVT_APP_CLOSE]      = "APP_CLOSE",
    [APP_EVT_TICK]           = "TICK",
    [APP_EVT_WIFI_UP]        = "WIFI_UP",
    [APP_EVT_WIFI_DOWN]      = "WIFI_DOWN",
    [APP_EVT_WIFI_SCAN_DONE] = "WIFI_SCAN_DONE",
    [APP_EVT_TIME_SYNCED]    = "TIME_SYNCED",
    [APP_EVT_OTA_PROGRESS]   = "OTA_PROGRESS",
    [APP_EVT_WEATHER_UPDATED] = "WEATHER_UPDATED",
    [APP_EVT_WEATHER_SEARCH_DONE] = "WEATHER_SEARCH_DONE",
    [APP_EVT_BT_CHANGED]     = "BT_CHANGED",
    [APP_EVT_BT_MESSAGE]     = "BT_MESSAGE",
};

static QueueHandle_t event_queue;
static app_state_t current_state = APP_STATE_BOOT;
static const app_desc_t *active_app;
static lv_obj_t *active_screen;

/* ---------- actions (display lock is held) ---------- */

static void back_pressed(void)
{
    app_sm_post(APP_EVT_APP_CLOSE, 0);
}

static bool open_app(const app_msg_t *msg)
{
    const app_desc_t *app = app_registry_get(msg->arg);
    if (!app) {
        ESP_LOGW(TAG, "no app at index %d", msg->arg);
        return false;
    }
    lv_obj_t *content;
    active_screen = ui_app_screen_create(app->name, back_pressed, &content);
    active_app = app;
    app->create(content);
    lv_screen_load(active_screen);
    ESP_LOGI(TAG, "opened app '%s' (stack left: %u bytes)", app->name, (unsigned)uxTaskGetStackHighWaterMark(NULL));
    return true;
}

static bool close_app(const app_msg_t *msg)
{
    if (active_app->destroy) {
        active_app->destroy();
    }
    lv_screen_load(launcher_screen());
    lv_obj_delete(active_screen);
    ESP_LOGI(TAG, "closed app '%s'", active_app->name);
    active_app = NULL;
    active_screen = NULL;
    return true;
}

static bool tick(const app_msg_t *msg)
{
    struct tm now;
    statusbar_set_time(time_svc_now(&now) ? &now : NULL);
    if (active_app && active_app->on_tick) {
        active_app->on_tick();
    }
    return true;
}

static bool forward_to_app(const app_msg_t *msg)
{
    if (active_app && active_app->on_event) {
        active_app->on_event(msg->event, msg->arg);
    }
    return true;
}

static bool wifi_up(const app_msg_t *msg)
{
    statusbar_set_wifi(true);
    quick_panel_refresh();
    time_svc_sync_start();
    ota_svc_start();
    return forward_to_app(msg);
}

static bool wifi_down(const app_msg_t *msg)
{
    statusbar_set_wifi(false);
    quick_panel_refresh();
    return forward_to_app(msg);
}

static bool ota_progress(const app_msg_t *msg)
{
    char text[32];
    if (msg->arg < 0) {
        snprintf(text, sizeof(text), "Update failed");
    } else if (msg->arg >= 100) {
        snprintf(text, sizeof(text), "Update done, restarting...");
    } else {
        snprintf(text, sizeof(text), "Update %d%%", msg->arg);
    }
    statusbar_set_message(text);
    return forward_to_app(msg);
}

static bool bt_changed(const app_msg_t *msg)
{
    bt_svc_status_t st;
    bt_svc_get_status(&st);
    statusbar_set_bt(st.enabled, st.connected);
    quick_panel_refresh();
    return forward_to_app(msg);
}

static bool bt_message(const app_msg_t *msg)
{
    char text[BT_SVC_MESSAGE_LEN + 1];
    if (bt_svc_get_message(text, sizeof(text))) {
        ui_toast_show(text);
    }
    return true;
}

static void state_enter(app_state_t state)
{
    switch (state) {
    case APP_STATE_HOME:
        lv_screen_load(launcher_screen());
        break;
    case APP_STATE_ERROR:
        lv_screen_load(ui_error_screen_create("Startup failed.\nCheck the serial monitor log."));
        break;
    default:
        break;
    }
}

/* ---------- engine ---------- */

static void handle_event(const app_msg_t *msg)
{
    for (size_t i = 0; i < sizeof(transitions) / sizeof(transitions[0]); i++) {
        const app_transition_t *t = &transitions[i];
        if ((t->state != current_state && t->state != APP_STATE_ANY) || t->event != msg->event) {
            continue;
        }
        if (t->action && !t->action(msg)) {
            return;
        }
        if (t->next != current_state && t->next != APP_STATE_ANY) {
            ESP_LOGI(TAG, "%s --%s--> %s", state_names[current_state], event_names[msg->event], state_names[t->next]);
            current_state = t->next;
            state_enter(current_state);
        }
        return;
    }
    // No matching row: event is not relevant in this state
}

static void app_sm_task(void *arg)
{
    app_msg_t msg;
    while (true) {
        if (xQueueReceive(event_queue, &msg, portMAX_DELAY) == pdTRUE) {
            bsp_display_lock(0);
            handle_event(&msg);
            bsp_display_unlock();
        }
    }
}

static void tick_timer_cb(void *arg)
{
    app_sm_post(APP_EVT_TICK, 0);
}

bool app_sm_post(app_event_t event, int arg)
{
    const app_msg_t msg = { .event = event, .arg = arg };
    if (!event_queue || xQueueSend(event_queue, &msg, 0) != pdTRUE) {
        ESP_LOGW(TAG, "event %d dropped", event);
        return false;
    }
    return true;
}

void app_sm_request_open(int app_index)
{
    app_sm_post(APP_EVT_APP_OPEN, app_index);
}

esp_err_t app_sm_start(void)
{
    event_queue = xQueueCreate(APP_SM_QUEUE_LEN, sizeof(app_msg_t));
    ESP_RETURN_ON_FALSE(event_queue, ESP_ERR_NO_MEM, TAG, "queue create failed");

    BaseType_t ok = xTaskCreate(app_sm_task, "app_sm", APP_SM_TASK_STACK, NULL, APP_SM_TASK_PRIO, NULL);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task create failed");

    const esp_timer_create_args_t timer_args = {
        .callback = tick_timer_cb,
        .name = "app_tick",
    };
    esp_timer_handle_t tick_timer;
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &tick_timer), TAG, "timer create failed");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(tick_timer, APP_SM_TICK_PERIOD_US), TAG, "timer start failed");

    ESP_LOGI(TAG, "started in state %s", state_names[current_state]);
    return ESP_OK;
}
