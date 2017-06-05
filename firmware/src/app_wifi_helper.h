
#ifndef _APP_WIFI_HELPER_H
#define _APP_WIFI_HELPER_H

#include "app.h"

// *****************************************************************************
// *****************************************************************************
// Section: Type Definitions
// *****************************************************************************
// *****************************************************************************
#if defined(TCPIP_IF_MRF24W)

#define WF_DISABLED DRV_WIFI_DISABLED
#define WF_ENABLED DRV_WIFI_ENABLED

#define WF_NETWORK_TYPE_INFRASTRUCTURE DRV_WIFI_NETWORK_TYPE_INFRASTRUCTURE
#define WF_NETWORK_TYPE_ADHOC DRV_WIFI_NETWORK_TYPE_ADHOC
#define WF_NETWORK_TYPE_P2P 0xff /* Unsupported */
#define WF_NETWORK_TYPE_SOFT_AP DRV_WIFI_NETWORK_TYPE_SOFT_AP

#define WF_SECURITY_OPEN DRV_WIFI_SECURITY_OPEN
#define WF_SECURITY_WEP_40 DRV_WIFI_SECURITY_WEP_40
#define WF_SECURITY_WEP_104 DRV_WIFI_SECURITY_WEP_104
#define WF_SECURITY_WPA_WITH_KEY DRV_WIFI_SECURITY_WPA_WITH_KEY
#define WF_SECURITY_WPA_WITH_PASS_PHRASE DRV_WIFI_SECURITY_WPA_WITH_PASS_PHRASE
#define WF_SECURITY_WPA2_WITH_KEY DRV_WIFI_SECURITY_WPA2_WITH_KEY
#define WF_SECURITY_WPA2_WITH_PASS_PHRASE DRV_WIFI_SECURITY_WPA2_WITH_PASS_PHRASE
#define WF_SECURITY_WPA_AUTO_WITH_KEY DRV_WIFI_SECURITY_WPA_AUTO_WITH_KEY
#define WF_SECURITY_WPA_AUTO_WITH_PASS_PHRASE DRV_WIFI_SECURITY_WPA_AUTO_WITH_PASS_PHRASE
#define WF_SECURITY_WPS_PUSH_BUTTON DRV_WIFI_SECURITY_WPS_PUSH_BUTTON
#define WF_SECURITY_WPS_PIN DRV_WIFI_SECURITY_WPS_PIN

#define WF_DEFAULT_ADHOC_HIDDEN_SSID DRV_WIFI_DEFAULT_ADHOC_HIDDEN_SSID
#define WF_DEFAULT_ADHOC_BEACON_PERIOD DRV_WIFI_DEFAULT_ADHOC_BEACON_PERIOD
#define WF_DEFAULT_ADHOC_MODE DRV_WIFI_DEFAULT_ADHOC_MODE

#define WF_DEFAULT_POWER_SAVE DRV_WIFI_DEFAULT_POWER_SAVE

#define WF_WEP_KEY_INVALID 0xff

#define WF_ASSERT(condition, msg) DRV_WIFI_ASSERT(condition, msg)

typedef DRV_WIFI_SCAN_RESULT WF_SCAN_RESULT;
typedef DRV_WIFI_CONFIG_DATA WF_CONFIG_DATA;
typedef DRV_WIFI_DEVICE_INFO WF_DEVICE_INFO;
typedef DRV_WIFI_ADHOC_NETWORK_CONTEXT WF_ADHOC_NETWORK_CONTEXT;

#endif /* defined(TCPIP_IF_MRF24W) */

typedef enum {
    WIFI_HELPER_TCPIP_WAIT_INIT,
    WIFI_HELPER_CONFIG,
    WIFI_HELPER_PRESCAN,
    WIFI_HELPER_TCPIP_MODULES_ENABLE,
    WIFI_HELPER_TCPIP_TRANSACT,
    WIFI_HELPER_WAIT_FOR_IP,
    WIFI_HELPER_CONNECT_DONE,
    WIFI_HELPER_ERROR,
} WIFI_HELPER_STATES;

typedef enum {
    /* Initialize the state machine, and also checks if prescan is allowed. */
    APP_WIFI_PRESCAN_INIT,
    /* In this state the application waits for the prescan to finish. */
    APP_WIFI_PRESCAN_WAIT,
    /* In this state the application saves the prescan results. */
    APP_WIFI_PRESCAN_SAVE,
    /* After prescan, Wi-Fi module is reset in this state. */
    APP_WIFI_PRESCAN_RESET,
    /* In this state, the application waits for Wi-Fi reset to finish. */
    APP_WIFI_PRESCAN_WAIT_RESET,
    /* Prescan is complete. */
    APP_WIFI_PRESCAN_DONE,

} APP_WIFI_PRESCAN_STATE;

typedef enum {
    MRF24WG_MODULE = 2,
    MRF24WN_MODULE = 3,
} MRF24W_MODULE_TYPE;

typedef struct {
    uint8_t ssid[32 + 1]; // 32-byte SSID plus null terminator
    uint8_t networkType;
    uint8_t prevSSID[32 + 1]; // previous SSID
    uint8_t prevNetworkType; // previous network type
    uint8_t wepKeyIndex;
    uint8_t securityMode;
    uint8_t securityKey[64 + 1]; // 64-byte key plus null terminator
    uint8_t securityKeyLen; // number of bytes in security key (can be 0)
} WF_REDIRECTION_CONFIG;

typedef struct {
    WIFI_HELPER_STATES wh_state;
    APP_WIFI_PRESCAN_STATE scan_state;
} APP_WIFI_HelperData;



static void APP_WIFI_RedirectionConfigInit(void);
static void APP_WIFIG_SSID_Set(TCPIP_NET_HANDLE netH);
static void APP_WIFI_IPv6MulticastFilter_Set(TCPIP_NET_HANDLE netH);
static void APP_TCPIP_IFModules_Enable(TCPIP_NET_HANDLE netH);
static void APP_TCPIP_IF_Down(TCPIP_NET_HANDLE netH);
static void APP_TCPIP_IF_Up(TCPIP_NET_HANDLE netH);
uint8_t APP_WIFI_Prescan(void);
static void APP_WIFI_DHCPS_Sync(TCPIP_NET_HANDLE netH);
static void APP_WIFI_PowerSave_Config(bool enable);
static void APP_TCPIP_IFModules_Disable(TCPIP_NET_HANDLE netH);

uint8_t APP_WIFI_Get_Network_Type();

#endif  /* _APP_WIFI_HELPER_H */