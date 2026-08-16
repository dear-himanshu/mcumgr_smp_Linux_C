
#include <stdio.h>
#include <string.h>
#include <cbor.h>

#include <stdint.h>
#include <stdio.h>
#include "app_stack_uart.h"
#include <stdlib.h>
#include <unistd.h>
#include "enc_dec_fun.h"

#include <stdlib.h>
#include <memory.h>

#include <openssl/sha.h>

char base46_map[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
                     'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
                     'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
                     'w', 'x', 'y', 'z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/'};

#define _SERIAL_DEV_ "/dev/ttyS0"

st_uart MY_UART;

#define CHUNK_SIZE 295

#define CRC16 0x1021
#define CRC16_POLY 0x1021

#define UART_FRAME_SIZE 127
#define FIRST_DATA_SIZE 126
#define MIDDLE_DATA_SIZE 124

static cbor_info_parse cbor_frame;

static const char b64_enc_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

/* Encodes `len` bytes from `in` into a freshly malloc'd, null-terminated
 * Base64 string. Caller must free() the result. Returns NULL on alloc fail. */
char *base64_encode(const uint8_t *in, size_t len)
{
    size_t out_len = 4 * ((len + 2) / 3); /* ceil(len/3) * 4 */
    char *out = malloc(out_len + 1);      /* +1 for '\0'     */
    if (!out)
        return NULL;

    size_t i, j;
    for (i = 0, j = 0; i + 2 < len; i += 3)
    {
        uint32_t n = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8) | (uint32_t)in[i + 2];
        out[j++] = b64_enc_table[(n >> 18) & 0x3F];
        out[j++] = b64_enc_table[(n >> 12) & 0x3F];
        out[j++] = b64_enc_table[(n >> 6) & 0x3F];
        out[j++] = b64_enc_table[n & 0x3F];
    }

    /* Handle the trailing 1 or 2 bytes. */
    size_t rem = len - i;
    if (rem == 1)
    {
        uint32_t n = (uint32_t)in[i] << 16;
        out[j++] = b64_enc_table[(n >> 18) & 0x3F];
        out[j++] = b64_enc_table[(n >> 12) & 0x3F];
        out[j++] = '=';
        out[j++] = '=';
    }
    else if (rem == 2)
    {
        uint32_t n = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8);
        out[j++] = b64_enc_table[(n >> 18) & 0x3F];
        out[j++] = b64_enc_table[(n >> 12) & 0x3F];
        out[j++] = b64_enc_table[(n >> 6) & 0x3F];
        out[j++] = '=';
    }

    out[j] = '\0';
    return out;
}

uint16_t gen_crc16(const uint8_t *data, size_t size)
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
                crc = (crc << 1) ^ CRC16_POLY;
            else
                crc <<= 1;
        }
    }

    return crc;
}

int build_echo_packet(uint8_t **packet, size_t *packet_len, smp_packet_t *echo_packet)
{
    // struct cbor_pair pair;

    /* -------------------------------
     * 1. Create CBOR payload
     * ------------------------------- */

    // pair.key = cbor_build_string("d");
    // pair.value = cbor_build_string("himanshu");

    // if (!pair.key || !pair.value)
    //     return -1;

    // cbor_item_t *item = cbor_new_definite_map(1);

    // if (!item)
    // {
    //     cbor_decref(&pair.key);
    //     cbor_decref(&pair.value);
    //     return -1;
    // }

    // cbor_map_add(item, pair);

    // uint8_t *cbor_buf = NULL;
    // size_t cbor_len = 0;

    // if (!cbor_serialize_alloc(item, &cbor_buf, &cbor_len))
    // {
    //     cbor_decref(&item);
    //     return -1;
    // }

    // printf("CBOR length = %zu\n", cbor_len);

    // printf("CBOR: ");
    // for (size_t i = 0; i < cbor_len; i++)
    //     printf("%02X ", cbor_buf[i]);

    // printf("\n");

    /*
     * For {"d":"hello"}:
     *
     * A1 61 64 65 68 65 6C 6C 6F
     *
     * length = 9
     */

    /* -------------------------------
     * 2. Create SMP header
     * ------------------------------- */

    uint8_t header[8];

    /*
     * First byte:
     *
     *   Res | Ver | Op
     *
     * For now use the values appropriate
     * for your SMP Echo request.
     */

    // header[0] = 0x00;   /* Op/version */
    header[0] = echo_packet->header.op_ver_res;
    // header[1] = 0x02;   /* Flags */
    header[1] = echo_packet->header.flags;

    /* Payload length - BIG ENDIAN */
    // header[2] = (cbor_len >> 8) & 0xFF;
    // header[3] = cbor_len & 0xFF;

    header[2] = (echo_packet->header.data_len >> 8) & 0xFF;
    header[3] = echo_packet->header.data_len & 0xFF;

    /* Group ID = 0 (OS) */
    // header[4] = 0x00;
    // header[5] = 0x01;
    header[4] = (echo_packet->header.group_id >> 8) & 0xFF;
    header[5] = echo_packet->header.group_id & 0xFF;

    /* Sequence */
    // header[6] = 0x00;
    header[6] = echo_packet->header.seq_num;

    /* Command ID */
    // header[7] = 0x00;
    header[7] = echo_packet->header.cmd_id;

    /* -------------------------------
     * 3. Allocate SMP packet
     * ------------------------------- */

    size_t smp_len = sizeof(header) + echo_packet->header.data_len;

    uint8_t *smp_packet = malloc(smp_len + 2);

    if (!smp_packet)
    {
        // free(cbor_buf);
        // cbor_decref(&item);
        return -1;
    }

    /* Copy header */
    memcpy(smp_packet, header, sizeof(header));

    /* Copy CBOR */
    // memcpy(smp_packet + sizeof(header),
    //        cbor_buf,
    //        cbor_len);
    memcpy(smp_packet + sizeof(header),
           echo_packet->data,
           echo_packet->header.data_len);

    /* -------------------------------
     * 4. Calculate CRC
     * ------------------------------- */
    printf("SMP packet length = %zu\n", smp_len);

    uint16_t crc = gen_crc16(smp_packet, smp_len); // 5B 5E
    // uint16_t crc = (uint16_t)((0x1C | (0x33 << 8 )));

    printf("CRC = %04X\n", crc);

    /*
     * Append CRC.
     *
     * Verify the byte order expected by
     * your MCUmgr serial transport.
     */

    smp_packet[smp_len] = (crc >> 8) & 0xFF;
    smp_packet[smp_len + 1] = crc & 0xFF;

    smp_len += 2;

    /* -------------------------------
     * 5. Print complete packet
     * ------------------------------- */

    printf("SMP packet length = %zu\n", smp_len);

    printf("SMP packet: ");

    for (size_t i = 0; i < smp_len; i++)
        printf("%02X ", smp_packet[i]);

    printf("\n");

    *packet = smp_packet;
    *packet_len = smp_len;

    // free(cbor_buf);
    // cbor_decref(&item);

    return 0;
}



char *base64_encode_1(uint8_t *plain, size_t len)
{

    char counts = 0;
    char buffer[3];
    char *cipher = malloc(len * 4 / 3 + 4);
    int i = 0, c = 0;

    for (i = 0; i < len; i++)
    {
        buffer[counts++] = plain[i];
        if (counts == 3)
        {
            cipher[c++] = base46_map[buffer[0] >> 2];
            cipher[c++] = base46_map[((buffer[0] & 0x03) << 4) + (buffer[1] >> 4)];
            cipher[c++] = base46_map[((buffer[1] & 0x0f) << 2) + (buffer[2] >> 6)];
            cipher[c++] = base46_map[buffer[2] & 0x3f];
            counts = 0;
        }
    }

    if (counts > 0)
    {
        cipher[c++] = base46_map[buffer[0] >> 2];
        if (counts == 1)
        {
            cipher[c++] = base46_map[(buffer[0] & 0x03) << 4];
            cipher[c++] = '=';
        }
        else
        { // if counts == 2
            cipher[c++] = base46_map[((buffer[0] & 0x03) << 4) + (buffer[1] >> 4)];
            cipher[c++] = base46_map[(buffer[1] & 0x0f) << 2];
        }
        cipher[c++] = '=';
    }

    cipher[c] = '\0'; /* string padding character */
    return cipher;
}

int build_seial_packet(uint8_t **packet_final, size_t *packet_final_len, smp_packet_t *echo_packet)
{
    uint8_t *packet = NULL;
    size_t packet_len = 0;

    if (build_echo_packet(&packet, &packet_len, echo_packet) != 0)
        return -1;

    printf("SMP packet + CRC:\n");

    for (size_t i = 0; i < packet_len; i++)
        printf("%02X ", packet[i]);

    printf("\n");

    /*
     * packet_len includes:
     *
     *     8-byte SMP header
     *   + CBOR payload
     *   + 2-byte CRC
     *
     * For your example:
     *
     *     8 + 9 + 2 = 19 bytes
     */

    /*
     * MCUmgr serial length field.
     *
     * According to the frame you showed earlier,
     * this represents the SMP packet length WITHOUT CRC.
     */
    uint16_t smp_len = packet_len;

    /*
     * Create:
     *
     *   length(2) + packet(SMP + CRC)
     */
    size_t transport_len = 2 + packet_len;

    uint8_t *transport = malloc(transport_len);

    if (transport == NULL)
    {
        free(packet);
        return -1;
    }

    /*
     * Length is BIG ENDIAN.
     */
    transport[0] = (smp_len >> 8) & 0xFF;
    transport[1] = smp_len & 0xFF;

    /*
     * Copy SMP packet + CRC.
     */
    memcpy(transport + 2, packet, packet_len);

    printf("Transport data BEFORE Base64:\n");

    for (size_t i = 0; i < transport_len; i++)
        printf("%02X ", transport[i]);

    printf("\n");

    /*
     * Base64 encode.
     */
    printf("lenght of transport : %zu \n", transport_len);
    char *b64 = base64_encode_1(transport, transport_len);

    if (b64 == NULL)
    {
        free(transport);
        free(packet);
        return -1;
    }

    printf("Base64:\n%s\n", b64);

    /*
     * Final MCUmgr serial frame:
     *
     *     06 09
     *     Base64 text
     *     0A
     */

    size_t b64_len = strlen(b64);

    size_t final_len = 2 + b64_len + 1;

    uint8_t *final_frame = malloc(final_len);

    if (final_frame == NULL)
    {
        free(b64);
        free(transport);
        free(packet);
        return -1;
    }

    size_t index = 0;

    /* Start sequence */
    final_frame[index++] = 0x06;
    final_frame[index++] = 0x09;

    /* Base64 ASCII */
    memcpy(final_frame + index, b64, b64_len);
    index += b64_len;

    /* End */
    final_frame[index++] = 0x0A;

    printf("FINAL UART FRAME:\n");

    for (size_t i = 0; i < final_len; i++)
    {
        printf("%02X ", final_frame[i]);
    }

    printf("\n");

    // set the value to the passed aruguments
    *packet_final = final_frame;
    *packet_final_len = final_len; // no confusion

    // free(final_frame);
    free(b64);
    free(transport);
    free(packet);

    return 0;
}

int build_seial_packet_continue(uint8_t **packet_final, size_t *packet_final_len, smp_packet_t *echo_packet)
{
    uint8_t *packet = NULL;
    size_t packet_len = 0;

    if (build_echo_packet(&packet, &packet_len, echo_packet) != 0)
        return -1;

    printf("SMP packet + CRC:\n");

    for (size_t i = 0; i < packet_len; i++)
        printf("%02X ", packet[i]);

    printf("\n");

    /*
     * packet_len includes:
     *
     *     8-byte SMP header
     *   + CBOR payload
     *   + 2-byte CRC
     *
     * For your example:
     *
     *     8 + 9 + 2 = 19 bytes
     */

    /*
     * MCUmgr serial length field.
     *
     * According to the frame you showed earlier,
     * this represents the SMP packet length WITHOUT CRC.
     */
    uint16_t smp_len = packet_len;

    /*
     * Create:
     *
     *   length(2) + packet(SMP + CRC)
     */
    // size_t transport_len = 2 + packet_len;
    size_t transport_len = packet_len;

    uint8_t *transport = malloc(transport_len);

    if (transport == NULL)
    {
        free(packet);
        return -1;
    }

    /*
     * Length is BIG ENDIAN.
     */
    // transport[0] = (smp_len >> 8) & 0xFF;
    // transport[1] = smp_len & 0xFF;

    /*
     * Copy SMP packet + CRC.
     */
    memcpy(transport, packet, packet_len);

    printf("Transport data BEFORE Base64:\n");

    for (size_t i = 0; i < transport_len; i++)
        printf("%02X ", transport[i]);

    printf("\n");

    /*
     * Base64 encode.
     */
    printf("lenght of transport : %zu \n", transport_len);
    char *b64 = base64_encode_1(transport, transport_len);

    if (b64 == NULL)
    {
        free(transport);
        free(packet);
        return -1;
    }

    printf("Base64:\n%s\n", b64);

    /*
     * Final MCUmgr serial frame:
     *
     *     06 09
     *     Base64 text
     *     0A
     */

    size_t b64_len = strlen(b64);

    size_t final_len = 2 + b64_len + 1;

    uint8_t *final_frame = malloc(final_len);

    if (final_frame == NULL)
    {
        free(b64);
        free(transport);
        free(packet);
        return -1;
    }

    size_t index = 0;

    /* Start sequence */
    final_frame[index++] = 0x04;
    final_frame[index++] = 0x14;

    /* Base64 ASCII */
    memcpy(final_frame + index, b64, b64_len);
    index += b64_len;

    /* End */
    final_frame[index++] = 0x0A;

    printf("FINAL UART FRAME:\n");

    for (size_t i = 0; i < final_len; i++)
    {
        printf("%02X ", final_frame[i]);
    }

    printf("\n");

    // set the value to the passed aruguments
    *packet_final = final_frame;
    *packet_final_len = final_len; // no confusion

    // free(final_frame);
    free(b64);
    free(transport);
    free(packet);

    return 0;
}

smp_packet_t *echo_function()
{

    smp_header_t *echo_header = (smp_header_t *)malloc(sizeof(smp_header_t));
    smp_packet_t *echo_packet = (smp_packet_t *)malloc(sizeof(smp_packet_t));

    if (echo_packet != NULL && echo_header != NULL)
    {
        echo_packet->data = (uint8_t *)malloc(MAX_BUFFER_SIZE);
    }
    else
    {
        printf("Memory allocation failed\n");
        return NULL;
    }
    /* for echo
     *
     * res 0
     * ver 0
     * OP ? 0 or 2
     * flags 0
     * data len ?
     * group id 0
     * seq num ?
     * cmd id 0
     * data ?
     */

    echo_header->op_ver_res = (uint8_t)0x00;
    echo_header->flags = (uint8_t)0x02;
    echo_header->data_len = (uint16_t)0x0000;
    echo_header->group_id = (uint16_t)0x0000;
    echo_header->seq_num = (uint8_t)0x00;
    echo_header->cmd_id = (uint8_t)0x00;

    struct cbor_pair pair;
    /* -------------------------------
     * 1. Create CBOR payload
     * ------------------------------- */

    pair.key = cbor_build_string("d");
    pair.value = cbor_build_string("himanshu");

    if (!pair.key || !pair.value)
        return NULL;

    cbor_item_t *item = cbor_new_definite_map(1);

    if (!item)
    {
        cbor_decref(&pair.key);
        cbor_decref(&pair.value);
        return NULL;
    }

    cbor_map_add(item, pair);

    uint8_t *cbor_buf = NULL;
    size_t cbor_len = 0;

    if (!cbor_serialize_alloc(item, &cbor_buf, &cbor_len))
    {
        cbor_decref(&item);
        return NULL;
    }

    printf("CBOR length fun= %zu\n", cbor_len);

    printf("CBOR PDU: ");
    for (size_t i = 0; i < cbor_len; i++)
        printf("%02X ", cbor_buf[i]);

    printf("\n");

    memcpy(echo_packet->data, cbor_buf, cbor_len);

    echo_header->data_len = (uint16_t)cbor_len;
    echo_packet->header = *echo_header;

    // echo_packet->header = echo_header ;
    // cbor pdu information
    return echo_packet;
}

smp_packet_t *img_list()
{

    smp_header_t *echo_header = (smp_header_t *)malloc(sizeof(smp_header_t));
    smp_packet_t *echo_packet = (smp_packet_t *)malloc(sizeof(smp_packet_t));

    if (echo_packet != NULL && echo_header != NULL)
    {
        echo_packet->data = (uint8_t *)malloc(MAX_BUFFER_SIZE);
    }
    else
    {
        printf("Memory allocation failed\n");
        return NULL;
    }
    /* for echo
     *
     * res 0
     * ver 0
     * OP ? 0 or 2
     * flags 0
     * data len ?
     * group id 0
     * seq num ?
     * cmd id 0
     * data ?
     */

    echo_header->op_ver_res = (uint8_t)0x00;
    echo_header->flags = (uint8_t)0x02;
    echo_header->data_len = (uint16_t)0x0000;
    echo_header->group_id = (uint16_t)0x0001;
    echo_header->seq_num = (uint8_t)0x00;
    echo_header->cmd_id = (uint8_t)0x00;

    struct cbor_pair pair;
    /* -------------------------------
     * 1. Create CBOR payload
     * ------------------------------- */

    // pair.key = cbor_build_string("d");
    // pair.value = cbor_build_string("himanshu");

    // if (!pair.key || !pair.value)
    //     return NULL;

    cbor_item_t *item = cbor_new_definite_map(1);

    if (!item)
    {
        // cbor_decref(&pair.key);
        // cbor_decref(&pair.value);
        return NULL;
    }

    // cbor_map_add(item, pair);

    uint8_t *cbor_buf = NULL;
    size_t cbor_len = 0;

    if (!cbor_serialize_alloc(item, &cbor_buf, &cbor_len))
    {
        cbor_decref(&item);
        return NULL;
    }

    printf("CBOR length fun= %zu\n", cbor_len);

    printf("CBOR PDU: ");
    for (size_t i = 0; i < cbor_len; i++)
        printf("%02X ", cbor_buf[i]);

    printf("\n");

    memcpy(echo_packet->data, cbor_buf, cbor_len);

    echo_header->data_len = (uint16_t)cbor_len;
    echo_packet->header = *echo_header;

    // echo_packet->header = echo_header ;
    // cbor pdu information
    return echo_packet;
}

smp_packet_t *mcu_reset()
{

    smp_header_t *echo_header = (smp_header_t *)malloc(sizeof(smp_header_t));
    smp_packet_t *echo_packet = (smp_packet_t *)malloc(sizeof(smp_packet_t));

    if (echo_packet != NULL && echo_header != NULL)
    {
        echo_packet->data = (uint8_t *)malloc(MAX_BUFFER_SIZE);
    }
    else
    {
        printf("Memory allocation failed\n");
        return NULL;
    }
    /* for echo
     *
     * res 0
     * ver 0
     * OP ? 0 or 2
     * flags 0
     * data len ?
     * group id 0
     * seq num ?
     * cmd id 0
     * data ?
     */

    echo_header->op_ver_res = (uint8_t)0x02;
    echo_header->flags = (uint8_t)0x02;
    echo_header->data_len = (uint16_t)0x0000;
    echo_header->group_id = (uint16_t)0x0000;
    echo_header->seq_num = (uint8_t)0x00;
    echo_header->cmd_id = (uint8_t)0x05;

    struct cbor_pair pair;
    /* -------------------------------
     * 1. Create CBOR payload
     * ------------------------------- */

    // pair.key = cbor_build_string("d");
    // pair.value = cbor_build_string("himanshu");

    // if (!pair.key || !pair.value)
    //     return NULL;

    cbor_item_t *item = cbor_new_definite_map(1);

    if (!item)
    {
        // cbor_decref(&pair.key);
        // cbor_decref(&pair.value);
        return NULL;
    }

    // cbor_map_add(item, pair);

    uint8_t *cbor_buf = NULL;
    size_t cbor_len = 0;

    if (!cbor_serialize_alloc(item, &cbor_buf, &cbor_len))
    {
        cbor_decref(&item);
        return NULL;
    }

    printf("CBOR length fun= %zu\n", cbor_len);

    printf("CBOR PDU: ");
    for (size_t i = 0; i < cbor_len; i++)
        printf("%02X ", cbor_buf[i]);

    printf("\n");

    memcpy(echo_packet->data, cbor_buf, cbor_len);

    echo_header->data_len = (uint16_t)cbor_len;
    echo_packet->header = *echo_header;

    // echo_packet->header = echo_header ;
    // cbor pdu information
    return echo_packet;
}

smp_packet_t *mcu_confirm_img(uint8_t hash_img[])
{

    smp_header_t *echo_header = (smp_header_t *)malloc(sizeof(smp_header_t));
    smp_packet_t *echo_packet = (smp_packet_t *)malloc(sizeof(smp_packet_t));

    if (echo_packet != NULL && echo_header != NULL)
    {
        echo_packet->data = (uint8_t *)malloc(MAX_BUFFER_SIZE);
    }
    else
    {
        printf("Memory allocation failed\n");
        return NULL;
    }
    /* for echo
     *
     * res 0
     * ver 0
     * OP ? 0 or 2
     * flags 0
     * data len ?
     * group id 0
     * seq num ?
     * cmd id 0
     * data ?
     */

    echo_header->op_ver_res = (uint8_t)0x02;
    echo_header->flags = (uint8_t)0x02;
    echo_header->data_len = (uint16_t)0x0000;
    echo_header->group_id = (uint16_t)0x0001;
    echo_header->seq_num = (uint8_t)0x00;
    echo_header->cmd_id = (uint8_t)0x00;

    struct cbor_pair pair;
    /* -------------------------------
     * 1. Create CBOR payload
     * ------------------------------- */

    pair.key = cbor_build_string("confirm");
    pair.value = cbor_build_bool(true);

    if (!pair.key || !pair.value)
        return NULL;

    cbor_item_t *item = cbor_new_definite_map(2);

    if (!item)
    {
        cbor_decref(&pair.key);
        cbor_decref(&pair.value);
        return NULL;
    }

    cbor_map_add(item, pair);

    cbor_data hash_str = hash_img; 
    // memcpy(hash_str,hash_img,32); 

    pair.key = cbor_build_string("hash");
    pair.value = cbor_build_bytestring(hash_str,32);
    cbor_map_add(item, pair);

    uint8_t *cbor_buf = NULL;
    size_t cbor_len = 0;

    if (!cbor_serialize_alloc(item, &cbor_buf, &cbor_len))
    {
        cbor_decref(&item);
        return NULL;
    }

    printf("CBOR length fun= %zu\n", cbor_len);

    printf("CBOR PDU: ");
    for (size_t i = 0; i < cbor_len; i++)
        printf("%02X ", cbor_buf[i]);

    printf("\n");

    memcpy(echo_packet->data, cbor_buf, cbor_len);

    echo_header->data_len = (uint16_t)cbor_len;
    echo_packet->header = *echo_header;

    // echo_packet->header = echo_header ;
    // cbor pdu information
    return echo_packet;
}

int32_t my_uart_callback_function(void *arg)
{
    st_uart_data *data = (st_uart_data *)arg;
    printf("\nUART callback function called\n");
    // printf("%.*s\n", data->count, data->data);
    // printf("%.*s\n", 4, arr);
    // for(int i=0 ;i<(data->count) ; i++){
    //     printf("%0X ",data->data[i]);
    // }

    printf("\n");
    printf("data size %d\r\n", data->count);

    // setting the data to a global buffer

    cbor_frame.count = (uint32_t)data->count - 8;

    memset(cbor_frame.data, 0, cbor_frame.count);

    memcpy(cbor_frame.data, &(data->data[8]), data->count);

    // data->data[3] //len of cbor_frame;
}

smp_packet_t *img_uploade_init(uint8_t *buffer, size_t buffer_size, uint32_t off, uint32_t len, uint8_t *sha, size_t sha_size)
{

    smp_header_t *echo_header = (smp_header_t *)malloc(sizeof(smp_header_t));
    smp_packet_t *echo_packet = (smp_packet_t *)malloc(sizeof(smp_packet_t));

    if (echo_packet != NULL && echo_header != NULL)
    {
        echo_packet->data = (uint8_t *)malloc(MAX_BUFFER_SIZE);
    }
    else
    {
        printf("Memory allocation failed\n");
        return NULL;
    }
    /* for echo
     *
     * res 0
     * ver 0
     * OP ? 0 or 2
     * flags 0
     * data len ?
     * group id 0
     * seq num ?
     * cmd id 0
     * data ?
     */

    echo_header->op_ver_res = (uint8_t)0x02;
    echo_header->flags = (uint8_t)0x00;
    echo_header->data_len = (uint16_t)0x0000;
    echo_header->group_id = (uint16_t)0x0001;
    echo_header->seq_num = (uint8_t)0x00;
    echo_header->cmd_id = (uint8_t)0x01;

    struct cbor_pair pair;
    /* -------------------------------
     * 1. Create CBOR payload  | adding image name
     * ------------------------------- */
    size_t map_size = (off == 0) ? 5 : 3;

    cbor_item_t *item = cbor_new_definite_map(map_size);
    // cbor_item_t *item = cbor_new_definite_map(6);

    pair.key = cbor_build_string("image");
    pair.value = cbor_build_uint8(0x00);

    if (!pair.key || !pair.value)
        return NULL;

    if (!item)
    {
        cbor_decref(&pair.key);
        cbor_decref(&pair.value);
        return NULL;
    }

    cbor_map_add(item, pair);

    //----------------------adding len

    if (off == 0)
    {
        pair.key = cbor_build_string("len");
        pair.value = cbor_build_uint32(len);

        if (!pair.key || !pair.value)
            return NULL;

        if (!item)
        {
            cbor_decref(&pair.key);
            cbor_decref(&pair.value);
            return NULL;
        }

        cbor_map_add(item, pair);
    }
    //--------------------adding off

    pair.key = cbor_build_string("off");
    pair.value = cbor_build_uint32(off);

    if (!pair.key || !pair.value)
        return NULL;

    if (!item)
    {
        cbor_decref(&pair.key);
        cbor_decref(&pair.value);
        return NULL;
    }

    cbor_map_add(item, pair);

    // --------------------adding sha

    if (off == 0)
    {

        cbor_data sha_bytestring = sha;
        size_t sha_len = sha_size;
        
        pair.key = cbor_build_string("sha");
        pair.value = cbor_build_bytestring(sha_bytestring, sha_len);

        if (!pair.key || !pair.value)
            return NULL;

        if (!item)
        {
            cbor_decref(&pair.key);
            cbor_decref(&pair.value);
            return NULL;
        }

        cbor_map_add(item, pair);
    }

    //---------------------adding data

    cbor_data data_bytestring = buffer;
    size_t data_len = buffer_size;
    pair.key = cbor_build_string("data");
    pair.value = cbor_build_bytestring(data_bytestring, data_len); //->>>>>>>>>>>this should be byte string

    if (!pair.key || !pair.value)
        return NULL;

    if (!item)
    {
        cbor_decref(&pair.key);
        cbor_decref(&pair.value);
        return NULL;
    }

    cbor_map_add(item, pair);

    // adding the CBOR to the PDU

    uint8_t *cbor_buf = NULL;
    size_t cbor_len = 0;

    if (!cbor_serialize_alloc(item, &cbor_buf, &cbor_len))
    {
        cbor_decref(&item);
        return NULL;
    }

    printf("CBOR length fun= %zu\n", cbor_len);

    printf("CBOR PDU: ");
    for (size_t i = 0; i < cbor_len; i++)
        printf("%02X ", cbor_buf[i]);

    printf("\n");

    memcpy(echo_packet->data, cbor_buf, cbor_len);

    echo_header->data_len = (uint16_t)cbor_len;
    echo_packet->header = *echo_header;

    // echo_packet->header = echo_header ;
    // cbor pdu information
    return echo_packet;
}

static long get_file_size(FILE *fp)
{
    long size;

    if (fseek(fp, 0, SEEK_END) != 0)
    {
        return -1;
    }

    size = ftell(fp);

    if (size < 0)
    {
        return -1;
    }

    rewind(fp);

    return size;
}

static int calculate_sha256(FILE *fp, unsigned char hash[SHA256_DIGEST_LENGTH])
{
    unsigned char buffer[4096];
    size_t bytes_read;

    SHA256_CTX sha256_ctx;

    if (SHA256_Init(&sha256_ctx) != 1)
    {
        return -1;
    }

    rewind(fp);

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), fp)) > 0)
    {

        if (SHA256_Update(&sha256_ctx, buffer, bytes_read) != 1)
        {
            return -1;
        }
    }

    if (ferror(fp))
    {
        return -1;
    }

    if (SHA256_Final(hash, &sha256_ctx) != 1)
    {
        return -1;
    }

    rewind(fp);

    return 0;
}

static void print_sha256(const unsigned char hash[SHA256_DIGEST_LENGTH])
{
    printf("SHA256: ");

    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
    {
        printf("%02x", hash[i]);
    }

    printf("\n");
}

static void print_hex(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++)
    {

        printf("%02X ", data[i]);

        if ((i + 1) % 16 == 0)
        {
            printf("\n");
        }
    }

    if (len % 16 != 0)
    {
        printf("\n");
    }
}

int upload_image()
{

    const char *file_name = "/home/riddle/Programming/golioth-firmware-sdk/examples/linux/golioth_basics/mcumgr_smp_Elinux/zephyr_cyrax.bin";

    FILE *fp;

    long file_size;

    uint8_t buffer[CHUNK_SIZE];

    size_t bytes_read;

    uint32_t off = 0;

    unsigned char sha256[SHA256_DIGEST_LENGTH];

    /*
     * OPEN  the file
     *
     */

    fp = fopen(file_name, "rb");

    if (fp == NULL)
    {
        perror("Failed to open firmware file");
        return EXIT_FAILURE;
    }

    // get the file size

    file_size = get_file_size(fp);

    if (file_size < 0)
    {
        perror("Failed to get file size");
        fclose(fp);
        return EXIT_FAILURE;
    }

    printf("\n");
    printf("========================================\n");
    printf("Firmware information\n");
    printf("========================================\n");

    printf("File      : %s\n", file_name);
    printf("Size      : %ld bytes\n", file_size);

    /*
     * Number of 64-byte chunks.
     *
     * Correct ceiling division:
     *
     *     (size + 63) / 64
     */

    size_t number_of_chunks =
        ((size_t)file_size + CHUNK_SIZE - 1) / CHUNK_SIZE;

    printf("Chunk size: %d bytes\n", CHUNK_SIZE);
    printf("Chunks    : %zu\n", number_of_chunks);

    /*
     * ---------------------------------------------------------
     * Calculate SHA256 of ENTIRE firmware image
     * ---------------------------------------------------------
     */

    printf("\nCalculating SHA256...\n");

    if (calculate_sha256(fp, sha256) != 0)
    {

        printf("Failed to calculate SHA256\n");

        fclose(fp);

        return EXIT_FAILURE;
    }

    print_sha256(sha256);

    printf("\n");
    printf("========================================\n");
    printf("Reading firmware in chunks\n");
    printf("========================================\n");

    rewind(fp);

    off = 0;

    size_t chunk_number = 0;

    while ((bytes_read = fread(buffer, 1, CHUNK_SIZE, fp)) > 0)
    {

        chunk_number++;
        printf("\n----------------------------------------\n");

        printf("Chunk       : %zu / %zu\n",
               chunk_number,
               number_of_chunks);

        printf("Offset      : %u\n", off);

        printf("Bytes read  : %zu\n", bytes_read);

        printf("End offset  : %u\n",
               off + (uint32_t)bytes_read - 1);

        printf("Data:\n");

        print_hex(buffer, bytes_read);

        /*
         * =====================================================
         *
         * THIS IS WHERE YOU CREATE YOUR MCUmgr IMAGE UPLOAD
         * CBOR REQUEST.
         *
         * First packet:
         *
         *     image = 0
         *     len   = file_size
         *     off   = 0
         *     sha   = sha256
         *     data  = buffer
         *
         * Subsequent packets:
         *
         *     off  = current offset
         *     data = buffer
         *
         * =====================================================
         */

        if (off == 0)
        {

            printf(">>> FIRST MCUmgr upload packet\n");

            printf("    image = 0\n");
            printf("    len   = %ld\n", file_size);
            printf("    off   = %u\n", off);
            printf("    sha   = ");

            for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
            {
                printf("%02X", sha256[i]);
            }

            printf("\n");

            printf("    data  = %zu bytes\n", bytes_read);

            size_t final_len = 0;
            uint8_t *final_frame = NULL;

            smp_packet_t *echo_packet = img_uploade_init(buffer, bytes_read, off, (uint32_t)file_size, sha256, SHA256_DIGEST_LENGTH);

            build_seial_packet(&final_frame, &final_len, echo_packet);
            printf("final_len : %zu \n", final_len);

            size_t offset = 0;
            size_t remaining = final_len;

            if (final_len <= UART_FRAME_SIZE)
            {

                serial_send_bytes(&MY_UART, final_frame, final_len);
                sleep(1);
            }
            else
            {

                /* --------------------------------
                 * FIRST FRAME
                 *
                 * 126 bytes data + 0A
                 * -------------------------------- */
                uint8_t first_frame[UART_FRAME_SIZE];

                memcpy(first_frame, final_frame, FIRST_DATA_SIZE);
                first_frame[126] = 0x0A;

                serial_send_bytes(&MY_UART, first_frame, UART_FRAME_SIZE);

                offset = FIRST_DATA_SIZE;
                remaining -= FIRST_DATA_SIZE;

                /* --------------------------------
                 * REMAINING FRAMES
                 * -------------------------------- */
                while (remaining > 0)
                {

                    if (remaining <= 124)
                    {

                        /*
                         * LAST FRAME
                         *
                         * 04 14 + remaining data
                         *
                         * No 0A here if your protocol
                         * specifies the final frame this way.
                         */
                        uint8_t last_frame[UART_FRAME_SIZE];

                        last_frame[0] = 0x04;
                        last_frame[1] = 0x14;

                        memcpy(last_frame + 2,
                               final_frame + offset,
                               remaining);

                        serial_send_bytes(&MY_UART,
                                          last_frame,
                                          remaining + 2);

                        offset += remaining;
                        remaining = 0;
                    }
                    else
                    {

                        /*
                         * MIDDLE FRAME
                         *
                         * 04 14 + 124 data + 0A
                         * = 127 bytes
                         */
                        uint8_t middle_frame[UART_FRAME_SIZE];

                        middle_frame[0] = 0x04;
                        middle_frame[1] = 0x14;

                        memcpy(middle_frame + 2,
                               final_frame + offset,
                               124);

                        middle_frame[126] = 0x0A;

                        serial_send_bytes(&MY_UART,
                                          middle_frame,
                                          UART_FRAME_SIZE);

                        offset += 124;
                        remaining -= 124;
                    }

                    sleep(1);
                }
            }

            // serial_send_bytes(&MY_UART, final_frame,final_len);
            // sleep(1);
        }
        else
        {

            printf(">>> MCUmgr upload packet\n");

            printf("    off  = %u\n", off);
            printf("    data = %zu bytes\n", bytes_read);

            size_t final_len = 0;
            uint8_t *final_frame = NULL;

            smp_packet_t *echo_packet = img_uploade_init(buffer, bytes_read, off, (uint32_t)file_size, sha256, SHA256_DIGEST_LENGTH);

            // build_seial_packet_continue(&final_frame, &final_len, echo_packet);
            build_seial_packet(&final_frame, &final_len, echo_packet);
            printf("final_len : %zu \n", final_len);

            // we will spilt the entire frame in 127 bytes so that we can send one by one

            //   algo desc ..
            /*1. If total number of bytes is less or equal to 127 then send no need to go inside loop
             * 2. rldr, Fetch first 126 bytes and append 0A at last and send to uart,
             * 3. Always check if the ( number of bytes remaining + 2 ) -- > is <= that 127 , append 04 14 only at first and send
             * 4. else now take another 124 bytes and pack it with 04 14 and 0A
             * 5. repead stp 3
             */

            size_t offset = 0;
            size_t remaining = final_len;

            if (final_len <= UART_FRAME_SIZE)
            {

                serial_send_bytes(&MY_UART, final_frame, final_len);
                sleep(1);
            }
            else
            {

                /* --------------------------------
                 * FIRST FRAME
                 *
                 * 126 bytes data + 0A
                 * -------------------------------- */
                uint8_t first_frame[UART_FRAME_SIZE];

                memcpy(first_frame, final_frame, FIRST_DATA_SIZE);
                first_frame[126] = 0x0A;

                serial_send_bytes(&MY_UART, first_frame, UART_FRAME_SIZE);

                offset = FIRST_DATA_SIZE;
                remaining -= FIRST_DATA_SIZE;

                /* --------------------------------
                 * REMAINING FRAMES
                 * -------------------------------- */
                while (remaining > 0)
                {

                    if (remaining <= 124)
                    {

                        /*
                         * LAST FRAME
                         *
                         * 04 14 + remaining data
                         *
                         * No 0A here if your protocol
                         * specifies the final frame this way.
                         */
                        uint8_t last_frame[UART_FRAME_SIZE];

                        last_frame[0] = 0x04;
                        last_frame[1] = 0x14;

                        memcpy(last_frame + 2,
                               final_frame + offset,
                               remaining);

                        serial_send_bytes(&MY_UART,
                                          last_frame,
                                          remaining + 2);

                        offset += remaining;
                        remaining = 0;
                    }
                    else
                    {

                        /*
                         * MIDDLE FRAME
                         *
                         * 04 14 + 124 data + 0A
                         * = 127 bytes
                         */
                        uint8_t middle_frame[UART_FRAME_SIZE];

                        middle_frame[0] = 0x04;
                        middle_frame[1] = 0x14;

                        memcpy(middle_frame + 2,
                               final_frame + offset,
                               124);

                        middle_frame[126] = 0x0A;

                        serial_send_bytes(&MY_UART,
                                          middle_frame,
                                          UART_FRAME_SIZE);

                        offset += 124;
                        remaining -= 124;
                    }

                    sleep(1);
                }
            }

            // serial_send_bytes(&MY_UART, final_frame,final_len);
            // sleep(1);
        }

        /*
         * -----------------------------------------------------
         * After successfully sending this chunk:
         * -----------------------------------------------------
         */

        off += (uint32_t)bytes_read;

        /*
         * IMPORTANT:
         *
         * Do not increment off by 64 blindly.
         *
         * The final chunk may be less than 64 bytes.
         *
         * Therefore:
         *
         *     off += bytes_read;
         */
    }

    /*
     * ---------------------------------------------------------
     * Check for file read error
     * ---------------------------------------------------------
     */

    if (ferror(fp))
    {

        printf("\nError while reading firmware file\n");

        fclose(fp);

        return EXIT_FAILURE;
    }

    /*
     * ---------------------------------------------------------
     * Verify final offset
     * ---------------------------------------------------------
     */

    printf("\n");
    printf("========================================\n");
    printf("Upload preparation complete\n");
    printf("========================================\n");

    printf("Total file size : %ld bytes\n", file_size);
    printf("Total bytes read: %u bytes\n", off);

    if (off == (uint32_t)file_size)
    {
        printf("Status          : SUCCESS\n");
    }
    else
    {
        printf("Status          : ERROR\n");
    }

    fclose(fp);

    return EXIT_SUCCESS;

    return 0; // breaking out form here just to test some function

    // size_t final_len = 0 ;
    // uint8_t *final_frame = NULL;

    // smp_packet_t* echo_packet = img_uploade_init(buffer, bytes_read, off, (uint32_t)file_size, sha256, SHA256_DIGEST_LENGTH) ;

    // build_seial_packet(&final_frame, &final_len, echo_packet);
    // printf("final_len : %zu \n", final_len);

    // if(uart_begin(&MY_UART,_SERIAL_DEV_,115200,&my_uart_callback_function) != _TRUE_)
    // {
    //     printf("Error : uart_begin failed\r\n");
    //     return 0;
    // }

    // serial_send_bytes(&MY_UART, final_frame,final_len);
    // sleep(1);

    return 0;
}

void test_fun(){
    printf("data output successful !!\r\n"); 
}

int image_smp_start(void)
{

    //-----------------------------------------------------------for image upload--------------
    // printf("setting up the UART driver\n");

    if (uart_begin(&MY_UART, _SERIAL_DEV_, 115200, &my_uart_callback_function) != _TRUE_)
    {
        printf("Error : uart_begin failed\r\n");
        return 0;
    }

    upload_image(); // just to test the function ----------> working fine

    // // // ---------------------------------------------------------for image upload ----------------
    // // return 0;

    size_t final_len = 0;
    uint8_t *final_frame = NULL;

    // smp_packet_t *echo_packet = echo_function();            //--------------> for
    smp_packet_t *echo_packet = img_list();
    // smp_packet_t* echo_packet = mcu_reset() ;
    // smp_packet_t* echo_packet = mcu_confirm_img();

    build_seial_packet(&final_frame, &final_len, echo_packet);
    printf("final_len : %zu \n", final_len);

    serial_send_bytes(&MY_UART, final_frame, final_len);

    sleep(5);
    printf("Data stream from the gloabl buffer and rcv callback!! size : %d\r\n", cbor_frame.count);
    for (int i = 0; i < cbor_frame.count; i++)
    {
        printf("%02X ", cbor_frame.data[i]);
    }
 
    // cbor_info_parse local_buff = {0};

    // memcpy(local_buff.data,cbor_frame.data,cbor_frame.count);
    // local_buff.count = cbor_frame.count;

    // struct cbor_load_result result;
    //  // And deserialize bytes back to an item
    // cbor_item_t* decoded_item = NULL ;
    // decoded_item = cbor_load(local_buff.data, local_buff.count, &result);
    // if (decoded_item == NULL ||
    // result.error.code != CBOR_ERR_NONE) {

    // printf("Error reading CBOR data! error=%d\n",
    //        result.error.code);

    // return result.error.code ;
    // }
    // int mp_sze = cbor_map_size(decoded_item);

    // printf("\r\nnumber of pair in the CBOR map: %d", cbor_map_size(decoded_item));

    // struct cbor_pair * image_list_pair = NULL;
    // image_list_pair = cbor_map_handle(decoded_item);

    // // Look up a text-string key using structural equality
    // cbor_item_t *key = cbor_build_string("splitStatus");
    // cbor_item_t *value = cbor_map_get(map, key, cbor_structurally_equal);
    // if (value != NULL) {
    //     // use value ...
    //     cbor_decref(&value);
    // }
    // cbor_decref(&key);

    uint8_t new_image_hash[32];

    cbor_info_parse local_buff = {0};

    memcpy(local_buff.data, cbor_frame.data, cbor_frame.count);
    local_buff.count = cbor_frame.count;

    /* Decode CBOR */
    struct cbor_load_result result = {0};

    cbor_item_t *decoded_item = NULL;

    decoded_item = cbor_load(local_buff.data,
                             local_buff.count,
                             &result);

    if (decoded_item == NULL ||
        result.error.code != CBOR_ERR_NONE)
    {

        printf("Error reading CBOR data! error=%d\r\n",
               result.error.code);

        return -1;
    }

    /* Check root */
    if (!cbor_isa_map(decoded_item))
    {

        printf("CBOR root is not a map\r\n");

        cbor_decref(&decoded_item);

        return -1;
    }

    printf("Number of pairs: %zu\r\n",
           cbor_map_size(decoded_item));

    /* =========================================================
     * Find "images" manually
     * ========================================================= */

    struct cbor_pair *root_pairs =
        cbor_map_handle(decoded_item);

    cbor_item_t *images = NULL;

    for (size_t i = 0; i < cbor_map_size(decoded_item); i++)
    {

        cbor_item_t *key = root_pairs[i].key;

        if (!cbor_isa_string(key))
            continue;

        size_t key_len = cbor_string_length(key);

        char *key_str =
            (char *)cbor_string_handle(key);

        if (key_len == 6 &&
            memcmp(key_str, "images", 6) == 0)
        {

            images = root_pairs[i].value;

            break;
        }
    }

    if (images == NULL)
    {

        printf("images not found\r\n");

        cbor_decref(&decoded_item);

        return -1;
    }

    if (!cbor_isa_array(images))
    {

        printf("images is not an array\r\n");

        cbor_decref(&decoded_item);

        return -1;
    }

    printf("Number of images: %zu\r\n",
           cbor_array_size(images));

    /* =========================================================
     * Iterate images
     * ========================================================= */

    for (size_t i = 0; i < cbor_array_size(images); i++)
    {

        cbor_item_t *image =
            cbor_array_get(images, i);

        if (image == NULL ||
            !cbor_isa_map(image))
        {

            continue;
        }

        struct cbor_pair *image_pairs =
            cbor_map_handle(image);

        size_t image_map_size =
            cbor_map_size(image);

        uint64_t slot = 0;

        bool active = false;

        bool slot_found = false;

        bool active_found = false;

        cbor_item_t *hash_value = NULL;

        cbor_item_t *version_value = NULL;

        /* =====================================================
         * Search fields inside image
         * ===================================================== */

        for (size_t j = 0; j < image_map_size; j++)
        {

            cbor_item_t *key =
                image_pairs[j].key;

            cbor_item_t *value =
                image_pairs[j].value;

            if (!cbor_isa_string(key))
                continue;

            size_t key_len =
                cbor_string_length(key);

            char *key_str =
                (char *)cbor_string_handle(key);

            /* -------------------------------
             * slot
             * ------------------------------- */

            if (key_len == 4 &&
                memcmp(key_str, "slot", 4) == 0)
            {

                if (cbor_isa_uint(value))
                {

                    slot =
                        cbor_get_uint64(value);

                    slot_found = true;
                }
            }

            /* -------------------------------
             * version
             * ------------------------------- */

            else if (key_len == 7 &&
                     memcmp(key_str, "version", 7) == 0)
            {

                version_value = value;
            }

            /* -------------------------------
             * active
             * ------------------------------- */

            else if (key_len == 6 &&
                     memcmp(key_str, "active", 6) == 0)
            {

                if (cbor_is_bool(value))
                {

                    active =
                        cbor_get_bool(value);

                    active_found = true;
                }
            }

            /* -------------------------------
             * hash
             * ------------------------------- */

            else if (key_len == 4 &&
                     memcmp(key_str, "hash", 4) == 0)
            {

                hash_value = value;
            }
        }

        /* =====================================================
         * Print image information
         * ===================================================== */

        printf("\r\n-----------------------------\r\n");

        if (slot_found)
        {

            printf("Slot    : %llu\r\n",
                   (unsigned long long)slot);
        }

        if (version_value != NULL &&
            cbor_isa_string(version_value))
        {

            printf("Version : %.*s\r\n",
                   (int)cbor_string_length(version_value),
                   (char *)cbor_string_handle(version_value));
        }

        if (active_found)
        {

            printf("Active  : %s\r\n",
                   active ? "true" : "false");
        }

        /* =====================================================
         * Find candidate/new image
         * ===================================================== */

        if (active_found &&
            active == false)
        {

            printf("\r\n*** CANDIDATE IMAGE FOUND ***\r\n");

            if (hash_value == NULL)
            {

                printf("Hash not found!\r\n");

                continue;
            }

            if (!cbor_isa_bytestring(hash_value))
            {

                printf("Hash is not a byte string!\r\n");

                continue;
            }

            size_t hash_len =
                cbor_bytestring_length(hash_value);

            unsigned char *hash =
                cbor_bytestring_handle(hash_value);

            printf("Hash length: %zu\r\n",
                   hash_len);

            printf("Hash: ");

            for (size_t k = 0; k < hash_len; k++)
            {

                printf("%02X", hash[k]);

                if (k < hash_len - 1)
                    printf(" ");
            }

            printf("\r\n");

            /*
             * If you need the hash after this function,
             * COPY it into your own buffer here.
             */

            if (hash_len == 32)
            {

                // uint8_t new_image_hash[32];

                memcpy(new_image_hash,
                       hash,
                       32);

                printf("32-byte hash copied successfully\r\n");
            }

            /* We found the candidate image */
            break;
        }
    }

    /* =========================================================
     * Cleanup
     * ========================================================= */

    cbor_decref(&decoded_item);

    // return 0;


    //-----------------------------------------------------------

    // using the hash to conifrm the image and then reset the module to load new firmware

    
    size_t confirm_frame_len = 0;
    uint8_t *confirm_frame = NULL;

    // smp_packet_t *echo_packet = echo_function();            //--------------> for
    // smp_packet_t *echo_packet = img_list();
    // smp_packet_t* echo_packet = mcu_reset() ;
    smp_packet_t* confirm_packet = mcu_confirm_img(new_image_hash);

    build_seial_packet(&confirm_frame, &confirm_frame_len, confirm_packet);
    printf("final_len : %zu \n", confirm_frame_len);

    serial_send_bytes(&MY_UART, confirm_frame, confirm_frame_len);

    sleep(5); 

    printf("Reset command start\r\n");

    size_t reset_frame_len = 0;
    uint8_t *reset_frame = NULL;

    // smp_packet_t *echo_packet = echo_function();            //--------------> for
    // smp_packet_t *echo_packet = img_list();
    smp_packet_t* reset_packet = mcu_reset() ;
    // smp_packet_t* echo_packet = mcu_confirm_img();

    build_seial_packet(&reset_frame, &reset_frame_len, reset_packet);
    printf("final_len : %zu \n", reset_frame_len);

    serial_send_bytes(&MY_UART, reset_frame, reset_frame_len);
    
    


    //--------------------------------------------------------------------------------------------

    /*
     * Send this to UART:
     *
     * write(fd, final_frame, final_len);
     */

    // size_t final_len = 0 ;
    // uint8_t *final_frame = NULL;

    // // smp_packet_t *echo_packet = echo_function();
    // // smp_packet_t* echo_packet = img_list() ;
    // // smp_packet_t* echo_packet = mcu_reset() ;
    // smp_packet_t* echo_packet = mcu_confirm_img() ;

    // build_seial_packet(&final_frame, &final_len, echo_packet);
    // printf("final_len : %zu \n", final_len);

    // if(uart_begin(&MY_UART,_SERIAL_DEV_,115200,&my_uart_callback_function) != _TRUE_)
    // {
    //     printf("Error : uart_begin failed\r\n");
    //     return 0;
    // }

    // while(1)
    // {
    // sleep(1);
    //     serial_send_bytes(&MY_UART, final_frame,final_len);
    //     sleep(10);
    // }

    return 0;
}
