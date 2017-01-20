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
#include <signal.h>

#define OFFSET_512K 		(19)
#define OFFSET_1G 		(30)
#define OFFSET_1T 		(40)
#define BASE_DATA_BLOCK_SIZE 	(1 << OFFSET_512K)			//512K
#define ARRAY_LEN		(BASE_DATA_BLOCK_SIZE >> 2)		//512K/4Byte
#define MAX_DATA_BLOCK_NUM	(1 << (OFFSET_1T - OFFSET_512K)) 	//MAX FileSize=1T

#define TRUE (1)
#define FALSE (0)

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
    REQUEST_TYPE_BUTT
}REQUEST_TYPE;

uint32_t g_FileSize;
uint32_t g_Rate_rw, g_Rate_hot;
uint64_t g_TestTime;
uint64_t g_TestStartTime;
uint64_t g_DataBlockNum;
uint32_t g_BlockSize;	//k
uint32_t g_BlockSize_Byte;

unsigned short g_seed[3];
uint8_t g_DataBlockAttr[MAX_DATA_BLOCK_NUM];
uint8_t g_DataDisMode;                       //data distribution mode
char g_FilePath[PATH_MAX];

int g_Fd;

typedef struct Stat
{
    uint32_t proc_blk_cnt[REQUEST_TYPE_BUTT];
}Stat;

Stat g_Stat;

typedef enum THREAD_TYPE
{
    MONITOR_THRD,
    DO_TEST_THRD,
    THREAD_TYPE_BUTT
}THREAD_TYPE;

pthread_t g_Thread[THREAD_TYPE_BUTT];

typedef struct BaseDataBlock
{
    uint32_t arr[ARRAY_LEN];
}BaseDataBlock;

char* g_pTemplate[DATA_TYPE_BUTT];
char* g_pBuff; //for data read

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

    g_seed[0] = (unsigned short)tv.tv_usec;
    g_seed[1] = (unsigned short)tv.tv_usec >> 10;
    g_seed[2] = (unsigned short)tv.tv_sec;

}

uint32_t get_random_num(uint32_t range)
{
    if(0 == range)
    {
        printf("ERR: range == 0\n");
        return 0;
    }

    if(0 == g_DataDisMode)
    {
        init_rand_seed();
    }

    return (nrand48(g_seed) % range);
}

int init_data_template()
{
   uint32_t bcnt, i;
  if(NULL == (g_pBuff = (char*)calloc(g_BlockSize, sizeof(BaseDataBlock)))) 
  {
      printf("Alloc read buffer fail!\n");
      return OP_ERROR;
  }

  DATA_TYPE dataType;
  for(dataType = 0; dataType < DATA_TYPE_BUTT; dataType++)
  {
      g_pTemplate[dataType] = (char*)calloc(g_BlockSize, sizeof(BaseDataBlock));
      if(NULL == g_pTemplate[dataType])
      {
          printf("Alloc write template fail! dataType=%d\n", dataType);
          return OP_ERROR;
      }
  }

  //init data content
  BaseDataBlock *pHotBlock, *pColdBlock;
  pHotBlock = (BaseDataBlock*)g_pTemplate[HOT_DATA];
  pColdBlock = (BaseDataBlock*)g_pTemplate[COLD_DATA];

  for(bcnt = 0; bcnt < g_BlockSize; bcnt++)
  {
      for(i = 0; i < ARRAY_LEN; i++)
      {
          pHotBlock->arr[i] = i;
          pColdBlock->arr[i] = (i << 1);
      }
      pHotBlock++;
      pColdBlock++;
  }

  return OP_OK;
}

void init_data_block(DATA_TYPE dataType, uint32_t blkIdx)
{
    g_DataBlockAttr[blkIdx] = dataType;
#ifdef INIT_FILE
    if(-1 == write(g_Fd, g_pTemplate[dataType], sizeof(BaseDataBlock)))
    {
        printf("Block %d Write Err:%s\n", blkIdx, strerror(errno));
    }
#endif
    printf(" %d", dataType);
}

int init_data()
{
    g_DataBlockNum = ((uint64_t)g_FileSize<<30)/(g_BlockSize * sizeof(BaseDataBlock)); 

    if(g_DataBlockNum > MAX_DATA_BLOCK_NUM)
    {
        printf("Illegal DataBlockNum:%d, legal range[1, %d]\n", g_DataBlockNum, MAX_DATA_BLOCK_NUM);
        return OP_ERROR;
    }

    init_rand_seed();
    if(OP_ERROR == init_data_template())
    {
        return OP_ERROR;
    }

    if(0 != posix_memalign((void**)&g_pTemplate, 1024, g_BlockSize_Byte * DATA_TYPE_BUTT))
    {
        printf("Posix_memalign failed!");
        return OP_ERROR;
    }

    g_Fd = open(g_FilePath, O_RDWR | O_CREAT | O_SYNC, 0666);
    if(-1 == g_Fd)
    {
        printf("Open file fail! Err = %s, filePath = %s\n", strerror(errno), g_FilePath);
        return OP_ERROR;
    }
    
    //init data block
    uint64_t i;
    int hot_block_num = 0;
    DATA_TYPE dataType;
    int block_num[DATA_TYPE_BUTT] = {0};

    memset(g_DataBlockAttr, 0, sizeof(g_DataBlockAttr));
    if(100 == g_Rate_hot)
    {
        //pure hot data
        for(i = 0; i < g_DataBlockNum; i++)
        {
            dataType = HOT_DATA;
            init_data_block(dataType, i);
        }
        block_num[HOT_DATA] = g_DataBlockNum;
    }
    else
    {
        //cold hot data mixed
        for(i = 0; i < g_DataBlockNum; i++)
        {
            dataType = (get_random_num(100) <= g_Rate_hot) ? HOT_DATA : COLD_DATA;
            init_data_block(dataType, i);
            block_num[dataType] += 1;
        }
    }

    printf("\n Set data block attribute finish!\n blockCount:%ld hotBlock:%ld hotDataRate:%f\n",
                    g_DataBlockNum,
                    block_num[HOT_DATA],
                    (double)block_num[HOT_DATA] / (double)g_DataBlockNum);
    return OP_OK;
}

int init_param(char** argv)
{
    g_FileSize = atoi(argv[1]);
    g_Rate_rw = atoi(argv[2]);
    g_Rate_hot = atoi(argv[3]);
    g_TestTime = atoi(argv[4]);
    g_DataDisMode = atoi(argv[5])%2;

    g_BlockSize = atoi(argv[6]);
    g_BlockSize_Byte = g_BlockSize * sizeof(BaseDataBlock);

    memset(g_FilePath, 0, sizeof(g_FilePath));
    strncpy(g_FilePath, argv[7], sizeof(g_FilePath));

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

    printf("FileSize:%d \nRate_rw:%d \nRate_hot:%d \nTestTime:%d \nDataDisMode:%d \nBlockSize:%dk \nFilePath:%s\n",
            g_FileSize,
            g_Rate_rw,
            g_Rate_hot,
            g_TestTime,
            g_DataDisMode,
            g_BlockSize,
            g_FilePath);

    return OP_OK;
}

REQUEST_TYPE get_request_type()
{
    return (get_random_num(100) <= g_Rate_rw) ? READ_REQ : WRITE_REQ;
}

uint32_t get_data_block_index()
{
    DATA_TYPE dataType = (get_random_num(100) >= 20) ? HOT_DATA : COLD_DATA;
    uint32_t blkIdx = get_random_num(g_DataBlockNum);
    uint32_t i;

    if(g_DataBlockAttr[blkIdx % MAX_DATA_BLOCK_NUM] == dataType)
    {
        return blkIdx;
    }

    for(i = blkIdx + 1; i < g_DataBlockNum; i++)
    {
        if(g_DataBlockAttr[i % MAX_DATA_BLOCK_NUM] == dataType)
        {
            return i;
        }
    }

    return 0;
}

void write_data()
{
    uint32_t blkIdx = get_data_block_index();
    int ret = 0;
    off_t offset;

#ifdef RANDOM_IDX_SHOW
    printf("w_idx:%ld\n", blkIdx);
#endif

    ret = lseek(g_Fd, sizeof(BaseDataBlock) * blkIdx, SEEK_SET);
    if(-1 == ret)
    {
        printf("Lseek file fail! Err:%s blockIdx:%d \n", strerror(errno), blkIdx);
        return;
    }

    DATA_TYPE dataType = g_DataBlockAttr[blkIdx];

    ret = write(g_Fd, g_pTemplate[dataType], g_BlockSize_Byte);
    if(-1 == ret)
    {
        printf("Write data fail! blockIdx=%d Err:%s\n");
        return;
    }
}

void read_data()
{
    uint32_t blkIdx = get_data_block_index();
    int ret;
    off_t offset;
#ifdef RANDOM_IDX_SHOW
    printf("r_idx:%ld \n", blkIdx);
#endif

    ret = lseek(g_Fd, sizeof(BaseDataBlock) * blkIdx, SEEK_SET);
    if(-1 == ret)
    {
        printf("Lseek file fail! Err:%s blockIdx:%d\n", strerror(errno), blkIdx);
        return;
    }

    DATA_TYPE dataType = g_DataBlockAttr[blkIdx];

    ret = read(g_Fd, g_pBuff, g_BlockSize * sizeof(BaseDataBlock));
    if(-1 == ret)
    {
        printf("Lseek file fail! Err:%s blockIdx:%ld \n", strerror(errno), blkIdx);
    }
    else
    {
        uint32_t i, arrIdx, data;
        uint64_t chkSum = 0;
        BaseDataBlock *pBlock = (BaseDataBlock*)g_pBuff;

        for(i = 0; i < g_BlockSize; i++)
        {
            for(arrIdx = 0; arrIdx < ARRAY_LEN; arrIdx++)
            {
#ifdef SHOW_READ_DATA
                printf("DataType:%d arr[%d]=%d \n",dataType, arrIdx, pBlock->arr[arrIdx]);
#endif
                data = pBlock->arr[arrIdx];
            }
            pBlock++;
        }
    }
}

int timesup()
{
    uint64_t now = get_curr_time();

    if(now - g_TestStartTime > g_TestTime*1000)
    {
        printf("Now:%ld start:%ld delta:%ld testTime:%ld \n",
                        now,
                        g_TestStartTime,
                        now - g_TestStartTime,
                        g_TestTime);
        return TRUE;
    }
    return FALSE;
}

void *do_test()
{
    REQUEST_TYPE request;
    memset(&g_Stat, 0, sizeof(Stat));

    do
    {
        request = get_request_type();
        switch(request)
        {
            case WRITE_REQ:
                write_data();
                break;

            case READ_REQ:
                read_data();
                break;

            default:
                printf("Illegal request type[%d]!", request);
        }

        g_Stat.proc_blk_cnt[request]++;
        if(timesup())
        {
            break;
        }

    }while(TRUE);
}

void* do_monitor()
{
    Stat last_stat;
    memset(&last_stat, 0, sizeof(Stat));

    do
    {
        REQUEST_TYPE type;
        double rwKib[REQUEST_TYPE_BUTT];

        for(type = 0; type < REQUEST_TYPE_BUTT; type++)
        {
            uint32_t increaseBlkCnt = g_Stat.proc_blk_cnt[type] - last_stat.proc_blk_cnt[type];
            rwKib[type] = (double)(increaseBlkCnt * g_BlockSize_Byte)/(double)(1 << 20);
        }

        printf("Monitoring ... read:%.2fM write:%.2fM \n", rwKib[READ_REQ], rwKib[WRITE_REQ]);
        memcpy(&last_stat, &g_Stat, sizeof(Stat));
        sleep(1);

        if(timesup())
        {
            printf("Time's up!\n");
            break;
        }
    }while(TRUE);
}

void create_thread()
{
    int ret = 0;
    memset(g_Thread, 0, sizeof(g_Thread));

    if(0!= pthread_create(&g_Thread[DO_TEST_THRD], NULL, do_test, NULL))
    {
        printf("Start testing fail!\n");
    }
    else
    {
        printf("Start testing success!\n");
    }

    if(0!= pthread_create(&g_Thread[MONITOR_THRD], NULL, do_monitor, NULL))
    {
        printf("Start monitoring fail!\n");
    }
    else
    {
        printf("Start monitoring success!\n");
    }
}

void wait_threads()
{
    THREAD_TYPE type;
    for(type = 0; type < THREAD_TYPE_BUTT; type++)
    {
        if(g_Thread[type] != 0)
        {
            pthread_join(g_Thread[type], NULL);
        }
    }
}

void print_result()
{
    uint64_t spendTime = (get_curr_time() - g_TestStartTime)/1000UL;

    REQUEST_TYPE type;
    double rwKib[REQUEST_TYPE_BUTT];

    for(type = 0; type < REQUEST_TYPE_BUTT; type++)
    {
        rwKib[type] = (double)((g_Stat.proc_blk_cnt[type] * g_BlockSize_Byte) >> 20)/(double)spendTime;
    }

    printf("Result - readBlkCnt:%lu writeBlkCnt:%lu read:%.2fM write:%.2fM \n",
                    g_Stat.proc_blk_cnt[READ_REQ],
                    g_Stat.proc_blk_cnt[WRITE_REQ],
                    rwKib[READ_REQ],
                    rwKib[WRITE_REQ]);
}

void stop()
{
    print_result();
    close(g_Fd);
    DATA_TYPE dataType;

    for(dataType = 0; dataType < DATA_TYPE_BUTT; dataType++)
    {
        free(g_pTemplate[dataType]);
    }
    free(g_pBuff);

    _exit(0);
}

int main(int argc, char** argv)
{
    if(8 != argc)
    {
        printf("Useage: data_rw_test FileSize(G) Rate_read Rate_hot TestTime(s) DataDisMode[0,1] BlockSize(K) FilePath\n");
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

    signal(SIGINT, stop);
    g_TestStartTime = get_curr_time();

    create_thread();
    wait_threads();
    close(g_Fd);

    print_result();

    return 0;
}







