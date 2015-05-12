
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/err.h>
#include <net/sock.h>
#include <linux/netlink.h>
#include <linux/skbuff.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/time.h>

#define debug(...)                      printk(KERN_DEBUG __VA_ARGS__);
#define info(...)                       printk(KERN_INFO __VA_ARGS__);
#define error(...)                      printk(KERN_ERR __VA_ARGS__);
#define warrning(...)                   printk(KERN_WARNING __VA_ARGS__);
#define critical(...)                   printk(KERN_CRIT __VA_ARGS__);

#define NETLINK_USER                    25
#define DATA_GPIO                       7
#define RH_INT_MASK                     (0xF << 32)
#define RH_DEC_MASK                     (0xF << 24)
#define T_INT_MASK                      (0xF << 16)
#define T_DEC_MASK                      (0xF << 8)
#define CRC_MASK                        (0xF)

#define T_START_BIT                     50 // usec
#define T_START_TRANS                   80 // usec
#define START_TIME                      500 // msec
#define T_BIT_LOW_L                     20
#define T_BIT_LOW_H                     30
#define T_BIT_HIGH_L                    70
#define T_BIT_HIGH_H                    90

#define NSEC_TO_USEC(x)                 (x)/1000

struct sock * sk                        = NULL;
static int PID                          = 0;
static struct timer_list data_timer;
static struct hrtimer hr_timer;
static volatile s64 t_start_low         = 0;
static volatile s64 t_end_low           = 0;
static volatile s64 t_start_high        = 0;
static volatile s64 t_end_high          = 0;
static volatile s64 t_low               = 0;
static volatile s64 t_high              = 0;
static bool start_seq                   = false;
static bool data_seq                    = false;
static unsigned long DATA               = 0;
static int data_pos_bit                 = 40;
/* Define GPIOs for BUTTONS */
static struct gpio temp_data_gpio =
        { DATA_GPIO, GPIOF_OUT_INIT_HIGH, "temp_data" };

/* Later on, the assigned IRQ numbers for the buttons are stored here */
static int data_irq                      = -1;


//static inline s64 nsec_to_usec(s64 time) { return time / 1000; }

static int sendMsg(int pid, const char * buffer);

static void startTransmissionHandler(unsigned long data)
{
    int result = 0;

    info("Transmission acceptance handler\n");

    gpio_set_value(temp_data_gpio.gpio,1);

    result = gpio_direction_input(temp_data_gpio.gpio);

    if(0 > result)
    {
        error("GPIO set input failed\n");
    }
    else
    {
        result = gpio_get_value(temp_data_gpio.gpio);
        info("value 123= %d\n",result);
        start_seq = true;
    }
}

enum hrtimer_restart my_hrtimer_callback( struct hrtimer *timer )
{
  error( "my_hrtimer_callback called (%ld).\n", jiffies );
  //start_seq = data_seq = false;
  return HRTIMER_NORESTART;
}

static int startTransmission(void)
{
    mdelay(5000);

    setup_timer(&data_timer, startTransmissionHandler, (unsigned long)NULL);

    // gpio low
    gpio_set_value(temp_data_gpio.gpio, 0);

    // run timer
    mod_timer(&data_timer, jiffies + msecs_to_jiffies(START_TIME));

    // init some variables
    start_seq = true;
    t_start_low = ktime_get().tv64;
    t_start_high = ktime_get().tv64;
    t_end_low = ktime_get().tv64;
    t_end_high = ktime_get().tv64;

    return 0;
}

/*
 * The interrupt service routine called on button presses
 */
static irqreturn_t temp_data_isr(int irq, void *data)
{
    int value = gpio_get_value(temp_data_gpio.gpio);
    ktime_t time = ktime_get();
    unsigned long delta = 0;
    info("data_pin 1 %s",value == 0 ? "low" : "high");
    if(irq == data_irq)
    {
        if(start_seq )
        {
            if( value == 0 )
            {
                t_start_low = (time.tv64);
                t_end_high  = (time.tv64);

                // calc time in high time
                t_high = t_end_high - t_start_high;

                info("Sltart Seq: t_start_high = %llu, t_end_high = %llu, t_high = %llu, ktime = %llu\n",
                        t_start_high, t_end_high, t_high,time.tv64);

                if(T_START_TRANS == t_high)
                {
                    info("DHT ready to transmit data - go to data bit procedure\n");
                    start_seq = false;
                    data_seq = true;
                }
            }
            else
            {
                t_end_low    = (time.tv64);
                t_start_high = (time.tv64);

                // calc time in low state
                t_low = t_end_low - t_start_low;

                info("Start Seq: t_start_low = %llu, t_end_low = %llu, t_low = %llu\n",
                        t_start_low, t_end_low, t_low);

                if(T_START_TRANS == t_low)
                {
                    info("DHT accepted transmission\n");
                }
            }

        }
        else if(data_seq)
        {
            if( 1 == value )
            {
                t_start_high = (time.tv64);
                t_end_low    = (time.tv64);

                t_low = t_end_low - t_start_low;

                info("Data Seq: t_low = %llu\n",t_low);
            }
            else if (-1 < data_pos_bit)
            {
                t_end_high  = (time.tv64);
                t_start_low = (time.tv64);
                t_high = t_end_high - t_start_high;

                info("Data Seq: t_high = %llu\n",t_high);

                if(25 < delta && 29 > delta)
                {
                    // bit is 0
                    DATA &= ~(1 << data_pos_bit);
                    --data_pos_bit;
                }
                else if (68 < delta && 72 > delta)
                {
                    // bit is 1
                    DATA |= (1 << data_pos_bit);
                    --data_pos_bit;
                }

                if(-1 == data_pos_bit)
                {
                    info("End of data transmission\n");
                    info("Data = %#lx\n",DATA);
                    start_seq = false;
                    data_seq = false;
                }
            }
        }
        else
        {
            info("wrong sequence\n");
        }
    }

    return IRQ_HANDLED;
}

static int sendMsg(int pid, const char * buffer)
{
    struct nlmsghdr * nlh = NULL;
    struct sk_buff * skb_out;
    int msg_size = 0;
    int res = 0;
    msg_size = strlen(buffer);

    printk(KERN_INFO "sendMsg to PID=%d\n",pid);

    skb_out = nlmsg_new(msg_size,0);
    if(!skb_out)
    {
        printk(KERN_ERR "Failed to allocate msg\n");
        res = -1;
    }
    else
    {
        nlh = nlmsg_put(skb_out,0,0,NLMSG_DONE,msg_size,0);
        NETLINK_CB(skb_out).dst_group = 0;
        strncpy(nlmsg_data(nlh),buffer,msg_size);

        res = nlmsg_unicast(sk,skb_out,pid);
        if(res < 0)
        {
            printk(KERN_ERR "Error in sending msg\n");
        }
    }

    return res;
}

static void server_msg(struct sk_buff * skb)
{
    struct nlmsghdr * nlh = NULL;
    const char * hello_msg = "Hello world from kernel";
    printk(KERN_INFO "server msg invoked!\n");


    nlh = (struct nlmsghdr *)skb->data;
    PID = nlh->nlmsg_pid;
    printk(KERN_INFO "Server received msg from PID=%d, payload=%s\n",PID, (char*)nlmsg_data(nlh));

    sendMsg(PID,hello_msg);

}

static int __init
temp_sensor_init(void)
{
    int ret = 0;
    struct netlink_kernel_cfg cfg =
    {
            .input = server_msg,
    };

    printk(KERN_INFO "Server module initialization\n");

    sk = netlink_kernel_create(&init_net,NETLINK_USER, &cfg);
    if(!sk)
    {
        printk(KERN_ERR "Error in creating netlink socket!\n");
        ret = -EINVAL;
    }
    else
    {
        // configure interrupts
        // register BUTTON gpios

        ret =  gpio_request(temp_data_gpio.gpio,"data");
        ret = gpio_direction_output(temp_data_gpio.gpio,1);
        if (ret)
        {
            printk(KERN_ERR "Unable to request GPIOs for BUTTONs: %d\n", ret);
            return ret;
        }


        printk(KERN_INFO "Current temp value: %d\n", gpio_get_value(temp_data_gpio.gpio));

        ret = gpio_to_irq(temp_data_gpio.gpio);

        if(ret < 0)
        {
            printk(KERN_ERR "Unable to request IRQ: %d\n", ret);
            return ret;
        }

        data_irq = ret;

        printk(KERN_INFO "Successfully requested BUTTON1 IRQ # %d\n", data_irq);

        ret = request_irq(data_irq, temp_data_isr, IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING | IRQF_DISABLED,
                "gpiomod#temp_data_gpio", NULL);


        if(ret)
        {
            printk(KERN_ERR "Unable to request IRQ: %d\n", ret);
            return ret;
        }
        else
        {
            //hrtimer_init( &hr_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL );
            //hr_timer.function = &my_hrtimer_callback;


            //disable_irq(data_irq);
        }


        info("start transmission\n");
        startTransmission();
    }


    return ret;
}



static void __exit
temp_sensor_exit(void)
{
    printk(KERN_INFO "button module exit\n");
    netlink_kernel_release(sk);

    // free irqs
    free_irq(data_irq, NULL);

    // unregister
    gpio_free(temp_data_gpio.gpio);
}

module_init(temp_sensor_init);
module_exit(temp_sensor_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("grzybek");
MODULE_DESCRIPTION("Button handling module");
MODULE_VERSION("dev");
