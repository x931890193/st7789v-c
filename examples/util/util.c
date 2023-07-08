//
// Created by sato on 2023/7/8.
//
#include <time.h>
#include "util.h"


PAINT_TIME get_time() {
    PAINT_TIME p_time;
    time_t timep;
    struct tm *p_tm;
    time((time_t * ) & timep);
    p_tm = localtime((time_t * ) & timep);
    printf("%p\n", p_tm);
    p_time.Year = 1900 + p_tm->tm_year;
    p_time.Month = p_tm->tm_mon + 1;
    p_time.Day = p_tm->tm_mday;
    p_time.Hour = p_tm->tm_hour;
    p_time.Min = p_tm->tm_min;
    p_time.Sec = p_tm->tm_sec;
    return p_time;
}
