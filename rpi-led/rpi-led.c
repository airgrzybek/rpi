
/*
 *  chardev.c: Creates a read-only char device that says how many times
 *  you've read from the dev file
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <asm/io.h>
#include <mach/platform.h>
#include <linux/timer.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/slab.h>
/*
 *  Prototypes - this would normally go in a .h file
 */

struct GpioRegisters
{
    unsigned int GPFSEL[6];
    unsigned int Reserved1;
    unsigned int GPSET[2];
    unsigned int Reserved2;
    unsigned int GPCLR[2];
    unsigned int Reserved3;
    unsigned int GPLEV[2];
};

static void SetGPIOFunction(int GPIO, int functionCode);
static void SetGPIOOutputValue(int GPIO, bool outputValue);
static ssize_t export_store(struct class *dev, struct class_attribute *attr,
        const char *buf, size_t count);
static ssize_t unexport_store(struct class *dev, struct class_attribute *attr,
        const char *buf, size_t count);
ssize_t led_state_show(struct device *dev, struct device_attribute *attr,
            char *buf);
static ssize_t led_state_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count);

struct GpioRegisters *s_pGpioRegisters;

static DEVICE_ATTR(ledset, 0200, NULL, led_state_store);
static DEVICE_ATTR(state, 0200, led_state_show, led_state_store);


static struct class_attribute led_class_attrs[] = {
    __ATTR(export, 0200, NULL, export_store),
    __ATTR(unexport, 0200, NULL, unexport_store),
    __ATTR_NULL,
};

static struct class deviceClass =
{
        .name = "LedState",
        .owner = THIS_MODULE,

        .class_attrs = led_class_attrs,
};


static struct device *deviceObject = NULL;
//static bool LED_STATE = false;
//static struct timer_list blinker;
static int blink_period = 1000;


struct timer_data_struct {
  int GPIO;
  unsigned int period;
  bool state;
  struct timer_list timer;
  struct list_head list;
};

LIST_HEAD( timer_list );

static void deleteTimer(int GPIO)
{
    struct timer_data_struct *my_timer = NULL;
    struct list_head * head, *q;

    printk(KERN_INFO "Delete timer for LED %d\n",GPIO);

    list_for_each_safe( head, q, &timer_list)
    {
       my_timer = list_entry(head,struct timer_data_struct, list);
       if(NULL != my_timer && GPIO == my_timer->GPIO)
       {
           del_timer(&my_timer->timer);
           list_del(head);
           kfree(my_timer);
       }
    }
}

static void deleteTimers(int unused)
{
    struct timer_data_struct *my_timer = NULL;
    struct list_head * head, *q;


    list_for_each_safe( head, q, &timer_list)
    {
       my_timer = list_entry(head,struct timer_data_struct, list);
       if(NULL != my_timer)
       {
           del_timer(&my_timer->timer);
           list_del(head);
           kfree(my_timer);
       }
    }
}

static void BlinkTimerHandler(unsigned long timer)
{
    struct timer_data_struct * timer_data = (struct timer_data_struct *)timer;
    if (NULL != timer_data)
    {
        timer_data->state = !timer_data->state;
        SetGPIOOutputValue(timer_data->GPIO, timer_data->state);
        mod_timer(&timer_data->timer,
                jiffies + msecs_to_jiffies(timer_data->period));
    }
    else
    {
        printk(KERN_ERR "timer_data is NULL\n");
    }
}

ssize_t led_state_show(struct device *dev, struct device_attribute *attr,
            char *buf)
{
    int result =0;
    printk(KERN_INFO "LED show state invoked\n");

    return result;
}
static ssize_t led_state_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
    int GPIO = dev->devt;
    struct timer_data_struct * timer_data = NULL;
    printk(KERN_INFO "LED store state invoked for %s\n",dev->init_name);
    printk(KERN_INFO "buffor: %s strlen: %d strcmp: %d\n", buf,strlen(buf), strlen("off\n"));

    deleteTimer(GPIO);

    if(0 == strcmp("on\n",buf))
    {
        printk(KERN_INFO "led_state_store: on\n");
        SetGPIOOutputValue(GPIO,false);
    }
    else if(0 == strcmp("off\n",buf))
    {
        printk(KERN_INFO "led_state_store: off\n");
        SetGPIOOutputValue(GPIO,true);
    }
    else if(0 == strcmp("blink\n",buf))
    {
        printk(KERN_INFO "led_state_store: blink\n");

        timer_data = (struct timer_data_struct *)
                    kmalloc( sizeof(struct timer_data_struct), GFP_KERNEL );
        timer_data->GPIO = GPIO;
        timer_data->period = blink_period;
        timer_data->state = true;
        setup_timer(&(timer_data->timer), BlinkTimerHandler, (unsigned long)timer_data);
        mod_timer(&timer_data->timer, jiffies + msecs_to_jiffies(timer_data->period));

        list_add( &timer_data->list, &timer_list );
    }
    else
    {
        printk(KERN_ERR "led_state_store: Operation not supported\n");
    }

    return count;
}


ssize_t export_store(struct class *dev, struct class_attribute *attr,
        const char *buf, size_t count)
{
    struct device * childDev = NULL;
    int GPIO = simple_strtol(buf,NULL,0);
    char name[20];

    sprintf(name,"led%d",GPIO);

    printk(KERN_INFO "LED %d is about to be switched on\n",GPIO);
    SetGPIOFunction(GPIO,1);
    SetGPIOOutputValue(GPIO,false);


    childDev = device_create(dev,
            NULL,
            GPIO,
            NULL,
            name);

    if(NULL == childDev)
    {
        printk(KERN_ERR "Child device is NULL\n");
        return -EINVAL;
    }

    printk(KERN_INFO "Create device file\n");

    device_create_file(childDev, &dev_attr_state);

    return count;
}

static ssize_t unexport_store(struct class *dev, struct class_attribute *attr,
        const char *buf, size_t count)
{
    int GPIO = simple_strtol(buf,NULL,0);

    printk(KERN_INFO "unexport GPIO %d\n",GPIO);

    deleteTimer(GPIO);
    device_destroy(&deviceClass, GPIO);

    return count;
}

static void SetGPIOFunction(int GPIO, int functionCode)
{
    int registerIndex = GPIO / 10;
    int bit = (GPIO % 10) * 3;

    unsigned oldValue = s_pGpioRegisters->GPFSEL[registerIndex];
    unsigned mask = 0b111 << bit;
    printk("Changing function of GPIO%d from %x to %x\n",
           GPIO,
           (oldValue >> bit) & 0b111,
           functionCode);

    s_pGpioRegisters->GPFSEL[registerIndex] =
        (oldValue & ~mask) | ((functionCode << bit) & mask);
}

static void SetGPIOOutputValue(int GPIO, bool outputValue)
{
    unsigned int reg = GPIO / 32;

    //printk(KERN_INFO "Changing GPIO %d output val to %d\n", GPIO, outputValue);
    if (outputValue)
        s_pGpioRegisters->GPSET[reg] = (1 << (GPIO % 32));
    else
        s_pGpioRegisters->GPCLR[reg] = (1 << (GPIO % 32));
}

static int __init
led_init(void)
{
    int result = 0;
    printk(KERN_INFO "LED DRIVER INIT FOR SYSYFS USAGE\n");

    s_pGpioRegisters =
            (struct GpioRegisters *)__io_address(GPIO_BASE);

    if(NULL == s_pGpioRegisters)
    {
        printk(KERN_ERR "GPIO base address is NULL");
        return -1;
    }

    //deviceClass = class_create(THIS_MODULE,"LedSet");

    result = class_register(&deviceClass);

    if(0 > result)
    {
        printk(KERN_ERR "Device class is NULL\n");
        return result;
    }

    deviceObject = device_create(&deviceClass,
            NULL,
            0,
            NULL,
            "ledchip");

    result = device_create_file(deviceObject, &dev_attr_ledset);

    return result;
}



static void __exit
led_exit(void)
{
    printk(KERN_INFO "LED DRIVER EXIT\n");
    deleteTimers(0);
    device_destroy(&deviceClass, 0);
    class_unregister(&deviceClass);
}

module_init(led_init);
module_exit(led_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("grzybek");
MODULE_DESCRIPTION("Simple led driver");
MODULE_VERSION("dev");
