#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "env.h"

// Pins
#define temp_pin  4
#define motion_sensor_pin  15
#define Neopixel_light_pin   23
#define fan_pin     22

#define num_leds    8
String server_url = "";

// DS18B20 confiug
OneWire oneWire(temp_pin);
DallasTemperature tempSensor(&oneWire);

// NeoPixel
Adafruit_NeoPixel lightStrip(num_leds, Neopixel_light_pin, NEO_GRB + NEO_KHZ800);
 
int post_code = 0;
int get_Code = 0;

//motion timer for debouncing
unsigned long motion_time = 0;
bool presence_state = false;


//millis clock
unsigned long lastRequest = 0;
const unsigned long interval = 2000; // 2 seconds
int counter = 0;

void init_temp_sensor() {
  tempSensor.begin();
}

void init_MotionSensor() {
  pinMode(motion_sensor_pin, INPUT);
}

void init_Fan() {
  pinMode(fan_pin, OUTPUT);
  digitalWrite(fan_pin, LOW);
}

void init_Light() {
  lightStrip.begin();
  lightStrip.clear();
  lightStrip.show();
}

float get_temp() {
  tempSensor.requestTemperatures();

  float temperature = tempSensor.getTempCByIndex(0);

  if (temperature == DEVICE_DISCONNECTED_C) { //-127 for disconnected sensor
    Serial.println("DS18B20 disconnected");
    return 0.0;
  }

  return temperature;
}

bool get_motion() {
  return digitalRead(motion_sensor_pin) == HIGH;
}

// //15s debounce for motion sensor to prevent the sensor timeout issue from affect light logics
// bool get_motion() {
//   bool raw_motion = digitalRead(motion_sensor_pin) == HIGH;

//   if (raw_motion && presence_state == false) {
//     presence_state = true;
//     motion_time = millis();
//   }

//   // If presence is true, wait for 15s before checking again
//   if (presence_state == true) {
//     if (millis() - motion_time >= 15000) {

//       // After 15s check sensor again
//       if (raw_motion) {

//         //  motion keep presence true and restart timer
//         presence_state = true;
//         motion_time = millis();
//       } else {
//         // No motion after 15s set presence false
//         presence_state = false;
//         motion_time = 0;
//       }
//     }
//   }

//   return presence_state;
// }

void set_fan(bool fan) {
  digitalWrite(fan_pin, fan);
}

void set_light(bool light) {
  if (light) {
    for (int i = 0; i < num_leds; i++) {
      lightStrip.setPixelColor(i, lightStrip.Color(255, 255, 255)); //set all LEDs to white
    }
  } else {
    lightStrip.clear();
  }

  lightStrip.show(); //update leds
}

void connectWiFi() {
  WiFi.begin(ssid, password);

  Serial.print("Connecting to WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi Connected!");
  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.localIP()); //diagnostic 
}

void sendData(float temp, bool presence){


  // if(WiFi.status() != WL_CONNECTED) {
  //   Serial.println("No WIFI- attempting to connect...");
  //   connectWiFi(); //stuck here until wifi connects
  // }

    //Json body
    JsonDocument doc;

    doc["temperature"] = temp;
    doc["presence"] = presence;

     String body;
    serializeJson(doc, body);


    //post request
    HTTPClient http;
      //http.begin(server_url);
     String url = String(Server_URL) + "/data";
      http.begin(url);
      http.addHeader("Content-Type", "application/json");

      post_code = http.POST(body); //make post request and get status code

      Serial.print("POST status code: ");
      Serial.println(post_code);

    if (post_code > 0) {
    String response = http.getString();
    Serial.print("POST response: ");
    Serial.println(response);
  } else {
    Serial.println("Post to endpoint failed :(");
  }
  http.end();

}


void get_State() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Cannot GET /state :( WiFi lost.");//reconnection in get state func
    return;
  }

  HTTPClient http;

  String url = String(Server_URL) + "/state";
  http.begin(url);

  get_Code = http.GET();

  Serial.print("GET status code: ");
  Serial.println(get_Code);

  if (get_Code == 200) {
    String payload = http.getString();

    Serial.print("GET response: ");
    Serial.println(payload);

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      bool fan_state = doc["fan"];
      bool light_state = doc["light"];

      set_fan(fan_state);
      set_light(light_state);

      Serial.print("fan: ");
      Serial.println(fan_state ? "true" : "false"); //intinary print for bool

      Serial.print("light: ");
      Serial.println(light_state ? "true" : "false");
    } else {
      Serial.println("JSON deserialization error in Get state response");
    }

  } else {
    Serial.println("GET state failed :(");
  }

  http.end();
}


void setup() {
  Serial.begin(115200);
  init_temp_sensor();
  init_MotionSensor();
  init_Fan();
  init_Light();
  connectWiFi();
}

void loop() {
  if (millis() - lastRequest >= interval) {
    lastRequest = millis();
    counter++;
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi Lost! Reconnecting...");
      connectWiFi();
      return;
    }

    float temperature = get_temp();
    bool presence = get_motion();

    Serial.print("temperature: ");
    Serial.println(temperature);

    Serial.print("presence: ");
    Serial.println(presence ? "true" : "false");

    sendData(temperature, presence);

    get_State();

    Serial.print("Request #: ");
    Serial.print(counter);
    Serial.print(" | ");
    Serial.print("Time (s):  ");
    Serial.println(millis() / 1000);
    Serial.println("--------------------------------------------------------------");
  }
}