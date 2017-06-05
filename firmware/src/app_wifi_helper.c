
/*
 * app_wifi_helper.c: Wifi helper file to the application used to configure and connect wifi.
 */

#include "app_wifi_helper.h"

#if defined(TCPIP_IF_MRF24W)
#define WIFI_INTERFACE_NAME "MRF24W"
#endif
#if defined(TCPIP_STACK_USE_ZEROCONF_MDNS_SD)
#define IS_MDNS_RUN() true
#else
#define IS_MDNS_RUN() false
#define TCPIP_MDNS_ServiceRegister(a, b, c, d, e, f, g, h) do {} while (0)
#define TCPIP_MDNS_ServiceDeregister(a) do {} while (0)
#endif /* defined(TCPIP_STACK_USE_ZEROCONF_MDNS_SD) */

#define IS_WF_INTF(x) ((strcmp(x, "MRF24W") == 0) || (strcmp(x, "MRF24WN") == 0))


//extern APP_DATA appData;

APP_WIFI_HelperData wh_data;

static IWPRIV_GET_PARAM s_app_get_param;
static IWPRIV_SET_PARAM s_app_set_param;
static IWPRIV_EXECUTE_PARAM s_app_execute_param;

bool g_redirect_signal = false;
WF_CONFIG_DATA g_wifi_cfg;
WF_DEVICE_INFO g_wifi_deviceInfo;
WF_REDIRECTION_CONFIG g_redirectionConfig;

void APP_WIFI_Helper_Init()
{
    s_app_set_param.conn.initConnAllowed = false;
    iwpriv_set(INITCONN_OPTION_SET, &s_app_set_param);
    wh_data.wh_state = WIFI_HELPER_TCPIP_WAIT_INIT;
    wh_data.scan_state = APP_WIFI_PRESCAN_INIT;
}

WIFI_HELPER_STATES APP_WIFI_Helper_Tasks()
{
    static bool isWiFiPowerSaveConfigured = false;
    static bool wasNetUp[2] = {true, true}; // this app supports 2 interfaces so far
    static uint32_t startTick = 0;
    static IPV4_ADDR defaultIPWiFi = {-1};
    static IPV4_ADDR dwLastIP[2] = { {-1}, {-1} }; // this app supports 2 interfaces so far
    static IPV4_ADDR ipAddr = {-1};
    TCPIP_NET_HANDLE netH;
    static TCPIP_NET_HANDLE netHandleWiFi;
    int i, nNets;
    
    switch (wh_data.wh_state) {
        case WIFI_HELPER_TCPIP_WAIT_INIT:
        {
            SYS_STATUS tcpipStat = TCPIP_STACK_Status(sysObj.tcpip);
            if (tcpipStat < 0)
            {
                printf("APP: TCP/IP stack initialization failed!\r\n");
                wh_data.wh_state = WIFI_HELPER_ERROR;
            }
            else if (tcpipStat == SYS_STATUS_READY)
            {
                printf("tcpipStat == SYS_STATUS_READY\r\n");
                wh_data.wh_state = WIFI_HELPER_CONFIG;
            }
            break;
        }
        
        case WIFI_HELPER_CONFIG:
        {
            /*
             * Following "if condition" is useless when demo firstly
             * boots up, since stack's status has already been checked in
             * APP_TCPIP_WAIT_INIT. But it is necessary in redirection or
             * Wi-Fi interface reset due to connection errors.
             */
            iwpriv_get(INITSTATUS_GET, &s_app_get_param);
            if (s_app_get_param.driverStatus.initStatus == IWPRIV_READY) {
                s_app_get_param.devInfo.data = &g_wifi_deviceInfo;
                iwpriv_get(DEVICEINFO_GET, &s_app_get_param);
                defaultIPWiFi.Val = TCPIP_STACK_NetAddress(netHandleWiFi);
                netHandleWiFi = TCPIP_STACK_NetHandleGet(WIFI_INTERFACE_NAME);

                // initialize redirection variable
                APP_WIFIG_SSID_Set(netHandleWiFi);
                APP_WIFI_RedirectionConfigInit();

                if (!g_redirect_signal) {
                    s_app_set_param.scan.prescanAllowed = true;
                    iwpriv_set(PRESCAN_OPTION_SET, &s_app_set_param);
                    wh_data.wh_state = WIFI_HELPER_PRESCAN;
                    printf("Going to PRESCAN\r\n");
                } else {
                    g_redirect_signal = false;
                    APP_TCPIP_IFModules_Enable(netHandleWiFi);
                    wh_data.wh_state = WIFI_HELPER_WAIT_FOR_IP;//WIFI_HELPER_TCPIP_TRANSACT;
                    printf("Going to TRANSACT\r\n");
                    break;
                }
            } else {
                break;
            }
            break;
        }

        case WIFI_HELPER_PRESCAN:
        {
            // if pre-scan option is set to false,
            // this state would just run once and pass,
            // APP_WIFI_Prescan() function would not actually
            // do anything
            uint8_t scanStatus = APP_WIFI_Prescan();
            if (scanStatus == IWPRIV_READY) {
                wh_data.wh_state = WIFI_HELPER_TCPIP_MODULES_ENABLE;
            } else if (scanStatus == IWPRIV_ERROR) {
                printf("Wi-Fi Prescan Error.\r\n");
                wh_data.wh_state = WIFI_HELPER_TCPIP_MODULES_ENABLE;
            } else {
                break;
            }
        }

        case WIFI_HELPER_TCPIP_MODULES_ENABLE:
        {
            // check the available interfaces
            nNets = TCPIP_STACK_NumberOfNetworksGet();
            for (i = 0; i < nNets; ++i)
                APP_TCPIP_IFModules_Enable(TCPIP_STACK_IndexToNet(i));
            wh_data.wh_state = WIFI_HELPER_TCPIP_TRANSACT;
            break;
        }

        case WIFI_HELPER_TCPIP_TRANSACT:
        {
            // wait for redirection command from custom_http_app.c
            iwpriv_get(CONNSTATUS_GET, &s_app_get_param);
            if (s_app_get_param.conn.status == IWPRIV_CONNECTION_FAILED || g_redirect_signal) {
                printf("********** || g_redirect_signal\r\n");
                APP_TCPIP_IFModules_Disable(netHandleWiFi);
                APP_TCPIP_IF_Down(netHandleWiFi);
                APP_TCPIP_IF_Up(netHandleWiFi);
                isWiFiPowerSaveConfigured = false;
                wh_data.wh_state = WIFI_HELPER_CONFIG;
                break;
            } else if (s_app_get_param.conn.status == IWPRIV_CONNECTION_REESTABLISHED) {
                printf("********** IWPRIV_CONNECTION_REESTABLISHED\r\n");
                // restart dhcp client and config power save
                iwpriv_get(OPERATIONMODE_GET, &s_app_get_param);
                if (!s_app_get_param.opMode.isServer) {
                    TCPIP_DHCP_Disable(netHandleWiFi);
                    TCPIP_DHCP_Enable(netHandleWiFi);
                    isWiFiPowerSaveConfigured = false;
                }
            }
            
            iwpriv_get(NETWORKTYPE_GET, &s_app_get_param);
            if (s_app_get_param.netType.type == WF_NETWORK_TYPE_INFRASTRUCTURE) {
                printf("*********************WF_NETWORK_TYPE_INFRASTRUCTURE\r\n");
                wh_data.wh_state = WIFI_HELPER_WAIT_FOR_IP;
                break;
            }

            /*
             * Following for loop is to deal with manually controlling
             * interface down/up (for example, through console commands
             * or web page).
             */
            nNets = TCPIP_STACK_NumberOfNetworksGet();
            for (i = 0; i < nNets; ++i) {
                TCPIP_NET_HANDLE netH = TCPIP_STACK_IndexToNet(i);
                if (!TCPIP_STACK_NetIsUp(netH) && wasNetUp[i]) {
                    printf("********************** APP_TCPIP_IFModules_Disable\r\n");
                    const char *netName = TCPIP_STACK_NetNameGet(netH);
                    wasNetUp[i] = false;
                    APP_TCPIP_IFModules_Disable(netH);
                    if (IS_WF_INTF(netName))
                        isWiFiPowerSaveConfigured = false;
                }

                if (TCPIP_STACK_NetIsUp(netH) && !wasNetUp[i]) {
                    printf("********************** APP_TCPIP_IFModules_Enable\r\n");
                    wasNetUp[i] = true;
                    APP_TCPIP_IFModules_Enable(netH);
                }
            }

            /*
             * If we get a new IP address that is different than the default one,
             * we will run PowerSave configuration.
             */
            if (!isWiFiPowerSaveConfigured &&
                    TCPIP_STACK_NetIsUp(netHandleWiFi) &&
                    (TCPIP_STACK_NetAddress(netHandleWiFi) != defaultIPWiFi.Val)) {
                printf("*********************************************  isWiFiPowerSaveConfigured\r\n");
                APP_WIFI_PowerSave_Config(true);
                isWiFiPowerSaveConfigured = true;
            }

            APP_WIFI_DHCPS_Sync(netHandleWiFi);                        

            if (SYS_TMR_TickCountGet() - startTick >= SYS_TMR_TickCounterFrequencyGet() / 2ul) {
                startTick = SYS_TMR_TickCountGet();
                BSP_LEDToggle(BSP_LED_1);
            }
            
            break;
        }
        
        case WIFI_HELPER_WAIT_FOR_IP:
        {
            /*
             * If the IP address of an interface has changed,
             * display the new value on the system console.
             */
            nNets = TCPIP_STACK_NumberOfNetworksGet();
            for (i = 0; i < nNets; i++) {
                netH = TCPIP_STACK_IndexToNet(i);
                ipAddr.Val = TCPIP_STACK_NetAddress(netH);
                if (dwLastIP[i].Val != ipAddr.Val) {
                    dwLastIP[i].Val = ipAddr.Val;

                    printf("%s\r\n", TCPIP_STACK_NetNameGet(netH));
                    printf(" IP Address: ");
                    printf("%d.%d.%d.%d \r\n", ipAddr.v[0], ipAddr.v[1], ipAddr.v[2], ipAddr.v[3]);
                    if (ipAddr.v[0] != 0 && ipAddr.v[0] != 169) // Wait for a Valid IP
                    {
                        printf("***************** WIFI_HELPER_CONNECT_DONE\r\n");
                        wh_data.wh_state = WIFI_HELPER_CONNECT_DONE;
                        break;
                    }
                }
            }
            break;
        }
        
        case WIFI_HELPER_CONNECT_DONE:
        {
            break;
        }
        
        case WIFI_HELPER_ERROR:
        {
            printf("WIFI_HELPER_ERROR\r\n");
            break;
        }
        
        default:
        {
            break;
        }
    }
    return wh_data.wh_state;
}

/**********************************************************************************************************/

uint8_t APP_WIFI_Prescan(void)
{
    switch (wh_data.scan_state) {
        case APP_WIFI_PRESCAN_INIT:
            iwpriv_get(PRESCAN_OPTION_GET, &s_app_get_param);
            if (s_app_get_param.scan.prescanAllowed) {
                iwpriv_get(NETWORKTYPE_GET, &s_app_get_param);
                uint8_t type = s_app_get_param.netType.type;
                iwpriv_get(CONNSTATUS_GET, &s_app_get_param);
                if (type == WF_NETWORK_TYPE_SOFT_AP && s_app_get_param.conn.status == IWPRIV_CONNECTION_SUCCESSFUL)
                    return IWPRIV_ERROR;
                iwpriv_execute(PRESCAN_START, &s_app_execute_param);
                wh_data.scan_state = APP_WIFI_PRESCAN_WAIT;
                break;
            } else {
                return IWPRIV_READY;
            }

        case APP_WIFI_PRESCAN_WAIT:
            iwpriv_get(PRESCAN_ISFINISHED_GET, &s_app_get_param);
            if (s_app_get_param.scan.prescanFinished)
            {
                iwpriv_get(SCANSTATUS_GET, &s_app_get_param);
                if (s_app_get_param.scan.scanStatus == IWPRIV_SCAN_SUCCESSFUL) {
                    wh_data.scan_state = APP_WIFI_PRESCAN_SAVE;
                } else {
                    wh_data.scan_state = APP_WIFI_PRESCAN_INIT;
                    return IWPRIV_ERROR;
                }
            } else {
                break;
            }

        case APP_WIFI_PRESCAN_SAVE:
            iwpriv_execute(SCANRESULTS_SAVE, &s_app_execute_param);
            if (s_app_execute_param.scan.saveStatus == IWPRIV_IN_PROGRESS)
                break;
            else // IWPRIV_READY
                wh_data.scan_state = APP_WIFI_PRESCAN_RESET;

        case APP_WIFI_PRESCAN_RESET: {
            TCPIP_NET_HANDLE netH = TCPIP_STACK_NetHandleGet(WIFI_INTERFACE_NAME);
            APP_TCPIP_IF_Down(netH);
            APP_TCPIP_IF_Up(netH);
            s_app_set_param.conn.initConnAllowed = true;
            iwpriv_set(INITCONN_OPTION_SET, &s_app_set_param);
            s_app_set_param.scan.prescanAllowed = false;
            iwpriv_set(PRESCAN_OPTION_SET, &s_app_set_param);
            wh_data.scan_state = APP_WIFI_PRESCAN_WAIT_RESET;
            break;
        }

        case APP_WIFI_PRESCAN_WAIT_RESET:
            iwpriv_get(INITSTATUS_GET, &s_app_get_param);
            if (s_app_get_param.driverStatus.initStatus == IWPRIV_READY)
                wh_data.scan_state = APP_WIFI_PRESCAN_DONE;
            else
                break;

        case APP_WIFI_PRESCAN_DONE:
            wh_data.scan_state = APP_WIFI_PRESCAN_INIT;
            return IWPRIV_READY;
    }

    return IWPRIV_IN_PROGRESS;
}

static void APP_TCPIP_IF_Down(TCPIP_NET_HANDLE netH)
{
    TCPIP_STACK_NetDown(netH);
}

static void APP_TCPIP_IF_Up(TCPIP_NET_HANDLE netH)
{
    SYS_MODULE_OBJ tcpipStackObj;
    TCPIP_STACK_INIT tcpip_init_data;
    const TCPIP_NETWORK_CONFIG *pIfConf;
    uint16_t net_ix = TCPIP_STACK_NetIndexGet(netH);

    tcpipStackObj = TCPIP_STACK_Initialize(0, 0);
    TCPIP_STACK_InitializeDataGet(tcpipStackObj, &tcpip_init_data);
    pIfConf = tcpip_init_data.pNetConf + net_ix;
    TCPIP_STACK_NetUp(netH, pIfConf);
}
static void APP_WIFI_RedirectionConfigInit(void)
{
    g_redirectionConfig.ssid[0] = 0;
    g_redirectionConfig.securityMode = WF_SECURITY_OPEN;
    g_redirectionConfig.securityKey[0] = 0;
    g_redirectionConfig.wepKeyIndex = WF_WEP_KEY_INVALID;
    g_redirectionConfig.networkType = WF_NETWORK_TYPE_INFRASTRUCTURE;
}

static void APP_WIFIG_SSID_Set(TCPIP_NET_HANDLE netH)
{
    const uint8_t *mac;
    uint8_t ssid[32 + 1] = {0};
    uint8_t ssidLen;

    s_app_get_param.ssid.ssid = ssid;
    iwpriv_get(SSID_GET, &s_app_get_param);
    ssidLen = s_app_get_param.ssid.ssidLen;
    if (ssidLen == 32) return;
    mac = TCPIP_STACK_NetAddressMac(netH);
    printf("SSID org = %s\r\n", ssid);
    if (strcmp((const char *)ssid, "xxxxxx_IoT") == 0) {
        sprintf((char *)ssid, "%02x%02x%02x_IoT", mac[3], mac[4], mac[5]);
        printf("SSID changed = %s\r\n", ssid);
        ssidLen = strlen((char *)ssid);
        s_app_set_param.ssid.ssid = ssid;
        s_app_set_param.ssid.ssidLen = ssidLen;
        iwpriv_set(SSID_SET, &s_app_set_param);
    }
}

static void APP_WIFI_IPv6MulticastFilter_Set(TCPIP_NET_HANDLE netH)
{
#if defined(TCPIP_STACK_USE_IPV6)
    const uint8_t *pMacAddr = TCPIP_STACK_NetAddressMac(netH);
    int i;
    uint8_t linkLocalSolicitedMulticastMacAddr[6];
    uint8_t solicitedNodeMulticastMACAddr[] = {0x33, 0x33, 0xff, 0x00, 0x00, 0x00};
    uint8_t allNodesMulticastMACAddr[] = {0x33, 0x33, 0x00, 0x00, 0x00, 0x01};

    linkLocalSolicitedMulticastMacAddr[0] = 0x33;
    linkLocalSolicitedMulticastMacAddr[1] = 0x33;
    linkLocalSolicitedMulticastMacAddr[2] = 0xff;

    for (i = 3; i < 6; i++)
        linkLocalSolicitedMulticastMacAddr[i] = pMacAddr[i];

    s_app_set_param.multicast.addr = linkLocalSolicitedMulticastMacAddr;
    iwpriv_set(MULTICASTFILTER_SET, &s_app_set_param);
    s_app_set_param.multicast.addr = solicitedNodeMulticastMACAddr;
    iwpriv_set(MULTICASTFILTER_SET, &s_app_set_param);
    s_app_set_param.multicast.addr = allNodesMulticastMACAddr;
    iwpriv_set(MULTICASTFILTER_SET, &s_app_set_param);
#endif
}

static void APP_WIFI_DHCPS_Sync(TCPIP_NET_HANDLE netH)
{
#if defined(TCPIP_STACK_USE_DHCP_SERVER)
    bool updated;
    TCPIP_MAC_ADDR addr;

    s_app_get_param.clientInfo.addr = addr.v;
    iwpriv_get(CLIENTINFO_GET, &s_app_get_param);
    updated = s_app_get_param.clientInfo.updated;

    if (updated)
        TCPIP_DHCPS_LeaseEntryRemove(netH, (TCPIP_MAC_ADDR *)&addr);
#endif
}

static void APP_WIFI_PowerSave_Config(bool enable)
{
#if WF_DEFAULT_POWER_SAVE == WF_ENABLED
    s_app_set_param.powerSave.enabled = enable;
    iwpriv_set(POWERSAVE_SET, &s_app_set_param);
#endif
}

static void APP_TCPIP_IFModules_Disable(TCPIP_NET_HANDLE netH)
{
    const char *netName = TCPIP_STACK_NetNameGet(netH);

    if (IS_WF_INTF(netName) && TCPIP_STACK_NetIsUp(netH))
        APP_WIFI_PowerSave_Config(false);
    TCPIP_DHCPS_Disable(netH);
    TCPIP_DHCP_Disable(netH);
    TCPIP_DNSS_Disable(netH);
    TCPIP_DNS_Disable(netH, true);
    TCPIP_MDNS_ServiceDeregister(netH);
}

static void APP_TCPIP_IFModules_Enable(TCPIP_NET_HANDLE netH)
{
    int netIndex = TCPIP_STACK_NetIndexGet(netH);
    const char *netName = TCPIP_STACK_NetNameGet(netH);

    /*
     * If it's not Wi-Fi interface, then leave it to the TCP/IP stack
     * to configure its DHCP server/client status.
     */
    if (IS_WF_INTF(netName)) {
        iwpriv_get(OPERATIONMODE_GET, &s_app_get_param);
        if (s_app_get_param.opMode.isServer) {
            TCPIP_DHCP_Disable(netH); // must stop DHCP client first
            TCPIP_DHCPS_Enable(netH); // start DHCP server
            TCPIP_DNS_Disable(netH, true);
            TCPIP_DNSS_Enable(netH);
        } else {
            TCPIP_DHCPS_Disable(netH); // must stop DHCP server first
            TCPIP_DHCP_Enable(netH); // start DHCP client
            TCPIP_DNSS_Disable(netH);
            TCPIP_DNS_Enable(netH, TCPIP_DNS_ENABLE_DEFAULT);
        }
        APP_WIFI_IPv6MulticastFilter_Set(netH);
    }
    if (IS_MDNS_RUN()) {
        char mDNSServiceName[] = "MyWebServiceNameX "; // base name of the service Must not exceed 16 bytes long
        // the last digit will be incremented by interface
        mDNSServiceName[sizeof(mDNSServiceName) - 2] = '1' + netIndex;
        TCPIP_MDNS_ServiceRegister(netH, mDNSServiceName, "_http._tcp.local", 80, ((const uint8_t *)"path=/index.htm"),
            1, NULL, NULL);
    }
}

uint8_t APP_WIFI_Get_Network_Type()
{
    IWPRIV_GET_PARAM s_app_wifi_get_param;
    iwpriv_get(NETWORKTYPE_GET, &s_app_wifi_get_param);
    return s_app_wifi_get_param.netType.type;
}