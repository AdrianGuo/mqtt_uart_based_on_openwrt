/*
 * MQTT subscribe
 *
 */


#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include "mqtt/mqtt_client.h"//������Ҷ�mqtt_client��װ���ͷ�ļ�

int running = 1;

void stop_running(int sig)
{
	signal(SIGINT, NULL);
	running = 0;
}


int mqtt_subscribe_sub(int argc, char ** argv) {

	mqtt_client *m; //mqtt_client ����ָ��
	int ret; //����ֵ
	char *host = "messagesight.demos.ibm.com:1883";//���Է�����
	char *topic = "test_topic"; //����
	char *client_id = "clientid33883";//�ͻ���ID�� �Բ��Է��������������д
	char *username = NULL;//�û�����������֤��ݡ��Բ��Է��������ޡ�
	char *password = NULL;//���룬������֤��ݡ��Բ��Է��������ޡ�
	int Qos; //Quality of Service

	//create new mqtt client object
	m = mqtt_new(host, MQTT_PORT, client_id); //��������MQTT_PORT = 1883
	if ( m == NULL ) {
		printf("mqtt client create failure, return  code = %d\n", errno);
		return 1;
	} else {
		printf("mqtt client created\n");
	}

	//connect to server
	ret = mqtt_connect(m, username, password); //���ӷ�����
	if (ret != MQTT_SUCCESS ) {
		printf("mqtt client connect failure, return code = %d\n", ret);
		return 1;
	} else {
		printf("mqtt client connect\n");
	}

	//subscribe
	Qos = QOS_EXACTLY_ONCE;
	ret = mqtt_subscribe(m, topic, Qos);//������Ϣ
	printf("mqtt client subscribe %s,  return code = %d\n", topic, ret);

	signal(SIGINT, stop_running);	//?�����ź���
	signal(SIGTERM, stop_running);	//-Ӧ�������ô����ź���

	printf("wait for message of topic: %s ...\n", topic);

	//-��������Կ���������ϢҲ��ͨ����ѯ�ķ�ʽ��,�����жϴ���
	//loop: waiting message
	while (running) {
		int timeout = 200;
		if ( mqtt_receive(m, timeout) == MQTT_SUCCESS ) { //recieve message��������Ϣ
			printf("received Topic=%s, Message=%s\n", m->received_topic, m->received_message);
		}
		mqtt_sleep(200); //sleep a while
	}

	mqtt_disconnect(m); //disconnect
	printf("mqtt client disconnect");
	mqtt_delete(m);  //delete mqtt client object
	return 0;
}
