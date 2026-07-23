#include <Wire.h>
#include <DHT.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <esp_task_wdt.h>

// ---------------- Pin Definitions ----------------
#define PIN_ECG          34
#define PIN_BP           35
#define PIN_DHT          15
#define PIN_BUZZER       13
#define PIN_LED_ALERT    12
#define DHTTYPE          DHT22

// ---------------- OLED ----------------
#define SCREEN_WIDTH     128
#define SCREEN_HEIGHT    64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ---------------- DHT ----------------
DHT dht(PIN_DHT, DHTTYPE);

// ---------------- FreeRTOS Handles ----------------
QueueHandle_t xSensorQueue;
SemaphoreHandle_t xAlertSemaphore;
SemaphoreHandle_t xI2CMutex;

// ---------------- Sensor Structure ----------------
struct SensorPayload {
  float ecgValue;
  float bloodPressure;
  float temperature;
  float humidity;
};

// ---------------- Function Prototypes ----------------
void vTaskReadVitals(void *pvParameters);
void vTaskReadEnv(void *pvParameters);
void vTaskDisplay(void *pvParameters);
void vTaskMQTTSim(void *pvParameters);
void vTaskAlertHandler(void *pvParameters);

// ====================================================
// SETUP
// ====================================================

void setup() {

  Serial.begin(115200);

  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_ALERT, OUTPUT);

  dht.begin();

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED allocation failed");
    while (true);
  }

  display.clearDisplay();
  display.display();

  // Create Queue
  xSensorQueue = xQueueCreate(5, sizeof(SensorPayload));

  // Create Semaphore
  xAlertSemaphore = xSemaphoreCreateBinary();

  // Create Mutex
  xI2CMutex = xSemaphoreCreateMutex();

  // ---------------- Watchdog ----------------

  esp_task_wdt_config_t wdt_config = {
      .timeout_ms = 5000,
      .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
      .trigger_panic = true
  };

  esp_task_wdt_init(&wdt_config);
  esp_task_wdt_add(NULL);

  // ---------------- Create Tasks ----------------

  xTaskCreatePinnedToCore(
      vTaskAlertHandler,
      "Alert Task",
      4096,
      NULL,
      4,
      NULL,
      1);

  xTaskCreatePinnedToCore(
      vTaskReadVitals,
      "Read Vitals",
      4096,
      NULL,
      3,
      NULL,
      1);

  xTaskCreatePinnedToCore(
      vTaskReadEnv,
      "Read Env",
      4096,
      NULL,
      2,
      NULL,
      1);

  xTaskCreatePinnedToCore(
      vTaskDisplay,
      "Display",
      4096,
      NULL,
      2,
      NULL,
      0);

  xTaskCreatePinnedToCore(
      vTaskMQTTSim,
      "MQTT",
      4096,
      NULL,
      1,
      NULL,
      0);
}

// ====================================================
// LOOP
// ====================================================

void loop() {

  esp_task_wdt_reset();

  vTaskDelay(pdMS_TO_TICKS(1000));
}

// ====================================================
// TASK 1 : READ HEALTH VITALS
// ====================================================

void vTaskReadVitals(void *pvParameters) {

  esp_task_wdt_add(NULL);

  SensorPayload currentData = {0};

  for (;;) {

    esp_task_wdt_reset();

    int rawECG = analogRead(PIN_ECG);
    int rawBP = analogRead(PIN_BP);

    currentData.ecgValue = (rawECG / 4095.0) * 3.3;

    currentData.bloodPressure =
        map(rawBP, 0, 4095, 60, 180);

    // Alert if BP abnormal
    if (currentData.bloodPressure > 140 ||
        currentData.bloodPressure < 80) {

      xSemaphoreGive(xAlertSemaphore);
    }

    xQueueOverwrite(
        xSensorQueue,
        &currentData);

    vTaskDelay(pdMS_TO_TICKS(200));
  }
}
// ====================================================
// TASK 2 : READ ENVIRONMENT
// ====================================================

void vTaskReadEnv(void *pvParameters) {

  esp_task_wdt_add(NULL);

  SensorPayload envData;

  for (;;) {

    esp_task_wdt_reset();

    float h = dht.readHumidity();
    float t = dht.readTemperature();

    if (!isnan(h) && !isnan(t)) {

      if (xQueuePeek(xSensorQueue, &envData, pdMS_TO_TICKS(50)) == pdTRUE) {

        envData.temperature = t;
        envData.humidity = h;

        xQueueOverwrite(xSensorQueue, &envData);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

// ====================================================
// TASK 3 : OLED DISPLAY
// ====================================================

void vTaskDisplay(void *pvParameters) {

  esp_task_wdt_add(NULL);

  SensorPayload displayData;

  for (;;) {

    esp_task_wdt_reset();

    if (xQueuePeek(xSensorQueue, &displayData, pdMS_TO_TICKS(100)) == pdTRUE) {

      if (xSemaphoreTake(xI2CMutex, pdMS_TO_TICKS(50)) == pdTRUE) {

        display.clearDisplay();

        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);

        display.setCursor(0,0);
        display.println("SMART HEALTH");

        display.print("ECG : ");
        display.println(displayData.ecgValue);

        display.print("BP  : ");
        display.println(displayData.bloodPressure);

        display.print("Temp: ");
        display.print(displayData.temperature);
        display.println(" C");

        display.print("Hum : ");
        display.print(displayData.humidity);
        display.println(" %");

        display.display();

        xSemaphoreGive(xI2CMutex);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

// ====================================================
// TASK 4 : MQTT SIMULATION
// ====================================================

void vTaskMQTTSim(void *pvParameters) {

  esp_task_wdt_add(NULL);

  SensorPayload payload;

  for (;;) {

    esp_task_wdt_reset();

    if (xQueuePeek(xSensorQueue, &payload, pdMS_TO_TICKS(100)) == pdTRUE) {

      Serial.println("--------------------------------");
      Serial.println("MQTT Publish");

      Serial.print("ECG : ");
      Serial.println(payload.ecgValue);

      Serial.print("Blood Pressure : ");
      Serial.println(payload.bloodPressure);

      Serial.print("Temperature : ");
      Serial.println(payload.temperature);

      Serial.print("Humidity : ");
      Serial.println(payload.humidity);

      Serial.println("--------------------------------");
    }

    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

// ====================================================
// TASK 5 : ALERT HANDLER
// ====================================================

void vTaskAlertHandler(void *pvParameters) {

  esp_task_wdt_add(NULL);

  for (;;) {

    esp_task_wdt_reset();

    if (xSemaphoreTake(xAlertSemaphore, portMAX_DELAY) == pdTRUE) {

      Serial.println("CRITICAL ALERT");

      for (int i = 0; i < 5; i++) {

        digitalWrite(PIN_LED_ALERT, HIGH);

        tone(PIN_BUZZER, 1000);

        vTaskDelay(pdMS_TO_TICKS(100));

        digitalWrite(PIN_LED_ALERT, LOW);

        noTone(PIN_BUZZER);

        vTaskDelay(pdMS_TO_TICKS(100));
      }
    }
  }
}
