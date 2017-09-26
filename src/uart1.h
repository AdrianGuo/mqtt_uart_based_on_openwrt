//-为了方便调试定义了系列变量以便调试输出

#ifndef UART_1_APP_H
#define UART_1_APP_H

void uart_1_Main(int fd);
int UART0_Send(int fd, char *send_buf,int data_len);

#endif /* UART_1_APP_H */
