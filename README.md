# DMA_Com_acelerometro 🚀

![Pico-W](https://img.shields.io/badge/Pico--W-Wi--Fi-blue?style=flat-square) ![C](https://img.shields.io/badge/Linguagem-C-brightgreen?style=flat-square) ![MPU6050](https://img.shields.io/badge/MPU6050-IMU-purple?style=flat-square) ![SSD1306](https://img.shields.io/badge/SSD1306-OLED-orange?style=flat-square) ![DMA](https://img.shields.io/badge/DMA-Direct--Memory--Access-yellow?style=flat-square) ![License](https://img.shields.io/badge/Licen%C3%A7a-MIT-lightgrey?style=flat-square)

**DMA_Com_acelerometro** é um projeto demonstrativo para **Raspberry Pi Pico / Pico W** que lê um **MPU6050** (aceleração + giroscópio), calcula *roll* e *pitch* em **Core 0** e apresenta os valores em um **OLED SSD1306 128×64** no **Core 1**. A transmissão do framebuffer para o display foi otimizada com **DMA**, liberando CPU durante a transferência I²C dos ~1024 bytes de imagem.

> Projeto mantido por **Heitor Rodrigues Lemos Dias** – Código aberto sob Licença MIT.

---

## 📂 Estrutura do repositório

| Caminho                   | Descrição                                                                    |
| ------------------------- | ---------------------------------------------------------------------------- |
| `dma_acelerometro.c`      | Código principal (Core 0 + Core 1) — leitura MPU6050, cálculo de ângulos, UI |
| `ssd1306.c` / `ssd1306.h` | Driver do display (inclui função `ssd1306_send_data_dma()`)                  |
| `font.h`                  | Fonte bitmap para renderer de texto                                          |
| `CMakeLists.txt`          | Script de build (ex.: para pico-sdk)                                         |
| `README.md`               | Este arquivo                                                                 |

---

## 🔧 Requisitos

### Hardware

Para o Hardware, foram utilizados os periféricos presentes na BitDogLab

|                 Componente | Qtde | Observação                      |
| -------------------------: | :--: | ------------------------------- |
| Raspberry Pi Pico / Pico W |   1  | com RP2040                      |
|              MPU6050 (I²C) |   1  | conexão I2C (ex.: i2c0)         |
|  OLED SSD1306 128×64 (I²C) |   1  | ligado a i2c1 no exemplo        |
|            LED (indicador) |   1  | opcional — GPIO 12 no exemplo   |
|            Jumpers / Fonte |   —  | alimentação estável recomendada |

### Software

| Ferramenta    | Versão mínima              |
| ------------- | -------------------------- |
| Extensão Pi Pico do VSCode     | 1.5.0 (compatível com 2.x) |


---

## ⚙️ Como configurar / compilar


```bash
git clone <repo-url>
cd DMA_Com_acelerometro
mkdir build && cd build
cmake .. -DPICO_BOARD=pico_w
make -j$(nproc)
```


---

## 🔌 Conexões (exemplo do código)

| Periférico              |      Sinal |    GPIO (exemplo) |
| ----------------------- | ---------: | ----------------: |
| MPU6050 SDA             | SDA (I2C0) |            GPIO 0 |
| MPU6050 SCL             | SCL (I2C0) |            GPIO 1 |
| SSD1306 SDA             | SDA (I2C1) |           GPIO 14 |
| SSD1306 SCL             | SCL (I2C1) |           GPIO 15 |
| LED indicador           |        Out |           GPIO 12 |
| Botão BOOTSEL (handler) |         In | GPIO 6 (opcional) |

Endereços I²C:

* MPU6050: `0x68`
* SSD1306: `0x3C`

Ajuste os `#define` no arquivo principal conforme seu hardware:

```c
#define I2C_PORT_SENSOR i2c0
#define I2C_SDA_SENSOR 0
#define I2C_SCL_SENSOR 1
#define MPU_ENDERECO 0x68

#define I2C_PORT_DISP i2c1
#define I2C_SDA_DISP 14
#define I2C_SCL_DISP 15
#define DISP_ENDERECO 0x3C
```

---

## 📖 Fluxo e funcionamento (resumo)

1. **Core 0**:

   * Inicializa I²C do MPU6050, faz reset e leituras brutas.
   * Converte acelerações para `ax`, `ay`, `az` e calcula *roll* e *pitch* via `atan2`.
   * Imprime no console e envia os valores ao Core 1 via **multicore FIFO** (multiplicados por 100 e empacotados como `uint32_t`).
   * Taxa de aquisição no exemplo: `sleep_ms(250)` → 4 Hz.

2. **Core 1**:

   * Inicializa I²C do display, driver SSD1306 e canal DMA.
   * Aguarda dados vindos do FIFO (`multicore_fifo_pop_blocking()`), reconstrói floats e atualiza o framebuffer.
   * Inicia a transferência do framebuffer para o SSD1306 **via DMA** chamando `ssd1306_send_data_dma(&ssd)`.
   * Pisca um LED a cada atualização para feedback.

---

## 🧠 Observação técnica — CPU vs DMA

> Anteriormente, o envio de dados para o display OLED era realizado inteiramente pela CPU (Core 1) através da função `i2c_write_blocking`, o que mantinha o processador ocupado durante toda a transmissão dos 1024 bytes de imagem.
> Na nova implementação, foi introduzido o **DMA (Direct Memory Access)**. Agora, a CPU é responsável apenas por preparar o buffer de imagem (convertendo para 16 bits para incluir os comandos de controle I2C) e configurar o canal de DMA. Uma vez iniciado, o controlador DMA assume a transferência de dados da memória RAM diretamente para o periférico I2C de forma autônoma. Isso retira a carga de transferência de dados da CPU, permitindo uma comunicação mais eficiente e cumprindo o requisito de uso de periféricos avançados do microcontrolador RP2040.

Em outras palavras: a CPU apenas prepara/desenha e dispara o DMA; o controlador de DMA cuida do envio byte-a-byte ao periférico I2C, reduzindo jitter e liberando ciclos para outras tarefas.

---

## ⚠️ Trechos relevantes (do código exemplo)

**Envio via FIFO (Core 0):**

```c
float roll  = ...;
float pitch = ...;
int32_t roll_int  = (int32_t)(roll * 100.0f);
int32_t pitch_int = (int32_t)(pitch * 100.0f);
multicore_fifo_push_blocking((uint32_t)roll_int);
multicore_fifo_push_blocking((uint32_t)pitch_int);
```

**Recepção (Core 1) e envio DMA:**

```c
int32_t roll_int  = (int32_t)multicore_fifo_pop_blocking();
int32_t pitch_int = (int32_t)multicore_fifo_pop_blocking();
float roll  = roll_int / 100.0f;
float pitch = pitch_int / 100.0f;

/* preparar buffer do SSD1306 (framebuffer + comandos I2C)
   chamar: ssd1306_send_data_dma(&ssd);
*/
```

---

## 🐛 Troubleshooting

* **Nada aparece no display**

  * Confirme endereços I²C e pull-ups.
  * Verifique se `i2c_init()` e `ssd1306_init()` retornaram sem erro.
  * Teste envio bloqueante (`i2c_write_blocking`) para isolar problema de DMA x I2C.

* **Valores do MPU6050 instáveis**

  * Verifique alimentação e terra.
  * Aguarde alguns ms após reset do MPU6050 antes de ler.
  * Considere filtro de média ou calibração de offset.

* **FIFO bloqueando**

  * Garanta que Core 1 esteja rodando (multicore_launch_core1) antes de empurrar muitos elementos.
  * Use `multicore_fifo_wready()` ou limite o envio se necessário.

* **DMA não dispara / não finaliza**

  * Verifique configuração do canal DMA no `ssd1306.c`.
  * Confirme que o periférico I2C está configurado para uso com DMA (registros corretos).
  * Cheque flags/IRQ de conclusão do DMA.

---

## ✅ Melhorias sugeridas

* Implementar filtro complementar ou Kalman para fusão de giroscópio e acelerômetro.
* Double buffering do framebuffer + DMA com interrupção de conclusão para evitar tearing.
* Exportar logs com timestamps via USB serial para análise de performance.
* Expor taxa de amostragem e opções via interface serial ou botões.
* Adicionar calibração de sensores (offsets de aceleração/giroscópio) persistente em flash/EEPROM (se disponível).

---

## 🧾 Licença

Distribuído sob **MIT License** — veja `LICENSE` para detalhes.

---
