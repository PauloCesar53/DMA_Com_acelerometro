#include "ssd1306.h"
#include "font.h"
#include "hardware/dma.h"

// Canal DMA 
int dma_tx_chan;
dma_channel_config dma_config;

// Buffer auxiliar de 16 bits para garantir o protocolo correto do I2C
// (Dado + Bits de Controle STOP/RESTART)
static uint16_t i2c_dma_buffer[1025]; 

void ssd1306_dma_setup(i2c_inst_t *i2c) {
    // 1. Requisitar canal DMA livre 
    dma_tx_chan = dma_claim_unused_channel(true);

    // 2. Configurações gerais
    dma_config = dma_channel_get_default_config(dma_tx_chan);
    
    // Usar 16 bits para enviar DADO + COMANDO juntos
    channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_16); 
    
    channel_config_set_read_increment(&dma_config, true);
    channel_config_set_write_increment(&dma_config, false);
    
    // Pacing pelo I2C TX
    channel_config_set_dreq(&dma_config, i2c_get_dreq(i2c, true)); 
}

void ssd1306_init(ssd1306_t *ssd, uint8_t width, uint8_t height, bool external_vcc, uint8_t address, i2c_inst_t *i2c) {
  ssd->width = width;
  ssd->height = height;
  ssd->pages = height / 8U;
  ssd->address = address;
  ssd->i2c_port = i2c;
  ssd->bufsize = ssd->pages * ssd->width + 1;
  ssd->ram_buffer = calloc(ssd->bufsize, sizeof(uint8_t));
  ssd->ram_buffer[0] = 0x40; // Byte de controle (Co=0, D/C=1)
  ssd->port_buffer[0] = 0x80;
}

void ssd1306_config(ssd1306_t *ssd) {
  ssd1306_command(ssd, SET_DISP | 0x00);
  ssd1306_command(ssd, SET_MEM_ADDR);
  ssd1306_command(ssd, 0x01);
  ssd1306_command(ssd, SET_DISP_START_LINE | 0x00);
  ssd1306_command(ssd, SET_SEG_REMAP | 0x01);
  ssd1306_command(ssd, SET_MUX_RATIO);
  ssd1306_command(ssd, HEIGHT - 1);
  ssd1306_command(ssd, SET_COM_OUT_DIR | 0x08);
  ssd1306_command(ssd, SET_DISP_OFFSET);
  ssd1306_command(ssd, 0x00);
  ssd1306_command(ssd, SET_COM_PIN_CFG);
  ssd1306_command(ssd, 0x12);
  ssd1306_command(ssd, SET_DISP_CLK_DIV);
  ssd1306_command(ssd, 0x80);
  ssd1306_command(ssd, SET_PRECHARGE);
  ssd1306_command(ssd, 0xF1);
  ssd1306_command(ssd, SET_VCOM_DESEL);
  ssd1306_command(ssd, 0x30);
  ssd1306_command(ssd, SET_CONTRAST);
  ssd1306_command(ssd, 0xFF);
  ssd1306_command(ssd, SET_ENTIRE_ON);
  ssd1306_command(ssd, SET_NORM_INV);
  ssd1306_command(ssd, SET_CHARGE_PUMP);
  ssd1306_command(ssd, 0x14);
  ssd1306_command(ssd, SET_DISP | 0x01);
}

void ssd1306_command(ssd1306_t *ssd, uint8_t command) {
  ssd->port_buffer[1] = command;
  i2c_write_blocking(
    ssd->i2c_port,
    ssd->address,
    ssd->port_buffer,
    2,
    false
  );
}

void ssd1306_send_data_dma(ssd1306_t *ssd) {
    // 1. Envia comandos de posicionamento (via CPU, é rápido)
    ssd1306_command(ssd, SET_COL_ADDR);
    ssd1306_command(ssd, 0);
    ssd1306_command(ssd, ssd->width - 1);
    ssd1306_command(ssd, SET_PAGE_ADDR);
    ssd1306_command(ssd, 0);
    ssd1306_command(ssd, ssd->pages - 1);

    // 2. PREPARAÇÃO DO BUFFER 
    // Copia os dados de 8 bits para 16 bits.
    // Isso garante que os bits 8..15 sejam ZERO (Modo Escrita).
    for (size_t i = 0; i < ssd->bufsize; i++) {
        i2c_dma_buffer[i] = ssd->ram_buffer[i];
    }

    // 3. ADICIONA O STOP BIT MANUALMENTE
    // No último byte, ativamos o bit 9 (0x200), que sinaliza o fim da transmissão I2C.
    i2c_dma_buffer[ssd->bufsize - 1] |= 0x200;

    // 4. ENVIA TUDO VIA DMA (SEM GAPS!)
    dma_channel_configure(
        dma_tx_chan,
        &dma_config,
        &i2c_get_hw(ssd->i2c_port)->data_cmd, // Registrador de dados
        i2c_dma_buffer,                       // Buffer preparado de 16 bits
        ssd->bufsize,                         // Tamanho total
        true                                  // Inicia agora
    );

    // Espera finalizar para não corromper o buffer na próxima volta
    dma_channel_wait_for_finish_blocking(dma_tx_chan);
}

void ssd1306_pixel(ssd1306_t *ssd, uint8_t x, uint8_t y, bool value) {
  uint16_t index = (y >> 3) + (x << 3) + 1;
  uint8_t pixel = (y & 0b111);
  if (value)
    ssd->ram_buffer[index] |= (1 << pixel);
  else
    ssd->ram_buffer[index] &= ~(1 << pixel);
}

void ssd1306_fill(ssd1306_t *ssd, bool value) {
    for (uint8_t y = 0; y < ssd->height; ++y) {
        for (uint8_t x = 0; x < ssd->width; ++x) {
            ssd1306_pixel(ssd, x, y, value);
        }
    }
}

void ssd1306_rect(ssd1306_t *ssd, uint8_t top, uint8_t left, uint8_t width, uint8_t height, bool value, bool fill) {
  for (uint8_t x = left; x < left + width; ++x) {
    ssd1306_pixel(ssd, x, top, value);
    ssd1306_pixel(ssd, x, top + height - 1, value);
  }
  for (uint8_t y = top; y < top + height; ++y) {
    ssd1306_pixel(ssd, left, y, value);
    ssd1306_pixel(ssd, left + width - 1, y, value);
  }
  if (fill) {
    for (uint8_t x = left + 1; x < left + width - 1; ++x) {
      for (uint8_t y = top + 1; y < top + height - 1; ++y) {
        ssd1306_pixel(ssd, x, y, value);
      }
    }
  }
}

void ssd1306_line(ssd1306_t *ssd, uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, bool value) {
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    while (true) {
        ssd1306_pixel(ssd, x0, y0, value);
        if (x0 == x1 && y0 == y1) break;
        int e2 = err * 2;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void ssd1306_hline(ssd1306_t *ssd, uint8_t x0, uint8_t x1, uint8_t y, bool value) {
  for (uint8_t x = x0; x <= x1; ++x)
    ssd1306_pixel(ssd, x, y, value);
}

void ssd1306_vline(ssd1306_t *ssd, uint8_t x, uint8_t y0, uint8_t y1, bool value) {
  for (uint8_t y = y0; y <= y1; ++y)
    ssd1306_pixel(ssd, x, y, value);
}

void ssd1306_draw_char(ssd1306_t *ssd, char c, uint8_t x, uint8_t y) {
  uint16_t index = 0;
  if (c >= ' ' && c <= '~') {
    index = (c - ' ') * 8;
  }
  for (uint8_t i = 0; i < 8; ++i) {
    uint8_t line = font[index + i];
    for (uint8_t j = 0; j < 8; ++j) {
      ssd1306_pixel(ssd, x + i, y + j, line & (1 << j));
    }
  }
}

void ssd1306_draw_string(ssd1306_t *ssd, const char *str, uint8_t x, uint8_t y) {
  while (*str) {
    ssd1306_draw_char(ssd, *str++, x, y);
    x += 8;
    if (x + 8 >= ssd->width) {
      x = 0;
      y += 8;
    }
    if (y + 8 >= ssd->height) {
      break;
    }
  }
}