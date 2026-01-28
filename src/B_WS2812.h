#pragma once

#include <esp_log.h>

// https://docs.espressif.com/projects/esp-idf/en/v5.0/esp32s2/api-reference/peripherals/rmt.html#rmt-encoder
#include <driver/rmt_tx.h>

#define B_RMT_FREQ 40000000 // 40 MHz - 0.025us (25 ns) per tick

// As per the WS2812 datasheet
#define B_T0H_uS 0.4
#define B_T1H_uS 0.8
#define B_T0L_uS 0.85
#define B_T1L_uS 0.45
#define B_RST_uS 50

// Convert time in microseconds to seconds and to RMT ticks
#define B_T0H_ticks (B_T0H_uS * B_RMT_FREQ / 1000000)
#define B_T1H_ticks (B_T1H_uS * B_RMT_FREQ / 1000000)
#define B_T0L_ticks (B_T0L_uS * B_RMT_FREQ / 1000000)
#define B_T1L_ticks (B_T1L_uS * B_RMT_FREQ / 1000000)
#define B_RST_ticks (B_RST_uS * B_RMT_FREQ / 1000000)

struct B_encoders {
	rmt_encoder_t defaultEncoder; // Manages the bytes and copy encoders, see B_DefaultEncoder
	rmt_encoder_t* bytesEncoder; // Encodes the dynamic color data
	rmt_encoder_t* copyEncoder; // Encodes the static reset signal
};

enum B_encodingState_t {
	B_COLOR_ENCODE,
	B_RESET_ENCODE
};


// size_t B_DefaultEncoder(rmt_encoder_t* encoder, rmt_channel_handle_t channel, const void* primary_data, size_t data_size, rmt_encode_state_t* ret_state);
// Default encoder function for WS2812 transmission, manages the color data and reset signal encoding
// rmt_transmit call this function
// Function declaration described in the rmt_encoder_t struct
// Returns: Number of RMT symbols that the primary data has been encoded into
// - Private function

// esp_err_t B_DelEncoder(rmt_encoder_t* encoder);
// Deletes the WS2812 encoder and its resources
// Function declaration described in the rmt_encoder_t struct
// Returns: ESP_OK on success, ESP_FAIL on failure
// - Private function

// esp_err_t B_ResetEncoder(rmt_encoder_t* encoder);
// Resets the WS2812 encoder state
// Function declaration described in the rmt_encoder_t struct
// Returns: ESP_OK on success, ESP_FAIL on failure
// - Private function

// esp_err_t B_CreateEncoders();
// Creates the necessary RMT encoders for WS2812 transmission
// Arguments: void
// Returns: ESP_OK on success, ESP_FAIL on failure
// - Private function

// Sets up the RMT channel for WS2812 transmission
void B_WS2812_SetUpRMTChannel(uint8_t gpio);

// Transmits the color buffer to the WS2812 LEDs
void B_WS2812_Transmit(const uint8_t* colorBuffer, size_t length);

// Cleans up the RMT channel and encoders
void B_WS2812_CleanupRMT();
