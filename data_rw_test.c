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
#define OFFSET_1G 		(30)
#define OFFSET_1T 		(40)
#define BASE_DATA_BLOCK_SIZE 	(1 << OFFSET_512K)			//512K
#define ARRAY_LEN		(BASE_DATA_BLOCK_SIZE >> 2)		//512K/4Byte
#define MAX_DATA_BLOCK_NUM	(1 << (OFFSET_1T - OFFSET_512K)) 	//MAX FileSize=1T

typedef enum OP_RESULT
{
    OP_ERROR,
    OP_OK,
    OP_RESULT_BUTT
}OP_RESULT;

typedef enum DATA_TYPE
{
    COLD_DATA,
    HOT_DATA,
    DATA_TYPE_BUTT
}DATA_TYPE;

typedef enum REQUEST_TYPE
{
    READ_REQ,
    WRITE_REQ,
    REQ_TYPE_BUTT
}REQUEST_TYPE;

uint32_t g_FileSize;
uint32_t g_Rate_rw, g_Rate_hot;
uint32_t g_TestTime;
uint32_t g_DataBlockNum;

unsigned short g_seed[3];
uint8_t g_DataBlockAttr[MAX_DATA_BLOCK_NUM];
uint8_t g_DataDisMode;                       //data distribution mode
char g_FilePath[PATH_MAX];

typedef struct BaseDataBlock
{
    uint32_t data[ARRAY_LEN];
}BaseDataBlock;

BaseDataBlock g_Template[DATA_TYPE_BUTT];

uint64_t get_curr_time()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);

    return (tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

void init_rand_seed()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);

    g_seed[2] = (unsigned short)tv.tv_usec;
    g_seed[1] = (unsigned short)tv.tv_usec >> 10;
    g_seed[0] = (unsigned short)tv.tv_sec;

}

uint32_t get_random_num(uint32_t range)
{
    if(0 == g_DataDisMode)
    {
        init_rand_seed();
    }

    return (nrand48(g_seed) % range);
}

void init_data_template()
{
    uint32_t i;
    for(i = 0; i < ARRAY_LEN; i++)
    {
        g_Template[COLD_DATA].data[i] = i;
        g_Template[HOT_DATA].data[i] = i << 1;
    }
}

void init_data_block(int fd, DATA_TYPE dataType, uint32_t blkIdx)
{
    g_DataBlockAttr[blkIdx] = dataType;

    if(-1 == write(fd, &g_Template[dataType], sizeof(BaseDataBlock)))
    {
        printf("Block %d Write Err:%s\n", blkIdx, strerror(errno));
    }

    printf(" %d", dataType);
}

int init_file()
{
    uint32_t i;
    DATA_TYPE dataType;
    int block_num[DATA_TYPE_BUTT] = {0};

    int fd = open(g_FilePath, O_RDWR | O_APPEND | O_CREAT, 0666);
    {
        if(-1 == fd)
        {
            printf("Open file fail! Err:%s Path=%s\n", strerror(errno), g_FilePath);
            return OP_ERROR;
        }
    }

    //init data block
    memset(g_DataBlockAttr, 0, sizeof(g_DataBlockAttr));

    if(100 > g_Rate_hot)
    {
        for(i = 0; i < g_DataBlockNum; i++)
        {
            dataType = (get_random_num(100) <= g_Rate_hot) ? HOT_DATA : COLD_DATA;
            init_data_block(fd, dataType, i);
            block_num[dataType] += 1;
        }
    }
    else
    {
        for(i = 0; i < g_DataBlockNum; i++)
        {
            dataType = HOT_DATA;
            init_data_block(fd, dataType, i);
        }
        block_num[HOT_DATA] = g_DataBlockNum;
        block_num[COLD_DATA] = 0;
    }

    printf("HotDataRate=%f\n", (double)block_num[HOT_DATA]/(double)g_DataBlockNum);
    close(fd);

    return OP_OK;
}

int init_data()
{
    g_DataBlockNum = (g_FileSize >> OFFSET_1G) / sizeof(BaseDataBlock);

    if(g_DataBlockNum > MAX_DATA_BLOCK_NUM)
    {
        printf("Illegal DataBlockNum:%d, legal range[1, %d]\n", g_DataBlockNum, MAX_DATA_BLOCK_NUM);
        return OP_ERROR;
    }

    init_rand_seed();
    init_data_template();
    
    return init_file();
}

int init_param(char** argv)
{
    g_FileSize = atoi(argv[1]);
    g_Rate_rw = atoi(argv[2]);
    g_Rate_hot = atoi(argv[3]);
    g_TestTime = atoi(argv[4]);
    g_DataDisMode = atoi(argv[5]);

    memset(g_FilePath, 0, sizeof(g_FilePath));
    strncpy(g_FilePath, argv[6], sizeof(g_FilePath));

    if(g_Rate_rw > 100 || g_Rate_hot > 100)
    {
        printf("Illegal rate Rate_rw:%d Rate_hot:%d, legal range[, 100]\n", g_Rate_rw, g_Rate_hot);
        return OP_ERROR;
    }

    if(g_FileSize > 1024)
    {
        printf("File too large(%dG), legal range is [1, 1024]G\n", g_FileSize);
        return OP_ERROR;
    }

    if(g_TestTime == 0)
    {
        printf("Illegal TestTime, must larger than 0\n");
        return OP_ERROR;
    }

    printf("FileSize:%d \nRate_rw:%d \nRate_hot:%d \nTestTime:%d \nDataDisMode:%d \nFilePath:%s\n",
            g_FileSize,
            g_Rate_rw,
            g_Rate_hot,
            g_TestTime,
            g_DataDisMode,
            g_FilePath);

    return OP_OK;
}

REQUEST_TYPE get_request_type()
{
    return WRITE_REQ;
}

uint32_t get_data_block_index()
{
    DATA_TYPE dataType = (get_random_num(100) >= g_Rate_hot) ? HOT_DATA : COLD_DATA;
    uint32_t blkIdx = get_random_num(g_DataBlockNum);
    uint32_t i;

    if(g_DataBlockAttr[blkIdx % MAX_DATA_BLOCK_NUM] == dataType)
    {
        return blkIdx;
    }

    for(i = blkIdx + 1; i < g_DataBlockNum; i++)
    {
        if(g_DataBlockAttr[blkIdx % MAX_DATA_BLOCK_NUM] == dataType)
        {
            return i;
        }
    }

    return 0;
}

void write_data()
{
    uint32_t blkIdx = get_data_block_index();
}

int main(int argc, char** argv)
{
    REQUEST_TYPE request;
    uint64_t start;

    if(7 != argc)
    {
        printf("Useage: data_rw_test FileSize(G) Rate_rw Rate_hot TestTime(s) DataDisMode FilePath\n");
        return 0;
    }

    if(OP_ERROR == init_param(argv))
    {
        return 0;
    }

    if(OP_ERROR == init_data())
    {
        return 0;
    }

    start = get_curr_time();

    do
    {
        request = get_request_type();
        switch(request)
        {
            case WRITE_REQ:
                write_data();
                break;

            default:
                printf("Illegal request_type\n");
        }

        if(get_curr_time() - start > (g_TestTime << 10))
        {
            break;
        }
    }while(1);

    return 0;
}







