//
// Created by sato on 2023/7/8.
//

#ifndef ST7789V_C_IMAGE_BASE_H
#define ST7789V_C_IMAGE_BASE_H

typedef struct {
    const uint8_t *data;
    uint16_t width;
    uint16_t height;
    uint8_t dataSize;
} tImage;


#include "0.h"
#include "1.h"
#include "2.h"
#include "3.h"
#include "4.h"
#include "5.h"
#include "6.h"
#include "7.h"
#include "8.h"
#include "9.h"
#include "l0.h"
#include "l1.h"
#include "l2.h"
#include "l3.h"
#include "l4.h"
#include "l5.h"
#include "l6.h"
#include "l7.h"
#include "l8.h"
#include "l9.h"
#include "l0c.h"
#include "l1c.h"
#include "l2c.h"
#include "l3c.h"
#include "l4c.h"
#include "l5c.h"
#include "l6c.h"
#include "l7c.h"
#include "l8c.h"
#include "l9c.h"
#include "sun.h"
#include "mon.h"
#include "tues.h"
#include "wed.h"
#include "thues.h"
#include "fri.h"
#include "sat.h"
#include "mao.h"
#include "temp.h"
#include "per.h"

uint8_t *time_hour_min_nums[10][4800] = {image_data_0, image_data_1, image_data_2, image_data_3,
                                         image_data_4, image_data_5, image_data_6, image_data_7,
                                         image_data_8, image_data_9}; // 0-9

uint8_t *time_sec_mu_nums[10][864] = {image_data_l0, image_data_l1, image_data_l2, image_data_l3,
                                      image_data_l4, image_data_l5, image_data_l6, image_data_l7,
                                      image_data_l8, image_data_l9}; // 0-9

uint8_t *week_day[7][6460] = {image_data_sun, image_data_mon, image_data_tues, image_data_wed,
                              image_data_thues, image_data_fri, image_data_sat}; // 0-6

#endif //ST7789V_C_IMAGE_BASE_H
