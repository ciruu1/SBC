#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_tls.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_wifi.h"
#include "esp_http_client.h"
#include "driver/gpio.h"
#include "freertos/event_groups.h"

/*HTTP buffer*/
#define MAX_HTTP_RECV_BUFFER 1024
#define MAX_HTTP_OUTPUT_BUFFER 2048

/* TAGs for the system*/
static const char *TAG = "HTTP_CLIENT Handler";
static const char *TAG1 = "wifi station";
static const char *TAG2 = "Sending getMe";
static const char *TAG3 = "Sending sendMessage";

/*WIFI configuration*/
#define ESP_WIFI_SSID      "SBC"
#define ESP_WIFI_PASS      "SBCwifi$"
#define ESP_MAXIMUM_RETRY  10

/*Telegram configuration*/
#define TOKEN "5871846278:AAEcEZ5vMRFDzu3DZ08qxwHXmVggLoQIWho"
char url_string[512] = "https://api.telegram.org/bot";
// Using in the task strcat(url_string,TOKEN)); the main direct from the url will be in url_string
//The chat id that will receive the message
#define chat_ID1 "@SBC"
#define chat_ID2 "-831361098"