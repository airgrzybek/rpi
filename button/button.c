
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/err.h>
#include <net/sock.h>
#include <linux/netlink.h>
#include <linux/skbuff.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>

#define NETLINK_USER 24

struct sock * sk = NULL;
static int PID = 0;

/* Define GPIOs for LEDs */
static struct gpio leds[] = {
        {  18, GPIOF_OUT_INIT_HIGH, "LED 1" },
};

/* Define GPIOs for BUTTONS */
static struct gpio buttons[] = {
        { 23, GPIOF_IN, "BUTTON 1" },   // turns LED on
};

/* Later on, the assigned IRQ numbers for the buttons are stored here */
static int button_irqs[] = { -1, -1 };

static int sendMsg(int pid, const char * buffer);

/*
 * The interrupt service routine called on button presses
 */
static irqreturn_t button_isr(int irq, void *data)
{
    int button_value = gpio_get_value(buttons[0].gpio);
    char buf[100] = {0};
    sprintf(buf,"Button 1 %s",button_value == 0 ? "low" : "high");
    if(irq == button_irqs[0])
    {
            gpio_set_value(leds[0].gpio, button_value);
            if (PID != 0)
            {
                sendMsg(PID,buf);
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
button_init(void)
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

        // register LED gpios
        ret = gpio_request_array(leds, ARRAY_SIZE(leds));
        if (ret)
        {
            printk(KERN_ERR "Unable to request GPIOs for LEDs: %d\n", ret);
            return ret;
        }

        // register BUTTON gpios
        ret = gpio_request_array(buttons, ARRAY_SIZE(buttons));
        if (ret)
        {
            printk(KERN_ERR "Unable to request GPIOs for BUTTONs: %d\n", ret);
            return ret;
        }


        printk(KERN_INFO "Current button1 value: %d\n", gpio_get_value(buttons[0].gpio));

        ret = gpio_to_irq(buttons[0].gpio);

        if(ret < 0)
        {
            printk(KERN_ERR "Unable to request IRQ: %d\n", ret);
            return ret;
        }

        button_irqs[0] = ret;

        printk(KERN_INFO "Successfully requested BUTTON1 IRQ # %d\n", button_irqs[0]);

        ret = request_irq(button_irqs[0], button_isr, IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING | IRQF_DISABLED,
                "gpiomod#button1", NULL);

        if(ret)
        {
            printk(KERN_ERR "Unable to request IRQ: %d\n", ret);
            return ret;
        }

    }


    return ret;
}



static void __exit
button_exit(void)
{
    int i =0;
    printk(KERN_INFO "button module exit\n");
    netlink_kernel_release(sk);

    // free irqs
    free_irq(button_irqs[0], NULL);

    // turn all LEDs off
    for(i = 0; i < ARRAY_SIZE(leds); ++i)
    {
        gpio_set_value(leds[i].gpio, 1);
    }

    // unregister
    gpio_free_array(leds, ARRAY_SIZE(leds));
    gpio_free_array(buttons, ARRAY_SIZE(buttons));
}

module_init(button_init);
module_exit(button_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("grzybek");
MODULE_DESCRIPTION("Button handling module");
MODULE_VERSION("dev");
