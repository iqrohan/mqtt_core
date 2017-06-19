
#ifndef _APP_H
#define _APP_H

// *****************************************************************************
// *****************************************************************************
// Section: Included Files
// *****************************************************************************
// *****************************************************************************

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include "system_config.h"
#include "system_definitions.h"

// DOM-IGNORE-BEGIN
#ifdef __cplusplus  // Provide C++ Compatibility

extern "C" {

#endif
// DOM-IGNORE-END 

typedef enum
{
	/* Application's state machine's initial state. */
	APP_STATE_INIT=0,
	APP_STATE_SERVICE_TASKS,

	/* TODO: Define states used by the application state machine. */

} APP_STATES;

typedef struct
{
    /* The application's current state */
    APP_STATES state;

    // Application hosts
    char aws_iot_host[256];
    // Cert and Key Locations
    __attribute__((aligned(4))) unsigned char clientCert[2048];
    __attribute__((aligned(4))) unsigned char clientKey[2048];
    
    char *thing_name, *update_string, *delta_string;    // Thing details
    MY_QUEUE_OBJECT publishObject;    
    bool pubFlag;
    MY_QUEUE myQueue;               // Variable's address shared with MQTT Helper
    char reported_payload[200];     // Variable's address shared with MQTT Helper
    
    bool led1;

} APP_DATA;

void set_publish_flag(bool val);
bool get_publish_flag();
void handler_topic_delta(unsigned char * payload);
int create_publish_payload(MY_QUEUE *que, char *reportedPayload);
void APP_Initialize ( void );
void APP_Tasks( void );


#endif /* _APP_H */

//DOM-IGNORE-BEGIN
#ifdef __cplusplus
}
#endif
//DOM-IGNORE-END

/*******************************************************************************
 End of File
 */

