//
// Created by sato on 2023/7/9.
//

#ifndef ST7789V_C_CLIENT_H
#define ST7789V_C_CLIENT_H

// MAX_HEADERS
#define MAX_HEADERS 128
// 定义http_header结构体
typedef struct {
    char *key; //响应头key
    char *value; //响应头value
} http_header; //响应头


// 定义http_response结构体
typedef struct {
    char version[16]; //http版本号
    int status_code; //状态码
    char status_text[16]; //状态码描述
    http_header *headers[MAX_HEADERS]; //响应头
    int header_count; //响应头数量
    char *body; //响应体
} http_response;

char * host_to_ip(const char *);

int http_create_socket(char *);

// 定义http_parse_url方法， 解析url， 返回hostname， resource， port
void http_parse_url(char *, char **, char **, int *);

// 定义http_send_request方法， 发送请求， 返回响应
http_response *http_request(char *method, char *url, char *body, char *headers);

// 定义http_parse_response方法， 解析响应， 返回http_response
http_response http_parse_response(char *response);

// 定义http_get
http_response *http_get(char *url, char *headers);

// 定义http_post
http_response *http_post(char *url, char *body, char *headers);

#endif //ST7789V_C_CLIENT_H
