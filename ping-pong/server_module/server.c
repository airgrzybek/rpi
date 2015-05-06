
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/err.h>
#include <net/sock.h>
#include <linux/netlink.h>
#include <linux/skbuff.h>

#define NETLINK_USER 24

struct sock * sk = NULL;

static int sendMsg(int pid, const char * buffer)
{
    struct nlmsghdr * nlh = NULL;
    struct sk_buff * skb_out;
    int msg_size = 0;
    int res = 0;

    printk(KERN_INFO "server msg invoked!\n");

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
    int pid = 0;
    const char * hello_msg = "Hello world from kernel";
    printk(KERN_INFO "server msg invoked!\n");


    nlh = (struct nlmsghdr *)skb->data;
    pid = nlh->nlmsg_pid;
    printk(KERN_INFO "Server received msg from PID=%d, payload=%s\n",pid, (char*)nlmsg_data(nlh));

    sendMsg(pid,hello_msg);

}

static int __init
server_init(void)
{
    int result = 0;
    struct netlink_kernel_cfg cfg =
    {
            .input = server_msg,
    };

    printk(KERN_INFO "Server module initialization\n");

    sk = netlink_kernel_create(&init_net,NETLINK_USER, &cfg);
    if(!sk)
    {
        printk(KERN_ERR "Error in creating netlink socket!\n");
        result = -EINVAL;
    }


    return result;
}



static void __exit
server_exit(void)
{
    printk(KERN_INFO "Server exit\n");
    netlink_kernel_release(sk);
}

module_init(server_init);
module_exit(server_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("grzybek");
MODULE_DESCRIPTION("Netlink server module");
MODULE_VERSION("dev");
