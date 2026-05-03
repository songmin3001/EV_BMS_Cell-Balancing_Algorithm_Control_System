# EV_BMS_Cell-Balancing_Algorithm_Control_System
# BMS (Battery Management System) - 셀 전압 기반 방전 제어 시스템

플로우차트 기반으로 구현된 4S BMS 코드입니다.  
Arduino C++, Python, C# 세 가지 버전을 제공합니다.

---

## 시스템 동작 흐름

```
초기화
  └─ 릴레이 모듈 설정, 방전 이력 초기화, 전류 센서 캘리브레이션, 로그 디렉터리 생성
개별 셀 전압 취득
  ├─ 전압 ≥ 3.2V → 방전 릴레이 ON (방전 시작)
  ├─ 전압 ≤ 3.1V → 방전 릴레이 OFF + 경고 (임계값 미만, 발열 위험)
  └─ SoC 추정 (쿨롱 카운팅)
30분 주기 데이터 로깅
  └─ Timestamp / 셀 전압 / Relay 상태 / 전체 전류 / SoC
종료 조건 판단
  └─ 경과 시간 초과 / 전압 급락 / SoC ≤ 5% → End
```

---

## 파일 구성

```
BMS_Project/
├── arduino/
│   └── bms_controller.ino     # Arduino C++ (하드웨어 직접 제어)
├── python/
│   └── bms_monitor.py         # Python (PC ↔ Arduino 시리얼 통신)
├── csharp/
│   └── BmsController.cs       # C# (WinForms/WPF 연동 가능)
└── README.md
```

---

## 하드웨어 구성 (공통)

| 부품 | 역할 |
|------|------|
| Arduino (Uno/Mega) | 메인 컨트롤러 |
| ACS712-5A | 전류 센서 (오프셋 2.5V, 감도 0.185V/A) |
| 전압 분압 회로 | 셀 전압 측정 (ADC 입력 범위 조정) |
| 릴레이 모듈 | 방전 회로 ON/OFF 제어 |
| DS3231 RTC | 타임스탬프 (Arduino 버전) |
| SD 카드 모듈 | CSV 로그 저장 (Arduino 버전) |

### Arduino 핀 배치

| 핀 | 용도 |
|----|------|
| D7 | 릴레이 제어 |
| A0 | 전류 센서 (ACS712) |
| A1 | 전압 분압 입력 |
| D10 | SD 카드 CS |
| I2C (A4/A5) | DS3231 RTC |

---

## 버전별 설명

### 1. Arduino C++ (`bms_controller.ino`)

하드웨어에서 직접 전압/전류를 읽고 릴레이를 제어합니다.  
SD 카드에 CSV 로그를 저장하며 RTC로 타임스탬프를 기록합니다.

**필요 라이브러리**
- `RTClib` (DS3231 RTC)
- `SD` (내장)
- `SPI` (내장)

**수정 필요 항목**
```cpp
const float CAPACITY_AH = 3.2;   // 실제 셀 용량으로 변경
const float V_REF       = 5.0;   // 사용 보드 기준 전압 확인
// readVoltage() 내 분압비 조정 필요
// readCurrent() 내 ACS712 모델 감도 확인
```

---

### 2. Python (`bms_monitor.py`)

PC에서 Arduino와 시리얼 통신으로 연결합니다.  
Arduino가 센서값을 `"V:3.25,I:1.20\n"` 형식으로 전송하면  
Python이 수신·분석·릴레이 명령을 내립니다.

**설치**
```bash
pip install pyserial
```

**실행**
```bash
python bms_monitor.py
```

**수정 필요 항목**
```python
PORT        = "COM3"    # 실제 포트로 변경 (Linux: /dev/ttyUSB0)
CAPACITY_AH = 3.2       # 실제 셀 용량으로 변경
MAX_TIME    = 7200      # 종료 기준 시간 (초)
```

**Arduino 시리얼 전송 형식 (Python 수신용)**
```
V:3.25,I:1.20\n
```

---

### 3. C# (`BmsController.cs`)

WinForms / WPF 앱에 통합하거나 콘솔로 단독 실행 가능합니다.  
`SerialPort.DataReceived` 이벤트 기반으로 비동기 수신합니다.

**수정 필요 항목**
```csharp
const string PORT        = "COM3";   // 실제 포트로 변경
const double CAPACITY_AH = 3.2;     // 실제 셀 용량으로 변경
const double MAX_SEC     = 7200;    // 종료 기준 시간 (초)
```

**WinForms 연동 예시**
```csharp
// Form_Load 이벤트에서 초기화
var bms = new BmsController();
bms.Initialize();
```

---

## SoC 계산 방식 (쿨롱 카운팅)

```
SoC(t1) = SoC(t0) - (1/C) × ∫I(t)dt
        ≈ SoC(t0) - (I × Δt) / C
```

| 변수 | 의미 |
|------|------|
| C | 셀 용량 (Ah) |
| I | 측정 전류 (A) |
| Δt | 측정 간격 (hour) |
| SoC | 0.0 ~ 1.0 범위 |

> 초기 SoC는 1.0 (100%)으로 설정됩니다.  
> 누적 오차가 발생할 수 있으므로 실제 운용 시 OCV 기반 보정 추가를 권장합니다.

---

## 데이터 로그 형식 (CSV)

```
Timestamp,Voltage(V),Current(A),Relay,SoC(%)
2025-04-01 10:00:00,3.250,1.200,ON,95.2
2025-04-01 10:30:00,3.180,1.150,ON,88.7
2025-04-01 11:00:00,3.095,0.980,OFF,81.3
```

---

## 종료 조건

| 조건 | 내용 |
|------|------|
| 경과 시간 초과 | 시작 후 2시간 (기본값, 변경 가능) |
| 전압 급락 | 셀 전압 < 3.0V (V_LOW - 0.1V) |
| SoC 고갈 | SoC ≤ 5% |

---

## 주의사항

- 전압 분압비는 사용하는 저항 값에 따라 `readVoltage()` 내에서 직접 수정하세요.
- ACS712 모델(5A / 20A / 30A)에 따라 감도(mV/A) 값이 다릅니다.
- 실제 배터리 연결 전 반드시 더미 부하로 동작을 검증하세요.
- 다셀(멀티셀) 구성 시 셀별 채널 확장이 필요합니다.
