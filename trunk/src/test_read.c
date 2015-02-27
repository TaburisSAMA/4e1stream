/*
 * =====================================================================================
 *
 *       Filename:  test_read.c
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  2014年07月08日 11时00分18秒
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  LYN (), taburissama@gmail.com
 *        Company:  bqvision
 *
 * =====================================================================================
 */

#include <poll.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdint.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <errno.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <tea/tea.h>

#include <v804_drivers.h>

//#define fpga_read(x)            readw((unsigned long)(fpga_base)+(x))
#define fpga_readw(base,x) (*(volatile unsigned short *)((unsigned long)base + (x)))
#define fpga_writew(base,x,val) (*(volatile unsigned short *)((unsigned long)base + (x)) = val)

#if 0
struct stream_handle_session 
{
    int bindid;

    int packet_size;
    int irq_threshold;
    unsigned short config;
    unsigned short tx_rate;

    uint32_t ssrc;
    uint16_t seq;

    volatile uint32_t packet_recv;
    volatile uint32_t packet_sent;
    volatile uint32_t packet_send_drop;
    volatile uint32_t packet_recv_drop;
    volatile uint32_t tscount;
    volatile uint32_t intr_status;
    volatile uint32_t overflow;
    volatile uint32_t waitfailed;

    int ts_fd;
    void *dma_buf;
    void *fpga_base;
};

static struct stream_handle_session *session = NULL;
#endif

static struct ChannelTransfer *channel_recv = NULL;
static struct ChannelTransfer *channel_send = NULL;

void dump_mem(void *p, int size)
{
    int i, j;

    for (i = 0; i <= size / 10; i++ )
    {
        printf("%lx: ", (unsigned long)p);

        for (j = 0; j < 10; j++)
            printf(" %4x", *(volatile unsigned char *)p++);

        printf("\n");
    }

}

int main(int args, char* argv[])
{
    int fd = -1;
    void *fpga_base, *buff, *writebuff;
    int i;
//    int j;
//    unsigned int choice;
    int r;
    int tscount;
    //int ts_flag;
//    int bufsize;
//    int payload_len; 
//    unsigned char *p;
    int tmp;

//    session = calloc(1, sizeof(stream_handle_session));
    channel_send = malloc(sizeof(struct ChannelTransfer));
    channel_recv = malloc(sizeof(struct ChannelTransfer));

    fd = open("/dev/fpga-0", O_RDWR);
    if(fd < 0)
    {
        printf("%s %d: Cannot open fpga-0 \n", __FUNCTION__, __LINE__);
        return -1;
    }

    buff = mmap(NULL, POOLSIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (buff == MAP_FAILED)
    {
        printf("%s %d: mmap /dev/fpga-0 failed!!!\n", __FUNCTION__, __LINE__);
        return -1;
    }
    writebuff = buff + (POOLSIZE>>1);

    fpga_base = mmap(NULL, FPGA_PHY_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, POOLSIZE);
    if (fpga_base == MAP_FAILED)
    {
        printf(" mmap /dev/fpga-0 failed!!!\n");
        return -1;
    }
#if 0
    volatile unsigned short *reg_data = (volatile unsigned short *)((unsigned long)fpga_base + FPGA_ADDR_DATA+0x40);
    while(1)
    {
        tscount = fpga_readw(fpga_base, 0x82);
        if(tscount == 0 )
        {
            continue;
        }
#if 1
        if(tscount > 512)
        {
            channel_recv->packsize = tscount;
            channel_recv->chn = 2;
            r = ioctl(fd, FPGA_DMA_IOC_GET_FRAME, channel_recv);
            if (r)
            {
                if (r == TEA_RSLT_AGAIN)
                    return TEA_RSLT_SUCCESS;
                else
                    BUG("didn't get data from fpga! \n");

                return TEA_RSLT_SUCCESS;
            }

            bufsize = channel_recv->packsize;
        }
#endif
        else
        {
            channel_recv->packsize = tscount;
            channel_recv->chn = 2;
            r = ioctl(fd, FPGA_DMA_IOC_GET_FRAME, channel_recv);
            if (r)
            {
                Debug("GET DATA failed \n");
                return TEA_RSLT_SUCCESS;
            }

            bufsize = channel_recv->packsize;
        }

        payload_len = bufsize;
        unsigned short *p = buff;
        int txcount, tmp;
//        printf("---------start---------\n");
//        dump_mem(buff, bufsize);
//        printf("------------------\n");
//        memcpy(writebuff, (char *)p, bufsize<<1);
//        channel_send->packsize = bufsize;
//        channel_send->chn = 2;
//        ioctl(fd, FPGA_DMA_IOC_SEND_FRAME, channel_send);
//        dump_mem(writebuff, channel_send->packsize);
//        printf("---------finish---------\n");
#if 1
        while (payload_len > 0)
        {
#define MAX_FIFO (0x2000)
            tmp = fpga_readw(fpga_base, FPGA_ADDR_TXCOUNT + 0x80);

            txcount = MAX_FIFO - tmp;

            channel_send->packsize = (payload_len > txcount) ? txcount: payload_len;
            channel_send->chn = 2;

            if (channel_send->packsize > 0)
            {
                memcpy(writebuff, (char*)p, channel_send->packsize<<1);
                
                r = ioctl(fd, FPGA_DMA_IOC_SEND_FRAME, channel_send);
                if (r)
                {
                    BUG("ioctl : TS_DMA_IOC_SEND_FRAME, failed!!!!!!!!\n");
                    return -1;
                }
                p += channel_send->packsize;
            }
            else
            {
                tmp = channel_send->packsize;
                while (tmp--)
                {
                    *reg_data = *p++;
                }
            }
            payload_len -= channel_send->packsize;
        }
#endif
    }
#endif

#if 1
    tscount = fpga_readw(fpga_base, 0x02);
    printf("tscount=%d\n", tscount);
    if (tscount > 0)
    {
        for (i=0; i<tscount; i++)
        {
            tmp = fpga_readw(fpga_base, 0x00);
        }
    }
    tmp = fpga_readw(fpga_base, 0x06);
    printf("0x06reg=%d\n", tmp);
    tmp = fpga_readw(fpga_base, 0x04);
    printf("0x04reg=%d\n", tmp);
    tmp = fpga_readw(fpga_base, 0x100);
    printf("0x100reg=%04x\n", tmp);
    tscount = fpga_readw(fpga_base, 0x02);
    do{
        printf("tscount=%d\n", tscount);
        usleep(10000);  
    }while((tscount=fpga_readw(fpga_base, 0x02))<2048);
#endif    
#if 1//驱动直接读写，内部dma，误码仪测试通过
    r = ioctl(fd, FPGA_DMA_IOC_TEST_LOOP, channel_recv);
#endif

#if 0   /* 误码仪测试读写 通过 */
    printf("fpga_read = 0x%x\n", fpga_readw(fpga_base, 0x42));
    printf("fpga_read = 0x%x\n", fpga_readw(fpga_base, 0x46));
    while (1)
    {
        r = fpga_readw(fpga_base, 0x42);
        if(r)
        {
                tmp = fpga_readw(fpga_base, 0x40);
                fpga_writew(fpga_base, 0x40, tmp);
//            printf("tmp = 0x%x\n", tmp);
        }
    }
#endif
#if 0 /* FPGA内部自环的DMA测试 通过 */
#define BUFSIZE 4096
    channel_recv->packsize = BUFSIZE;
    channel_recv->chn = 0;

    channel_send->packsize = BUFSIZE;
    channel_send->chn = 0;
    for(i = 0; i < 100; i++)
    {
        p = writebuff;
        printf("-------start--------\n");
        for(j = 0; j < BUFSIZE<<1; j++)
        {
            *(unsigned char *)p++ = j;
        }
        dump_mem(writebuff, BUFSIZE);
        ioctl(fd, FPGA_DMA_IOC_SEND_FRAME, channel_send);
        ioctl(fd, FPGA_DMA_IOC_GET_FRAME, channel_recv);
        printf("-----------------------\n");
        dump_mem(buff + 2, BUFSIZE);
        tmp = memcmp((unsigned char *)buff + 2, writebuff, BUFSIZE);
        if(tmp)
        {
            printf("tmp = %d, i = %d\n", tmp, i);
        }
        printf("--------end----------\n");
    }
#endif
#if 0
    session->ts_fd      = fd;
    session->dma_buf    = buff;
    session->fpga_base  = fpga_base;
    session->packet_size = 256;
#endif
#if 0
    choice = TEST_DMA_LOOP;
    r = ioctl(fd, FPGA_DMA_IOC_TEST, &choice);
    if(r)
    {
        perror("test_dma_loop failed");
    }
#endif
#if 0
    printf("fpga_read = 0x%x\n", fpga_readw(fpga_base, 0xa0<<1));
    choice = TEST_DMA_READ;
    r = ioctl(fd, TS_DMA_IOC_TEST, &choice);
    if(r)
    {
        perror("test_dma_read failed");
    }
#endif
#if 0
    choice = TEST_DMA_WRITE;
    ioctl(fd, FPGA_DMA_IOC_TEST, &choice);
    if(r)
    {
        perror("test_dma_write failed");
    }
#endif  
    free(channel_recv);
    free(channel_send);
    close(fd);
    return 0;
}
