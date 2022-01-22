/* Scan Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

/*
    This example shows how to scan for available set of APs
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include "esp_private/esp_clk.h"

#include "soc/soc_caps.h"

#include "unity.h"
#include "mbedtls/sha1.h"
#include "mbedtls/sha256.h"

#include "sha/sha_parallel_engine.h"
/* The examples use WiFi configuration that you can set via project configuration menu

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_ESP_WIFI_SSID      "Redmi Note 8T"
#define EXAMPLE_ESP_WIFI_PASS      "375f59e10fc2"
#define EXAMPLE_ESP_MAXIMUM_RETRY  10

#define APP_USERNAME "Ben"
uint8_t APP_PASSWORD [32]={ 0x16, 0xed, 0x3f, 0xe5, 0x3e, 0x22, 0xcb, 0x4e,
                                         0x1e, 0xb2, 0x29, 0x87, 0xdd, 0x4e, 0xdb, 0xcc,
                                         0xba, 0xc4, 0xd5, 0xb3, 0x4d, 0x70, 0xe9, 0x4f,
                                         0xa1, 0x2d, 0xc1, 0x49, 0x2d, 0x59, 0x5f, 0x85,
                                         };

/*Configuration for TCP connexion*/
#define PORT                        4096
/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

#define DEFAULT_SCAN_LIST_SIZE CONFIG_EXAMPLE_SCAN_LIST_SIZE

static const char *TAGSCAN = "scan";

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAGCONNECTION = "wifi station";

static int s_retry_num = 0;

static const char *TAGSOCKET = "socket";

/*Task for TCP socket, STUB*/
static void do_retransmit(const int sock)
{
    int len;

    char username[32];
    size_t password_size = sizeof(unsigned char)*11;
    unsigned char * password =  heap_caps_malloc(password_size, MALLOC_CAP_8BIT);
    uint8_t   passwordsha[32] = {0};
    int written = send(sock, "Username\n", sizeof("Username\n"), 0);
    if (written < 0) {
        ESP_LOGE(TAGSOCKET, "Error occurred during sending: errno %d", errno);
        
        return;
    }
    len = recv(sock,username,sizeof(username)-1,0);
    username[strcspn((char *)username,"\n")]=0;
     if (len < 0) {
        ESP_LOGE(TAGSOCKET, "Error occurred during receiving: errno %d", errno);
        return;
    } else if (len == 0) {
        ESP_LOGW(TAGSOCKET, "Connection closed");
        return;
    } else {
        if(!strcmp(username,APP_USERNAME)) {
            ESP_LOGI(TAGSOCKET, "Username is correct");
        }
        else {
            written = send(sock, "Incorrect Username\n", sizeof("Incorrect Username\n"), 0);
              if (written < 0) {
                    ESP_LOGE(TAGSOCKET, "Error occurred during sending: errno %d", errno);
        
                    return;
                }
            ESP_LOGI(TAGSOCKET,"Incorrect username , quitting...");
            
            return;
        }
    }
    written = send(sock, "Password\n", sizeof("Password\n"), 0);
    if (written < 0) {
        ESP_LOGE(TAGSOCKET, "Error occurred during sending: errno %d", errno);
        
        return;
    }

    len = recv(sock,password,password_size-1,0);
    password[password_size-1]=0;
     if (len < 0) {
        ESP_LOGE(TAGSOCKET, "Error occurred during receiving: errno %d", errno);
        return;
    } else if (len == 0) {
        ESP_LOGW(TAGSOCKET, "Connection closed");
        return;
    } else {        
        esp_sha(SHA2_256,password,password_size-1,passwordsha);     
        for (int ptr=0; ptr < sizeof(APP_PASSWORD);ptr++) {
            if (*(APP_PASSWORD+ptr)!=*(passwordsha+ptr)) {
                written = send(sock, "Incorrect Password\n", sizeof("Incorrect Password\n"), 0);
                if (written < 0) {
                    ESP_LOGE(TAGSOCKET, "Error occurred during sending: errno %d", errno);
        
                    return;
                }
                ESP_LOGI(TAGSOCKET,"Incorrect password , quitting...");
            
            return;
            }
        }
            ESP_LOGI(TAGSOCKET, "Password is correct");
        
    }while (1) {
    
        int written = send(sock, "24 ° C\n", sizeof("24 ° C\n"), 0);
        if (written < 0) {
            ESP_LOGE(TAGSOCKET, "Error occurred during sending: errno %d", errno);
            return;
        }
        vTaskDelay(1000);
    }
    
}
/*TCP socket creation and connection
   Will maybe will also be needed for UDP*/
static void tcp_server_task(void *pvParameters)
{
    char addr_str[128];
    int addr_family = (int)pvParameters;
    int ip_protocol = 0;
    int keepAlive = 1;
    struct sockaddr_storage dest_addr;

    if (addr_family == AF_INET) {
        struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
        dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr_ip4->sin_family = AF_INET;
        dest_addr_ip4->sin_port = htons(PORT);
        ip_protocol = IPPROTO_IP;
    }

    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0) {
        ESP_LOGE(TAGSOCKET, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    ESP_LOGI(TAGSOCKET, "Socket created");

    int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAGSOCKET, "Socket unable to bind: errno %d", errno);
        ESP_LOGE(TAGSOCKET, "IPPROTO: %d", addr_family);
        goto CLEAN_UP;
    }
    ESP_LOGI(TAGSOCKET, "Socket bound, port %d", PORT);

    err = listen(listen_sock, 1);
    if (err != 0) {
        ESP_LOGE(TAGSOCKET, "Error occurred during listen: errno %d", errno);
        goto CLEAN_UP;
    }

    while (1) {

        ESP_LOGI(TAGSOCKET, "Socket listening");

        struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
        socklen_t addr_len = sizeof(source_addr);
        int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (sock < 0) {
            ESP_LOGE(TAGSOCKET, "Unable to accept connection: errno %d", errno);
            break;
        }

        // Set tcp keepalive option
        setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
        // Convert ip address to string
        if (source_addr.ss_family == PF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
        }
        ESP_LOGI(TAGSOCKET, "Socket accepted ip address: %s", addr_str);

        do_retransmit(sock);

        shutdown(sock, 0);
        close(sock);
    }

CLEAN_UP:
    close(listen_sock);
    vTaskDelete(NULL);
}

/*Connection to the Acess Point*/
static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAGCONNECTION, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAGCONNECTION,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAGCONNECTION, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/*Auth modes of the wifi when scanning*/
static void print_auth_mode(int authmode)
{
    switch (authmode) {
    case WIFI_AUTH_OPEN:
        ESP_LOGI(TAGSCAN, "Authmode \tWIFI_AUTH_OPEN");
        break;
    case WIFI_AUTH_WEP:
        ESP_LOGI(TAGSCAN, "Authmode \tWIFI_AUTH_WEP");
        break;
    case WIFI_AUTH_WPA_PSK:
        ESP_LOGI(TAGSCAN, "Authmode \tWIFI_AUTH_WPA_PSK");
        break;
    case WIFI_AUTH_WPA2_PSK:
        ESP_LOGI(TAGSCAN, "Authmode \tWIFI_AUTH_WPA2_PSK");
        break;
    case WIFI_AUTH_WPA_WPA2_PSK:
        ESP_LOGI(TAGSCAN, "Authmode \tWIFI_AUTH_WPA_WPA2_PSK");
        break;
    case WIFI_AUTH_WPA2_ENTERPRISE:
        ESP_LOGI(TAGSCAN, "Authmode \tWIFI_AUTH_WPA2_ENTERPRISE");
        break;
    case WIFI_AUTH_WPA3_PSK:
        ESP_LOGI(TAGSCAN, "Authmode \tWIFI_AUTH_WPA3_PSK");
        break;
    case WIFI_AUTH_WPA2_WPA3_PSK:
        ESP_LOGI(TAGSCAN, "Authmode \tWIFI_AUTH_WPA2_WPA3_PSK");
        break;
    default:
        ESP_LOGI(TAGSCAN, "Authmode \tWIFI_AUTH_UNKNOWN");
        break;
    }
}

/*Cypher type of Wifi when scanning*/
static void print_cipher_type(int pairwise_cipher, int group_cipher)
{
    switch (pairwise_cipher) {
    case WIFI_CIPHER_TYPE_NONE:
        ESP_LOGI(TAGSCAN, "Pairwise Cipher \tWIFI_CIPHER_TYPE_NONE");
        break;
    case WIFI_CIPHER_TYPE_WEP40:
        ESP_LOGI(TAGSCAN, "Pairwise Cipher \tWIFI_CIPHER_TYPE_WEP40");
        break;
    case WIFI_CIPHER_TYPE_WEP104:
        ESP_LOGI(TAGSCAN, "Pairwise Cipher \tWIFI_CIPHER_TYPE_WEP104");
        break;
    case WIFI_CIPHER_TYPE_TKIP:
        ESP_LOGI(TAGSCAN, "Pairwise Cipher \tWIFI_CIPHER_TYPE_TKIP");
        break;
    case WIFI_CIPHER_TYPE_CCMP:
        ESP_LOGI(TAGSCAN, "Pairwise Cipher \tWIFI_CIPHER_TYPE_CCMP");
        break;
    case WIFI_CIPHER_TYPE_TKIP_CCMP:
        ESP_LOGI(TAGSCAN, "Pairwise Cipher \tWIFI_CIPHER_TYPE_TKIP_CCMP");
        break;
    default:
        ESP_LOGI(TAGSCAN, "Pairwise Cipher \tWIFI_CIPHER_TYPE_UNKNOWN");
        break;
    }

    switch (group_cipher) {
    case WIFI_CIPHER_TYPE_NONE:
        ESP_LOGI(TAGSCAN, "Group Cipher \tWIFI_CIPHER_TYPE_NONE");
        break;
    case WIFI_CIPHER_TYPE_WEP40:
        ESP_LOGI(TAGSCAN, "Group Cipher \tWIFI_CIPHER_TYPE_WEP40");
        break;
    case WIFI_CIPHER_TYPE_WEP104:
        ESP_LOGI(TAGSCAN, "Group Cipher \tWIFI_CIPHER_TYPE_WEP104");
        break;
    case WIFI_CIPHER_TYPE_TKIP:
        ESP_LOGI(TAGSCAN, "Group Cipher \tWIFI_CIPHER_TYPE_TKIP");
        break;
    case WIFI_CIPHER_TYPE_CCMP:
        ESP_LOGI(TAGSCAN, "Group Cipher \tWIFI_CIPHER_TYPE_CCMP");
        break;
    case WIFI_CIPHER_TYPE_TKIP_CCMP:
        ESP_LOGI(TAGSCAN, "Group Cipher \tWIFI_CIPHER_TYPE_TKIP_CCMP");
        break;
    default:
        ESP_LOGI(TAGSCAN, "Group Cipher \tWIFI_CIPHER_TYPE_UNKNOWN");
        break;
    }
}

/* Initialize Wi-Fi as sta and set scan method */
static void wifi_socket(void)
{

    s_wifi_event_group = xEventGroupCreate();


    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
   
    uint16_t number = DEFAULT_SCAN_LIST_SIZE;
    wifi_ap_record_t ap_info[DEFAULT_SCAN_LIST_SIZE];
    uint16_t ap_count = 0;
    memset(ap_info, 0, sizeof(ap_info));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_scan_start(NULL, true);
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&number, ap_info));
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
    ESP_LOGI(TAGSCAN, "Total APs scanned = %u", ap_count);
    for (int i = 0; (i < DEFAULT_SCAN_LIST_SIZE) && (i < ap_count); i++) {
        ESP_LOGI(TAGSCAN, "SSID \t\t%s", ap_info[i].ssid);
        ESP_LOGI(TAGSCAN, "RSSI \t\t%d", ap_info[i].rssi);
        print_auth_mode(ap_info[i].authmode);
        if (ap_info[i].authmode != WIFI_AUTH_WEP) {
            print_cipher_type(ap_info[i].pairwise_cipher, ap_info[i].group_cipher);
        }
        ESP_LOGI(TAGSCAN, "Channel \t\t%d\n", ap_info[i].primary);
        if(!strcmp((char *)ap_info[i].ssid,EXAMPLE_ESP_WIFI_SSID)) {

            ESP_LOGI(TAGCONNECTION, "FOUNDED EXPECTED SSID, trying to connect...");
            wifi_config_t wifi_config = {
                .sta = {
                    .ssid = EXAMPLE_ESP_WIFI_SSID,
                    .password = EXAMPLE_ESP_WIFI_PASS,
                    /* Setting a password implies station will connect to all security modes including WEP/WPA.
                     * However these modes are deprecated and not advisable to be used. Incase your Access point
                    * doesn't support WPA2, these mode can be enabled by commenting below line */
	            .threshold.authmode = WIFI_AUTH_WPA2_PSK,

                    .pmf_cfg = {
                        .capable = true,
                        .required = false
                    },
                },
            };

            esp_event_handler_instance_t instance_any_id;
            esp_event_handler_instance_t instance_got_ip;
            ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                ESP_EVENT_ANY_ID,
                                                &event_handler,
                                                NULL,
                                                &instance_any_id));
            ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                IP_EVENT_STA_GOT_IP,
                                                &event_handler,
                                                NULL,
                                                &instance_got_ip));
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
            ESP_ERROR_CHECK(esp_wifi_stop());
            ESP_ERROR_CHECK(esp_wifi_start());
            ESP_LOGI(TAGCONNECTION,"Wifi connection intialisation finished");
            /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
            * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
            EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);
            ESP_LOGI(TAGCONNECTION,"Event passed");
               /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
                * happened. */
             if (bits & WIFI_CONNECTED_BIT) {
                 ESP_LOGI(TAGCONNECTION, "connected to ap SSID:%s password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
            } else if (bits & WIFI_FAIL_BIT) {
                ESP_LOGI(TAGCONNECTION, "Failed to connect to SSID:%s, password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
            } else {
                ESP_LOGE(TAGCONNECTION, "UNEXPECTED EVENT");
            }

            /* The event will not be processed after unregister */
            ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
            ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
            vEventGroupDelete(s_wifi_event_group);
        }


    }

}

void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );
    wifi_socket();

    xTaskCreate(tcp_server_task, "tcp_server", PORT, (void*)AF_INET, 5, NULL);
}
