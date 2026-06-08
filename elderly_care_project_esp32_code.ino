#include <WiFi.h>
#include <FirebaseESP32.h>
#include "time.h"

// 1. Wi-Fi 및 파이어베이스 설정 (환경에 맞게 변경)
#define WIFI_SSID "Wokwi-GUEST"          
#define WIFI_PASSWORD ""  
#define FIREBASE_HOST "https://bell-d3c5f-default-rtdb.firebaseio.com/"

// 2. 핀 번호 설정 (회로도 기준)
const int BUTTON_PIN = 19; 
const int BUZZER_PIN = 26; 

// 3. NTP 서버 설정 (한국 표준시)
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 9 * 3600; 
const int   daylightOffset_sec = 0;

FirebaseData firebaseData;
FirebaseConfig config;
FirebaseAuth auth;

// 가상 시간 변수 (Wokwi 테스트용: 필요 시 실제 시간으로 교체 가능)
int mockHour = 6;
int mockMinute = 55;
int mockSecond = 0;
unsigned long lastClockTime = 0;

bool isAlarming = false;       
String currentMealName = "";   
unsigned long alarmStartTime = 0;
unsigned long lastBuzzerTime = 0;

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Wi-Fi 연결 중");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi 연결 완료!");

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  config.database_url = FIREBASE_HOST;
  config.signer.test_mode = true;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  Serial.println("\n=== Wokwi 테스트 명령어 ===");
  Serial.println("'m1' 입력: 아침 알람 5초 전 (06:59:55)");
  Serial.println("'m2' 입력: 점심 알람 5초 전 (11:59:55)");
  Serial.println("'m3' 입력: 저녁 알람 5초 전 (16:59:55)");
  Serial.println("==========================\n");
}

void loop() {
  // --- [Wokwi 가상 시계 로직] ---
  if (millis() - lastClockTime >= 1000) {
    lastClockTime = millis();
    mockSecond++;
    if (mockSecond >= 60) { mockSecond = 0; mockMinute++; }
    if (mockMinute >= 60) { mockMinute = 0; mockHour++; }
    if (mockHour >= 24) { mockHour = 0; }
    if (!isAlarming) {
      Serial.printf("현재 가상 시간 -> %02d:%02d:%02d\n", mockHour, mockMinute, mockSecond);
    }
  }

  // 시리얼 명령 워프 기능
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input == "m1") { mockHour = 6; mockMinute = 59; mockSecond = 55; }
    else if (input == "m2") { mockHour = 11; mockMinute = 59; mockSecond = 55; }
    else if (input == "m3") { mockHour = 16; mockMinute = 59; mockSecond = 55; }
  }

  // --- [1. 알람 트리거 로직] ---
  if (mockSecond == 0) {
    bool trigger = false;
    if (mockHour == 7 && mockMinute == 0) { currentMealName = "아침 식사"; trigger = true; }
    else if (mockHour == 12 && mockMinute == 0) { currentMealName = "점심 식사"; trigger = true; }
    else if (mockHour == 17 && mockMinute == 0) { currentMealName = "저녁 식사"; trigger = true; }

    if (trigger) {
      isAlarming = true;
      alarmStartTime = millis();
      // 파이어베이스에 알람 시작 시간 저장 (웹사이트 타이머용)
      Firebase.setInt(firebaseData, "/" + currentMealName + "_time", alarmStartTime);
      Firebase.setString(firebaseData, "/" + currentMealName, "waiting");
      Serial.println("\n🚨 " + currentMealName + " 알람 발생!");
    }
  }

  // --- [2. 부저 제어 및 5분 응답 마감 로직] ---
  if (isAlarming) {
    // 5분이 지나면 자동으로 알람 폭주 상태(웹에서 감지하도록 timeout 처리)
    if (millis() - alarmStartTime >= 300000) { // 5분 = 300,000ms
      isAlarming = false;
      digitalWrite(BUZZER_PIN, LOW);
      Firebase.setString(firebaseData, "/" + currentMealName, "timeout");
      Serial.println("\n❌ 5분간 응답 없음 - 응급/확인 요망 상태로 전환");
    } else {
      // 5분 이내에는 1초 간격 삑- 삑-
      if (millis() - lastBuzzerTime >= 1000) {
        lastBuzzerTime = millis();
        digitalWrite(BUZZER_PIN, !digitalRead(BUZZER_PIN));
      }
    }
  }

  // --- [3. 안부 확인 버튼 클릭 로직] ---
  if (isAlarming && digitalRead(BUTTON_PIN) == HIGH) {
    delay(50); // 디바운스
    if (digitalRead(BUTTON_PIN) == HIGH) {
      isAlarming = false;
      digitalWrite(BUZZER_PIN, LOW);
      Serial.println("\n🟢 안부 확인 버튼 눌림!");

      // 파이어베이스 상태를 'done'으로 변경 -> 웹에서 감지 후 텔레그램 발송
      if (Firebase.setString(firebaseData, "/" + currentMealName, "done")) {
        Serial.println("=> Firebase 전송 완료 ('done')");
      }
      while (digitalRead(BUTTON_PIN) == HIGH) { delay(10); }
    }
  }
}
