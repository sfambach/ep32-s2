/* Wi-Fi FTM Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <string.h>
#include "nvs_flash.h"
#include "cmd_system.h"
#include "argtable3/argtable3.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_console.h"

#include "lwip/err.h"
#include "lwip/sys.h"

/* preconfigure your connection */
#define ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define ESP_MAXIMUM_RETRY  CONFIG_ESP_MAXIMUM_RETRY


typedef struct {
    struct arg_str *ssid;
    struct arg_str *password;
    struct arg_end *end;
} wifi_args_t;

typedef struct {
    struct arg_str *ssid;
    struct arg_end *end;
} wifi_scan_arg_t;

typedef struct {
    struct arg_lit *mode;
    struct arg_int *frm_count;
    struct arg_int *burst_period;
    struct arg_end *end;
} wifi_ftm_args_t;

static wifi_args_t sta_args;
static wifi_args_t ap_args;
static wifi_scan_arg_t scan_args;
static wifi_ftm_args_t ftm_args;

static bool s_reconnect = true;
static const char *TAG_STA = "ftm_station";
static const char *TAG_AP = "ftm_ap";

static EventGroupHandle_t wifi_event_group;
const int CONNECTED_BIT = BIT0;
const int DISCONNECTED_BIT = BIT1;

static void scan_done_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    uint16_t sta_number = 0;
    uint8_t i;
    wifi_ap_record_t *ap_list_buffer;

    esp_wifi_scan_get_ap_num(&sta_number);
    ap_list_buffer = malloc(sta_number * sizeof(wifi_ap_record_t));
    if (ap_list_buffer == NULL) {
        ESP_LOGE(TAG_STA, "Failed to malloc buffer to print scan results");
        return;
    }

    if (esp_wifi_scan_get_ap_records(&sta_number, (wifi_ap_record_t *)ap_list_buffer) == ESP_OK) {
        for (i = 0; i < sta_number; i++) {
            ESP_LOGI(TAG_STA, "[%s][rssi=%d]""%s", ap_list_buffer[i].ssid, ap_list_buffer[i].rssi,
                     ap_list_buffer[i].ftm_responder ? "[FTM Responder]" : "");
        }
    }
    free(ap_list_buffer);
    ESP_LOGI(TAG_STA, "sta scan done");
}

static void wifi_connected_handler(void *arg, esp_event_base_t event_base,
                                   int32_t event_id, void *event_data)
{
    wifi_event_sta_connected_t *event = (wifi_event_sta_connected_t *)event_data;

    ESP_LOGI(TAG_STA, "Connected to %s (BSSID: "MACSTR", Channel: %d)", event->ssid,
             MAC2STR(event->bssid), event->channel);

    xEventGroupClearBits(wifi_event_group, DISCONNECTED_BIT);
    xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
}

static void disconnect_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (s_reconnect) {
        ESP_LOGI(TAG_STA, "sta disconnect, s_reconnect...");
        esp_wifi_connect();
    } else {
        ESP_LOGI(TAG_STA, "sta disconnect");
    }
    xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
    xEventGroupSetBits(wifi_event_group, DISCONNECTED_BIT);
}

static void ftm_report_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    wifi_event_ftm_report_t *event = (wifi_event_ftm_report_t *) event_data;

    ESP_LOGI(TAG_STA, "Estimated RTT - %d nSec, Estimated Distance - %d.%02d meters", event->rtt_est,
             event->dist_est / 100, event->dist_est % 100);
}

void initialise_wifi(void)
{
    esp_log_level_set("wifi", ESP_LOG_WARN);
    static bool initialized = false;

    if (initialized) {
        return;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK( esp_event_loop_create_default() );
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                    WIFI_EVENT_SCAN_DONE,
                    &scan_done_handler,
                    NULL,
                    NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                    WIFI_EVENT_STA_CONNECTED,
                    &wifi_connected_handler,
                    NULL,
                    NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                    WIFI_EVENT_STA_DISCONNECTED,
                    &disconnect_handler,
                    NULL,
                    NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                    WIFI_EVENT_FTM_REPORT,
                    &ftm_report_handler,
                    NULL,
                    NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL) );
    ESP_ERROR_CHECK(esp_wifi_start() );
    initialized = true;
}

static bool wifi_cmd_sta_join(const char *ssid, const char *pass)
{
    int bits = xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, 0, 1, 0);

    wifi_config_t wifi_config = { 0 };

    strlcpy((char *) wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    if (pass) {
        strlcpy((char *) wifi_config.sta.password, pass, sizeof(wifi_config.sta.password));
    }

    if (bits & CONNECTED_BIT) {
        s_reconnect = false;
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
        ESP_ERROR_CHECK( esp_wifi_disconnect() );
        xEventGroupWaitBits(wifi_event_group, DISCONNECTED_BIT, 0, 1, portTICK_RATE_MS);
    }

    s_reconnect = true;
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK( esp_wifi_connect() );

    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, 0, 1, 5000 / portTICK_RATE_MS);

    return true;
}

static int wifi_cmd_sta(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &sta_args);

    if (nerrors != 0) {
        arg_print_errors(stderr, sta_args.end, argv[0]);
        return 1;
    }

    ESP_LOGI(TAG_STA, "sta connecting to '%s'", sta_args.ssid->sval[0]);
    wifi_cmd_sta_join(sta_args.ssid->sval[0], sta_args.password->sval[0]);
    return 0;
}

static bool wifi_cmd_sta_scan(const char *ssid)
{
    wifi_scan_config_t scan_config = { 0 };
    scan_config.ssid = (uint8_t *) ssid;

    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_scan_start(&scan_config, false) );

    return true;
}

static int wifi_cmd_scan(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &scan_args);

    if (nerrors != 0) {
        arg_print_errors(stderr, scan_args.end, argv[0]);
        return 1;
    }

    ESP_LOGI(TAG_STA, "sta start to scan");
    if ( scan_args.ssid->count == 1 ) {
        wifi_cmd_sta_scan(scan_args.ssid->sval[0]);
    } else {
        wifi_cmd_sta_scan(NULL);
    }
    return 0;
}

static bool wifi_cmd_ap_set(const char* ssid, const char* pass)
{
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "",
            .ssid_len = 0,
            .max_connection = 4,
            .password = "",
            .authmode = WIFI_AUTH_WPA2_PSK
        },
    };

    s_reconnect = false;
    strlcpy((char*) wifi_config.ap.ssid, ssid, sizeof(wifi_config.ap.ssid));
    if (pass) {
        if (strlen(pass) != 0 && strlen(pass) < 8) {
            s_reconnect = true;
            ESP_LOGE(TAG_AP, "password less than 8");
            return false;
        }
        strlcpy((char*) wifi_config.ap.password, pass, sizeof(wifi_config.ap.password));
    }

    if (strlen(pass) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    return true;
}

static int wifi_cmd_ap(int argc, char** argv)
{
    int nerrors = arg_parse(argc, argv, (void**) &ap_args);

    if (nerrors != 0) {
        arg_print_errors(stderr, ap_args.end, argv[0]);
        return 1;
    }

    wifi_cmd_ap_set(ap_args.ssid->sval[0], ap_args.password->sval[0]);
    ESP_LOGI(TAG_AP, "Starting SoftAP with FTM Responder support, SSID - %s, Password - %s", ap_args.ssid->sval[0], ap_args.password->sval[0]);
    return 0;
}

static int wifi_cmd_query(int argc, char **argv)
{
    wifi_config_t cfg;
    wifi_mode_t mode;

    esp_wifi_get_mode(&mode);
    if (WIFI_MODE_AP == mode) {
        esp_wifi_get_config(WIFI_IF_AP, &cfg);
        ESP_LOGI(TAG_AP, "AP mode, %s %s", cfg.ap.ssid, cfg.ap.password);
    } else if (WIFI_MODE_STA == mode) {
        int bits = xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, 0, 1, 0);
        if (bits & CONNECTED_BIT) {
            esp_wifi_get_config(WIFI_IF_STA, &cfg);
            ESP_LOGI(TAG_STA, "sta mode, connected %s", cfg.ap.ssid);
        } else {
            ESP_LOGI(TAG_STA, "sta mode, disconnected");
        }
    } else {
        ESP_LOGI(TAG_STA, "NULL mode");
        return 0;
    }

    return 0;
}

static int wifi_cmd_ftm(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &ftm_args);

    wifi_ftm_initiator_cfg_t ftmi_cfg = {
        .frm_count = 32,
        .burst_period = 2,
    };

    if (nerrors != 0) {
        arg_print_errors(stderr, ftm_args.end, argv[0]);
        return 0;
    }

    if (ftm_args.mode->count == 0) {
        goto ftm_start;
    }

    if (ftm_args.frm_count->count != 0) {
        uint8_t count = ftm_args.frm_count->ival[0];
        if (count != 0 && count != 16 && count != 24 &&
            count != 32 && count != 64) {
            count = 0;
        }
        ftmi_cfg.frm_count = count;
    }

    if (ftm_args.burst_period->count != 0) {
        if (ftm_args.burst_period->ival[0] > 0 &&
                ftm_args.burst_period->ival[0] < 256) {
            ftmi_cfg.burst_period = ftm_args.burst_period->ival[0];
        } else {
            ftmi_cfg.burst_period = 0;
        }
    }

ftm_start:
    ESP_LOGI(TAG_STA, "Starting FTM Initiator with Frm Count %d, Burst Period - %dmSec",
             ftmi_cfg.frm_count, ftmi_cfg.burst_period * 100);
    esp_wifi_ftm_start_initiator(&ftmi_cfg);

    return 0;
}

void register_wifi(void)
{
    sta_args.ssid = arg_str1(NULL, NULL, "<ssid>", "SSID of AP");
    sta_args.password = arg_str0(NULL, NULL, "<pass>", "password of AP");
    sta_args.end = arg_end(2);

    const esp_console_cmd_t sta_cmd = {
        .command = "sta",
        .help = "WiFi is station mode, join specified soft-AP",
        .hint = NULL,
        .func = &wifi_cmd_sta,
        .argtable = &sta_args
    };

    ESP_ERROR_CHECK( esp_console_cmd_register(&sta_cmd) );

    ap_args.ssid = arg_str1(NULL, NULL, "<ssid>", "SSID of AP");
    ap_args.password = arg_str0(NULL, NULL, "<pass>", "password of AP");
    ap_args.end = arg_end(2);

    const esp_console_cmd_t ap_cmd = {
        .command = "ap",
        .help = "AP mode, configure ssid and password",
        .hint = NULL,
        .func = &wifi_cmd_ap,
        .argtable = &ap_args
    };

    ESP_ERROR_CHECK( esp_console_cmd_register(&ap_cmd) );

    scan_args.ssid = arg_str0(NULL, NULL, "<ssid>", "SSID of AP want to be scanned");
    scan_args.end = arg_end(1);

    const esp_console_cmd_t scan_cmd = {
        .command = "scan",
        .help = "WiFi is station mode, start scan ap",
        .hint = NULL,
        .func = &wifi_cmd_scan,
        .argtable = &scan_args
    };

    ESP_ERROR_CHECK( esp_console_cmd_register(&scan_cmd) );

    const esp_console_cmd_t query_cmd = {
        .command = "query",
        .help = "query WiFi info",
        .hint = NULL,
        .func = &wifi_cmd_query,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&query_cmd) );

    ftm_args.mode = arg_lit1("I", "ftm_initiator", "FTM Initiator mode");
    ftm_args.frm_count = arg_int0("c", "frm_count", "<0/16/24/32/64>", "FTM frames to be exchanged (0: No preference)");
    ftm_args.burst_period = arg_int0("p", "burst_period", "<0-255 (x 100 mSec)>", "Periodicity of FTM bursts in 100's of miliseconds (0: No preference)");
    ftm_args.end = arg_end(1);

    const esp_console_cmd_t ftm_cmd = {
        .command = "ftm",
        .help = "FTM command",
        .hint = NULL,
        .func = &wifi_cmd_ftm,
        .argtable = &ftm_args
    };

    ESP_ERROR_CHECK( esp_console_cmd_register(&ftm_cmd) );
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    initialise_wifi();

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    repl_config.prompt = "ftm>";
    // init console REPL environment
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));
    /* Register commands */
    register_system();
    register_wifi();

    printf("\n ==========================================================\n");
    printf(" |                      Steps to test FTM                 |\n");
    printf(" |                                                        |\n");
    printf(" |  1. Print 'help' to gain overview of commands          |\n");
    printf(" |  2. Use 'scan' command for AP that support FTM         |\n");
    printf(" |                          OR                            |\n");
    printf(" |  2. Start SoftAP on another ESP32S2 with 'ap' command  |\n");
    printf(" |  3. Setup connection with the AP using 'sta' command   |\n");
    printf(" |  4. Initiate FTM from Station using 'ftm -I' command   |\n");
    printf(" |                                                        |\n");
    printf(" ==========================================================\n\n");

    // start console REPL
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
