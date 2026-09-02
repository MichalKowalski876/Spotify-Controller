#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>

#include "codes.h"

WiFiClientSecure client;
//API
const char* host = "api.spotify.com";
const char* token_host = "accounts.spotify.com";

const int httpsPort = 443;
unsigned long lastTokenTime = 0;
const unsigned long tokenExpireTime = 3500000;

String access_token = "";

String mainDevice = "device_name";

//SONG DATA
String songRuntime;
String song;
String albumCover;

bool ensureSecureConnection(const char* targetHost) {
    if (WiFi.status() != WL_CONNECTED) return false;
    
    if (client.connected()) {
        return true; 
    }
    
    client.setTimeout(5000);
    
    if (!client.connect(targetHost, httpsPort)) {
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

    access_token = getAccessToken();

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
    // getDevice();A
}   

void loop(){

    if (millis() - lastTokenTime >= tokenExpireTime) {
        Serial.println("NEW TOKEN");
        lastTokenTime = millis();
    }
}

