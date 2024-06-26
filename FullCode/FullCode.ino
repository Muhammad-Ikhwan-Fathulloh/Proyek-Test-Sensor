//Check Board ESP
#ifdef ESP8266
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#elif defined(ESP32)
#include <WiFi.h>
#include <ESPmDNS.h>
#else
#error "Board not found"
#endif

// Import MQTT
#include <MQTT.h>

// Import LCD 16x2
#include <LiquidCrystal_I2C.h>//I2C LCD
LiquidCrystal_I2C lcd(0x27,16,2);//SDA dan SDL

//Device Code
String deviceCode = "Device01";
// String deviceCode = "Device02";

//Breath Sensor
#define AMBANG_BATAS_DETEKSI 100 // Nilai ambang batas untuk mendeteksi suara
#define AMBANG_BATAS_TIDAK_DETEKSI 4095 // Nilai ambang batas saat tidak ada suara

//Heart Rate Sensor
#include <Wire.h>
#include "MAX30105.h"

#include "heartRate.h"

MAX30105 particleSensor;

const byte RATE_SIZE = 4; //Increase this for more averaging. 4 is good.
byte rates[RATE_SIZE]; //Array of heart rates
byte rateSpot = 0;
long lastBeat = 0; //Time at which the last beat occurred

float beatsPerMinute;
int beatAvg;

//Temperature Sensor
#include <OneWire.h>
#include <DallasTemperature.h>

// GPIO where the DS18B20 is connected to
const int oneWireBus = 2;     

// Setup a oneWire instance to communicate with any OneWire devices
OneWire oneWire(oneWireBus);

// Pass our oneWire reference to Dallas Temperature sensor 
DallasTemperature sensors(&oneWire);

//Wifi Configuration
const char ssid[] = "ssid";
const char pass[] = "pass";

WiFiClient net;
MQTTClient client;

unsigned long lastMillis = 0;

void connect() {
  Serial.print("checking wifi...");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }

  Serial.print("\nconnecting...");
  while (!client.connect("arduino", "public", "public")) {
    Serial.print(".");
    delay(1000);
  }

  Serial.println("\nconnected!");

  client.subscribe("158928/farm/#");
}

void messageReceived(String &topic, String &payload) {
  Serial.println(topic + ": " + payload);
}

void setup() {
  Serial.begin(115200);

  // Initialize sensor
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) //Use default I2C port, 400kHz speed
  {
    Serial.println("MAX30105 was not found. Please check wiring/power. ");
    while (1);
  }
  Serial.println("Place your index finger on the sensor with steady pressure.");

  particleSensor.setup(); //Configure sensor with default settings
  particleSensor.setPulseAmplitudeRed(0x0A); //Turn Red LED to low to indicate sensor is running
  particleSensor.setPulseAmplitudeGreen(0); //Turn off Green LED

  // Start the DS18B20 sensor
  sensors.begin();

  // start wifi and mqtt
  WiFi.begin(ssid, pass);
  client.begin("public.cloud.shiftr.io", net);
  client.onMessage(messageReceived);

  //LCD Display
  lcd.begin();
  lcd.home();
  //lcd.init();                      
  lcd.backlight();
  lcd.setCursor(0,0);
  lcd.print("Farm System");
  lcd.setCursor(0,1);
  lcd.print("Koneksikan Alat");

  connect();
}

void loop() {
  client.loop();
  delay(10);

  // check if connected
  if (!client.connected()) {
    connect();
  }

  // publish a message roughly every second.
  if (millis() - lastMillis > 1000) {
    lastMillis = millis();
    client.publish("158928/farm/device_code", String(deviceCode));
    breathSensor();
    HeartRateSensor();
    TemperatureSensor();
    client.publish("158928/farm/prediction", String("Test"));
  }
}

void breathSensor(){
  // put your main code here, to run repeatedly:
  static unsigned long lastMinute = 0; // variabel untuk menyimpan waktu terakhir perhitungan
  static unsigned long soundCount = 0; // variabel untuk menyimpan jumlah suara yang terdeteksi
  static int ambangBatas = AMBANG_BATAS_TIDAK_DETEKSI; // Nilai ambang batas sensor default
  
  unsigned long currentMillis = millis(); // mendapatkan waktu saat ini
  
  // Membaca sensor suara
  int soundValue = analogRead(34);
  Serial.println(soundValue);
  // Menyesuaikan nilai ambang batas berdasarkan deteksi suara
  if(soundValue < AMBANG_BATAS_DETEKSI) {
    ambangBatas = AMBANG_BATAS_TIDAK_DETEKSI; // Gunakan nilai ambang batas tinggi saat tidak ada suara terdeteksi
    soundCount++;
  } else {
    ambangBatas = 0; // Ubah nilai ambang batas kembali ke default jika suara terdeteksi
  }
  
  // Menghitung jumlah suara per menit
  if(currentMillis - lastMinute >= 60000) { // 60000 milliseconds = 1 menit
    float soundPerMinute = (float)soundCount / ((float)(currentMillis - lastMinute) / 60000.0);
    Serial.print("Jumlah suara per menit: ");
    Serial.println(soundPerMinute);
    breathRate = soundPerMinute;
    // Reset jumlah suara dan waktu terakhir perhitungan
    soundCount = 0;
    lastMinute = currentMillis;
    delay(5000);
  }

  //LCD Display
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Pernapasan");
  lcd.setCursor(0,1);
  lcd.print(String(breathRate) + "per Menit");

  client.publish("158928/farm/breath_sensor", String(soundCount));
  
  delay(50);
}

void HeartRateSensor() {
  long irValue = particleSensor.getIR();

  if (checkForBeat(irValue)) {
    //We sensed a beat!
    long delta = millis() - lastBeat;
    lastBeat = millis();

    beatsPerMinute = 60 / (delta / 1000.0);

    if (beatsPerMinute < 255 && beatsPerMinute > 20) {
      rates[rateSpot++] = (byte)beatsPerMinute; //Store this reading in the array
      rateSpot %= RATE_SIZE; //Wrap variable

      //Take average of readings
      beatAvg = 0;
      for (byte x = 0; x < RATE_SIZE; x++)
        beatAvg += rates[x];
      beatAvg /= RATE_SIZE;
    }

    Serial.print("IR=");
    Serial.print(irValue);
    Serial.print(", BPM=");
    Serial.print(beatsPerMinute);
    Serial.print(", Avg BPM=");
    Serial.print(beatAvg);

    heartRate = beatsPerMinute;

    //LCD Display
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Detak Jantung");
    lcd.setCursor(0, 1);
    lcd.print(String(beatsPerMinute) + "per Menit");

    client.publish("158928/farm/heart_sensor", String(beatsPerMinute));

    if (irValue < 50000)
      Serial.print(" No finger?");

    Serial.println();
  }
}

void TemperatureSensor(){
  sensors.requestTemperatures(); 
  float temperatureC = sensors.getTempCByIndex(0);
  float temperatureF = sensors.getTempFByIndex(0);
  temperature = temperatureC;

  //LCD Display
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Suhu");
  lcd.setCursor(0,1);
  lcd.print(String(temperature) + "ºC");
  
  client.publish("158928/farm/temperature_sensor", String(temperatureC));
  Serial.print(temperatureC);
  Serial.println("ºC");
  Serial.print(temperatureF);
  Serial.println("ºF");
  delay(5000);
}
