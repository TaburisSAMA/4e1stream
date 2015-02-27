/*
 * =====================================================================================
 *
 *       Filename:  dma_app.c
 *
 *    Description:  TS application under Tea framework
 *
 *        Version:  1.0
 *        Created:  2010年03月03日 09时45分14秒
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  TangYiyi (), tangyiyi008@gmail.com
 *        Company:  bqvision
 *
 * =====================================================================================
 */

#include <tea/tea.h>
#include <misc-app/timer.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <poll.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <v804_drivers.h>

#define RTP_LITTLE_ENDIAN 1
#include <edklib/raw_rtp.h>
#include <edklib/rtp_pt.h>

#define STREAM_PROFILE  (0x2012)
#define MAX_SEND_FIFO (8192)
#define MAX_RECV_FIFO (2048)

typedef unsigned char __u8;

#define fpga_readw(base,x) (*(volatile unsigned short *)((unsigned long)base + (x)))
#define fpga_writew(base,x,val) (*(volatile unsigned short *)((unsigned long)base + (x)) = val)

#undef M_DEBUG
#define M_DEBUG

#define SLICE_SIZE  (2048)
#define MAX_STREAM_NUM (4)
#define MIN_PACKET_SIZE (512)
#define MIN_RESERVE_SIZE (1)

//#define DEBUG_COUNTER 1
#ifdef DEBUG_COUNTER
#include <edklib/pulse_counter.h>
struct pulse_counter* counter[2];
#endif

struct v804_stream_format
{
    uint16_t profile;           /**< 应当为网络字节序的固定值 0x2012 */
    uint16_t lenth;             /**< 应当为 1 */
    uint16_t rate_info;    /** RS422 transfer speed**/
    uint16_t rsvd;              /**< 保留使用，应当以 0 填充*/
}__attribute__((packed));

struct v804_stream_rtp
{
    rtp_hdr_t rtp_hdr;
    //struct v807_stream_format format;
}__attribute__((packed));

enum STREAM_DIR
{
    STREAM_IN_1,
    STREAM_OUT_1,
    STREAM_IN_2,
    STREAM_OUT_2,
    STREAM_IN_3,
    STREAM_OUT_3,
    STREAM_IN_4,
    STREAM_OUT_4
};


struct stream_handle_session 
{
    int bindid;

    int packet_size;
    //    int irq_threshold;
    int chn_num;

    uint32_t ssrc;
    uint16_t seq;

    volatile uint32_t packet_recv;
    volatile uint32_t packet_sent;
    volatile uint32_t packet_send_drop;
    volatile uint32_t packet_recv_drop;
    volatile uint32_t tscount;
    volatile uint32_t intr_status;
    volatile uint32_t overflow;
    volatile uint32_t underflow;
    volatile uint32_t tx_overflow;
    volatile uint32_t tx_underflow;
    volatile uint32_t tx_count;
    volatile uint32_t waitfailed;

    int ts_fd;
    void *read_dma_buf;
    void *write_dma_buf;
    void *fpga_base;
    int ts_count_addr;
    int tx_count_addr;
};

/* 各通道位移 */
static unsigned short addr_offset[] = {0x00, 0x40, 0x80, 0xc0};
static struct stream_handle_session *session_table[MAX_STREAM_NUM] = {NULL, NULL, NULL, NULL};
static struct ChannelTransfer *channel_recv = NULL;
static struct ChannelTransfer *channel_send = NULL;

static tea_result_t tsk_init(worker_t* worker)
{
    int32_t r;
    int i, j;
    char path[64];
    int tmp;

    struct N_node *nn = worker->nn_inst;
    struct stream_handle_session *session = NULL;

    ENTER_FUN();

    for (i=0; i<MAX_STREAM_NUM; i++)
    {
        session = session_table[i];

        random16(&session->seq);
        session->overflow = 0;

        memset(path, 0, 64);
        snprintf(path, 64, "session[%d]/packet_size", i+1);
        r = xT_read_int_2(nn, path, &session->packet_size);
        CHECK_RESULT(r);
        session->packet_size = session->packet_size >> 1;
        Debug("packet size %d", session->packet_size);

        r = task_stream_setopt(worker, STREAM_OUT_1+i*2, stream_opt_rtp, (void*) FALSE);
        CHECK_RESULT(r);

        //tmp = 360;
        //r = task_stream_setopt(worker, STREAM_OUT_1+i*2, stream_opt_duration, (void*)tmp);
        //CHECK_RESULT(r);


#if 1    /* task启动前先把读缓冲区清空 */
        tmp = fpga_readw(session->fpga_base, session->ts_count_addr);
        Debug("[DEBUG] tmp = %x", tmp);
        for(j=0; j<tmp; j++)
        {
            fpga_readw(session->fpga_base, FPGA_ADDR_DATA);
        }
        Debug("FPGA_ADDR_DATA CLEAR");
#endif
    }

#ifdef DEBUG_COUNTER
    counter[0] = pulse_counter_create();
    ASSERT(counter[0]);
    pulse_counter_setopt(counter[0], pulse_counter_interval, (void*) 2);
    pulse_counter_setopt(counter[0], pulse_counter_volume, (void*) 4000);
    pulse_counter_setopt(counter[0], pulse_counter_name, "r");

    
    counter[1] = pulse_counter_create();
    ASSERT(counter[1]);
    pulse_counter_setopt(counter[1], pulse_counter_interval, (void*) 2);
    pulse_counter_setopt(counter[1], pulse_counter_volume, (void*) 4000);
    pulse_counter_setopt(counter[1], pulse_counter_name, "w");
#endif

    return 0;
}

static tea_result_t tsk_cleanup(worker_t *worker)
{
#ifdef DEBUG_COUNTER
    pulse_counter_destroy(counter[0]);
    pulse_counter_destroy(counter[1]);
#endif
    return 0;
}

int get_single_bulk(struct stream_handle_session *session, struct ChannelTransfer *bulk) /* bulk_size words, 16bits */
{
    int32_t r;
    int fd = session->ts_fd;

    r = ioctl(fd, FPGA_DMA_IOC_GET_FRAME, bulk);
    if (r)
    {
        if (errno == EAGAIN)
            return TEA_RSLT_AGAIN;
        else
        {
            ASSERT(0);
            return TEA_RSLT_FAIL;
        }
    }   

    return 0;
}

int send_single_bulk(struct stream_handle_session *session, struct ChannelTransfer *bulk ) /* 16bit word */
{
    int fd = session->ts_fd;
    int32_t r;

    r = ioctl(fd, FPGA_DMA_IOC_SEND_FRAME, bulk);/* 参数更改 */
    if (r)
    {
        BUG("ioctl : TS_DMA_IOC_SEND_FRAME, failed!!!!!!!!\n");
        return -1;
    }

    return 0;
}

#if 1
/* 发送从udp接收的流,接收从FPGA读取的数据 */
tea_result_t tsk_repeat_transfer(worker_t* worker)
{
    int32_t r;
    void *dmabuf = NULL;
    stream_frame_t frm;
    rtp_hdr_t *rtp_hdr;
    int i;
    int payload_len; 
    int txcount, writeable_count; 
    //unsigned short *p = frm.buf;
    int transfer_size = 0;
    int sleep_flag = 0;
    struct stream_handle_session *session = NULL;
    timeout_t tout={0,0};
    enum frame_flag flags;
    void *buf = NULL; // point to a buffer provided by a stream;
    static int sleep_count = 0;

    //for (i=0; i<4; i++)
    for (i=0; i<MAX_STREAM_NUM; i++)
    {
        session = session_table[i];
        dmabuf = session->read_dma_buf;
        session->tscount = fpga_readw(session->fpga_base, session->ts_count_addr);
        if (session->tscount >= MAX_RECV_FIFO)
        {
            session->overflow ++;
        }
        if (session->tscount == 0)
        {
            session->underflow ++;
        }

        if (session->tscount >= (uint32_t)(session->packet_size)+MIN_RESERVE_SIZE)
        //if (session->tscount >= MIN_PACKET_SIZE+MIN_RESERVE_SIZE)
        {
            sleep_flag |= (0x1<<(i+MAX_STREAM_NUM));

            //channel_recv->packsize = MIN_PACKET_SIZE;
            channel_recv->packsize = session->packet_size;
            channel_recv->chn = i;

#ifdef DEBUG_COUNTER
          pulse_counter_input_begin(counter[0]);
#endif
            get_single_bulk(session, channel_recv);
#ifdef DEBUG_COUNTER
          pulse_counter_input_end(counter[0], session->tscount);
#endif

            transfer_size = channel_recv->packsize << 1;

            r = task_stream_get_buffer(worker, STREAM_IN_1+i*2, (transfer_size) + sizeof(struct v804_stream_rtp) , &tout, &buf); /* STREAM_IN_xx */
            if (RESULT_SUCCESS(r))
            {
                rtp_hdr     = buf;
                rtp_hdr->cc = 0;
                rtp_hdr->x  = 0;
                rtp_hdr->p  = 0;
                rtp_hdr->version = RTP_VERSION;
                rtp_hdr->pt = 100;
                rtp_hdr->m  = 0;
                rtp_hdr->ssrc = htonl(session->ssrc);           

                session->seq++;
                memcpy((char*)buf+sizeof(struct v804_stream_rtp), (char*)dmabuf, transfer_size);

                r = task_stream_commit(worker, STREAM_IN_1+i*2, (transfer_size)+sizeof(struct v804_stream_rtp));
            }
            //ASSERT(r == 0);

            session->packet_recv++;
        }
    }

    //for (i=3; i<4; i++)
    for (i=0; i<MAX_STREAM_NUM; i++)
    {
        session = session_table[i];
        dmabuf = session->write_dma_buf;
        session->tx_count = fpga_readw(session->fpga_base, session->tx_count_addr);
        if (session->tx_count >= MAX_SEND_FIFO)
        {
            session->tx_overflow ++;
            //continue;
        }

        if (session->tx_count == 0)
        {
            session->tx_underflow ++;
        }

        r = task_stream_get_frame_3(worker, STREAM_OUT_1+i*2, &frm, &tout, &flags, &rtp_hdr);
        if(RESULT_SUCCESS(r))
        {
            //ASSERT(frm.len == 1024);
            sleep_flag |= 0x1<<i;
            if (!(flags & frame_flag_rtp))
            {
                WARNING("flags %d is not rtp\n", flags);
                ++session->packet_send_drop;
                r = task_stream_release_frame(worker, STREAM_OUT_1+i);
                continue;
            }

            if (frm.len & 0x1)
            {
                WARNING("payload is not supposed to bo odd %d\n", frm.len);
                ++session->packet_send_drop;
                r = task_stream_release_frame(worker, STREAM_OUT_1+i);
                continue;
            }

            payload_len = frm.len>>1; 
            txcount = session->tx_count;
            writeable_count = MAX_SEND_FIFO - txcount;
            
            //if (payload_len < writeable_count)
            {
                channel_send->packsize = payload_len;
                channel_send->chn = i;
                //if (payload_len >= 256)
                {
                    memcpy(session->write_dma_buf, frm.buf, frm.len);
#ifdef DEBUG_COUNTER
                    pulse_counter_input_begin(counter[1]);
#endif
                    send_single_bulk(session, channel_send);
#ifdef DEBUG_COUNTER
                    pulse_counter_input_end(counter[1], txcount);
#endif
                }
                session->packet_sent++;
                r = task_stream_release_frame(worker, STREAM_OUT_1+i*2);
                ASSERT(0 == r);
            }
            //session->packet_sent++;
            //r = task_stream_release_frame(worker, STREAM_OUT_1+i*2);
            //ASSERT(0 == r);
        }
    }

    if (sleep_flag == 0)
    {
        sleep_count++;
        if (sleep_count > 1000)
        {
            //Debug("sleep %d times", sleep_count);
            sleep_count = 0;
        }
        usleep(10);
    }

    return 0;
}
#else
tea_result_t tsk_repeat_transfer(worker_t* worker)
{
    int32_t r;
#if 1    
    void *read_dmabuf = NULL;
    void *send_dmabuf = NULL;
    struct stream_handle_session *session = NULL;
    int sleep_flag = 0;
    int transfer_size = 0;
    int i;

    for (i=0; i<MAX_STREAM_NUM; i++)
    {
        session = session_table[i];
        read_dmabuf = session->read_dma_buf;
        send_dmabuf = session->write_dma_buf;
        session->tscount = fpga_readw(session->fpga_base, session->ts_count_addr);
        if (session->tscount >= MAX_RECV_FIFO)
        {
            session->overflow ++;
        }

        if (session->tscount >= (MIN_PACKET_SIZE+MIN_RESERVE_SIZE))
        {
            sleep_flag |= (0x1<<(i+MAX_STREAM_NUM));

            channel_recv->packsize = MIN_PACKET_SIZE;
            channel_recv->chn = i;
            r = get_single_bulk(session, channel_recv);
            if (r != 0)
            {
                Debug("get single bulk failed!");
            }

            transfer_size = channel_recv->packsize<<1;
            memcpy(send_dmabuf, read_dmabuf, transfer_size);

            channel_send->packsize = channel_recv->packsize;
            channel_send->chn = i;
            r = send_single_bulk(session, channel_send);
            if (r != 0)
            {
                Debug("send single bulk failed!");
            }
        }
    }
    if (sleep_flag == 0)
    {
        usleep(10);
    }
#endif
    //r = ioctl(session_table[0]->ts_fd, FPGA_DMA_IOC_TEST_LOOP, channel_recv);

    return 0;
}
#endif


static tea_result_t create(struct N_node *nn)
{
    int32_t r;
    int fd = -1;
    //int timerid;
    void *buff, *fpga_base;
    //    struct T_node *tn;
    int i;
    struct stream_handle_session *session;
    char path[64];

    fd = open("/dev/fpga-0", O_RDWR);
    if (fd < 0)
    {
        printf("%s %d: can't not open /dev/fpga-0!!!\n", __FUNCTION__, __LINE__);
        return TEA_RSLT_FAIL;
    }

    SLEEP(0, 100*1000*1000);
    printf("open /dev/fpga-0, ts_fd is %d\n", fd);
    buff = mmap(NULL, POOLSIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (buff == MAP_FAILED)
    {
        printf("%s %d: mmap /dev/fpga-0 failed!!!\n", __FUNCTION__, __LINE__);
        return TEA_RSLT_FAIL;
    }

    fpga_base = mmap(NULL, FPGA_PHY_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, POOLSIZE);
    if (fpga_base == MAP_FAILED)
    {
        printf("%s %d: mmap /dev/fpga-0 failed!!!\n", __FUNCTION__, __LINE__);
        return TEA_RSLT_FAIL;
    }
    
    for (i=0; i<MAX_STREAM_NUM; i++)
    {
        memset(path, 0, 64);
        session = session_table[i];

        snprintf(path, 64, "session[%d]/packet_recv", i+1);
        r = xT_set_realtime_x(nn, path, T_NODE_RTT_UINT, &session->packet_recv);
        CHECK_RESULT(r);

        snprintf(path, 64, "session[%d]/packet_sent", i+1);
        r = xT_set_realtime_x(nn, path, T_NODE_RTT_UINT, &session->packet_sent);
        CHECK_RESULT(r);

        snprintf(path, 64, "session[%d]/packet_send_drop", i+1);
        r = xT_set_realtime_x(nn, path, T_NODE_RTT_UINT, &session->packet_send_drop);
        CHECK_RESULT(r);

        snprintf(path, 64, "session[%d]/packet_recv_drop", i+1);
        r = xT_set_realtime_x(nn, path, T_NODE_RTT_UINT, &session->packet_recv_drop);
        CHECK_RESULT(r);

        snprintf(path, 64, "session[%d]/tscount", i+1);
        r = xT_set_realtime_x(nn, path, T_NODE_RTT_UINT, &session->tscount);
        CHECK_RESULT(r);

        snprintf(path, 64, "session[%d]/intr_status", i+1);
        r = xT_set_realtime_x(nn, path, T_NODE_RTT_UINT, &session->intr_status);
        CHECK_RESULT(r);

        snprintf(path, 64, "session[%d]/overflow", i+1);
        r = xT_set_realtime_x(nn, path, T_NODE_RTT_UINT, &session->overflow);
        CHECK_RESULT(r);

        snprintf(path, 64, "session[%d]/underflow", i+1);
        r = xT_set_realtime_x(nn, path, T_NODE_RTT_UINT, &session->underflow);
        CHECK_RESULT(r);

        snprintf(path, 64, "session[%d]/tx_overflow", i+1);
        r = xT_set_realtime_x(nn, path, T_NODE_RTT_UINT, &session->tx_overflow);
        CHECK_RESULT(r);

        snprintf(path, 64, "session[%d]/tx_underflow", i+1);
        r = xT_set_realtime_x(nn, path, T_NODE_RTT_UINT, &session->tx_underflow);
        CHECK_RESULT(r);

        snprintf(path, 64, "session[%d]/tx_count", i+1);
        r = xT_set_realtime_x(nn, path, T_NODE_RTT_UINT, &session->tx_count);
        CHECK_RESULT(r);

        snprintf(path, 64, "session[%d]/waitfailed", i+1);
        r = xT_set_realtime_x(nn, path, T_NODE_RTT_UINT, &session->waitfailed);
        CHECK_RESULT(r);

        random32(&session->ssrc);
        session->ts_fd          = fd;
        session->read_dma_buf   = buff+i*DMA_BUFFER_SLICE_SIZE;
        session->write_dma_buf  = buff+(POOLSIZE>>1)+i*DMA_BUFFER_SLICE_SIZE;
        session->fpga_base      = fpga_base;
        session->tx_count_addr  = FPGA_ADDR_TXCOUNT+addr_offset[i];
        session->ts_count_addr  = FPGA_ADDR_TSCOUNT+addr_offset[i];
        Debug("fd=%d, read_buf=0x%x, write_buf=0x%x, fpga_base=0x%x, ts_count=0x%x, tx_count=0x%x",
                session->ts_fd, session->read_dma_buf, session->write_dma_buf, 
                session->fpga_base, session->ts_count_addr, session->tx_count_addr);
    }

    return 0;
}


static tea_result_t destroy(struct N_node *nn)
{
    //int32_t r;
    return 0;
}

static tea_result_t v804_stream_init(void)
{
    int i;

    Debug("INTO init");

    for (i=0; i<MAX_STREAM_NUM; i++)
    {
        session_table[i] = calloc(1, sizeof(struct stream_handle_session));
        CHECK_POINTER(session_table[i]);
    }

    channel_send = calloc(1, sizeof(struct ChannelTransfer));
    CHECK_POINTER(channel_send);

    channel_recv = calloc(1, sizeof(struct ChannelTransfer));
    CHECK_POINTER(channel_recv);

    return 0;
}

static tea_result_t v804_stream_fini(void)
{
    int i;

    for (i=0; i<MAX_STREAM_NUM; i++)
    {
        if (session_table[i])
        {
            free(session_table[i]);
        }
    }
    if (channel_recv)
    {
        free(channel_recv);
    }
    if (channel_send)
    {
        free(channel_send);
    }

    return 0;
}

static task_func_t repeat_table[] = {tsk_repeat_transfer, NULL};

static struct task_logic logic =
{
init:
    tsk_init,
    repeat:
        repeat_table,
    cleanup:
        tsk_cleanup
};

tea_app_t v804_stream = 
{
    .version    = TEA_VERSION(0,5),
    .init       = v804_stream_init,
    .fini       = v804_stream_fini,
    .create     = create,
    .destroy    = destroy,
    .task       = &logic
};

