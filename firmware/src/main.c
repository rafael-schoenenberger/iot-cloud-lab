#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "stream_buffer.h"
#include "aws_certs.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* 1 = build for the Renode simulation, 0 = build for real STM32F407 hardware.
 * Renode's stm32f4.repl doesn't map the Cortex-M bit-band alias, so the
 * standard HAL_RCC_OscConfig() PLL enable (__HAL_RCC_PLL_ENABLE(), which
 * writes PLLON through that alias) never sets PLLRDY and hangs in
 * Error_Handler(). Override with -DTARGET_RENODE=0 to build for hardware. */
#ifndef TARGET_RENODE
#define TARGET_RENODE 1
#endif

static UART_HandleTypeDef huart3;
static UART_HandleTypeDef huart1;
static I2C_HandleTypeDef hi2c1;
static QueueHandle_t qSensorData;

typedef struct {
    int8_t temp_c;
    uint32_t uptime_ms; /* HAL_GetTick() at the moment the sample was read on I2C */
} SensorSample_t;

/* UART1 RX: interrupt-driven, one byte at a time, fed into a StreamBuffer
 * so ModemTask can block (xStreamBufferReceive) instead of polling for AT
 * responses - see HAL_UART_RxCpltCallback/USART1_IRQHandler below. */
static StreamBufferHandle_t xUart1Rx;
static uint8_t uart1_rx_byte;

#define TMP108_ADDR 0x48

/* Placeholder - a real SIM/carrier would define the actual APN string. */
#define AWS_APN "iot"

extern void xPortSysTickHandler(void);

void SysTick_Handler(void)
{
    HAL_IncTick();
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        xPortSysTickHandler();
    }
}

void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart1);
}

/* Fires once per received byte; uart1_init() re-arms HAL_UART_Receive_IT
 * for the next byte right after handing this one to ModemTask. */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART1) {
        return;
    }

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xStreamBufferSendFromISR(xUart1Rx, &uart1_rx_byte, 1, &xHigherPriorityTaskWoken);
    HAL_UART_Receive_IT(&huart1, &uart1_rx_byte, 1);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

static void Error_Handler(void)
{
    __disable_irq();
    while (1) {
    }
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;
    Error_Handler();
}

/* HSE = 25 MHz -> PLL -> SYSCLK = 168 MHz, APB1 = 42 MHz, APB2 = 84 MHz */
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;

#if TARGET_RENODE
    /* Bring up HSE only through HAL_RCC_OscConfig(); the PLL is started
     * manually below via plain register writes (see TARGET_RENODE above). */
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        Error_Handler();
    }

    WRITE_REG(RCC->PLLCFGR, RCC_PLLSOURCE_HSE | 25U
              | (336U << RCC_PLLCFGR_PLLN_Pos)
              | (((RCC_PLLP_DIV2 >> 1U) - 1U) << RCC_PLLCFGR_PLLP_Pos)
              | (7U << RCC_PLLCFGR_PLLQ_Pos));
    SET_BIT(RCC->CR, RCC_CR_PLLON);
    {
        uint32_t tickstart = HAL_GetTick();
        while (READ_BIT(RCC->CR, RCC_CR_PLLRDY) == 0U) {
            if ((HAL_GetTick() - tickstart) > 100U) {
                Error_Handler();
            }
        }
    }
#else
    /* Real hardware: the standard HAL path works fine since the CPU's
     * bit-band alias is actually wired up. */
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM = 25;
    RCC_OscInitStruct.PLL.PLLN = 336;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ = 7;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        Error_Handler();
    }
#endif

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                 | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK) {
        Error_Handler();
    }
}

void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART3) {
        __HAL_RCC_USART3_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();

        /* PB10 = USART3_TX, PB11 = USART3_RX (AF7) */
        GPIO_InitTypeDef gpio = {0};
        gpio.Pin = GPIO_PIN_10 | GPIO_PIN_11;
        gpio.Mode = GPIO_MODE_AF_PP;
        gpio.Pull = GPIO_PULLUP;
        gpio.Speed = GPIO_SPEED_FREQ_HIGH;
        gpio.Alternate = GPIO_AF7_USART3;
        HAL_GPIO_Init(GPIOB, &gpio);
        return;
    }

    if (huart->Instance == USART1) {
        __HAL_RCC_USART1_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();

        /* PA9 = USART1_TX, PA10 = USART1_RX (AF7) - SARA-R412M modem link */
        GPIO_InitTypeDef gpio = {0};
        gpio.Pin = GPIO_PIN_9 | GPIO_PIN_10;
        gpio.Mode = GPIO_MODE_AF_PP;
        gpio.Pull = GPIO_PULLUP;
        gpio.Speed = GPIO_SPEED_FREQ_HIGH;
        gpio.Alternate = GPIO_AF7_USART1;
        HAL_GPIO_Init(GPIOA, &gpio);

        /* Priority 6: numerically >= configMAX_SYSCALL_INTERRUPT_PRIORITY,
         * required for the ISR above to safely call xStreamBufferSendFromISR(). */
        HAL_NVIC_SetPriority(USART1_IRQn, 6, 0);
        HAL_NVIC_EnableIRQ(USART1_IRQn);
        return;
    }
}

static void uart3_init(void)
{
    huart3.Instance = USART3;
    huart3.Init.BaudRate = 115200;
    huart3.Init.WordLength = UART_WORDLENGTH_8B;
    huart3.Init.StopBits = UART_STOPBITS_1;
    huart3.Init.Parity = UART_PARITY_NONE;
    huart3.Init.Mode = UART_MODE_TX_RX;
    huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart3.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart3);
}

static void uart1_init(void)
{
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart1);

    xUart1Rx = xStreamBufferCreate(128, 1);
    configASSERT(xUart1Rx != NULL);
    HAL_UART_Receive_IT(&huart1, &uart1_rx_byte, 1);
}

void HAL_I2C_MspInit(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance != I2C1) {
        return;
    }

    __HAL_RCC_I2C1_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* PB6 = I2C1_SCL, PB7 = I2C1_SDA (AF4), open-drain as required on an I2C bus */
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    gpio.Mode = GPIO_MODE_AF_OD;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &gpio);
}

static void i2c1_init(void)
{
    hi2c1.Instance = I2C1;
    hi2c1.Init.ClockSpeed = 100000;
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    HAL_I2C_Init(&hi2c1);
}

static void at_send(const char *cmd)
{
    HAL_UART_Transmit(&huart1, (const uint8_t *)cmd, (uint16_t)strlen(cmd), HAL_MAX_DELAY);
}

/* Blocks (no polling) until `expected` arrives on UART1 or the timeout
 * elapses. Used only for AT+USECMNG's '>' prompt, which - unlike every
 * other modem response - is not itself CR/LF-terminated. */
static bool uart1_wait_char(char expected, uint32_t timeout_ms)
{
    TickType_t start = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

    for (;;) {
        TickType_t elapsed = xTaskGetTickCount() - start;
        if (elapsed >= timeout_ticks) {
            return false;
        }
        uint8_t byte;
        if (xStreamBufferReceive(xUart1Rx, &byte, 1, timeout_ticks - elapsed) == 0) {
            return false;
        }
        if ((char)byte == expected) {
            return true;
        }
    }
}

/* Blocks until one full CR/LF-terminated line arrives on UART1 (leading/
 * blank lines, e.g. the \r\n before every response, are skipped), or the
 * timeout elapses. `line` is left NUL-terminated with the terminator
 * stripped. */
static bool uart1_read_line(char *line, size_t line_size, uint32_t timeout_ms)
{
    size_t len = 0;
    TickType_t start = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

    for (;;) {
        TickType_t elapsed = xTaskGetTickCount() - start;
        if (elapsed >= timeout_ticks) {
            return false;
        }
        uint8_t byte;
        if (xStreamBufferReceive(xUart1Rx, &byte, 1, timeout_ticks - elapsed) == 0) {
            return false;
        }
        if (byte == '\r' || byte == '\n') {
            if (len == 0) {
                continue;
            }
            line[len] = '\0';
            return true;
        }
        if (len < line_size - 1) {
            line[len++] = (char)byte;
        }
    }
}

/* Reads lines until one contains `needle` (success) or an "ERROR" line or
 * the timeout arrives first (failure). Some responses (e.g. AT+USECMNG's
 * "+USECMNG: ...\r\nOK\r\n") span an info line before the final result
 * code, so a single uart1_read_line() call is not always enough. */
static bool at_wait_for(const char *needle, uint32_t timeout_ms)
{
    char line[128];
    TickType_t start = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

    while ((xTaskGetTickCount() - start) < timeout_ticks) {
        TickType_t elapsed = xTaskGetTickCount() - start;
        if (!uart1_read_line(line, sizeof(line), pdTICKS_TO_MS(timeout_ticks - elapsed))) {
            return false;
        }
        if (strstr(line, needle) != NULL) {
            return true;
        }
        if (strcmp(line, "ERROR") == 0) {
            return false;
        }
    }
    return false;
}

static bool at_wait_ok(uint32_t timeout_ms)
{
    return at_wait_for("OK", timeout_ms);
}

/* Streams a certificate/key over UART1 via AT+USECMNG=0,... (section 19.2
 * of the SARA-R4/N4 AT Commands Manual): send the command, wait for the
 * '>' prompt, then push the raw bytes. */
static bool at_send_cert(int type, const char *internal_name, const char *data, size_t data_len)
{
    char cmd[96];
    snprintf(cmd, sizeof(cmd), "AT+USECMNG=0,%d,\"%s\",%u\r\n",
             type, internal_name, (unsigned)data_len);
    at_send(cmd);

    if (!uart1_wait_char('>', 2000)) {
        return false;
    }

    HAL_UART_Transmit(&huart1, (const uint8_t *)data, (uint16_t)data_len, HAL_MAX_DELAY);
    return at_wait_ok(5000);
}

/* AT+UMQTTC's publish command takes the message as a quoted string
 * parameter, but our payloads are JSON containing literal '"' characters,
 * which a quoted-parameter AT parser cannot tell apart from the closing
 * quote. Hex-encoding (hex_mode=1, see 24.5.3) sidesteps that entirely. */
static void hex_encode(const char *in, char *out, size_t out_size)
{
    static const char hex_chars[] = "0123456789abcdef";
    size_t i = 0;
    for (; in[i] != '\0' && (i * 2 + 2) < out_size; i++) {
        out[i * 2] = hex_chars[((uint8_t)in[i] >> 4) & 0xF];
        out[i * 2 + 1] = hex_chars[(uint8_t)in[i] & 0xF];
    }
    out[i * 2] = '\0';
}

static void HWTask(void *argument)
{
    (void)argument;
    char msg[32];
    int count = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        int len = snprintf(msg, sizeof(msg), "Hello World #%d\r\n", ++count);
        HAL_UART_Transmit(&huart3, (uint8_t *)msg, (uint16_t)len, HAL_MAX_DELAY);
    }
}

static void TempTask(void *argument)
{
    (void)argument;
    char msg[64];

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(2000));

        int8_t raw;
        if (HAL_I2C_Mem_Read(&hi2c1, TMP108_ADDR << 1, 0x00,
                              I2C_MEMADD_SIZE_8BIT, (uint8_t *)&raw, 1,
                              HAL_MAX_DELAY) != HAL_OK) {
            continue;
        }

        SensorSample_t sample = { .temp_c = raw, .uptime_ms = HAL_GetTick() };

        int len = snprintf(msg, sizeof(msg), "{\"temp_c\":%d,\"uptime_ms\":%lu}\r\n",
                            sample.temp_c, (unsigned long)sample.uptime_ms);
        HAL_UART_Transmit(&huart3, (uint8_t *)msg, (uint16_t)len, HAL_MAX_DELAY);

        xQueueSend(qSensorData, &sample, 0); /* 0 timeout: skip if still full */
    }
}

static void ModemTask(void *argument)
{
    (void)argument;
    char cmd[256];

    at_send("AT\r\n");
    at_wait_ok(2000);

    snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"\r\n", AWS_APN);
    at_send(cmd);
    at_wait_ok(2000);

    at_send("AT+CGATT=1\r\n");
    at_wait_ok(2000);

    at_send("AT+CGACT=1,1\r\n");
    at_wait_ok(2000);

    at_send_cert(0, "AWS-CA", AWS_ROOT_CA, AWS_ROOT_CA_LEN);
    at_send_cert(1, "AWS-CERT", AWS_DEVICE_CERT, AWS_DEVICE_CERT_LEN);
    at_send_cert(2, "AWS-KEY", AWS_DEVICE_KEY, AWS_DEVICE_KEY_LEN);

    /* TLS profile 0: validate the server cert against our imported CA. */
    at_send("AT+USECPRF=0,0,1\r\n");
    at_wait_ok(2000);
    at_send("AT+USECPRF=0,3,\"AWS-CA\"\r\n");
    at_wait_ok(2000);
    at_send("AT+USECPRF=0,5,\"AWS-CERT\"\r\n");
    at_wait_ok(2000);
    at_send("AT+USECPRF=0,6,\"AWS-KEY\"\r\n");
    at_wait_ok(2000);

    snprintf(cmd, sizeof(cmd), "AT+UMQTT=0,\"%s\"\r\n", AWS_IOT_CLIENT_ID);
    at_send(cmd);
    at_wait_ok(2000);

    snprintf(cmd, sizeof(cmd), "AT+UMQTT=2,\"%s\",8883\r\n", AWS_IOT_ENDPOINT);
    at_send(cmd);
    at_wait_ok(2000);

    at_send("AT+UMQTT=11,1,0\r\n"); /* TLS on, USECMNG profile 0 */
    at_wait_ok(2000);
    at_send("AT+UMQTT=12,1\r\n"); /* clean session */
    at_wait_ok(2000);

    at_send("AT+UMQTTC=1\r\n");
    at_wait_ok(2000);                    /* immediate ack that the request was accepted */
    at_wait_for("+UUMQTTC: 1,0", 30000); /* async connect result (24.5.4) */

    for (;;) {
        SensorSample_t sample;
        xQueueReceive(qSensorData, &sample, portMAX_DELAY);

        char json[48];
        snprintf(json, sizeof(json), "{\"temp_c\":%d,\"uptime_ms\":%lu}",
                 sample.temp_c, (unsigned long)sample.uptime_ms);
        char hex[96];
        hex_encode(json, hex, sizeof(hex));

        snprintf(cmd, sizeof(cmd), "AT+UMQTTC=2,0,0,1,\"%s\",\"%s\"\r\n", AWS_MQTT_TOPIC, hex);
        at_send(cmd);
        at_wait_ok(5000);
    }
}

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    uart3_init();
    i2c1_init();
    uart1_init();

    qSensorData = xQueueCreate(4, sizeof(SensorSample_t));
    configASSERT(qSensorData != NULL);

    // configASSERT(xTaskCreate(HWTask, "HWTask", 256, NULL, tskIDLE_PRIORITY + 1, NULL) == pdPASS);
    configASSERT(xTaskCreate(TempTask, "TempTask", 256, NULL, tskIDLE_PRIORITY + 1, NULL) == pdPASS);
    configASSERT(xTaskCreate(ModemTask, "ModemTask", 512, NULL, tskIDLE_PRIORITY + 1, NULL) == pdPASS);

    vTaskStartScheduler();

    while (1) {
        /* unreachable: vTaskStartScheduler() only returns on failure */
    }
}
