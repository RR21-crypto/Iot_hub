/*
 * Firmware update over WiFi (OTA) from a web browser. See ota_svc.h.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "esp_system.h"
#include "main.h"
#include "core/app_sm.h"
#include "ota_svc.h"

static const char *TAG = "ota_svc";

#define OTA_BUF_SIZE        4096
#define OTA_RECV_RETRIES    5

static httpd_handle_t server;
static char code[5];

// The upload page. %s = firmware version.
static const char page_fmt[] =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>" APP_DISPLAY_NAME " Update</title><style>"
    "body{font-family:sans-serif;background:#12101a;color:#eee;max-width:420px;margin:40px auto;padding:0 16px}"
    "h1{color:#b388ff}input,button{font-size:16px;margin:8px 0;width:100%%;box-sizing:border-box;padding:10px}"
    "button{background:#b388ff;border:0;border-radius:8px;color:#12101a;font-weight:bold}"
    "progress{width:100%%;height:20px}</style></head><body>"
    "<h1>" APP_DISPLAY_NAME " Update</h1><p>Current version: <b>%s</b></p>"
    "<p>Firmware file (build/anna_display.bin):</p><input type='file' id='f' accept='.bin'>"
    "<p>Code on the screen (Settings &gt; Firmware update):</p><input id='c' inputmode='numeric' maxlength='4'>"
    "<button onclick='up()'>Upload</button><progress id='p' value='0' max='100'></progress><p id='s'></p>"
    "<script>function up(){var f=document.getElementById('f').files[0],c=document.getElementById('c').value,"
    "s=document.getElementById('s'),p=document.getElementById('p');p.value=0;"
    "if(!f){s.textContent='Choose a file first';return}"
    "if(!/^[0-9]{4}$/.test(c)){s.textContent='Enter the 4-digit code from the screen first';return}"
    // Check the code first, so a wrong code does not send the whole file for nothing
    "fetch('/check?code='+c).then(function(r){if(!r.ok){s.textContent='Wrong code. See Settings > Firmware update on the screen.';return}"
    "var x=new XMLHttpRequest();x.open('POST','/update?code='+c);"
    "x.upload.onprogress=function(e){p.value=e.loaded*100/e.total};"
    "x.onload=function(){s.textContent=x.responseText};x.onerror=function(){s.textContent='Connection failed'};"
    "s.textContent='Uploading...';x.send(f)}).catch(function(){s.textContent='Connection failed'})}"
    "</script></body></html>";

static esp_err_t page_get(httpd_req_t *req)
{
    char *page = malloc(sizeof(page_fmt) + 32);
    if (!page) {
        return httpd_resp_send_500(req);
    }
    int len = snprintf(page, sizeof(page_fmt) + 32, page_fmt, esp_app_get_description()->version);
    httpd_resp_set_type(req, "text/html");
    esp_err_t ret = httpd_resp_send(req, page, len);
    free(page);
    return ret;
}

static bool code_ok(httpd_req_t *req)
{
    char query[32], value[8];
    return httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK
           && httpd_query_key_value(query, "code", value, sizeof(value)) == ESP_OK
           && strcmp(value, code) == 0;
}

// Lets the page verify the code before uploading the whole file
static esp_err_t check_get(httpd_req_t *req)
{
    if (!code_ok(req)) {
        httpd_resp_set_status(req, "403 Forbidden");
    }
    return httpd_resp_sendstr(req, "");
}

static esp_err_t fail(httpd_req_t *req, esp_ota_handle_t ota, const char *msg)
{
    ESP_LOGE(TAG, "%s", msg);
    if (ota) {
        esp_ota_abort(ota);
    }
    app_sm_post(APP_EVT_OTA_PROGRESS, -1);
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_sendstr(req, msg);
}

static esp_err_t update_post(httpd_req_t *req)
{
    if (!code_ok(req)) {
        return fail(req, 0, "Wrong code. See Settings > Firmware update on the screen.");
    }
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part || req->content_len == 0 || req->content_len > part->size) {
        return fail(req, 0, "File size does not fit.");
    }

    esp_ota_handle_t ota = 0;
    if (esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &ota) != ESP_OK) {
        return fail(req, 0, "Could not start the update.");
    }
    ESP_LOGI(TAG, "receiving %u bytes into %s", (unsigned)req->content_len, part->label);

    static char buf[OTA_BUF_SIZE];
    size_t remaining = req->content_len;
    int retries = 0, last_pct = -1;
    while (remaining > 0) {
        int n = httpd_req_recv(req, buf, remaining < sizeof(buf) ? remaining : sizeof(buf));
        if (n == HTTPD_SOCK_ERR_TIMEOUT && ++retries <= OTA_RECV_RETRIES) {
            continue;
        }
        if (n <= 0) {
            return fail(req, ota, "Upload interrupted.");
        }
        if (esp_ota_write(ota, buf, n) != ESP_OK) {
            return fail(req, ota, "Flash write failed.");
        }
        remaining -= n;
        int pct = (int)((req->content_len - remaining) * 100 / req->content_len);
        if (pct / 5 != last_pct / 5 && pct < 100) {     // every 5%
            app_sm_post(APP_EVT_OTA_PROGRESS, pct);
            last_pct = pct;
        }
    }

    // esp_ota_end() checks the image (header, checksum) before we switch to it
    esp_err_t err = esp_ota_end(ota);
    if (err != ESP_OK) {
        app_sm_post(APP_EVT_OTA_PROGRESS, -1);
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "This file is not a valid firmware.");
    }
    if (esp_ota_set_boot_partition(part) != ESP_OK) {
        app_sm_post(APP_EVT_OTA_PROGRESS, -1);
        return httpd_resp_send_500(req);
    }

    app_sm_post(APP_EVT_OTA_PROGRESS, 100);
    httpd_resp_sendstr(req, "Done! The board is restarting with the new firmware...");
    ESP_LOGI(TAG, "update OK, restarting");
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
    return ESP_OK;
}

esp_err_t ota_svc_start(void)
{
    if (server) {
        return ESP_OK;
    }
    snprintf(code, sizeof(code), "%04u", (unsigned)(esp_random() % 10000));

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 6144;
    cfg.recv_wait_timeout = 10;
    ESP_RETURN_ON_ERROR(httpd_start(&server, &cfg), TAG, "server start failed");

    const httpd_uri_t page = { .uri = "/", .method = HTTP_GET, .handler = page_get };
    const httpd_uri_t check = { .uri = "/check", .method = HTTP_GET, .handler = check_get };
    const httpd_uri_t update = { .uri = "/update", .method = HTTP_POST, .handler = update_post };
    httpd_register_uri_handler(server, &page);
    httpd_register_uri_handler(server, &check);
    httpd_register_uri_handler(server, &update);
    ESP_LOGI(TAG, "OTA page ready, code %s", code);
    return ESP_OK;
}

const char *ota_svc_code(void)
{
    return server ? code : NULL;
}

void ota_svc_mark_valid(void)
{
    esp_ota_img_states_t state;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "new firmware confirmed, rollback cancelled");
    }
}
