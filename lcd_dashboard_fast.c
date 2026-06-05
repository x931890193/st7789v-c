// lcd_dashboard_fast.c
// Fast 240x320 ST7789V dashboard for Raspberry Pi + Waveshare 2inch SPI LCD.
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <ifaddrs.h>
#include <math.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>
#include <wiringPi.h>
#include <wiringPiSPI.h>

#ifdef __has_include
#if __has_include(<softPwm.h>)
#include <softPwm.h>
#define HAS_SOFTPWM 1
#endif
#endif
#ifndef HAS_SOFTPWM
#define HAS_SOFTPWM 0
#endif

#define WIDTH 240
#define HEIGHT 320

#define LCD_CS 8
#define LCD_RST 17
#define LCD_DC 21
#define LCD_BL 24

#define SPI_CHANNEL 0
#define SPI_SPEED 40000000
#define SPI_CHUNK 4096

#define ST7789_SWRESET 0x01
#define ST7789_SLPOUT 0x11
#define ST7789_NORON 0x13
#define ST7789_INVON 0x21
#define ST7789_DISPON 0x29
#define ST7789_CASET 0x2A
#define ST7789_RASET 0x2B
#define ST7789_RAMWR 0x2C
#define ST7789_COLMOD 0x3A
#define ST7789_MADCTL 0x36

#define MADCTL_DEFAULT 0x80

#define RGB565(r, g, b) (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} Color;

typedef struct {
    char city[32];
    char temp[12];
    char feels[12];
    char humidity[12];
    char wind[16];
    char desc[64];
    int online;
    time_t next_fetch;
    time_t last_success;
} Weather;

typedef struct {
    char ifname[IFNAMSIZ];
    char ip[INET_ADDRSTRLEN];
    char state[8];
    unsigned long long prev_rx;
    unsigned long long prev_tx;
    double rx_rate;
    double tx_rate;
    time_t last_sample;
} NetInfo;

typedef struct {
    int brightness_day;
    int brightness_dim;
    int brightness_night;
    int night_start;
    int night_end;
    int dim_evening_hour;
    int dim_morning_hour;
    int cpu_alert;
    int mem_alert;
    int temp_alert;
    int disk_alert;
    int net_smoothing_seconds;
    char weather_location[64];
} DashboardConfig;

typedef struct {
    Weather weather;
    float cpu;
    float temp;
    int mem_pct;
    long long mem_used;
    long long mem_total;
    int disk_pct;
    double disk_free;
    double load_avg;
    char host[32];
    char uptime[24];
} DashboardData;

static uint16_t framebuffer[WIDTH * HEIGHT];
static uint8_t txbuf[WIDTH * HEIGHT * 2];
static uint8_t madctl = MADCTL_DEFAULT;

static const Color C_BG_TOP = {7, 13, 22};
static const Color C_BG_BOTTOM = {24, 18, 34};
static const Color C_CARD_TOP = {23, 29, 46};
static const Color C_CARD_BOTTOM = {16, 21, 34};
static const Color C_PANEL_TOP = {16, 85, 99};
static const Color C_PANEL_BOTTOM = {36, 42, 79};
static const Color C_WHITE = {248, 250, 252};
static const Color C_MUTED = {151, 164, 190};
static const Color C_CYAN = {45, 212, 191};
static const Color C_BLUE = {96, 165, 250};
static const Color C_YELLOW = {251, 191, 36};
static const Color C_PINK = {244, 114, 182};
static const Color C_CLOUD = {215, 226, 245};
static const Color C_TRACK = {34, 41, 58};
static const Color C_OUTLINE = {50, 62, 90};
static const Color C_DANGER = {248, 113, 113};

static int backlight_pwm_enabled = 0;
static int backlight_percent = -1;
static int preview_mode = 0;
static char preview_path[256] = "";
static NetInfo net_info = {.ifname = "NET", .ip = "0.0.0.0", .state = "DOWN"};
static DashboardConfig config = {
    .brightness_day = 100,
    .brightness_dim = 60,
    .brightness_night = 28,
    .night_start = 23,
    .night_end = 7,
    .dim_evening_hour = 22,
    .dim_morning_hour = 7,
    .cpu_alert = 80,
    .mem_alert = 85,
    .temp_alert = 70,
    .disk_alert = 85,
    .net_smoothing_seconds = 5,
    .weather_location = "",
};

// ASCII 32..90, enough for uppercase labels, digits, punctuation and IP text.
static const uint8_t font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x5F,0x00,0x00},
    {0x00,0x07,0x00,0x07,0x00}, {0x14,0x7F,0x14,0x7F,0x14},
    {0x24,0x2A,0x7F,0x2A,0x12}, {0x23,0x13,0x08,0x64,0x62},
    {0x36,0x49,0x55,0x22,0x50}, {0x00,0x05,0x03,0x00,0x00},
    {0x00,0x1C,0x22,0x41,0x00}, {0x00,0x41,0x22,0x1C,0x00},
    {0x14,0x08,0x3E,0x08,0x14}, {0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00}, {0x08,0x08,0x08,0x08,0x08},
    {0x00,0x60,0x60,0x00,0x00}, {0x20,0x10,0x08,0x04,0x02},
    {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10}, {0x27,0x45,0x45,0x45,0x39},
    {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E},
    {0x00,0x36,0x36,0x00,0x00}, {0x00,0x56,0x36,0x00,0x00},
    {0x08,0x14,0x22,0x41,0x00}, {0x14,0x14,0x14,0x14,0x14},
    {0x00,0x41,0x22,0x14,0x08}, {0x02,0x01,0x51,0x09,0x06},
    {0x32,0x49,0x79,0x41,0x3E}, {0x7E,0x11,0x11,0x11,0x7E},
    {0x7F,0x49,0x49,0x49,0x36}, {0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C}, {0x7F,0x49,0x49,0x49,0x41},
    {0x7F,0x09,0x09,0x09,0x01}, {0x3E,0x41,0x49,0x49,0x7A},
    {0x7F,0x08,0x08,0x08,0x7F}, {0x00,0x41,0x7F,0x41,0x00},
    {0x20,0x40,0x41,0x3F,0x01}, {0x7F,0x08,0x14,0x22,0x41},
    {0x7F,0x40,0x40,0x40,0x40}, {0x7F,0x02,0x0C,0x02,0x7F},
    {0x7F,0x04,0x08,0x10,0x7F}, {0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06}, {0x3E,0x41,0x51,0x21,0x5E},
    {0x7F,0x09,0x19,0x29,0x46}, {0x46,0x49,0x49,0x49,0x31},
    {0x01,0x01,0x7F,0x01,0x01}, {0x3F,0x40,0x40,0x40,0x3F},
    {0x1F,0x20,0x40,0x20,0x1F}, {0x3F,0x40,0x38,0x40,0x3F},
    {0x63,0x14,0x08,0x14,0x63}, {0x07,0x08,0x70,0x08,0x07},
    {0x61,0x51,0x49,0x45,0x43},
};

static uint16_t color565(Color c) {
    return RGB565(c.r, c.g, c.b);
}

static Color mix_color(Color a, Color b, float t) {
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    Color c = {
        (uint8_t)(a.r + (b.r - a.r) * t),
        (uint8_t)(a.g + (b.g - a.g) * t),
        (uint8_t)(a.b + (b.b - a.b) * t),
    };
    return c;
}

static int clamp_int(int value, int low, int high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static void upper_ascii(char *s) {
    for (; *s; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch >= 'a' && ch <= 'z') {
            *s = (char)toupper(ch);
        } else if (ch < 32 || ch > 126) {
            *s = ' ';
        }
    }
}

static void fit_text(char *s, size_t size, int max_px, int scale) {
    int max_chars = max_px / (6 * scale);
    if (max_chars < 0) max_chars = 0;
    if ((int)strlen(s) > max_chars && max_chars + 1 < (int)size) {
        if (max_chars >= 2) {
            s[max_chars - 1] = '.';
            s[max_chars] = '\0';
        } else {
            s[max_chars] = '\0';
        }
    }
}

static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) {
        end--;
        *end = '\0';
    }
    return s;
}

static void set_config_value(const char *key, const char *value) {
    int ivalue = atoi(value);
    if (strcmp(key, "brightness_day") == 0) config.brightness_day = clamp_int(ivalue, 1, 100);
    else if (strcmp(key, "brightness_dim") == 0) config.brightness_dim = clamp_int(ivalue, 1, 100);
    else if (strcmp(key, "brightness_night") == 0) config.brightness_night = clamp_int(ivalue, 1, 100);
    else if (strcmp(key, "night_start") == 0) config.night_start = clamp_int(ivalue, 0, 23);
    else if (strcmp(key, "night_end") == 0) config.night_end = clamp_int(ivalue, 0, 23);
    else if (strcmp(key, "dim_evening_hour") == 0) config.dim_evening_hour = clamp_int(ivalue, 0, 23);
    else if (strcmp(key, "dim_morning_hour") == 0) config.dim_morning_hour = clamp_int(ivalue, 0, 23);
    else if (strcmp(key, "cpu_alert") == 0) config.cpu_alert = clamp_int(ivalue, 1, 100);
    else if (strcmp(key, "mem_alert") == 0) config.mem_alert = clamp_int(ivalue, 1, 100);
    else if (strcmp(key, "temp_alert") == 0) config.temp_alert = clamp_int(ivalue, 1, 120);
    else if (strcmp(key, "disk_alert") == 0) config.disk_alert = clamp_int(ivalue, 1, 100);
    else if (strcmp(key, "net_smoothing_seconds") == 0) config.net_smoothing_seconds = clamp_int(ivalue, 1, 30);
    else if (strcmp(key, "weather_location") == 0) {
        snprintf(config.weather_location, sizeof(config.weather_location), "%s", value);
        trim(config.weather_location);
    }
}

static void load_config(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) return;
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = trim(line);
        char *value = trim(eq + 1);
        if (!key[0]) continue;
        set_config_value(key, value);
    }
    fclose(fp);
}

static void spi_write(uint8_t *data, int len) {
    digitalWrite(LCD_CS, LOW);
    for (int pos = 0; pos < len; pos += SPI_CHUNK) {
        int chunk = len - pos;
        if (chunk > SPI_CHUNK) chunk = SPI_CHUNK;
        wiringPiSPIDataRW(SPI_CHANNEL, data + pos, chunk);
    }
    digitalWrite(LCD_CS, HIGH);
}

static void lcd_cmd(uint8_t cmd) {
    digitalWrite(LCD_DC, LOW);
    spi_write(&cmd, 1);
}

static void lcd_data8(uint8_t data) {
    digitalWrite(LCD_DC, HIGH);
    spi_write(&data, 1);
}

static void lcd_data(const uint8_t *data, int len) {
    digitalWrite(LCD_DC, HIGH);
    digitalWrite(LCD_CS, LOW);
    for (int pos = 0; pos < len; pos += SPI_CHUNK) {
        int chunk = len - pos;
        if (chunk > SPI_CHUNK) chunk = SPI_CHUNK;
        memcpy(txbuf, data + pos, chunk);
        wiringPiSPIDataRW(SPI_CHANNEL, txbuf, chunk);
    }
    digitalWrite(LCD_CS, HIGH);
}

static void lcd_data16(uint16_t data) {
    uint8_t buf[2] = {(uint8_t)(data >> 8), (uint8_t)(data & 0xFF)};
    lcd_data(buf, 2);
}

static void lcd_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    lcd_cmd(ST7789_CASET);
    lcd_data16(x);
    lcd_data16(x + w - 1);
    lcd_cmd(ST7789_RASET);
    lcd_data16(y);
    lcd_data16(y + h - 1);
    lcd_cmd(ST7789_RAMWR);
}

static void lcd_init(void) {
    digitalWrite(LCD_RST, HIGH);
    delay(50);
    digitalWrite(LCD_RST, LOW);
    delay(50);
    digitalWrite(LCD_RST, HIGH);
    delay(150);

    lcd_cmd(ST7789_SWRESET);
    delay(150);
    lcd_cmd(ST7789_SLPOUT);
    delay(120);

    lcd_cmd(ST7789_COLMOD);
    lcd_data8(0x05);
    delay(10);

    lcd_cmd(ST7789_MADCTL);
    lcd_data8(madctl);
    delay(10);

    lcd_cmd(0xB2);
    lcd_data8(0x0C); lcd_data8(0x0C); lcd_data8(0x00); lcd_data8(0x33); lcd_data8(0x33);
    lcd_cmd(0xB7);
    lcd_data8(0x35);
    lcd_cmd(0xBB);
    lcd_data8(0x20);
    lcd_cmd(0xC2);
    lcd_data8(0x01); lcd_data8(0xFF);
    lcd_cmd(0xC3);
    lcd_data8(0x0B);
    lcd_cmd(0xC4);
    lcd_data8(0x20);
    lcd_cmd(0xC5);
    lcd_data8(0x20);
    lcd_cmd(0xD0);
    lcd_data8(0xA4); lcd_data8(0xA1);

    lcd_cmd(0xE0);
    uint8_t gamma_pos[] = {0xD0,0x00,0x02,0x07,0x0A,0x28,0x32,0x44,0x42,0x06,0x0E,0x12,0x14,0x17};
    lcd_data(gamma_pos, (int)sizeof(gamma_pos));
    lcd_cmd(0xE1);
    uint8_t gamma_neg[] = {0xD0,0x00,0x02,0x07,0x0A,0x28,0x31,0x54,0x47,0x0E,0x1C,0x17,0x1B,0x1E};
    lcd_data(gamma_neg, (int)sizeof(gamma_neg));

    lcd_cmd(ST7789_INVON);
    lcd_cmd(ST7789_NORON);
    lcd_cmd(ST7789_DISPON);
    delay(100);
}

static void lcd_commit(void) {
    lcd_set_window(0, 0, WIDTH, HEIGHT);
    for (int i = 0, j = 0; i < WIDTH * HEIGHT; i++, j += 2) {
        uint16_t v = framebuffer[i];
        txbuf[j] = (uint8_t)(v >> 8);
        txbuf[j + 1] = (uint8_t)(v & 0xFF);
    }
    digitalWrite(LCD_DC, HIGH);
    digitalWrite(LCD_CS, LOW);
    int total = WIDTH * HEIGHT * 2;
    for (int pos = 0; pos < total; pos += SPI_CHUNK) {
        int chunk = total - pos;
        if (chunk > SPI_CHUNK) chunk = SPI_CHUNK;
        wiringPiSPIDataRW(SPI_CHANNEL, txbuf + pos, chunk);
    }
    digitalWrite(LCD_CS, HIGH);
}

static void draw_pixel(int x, int y, uint16_t color) {
    if (x < 0 || y < 0 || x >= WIDTH || y >= HEIGHT) return;
    framebuffer[y * WIDTH + x] = color;
}

static void fill_rect(int x, int y, int w, int h, uint16_t color) {
    if (w <= 0 || h <= 0) return;
    int x0 = clamp_int(x, 0, WIDTH);
    int y0 = clamp_int(y, 0, HEIGHT);
    int x1 = clamp_int(x + w, 0, WIDTH);
    int y1 = clamp_int(y + h, 0, HEIGHT);
    for (int yy = y0; yy < y1; yy++) {
        uint16_t *row = framebuffer + yy * WIDTH;
        for (int xx = x0; xx < x1; xx++) row[xx] = color;
    }
}

static void draw_circle(int cx, int cy, int r, uint16_t color) {
    int rr = r * r;
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            if (x * x + y * y <= rr) draw_pixel(cx + x, cy + y, color);
        }
    }
}

static void fill_round_rect(int x, int y, int w, int h, int r, uint16_t color) {
    if (r < 1) {
        fill_rect(x, y, w, h, color);
        return;
    }
    fill_rect(x + r, y, w - 2 * r, h, color);
    fill_rect(x, y + r, w, h - 2 * r, color);
    draw_circle(x + r, y + r, r, color);
    draw_circle(x + w - r - 1, y + r, r, color);
    draw_circle(x + r, y + h - r - 1, r, color);
    draw_circle(x + w - r - 1, y + h - r - 1, r, color);
}

static void draw_round_rect_border(int x, int y, int w, int h, int r, uint16_t color) {
    if (w <= 0 || h <= 0) return;
    r = clamp_int(r, 0, w / 2);
    r = clamp_int(r, 0, h / 2);
    for (int i = x + r; i < x + w - r; i++) {
        draw_pixel(i, y, color);
        draw_pixel(i, y + h - 1, color);
    }
    for (int i = y + r; i < y + h - r; i++) {
        draw_pixel(x, i, color);
        draw_pixel(x + w - 1, i, color);
    }
    for (int a = 0; a <= 90; a += 3) {
        float rad = (float)a * 3.1415926f / 180.0f;
        int dx = (int)lroundf(cosf(rad) * r);
        int dy = (int)lroundf(sinf(rad) * r);
        draw_pixel(x + r - dx, y + r - dy, color);
        draw_pixel(x + w - r - 1 + dx, y + r - dy, color);
        draw_pixel(x + w - r - 1 + dx, y + h - r - 1 + dy, color);
        draw_pixel(x + r - dx, y + h - r - 1 + dy, color);
    }
}

static void round_gradient_card(int x, int y, int w, int h, int r, Color top, Color bottom, Color outline) {
    for (int yy = 0; yy < h; yy++) {
        Color c = mix_color(top, bottom, h <= 1 ? 0 : (float)yy / (float)(h - 1));
        uint16_t color = color565(c);
        for (int xx = 0; xx < w; xx++) {
            int dx = 0;
            int dy = 0;
            if (xx < r) dx = r - xx;
            else if (xx >= w - r) dx = xx - (w - r - 1);
            if (yy < r) dy = r - yy;
            else if (yy >= h - r) dy = yy - (h - r - 1);
            if (dx * dx + dy * dy <= r * r) draw_pixel(x + xx, y + yy, color);
        }
    }
    draw_round_rect_border(x, y, w, h, r, color565(outline));
}

static int text_width(const char *s, int scale) {
    return (int)strlen(s) * 6 * scale;
}

static char safe_char(char c) {
    if (c >= 'a' && c <= 'z') c = (char)toupper(c);
    if (c < 32 || c > 90) return ' ';
    return c;
}

static uint16_t blend565(uint16_t bg, uint16_t fg, int alpha) {
    alpha = clamp_int(alpha, 0, 255);
    int br = (bg >> 11) & 0x1F;
    int bgc = (bg >> 5) & 0x3F;
    int bb = bg & 0x1F;
    int fr = (fg >> 11) & 0x1F;
    int fgc = (fg >> 5) & 0x3F;
    int fb = fg & 0x1F;
    int r = br + ((fr - br) * alpha) / 255;
    int g = bgc + ((fgc - bgc) * alpha) / 255;
    int b = bb + ((fb - bb) * alpha) / 255;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static void blend_pixel(int x, int y, uint16_t color, int alpha) {
    if (x < 0 || y < 0 || x >= WIDTH || y >= HEIGHT) return;
    uint16_t *p = framebuffer + y * WIDTH + x;
    *p = blend565(*p, color, alpha);
}

static void soft_rect_edge(int x, int y, int w, int h, uint16_t color, int alpha) {
    for (int xx = x; xx < x + w; xx++) {
        blend_pixel(xx, y - 1, color, alpha);
        blend_pixel(xx, y + h, color, alpha);
    }
    for (int yy = y; yy < y + h; yy++) {
        blend_pixel(x - 1, yy, color, alpha);
        blend_pixel(x + w, yy, color, alpha);
    }
}

static void draw_char_scaled(int x, int y, char ch, uint16_t color, int scale) {
    ch = safe_char(ch);
    int idx = ch - 32;
    for (int col = 0; col < 5; col++) {
        uint8_t bits = font5x7[idx][col];
        for (int row = 0; row < 7; row++) {
            if (bits & (1 << (6 - row))) {
                int px = x + col * scale;
                int py = y + row * scale;
                if (scale >= 2) soft_rect_edge(px, py, scale, scale, color, 72);
                fill_rect(px, py, scale, scale, color);
            }
        }
    }
}

static void draw_text(int x, int y, const char *s, Color color, int scale) {
    uint16_t c = color565(color);
    for (; *s; s++) {
        draw_char_scaled(x, y, *s, c, scale);
        x += 6 * scale;
    }
}

static void draw_text_right(int right, int y, const char *s, Color color, int scale) {
    draw_text(right - text_width(s, scale), y, s, color, scale);
}

static void draw_text_center(int cx, int y, const char *s, Color color, int scale) {
    draw_text(cx - text_width(s, scale) / 2, y, s, color, scale);
}

static int py_box(int y, int h) {
    return HEIGHT - y - h;
}

static int py_text(int y, int scale) {
    return py_box(y, 7 * scale);
}

static int py_center(int y) {
    return HEIGHT - 1 - y;
}

static void phys_fill_rect(int x, int y, int w, int h, uint16_t color) {
    fill_rect(x, py_box(y, h), w, h, color);
}

static void phys_fill_round_rect(int x, int y, int w, int h, int r, uint16_t color) {
    fill_round_rect(x, py_box(y, h), w, h, r, color);
}

static void phys_round_border(int x, int y, int w, int h, int r, uint16_t color) {
    draw_round_rect_border(x, py_box(y, h), w, h, r, color);
}

static void phys_round_gradient_card(int x, int y, int w, int h, int r, Color top, Color bottom, Color outline) {
    round_gradient_card(x, py_box(y, h), w, h, r, bottom, top, outline);
}

static void phys_draw_text(int x, int y, const char *s, Color color, int scale) {
    draw_text(x, py_text(y, scale), s, color, scale);
}

static void phys_draw_text_right(int right, int y, const char *s, Color color, int scale) {
    draw_text_right(right, py_text(y, scale), s, color, scale);
}

static void phys_draw_text_center(int cx, int y, const char *s, Color color, int scale) {
    draw_text_center(cx, py_text(y, scale), s, color, scale);
}

static void draw_line(int x0, int y0, int x1, int y1, uint16_t color) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (1) {
        draw_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void phys_line(int x0, int y0, int x1, int y1, uint16_t color) {
    draw_line(x0, py_center(y0), x1, py_center(y1), color);
}

static void phys_arrow(int cx, int cy, int up, Color color) {
    uint16_t col = color565(color);
    int tip_y = up ? cy - 4 : cy + 4;
    int tail_y = up ? cy + 4 : cy - 4;
    phys_line(cx, tail_y, cx, tip_y, col);
    phys_line(cx, tip_y, cx - 4, cy, col);
    phys_line(cx, tip_y, cx + 4, cy, col);
}

static void phys_circle(int cx, int cy, int r, uint16_t color) {
    draw_circle(cx, py_center(cy), r, color);
}

static void phys_circle_outline(int cx, int cy, int r, uint16_t color) {
    int x = r;
    int y = 0;
    int err = 1 - r;
    while (x >= y) {
        draw_pixel(cx + x, py_center(cy + y), color);
        draw_pixel(cx + y, py_center(cy + x), color);
        draw_pixel(cx - y, py_center(cy + x), color);
        draw_pixel(cx - x, py_center(cy + y), color);
        draw_pixel(cx - x, py_center(cy - y), color);
        draw_pixel(cx - y, py_center(cy - x), color);
        draw_pixel(cx + y, py_center(cy - x), color);
        draw_pixel(cx + x, py_center(cy - y), color);
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

static void progress_bar(int x, int y, int w, int h, int percent, Color color) {
    percent = clamp_int(percent, 0, 100);
    fill_round_rect(x, y, w, h, 2, color565(C_TRACK));
    int fill_w = w * percent / 100;
    if (fill_w > 0) fill_round_rect(x, y, fill_w, h, 2, color565(color));
}

static void draw_cpu_icon(int cx, int cy, Color c) {
    uint16_t col = color565(c);
    draw_round_rect_border(cx - 9, cy - 9, 18, 18, 3, col);
    fill_rect(cx - 3, cy - 3, 6, 6, col);
    for (int p = -6; p <= 6; p += 6) {
        draw_line(cx - 13, cy + p, cx - 9, cy + p, col);
        draw_line(cx + 9, cy + p, cx + 13, cy + p, col);
        draw_line(cx + p, cy - 13, cx + p, cy - 9, col);
        draw_line(cx + p, cy + 9, cx + p, cy + 13, col);
    }
}

static void draw_mem_icon(int cx, int cy, Color c) {
    uint16_t col = color565(c);
    draw_round_rect_border(cx - 13, cy - 8, 26, 16, 3, col);
    for (int i = 0; i < 4; i++) fill_rect(cx - 9 + i * 5, cy - 4, 3, 8, col);
}

static void draw_disk_icon(int cx, int cy, Color c) {
    uint16_t col = color565(c);
    draw_round_rect_border(cx - 12, cy - 8, 24, 18, 3, col);
    fill_rect(cx - 8, cy - 4, 16, 3, col);
    fill_rect(cx - 8, cy + 4, 16, 3, col);
}

static void draw_temp_icon(int cx, int cy, Color c) {
    uint16_t col = color565(c);
    draw_round_rect_border(cx - 4, cy - 14, 9, 22, 4, col);
    draw_circle(cx, cy + 9, 7, col);
    fill_rect(cx - 1, cy - 8, 3, 16, col);
}

static void draw_sun(int cx, int cy, int r) {
    uint16_t col = color565(C_YELLOW);
    for (int i = 0; i < 8; i++) {
        float a = (float)i * 3.14159f * 2.0f / 8.0f;
        phys_line(cx + (int)(cosf(a) * (r + 4)), cy + (int)(sinf(a) * (r + 4)),
                  cx + (int)(cosf(a) * (r + 10)), cy + (int)(sinf(a) * (r + 10)), col);
    }
    phys_circle(cx, cy, r, col);
}

static void draw_cloud(int cx, int cy) {
    uint16_t shadow = RGB565(58, 66, 90);
    uint16_t cloud = color565(C_CLOUD);
    phys_fill_round_rect(cx - 26, cy + 1, 54, 16, 8, shadow);
    phys_circle(cx - 16, cy, 10, shadow);
    phys_circle(cx, cy - 8, 14, shadow);
    phys_circle(cx + 17, cy, 11, shadow);
    phys_fill_round_rect(cx - 28, cy - 1, 56, 15, 8, cloud);
    phys_circle(cx - 17, cy - 2, 10, cloud);
    phys_circle(cx, cy - 10, 14, cloud);
    phys_circle(cx + 16, cy - 2, 11, cloud);
}

static void draw_weather_icon(const char *desc, int cx, int cy) {
    char upper[64];
    snprintf(upper, sizeof(upper), "%s", desc);
    upper_ascii(upper);
    if (strstr(upper, "RAIN") || strstr(upper, "SHOWER") || strstr(upper, "DRIZZLE")) {
        draw_cloud(cx, cy);
        uint16_t rain = color565(C_CYAN);
        phys_line(cx - 14, cy + 18, cx - 18, cy + 28, rain);
        phys_line(cx, cy + 18, cx - 4, cy + 28, rain);
        phys_line(cx + 14, cy + 18, cx + 10, cy + 28, rain);
    } else if (strstr(upper, "CLOUD") || strstr(upper, "OVERCAST")) {
        draw_sun(cx - 7, cy - 12, 8);
        draw_cloud(cx + 6, cy + 5);
    } else {
        draw_sun(cx + 2, cy, 14);
    }
}

static float get_cpu_temp(void) {
    FILE *fp = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (!fp) return 0.0f;
    int temp = 0;
    if (fscanf(fp, "%d", &temp) != 1) temp = 0;
    fclose(fp);
    return temp / 1000.0f;
}

static float get_cpu_usage(void) {
    static long long prev_idle = 0;
    static long long prev_total = 0;
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) return 0.0f;
    long long user = 0, nice = 0, system = 0, idle = 0, iowait = 0, irq = 0, softirq = 0, steal = 0;
    fscanf(fp, "cpu %lld %lld %lld %lld %lld %lld %lld %lld",
           &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);
    fclose(fp);

    long long idle_all = idle + iowait;
    long long total = user + nice + system + idle_all + irq + softirq + steal;
    float usage = 0.0f;
    long long total_delta = total - prev_total;
    long long idle_delta = idle_all - prev_idle;
    if (prev_total > 0 && total_delta > 0) {
        usage = 100.0f * (1.0f - (float)idle_delta / (float)total_delta);
        if (usage < 0) usage = 0;
        if (usage > 100) usage = 100;
    }
    prev_idle = idle_all;
    prev_total = total;
    return usage;
}

static void get_memory(int *percent, long long *used_kb, long long *total_kb) {
    FILE *fp = fopen("/proc/meminfo", "r");
    long long total = 0;
    long long available = 0;
    char line[256];
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            sscanf(line, "MemTotal: %lld kB", &total);
            sscanf(line, "MemAvailable: %lld kB", &available);
        }
        fclose(fp);
    }
    if (total <= 0) total = 1;
    *total_kb = total;
    *used_kb = total - available;
    *percent = (int)((*used_kb) * 100 / total);
}

static void get_disk(int *percent, double *free_gb) {
    struct statvfs s;
    if (statvfs("/", &s) != 0 || s.f_blocks == 0) {
        *percent = 0;
        *free_gb = 0;
        return;
    }
    unsigned long long total = (unsigned long long)s.f_blocks * s.f_frsize;
    unsigned long long free_b = (unsigned long long)s.f_bavail * s.f_frsize;
    unsigned long long used = total - free_b;
    *percent = (int)(used * 100 / total);
    *free_gb = (double)free_b / 1024.0 / 1024.0 / 1024.0;
}

static unsigned long long read_net_counter(const char *ifname, const char *counter) {
    char path[160];
    snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/%s", ifname, counter);
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;
    unsigned long long value = 0;
    fscanf(fp, "%llu", &value);
    fclose(fp);
    return value;
}

static void read_operstate(const char *ifname, char *state, size_t size) {
    char path[160];
    snprintf(path, sizeof(path), "/sys/class/net/%s/operstate", ifname);
    FILE *fp = fopen(path, "r");
    if (!fp) {
        snprintf(state, size, "UP");
        return;
    }
    if (!fgets(state, (int)size, fp)) snprintf(state, size, "UP");
    fclose(fp);
    state[strcspn(state, "\r\n")] = '\0';
    upper_ascii(state);
    if (!state[0]) snprintf(state, size, "UP");
}

static int default_route_ifname(char *ifname, size_t size) {
    FILE *fp = fopen("/proc/net/route", "r");
    if (!fp) return 0;
    char line[256];
    int best_metric = 0x7fffffff;
    char best[IFNAMSIZ] = "";
    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return 0;
    }
    while (fgets(line, sizeof(line), fp)) {
        char iface[IFNAMSIZ] = "";
        unsigned long dest = 0;
        unsigned long gateway = 0;
        unsigned int flags = 0;
        int metric = 0;
        if (sscanf(line, "%15s %lx %lx %x %*d %*d %d", iface, &dest, &gateway, &flags, &metric) != 5) continue;
        if (dest != 0) continue;
        if (!(flags & 0x1)) continue;
        if (metric < best_metric) {
            best_metric = metric;
            snprintf(best, sizeof(best), "%s", iface);
        }
    }
    fclose(fp);
    if (!best[0]) return 0;
    snprintf(ifname, size, "%s", best);
    return 1;
}

static void update_network(NetInfo *net) {
    char found_if[IFNAMSIZ] = "";
    char found_ip[INET_ADDRSTRLEN] = "0.0.0.0";
    char default_if[IFNAMSIZ] = "";
    int have_default = default_route_ifname(default_if, sizeof(default_if));
    unsigned long long best_total = 0;
    struct ifaddrs *ifaddr = NULL;
    if (getifaddrs(&ifaddr) == -1) {
        snprintf(net->state, sizeof(net->state), "DOWN");
        return;
    }
    for (struct ifaddrs *ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;
        if (!(ifa->ifa_flags & IFF_UP)) continue;
        if (have_default && strcmp(ifa->ifa_name, default_if) != 0) continue;
        unsigned long long rx_total = read_net_counter(ifa->ifa_name, "rx_bytes");
        unsigned long long tx_total = read_net_counter(ifa->ifa_name, "tx_bytes");
        unsigned long long total = rx_total + tx_total;
        if (found_if[0] && total <= best_total) continue;
        void *addr = &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
        inet_ntop(AF_INET, addr, found_ip, sizeof(found_ip));
        snprintf(found_if, sizeof(found_if), "%s", ifa->ifa_name);
        best_total = total;
    }
    if (!found_if[0] && have_default) {
        for (struct ifaddrs *ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
            if (ifa->ifa_flags & IFF_LOOPBACK) continue;
            if (!(ifa->ifa_flags & IFF_UP)) continue;
            unsigned long long rx_total = read_net_counter(ifa->ifa_name, "rx_bytes");
            unsigned long long tx_total = read_net_counter(ifa->ifa_name, "tx_bytes");
            unsigned long long total = rx_total + tx_total;
            if (found_if[0] && total <= best_total) continue;
            void *addr = &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
            inet_ntop(AF_INET, addr, found_ip, sizeof(found_ip));
            snprintf(found_if, sizeof(found_if), "%s", ifa->ifa_name);
            best_total = total;
        }
    }
    freeifaddrs(ifaddr);
    if (!found_if[0]) {
        snprintf(net->ifname, sizeof(net->ifname), "NET");
        snprintf(net->ip, sizeof(net->ip), "0.0.0.0");
        snprintf(net->state, sizeof(net->state), "DOWN");
        net->rx_rate = 0;
        net->tx_rate = 0;
        return;
    }

    if (strcmp(net->ifname, found_if) != 0) {
        net->prev_rx = 0;
        net->prev_tx = 0;
        net->last_sample = 0;
    }
    snprintf(net->ifname, sizeof(net->ifname), "%s", found_if);
    snprintf(net->ip, sizeof(net->ip), "%s", found_ip);
    read_operstate(net->ifname, net->state, sizeof(net->state));

    unsigned long long rx = read_net_counter(found_if, "rx_bytes");
    unsigned long long tx = read_net_counter(found_if, "tx_bytes");
    time_t now = time(NULL);
    double elapsed = difftime(now, net->last_sample);
    if (net->last_sample > 0 && elapsed > 0) {
        double raw_rx = rx >= net->prev_rx ? (double)(rx - net->prev_rx) / elapsed : 0;
        double raw_tx = tx >= net->prev_tx ? (double)(tx - net->prev_tx) / elapsed : 0;
        double alpha = elapsed / (double)config.net_smoothing_seconds;
        if (alpha < 0.05) alpha = 0.05;
        if (alpha > 1.0) alpha = 1.0;
        net->rx_rate = net->rx_rate <= 0 ? raw_rx : net->rx_rate + (raw_rx - net->rx_rate) * alpha;
        net->tx_rate = net->tx_rate <= 0 ? raw_tx : net->tx_rate + (raw_tx - net->tx_rate) * alpha;
    }
    net->prev_rx = rx;
    net->prev_tx = tx;
    net->last_sample = now;
}

static void update_network_rates(NetInfo *net) {
    if (!net->ifname[0] || strcmp(net->ifname, "NET") == 0) {
        snprintf(net->state, sizeof(net->state), "DOWN");
        net->rx_rate = 0;
        net->tx_rate = 0;
        return;
    }
    read_operstate(net->ifname, net->state, sizeof(net->state));
    unsigned long long rx = read_net_counter(net->ifname, "rx_bytes");
    unsigned long long tx = read_net_counter(net->ifname, "tx_bytes");
    time_t now = time(NULL);
    double elapsed = difftime(now, net->last_sample);
    if (net->last_sample > 0 && elapsed > 0) {
        double raw_rx = rx >= net->prev_rx ? (double)(rx - net->prev_rx) / elapsed : 0;
        double raw_tx = tx >= net->prev_tx ? (double)(tx - net->prev_tx) / elapsed : 0;
        double alpha = elapsed / (double)config.net_smoothing_seconds;
        if (alpha < 0.05) alpha = 0.05;
        if (alpha > 1.0) alpha = 1.0;
        net->rx_rate = net->rx_rate <= 0 ? raw_rx : net->rx_rate + (raw_rx - net->rx_rate) * alpha;
        net->tx_rate = net->tx_rate <= 0 ? raw_tx : net->tx_rate + (raw_tx - net->tx_rate) * alpha;
    }
    net->prev_rx = rx;
    net->prev_tx = tx;
    net->last_sample = now;
}

static void format_rate(double bytes_per_sec, char *buf, size_t size) {
    if (bytes_per_sec < 1000.0) {
        snprintf(buf, size, "%.0FB", bytes_per_sec);
    } else if (bytes_per_sec < 1024.0 * 1024.0) {
        double k = bytes_per_sec / 1024.0;
        if (k >= 1000.0) snprintf(buf, size, "1M");
        else snprintf(buf, size, "%.0FK", k);
    } else {
        double m = bytes_per_sec / 1024.0 / 1024.0;
        if (m < 10.0) snprintf(buf, size, "%.1FM", m);
        else snprintf(buf, size, "%.0FM", m);
    }
}

static int desired_backlight(const struct tm *tmv) {
    int hour = tmv->tm_hour;
    if (config.night_start > config.night_end) {
        if (hour >= config.night_start || hour < config.night_end) return config.brightness_night;
    } else if (hour >= config.night_start && hour < config.night_end) {
        return config.brightness_night;
    }
    if (hour == config.dim_evening_hour || hour == config.dim_morning_hour) return config.brightness_dim;
    return config.brightness_day;
}

static void backlight_init(void) {
#if HAS_SOFTPWM
    if (softPwmCreate(LCD_BL, 100, 100) == 0) {
        backlight_pwm_enabled = 1;
        backlight_percent = 100;
        return;
    }
#endif
    digitalWrite(LCD_BL, HIGH);
    backlight_pwm_enabled = 0;
    backlight_percent = 100;
}

static void update_backlight(const struct tm *tmv) {
    if (preview_mode) return;
    int target = desired_backlight(tmv);
    if (target == backlight_percent) return;
#if HAS_SOFTPWM
    if (backlight_pwm_enabled) {
        softPwmWrite(LCD_BL, target);
        backlight_percent = target;
        return;
    }
#endif
    digitalWrite(LCD_BL, target > 0 ? HIGH : LOW);
    backlight_percent = target;
}

static void get_uptime(char *buf, size_t size) {
    FILE *fp = fopen("/proc/uptime", "r");
    double seconds = 0;
    if (fp) {
        fscanf(fp, "%lf", &seconds);
        fclose(fp);
    }
    int total = (int)seconds;
    int days = total / 86400;
    int hours = (total % 86400) / 3600;
    int mins = (total % 3600) / 60;
    if (days >= 100) snprintf(buf, size, "99D+");
    else if (days > 0) snprintf(buf, size, "%dD %dH", days, hours);
    else snprintf(buf, size, "%dH %dM", hours, mins);
}

static void clean_field(char *s) {
    while (*s == ' ' || *s == '+' || *s == '"') memmove(s, s + 1, strlen(s));
    for (char *p = s; *p; p++) {
        if (*p == '\n' || *p == '\r' || *p == '"' || *p == '\'') {
            *p = '\0';
            break;
        }
    }
    upper_ascii(s);
}

static void extract_temp_number(const char *raw, char *out, size_t size) {
    size_t j = 0;
    int has_digit = 0;
    for (const char *p = raw; *p && j + 1 < size; p++) {
        unsigned char ch = (unsigned char)*p;
        if ((ch == '-' || ch == '+') && j == 0) {
            if (ch == '-') out[j++] = '-';
            continue;
        }
        if (isdigit(ch)) {
            out[j++] = (char)ch;
            has_digit = 1;
            continue;
        }
        if (has_digit) break;
    }
    if (!has_digit) {
        snprintf(out, size, "--");
    } else {
        out[j] = '\0';
    }
}

static void weather_location_path(char *buf, size_t size) {
    buf[0] = '\0';
    if (!config.weather_location[0]) return;
    size_t out = 0;
    for (const char *p = config.weather_location; *p && out + 4 < size; p++) {
        unsigned char ch = (unsigned char)*p;
        if (isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == ',') {
            buf[out++] = (char)ch;
        } else if (isspace(ch)) {
            if (out + 3 < size) {
                buf[out++] = '%';
                buf[out++] = '2';
                buf[out++] = '0';
            }
        }
    }
    buf[out] = '\0';
}

static void fetch_weather(Weather *w) {
    time_t now = time(NULL);
    if (now < w->next_fetch) return;
    w->next_fetch = now + 600;

    char location[96];
    char cmd[256];
    weather_location_path(location, sizeof(location));
    if (location[0]) {
        snprintf(cmd, sizeof(cmd), "curl -m 3 -fsS 'https://wttr.in/%s?format=%%l|%%t|%%C|%%h|%%w' 2>/dev/null", location);
    } else {
        snprintf(cmd, sizeof(cmd), "curl -m 3 -fsS 'https://wttr.in/?format=%%l|%%t|%%C|%%h|%%w' 2>/dev/null");
    }

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        w->online = 0;
        return;
    }
    char line[192] = {0};
    int ok = 0;
    if (fgets(line, sizeof(line), fp)) {
        char *fields[5] = {0};
        int idx = 0;
        fields[idx++] = line;
        for (char *p = line; *p && idx < 5; p++) {
            if (*p == '|') {
                *p = '\0';
                fields[idx++] = p + 1;
            }
        }
        if (idx >= 3) {
            snprintf(w->city, sizeof(w->city), "%s", fields[0]);
            snprintf(w->temp, sizeof(w->temp), "%s", fields[1]);
            snprintf(w->desc, sizeof(w->desc), "%s", fields[2]);
            if (idx > 3) snprintf(w->humidity, sizeof(w->humidity), "%s", fields[3]);
            if (idx > 4) snprintf(w->wind, sizeof(w->wind), "%s", fields[4]);
            clean_field(w->city);
            clean_field(w->temp);
            clean_field(w->desc);
            clean_field(w->humidity);
            clean_field(w->wind);
            fit_text(w->city, sizeof(w->city), 58, 1);
            fit_text(w->desc, sizeof(w->desc), 92, 1);
            w->last_success = now;
            ok = 1;
        }
    }
    pclose(fp);
    w->online = ok;
}

static void init_weather(Weather *w) {
    memset(w, 0, sizeof(*w));
    snprintf(w->city, sizeof(w->city), "AUTO");
    snprintf(w->temp, sizeof(w->temp), "--C");
    snprintf(w->feels, sizeof(w->feels), "--");
    snprintf(w->humidity, sizeof(w->humidity), "--");
    snprintf(w->wind, sizeof(w->wind), "--");
    snprintf(w->desc, sizeof(w->desc), "WEATHER");
    w->online = 0;
    w->next_fetch = 0;
    w->last_success = 0;
}

static void update_host_data(DashboardData *data) {
    snprintf(data->host, sizeof(data->host), "RASPBERRYPI");
    gethostname(data->host, sizeof(data->host));
    data->host[sizeof(data->host) - 1] = '\0';
    upper_ascii(data->host);
    fit_text(data->host, sizeof(data->host), 48, 1);
}

static void update_uptime_data(DashboardData *data) {
    get_uptime(data->uptime, sizeof(data->uptime));
    fit_text(data->uptime, sizeof(data->uptime), 42, 1);
}

static void update_system_data(DashboardData *data) {
    data->cpu = get_cpu_usage();
    data->temp = get_cpu_temp();
    get_memory(&data->mem_pct, &data->mem_used, &data->mem_total);
    getloadavg(&data->load_avg, 1);
}

static void update_disk_data(DashboardData *data) {
    get_disk(&data->disk_pct, &data->disk_free);
}

static void init_dashboard_data(DashboardData *data) {
    memset(data, 0, sizeof(*data));
    init_weather(&data->weather);
    update_host_data(data);
    update_uptime_data(data);
    get_cpu_usage();
    update_system_data(data);
    update_disk_data(data);
    update_network(&net_info);
    fetch_weather(&data->weather);
}

static void metric_card(int x, int y, const char *label, const char *value, const char *sub,
                        int percent, Color accent, void (*icon)(int, int, Color), int alert) {
    char sub_fit[18];
    snprintf(sub_fit, sizeof(sub_fit), "%s", sub ? sub : "");
    fit_text(sub_fit, sizeof(sub_fit), 58, 1);

    Color outline = alert ? C_DANGER : C_OUTLINE;
    phys_round_gradient_card(x, y, 104, 52, 8, C_CARD_TOP, C_CARD_BOTTOM, outline);
    icon(x + 19, py_center(y + 24), accent);
    phys_draw_text(x + 40, y + 7, label, C_MUTED, 1);
    phys_draw_text(x + 40, y + 18, value, C_WHITE, 2);
    if (sub_fit[0]) phys_draw_text(x + 40, y + 35, sub_fit, (Color){136, 150, 176}, 1);
    if (alert) phys_fill_round_rect(x + 90, y + 8, 5, 5, 3, color565(C_DANGER));
    progress_bar(x + 10, py_box(y + 47, 3), 84, 3, percent, accent);
}

static void draw_background(void) {
    for (int y = 0; y < HEIGHT; y++) {
        float t = (float)y / (float)(HEIGHT - 1);
        Color base = mix_color(C_BG_TOP, C_BG_BOTTOM, t);
        if (y < 124) {
            Color accent = mix_color((Color){10, 48, 58}, (Color){40, 24, 54}, t);
            base = mix_color(base, accent, 0.32f);
        }
        fill_rect(0, py_box(y, 1), WIDTH, 1, color565(base));
    }
}

static void draw_dashboard(const DashboardData *data) {
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    const Weather *weather = &data->weather;

    char rx_text[10], tx_text[10];
    format_rate(net_info.rx_rate, rx_text, sizeof(rx_text));
    format_rate(net_info.tx_rate, tx_text, sizeof(tx_text));

    draw_background();

    phys_fill_round_rect(12, 10, 78, 18, 8, RGB565(17, 24, 39));
    phys_round_border(12, 10, 78, 18, 8, color565(C_OUTLINE));
    phys_draw_text(22, 15, data->host, (Color){204, 213, 232}, 1);
    phys_arrow(101, 19, 0, C_CYAN);
    phys_draw_text(110, 15, rx_text, C_CYAN, 1);
    phys_arrow(145, 19, 1, C_PINK);
    phys_draw_text(154, 15, tx_text, C_PINK, 1);
    phys_draw_text_right(228, 15, data->uptime, C_MUTED, 1);

    char hour_str[4], min_str[4];
    strftime(hour_str, sizeof(hour_str), "%H", &tmv);
    strftime(min_str, sizeof(min_str), "%M", &tmv);
    phys_draw_text(45, 38, hour_str, C_WHITE, 5);
    if ((tmv.tm_sec % 2) == 0) {
        phys_draw_text(108, 41, ":", C_WHITE, 4);
    }
    phys_draw_text(135, 38, min_str, C_WHITE, 5);

    char sec_str[4];
    strftime(sec_str, sizeof(sec_str), "%S", &tmv);
    phys_draw_text_right(230, 62, sec_str, C_CYAN, 2);

    char date_str[32];
    strftime(date_str, sizeof(date_str), "%Y.%m.%d  %a", &tmv);
    upper_ascii(date_str);
    phys_round_border(15, 84, 17, 14, 2, color565(C_YELLOW));
    phys_fill_rect(16, 85, 15, 4, color565(C_YELLOW));
    phys_draw_text(42, 85, date_str, (Color){207, 216, 235}, 2);

    phys_round_gradient_card(12, 106, 216, 62, 12, C_PANEL_TOP, C_PANEL_BOTTOM, (Color){68, 85, 128});
    draw_weather_icon(weather->desc, 42, 137);
    phys_fill_round_rect(217, 113, 5, 5, 3, color565(weather->online ? C_CYAN : C_DANGER));
    char temp_text[16];
    snprintf(temp_text, sizeof(temp_text), "%s", weather->temp[0] ? weather->temp : "--C");
    clean_field(temp_text);
    char temp_num[12];
    extract_temp_number(temp_text, temp_num, sizeof(temp_num));
    fit_text(temp_num, sizeof(temp_num), 62, 3);
    phys_draw_text(78, 119, temp_num, C_WHITE, 3);
    if (strcmp(temp_num, "--") != 0) {
        int degree_x = 78 + text_width(temp_num, 3) + 7;
        phys_circle_outline(degree_x, 124, 4, color565(C_WHITE));
        phys_circle_outline(degree_x, 124, 3, color565(C_WHITE));
    }

    char desc[64];
    snprintf(desc, sizeof(desc), "%s", weather->desc);
    fit_text(desc, sizeof(desc), 92, 1);
    phys_draw_text(78, 149, desc, (Color){213, 236, 245}, 1);
    phys_draw_text_center(177, 118, weather->city, (Color){236, 244, 255}, 1);

    char hum[24];
    snprintf(hum, sizeof(hum), "HUM %s", weather->humidity[0] ? weather->humidity : "--");
    fit_text(hum, sizeof(hum), 76, 1);
    phys_draw_text(150, 136, hum, (Color){196, 225, 255}, 1);
    char wind[24];
    snprintf(wind, sizeof(wind), "%s", weather->wind[0] ? weather->wind : "--");
    fit_text(wind, sizeof(wind), 76, 1);
    phys_draw_text(150, 150, wind, (Color){196, 225, 255}, 1);

    char cpu_val[12], mem_val[12], temp_val[12], disk_val[12];
    char cpu_sub[18], mem_sub[18], disk_sub[18];
    snprintf(cpu_val, sizeof(cpu_val), "%02.0F%%", data->cpu);
    snprintf(cpu_sub, sizeof(cpu_sub), "L %.2F", data->load_avg);
    snprintf(mem_val, sizeof(mem_val), "%02d%%", data->mem_pct);
    snprintf(mem_sub, sizeof(mem_sub), "%.1F/%.1FG", data->mem_used / 1048576.0, data->mem_total / 1048576.0);
    fit_text(mem_sub, sizeof(mem_sub), 62, 1);
    snprintf(temp_val, sizeof(temp_val), "%02.0FC", data->temp);
    snprintf(disk_val, sizeof(disk_val), "%02d%%", data->disk_pct);
    snprintf(disk_sub, sizeof(disk_sub), "%.1FG FREE", data->disk_free);
    fit_text(disk_sub, sizeof(disk_sub), 62, 1);

    metric_card(12, 176, "CPU", cpu_val, cpu_sub, (int)data->cpu, C_CYAN, draw_cpu_icon, data->cpu >= (float)config.cpu_alert);
    metric_card(124, 176, "MEM", mem_val, mem_sub, data->mem_pct, C_BLUE, draw_mem_icon, data->mem_pct >= config.mem_alert);
    metric_card(12, 232, "TEMP", temp_val, "CPU", (int)(data->temp * 100.0f / 85.0f), C_YELLOW, draw_temp_icon, data->temp >= (float)config.temp_alert);
    metric_card(124, 232, "DISK", disk_val, disk_sub, data->disk_pct, C_PINK, draw_disk_icon, data->disk_pct >= config.disk_alert);

    phys_round_gradient_card(12, 286, 216, 28, 8, (Color){20, 26, 42}, (Color){15, 20, 32}, C_OUTLINE);
    phys_draw_text_center(120, 293, net_info.ip, C_WHITE, 2);
}

static int write_preview_ppm(const char *path) {
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        fprintf(stderr, "preview open failed: %s\n", path);
        return 1;
    }
    fprintf(fp, "P6\n%d %d\n255\n", WIDTH, HEIGHT);
    for (int y = 0; y < HEIGHT; y++) {
        int fy = py_center(y);
        for (int x = 0; x < WIDTH; x++) {
            uint16_t v = framebuffer[fy * WIDTH + x];
            uint8_t rgb[3] = {
                (uint8_t)(((v >> 11) & 0x1F) * 255 / 31),
                (uint8_t)(((v >> 5) & 0x3F) * 255 / 63),
                (uint8_t)((v & 0x1F) * 255 / 31),
            };
            fwrite(rgb, 1, 3, fp);
        }
    }
    fclose(fp);
    return 0;
}

static void parse_args(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--madctl=", 9) == 0) {
            madctl = (uint8_t)strtol(argv[i] + 9, NULL, 0);
        } else if (strncmp(argv[i], "--dump-ppm=", 11) == 0) {
            preview_mode = 1;
            snprintf(preview_path, sizeof(preview_path), "%s", argv[i] + 11);
        } else if (strcmp(argv[i], "--mirror-x") == 0) {
            madctl ^= 0x40;
        } else if (strcmp(argv[i], "--mirror-y") == 0) {
            madctl ^= 0x80;
        } else if (strcmp(argv[i], "--swap-xy") == 0) {
            madctl ^= 0x20;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [--madctl=0x60] [--dump-ppm=/tmp/frame.ppm] [--mirror-x] [--mirror-y] [--swap-xy]\n", argv[0]);
            exit(0);
        }
    }
}

int main(int argc, char **argv) {
    parse_args(argc, argv);
    load_config("/root/st7789v-c/dashboard.conf");

    if (preview_mode) {
        DashboardData data;
        init_dashboard_data(&data);
        draw_dashboard(&data);
        int rc = write_preview_ppm(preview_path[0] ? preview_path : "/tmp/lcd_dashboard.ppm");
        if (rc == 0) printf("preview saved: %s\n", preview_path[0] ? preview_path : "/tmp/lcd_dashboard.ppm");
        return rc;
    }

    if (wiringPiSetupGpio() == -1) {
        fprintf(stderr, "wiringPi setup failed\n");
        return 1;
    }
    if (wiringPiSPISetup(SPI_CHANNEL, SPI_SPEED) == -1) {
        fprintf(stderr, "SPI setup failed: %s\n", strerror(errno));
        return 1;
    }

    pinMode(LCD_CS, OUTPUT);
    pinMode(LCD_RST, OUTPUT);
    pinMode(LCD_DC, OUTPUT);
    pinMode(LCD_BL, OUTPUT);
    digitalWrite(LCD_CS, HIGH);
    digitalWrite(LCD_DC, HIGH);
    digitalWrite(LCD_RST, HIGH);
    digitalWrite(LCD_BL, HIGH);

    lcd_init();
    backlight_init();
    printf(
        "fast dashboard started: %dx%d SPI=%d MADCTL=0x%02X softPwm=%d smoothing=%ds weather=%s\n",
        WIDTH, HEIGHT, SPI_SPEED, madctl, backlight_pwm_enabled,
        config.net_smoothing_seconds,
        config.weather_location[0] ? config.weather_location : "auto-ip"
    );
    fflush(stdout);

    DashboardData data;
    init_dashboard_data(&data);
    time_t boot_now = time(NULL);
    struct tm boot_tmv;
    localtime_r(&boot_now, &boot_tmv);
    update_backlight(&boot_tmv);
    draw_dashboard(&data);
    lcd_commit();

    time_t last_sec = boot_now;
    time_t last_system_sec = 0;
    time_t last_rate_sec = 0;
    time_t last_net_identity_sec = 0;
    time_t last_disk_sec = 0;
    time_t last_uptime_sec = 0;
    time_t last_weather_sec = 0;
    time_t last_backlight_sec = 0;
    while (1) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        time_t now = ts.tv_sec;
        int ms = (int)(ts.tv_nsec / 1000000L);

        if (now != last_sec) {
            last_sec = now;
            draw_dashboard(&data);
            lcd_commit();
            usleep(20000);
            continue;
        }

        int needs_draw = 0;
        if (now % 3 == 0 && ms >= 280 && last_system_sec != now) {
            update_system_data(&data);
            last_system_sec = now;
            needs_draw = 1;
        } else if (now % 2 == 0 && ms >= 520 && last_rate_sec != now) {
            update_network_rates(&net_info);
            last_rate_sec = now;
            needs_draw = 1;
        } else if (now % 60 == 0 && ms >= 650 && last_net_identity_sec != now) {
            update_network(&net_info);
            last_net_identity_sec = now;
            needs_draw = 1;
        } else if (now % 30 == 0 && ms >= 760 && last_disk_sec != now) {
            update_disk_data(&data);
            last_disk_sec = now;
            needs_draw = 1;
        } else if (now % 60 == 0 && ms >= 860 && last_uptime_sec != now) {
            update_host_data(&data);
            update_uptime_data(&data);
            last_uptime_sec = now;
            needs_draw = 1;
        } else if (now % 60 == 0 && ms >= 930 && last_weather_sec != now) {
            fetch_weather(&data.weather);
            last_weather_sec = now;
            needs_draw = 1;
        } else if (now % 60 == 0 && ms >= 980 && last_backlight_sec != now) {
            struct tm tmv;
            localtime_r(&now, &tmv);
            update_backlight(&tmv);
            last_backlight_sec = now;
        }

        if (needs_draw) {
            draw_dashboard(&data);
            lcd_commit();
        }
        usleep(20000);
    }
    return 0;
}
