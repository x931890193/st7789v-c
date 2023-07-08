//
// Created by sato on 2023/7/5.
//

// Color R5G6B5
// * data block size: 8 bit(s), uint8_t
// * bits per pixel: 16
#include "image_base.h"

const uint8_t time_hour_min_nums[10][4800] = {*image_data_0, *image_data_1, *image_data_2, *image_data_3,
                                              *image_data_4, *image_data_5, *image_data_6, *image_data_7,
                                              *image_data_8, *image_data_9};

const uint8_t time_sec_mu_nums[10][864] = {*image_data_l0, *image_data_l1, *image_data_l2, *image_data_l3,
                                           *image_data_l4, *image_data_l5, *image_data_l6, *image_data_l7,
                                           *image_data_l8, *image_data_l9};

const uint8_t week_day[7][6460] = {*image_data_sun, *image_data_mon, *image_data_tues, *image_data_wed,
                                   *image_data_thues, *image_data_fri, *image_data_sat};