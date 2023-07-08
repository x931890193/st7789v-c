//
// Created by sato on 2023/5/29.
//
#include <arpa/inet.h>
#include <stdio.h>        //printf()
#include <stdlib.h>       //exit()
#include <signal.h>       //signal()
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <string.h>


#include "DEV_Config.h"
#include "LCD_2inch.h"
#include "GUI_BMP.h"
#include "GUI_Paint.h"
#include "fonts.h"
#include "cjson.h"
#include "image_base.h"
#include "util.h"

#define BROADCAST_ADDR "255.255.255.255"
#define BROADCAST_PORT 4000
#define COUNT  10
#define SendSleepTime 3

struct BroadcastInfo {
    char host[64];
    char from[64];
    char temp[8];
    char time[64];
    char ip[32];
};

int *sock_fd;
int create_udp_socket();
float get_temperature();
void get_localhost_ip(char *);
void *broadcast_receiver();
void *parse_broadcast_to_struct(char *buffer, char *ip);
void show_broadcast_info();

// 定义一个数组存放广播信息
struct BroadcastInfo *broadcast_info[COUNT];

// 创建udp套接字
int create_udp_socket() {
    // 如果已经创建过套接字，直接返回
    if (sock_fd != NULL) {
        return 0;
    }
    sock_fd = (int *) malloc(sizeof(int));
    *sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (*sock_fd < 0) {
        printf("create socket error\n");
        return -1;
    }
    return 0;
}

// 获取温度
float get_temperature() {
    FILE *fp;
    char buf[128];
    char temperature[10];
    float temp;
    fp = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (fp == NULL) {
        printf("open file error\n");
        return -1;
    }
    fgets(buf, sizeof(buf), fp);
    strncpy(temperature, buf, 2);
    temperature[2] = '.';
    strncpy(temperature + 3, buf + 2, 3);
    temp = atof(temperature);
    fclose(fp);
    return temp;
}

// 获取本机ip
void get_localhost_ip(char *ip_info) {
    int interface_idx = 0;  // 网络接口索引
    struct ifconf ifc;
    char buf[1024] = {0};   // 缓冲区
    char ip[20] = {0};      // ip地址
    char *interface;        // 网络接口

    struct ifreq *ifr;

    ifc.ifc_len = 1024;
    ifc.ifc_buf = buf;
    create_udp_socket();
    ioctl(*sock_fd, SIOCGIFCONF, &ifc);
    ifr = (struct ifreq *) buf;
    for (interface_idx = (ifc.ifc_len / sizeof(struct ifreq)); interface_idx > 0; interface_idx--) {
        if (strcmp(ifr->ifr_name, "lo") == 0) {
            ifr = ifr + 1;
            continue;
        }
        interface = ifr->ifr_name;
        if (strlen(interface) > 5) { interface = "wifi"; }
        strcat(ip_info, interface);
        strcat(ip_info, ":");
        inet_ntop(AF_INET, &((struct sockaddr_in *) &ifr->ifr_addr)->sin_addr, ip, 20);
        strcat(ip_info, ip);
        strcat(ip_info, "\n");
        ifr = ifr + 1;
    }
}

// 发送广播
void *sender() {
    //1.创建udp套接字
    int sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd < 0) {
        perror("socket error");
        return (void *) -1;
    }
    //2.开启广播
    int on = 1;
    int ret = setsockopt(sock_fd, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
    if (ret < 0) {
        perror("setsockopt error");
        goto err;
    }

    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;//地址族IPV4
    dest_addr.sin_port = htons(BROADCAST_PORT);//设置端口号
    dest_addr.sin_addr.s_addr = inet_addr(BROADCAST_ADDR);//设置广播地址
    char time_str[128] = "";
    time_t now;

    while (1) {
        char host_buffer[128];
        ret = gethostname(host_buffer, sizeof(host_buffer));
        if (ret < 0) {
            perror("get hostname error");
            goto err;
        }
        char *to_send = (char *) malloc(1024);
        time(&now);
        strftime(time_str, 128, "%Y-%m-%d %H:%M:%S", localtime(&now));

        float temp = get_temperature();
        sprintf(to_send, "{\"host\": \"%s\", \"time\": \"%s\", \"from\": \"C\", \"temp\": \"%.2f\"}", host_buffer, time_str, temp);
        // 3.发送数据
        ret = sendto(sock_fd, to_send, strlen(to_send), 0, (struct sockaddr *) &dest_addr, sizeof(dest_addr));
        free(to_send);
        if (ret < 0) {
            perror("sendto error");
            goto err;
        }
        sleep(SendSleepTime);
    }
    err:
    // 4.关闭套接字
    close(sock_fd);
    return NULL;
}

// 广播接收线程
void *broadcast_receiver() {
    struct sockaddr_in addr;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    char buffer[1024];
    int recv_len;
    create_udp_socket();
    addr.sin_family = AF_INET;
    addr.sin_port = htons(BROADCAST_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(*sock_fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        printf("bind error\n");
        return NULL;
    }
    while (1) {
        memset(buffer, 0, sizeof(buffer));
        recv_len = recvfrom(*sock_fd, buffer, sizeof(buffer), 0, (struct sockaddr *) &client_addr, &client_addr_len);
        if (recv_len < 0) {
            printf("recvfrom error\n");
            continue;
        }
        // 解析广播信息
        char to_analyze_buffer[1024] = {0};
        char *ipString = inet_ntoa(client_addr.sin_addr);

        strncpy(to_analyze_buffer, buffer, recv_len);
        parse_broadcast_to_struct(to_analyze_buffer, ipString);
    }
}

// 解析广播信息 根据host 放到一个广播数组中
void *parse_broadcast_to_struct(char *buffer, char *ip) {
    cJSON *json;

    cJSON *host;
    cJSON *from;
    cJSON *temp;
    cJSON *time;

    struct BroadcastInfo *info;
    json = cJSON_Parse(buffer);
    if (json == NULL) {
        printf("parse json error\n");
        return NULL;
    }
    host = cJSON_GetObjectItem(json, "host");
    from = cJSON_GetObjectItem(json, "from");
    temp = cJSON_GetObjectItem(json, "temp");
    time = cJSON_GetObjectItem(json, "time");

    info = (struct BroadcastInfo *) malloc(sizeof(struct BroadcastInfo));
    strcpy(info->host, host->valuestring);
    strcpy(info->from, from->valuestring);
    strcpy(info->temp, temp->valuestring);
    strcpy(info->time, time->valuestring);
    strcpy(info->ip, ip);
    for (int i = 0; i < 10; i++) {
        if (broadcast_info[i] == NULL) {
            broadcast_info[i] = info;
            break;
        }
        if (strcmp(broadcast_info[i]->host, info->host) == 0) {
            broadcast_info[i] = info;
            break;
        }
    }
    return NULL;
}

// 循环广播数组显示信息
void show_broadcast_info() {
    int base_y = 6;
    int base_x = 5;
    int line_height = 16;
    for (int i = 0; i < COUNT; i++) {
        if (broadcast_info[i] != NULL) {
            // hostname
            char hostname_info[128] = {0};
            sprintf(hostname_info, "%s", broadcast_info[i]->host);
            Paint_DrawString_EN((LCD_2IN_HEIGHT - strlen(hostname_info)) / 5, base_y + line_height * 0,  hostname_info, &Font16, WHITE, BLACK);
            // ip
            char ip_info[128] = {0};
            sprintf(ip_info, "IP:%s", broadcast_info[i]->ip);
            Paint_DrawString_EN(base_x, base_y + line_height * 1, ip_info, &Font12, WHITE, BLACK);
            // temp
            char temperature_info[32] = {0};
            sprintf(temperature_info, "Temp:%s'C", broadcast_info[i]->temp); // TODO:温度单位 ℃
            Paint_DrawString_EN(base_x, base_y + line_height * 2, temperature_info, &Font12, WHITE, BLACK);
            // time
            char time_info[128] = {0};
            sprintf(time_info, "Time:%s", broadcast_info[i]->time);
            Paint_DrawString_EN(base_x, base_y + line_height * 3, time_info, &Font12, WHITE, BLACK);
            base_y = base_y + line_height * 4;
        }
    }
}

// 初始化lcd
UWORD *set_up()
{
    // Exception handling:ctrl + c
    signal(SIGINT, Handler_2IN_LCD);

    /* Module Init */
    if (DEV_ModuleInit() != 0) {
        DEV_ModuleExit();
        exit(0);
    }

    /* LCD Init */
    printf("2inch LCD monitor...\n");
    LCD_2IN_Init();
    LCD_2IN_Clear(WHITE);
    LCD_SetBacklight(1010);

    UDOUBLE Imagesize = LCD_2IN_HEIGHT * LCD_2IN_WIDTH * 2;
    UWORD *BlackImage;
    if ((BlackImage = (UWORD *) malloc(Imagesize)) == NULL) {
        printf("Failed to apply for black memory...\n");
        exit(0);
    }

    // *1.Create a new image cache named IMAGE_RGB and fill it with white*/
    Paint_NewImage(BlackImage, LCD_2IN_WIDTH, LCD_2IN_HEIGHT, 90, WHITE, 16);
    Paint_Clear(WHITE);
    Paint_SetRotate(ROTATE_270);
    return BlackImage;
}

// official demo
void base_demo() {
    UWORD *BlackImage;
    BlackImage = set_up();
    // /* GUI */
    printf("drawing...\n");

    // /*2.Drawing on the image*/
    Paint_DrawPoint(5, 10, BLACK, DOT_PIXEL_1X1, DOT_STYLE_DFT);
    Paint_DrawPoint(5, 25, BLACK, DOT_PIXEL_2X2, DOT_STYLE_DFT);
    Paint_DrawPoint(5, 40, BLACK, DOT_PIXEL_3X3, DOT_STYLE_DFT);
    Paint_DrawPoint(5, 55, BLACK, DOT_PIXEL_4X4, DOT_STYLE_DFT);

    Paint_DrawLine(20, 10, 70, 60, RED, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(70, 10, 20, 60, RED, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(170, 15, 170, 55, RED, DOT_PIXEL_1X1, LINE_STYLE_DOTTED);
    Paint_DrawLine(150, 35, 190, 35, RED, DOT_PIXEL_1X1, LINE_STYLE_DOTTED);

    Paint_DrawRectangle(20, 10, 70, 60, BLUE, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
    Paint_DrawRectangle(85, 10, 130, 60, BLUE, DOT_PIXEL_1X1, DRAW_FILL_FULL);

    Paint_DrawCircle(170, 35, 20, GREEN, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
    Paint_DrawCircle(170, 85, 20, GREEN, DOT_PIXEL_1X1, DRAW_FILL_FULL);

    Paint_DrawString_EN(5, 70, "hello world", &Font16, WHITE, BLACK);
    Paint_DrawString_EN(5, 90, "waveshare", &Font20, RED, IMAGE_BACKGROUND);

    Paint_DrawNum(5, 160, 123456789, &Font20, GREEN, IMAGE_BACKGROUND);
    Paint_DrawString_CN(5, 200, "΢ѩ����", &Font24CN, IMAGE_BACKGROUND, BLUE);

    // /*3.Refresh the picture in RAM to LCD*/
    LCD_2IN_Display((UBYTE *) BlackImage);
    DEV_Delay_ms(300);

    int status = GUI_ReadBmp("./pic/LCD_2inch.bmp");
    if (status == 0) {
        printf("show bmp\n");
    } else {
        printf("show bmp error\n");
        exit(-1);
    }
    LCD_2IN_Display((UBYTE *) BlackImage);
    DEV_Delay_ms(300);
    //#else
    short step = 0;
    time_t timep;
    struct tm *p_tm;
    PAINT_TIME p_time;
    while (1) {
        Paint_Clear(WHITE);
        switch (step) {
            case 0:
                GUI_ReadBmp("./pic/visionfive.bmp");
                //Paint_DrawRectangle(85, 10, 130, 60, GREEN, DOT_PIXEL_1X1, DRAW_FILL_FULL);
                step++;
                break;
                /*case 1:
                    Paint_DrawRectangle(85, 10, 130, 60, BLUE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
                    Paint_DrawCircle(170, 85, 20, GREEN, DOT_PIXEL_1X1, DRAW_FILL_FULL);
                    step++;
                    break;
                case 2:
                    Paint_DrawRectangle(85, 10, 130, 60, RED, DOT_PIXEL_1X1, DRAW_FILL_FULL);
                    Paint_DrawCircle(170, 35, 20, GREEN, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
                    step++;
                    break;*/
            case 1:
                GUI_ReadBmp("./pic/LCD_2inch.bmp");
                step = 0;
                break;
            default:
                break;
        }
        Paint_DrawString_EN(5, 70, "hello world", &Font16, WHITE, BLACK);
        time((time_t * ) & timep);
        p_tm = localtime((time_t * ) & timep);

        p_time.Year = 1900 + p_tm->tm_year;
        p_time.Month = p_tm->tm_mon + 1;
        p_time.Day = p_tm->tm_mday;
        p_time.Hour = p_tm->tm_hour;
        p_time.Min = p_tm->tm_min;
        p_time.Sec = p_tm->tm_sec;

        printf("%4d-%2d-%2d %2d:%2d:%2d\n", 1900 + p_tm->tm_year, p_tm->tm_mon + 1, p_tm->tm_mday, p_tm->tm_hour,
               p_tm->tm_min, p_tm->tm_sec);
        Paint_DrawTime(5, 200, &p_time, &Font20, IMAGE_BACKGROUND, BLUE);

        LCD_2IN_Display((UBYTE *) BlackImage);
        DEV_Delay_ms(1000);
    }
    /* Module Exit */
    free(BlackImage);
    BlackImage = NULL;
}

// broadcast demo
void *broadcast_demo() {
    UWORD *BlackImage;
    BlackImage = set_up();
    short step = 0;
    time_t timep;
    struct tm *p_tm;
    PAINT_TIME p_time;

    // main loop
    while (1) {
        Paint_Clear(WHITE);
        switch (step) {
            case 0:
                step++;
                break;
            case 1:
                step = 0;
                break;
            default:
                exit(0);
        }
        // 打印时间
        time((time_t * ) & timep);
        p_tm = localtime((time_t * ) & timep);
        p_time.Year = 1900 + p_tm->tm_year;
        p_time.Month = p_tm->tm_mon + 1;
        p_time.Day = p_tm->tm_mday;
        p_time.Hour = p_tm->tm_hour;
        p_time.Min = p_tm->tm_min;
        p_time.Sec = p_tm->tm_sec;
        Paint_DrawTime(200, 200, &p_time, &Font20, IMAGE_BACKGROUND, BLUE);
        show_broadcast_info();
        LCD_2IN_Display((UBYTE *) BlackImage);
        DEV_Delay_ms(500);
    }
    free(BlackImage);
    BlackImage = NULL;
}

void draw_time() {
    PAINT_TIME p_time;
    p_time = get_time();
    UBYTE hour_ten, hour_unit, min_ten, min_unit, sec_ten, sec_unit;
    hour_ten = p_time.Hour / 10;
    hour_unit = p_time.Hour % 10;
    min_ten = p_time.Min / 10;
    min_unit = p_time.Min % 10;
    sec_ten = p_time.Sec / 10;
    sec_unit = p_time.Sec % 10;
    printf("hour_ten:%d,hour_unit:%d,min_ten:%d,min_unit:%d,sec_ten:%d,sec_unit:%d\n", hour_ten, hour_unit, min_ten,
           min_unit, sec_ten, sec_unit);
    Paint_DrawImage(time_hour_min_nums[hour_ten ? hour_ten - 1: hour_ten], 38, 75, 40, 60);
    Paint_DrawImage(time_hour_min_nums[hour_unit ? hour_unit - 1: hour_unit], 38 + 40, 75, 40, 60);
    Paint_DrawImage(image_data_mao, 38 + 40, 75, 40, 60);
    Paint_DrawImage(time_hour_min_nums[min_ten ? min_ten - 1: min_ten], 38 + 80, 75, 40, 60);
    Paint_DrawImage(time_hour_min_nums[min_unit ? min_unit - 1: min_unit], 38 + 120, 75, 40, 60);
    Paint_DrawImage(time_sec_mu_nums[sec_ten ? sec_ten - 1: sec_ten], 38 + 160, 75 + 30, 18, 24);
    Paint_DrawImage(time_sec_mu_nums[sec_unit ? sec_unit - 1: sec_unit], 38 + 180, 75 + 30, 18, 24);
}
// desktop显示
void *desktop() {
    UWORD *BlackImage;
    BlackImage = set_up();
    draw_time();
    while (1) {
        Paint_Clear(WHITE);
        draw_time();
        LCD_2IN_Display((UBYTE *) BlackImage);
    }
    free(BlackImage);
}

int main() {
    pthread_t p_send, p_recv, p_display;
    pthread_create(&p_send, NULL, sender, NULL);
    pthread_create(&p_recv, NULL, broadcast_receiver, NULL);
    pthread_create(&p_display, NULL, desktop, NULL);

    pthread_join(p_send, NULL);
    pthread_join(p_recv, NULL);
    pthread_join(p_display, NULL);

    return 0;
}
