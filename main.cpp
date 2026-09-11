#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>

#include <SPI.h>
#include "codes.h"

#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

#include <SD.h>
#include "FS.h"

//SCREEN
#define TFT_DC   12
#define TFT_RST  13

#define TFT_CS   -1

Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);

//HARDWARE
const int REVERSE = 27;
const int PAUSE = 25; 
const int NEXT = 32;
const int VOLUME = 34;

int potVal = 0;
int potValDiff = 0;

int StateChangeDetection = HIGH;

const int SdCsPin = 5;

//API
const char* host = "api.spotify.com";
const char* token_host = "accounts.spotify.com";

const int httpsPort = 443;
unsigned long lastTokenTime = 0;
const unsigned long tokenExpireTime = 3500000;

bool isPLaying = true;

WiFiClientSecure client;

String access_token = "";

String mainDevice = "DESKTOP-NLK6KKA";

//SONG DATA
String songRuntime;
String song;
String albumCover;

bool ensureSecureConnection(const char* targetHost) {
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    client.stop();

    client.setTimeout(3000);

    if (!client.connect(targetHost, httpsPort)) {
        Serial.println("HTTPS connection failed");
        return false;
    }

    return true;
}

String getAccessToken() {
    if (!ensureSecureConnection(token_host)) return "";

    String body = "grant_type=refresh_token"
                 "&refresh_token=" + String(refresh_token) +
                 "&client_id=" + String(client_id) +
                 "&client_secret=" + String(client_secret);

    client.println("POST /api/token HTTP/1.1");
    client.println("Host: accounts.spotify.com");
    client.println("Content-Type: application/x-www-form-urlencoded");
    client.print("Content-Length: ");
    client.println(body.length());
    client.println("Connection: close");
    client.println();
    client.print(body);
    

    unsigned long timeout = millis() + 5000;
    while (client.connected() && millis()< timeout){
        String line = client.readStringUntil('\n');
        if (line == "\r") break;
    }

    if (millis() >= timeout){
        client.stop();
        return "";
    }

    String response = client.readString();
    client.stop();

    int startIndex = response.indexOf("\"access_token\":\"") + 16;
    int endIndex = response.indexOf("\"", startIndex);

    if (startIndex > 15 && endIndex > startIndex){
        return response.substring(startIndex, endIndex);
    }

    return "";
}

String getDevice(){
    if (!ensureSecureConnection(host)) return "";

    client.printf("GET /v1/me/player HTTP/1.1\r\n");
    client.println("Host: api.spotify.com");
    client.print("Authorization: Bearer ");
    client.println(access_token);
    client.println("Accept: application/json");
    client.println("Connection: close");
    client.println();

    unsigned long timeout = millis() + 8000;
    while (client.connected() && millis()< timeout){
        String line = client.readStringUntil('\n');
        if (line == "\r") break;
    }

    if (millis() >= timeout){
        client.stop();
        return "";
    }

    StaticJsonDocument<128> filter;
    filter["device"]["name"] = true;

    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, client, DeserializationOption::Filter(filter));
    
    client.stop();

    if (error) {
        Serial.println("Failed to parse JSON for device");
        return "";
    }

    String device = doc["device"]["name"];

    return device;
}

void getSongData(){
    if (!ensureSecureConnection(host)) return;

    client.printf("GET /v1/me/player/currently-playing HTTP/1.1\r\n");
    client.println("Host: api.spotify.com");
    client.print("Authorization: Bearer ");
    client.println(access_token);
    client.println("Accept: application/json");
    client.println("Connection: close");
    client.println();

    unsigned long timeout = millis() + 8000;
    while (client.connected() && millis()< timeout){
        String line = client.readStringUntil('\n');
        if (line == "\r") break;
    }

    if (millis() >= timeout){
        client.stop();
        return;
    }

    // SONG NAME AND ARTIST

    StaticJsonDocument<256> filter;
    filter["item"]["artists"][0]["name"] = true;
    filter["item"]["name"] = true;
    filter["item"]["duration_ms"] = true;
    filter["item"]["album"]["images"][0]["url"] = true;
    filter["item"]["duration_ms"] = true;

    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, client, DeserializationOption::Filter(filter));

    client.stop();

    if (error) {
    Serial.println("Failed to parse JSON");
    }

    String artist = doc["item"]["artists"][0]["name"];
    String title = doc["item"]["name"];

    song = artist + " - " + title;

    //RUNTIME 
    int songRuntimeMs = doc["item"]["duration_ms"];
    int songRuntimeMin = songRuntimeMs / 60000;
    int songRuntimeSec = (songRuntimeMs % 60000) / 1000;
    String formatSongRuntimeSec = String(songRuntimeSec);
    
    if (songRuntimeSec  == 0){
        formatSongRuntimeSec = String(songRuntimeSec) + "0";
    } else if (songRuntimeSec < 10){
        formatSongRuntimeSec = "0" + String(songRuntimeSec);
    }

    songRuntime = String(songRuntimeMin) + ":" + formatSongRuntimeSec;

    //ALBUM COVER
    albumCover = doc["item"]["album"]["images"][0]["url"].as<String>();

    //PLAYBACK STATE
    // isPLaying = doc["is_playing"];
}

void commandSend(const char* endpoint, const char* method = "POST"){
    if (!ensureSecureConnection(host)) return;

    client.printf("%s /%s HTTP/1.1\r\n", method, endpoint);
    client.println("Host: api.spotify.com");
    client.print("Authorization: Bearer ");
    client.println(access_token);
    client.println("Content-Length: 0");
    client.println("Connection: close");
    client.println();
    
    unsigned long timeout = millis() + 1000;

    while (client.connected() && millis() < timeout) {
        String line = client.readStringUntil('\n');
        if (line == "\r" || line == "") break;
    }
}


void setup() {
    Serial.begin(115200);
    delay(100);

    //HARDWARE
    pinMode(REVERSE, INPUT_PULLUP);
    pinMode(PAUSE, INPUT_PULLUP);
    pinMode(NEXT, INPUT_PULLUP);
    pinMode(SdCsPin, OUTPUT);

      if (!SD.begin(SdCsPin)) {
    Serial.println("SD Card Mount Failed");
    }else{
        Serial.println("SD Card Mounted");
    }

    // TFT SCREEN
    tft.begin();
    tft.setTextSize(1);
    tft.setRotation(2);
    tft.fillScreen(ILI9341_BLACK);
    tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(20, 20);
    tft.println("LOADING");

    //WIFI
    Serial.println("");
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(ssid);
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED){
        Serial.print(".");
        delay(500);
    }

    client.setCACert(root_ca);
    
    Serial.print("Connected to ");
    Serial.print(ssid);
    Serial.println("");

    Serial.println(WiFi.localIP());

    //API
    access_token = getAccessToken();
    getSongData();
    Serial.println(song);
    Serial.println(songRuntime);
    Serial.println(albumCover);
    Serial.println(isPLaying);

    tft.fillScreen(ILI9341_BLACK);
    tft.println(song);
}  


void loop(){
    potVal = round(analogRead(VOLUME)/40.95);
    int reverseState = digitalRead(REVERSE);
    int pauseState = digitalRead(PAUSE);
    int nextState = digitalRead(NEXT);

    if (reverseState == LOW && StateChangeDetection == HIGH) {
        Serial.println("REVERSE");
        isPLaying = true;
        commandSend("v1/me/player/previous");
        getSongData();
        StateChangeDetection = LOW;
    } 
    else if (pauseState == LOW && StateChangeDetection == HIGH) {
        Serial.println("PAUSE/PLAY");
        if (isPLaying) {
            isPLaying = false;
            commandSend("v1/me/player/pause", "PUT");
            Serial.print(isPLaying);
        } else {
            isPLaying = true;
            commandSend("v1/me/player/play", "PUT");
            Serial.print(isPLaying);

        }

        StateChangeDetection = LOW;
    } 
    else if (nextState == LOW && StateChangeDetection == HIGH) {
        Serial.println("NEXT");
        isPLaying = true;
        commandSend("v1/me/player/next");
        getSongData();
        StateChangeDetection = LOW;
    }
    else if (reverseState == HIGH && pauseState == HIGH && nextState == HIGH ){
        StateChangeDetection = HIGH;
    }


    if (potVal != potValDiff && potValDiff != 0){
        Serial.println(potVal);
        String command = "v1/me/player/volume?volume_percent=" + String(potVal);
        commandSend(command.c_str(), "PUT");
    }
    
  
    potValDiff = potVal;
    delay(150);
    if (millis() - lastTokenTime >= tokenExpireTime) {
        access_token = getAccessToken();
        lastTokenTime = millis();
    }
}



    //Active functions

    // Next Song
    // commandSend("v1/me/player/next");

    //Pause song
    // commandSend("v1/me/player/pause", "PUT");

    // Prev Song
    // commandSend("v1/me/player/previous");

    //Volume change
    // commandSend("v1/me/player/volume?volume_percent=0", "PUT");

    //Passive functions

    //Get Song data
    // getSongData();

    //Get Device
    // getDevice();


    //     TFT             ESP32
    // ----------------------
    // GND      --->   GND
    // VCC      --->   3.3V
    // CLK      --->   GPIO18
    // MOSI     --->   GPIO23
    // RES      --->   GPIO27
    // DC       --->   GPIO26
    // BLK      --->   3.3V
    // MISO     --->   GPIO19
