#include <Wire.h>
#include <WiFi.h>
#include <DHT.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---------------- OLED ----------------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(
SCREEN_WIDTH,
SCREEN_HEIGHT,
&Wire,
-1);

// ---------------- DHT ----------------
#define DHTPIN 15
#define DHTTYPE DHT22

DHT dht(DHTPIN, DHTTYPE);

// ---------------- Pins ----------------
#define HEART_PIN      34
#define SPO2_PIN       35
#define OXYGEN_PIN     32
#define AQI_PIN        33

#define LED_PIN        12
#define BUZZER_PIN     13

// ---------------- Structures ----------------

struct MedicalData
{
  int heartRate;
  int spo2;
  float bodyTemp;
};

struct FacilityData
{
  float roomTemp;
  int oxygen;
  int aqi;
};

// ---------------- Global Variables ----------------

MedicalData medData;
FacilityData facData;

// ---------------- FreeRTOS ----------------

SemaphoreHandle_t displayMutex;

// ---------------- Function Prototypes ----------------

void MedicalTask(void *pvParameters);
void FacilityTask(void *pvParameters);
void DisplayTask(void *pvParameters);
void MQTTTask(void *pvParameters);
void AlertTask(void *pvParameters);
void SummaryTask(void *pvParameters);

// ---------------- Setup ----------------

void setup()
{
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  dht.begin();

  Wire.begin(21,22);

  if(!display.begin(
      SSD1306_SWITCHCAPVCC,
      0x3C))
  {
    Serial.println("OLED Failed");

    while(true);
  }

  display.clearDisplay();
  display.display();

  displayMutex = xSemaphoreCreateMutex();

  // -------- Medical Task --------

  xTaskCreatePinnedToCore(
      MedicalTask,
      "Medical",
      4096,
      NULL,
      3,
      NULL,
      1);

  // -------- Facility Task --------

  xTaskCreatePinnedToCore(
      FacilityTask,
      "Facility",
      4096,
      NULL,
      3,
      NULL,
      1);

  // -------- OLED --------

  xTaskCreatePinnedToCore(
      DisplayTask,
      "Display",
      4096,
      NULL,
      2,
      NULL,
      0);

  // -------- MQTT --------

  xTaskCreatePinnedToCore(
      MQTTTask,
      "MQTT",
      4096,
      NULL,
      2,
      NULL,
      0);

  // -------- Alert --------

  xTaskCreatePinnedToCore(
      AlertTask,
      "Alert",
      3072,
      NULL,
      4,
      NULL,
      1);

  // -------- Summary --------

  xTaskCreatePinnedToCore(
      SummaryTask,
      "Summary",
      4096,
      NULL,
      1,
      NULL,
      0);

  Serial.println("System Started...");
}

void loop()
{
  vTaskDelay(pdMS_TO_TICKS(1000));
}
// ======================================================
// MEDICAL TASK
// Reads Heart Rate & SpO2
// ======================================================

void MedicalTask(void *pvParameters)
{
  while (true)
  {
    int heartRaw = analogRead(HEART_PIN);
    int spo2Raw = analogRead(SPO2_PIN);

    medData.heartRate = map(heartRaw, 0, 4095, 60, 120);
    medData.spo2 = map(spo2Raw, 0, 4095, 85, 100);

    // Simulated Body Temperature
    medData.bodyTemp = 36.0 + random(0, 30) / 10.0;

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}


// ======================================================
// FACILITY TASK
// Reads Room Temp, Oxygen & AQI
// ======================================================

void FacilityTask(void *pvParameters)
{
  while (true)
  {
    float temp = dht.readTemperature();

    if (isnan(temp))
    {
      temp = 28.0;
    }

    facData.roomTemp = temp;

    int oxygenRaw = analogRead(OXYGEN_PIN);
    int aqiRaw = analogRead(AQI_PIN);

    facData.oxygen = map(oxygenRaw, 0, 4095, 18, 23);

    facData.aqi = map(aqiRaw, 0, 4095, 20, 300);

    vTaskDelay(pdMS_TO_TICKS(1500));
  }
}
// ======================================================
// DISPLAY TASK
// ======================================================

void DisplayTask(void *pvParameters)
{
  bool medicalScreen = true;

  while (true)
  {
    if (xSemaphoreTake(displayMutex, portMAX_DELAY) == pdTRUE)
    {
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);

      if (medicalScreen)
      {
        display.setCursor(0, 0);
        display.println("MEDICAL DASHBOARD");
        display.println("----------------");

        display.print("Heart Rate : ");
        display.print(medData.heartRate);
        display.println(" BPM");

        display.print("SpO2       : ");
        display.print(medData.spo2);
        display.println("%");

        display.print("Body Temp  : ");
        display.print(medData.bodyTemp);
        display.println(" C");
      }
      else
      {
        display.setCursor(0, 0);
        display.println("FACILITY DASHBOARD");
        display.println("------------------");

        display.print("Room Temp : ");
        display.print(facData.roomTemp);
        display.println(" C");

        display.print("Oxygen    : ");
        display.print(facData.oxygen);
        display.println("%");

        display.print("AQI       : ");
        display.println(facData.aqi);
      }

      display.display();

      xSemaphoreGive(displayMutex);
    }

    medicalScreen = !medicalScreen;

    vTaskDelay(pdMS_TO_TICKS(3000));
  }
}


// ======================================================
// MQTT SIMULATION TASK
// ======================================================

void MQTTTask(void *pvParameters)
{
  while (true)
  {
    Serial.println();
    Serial.println("==============================");
    Serial.println("MQTT PUBLISH");
    Serial.println("==============================");

    Serial.println();
    Serial.println("Topic : medical/patient");

    Serial.print("Heart Rate : ");
    Serial.println(medData.heartRate);

    Serial.print("SpO2 : ");
    Serial.println(medData.spo2);

    Serial.print("Body Temp : ");
    Serial.println(medData.bodyTemp);

    Serial.println();

    Serial.println("Topic : facility/environment");

    Serial.print("Room Temp : ");
    Serial.println(facData.roomTemp);

    Serial.print("Oxygen : ");
    Serial.println(facData.oxygen);

    Serial.print("AQI : ");
    Serial.println(facData.aqi);

    Serial.println("==============================");

    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}
// ======================================================
// ALERT TASK
// ======================================================

void AlertTask(void *pvParameters)
{
  while (true)
  {
    bool alert = false;

    if (medData.spo2 < 92)
      alert = true;

    if (medData.bodyTemp > 38.0)
      alert = true;

    if (facData.aqi > 150)
      alert = true;

    if (alert)
    {
      Serial.println();
      Serial.println("***** CRITICAL ALERT *****");

      digitalWrite(LED_PIN, HIGH);

      // Buzzer ON
      digitalWrite(BUZZER_PIN, HIGH);
      vTaskDelay(pdMS_TO_TICKS(300));

      digitalWrite(BUZZER_PIN, LOW);
      digitalWrite(LED_PIN, LOW);

      vTaskDelay(pdMS_TO_TICKS(300));
    }
    else
    {
      digitalWrite(LED_PIN, LOW);
      digitalWrite(BUZZER_PIN, LOW);

      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }
}


// ======================================================
// SUMMARY TASK
// ======================================================

void SummaryTask(void *pvParameters)
{
  float hrSum = 0;
  float tempSum = 0;
  float aqiSum = 0;

  int samples = 0;

  while (true)
  {
    hrSum += medData.heartRate;
    tempSum += medData.bodyTemp;
    aqiSum += facData.aqi;

    samples++;

    if (samples >= 6)
    {
      Serial.println();
      Serial.println("========== DATA SUMMARY ==========");

      Serial.print("Average Heart Rate : ");
      Serial.print(hrSum / samples);
      Serial.println(" BPM");

      Serial.print("Average Body Temp  : ");
      Serial.print(tempSum / samples);
      Serial.println(" C");

      Serial.print("Average AQI        : ");
      Serial.println(aqiSum / samples);

      Serial.println("==================================");

      hrSum = 0;
      tempSum = 0;
      aqiSum = 0;

      samples = 0;
    }

    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}
