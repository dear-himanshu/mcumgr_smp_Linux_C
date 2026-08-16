#ifndef ENC_DEC_FUN_H
#define ENC_DEC_FUN_H


//void encode(void); 
#include "app_stack_uart.h"
#include <stdint.h>
 
// max buffer size
#define MAX_BUFFER_SIZE 1024
 
typedef struct 
{
    uint8_t op_ver_res;
    uint8_t flags;
    uint16_t data_len;
    uint16_t group_id;
    uint8_t seq_num;
    uint8_t cmd_id;
} smp_header_t;
 
typedef struct 
{
    smp_header_t header;
    uint8_t * data;
} smp_packet_t;
 
enum mcumgr_op_t
{
    MGMT_OP_READ,
    MGMT_OP_READ_RSP,
    MGMT_OP_WRITE,
    MGMT_OP_WRITE_RSP,
    MGMT_OP_COUNT
};
 
enum mcumgr_group_t {
    MGMT_GROUP_ID_OS,
    MGMT_GROUP_ID_IMAGE,
    MGMT_GROUP_ID_STAT,
    MGMT_GROUP_ID_SETTING,
    MGMT_GROUP_ID_LOG,
    MGMT_GROUP_ID_CRASH,
    MGMT_GROUP_ID_SPLIT,
    MGMT_GROUP_ID_RUN,
    MGMT_GROUP_ID_FS,
    MGMT_GROUP_ID_SHELL,
    MGMT_GROUP_ID_ENUM,
    MGMT_GROUP_ID_PERUSER,
    ZEPHYR_MGMT_GRP_BASIC
};
 

int image_smp_start(void); 
void test_fun(); 

#endif // ENC_DEC_FUN_H