#pragma once

//==============================================================================
// HARDWARE CONFIGURATION CONSTANTS
//==============================================================================
// This file contains hardware-specific configuration values that define the
// physical characteristics and constraints of the ESP32-S3 coffee grinder system.
// These values are tied to the specific hardware implementation and should only
// be modified when changing hardware components.

//------------------------------------------------------------------------------
// GPIO PIN ASSIGNMENTS
//------------------------------------------------------------------------------

// Load Cell ADC Pins
#define HW_LOADCELL_DOUT_PIN 3                                                // HX711 data output pin
#define HW_LOADCELL_SCK_PIN 2                                                 // HX711 serial clock pin

// Motor Control
#define HW_MOTOR_RELAY_PIN 43                                                 // GPIO pin for grinder motor control relay
#define HW_GRINDER_SETTLING_TIME_MS 500                                       // Startup transient immunity (tune based on mechanical rigidity, 0 to disable)

//------------------------------------------------------------------------------
// LOAD CELL ADC SPECIFICATIONS
//------------------------------------------------------------------------------
// Sample rate configuration
#define HW_LOADCELL_SAMPLE_RATE_SPS 10                                        // Current sample rate setting
#define HW_LOADCELL_SAMPLE_INTERVAL_MS (1000 / HW_LOADCELL_SAMPLE_RATE_SPS)   // Calculated sample interval

// Calibration validation
#define HW_LOADCELL_CAL_MIN_ADC_VALUE 1000                                    // Minimum ADC value to confirm weight placed on scale

//------------------------------------------------------------------------------
// DISPLAY SPECIFICATIONS  
//------------------------------------------------------------------------------
// All display configuration is now in src/config/display_profiles.h
// This includes: dimensions, pins, rotation, color order, inversion, brightness

#define HW_DISPLAY_MINIMAL_BRIGHTNESS_PERCENT 10                                 // Minimum brightness percentage to prevent display from going too dim

//------------------------------------------------------------------------------
// SERIAL COMMUNICATION
//------------------------------------------------------------------------------
#define HW_SERIAL_BAUD_RATE 115200                                             // UART baud rate for debug/logging output
