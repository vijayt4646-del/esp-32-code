#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

// =====================================
// PIN CONFIGURATION
// =====================================

#define PMS_RX 16
#define PMS_TX 17

#define DHT_PIN 4
#define DHT_TYPE DHT11

#define RED_LED 25
#define GREEN_LED 26

// =====================================
// OLED CONFIGURATION
// =====================================

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  -1
);

bool oledReady = false;

// =====================================
// SENSOR OBJECTS
// =====================================

DHT dht(DHT_PIN, DHT_TYPE);
HardwareSerial pmsSerial(2);
WebServer server(80);

// =====================================
// WIFI ACCESS POINT
// =====================================

const char* apName = "AirQualityMonitor";
const char* apPassword = "air12345";

// =====================================
// AIR QUALITY THRESHOLDS
// =====================================

const float PM25_LIMIT = 60.0;
const float PM10_LIMIT = 100.0;

// =====================================
// SENSOR VARIABLES
// =====================================

float pm25 = 0;
float pm10 = 0;

float temperature = 0;
float humidity = 0;

bool havePMS = false;

// =====================================
// PMS5003 BUFFER
// =====================================

uint8_t pmsBuffer[32];
int pmsIndex = 0;

// =====================================
// GRAPH HISTORY
// =====================================

const int MAX_POINTS = 120;

float historyPM25[MAX_POINTS];
float historyPM10[MAX_POINTS];
float historyTemp[MAX_POINTS];
float historyHumidity[MAX_POINTS];

// Time since ESP32 started, in seconds
unsigned long historyTime[MAX_POINTS];

int historyCount = 0;
int historyWriteIndex = 0;

// =====================================
// TIMERS
// =====================================

unsigned long lastDHT = 0;
unsigned long lastSample = 0;
unsigned long lastOLED = 0;

// =====================================
// READ PMS5003 SENSOR
// =====================================

void readPMS() {

  while (pmsSerial.available()) {

    uint8_t b = pmsSerial.read();

    if (pmsIndex == 0) {

      if (b == 0x42) {
        pmsBuffer[pmsIndex++] = b;
      }

    }

    else if (pmsIndex == 1) {

      if (b == 0x4D) {
        pmsBuffer[pmsIndex++] = b;
      }
      else {
        pmsIndex = 0;
      }

    }

    else {

      pmsBuffer[pmsIndex++] = b;

    }

    if (pmsIndex == 32) {

      uint16_t checksum = 0;

      for (int i = 0; i < 30; i++) {
        checksum += pmsBuffer[i];
      }

      uint16_t received =
        ((uint16_t)pmsBuffer[30] << 8) |
        pmsBuffer[31];

      if (checksum == received) {

        // Atmospheric PM readings

        pm25 =
          (pmsBuffer[12] << 8) |
          pmsBuffer[13];

        pm10 =
          (pmsBuffer[14] << 8) |
          pmsBuffer[15];

        havePMS = true;

      }

      pmsIndex = 0;

    }

  }

}

// =====================================
// READ DHT11 SENSOR
// =====================================

void readDHT() {

  if (millis() - lastDHT >= 2000) {

    lastDHT = millis();

    float t = dht.readTemperature();
    float h = dht.readHumidity();

    if (!isnan(t)) {
      temperature = t;
    }

    if (!isnan(h)) {
      humidity = h;
    }

  }

}

// =====================================
// LED INDICATION
// =====================================

void updateLEDs() {

  if (!havePMS) {

    digitalWrite(RED_LED, LOW);
    digitalWrite(GREEN_LED, LOW);

  }

  else if (
    pm25 > PM25_LIMIT ||
    pm10 > PM10_LIMIT
  ) {

    digitalWrite(RED_LED, HIGH);
    digitalWrite(GREEN_LED, LOW);

  }

  else {

    digitalWrite(RED_LED, LOW);
    digitalWrite(GREEN_LED, HIGH);

  }

}

// =====================================
// OLED DISPLAY
// =====================================

void updateOLED() {

  if (!oledReady) return;

  if (millis() - lastOLED < 1000) return;

  lastOLED = millis();

  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);

  display.println("AIR QUALITY MONITOR");

  display.println("---------------------");

  display.print("PM2.5: ");

  if (havePMS) {
    display.print(pm25);
  }
  else {
    display.print("--");
  }

  display.println(" ug/m3");

  display.print("PM10 : ");

  if (havePMS) {
    display.print(pm10);
  }
  else {
    display.print("--");
  }

  display.println(" ug/m3");

  display.print("Temp : ");
  display.print(temperature);
  display.println(" C");

  display.print("Humid: ");
  display.print(humidity);
  display.println(" %");

  display.println("---------------------");

  if (!havePMS) {

    display.println("Waiting for PMS5003");

  }

  else if (
    pm25 > PM25_LIMIT ||
    pm10 > PM10_LIMIT
  ) {

    display.println("STATUS: HIGH");

  }

  else {

    display.println("STATUS: NORMAL");

  }

  display.display();

}

// =====================================
// SAVE GRAPH DATA
// =====================================

void saveHistory() {

  if (millis() - lastSample < 5000) return;

  lastSample = millis();

  int i = historyWriteIndex;

  historyPM25[i] = havePMS ? pm25 : 0;
  historyPM10[i] = havePMS ? pm10 : 0;

  historyTemp[i] = temperature;
  historyHumidity[i] = humidity;

  // Store elapsed time in seconds
  historyTime[i] = millis() / 1000UL;

  historyWriteIndex =
    (historyWriteIndex + 1) % MAX_POINTS;

  if (historyCount < MAX_POINTS) {
    historyCount++;
  }

}

// =====================================
// WEBPAGE
// =====================================

const char webpage[] PROGMEM = R"rawliteral(

<!DOCTYPE html>
<html>

<head>

<meta name="viewport"
content="width=device-width, initial-scale=1">

<title>Air Quality Monitoring System</title>

<style>

body {
  font-family: Arial;
  background: #eef2f3;
  text-align: center;
  margin: 10px;
}

h2 {
  color: #222;
}

.card {
  background: white;
  padding: 12px;
  margin: 12px auto;
  border-radius: 10px;
  max-width: 850px;
}

canvas {
  width: 100%;
  height: 200px;
}

button {
  background: #0077cc;
  color: white;
  border: none;
  padding: 11px 14px;
  margin: 5px;
  border-radius: 6px;
  cursor: pointer;
  font-size: 14px;
}

button:hover {
  background: #005fa3;
}

</style>

</head>

<body>

<h2>Air Quality Monitoring System</h2>

<!-- DOWNLOAD BUTTONS -->

<div class="card">

  <h3>Download Reports</h3>

  <button onclick="downloadGraph('g1','PM25.png')">
    Download PM2.5 Graph
  </button>

  <button onclick="downloadGraph('g2','PM10.png')">
    Download PM10 Graph
  </button>

  <button onclick="downloadGraph('g3','Temperature.png')">
    Download Temperature Graph
  </button>

  <button onclick="downloadGraph('g4','Humidity.png')">
    Download Humidity Graph
  </button>

  <button onclick="window.location.href='/download'">
    Download All Sensor Data (CSV)
  </button>

</div>

<!-- PM2.5 GRAPH -->

<div class="card">

<h3>PM2.5 vs Time</h3>

<canvas id="g1"
width="800" height="200"></canvas>

</div>

<!-- PM10 GRAPH -->

<div class="card">

<h3>PM10 vs Time</h3>

<canvas id="g2"
width="800" height="200"></canvas>

</div>

<!-- TEMPERATURE GRAPH -->

<div class="card">

<h3>Temperature vs Time</h3>

<canvas id="g3"
width="800" height="200"></canvas>

</div>

<!-- HUMIDITY GRAPH -->

<div class="card">

<h3>Humidity vs Time</h3>

<canvas id="g4"
width="800" height="200"></canvas>

</div>

<script>

// =====================================
// DRAW GRAPH
// =====================================

function drawGraph(id, values) {

  let c = document.getElementById(id);

  let ctx = c.getContext("2d");

  let w = c.width;
  let h = c.height;

  ctx.clearRect(0, 0, w, h);

  ctx.strokeStyle = "#cccccc";

  for (let i = 0; i < 5; i++) {

    let y = 15 + i * 42;

    ctx.beginPath();

    ctx.moveTo(45, y);
    ctx.lineTo(w - 10, y);

    ctx.stroke();

  }

  if (values.length === 0) return;

  let max = Math.max(10, ...values) * 1.2;

  ctx.strokeStyle = "#0077cc";

  ctx.lineWidth = 3;

  ctx.beginPath();

  values.forEach((v, i) => {

    let x = 45 + i * (w - 60) /
      Math.max(1, values.length - 1);

    let y = h - 20 -
      (v / max) * (h - 40);

    if (i === 0) {
      ctx.moveTo(x, y);
    }

    else {
      ctx.lineTo(x, y);
    }

  });

  ctx.stroke();

  ctx.fillStyle = "#222";

  ctx.font = "14px Arial";

  ctx.fillText("0", 10, h - 15);

  ctx.fillText(max.toFixed(1), 5, 20);

}

// =====================================
// DOWNLOAD GRAPH AS PNG
// =====================================

function downloadGraph(id, filename) {

  let canvas = document.getElementById(id);

  let link = document.createElement("a");

  link.download = filename;

  link.href = canvas.toDataURL("image/png");

  link.click();

}

// =====================================
// UPDATE ALL FOUR GRAPHS
// =====================================

async function updateGraphs() {

  try {

    let response = await fetch("/data");

    let d = await response.json();

    drawGraph("g1", d.pm25);

    drawGraph("g2", d.pm10);

    drawGraph("g3", d.temp);

    drawGraph("g4", d.humidity);

  }

  catch(error) {

    console.log(error);

  }

}

updateGraphs();

setInterval(updateGraphs, 5000);

</script>

</body>

</html>

)rawliteral";

// =====================================
// SEND WEBPAGE
// =====================================

void handleRoot() {

  server.send_P(
    200,
    "text/html",
    webpage
  );

}

// =====================================
// SEND GRAPH DATA
// =====================================

void handleData() {

  String json = "{\"pm25\":[";

  int oldest =
    (historyWriteIndex - historyCount + MAX_POINTS)
    % MAX_POINTS;

  for (int i = 0; i < historyCount; i++) {

    int index = (oldest + i) % MAX_POINTS;

    if (i) json += ",";

    json += String(historyPM25[index], 1);

  }

  json += "],\"pm10\":[";

  for (int i = 0; i < historyCount; i++) {

    int index = (oldest + i) % MAX_POINTS;

    if (i) json += ",";

    json += String(historyPM10[index], 1);

  }

  json += "],\"temp\":[";

  for (int i = 0; i < historyCount; i++) {

    int index = (oldest + i) % MAX_POINTS;

    if (i) json += ",";

    json += String(historyTemp[index], 1);

  }

  json += "],\"humidity\":[";

  for (int i = 0; i < historyCount; i++) {

    int index = (oldest + i) % MAX_POINTS;

    if (i) json += ",";

    json += String(historyHumidity[index], 1);

  }

  json += "]}";

  server.send(
    200,
    "application/json",
    json
  );

}

// =====================================
// DOWNLOAD ALL SENSOR DATA AS CSV
// =====================================

void handleCSV() {

  String csv =
    "Time (seconds),PM2.5,PM10,Temperature (C),Humidity (%)\n";

  // Find the oldest stored sample

  int oldest =
    (historyWriteIndex - historyCount + MAX_POINTS)
    % MAX_POINTS;

  for (int i = 0; i < historyCount; i++) {

    int index = (oldest + i) % MAX_POINTS;

    csv += String(historyTime[index]);

    csv += ",";

    csv += String(historyPM25[index], 1);

    csv += ",";

    csv += String(historyPM10[index], 1);

    csv += ",";

    csv += String(historyTemp[index], 1);

    csv += ",";

    csv += String(historyHumidity[index], 1);

    csv += "\n";

  }

  server.sendHeader(
    "Content-Disposition",
    "attachment; filename=Air_Quality_Data.csv"
  );

  server.send(
    200,
    "text/csv",
    csv
  );

}

// =====================================
// SETUP
// =====================================

void setup() {

  Serial.begin(115200);

  // LED PINS

  pinMode(RED_LED, OUTPUT);

  pinMode(GREEN_LED, OUTPUT);

  digitalWrite(RED_LED, LOW);

  digitalWrite(GREEN_LED, LOW);

  // PMS5003

  pmsSerial.begin(
    9600,
    SERIAL_8N1,
    PMS_RX,
    PMS_TX
  );

  // DHT11

  dht.begin();

  // OLED

  Wire.begin(21, 22);

  oledReady = display.begin(
    SSD1306_SWITCHCAPVCC,
    0x3C
  );

  if (oledReady) {

    display.clearDisplay();

    display.setTextSize(1);

    display.setTextColor(SSD1306_WHITE);

    display.setCursor(0, 0);

    display.println("Air Quality Monitor");

    display.println("Starting...");

    display.display();

  }

  else {

    Serial.println("OLED not found!");

  }

  // ESP32 WIFI ACCESS POINT

  WiFi.mode(WIFI_AP);

  WiFi.softAP(
    apName,
    apPassword
  );

  Serial.print("IP Address: ");

  Serial.println(WiFi.softAPIP());

  // WEB SERVER ROUTES

  server.on("/", handleRoot);

  server.on("/data", handleData);

  server.on("/download", handleCSV);

  server.begin();

  Serial.println("System Started");

}

// =====================================
// MAIN LOOP
// =====================================

void loop() {

  readPMS();

  readDHT();

  updateLEDs();

  updateOLED();

  saveHistory();

  server.handleClient();

}
