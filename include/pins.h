#pragma once
// Pinagem fixa do armário. Toda referência a GPIO no firmware passa por aqui.
//
// Quase tudo fica do lado do 3V3 do DevKit de 30 pinos: numa protoboard só, o
// lado do VIN fica sem furo livre. Lá ficam apenas servo e buzzer, que já
// precisam do 5 V do VIN. Validada em bancada com o firmware de diagnóstico.

// PN532 em SPI (VSPI)
#define PIN_NFC_SCK 18
#define PIN_NFC_MISO 19
#define PIN_NFC_MOSI 23
#define PIN_NFC_SS 5  // Strapping com pull-up no boot: o PN532 fica desmarcado.

// OLED SSD1306 em I2C
#define PIN_OLED_SDA 21
#define PIN_OLED_SCL 22
#define OLED_I2C_ADDR 0x3C

// Servo da tranca (LEDC a 50 Hz)
#define PIN_SERVO 13

// Microswitches: C ao GND, NA ao GPIO. Pressionado (notebook presente) = LOW.
// GPIO 16/17 ficam livres porque o módulo WROOM-32 não tem PSRAM.
#define PIN_SLOT_1 16
#define PIN_SLOT_2 17

// GPIO 2 tem o LED azul da placa em paralelo: ele acende junto com o verde.
// GPIO 15 é strapping: o vermelho pode piscar fraco no reset.
#define PIN_LED_GREEN 2
#define PIN_LED_RED 15

// Base do BC547 via 1 kΩ. Nível ALTO = toca.
#define PIN_BUZZER 27

// Segundo atuador (pendência de hardware — ver README).
#define PIN_AUX_ACTUATOR 4
