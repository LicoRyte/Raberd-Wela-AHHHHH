#include <WiFiS3.h>
#include <MQTTClient.h>
#include <LiquidCrystal_I2C.h>

const char WIFI_SSID[]     = "Pxnup_";
const char WIFI_PASSWORD[] = "T1a7e0550";

const char MQTT_BROKER_ADDRESS[] = "mqtt-dashboard.com";
const int  MQTT_PORT = 1883;

const char PLAYER_ID[] = "P99";   // CHANGE PER BOARD
const char HOST_ID[]   = "HOST";

const char TOPIC_ID[]       = "boom-game/phycom/ID";
const char TOPIC_QUESTION[] = "boom-game/phycom/question";
const char TOPIC_ANSWER[]   = "boom-game/phycom/answer";


char inputBuf[17];
byte inputLen = 0;


char clientId[48];
char willMsg[24];

bool needJoin = false;
String serialBuf;

WiFiClient net;
MQTTClient client(256);
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ---------- keypad ----------
char keys[4][3] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'},
  {'*','0','#'}
};
byte rowPins[4] = {11, 10, 9, 8};
byte colPins[3] = {4, 3, 2};

char lastKey = 0;
unsigned long lastScan = 0;

void setupKeypad() {
  for (int r = 0; r < 4; r++) pinMode(rowPins[r], INPUT);
  for (int c = 0; c < 3; c++) pinMode(colPins[c], INPUT_PULLUP);
}

char scanKeypad() {
  for (int r = 0; r < 4; r++) {
    pinMode(rowPins[r], OUTPUT);
    digitalWrite(rowPins[r], LOW);
    delayMicroseconds(10);

    for (int c = 0; c < 3; c++) {
      if (digitalRead(colPins[c]) == LOW) {
        pinMode(rowPins[r], INPUT);
        return keys[r][c];
      }
    }
    pinMode(rowPins[r], INPUT);
  }
  return 0;
}

void addChar(char *s, char c) {

    while (*s++);
    *(s - 1) = c;
    *s = '\0';
}

// returns a key once per press, else 0
char getKey() {
  if (millis() - lastScan < 20) return 0;   // scan every 20ms = simple debounce
  lastScan = millis();

  char k = scanKeypad();
  char pressed = (k != 0 && k != lastKey) ? k : 0;
  lastKey = k;
  return pressed;
}

// ---------- LCD ----------
bool screenOn = true;

void setScreen(bool on) {
  if (on == screenOn) return;
  screenOn = on;
  if (on) { lcd.display();   lcd.backlight();   }
  else    { lcd.noDisplay(); lcd.noBacklight(); }
}

void clearRow(byte row) {
  lcd.setCursor(0, row);
  lcd.print("                ");
}

void printLCD(byte row, const String& text) {
  clearRow(row);
  lcd.setCursor(0, row);
  lcd.print(text.substring(0, 16));
}

void show(const String& line1, const String& line2 = "") {
  printLCD(0, line1);
  printLCD(1, line2);
  setScreen(true);
}

void submit() {
  int n = atoi(inputBuf);
  send(TOPIC_ANSWER, String(n));
  Serial.println("Answer sent: " + String(n));

  inputBuf[0] = '\0';
  inputLen = 0;
}

void resetInput() {
  inputBuf[0] = '\0';
  inputLen = 0;
}

// ---------- MQTT ----------
void send(const char* topic, const String& data) {
  client.publish(topic, String(PLAYER_ID) + ":" + data, false, 1);
}

void onMessage(String &topic, String &payload) {
  int sep = payload.indexOf(':');
  if (sep <= 0) return;
  String id   = payload.substring(0, sep);
  String data = payload.substring(sep + 1);
  id.trim();
  data.trim();

  if (topic == TOPIC_ID) {
    if (id == HOST_ID) {
      if (data == "rollcall")     needJoin = true;
      else if (data == "offline") show("Host offline");
      else                        show("Host:", data);
    }
    else if (id == PLAYER_ID) {
      if (data == "join") return;
      else if (data == "welcome") show("Joined!", "Waiting...");
      else if (data == "full")    show("Game is full");
      else if (data == "correct") show("Correct!");
      else if (data == "wrong")   show("Wrong!", "Try again");
      else if (data == "out")     show("You're OUT");
      else if (data.startsWith("boom=")) show("BOOM!", "Lives: " + data.substring(5));
      else                        show(data);
    }
    else {
      Serial.println(id + " " + data);
    }
  }
  else if (topic == TOPIC_QUESTION) {
    if (id == PLAYER_ID) {
      resetInput();
      printLCD(0, data);
      setScreen(true);
    } else {
      setScreen(false);
    }
  }
}


void readSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      serialBuf.trim();
      if (serialBuf.length() > 0) {
        send(TOPIC_ANSWER, serialBuf);
        Serial.println("Answer sent: " + serialBuf);
      }
      serialBuf = "";
    } else if (serialBuf.length() < 30) {
      serialBuf += c;
    }
  }
}

// ---------- connection ----------
void connectWiFi() {
  while (WiFi.status() != WL_CONNECTED) {
    show("Connecting", "WiFi...");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    delay(3000);
  }
  Serial.println("WiFi OK");
}

void connectMqtt() {
  client.setWill(TOPIC_ID, willMsg, false, 1);

  while (!client.connect(clientId)) {
    show("Connecting", "MQTT...");
    Serial.print("MQTT failed, error=");
    Serial.print(client.lastError());
    Serial.print(" rc=");
    Serial.println(client.returnCode());
    delay(2000);
  }

  client.subscribe(TOPIC_ID, 1);
  client.subscribe(TOPIC_QUESTION, 1);
  send(TOPIC_ID, "join");
  show(String(PLAYER_ID) + " online", "Joining...");
  Serial.println("MQTT OK as " + String(PLAYER_ID));
}

// ---------- main ----------
void setup() {
  Serial.begin(115200);
  snprintf(clientId, sizeof(clientId), "boom-phycom-%s", PLAYER_ID);
  snprintf(willMsg,  sizeof(willMsg),  "%s:offline",     PLAYER_ID);

  setupKeypad();

  lcd.init();
  lcd.backlight();
  show("Booting...");

  connectWiFi();
  client.begin(MQTT_BROKER_ADDRESS, MQTT_PORT, net);
  client.onMessage(onMessage);
  client.setKeepAlive(10);
  connectMqtt();
}

void loop() {
  client.loop();
  if (WiFi.status() != WL_CONNECTED) connectWiFi();
  if (!client.connected()) connectMqtt();

  if (needJoin) {
    needJoin = false;
    send(TOPIC_ID, "join");
  }

  char key = getKey();
  if (key >= '0' && key <= '9' && inputLen < 16) {
  inputBuf[inputLen++] = key;
  inputBuf[inputLen] = '\0';
  printLCD(1, inputBuf);
  } 
  else if (key == '#') submit();
  else if (key == '*' && inputLen > 0) {
  inputBuf[--inputLen] = '\0';
  printLCD(1, inputBuf);
}



  readSerial();
}