using System;
using System.IO;
using System.IO.Ports;
using System.Timers;

namespace BMS_Monitor
{
    class BmsController
    {
        // ── 설정 ────────────────────────────────────────
        const string PORT        = "COM3";
        const int    BAUD        = 9600;
        const double V_HIGH      = 3.2;
        const double V_LOW       = 3.1;
        const double CAPACITY_AH = 2.5;
        const double LOG_SEC     = 1800;   // 30분
        const double MAX_SEC     = 7200;   // 2시간

        // ── 상태 변수 ────────────────────────────────────
        SerialPort _port;
        double _soc        = 1.0;
        bool   _relayOn    = false;
        DateTime _startTime;
        DateTime _lastLog;
        DateTime _lastSoc;
        string _logPath;

        // ── 초기화 ───────────────────────────────────────
        public void Initialize()
        {
            // 로그 디렉터리 생성
            Directory.CreateDirectory("logs");
            _logPath = $"logs/bms_log_{DateTime.Now:yyyyMMdd_HHmmss}.csv";

            using var sw = new StreamWriter(_logPath, false);
            sw.WriteLine("Timestamp,Voltage(V),Current(A),Relay,SoC(%)");

            // 시리얼 포트 설정
            _port = new SerialPort(PORT, BAUD) { ReadTimeout = 1000 };
            _port.DataReceived += OnDataReceived;
            _port.Open();

            _startTime = DateTime.Now;
            _lastLog   = DateTime.Now;
            _lastSoc   = DateTime.Now;

            Console.WriteLine("=== BMS 초기화 완료 ===");
        }

        // ── 시리얼 수신 ──────────────────────────────────
        // Arduino가 "V:3.25,I:1.20\n" 형식으로 전송
        void OnDataReceived(object sender, SerialDataReceivedEventArgs e)
        {
            try
            {
                string line = _port.ReadLine().Trim();
                var (voltage, current) = ParseSensors(line);
                ProcessCycle(voltage, current);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[수신 오류] {ex.Message}");
            }
        }

        (double v, double i) ParseSensors(string line)
        {
            double v = 0, i = 0;
            foreach (var part in line.Split(','))
            {
                var kv = part.Split(':');
                if (kv[0] == "V") v = double.Parse(kv[1]);
                if (kv[0] == "I") i = double.Parse(kv[1]);
            }
            return (v, i);
        }

        // ── 메인 사이클 ──────────────────────────────────
        void ProcessCycle(double voltage, double current)
        {
            var now = DateTime.Now;

            // ── 릴레이 제어 ──
            if (voltage >= V_HIGH && !_relayOn)
            {
                SendRelay(true);
                Console.WriteLine($"[방전 시작] V={voltage:F3}V");
            }
            else if (voltage <= V_LOW && _relayOn)
            {
                SendRelay(false);
                Console.WriteLine($"[경고] 셀 전압 임계값 미만: {voltage:F3}V");
            }

            // ── SoC 업데이트 ──
            double dtHour = (now - _lastSoc).TotalHours;
            _soc -= (current * dtHour) / CAPACITY_AH;
            _soc  = Math.Clamp(_soc, 0.0, 1.0);
            _lastSoc = now;

            // ── 30분 주기 로깅 ──
            if ((now - _lastLog).TotalSeconds >= LOG_SEC)
            {
                LogData(voltage, current, now);
                _lastLog = now;
            }

            // ── 종료 조건 ──
            if (CheckTermination(voltage, now))
            {
                SendRelay(false);
                Console.WriteLine("=== BMS 정지 ===");
                _port.Close();
            }
        }

        // ── 릴레이 명령 전송 ─────────────────────────────
        void SendRelay(bool on)
        {
            _relayOn = on;
            _port.WriteLine(on ? "RELAY_ON" : "RELAY_OFF");
        }

        // ── CSV 로깅 ─────────────────────────────────────
        void LogData(double voltage, double current, DateTime now)
        {
            using var sw = new StreamWriter(_logPath, append: true);
            sw.WriteLine($"{now:yyyy-MM-dd HH:mm:ss}," +
                         $"{voltage:F3}," +
                         $"{current:F3}," +
                         $"{(_relayOn ? "ON" : "OFF")}," +
                         $"{_soc * 100:F1}");
            Console.WriteLine($"[로그] 저장 완료 | SoC={_soc*100:F1}%");
        }

        // ── 종료 조건 ────────────────────────────────────
        bool CheckTermination(double voltage, DateTime now)
        {
            double elapsed = (now - _startTime).TotalSeconds;
            if (elapsed >= MAX_SEC)      { Console.WriteLine("[종료] 시간 초과");   return true; }
            if (voltage < V_LOW - 0.1)  { Console.WriteLine("[종료] 전압 급락");   return true; }
            if (_soc <= 0.05)           { Console.WriteLine("[종료] SoC 5% 이하"); return true; }
            return false;
        }
    }

    class Program
    {
        static void Main()
        {
            var bms = new BmsController();
            bms.Initialize();
            Console.WriteLine("실행 중... 종료하려면 Ctrl+C");
            Console.ReadLine();
        }
    }
}