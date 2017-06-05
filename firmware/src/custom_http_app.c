/*******************************************************************************
  Application to Demo HTTP Server

  Summary:
    Support for HTTP module in Microchip TCP/IP Stack

  Description:
    -Implements the application
    -Reference: RFC 1002
 *******************************************************************************/

/*******************************************************************************
File Name: custom_http_app.c
Copyright (C) 2012 released Microchip Technology Inc.  All rights
reserved.

Microchip licenses to you the right to use, modify, copy and distribute
Software only when embedded on a Microchip microcontroller or digital signal
controller that is integrated into your product or third party product
(pursuant to the sublicense terms in the accompanying license agreement).

You should refer to the license agreement accompanying this Software for
additional information regarding your rights and obligations.

SOFTWARE AND DOCUMENTATION ARE PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION, ANY WARRANTY OF
MERCHANTABILITY, TITLE, NON-INFRINGEMENT AND FITNESS FOR A PARTICULAR PURPOSE.
IN NO EVENT SHALL MICROCHIP OR ITS LICENSORS BE LIABLE OR OBLIGATED UNDER
CONTRACT, NEGLIGENCE, STRICT LIABILITY, CONTRIBUTION, BREACH OF WARRANTY, OR
OTHER LEGAL EQUITABLE THEORY ANY DIRECT OR INDIRECT DAMAGES OR EXPENSES
INCLUDING BUT NOT LIMITED TO ANY INCIDENTAL, SPECIAL, INDIRECT, PUNITIVE OR
CONSEQUENTIAL DAMAGES, LOST PROFITS OR LOST DATA, COST OF PROCUREMENT OF
SUBSTITUTE GOODS, TECHNOLOGY, SERVICES, OR ANY CLAIMS BY THIRD PARTIES
(INCLUDING BUT NOT LIMITED TO ANY DEFENSE THEREOF), OR OTHER SIMILAR COSTS.
 *******************************************************************************/

#define __CUSTOMHTTPAPP_C

#include <ctype.h>
#include "system_config.h"

#if defined(TCPIP_STACK_USE_HTTP_SERVER)

#include "tcpip/tcpip.h"
#include "system/tmr/sys_tmr.h"
#include "system/random/sys_random.h"
#include "tcpip/src/common/helpers.h"
#include "crypto/crypto.h"

#include "tcpip/src/tcpip_private.h"

#include "app.h"
//extern APP_DATA appData;

/****************************************************************************
  Section:
    Definitions
 ****************************************************************************/
// Use the web page in the Demo App (~2.5kb ROM, ~0b RAM)
//#define HTTP_APP_USE_RECONFIG

#ifndef NO_MD5
// Use the MD5 Demo web page (~5kb ROM, ~160b RAM)
//#define HTTP_APP_USE_MD5
#endif

// Use the e-mail demo web page
#if defined(TCPIP_STACK_USE_SMTP_CLIENT)
#define HTTP_APP_USE_EMAIL
#endif

#if defined(TCPIP_IF_MRF24W) || defined(TCPIP_IF_MRF24WN)
#define HTTP_APP_USE_WIFI
#endif

#define HTTP_APP_REDIRECTION_DELAY_TIME (1ul) /* second */
#define HTTP_APP_IPV4_ADDRESS_BUFFER_SIZE 20

/****************************************************************************
  Section:
    Function Prototypes
 ****************************************************************************/
#if defined(TCPIP_HTTP_USE_POST)
#if defined(SYS_OUT_ENABLE)
static HTTP_IO_RESULT HTTPPostLCD(HTTP_CONN_HANDLE connHandle);
#endif
#if defined(HTTP_APP_USE_MD5)
static HTTP_IO_RESULT HTTPPostMD5(HTTP_CONN_HANDLE connHandle);
#endif
#if defined(HTTP_APP_USE_RECONFIG)
static HTTP_IO_RESULT HTTPPostConfig(HTTP_CONN_HANDLE connHandle);
#if defined(TCPIP_STACK_USE_SNMP_SERVER)
static HTTP_IO_RESULT HTTPPostSNMPCommunity(HTTP_CONN_HANDLE connHandle);
#endif
#endif
#if defined(HTTP_APP_USE_EMAIL) || defined(TCPIP_STACK_USE_SMTP_CLIENT)
static HTTP_IO_RESULT HTTPPostEmail(HTTP_CONN_HANDLE connHandle);
#endif
#if defined(TCPIP_STACK_USE_DYNAMICDNS_CLIENT)
static HTTP_IO_RESULT HTTPPostDDNSConfig(HTTP_CONN_HANDLE connHandle);
#endif
#if defined(HTTP_APP_USE_WIFI)
static HTTP_IO_RESULT HTTPPostWIFIConfig(HTTP_CONN_HANDLE connHandle);
#endif
#endif

/****************************************************************************
  Section:
    Variables
 ****************************************************************************/
extern bool g_redirect_signal;
extern WF_CONFIG_DATA g_wifi_cfg;
extern WF_DEVICE_INFO g_wifi_deviceInfo;
extern WF_REDIRECTION_CONFIG g_redirectionConfig;
static bool s_scanResultIsValid = false;
static WF_SCAN_RESULT *s_scanResult;
static IWPRIV_GET_PARAM s_httpapp_get_param;
static IWPRIV_SET_PARAM s_httpapp_set_param;
static IWPRIV_EXECUTE_PARAM s_httpapp_execute_param;
static uint8_t s_buf_ipv4addr[HTTP_APP_IPV4_ADDRESS_BUFFER_SIZE];

extern const char * const ddnsServiceHosts[];
// RAM allocated for DDNS parameters
#if defined(TCPIP_STACK_USE_DYNAMICDNS_CLIENT)
static uint8_t DDNSData[100];
#endif

// Sticky status message variable.
// This is used to indicated whether or not the previous POST operation was
// successful.  The application uses these to store status messages when a
// POST operation redirects.  This lets the application provide status messages
// after a redirect, when connection instance data has already been lost.
static bool lastSuccess = false;

// Stick status message variable.  See lastSuccess for details.
static bool lastFailure = false;

/****************************************************************************
  Section:
    Helper Functions
 ****************************************************************************/

/*******************************************************************************
 * FUNCTION: Helper_HEXCharToBIN
 *
 * RETURNS: binary value associated with ASCII HEX input value
 *
 * PARAMS: hex_char -- ASCII HEX character
 *
 * NOTES: Converts an input ASCII HEX character to its binary value.  Function
 *        does not error check; it assumes only hex characters are passed in.
 *******************************************************************************/
static uint8_t Helper_HEXCharToBIN(uint8_t hex_char) {
    if ((hex_char >= 'a') && (hex_char <= 'f')) {
        return (0x0a + (hex_char - 'a'));
    } else if ((hex_char >= 'A') && (hex_char <= 'F')) {
        return (0x0a + (hex_char - 'A'));
    } else // ((hex_char >= '0') && (hex_char <= '9'))
    {
        return (0x00 + (hex_char - '0'));
    }
}

/*******************************************************************************
 * FUNCTION: Helper_HEXStrToBIN
 *
 * RETURNS: true if conversion successful, else false
 *
 * PARAMS: p_ascii_hex_str -- ASCII HEX string to be converted
 *         p_bin -- binary value if conversion successful
 *
 * NOTES: Converts an input ASCII HEX string to binary value (up to 32-bit value)
 *******************************************************************************/
static bool Helper_HEXStrToBIN(char *p_ascii_hex_str, uint16_t *p_bin) {
    int8_t i;
    uint32_t multiplier = 1;

    *p_bin = 0;

    // not allowed to have a string of more than 4 nibbles
    if (strlen((char *) p_ascii_hex_str) > 8u) {
        return false;
    }

    // first, ensure all characters are a hex digit
    for (i = (uint8_t) strlen((char *) p_ascii_hex_str) - 1; i >= 0; --i) {
        if (!isxdigit(p_ascii_hex_str[i])) {
            return false;
        }
        *p_bin += multiplier * Helper_HEXCharToBIN(p_ascii_hex_str[i]);
        multiplier *= 16;
    }

    return true;
}

static bool Helper_HEXStrToBINInplace(char *p_str, uint8_t expected_binary_size) {
    char str_buffer[3];
    uint8_t binary_index = 0;
    char *ascii_hex_str_start = p_str;
    uint16_t bin_buffer = 0;

    /* gobble up any hex prefix */
    if (memcmp(ascii_hex_str_start, (const char *) "0x", 2) == 0) {
        ascii_hex_str_start += 2;
    }

    if (strlen((char *) ascii_hex_str_start) != (expected_binary_size * 2)) {
        return false;
    }

    while (binary_index < expected_binary_size) {
        memcpy(str_buffer, (const char *) ascii_hex_str_start, 2);
        str_buffer[2] = '\0';

        /* convert the hex string to binary value */
        if (!Helper_HEXStrToBIN(str_buffer, &bin_buffer)) {
            return false;
        }

        p_str[binary_index++] = (uint8_t) bin_buffer;
        ascii_hex_str_start += 2;
    }

    return true;
}

static bool Helper_WIFI_SecurityHandle(WF_REDIRECTION_CONFIG *cfg, const char *str) {
    uint8_t ascii_key = 0, key_size = 0;
    switch (cfg->securityMode) {
        case WF_SECURITY_OPEN: // Keep compiler happy, nothing to do here!
            ascii_key = true;
            break;
        case WF_SECURITY_WEP_40:
            key_size = 10; /* Assume hex size. */
            if (strlen(str) == 5) {
                ascii_key = true;
                key_size = 5; /* ASCII key support. */
            }
            cfg->wepKeyIndex = 0; /* Example uses only key idx 0 (sometimes called 1). */
            break;
        case WF_SECURITY_WEP_104:
            key_size = 26; /* Assume hex size. */
            if (strlen(str) == 13) {
                ascii_key = true;
                key_size = 13; /* ASCII key support. */
            }
            cfg->wepKeyIndex = 0; /* Example uses only key idx 0 (sometimes called 1). */
            break;
        case WF_SECURITY_WPA_WITH_PASS_PHRASE:
        case WF_SECURITY_WPA2_WITH_PASS_PHRASE:
        case WF_SECURITY_WPA_AUTO_WITH_PASS_PHRASE:
            ascii_key = true;
            key_size = strlen(str);
            // between 8 - 63 characters, passphrase
            if ((key_size < 8) || (key_size > 63))
                return false;
            break;
    }
    if (strlen(str) != key_size) {
        SYS_CONSOLE_MESSAGE("\r\nIncomplete key received\r\n");
        return false;
    }
    memcpy(cfg->securityKey, (void *) str, key_size);
    cfg->securityKey[key_size] = 0; /* terminate string */
    if (!ascii_key) {
        key_size /= 2;
        if (!Helper_HEXStrToBINInplace((char *) cfg->securityKey, key_size)) {
            SYS_CONSOLE_MESSAGE("\r\nFailed to convert ASCII string (representing HEX digits) to real HEX string!\r\n");
            return false;
        }
    }
    cfg->securityKeyLen = key_size;
    return true;
}

static void Helper_WIFI_KeySave(WF_REDIRECTION_CONFIG *redirectCfg, WF_CONFIG_DATA *cfg) {
    uint8_t key_size = 0;
    switch ((uint8_t) redirectCfg->securityMode) {
        case WF_SECURITY_WEP_40:
            key_size = 5;
            break;
        case WF_SECURITY_WEP_104:
            key_size = 13;
            break;
        case WF_SECURITY_WPA_WITH_PASS_PHRASE:
        case WF_SECURITY_WPA2_WITH_PASS_PHRASE:
        case WF_SECURITY_WPA_AUTO_WITH_PASS_PHRASE:
            key_size = strlen((const char *) (redirectCfg->securityKey)); // ascii so use strlen
            break;
    }
    memcpy(cfg->securityKey, redirectCfg->securityKey, key_size);
    cfg->securityKey[strlen((const char *) (redirectCfg->securityKey))] = 0;
    cfg->securityKeyLen = key_size;
}

static void Helper_APP_RedirectionFlagSet(uintptr_t context, uint32_t currTick) {
    g_redirect_signal = true;
}

static HTTP_IO_RESULT Helper_APP_ConfigFailure(HTTP_CONN_HANDLE connHandle, uint8_t *httpDataBuff) {
    printf("Helper_APP_ConfigFailure\r\n");
    lastFailure = true;
    if (httpDataBuff)
        strcpy((char *) httpDataBuff, "/error.htm");
    TCPIP_HTTP_CurrentConnectionStatusSet(connHandle, HTTP_REDIRECT);
    return HTTP_IO_DONE;
}

/****************************************************************************
  Section:
    GET Form Handlers
 ****************************************************************************/

/****************************************************************************
  Function:
    HTTP_IO_RESULT TCPIP_HTTP_GetExecute(HTTP_CONN_HANDLE connHandle)

  Internal:
    See documentation in the TCP/IP Stack API or http.h for details.
 ****************************************************************************/
HTTP_IO_RESULT TCPIP_HTTP_GetExecute(HTTP_CONN_HANDLE connHandle) {
    printf("TCPIP_HTTP_GetExecute\r\n");

    const uint8_t *ptr;
    const uint8_t *ptr1;
    uint8_t bssIdx;
    uint8_t filename[20];
    uint8_t *httpDataBuff;
    TCPIP_UINT16_VAL bssIdxStr;

    // Load the file name.
    // Make sure uint8_t filename[] above is large enough for your longest name.
    SYS_FS_FileNameGet(TCPIP_HTTP_CurrentConnectionFileGet(connHandle), filename, 20);

    httpDataBuff = TCPIP_HTTP_CurrentConnectionDataBufferGet(connHandle);

    // If its the forms.htm page.
    if (!memcmp(filename, "forms.htm", 9)) {
        printf("TCPIP_HTTP_GetExecute: forms.htm\r\n");
        // Seek out each of the four LED strings, and if it exists set the LED states.
        ptr = TCPIP_HTTP_ArgGet(httpDataBuff, (const uint8_t *) "led2");
        if (ptr)
            BSP_LEDStateSet(APP_TCPIP_LED_3, (*ptr == '1'));
        //LED2_IO = (*ptr == '1');

        ptr = TCPIP_HTTP_ArgGet(httpDataBuff, (const uint8_t *) "led1");
        if (ptr)
            BSP_LEDStateSet(APP_TCPIP_LED_2, (*ptr == '1'));
        //LED1_IO = (*ptr == '1');
    } else if (!memcmp(filename, "cookies.htm", 11)) {
        printf("TCPIP_HTTP_GetExecute: cookies.htm\r\n");
        // This is very simple.  The names and values we want are already in
        // the data array.  We just set the hasArgs value to indicate how many
        // name/value pairs we want stored as cookies.
        // To add the second cookie, just increment this value.
        // remember to also add a dynamic variable callback to control the printout.
        TCPIP_HTTP_CurrentConnectionHasArgsSet(connHandle, 0x01);
    }// If it's the LED updater file.
    else if (!memcmp(filename, "leds.cgi", 8)) {
        printf("TCPIP_HTTP_GetExecute: leds.cgi\r\n");
        // Determine which LED to toggle.
        ptr = TCPIP_HTTP_ArgGet(httpDataBuff, (const uint8_t *) "led");

        // Toggle the specified LED.
        switch (*ptr) {
            case '0':
                BSP_LEDToggle(APP_TCPIP_LED_1);
                //LED0_IO ^= 1;
                break;
            case '1':
                BSP_LEDToggle(APP_TCPIP_LED_2);
                //LED1_IO ^= 1;
                break;
            case '2':
                BSP_LEDToggle(APP_TCPIP_LED_3);
                //LED2_IO ^= 1;
                break;
        }
    } else if (!memcmp(filename, "scan.cgi", 8)) {
        printf("TCPIP_HTTP_GetExecute: scan.cgi\r\n");
        ptr = TCPIP_HTTP_ArgGet(httpDataBuff, (const uint8_t *) "scan");
        ptr1 = TCPIP_HTTP_ArgGet(httpDataBuff, (const uint8_t *) "getBss");

        s_httpapp_get_param.config.data = &g_wifi_cfg;
        iwpriv_get(CONFIG_GET, &s_httpapp_get_param);

        if ((ptr != NULL) && (ptr1 == NULL)) {
            // scan request
            s_scanResultIsValid = false;

            /*
             * Display pre-scan results if pre-scan results are available,
             * otherwise initiate a new scan.
             */
            iwpriv_get(SCANRESULTS_COUNT_GET, &s_httpapp_get_param);
            if (s_httpapp_get_param.scan.numberOfResults == 0) {
                iwpriv_execute(SCAN_START, &s_httpapp_execute_param);
                do {
                    iwpriv_get(SCANSTATUS_GET, &s_httpapp_get_param);
                } while (s_httpapp_get_param.scan.scanStatus == IWPRIV_SCAN_IN_PROGRESS);
                do {
                    iwpriv_execute(SCANRESULTS_SAVE, &s_httpapp_execute_param);
                } while (s_httpapp_execute_param.scan.saveStatus == IWPRIV_IN_PROGRESS);
            }
        } else if ((ptr == NULL) && (ptr1 != NULL)) {
            // getBss request
            // use the value to get the nth bss stored on chip
            s_scanResultIsValid = false;
            bssIdxStr.v[1] = *ptr1;
            bssIdxStr.v[0] = *(ptr1 + 1);
            bssIdx = hexatob(bssIdxStr.Val);

            s_httpapp_get_param.scan.index = (uint16_t) bssIdx;
            iwpriv_get(SCANRESULT_GET, &s_httpapp_get_param);
            s_scanResult = (WF_SCAN_RESULT *) s_httpapp_get_param.scan.data;

            if (s_scanResult) {
                if (s_scanResult->ssidLen < 32)
                    s_scanResult->ssid[s_scanResult->ssidLen] = 0;
                s_scanResultIsValid = true;
            }
        } else {
            printf("TCPIP_HTTP_GetExecute: impossible to get here\r\n");
            // impossible to get here
        }
    }

    return HTTP_IO_DONE;
}

/****************************************************************************
  Section:
    POST Form Handlers
 ****************************************************************************/
#if defined(TCPIP_HTTP_USE_POST)

/****************************************************************************
  Function:
    HTTP_IO_RESULT TCPIP_HTTP_PostExecute(HTTP_CONN_HANDLE connHandle)

  Internal:
    See documentation in the TCP/IP Stack API or HTTP.h for details.
 ****************************************************************************/
HTTP_IO_RESULT TCPIP_HTTP_PostExecute(HTTP_CONN_HANDLE connHandle) {
    // Resolve which function to use and pass along
    uint8_t filename[20];

    // Load the file name
    // Make sure uint8_t filename[] above is large enough for your longest name
    SYS_FS_FileNameGet(TCPIP_HTTP_CurrentConnectionFileGet(connHandle), filename, sizeof (filename));

    printf("TCPIP_HTTP_PostExecute\r\n");

    // Post resources
    if (!memcmp(filename, "index.htm", 9))
        return HTTPPostWIFIConfig(connHandle) /*HTTPPostConfig()*/;

    return HTTP_IO_DONE;
}
/*******************************************************************************
  Function:
    static HTTP_IO_RESULT HTTPPostWIFIConfig(HTTP_CONN_HANDLE connHandle)

  Summary:
    Processes the Wi-Fi configuration data.

  Description:
    Accepts wireless configuration data from the www site and saves them to a
    structure to be applied by the Wi-Fi module configuration manager.

    The following configurations are possible:
         i) Mode: adhoc or infrastructure
        ii) Security:
               - None
               - WEP 64-bit
               - WEP 128-bit
               - WPA Auto pre-calculated key
               - WPA1 passphrase
               - WPA2 passphrase
               - WPA Auto passphrase
       iii) Key material

    If an error occurs, such as data is invalid they will be redirected to a page
    informing the user of such results.

    NOTE: This code for modified originally from HTTPPostWIFIConfig as distributed
          by Microchip.

  Precondition:
    None.

  Parameters:
    None.

  Return Values:
    HTTP_IO_DONE - all parameters have been processed
    HTTP_IO_NEED_DATA - data needed by this function has not yet arrived
 *******************************************************************************/
#if defined(HTTP_APP_USE_WIFI)

static HTTP_IO_RESULT HTTPPostWIFIConfig(HTTP_CONN_HANDLE connHandle) {
    printf("HTTPPostWIFIConfig\r\n");

    uint8_t ssidLen;
    uint32_t byteCount;
    TCP_SOCKET sktHTTP;
    uint8_t *httpDataBuff = 0;

    byteCount = TCPIP_HTTP_CurrentConnectionByteCountGet(connHandle);
#if DEBUG_LOG
    printf("HTTPPostWIFIConfig GET Byte Count=%d\r\n", byteCount);
#endif
    sktHTTP = TCPIP_HTTP_CurrentConnectionSocketGet(connHandle);

    if (byteCount > TCPIP_TCP_GetIsReady(sktHTTP) + TCPIP_TCP_FifoRxFreeGet(sktHTTP)) {
#if DEBUG_LOG
        printf("************************** TCPIP_TCP_GetIsReady(sktHTTP)=%d, TCPIP_TCP_FifoRxFreeGet(sktHTTP)=%d\r\n", TCPIP_TCP_GetIsReady(sktHTTP), TCPIP_TCP_FifoRxFreeGet(sktHTTP));
        printf("************************** Byte Count=%d > [TCPIP_TCP_GetIsReady(sktHTTP) + TCPIP_TCP_FifoRxFreeGet(sktHTTP)]=%d\r\n", byteCount, TCPIP_TCP_GetIsReady(sktHTTP) + TCPIP_TCP_FifoRxFreeGet(sktHTTP));
#endif
        return Helper_APP_ConfigFailure(connHandle, httpDataBuff);
    }
#if DEBUG_LOG
    printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ TCPIP_TCP_GetIsReady(sktHTTP)=%d, TCPIP_TCP_FifoRxFreeGet(sktHTTP)=%d\r\n", TCPIP_TCP_GetIsReady(sktHTTP), TCPIP_TCP_FifoRxFreeGet(sktHTTP));
    printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ Byte Count=%d > [TCPIP_TCP_GetIsReady(sktHTTP) + TCPIP_TCP_FifoRxFreeGet(sktHTTP)]=%d\r\n", byteCount, TCPIP_TCP_GetIsReady(sktHTTP) + TCPIP_TCP_FifoRxFreeGet(sktHTTP));
    printf("************************** skip Helper_APP_ConfigFailure\r\n");
#endif
    // Ensure that all data is waiting to be parsed.  If not, keep waiting for
    // all of it to arrive.
    if (TCPIP_TCP_GetIsReady(sktHTTP) < byteCount)
        return HTTP_IO_NEED_DATA;

    // Use current config in non-volatile memory as defaults
    httpDataBuff = TCPIP_HTTP_CurrentConnectionDataBufferGet(connHandle);
    // Read all browser POST data.
    while (TCPIP_HTTP_CurrentConnectionByteCountGet(connHandle)) {
        // Read a form field name.
        if (TCPIP_HTTP_PostNameRead(connHandle, httpDataBuff, 6) != HTTP_READ_OK)
            return Helper_APP_ConfigFailure(connHandle, httpDataBuff);

        // Read a form field value.
        if (TCPIP_HTTP_PostValueRead(connHandle, httpDataBuff + 6, TCPIP_HTTP_MAX_DATA_LEN - 6 - 2) != HTTP_READ_OK)
            return Helper_APP_ConfigFailure(connHandle, httpDataBuff);

        // Parse the value that was read.
        if (!strcmp((char *) httpDataBuff, (const char *) "wlan")) {
            // Get the network type: Ad-Hoc or Infrastructure.
            char networkType[6];
            if (strlen((char *) (httpDataBuff + 6)) > 5) /* Sanity check. */
                return Helper_APP_ConfigFailure(connHandle, httpDataBuff);

            memcpy(networkType, (void *) (httpDataBuff + 6), strlen((char *) (httpDataBuff + 6)));
            networkType[strlen((char *) (httpDataBuff + 6))] = 0; /* Terminate string. */
            if (!strcmp((char *) networkType, (const char *) "infra")) {
                g_redirectionConfig.networkType = WF_NETWORK_TYPE_INFRASTRUCTURE;
            } else if (!strcmp((char *) networkType, "adhoc")) {
                WF_ADHOC_NETWORK_CONTEXT adhocContext;
                g_redirectionConfig.networkType = WF_NETWORK_TYPE_ADHOC;

                // Always setup Ad-Hoc to attempt to connect first, then start.
                adhocContext.mode = WF_DEFAULT_ADHOC_MODE;
                adhocContext.beaconPeriod = WF_DEFAULT_ADHOC_BEACON_PERIOD;
                adhocContext.hiddenSsid = WF_DEFAULT_ADHOC_HIDDEN_SSID;
                s_httpapp_set_param.ctx.data = &adhocContext;
                iwpriv_set(ADHOCCTX_SET, &s_httpapp_set_param);
            } else {
                // Network type no good. :-(
                SYS_CONSOLE_MESSAGE((const char *) "\r\nInvalid redirection network type\r\n");
                printf((const char *) "\r\nInvalid redirection network type\r\n");
                return Helper_APP_ConfigFailure(connHandle, httpDataBuff);
            }

            // Save old network type.
            iwpriv_get(NETWORKTYPE_GET, &s_httpapp_get_param);
            g_redirectionConfig.prevNetworkType = s_httpapp_get_param.netType.type;
        } else if (!strcmp((char *) httpDataBuff, "ssid")) {
            // Get new SSID and make sure it is valid.
            if (strlen((char *) (httpDataBuff + 6)) < 33u) {
                memcpy(g_redirectionConfig.ssid, (void *) (httpDataBuff + 6), strlen((char *) (httpDataBuff + 6)));
                g_redirectionConfig.ssid[strlen((char *) (httpDataBuff + 6))] = 0; /* Terminate string. */

                /* Save current profile SSID for displaying later. */
                s_httpapp_get_param.ssid.ssid = g_redirectionConfig.prevSSID;
                iwpriv_get(SSID_GET, &s_httpapp_get_param);
                ssidLen = s_httpapp_get_param.ssid.ssidLen;
                g_redirectionConfig.prevSSID[ssidLen] = 0;
            } else {
                // Invalid SSID... :-(
                return Helper_APP_ConfigFailure(connHandle, httpDataBuff);
            }
        } else if (!strcmp((char *) httpDataBuff, (const char *) "sec")) {
            char securityMode[7]; // Read security mode.

            if (strlen((char *) (httpDataBuff + 6)) > 6) /* Sanity check. */
                return Helper_APP_ConfigFailure(connHandle, httpDataBuff);

            memcpy(securityMode, (void *) (httpDataBuff + 6), strlen((char *) (httpDataBuff + 6)));
            securityMode[strlen((char *) (httpDataBuff + 6))] = 0; /* Terminate string. */

            if (!strcmp((char *) securityMode, (const char *) "no")) {
                g_redirectionConfig.securityMode = WF_SECURITY_OPEN;
            } else if (!strcmp((char *) securityMode, (const char *) "wep40")) {
                g_redirectionConfig.securityMode = WF_SECURITY_WEP_40;
            } else if (!strcmp((char *) securityMode, (const char *) "wep104")) {
                g_redirectionConfig.securityMode = WF_SECURITY_WEP_104;
            } else if (!strcmp((char *) securityMode, (const char *) "wpa1")) {
                if (g_wifi_deviceInfo.deviceType == MRF24WG_MODULE) {
                    g_redirectionConfig.securityMode = WF_SECURITY_WPA_AUTO_WITH_PASS_PHRASE;
                } else if (g_wifi_deviceInfo.deviceType == MRF24WN_MODULE) {
                    g_redirectionConfig.securityMode = WF_SECURITY_WPA_WITH_PASS_PHRASE;
                } else {
                    WF_ASSERT(false, "Incorrect Wi-Fi Device Info");
                }
            } else if (!strcmp((char *) securityMode, (const char *) "wpa2")) {
                if (g_wifi_deviceInfo.deviceType == MRF24WG_MODULE) {
                    g_redirectionConfig.securityMode = WF_SECURITY_WPA_AUTO_WITH_PASS_PHRASE;
                } else if (g_wifi_deviceInfo.deviceType == MRF24WN_MODULE) {
                    g_redirectionConfig.securityMode = WF_SECURITY_WPA2_WITH_PASS_PHRASE;
                } else {
                    WF_ASSERT(false, "Incorrect Wi-Fi Device Info");
                }
            } else if (!strcmp((char *) securityMode, (const char *) "wpa")) {
                g_redirectionConfig.securityMode = WF_SECURITY_WPA_AUTO_WITH_PASS_PHRASE;
            } else {
                // Security mode no good. :-(
                SYS_CONSOLE_MESSAGE("\r\nInvalid redirection security mode\r\n\r\n");
                return Helper_APP_ConfigFailure(connHandle, httpDataBuff);
            }
        } else if (!strcmp((char *) httpDataBuff, (const char *) "key")) {
            // Read new key material.
            if (!Helper_WIFI_SecurityHandle(&g_redirectionConfig, (const char *) (httpDataBuff + 6)))
                return Helper_APP_ConfigFailure(connHandle, httpDataBuff);
        } else if (!strcmp((char*) httpDataBuff, (const char*) "rs")) {
            memcpy((uint8_t *) appData.aws_iot_host, (void*) (httpDataBuff + 6), strlen((char*) (httpDataBuff + 6)));
            appData.aws_iot_host[strlen((char*) (httpDataBuff + 6))] = 0; /* Terminate string */

#if HARDCODE_AWS_CERT_KEY
            //memset(appData.aws_iot_host, 0, LENGTH_OF_HOST_NAME);
            strncpy(appData.aws_iot_host, "ANTFR029IN8FO.iot.us-east-1.amazonaws.com\0", 42);
            //printf("Host = %s\r\n", appData.aws_iot_host);
#endif                   
        } else if (!strcmp((char*) httpDataBuff, (const char*) "cc")) {
            memcpy((uint8_t *) appData.clientCert, (void*) (httpDataBuff + 6), strlen((char*) (httpDataBuff + 6)));
            appData.clientCert[strlen((char*) (httpDataBuff + 6))] = 0; /* Terminate string */

#if HARDCODE_AWS_CERT_KEY
            //memset(appData.clientCert, 0, LENGTH_OF_CERTIFICATE_AND_KEY);
            strncpy(appData.clientCert, "-----BEGIN CERTIFICATE-----\nMIIDWjCCAkKgAwIBAgIVAIut779YhzHt8rHraWjM83gWbD3RMA0GCSqGSIb3DQEBCwUAME0xSzBJBgNVBAsMQkFtYXpvbiBXZWIgU2VydmljZXMgTz1BbWF6b24uY29tIEluYy4gTD1TZWF0dGxlIFNUPVdhc2hpbmd0b24gQz1VUzAeFw0xNjA1MDExMjA4MDBaFw00OTEyMzEyMzU5NTlaMB4xHDAaBgNVBAMME0FXUyBJb1QgQ2VydGlmaWNhdGUwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQClptVs5b3c6Cr1duo4qvkESrWmCZMX2Bwj0YDCRc4RPZBW4vsEzwuI4eKEFWjmQ3/W1bDyx3WP4SiUBuPB+NC9QsH9oXnyKi+F2v2gQwBHCqlpyNWhiX80SrPLBDlhQq7cphORnuR4gYbd/m5eLYuEz49ZXKwCLU0aJ9+YCG5I/2v/F81gk94rHyCIoHOmnJBqL2RTW5sYU+6gI6G2By83+tiarpjlHwr33mjaMg1Z4+EaNYSjqJUOPJP90OcxnBfHqZ7jjm5GkKrruVBLcdkbOx0I/S7tAUfR0DVrpxC+BT6fktLnSuZumd736gAspdC2hXNdZKDIRzEGjvfNShitAgMBAAGjYDBeMB8GA1UdIwQYMBaAFB57ZDP28UfKdy6MuJzaNXXJbGzhMB0GA1UdDgQWBBSRajfFsjscYvBPSAWrAQN33mz72jAMBgNVHRMBAf8EAjAAMA4GA1UdDwEB/wQEAwIHgDANBgkqhkiG9w0BAQsFAAOCAQEAq636h8tXvrf5c94J7IEZSd/OSwfuD++iHCyM5mij7GTOJ+BuyEWfeh3Dmkg5ZVyMLX7MjpIN9R2gmxTucOdP4gN0neSVKNOXre0YP5ul2vUnxxUpR04qK2zMwMADrSeUs1ZNRfCp9ToUOuajzrXY3noEJkCHBCcQepqGF4mFGJqizp93etxP9G6IkslfAajz2gyV6i6zFC10RiMHVi7+QUSUQpekT0lxFq8dQe+0nIv8+HPQntqq+6s3dTRLt4DIAMOQ0sztKrplIGhOGkBYVjp9sert8RBt4psJF/HiC1kaTxCkqiXCdldUq3AykbLk7POtRoIz9KMnmLbhmgIHMQ==\n-----END CERTIFICATE-----\0", 1207);
            //printf("Cert = %s\r\n", appData.clientCert);
#endif
        } else if (!strcmp((char*) httpDataBuff, (const char*) "ck")) {
            memcpy((uint8_t *) appData.clientKey, (void*) (httpDataBuff + 6), strlen((char*) (httpDataBuff + 6)));
            appData.clientKey[strlen((char*) (httpDataBuff + 6))] = 0; /* Terminate string */

#if HARDCODE_AWS_CERT_KEY
            //memset(appData.clientKey, 0, LENGTH_OF_CERTIFICATE_AND_KEY);
            strncpy(appData.clientKey, "-----BEGIN RSA PRIVATE KEY-----\nMIIEogIBAAKCAQEApabVbOW93Ogq9XbqOKr5BEq1pgmTF9gcI9GAwkXOET2QVuL7BM8LiOHihBVo5kN/1tWw8sd1j+EolAbjwfjQvULB/aF58iovhdr9oEMARwqpacjVoYl/NEqzywQ5YUKu3KYTkZ7keIGG3f5uXi2LhM+PWVysAi1NGiffmAhuSP9r/xfNYJPeKx8giKBzppyQai9kU1ubGFPuoCOhtgcvN/rYmq6Y5R8K995o2jINWePhGjWEo6iVDjyT/dDnMZwXx6me445uRpCq67lQS3HZGzsdCP0u7QFH0dA1a6cQvgU+n5LS50rmbpne9+oALKXQtoVzXWSgyEcxBo73zUoYrQIDAQABAoIBAFyF471wUOzbLGtwIQDH1lCkXfNAc80kowsKkG1ySdftV/p/yw7zihDJghieULoUR4o6Txw7dhmH/H85nesQy556dBnzIEa3c1XDbFggND554Qg56cDRKKCPfP7O/DGr6jlJ9bInWptIVYkW/JRTwhLLT9js3xJUNTV/we6L/vV79NmI/dgo09AUdOj/pQIZYGfZmFmkHPSjpm5JFdo5oLizsaAy2zQZcBooC/tVwnR7PCvmVanefF+1gVxLPoIFVssOmR4MHiUmY7gh3OlG4vb10aUGjgPbgDmDJZLQ0aYSlzp3PNKnkPlro9KFE7LZC6y258a+rEWt6rRL9Am2x60CgYEA6pgCEsyvnsrcL5hMnZPJtyGd+L1U8cofLQrn8pyZwxzYHW6q2XPb2APqstXGw5nELUuXHQzXtyJ1J8wabozVQRPSt6jz92cH4YV9eAPkfXcQQHjeGva8AdHhDLTc598gS8Lcu/oP6X6DG47LEH8ammaiT7l/YwiBt7NMz5Vh4kMCgYEAtMRflBAgum3ADm88cVGdxV06IcnC467BVWnngFTB8nNLOL/ZOyUq+MwS1Ns1LiNdaojP80GpN/R616p7DiEsLxdyQBsWsmvw0S/BtzS0tYt5gAn7ruUXO++Wbk6xym/nj+qlb5IxOHlq5Pb3IJPZ5nfRCVGers/VlZ6omo1AQk8CgYAiLXs/2epMLCngFUQ0QO/GanNlZdAWWego28nnIsGUF4O05gamH6cL1aY/371Rifl2z+h4JwLWf4KqBaZkLMh07rpoX4kLpksTpCsfbRHA5bPMcM+LBh9l9HnhLAhzD6LY5s7Z5BilX/Uh8q/K+3mYvRMwoknY18huRwUNySm1mQKBgA6fzHO4Ek3Cz0TbrvIj/jWXYzqUjhXJb46vcLErKj2pIi7HJpXoXd+e8u8RhO3koowZ4Wj2qPAD8WQ9QJNWD7MHnJCfIGjy3pqt2Ggv9Wayj/PR2vC9S/HnYi4uY6fTAdLc0uGB3EWLXSCE8l1KWNiSXPD6D3JpEgh2u4E1aDt1AoGAIWGOabcICSCDLBKMzlHp3SLIFHfFFcGiOiUie0dsYfd4J4UIRMFWTOiGy11+hSQCsJB+NSYsOqgalRZspf9LjcoEnHQjo714aLGRBLt7rgpleCrD0s/FOerhgYkq8uhQRYk1MM6HJsB30QQE9ae9vqsGXCF2Q74lUWpVL+s2kVs=\n-----END RSA PRIVATE KEY-----\0", 1651);
            //printf("CertKey = %s\r\n", appData.clientKey);
#endif            
        }
    }

    /* Check if WPA hasn't been selected with Ad-Hoc, if it has we choke! */
    if ((g_redirectionConfig.networkType == WF_NETWORK_TYPE_ADHOC) && (
            (g_redirectionConfig.securityMode == WF_SECURITY_WPA_WITH_PASS_PHRASE) ||
            (g_redirectionConfig.securityMode == WF_SECURITY_WPA2_WITH_PASS_PHRASE) ||
            (g_redirectionConfig.securityMode == WF_SECURITY_WPA_AUTO_WITH_PASS_PHRASE)))
        return Helper_APP_ConfigFailure(connHandle, httpDataBuff);

    /*
     * All parsing complete!  If we have got to here all data has been validated and
     * We can handle what is necessary to start the reconfigure process of the Wi-Fi device.
     */

    /* Copy Wi-Fi cfg data to be committed to NVM. */
    s_httpapp_get_param.config.data = &g_wifi_cfg;
    iwpriv_get(CONFIG_GET, &s_httpapp_get_param);
    strcpy((char *) g_wifi_cfg.ssid, (char *) g_redirectionConfig.ssid);
    g_wifi_cfg.ssidLen = strlen((char *) (g_redirectionConfig.ssid));
    /* Going to set security type. */
    g_wifi_cfg.securityMode = g_redirectionConfig.securityMode;
    /* Going to save the key, if required. */
    if (g_redirectionConfig.securityMode != WF_SECURITY_OPEN)
        Helper_WIFI_KeySave(&g_redirectionConfig, &g_wifi_cfg);
    /* Going to save the network type. */
    g_wifi_cfg.networkType = g_redirectionConfig.networkType;
    s_httpapp_set_param.config.data = &g_wifi_cfg;
    iwpriv_set(CONFIG_SET, &s_httpapp_set_param);



    APP_NVM_Write(DRV_CLIENT_CERTIFICATE, (uint8_t *) appData.clientCert);
    APP_NVM_Write(DRV_CLIENT_PRIVATE_KEY, (uint8_t *) appData.clientKey);

    BSP_LED_LightShowSet(BSP_LED_CONNECTING_TO_AP);

    /* Set 1s delay before redirection, goal is to display the redirection web page. */
    uint16_t redirection_delay = SYS_TMR_TickCounterFrequencyGet() * HTTP_APP_REDIRECTION_DELAY_TIME;
    SYS_TMR_CallbackSingle(redirection_delay, 0, Helper_APP_RedirectionFlagSet);

    strcpy((char *) httpDataBuff, "/reconnect.htm");
    TCPIP_HTTP_CurrentConnectionStatusSet(connHandle, HTTP_REDIRECT);

    return HTTP_IO_DONE;
}
#endif // defined(HTTP_APP_USE_WIFI)
#endif // defined(TCPIP_HTTP_USE_POST)

/****************************************************************************
  Section:
    Dynamic Variable Callback Functions
 ****************************************************************************/

/*****************************************************************************
  Function:
    void TCPIP_HTTP_Print_varname(void)

  Internal:
    See documentation in the TCP/IP Stack API or HTTP.h for details.
 ***************************************************************************/

void TCPIP_HTTP_Print_remoteServer(HTTP_CONN_HANDLE connHandle) {
    TCP_SOCKET sktHTTP;
    sktHTTP = TCPIP_HTTP_CurrentConnectionSocketGet(connHandle);
    TCPIP_TCP_StringPut(sktHTTP, (const uint8_t*) appData.aws_iot_host);
}

void TCPIP_HTTP_Print_nextSSID(HTTP_CONN_HANDLE connHandle) {
    TCP_SOCKET sktHTTP = TCPIP_HTTP_CurrentConnectionSocketGet(connHandle);

    TCPIP_TCP_StringPut(sktHTTP, (uint8_t *) g_redirectionConfig.ssid); // nextSSID
}
#endif // #if defined(TCPIP_STACK_USE_HTTP_SERVER)
