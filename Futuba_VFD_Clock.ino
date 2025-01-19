#include <SoftwareSerial.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

// Define RX and TX pins for SoftwareSerial
#define VFD_RX_PIN 4  // Not used but needs to be defined
#define VFD_TX_PIN 5  // ESP8266 GPIO5 (D1)

// Create a SoftwareSerial object for communication with the VFD display
SoftwareSerial VFD(VFD_RX_PIN, VFD_TX_PIN);

// WiFi credentials
const char* ssid = "Your WiFi SSID";
const char* password = "Your WiFi key";

// OpenWeatherMap API settings
const char* apiKey = "Your API key";
const char* location = "Your location"; //London,GB for example

// NTP server settings
const char* ntpServer = "pool.ntp.org";
const int timeZoneOffset = 0; // Adjust for your timezone in seconds
const int daylightOffset = 0; // Adjust for daylight savings if applicable

WiFiClientSecure client;
unsigned long lastWeatherUpdate = 0;
const int weatherUpdateInterval = 600000; //10 min update
float currentTemperature = 0.0;
float currentHumidity = 0.0;
float currentPressure = 0.0;
float windSpeed = 0.0;
int cloudCoverage = 0;
float tempMin = 0.0;
float tempMax = 0.0;
char forecastDescription[81] = ""; 
unsigned long sunrise = 0;
unsigned long sunset = 0;

unsigned long lastScrollUpdate = 0;
const int scrollInterval = 300;
int scrollPosition = 0;

char fixedLine[21] = ""; 
char scrollingContent[161] = ""; 

time_t currentTime; 

void setup() {
    Serial.begin(115200);
    VFD.begin(9600);
    VFD.write(0x1F);
    delay(10);
    VFD.write(0x0D);
    delay(10);

    VFD.print("Connecting WiFi...");
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected!");

    VFD.write(0x0D);
    delay(10);
    VFD.print("Initializing NTP...");
    configTime(timeZoneOffset, daylightOffset, ntpServer);
    delay(2000);

    currentTime = time(nullptr);
    VFD.write(0x0D);
    delay(10);
    VFD.print("Ready");
    Serial.println("NTP initialized");
}

void loop() {
    static unsigned long lastSecondUpdate = 0;
    if (millis() - lastSecondUpdate >= 1000) {
        lastSecondUpdate = millis();
        updateClock();
    }

    if (millis() - lastWeatherUpdate >= weatherUpdateInterval) {
        lastWeatherUpdate = millis();
        updateWeatherAndForecast();
    }

    if (millis() - lastScrollUpdate >= scrollInterval) {
        lastScrollUpdate = millis();
        scrollScrollingLine();
    }
}

void updateClock() {
    currentTime++;
    struct tm* timeInfo = localtime(&currentTime);

    if (timeInfo) {
        snprintf(fixedLine, sizeof(fixedLine), "%02d:%02d:%02d %04d-%02d-%02d",
                timeInfo->tm_hour, timeInfo->tm_min, timeInfo->tm_sec,
                timeInfo->tm_year + 1900, timeInfo->tm_mon + 1, timeInfo->tm_mday);

        setCursor(0);
        VFD.print(fixedLine);
        Serial.println("Updated clock.");
    } else {
        Serial.println("Failed to update clock.");
    }
}

void updateWeatherAndForecast() {
    char weatherPath[100];
    snprintf(weatherPath, sizeof(weatherPath), "/data/2.5/weather?q=%s&appid=%s", location, apiKey);

    Serial.println("Connecting to weather server...");
    client.setInsecure();

    if (!client.connect("api.openweathermap.org", 443)) {
        Serial.println("Failed to connect to weather server.");
        return;
    }
    Serial.println("Connected to weather server.");

    client.print(String("GET ") + weatherPath + " HTTP/1.1\r\n" +
                 "Host: api.openweathermap.org\r\n" +
                 "Connection: close\r\n\r\n");

    String response = "";
    while (client.connected() || client.available()) {
        response += client.readStringUntil('\n');
    }
    client.stop();

    Serial.println("Raw Weather API Response:");
    Serial.println(response);

    int jsonStart = response.indexOf('{');
    if (jsonStart == -1) {
        Serial.println("JSON not found in the response.");
        return;
    }
    String jsonResponse = response.substring(jsonStart);

    Serial.println("Extracted JSON Response:");
    Serial.println(jsonResponse);

    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, jsonResponse);
    if (error) {
        Serial.print("JSON Deserialization failed: ");
        Serial.println(error.c_str());
        return;
    }

    // Parse temperature and humidity
    currentTemperature = doc["main"]["temp"].as<float>() - 273.15;
    currentHumidity = doc["main"]["humidity"].as<float>();
    currentPressure = doc["main"]["pressure"].as<float>();
    windSpeed = doc["wind"]["speed"].as<float>();
    cloudCoverage = doc["clouds"]["all"].as<int>();
    tempMin = doc["main"]["temp_min"].as<float>() - 273.15;
    tempMax = doc["main"]["temp_max"].as<float>() - 273.15;

    // Parse weather description
    const char* description = doc["weather"][0]["description"];
    strncpy(forecastDescription, description, sizeof(forecastDescription) - 1);
    forecastDescription[sizeof(forecastDescription) - 1] = '\0';

    sunrise = doc["sys"]["sunrise"].as<unsigned long>();
    sunset = doc["sys"]["sunset"].as<unsigned long>();

    snprintf(scrollingContent, sizeof(scrollingContent),
             "Temp: %.1fC Min: %.1fC Max: %.1fC Hum: %d%% Pres: %.1fhPa Wind: %.1fm/s Cloud: %d%% Desc: %s Sunrise: %s Sunset: %s ",
             currentTemperature, tempMin, tempMax, (int)currentHumidity, currentPressure, windSpeed, cloudCoverage,
             forecastDescription, formatUnixTime(sunrise).c_str(), formatUnixTime(sunset).c_str());

    Serial.println("Weather information updated successfully.");
}

void scrollScrollingLine() {
    int length = strlen(scrollingContent);
    if (length > 20) {
        char displayLine[21];
        int start = scrollPosition % length;

        for (int i = 0; i < 20; i++) {
            displayLine[i] = scrollingContent[(start + i) % length];
        }
        displayLine[20] = '\0';

        setCursor(20);
        VFD.print(displayLine);

        scrollPosition++;
    } else {
        setCursor(20);
        VFD.print(scrollingContent);
    }
}

String formatUnixTime(unsigned long unixTime) {
    time_t time = unixTime;
    struct tm* timeInfo = localtime(&time);
    char buffer[9];
    snprintf(buffer, sizeof(buffer), "%02d:%02d", timeInfo->tm_hour, timeInfo->tm_min);
    return String(buffer);
}

void setCursor(byte position) {
    VFD.write(0x10);
    VFD.write(position);
}
