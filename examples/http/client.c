//
// Created by sato on 2023/7/9.
//

#include "client.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define HTTP_VERSION        "HTTP/1.1"
#define CONNECTION_TYPE     "Connection: close\r\n"
//keep-alive

#define BUFFER_SIZE  4096

//通过DNS将域名转为 IP
char *host_to_ip(const char *hostname) {
    struct hostent *host_entry = gethostbyname(hostname);

    if (host_entry) {
        //h_addr_list其实是一个指针数组，数组中每个元素char*都是in_addr型指针(都是指针当然可转换)
        //host_entry->h_addr_list类型为 char **, 表明是char*类型数组
        //*host_entry->h_addr_list类型为 char *,即第一个ip地址(数组的第一个元素)
        //网络字节序地址转换为点分十进制地址
        return inet_ntoa(*(struct in_addr *) *host_entry->h_addr_list);
    }

    return NULL;
}

int http_create_socket(char *ip) {
    //客户端连接到服务端

    //http使用TCP协议
    //1.创建socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    //2.设置地址的相关参数（IP PORT PROTOCOL）
    struct sockaddr_in sin = {0};
    sin.sin_family = AF_INET;
    sin.sin_port = htons(80); //http默认端口号为80
    sin.sin_addr.s_addr = inet_addr(ip);//点分十进制地址转为网络字节序地址

    //3.connect
    //connect成功返回0
    if (0 != connect(sockfd, (struct sockaddr *) &sin, sizeof(struct sockaddr_in))) {
        return -1;
    }

    //socket设置为非阻塞，保证read没有读出数据时也能够立马返回
    //而不会一直阻塞导致后面的代码不执行
    fcntl(sockfd, F_SETFL, O_NONBLOCK);

    return sockfd;
}

char *http_send_request(const char *hostname, const char *resource) {
    //1.通过域名查询获得ip地址
    char *ip = host_to_ip(hostname);

    //2.创建socket（采用TCP连接）
    int sockfd = http_create_socket(ip);

    //3.组织请求报文
    char buffer[BUFFER_SIZE] = {0};// 或者memset清零
    //字符串不在同一行的时候每行结尾要加反斜杠
    //这里格式一定要注意，报文中空格不能多也不能少
    sprintf(buffer,
            "GET %s %s\r\n\
Host: %s\r\n\
%s\r\n\
\r\n",
            resource, HTTP_VERSION,
            hostname,
            CONNECTION_TYPE); //CONNECTION_TYPE我们设置为close


    //4.发送http请求报文
    //最后一个参数为0,表示为阻塞式发送
    //即送不成功会一直阻塞，直到被某个信号终端终止，或者直到发送成功为止。
    send(sockfd, buffer, strlen(buffer), 0);

    //不能简单使用recv()接受响应报文，因为我们创建的socket是非阻塞
    //如果使用recv可能没收到数据也返回了。

    //5.用select实现多路复用IO，循环检测是否有可读事件到来，从而进行recv
    fd_set fdread; // 可读fd的集合
    FD_ZERO(&fdread); //清零
    FD_SET(sockfd, &fdread);//将sockfd添加到待检测的可读fd集合

    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;


    char *result = malloc(sizeof(int));
    memset(result, 0x00, sizeof(int));
    while (1) {
        //第一参数：是一个整数值，是指集合中所有文件描述符的范围，即所有文件描述符的最大值加1
        //第二参数：可读文件描述符的集合[in/out]
        //第三参数：可写文件描述符的集合[in/out]
        //第四参数：出现错误文件描述符的集合[in/out]
        //第五参数：轮询的时间
        //返回值: 成功返回发生变化的文件描述符的个数
        //0：等待超时，没有可读写或错误的文件
        //失败返回-1, 并设置errno值.
        int selection = select(sockfd + 1, &fdread, NULL, NULL, &tv);

        //FD_ISSET判断fd是否在集合中
        if (!selection || !FD_ISSET(sockfd, &fdread)) {
            break;
        } else {
            memset(buffer, 0x00, BUFFER_SIZE);
            //返回接受到的字节数
            int len = recv(sockfd, buffer, BUFFER_SIZE, 0);
            if (len == 0) {
                //disconnect
                break;
            }
            //如果是扩大内存操作会把 result 指向的内存中的数据复制到新地址
            result = realloc(result, (strlen(result) + len + 1) * sizeof(char));
            strncat(result, buffer, len);
        }
    }

    return result;
}
