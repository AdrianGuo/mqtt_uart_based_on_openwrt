/*******************************************************************************
 * Copyright (c) 2009, 2013 IBM Corp.
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * and Eclipse Distribution License v1.0 which accompany this distribution. 
 *
 * The Eclipse Public License is available at 
 *    http://www.eclipse.org/legal/epl-v10.html
 * and the Eclipse Distribution License is available at 
 *   http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * Contributors:
 *    Ian Craggs - initial API and implementation and/or initial documentation
 *    Ian Craggs, Allan Stockdill-Mander - SSL updates
 *    Ian Craggs - fix for bug 413429 - connectionLost not called
 *    Ian Craggs - fix for bug 421103 - trying to write to same socket, in retry
 *    Rong Xiang, Ian Craggs - C++ compatibility
 *    Ian Craggs - turn off DUP flag for PUBREL - MQTT 3.1.1
 *******************************************************************************/

/**
 * @file
 * \brief Functions dealing with the MQTT protocol exchanges
 *
 * Some other related functions are in the MQTTProtocolOut module
 * */


#include <stdlib.h>

#include "MQTTProtocolClient.h"
#if !defined(NO_PERSISTENCE)
#include "MQTTPersistence.h"
#endif
#include "SocketBuffer.h"
#include "StackTrace.h"
#include "Heap.h"

#if !defined(min)
#define min(A,B) ( (A) < (B) ? (A):(B))
#endif

void Protocol_processPublication(Publish* publish, Clients* client);
void MQTTProtocol_closeSession(Clients* client, int sendwill);

extern MQTTProtocol state;
extern ClientStates* bstate;

/**
 * List callback function for comparing Message structures by message id
 * @param a first integer value
 * @param b second integer value
 * @return boolean indicating whether a and b are equal
 */
int messageIDCompare(void* a, void* b)	//-对这样的东西进行比较,
{
	Messages* msg = (Messages*)a;
	return msg->msgid == *(int*)b;
}


/**
 * Assign a new message id for a client.  Make sure it isn't already being used and does
 * not exceed the maximum.
 * @param client a client structure
 * @return the next message id to use, or 0 if none available
 */
int MQTTProtocol_assignMsgId(Clients* client)	//-为客户端分配一个新的信息ID,这个原则上序列增加,但不是硬性规定,不重复就行
{
	int start_msgid = client->msgID;
	int msgid = start_msgid;

	FUNC_ENTRY;
	msgid = (msgid == MAX_MSG_ID) ? 1 : msgid + 1;	//-序列加一
	while (ListFindItem(client->outboundMsgs, &msgid, messageIDCompare) != NULL)	//-又得查列表
	{//-如果没有重复的话就认为合适了
		msgid = (msgid == MAX_MSG_ID) ? 1 : msgid + 1;
		if (msgid == start_msgid) 
		{ /* we've tried them all - none free */
			msgid = 0;
			break;
		}
	}
	if (msgid != 0)
		client->msgID = msgid;	//-记录的是信息ID,这个是唯一的
	FUNC_EXIT_RC(msgid);
	return msgid;
}


void MQTTProtocol_storeQoS0(Clients* pubclient, Publish* publish)	//-这里的发布可能比较简单仅仅送出去就行,不需要复杂的处理:至多一次
{
	int len;
	pending_write* pw = NULL;

	FUNC_ENTRY;
	/* store the publication until the write is finished */
	pw = malloc(sizeof(pending_write));
	Log(TRACE_MIN, 12, NULL);
	pw->p = MQTTProtocol_storePublication(publish, &len);
	pw->socket = pubclient->net.socket;
	ListAppend(&(state.pending_writes), pw, sizeof(pending_write)+len);
	/* we don't copy QoS 0 messages unless we have to, so now we have to tell the socket buffer where
	the saved copy is */
	if (SocketBuffer_updateWrite(pw->socket, pw->p->topic, pw->p->payload) == NULL)
		Log(LOG_SEVERE, 0, "Error updating write");
	FUNC_EXIT;
}


/**
 * Utility function to start a new publish exchange.
 * @param pubclient the client to send the publication to
 * @param publish the publication data
 * @param qos the MQTT QoS to use
 * @param retained boolean - whether to set the MQTT retained flag
 * @return the completion code
 */
int MQTTProtocol_startPublishCommon(Clients* pubclient, Publish* publish, int qos, int retained)	//-通用处理
{
	int rc = TCPSOCKET_COMPLETE;

	FUNC_ENTRY;
	rc = MQTTPacket_send_publish(publish, 0, qos, retained, &pubclient->net, pubclient->clientID);
	if (qos == 0 && rc == TCPSOCKET_INTERRUPTED)
		MQTTProtocol_storeQoS0(pubclient, publish);
	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 * Start a new publish exchange.  Store any state necessary and try to send the packet
 * @param pubclient the client to send the publication to
 * @param publish the publication data
 * @param qos the MQTT QoS to use
 * @param retained boolean - whether to set the MQTT retained flag
 * @param mm - pointer to the message to send
 * @return the completion code
 */
int MQTTProtocol_startPublish(Clients* pubclient, Publish* publish, int qos, int retained, Messages** mm)	//-开始启动发布
{
	Publish p = *publish;
	int rc = 0;

	FUNC_ENTRY;
	if (qos > 0)	//-0 至多一次,1 至少一次,2 只有一次
	{
		*mm = MQTTProtocol_createMessage(publish, mm, qos, retained);	//-首先创建消息
		ListAppend(pubclient->outboundMsgs, *mm, (*mm)->len);	//-然后加入列表管理
		/* we change these pointers to the saved message location just in case the packet could not be written
		entirely; the socket buffer will use these locations to finish writing the packet */
		p.payload = (*mm)->publish->payload;
		p.topic = (*mm)->publish->topic;
	}
	rc = MQTTProtocol_startPublishCommon(pubclient, &p, qos, retained);
	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 * Copy and store message data for retries
 * @param publish the publication data
 * @param mm - pointer to the message data to store
 * @param qos the MQTT QoS to use
 * @param retained boolean - whether to set the MQTT retained flag
 * @return pointer to the message data stored
 */
Messages* MQTTProtocol_createMessage(Publish* publish, Messages **mm, int qos, int retained)	//-创建了一个信息
{
	Messages* m = malloc(sizeof(Messages));

	FUNC_ENTRY;
	m->len = sizeof(Messages);
	if (*mm == NULL || (*mm)->publish == NULL)
	{
		int len1;
		*mm = m;
		m->publish = MQTTProtocol_storePublication(publish, &len1);
		m->len += len1;
	}
	else
	{
		++(((*mm)->publish)->refcount);
		m->publish = (*mm)->publish;
	}
	m->msgid = publish->msgId;
	m->qos = qos;
	m->retain = retained;
	time(&(m->lastTouch));
	if (qos == 2)
		m->nextMessageType = PUBREC;
	FUNC_EXIT;
	return m;
}


/**
 * Store message data for possible retry
 * @param publish the publication data
 * @param len returned length of the data stored
 * @return the publication stored
 */
Publications* MQTTProtocol_storePublication(Publish* publish, int* len)	//-对信息进行存储,以便可能的重发
{
	Publications* p = malloc(sizeof(Publications));	//-开辟一个空间存储发布的消息

	FUNC_ENTRY;
	p->refcount = 1;

	*len = strlen(publish->topic)+1;
	if (Heap_findItem(publish->topic))
		p->topic = publish->topic;	//-从一个完整的发布帧中提取需要的信息到一个存储的发布帧中
	else
	{
		p->topic = malloc(*len);
		strcpy(p->topic, publish->topic);
	}
	*len += sizeof(Publications);

	p->topiclen = publish->topiclen;
	p->payloadlen = publish->payloadlen;
	p->payload = malloc(publish->payloadlen);
	memcpy(p->payload, publish->payload, p->payloadlen);
	*len += publish->payloadlen;

	ListAppend(&(state.publications), p, *len);	//-在存储队列中增加一个
	FUNC_EXIT;
	return p;
}

/**
 * Remove stored message data.  Opposite of storePublication
 * @param p stored publication to remove
 */
void MQTTProtocol_removePublication(Publications* p)
{
	FUNC_ENTRY;
	if (--(p->refcount) == 0)
	{
		free(p->payload);
		free(p->topic);
		ListRemove(&(state.publications), p);
	}
	FUNC_EXIT;
}

/*
PUBREC消息是用来响应QoS级别为2的PUBLISH消息的。这是QoS级别为2的protocol flow（ 协议流）
的第二个消息。 PUBREC消息由服务器发送以响应发布客户端的PUBLISH消息，或者由订阅者发送以响应
来自服务器的PUBLISH消息。
*/
/**
 * Process an incoming publish packet for a socket
 * @param pack pointer to the publish packet
 * @param sock the socket on which the packet was received
 * @return completion code
 */
int MQTTProtocol_handlePublishes(void* pack, int sock)	//-这个就是服务器发送过来的消息
{
	Publish* publish = (Publish*)pack;
	Clients* client = NULL;
	char* clientid = NULL;
	int rc = TCPSOCKET_COMPLETE;

	FUNC_ENTRY;
	client = (Clients*)(ListFindItem(bstate->clients, &sock, clientSocketCompare)->content);	//-通过标志位在链表中检索出内容
	clientid = client->clientID;
	Log(LOG_PROTOCOL, 11, NULL, sock, clientid, publish->msgId, publish->header.bits.qos,
					publish->header.bits.retain, min(20, publish->payloadlen), publish->payload);

	if (publish->header.bits.qos == 0)
		Protocol_processPublication(publish, client);
	else if (publish->header.bits.qos == 1)
	{
		/* send puback before processing the publications because a lot of return publications could fill up the socket buffer */
		rc = MQTTPacket_send_puback(publish->msgId, &client->net, client->clientID);
		/* if we get a socket error from sending the puback, should we ignore the publication? */
		Protocol_processPublication(publish, client);
	}
	else if (publish->header.bits.qos == 2)
	{
		/* store publication in inbound list */
		int len;
		ListElement* listElem = NULL;
		Messages* m = malloc(sizeof(Messages));
		Publications* p = MQTTProtocol_storePublication(publish, &len);
		m->publish = p;
		m->msgid = publish->msgId;
		m->qos = publish->header.bits.qos;
		m->retain = publish->header.bits.retain;
		m->nextMessageType = PUBREL;
		if ( ( listElem = ListFindItem(client->inboundMsgs, &(m->msgid), messageIDCompare) ) != NULL )
		{   /* discard queued publication with same msgID that the current incoming message */
			Messages* msg = (Messages*)(listElem->content);
			MQTTProtocol_removePublication(msg->publish);
			ListInsert(client->inboundMsgs, m, sizeof(Messages) + len, listElem);
			ListRemove(client->inboundMsgs, msg);
		} else
			ListAppend(client->inboundMsgs, m, sizeof(Messages) + len);
		rc = MQTTPacket_send_pubrec(publish->msgId, &client->net, client->clientID);
		publish->topic = NULL;
	}
	MQTTPacket_freePublish(publish);
	FUNC_EXIT_RC(rc);
	return rc;
}

/**
 * Process an incoming puback packet for a socket
 * @param pack pointer to the publish packet
 * @param sock the socket on which the packet was received
 * @return completion code
 */
int MQTTProtocol_handlePubacks(void* pack, int sock)
{
	Puback* puback = (Puback*)pack;
	Clients* client = NULL;
	int rc = TCPSOCKET_COMPLETE;

	FUNC_ENTRY;
	client = (Clients*)(ListFindItem(bstate->clients, &sock, clientSocketCompare)->content);
	Log(LOG_PROTOCOL, 14, NULL, sock, client->clientID, puback->msgId);

	/* look for the message by message id in the records of outbound messages for this client */
	if (ListFindItem(client->outboundMsgs, &(puback->msgId), messageIDCompare) == NULL)
		Log(TRACE_MIN, 3, NULL, "PUBACK", client->clientID, puback->msgId);
	else
	{
		Messages* m = (Messages*)(client->outboundMsgs->current->content);
		if (m->qos != 1)
			Log(TRACE_MIN, 4, NULL, "PUBACK", client->clientID, puback->msgId, m->qos);
		else
		{
			Log(TRACE_MIN, 6, NULL, "PUBACK", client->clientID, puback->msgId);
			#if !defined(NO_PERSISTENCE)
				rc = MQTTPersistence_remove(client, PERSISTENCE_PUBLISH_SENT, m->qos, puback->msgId);
			#endif
			MQTTProtocol_removePublication(m->publish);
			ListRemove(client->outboundMsgs, m);
		}
	}
	free(pack);
	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 * Process an incoming pubrec packet for a socket
 * @param pack pointer to the publish packet
 * @param sock the socket on which the packet was received
 * @return completion code
 */
int MQTTProtocol_handlePubrecs(void* pack, int sock)
{
	Pubrec* pubrec = (Pubrec*)pack;
	Clients* client = NULL;
	int rc = TCPSOCKET_COMPLETE;

	FUNC_ENTRY;
	client = (Clients*)(ListFindItem(bstate->clients, &sock, clientSocketCompare)->content);
	Log(LOG_PROTOCOL, 15, NULL, sock, client->clientID, pubrec->msgId);

	/* look for the message by message id in the records of outbound messages for this client */
	client->outboundMsgs->current = NULL;
	if (ListFindItem(client->outboundMsgs, &(pubrec->msgId), messageIDCompare) == NULL)
	{
		if (pubrec->header.bits.dup == 0)
			Log(TRACE_MIN, 3, NULL, "PUBREC", client->clientID, pubrec->msgId);
	}
	else
	{
		Messages* m = (Messages*)(client->outboundMsgs->current->content);
		if (m->qos != 2)
		{
			if (pubrec->header.bits.dup == 0)
				Log(TRACE_MIN, 4, NULL, "PUBREC", client->clientID, pubrec->msgId, m->qos);
		}
		else if (m->nextMessageType != PUBREC)
		{
			if (pubrec->header.bits.dup == 0)
				Log(TRACE_MIN, 5, NULL, "PUBREC", client->clientID, pubrec->msgId);
		}
		else
		{
			rc = MQTTPacket_send_pubrel(pubrec->msgId, 0, &client->net, client->clientID);
			m->nextMessageType = PUBCOMP;
			time(&(m->lastTouch));
		}
	}
	free(pack);
	FUNC_EXIT_RC(rc);
	return rc;
}

/*
PUBREL消息是QoS级别为2的协议流的第3个消息。
PUBREL消息用用来响应PUBREC消息的，可以是发布者对来自服务器的PUBREC的响应，也可以是
服务器对来自订阅者的PUBREC的响应。
（双方互相确认，确保发布的消息到了server，也到了各个订阅者）
*/
/**
 * Process an incoming pubrel packet for a socket
 * @param pack pointer to the publish packet
 * @param sock the socket on which the packet was received
 * @return completion code
 */
int MQTTProtocol_handlePubrels(void* pack, int sock)
{
	Pubrel* pubrel = (Pubrel*)pack;
	Clients* client = NULL;
	int rc = TCPSOCKET_COMPLETE;

	FUNC_ENTRY;
	client = (Clients*)(ListFindItem(bstate->clients, &sock, clientSocketCompare)->content);
	Log(LOG_PROTOCOL, 17, NULL, sock, client->clientID, pubrel->msgId);

	/* look for the message by message id in the records of inbound messages for this client */
	if (ListFindItem(client->inboundMsgs, &(pubrel->msgId), messageIDCompare) == NULL)
	{
		if (pubrel->header.bits.dup == 0)
			Log(TRACE_MIN, 3, NULL, "PUBREL", client->clientID, pubrel->msgId);
		else
			/* Apparently this is "normal" behaviour, so we don't need to issue a warning */
			rc = MQTTPacket_send_pubcomp(pubrel->msgId, &client->net, client->clientID);
	}
	else
	{
		Messages* m = (Messages*)(client->inboundMsgs->current->content);
		if (m->qos != 2)
			Log(TRACE_MIN, 4, NULL, "PUBREL", client->clientID, pubrel->msgId, m->qos);
		else if (m->nextMessageType != PUBREL)
			Log(TRACE_MIN, 5, NULL, "PUBREL", client->clientID, pubrel->msgId);
		else
		{
			Publish publish;

			/* send pubcomp before processing the publications because a lot of return publications could fill up the socket buffer */
			rc = MQTTPacket_send_pubcomp(pubrel->msgId, &client->net, client->clientID);
			publish.header.bits.qos = m->qos;
			publish.header.bits.retain = m->retain;
			publish.msgId = m->msgid;
			publish.topic = m->publish->topic;
			publish.topiclen = m->publish->topiclen;
			publish.payload = m->publish->payload;
			publish.payloadlen = m->publish->payloadlen;
			Protocol_processPublication(&publish, client);
			#if !defined(NO_PERSISTENCE)
				rc += MQTTPersistence_remove(client, PERSISTENCE_PUBLISH_RECEIVED, m->qos, pubrel->msgId);
			#endif
			ListRemove(&(state.publications), m->publish);
			ListRemove(client->inboundMsgs, m);
			++(state.msgs_received);
		}
	}
	free(pack);
	FUNC_EXIT_RC(rc);
	return rc;
}

/*
这是QoS基本为2的协议流的第4个，也是最后一个消息。
这个消息是对PUBREL消息的响应。
*/
/**
 * Process an incoming pubcomp packet for a socket
 * @param pack pointer to the publish packet
 * @param sock the socket on which the packet was received
 * @return completion code
 */
int MQTTProtocol_handlePubcomps(void* pack, int sock)
{
	Pubcomp* pubcomp = (Pubcomp*)pack;
	Clients* client = NULL;
	int rc = TCPSOCKET_COMPLETE;

	FUNC_ENTRY;
	client = (Clients*)(ListFindItem(bstate->clients, &sock, clientSocketCompare)->content);
	Log(LOG_PROTOCOL, 19, NULL, sock, client->clientID, pubcomp->msgId);

	/* look for the message by message id in the records of outbound messages for this client */
	if (ListFindItem(client->outboundMsgs, &(pubcomp->msgId), messageIDCompare) == NULL)
	{
		if (pubcomp->header.bits.dup == 0)
			Log(TRACE_MIN, 3, NULL, "PUBCOMP", client->clientID, pubcomp->msgId);
	}
	else
	{
		Messages* m = (Messages*)(client->outboundMsgs->current->content);
		if (m->qos != 2)
			Log(TRACE_MIN, 4, NULL, "PUBCOMP", client->clientID, pubcomp->msgId, m->qos);
		else
		{
			if (m->nextMessageType != PUBCOMP)
				Log(TRACE_MIN, 5, NULL, "PUBCOMP", client->clientID, pubcomp->msgId);
			else
			{
				Log(TRACE_MIN, 6, NULL, "PUBCOMP", client->clientID, pubcomp->msgId);
				#if !defined(NO_PERSISTENCE)
					rc = MQTTPersistence_remove(client, PERSISTENCE_PUBLISH_SENT, m->qos, pubcomp->msgId);
				#endif
				MQTTProtocol_removePublication(m->publish);
				ListRemove(client->outboundMsgs, m);
				(++state.msgs_sent);
			}
		}
	}
	free(pack);
	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 * MQTT protocol keepAlive processing.  Sends PINGREQ packets as required.
 * @param now current time
 */
void MQTTProtocol_keepalive(time_t now)	//-通过调用这个子函数,可以使这个保持活跃状态
{
	ListElement* current = NULL;

	FUNC_ENTRY;
	ListNextElement(bstate->clients, &current);	//-在一个链表中寻找到一个元素
	while (current)
	{//-对一个客户端进行检查
		Clients* client =	(Clients*)(current->content);	//-指向一个客户端实体参数
		ListNextElement(bstate->clients, &current);		//-切换下一个客户端,准备检查,直到没有
		if (client->connected && client->keepAliveInterval > 0 &&
			(difftime(now, client->net.lastSent) >= client->keepAliveInterval ||
					difftime(now, client->net.lastReceived) >= client->keepAliveInterval))
		{//-检查每个用到的客户端,如果超时了就需要处理,否则不需要
			if (client->ping_outstanding == 0)	//-没有清空说明没有接收到应答
			{
				if (Socket_noPendingWrites(client->net.socket))	//-如果有内容没有发送出去就不需要ping了
				{//-如果在一段时间内没有数据交换,那么客户端就需要主动向服务器发送ping请求
					if (MQTTPacket_send_pingreq(&client->net, client->clientID) != TCPSOCKET_COMPLETE)
					{
						Log(TRACE_PROTOCOL, -1, "Error sending PINGREQ for client %s on socket %d, disconnecting", client->clientID, client->net.socket);
						MQTTProtocol_closeSession(client, 1);
					}
					else
					{
						client->net.lastSent = now;
						client->ping_outstanding = 1;	//-已经发送出去了一个ping请求帧
					}
				}
			}
			else
			{//-ping应答没有被接收到,在活跃间隔内
				Log(TRACE_PROTOCOL, -1, "PINGRESP not received in keepalive interval for client %s on socket %d, disconnecting", client->clientID, client->net.socket);
				MQTTProtocol_closeSession(client, 1);	//-所以这里需要断开连接
			}
		}
	}
	FUNC_EXIT;
}


/**
 * MQTT retry processing per client
 * @param now current time
 * @param client - the client to which to apply the retry processing
 * @param regardless boolean - retry packets regardless of retry interval (used on reconnect)
 */
void MQTTProtocol_retries(time_t now, Clients* client, int regardless)	//-每个客户端重试处理
{
	ListElement* outcurrent = NULL;

	FUNC_ENTRY;

	if (!regardless && client->retryInterval <= 0) /* 0 or -ive retryInterval turns off retry except on reconnect */
		goto exit;

	while (client && ListNextElement(client->outboundMsgs, &outcurrent) &&
		   client->connected && client->good &&        /* client is connected and has no errors */
		   Socket_noPendingWrites(client->net.socket)) /* there aren't any previous packets still stacked up on the socket */
	{
		Messages* m = (Messages*)(outcurrent->content);
		if (regardless || difftime(now, m->lastTouch) > max(client->retryInterval, 10))
		{
			if (m->qos == 1 || (m->qos == 2 && m->nextMessageType == PUBREC))
			{
				Publish publish;
				int rc;

				Log(TRACE_MIN, 7, NULL, "PUBLISH", client->clientID, client->net.socket, m->msgid);
				publish.msgId = m->msgid;
				publish.topic = m->publish->topic;
				publish.payload = m->publish->payload;
				publish.payloadlen = m->publish->payloadlen;
				rc = MQTTPacket_send_publish(&publish, 1, m->qos, m->retain, &client->net, client->clientID);
				if (rc == SOCKET_ERROR)
				{
					client->good = 0;
					Log(TRACE_PROTOCOL, 29, NULL, client->clientID, client->net.socket,
												Socket_getpeer(client->net.socket));
					MQTTProtocol_closeSession(client, 1);
					client = NULL;
				}
				else
				{
					if (m->qos == 0 && rc == TCPSOCKET_INTERRUPTED)
						MQTTProtocol_storeQoS0(client, &publish);
					time(&(m->lastTouch));
				}
			}
			else if (m->qos && m->nextMessageType == PUBCOMP)
			{
				Log(TRACE_MIN, 7, NULL, "PUBREL", client->clientID, client->net.socket, m->msgid);
				if (MQTTPacket_send_pubrel(m->msgid, 0, &client->net, client->clientID) != TCPSOCKET_COMPLETE)
				{
					client->good = 0;
					Log(TRACE_PROTOCOL, 29, NULL, client->clientID, client->net.socket,
							Socket_getpeer(client->net.socket));
					MQTTProtocol_closeSession(client, 1);
					client = NULL;
				}
				else
					time(&(m->lastTouch));
			}
			/* break; why not do all retries at once? */
		}
	}
exit:
	FUNC_EXIT;
}


/**
 * MQTT retry protocol and socket pending writes processing.
 * @param now current time
 * @param doRetry boolean - retries as well as pending writes?
 * @param regardless boolean - retry packets regardless of retry interval (used on reconnect)
 */
void MQTTProtocol_retry(time_t now, int doRetry, int regardless)	//-再次重试协议和套接字悬挂的写处理
{
	ListElement* current = NULL;

	FUNC_ENTRY;
	ListNextElement(bstate->clients, &current);
	/* look through the outbound message list of each client, checking to see if a retry is necessary */
	while (current)	//-通过查询标识位,看看是否有必要重试写
	{//-一个客户端一个客户端轮询
		Clients* client = (Clients*)(current->content);	//-指向客户端的实体
		ListNextElement(bstate->clients, &current);	//-切换到下一个准备查询
		if (client->connected == 0)	//-如果没有连接的话就算了
			continue;
		if (client->good == 0)
		{//-如果连接了,但是又不是好状态的话,需要主动关闭会话
			MQTTProtocol_closeSession(client, 1);
			continue;
		}
		if (Socket_noPendingWrites(client->net.socket) == 0)	//-周期查询是否有处于悬挂队列中的
			continue;
		if (doRetry)
			MQTTProtocol_retries(now, client, regardless);
	}
	FUNC_EXIT;
}


/**
 * Free a client structure
 * @param client the client data to free
 */
void MQTTProtocol_freeClient(Clients* client)	//-释放了客户结构体,对于程序而已可能就释放了客户端了
{
	FUNC_ENTRY;
	/* free up pending message lists here, and any other allocated data */
	MQTTProtocol_freeMessageList(client->outboundMsgs);
	MQTTProtocol_freeMessageList(client->inboundMsgs);
	ListFree(client->messageQueue);
	free(client->clientID);
	if (client->will)
	{
		free(client->will->msg);
		free(client->will->topic);
		free(client->will);
	}
#if defined(OPENSSL)
	if (client->sslopts)
	{
		if (client->sslopts->trustStore)
			free((void*)client->sslopts->trustStore);
		if (client->sslopts->keyStore)
			free((void*)client->sslopts->keyStore);
		if (client->sslopts->privateKey)
			free((void*)client->sslopts->privateKey);
		if (client->sslopts->privateKeyPassword)
			free((void*)client->sslopts->privateKeyPassword);
		if (client->sslopts->enabledCipherSuites)
			free((void*)client->sslopts->enabledCipherSuites);
		free(client->sslopts);
	}
#endif
	/* don't free the client structure itself... this is done elsewhere */
	FUNC_EXIT;
}


/**
 * Empty a message list, leaving it able to accept new messages
 * @param msgList the message list to empty
 */
void MQTTProtocol_emptyMessageList(List* msgList)	//-清空一个消息队列,使得她可以获得新的消息
{
	ListElement* current = NULL;

	FUNC_ENTRY;
	while (ListNextElement(msgList, &current))	//-是否可以通过这个猜测到消息是通过队列来存储并管理的
	{
		Messages* m = (Messages*)(current->content);
		MQTTProtocol_removePublication(m->publish);
	}
	ListEmpty(msgList);
	FUNC_EXIT;
}


/**
 * Empty and free up all storage used by a message list
 * @param msgList the message list to empty and free
 */
void MQTTProtocol_freeMessageList(List* msgList)	//-清空所有的被一个消息队列使用的仓库
{
	FUNC_ENTRY;
	MQTTProtocol_emptyMessageList(msgList);
	ListFree(msgList);
	FUNC_EXIT;
}


/**
* Copy no more than dest_size -1 characters from the string pointed to by src to the array pointed to by dest.
* The destination string will always be null-terminated.
* @param dest the array which characters copy to
* @param src the source string which characters copy from
* @param dest_size the size of the memory pointed to by dest: copy no more than this -1 (allow for null).  Must be >= 1
* @return the destination string pointer
*/
char* MQTTStrncpy(char *dest, const char *src, size_t dest_size)	//-没有什么特殊的就是复制内容到另一个地方
{
  size_t count = dest_size;
  char *temp = dest;

  FUNC_ENTRY; 
  if (dest_size < strlen(src))
    Log(TRACE_MIN, -1, "the src string is truncated");	//-字符串被截断

  /* We must copy only the first (dest_size - 1) bytes */
  while (count > 1 && (*temp++ = *src++))
    count--;

  *temp = '\0';

  FUNC_EXIT;
  return dest;
}


/**
* Duplicate a string, safely, allocating space on the heap
* @param src the source string which characters copy from
* @return the duplicated, allocated string
*/
char* MQTTStrdup(const char* src)	//-复制字符串,分配在堆的空间上
{
	size_t mlen = strlen(src) + 1;
	char* temp = malloc(mlen);
	MQTTStrncpy(temp, src, mlen);	//-里面仅仅进行了字符的复制
	return temp;
}
