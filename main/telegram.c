/* esp-idf-telegram-bot
 *
 * Author: antusystem
 * e-mail: aleantunes95@gmail.com
 * Date: 11-01-2020
 * MIT License
 * As it is described in the readme file
 *
*/

#include <string.h>
#include <stdio.h>
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
#include "cJSON.h"

#include "claves.c"


// VARIABLES GLOBALES
static double O2 = 0;
static double BPM = 0;
static double LONGITUD = -3.62754833;
static double LATITUD = 40.38984667;

static double DATE = 0;


//Pin connected to a led
#define LED (GPIO_NUM_13)
int impreso = 0;

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_num = 0;


/* Root cert for extracted from:
 *
 * https://github.com/witnessmenow/Universal-Arduino-Telegram-Bot/blob/master/src/TelegramCertificate.h

   To embed it in the app binary, the PEM file is named
   in the component.mk COMPONENT_EMBED_TXTFILES variable.
*/
extern const char telegram_certificate_pem_start[] asm("_binary_telegram_certificate_pem_start");
extern const char telegram_certificate_pem_end[]   asm("_binary_telegram_certificate_pem_end");

char *concatenate(const char *str, double num) {
    int str_len = strlen(str);
    int num_len = snprintf(NULL, 0, "%f", num);
    char *result = malloc(str_len + num_len + 1);
    strcpy(result, str);
    snprintf(result + str_len, num_len + 1, "%f", num);
    return result;
}

esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    static char *output_buffer;  // Buffer to store response of http request from event handler
    static int output_len;       // Stores number of bytes read
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            /*
             *  Check for chunked encoding is added as the URL for chunked encoding used in this example returns binary data.
             *  However, event handler can also be used in case chunked encoding is used.
             */
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // If user_data buffer is configured, copy the response into the buffer
                if (evt->user_data) {
                    memcpy(evt->user_data + output_len, evt->data, evt->data_len);
                } else {
                    if (output_buffer == NULL) {
                        output_buffer = (char *) malloc(esp_http_client_get_content_length(evt->client));
                        output_len = 0;
                        if (output_buffer == NULL) {
                            ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
                            return ESP_FAIL;
                        }
                    }
                    memcpy(output_buffer + output_len, evt->data, evt->data_len);
                }
                output_len += evt->data_len;
            }

            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            if (output_buffer != NULL) {
                // Response is accumulated in output_buffer. Uncomment the below line to print the accumulated response
                // ESP_LOG_BUFFER_HEX(TAG, output_buffer, output_len);
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            int mbedtls_err = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error(evt->data, &mbedtls_err, NULL);
            if (err != 0) {
                if (output_buffer != NULL) {
                    free(output_buffer);
                    output_buffer = NULL;
                }
                output_len = 0;
                ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
                ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            }
            break;
    }
    return ESP_OK;
}


/*
 *  http_native_request() demonstrates use of low level APIs to connect to a server,
 *  make a http request and read response. Event handler is not used in this case.
 *  Note: This approach should only be used in case use of low level APIs is required.
 *  The easiest way is to use esp_http_perform()
 */
static void http_native_request(void) {
    char output_buffer[MAX_HTTP_OUTPUT_BUFFER] = "";   // Buffer to store response of http request
    int content_length = 0;
    esp_http_client_config_t config = {
            .url = "http://httpbin.org/get",
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // GET Request
    esp_http_client_set_method(client, HTTP_METHOD_GET);
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
    } else {
        content_length = esp_http_client_fetch_headers(client);
        if (content_length < 0) {
            ESP_LOGE(TAG, "HTTP client fetch headers failed");
        } else {
            int data_read = esp_http_client_read_response(client, output_buffer, MAX_HTTP_OUTPUT_BUFFER);
            if (data_read >= 0) {
                ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %d",
                         esp_http_client_get_status_code(client),
                         esp_http_client_get_content_length(client));
                //ESP_LOG_BUFFER_HEX(TAG, output_buffer, strlen(output_buffer));
                for (int i = 0; i < esp_http_client_get_content_length(client); i++) {
                    putchar(output_buffer[i]);
                }
                putchar('\r');
                putchar('\n');
            } else {
                ESP_LOGE(TAG, "Failed to read response");
            }
        }
    }
    esp_http_client_close(client);

    // POST Request
    const char *post_data = "{\"field1\":\"value1\"}";
    esp_http_client_set_url(client, "http://httpbin.org/post");
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    err = esp_http_client_open(client, strlen(post_data));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
    } else {
        int wlen = esp_http_client_write(client, post_data, strlen(post_data));
        if (wlen < 0) {
            ESP_LOGE(TAG, "Write failed");
        }
        int data_read = esp_http_client_read_response(client, output_buffer, MAX_HTTP_OUTPUT_BUFFER);
        if (data_read >= 0) {
            ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %d",
                     esp_http_client_get_status_code(client),
                     esp_http_client_get_content_length(client));
            //ESP_LOG_BUFFER_HEX(TAG, output_buffer, strlen(output_buffer));
            for (int i = 0; i < esp_http_client_get_content_length(client); i++) {
                putchar(output_buffer[i]);
            }
            putchar('\r');
            putchar('\n');
        } else {
            ESP_LOGE(TAG, "Failed to read response");
        }
    }
    esp_http_client_cleanup(client);
}


static void https_telegram_getMe_perform(void) {
    char buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};   // Buffer to store response of http request
    char url[512] = "";
    esp_http_client_config_t config = {
            .url = "https://api.telegram.org",
            .transport_type = HTTP_TRANSPORT_OVER_SSL,
            .event_handler = _http_event_handler,
            .cert_pem = telegram_certificate_pem_start,
            .user_data = buffer,        // Pass address of local buffer to get response
    };
    /* Creating the string of the url*/
    //Copy the url+TOKEN
    strcat(url, url_string);
    //Adding the method
    strcat(url, "/getMe");
    //ESP_LOGW(TAG2, "url es: %s",url);
    //ESP_LOGW(TAG, "Iniciare");
    esp_http_client_handle_t client = esp_http_client_init(&config);
    //You set the real url for the request
    esp_http_client_set_url(client, url);
    //ESP_LOGW(TAG, "Selecting the http method");
    esp_http_client_set_method(client, HTTP_METHOD_GET);
    //ESP_LOGW(TAG, "Perform");
    esp_err_t err = esp_http_client_perform(client);

    //ESP_LOGW(TAG, "Revisare");
    if (err == ESP_OK) {
        ESP_LOGI(TAG2, "HTTPS Status = %d, content_length = %d",
                 esp_http_client_get_status_code(client),
                 esp_http_client_get_content_length(client));
        ESP_LOGW(TAG2, "Desde Perform el output es: %s", buffer);
    } else {
        ESP_LOGE(TAG2, "Error perform http request %s", esp_err_to_name(err));
    }

    ESP_LOGW(TAG2, "Cerrar Cliente");
    esp_http_client_close(client);
    ESP_LOGW(TAG, "Limpiare");
    esp_http_client_cleanup(client);
}


static void https_telegram_sendLocation_perform_post(double latitude, double longitude) {

    char url[512] = "";
    char output_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};   // Buffer to store response of http request
    esp_http_client_config_t config = {
            .url = "https://api.telegram.org",
            .transport_type = HTTP_TRANSPORT_OVER_SSL,
            .event_handler = _http_event_handler,
            .cert_pem = telegram_certificate_pem_start,
            .user_data = output_buffer,
    };
    //POST
    ESP_LOGW(TAG3, "Iniciare");
    esp_http_client_handle_t client = esp_http_client_init(&config);

    /* Creating the string of the url*/
    //Copy the url+TOKEN
    strcat(url, url_string);
    //Passing the method
    strcat(url, "/sendLocation");
    //ESP_LOGW(TAG3, "url string es: %s",url);
    //You set the real url for the request
    esp_http_client_set_url(client, url);


    ESP_LOGW(TAG3, "Enviare POST");
    /*Here you add the text and the chat id
     * The format for the json for the telegram request is: {"chat_id":123456789,"text":"Here goes the message"}
      */
    // The example had this, but to add the chat id easierly I decided not to use a pointer
    //const char *post_data = "{\"chat_id\":852596694,\"text\":\"Envio de post\"}";
    char post_data[512] = "";
    //sprintf(post_data,"{\"chat_id\":%s,\"text\":\"%s\"}",chat_ID2, text);
    sprintf(post_data, "chat_id=%s&latitude=%f&longitude=%f", chat_ID2, latitude, longitude);
    //ESP_LOGW(TAG, "El json es es: %s",post_data);
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG3, "HTTP POST Status = %d, content_length = %d",
                 esp_http_client_get_status_code(client),
                 esp_http_client_get_content_length(client));
        ESP_LOGW(TAG3, "Desde Perform el output es: %s", output_buffer);

    } else {
        ESP_LOGE(TAG3, "HTTP POST request failed: %s", esp_err_to_name(err));
    }

    ESP_LOGW(TAG, "Limpiare");
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    ESP_LOGI(TAG3, "esp_get_free_heap_size: %d", esp_get_free_heap_size());
}


static void https_telegram_sendMessage_perform_post(char *text) {

    if (text == NULL) {
        text = " texto vacio";
    }


    char url[512] = "";
    char output_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};   // Buffer to store response of http request
    esp_http_client_config_t config = {
            .url = "https://api.telegram.org",
            .transport_type = HTTP_TRANSPORT_OVER_SSL,
            .event_handler = _http_event_handler,
            .cert_pem = telegram_certificate_pem_start,
            .user_data = output_buffer,
    };
    //POST
    ESP_LOGW(TAG3, "Iniciare");
    esp_http_client_handle_t client = esp_http_client_init(&config);

    /* Creating the string of the url*/
    //Copy the url+TOKEN
    strcat(url, url_string);
    //Passing the method
    strcat(url, "/sendMessage");
    //ESP_LOGW(TAG3, "url string es: %s",url);
    //You set the real url for the request
    esp_http_client_set_url(client, url);


    ESP_LOGW(TAG3, "Enviare POST");
    /*Here you add the text and the chat id
     * The format for the json for the telegram request is: {"chat_id":123456789,"text":"Here goes the message"}
      */
    // The example had this, but to add the chat id easierly I decided not to use a pointer
    //const char *post_data = "{\"chat_id\":852596694,\"text\":\"Envio de post\"}";
    char post_data[512] = "";
    sprintf(post_data, "{\"chat_id\":%s,\"text\":\"%s\"}", chat_ID2, text);
    //ESP_LOGW(TAG, "El json es es: %s",post_data);
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG3, "HTTP POST Status = %d, content_length = %d",
                 esp_http_client_get_status_code(client),
                 esp_http_client_get_content_length(client));
        ESP_LOGW(TAG3, "Desde Perform el output es: %s", output_buffer);

    } else {
        ESP_LOGE(TAG3, "HTTP POST request failed: %s", esp_err_to_name(err));
    }

    ESP_LOGW(TAG, "Limpiare");
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    ESP_LOGI(TAG3, "esp_get_free_heap_size: %d", esp_get_free_heap_size());
}

static char * https_telegram_receiveMessage_perform_post(void) {


    char url[512] = "";
    char output_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};   // Buffer to store response of http request
    esp_http_client_config_t config = {
            .url = "https://api.telegram.org",
            .transport_type = HTTP_TRANSPORT_OVER_SSL,
            .event_handler = _http_event_handler,
            .cert_pem = telegram_certificate_pem_start,
            .user_data = output_buffer,
    };
    //POST
    //ESP_LOGW(TAG3, "Iniciare");
    esp_http_client_handle_t client = esp_http_client_init(&config);

    /* Creating the string of the url*/
    //Copy the url+TOKEN
    strcat(url, url_string);
    //Passing the method
    strcat(url, "/getUpdates");

    strcat(url, "?offset=-1");
    //ESP_LOGW(TAG3, "url string es: %s",url);
    //You set the real url for the request
    esp_http_client_set_url(client, url);


    ESP_LOGW(TAG3, "Recibire POST");
    /*Here you add the text and the chat id
     * The format for the json for the telegram request is: {"chat_id":123456789,"text":"Here goes the message"}
      */
    // The example had this, but to add the chat id easierly I decided not to use a pointer
    //const char *post_data = "{\"chat_id\":852596694,\"text\":\"Envio de post\"}";
    char post_data[512] = "";
    sprintf(post_data, "{\"chat_id\":%s,\"text\":\"Here goes the message from post\"}", chat_ID2);
    //ESP_LOGW(TAG, "El json es es: %s",post_data);
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {

        ESP_LOGI(TAG3, "HTTP POST Status = %d, content_length = %d",
                 esp_http_client_get_status_code(client),
                 esp_http_client_get_content_length(client));
        ESP_LOGW(TAG3, "Desde Perform el output es: %s", output_buffer);


        cJSON *elem = cJSON_Parse(output_buffer);
        cJSON *result;
        cJSON *text;
        cJSON *message;
        char *texto;
        cJSON *channelPost;
        cJSON *date;
        double date_string;


        result = cJSON_GetObjectItem(elem, "result");
        message = cJSON_GetArrayItem(result, cJSON_GetArraySize(result) - 1);
        message = cJSON_GetObjectItem(message, "message");
        if (message == NULL) {
            message = cJSON_GetObjectItem(message, "message");
        }

        date = cJSON_GetObjectItem(message, "date");
        date_string = cJSON_GetNumberValue(date);
        ESP_LOGW(TAG3, "------------------DATE: %f", date_string);
        if (DATE != date_string) {
            DATE = date_string;

            text = cJSON_GetObjectItem(message, "text");
            texto = cJSON_GetStringValue(text);


            ESP_LOGW(TAG3, "El texto es: %s", texto);

            ESP_LOGW(TAG, "Limpiare");
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            ESP_LOGI(TAG3, "esp_get_free_heap_size: %d", esp_get_free_heap_size());

            return texto;
        } else {
            ESP_LOGW(TAG, "Limpiare");
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            ESP_LOGI(TAG3, "esp_get_free_heap_size: %d", esp_get_free_heap_size());

            return "NULL";
        }




    } else {
        ESP_LOGE(TAG3, "HTTP POST request failed: %s", esp_err_to_name(err));

        ESP_LOGW(TAG, "Limpiare");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        ESP_LOGI(TAG3, "esp_get_free_heap_size: %d", esp_get_free_heap_size());
        return "NULL";
    }

}

static void procesar_texto(char* texto) {
    ESP_LOGW(TAG3, "\n TEXTO: %s", texto);
    if (!strcmp(texto, "/stats")) {

        //ESP_LOGW(TAG3, "\n Oxigeno: \n BPM:  \n GPS: ");
        https_telegram_sendMessage_perform_post(strcat(concatenate("\nEl nivel de O2 en sangre es: ", O2), "%"));
        https_telegram_sendMessage_perform_post(strcat(concatenate("\n Las pulsaciones son: ", BPM), " BPM"));
        https_telegram_sendLocation_perform_post(LATITUD, LONGITUD);

    } else if (!strcmp(texto, "/o2")) {

        //ESP_LOGW(TAG3, "\n Oxigeno: ");

        https_telegram_sendMessage_perform_post(strcat(concatenate("\nEl nivel de O2 en sangre es: ", O2), "%"));


    } else if (!strcmp(texto, "/bpm")) {
        https_telegram_sendMessage_perform_post(strcat(concatenate("\n Las pulsaciones son: ", BPM), " BPM"));
        //ESP_LOGW(TAG3, "\n BPM:  ");
    } else if (!strcmp(texto, "/gps")) {

        //ESP_LOGW(TAG3, "\n GPS: ");
        https_telegram_sendLocation_perform_post(LATITUD, LONGITUD);

    }
}

static void http_test_task(void *pvParameters) {
    /* Creating the string of the url*/
    // You concatenate the host with the Token so you only have to write the method
    strcat(url_string, TOKEN);
    ESP_LOGW(TAG, "Wait 2 second before start");
    vTaskDelay(2000 / portTICK_PERIOD_MS);

    ESP_LOGW(TAG, "https_telegram_getMe_perform");
    https_telegram_getMe_perform();
    /* The functions https_telegram_getMe_native_get and https_telegram_sendMessage_native_get usually reboot the esp32 at when you use it after another and
     *  the second one finish, but I don't know why. Either way, it still send the message and obtain the getMe response, but the perform way is better
     *  for both options, especially for sending message with Json.*/
    //ESP_LOGW(TAG, "https_telegram_getMe_native_get");
    //https_telegram_getMe_native_get();
    //ESP_LOGW(TAG, "https_telegram_sendMessage_native_get");
    //https_telegram_sendMessage_native_get();

    /*
    if (impreso == 0) {
        ESP_LOGW(TAG, "https_telegram_sendMessage_perform_post");

        https_telegram_sendMessage_perform_post(
                "\n Bienvenido\n Escriba los siguientes comandos:\n'\'bpm pulsaciones\n'\'o2 Oxigeno en sangre\n'\'stats estado general\n'\'gps localizacion");
        impreso = 1;
    }
    */


    while (1) {
        procesar_texto(https_telegram_receiveMessage_perform_post());

        ESP_LOGW(TAG, "Wait 10 seconds");
        vTaskDelay(10000 / portTICK_PERIOD_MS);
    }



    //ESP_LOGI(TAG, "Finish http example");
    //vTaskDelete(NULL);
}





