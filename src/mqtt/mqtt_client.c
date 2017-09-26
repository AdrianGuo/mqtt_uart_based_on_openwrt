/*
 * MQTT client base on Eclipse Paho C
 *
 * Author: JoStudio
 * Verion: 0.5, 2016.5.26
 */

#include <stdlib.h>
#include <memory.h>
#include <string.h>
#include <errno.h>
#include "mqtt_client.h"
#include "MQTTClientPersistence.h"

/**
 * create a MQTT client
 *
 * @param host host name of server
 * @param port port (default is MQTT_DEFAULT_PORT(1883) )
 *
 * @return client handle if success. return NULL if fail;
 *
 */
mqtt_client * mqtt_new(char * host, int port, char *client_id)	//-这里将根据参数,创建一个客户端,以一个结构体的形式记录下所有内容
{//-这个地方作为一个创建函数应该考虑到了可重入问题,尽量减少了全局函数的使用
	mqtt_client * m;	//-这个应该是客户端的一个实体,记录了整个一个客户端的全部信息
	int rc;

	m = malloc(sizeof(mqtt_client));	//-大量使用临时申请空间,这样是有好处的不固定占用内存空间
	if ( m != NULL) {	//-首先创建了最初的MQTT客户端实体对象
		memset(m , 0, sizeof(mqtt_client));	//-初始化对象变量
		rc = MQTTClient_create(&(m->client), host, client_id, MQTTCLIENT_PERSISTENCE_NONE, NULL);	//-这里创建客户端,并没有套接字的操作,仅仅是内部描述信息等
		if ( rc == MQTTCLIENT_SUCCESS ) {
			m->timeout = MQTT_DEFAULT_TIME_OUT;	//-赋予了一个有效的初始值
			m->received_msg = NULL;
			//mqtt_set_callback_message_arrived(m, m->on_message_arrived);
		} else {
			free(m);
			errno = rc;
			m = NULL;
			return NULL;
		}
	}
	return m;
}


/**
 * Connect to server
 * @param username user name, (set it to NULL if no usernmae)
 * @param password password, (set it to NULL if no password)
 *
 * @return 0 if success, else return error code
 */
int mqtt_connect(mqtt_client * m, char *username, char *password)	//-使用账户登录服务器,创建的客户端已经形成了一个信息体
{
	int rc;
	MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;	//-要做一件事情,就需要一系列参数,来指示选项,这样程序就会简化

	if (!m) return -1;	//-保证客户端成功创建了,才有必要进行连接操作

	conn_opts.keepAliveInterval = 20;	//-针对默认选项进行参数修改,以便实现需要的操作
	conn_opts.cleansession = 1;	//-表示这个链接需要干净断开,没有持久保持特性,在和服务器进行链接的时候会通知服务器

	rc = MQTTClient_connect(m->client, &conn_opts);	//-把前面创建的客户端连接到服务器,使用指定的参数
	return rc;
}


/**
 * Disconnect
 *
 * @param m pointer to MQTT client object
 *
 * @return 0 if success, else return error code
 */
int mqtt_disconnect(mqtt_client * m)
{
	if (!m) return -1;
	return MQTTClient_disconnect(m->client, 10000);
}

/**
 * Return 1 if MQTT client is connected
 *
 * @param m pointer to MQTT client object
 *
 * @return 1 if MQTT client is connected, else return 0
 */
int mqtt_is_connected(mqtt_client *m)	//-现成的判断连接是否存在
{
	if (!m) return 0;
	return MQTTClient_isConnected(m->client);
}


/**
 * When implementing a single-threaded client, call this function periodically to allow
 * processing of message retries and to send MQTT keepalive pings. If the application is
 * calling mqtt_receive() regularly, then it is not necessary to call this function.
 */
void mqtt_yield(void)
{
	MQTTClient_yield();
}

/**
 * Set timeout
 */
int mqtt_set_timeout(mqtt_client *m, int timeout)
{
	if (!m) return -1;
	m->timeout = timeout;
	return MQTT_SUCCESS;
}

//internal callback function
static int internal_callback_message_arrived(void *context, char *topicName, int topicLen, MQTTClient_message *message)
{
	mqtt_client *m;

	m = (mqtt_client *)context;
	if (!m) return -1;
	if (m->on_message_arrived == NULL) return -1;

	if ( topicName[topicLen] != 0 )
		topicName[topicLen] = 0;

	return m->on_message_arrived(m, topicName, message->payload, message->payloadlen);
}

//internal callback function
static void internal_callback_connectionLost(void *context, char *cause)
{
	return;
}

//internal callback function
void internal_callback_delivery_complete(void *context, MQTTClient_deliveryToken dt)
{
	return;
}


/**
 * set callback function when message arrived
 */
int mqtt_set_callback_message_arrived(mqtt_client *m, CALLBACK_MESSAGE_ARRIVED * function)
{
	int ret;

	if (!m) return -1;
	m->on_message_arrived = function;
	ret = MQTTClient_setCallbacks(m->client, m,
		internal_callback_connectionLost,    //MQTTClient_connectionLost * 	cl,
		internal_callback_message_arrived,   //MQTTClient_messageArrived * 	ma,
		internal_callback_delivery_complete  //MQTTClient_deliveryComplete * 	dc
		);
	return ret;
}

/**
 * Subscribe a topic
 *
 * @param m pointer to MQTT client object
 * @param Qos quality of service, QOS_AT_MOST_ONCE or QOS_AT_LEAST_ONCE or QOS_EXACTLY_ONCE
 *
 * @return 0 if success, else return error code
 */
int mqtt_subscribe(mqtt_client *m, char *topic, int Qos)
{
	if (!m) return -1;
	return MQTTClient_subscribe	(m->client, topic, Qos);
}

/**
 * Unsubscribe a topic
 *
 * @param m pointer to MQTT client object
 *
 * @return 0 if success, else return error code
 */
int mqtt_unsubscribe(mqtt_client *m, char *topic)	//-客户端取消订阅
{
	if (!m) return -1;
	return MQTTClient_unsubscribe	(m->client, topic);
}


/**
 * Delete MQTT client object
 */
int mqtt_delete(mqtt_client *m)
{
	if (!m) return -1;
	MQTTClient_destroy(&(m->client));
	return 0;
}


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
int mqtt_publish_data(mqtt_client * m, char *topic, void *data, int length, int Qos)
{
	MQTTClient_message pubmsg = MQTTClient_message_initializer;	//-记录一条消息的结构体
	MQTTClient_deliveryToken token = -1;	//-投递 标志,通过这个标志在一个链表中检索
	int rc;

	if (!m) return -1;	//-有必要进行下面操作的前提就是有这样一个实体

	pubmsg.payload = data;
	pubmsg.payloadlen = length;
	pubmsg.qos = Qos;
	pubmsg.retained = 0;	//-消息是否保持标志
	//-上面进行了格式转化
	rc = MQTTClient_publishMessage(m->client, topic, &pubmsg, &token);
	if ( rc != MQTTCLIENT_SUCCESS )
		return rc;

	if ( m->timeout > 0 ) {
		rc = MQTTClient_waitForCompletion(m->client, token, m->timeout);
		if ( rc != MQTTCLIENT_SUCCESS )
			return rc;
		else
			return token;
	}

	return token;
}


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
int mqtt_publish(mqtt_client * m, char *topic, char *message, int Qos)	//-这里发部消息,都使用一个函数进行转接,其实完全没有这个必要,所以你要考虑考虑人家的用意
{
	return mqtt_publish_data(m, topic, message, strlen(message), Qos);
}


static void mqtt_clear_received(mqtt_client *m)	//-把客户端的标志位清零什么意思
{
	if (!m) return;

	//MQTTClient_freeMessage();

	if ( m->received_msg != NULL ) {
		//free(m->received_msg->payload);
		//free(m->received_msg);
		m->received_msg = NULL;
		m->received_message = NULL;
		m->received_message_len = 0;
		m->received_message_id = 0;
	}

	/*
	if ( m->received_topic != NULL) {
		free(m->received_topic);
		m->received_topic = NULL;
		m->received_topic_len = 0;
	}
	*/

}

/**
 * Receive message
 *
 * @param m pointer to MQTT client object
 * @param topic pointer to topic
 * @param message pointer to message
 *
 * @return 0 if message is recieved.
 */
int mqtt_receive(mqtt_client *m, unsigned long timeout)	//-接收特定客户端上的消息
{
	int rc;

	if (!m) return -1;

	mqtt_clear_received(m);

	rc = MQTTClient_receive(m->client, &(m->received_topic),
			&(m->received_topic_len), &(m->received_msg), timeout);

	if ( rc == MQTTCLIENT_SUCCESS || rc == MQTTCLIENT_TOPICNAME_TRUNCATED ) {
		if ( m->received_msg == NULL) {
			rc = -1;
		} else {
			rc = MQTTCLIENT_SUCCESS;	//-从下面看好像一次客户端就记录一个消息
			if ( m->received_topic[m->received_topic_len] != 0 )
				m->received_topic[m->received_topic_len] = 0;
			m->received_message = m->received_msg->payload;
			m->received_message_len = m->received_msg->payloadlen;
			m->received_message_id = m->received_msg->msgid;
		}
	}

	return rc;
}


/**
 * Sleep a while
 *
 * @param milliseconds
 *
 * @return none
 */
void mqtt_sleep(int milliseconds)
{
	MQTTClient_sleep(milliseconds);
}

