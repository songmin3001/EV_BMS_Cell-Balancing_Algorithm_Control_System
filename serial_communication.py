import serial
import time
import csv
import os
from datetime import datetime

# ── 설정 ──────────────────────────────────────────────
PORT        = "COM3"          # Windows: COM3 / Linux: /dev/ttyUSB0
BAUD        = 9600
V_HIGH      = 4.2
V_LOW       = 3.1
CAPACITY_AH = 3.2
LOG_INTERVAL = 1800           # 30분 (초)
MAX_TIME     = 7200           # 종료 조건: 2시간 (초)
LOG_FILE     = "bms_log.csv"

# ── 초기화 ────────────────────────────────────────────
def initialize():
    os.makedirs("logs", exist_ok=True)
    log_path = os.path.join("logs", LOG_FILE)
    with open(log_path, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["Timestamp", "Voltage(V)", "Current(A)", "Relay", "SoC(%)"])
    print("=== BMS 초기화 완료 ===")
    return log_path

# ── 전압/전류 읽기 (Arduino Serial 파싱) ──────────────
def read_sensor(ser) -> dict:
    """
    Arduino에서 "V:3.25,I:1.20\n" 형식으로 전송한다고 가정
    """
    line = ser.readline().decode("utf-8").strip()
    data = {}
    for part in line.split(","):
        key, val = part.split(":")
        data[key] = float(val)
    return data  # {"V": 3.25, "I": 1.20}

# ── 릴레이 명령 전송 ──────────────────────────────────
def set_relay(ser, on: bool):
    cmd = b"RELAY_ON\n" if on else b"RELAY_OFF\n"
    ser.write(cmd)

# ── SoC 업데이트 (쿨롱 카운팅) ───────────────────────
def update_soc(soc: float, current: float, dt_hour: float, capacity: float) -> float:
    soc -= (current * dt_hour) / capacity
    return max(0.0, min(1.0, soc))

# ── 데이터 로깅 ───────────────────────────────────────
def log_data(log_path, voltage, current, relay_on, soc):
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    with open(log_path, "a", newline="") as f:
        writer = csv.writer(f)
        writer.writerow([
            timestamp,
            f"{voltage:.3f}",
            f"{current:.3f}",
            "ON" if relay_on else "OFF",
            f"{soc * 100:.1f}"
        ])
    print(f"[로그] {timestamp} | V={voltage:.3f}V | I={current:.3f}A | SoC={soc*100:.1f}%")

# ── 종료 조건 ─────────────────────────────────────────
def check_termination(voltage, soc, elapsed):
    if elapsed >= MAX_TIME:
        print("[종료] 시간 초과")
        return True
    if voltage < V_LOW - 0.1:
        print("[종료] 전압 급락")
        return True
    if soc <= 0.05:
        print("[종료] SoC 5% 이하")
        return True
    return False

# ── 메인 루프 ─────────────────────────────────────────
def main():
    log_path   = initialize()
    soc        = 1.0
    relay_on   = False
    start_time = time.time()
    last_log   = time.time()
    last_soc_t = time.time()

    with serial.Serial(PORT, BAUD, timeout=1) as ser:
        time.sleep(2)  # Arduino 리셋 대기
        print(f"[연결] {PORT} 포트 열림")

        while True:
            try:
                sensors = read_sensor(ser)
                voltage = sensors.get("V", 0.0)
                current = sensors.get("I", 0.0)
            except Exception as e:
                print(f"[센서 오류] {e}")
                continue

            now = time.time()

            # ── 방전 릴레이 제어 ──
            if voltage >= V_HIGH and not relay_on:
                set_relay(ser, True)
                relay_on = True
                print(f"[방전 시작] V={voltage:.3f}V")
            elif voltage <= V_LOW and relay_on:
                set_relay(ser, False)
                relay_on = False
                print(f"[경고] 셀 전압 임계값 미만: {voltage:.3f}V → 방전 정지")

            # ── SoC 업데이트 ──
            dt_hour = (now - last_soc_t) / 3600.0
            soc = update_soc(soc, current, dt_hour, CAPACITY_AH)
            last_soc_t = now

            # ── 30분 주기 로깅 ──
            if now - last_log >= LOG_INTERVAL:
                log_data(log_path, voltage, current, relay_on, soc)
                last_log = now

            # ── 종료 조건 ──
            elapsed = now - start_time
            if check_termination(voltage, soc, elapsed):
                set_relay(ser, False)
                print("=== BMS 정지 ===")
                break

            time.sleep(1)

if __name__ == "__main__":
    main()