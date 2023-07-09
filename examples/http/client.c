//
// Created by sato on 2023/7/9.
//

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "client.h"


#define HTTP_VERSION        "HTTP/1.1"
#define CONNECTION_TYPE     "Connection: close\r\n"
//keep-alive

#define BUFFER_SIZE  4096

char * host_to_ip_v1(const char *hostname)
{
    struct hostent *host_entry = gethostbyname(hostname);

    if(host_entry)
    {
        //h_addr_list其实是一个指针数组，数组中每个元素char*都是in_addr型指针(都是指针当然可转换)
        //host_entry->h_addr_list类型为 char **, 表明是char*类型数组
        //*host_entry->h_addr_list类型为 char *,即第一个ip地址(数组的第一个元素)
        //网络字节序地址转换为点分十进制地址
        return inet_ntoa(*(struct in_addr *)*host_entry->h_addr_list);
    }

    return NULL;
}

//通过DNS将域名转为 IP
char *host_to_ip(const char *hostname) {
    struct addrinfo hints, *res, *p;
    int status;
    char ipstr[INET_ADDRSTRLEN];

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; // IPv4
    hints.ai_socktype = SOCK_STREAM;

    if ((status = getaddrinfo(hostname, NULL, &hints, &res)) != 0) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        return NULL;
    }

    for (p = res; p != NULL; p = p->ai_next) {
        struct sockaddr_in *ipv4 = (struct sockaddr_in *)p->ai_addr;
        void *addr = &(ipv4->sin_addr);
        inet_ntop(AF_INET, addr, ipstr, sizeof(ipstr));
        return strdup(ipstr);
    }

    freeaddrinfo(res);

    return NULL;
}

// http_parse_url
void http_parse_url(char *url, char **hostname, char **resource, int *port) {
    //1.判断是否包含http://
    char *pos = strstr(url, "://");
    if (pos) {
        *hostname = pos + 3;
    } else {
        *hostname = url;
    }

    //2.判断是否包含端口号
    *resource = strchr(*hostname, '/');
    if (*resource) {
        **resource = '\0';
        (*resource)++;
    } else {
        *resource = "/";
    }

    //3.判断是否包含端口号
    *port = 80;
    pos = strchr(*hostname, ':');
    if (pos) {
        *port = atoi(pos + 1);
        *pos = '\0';
    }
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

// 定义get请求方法，使用http_request结构体，返回http_response，模仿python的requests库
http_response *http_request(char *method, char *url, char *body, char *headers) {
    //1.解析url
    char *hostname, *resource;
    int port;
    http_parse_url(url, &hostname, &resource, &port);
    printf("hostname: %s, resource: %s, port: %d\n", hostname, resource, port);
    //2.通过DNS将域名转为 IP
    char *ip = host_to_ip(hostname);
    if (ip == NULL) {
        printf("DNS解析失败\n");
        return NULL;
    }

    //3.创建socket
    int sockfd = http_create_socket(ip);
    if (sockfd < 0) {
        printf("创建socket失败\n");
        return NULL;
    }

    //4.构建http请求
    char buffer[BUFFER_SIZE];
    sprintf(buffer, "%s %s %s\r\n", method, resource, HTTP_VERSION);
    sprintf(buffer + strlen(buffer), "Host: %s\r\n", hostname);
    sprintf(buffer + strlen(buffer), "%s", CONNECTION_TYPE);
    if (headers != NULL) {
        sprintf(buffer + strlen(buffer), "%s", headers);
    }
    if (body != NULL) {
        sprintf(buffer + strlen(buffer), "Content-Length: %ld\r\n\r\n", strlen(body));
        sprintf(buffer + strlen(buffer), "%s", body);
    } else {
        sprintf(buffer + strlen(buffer), "\r\n");
    }

    //5.发送http请求
    ssize_t size = send(sockfd, buffer, strlen(buffer), 0);
    if (size < 0) {
        printf("发送http请求失败\n");
        return NULL;
    }

    //6.读取http响应
    http_response *response = (http_response *) malloc(sizeof(http_response));
    response->status_code = -1;
    response->body = NULL;
    response->headers = NULL;

    //6.1读取响应行
    char status_line[BUFFER_SIZE];
    size = read(sockfd, status_line, BUFFER_SIZE);
    if (size < 0) {
        printf("读取响应行失败\n");
        return NULL;
    }
    char *pos = strstr(status_line, " ");
    if (pos == NULL) {
        printf("响应行格式错误\n");
        return NULL;
    }
    *pos = '\0';
    response->status_code = atoi(pos + 1);

    //6.2读取响应头
    char header[BUFFER_SIZE];
    while (1) {
        memset(header, 0, BUFFER_SIZE);
        size = read(sockfd, header, BUFFER_SIZE);
        if (size <= 0) {
            break;
        }
        if (strcmp(header, "\r\n") == 0) {
            break;
        }
        header[strlen(header) - 2] = '\0';
        response->headers = realloc(response->headers, strlen(header) + 1);
        strcpy(response->headers, header);
    }
    //6.3读取响应体
    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        size = read(sockfd, buffer, BUFFER_SIZE);
        if (size <= 0) {
            break;
        }
        response->body = realloc(response->body, strlen(buffer) + 1);
        strcpy(response->body, buffer);
    }
    // 7.关闭socket
    close(sockfd);
    // 8.返回响应
    printf("response->status_code: %d\n", response->status_code);
    return response;
}

// 定义get请求方法
http_response *http_get(char *url, char *headers) {
    return http_request("GET", url, NULL, headers);
}
// 定义post请求方法
http_response *http_post(char *url, char *body, char *headers) {
    return http_request("POST", url, body, headers);
}