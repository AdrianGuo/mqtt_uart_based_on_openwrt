/*
201610172221
tcpdump��Ϊһ��Linux��ץ������,���ܸܺ���,Ŀǰ�Ҳ�����Ҫʵ��ȫ������.���Ȳο���
����ץȡ�򵥵����籨�ļ���
libpcap��һ���������ݰ��������⣬���ܷǳ�ǿ��Linux��������tcpdump��������Ϊ�����ġ�
*/
#include <pcap.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>




//-��һ��������pcap_loop�����һ�����������յ��㹻�����İ���pcap_loop�����callback�ص�������ͬʱ��pcap_loop()��user�������ݸ���
//-�ڶ����������յ������ݰ���pcap_pkthdr���͵�ָ��
//-�������������յ������ݰ�����
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
  devStr = pcap_lookupdev(errBuf);	//-���ص�һ�����ʵ�����ӿڵ��ַ���ָ��
  
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
  pcap_t * device = pcap_open_live(devStr, 65535, 1, 0, errBuf);	//-����ָ���ӿڵ�pcap_t����ָ�룬��������в�����Ҫʹ�����ָ��
  //-��һ�������ǵ�һ����ȡ������ӿ��ַ���������ֱ��ʹ��Ӳ���롣
  //-�ڶ��������Ƕ���ÿ�����ݰ����ӿ�ͷҪץ���ٸ��ֽڣ����ǿ����������ֵ��ֻץÿ�����ݰ���ͷ�����������ľ�������ݡ����͵���̫��֡������1518�ֽڣ���������ĳЩЭ������ݰ������һ�㣬���κ�һ��Э���һ�����ݰ����ȶ���ȻС��65535���ֽڡ�
  //-����������ָ���Ƿ�򿪻���ģʽ(Promiscuous Mode)��0��ʾ�ǻ���ģʽ���κ�����ֵ��ʾ���ģʽ�����Ҫ�򿪻���ģʽ����ô��������ҲҪ�򿪻���ģʽ������ʹ�����µ������eth0����ģʽ��
  //-ifconfig eth0 promisc
  //-���ĸ�����ָ����Ҫ�ȴ��ĺ����������������ֵ�󣬵�3����ȡ���ݰ����⼸�������ͻ��������ء�0��ʾһֱ�ȴ�ֱ�������ݰ�������
  //-����������Ǵ�ų�����Ϣ�����顣

  if(!device)
  {
    printf("error: pcap_open_live(): %s\n", errBuf);
    exit(1);
  }
  
  /* construct a filter */
  struct bpf_program filter;	//-����һ�����˱��ʽ
  pcap_compile(device, &filter, "dst port 80", 1, 0);	//-����������ʽ
  pcap_setfilter(device, &filter);	//-Ӧ�����������
  
  //-Ӧ������˱��ʽ֮�����Ǳ����ʹ��pcap_loop()��pcap_next()��ץ��������ץ���ˡ�
  /* wait loop forever */
  int id = 0;
  pcap_loop(device, -1, getPacket, (u_char*)&id);
  
  pcap_close(device);	//-�ر�pcap_open_live()��ȡ��pcap_t������ӿڶ����ͷ������Դ

  return 0;
}


//-����
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
