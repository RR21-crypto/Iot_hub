/*
 * Bluetooth Low Energy service (NimBLE). See bt_svc.h.
 * Based on the ESP-IDF "bleprph" example, without pairing/bonding.
 */
#include <stdio.h>
#include <string.h>
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "main.h"
#include "core/app_sm.h"
#include "services/time_svc.h"
#include "bt_svc.h"

static const char *TAG = "bt_svc";

// 128-bit UUIDs, written little-endian: 414e4e41-xxxx-4875-6200-000000000000 ("ANNA ... Hub")
#define ANNA_UUID(x)    BLE_UUID128_INIT(0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x62, \
                                         0x75, 0x48, (x), 0x00, 0x41, 0x4e, 0x4e, 0x41)
static const ble_uuid128_t svc_uuid  = ANNA_UUID(0x01);
static const ble_uuid128_t info_uuid = ANNA_UUID(0x02);
static const ble_uuid128_t msg_uuid  = ANNA_UUID(0x03);
#define UUID_USER_DESCRIPTION   0x2901  // standard descriptor: shows the names in nRF Connect

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static bt_svc_status_t status;          // guarded by lock
static char message[BT_SVC_MESSAGE_LEN + 1];
static bool stack_started;
static bool synced;                     // host and controller are ready
static uint8_t own_addr_type;
static uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;

static void advertise(void);

/* ---------- GATT ---------- */

static int chr_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ble_uuid_cmp(ctxt->chr->uuid, &info_uuid.u) == 0 && ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        char text[96];
        struct tm now;
        bool have_time = time_svc_now(&now);
        snprintf(text, sizeof(text), "%s %s | %02d:%02d:%02d | up %lus", APP_DISPLAY_NAME,
                 esp_app_get_description()->version, have_time ? now.tm_hour : 0,
                 have_time ? now.tm_min : 0, have_time ? now.tm_sec : 0,
                 (unsigned long)(esp_timer_get_time() / 1000000));
        return os_mbuf_append(ctxt->om, text, strlen(text)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (ble_uuid_cmp(ctxt->chr->uuid, &msg_uuid.u) == 0 && ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        char buf[BT_SVC_MESSAGE_LEN + 1];
        uint16_t len = 0;
        if (ble_hs_mbuf_to_flat(ctxt->om, buf, BT_SVC_MESSAGE_LEN, &len) != 0) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        buf[len] = '\0';
        portENTER_CRITICAL(&lock);
        memcpy(message, buf, len + 1);
        portEXIT_CRITICAL(&lock);
        ESP_LOGI(TAG, "message: %s", buf);
        app_sm_post(APP_EVT_BT_MESSAGE, 0);
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int desc_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    const char *name = arg;
    return os_mbuf_append(ctxt->om, name, strlen(name)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static const struct ble_gatt_svc_def gatt_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &info_uuid.u,
                .access_cb = chr_access,
                .flags = BLE_GATT_CHR_F_READ,
                .descriptors = (struct ble_gatt_dsc_def[]) {
                    { .uuid = BLE_UUID16_DECLARE(UUID_USER_DESCRIPTION), .att_flags = BLE_ATT_F_READ,
                      .access_cb = desc_access, .arg = "Info" },
                    { 0 },
                },
            },
            {
                .uuid = &msg_uuid.u,
                .access_cb = chr_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                .descriptors = (struct ble_gatt_dsc_def[]) {
                    { .uuid = BLE_UUID16_DECLARE(UUID_USER_DESCRIPTION), .att_flags = BLE_ATT_F_READ,
                      .access_cb = desc_access, .arg = "Message (text shown on screen)" },
                    { 0 },
                },
            },
            { 0 },
        },
    },
    { 0 },
};

/* ---------- GAP ---------- */

static void set_connected(bool connected, const ble_addr_t *peer)
{
    portENTER_CRITICAL(&lock);
    status.connected = connected;
    if (connected && peer) {
        snprintf(status.peer, sizeof(status.peer), "%02X:%02X:%02X:%02X:%02X:%02X",
                 peer->val[5], peer->val[4], peer->val[3], peer->val[2], peer->val[1], peer->val[0]);
    } else {
        status.peer[0] = '\0';
    }
    portEXIT_CRITICAL(&lock);
    app_sm_post(APP_EVT_BT_CHANGED, connected);
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            struct ble_gap_conn_desc desc;
            conn_handle = event->connect.conn_handle;
            ble_gap_conn_find(conn_handle, &desc);
            set_connected(true, &desc.peer_id_addr);
            ESP_LOGI(TAG, "connected to %s", status.peer);
        } else {
            advertise();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnected, reason %d", event->disconnect.reason);
        conn_handle = BLE_HS_CONN_HANDLE_NONE;
        set_connected(false, NULL);
        advertise();
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise();
        break;
    default:
        break;
    }
    return 0;
}

static void advertise(void)
{
    if (!synced || !status.enabled || conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        return;
    }
    struct ble_hs_adv_fields fields = { 0 };
    const char *name = ble_svc_gap_device_name();
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv fields failed: %d", rc);
        return;
    }
    struct ble_gap_adv_params params = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,     // connectable
        .disc_mode = BLE_GAP_DISC_MODE_GEN,     // discoverable
    };
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "adv start failed: %d", rc);
    } else {
        ESP_LOGI(TAG, "advertising as \"%s\"", name);
    }
}

static void on_sync(void)
{
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &own_addr_type);
    synced = true;
    advertise();
}

static void on_reset(int reason)
{
    synced = false;
    ESP_LOGW(TAG, "BLE host reset, reason %d", reason);
}

static void host_task(void *param)
{
    nimble_port_run();      // returns only after nimble_port_stop()
    nimble_port_freertos_deinit();
}

static esp_err_t stack_start(void)
{
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble init failed: %s", esp_err_to_name(err));
        return err;
    }
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    if (ble_gatts_count_cfg(gatt_services) != 0 || ble_gatts_add_svcs(gatt_services) != 0) {
        ESP_LOGE(TAG, "GATT setup failed");
        return ESP_FAIL;
    }
    ble_svc_gap_device_name_set(APP_DISPLAY_NAME);
    nimble_port_freertos_init(host_task);
    stack_started = true;
    return ESP_OK;
}

/* ---------- API ---------- */

esp_err_t bt_svc_set_enabled(bool enabled)
{
    status.enabled = enabled;
    if (enabled) {
        if (!stack_started) {
            esp_err_t err = stack_start();      // advertising starts from on_sync
            if (err != ESP_OK) {
                status.enabled = false;
                return err;
            }
        } else {
            advertise();
        }
        ESP_LOGI(TAG, "on");
    } else if (stack_started) {
        ble_gap_adv_stop();
        if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        ESP_LOGI(TAG, "off");
    }
    app_sm_post(APP_EVT_BT_CHANGED, status.connected);
    return ESP_OK;
}

void bt_svc_get_status(bt_svc_status_t *out)
{
    portENTER_CRITICAL(&lock);
    *out = status;
    portEXIT_CRITICAL(&lock);
}

size_t bt_svc_get_message(char *buf, size_t len)
{
    portENTER_CRITICAL(&lock);
    strlcpy(buf, message, len);
    portEXIT_CRITICAL(&lock);
    return strlen(buf);
}
