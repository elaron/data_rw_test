#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#define OFFSET_512K 		(19)
#define OFFSET_1T 		(40)
#define BASE_DATA_BLOCK_SIZE 	(1 << OFFSET_512K)			//512K
#define ARRAY_LEN		(BASE_DATA_BLOCK_SIZE >> 2)		//512K/4Byte
#define MAX_DATA_BLOCK_NUM	(1 << (OFFSET_1T - OFFSET_512K)) 	//MAX FileSize=1T

typedef enum OP_RESULT
{
	OP_ERROR,
	OP_OK,
	OP_RESULT_BUTT
};

typedef enum DATA_TYPE
{
    COLD_DATA,
    HOT_DATA,
    DATA_TYPE_BUTT
};

typedef enum REQUEST_TYPE
{
    READ_REQ,
    WRITE_REQ,
    REQ_TYPE_BUTT
};

uint32_t g_FileSize;
uint32_t g_Rate_rw, g_Rate_hot;
uint32_t g_TestTime;
uint32_t g_DataBlockNum;
uint32_t g_HotBlockNum;

unsigned short g_seed[3];
uint8_t g_DataBlockAttr[MAX_DATA_BLOCK_NUM];
uint8_t g_DataDisMode; //data distribution mode
char g_FilePath[PATH_MAX];

typedef struct BaseDataBlock
{
    uint32_t data[ARRAY_LEN];
};

BaseDataBlock g_Template[DATA_TYPE_BUTT];

uint64_t get_curr_time()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);

    return (tv.tv_sec * 1000 + tv.tv_usec / 1000);
}


