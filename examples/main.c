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
#include "GUI_Paint.h"
#include "GUI_BMP.h"
#include "GUI_Paint.h"
#include "fonts.h"

int *sock_fd;

int create_udp_socket();

float get_temperature();

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

void get_localhost_ip(char *);

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

void base_demo() {
    // Exception handling:ctrl + c
    signal(SIGINT, Handler_2IN_LCD);

    /* Module Init */
    if (DEV_ModuleInit() != 0) {
        DEV_ModuleExit();
        exit(0);
    }

    /* LCD Init */
    printf("2inch LCD demo...\n");
    LCD_2IN_Init();
    LCD_2IN_Clear(WHITE);
    LCD_SetBacklight(1010);

    UDOUBLE Imagesize = LCD_2IN_HEIGHT * LCD_2IN_WIDTH * 2;
    UWORD *BlackImage;
    if ((BlackImage = (UWORD *) malloc(Imagesize)) == NULL) {
        printf("Failed to apply for black memory...\n");
        exit(0);
    }

    // /*1.Create a new image cache named IMAGE_RGB and fill it with white*/
    Paint_NewImage(BlackImage, LCD_2IN_WIDTH, LCD_2IN_HEIGHT, 90, WHITE, 16);
    Paint_Clear(WHITE);
    Paint_SetRotate(ROTATE_270);
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
    DEV_ModuleExit();
}

void monitor() {
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
    short step = 0;
    time_t timep;
    struct tm *p_tm;
    PAINT_TIME p_time;
    int base_y = 10;
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
        // 本机主机名
        char hostname[128];
        gethostname(hostname, 128);
        Paint_DrawString_EN(80, base_y, hostname, &Font20, WHITE, BLACK);
        // 打印时间
        time((time_t * ) & timep);
        p_tm = localtime((time_t * ) & timep);

        p_time.Year = 1900 + p_tm->tm_year;
        p_time.Month = p_tm->tm_mon + 1;
        p_time.Day = p_tm->tm_mday;
        p_time.Hour = p_tm->tm_hour;
        p_time.Min = p_tm->tm_min;
        p_time.Sec = p_tm->tm_sec;

        Paint_DrawTime(5, 200, &p_time, &Font20, IMAGE_BACKGROUND, BLUE);
        // 本机IP信息
        char ip_info[1024] = {0};
        get_localhost_ip(ip_info);
        //printf("ip_info:%s\n", ip_info);
        char *pch;
        pch = strtok(ip_info, "\n");
        int line_height = 20;
        int i = 0;
        while (pch != NULL) {
            Paint_DrawString_EN(5, base_y + line_height * (i + 1), pch, &Font16, WHITE, BLACK);
            pch = strtok(NULL, "\n");
            i++;
        }
        // 本机温度信息
        float temperature = 0;
        temperature = get_temperature();
        char temperature_info[32] = {0};
        sprintf(temperature_info, "Temp:%.2f", temperature); // TODO:温度单位 ℃
        Paint_DrawString_EN(5, base_y + line_height * (i + 1), temperature_info, &Font16, WHITE, BLACK);
        LCD_2IN_Display((UBYTE *) BlackImage);
    }
    free(BlackImage);
    BlackImage = NULL;
    DEV_ModuleExit();
}

int main() {
    printf("start\n");
    monitor();
    return 0;
}
