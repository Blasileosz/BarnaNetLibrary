#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <esp_system.h>
#include <esp_err.h>
#include <esp_event.h>
#include <esp_log.h>

#include <math.h>

typedef struct {
	uint8_t red;
	uint8_t green;
	uint8_t blue;
} B_color_t;

#define B_COLOR_INIT(r, g, b) (B_color_t){ .red = r, .green = g, .blue = b }
#define B_COLOR_INIT_HEX(hex) (B_color_t){ .red = ((hex) >> 16) & 0xFF, .green = ((hex) >> 8) & 0xFF, .blue = (hex) & 0xFF }

#define B_COLOR_EQUAL(c1, c2) ((c1).red == (c2).red && (c1).green == (c2).green && (c1).blue == (c2).blue)

#define B_COLOR_BLACK B_COLOR_INIT(0, 0, 0)

// Linear Interpolate between colors
void B_ColorLerp(const B_color_t* const c1, const B_color_t* const c2, float t, B_color_t* const out);

B_color_t B_HSLtoRGB(float hue, float saturation, float lightness);
