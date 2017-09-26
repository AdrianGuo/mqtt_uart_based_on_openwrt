/*
201610172221
tcpdump作为一个Linux下抓包工具,功能很复杂,目前我并不需要实现全部功能.首先参考他
可以抓取简单的网络报文即可
libpcap是一个网络数据包捕获函数库，功能非常强大，Linux下著名的tcpdump就是以它为基础的。
*/
#include <pcap.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>




//-第一个参数是pcap_loop的最后一个参数，当收到足够数量的包后pcap_loop会调用callback回调函数，同时将pcap_loop()的user参数传递给它
//-第二个参数是收到的数据包的pcap_pkthdr类型的指针
//-第三个参数是收到的数据包数据
void getPacket(u_char * arg, const struct pcap_pkthdr * pkthdr, const u_char * packet)
{
  int * id = (int *)arg;
  
  printf("id: %d\n", ++(*id));
  printf("Packet length: %d\n", pkthdr->len);
  printf("Number of bytes: %d\n", pkthdr->caplen);
  printf("Recieved time: %s", ctime((const time_t *)&pkthdr->ts.tv_sec)); 
  
  int i;
  for(i=0; i<pkthdr->len; ++i)
  {
    printf(" %02x", packet[i]);
    if( (i + 1) % 16 == 0 )
    {
      printf("\n");
    }
  }
  
  printf("\n\n");
}

int sniffer_sub(int argc,char* argv[])
{
  char errBuf[PCAP_ERRBUF_SIZE], * devStr;
  
  /* get a device */
  devStr = pcap_lookupdev(errBuf);	//-返回第一个合适的网络接口的字符串指针
  
  if(devStr)
  {
    printf("success: device: %s\n", devStr);
  }
  else
  {
    printf("error: %s\n", errBuf);
    exit(1);
  }
  
  /* open a device, wait until a packet arrives */
  pcap_t * device = pcap_open_live(devStr, 65535, 1, 0, errBuf);	//-返回指定接口的pcap_t类型指针，后面的所有操作都要使用这个指针
  //-第一个参数是第一步获取的网络接口字符串，可以直接使用硬编码。
  //-第二个参数是对于每个数据包，从开头要抓多少个字节，我们可以设置这个值来只抓每个数据包的头部，而不关心具体的内容。典型的以太网帧长度是1518字节，但其他的某些协议的数据包会更长一点，但任何一个协议的一个数据包长度都必然小于65535个字节。
  //-第三个参数指定是否打开混杂模式(Promiscuous Mode)，0表示非混杂模式，任何其他值表示混合模式。如果要打开混杂模式，那么网卡必须也要打开混杂模式，可以使用如下的命令打开eth0混杂模式：
  //-ifconfig eth0 promisc
  //-第四个参数指定需要等待的毫秒数，超过这个数值后，第3步获取数据包的这几个函数就会立即返回。0表示一直等待直到有数据包到来。
  //-第五个参数是存放出错信息的数组。

  if(!device)
  {
    printf("error: pcap_open_live(): %s\n", errBuf);
    exit(1);
  }
  
  /* construct a filter */
  struct bpf_program filter;	//-构造一个过滤表达式
  pcap_compile(device, &filter, "dst port 80", 1, 0);	//-编译这个表达式
  pcap_setfilter(device, &filter);	//-应用这个过滤器
  
  //-应用完过滤表达式之后我们便可以使用pcap_loop()或pcap_next()等抓包函数来抓包了。
  /* wait loop forever */
  int id = 0;
  pcap_loop(device, -1, getPacket, (u_char*)&id);
  
  pcap_close(device);	//-关闭pcap_open_live()获取的pcap_t的网络接口对象并释放相关资源

  return 0;
}


//-例子
/*
01.hutao@hutao-VirtualBox:~/test3$ sudo ./test  
02.success: device: eth0  
03.id: 1  
04.Packet length: 60  
05.Number of bytes: 60  
06.Recieved time: Sat Apr 28 19:57:50 2012  
07. ff ff ff ff ff ff 0a 00 27 00 00 00 08 06 00 01  
08. 08 00 06 04 00 01 0a 00 27 00 00 00 c0 a8 38 01  
09. 00 00 00 00 00 00 c0 a8 38 65 00 00 00 00 00 00  
10. 00 00 00 00 00 00 00 00 00 00 00 00  
11.  
12.id: 2  
13.Packet length: 42  
14.Number of bytes: 42  
15.Recieved time: Sat Apr 28 19:57:50 2012  
16. 0a 00 27 00 00 00 08 00 27 9c ff b1 08 06 00 01  
17. 08 00 06 04 00 02 08 00 27 9c ff b1 c0 a8 38 65  
18. 0a 00 27 00 00 00 c0 a8 38 01  
19.  
*/
