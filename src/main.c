/**
 * @file main.c
 * @author Arijit Sadhu (arijitsadhu@users.noreply.github.com)
 * @brief Pico-W IOT Example Project to demonstrate IOT using the pico-w
 * @version 0.1
 * @date 2024-03-05
 *
 * @copyright Copyright (c) 2024 Arijit Sadhu
 *
 */

/* INCLUDES ****************************************/

#include <stdio.h>
#include <time.h>

#include "hardware/adc.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "lwip/apps/httpd.h"
#include "lwip/apps/mdns.h"
#include "lwip/apps/mqtt.h"
#include "lwip/apps/sntp.h"
#include "pico/aon_timer.h"
#include "pico/binary_info.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

#include "bm.h"
#include "dhserver.h"
#include "dnserver.h"
#include "font.xbm"
#include "lwipopts.h"
#include "uc8151c.h"

/* MACROS ****************************************/

/**
 * @brief Wi-Fi connection timeout in ms
 *
 */
#define WIFI_TIMEOUT (15000)

/**
 * @brief Watchdog timeout in ms
 *
 * Should take in account Wi-Fi timout and display time
 */
#define WATCHDOG_TIMEOUT (30000)

/**
 * @brief Main loop poll interval in ms
 *
 */
#define POLL_INTERVAL (1000)

/**
 * @brief We're going to erase and reprogram a region 256k from the start of flash.
 *
 * Once done, we can access this at XIP_BASE + 256k.
 */
#define FLASH_TARGET_OFFSET (256 * 1024)

/**
 * @brief flash check
 *
 */
#define CONFIG_MAGIC (0x4c0ffe5)

/**
 * @brief seconds between 1 Jan 1900 and 1 Jan 1970
 *
 */
#define NTP_DELTA (2208988800)

#define INIT_IP4(a, b, c, d)               \
    {                                      \
        PP_HTONL(LWIP_MAKEU32(a, b, c, d)) \
    }

// button GPIO mappings
#define BTNA (12)
#define BTNB (13)
#define BTNC (14)

// GPIO event types
#define LEVEL_LOW (0x1)
#define LEVEL_HIGH (0x2)
#define EDGE_FALL (0x4)
#define EDGE_RISE (0x8)

#define TOUPPER(c) (((c >= 'a') && (c <= 'z')) ? (c - ('a' - 'A')) : (c))
#define HEXNUMBER(c) ((((c) < '0') || ((c) > 'F') || (((c) > '9') && ((c) < 'A'))) ? 0 : ((c) >= 'A') ? ((c) - ('0' + 7)) \
                                                                                                      : ((c) - '0'))

/* TYPES ****************************************/

extern cyw43_t cyw43_state;

/**
 * @brief Application states
 *
 */
typedef enum {
    ST_BOOT = 0,
    ST_CONNECT,
    ST_SETUP,
    ST_WAIT,
    ST_RETRY,
    ST_INIT,
    ST_RUN,
    ST_RESET,
    ST_MAX
} states_t;

/**
 * @brief uUser modes
 *
 */
typedef enum {
    MODE_OFF = 0,
    MODE_AUTO,
    MODE_ON,
    MODE_MAX
} modes_t;

/**
 * @brief HTTP SSI tags
 *
 */
typedef enum {
    TAG_NAME = 0,
    TAG_ADDR,
    TAG_TIME,
    TAG_MQTT_IP,
    TAG_SETUP,
    TAG_MODE,
    TAG_TEMP,
    TAG_THERM,
    TAG_TIMER1,
    TAG_TIMER2,
    TAG_OUT,
    TAG_MAX
} http_ssi_tags_t;

/**
 * @brief MQTT subscribed topics
 *
 */
typedef enum {
    TOPIC_MODE = 0,
    TOPIC_MAX
} mqtt_topics_t;

/**
 * @brief Flash-able Configuration Type
 *
 */
typedef union {
    struct {
        uint32_t magic; ///< Flash verification
        char ssid[33]; ///< Wi-Fi SSID
        char pass[65]; ///< Wi-Fi Password
        int tz; ///< Time-zone in minutes
        char mqtt_ip[40]; ///< MQTT IP Address
        modes_t mode; ///< User mode
        int8_t therm; ///< Thermostat temperature in C
        char timer1[6]; ///< Thermostat start time
        char timer2[6]; ///< Thermostat end time
    } data;
    uint8_t padding[FLASH_PAGE_SIZE];
} config_t;

/**
 * @brief Runtime Status type
 *
 */
typedef struct {
    states_t state; ///< State machine state
    bool run; ///< Program running
    bool save; ///< Configuration queued for save
    char name[sizeof(PROGRAM_NAME) + 9]; ///< Identity
    char addr[17]; ///< Own IP address
    char time[10]; ///< Current displayed time
    bool mqtt_con; ///< MQTT connected to server
    mqtt_topics_t mqtt_topic; ///< Last MQTT received publish topic
    float temp; ///< Current temperature
    uint32_t btna; ///< Button A state
    uint32_t btnb; ///< Button B state
    uint32_t btnc; ///< Button C state
    bool out; ///< Output is driven
} status_t;

/* FUNCTION PROTOTYPES ****************************************/

static const char* http_cgi_handler_basic(int iIndex, int iNumParams, char* pcParam[], char* pcValue[]);

/* GLOBAL VARIABLES ****************************************/

/* LOCAL VARIABLES ****************************************/

/**
 * @brief Flash address
 *
 */
static const uint8_t* flash_target_contents = (const uint8_t*)(XIP_BASE + FLASH_TARGET_OFFSET);

/**
 * @brief Configuration
 *
 */
static config_t config = {
    .data = {
        .magic = CONFIG_MAGIC,
        .ssid = WIFI_SSID,
        .pass = WIFI_PASSWORD,
        .tz = 0,
        .mqtt_ip = "",
        .mode = 0,
        .therm = 20,
        .timer1 = "00:00",
        .timer2 = "00:00",
    }
};

/**
 * @brief System status
 *
 */
static status_t status = {
    .state = ST_BOOT,
    .run = true,
    .save = false,
    .name = PROGRAM_NAME,
    .addr = "192.168.4.1",
    .time = "",
    .mqtt_con = false,
    .mqtt_topic = TOPIC_MAX,
    .temp = 20.0,
    .btna = 0,
    .btnb = 0,
    .btnc = 0,
    .out = false,
};

/**
 * @brief database IP addresses that can be offered to the host
 *
 * this must be in RAM to store assigned MAC addresses
 */
static dhcp_entry_t __not_in_flash("dhserver") dhcp_entries[] = {
    // mac   ip address                lease time
    { { 0 }, INIT_IP4(192, 168, 4, 2), 24 * 60 * 60 },
    { { 0 }, INIT_IP4(192, 168, 4, 3), 24 * 60 * 60 },
    { { 0 }, INIT_IP4(192, 168, 4, 4), 24 * 60 * 60 },
};

/**
 * @brief DHCP configuration
 *
 */
static const dhcp_config_t dhcp_config = {
    .router = INIT_IP4(192, 168, 4, 1), // router address (if any)
    .port = 67, // listen port
    .dns = INIT_IP4(192, 168, 4, 1), // dns server (if any)
    .domain = "", // dns suffix
    .num_entry = sizeof(dhcp_entries) / sizeof(dhcp_entries[0]), // num entry
    .entries = dhcp_entries // entries
};

/**
 * @brief HTTP SSI tags
 *
 * max length of the tags defaults to be 8 chars
 * LWIP_HTTPD_MAX_TAG_NAME_LEN
 */
static const char* __not_in_flash("httpd") http_ssi_tags[] = {
    [TAG_NAME] = "name",
    [TAG_ADDR] = "addr",
    [TAG_TIME] = "time",
    [TAG_MQTT_IP] = "mqtt_ip",
    [TAG_SETUP] = "setup",
    [TAG_MODE] = "mode",
    [TAG_TEMP] = "temp",
    [TAG_THERM] = "therm",
    [TAG_TIMER1] = "timer1",
    [TAG_TIMER2] = "timer2",
    [TAG_OUT] = "out",
};

/**
 * @brief CGI routing
 *
 * Base filename (URL) of a CGI and the associated function which is to be called when that URL is requested.
 */
static const tCGI http_cgi_handlers[] = {
    { "/", http_cgi_handler_basic },
};

/**
 * @brief MQTT Subscribed topics
 *
 */
static const char* __not_in_flash("mqtt") mqtt_topics[] = {
    [TOPIC_MODE] = "mode",
};

/**
 * @brief MQTT handle
 *
 */
static mqtt_client_t* mqtt_client = NULL;

/* LOCAL FUNCTIONS ****************************************/

/**
 * @brief Save config in flash
 *
 * @param config configuration data
 */
static void flash_config_save(config_t* config)
{
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_TARGET_OFFSET, (const uint8_t*)config, FLASH_PAGE_SIZE);
    restore_interrupts(ints);
}

/**
 * @brief Load config from flash
 *
 * @param config configuration data
 */
static void flash_config_load(config_t* config)
{
    if (CONFIG_MAGIC == ((config_t*)flash_target_contents)->data.magic) {
        memcpy(config, flash_target_contents, sizeof(config->data));
        printf("loaded cofiguration\n");
    }
}

/**
 * @brief Callback for GPIO button events
 *
 * @param gpio GPIO number
 * @param events event type
 */
static void gpio_callback(uint gpio, uint32_t events)
{
    if (BTNA == gpio) {
        status.btna = events;
    } else if (BTNB == gpio) {
        status.btnb = events;
    } else if (BTNC == gpio) {
        status.btnc = events;
    }
}

/**
 * @brief drive output pin
 *
 * @param en set output pin to high
 */
static void out(bool en)
{
    if (en) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
    } else {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
    }
}

/**
 * @brief URL decoder
 *
 * @param text
 */
static void http_cgi_urldecode(char* text)
{
    char* ptr = text;
    char c;
    int val;

    while ((c = *ptr) != '\0') {
        if (c == '+') {
            *text = ' ';
        } else if (c == '%') {
            c = *(++ptr);
            c = TOUPPER(c);
            val = (HEXNUMBER(c) << 4);
            c = *(++ptr);
            c = TOUPPER(c);
            val += HEXNUMBER(c);
            *text = (char)val;
        } else {
            *text = *ptr;
        }
        ++ptr;
        ++text;
    }
    *text = '\0';
}

/**
 * @brief HTTP SSI tag handler callback
 *
 * @param ssi id
 * @param pcinsert text insertion pointer
 * @param iInsertLen maximum inserted text length
 * @returns inserted text length
 */
static u16_t __time_critical_func(http_ssi_handler)(int iIndex, char* pcInsert, int iInsertLen)
{
    size_t printed;
    switch (iIndex) {
    case TAG_NAME:
        printed = snprintf(pcInsert, iInsertLen, "%s", status.name);
        break;
    case TAG_ADDR:
        printed = snprintf(pcInsert, iInsertLen, "%s", status.addr);
        break;
    case TAG_TIME:
        printed = snprintf(pcInsert, iInsertLen, "%s", status.time);
        break;
    case TAG_SETUP:
        printed = snprintf(pcInsert, iInsertLen, ST_RUN == status.state ? "false" : "true");
        break;
    case TAG_MQTT_IP:
        printed = snprintf(pcInsert, iInsertLen, "%s", config.data.mqtt_ip);
        break;
    case TAG_MODE:
        printed = snprintf(pcInsert, iInsertLen, "%d", config.data.mode);
        break;
    case TAG_TEMP:
        printed = snprintf(pcInsert, iInsertLen, "%.01f", status.temp);
        break;
    case TAG_THERM:
        printed = snprintf(pcInsert, iInsertLen, "%d", config.data.therm);
        break;
    case TAG_TIMER1:
        printed = snprintf(pcInsert, iInsertLen, "%s", config.data.timer1);
        break;
    case TAG_TIMER2:
        printed = snprintf(pcInsert, iInsertLen, "%s", config.data.timer2);
        break;
    case TAG_OUT:
        printed = snprintf(pcInsert, iInsertLen, status.out ? "true" : "false");
        break;
    }
    return (u16_t)printed;
}

/**
 * @brief HTTP CGI-handler triggered by a request
 *
 * @param iIndex http_cgi_handlers index
 * @param iNumParams number of parameters
 * @param pcParam parameters keys
 * @param pcValue parameters values
 * @return html file as http response
 */
static const char* http_cgi_handler_basic(int iIndex, int iNumParams, char* pcParam[], char* pcValue[])
{
    printf("cgi_handler_basic called with index %d and %d params\n", iIndex, iNumParams);

    for (int i = 0; i < iNumParams; i++) {
        http_cgi_urldecode(pcValue[i]);
        if (strcmp(pcParam[i], "ssid") == 0) {
            strncpy(config.data.ssid, pcValue[i], sizeof(config.data.ssid));
            status.state = ST_RETRY;
            status.save = true;
        } else if (strcmp(pcParam[i], "pass") == 0) {
            strncpy(config.data.pass, pcValue[i], sizeof(config.data.pass));
            status.state = ST_RETRY;
            status.save = true;
        } else if (strcmp(pcParam[i], "tz") == 0) {
            config.data.tz = atoi(pcValue[i]);
            status.save = true;
        } else if (strcmp(pcParam[i], "time") == 0) {
            time_t now = atoi(pcValue[i]);

            struct timeval tv = {
                .tv_sec = now
            };
            settimeofday(&tv, NULL);

            struct timespec ts = {
                .tv_sec = now
            };
            aon_timer_set_time(&ts);
        } else if (strcmp(pcParam[i], "mqtt_ip") == 0) {
            strncpy(config.data.mqtt_ip, pcValue[i], sizeof(config.data.mqtt_ip));
            // force reconnection
            if (status.mqtt_con) {
                mqtt_disconnect(mqtt_client);
                status.mqtt_con = false;
            }
        } else if (strcmp(pcParam[i], "mode") == 0) {
            uint8_t mode = atoi(pcValue[i]);
            if (MODE_MAX > mode) {
                config.data.mode = mode;
                status.save = true;
            }
        } else if (strcmp(pcParam[i], "therm") == 0) {
            config.data.therm = atoi(pcValue[i]);
            status.save = true;
        } else if (strcmp(pcParam[i], "timer1") == 0) {
            strncpy(config.data.timer1, pcValue[i], sizeof(config.data.timer1));
        } else if (strcmp(pcParam[i], "timer2") == 0) {
            strncpy(config.data.timer2, pcValue[i], sizeof(config.data.timer2));
        }
    }

    // Server redirect to clear get request
    return "/302.html";
}

/**
 * @brief mDNS Callback function to add text to a reply, called when generating the reply
 *
 * @param service mDNS instance
 * @param txt_userdata  user data (unused)
 */
static void mdns_srv_txt(struct mdns_service* service, void* txt_userdata)
{
    (void)txt_userdata;

    if (ERR_OK != mdns_resp_add_service_txtitem(service, "path=/", 6)) {
        printf("mdns add service txt failed res\n");
    }
}

/**
 * @brief mDNS callback function that is called if probing is completed successfully or with a conflict.
 *
 * @param netif network interface
 * @param result transaction result
 * @param service service number
 */
static void mdns_report(struct netif* netif, u8_t result, s8_t service)
{
    printf("mdns status[netif %d][service %d]: %d\n", netif->num, service, result);
}

/**
 * @brief DNS handle any requests from dns-server
 *
 * Set to own address in access point mode to force open home page
 *
 * @param name name query dns name
 * @param addr addr return ip address
 * @return true
 * @return false
 */
static bool dns_query_proc(const char* name, ip4_addr_t* addr)
{
    static const ip4_addr_t ipaddr = INIT_IP4(192, 168, 4, 1);

    printf("DNS query: %s\n", name);
    *addr = ipaddr;
    return true;
}

/**
 * @brief Callback for incoming publish topic
 *
 * @param arg
 * @param topic
 * @param tot_len
 */
static void mqtt_incoming_publish_cb(void* arg, const char* topic, u32_t tot_len)
{
    printf("Incoming publish at topic %s with total length %u\n", topic, (unsigned int)tot_len);

    // Decode topic string into a user defined reference
    if (0 == strcmp(topic, mqtt_topics[TOPIC_MODE])) {
        status.mqtt_topic = TOPIC_MODE;
    } else {
        status.mqtt_topic = TOPIC_MAX;
    }
}

/**
 * @brief Callback for incoming publish data
 *
 * @param arg
 * @param data
 * @param len
 * @param flags
 */
static void mqtt_incoming_data_cb(void* arg, const u8_t* data, u16_t len, u8_t flags)
{
    printf("Incoming publish payload with length %d, flags %u\n", len, (unsigned int)flags);

    if (flags & MQTT_DATA_FLAG_LAST) {
        // Last fragment of payload received (or whole part if payload fits receive buffer
        // See MQTT_VAR_HEADER_BUFFER_LEN)

        // Call function or do action depending on reference, in this case inpub_id
        switch (status.mqtt_topic) {
        case TOPIC_MODE:
            uint8_t mode = atoi(data);
            if (MODE_MAX > mode) {
                config.data.mode = mode;
            }
            // Don't trust the publisher, check zero termination
            if (data[len - 1] == 0) {
                printf("mqtt_incoming_data_cb: %s\n", (const char*)data);
            }
            break;

        default:
            printf("mqtt_incoming_data_cb: Ignoring payload...\n");
        }
    } else {
        // Handle fragmented payload, store in buffer, write to file or whatever
    }
}

/**
 * @brief Subscribe status callback
 *
 * Just print the result code here for simplicity,
 * normal behaviour would be to take some action if subscribe fails like
 * notifying user, retry subscribe or disconnect from server
 *
 * @param arg
 * @param result
 */
static void mqtt_sub_request_cb(void* arg, err_t result)
{
    printf("Subscribe result: %d\n", result);
}

/**
 * @brief connection status callback
 *
 * @param client
 * @param arg
 * @param status
 */
static void mqtt_connection_cb(mqtt_client_t* client, void* arg, mqtt_connection_status_t connection_status)
{
    if (connection_status == MQTT_CONNECT_ACCEPTED) {
        printf("mqtt_connection_cb: Successfully connected\n");
        status.mqtt_con = true;

        // Setup callback for incoming publish requests
        mqtt_set_inpub_callback(client, mqtt_incoming_publish_cb, mqtt_incoming_data_cb, arg);

        // Subscribe to a topic named "mode" with QoS level 1, call mqtt_sub_request_cb with result
        if (ERR_OK != mqtt_subscribe(client, "mode", 1, mqtt_sub_request_cb, arg)) {
            printf("mqtt_subscribe failed\n");
        }
    } else {
        status.mqtt_con = false;
        printf("mqtt_connection_cb: Disconnected, reason: %d\n", connection_status);
    }
}

/**
 * @brief Called when publish is complete either with success or failure
 *
 * @param arg
 * @param result
 */
static void mqtt_pub_request_cb(void* arg, err_t result)
{
    if (ERR_OK != result) {
        printf("Publish result: %d\n", result);
    }
}

/* GLOBAL FUCNTIONS ****************************************/

/**
 * @brief SNTP Set the time in microseconds
 *
 * @param sec seconds
 * @param us microseconds
 */
void sntp_set_system_time_us(unsigned long sec, unsigned long us)
{
    struct timeval tv = {
        .tv_sec = sec - NTP_DELTA - (config.data.tz * 60),
        .tv_usec = us
    };
    settimeofday(&tv, NULL);

    struct timespec ts = {
        .tv_sec = sec - NTP_DELTA - (config.data.tz * 60),
        .tv_nsec = us * 1000,
    };
    aon_timer_set_time(&ts);
}

/**
 * @brief Main
 *
 * @return int
 */
int main()
{
    while (status.run) {
        switch (status.state) {
        case ST_BOOT:
            // Set program description
            bi_decl(bi_program_description(PROGRAM_NAME));

            // Needed to get the RP2040 chip talking with the wireless module
            stdio_init_all();

            // Enable watchdog
            watchdog_enable(WATCHDOG_TIMEOUT, 1);

            // Load configuration from flash
            flash_config_load(&config);

            // Initialize GPIOs
            gpio_init(BTNA);
            gpio_set_dir(BTNA, GPIO_IN);
            gpio_pull_up(BTNA);
            gpio_set_irq_enabled_with_callback(BTNA, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &gpio_callback);

            gpio_init(BTNB);
            gpio_set_dir(BTNB, GPIO_IN);
            gpio_pull_up(BTNB);
            gpio_set_irq_enabled_with_callback(BTNB, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &gpio_callback);

            gpio_init(BTNC);
            gpio_set_dir(BTNC, GPIO_IN);
            gpio_pull_up(BTNC);
            gpio_set_irq_enabled_with_callback(BTNC, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &gpio_callback);

            // Initializes ADC
            adc_init();
            adc_set_temp_sensor_enabled(true);
            adc_select_input(4);

            // Initialize the AON timer
            aon_timer_start_with_timeofday();

            // Initialise the display
            uc8151_setup();
            uc8151_init();
            bm_init(uc8151_draw_bitmap);

            // Clear the display
            uc8151_clear();

            // Initialize Wi-Fi
            if (cyw43_arch_init()) {
                printf("Wi-Fi init failed");
                status.state = ST_RESET;
                break;
            }

            // Start web server
            for (int i = 0; i < LWIP_ARRAYSIZE(http_ssi_tags); i++) {
                LWIP_ASSERT("HTTP SSI tag too long for LWIP_HTTPD_MAX_TAG_NAME_LEN", strlen(http_ssi_tags[i]) <= LWIP_HTTPD_MAX_TAG_NAME_LEN);
            }
            httpd_init();
            http_set_ssi_handler(http_ssi_handler, http_ssi_tags, LWIP_ARRAYSIZE(http_ssi_tags));
            http_set_cgi_handlers(http_cgi_handlers, LWIP_ARRAYSIZE(http_cgi_handlers));

        case ST_CONNECT:
            // Attempt connect to Wi-Fi access point
            printf("Connecting to AP\n");

            cyw43_arch_enable_sta_mode();

            // Setup identify
            uint8_t* mac = cyw43_state.mac;
            snprintf(status.name, sizeof(status.name), PROGRAM_NAME "%d", mac[5] + (mac[4] << 8) + (mac[3] << 16));
            printf("name: %s\n", status.name);

            if (cyw43_arch_wifi_connect_timeout_ms(config.data.ssid, config.data.pass, CYW43_AUTH_WPA2_MIXED_PSK, WIFI_TIMEOUT)) {
                status.state = ST_SETUP;
            } else {
                status.state = ST_INIT;
            }
            break;

        case ST_SETUP:
            // Access point not found, create access point for user to setup wifi
            printf("Cannot find Wi-Fi, fallback to AP mode\n");

            cyw43_arch_disable_sta_mode();

            // Create access point
            cyw43_arch_enable_ap_mode(status.name, NULL, CYW43_AUTH_OPEN);

            // Start the DHCP server
            if (ERR_OK != dhserv_init(&dhcp_config)) {
                printf("DHCP server initialization failed\n");
                status.state = ST_RESET;
                break;
            }

            // Start the DNS server
            if (ERR_OK != dnserv_init(IP_ADDR_ANY, 53, dns_query_proc)) {
                printf("DNS server initialization failed\n");
                status.state = ST_RESET;
                break;
            }

            // Display title
            bmp_printf("/monospace.bmp", 0, 0, status.name);

            // Display wifi login qr code
            bm_qr_printf(0, 32, "WIFI:S:%s;T:WPA;;;", status.name);
            bmp_printf("/monospace.bmp", 96, 32, "Setup");

            // Update display
            uc8151_refresh();

            status.state = ST_WAIT;
            break;

        case ST_WAIT:
            // wait for Wi-Fi settings
            break;

        case ST_RETRY:
            // Configuration set, reconnect
            printf("Configuration set, reconnecting\n");

            cyw43_arch_disable_ap_mode();
            dnserv_free();
            dhserv_free();
            status.state = ST_CONNECT;
            break;

        case ST_INIT:
            // Access point found, start web-server and NTP
            printf("Connected.\n");

            // low power Wi-Fi
            CYW43_AGGRESSIVE_PM;

            // Print URL
            uint32_t ip_addr = cyw43_state.netif[CYW43_ITF_STA].ip_addr.addr;
            snprintf(status.addr, sizeof(status.addr), "%lu.%lu.%lu.%lu", ip_addr & 0xFF, (ip_addr >> 8) & 0xFF, (ip_addr >> 16) & 0xFF, ip_addr >> 24);
            printf("IP Address: %s\n", status.addr);

            // Configure and start the SNTP client
            sntp_setoperatingmode(SNTP_OPMODE_POLL);
            sntp_init();

            // Start mDNS
            mdns_resp_register_name_result_cb(mdns_report);
            mdns_resp_init();
            mdns_resp_add_netif(netif_default, status.name);
            mdns_resp_add_service(netif_default, status.name, "_http", DNSSD_PROTO_TCP, 80, mdns_srv_txt, NULL);
            mdns_resp_announce(netif_default);

            // Start MQTT
            if (NULL == (mqtt_client = mqtt_client_new())) {
                printf("MQTT allocation failed\n");
                status.state = ST_RESET;
                break;
            }

            status.state = ST_RUN;
            break;

        case ST_RUN:
            // Normal operation poll every second

            // Detect button presses
            if (status.btna & EDGE_FALL) {
                config.data.therm++;
                if (!status.save) {
                    uc8151_init();
                    uc8151_clear();
                }
                status.save = true;
                bmp_printf("/monospace.bmp", 96, 32, "%dC", config.data.therm);
                uc8151_refresh();
            }

            if (status.btna & EDGE_RISE) {
                status.btna = 0;
            }

            if (status.btnb & EDGE_RISE) {
                status.btnb = 0;
                config.data.mode = (config.data.mode + 1) % MODE_MAX;
                if (!status.save) {
                    uc8151_init();
                    uc8151_clear();
                }
                status.save = true;
                switch (config.data.mode) {
                case MODE_OFF:
                    bmp_draw("/no_sign.bmp", UC8151_WIDTH - 32, 48);
                    break;
                case MODE_AUTO:
                    bmp_draw("/clock.bmp", UC8151_WIDTH - 32, 48);
                    break;
                case MODE_ON:
                    bmp_draw("/radio_on.bmp", UC8151_WIDTH - 32, 48);
                    break;
                default:
                    printf("Invalid mode\n");
                    status.state = ST_RESET;
                }
                uc8151_refresh();
            }

            if (status.btnc & EDGE_FALL) {
                config.data.therm--;
                if (!status.save) {
                    uc8151_init();
                    uc8151_clear();
                }
                status.save = true;
                bmp_printf("/monospace.bmp", 96, 32, "%dC", config.data.therm);
                uc8151_refresh();
            }

            if (status.btnc & EDGE_RISE) {
                status.btnc = 0;
            }

            // Update every 1 min
            struct timespec ts;
            aon_timer_get_time(&ts);
            struct tm* time = localtime(&ts.tv_sec);
            if (time) {
                static int8_t min = 100;
                if (time->tm_min != min) {
                    min = time->tm_min;

                    // Save configuration in flash
                    if (status.save) {
                        status.save = false;
                        flash_config_save(&config);
                    } else {
                        uc8151_init();
                    }

                    // Clear the Display
                    uc8151_clear();

                    // Display title
                    bmp_printf("/monospace.bmp", 0, 0, status.name);

                    // Display url qr code
                    bm_printf(font_bits, font_height, font_width, 0, UC8151_HEIGHT - 8, "http://%s", status.addr);
                    bm_qr_printf(0, 32, "http://%s", status.addr);

                    // Display time
                    bmp_printf("/monospace.bmp", 96, 64, "%02d:%02d", time->tm_hour, time->tm_min);
                    snprintf(status.time, sizeof(status.time), "%02d:%02d", time->tm_hour, time->tm_min);

                    // Read temperature
                    status.temp = 27.0f - (((float)adc_read() * 3.3f / 4096) - 0.706f) / 0.001721f;
                    printf("Onboard temperature = %.01f C\n", status.temp);
                    bmp_printf("/monospace.bmp", 96, 32, "%.01fC", status.temp);

                    // Process mode
                    switch (config.data.mode) {
                    case MODE_OFF:
                        status.out = false;
                        bmp_draw("/no_sign.bmp", UC8151_WIDTH - 32, 48);
                        break;
                    case MODE_AUTO:
                        int starthour = 0;
                        int startmin = 0;
                        int endhour = 0;
                        int endmin = 0;
                        sscanf(config.data.timer1, "%d:%d", &starthour, &startmin);
                        sscanf(config.data.timer2, "%d:%d", &endhour, &endmin);

                        status.out = (starthour * 60 + startmin >= time->tm_hour * 60 + time->tm_min) && (endhour * 60 + endmin <= time->tm_hour * 60 + time->tm_min) && (config.data.therm < status.temp);
                        bmp_draw("/clock.bmp", UC8151_WIDTH - 32, 48);
                        break;
                    case MODE_ON:
                        status.out = true;
                        bmp_draw("/radio_on.bmp", UC8151_WIDTH - 32, 48);
                        break;
                    default:
                        printf("Invalid mode\n");
                        status.state = ST_RESET;
                    }

                    // Drive output
                    out(status.out);
                    if (status.out) {
                        bmp_draw("/lightning.bmp", UC8151_WIDTH - 32, 88);
                    } else {
                        uc8151_fill_rectangle(UC8151_WIDTH - 32, 88, UC8151_WIDTH, 120, 0xff);
                    }

                    // MQTT reconnect
                    if (!status.mqtt_con) {
                        ip_addr_t mqtt_server_address;
                        if (ipaddr_aton(config.data.mqtt_ip, &mqtt_server_address)) {
                            struct mqtt_connect_client_info_t ci = {
                                .client_id = status.name
                            };

                            // Initiate client and connect to server, if this fails immediately an error code is returned
                            // otherwise mqtt_connection_cb will be called with connection result after attempting
                            // to establish a connection with the server.
                            // For now MQTT version 3.1.1 is always used
                            if (ERR_OK != mqtt_client_connect(mqtt_client, &mqtt_server_address, MQTT_PORT, mqtt_connection_cb, NULL, &ci)) {
                                printf("MQTT client connect failed\n");
                            }
                        }
                    }

                    // MQTT Publish
                    if (status.mqtt_con) {
                        char payload[5] = "";
                        sprintf(payload, "%.01f", status.temp);
                        if (ERR_OK != mqtt_publish(mqtt_client, "temperature", payload, strlen(payload), 2, 0, mqtt_pub_request_cb, NULL)) {
                            printf("Publish failed\n");
                        }
                        sprintf(payload, status.out ? "on" : "off");
                        if (ERR_OK != mqtt_publish(mqtt_client, "output", payload, strlen(payload), 2, 0, mqtt_pub_request_cb, NULL)) {
                            printf("Publish failed\n");
                        }
                    }

                    // Update display
                    uc8151_refresh();

                    // power down display
                    uc8151_sleep();
                }
            }
            break;

        case ST_RESET:
            // Save config, deinitiliaze and trip watchdog to reset
            if (status.save) {
                flash_config_save(&config);
            }

            if (status.mqtt_con) {
                mqtt_disconnect(mqtt_client);
            }

            uc8151_sleep();

            cyw43_arch_deinit();

            printf("Waiting for reset\n");
            status.run = false;
            break;

        default:
            printf("Invalid state\n");
            status.state = ST_RESET;
        }

        // keep alive
        watchdog_update();

        // low power sleep
        sleep_ms(POLL_INTERVAL);
    }

    return 0;
}
