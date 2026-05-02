#include <SD.h>
#include <SPI.h>
#include <RTClib.h>

// ── 핀 설정 ──────────────────────────────────────────
#define RELAY_PIN       7
#define CURRENT_PIN     A0
#define VOLTAGE_PIN     A1
#define SD_CS_PIN       10

// ── 상수 ─────────────────────────────────────────────
const float V_HIGH       = 3.2;   // 방전 시작 전압
const float V_LOW        = 3.1;   // 방전 종료 전압
const float CAPACITY_AH  = 2.5;   // 셀 용량 (Ah)
const float V_REF        = 5.0;   // ADC 기준 전압
const unsigned long LOG_INTERVAL = 1800000UL; // 30분 (ms)
const unsigned long MAX_TIME     = 7200000UL; // 종료 조건: 2시간 (ms)

// ── 전역 변수 ─────────────────────────────────────────
RTC_DS3231 rtc;
float soc = 1.0;                  // 초기 SoC 100%
float prevCurrent = 0.0;
unsigned long prevLogTime  = 0;
unsigned long prevSocTime  = 0;
unsigned long startTime    = 0;
bool relayOn = false;

// ── 함수 선언 ─────────────────────────────────────────
float readVoltage();
float readCurrent();
void  setRelay(bool on);
void  updateSoC(float current, float dtHour);
void  logData(float voltage, float current);
bool  checkTermination(float voltage);

// ─────────────────────────────────────────────────────
void setup() {
  Serial.begin(9600);
  pinMode(RELAY_PIN, OUTPUT);
  setRelay(false);

  // RTC 초기화
  if (!rtc.begin()) {
    Serial.println("RTC 오류");
    while (1);
  }

  // SD 초기화
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("SD 오류");
    while (1);
  }

  // 로그 헤더 작성
  File f = SD.open("bms_log.csv", FILE_WRITE);
  if (f) {
    f.println("Timestamp,Voltage(V),Current(A),Relay,SoC(%)");
    f.close();
  }

  // 전류 센서 캘리브레이션 (오프셋 보정)
  prevCurrent = readCurrent();
  prevLogTime = millis();
  prevSocTime = millis();
  startTime   = millis();

  Serial.println("=== BMS 초기화 완료 ===");
}

// ─────────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();
  float voltage = readVoltage();
  float current = readCurrent();

  // ── 방전 릴레이 제어 ──
  if (voltage >= V_HIGH && !relayOn) {
    setRelay(true);
    Serial.println("[경고 해제] 방전 시작");
  } else if (voltage <= V_LOW && relayOn) {
    setRelay(false);
    Serial.print("[경고] 셀 전압 임계값 미만: ");
    Serial.print(voltage); Serial.println("V");
  }

  // ── SoC 업데이트 (쿨롱 카운팅) ──
  float dtHour = (now - prevSocTime) / 3600000.0;
  updateSoC(current, dtHour);
  prevSocTime = now;

  // ── 30분 주기 데이터 로깅 ──
  if (now - prevLogTime >= LOG_INTERVAL) {
    logData(voltage, current);
    prevLogTime = now;
  }

  // ── 종료 조건 확인 ──
  if (checkTermination(voltage)) {
    setRelay(false);
    Serial.println("=== 종료 조건 도달. BMS 정지 ===");
    while (1);  // 정지
  }

  delay(1000);
}

// ─────────────────────────────────────────────────────
float readVoltage() {
  int raw = analogRead(VOLTAGE_PIN);
  // 분압 회로 비율에 맞게 수정 필요 (예: ×2)
  return (raw / 1023.0) * V_REF * 2.0;
}

float readCurrent() {
  int raw = analogRead(CURRENT_PIN);
  float voltage = (raw / 1023.0) * V_REF;
  // ACS712-5A 기준: 오프셋 2.5V, 감도 0.185V/A
  return (voltage - 2.5) / 0.185;
}

void setRelay(bool on) {
  relayOn = on;
  digitalWrite(RELAY_PIN, on ? HIGH : LOW);
}

void updateSoC(float current, float dtHour) {
  // SoC(t1) = SoC(t0) - (1/C) * I * dt
  soc -= (current * dtHour) / CAPACITY_AH;
  soc = constrain(soc, 0.0, 1.0);
}

void logData(float voltage, float current) {
  DateTime now = rtc.now();
  File f = SD.open("bms_log.csv", FILE_WRITE);
  if (f) {
    char buf[64];
    sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d",
            now.year(), now.month(), now.day(),
            now.hour(), now.minute(), now.second());
    f.print(buf);        f.print(",");
    f.print(voltage, 3); f.print(",");
    f.print(current, 3); f.print(",");
    f.print(relayOn ? "ON" : "OFF"); f.print(",");
    f.println(soc * 100.0, 1);
    f.close();
    Serial.println("[로그] 저장 완료");
  }
}

bool checkTermination(float voltage) {
  unsigned long elapsed = millis() - startTime;
  if (elapsed >= MAX_TIME)       return true;  // 시간 초과
  if (voltage < V_LOW - 0.1)    return true;  // 전압 급락
  if (soc <= 0.05)               return true;  // SoC ≤ 5%
  return false;
}