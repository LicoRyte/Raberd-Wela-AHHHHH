// ===== BOOM GAME HOST (Arduino UNO R4 WiFi + MQTTClient + LCD I2C + Buzzer) =====
// FIXED VERSION — same structure, see "FIX" comments

#include <WiFiS3.h>
#include <MQTTClient.h>
#include <LiquidCrystal_I2C.h>

// ---------- config ----------
LiquidCrystal_I2C lcd(0x27, 16, 2);

const char WIFI_SSID[]     = "Pxnup_";
const char WIFI_PASSWORD[] = "T1a7e0550";

const char MQTT_BROKER_ADDRESS[] = "mqtt-dashboard.com";
const int  MQTT_PORT = 1883;

const char HOST_ID[]   = "HOST";
const char CLIENT_ID[] = "boom-phycom-HOST";

const char TOPIC_ID[]       = "boom-game/phycom/ID";
const char TOPIC_QUESTION[] = "boom-game/phycom/question";
const char TOPIC_ANSWER[]   = "boom-game/phycom/answer";

const int MAX_PLAYERS             = 3;
const int START_LIVES             = 3;
const unsigned long TIME_LIMIT    = 10000;   // ms ต่อข้อ (10 วินาที)
const unsigned long GAMEOVER_WAIT = 5000;    // ms ก่อนเริ่มห้องใหม่

const int BUZZER_PIN = 8;

const int EQ_BUF = 24;   // FIX: equation buffer size (was 17, level 10 overflowed it)

unsigned long lastBeepTime = 0;
int lastDisplayedSec = -1;

// กำหนดรูปหัวใจแบบ Custom Character (8x8 pixels)
byte heart[8] = {
  B00000,
  B01010,
  B11111,
  B11111,
  B11111,
  B01110,
  B00100,
  B00000
};

// ---------- player table ----------
struct Player {
  String id;
  bool connected;
  bool alive;
  int lives;
};

Player players[MAX_PLAYERS];
int playerCount = 0;

// ---------- game tracking ----------
int currentRound = 1;     // นับรอบเกมปัจจุบัน
int playerIndex = 0;      // ชี้ว่าตอนนี้เป็นเทิร์นของใคร
long correctAnswer = 0;   // เก็บคำตอบของโจทย์ปัจจุบัน

// ---------- flags set in onMessage, handled in loop ----------
bool welcomePending[MAX_PLAYERS];
bool fullPending = false;
String fullId;
bool countChanged = false;

bool answerReady = false;
String answerText;

// ---------- game state ----------
enum State { WAIT_PLAYERS, SETUP, ASK, WAIT_ANSWER, GAME_OVER };
State state = WAIT_PLAYERS;

int currentPlayer = -1;
unsigned long questionStart = 0;
unsigned long gameOverAt = 0;

WiFiClient net;
MQTTClient client(256);

// FIX: wait without freezing MQTT (replaces delay() during the game)
void waitWithMqtt(unsigned long ms) {
  unsigned long start = millis();
  while (millis() - start < ms) {
    if (client.connected()) client.loop();
    delay(5);
  }
}

// ---------- equation generator (New Version Up to Level 10) ----------
// FIX: all sprintf -> snprintf with EQ_BUF so it can never overflow
long generateEquation(int level, char *textBuffer) {
  long answer = 0;
  int a, b, c;
  int rootAns; // ไว้เก็บค่าคำตอบของรูทชั่วคราว

  if (level > 10) level = 10;
  if (level < 1) level = 1;

  switch (level) {
    case 1: {
      a = random(5, 21);
      b = random(1, a);
      if (random(0, 2) == 0) {
        answer = a + b;
        snprintf(textBuffer, EQ_BUF, "%d+%d", a, b);
      } else {
        answer = a - b;
        snprintf(textBuffer, EQ_BUF, "%d-%d", a, b);
      }
      break;
    }

    case 2: {
      a = random(2, 13);
      b = random(2, 13);
      answer = a * b;
      snprintf(textBuffer, EQ_BUF, "%d*%d", a, b);
      break;
    }

    case 3: {
      answer = random(2, 13);
      b = random(2, 13);
      a = answer * b;
      snprintf(textBuffer, EQ_BUF, "%d/%d", a, b);
      break;
    }

    case 4: {
      a = random(2, 10);
      b = random(2, 10);
      c = random(1, 20);
      if (random(0, 2) == 0) {
        answer = (a * b) + c;
        snprintf(textBuffer, EQ_BUF, "%d*%d+%d", a, b, c);
      } else {
        c = random(1, (a * b));
        answer = (a * b) - c;
        snprintf(textBuffer, EQ_BUF, "%d*%d-%d", a, b, c);
      }
      break;
    }

    case 5: {
      a = random(2, 8);
      b = random(2, 10);
      c = random(2, 10);
      answer = a * (b + c);
      snprintf(textBuffer, EQ_BUF, "%d*(%d+%d)", a, b, c);
      break;
    }

    case 6: {
      if (random(0, 2) == 0) {
        a = random(2, 16);
        answer = a * a;
        snprintf(textBuffer, EQ_BUF, "%d^2", a);
      } else {
        a = random(2, 6);
        answer = a * a * a;
        snprintf(textBuffer, EQ_BUF, "%d^3", a);
      }
      break;
    }

    case 7: {
      answer = random(2, 16);
      a = answer * answer;
      snprintf(textBuffer, EQ_BUF, "sqrt(%d)", a);
      break;
    }

    case 8: {
      a = random(3, 11);
      if (random(0, 2) == 0) {
        c = random(5, 30);
        answer = (a * a) + c;
        snprintf(textBuffer, EQ_BUF, "%d^2+%d", a, c);
      } else {
        c = random(1, a * a);
        answer = (a * a) - c;
        snprintf(textBuffer, EQ_BUF, "%d^2-%d", a, c);
      }
      break;
    }

    case 9: {
      rootAns = random(2, 13);
      a = rootAns * rootAns;
      b = random(2, 10);
      answer = rootAns * b;
      snprintf(textBuffer, EQ_BUF, "sqrt(%d)*%d", a, b);
      break;
    }

    case 10: {
      rootAns = random(2, 11);
      a = rootAns * rootAns;

      b = random(2, 5);
      int powerAns = b * b;

      c = random(1, (rootAns * powerAns));

      answer = (rootAns * powerAns) - c;
      snprintf(textBuffer, EQ_BUF, "sqrt(%d)*%d^2-%d", a, b, c);
      break;
    }

    default: {
      answer = 1;
      snprintf(textBuffer, EQ_BUF, "1+0");
      break;
    }
  }

  return answer;
}

// FIX: const reference instead of copying the whole Player (and its String)
int calculateLevel(int round, const Player& p) {
  // คำนวณเลเวลตามรอบ (เช่น ทุกๆ 2 รอบ เลเวลจะเพิ่มขึ้น 1 สูงสุดไม่เกิน 10)
  int lvl = (round / 2) + 1;
  if (lvl > 10) lvl = 10;
  return lvl;
}

// ---------- helpers ----------
int findPlayer(const String& id) {
  for (int i = 0; i < playerCount; i++)
    if (players[i].id == id) return i;
  return -1;
}

int connectedCount() {
  int n = 0;
  for (int i = 0; i < playerCount; i++) if (players[i].connected) n++;
  return n;
}

int activeCount() {
  int n = 0;
  for (int i = 0; i < playerCount; i++)
    if (players[i].alive && players[i].connected) n++;
  return n;
}

void send(const char* topic, const String& id, const String& data) {
  if (!client.connected()) return;          // FIX: don't publish into a dead connection
  client.publish(topic, id + ":" + data, false, 1);
}

void updateLCDStatus() {
  lcd.clear();
  lcd.setCursor(0, 0);
  if (state == WAIT_PLAYERS) {
    lcd.print("Waiting Players");
    lcd.setCursor(0, 1);
    lcd.print("Connected: " + String(connectedCount()) + "/" + String(MAX_PLAYERS));
  } else if (state == GAME_OVER) {
    lcd.print("Game Over!");
  }
}

void announceCount() {
  send(TOPIC_ID, HOST_ID,
       "players=" + String(connectedCount()) + "/" + String(MAX_PLAYERS));
  if (state == WAIT_PLAYERS) updateLCDStatus();
}

void printPlayers() {
  Serial.println("---- players " + String(connectedCount()) + "/" +
                 String(MAX_PLAYERS) + " ----");
  for (int i = 0; i < playerCount; i++)
    Serial.println("[" + String(i) + "] " + players[i].id + "  " +
                   (players[i].connected ? "online" : "offline"));
}

int nextPlayer() {
  if (playerCount == 0) return -1;          // FIX: avoid % 0
  for (int step = 1; step <= playerCount; step++) {
    int i = (currentPlayer + step) % playerCount;
    if (i < 0) i += playerCount;
    if (players[i].alive && players[i].connected) return i;
  }
  return -1;
}

// ---------- game stubs ----------
void setupGame() {
  for (int i = 0; i < playerCount; i++) {
    players[i].alive = true;
    players[i].lives = START_LIVES;
  }
  currentPlayer = -1;
  currentRound = 1;
  Serial.println("=== GAME START ===");
  send(TOPIC_ID, HOST_ID, "start");

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("== GAME START ==");
  waitWithMqtt(1500);                       // FIX: was delay()
}

void giveQuestion(int p) {
  playerIndex = p;
  int level = calculateLevel(currentRound, players[playerIndex]);

  char equationText[EQ_BUF];                // FIX: was [17]
  correctAnswer = generateEquation(level, equationText);

  // Debug ออก Serial
  Serial.print("Player ");
  Serial.print(players[playerIndex].id);
  Serial.print(" Level: ");
  Serial.println(level);
  Serial.print("Equation: ");
  Serial.println(equationText);
  Serial.print("Answer is: ");
  Serial.println(correctAnswer);

  // ส่งโจทย์ไปให้ผู้เล่นคนนั้นผ่าน MQTT (รูปแบบ: ID:สมการ)
  send(TOPIC_QUESTION, players[p].id, String(equationText));

  // แสดงผลบน LCD บรรทัดบน (แสดงโจทย์ที่ส่งให้ผู้เล่น เช่น P1:5+8)
  lcd.setCursor(0, 0);
  lcd.print("                "); // เคลียร์บรรทัดบนก่อนพิมพ์
  lcd.setCursor(0, 0);
  lcd.print((players[p].id + ":" + String(equationText)).substring(0, 16));  // FIX: max 16
}

bool isCorrect(const String& answer) {
  // FIX: reject empty / non-number answers (toInt() turns them into 0)
  if (answer.length() == 0) return false;
  for (unsigned int i = 0; i < answer.length(); i++) {
    if (answer[i] < '0' || answer[i] > '9') return false;
  }
  long playerAns = answer.toInt();
  return (playerAns == correctAnswer);
}

void playExplodeSound() {
  for (int freq = 1000; freq > 100; freq -= 50) {
    tone(BUZZER_PIN, freq);
    waitWithMqtt(20);                       // FIX: was delay()
  }
  noTone(BUZZER_PIN);
}

void explode(int p) {
  noTone(BUZZER_PIN);                       // FIX: stop the ticking tone first
  playExplodeSound();

  players[p].lives--;
  if (players[p].lives < 0) players[p].lives = 0;
  Serial.println(players[p].id + " exploded, lives=" + String(players[p].lives));
  send(TOPIC_ID, players[p].id, "boom=" + String(players[p].lives));

  // FIX: mark dead BEFORE the wait, so nothing during the wait treats them as alive
  if (players[p].lives <= 0) {
    players[p].alive = false;
    send(TOPIC_ID, players[p].id, "out");
    Serial.println(players[p].id + " is OUT");
  }

  // เคลียร์จอทั้งหมดเพื่อล้างข้อความเก่า (เช่น ตัวนับเวลาเดิม) ไม่ให้ค้าง
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(players[p].id + (players[p].alive ? " BOOM!" : " is OUT!"));
  lcd.setCursor(0, 1);
  lcd.print("Lives left: " + String(players[p].lives));
  waitWithMqtt(1500);                       // FIX: was delay()
}

void announceWinner() {
  noTone(BUZZER_PIN);
  String winner = "NONE";
  for (int i = 0; i < playerCount; i++)
    if (players[i].alive && players[i].connected) winner = players[i].id;
  Serial.println("Winner: " + winner);
  send(TOPIC_ID, HOST_ID, "winner=" + winner);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Winner: " + winner);
  lcd.setCursor(0, 1);
  lcd.print("Restarting...");

  int melody[] = {
    659, 150,  659, 150,  0, 150,  659, 150,  0, 150,  523, 150,  659, 150,  784, 300,  392, 300
  };
  int notes = sizeof(melody) / sizeof(melody[0]) / 2;
  for (int i = 0; i < notes; i++) {
    int freq = melody[i * 2];
    int duration = melody[i * 2 + 1];
    if (freq == 0) noTone(BUZZER_PIN);
    else tone(BUZZER_PIN, freq, duration);
    waitWithMqtt(duration + 30);            // FIX: was delay()
  }
  noTone(BUZZER_PIN);
}

void goNext() {
  noTone(BUZZER_PIN);
  if (activeCount() <= 1) { state = GAME_OVER; return; }
  int n = nextPlayer();
  if (n < 0) { state = GAME_OVER; return; }
  currentPlayer = n;
  currentRound++;
  state = ASK;
}

// ---------- incoming ----------
void onMessage(String &topic, String &payload) {
  int sep = payload.indexOf(':');
  if (sep <= 0) return;
  String id   = payload.substring(0, sep);
  String data = payload.substring(sep + 1);
  id.trim();
  data.trim();
  if (id == HOST_ID) return;

  if (topic == TOPIC_ID) {
    if (data == "join") {
      int i = findPlayer(id);
      if (i < 0) {
        if (playerCount < MAX_PLAYERS && state == WAIT_PLAYERS) {
          i = playerCount;
          players[playerCount++] = { id, true, true, START_LIVES };
        } else {
          fullPending = true;
          fullId = id;
          return;
        }
      }
      players[i].connected = true;
      welcomePending[i] = true;
      countChanged = true;
    }
    else if (data == "offline") {
      int i = findPlayer(id);
      if (i >= 0 && players[i].connected) {
        players[i].connected = false;
        countChanged = true;
      }
    }
  }
  else if (topic == TOPIC_ANSWER) {
    if (state == WAIT_ANSWER && currentPlayer >= 0 &&
        id == players[currentPlayer].id) {
      answerText  = data;
      answerReady = true;
    }
  }
}

void handleFlags() {
  for (int i = 0; i < playerCount; i++) {
    if (welcomePending[i]) {
      welcomePending[i] = false;
      send(TOPIC_ID, players[i].id, "welcome");
    }
  }
  if (fullPending) {
    fullPending = false;
    send(TOPIC_ID, fullId, "full");
  }
  if (countChanged) {
    countChanged = false;
    announceCount();
    printPlayers();
  }
}

// ---------- game loop ----------
void runGame() {
  switch (state) {

    case WAIT_PLAYERS:
      if (playerCount == MAX_PLAYERS && connectedCount() == MAX_PLAYERS)
        state = SETUP;
      break;

    case SETUP:
      setupGame();
      goNext();
      break;

    case ASK:
      if (currentPlayer < 0 || !players[currentPlayer].connected) {  // FIX: safety
        goNext();
        break;
      }
      giveQuestion(currentPlayer);
      questionStart = millis();
      answerReady = false;
      lastDisplayedSec = -1;

      // เตรียมหน้าจอ LCD บรรทัดล่างสำหรับ Timer และหัวใจ
      lcd.setCursor(0, 1);
      lcd.print("Timer : ");

      state = WAIT_ANSWER;
      break;

    case WAIT_ANSWER:
      {
        unsigned long elapsed = millis() - questionStart;

        if (currentPlayer < 0 || !players[currentPlayer].connected) {
          goNext();
        }
        else if (answerReady) {
          answerReady = false;
          if (isCorrect(answerText)) {
            send(TOPIC_ID, players[currentPlayer].id, "correct");
            goNext();
          } else {
            send(TOPIC_ID, players[currentPlayer].id, "wrong");
          }
        }
        else if (elapsed >= TIME_LIMIT) {
          explode(currentPlayer);
          goNext();
        }
        else {
          long remainingMs = TIME_LIMIT - elapsed;
          int remainingSec = (remainingMs + 999) / 1000;
          if (remainingSec < 0) remainingSec = 0;

          if (remainingSec != lastDisplayedSec) {
            lastDisplayedSec = remainingSec;

            // แสดงเวลาถอยหลัง 2 หลัก
            lcd.setCursor(8, 1);
            if (remainingSec < 10) lcd.print("0");
            lcd.print(remainingSec);

            lcd.print("   ");
            int currentLives = players[currentPlayer].lives;
            for (int h = 0; h < START_LIVES; h++) {
              lcd.setCursor(13 + h, 1);
              if (h < currentLives) {
                lcd.write(byte(0));
              } else {
                lcd.print(" ");
              }
            }
          }

          unsigned long currentInterval = map(elapsed, 0, TIME_LIMIT, 800, 100);
          if (currentInterval < 100) currentInterval = 100;

          if (millis() - lastBeepTime >= currentInterval) {
            lastBeepTime = millis();
            tone(BUZZER_PIN, 2500, 50);
          }
        }
      }
      break;

    case GAME_OVER:
      if (gameOverAt == 0) {
        announceWinner();
        gameOverAt = millis();
      } else if (millis() - gameOverAt >= GAMEOVER_WAIT) {
        gameOverAt = 0;
        state = WAIT_PLAYERS;
        announceCount();
      }
      break;
  }
}

void connectWiFi() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.println("Connecting WiFi...");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    delay(3000);
  }
  Serial.println("WiFi OK");
  lcd.clear();
  lcd.print("WiFi Connected!");
  delay(1000);
}

void connectMqtt() {
  client.setWill(TOPIC_ID, "HOST:offline", false, 1);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connecting MQTT");
  while (!client.connect(CLIENT_ID)) {
    if (WiFi.status() != WL_CONNECTED) connectWiFi();   // FIX: don't retry on dead WiFi forever
    Serial.print("MQTT failed, error=");
    Serial.print(client.lastError());
    Serial.print(" rc=");
    Serial.println(client.returnCode());
    delay(2000);
  }

  client.subscribe(TOPIC_ID, 1);
  client.subscribe(TOPIC_ANSWER, 1);
  Serial.println("MQTT OK as HOST");

  lcd.clear();
  lcd.print("MQTT Connected!");
  delay(1000);


  if (state == WAIT_PLAYERS) {
    for (int i = 0; i < playerCount; i++) players[i].connected = false;
  }
  send(TOPIC_ID, HOST_ID, "rollcall");
}

// ---------- main ----------
void setup() {
  Serial.begin(115200);

  pinMode(BUZZER_PIN, OUTPUT);
  randomSeed(analogRead(A0) ^ micros());    // FIX: different questions every boot

  Wire.begin();
  lcd.init();
  lcd.backlight();

  lcd.createChar(0, heart);

  lcd.setCursor(0, 0);
  lcd.print("Booting Host...");

  connectWiFi();
  client.begin(MQTT_BROKER_ADDRESS, MQTT_PORT, net);
  client.onMessage(onMessage);
  client.setKeepAlive(10);
  connectMqtt();
  announceCount();
}

void loop() {
  client.loop();
  if (WiFi.status() != WL_CONNECTED) connectWiFi();
  if (!client.connected()) connectMqtt();

  handleFlags();
  runGame();
}
