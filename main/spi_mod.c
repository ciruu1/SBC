#include "driver/spi_master.h"

#include "spi_mod.h"

/* SPI Config */
spi_device_handle_t spi_init(void) {
  spi_device_handle_t spi;
  esp_err_t ret;
  spi_bus_config_t buscfg = {
    .miso_io_num = PIN_NUM_MISO,
    .mosi_io_num = PIN_NUM_MOSI,
    .sclk_io_num = PIN_NUM_CLK,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
  };

  //Initialize the SPI bus
  ret = spi_bus_initialize(VSPI_HOST, &buscfg, DMA_CHAN);
  ESP_ERROR_CHECK(ret);

  spi_device_interface_config_t devCfg={
    .mode = 0,
    .clock_speed_hz = 1000000,
    .spics_io_num = PIN_NUM_CS,
    .queue_size=8,
    .address_bits = 8, 
    .command_bits = 8
  };

  ret = spi_bus_add_device(VSPI_HOST, &devCfg, &spi);
  ESP_ERROR_CHECK(ret);

  return spi;
}
