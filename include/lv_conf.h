/**
 * @file lv_conf.h
 * Configuration for LVGL 9.x
 * Copy from lvgl/lv_conf_template.h and customize
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#ifndef __ASSEMBLY__
#include <stdint.h>
#endif

/* Color depth: 16-bit RGB565 for 800x480 display */
#define LV_COLOR_DEPTH 16

/* Use standard malloc (NO PSRAM on this board) */
#define LV_MEM_CUSTOM 1
#if LV_MEM_CUSTOM
    #define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
    #define LV_MEM_CUSTOM_ALLOC   malloc
    #define LV_MEM_CUSTOM_FREE    free
    #define LV_MEM_CUSTOM_REALLOC realloc
#endif

/* Display resolution */
#define LV_HOR_RES_MAX 800
#define LV_VER_RES_MAX 480

/* DPI for touch accuracy */
#define LV_DPI_DEF 130

/* Tick period for LVGL tasks */
#define LV_TICK_CUSTOM 1
#if LV_TICK_CUSTOM
    #define LV_TICK_CUSTOM_INCLUDE <Arduino.h>
    #define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())
#endif

/* Enable animations */
#define LV_USE_ANIMATION 1

/* Font sizes needed */
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_32 1

/* Enable widgets we use */
#define LV_USE_BTN 1
#define LV_USE_LABEL 1
#define LV_USE_BAR 1
#define LV_USE_CONT 1
#define LV_USE_PAGE 1
#define LV_USE_TABVIEW 1
#define LV_USE_WIN 1
#define LV_USE_ARC 1

/* Logging */
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN

/* Memory pool size */
#define LV_MEM_SIZE (32 * 1024) /* 32KB */

/* GPU - none, use CPU rendering */
#define LV_USE_GPU_ARM_MALI 0
#define LV_USE_GPU_STM32_DMA2D 0

/* Enable optional modules */
#define LV_USE_PERF_MONITOR 0

#endif /* LV_CONF_H */
