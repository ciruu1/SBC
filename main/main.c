
#include <stdio.h>
#include "driver/uart.h"
#include "minmea.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"
#include "mqtt_client.h"

#include "cJSON.h"

#include "driver/i2c.h"
#include "driver/gpio.h"

#include "max30100/max30100.h"

#include "driver/spi_master.h"
#include "pins.h"
#include "spi_mod.h"

#define FALL_THRESHOLD 765 // Umbral de caída en m/s^2

#include "math.h"

#include "telegram.c"

static const int RX_BUF_SIZE = 4096;

#define TXD_PIN (17)
#define RXD_PIN (16)

#define UART UART_NUM_2

static char tag[] = "gps";

int num = 0;

#define BUTTON_GPIO 12

#define I2C_SDA 26
#define I2C_SCL 25
#define I2C_FRQ 100000
#define I2C_PORT I2C_NUM_0

max30100_config_t max30100 = {};

esp_mqtt_client_handle_t client = NULL;

esp_err_t i2c_master_init(i2c_port_t i2c_port){
    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = I2C_SDA;
    conf.scl_io_num = I2C_SCL;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = I2C_FRQ;
    i2c_param_config(i2c_port, &conf);
    return i2c_driver_install(i2c_port, I2C_MODE_MASTER, 0, 0, 0);
}


uint16_t twosToBin(uint16_t input){

    //flip all 11 bits
    input = input ^ 0x07ff;

    //subtract 1 to get binary
    input = input - 1;

    return input;
}

int getData(uint8_t registro1, uint8_t registro2){
    uint16_t buffer = 0;
    int result = 0;
    int sign = 0;

    buffer = registro1;

    buffer = buffer << 8;

    buffer = registro2 | buffer;

    if(buffer >= 0x8000){// If negative
        sign = 1;
        buffer = twosToBin(buffer);
    }
    //Get rid of first 4 bits
    result = buffer & 0x07ff;

    if(sign == 1){
        result = 0 - result;
    }

    return result;
}




void ACL2_task(void * pvParams) {

    //Parametros
    spi_device_handle_t spi = (spi_device_handle_t) pvParams;

    //datos

    //uint8_t transaccion = 0;
    uint8_t X_High;
    uint8_t X_Low;
    uint8_t BeginMedicion = 0x02;
    uint8_t Setings = 0x83;




    uint8_t y_High;
    uint8_t y_Low;
    uint8_t z_High;
    uint8_t z_Low;

    //transacciones

    //Inicializacion del sensor:
    spi_transaction_t Begin_Measurement = {
            .cmd = 0X0A,
            .addr = 0x2D,
            .tx_buffer = &BeginMedicion,
            .length = 8,
    };
    spi_transaction_t Config_ACL = {
            .cmd = 0X0A,
            .addr = 0x2C,
            .tx_buffer = &Setings,
            .length = 8,
    };



    //EjeX

    spi_transaction_t EjeX_Mayor = {
            .cmd = 0X0B,
            .addr = 0x0F,
            .rx_buffer = &X_High,
            .length = 8,
            .rxlength = 8,
    };

    spi_transaction_t EjeX_Menor = {
            .cmd = 0X0B,
            .addr = 0x0E,
            .rx_buffer = &X_Low,
            .length = 8,
            .rxlength = 8,
    };



    //EjeY
    spi_transaction_t EjeY_Mayor = {
            .cmd = 0X0B,
            .addr = 0x11,
            .rx_buffer = &y_High,
            .length = 8,
            .rxlength = 8,
    };
    spi_transaction_t EjeY_Menor = {
            .cmd = 0X0B,
            .addr = 0x10,
            .tx_buffer = NULL,
            .rx_buffer = &y_Low,
            .length = 8,
            .rxlength = 8,
    };

    //EjeZ
    spi_transaction_t EjeZ_Mayor = {
            .cmd = 0X0B,
            .addr = 0x13 ,
            .tx_buffer = NULL,
            .rx_buffer = &z_High,
            .length = 8,
            .rxlength = 8,
    };
    spi_transaction_t EjeZ_Menor = {
            .cmd = 0X0B,
            .addr = 0x12,
            .tx_buffer = NULL,
            .rx_buffer = &z_Low,
            .length = 8,
            .rxlength = 8,

    };



    for (;;) {


        spi_device_acquire_bus(spi, portMAX_DELAY);
        spi_device_transmit(spi, &Config_ACL);
        spi_device_release_bus(spi);

        spi_device_acquire_bus(spi, portMAX_DELAY);
        spi_device_transmit(spi, &Begin_Measurement);
        spi_device_release_bus(spi);



        spi_device_acquire_bus(spi, portMAX_DELAY);


        //Trasmisiones eje X
        spi_device_transmit(spi, &EjeX_Mayor);
        spi_device_release_bus(spi);

        spi_device_acquire_bus(spi, portMAX_DELAY);
        spi_device_transmit(spi, &EjeX_Menor);
        spi_device_release_bus(spi);

        //Trasmisiones eje Y
        spi_device_acquire_bus(spi, portMAX_DELAY);
        spi_device_transmit(spi, &EjeY_Mayor);
        spi_device_release_bus(spi);

        spi_device_acquire_bus(spi, portMAX_DELAY);
        spi_device_transmit(spi, &EjeY_Menor);
        spi_device_release_bus(spi);


        //Trasmisiones eje >
        spi_device_acquire_bus(spi, portMAX_DELAY);
        spi_device_transmit(spi, &EjeZ_Mayor);
        spi_device_release_bus(spi);

        spi_device_acquire_bus(spi, portMAX_DELAY);
        spi_device_transmit(spi, &EjeZ_Menor);
        spi_device_release_bus(spi);



        int resultado_EjeX = getData(X_High,X_Low);
        int resultado_EjeY = getData(y_High,y_Low);
        int resultado_EjeZ = getData(z_High,z_Low);



        /*
         int8_t res_X_High = (int8_t) SPI_SWAP_DATA_RX(X_High, 8);
         int8_t res_X_Low = (int8_t) SPI_SWAP_DATA_RX(X_Low, 8);
         int8_t res_Y_High = (int8_t) SPI_SWAP_DATA_RX(y_High, 8);
         int8_t res_Y_Low = (int8_t) SPI_SWAP_DATA_RX(y_Low, 8);
         int8_t res_Z_High = (int8_t) SPI_SWAP_DATA_RX(z_High, 8);
         int8_t res_Z_Low = (int8_t) SPI_SWAP_DATA_RX(z_Low, 8);*/



        //Transformacion de los datos recibidos a datos entendibles

        //ESP_LOGI(tag, "Valor EjeX = %d ---- EjeY = %d ---- EjeZ = %d \n", resultado_EjeX,resultado_EjeY,resultado_EjeZ);

/*
    printf("Valor EjeX = %d  --- Valor EjeX_Low = %d\n", X_High,X_Low);
    printf("Valor EjeY_High =  %d --- Valor EjeY_Low = %d\n", y_High, y_Low);
    printf("Valor EjeZ_High =  %d --- Valor EjeZ_Low = %d\n", z_High, z_Low);*/
        //ESP_LOGI(tag, "---------------------------------------------------------------------\n");

        double magnitude = sqrt(resultado_EjeX*resultado_EjeX + resultado_EjeY*resultado_EjeY + resultado_EjeZ*resultado_EjeZ);
        if (magnitude > FALL_THRESHOLD) {
            ESP_LOGI(TAG, "Posible caída detectada");
            https_telegram_sendMessage_perform_post("Posible caída detectada");
        }

        vTaskDelay(pdMS_TO_TICKS(300));
    }
}


void publishDataNumber(char * key, double number) {
    // Crear json que se quiere enviar al ThingsBoard
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, key, number); // En la telemetría de Thingsboard aparecerá key = key y value = 0.336
    char *post_data = cJSON_PrintUnformatted(root);

    //ESP_LOGI(tag, "Published %.18f", number);

    // Enviar los datos
    esp_mqtt_client_publish(client, "v1/devices/me/telemetry", post_data, 0, 1, 0); // v1/devices/me/telemetry sale de la MQTT Device API Reference de ThingsBoard
    cJSON_Delete(root);
    // Free is intentional, it's client responsibility to free the result of cJSON_Print
    free(post_data);
}

void get_bpm(void* param) {
    ESP_LOGI(tag, "MAX320100 Test\n");
    max30100_data_t result = {};
    while(true) {
        if (client == NULL)
            continue;
        //Update sensor, saving to "result"
        ESP_ERROR_CHECK(max30100_update(&max30100, &result));
        if(result.pulse_detected) {
            //printf("BEAT\n");
            //printf("BPM: %f | SpO2: %f%%\n", result.heart_bpm, result.spO2);
            //ESP_LOGI(tag, "[%f,%f]\n",result.heart_bpm, result.spO2);

            // Oximeter
            publishDataNumber("SpO2", result.spO2);
            O2 = result.spO2;

            if (O2 < 90)
                https_telegram_sendMessage_perform_post("Oxígeno en sangre a menos del 90%");
            if (O2 < 85)
                https_telegram_sendMessage_perform_post("CUIDADO\nOxígeno en sangre a menos del 85%");

            // BPM
            publishDataNumber("BPM", result.heart_bpm);
            BPM = result.heart_bpm;

            if (BPM < 40)
                https_telegram_sendMessage_perform_post("Pulsaciones por debajo de 40 BPM");
            if (BPM > 120)
                https_telegram_sendMessage_perform_post("Pulsaciones por encima de 120 BPM");

        }
        //Update rate: 100Hz
        vTaskDelay(10/portTICK_PERIOD_MS);
    }
}

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    //ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        //ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        msg_id = esp_mqtt_client_publish(client, "/topic/qos1", "data_3", 0, 1, 0);
        //ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "/topic/qos0", 0);
        //ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "/topic/qos1", 1);
        //ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_unsubscribe(client, "/topic/qos1");
        //ESP_LOGI(TAG, "sent unsubscribe successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_DISCONNECTED:
        //ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        //ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
        //ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        //ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        //ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        //ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        break;
    case MQTT_EVENT_ERROR:
        //ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
            //ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));

        }
        break;
    default:
        //ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
    .uri = "mqtt://demo.thingsboard.io",
    .event_handle = mqtt_event_handler,
    .port = 1883,
    .username = "C4t4MkAqlOuVlrfQsdFE", //token
    };
#if CONFIG_BROKER_URL_FROM_STDIN
    char line[128];

    if (strcmp(mqtt_cfg.uri, "FROM_STDIN") == 0) {
        int count = 0;
        printf("Please enter url of mqtt broker\n");
        while (count < 128) {
            int c = fgetc(stdin);
            if (c == '\n') {
                line[count] = '\0';
                break;
            } else if (c > 0 && c < 127) {
                line[count] = c;
                ++count;
            }
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
        mqtt_cfg.uri = line;
        printf("Broker url: %s\n", line);
    } else {
        ESP_LOGE(TAG, "Configuration mismatch: wrong broker url");
        abort();
    }
#endif /* CONFIG_BROKER_URL_FROM_STDIN */

    //esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
    esp_mqtt_client_start(client);

    /*
    // Crear json que se quiere enviar al ThingsBoard
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "key", 0.336); // En la telemetría de Thingsboard aparecerá key = key y value = 0.336
    char *post_data = cJSON_PrintUnformatted(root);

    // Enviar los datos
    esp_mqtt_client_publish(client, "v1/devices/me/telemetry", post_data, 0, 1, 0); // v1/devices/me/telemetry sale de la MQTT Device API Reference de ThingsBoard
    cJSON_Delete(root);
    // Free is intentional, it's client responsibility to free the result of cJSON_Print
    free(post_data);
    */
}


void init_uart(void)
{
    const uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };

    // We won't use a buffer for sending data.
    uart_driver_install(UART, RX_BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART, &uart_config);
    uart_set_pin(UART, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

double convert_num_fixed(double num) {
    int grados = (int) num / 100;
    double minutos = num - (grados * 100);
    num = grados + (minutos/60);
    return num;
}

static void parse(char * line) {
    /*
    if (minmea_check(line, false)) {
        printf("TRUE");
    }
    */
    switch(minmea_sentence_id(line, false)) {
        /*
        case MINMEA_SENTENCE_RMC:
            ESP_LOGI(tag, "RMC");
            ESP_LOGD(tag, "Sentence - MINMEA_SENTENCE_RMC");
            struct minmea_sentence_rmc frame_rmc;
            if (minmea_parse_rmc(&frame_rmc, line)) {
                ESP_LOGD(tag, "$xxRMC: raw coordinates and speed: (%d/%d,%d/%d) %d/%d",
                         frame_rmc.latitude.value, frame_rmc.latitude.scale,
                         frame_rmc.longitude.value, frame_rmc.longitude.scale,
                         frame_rmc.speed.value, frame_rmc.speed.scale);
                ESP_LOGD(tag, "$xxRMC fixed-point coordinates and speed scaled to three decimal places: (%d,%d) %d",
                         minmea_rescale(&frame_rmc.latitude, 1000),
                         minmea_rescale(&frame_rmc.longitude, 1000),
                         minmea_rescale(&frame_rmc.speed, 1000));
                ESP_LOGD(tag, "$xxRMC floating point degree coordinates and speed: (%f,%f) %f",
                         minmea_tocoord(&frame_rmc.latitude),
                         minmea_tocoord(&frame_rmc.longitude),
                         minmea_tofloat(&frame_rmc.speed));
            }
            else {
                ESP_LOGD(tag, "$xxRMC sentence is not parsed\n");
            }
            break;
        */

        case MINMEA_SENTENCE_GGA: {
            //ESP_LOGI(tag, "GGA");
            struct minmea_sentence_gga frame_gga;
            if (minmea_parse_gga(&frame_gga, line)) {
                /*
                ESP_LOGI(tag, "$xxGGA: Latitude:Longitude %f:%f Time: %d:%d:%d\n",
                         minmea_tofloat(&frame_gga.latitude),
                         minmea_tofloat(&frame_gga.longitude),
                         frame_gga.time.hours,
                         frame_gga.time.minutes,
                         frame_gga.time.seconds);
                */
                double lat = convert_num_fixed(((double) minmea_tofloat(&frame_gga.latitude)));
                double lon = convert_num_fixed(((double) minmea_tofloat(&frame_gga.longitude)));
                if (isnan(lat))
                    lat = 0;
                if (isnan(lon))
                    lon = 0;
                //printf("LAT,LON: %f, %f", lat, lon);
                LATITUD = lat;
                LONGITUD = lon;

                publishDataNumber("latitude", lat);
                publishDataNumber("longitude", lon);
            }
            else {
                ESP_LOGI(tag, "$xxGGA sentence is not parsed\n");
            }
        } break;

        default:
            //ESP_LOGI(tag, "DEFAULT");
            //ESP_LOGD(tag, "Sentence - other");
            break;
    }
}


static void parse_line(char *line)
{
    char *p;
    int i;
    for (i = 1, p = strtok(line,"\n"); p != NULL; p = strtok(NULL,"\n"), i++) {
        //printf("Output%u=%s;\n", i, p);
        //ESP_LOGI(tag, "%u>>>>>>>>>>>>>>>> %s",i , p);
        parse(p);
    }
    //ESP_LOGI(tag, "--------------------------------------------------------------");
}

static void rx_task(void *arg)
{
    static const char *RX_TASK_TAG = "RX_TASK";
    esp_log_level_set(RX_TASK_TAG, ESP_LOG_INFO);
    while (1) {
        uint8_t* data = (uint8_t*) malloc(RX_BUF_SIZE+1);
        int length = 0;
        ESP_ERROR_CHECK(uart_get_buffered_data_len(UART, (size_t*)&length));
        length = uart_read_bytes(UART, data, RX_BUF_SIZE, 500 / portTICK_RATE_MS);
        //ESP_LOGI(RX_TASK_TAG, "Read %d bytes: '%s'", length, data);
        parse_line((char *)data);
        free(data);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void button_task(void *pvParameter) {
    gpio_config_t button_config = {
            .pin_bit_mask = 1LL << BUTTON_GPIO,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE
    };
    gpio_config(&button_config);

    while (1) {
        if (gpio_get_level(BUTTON_GPIO) == 1) {
            //printf("BOTÓN DE EMERGENCIA ACTIVADO\n");
            https_telegram_sendMessage_perform_post("BOTÓN DE EMERGENCIA ACTIVADO");
            //Esperar a que el botón sea soltado
            //while (gpio_get_level(BUTTON_GPIO) == 0) {};
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);

    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
    esp_log_level_set("MQTT_EXAMPLE", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_BASE", ESP_LOG_VERBOSE);
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    esp_log_level_set("OUTBOX", ESP_LOG_VERBOSE);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());


    //Init I2C_NUM_0
    ESP_ERROR_CHECK(i2c_master_init(I2C_PORT));
    //Init sensor at I2C_NUM_0
    ESP_ERROR_CHECK(max30100_init( &max30100, I2C_PORT,
                                   MAX30100_DEFAULT_OPERATING_MODE,
                                   MAX30100_DEFAULT_SAMPLING_RATE,
                                   MAX30100_DEFAULT_LED_PULSE_WIDTH,
                                   MAX30100_DEFAULT_IR_LED_CURRENT,
                                   MAX30100_DEFAULT_START_RED_LED_CURRENT,
                                   MAX30100_DEFAULT_MEAN_FILTER_SIZE,
                                   MAX30100_DEFAULT_PULSE_BPM_SAMPLE_SIZE,
                                   true, false ));


    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());

    mqtt_app_start();

    init_uart();

    //Start test task
    // GPS
    xTaskCreate(rx_task, "uart_rx_task", 8192, NULL, configMAX_PRIORITIES-4, NULL);

    // SPI
    spi_device_handle_t spi;
    spi = spi_init();
    xTaskCreate(ACL2_task, "ACL_task", 8192, spi, configMAX_PRIORITIES-2, NULL);

    // BPM
    xTaskCreate(get_bpm, "Get BPM", 8192, NULL, configMAX_PRIORITIES-3, NULL);


    // BUTTON
    xTaskCreate(button_task, "button_task", 8192, NULL, configMAX_PRIORITIES-1, NULL);

    // TELEGRAM
    xTaskCreate(http_test_task, "telegram_task", 8192, NULL, 1, NULL);

}