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
mqtt_client * mqtt_new(char * host, int port, char *client_id)	//-���ｫ���ݲ���,����һ���ͻ���,��һ���ṹ�����ʽ��¼����������
{//-����ط���Ϊһ����������Ӧ�ÿ��ǵ��˿���������,����������ȫ�ֺ�����ʹ��
	mqtt_client * m;	//-���Ӧ���ǿͻ��˵�һ��ʵ��,��¼������һ���ͻ��˵�ȫ����Ϣ
	int rc;

	m = malloc(sizeof(mqtt_client));	//-����ʹ����ʱ����ռ�,�������кô��Ĳ��̶�ռ���ڴ�ռ�
	if ( m != NULL) {	//-���ȴ����������MQTT�ͻ���ʵ�����
		memset(m , 0, sizeof(mqtt_client));	//-��ʼ���������
		rc = MQTTClient_create(&(m->client), host, client_id, MQTTCLIENT_PERSISTENCE_NONE, NULL);	//-���ﴴ���ͻ���,��û���׽��ֵĲ���,�������ڲ�������Ϣ��
		if ( rc == MQTTCLIENT_SUCCESS ) {
			m->timeout = MQTT_DEFAULT_TIME_OUT;	//-������һ����Ч�ĳ�ʼֵ
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
int mqtt_connect(mqtt_client * m, char *username, char *password)	//-ʹ���˻���¼������,�����Ŀͻ����Ѿ��γ���һ����Ϣ��
{
	int rc;
	MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;	//-Ҫ��һ������,����Ҫһϵ�в���,��ָʾѡ��,��������ͻ��

	if (!m) return -1;	//-��֤�ͻ��˳ɹ�������,���б�Ҫ�������Ӳ���

	conn_opts.keepAliveInterval = 20;	//-���Ĭ��ѡ����в����޸�,�Ա�ʵ����Ҫ�Ĳ���
	conn_opts.cleansession = 1;	//-��ʾ���������Ҫ�ɾ��Ͽ�,û�г־ñ�������,�ںͷ������������ӵ�ʱ���֪ͨ������

	rc = MQTTClient_connect(m->client, &conn_opts);	//-��ǰ�洴���Ŀͻ������ӵ�������,ʹ��ָ���Ĳ���
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
int mqtt_is_connected(mqtt_client *m)	//-�ֳɵ��ж������Ƿ����
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
int mqtt_unsubscribe(mqtt_client *m, char *topic)	//-�ͻ���ȡ������
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
	MQTTClient_message pubmsg = MQTTClient_message_initializer;	//-��¼һ����Ϣ�Ľṹ��
	MQTTClient_deliveryToken token = -1;	//-Ͷ�� ��־,ͨ�������־��һ�������м���
	int rc;

	if (!m) return -1;	//-�б�Ҫ�������������ǰ�����������һ��ʵ��

	pubmsg.payload = data;
	pubmsg.payloadlen = length;
	pubmsg.qos = Qos;
	pubmsg.retained = 0;	//-��Ϣ�Ƿ񱣳ֱ�־
	//-��������˸�ʽת��
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
int mqtt_publish(mqtt_client * m, char *topic, char *message, int Qos)	//-���﷢����Ϣ,��ʹ��һ����������ת��,��ʵ��ȫû�������Ҫ,������Ҫ���ǿ����˼ҵ�����
{
	return mqtt_publish_data(m, topic, message, strlen(message), Qos);
}


static void mqtt_clear_received(mqtt_client *m)	//-�ѿͻ��˵ı�־λ����ʲô��˼
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
int mqtt_receive(mqtt_client *m, unsigned long timeout)	//-�����ض��ͻ����ϵ���Ϣ
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
			rc = MQTTCLIENT_SUCCESS;	//-�����濴����һ�οͻ��˾ͼ�¼һ����Ϣ
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

