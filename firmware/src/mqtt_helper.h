
#ifndef _MQTT_HELPER_H
#define _MQTT_HELPER_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

#define MQTT_KEEP_ALIVE 60
#define MQTT_PING_REQ 45
#define MQTT_PING_RESP_TIMEOUT 10
#define MQTT_PORT 8883

typedef enum {
    MQTT_HELPER_TCPIP_WAIT_INIT,
    MQTT_HELPER_TCPIP_STORE_HOSTNAME,
    MQTT_HELPER_TCPIP_RESOLVE_DNS,
    MQTT_HELPER_TCPIP_WAIT_ON_DNS,
    MQTT_HELPER_TCPIP_DNS_RESOLVED,
    MQTT_HELPER_TCPIP_WAIT_SOCKET,
    MQTT_HELPER_TCPIP_MQTT_CONNECT,
    MQTT_HELPER_TCPIP_MQTT_CONNACK,
    MQTT_HELPER_TCPIP_MQTT_SUBSCRIBE,
    MQTT_HELPER_TCPIP_MQTT_SUBACK,
    MQTT_HELPER_TCPIP_MQTT_IDLE,
    MQTT_HELPER_TCPIP_MQTT_PINGREQ,
    MQTT_HELPER_TCPIP_MQTT_PINGRESP,
    MQTT_HELPER_TCPIP_MQTT_PUBLISH,
    MQTT_HELPER_TCPIP_ERROR,
} MQTT_HELPER_TCPIP_STATES;

typedef struct {
    MQTT_HELPER_TCPIP_STATES tcpip_state;
    IP_MULTI_ADDRESS aws_iot_ipv4;
    
    MQTTPacket_connectData data;
    MQTTString topicStringUpdate;
    MQTTString topicStringDelta;
    
    NET_PRES_SKT_HANDLE_T socket;
    
    MY_QUEUE *queue;
    char *reportedPayload;
    
    TCP_PORT port;
    NET_PRES_SKT_ERROR_T error;

    uint32_t tcpipTimeout;
    uint32_t mqttMessageTimer;
    uint32_t mqttPingTimer;    
}MQTT_HELPER_DATA;


bool mqtt_helper_timer_expired(uint32_t * timer, uint32_t seconds);
bool mqtt_helper_timer_set(uint32_t * timer);
void mqtt_helper_tcpip_check_socket_was_reset(void);
int mqtt_deserialize_pingresp(unsigned char* buf, int buflen);
void mqtt_helper_payload_info(MY_QUEUE *que_addr, char *payload_addr);
void mqtt_helper_thing_info(char *thing_name, char *update_string, char *delta_string);
void mqtt_helper_init();
void mqtt_helper_tasks();

#endif  /* _MQTT_HELPER_H */