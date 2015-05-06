#include <sys/socket.h>
#include <linux/netlink.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>

#define NETLINK_USER 24
#define MAX_PAYLOAD 4096

struct sockaddr_nl src_addr;
int sock_fd;

int sendMsg(int fd, const char * buffer)
{
    struct nlmsghdr *nh = NULL;    /* The nlmsghdr with payload to send. */
    struct sockaddr_nl sa;
    struct iovec iov;// = { (void *) nh, nh->nlmsg_len };
    struct msghdr msg;
    int len = 0;

    nh = (struct nlmsghdr *)malloc(NLMSG_SPACE(MAX_PAYLOAD));

    if(NULL != nh)
    {
        memset(nh, 0, NLMSG_SPACE(MAX_PAYLOAD));
        nh->nlmsg_len = NLMSG_SPACE(MAX_PAYLOAD);
        nh->nlmsg_pid = getpid();
        nh->nlmsg_flags = 0;

        iov.iov_base = (void*) nh;
        iov.iov_len = nh->nlmsg_len;

        memset(&sa, 0, sizeof(sa));
        sa.nl_family = AF_NETLINK;
        sa.nl_pid = 0; /*For Linux Kernel*/
        sa.nl_groups = 0; /* unicast */

        strcpy(NLMSG_DATA(nh), buffer);

        msg.msg_name = (void *) &sa;
        msg.msg_namelen = sizeof(sa);
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = NULL;
        msg.msg_controllen = 0;
        msg.msg_flags = 0;

        len = sendmsg(fd, &msg, 0);
    }
    else
    {
        printf("Unable to allocate memory\n");
        len = -1;
    }

    return len;
}

int receiveMsg(int fd, char * buffer, int buflen)
{
    int len;
    char buf[4096] = {0};
    struct iovec iov =
    { buf, sizeof(buf) };
    struct sockaddr_nl sa;
    struct nlmsghdr *nh;

    struct msghdr msg = {(void *)&sa, sizeof(sa), &iov, 1, NULL, 0, 0};
    len = recvmsg(fd, &msg, 0);

    nh = (struct nlmsghdr *) buf;

    if(nh->nlmsg_len-sizeof(struct nlmsghdr) > buflen)
    {
        len = -1;
    }
    else
    {
        strcpy(buffer,NLMSG_DATA(nh));
    }

    return len;
}

int main(int argc, char **argv)
{
    char buf[MAX_PAYLOAD];
    int c;

    sock_fd = socket(PF_NETLINK, SOCK_RAW, NETLINK_USER);
    if (sock_fd < 0)
    {
        printf("Unable to create socket\n");
        return -1;
    }
    memset(&src_addr, 0, sizeof(src_addr));
    src_addr.nl_family = AF_NETLINK;
    src_addr.nl_pid = getpid(); /* self pid */

    bind(sock_fd, (struct sockaddr *)&src_addr, sizeof(src_addr));

    c = getopt(argc,argv,"m:hb");
    if(-1 == c)
    {
        printf("No arguments specified\n");
    }
    else
    {
        switch (c)
        {
        case 'm':
            printf("Sending %s to kernel\n",optarg);
            sendMsg(sock_fd,optarg);
            printf("Waiting for message from kernel\n");
            receiveMsg(sock_fd,buf,MAX_PAYLOAD);
            printf("Received message payload: %s\n", buf);
            break;

        case 'h':
            printf("Sending hello to kernel\n");
            sendMsg(sock_fd,"Hello");
            printf("Waiting for message from kernel\n");
            receiveMsg(sock_fd,buf,MAX_PAYLOAD);
            printf("Received message payload: %s\n", buf);
            break;

        case 'b':
            printf("Sending hello to kernel\n");
            sendMsg(sock_fd,"Hello");
            printf("Waiting for message from kernel\n");
            receiveMsg(sock_fd,buf,MAX_PAYLOAD);
            printf("Received message payload: %s\n", buf);

            while(1)
            {
                printf("Waiting for msg ...\n");
                memset(buf,'\0',MAX_PAYLOAD);
                if(receiveMsg(sock_fd,buf,MAX_PAYLOAD) > 0)
                {
                    printf("Received message payload: %s\n", buf);
                }
            }
            break;

        default:
            printf("Argument not recognized\n");
            break;
        }
    }

    close(sock_fd);

    return 0;
}
