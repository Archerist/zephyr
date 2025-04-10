/*
 */
#include "max31856.h"

static int max31856_spi_write(const struct device *dev, uint8_t reg, uint8_t *data, size_t len)
{
	const struct max31856_config *cfg = dev->config;

	const struct spi_buf bufs[] = {{
					       .buf = &reg,
					       .len = 1,
				       },
				       {.buf = data, .len = len}};

	const struct spi_buf_set tx = {.buffers = bufs, .count = 2};

	return spi_write_dt(&cfg->spi, &tx);
}

static int max31856_spi_read(const struct device *dev, uint8_t reg, uint8_t *data, size_t len)
{
	const struct max31856_config *cfg = dev->config;

	reg &= 0x7F;
	const struct spi_buf tx_buf = {.buf = &reg, .len = 1};
	const struct spi_buf_set tx = {.buffers = &tx_buf, .count = 1};
	struct spi_buf rx_buf[] = {{
					   .buf = &reg,
					   .len = 1,
				   },
				   {.buf = data, .len = len}};
	const struct spi_buf_set rx = {.buffers = rx_buf, .count = 2};

	return spi_transceive_dt(&cfg->spi, &tx, &rx);
}

/**
 * @brief Set device fail threshold registers
 *
 * @param device device instance
 * @return 0 if successful, or negative error code from SPI API
 */
static int set_threshold_values(const struct device *dev)
{
	const struct max31856_config *config = dev->config;
	uint8_t cmd[] = {(config->cj_high_fault >>7) & 0x00ff,(config->cj_low_fault) & 0x00ff,
		(config->lt_high_fault >> 7) & 0x00ff, (config->lt_high_fault << 1) & 0x00ff,
		(config->lt_low_fault >> 7) & 0x00ff, (config->lt_low_fault << 1) & 0x00ff};
	int err = max31856_spi_write(dev, WR(REG_LT_HIGH_FAULT_THR_MSB), cmd, 6);

	if (err < 0) {
		LOG_ERR("Error write SPI%d\n", err);
	}
	return err;
}

/**
 * @brief Set device configuration register
 *
 * @param device device instance
 * @return 0 if successful, or negative error code from SPI API
 */
static int configure_device(const struct device *dev)
{
	struct max31856_data *data = dev->data;
	uint8_t cmd[] = {data->cr0};
	int err = max31856_spi_write(dev, WR(REG_CR0), cmd, 1);
	uint8_t cmd2[] = {data->cr1};
	err = max31856_spi_write(dev, WR(REG_CR1), cmd2, 1);
	uint8_t cmd3[] = {data->fault_mask};
	err = max31856_spi_write(dev, WR(REG_FAULT_MASK), cmd3, 1);
	err = set_threshold_values(dev);
	if (err < 0) {
		LOG_ERR("Error write SPI%d\n", err);
	}
	return err;
}


/**
 * @brief Enable/Disable One Shot for Max31856
 *
 * @param device device instance
 * @param enable true, turn on one-shot, false, turn off one-shot
 * @return 0 if successful, or negative error code from SPI API
 */
static int max31856_set_one_shot(const struct device *dev, bool enable)
{
	struct max31856_data *data = dev->data;

	WRITE_BIT(data->cr0, 7, enable);
	return configure_device(dev);
}

static int max31856_fault_register(const struct device *dev)
{
	uint8_t fault_register;
	uint8_t saved_fault_bits;

	max31856_spi_read(dev, (REG_FAULT_STATUS), &fault_register, 1);
	struct max31856_data *data = dev->data;
	struct max31856_config *config = dev->config;
	saved_fault_bits  = data->cr0;
	/*Clear fault register */
	if(config->fault_interrupt_mode){
		WRITE_BIT(data->cr0, 1, 1);
		configure_device(dev);	
	}
	LOG_ERR("Fault Register: 0x%02x", fault_register);
	WRITE_BIT(data->cr0, 1, 0);
	data->cr0 |= saved_fault_bits;

	return 0;
}

/**
 * @brief Get temperature value in oC for device
 *
 * @param device device instance
 * @param temperature measured temperature
 * @return 0 if successful, or negative error code
 */
static int max31856_get_temperature(const struct device *dev)
{
	const struct max31856_config *config = dev->config;
	struct max31856_data *data = dev->data;

	uint8_t read_reg[5] = {0};
	/* Maximum Waiting Time for Temperature Conversion (Page 4 of the datasheet)*/
	k_sleep(K_MSEC(185));

	if (gpio_pin_get_dt(&config->fault_gpios)){
		max31856_fault_register(dev);
		return -EIO;
	}

	int err = max31856_spi_read(dev, (REG_CJTH), read_reg, 5);

	if (err < 0) {
		LOG_ERR("SPI read %d\n", err);
		return -EIO;
	}

	uint32_t temp_reg = (read_reg[2] << 16) | (read_reg[3]<<8) | read_reg[4];
	temp_reg = sys_be32_to_cpu(temp_reg);

	uint32_t cj_reg = (read_reg[0]<<8) | read_reg[1];


	LOG_DBG("RAW: %02X %02X %02X %02X %02X , %08X %08X");
	uint8_t HB = read_reg[4];
	temp_reg = temp_reg >>5;
    if(HB & 0x80){
        temp_reg -= 0x80000;
    }
	data->temperature = temp_reg / 128.f;

	HB = read_reg[0];
    cj_reg >>= 2;
    if(HB & 0x80){
        cj_reg -= 0x800;
    }
    data->cold_junction = cj_reg/64.f;
	return 0;
}

static int max31856_init(const struct device *dev)
{
	const struct max31856_config *config = dev->config;

	if (!spi_is_ready_dt(&config->spi)) {
		return -ENODEV;
	}
	struct max31856_data *data = dev->data;
	/* Set the confgiuration register */
	data->cr0 = 0;
	data->cr1 = 0;

	WRITE_BIT(data->cr0, 7, config->conversion_mode);
	WRITE_BIT(data->cr0, 6, config->one_shot);
	WRITE_BIT(data->cr0, 3, config->cold_junction);
	WRITE_BIT(data->cr0, 2, config->fault_interrupt_mode);
	WRITE_BIT(data->cr0, 0, config->filter_50hz);
	data->cr1 |= config->thermocouple_type & 0b00001111;
	data->cr1 |= config->averaging_mode & 0b01110000;
	configure_device(dev);
	return 0;
}

static int max31856_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	if (chan != SENSOR_CHAN_ALL && chan != SENSOR_CHAN_AMBIENT_TEMP && chan != SENSOR_CHAN_DIE_TEMP) {
		LOG_ERR("Invalid channel provided");
		return -ENOTSUP;
	}
	return max31856_get_temperature(dev);
}

static int max31856_channel_get(const struct device *dev, enum sensor_channel chan,
				struct sensor_value *val)
{
	struct max31856_data *data = dev->data;

	switch (chan) {
	case SENSOR_CHAN_AMBIENT_TEMP:
		return sensor_value_from_double(val, data->temperature);
	case SENSOR_CHAN_DIE_TEMP:
		return sensor_value_from_double(val, data->cold_junction);
	default:
		return -ENOTSUP;
	}
}


static DEVICE_API(sensor, max31856_api_funcs) = {
	.sample_fetch = max31856_sample_fetch,
	.channel_get = max31856_channel_get,
};

#define MAX31856_DEFINE(inst)                                                                      \
                                                                                                   \
	static struct max31856_data max31856_data_##inst;                                          \
                                                                                                   \
	static const struct max31856_config max31856_config_##inst = {                             \
		.spi = SPI_DT_SPEC_INST_GET(inst, SPI_MODE_CPHA | SPI_WORD_SET(8), 0),             \
		.fault_gpios =   GPIO_DT_SPEC_INST_GET(inst, fault_gpios),                      \
		.drdy_gpios = GPIO_DT_SPEC_INST_GET(inst, drdy_gpios),                  \
		.conversion_mode = false,                                                          \
		.one_shot = true,                                      \
		.filter_50hz = DT_INST_PROP(inst, filter_50hz),                                    \
		.cj_high_fault = DT_INST_PROP(inst, cj_low_fault),                                \
		.cj_low_fault = DT_INST_PROP(inst, cj_high_fault),                              \
		.cj_offset = DT_INST_PROP(inst, cj_offset),                              \
		.lt_high_fault = DT_INST_PROP(inst, lt_high_fault),                              \
		.lt_low_fault = DT_INST_PROP(inst, lt_low_fault),                              \
		.fault_mask = DT_INST_PROP(inst, fault_mask),                              \
	};                                                                                         \
                                                                                                   \
	SENSOR_DEVICE_DT_INST_DEFINE(inst, max31856_init, NULL, &max31856_data_##inst,             \
			      &max31856_config_##inst, POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY,   \
			      &max31856_api_funcs);

/* Create the struct device for every status "okay" node in the devicetree. */
DT_INST_FOREACH_STATUS_OKAY(MAX31856_DEFINE)
