//===============================
// Header
//===============================
#include <stdint.h>
#include <string.h>

#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "driver/gpio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/event_groups.h"

#include "tcpip_adapter.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/api.h"
#include "lwip/tcp.h"
#include "lwip/priv/tcp_priv.h"
#include "lwip/priv/api_msg.h"
#include "lwip/err.h"

//===============================
// MACRO Constants
//===============================
#define LED_BUILTIN 16
#define MAX_CONNECTIONS 4
#define SPECIAL_STRING_LENGTH 64
#define EXTRACT_URL_TEMPLATE_SIZE 128
//===============================
// MACRO functions
//===============================
#define delay(ms) (vTaskDelay(ms/portTICK_RATE_MS))
//===============================
// Constants Field
//===============================
const static char* TAG = "webserver-see";

const static char http_html_hdr[] = "HTTP/1.1 200 OK\r\n"
                                    "Access-Control-Allow-Origin: *\r\n"
                                    "Content-type: text/html\r\n"
                                    "\r\n";
const static char http_sse_hdr[]  = "HTTP/1.1 200 OK\r\n"
                                    "Access-Control-Allow-Origin: *\r\n"
                                    "Content-Type: text/event-stream\r\n"
                                    "Cache-Control: no-cache\r\n"
                                    "Connection: keep-alive\r\n"
                                    "\r\n";

const static char http_index_html[] = "<!DOCTYPE html>"
                                      "<html>\n"
                                      "<head>\n"
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

const static char success[]         = "success";
const static char failure[]         = "failure";
const static char notfound_404[]    = "404 not found!";
const static char reply_once[] = "id: 1\ndata: testing!\n\n\r\n";
//===============================
// Structures and Enumerations
//===============================
//===============================
// Global Variables
//===============================
//// WiFi
static EventGroupHandle_t wifi_event_group;
const int CONNECTED_BIT = BIT0;
//// TCP Webserver Global Process Control Block
// struct tcp_pcb * global_pcb;
//// SSE Global Structure
struct netconn * clients[MAX_CONNECTIONS] = { NULL };
char special_string[SPECIAL_STRING_LENGTH] = "special";
uint32_t sse_connections = 0;
SemaphoreHandle_t xSemaphore;
//===============================
// Function Fields
//===============================
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
            .ssid = "SSE-ESP32-EXAMPLE",
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

bool extract_url_variable(const char * variable,
                          const char * url,
                          uint32_t url_length,
                          char * dest,
                          uint32_t dest_length)
{
    bool success = false;
    char template[EXTRACT_URL_TEMPLATE_SIZE] = { 0 };
    char * variable_found = strstr(url, variable);
    if (variable_found != NULL)
    {
        snprintf(template, EXTRACT_URL_TEMPLATE_SIZE, "%s=%%%d[^&\\ \n\r]", variable, dest_length);
        sscanf(variable_found, (const char *)template, dest);
        success = true;
    }
    return success;
}

bool add_sse_client(struct netconn * conn)
{
    bool success = false;
    bool semaphore_taken = false;
    semaphore_taken = xSemaphoreTake( xSemaphore, ( TickType_t ) 10 );
    if(semaphore_taken == pdTRUE)
    {
        for (int i = 0; i < MAX_CONNECTIONS; ++i)
        {
            if (clients[i] == NULL)
            {
                //// 75 seconds
                // conn->so_options |= SOF_KEEPALIVE;
                // conn->keep_intvl = 75000;
                clients[i] = conn;
                printf("Added SSE to Channel (%d)\n", i);
                success = true;
                break;
            }
        }
    }
    if (!success || semaphore_taken != pdTRUE)
    {
        printf("Refused! SSE Channels full!\n");
    }
    xSemaphoreGive(xSemaphore);
    return success;
}
uint32_t connections = 0;
static void http_server_netconn_serve(void *pvParameters)
{
    struct netconn *conn = (struct netconn *)pvParameters;
    struct netbuf *inbuf;
    char *buf;
    uint16_t buflen;
    bool close_flag = true;
    char url[256] = { 0 };
    char * is_event_stream = NULL;
    err_t err;

    /* Read the data from the port, blocking if nothing yet there.
     We assume the request (the part we care about) is in one netbuf */
    err = netconn_recv(conn, &inbuf);

    if (err == ERR_OK)
    {
        netbuf_data(inbuf, (void**)&buf, &buflen);
        //// NULL Terminate end of buffer
        buf[buflen] = '\0';
        //// Attempt to find "Accept: text/event-stream" string within request
        //// Adding the first and last "\r\n" means that this is not the first or last line
        is_event_stream = strstr(buf, "\r\nAccept: text/event-stream\r\n");
        //// Output captured http request information
        // printf("buf = %.*s\n", buflen, buf);
        // printf("ACCEPT = 0x%X\n", is_event_stream);

        if(is_event_stream != NULL)
        {
            if (add_sse_client(conn))
            {
                netconn_write(conn, http_sse_hdr, sizeof(http_sse_hdr) - 1, NETCONN_NOCOPY);
                close_flag = false;
            }
        }
        else
        {
            sscanf(buf, "GET %255s HTTP/1.1\n", url);
            // printf("url = %s\n", url);
            //// Beyond this point, I am only sending html data back!
            netconn_write(conn, http_html_hdr, sizeof(http_html_hdr) - 1, NETCONN_NOCOPY);
            //// More cases
            if (strstr(url, "/?") != NULL)
            {
                bool success_flag = extract_url_variable(
                    "data",
                    url,
                    strlen(url),
                    special_string,
                    SPECIAL_STRING_LENGTH
                );
                // printf("%d)%s\n", success_flag, special_string);
                if (success_flag)
                {
                    netconn_write(conn, success, sizeof(success) - 1, NETCONN_NOCOPY);
                }
                else
                {
                    netconn_write(conn, failure, sizeof(failure) - 1, NETCONN_NOCOPY);
                }
            }
            else if (strstr(url, "/sse") != NULL)
            {
                netconn_write(conn, http_sse_html, sizeof(http_sse_html) - 1, NETCONN_NOCOPY);
            }
            else if (strstr(url, "/") != NULL || strstr(url, "/index") != NULL)
            {
                netconn_write(conn, http_index_html, sizeof(http_index_html) - 1, NETCONN_NOCOPY);
            }
            else
            {
                netconn_write(conn, notfound_404, sizeof(notfound_404) - 1, NETCONN_NOCOPY);
            }
        }
    }
    else
    {
        printf("err = %d \n", err);
    }

    netbuf_delete(inbuf);

    if (close_flag)
    {
        netconn_close(conn);
        if(err == ERR_OK)
        {
            netconn_free(conn);
        }
        else
        {
            printf("DID NOT FREE NETCONN BECAUSE err != ERR_OK\n");
        }
    }

    connections--;

    /* Delete this thread, it is done now. Sleep precious child. */
    vTaskDelete(NULL);
}

inline char int_to_ascii_hex(uint32_t n)
{
    char c = '0';
    if(n < 10)
    {
        c = '0'+n;
    }
    else
    {
        c = 'A'+(n-10);
    }
    return c;
}

static void handle_sse()
{
    char sse_buffer[128] = { 0 };
    uint32_t sse_id = 0;
    while (true)
    {
        printf("ram=%d\n",esp_get_free_heap_size());
        sprintf(sse_buffer, "id: %08X\ndata: (_) :: %s\n\n\r\n", sse_id, special_string);

        for (int i = 0; i < MAX_CONNECTIONS; ++i)
        {
            if (clients[i] != NULL)
            {
                sse_buffer[20] = int_to_ascii_hex(i);
                err_t error = netconn_write(clients[i], sse_buffer, strlen(sse_buffer) - 1, NETCONN_COPY);
                // printf(sse_buffer);
                if (error != ERR_OK)
                {
                    printf("connection #%d shows error %d\n", i, error);
                    netconn_close(clients[i]);
                    netconn_delete(clients[i]);
                    clients[i] = NULL;
                }
            }
        }

        sse_id++;
        vTaskDelay(200);
    }
}

static void http_server(void *pvParameters)
{
    struct netconn *conn, *newconn;
    err_t err;
    conn = netconn_new(NETCONN_TCP);
    netconn_bind(conn, NULL, 80);
    netconn_listen(conn);
    do
    {
        err = netconn_accept(conn, &newconn);
        uint32_t tasks = uxTaskGetNumberOfTasks();
        if (err == ERR_OK)
        {
            connections++;
            // printf("new conn+task :: %d :: %d :: %d\n", tasks, connections, esp_get_free_heap_size());
            xTaskCreate(http_server_netconn_serve, "http_server_netconn_serve", 2048,  newconn, 5, NULL);
        }
    }
    while (err == ERR_OK);
    /* Delete Listening Server */
    netconn_close(conn);
    netconn_delete(conn);
}

int app_main(void)
{
    //// Initialize memory
    nvs_flash_init();
    xSemaphore = xSemaphoreCreateMutex();
    initialise_wifi();
    gpio_pad_select_gpio(LED_BUILTIN);
    gpio_set_direction(LED_BUILTIN, GPIO_MODE_OUTPUT);

    xTaskCreate(&handle_sse, "handle_sse", 4096, NULL, 1, NULL);
    xTaskCreate(&http_server, "http_server", 4096, NULL, 1, NULL);
    return 0;
}
