#include "B_colorUtil.h"

void B_ColorLerp(const B_color_t* const c1, const B_color_t* const c2, float t, B_color_t* const out)
{
	if (t > 1)
		t = 1;
	out->red = (uint8_t)(c1->red + t * (c2->red - c1->red));
	out->green = (uint8_t)(c1->green + t * (c2->green - c1->green));
	out->blue = (uint8_t)(c1->blue + t * (c2->blue - c1->blue));
}

// Function to convert HSL to RGB
B_color_t B_HSLtoRGB(float hue, float saturation, float lightness)
{
	float c = (1.0 - fabs(2.0 * lightness - 1.0)) * saturation;
	float x = c * (1.0 - fabs(fmod(hue / 60.0, 2) - 1.0));
	float m = lightness - c / 2.0;

	float r, g, b;
	if (hue < 60.0) {
		r = c; g = x; b = 0;
	}
	else if (hue < 120.0) {
		r = x; g = c; b = 0;
	}
	else if (hue < 180.0) {
		r = 0; g = c; b = x;
	}
	else if (hue < 240.0) {
		r = 0; g = x; b = c;
	}
	else if (hue < 300.0) {
		r = x; g = 0; b = c;
	}
	else {
		r = c; g = 0; b = x;
	}

	B_color_t rgb;
	rgb.red = (r + m) * 255;
	rgb.green = (g + m) * 255;
	rgb.blue = (b + m) * 255;
	return rgb;
}
