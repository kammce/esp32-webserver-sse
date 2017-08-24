#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "freertos/portmacro.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "tcpip_adapter.h"
#include "http_parser.h"

static EventGroupHandle_t wifi_event_group;
const int CONNECTED_BIT = BIT0;
static char* TAG = "webserver-see";

#define READY 1
#define BUSY  0

#include "lwip/err.h"
#include "string.h"

#define LED_BUILTIN 16
#define delay(ms) (vTaskDelay(ms/portTICK_RATE_MS))
char* json_unformatted;
const static char http_html_hdr[] =
    "HTTP/1.1 200 OK\r\nAccess-Control-Allow-Origin: *\r\nContent-type: text/html\r\n\r\n";
const static char http_sse_hdr[] =
    "HTTP/1.1 200 OK\r\nAccess-Control-Allow-Origin: *\r\nContent-Type: text/event-stream\r\nCache-Control: no-cache\r\nConnection: keep-alive\r\n";

const static char http_index_html[] = "<!DOCTYPE html>"
                                     "<html>\n"
                                     "<head>\n"
                                     "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
                                     "  <style type=\"text/css\">\n"
                                     "    html, body, iframe { margin: 0; padding: 0; height: 100%; }\n"
                                     "    iframe { display: block; width: 100%; border: none; }\n"
                                     "  </style>\n"
                                     "<title>HELLO ESP32</title>\n"
                                     "</head>\n"
                                     "<body>\n"
                                     "<h1>Hello World, from ESP32!</h1>\n"
                                     "</body>\n"
                                     "</html>\n";
const static char http_sse_html[]  = "<!DOCTYPE html>\n"
                                        "<html>\n"
                                        "<head>\n"
                                        "   <title>ESP32 SSE Example</title>\n"
                                        "   <style type='text/css'>\n"
                                        "       html, body { height: 100%; background: #f9f9f9; }\n"
                                        "       body { font-family: 'Courier New', Courier, monospace; }\n"
                                        "       #container\n"
                                        "       {\n"
                                        "           position: fixed;\n"
                                        "           top: 50%;\n"
                                        "           left: 50%;\n"
                                        "           transform: translate(-50%, -50%);\n"
                                        "           width: 80%;\n"
                                        "           height: 80%;\n"
                                        "           background: white;\n"
                                        "           border-radius: 10px;\n"
                                        "           display: flex;\n"
                                        "           align-items: center;\n"
                                        "           justify-content: center;\n"
                                        "           flex-direction: column;\n"
                                        "           min-height: 600px;\n"
                                        "       }\n"
                                        "       #container div\n"
                                        "       {\n"
                                        "           text-align: center;\n"
                                        "           width: 100%;\n"
                                        "       }\n"
                                        "       #container div button\n"
                                        "       {\n"
                                        "           text-align: center;\n"
                                        "           width: calc(100% / 3.2);\n"
                                        "           border-radius: 5px;\n"
                                        "           border: 1px solid #ccc;\n"
                                        "           background: white;\n"
                                        "           height: 50%;\n"
                                        "       }\n"
                                        "       #container div button:hover\n"
                                        "       {\n"
                                        "           background: #ccc;\n"
                                        "           cursor: pointer;\n"
                                        "       }\n"
                                        "       #container div input\n"
                                        "       {\n"
                                        "           border-radius: 5px;\n"
                                        "           height: 50%;\n"
                                        "           border: 1px solid #ccc;\n"
                                        "       }\n"
                                        "       #container div p { color: red; }\n"
                                        "       #server-data\n"
                                        "       {\n"
                                        "           height: 92.5%;\n"
                                        "           width:90%;\n"
                                        "           border-radius: 5px;\n"
                                        "           border: 1px solid #ccc;\n"
                                        "       }\n"
                                        "   </style>\n"
                                        "</head>\n"
                                        "<body>\n"
                                        "   <div id='container'>\n"
                                        "\n"
                                        "       <div style='height: 5%; margin-top: 2.5%;'>\n"
                                        "           <h2>XMLHttpRequest &amp; Server Sent Events</h2>\n"
                                        "       </div>\n"
                                        "       <div style='height: 7.5%;margin-top: 2.5%;'>\n"
                                        "           <button onclick='sendData()'>AJAX Send</button>\n"
                                        "           <button onclick='recieveData()'>Start Server Side Events</button>\n"
                                        "           <button onclick='toggleInterval()'>Toggle Interval</button>\n"
                                        "           <input name='client-data' id='client-data' type='text' style='width:90%' />\n"
                                        "       </div>\n"
                                        "       <div style='height: 3.5%;'> <p id='response'> response </p> </div>\n"
                                        "       <div style='height: 90%; margin-top: 2.5%;'>\n"
                                        "           <textarea id='server-data' ></textarea>\n"
                                        "       </div>\n"
                                        "   </div>\n"
                                        "</body>\n"
                                        "<script type='text/javascript'>\n"
                                        "    var source_sta, source_ap;\n"
                                        "    const URL = 'http://192.168.4.1';\n"
                                        "    // const URL = 'http://192.168.0.104';\n"
                                        "    var response = document.querySelector('#response');\n"
                                        "    var busy_counter = 0;\n"
                                        "\n"
                                        "    function recieveData()\n"
                                        "    {\n"
                                        "       if(source_ap != null)\n"
                                        "       {\n"
                                        "           response.innerHTML = 'Resetting Event Source';\n"
                                        "           source_ap.close();\n"
                                        "           source_ap = null;\n"
                                        "       }\n"
                                        "       response.innerHTML = 'Connecting...';\n"
                                        "       source_ap = new EventSource(URL);\n"
                                        "       source_ap.onopen = function()\n"
                                        "       {\n"
                                        "           response.innerHTML = 'Connected';\n"
                                        "       }\n"
                                        "       source_ap.onmessage = function(event)\n"
                                        "       {\n"
                                        "           console.log(event);\n"
                                        "           var text_area = document.querySelector('#server-data');\n"
                                        "           text_area.innerHTML = `id = ${event.lastEventId} :: data = ${event.data}\n` + text_area.innerHTML;\n"
                                        "       };\n"
                                        "       source_ap.onerror = function(event)\n"
                                        "       {\n"
                                        "           response.innerHTML = `failed to connect to ${URL}`;\n"
                                        "       };\n"
                                        "    }\n"
                                        "    function sendData()\n"
                                        "    {\n"
                                        "       var oReq = new XMLHttpRequest();\n"
                                        "       var value = document.querySelector('#client-data').value.replace(/ /g, '_');\n"
                                        "       oReq.open('GET', `${URL}?data=${value}`);\n"
                                        "       oReq.send();\n"
                                        "    }\n"
                                        "    var counter = 0;\n"
                                        "    var interval;\n"
                                        "    function toggleInterval()\n"
                                        "    {\n"
                                        "       console.log(interval);\n"
                                        "       if(interval)\n"
                                        "       {\n"
                                        "           clearInterval(interval);\n"
                                        "           interval = null;\n"
                                        "       }\n"
                                        "       else\n"
                                        "       {\n"
                                        "           interval = setInterval(() =>\n"
                                        "           {\n"
                                        "               var oReq = new XMLHttpRequest();\n"
                                        "               var value = `testing_${counter++}`;\n"
                                        "               console.log(value);\n"
                                        "               oReq.open('GET', `${URL}?data=${value}`);\n"
                                        "               oReq.send();\n"
                                        "           }, 200);\n"
                                        "       }\n"
                                        "    }\n"
                                        "</script>\n"
                                        "</html>\n";

const static char success[] = "success";
const static char failure[] = "failure";

#define MAX_CONNECTIONS 4
#define SPECIAL_STRING_LENGTH 64
struct tcp_pcb * clients[MAX_CONNECTIONS] = { NULL };
uint32_t connections = 0;
char special_string[SPECIAL_STRING_LENGTH] = "special";
portMUX_TYPE myMutex = portMUX_INITIALIZER_UNLOCKED;

SemaphoreHandle_t xSemaphore;

struct tcp_pcb * pcb;
uint32_t sse_connections = 0;
const static char reply_once[] = "id: 1\ndata: testing!\n\n\r\n";

#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/api.h"
#include "lwip/tcp.h"
#include "lwip/priv/tcp_priv.h"

enum HTTP_FIELDS
{
    DONT_CARE = -1,
    ACCEPT = 0,
    /* add more later */
};

typedef struct
{
    const char * str;
    uint32_t length;
} c_string;

typedef struct
{
    c_string url;
    c_string fields[1];
    int32_t field_pointer;
} http_structure;

/* HTTP Parsing Setup */
http_structure captured_http = { 0 };
http_parser_settings settings;
http_parser parser;

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch (event->event_id)
    {
        case SYSTEM_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
            printf("got ip\n");
            printf("ip: " IPSTR "\n", IP2STR(&event->event_info.got_ip.ip_info.ip));
            printf("netmask: " IPSTR "\n", IP2STR(&event->event_info.got_ip.ip_info.netmask));
            printf("gw: " IPSTR "\n", IP2STR(&event->event_info.got_ip.ip_info.gw));
            printf("\n");
            fflush(stdout);
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            /* This is a workaround as ESP32 WiFi libs don't currently
                 auto-reassociate.
                But for this project's purposes, do not reconnect!
            */
            // esp_wifi_connect();
            xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
            break;
        case SYSTEM_EVENT_AP_STADISCONNECTED:

            break;
        default:
            break;
    }
    return ESP_OK;
}

static void initialise_wifi(void)
{
    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_APSTA) );
    // ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    // ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_AP) );
    wifi_config_t sta_config =
    {
        .sta = {
            .ssid = "Chen Family",
            .password = "June15#a",
            .bssid_set = false
        }
    };
    wifi_config_t ap_config =
    {
        .ap = {
            .ssid = "DRIVE-SYSTEM-ESP32",
            .ssid_len = 0,
            .password = "testing1234",
            .channel = 3,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .beacon_interval = 500,
            .max_connection = 16,
        }
    };
    ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &sta_config) );
    ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_AP, &ap_config) );
    ESP_ERROR_CHECK( esp_wifi_start() );
    ESP_ERROR_CHECK( esp_wifi_connect() );
}


int on_message_begin(http_parser* _)
{
    (void)_;
    ESP_LOGV(TAG, "\n***MESSAGE BEGIN***\n\n");
    return 0;
}

int on_headers_complete(http_parser* _)
{
    (void)_;
    ESP_LOGV(TAG, "\n***HEADERS COMPLETE***\n\n");
    return 0;
}

int on_message_complete(http_parser* _)
{
    (void)_;
    ESP_LOGV(TAG, "\n***MESSAGE COMPLETE***\n\n");
    return 0;
}

int on_url(http_parser* _, const char* at, size_t length)
{
    (void)_;
    ESP_LOGV(TAG, "Url: %.*s\n", (int)length, at);
    captured_http.url.str = at;
    captured_http.url.length = length;
    return 0;
}

int on_header_field(http_parser* _, const char* at, size_t length)
{
    (void)_;
    ESP_LOGV(TAG, "Header field: %.*s\n", (int)length, at);
    if (strncmp("Accept", at, length) == 0)
    {
        ESP_LOGV(TAG, "======== !ACCEPT FOUND! ========\n");
        captured_http.field_pointer = ACCEPT;
    }
    else
    {
        captured_http.field_pointer = DONT_CARE;
    }
    return 0;
}

int on_header_value(http_parser* _, const char* at, size_t length)
{
    (void)_;
    ESP_LOGV(TAG, "Header value: %.*s\n", (int)length, at);
    if (captured_http.field_pointer != DONT_CARE)
    {
        captured_http.fields[captured_http.field_pointer].str = at;
        captured_http.fields[captured_http.field_pointer].length = length;
    }
    return 0;
}

int on_body(http_parser* _, const char* at, size_t length)
{
    (void)_;
    ESP_LOGV(TAG, "Body: %.*s\n", (int)length, at);
    return 0;
}

bool c_string_cmp(c_string str1, const char * str2)
{
    return (strncmp(str1.str, str2, str1.length) == 0);
}

bool c_string_contains(c_string str1, const char * str2)
{
    return (strstr(str1.str, str2) != NULL);
}

void reset_captured_http()
{
    captured_http.url.str = NULL;
    captured_http.url.length = 0;
    captured_http.fields[ACCEPT].str = NULL;
    captured_http.fields[ACCEPT].length = 0;
}

#define EXTRACT_URL_TEMPLATE_SIZE 128
bool extract_url_variable(const char * variable,
                          const char * url,
                          uint32_t url_length,
                          char * dest,
                          uint32_t dest_length)
{
    bool success = false;

    char template[EXTRACT_URL_TEMPLATE_SIZE] = { 0 };

    char * variable_found = strstr(url, variable);
    if(variable_found != NULL)
    {
        snprintf(template, EXTRACT_URL_TEMPLATE_SIZE, "%s=%%%d[^&\\ \n\r]", variable, dest_length);
        sscanf(variable_found, (const char *)template, dest);
        success = true;
    }
    return success;
}

// bool add_sse_client(struct tcp_pcb * tpcb)
// {
//     bool success = false;
//     if(sse_connections < MAX_CONNECTIONS)
//     {
//         //// 75 seconds
//         tpcb->so_options |= SOF_KEEPALIVE;
//         tpcb->keep_intvl = 75000;
//         sse_connections++;
//         printf("Added SSE, count = %d\n", sse_connections);
//         success = true;
//     }
//     else
//     {
//         printf("Refused! SSE Channels full! (%d)\n", sse_connections);
//     }
//     return success;
// }

bool add_sse_client(struct tcp_pcb * tpcb)
{
    bool success = false;
    if(xSemaphoreTake( xSemaphore, portMAX_DELAY ) == pdTRUE )
    {
        printf("add_sse_client) TOOK!\n");
        for (int i = 0; i < MAX_CONNECTIONS; ++i)
        {
            if (clients[i] == NULL)
            {
                //// 75 seconds
                tpcb->so_options |= SOF_KEEPALIVE;
                tpcb->keep_intvl = 75000;
                clients[i] = tpcb;
                printf("Added SSE to Channel (%d)\n", i);
                success = true;
                break;
            }
        }
        xSemaphoreGive( xSemaphore );
        printf("add_sse_client) GIVE!\n");
    }
    if(!success)
    {
        printf("Refused! SSE Channels full!\n");
    }
    return success;
}

uint32_t sse_id = 0;

err_t polling_callback(void * arg, struct tcp_pcb * tpcb)
{
    char sse_buffer[128] = { 0 };

    err_t error = ERR_OK;
    uint32_t * arg_changer = arg;

    // if(arg != NULL && tpcb->unacked == NULL && tpcb->unsent == NULL)
    if(arg != NULL)
    {
        if(*arg_changer == READY)
        {
            uint16_t amount = tcp_sndbuf(tpcb);
            sprintf(sse_buffer, "id: %08X\ndata: amount = %d :: 0x%X :: %s\n\n\r\n", sse_id, amount, tpcb, special_string);
            error = tcp_write(tpcb, sse_buffer, strlen(sse_buffer) - 1, TCP_WRITE_FLAG_COPY);
            *arg_changer = BUSY;
            printf(sse_buffer);
            if (error != ERR_OK)
            {
                printf("POLLING) connection # shows error %d, closing connections\n", error);
                tcp_close(tpcb);
                mem_free(arg);
                sse_connections--;
            }
            else
            {
                // vTaskDelayUntil(10);
                polling_callback(arg, tpcb);
                tcp_output(tpcb);
            }
        }
    }
    return error;
}

err_t recieved_callback(void * arg, struct tcp_pcb * tpcb, struct pbuf * p, err_t err)
{
    bool close_flag = true;
    if (err == ERR_OK && p != NULL)
    {
        // printf("payload length = %d\n", p->len);
        // printf("test_buffer = %.*s\n", p->len, p->payload);

        // tcp_write(tpcb, http_html_hdr, sizeof(http_html_hdr)-1, 0);
        // tcp_write(tpcb, success, sizeof(success)-1, 0);
        // tcp_recved(tpcb, p->len);
        // pbuf_free(p);
        // tcp_close(tpcb);
        // return ERR_OK;

        tcp_recved(tpcb, p->len);

        //// Parse http request
        size_t nparsed = http_parser_execute(&parser, &settings, p->payload, p->len);
        if (nparsed != (size_t)p->len)
        {
            fprintf(stderr,
                    "Error: %s (%s)\n",
                    http_errno_description(HTTP_PARSER_ERRNO(&parser)),
                    http_errno_name(HTTP_PARSER_ERRNO(&parser)));
            tcp_close(tpcb);
            return ERR_OK;
        }

        /* Output captured http request information */
        // printf("url = %.*s\n", captured_http.url.length, captured_http.url.str);
        // printf("fields[ACCEPT] = %.*s\n", captured_http.fields[ACCEPT].length, captured_http.fields[ACCEPT].str);

        if(c_string_cmp(captured_http.fields[ACCEPT], "text/event-stream"))
        {
            if(add_sse_client(tpcb))
            {
                tcp_write(tpcb, http_sse_hdr, sizeof(http_sse_hdr) - 1, 0);
                // uint32_t * newarg = (uint32_t *) mem_malloc(sizeof(uint32_t));
                // *newarg = READY;
                // tcp_arg(tpcb, newarg);
                // tcp_poll(tpcb, polling_callback, 0);
            }
            close_flag = false;
        }
        else
        {
            //// Beyound this point, I am sending html data back!
            tcp_write(tpcb, http_html_hdr, sizeof(http_html_hdr)-1, 0);
            //// More cases
            if(c_string_contains(captured_http.url, "/?"))
            {
                bool success_flag = extract_url_variable(
                                    "data",
                                    captured_http.url.str,
                                    captured_http.url.length,
                                    special_string,
                                    SPECIAL_STRING_LENGTH
                                );
                printf("%d)%s\n", success_flag, special_string);
                if(success_flag)
                {
                    tcp_write(tpcb, success, sizeof(success)-1, 0);
                }
                else
                {
                    tcp_write(tpcb, failure, sizeof(failure)-1, 0);
                }
            }
            else if(c_string_cmp(captured_http.url, "/"))
            {
                tcp_write(tpcb, http_index_html, sizeof(http_index_html)-1, 0);
            }
            else if(c_string_cmp(captured_http.url, "/sse"))
            {
                tcp_write(tpcb, http_sse_html, sizeof(http_sse_html)-1, 0);
            }
        }

        reset_captured_http();
        pbuf_free(p);
    }
    else
    {
        // ESP_LOGV(TAG, "Null Payload Detected || err = %d \n", err);
        printf("Null Payload Detected || err = %d \n", err);
        if(arg != NULL)
        {
            mem_free(arg);
            tcp_arg(tpcb, NULL);
            sse_connections--;
            printf("NULL) connections %d, closing connections\n", sse_connections);
        }
    }

    if(close_flag)
    {
        tcp_recv(tpcb, NULL);
        err_t close_err = tcp_close(tpcb);
        if(close_err == ERR_MEM)
        {
            printf("=====================\nRan out of memory to close tpcb\n=====================\n");
        }
    }

    return ERR_OK;
}

void error_callabck(void * arg, err_t err)
{
    printf("ERROR OCCURED = %d :: %d\n", err, ESP_LWIP);
}

err_t sent_callback(void * arg, struct tcp_pcb * tpcb, u16_t len)
{
    ESP_LOGI(TAG, "Everything sent! Data length = %d\n", len);
    uint32_t * arg_changer = arg;
    if(arg != NULL)
    {
        *arg_changer = READY;
    }
    return ERR_OK;
}

err_t accept_callback(void * arg, struct tcp_pcb * newpcb, err_t err)
{
    tcp_accepted(pcb);
    tcp_arg(newpcb, NULL);
    tcp_recv(newpcb, recieved_callback);
    tcp_err(newpcb, error_callabck);
    tcp_sent(newpcb, sent_callback);
    return ERR_OK;
}

static void http_server(void *pvParameters)
{
    pcb = tcp_new();
    tcp_bind(pcb, NULL, 80);
    pcb = tcp_listen(pcb);
    if(pcb == NULL)
    {
        printf("PCB IS NULL AFTER TCP LISTEN!");
    }
    tcp_accept(pcb, accept_callback);
    tcp_nagle_disable(pcb);
    while(true)
    {
        // tcp_tmr();
        sse_id++;
        // printf(".");
        vTaskDelay(250);
    }
}

static void handle_sse()
{
    char sse_buffer[128] = { 0 };
    uint32_t sse_id = 0;
    while (true)
    {
        sys_prot_t pval = sys_arch_protect();
        for (int i = 0; i < MAX_CONNECTIONS; ++i)
        {
            if (clients[i] != NULL)
            {
                sprintf(sse_buffer, "id: %08X\ndata: connection # = %X :: %s\n\n\r\n", sse_id, i, special_string);
                err_t error = tcp_write(clients[i], sse_buffer, strlen(sse_buffer) - 1, 0);
                printf(sse_buffer);

                if (error != ERR_OK)
                {
                    printf("connection #%d shows error %d\n", i, error);
                    clients[i] = NULL;
                }
                else
                {
                    // tcp_output(clients[i]);
                }
            }
        }
        sys_arch_unprotect(pval);
        sse_id++;
        vTaskDelay(200);
    }
}

int app_main(void)
{
    memset(&settings, 0, sizeof(settings));
    settings.on_message_begin = on_message_begin;
    settings.on_url = on_url;
    settings.on_header_field = on_header_field;
    settings.on_header_value = on_header_value;
    settings.on_headers_complete = on_headers_complete;
    settings.on_body = on_body;
    settings.on_message_complete = on_message_complete;

    xSemaphore = xSemaphoreCreateMutex();
    xSemaphoreGive( xSemaphore );

    //// Initialize parser for HTTP Requests
    http_parser_init(&parser, HTTP_REQUEST);

    nvs_flash_init();
    // system_init();
    initialise_wifi();
    gpio_pad_select_gpio(LED_BUILTIN);
    /* Set the GPIO as a push/pull output */
    gpio_set_direction(LED_BUILTIN, GPIO_MODE_OUTPUT);
    // xTaskCreate(&generate_json, "json", 2048, NULL, 5, NULL);
    xTaskCreate(&handle_sse, "handle_sse", 4096+2048, NULL, 1, NULL);
    xTaskCreate(&http_server, "http_server", 4096+(2048*10), NULL, 1, NULL);
    return 0;
}