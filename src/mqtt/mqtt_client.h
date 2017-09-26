/*
 * MQTT client base on Eclipse Paho C
 *
 * Author: JoStudio
 * Verion: 0.5, 2016.5.26
 */

#ifndef __MQTT_CLIENT_H__
#define __MQTT_CLIENT_H__

#include "MQTTClient.h"

#ifdef __cplusplus
extern "C" {
#endif



/**
 * Return code: No error. Indicates successful completion of an MQTT client operation
 */
#define MQTT_SUCCESS   0

/**
 * Return code:  A generic error code indicating the failure of an MQTT client operation
 */
#define MQTT_FAILURE  -1

/**
 * Return code: The client is disconnected.
 */
#define MQTT_DISCONNECTED -3

/**
 * Return code: The maximum number of messages allowed to be simultaneously
 * in-flight has been reached.
 */
#define MQTT_MAX_MESSAGES_INFLIGHT -4

/**
 * Return code: An invalid UTF-8 string has been detected.
 */
#define MQTT_BAD_UTF8_STRING -5

/**
 * Return code: A NULL parameter has been supplied when this is invalid.
 */
#define MQTT_NULL_PARAMETER -6

/**
 * Return code: The topic has been truncated (the topic string includes
 * embedded NULL characters). String functions will not access the full topic.
 * Use the topic length value to access the full topic.
 */
#define MQTT_TOPICNAME_TRUNCATED -7

/**
 * Return code: A structure parameter does not have the correct eyecatcher
 * and version number.
 */
#define MQTT_BAD_STRUCTURE -8

/**
 * Return code: A QoS value that falls outside of the acceptable range (0,1,2)
 */
#define MQTT_BAD_QOS -9



/**
 * Quality of service : At most once
 */
#define QOS_AT_MOST_ONCE  0

/**
 * Quality of service : At least once
 */
#define QOS_AT_LEAST_ONCE 1

/**
 * Quality of service : Exactly once
 */
#define QOS_EXACTLY_ONCE  2

/**
 * Default port of MQTT server
 */
#define MQTT_PORT 1883

/**
 * Default time out of MQTT client
 */
#define MQTT_DEFAULT_TIME_OUT  3000


/* MQTT client object*/
typedef struct _mqtt_client mqtt_client;

/**
 * prototype of callback function when message arrived
 */
typedef int CALLBACK_MESSAGE_ARRIVED(mqtt_client *m, char *topic, char *data, int length);

/* structure of MQTT client object*/
struct _mqtt_client {
	MQTTClient client;
	//int Qos;     //Quality of service
	int timeout; //time out (milliseconds)
	CALLBACK_MESSAGE_ARRIVED *on_message_arrived;

	int    received_message_id;
	char * received_topic;
	char * received_message;
	int    received_message_len;
	int    received_topic_len;

	MQTTClient_message * received_msg;
};//-一个程序是可以创建几个客户端的,一个客户端又是可以存在几个连接的,一个连接上是有各种报文的


/**
 * Create a new MQTT client object
 *
 * @param host host name of server
 * @param port port (default is MQTT_DEFAULT_PORT(1883) )
 *
 * @return pointer to MQTT client object if success. return NULL if fail;
 *
 */
mqtt_client * mqtt_new(char * host, int port, char *client_id);

/**
 * Delete MQTT client object
 */
int mqtt_delete(mqtt_client *m);


/**
 * Connect to server
 *
 * @param m pointer to MQTT client object
 * @param username user name, (set it to NULL if no usernmae)
 * @param password password, (set it to NULL if no password)
 *
 * @return 0 if success, else return error code
 */
int mqtt_connect(mqtt_client * m, char *username, char *password);

/**
 * Disconnect
 *
 * @param m pointer to MQTT client object
 *
 * @return 0 if success, else return error code
 */
int mqtt_disconnect(mqtt_client * m);

/**
 * Return 1 if MQTT client is connected
 *
 * @param m pointer to MQTT client object
 *
 * @return 1 if MQTT client is connected, else return 0
 */
int mqtt_is_connected(mqtt_client *m);


/**
 * When implementing a single-threaded client, call this function periodically to allow
 * processing of message retries and to send MQTT keepalive pings. If the application is
 * calling mqtt_receive() regularly, then it is not necessary to call this function.
 */
void mqtt_yield(void);


/**
 * Set timeout
 *
 * @param m pointer to MQTT client object
 *
 * @return 0 if success, else return error code
 */
int mqtt_set_timeout(mqtt_client *m, int timeout);

/**
 * set callback function when message arrived
 */
int mqtt_set_callback_message_arrived(mqtt_client *m, CALLBACK_MESSAGE_ARRIVED * function);

/**
 * Subscribe a topic
 *
 * @param m pointer to MQTT client object
 * @param Qos quality of service, QOS_AT_MOST_ONCE or QOS_AT_LEAST_ONCE or QOS_EXACTLY_ONCE
 *
 * @return 0 if success, else return error code
 */
int mqtt_subscribe(mqtt_client *m, char *topic, int Qos);


/**
 * Unsubscribe a topic
 *
 * @param m pointer to MQTT client object
 *
 * @return 0 if success, else return error code
 */
int mqtt_unsubscribe(mqtt_client *m, char *topic);




/**
 * Publish a data
 *
 * @param m pointer to MQTT client object
 * @param topic topic of message
 * @param data content of message
 * @param length length of data
 * @param Qos quality of service, QOS_AT_MOST_ONCE or QOS_AT_LEAST_ONCE or QOS_EXACTLY_ONCE
 *
 * @return positive integer of message token if success. return negative integer of error code if fail
 *    Token is a value representing an MQTT message
 */
int mqtt_publish_data(mqtt_client * m, char *topic, void *data, int length, int Qos);


/**
 * Publish a text message
 *
 * @param m pointer to MQTT client object
 * @param topic topic of message
 * @param message message
 * @param Qos quality of service, QOS_AT_MOST_ONCE or QOS_AT_LEAST_ONCE or QOS_EXACTLY_ONCE
 *
 * @return positive integer of message token if success. return negative integer of error code if fail
 *    Token is a value representing an MQTT message
 */
int mqtt_publish(mqtt_client * m, char *topic, char *message, int Qos);



/**
 * Receive message
 *
 * @param m pointer to MQTT client object
 *
 * @return 0 if message is recieved.
 */
int mqtt_receive(mqtt_client *m, unsigned long timeout);


/**
 * Sleep a while
 *
 * @param milliseconds
 *
 * @return none
 */
void mqtt_sleep(int milliseconds);


#ifdef __cplusplus
}
#endif

#endif /* __MQTT_CLIENT_H__ */
