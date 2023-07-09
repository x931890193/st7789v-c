//
// Created by sato on 2023/7/9.
//

#ifndef ST7789V_C_CLIENT_H
#define ST7789V_C_CLIENT_H

char * host_to_ip(const char *);
int http_create_socket(char *);
char * http_send_request(const char *, const char *)

#endif //ST7789V_C_CLIENT_H
