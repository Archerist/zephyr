/*
 */
#ifndef _MAX31856""_H
#define _MAX31856_H

#define DT_DRV_COMPAT maxim_max31856

#include <math.h>

#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util_macro.h>
#include <zephyr/sys/byteorder.h>
LOG_MODULE_REGISTER(MAX31856, CONFIG_SENSOR_LOG_LEVEL);

#define WR(reg)		       ((reg) | 0x80)
#define REG_CR0				0x00
#define REG_CR1				0x01
#define REG_FAULT_MASK		0x02
#define REG_CJ_HIGH_FAULT_THR_MSB 0x03
#define REG_LT_HIGH_FAULT_THR_MSB 0x05
#define REG_LT_TEMP_HB 0x0C
#define REG_FAULT_STATUS 0x0F
#define REG_CJTH 0x0A

struct max31856_data {
	double temperature;
	double cold_junction;
	uint8_t cr0;
	uint8_t cr1;
	uint8_t fault_mask;
};

/**
 * Configuration struct to the MAX31856.
 */
struct max31856_config {
	const struct spi_dt_spec spi;
	const struct gpio_dt_spec fault_pin;
	const struct gpio_dt_spec drdy_pin;
	bool conversion_mode;
	bool one_shot;
	uint8_t thermocouple_type;
	uint8_t averaging_mode;
	bool filter_50hz;
	bool fault_interrupt_mode;
	bool cold_junction;
	uint8_t fault_mask;
	int8_t cj_offset;
	int8_t cj_low_fault;
	int8_t cj_high_fault;
	int16_t lt_low_fault;
	int16_t lt_high_fault;
};

/* Bit manipulation macros */
#define TESTBIT(data, pos) ((0u == (data & BIT(pos))) ? 0u : 1u)

#endif
