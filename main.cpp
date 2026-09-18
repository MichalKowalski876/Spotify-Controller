#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <SPI.h>
#include "codes.h"
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <TJpg_Decoder.h>
#include <SPIFFS.h>
#include <HTTPClient.h>
#include <tft_eSPI.h>

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


//API
const char* host = "api.spotify.com";
const char* token_host = "accounts.spotify.com";

const int httpsPort = 443;
unsigned long lastTokenTime = 0;
const unsigned long tokenExpireTime = 3500000;

bool isPLaying = true;

WiFiClientSecure client;
HTTPClient http;

String access_token;

//IMAGE DATA
const int srcW = 300;
const int srcH = 300;

const int dstW = 230;
const int dstH = 230;

const int imageX = 5;
const int imageY = 5;

const char *imagePath ="/album.jpg";

//SONG DATA
String songRuntime;
String song;
String albumCover;

//COLROS 
#define GRAY tft.color565(18, 18, 18)
#define GREEN tft.color565(30, 215,96)
#define WHITE tft.color565(248, 248, 248)
#define LIGHT_GRAY tft.color565(53, 53, 53)



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

    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, client, DeserializationOption::Filter(filter));

    client.stop();

    if (error) {
    Serial.println("Failed to parse JSON");
    }

    String artist = doc["item"]["artists"][0]["name"];
    String title = doc["item"]["name"];

    song = artist + " - " + title;

    if (song.length() >=37){
        song = song.substring(0, 37) + "...";
    }


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
    albumCover = doc["item"]["album"]["images"][1]["url"].as<String>();

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

bool jpgCallback(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
    int max_by = (y + h > srcH) ? (srcH - y) : h;
    int max_bx = (x + w > srcW) ? (srcW - x) : w;

    if (max_bx <= 0 || max_by <= 0) return true;

    int16_t newX_map[16]; 
    for (int bx = 0; bx < max_bx; bx++) {
        newX_map[bx] = ((x + bx) * dstW) / srcW;
    }

    for (int by = 0; by < max_by; by++) {
        int newY = ((y + by) * dstH) / srcH;
        if (newY >= dstH) break; 
        
        int screenY = imageY + newY;
        
        uint16_t* srcRow = &bitmap[by * w]; 

        for (int bx = 0; bx < max_bx; bx++) {
            int mappedX = newX_map[bx];
            if (mappedX >= dstW) break; 

            tft.drawPixel(imageX + mappedX, screenY, srcRow[bx]);
        }
    }
    return true;
}

bool downloadImage(){
    http.begin(albumCover);
    int httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK){
        http.end();
        return false;
    }

    int contentLength = http.getSize();
    WiFiClient *stream = http.getStreamPtr();

    if (SPIFFS.exists(imagePath)){
        SPIFFS.remove(imagePath);
    }

    File file = SPIFFS.open(imagePath, FILE_WRITE);

    if (!file){
        http.end();
        return false;
    }

    uint8_t buffer[1024];
    int totalBytes = 0;

    while(http.connected() && (contentLength > 0 || contentLength == -1)){
        size_t available = stream->available();
        if (available > 0){
            size_t bytesToRead = min(available, sizeof(buffer));
            int bytesRead = stream->readBytes(buffer, bytesToRead);
            if (bytesRead > 0){
                file.write(buffer, bytesRead);
                totalBytes += bytesRead;
                if (contentLength > 0){
                    contentLength -= bytesRead;
                }

            }
        }
    }
    delay(1);
    file.close();
    http.end();
    return totalBytes > 0;
}

void pauseButton(){
    tft.fillRect(111, 270, 23, 28, GRAY);

    if (isPLaying){
        tft.fillRect(111, 270, 8, 26, WHITE);
        tft.fillRect(125, 270, 8, 26, WHITE);
    } else {
        tft.fillTriangle (111, 270, 111, 296, 133, 283, WHITE);
    }
}

void nextButton(){
    if (StateChangeDetection == HIGH){
        tft.fillTriangle(150, 296, 150, 270, 172, 283, WHITE);
        tft.fillRect(172, 270, 8, 26, WHITE);
    }else{
        tft.fillTriangle(150, 296, 150, 270, 172, 283, GREEN);
        tft.fillRect(172, 270, 8, 26, GREEN);
    }
}  

void prevButton(){
    if (StateChangeDetection == HIGH){
        tft.fillTriangle(86, 270, 86, 296, 64, 283, WHITE);
        tft.fillRect(57, 270, 8, 26, WHITE);
    }else{
        tft.fillTriangle(86, 270, 86, 296, 64, 283, GREEN);
        tft.fillRect(57, 270, 8, 26, GREEN);
    }
}

void progressBar(){
    tft.drawRect(56, 256, 170, 5, WHITE);
}

void battryBar(){
    tft.drawRect(10, 304, 100, 5, WHITE);
    tft.setCursor(22,311);
    tft.print("BATTERY: ");
    tft.print("100");
    tft.print("%");
}   

void volumeBar(){
    tft.drawRect(130, 304, 100, 5, WHITE);
    tft.setCursor(144, 311);
    tft.fillRect(190, 310, 30, 10, GRAY);
    tft.print("VOLUME: ");
    tft.print(potVal);
    tft.print("%");

    if (potVal > potValDiff) {
    tft.fillRect(131, 305, potVal-2, 3, GREEN);
    } else{
        tft.fillRect(228, 305, potVal-98, 3, GRAY);
    }
}


void SongDisplay(){
    tft.fillRect(0, 0, 240, 255, GRAY);
    tft.fillRect(0, 255, 55, 6, GRAY);

    tft.setCursor(0, 240);
    tft.println(song);
    progressBar();

    tft.setCursor(0, 255);
    tft.print(songRuntime);
    tft.print("/0:00");

    bool download = downloadImage();
    if (!download){
        Serial.println("Image failed to download");
    } else {
        TJpgDec.drawFsJpg(imageX, imageY, imagePath);
    }

    if (SPIFFS.exists(imagePath)){
        SPIFFS.remove(imagePath);
        Serial.println("temporary jpg deleted");
    }
}



void setup() {
    Serial.begin(115200);
    delay(100);

    //HARDWARE
    pinMode(REVERSE, INPUT_PULLUP);
    pinMode(PAUSE, INPUT_PULLUP);
    pinMode(NEXT, INPUT_PULLUP);

    // TFT SCREEN
    tft.begin();
    tft.setTextSize(3);
    tft.setRotation(4);
    tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(60, 140);
    tft.fillScreen(GRAY);
    tft.println("LOADING");
    tft.setTextSize(1);

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
    
    Serial.print("Connected");
    Serial.println("");

    Serial.println(WiFi.localIP());

    tft.setCursor(60, 170);
    tft.print("CONNECTED ");
    tft.print(WiFi.localIP());

    access_token = getAccessToken();
    Serial.println(access_token);
    getSongData();
    tft.setCursor(60, 180);    
    tft.println("Access token recieved");

    //SPIFF
    if(!SPIFFS.begin(true)){
        Serial.println("SPIFFS failed");
    }
    tft.fillScreen(GRAY);

    TJpgDec.setJpgScale(1);
    TJpgDec.setSwapBytes(false);
    TJpgDec.setCallback(jpgCallback);   
    tft.setCursor(70, 190);    
    tft.println("SPIFFS initialized");

    Serial.println(song);
    Serial.println(albumCover);

    battryBar();
    volumeBar();

    pauseButton();
    nextButton();
    prevButton();

    SongDisplay();
}  


void loop(){
    potVal = round(analogRead(VOLUME)/40.95);
    int reverseState = digitalRead(REVERSE);
    int pauseState = digitalRead(PAUSE);
    int nextState = digitalRead(NEXT);

    if (reverseState == LOW && StateChangeDetection == HIGH) {
        StateChangeDetection = LOW;
        Serial.println("REVERSE");
        prevButton();
        isPLaying = true;
        commandSend("v1/me/player/previous");
        getSongData();
        SongDisplay();
    } 
    else if (pauseState == LOW && StateChangeDetection == HIGH) {
        Serial.println("PAUSE/PLAY");
        if (isPLaying) {
            isPLaying = false;
            commandSend("v1/me/player/pause", "PUT");
        } else {
            isPLaying = true;
            commandSend("v1/me/player/play", "PUT");
        }
        pauseButton();
        StateChangeDetection = LOW;
    } 
    else if (nextState == LOW && StateChangeDetection == HIGH) {
        StateChangeDetection = LOW;
        Serial.println("NEXT");
        nextButton();
        isPLaying = true;
        commandSend("v1/me/player/next");
        getSongData();
        SongDisplay();
    }
    else if (reverseState == HIGH && pauseState == HIGH && nextState == HIGH ){
        StateChangeDetection = HIGH;
        nextButton();

    }


    if (potVal != potValDiff && potValDiff != 0 && abs(potValDiff - potVal) > 2){
        Serial.println(potVal);
        String command = "v1/me/player/volume?volume_percent=" + String(potVal);
        commandSend(command.c_str(), "PUT");
        volumeBar();
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
