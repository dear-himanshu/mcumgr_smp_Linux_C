/*************************
 * UART For Raspberry Pi (2014/10/10)
 * gcc version 4.6.3 (Debian 4.6.3-14+rpi1)
 * ***********************/

#ifndef APP_STACK_UART_H
#define APP_STACK_UART_H

#include <pthread.h>
#include <stdint.h>

#define _UART_BUFFER_SIZE_ 255
#define _UART_PROTO_HEADER_ '<'
#define _UART_PROTO_FOOTER_ '>'

#define _TRUE_ 0
#define _FALSE_ -1


typedef struct
{
    uint32_t count;
    char data[_UART_BUFFER_SIZE_];
}st_uart_data;

typedef struct
{ // parsing size is limited to 255
    uint32_t count;
    char data[_UART_BUFFER_SIZE_];
}cbor_info_parse;


typedef struct
{
    uint32_t serial_fd;
    pthread_t th_recv;

    int32_t recv_temp_cnt;
    int32_t recv_buff_cnt;
    char recv_temp[_UART_BUFFER_SIZE_];
    char recv_buff[_UART_BUFFER_SIZE_];
    char send_buff[_UART_BUFFER_SIZE_ + 2];
    int32_t (*callback_function)(void *);

    st_uart_data data;

}st_uart;



int32_t serial_send(st_uart *s_uart,char *data);
int32_t uart_begin(st_uart *s_uart,char *device,int32_t baudrate,int32_t (*callback)(void *));
int32_t convert_baudrate(int32_t baudrate);
int32_t th_serial_recv(void *arg);

int32_t serial_send_bytes(st_uart *s_uart, uint8_t data[], size_t buff_size); 

#endif // APP_STACK_UART_H
