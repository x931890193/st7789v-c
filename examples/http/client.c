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


// 定义http_response结构体
//typedef struct {
//    char version[16]; //http版本号
//    int status_code; //状态码
//    char status_text[16]; //状态码描述
//    http_header *headers; //响应头
//    int header_count; //响应头数量
//    char *body; //响应体
//} http_response;

// http_parse_response
http_response http_parse_response(const char *response_message) {
    http_response response;

    // 解析起始行
    sscanf(response_message, "%s %d %s", response.version, &response.status_code, response.status_text);

    // 解析头部字段
    int offset = strcspn(response_message, "\r\n") + 2; // 找到第一个换行符并向后偏移两个字符
    int i = 0;
    while (strncmp(response_message + offset, "\r\n", 2) != 0) {
        // 解析响应头的键
        int key_length = strcspn(response_message + offset, ":");
        response.headers[i]->key = malloc(key_length + 1);
        strncpy(response.headers[i]->key, response_message + offset, key_length);
        response.headers[i]->key[key_length] = '\0';

        // 解析响应头的值
        int value_start = offset + key_length + 2; // 偏移冒号和一个空格的长度
        int value_length = strcspn(response_message + value_start, "\r\n");
        response.headers[i]->value = malloc(value_length + 1);
        strncpy(response.headers[i]->value, response_message + value_start, value_length);
        response.headers[i]->value[value_length] = '\0';

        offset += value_start + value_length + 2; // 偏移冒号、空格和换行符的长度
        i++;
    }
    response.header_count = i;

    // 解析消息主体
    offset += 2; // 跳过最后的换行符
    int body_length = strlen(response_message) - offset;
    response.body = malloc(body_length + 1);
    strncpy(response.body, response_message + offset, body_length);
    response.body[body_length] = '\0';

    return response;
}

void free_http_response(http_response *response) {
    // 释放动态分配的内存
    for (int i = 0; i < response->header_count; i++) {
        free(response->headers[i]->key);
        free(response->headers[i]->value);
    }
    free(response->body);
}

// 定义get请求方法，使用http_request结构体，返回http_response，模仿python的requests库
http_response *http_request(char *method, char *url, char *body, char *headers) {
    //1.解析url
    char *hostname, *resource;
    int port;
    http_parse_url(url, &hostname, &resource, &port);
    // resource 不是以 / 开头的，需要加上 /
    if (resource[0] != '/') {
        char *temp = (char *) malloc(strlen(resource) + 2);
        sprintf(temp, "/%s", resource);
        resource = temp;
    }
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

    //4.构建http请求  根据不同的请求方法，构建不同的http请求参数
    char buffer[BUFFER_SIZE];
    if (strcasecmp(method, "GET") == 0) {
        sprintf(buffer, "GET %s HTTP/1.1\r\n", resource);
    } else if (strcasecmp(method, "POST") == 0) {
        sprintf(buffer, "POST %s HTTP/1.1\r\n", resource);
    } else if (strcasecmp(method, "PUT") == 0) {
        sprintf(buffer, "PUT %s HTTP/1.1\r\n", resource);
    } else if (strcasecmp(method, "DELETE") == 0) {
        sprintf(buffer, "DELETE %s HTTP/1.1\r\n", resource);
    } else {
        printf("不支持的http请求方法\n");
        return NULL;
    }
    // 4.1 添加host请求头
    sprintf(buffer + strlen(buffer), "Host: %s\r\n", hostname);
    // 4.2 添加自定义请求头
    if (headers != NULL) {
        sprintf(buffer + strlen(buffer), "%s\r\n", headers);
    }
    // 4.3 添加Content-Length请求头
    if (strcasecmp(method, "POST") == 0 || strcasecmp(method, "PUT") == 0) {
        sprintf(buffer + strlen(buffer), "Content-Length: %lu\r\n", strlen(body));
    }
    // 4.4 添加Connection: close请求头
    sprintf(buffer + strlen(buffer), "Connection: close\r\n");
    // 4.5 添加空行
    sprintf(buffer + strlen(buffer), "\r\n");
    // 4.6 添加body
    if (strcasecmp(method, "POST") == 0 || strcasecmp(method, "PUT") == 0) {
        sprintf(buffer + strlen(buffer), "%s\r\n", body);
    }

    //5.发送http请求
    ssize_t size = send(sockfd, buffer, strlen(buffer), 0);
    if (size < 0) {
        printf("发送http请求失败\n");
        return NULL;
    }
    //5.用select实现多路复用IO，循环检测是否有可读事件到来，从而进行recv
    fd_set fdset;
    FD_ZERO(&fdset);
    FD_SET(sockfd, &fdset);
    struct timeval timeout = {3, 0};
    int ret = select(sockfd + 1, &fdset, NULL, NULL, &timeout);
    if (ret < 0) {
        printf("select error\n");
        return NULL;
    } else if (ret == 0) {
        printf("select timeout\n");
        return NULL;
    }
    //5.1 读取响应数据
    char *result = (char *) malloc(sizeof(char) * BUFFER_SIZE);
    memset(result, 0x00, BUFFER_SIZE);
    while (1) {
        memset(buffer, 0x00, BUFFER_SIZE);
        size = recv(sockfd, buffer, BUFFER_SIZE - 1, 0);
        if (size <= 0) {
            break;
        }
        result = realloc(result, strlen(result) + size + 1);
        strncat(result, buffer, size);
    }
    // 解析result 字符串
    http_response response = http_parse_response(result);
    // 释放内存
    free(result);
    free(ip);
    free(hostname);
    free(resource);
    close(sockfd);
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