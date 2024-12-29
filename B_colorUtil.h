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

// Linear Interpolate between colors
void B_ColorLerp(const B_color_t* const c1, const B_color_t* const c2, float t, B_color_t* const out);

B_color_t B_HSLtoRGB(float hue, float saturation, float lightness);
