//
// Created by sato on 2023/5/29.
//

#include "DEV_Config.h"
#include "LCD_2inch.h"
#include "GUI_Paint.h"
#include "GUI_BMP.h"
#include "GUI_Paint.h"
#include "fonts.h"

#include <stdio.h>        //printf()
#include <stdlib.h>        //exit()
#include <signal.h>     //signal()
#include <time.h>


// datetime ico weather ico
void LCD_2IN() {
    // Exception handling:ctrl + c
    signal(SIGINT, Handler_2IN_LCD);

    /* Module Init */
    if (DEV_ModuleInit() != 0) {
        DEV_ModuleExit();
        exit(0);
    }

    /* LCD Init */
    printf("2inch LCD demo...\r\n");
    LCD_2IN_Init();
    LCD_2IN_Clear(WHITE);
    LCD_SetBacklight(1010);

    UDOUBLE Imagesize = LCD_2IN_HEIGHT * LCD_2IN_WIDTH * 2;
    UWORD *BlackImage;
    if ((BlackImage = (UWORD *) malloc(Imagesize)) == NULL) {
        printf("Failed to apply for black memory...\r\n");
        exit(0);
    }

    // /*1.Create a new image cache named IMAGE_RGB and fill it with white*/
    Paint_NewImage(BlackImage, LCD_2IN_WIDTH, LCD_2IN_HEIGHT, 90, WHITE, 16);
    Paint_Clear(WHITE);
    Paint_SetRotate(ROTATE_270);
    // /* GUI */
    printf("drawing...\r\n");

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
        printf("show bmp\r\n");
    } else {
        printf("show bmp error\r\n");
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

int main() {
    printf("start\n");
    LCD_2IN();
    return 0;
}
