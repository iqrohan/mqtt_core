
#include "app.h"
#include "mqtt_helper.h"
#include "queue.h"

// *****************************************************************************
// *****************************************************************************
// Section: Global Data Definitions
// *****************************************************************************
// *****************************************************************************
APP_DATA appData;

// *****************************************************************************
// *****************************************************************************
// Section: Application Callback Functions
// *****************************************************************************
// *****************************************************************************


// *****************************************************************************
// *****************************************************************************
// Section: Application Local Functions
// *****************************************************************************
// *****************************************************************************
void set_publish_flag(bool val)
{
    appData.pubFlag = val;
}
bool get_publish_flag()
{
    return appData.pubFlag;
}

void handler_topic_delta(unsigned char * payload)       // Callback from MQTT Helper
{
    JSON_Value *root_value = json_parse_string(payload);
    if (json_value_get_type(root_value) != JSONObject)
        return;
    JSON_Object * tObject = json_value_get_object(root_value);
    if (json_object_dotget_string(tObject, "state.led1") != NULL) {
        if (strcmp(json_object_dotget_string(tObject, "state.led1"), "on") == 0) {
            BSP_LEDOn(BSP_LED_1);
            appData.led1 = true;
            appData.publishObject.led1 = true;
        } else if (strcmp(json_object_dotget_string(tObject, "state.led1"), "off") == 0) {
            BSP_LEDOff(BSP_LED_1);
            appData.led1 = false;
            appData.publishObject.led1 = true;
        }
        set_publish_flag(true);
    }
    json_value_free(root_value);
}

int create_publish_payload(MY_QUEUE *que, char *reportedPayload)
{
    // We build our PUBLISH payload
    //char reportedPayload[200];
    JSON_Value *root_value = json_value_init_object();
    JSON_Object *root_object = json_value_get_object(root_value);
    char *serialized_string = NULL;            

    if(que->message_queue[que->front].s1)
    {
        json_object_dotset_string(root_object, "state.reported.button1", (bspData.previousStateS1 ? "up" : "down"));
    }
    if(que->message_queue[que->front].led1)
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
    return reportedPayload_len;
}
// *****************************************************************************
// *****************************************************************************
// Section: Application Initialization and State Machine Functions
// *****************************************************************************
// *****************************************************************************

void APP_Initialize ( void )
{
    /* Place the App state machine in its initial state. */
    appData.state = APP_STATE_INIT;

}

void APP_Tasks ( void )
{ 
    set_publish_flag(false);
    
    if (BSP_SWITCH_SwitchTest(BSP_SWITCH_1) != bspData.previousStateS1) {
        set_publish_flag(true);
        appData.publishObject.s1 = true;
        bspData.previousStateS1 = BSP_SWITCH_SwitchTest(BSP_SWITCH_1);
    }
    
    if (get_publish_flag() == true) {
        // TODO: check which data should be published and then enqueue
        enqueue((&appData.myQueue), appData.publishObject);
        create_publish_payload(&appData.myQueue, appData.reported_payload);
        set_publish_flag(false);
    }
    
    switch ( appData.state )
    {
        case APP_STATE_INIT:
        {
            mqtt_helper_payload_info(&appData.myQueue, appData.reported_payload);
            mqtt_helper_thing_info(appData.thing_name, appData.update_string, appData.delta_string);
            mqtt_helper_init();
            break;
        }

        case APP_STATE_SERVICE_TASKS:
        {
        
            break;
        }

        default:
        {
            /* TODO: Handle error in application's state machine. */
            break;
        }
    }
}

 

/*******************************************************************************
 End of File
 */
