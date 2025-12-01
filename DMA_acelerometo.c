/*
  * - Core 0: Lê MPU6050 (Roll/Pitch) e envia via FIFO.
 * - Core 1: Recebe dados via FIFO e exibe no Display SSD1306.
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "pico/stdlib.h"
#include "pico/binary_info.h"
#include "hardware/i2c.h"
#include "pico/multicore.h"  

// Libs do display
#include "ssd1306.h"
#include "font.h"

// --- Configurações ---

// Pinos I2C para o MPU6050 (Sensor)
#define I2C_PORT_SENSOR i2c0
#define I2C_SDA_SENSOR 0
#define I2C_SCL_SENSOR 1
#define MPU_ENDERECO 0x68

// Pinos I2C para o Display OLED (UI)
#define I2C_PORT_DISP i2c1
#define I2C_SDA_DISP 14
#define I2C_SCL_DISP 15
#define DISP_ENDERECO 0x3C

// Pino para o LED 
#define LED_GPIO 12 // LED Azul 

// Funções de baixo nível para o MPU6050
// -------------------------------------------------
static void mpu6050_reset() {
    uint8_t buf[] = {0x6B, 0x80};// Registrador 0x6B (PWR_MGMT_1), valor 0x80 (Reset) 
    i2c_write_blocking(I2C_PORT_SENSOR, MPU_ENDERECO, buf, 2, false);
    sleep_ms(100);
    buf[1] = 0x00; // Valor 0x00 (Acordar)
    i2c_write_blocking(I2C_PORT_SENSOR, MPU_ENDERECO, buf, 2, false);
    sleep_ms(10);
}

static void mpu6050_read_raw(int16_t accel[3], int16_t gyro[3], int16_t *temp) {
    uint8_t buffer[6];
    uint8_t val = 0x3B; // Registrador inicial do Acelerômetro
    i2c_write_blocking(I2C_PORT_SENSOR, MPU_ENDERECO, &val, 1, true);
    i2c_read_blocking(I2C_PORT_SENSOR, MPU_ENDERECO, buffer, 6, false);
    for (int i = 0; i < 3; i++) {
        accel[i] = (buffer[i * 2] << 8) | buffer[(i * 2) + 1];
    }
    
    val = 0x43; // Registrador inicial do Giroscópio
    i2c_write_blocking(I2C_PORT_SENSOR, MPU_ENDERECO, &val, 1, true);
    i2c_read_blocking(I2C_PORT_SENSOR, MPU_ENDERECO, buffer, 6, false);
    for (int i = 0; i < 3; i++) {
        gyro[i] = (buffer[i * 2] << 8) | buffer[(i * 2) + 1];
    }

    val = 0x41; // Registrador da Temperatura
    i2c_write_blocking(I2C_PORT_SENSOR, MPU_ENDERECO, &val, 1, true);
    i2c_read_blocking(I2C_PORT_SENSOR, MPU_ENDERECO, buffer, 2, false);
    *temp = (buffer[0] << 8) | buffer[1];
}

// Função de callback para o botão (modo BOOTSEL)
#include "pico/bootrom.h"
#define botaoB 6
void gpio_irq_handler(uint gpio, uint32_t events) {
    reset_usb_boot(0, 0);
}
// -------------------------------------------------


// =================================================================
// == NÚCLEO 1 (CORE 1) - INTERFACE DO USUÁRIO (UI)
// == Esta função será executada em paralelo no Core 1 
// =================================================================
void core1_entry() {
    ssd1306_t ssd;
    char str_roll[20], str_pitch[20]; // Strings para os valores
    
    // 1. Inicializa periféricos do Core 1 (UI)
    
    // Inicializa I2C do Display
    i2c_init(I2C_PORT_DISP, 400 * 1000);
    gpio_set_function(I2C_SDA_DISP, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_DISP, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_DISP);
    gpio_pull_up(I2C_SCL_DISP);

    // Inicializa Display
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, DISP_ENDERECO, I2C_PORT_DISP);
    ssd1306_config(&ssd);
    ssd1306_fill(&ssd, false);
    ssd1306_send_data(&ssd);

    // Inicializa LED
    gpio_init(LED_GPIO); 
    gpio_set_dir(LED_GPIO, GPIO_OUT); 

    // 2. Loop principal do Core 1
    // Este loop aguarda dados do Core 0 via FIFO 
    while (1) {
        // Aguarda (bloqueia) até que os dados cheguem do Core 0 
        int32_t roll_int  = (int32_t)multicore_fifo_pop_blocking();
        int32_t pitch_int = (int32_t)multicore_fifo_pop_blocking();

        // Converte os dados de volta para float
        // (O Core 0 enviou como inteiro * 100)
        float roll  = roll_int / 100.0f;
        float pitch = pitch_int / 100.0f;
        
        // Pisca o LED para indicar que dados foram recebidos 
        gpio_put(LED_GPIO, !gpio_get(LED_GPIO));

        // Prepara as strings (usando o formato 5.1f)
        snprintf(str_roll,  sizeof(str_roll),  "%5.1f", roll);
        snprintf(str_pitch, sizeof(str_pitch), "%5.1f", pitch);

        // Atualiza o display 
        ssd1306_fill(&ssd, false); // Limpa o buffer
        
        // Desenha a interface estática
        ssd1306_draw_string(&ssd, "Monitoramento", 8, 9);
        ssd1306_draw_string(&ssd, "IMU    MPU6050", 10, 28);
        ssd1306_line(&ssd, 3, 37, 123, 37, true); // Linha divisória horizontal
        ssd1306_line(&ssd, 63, 37, 63, 60, true); // Linha divisória vertical
        ssd1306_line(&ssd, 3, 17, 123, 17, true); // Linha divisória horizontal
       
        // Desenha os rótulos dos dados
        ssd1306_draw_string(&ssd, "roll", 14, 41);
        ssd1306_draw_string(&ssd, "pitch", 73, 41);

        // Exibe os valores dinâmicos (recebidos do Core 0)
        ssd1306_draw_string(&ssd, str_roll, 14, 52);
        ssd1306_draw_string(&ssd, str_pitch, 73, 52);
        
        ssd1306_send_data(&ssd); // Envia o buffer para o display
    }
}

// =================================================================
// == NÚCLEO 0 (CORE 0) - SENSORES E PROCESSAMENTO
// =================================================================
int main() {
    // Inicialização do BOOTSEL
    gpio_init(botaoB);
    gpio_set_dir(botaoB, GPIO_IN);
    gpio_pull_up(botaoB);
    gpio_set_irq_enabled_with_callback(botaoB, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);

    stdio_init_all();
    printf("Tarefa Multicore - Iniciando Core 0 (Sensores)\n");
    sleep_ms(2000); // Delay para o monitor serial conectar

   // 1. Inicializa periféricos do Core 0 (Sensor MPU6050) 
    
    // Inicializa I2C do MPU6050
    i2c_init(I2C_PORT_SENSOR, 400 * 1000);
    gpio_set_function(I2C_SDA_SENSOR, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_SENSOR, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_SENSOR);
    gpio_pull_up(I2C_SCL_SENSOR);
    bi_decl(bi_2pins_with_func(I2C_SDA_SENSOR, I2C_SCL_SENSOR, GPIO_FUNC_I2C));
    
    mpu6050_reset();

    // Variáveis dos sensores
    int16_t aceleracao[3], gyro[3], temp;

    // 2. Lança o Core 1
    // Esta função inicia a 'core1_entry' no outro núcleo 
    printf("Iniciando Core 1 (UI)...\n");
    multicore_launch_core1(core1_entry);
    printf("Core 0 (Sensores) em execucao.\n");

    // 3. Loop principal do Core 0
   // Este loop lê o sensor e envia os dados para o Core 1 
    while (1) {
        // Leitura Sensor 1: MPU6050
        mpu6050_read_raw(aceleracao, gyro, &temp);
        
        // Processamento MPU6050 (cálculos de ângulo)
        float ax = aceleracao[0] / 16384.0f;
        float ay = aceleracao[1] / 16384.0f;
        float az = aceleracao[2] / 16384.0f;
        float roll  = atan2(ay, az) * 180.0f / M_PI;
        float pitch = atan2(-ax, sqrt(ay*ay + az*az)) * 180.0f / M_PI;

        // Imprime dados no monitor serial (Feedback do Core 0)
        printf("Core 0: [R: %.1f, P: %.1f]\n", roll, pitch);

        // Para enviar floats via FIFO (que só aceita uint32_t),
        // multiplicamos por 100 e enviamos como inteiros.
        int32_t roll_int  = (int32_t)(roll * 100.0f);
        int32_t pitch_int = (int32_t)(pitch * 100.0f);
        
        // Envia dados processados para o Core 1 via FIFO 
        multicore_fifo_push_blocking((uint32_t)roll_int);
        multicore_fifo_push_blocking((uint32_t)pitch_int);

        // Controla a taxa de aquisição
        sleep_ms(250); 
    }
}