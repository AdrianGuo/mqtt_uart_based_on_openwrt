/*
 * MQTT subscribe
 *
 */


#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include "mqtt/mqtt_client.h"//这个是我对mqtt_client封装后的头文件

int running = 1;

void stop_running(int sig)
{
	signal(SIGINT, NULL);
	running = 0;
}


int mqtt_subscribe_sub(int argc, char ** argv) {

	mqtt_client *m; //mqtt_client 对象指针
	int ret; //返回值
	char *host = "messagesight.demos.ibm.com:1883";//测试服务器
	char *topic = "test_topic"; //主题
	char *client_id = "clientid33883";//客户端ID； 对测试服务器，可以随便写
	char *username = NULL;//用户名，用于验证身份。对测试服务器，无。
	char *password = NULL;//密码，用于验证身份。对测试服务器，无。
	int Qos; //Quality of Service

	//create new mqtt client object
	m = mqtt_new(host, MQTT_PORT, client_id); //创建对象，MQTT_PORT = 1883
	if ( m == NULL ) {
		printf("mqtt client create failure, return  code = %d\n", errno);
		return 1;
	} else {
		printf("mqtt client created\n");
	}

	//connect to server
	ret = mqtt_connect(m, username, password); //连接服务器
	if (ret != MQTT_SUCCESS ) {
		printf("mqtt client connect failure, return code = %d\n", ret);
		return 1;
	} else {
		printf("mqtt client connect\n");
	}

	//subscribe
	Qos = QOS_EXACTLY_ONCE;
	ret = mqtt_subscribe(m, topic, Qos);//订阅消息
	printf("mqtt client subscribe %s,  return code = %d\n", topic, ret);

	signal(SIGINT, stop_running);	//?发送信号量
	signal(SIGTERM, stop_running);	//-应该是设置处理信号量

	printf("wait for message of topic: %s ...\n", topic);

	//-从下面可以看出接收消息也是通过查询的方式的,不是中断处理
	//loop: waiting message
	while (running) {
		int timeout = 200;
		if ( mqtt_receive(m, timeout) == MQTT_SUCCESS ) { //recieve message，接收消息
			printf("received Topic=%s, Message=%s\n", m->received_topic, m->received_message);
		}
		mqtt_sleep(200); //sleep a while
	}

	mqtt_disconnect(m); //disconnect
	printf("mqtt client disconnect");
	mqtt_delete(m);  //delete mqtt client object
	return 0;
}
