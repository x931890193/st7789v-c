//
// Created by sato on 2023/7/9.
//

#ifndef ST7789V_C_CLIENT_H
#define ST7789V_C_CLIENT_H

// 完整的HTTP响应结构体 包含响应头和响应体状态码
// 定义http_response结构体
typedef struct {
    char version[16]; //http版本号
    int status_code; //状态码
    char status_text[16]; //状态码描述
    http_header *headers; //响应头
    int header_count; //响应头数量
    char *body; //响应体
} http_response;

// 定义http_header结构体
typedef struct {
    char *key; //响应头key
    char *value; //响应头value
} http_header; //响应头

// 定义http_request结构体
typedef struct {
    char *method; //请求方法
    char *path; //请求路径
    char *version; //http版本号
    http_header *headers; //请求头
    int header_count; //请求头数量
    char *body; //请求体
} http_request;

// 定义http_header结构体
typedef struct {
    char *key; //请求头key
    char *value; //请求头value
} http_header; //请求头


char * host_to_ip(const char *);

int http_create_socket(char *);
// 定义get请求方法， 返回http_response， 模仿python的requests库
http_response *http_get(char *);
// 定义post请求方法， 返回http_response， 模仿python的requests库
http_response *http_post(char *, char *);
// 定义http_parse_url方法， 解析url， 返回hostname， resource， port
void http_parse_url(char *, char **, char **, int *);
// 定义http_send_request方法， 发送请求， 返回响应
http_response *http_request(char *method, char *url, char *body, char *headers);
// 定义http_get
http_response *http_get(char *url, char *headers);
// 定义http_post
http_response *http_post(char *url, char *body, char *headers);

#endif //ST7789V_C_CLIENT_H
