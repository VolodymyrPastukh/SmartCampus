#include "secrets.h"
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "WiFi.h"
#include <NTPClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>

//aws topics (publishing and subscribe)
#define AWS_IOT_PUBLISH_TOPIC   "esp32/pub"
#define AWS_IOT_SUBSCRIBE_TOPIC "esp32/sub"


//main pins-------------------------------
const int flamePin = 25;
const int temperaturePin = 4; 
const int lightPin = 34;

//buzzer and rgb--------------------------
const int buzz = 2;
const int red = 32;
const int green = 33;
const int blue = 17;

/*----------------NTP Client----------------------*/
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
const long  gmtOffset_sec = 7200;

//timer args------------------------------
int prescaler = 80;
hw_timer_t * timer = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

//onewire-----------------------------------
OneWire oneWire(temperaturePin);
DallasTemperature sensors(&oneWire);

//Mqtt Client--------------------------------
WiFiClientSecure net = WiFiClientSecure();
PubSubClient client(net);

//common args----------------------------
int timeStep = 10;
volatile bool statePacks = false;
volatile bool stateFlame = false;
int flameOneMessageCounter = 1;
String formattedTime;


/*
 * Enum of RGB states
 */
enum RGB {
  NONE,
  RED,
  GREEN,
  BLUE,
  REDGREEN,
  REDBLUE,
  GREENBLUE,
  ALL
};




/*
 * Function of timer interrupt which allows publishing of temperature and light
 */
void IRAM_ATTR tempAndLight() {
  portENTER_CRITICAL_ISR(&timerMux);
  statePacks = true;
  portEXIT_CRITICAL_ISR(&timerMux);
}

/*
 * Function of embedded interrupt which allows publishing detected flame
 */
void IRAM_ATTR detectFlame(){
  portENTER_CRITICAL_ISR(&timerMux);
  stateFlame = true;
  portEXIT_CRITICAL_ISR(&timerMux);
}

/*
 * Connection to AWS---------------------------------------------------------------------
 */
void connectAWS()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.println("Connecting to Wi-Fi");

  while (WiFi.status() != WL_CONNECTED){
    delay(500);
    Serial.print(".");
  }
  Serial.print("Connected to WiFi network with IP Address: ");
  Serial.println(WiFi.localIP());

  // Configure WiFiClientSecure credentials
  net.setCACert(AWS_CERT_CA);
  net.setCertificate(AWS_CERT_CRT);
  net.setPrivateKey(AWS_CERT_PRIVATE);

  // Connect to the MQTT broker
  client.setServer(AWS_IOT_ENDPOINT, 8883);

  // Create a message handler
  client.setCallback(messageHandler);

  Serial.print("Connecting to AWS IOT");

  while (!client.connect(THINGNAME)) {
    Serial.print(".");
    delay(100);
  }

  if(!client.connected()){
    Serial.println("AWS IoT Timeout!");
    return;
  }

  // Subscribe to a topic
  client.subscribe(AWS_IOT_SUBSCRIBE_TOPIC);

  Serial.println("AWS IoT Connected!");
}

/*
 * Function gets information outside.
 * @params topic - topic on AWS_IoT core service
 * @params payload - data in byte format
 * @params length - lenght of data
*/
void messageHandler(char* topic, byte* payload, unsigned int length) {
  Serial.print("Data from outside: ");

  StaticJsonDocument<200> doc;
  deserializeJson(doc, payload);
  const char* message = doc["message"];
  const int rgb = doc["rgb"];
  const int buzzer = doc["buzzer"];
  RGB val = static_cast<RGB>(rgb);
  setRGB(val); //set color of rgb-led
  setBuzzer(buzzer); //set state of buzzer
}

/*
 * Function change state of buzzer.
 * @params state - accepts 0 (off) or 1 (on) state
*/
void setBuzzer(int state){
  switch(state){
    case 0:
      digitalWrite(buzz, LOW);
      break;
    case 1:
      digitalWrite(buzz, HIGH);
      break;
  }
}

/*
 * Function sets up color to RGB-led.
 * @params state - can accept 8 different states
*/
void setRGB(RGB state){
  switch(state){
    case NONE:
      digitalWrite(red, LOW);
      digitalWrite(green, LOW);
      digitalWrite(blue, LOW);
      break;
    case RED:
      digitalWrite(red, HIGH);
      digitalWrite(green, LOW);
      digitalWrite(blue, LOW);
      break;
    case GREEN:
      digitalWrite(red, LOW);
      digitalWrite(green, HIGH);
      digitalWrite(blue, LOW);
      break;
    case BLUE:
      digitalWrite(red, LOW);
      digitalWrite(green, LOW);
      digitalWrite(blue, HIGH);
      break;
    case REDGREEN:
      digitalWrite(red, HIGH);
      digitalWrite(green, HIGH);
      digitalWrite(blue, LOW);
      break;
    case REDBLUE:
      digitalWrite(red, HIGH);
      digitalWrite(green, LOW);
      digitalWrite(blue, HIGH);
      break;
    case GREENBLUE:
      digitalWrite(red, LOW);
      digitalWrite(green, HIGH);
      digitalWrite(blue, HIGH);
      break;
    case ALL:
      digitalWrite(red, HIGH);
      digitalWrite(green, HIGH);
      digitalWrite(blue, HIGH);
      break;
  }
}

/*
 * Function sends message (temperature and light) to AWS_IoT COre Broker.
 * Data has to be in JSON format.
*/
void publishTemperatureAndLight(){
  timeClient.forceUpdate();
  formattedTime = timeClient.getFormattedTime();
  
  sensors.requestTemperatures(); 
  float temperature = sensors.getTempCByIndex(0);
  int light = analogRead(lightPin);

  StaticJsonDocument<200> doc;
  doc["time"] = formattedTime;
  doc["Temperature"] = temperature;
  doc["Light"] = light;
  char jsonBuffer[512];
  serializeJson(doc, jsonBuffer); // print to client

  client.publish(AWS_IOT_PUBLISH_TOPIC, jsonBuffer);
}

/*
 * Function sends message (flame wos detected) to AWS_IoT COre Broker.
 * Data has to be in JSON format.
*/
void publishFlame(){
  if( !((flameOneMessageCounter % 1) == 0) || !((flameOneMessageCounter % 20) == 0)){
    flameOneMessageCounter++;
    return;
  }
  
  timeClient.forceUpdate();
  formattedTime = timeClient.getFormattedTime();
  
  StaticJsonDocument<200> doc;
  doc["time"] = formattedTime;
  doc["Flame"] = "Flame was detected";
  char jsonBuffer[512];
  serializeJson(doc, jsonBuffer);
  client.publish(AWS_IOT_PUBLISH_TOPIC, jsonBuffer);
}


/*
 * Setup------------------------------------------------------------------------------
 */
void setup() {
  Serial.begin(115200);
  pinMode (buzz, OUTPUT);
  pinMode(red, OUTPUT);
  pinMode(green, OUTPUT);
  pinMode(blue, OUTPUT);
  
  pinMode (flamePin, INPUT_PULLUP); //flame detector
  attachInterrupt(flamePin, detectFlame, FALLING); //interrupt
 
  timeClient.setTimeOffset(gmtOffset_sec);

  sensors.begin();

  timer = timerBegin(0, prescaler, true);
  timerAttachInterrupt(timer, &tempAndLight, true);
  timerAlarmWrite(timer, timeStep * 1000000, true);
  timerAlarmEnable(timer);
  
  connectAWS();
}

/*
 * Loop------------------------------------------------------------------------------
 */
void loop() {
  if(WiFi.status()== WL_CONNECTED){
    //PACKS POST 
    if(statePacks){
      Serial.println("TEMPERATURE_______");
      publishTemperatureAndLight();
      
      portENTER_CRITICAL(&timerMux);
      statePacks = false;
      portEXIT_CRITICAL(&timerMux);
    }

    //FLAME POST 
    if(stateFlame){
      Serial.println("FLAME_______");
      publishFlame();
      
      portENTER_CRITICAL(&timerMux);
      stateFlame = false;
      portEXIT_CRITICAL(&timerMux);
    }
    
  }else {
    Serial.println("WiFi Disconnected");
  }
  client.loop();
}
