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

#include "tempsensor.h"

#define debug(...)                      printk(KERN_DEBUG __VA_ARGS__);
#define info(...)                       printk(KERN_INFO __VA_ARGS__);
#define error(...)                      printk(KERN_ERR __VA_ARGS__);
#define warrning(...)                   printk(KERN_WARNING __VA_ARGS__);
#define NSEC_TO_USEC(x)                 (x)/1000
#define critical(...)                   printk(KERN_CRIT __VA_ARGS__);

static struct timer_list data_timer;
static struct timer_list guard_timer;
static volatile s64 t_start_low = 0;
static volatile s64 t_end_low = 0;
static volatile s64 t_start_high = 0;
static volatile s64 t_end_high = 0;
static volatile s64 t_low = 0;
static volatile s64 t_high = 0;
static volatile Sequence sequence = STOP;
static volatile unsigned long long receivedData = 0;
static volatile signed long long data_pos_bit = 39;
/* Define GPIOs for BUTTONS */
static struct gpio temp_data_gpio =
{ DATA_GPIO, GPIOF_OUT_INIT_HIGH, "temp_data" };

static const limits bit_low         = { .min = T_BIT_LOW_L, .max = T_BIT_LOW_H } ;
static const limits bit_high        = { .min = T_BIT_HIGH_L, .max = T_BIT_HIGH_H } ;
static const limits bit_start       = { .min = T_BIT_START_L, .max = T_BIT_START_H } ;
static const limits start_ack       = { .min = T_START_ACK_L, .max = T_START_ACK_H } ;


/* Later on, the assigned IRQ numbers for the buttons are stored here */
static int data_irq = -1;

static unsigned char decode(unsigned long long data,  unsigned long bit)
{
    return (data & (0xffULL << bit)) >> bit;
}

static unsigned char checksum(unsigned long long data)
{
    return (decode(data,32) + decode(data,24) + decode(data,16) + decode(data,8))&0xFF;
}

static void guardTimerHandler(unsigned long data)
{
    info("Guard timer expired - transmission aborted!\n");
}


static void startTransmissionHandler(unsigned long data)
{
    int result = 0;

    info("Transmission acceptance handler\n");

    gpio_set_value(temp_data_gpio.gpio, 1);

    result = gpio_direction_input(temp_data_gpio.gpio);

    if (0 > result)
    {
        error("GPIO set input failed\n");
    }
    else
    {
        result = gpio_get_value(temp_data_gpio.gpio);
        info("value 123= %d\n", result);
        sequence = START;
    }

    // run guard timer
    setup_timer(&guard_timer, guardTimerHandler, (unsigned long)NULL);
    mod_timer(&guard_timer, jiffies + msecs_to_jiffies(T_GUARD_TIMEOUT));
}

static int startTransmission(void)
{
    setup_timer(&data_timer, startTransmissionHandler, (unsigned long)NULL);

    // gpio low
    gpio_set_value(temp_data_gpio.gpio, 0);

    // run timer
    mod_timer(&data_timer, jiffies + msecs_to_jiffies(START_TIME));

    // init some variables
    sequence = START;
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
    unsigned char crc = 0;
    info("data_pin 1 %s", value == 0 ? "low" : "high");
    if (irq == data_irq)
    {
        switch (sequence)
        {
        case START:
            if (0 == value)
            {
                t_start_low = (time.tv64);
                t_end_high = (time.tv64);

                // calc time in high time
                t_high = t_end_high - t_start_high;

                info(
                        "Start Seq: t_start_high = %llu, t_end_high = %llu, t_high = %llu, ktime = %llu\n",
                        t_start_high, t_end_high, t_high, time.tv64);

                if (inLimits(t_high, &start_ack))
                {
                    info(
                            "DHT ready to transmit data - go to data bit procedure\n");
                    sequence = DATA;
                }
            }
            else
            {
                t_end_low = (time.tv64);
                t_start_high = (time.tv64);

                // calc time in low state
                t_low = t_end_low - t_start_low;

                info(
                        "Start Seq: t_start_low = %llu, t_end_low = %llu, t_low = %llu\n",
                        t_start_low, t_end_low, t_low);

                if (inLimits(t_low, &start_ack))
                {
                    info("DHT accepted transmission\n");
                }
            }
            break;

        case DATA:
            if (1 == value)
            {
                t_start_high = (time.tv64);
                t_end_low = (time.tv64);

                t_low = t_end_low - t_start_low;

                info("Data Seq: t_low = %llu\n", t_low);
            }
            else if (-1 < data_pos_bit)
            {
                t_end_high = (time.tv64);
                t_start_low = (time.tv64);
                t_high = t_end_high - t_start_high;

                info("Data Seq: t_high = %llu, data_pos_bit=%lld\n", t_high, data_pos_bit);

                if (inLimits(t_high, &bit_low))
                {
                    // bit is 0
                    //receivedData &= ~(1 << data_pos_bit);
                    --data_pos_bit;
                }
                else if (inLimits(t_high, &bit_high))
                {
                    // bit is 1
                    receivedData |= (1ULL << data_pos_bit);
                    --data_pos_bit;
                }
                else
                {
                    info("decoding error\n");
                }
                info("received data = %#llx\n", receivedData);


                if (-1 == data_pos_bit)
                {
                    info("End of data transmission\n");
                    info("Data ll = %#llx\n", receivedData);
                    crc = checksum(receivedData);
                    info("crc = %#x\n",crc);
                    if(crc == (receivedData & CRC_MASK))
                    {
                        info("CRC OK \n");
                    }

                    sequence = STOP;
                }
            }
            break;

        case STOP: // no break
        default:
            info("Wrong sequence\n");
            break;
        }
    }

    return IRQ_HANDLED;
}
static int __init
temp_sensor_init(void)
{
    int ret = 0;

    // configure interrupts
    // register BUTTON gpios

    ret = gpio_request(temp_data_gpio.gpio, "data");
    ret = gpio_direction_output(temp_data_gpio.gpio, 1);
    if (ret)
    {
        printk(KERN_ERR "Unable to request GPIOs for BUTTONs: %d\n", ret);
        return ret;
    }

    printk(KERN_INFO "Current temp value: %d\n",
            gpio_get_value(temp_data_gpio.gpio));

    ret = gpio_to_irq(temp_data_gpio.gpio);

    if (ret < 0)
    {
        printk(KERN_ERR "Unable to request IRQ: %d\n", ret);
        return ret;
    }

    data_irq = ret;

    printk(KERN_INFO "Successfully requested BUTTON1 IRQ # %d\n", data_irq);

    ret = request_irq(data_irq, temp_data_isr,
            IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING | IRQF_DISABLED,
            "gpiomod#temp_data_gpio", NULL);

    if (ret)
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

    return ret;
}

static void __exit
temp_sensor_exit(void)
{
    printk(KERN_INFO "tempsensor module exit\n");

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
