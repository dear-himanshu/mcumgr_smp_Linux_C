#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/select.h>
#include <string.h>
#include <errno.h>

#include "app_stack_uart.h"


#define MCUMGR_MAX_B64       4096
#define MCUMGR_MAX_PACKET    4096

#define FRAME_START_1        0x06
#define FRAME_START_2        0x09

#define FRAME_CONT_1         0x04
#define FRAME_CONT_2         0x14

#define FRAME_END            0x0A


/* ============================================================
 * CRC16
 * CRC-16/CCITT
 * Polynomial = 0x1021
 * Initial = 0x0000
 * ============================================================ */

static uint16_t gen_crc16(const uint8_t *data, size_t size)
{
    uint16_t crc = 0x0000;

    if (data == NULL)
        return 0;

    while (size--)
    {
        crc ^= (uint16_t)(*data++) << 8;

        for (int i = 0; i < 8; i++)
        {
            if (crc & 0x8000)
                crc = (uint16_t)((crc << 1) ^ 0x1021);
            else
                crc <<= 1;
        }
    }

    return crc;
}


/* ============================================================
 * Base64
 * ============================================================ */

static int base64_value(uint8_t c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A';

    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;

    if (c >= '0' && c <= '9')
        return c - '0' + 52;

    if (c == '+')
        return 62;

    if (c == '/')
        return 63;

    return -1;
}


static int is_base64_char(uint8_t c)
{
    return ((c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '+' ||
            c == '/' ||
            c == '=');
}


static int base64_decode(const uint8_t *input,
                         size_t input_len,
                         uint8_t *output,
                         size_t output_size)
{
    size_t i = 0;
    size_t out_len = 0;

    if (input == NULL || output == NULL)
        return -1;

    if (input_len == 0)
        return 0;

    if ((input_len % 4) != 0)
    {
        printf("ERROR: Base64 length %zu is not multiple of 4\n",
               input_len);
        return -1;
    }

    while (i < input_len)
    {
        uint8_t c0 = input[i++];
        uint8_t c1 = input[i++];
        uint8_t c2 = input[i++];
        uint8_t c3 = input[i++];

        int v0;
        int v1;
        int v2;
        int v3;

        if (c0 == '=' || c1 == '=')
        {
            printf("ERROR: Invalid Base64 padding\n");
            return -1;
        }

        v0 = base64_value(c0);
        v1 = base64_value(c1);

        if (v0 < 0 || v1 < 0)
        {
            printf("ERROR: Invalid Base64 character\n");
            return -1;
        }

        if (out_len + 1 > output_size)
            return -1;

        output[out_len++] =
            (uint8_t)((v0 << 2) | (v1 >> 4));


        /* XX== */

        if (c2 == '=')
        {
            if (c3 != '=')
            {
                printf("ERROR: Invalid Base64 padding\n");
                return -1;
            }

            if (i != input_len)
            {
                printf("ERROR: Base64 padding before end\n");
                return -1;
            }

            break;
        }


        v2 = base64_value(c2);

        if (v2 < 0)
        {
            printf("ERROR: Invalid Base64 character 0x%02X\n",
                   c2);
            return -1;
        }


        if (out_len + 1 > output_size)
            return -1;

        output[out_len++] =
            (uint8_t)(((v1 & 0x0F) << 4) |
                      (v2 >> 2));


        /* XXX= */

        if (c3 == '=')
        {
            if (i != input_len)
            {
                printf("ERROR: Base64 padding before end\n");
                return -1;
            }

            break;
        }


        v3 = base64_value(c3);

        if (v3 < 0)
        {
            printf("ERROR: Invalid Base64 character 0x%02X\n",
                   c3);
            return -1;
        }


        if (out_len + 1 > output_size)
            return -1;

        output[out_len++] =
            (uint8_t)(((v2 & 0x03) << 6) | v3);
    }

    return (int)out_len;
}


/* ============================================================
 * HEX DUMP
 * ============================================================ */

static void dump_hex(const char *title,
                     const uint8_t *data,
                     size_t len)
{
    printf("%s", title);

    for (size_t i = 0; i < len; i++)
        printf("%02X ", data[i]);

    printf("\n");
}


/* ============================================================
 * PROCESS COMPLETE SMP PACKET
 * ============================================================ */

static void process_complete_smp_packet(
        st_uart *s_uart,
        uint8_t *smp_buf,
        size_t smp_len)
{
    printf("\n");
    printf("========================================\n");
    printf("COMPLETE SMP PACKET\n");
    printf("========================================\n");


    dump_hex("SMP packet: ",
             smp_buf,
             smp_len);


    if (smp_len < 10)
    {
        printf("ERROR: SMP packet too short\n");
        return;
    }


    /*
     * Last two bytes = CRC
     */
    size_t data_len = smp_len - 2;


    uint16_t received_crc =
        ((uint16_t)smp_buf[data_len] << 8) |
        smp_buf[data_len + 1];


    uint16_t calculated_crc =
        gen_crc16(smp_buf, data_len);


    printf("Received CRC  : %04X\n",
           received_crc);

    printf("Calculated CRC: %04X\n",
           calculated_crc);


    if (received_crc != calculated_crc)
    {
        printf("ERROR: CRC mismatch\n");
        return;
    }


    printf("CRC OK\n");


    /*
     * SMP HEADER
     *
     * Byte 0     = OP
     * Byte 1     = FLAGS
     * Byte 2-3   = CBOR length
     * Byte 4-5   = GROUP
     * Byte 6     = SEQUENCE
     * Byte 7     = COMMAND
     */

    uint8_t op =
        smp_buf[0];

    uint8_t flags =
        smp_buf[1];

    uint16_t cbor_length =
        ((uint16_t)smp_buf[2] << 8) |
        smp_buf[3];

    uint16_t group =
        ((uint16_t)smp_buf[4] << 8) |
        smp_buf[5];

    uint8_t sequence =
        smp_buf[6];

    uint8_t command =
        smp_buf[7];


    printf("\nSMP HEADER\n");
    printf("----------------------------\n");

    printf("OP       : 0x%02X\n", op);
    printf("FLAGS    : 0x%02X\n", flags);
    printf("CBOR LEN : %u\n", cbor_length);
    printf("GROUP    : %u (0x%04X)\n", group, group);
    printf("SEQUENCE : %u\n", sequence);
    printf("COMMAND  : %u\n", command);


    /*
     * Total SMP packet:
     *
     * 8 byte header
     * + CBOR
     * + 2 byte CRC
     */

    size_t expected_length =
        8 + cbor_length + 2;


    if (expected_length != smp_len)
    {
        printf("\nERROR: SMP length mismatch\n");

        printf("Expected : %zu\n",
               expected_length);

        printf("Received : %zu\n",
               smp_len);

        return;
    }


    /*
     * CBOR starts at byte 8.
     */

    uint8_t *cbor =
        &smp_buf[8];


    printf("\nCBOR (%u bytes):\n",
           cbor_length);

    dump_hex("", cbor, cbor_length);


    /*
     * Give SMP header + CBOR to callback.
     * CRC is excluded.
     */

    if (s_uart != NULL &&
        s_uart->callback_function != NULL)
    {
        memcpy(s_uart->data.data,
               smp_buf,
               data_len);

        s_uart->data.count =
            data_len;

        s_uart->callback_function(
            &s_uart->data);
    }
}


/* ============================================================
 * UART RECEIVE
 * ============================================================ */

int32_t th_serial_recv(void *arg)
{
    st_uart *s_uart =
        (st_uart *)arg;


    enum rx_state
    {
        WAIT_START_06,
        WAIT_START_09,
        RECEIVE_FIRST_B64,
        WAIT_CONT_04,
        WAIT_CONT_14,
        RECEIVE_CONT_B64
    };


    enum rx_state state =
        WAIT_START_06;


    /*
     * Current physical Base64 frame.
     */
    uint8_t b64_buf[MCUMGR_MAX_B64];

    size_t b64_len = 0;


    /*
     * Accumulated SMP packet.
     */
    uint8_t smp_buf[MCUMGR_MAX_PACKET];

    size_t smp_len = 0;


    /*
     * SMP length from transport header.
     *
     * Example:
     *
     *     00 90
     *
     * = 144 bytes.
     */
    uint16_t expected_smp_len = 0;


    while (1)
    {
        fd_set rset;
        struct timeval tv;


        FD_ZERO(&rset);

        FD_SET(s_uart->serial_fd,
               &rset);


        tv.tv_sec = 30;
        tv.tv_usec = 0;


        int count =
            select(s_uart->serial_fd + 1,
                   &rset,
                   NULL,
                   NULL,
                   &tv);


        if (count < 0)
        {
            if (errno == EINTR)
                continue;

            perror("select");
            continue;
        }


        if (count == 0)
            continue;


        if (!FD_ISSET(s_uart->serial_fd,
                      &rset))
        {
            continue;
        }


        /*
         * Read one byte at a time.
         *
         * This is important because UART may split:
         *
         *     06
         *     09 41
         *     42
         *     ...
         */

        uint8_t rx_byte;


        ssize_t n =
            read(s_uart->serial_fd,
                 &rx_byte,
                 1);


        if (n < 0)
        {
            if (errno == EINTR)
                continue;

            perror("read");
            continue;
        }


        if (n == 0)
            continue;


        /* ====================================================
         * WAIT FOR 06
         * ==================================================== */

        if (state == WAIT_START_06)
        {
            if (rx_byte == FRAME_START_1)
            {
                printf("\nReceived 06\n");

                state = WAIT_START_09;

                b64_len = 0;
                smp_len = 0;
                expected_smp_len = 0;
            }

            continue;
        }


        /* ====================================================
         * WAIT FOR 09
         * ==================================================== */

        if (state == WAIT_START_09)
        {
            if (rx_byte == FRAME_START_2)
            {
                printf("Received 06 09\n");

                state = RECEIVE_FIRST_B64;

                b64_len = 0;

                continue;
            }


            /*
             * Another 06 can start a new packet.
             */

            if (rx_byte == FRAME_START_1)
            {
                state = WAIT_START_09;

                b64_len = 0;
                smp_len = 0;
                expected_smp_len = 0;

                continue;
            }


            printf("Invalid byte after 06: %02X\n",
                   rx_byte);

            state = WAIT_START_06;

            continue;
        }


        /* ====================================================
         * FIRST BASE64 FRAME
         * ==================================================== */

        if (state == RECEIVE_FIRST_B64)
        {
            /*
             * 0A terminates THIS physical frame.
             */

            if (rx_byte == FRAME_END)
            {
                printf("\nReceived 0A\n");

                printf("Base64 frame length = %zu\n",
                       b64_len);


                if (b64_len == 0)
                {
                    printf("ERROR: Empty Base64 frame\n");

                    state = WAIT_START_06;

                    continue;
                }


                uint8_t decoded[MCUMGR_MAX_PACKET];


                int decoded_len =
                    base64_decode(
                        b64_buf,
                        b64_len,
                        decoded,
                        sizeof(decoded));


                if (decoded_len < 0)
                {
                    printf("ERROR: Base64 decode failed\n");

                    state = WAIT_START_06;

                    b64_len = 0;
                    smp_len = 0;
                    expected_smp_len = 0;

                    continue;
                }


                printf("Base64 decoded length = %d\n",
                       decoded_len);


                dump_hex("Decoded data:\n",
                         decoded,
                         decoded_len);


                /*
                 * First two bytes are transport length.
                 */

                if (decoded_len < 2)
                {
                    printf("ERROR: First frame too short\n");

                    state = WAIT_START_06;

                    continue;
                }


                expected_smp_len =
                    ((uint16_t)decoded[0] << 8) |
                    decoded[1];


                printf("MCUmgr packet length = %u\n",
                       expected_smp_len);


                if (expected_smp_len == 0)
                {
                    printf("ERROR: Invalid SMP length\n");

                    state = WAIT_START_06;

                    continue;
                }


                if (expected_smp_len >
                    MCUMGR_MAX_PACKET)
                {
                    printf("ERROR: SMP packet too large\n");

                    state = WAIT_START_06;

                    continue;
                }


                /*
                 * Everything after transport length
                 * belongs to SMP.
                 */

                size_t decoded_smp_bytes =
                    decoded_len - 2;


                if (decoded_smp_bytes >
                    expected_smp_len)
                {
                    printf("ERROR: Received more data "
                           "than expected\n");

                    state = WAIT_START_06;

                    b64_len = 0;
                    smp_len = 0;
                    expected_smp_len = 0;

                    continue;
                }


                memcpy(smp_buf,
                       &decoded[2],
                       decoded_smp_bytes);


                smp_len =
                    decoded_smp_bytes;


                printf("Accumulated decoded length = %zu\n",
                       smp_len);

                printf("SMP bytes received = %zu / %u\n",
                       smp_len,
                       expected_smp_len);


                /*
                 * Complete in first frame.
                 */

                if (smp_len == expected_smp_len)
                {
                    process_complete_smp_packet(
                        s_uart,
                        smp_buf,
                        smp_len);


                    state = WAIT_START_06;

                    b64_len = 0;
                    smp_len = 0;
                    expected_smp_len = 0;

                    continue;
                }


                /*
                 * More frames required.
                 */

                printf("SMP packet INCOMPLETE\n");

                printf("Remaining bytes = %u\n",
                       expected_smp_len -
                       smp_len);


                state = WAIT_CONT_04;

                b64_len = 0;

                continue;
            }


            /*
             * '=' IS VALID Base64.
             */

            if (!is_base64_char(rx_byte))
            {
                printf("ERROR: Invalid Base64 character: "
                       "0x%02X\n",
                       rx_byte);

                state = WAIT_START_06;

                b64_len = 0;
                smp_len = 0;
                expected_smp_len = 0;

                continue;
            }


            if (b64_len >= sizeof(b64_buf))
            {
                printf("ERROR: Base64 buffer overflow\n");

                state = WAIT_START_06;

                b64_len = 0;
                smp_len = 0;
                expected_smp_len = 0;

                continue;
            }


            b64_buf[b64_len++] =
                rx_byte;

            continue;
        }


        /* ====================================================
         * WAIT FOR 04
         * ==================================================== */

        if (state == WAIT_CONT_04)
        {
            if (rx_byte == FRAME_CONT_1)
            {
                printf("\nReceived continuation marker 04\n");

                state = WAIT_CONT_14;

                continue;
            }


            /*
             * New packet can start.
             */

            if (rx_byte == FRAME_START_1)
            {
                printf("New 06 received\n");

                state = WAIT_START_09;

                b64_len = 0;
                smp_len = 0;
                expected_smp_len = 0;

                continue;
            }


            printf("Ignoring byte while waiting "
                   "for 04: %02X\n",
                   rx_byte);

            continue;
        }


        /* ====================================================
         * WAIT FOR 14
         * ==================================================== */

        if (state == WAIT_CONT_14)
        {
            if (rx_byte == FRAME_CONT_2)
            {
                printf("Received continuation marker 04 14\n");

                state = RECEIVE_CONT_B64;

                b64_len = 0;

                continue;
            }


            if (rx_byte == FRAME_CONT_1)
            {
                state = WAIT_CONT_14;
                continue;
            }


            printf("Invalid continuation byte: %02X\n",
                   rx_byte);

            state = WAIT_START_06;

            b64_len = 0;
            smp_len = 0;
            expected_smp_len = 0;

            continue;
        }


        /* ====================================================
         * CONTINUATION BASE64 FRAME
         * ==================================================== */

        if (state == RECEIVE_CONT_B64)
        {
            /*
             * 0A terminates THIS physical frame.
             */

            if (rx_byte == FRAME_END)
            {
                printf("\nReceived 0A\n");

                printf("Base64 frame length = %zu\n",
                       b64_len);


                if (b64_len == 0)
                {
                    printf("ERROR: Empty continuation frame\n");

                    state = WAIT_START_06;

                    continue;
                }


                uint8_t decoded[MCUMGR_MAX_PACKET];


                int decoded_len =
                    base64_decode(
                        b64_buf,
                        b64_len,
                        decoded,
                        sizeof(decoded));


                if (decoded_len < 0)
                {
                    printf("ERROR: Base64 decode failed\n");

                    /*
                     * Current SMP packet is invalid.
                     */

                    state = WAIT_START_06;

                    b64_len = 0;
                    smp_len = 0;
                    expected_smp_len = 0;

                    continue;
                }


                printf("Base64 decoded length = %d\n",
                       decoded_len);


                dump_hex("Continuation decoded data:\n",
                         decoded,
                         decoded_len);


                /*
                 * Make sure we do not exceed the expected
                 * SMP packet length.
                 */

                if (smp_len + decoded_len >
                    expected_smp_len)
                {
                    printf("\nERROR: Continuation contains "
                           "too much data\n");

                    printf("Current SMP length = %zu\n",
                           smp_len);

                    printf("Continuation length = %d\n",
                           decoded_len);

                    printf("Expected total = %u\n",
                           expected_smp_len);

                    printf("Expected remaining = %u\n",
                           expected_smp_len -
                           smp_len);


                    /*
                     * Do NOT append invalid bytes.
                     */

                    state = WAIT_START_06;

                    b64_len = 0;
                    smp_len = 0;
                    expected_smp_len = 0;

                    continue;
                }


                /*
                 * Append continuation data.
                 */

                memcpy(&smp_buf[smp_len],
                       decoded,
                       decoded_len);


                smp_len +=
                    decoded_len;


                printf("Accumulated SMP length = %zu\n",
                       smp_len);

                printf("Expected SMP length = %u\n",
                       expected_smp_len);


                /*
                 * Still incomplete.
                 */

                if (smp_len < expected_smp_len)
                {
                    printf("SMP packet still incomplete\n");

                    printf("Remaining = %u\n",
                           expected_smp_len -
                           smp_len);


                    state = WAIT_CONT_04;

                    b64_len = 0;

                    continue;
                }


                /*
                 * Complete.
                 */

                printf("\n");
                printf("========================================\n");
                printf("SMP PACKET COMPLETE\n");
                printf("========================================\n");


                process_complete_smp_packet(
                    s_uart,
                    smp_buf,
                    smp_len);


                state = WAIT_START_06;

                b64_len = 0;
                smp_len = 0;
                expected_smp_len = 0;

                continue;
            }


            /*
             * '=' is valid.
             */

            if (!is_base64_char(rx_byte))
            {
                printf("ERROR: Invalid Base64 character "
                       "inside continuation: 0x%02X\n",
                       rx_byte);

                /*
                 * Ignore invalid byte.
                 *
                 * Do not put it into Base64 buffer.
                 */

                continue;
            }


            if (b64_len >= sizeof(b64_buf))
            {
                printf("ERROR: Continuation Base64 "
                       "buffer overflow\n");

                state = WAIT_START_06;

                b64_len = 0;
                smp_len = 0;
                expected_smp_len = 0;

                continue;
            }


            b64_buf[b64_len++] =
                rx_byte;

            continue;
        }
    }


    return 0;
}


/* ============================================================
 * UART BEGIN
 * ============================================================ */

int32_t uart_begin(st_uart *s_uart,
                   char *device,
                   int32_t baudrate,
                   int32_t (*callback)(void *))
{
    struct termios options;

    int32_t br = 0;


    if (callback != NULL)
    {
        s_uart->callback_function =
            callback;
    }


    if (baudrate >= 1200 &&
        baudrate <= 230400)
    {
        br = convert_baudrate(baudrate);
    }
    else
    {
        printf("Error : baudrate must be "
               "1200 - 230400\r\n");

        return _FALSE_;
    }


    s_uart->serial_fd =
        open(device,
             O_RDWR |
             O_NOCTTY |
             O_NDELAY);


    if (s_uart->serial_fd == -1)
    {
        printf("Error : open serial device: "
               "%s\r\n",
               device);

        perror("OPEN");

        return _FALSE_;
    }


    if (tcgetattr(s_uart->serial_fd,
                  &options) < 0)
    {
        perror("tcgetattr");

        close(s_uart->serial_fd);

        return _FALSE_;
    }


    options.c_cflag =
        br |
        CS8 |
        CLOCAL |
        CREAD;


    options.c_iflag =
        IGNPAR;


    options.c_oflag =
        0;


    options.c_lflag =
        0;


    tcflush(s_uart->serial_fd,
            TCIFLUSH);


    if (tcsetattr(s_uart->serial_fd,
                  TCSANOW,
                  &options) < 0)
    {
        printf("ERROR : Setup serial failed\r\n");

        close(s_uart->serial_fd);

        return _FALSE_;
    }


    if (pthread_create(
            &s_uart->th_recv,
            NULL,
            (void *)th_serial_recv,
            (void *)s_uart) != _TRUE_)
    {
        printf("ERROR : initial thread receive "
               "serial failed\r\n");

        close(s_uart->serial_fd);

        return _FALSE_;
    }


    return _TRUE_;
}


/* ============================================================
 * SEND STRING
 * ============================================================ */

int32_t serial_send(st_uart *s_uart,
                    char *data)
{
    if (strlen(data) <
        _UART_BUFFER_SIZE_)
    {
        memset(s_uart->send_buff,
               0,
               sizeof(s_uart->send_buff));


        sprintf(s_uart->send_buff,
                "<%s>",
                data);


        return write(
            s_uart->serial_fd,
            s_uart->send_buff,
            strlen(s_uart->send_buff));
    }


    return -1;
}


/* ============================================================
 * SEND RAW BYTES
 * ============================================================ */

int32_t serial_send_bytes(st_uart *s_uart,
                          uint8_t data[],
                          size_t buff_size)
{
    int ret =
        write(s_uart->serial_fd,
              data,
              buff_size);


    printf("write() returned %d\n",
           ret);


    if (ret < 0)
    {
        printf("errno=%d (%s)\n",
               errno,
               strerror(errno));
    }


    return ret;
}


/* ============================================================
 * BAUDRATE
 * ============================================================ */

int32_t convert_baudrate(int32_t baudrate)
{
    switch (baudrate)
    {
        case 1200:
            return B1200;

        case 2400:
            return B2400;

        case 4800:
            return B4800;

        case 9600:
            return B9600;

        case 19200:
            return B19200;

        case 38400:
            return B38400;

        case 57600:
            return B57600;

        case 115200:
            return B115200;

        case 230400:
            return B230400;
    }

    return B9600;
}