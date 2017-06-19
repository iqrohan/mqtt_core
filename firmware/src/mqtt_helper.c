
/*
 * mqtt_helper.c: MQTT helper file to the application used to manage all MQTT transfers.
 */

#include "mqtt_helper.h"
#include "app_wifi_helper.h"
#include "MQTTPacket/src/MQTTPacket.h"

MQTT_HELPER_DATA mqtt_data;

bool mqtt_helper_timer_expired(uint32_t * timer, uint32_t seconds)
{
    if ((SYS_TMR_TickCountGet() - *timer) > (seconds * 1000)) {
        return true;
    } else {
        return false;
    }
    return false;
}

bool mqtt_helper_timer_set(uint32_t * timer)
{
    *timer = SYS_TMR_TickCountGet();
    return true;
}

void mqtt_helper_tcpip_check_socket_was_reset(void)
{
    if (NET_PRES_SocketWasReset(mqtt_data.socket)) {
        mqtt_data.tcpip_state = MQTT_HELPER_TCPIP_ERROR;
    }
    return;
}

int mqtt_deserialize_pingresp(unsigned char* buf, int buflen)
{
    unsigned char* curdata = buf;
	int rc = 0;
    MQTTHeader header = {0};

    header.byte = readChar(&curdata);
	if (header.bits.type != PINGRESP)
		return 0;
    else
        return 1;
}

void mqtt_helper_payload_info(MY_QUEUE *que_addr, char *payload_addr)   // Received from APP before init
{
    mqtt_data.queue = que_addr;
    mqtt_data.reportedPayload = payload_addr;
}
void mqtt_helper_thing_info(char *thing_name, char *update_string, char *delta_string) // Received from APP before init
{
    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
    MQTTString topicStringUpdate = MQTTString_initializer;
    MQTTString topicStringDelta = MQTTString_initializer;
    
    memcpy(&mqtt_data.data, &data, sizeof(MQTTPacket_connectData));
    memcpy(&mqtt_data.topicStringUpdate, &topicStringUpdate, sizeof(MQTTString));
    memcpy(&mqtt_data.topicStringDelta, &topicStringDelta, sizeof(MQTTString));

    mqtt_data.data.clientID.cstring = thing_name;//"e000001iot";
    mqtt_data.topicStringUpdate.cstring = update_string;//"$aws/things/e000001iot/shadow/update";
    mqtt_data.topicStringDelta.cstring = delta_string;//"$aws/things/e000001iot/shadow/update/delta";
        
	mqtt_data.data.keepAliveInterval = MQTT_KEEP_ALIVE;
	mqtt_data.data.cleansession = 1;
}

void mqtt_helper_init()
{
    mqtt_data.tcpip_state = MQTT_HELPER_TCPIP_WAIT_INIT;
    APP_WIFI_Helper_Init();
    mqtt_data.port = MQTT_PORT;
}

void mqtt_helper_tasks()
{
    static IPV4_ADDR dwLastIP[2] = { {-1}, {-1} };
    static IPV4_ADDR ipAddr = {-1};        
    
    int req_qos = 0;
    unsigned char buf[1024];
    int buflen = sizeof (buf);
    int len = 0;    
    
    int msgid = 1;
    
    switch (mqtt_data.tcpip_state) {
        case MQTT_HELPER_TCPIP_WAIT_INIT:
        {
            WIFI_HELPER_STATES st = APP_WIFI_Helper_Tasks();
            if (st == WIFI_HELPER_CONNECT_DONE) {
                mqtt_data.tcpip_state = MQTT_HELPER_TCPIP_STORE_HOSTNAME;
                printf("WIFI_HELPER_CONNECT_DONE\r\n");
            }
            break;
        }
        case MQTT_HELPER_TCPIP_STORE_HOSTNAME:
        {            
            uint8_t type = APP_WIFI_Get_Network_Type();

            if (type == DRV_WIFI_NETWORK_TYPE_SOFT_AP) {
                break;
            } else {
#if 0
                // Write the server we retrieved from user
                if (APP_NVM_Write(DRV_SERVER_NVM_SPACE_ADDR, (uint8_t *) appData.aws_iot_host) != true) {
//                    APP_NVM_Write(DRV_CLIENT_CERTIFICATE, (uint8_t *) appData.clientCert);
//                    APP_NVM_Write(DRV_CLIENT_PRIVATE_KEY, (uint8_t *) appData.clientKey);
                    printf("Host: DRV_SERVER_NVM_SPACE_ADDR\r\n");
                    break;
                } else {
#endif
                    TCPIP_NET_HANDLE netH;
                    int i, nNets;

                    nNets = TCPIP_STACK_NumberOfNetworksGet();
                    for (i = 0; i < nNets; i++) {
                        printf("Host: DRV_SERVER_NVM_SPACE_ADDR\r\n");
                        netH = TCPIP_STACK_IndexToNet(i);
                        ipAddr.Val = TCPIP_STACK_NetAddress(netH);
                        if (dwLastIP[i].Val != ipAddr.Val) {
                            dwLastIP[i].Val = ipAddr.Val;
                            printf("%s\r\n", TCPIP_STACK_NetNameGet(netH));
                            printf(" IP Address: ");
                            printf("%d.%d.%d.%d \r\n", ipAddr.v[0], ipAddr.v[1], ipAddr.v[2], ipAddr.v[3]);
                            if (ipAddr.v[0] != 0 && ipAddr.v[0] != 169) // Wait for a Valid IP
                            {
                                mqtt_data.tcpip_state = MQTT_HELPER_TCPIP_RESOLVE_DNS; //APP_DELAY;
                                printf("Print Details of HOST,CERT,KEY:- \r\n");
#if 0//PRINT_HOST_CERT_KEY
                                printf("HOST: %s\r\n", appData.aws_iot_host);
                                printf("Cert: %s\r\n", appData.clientCert);
                                printf("CertKey: %s\r\n", appData.clientKey);
#endif
                            }
                        }
                    }
                //}
            }
            break;
        }
        
        case MQTT_HELPER_TCPIP_RESOLVE_DNS:
        {
            printf("APP_TCPIP_RESOLVE_DNS\r\n");
            TCPIP_DNS_RESULT result;
            result = TCPIP_DNS_Resolve(appData.aws_iot_host, TCPIP_DNS_TYPE_A);
            printf("DNS result: %d\r\n", result);
            if (result < 0) {
                printf("DNS Error: %d\r\n", result);
                BSP_LED_LightShowSet(BSP_LED_DNS_FAILED);
                mqtt_data.tcpip_state = MQTT_HELPER_TCPIP_ERROR;
                break;
            }
            mqtt_data.tcpip_state = MQTT_HELPER_TCPIP_WAIT_ON_DNS;
            mqtt_helper_timer_set(&mqtt_data.tcpipTimeout);

            break;
        }
        case MQTT_HELPER_TCPIP_WAIT_ON_DNS:
        {
            // If it takes longer than 10 seconds to resolve domain name, timeout
            if (mqtt_helper_timer_expired(&mqtt_data.tcpipTimeout, 5)) {
                BSP_LED_LightShowSet(BSP_LED_DNS_FAILED);
                mqtt_data.tcpip_state = MQTT_HELPER_TCPIP_RESOLVE_DNS;
                break;
            }

            TCPIP_DNS_RESULT result = TCPIP_DNS_IsResolved(appData.aws_iot_host, &mqtt_data.aws_iot_ipv4, IP_ADDRESS_TYPE_IPV4);
            switch (result) {
                case TCPIP_DNS_RES_OK:
                    printf("DNS Resolved to %d.%d.%d.%d\r\n", mqtt_data.aws_iot_ipv4.v4Add.v[0], mqtt_data.aws_iot_ipv4.v4Add.v[1], mqtt_data.aws_iot_ipv4.v4Add.v[2], mqtt_data.aws_iot_ipv4.v4Add.v[3]);
                    mqtt_data.tcpip_state = MQTT_HELPER_TCPIP_DNS_RESOLVED;
                    break;
                case TCPIP_DNS_RES_PENDING:
                    break;
                case TCPIP_DNS_RES_SERVER_TMO:
                case TCPIP_DNS_RES_NO_IP_ENTRY:
                default:
                    printf("TCPIP_DNS_IsResolved returned failure\r\n");
                    break;
            }
            break;
        }
        case MQTT_HELPER_TCPIP_DNS_RESOLVED:
        {
            mqtt_data.socket = NET_PRES_SocketOpen(0, NET_PRES_SKT_ENCRYPTED_STREAM_CLIENT, IP_ADDRESS_TYPE_IPV4, mqtt_data.port, (NET_PRES_ADDRESS *) & mqtt_data.aws_iot_ipv4, &mqtt_data.error);
            NET_PRES_SocketWasReset(mqtt_data.socket);
            if (mqtt_data.socket == INVALID_SOCKET) {
                printf("Invalid Socket: %d\r\n", mqtt_data.error);
                BSP_LED_LightShowSet(BSP_LED_SERVER_CONNECT_FAILED);
                break;
            }
            mqtt_helper_timer_set(&mqtt_data.tcpipTimeout);
            mqtt_data.tcpip_state = MQTT_HELPER_TCPIP_WAIT_SOCKET;
            break;
        }

        case MQTT_HELPER_TCPIP_WAIT_SOCKET:
        {
            // Wait for a valid socket connection, else timeout if none present
            if (mqtt_helper_timer_expired(&mqtt_data.tcpipTimeout, 20)) {
                BSP_LED_LightShowSet(BSP_LED_SERVER_CONNECT_FAILED);
                mqtt_helper_timer_set(&mqtt_data.tcpipTimeout);
                
                NET_PRES_SocketClose(mqtt_data.socket); // close the current socket.
                //if (error == NET_PRES_SKT_OK) {
                    mqtt_data.tcpip_state = MQTT_HELPER_TCPIP_RESOLVE_DNS; // App_State go back to APP_TCPIP_RESOLVE_DNS for, again resolve DNS for avoid server connection error when wifi present.
                //}                                
                //mqtt_data.tcpip_state = APP_TCPIP_DNS_RESOLVED;
                break;
            }
            if (!NET_PRES_SocketIsConnected(mqtt_data.socket)) {
                break;
            }

            mqtt_helper_tcpip_check_socket_was_reset();

            if (NET_PRES_SocketIsNegotiatingEncryption(mqtt_data.socket)) {
                break;
            }
            if (!NET_PRES_SocketIsSecure(mqtt_data.socket)) {
                BSP_LED_LightShowSet(BSP_LED_SERVER_CONNECT_FAILED);
                printf("SocketIsSecure Failed\r\n");
                mqtt_data.tcpip_state = MQTT_HELPER_TCPIP_ERROR;
                break;
            }
            BSP_LED_LightShowSet(BSP_LED_ALL_GOOD);
            printf("All Good..\r\n");
            
            //appData.led1 ? BSP_LEDOn(BSP_LED_1) : BSP_LEDOff(BSP_LED_1);
            //MY_QUEUE_OBJECT currentLeds = {false, true};
            //enqueue((&appData.myQueue), currentLeds);
            mqtt_data.tcpip_state = MQTT_HELPER_TCPIP_MQTT_CONNECT;

            break;
        }
        
        case MQTT_HELPER_TCPIP_MQTT_CONNECT:
        {
            // We connect as an MQTT client so we send a CONNECT packet
            mqtt_helper_tcpip_check_socket_was_reset();

            // Serialize an MQTT CONNECT packet
            len = MQTTSerialize_connect(buf, buflen, &mqtt_data.data);

            if (NET_PRES_SocketWriteIsReady(mqtt_data.socket, len, 0) == 0) {
                break;
            }

            // Write the packet to the network TX buffer to be sent
            uint16_t wlen = NET_PRES_SocketWrite(mqtt_data.socket, (uint8_t*) buf, len);

            printf("len=%d, wlen=%d\r\n", len, wlen);

            mqtt_helper_timer_set(&mqtt_data.mqttMessageTimer);
            mqtt_data.tcpip_state = MQTT_HELPER_TCPIP_MQTT_CONNACK;
            break;
        }

        case MQTT_HELPER_TCPIP_MQTT_CONNACK:
        {
            if (mqtt_helper_timer_expired(&mqtt_data.mqttMessageTimer, 20)) {
                BSP_LED_LightShowSet(BSP_LED_SERVER_CONNECT_FAILED);
                mqtt_data.tcpip_state = MQTT_HELPER_TCPIP_DNS_RESOLVED;
                break;
            }
            mqtt_helper_tcpip_check_socket_was_reset();

            if (NET_PRES_SocketReadIsReady(mqtt_data.socket) == 0) {
                if (NET_PRES_SocketWasReset(mqtt_data.socket)) {
                    mqtt_data.tcpip_state = MQTT_HELPER_TCPIP_ERROR;
                }
                break;
            }
            // Read from network RX buffer
            uint16_t res = NET_PRES_SocketRead(mqtt_data.socket, (uint8_t*) buf, sizeof (buf));
            printf("Read: %d\r\n", res);

            // Check to make sure we receive an CONNACK after our CONNECT packet is sent
            unsigned char sessionPresent, connack_rc;
            if (MQTTDeserialize_connack(&sessionPresent, &connack_rc, buf, buflen) != 1 || connack_rc != 0) {
                printf("%s, %d: MQTTDeserialize_connack Error\r\n", __FILE__, __LINE__);
                mqtt_data.tcpip_state = MQTT_HELPER_TCPIP_ERROR;
                break;
            } else {
                // We got a CONNACK, lets set our subscriptions
                printf("We got a CONNACK\r\n");
                mqtt_data.tcpip_state = MQTT_HELPER_TCPIP_MQTT_SUBSCRIBE;
                mqtt_helper_timer_set(&mqtt_data.mqttPingTimer);
            }
            break;
        }

        case MQTT_HELPER_TCPIP_MQTT_SUBSCRIBE:
        {
            mqtt_helper_tcpip_check_socket_was_reset();
            // Serialize an MQTT packet using the variables set at the top
            len = MQTTSerialize_subscribe(buf, buflen, 0, msgid, 1, &topicStringDelta, &req_qos);
            if (NET_PRES_SocketWriteIsReady(mqtt_data.socket, len, 0) == 0) {
                break;
            }
            // Send packet to network TX buffer to be sent
            uint16_t wlen = NET_PRES_SocketWrite(mqtt_data.socket, (uint8_t*) buf, len);
            mqtt_data.tcpip_state = MQTT_HELPER_TCPIP_MQTT_SUBACK;
            mqtt_helper_timer_set(&mqtt_data.mqttMessageTimer);
            break;
        }

        case MQTT_HELPER_TCPIP_MQTT_SUBACK:
        {
            if (mqtt_helper_timer_expired(&mqtt_data.mqttMessageTimer, 20)) {
                BSP_LED_LightShowSet(BSP_LED_SERVER_CONNECT_FAILED);
                mqtt_data.tcpip_state = MQTT_HELPER_TCPIP_ERROR;
                break;
            }

            mqtt_helper_tcpip_check_socket_was_reset();

            if (NET_PRES_SocketReadIsReady(mqtt_data.socket) == 0) {
                if (NET_PRES_SocketWasReset(mqtt_data.socket)) {
                    mqtt_data.tcpip_state = MQTT_HELPER_TCPIP_ERROR;
                }
                break;
            }

            // Read from network RX buffer
            uint16_t res = NET_PRES_SocketRead(mqtt_data.socket, (uint8_t*) buf, sizeof (buf));
            printf("Read SubACK: %d\r\n", res);

            unsigned short submsgid;
            int subcount;
            int granted_qos;
            res = MQTTDeserialize_suback(&submsgid, 1, &subcount, &granted_qos, buf, buflen);
            if (granted_qos != 0) {
                mqtt_data.tcpip_state = MQTT_HELPER_TCPIP_ERROR;
                break;
            } else {
                mqtt_data.tcpip_state = MQTT_HELPER_TCPIP_MQTT_IDLE;
                mqtt_helper_timer_set(&mqtt_data.mqttPingTimer);
            }

            break;
        }
        case MQTT_HELPER_TCPIP_MQTT_IDLE:
        {            
            if (!NET_PRES_SocketIsConnected(mqtt_data.socket)) {
                mqtt_data.tcpip_state = MQTT_HELPER_TCPIP_DNS_RESOLVED;
                break;
            }
            if (mqtt_helper_timer_expired(&mqtt_data.mqttPingTimer, MQTT_PING_REQ)) {
                mqtt_helper_timer_set(&mqtt_data.mqttPingTimer);
                mqtt_data.tcpip_state = MQTT_HELPER_TCPIP_MQTT_PINGREQ;
                break;
            }
            mqtt_helper_tcpip_check_socket_was_reset();

            if (NET_PRES_SocketReadIsReady(mqtt_data.socket) == 0) {
                if (NET_PRES_SocketWasReset(mqtt_data.socket)) {
                    mqtt_data.tcpip_state = MQTT_HELPER_TCPIP_ERROR;
                    break;
                }
                if (!queue_isEmpty(mqtt_data.queue)) {
                    mqtt_data.tcpip_state = MQTT_HELPER_TCPIP_MQTT_PUBLISH;
                    break;
                }
                break;
            }
            uint16_t res = NET_PRES_SocketRead(mqtt_data.socket, (uint8_t*) buf, sizeof (buf));
            printf("Read IDLE: %d\r\n", res);
            
            unsigned char dup, retained;
			unsigned short msgid;
			int payloadlen_in, rc, qos;
			unsigned char* payload_in;
            MQTTString receivedTopic;
            rc = MQTTDeserialize_publish(&dup, &qos, &retained, &msgid, &receivedTopic, &payload_in, &payloadlen_in, buf, buflen);
            BSP_LED_LightShowSet(BSP_LED_RX);
            //Check to see if the topic that came in matches what we subscribed to,
            // if it does (ret = 1) then call our message handler and pass it the payload
            int ret = MQTTPacket_equals(&receivedTopic, topicStringDelta.cstring);
            if(ret){
                printf("handler_topic_delta\r\n");
                handler_topic_delta(payload_in);
            }
            break;
        }
        case MQTT_HELPER_TCPIP_MQTT_PINGREQ:
        {
            // We have no activity so send a PINREQ to keep the connection alive
            mqtt_helper_timer_set(&mqtt_data.mqttPingTimer);
            BSP_LED_LightShowSet(BSP_LED_TX);

            mqtt_helper_tcpip_check_socket_was_reset();

            len = MQTTSerialize_pingreq(buf, buflen);
            if (NET_PRES_SocketWriteIsReady(mqtt_data.socket, len, 0) == 0) {
                break;
            }
            uint16_t wlen = NET_PRES_SocketWrite(mqtt_data.socket, (uint8_t*) buf, len);
            mqtt_data.tcpip_state = MQTT_HELPER_TCPIP_MQTT_PINGRESP;
            break;
        }
        case MQTT_HELPER_TCPIP_MQTT_PINGRESP:
        {
            // Wait for PINGRESP from service
            if (mqtt_helper_timer_expired(&mqtt_data.mqttPingTimer, MQTT_PING_RESP_TIMEOUT)) {
                mqtt_helper_timer_set(&mqtt_data.mqttPingTimer);
                mqtt_data.tcpip_state = MQTT_HELPER_TCPIP_ERROR;
                break;
            }

            mqtt_helper_tcpip_check_socket_was_reset();

            if (NET_PRES_SocketReadIsReady(mqtt_data.socket) == 0) {
                if (NET_PRES_SocketWasReset(mqtt_data.socket)) {
                    mqtt_data.tcpip_state = MQTT_HELPER_TCPIP_ERROR;
                }
                break;
            }
            uint16_t res = NET_PRES_SocketRead(mqtt_data.socket, (uint8_t*) buf, sizeof (buf));
            printf("Read PINGRESP: %d\r\n", res);
            int rc = mqtt_deserialize_pingresp((uint8_t*) buf, sizeof (buf));
            if (rc == 1) {
                mqtt_helper_timer_set(&mqtt_data.mqttPingTimer);
                mqtt_data.tcpip_state = MQTT_HELPER_TCPIP_MQTT_IDLE;
                BSP_LEDOff(BSP_LED_6);
            }

            break;
        }
        case MQTT_HELPER_TCPIP_MQTT_PUBLISH:
        {
            mqtt_helper_tcpip_check_socket_was_reset();
#if 0
            // We build our PUBLISH payload
            char reportedPayload[200];
            JSON_Value *root_value = json_value_init_object();
            JSON_Object *root_object = json_value_get_object(root_value);
            char *serialized_string = NULL;            
            
            if(mqtt_data.queue->message_queue[mqtt_data.queue->front].s1)
            {
                json_object_dotset_string(root_object, "state.reported.button1", (bspData.previousStateS1 ? "up" : "down"));
            }
            if(mqtt_data.queue->message_queue[mqtt_data.queue->front].led1)
            {
                json_object_dotset_string(root_object, "state.reported.led1", (appData.led1 ? "on" : "off"));
            }

            // Serialize this JSON payload into a string
            serialized_string = json_serialize_to_string(root_value);
            sprintf(reportedPayload, serialized_string);
            json_free_serialized_string(serialized_string);
            json_value_free(root_value);
            
            printf("Reported: %s\r\n", reportedPayload);
            
            int  reportedPayload_len = strlen(reportedPayload);
#endif
            int  reportedPayload_len = strlen(mqtt_data.reportedPayload);
            
            // Serialize this JSON payload into an MQTT PUBLISH and send to network TX buffer
            len = MQTTSerialize_publish(buf, buflen, 0, 0, 0, 0, topicStringUpdate, (unsigned char*)mqtt_data.reportedPayload, reportedPayload_len);
            if (NET_PRES_SocketWriteIsReady(mqtt_data.socket, len, 0) == 0) {
                break;
            }
            uint16_t wlen = NET_PRES_SocketWrite(mqtt_data.socket, (uint8_t*) buf, len);
            BSP_LED_LightShowSet(BSP_LED_TX);
            mqtt_helper_timer_set(&mqtt_data.mqttPingTimer);
            dequeue(mqtt_data.queue);
            mqtt_data.tcpip_state = MQTT_HELPER_TCPIP_MQTT_IDLE;
            break;
        }

        case MQTT_HELPER_TCPIP_ERROR:
        {
            // Set error code and close network connection and reset state
            BSP_LED_LightShowSet(BSP_LED_SERVER_CONNECT_FAILED);
            NET_PRES_SocketClose(mqtt_data.socket);
            mqtt_data.tcpip_state = MQTT_HELPER_TCPIP_DNS_RESOLVED;
            break;
        }
    }        
}