/*
  MedBox - ESP8266 (NodeMCU) medicine reminder & dispenser (Stage 1 proof of concept)

  Features
  - Joins your 2.4 GHz Wi-Fi and syncs time via NTP
  - Serves a phone-friendly web page to add/delete dose times
  - Saves the schedule in flash (EEPROM emulation), survives power loss
  - At dose time: blinks LED + buzzer; press button to dispense (servo)
  - JSON endpoint (GET /api/schedule) ready for a future native app

  Board package: "esp8266" by ESP8266 Community
  Board setting: NodeMCU 1.0 (ESP-12E Module), Upload Speed 115200
  No extra libraries needed (Servo, EEPROM are built in).

  Wiring (pins on the board's "ESP Pin's" header)
  - Servo signal -> GPIO12   (servo power from external 5V, common GND)
  - LED + 220 ohm -> GPIO2   (LED long leg toward the resistor, other end to GND)
  - Active buzzer -> GPIO16  (3.3V-compatible, or drive via transistor)
  - Button -> GPIO4 to GND   (internal pull-up)

  GPIO2 is a boot pin: if the board stops connecting to Wi-Fi after you
  wire the LED, unplug the LED wire and re-test.
*/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <EEPROM.h>
#include <Servo.h>
#include <time.h>

// ---------- CONFIG ----------
const char* WIFI_SSID = "Airtel_amar_9194";
const char* WIFI_PASS = "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
const char* HOSTNAME  = "medbox";            // http://medbox.local

const long  GMT_OFFSET_SEC = 19800;          // India (UTC+5:30)
const int   DST_OFFSET_SEC = 0;
const char* NTP_SERVER     = "pool.ntp.org";

const int SERVO_PIN  = 12;   // GPIO12 on the "ESP Pin's" header
const int LED_PIN    = 2;    // GPIO2
const int BUZZER_PIN = 16;   // GPIO16
const int BUTTON_PIN = 4;    // GPIO4

const int SERVO_CLOSED = 0;
const int SERVO_OPEN   = 90;
const unsigned long ALERT_TIMEOUT_MS = 45000UL;   // missed-dose timeout
// ----------------------------

const int MAX_DOSES = 8;
const uint32_t MAGIC = 0x4D454442;           // marks valid saved data

struct Dose {
  bool    used;
  uint8_t hour;
  uint8_t minute;
  char    name[24];
};

Dose doses[MAX_DOSES];
int  lastFired[MAX_DOSES];

ESP8266WebServer server(80);
Servo dispenser;

enum State { WAITING, ALERTING, DISPENSING };
State state = WAITING;
unsigned long alertStart = 0;
String currentDoseName = "";
unsigned long lastTimeCheck = 0;

// ---------- Storage ----------
void saveDoses() {
  EEPROM.put(0, MAGIC);
  EEPROM.put(8, doses);
  EEPROM.commit();
}

void loadDoses() {
  memset(doses, 0, sizeof(doses));
  uint32_t m = 0;
  EEPROM.get(0, m);
  if (m == MAGIC) {
    EEPROM.get(8, doses);
  }
  for (int i = 0; i < MAX_DOSES; i++) {
    lastFired[i] = -1;
    doses[i].name[sizeof(doses[i].name) - 1] = 0;   // safety
  }
}

// ---------- Helpers ----------
String esc(const String& s) {
  String o;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '&') o += "&amp;";
    else if (c == '"') o += "&quot;";
    else o += c;
  }
  return o;
}

String jsonEsc(const String& s) {
  String o;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if (c >= 32) o += c;
  }
  return o;
}

String two(int v) { return v < 10 ? "0" + String(v) : String(v); }

// Returns true if the clock has been set by NTP
bool getTimeNow(struct tm* t) {
  time_t now = time(nullptr);
  if (now < 100000) return false;            // not synced yet
  localtime_r(&now, t);
  return true;
}

String nowString() {
  struct tm t;
  if (!getTimeNow(&t)) return "syncing time...";
  return two(t.tm_hour) + ":" + two(t.tm_min) + ":" + two(t.tm_sec);
}

// ---------- Hardware actions ----------
void beep(int ms) {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(ms);
  digitalWrite(BUZZER_PIN, LOW);
}

void dispense() {
  dispenser.write(SERVO_OPEN);
  delay(10000);
  dispenser.write(SERVO_CLOSED);
  delay(500);
}

void startAlert(const String& name) {
  currentDoseName = name;
  alertStart = millis();
  state = ALERTING;
  Serial.println("Dose time: " + name + " - press the button.");
}

// ---------- Web handlers ----------
void handleRoot() {
  String h;
  h.reserve(3000);
  h += F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
         "<meta name='viewport' content='width=device-width,initial-scale=1'>"
         "<title>MedBox</title><style>"
         "body{font-family:sans-serif;max-width:480px;margin:auto;padding:16px}"
         "h1{font-size:1.4em}.card{border:1px solid #ccc;border-radius:10px;padding:12px;margin:10px 0}"
         "input,button{font-size:1em;padding:10px;margin:4px 0;width:100%;box-sizing:border-box}"
         "button{border:0;border-radius:8px;background:#1a73e8;color:#fff}"
         "button.del{background:#d93025;width:auto;padding:6px 12px}"
         ".row{display:flex;justify-content:space-between;align-items:center}"
         "</style></head><body>");
  h += F("<h1>MedBox schedule</h1>");
  h += "<p>Device time: <b>" + nowString() + "</b></p>";

  bool any = false;
  for (int i = 0; i < MAX_DOSES; i++) {
    if (!doses[i].used) continue;
    any = true;
    h += "<div class='card row'><div><b>" + two(doses[i].hour) + ":" + two(doses[i].minute) +
         "</b> &nbsp;" + esc(doses[i].name) + "</div>"
         "<form method='POST' action='/delete'><input type='hidden' name='i' value='" + String(i) +
         "'><button class='del'>Delete</button></form></div>";
  }
  if (!any) h += F("<p>No doses scheduled yet.</p>");

  h += F("<div class='card'><b>Add dose</b>"
         "<form method='POST' action='/add'>"
         "<input name='name' placeholder='Medicine name' maxlength='23' required>"
         "<input type='time' name='time' required>"
         "<button>Add</button></form></div>"
         "<form method='POST' action='/test'><button style='background:#188038'>Test alert now</button></form>"
         "</body></html>");

  server.send(200, "text/html", h);
}

void redirectHome() {
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleAdd() {
  String name = server.arg("name");
  String tm   = server.arg("time");          // "HH:MM"
  int hh, mm;
  if (name.length() == 0 || sscanf(tm.c_str(), "%d:%d", &hh, &mm) != 2 ||
      hh < 0 || hh > 23 || mm < 0 || mm > 59) {
    server.send(400, "text/plain", "Invalid name or time");
    return;
  }
  for (int i = 0; i < MAX_DOSES; i++) {
    if (!doses[i].used) {
      doses[i].used = true;
      doses[i].hour = hh;
      doses[i].minute = mm;
      memset(doses[i].name, 0, sizeof(doses[i].name));
      name.toCharArray(doses[i].name, sizeof(doses[i].name));
      lastFired[i] = -1;
      saveDoses();
      Serial.println("Added: " + name + " at " + tm);
      redirectHome();
      return;
    }
  }
  server.send(400, "text/plain", "Schedule full");
}

void handleDelete() {
  int i = server.arg("i").toInt();
  if (i >= 0 && i < MAX_DOSES && doses[i].used) {
    doses[i].used = false;
    saveDoses();
    Serial.println("Deleted dose " + String(i));
  }
  redirectHome();
}

void handleTest() {
  if (state == WAITING) startAlert("Test dose");
  redirectHome();
}

// JSON for the future native app
void handleApiSchedule() {
  String j = "{\"time\":\"" + nowString() + "\",\"doses\":[";
  bool first = true;
  for (int i = 0; i < MAX_DOSES; i++) {
    if (!doses[i].used) continue;
    if (!first) j += ",";
    first = false;
    j += "{\"id\":" + String(i) + ",\"name\":\"" + jsonEsc(doses[i].name) +
         "\",\"time\":\"" + two(doses[i].hour) + ":" + two(doses[i].minute) + "\"}";
  }
  j += "]}";
  server.send(200, "application/json", j);
}

// ---------- Setup ----------
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.hostname(HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to Wi-Fi");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis() - start > 20000) {
      Serial.println("\nWi-Fi failed (check SSID/password, 2.4 GHz only). Restarting...");
      ESP.restart();
    }
  }
  Serial.println("\nConnected. IP: " + WiFi.localIP().toString());
}

void setup() {
  Serial.begin(115200);
  EEPROM.begin(512);

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  dispenser.attach(SERVO_PIN, 500, 2400);
  dispenser.write(SERVO_CLOSED);

  loadDoses();
  connectWiFi();

  configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, NTP_SERVER);

  if (MDNS.begin(HOSTNAME)) {
    Serial.println("Open http://" + String(HOSTNAME) + ".local on your phone");
  }
  Serial.println("Or use http://" + WiFi.localIP().toString());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/add", HTTP_POST, handleAdd);
  server.on("/delete", HTTP_POST, handleDelete);
  server.on("/test", HTTP_POST, handleTest);
  server.on("/api/schedule", HTTP_GET, handleApiSchedule);
  server.begin();
}

// ---------- Main loop ----------
void loop() {
  server.handleClient();
  MDNS.update();
  unsigned long now = millis();

  switch (state) {
    case WAITING: {
      digitalWrite(LED_PIN, LOW);
      digitalWrite(BUZZER_PIN, LOW);

      if (now - lastTimeCheck >= 1000) {          // check the clock once a second
        lastTimeCheck = now;
        struct tm t;
        if (getTimeNow(&t)) {
          int key = t.tm_yday * 1440 + t.tm_hour * 60 + t.tm_min;
          for (int i = 0; i < MAX_DOSES; i++) {
            if (doses[i].used && doses[i].hour == t.tm_hour &&
                doses[i].minute == t.tm_min && lastFired[i] != key) {
              lastFired[i] = key;
              startAlert(String(doses[i].name));
              break;
            }
          }
        }
      }
      break;
    }

    case ALERTING: {
      bool on = (now / 400) % 2;
      digitalWrite(LED_PIN, on);
      digitalWrite(BUZZER_PIN, on);

      if (digitalRead(BUTTON_PIN) == LOW) {
        digitalWrite(BUZZER_PIN, LOW);
        digitalWrite(LED_PIN, HIGH);
        state = DISPENSING;
      } else if (now - alertStart >= ALERT_TIMEOUT_MS) {
        Serial.println("Dose MISSED: " + currentDoseName);
        state = WAITING;
      }
      break;
    }

    case DISPENSING:
      Serial.println("Dispensing: " + currentDoseName);
      dispense();
      beep(150);
      digitalWrite(LED_PIN, LOW);
      state = WAITING;
      break;
  }
}
