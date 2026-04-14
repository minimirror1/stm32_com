# JSON → Binary Protocol 마이그레이션 분석 리포트

## 1. 현황 요약

| 항목 | 현재 상태 |
|------|----------|
| 논리 프로토콜 | JSON (UTF-8 텍스트) |
| 전송 계층 | XBee DigiMesh (Zigbee) |
| 조각 프로토콜 | Fragment Protocol (30B payload/fragment) |
| 조각 오버헤드 | Header 13B + CRC 2B = **15B/fragment** |
| 최대 메시지 | 10KB |
| 명령 수 | 10종 (ping, pong, move, motion_ctrl, get_motors, get_motor_state, get_files, get_file, save_file, verify_file) |

---

## 2. 현재 JSON 패킷 실측 크기

### 2.1 요청 메시지

| 명령 | JSON 예시 | 바이트 |
|------|----------|--------|
| `ping` | `{"src_id":0,"tar_id":2,"cmd":"ping"}` | **36** |
| `get_motors` | `{"src_id":0,"tar_id":2,"cmd":"get_motors","payload":null}` | **56** |
| `get_motor_state` | `{"src_id":0,"tar_id":2,"cmd":"get_motor_state","payload":null}` | **62** |
| `move` | `{"src_id":0,"tar_id":2,"cmd":"move","payload":{"motorId":1,"pos":2048}}` | **71** |
| `motion_ctrl` (play) | `{"src_id":0,"tar_id":2,"cmd":"motion_ctrl","payload":{"action":"play"}}` | **71** |
| `motion_ctrl` (seek) | `{"src_id":0,"tar_id":2,"cmd":"motion_ctrl","payload":{"action":"seek","time":12.5}}` | **82** |
| `get_file` | `{"src_id":0,"tar_id":2,"cmd":"get_file","payload":{"path":"Setting/MT_ST.TXT"}}` | **79** |
| `save_file` | (가변 — content 크기에 따라) | **200~10000+** |

### 2.2 응답 메시지

| 명령 | 모터 수 | JSON 바이트 |
|------|---------|------------|
| `pong` | — | **80** |
| `get_motors` | 1대 | **224** |
| `get_motors` | 8대 | **~1,162** |
| `get_motors` | 16대 | **~2,100** |
| `get_motor_state` | 1대 | **148** |
| `get_motor_state` | 8대 | **~547** |
| `get_motor_state` | 16대 | **~946** |
| `motion_ctrl` resp | — | **~90** |
| `move` resp | — | **~95** |

> **핵심 문제:** `get_motor_state`는 주기적 폴링 명령으로, 모터 16대 기준 946바이트가 **초당 수회** 왕복한다.
> 이는 Zigbee 대역폭(250kbps, 실효 ~30kbps)에서 심각한 병목이다.

---

## 3. 제안 바이너리 프로토콜 설계

### 3.1 패킷 구조

```
┌──────────────────── Header (5~6 bytes) ────────────────────┐
│ src_id(1) │ tar_id(1) │ cmd(1) │ [status(1)] │ len(2, LE) │
└────────────────────────────────────────────────────────────┘
│                    Payload (0~N bytes)                      │
└────────────────────────────────────────────────────────────┘
```

- **요청 헤더:** 5바이트 (src_id + tar_id + cmd + payload_len)
- **응답 헤더:** 6바이트 (src_id + tar_id + cmd + status + payload_len)
- `status`: 0x00=OK, 0x01=ERROR
- `payload_len`: uint16 LE, 최대 10240

### 3.2 Command Enum (uint8)

| 값 | 명령 | 방향 |
|----|------|------|
| 0x01 | PING | REQ |
| 0x02 | PONG | RESP |
| 0x03 | MOVE | REQ |
| 0x04 | MOTION_CTRL | REQ |
| 0x10 | GET_MOTORS | REQ/RESP |
| 0x11 | GET_MOTOR_STATE | REQ/RESP |
| 0x20 | GET_FILES | REQ/RESP |
| 0x21 | GET_FILE | REQ/RESP |
| 0x22 | SAVE_FILE | REQ/RESP |
| 0x23 | VERIFY_FILE | REQ/RESP |
| 0xE0~0xEF | (예약: 향후 Push 이벤트) | — |
| 0xFE | (예약: 버전 협상) | — |
| 0xFF | ERROR | RESP |

### 3.3 ResponseStatus Enum (uint8)

| 값 | 의미 |
|----|------|
| 0x00 | OK |
| 0x01 | ERROR |

### 3.4 MotorStatus Enum (uint8)

| 값 | 의미 |
|----|------|
| 0x00 | Normal |
| 0x01 | Error |
| 0x02 | Overload |
| 0x03 | Disconnected |

### 3.4b Motor Type Enum (uint8)

| 값 | 의미 |
|----|------|
| 0x00 | Servo |
| 0x01 | DC |
| 0x02 | Stepper |

### 3.5 Action Enum (uint8)

| 값 | 의미 |
|----|------|
| 0x00 | play |
| 0x01 | stop |
| 0x02 | pause |
| 0x03 | seek |

---

## 4. 명령별 바이너리 크기 비교

### 4.1 요청 메시지

| 명령 | JSON (B) | Binary (B) | Binary 구조 | 절감률 |
|------|----------|------------|------------|--------|
| `ping` | 36 | **5** | hdr(5) | **86%** |
| `get_motors` | 56 | **5** | hdr(5) | **91%** |
| `get_motor_state` | 62 | **5** | hdr(5) | **92%** |
| `move` | 71 | **8** | hdr(5)+motor_id(1)+pos(2) | **89%** |
| `motion_ctrl` play | 71 | **6** | hdr(5)+action(1) | **92%** |
| `motion_ctrl` seek | 82 | **10** | hdr(5)+action(1)+time_ms(4) | **88%** |
| `get_file` | 79 | **7+N** | hdr(5)+path_len(2)+path(N) | **~68%** |

### 4.2 응답 메시지

| 명령 | 모터 수 | JSON (B) | Binary (B) | Binary 구조 | 절감률 |
|------|---------|----------|------------|------------|--------|
| `pong` | — | 80 | **6** | hdr(6) | **92%** |
| `move` resp | — | 95 | **8** | hdr(6)+device_id(1)+motor_id(1) | **92%** |
| `motion_ctrl` resp | — | 90 | **8** | hdr(6)+action(1)+device_id(1) | **91%** |
| `get_motors` | 1 | 224 | **24** | hdr(6)+cnt(1)+17×1 | **89%** |
| `get_motors` | 8 | 1,162 | **143** | hdr(6)+cnt(1)+17×8 | **88%** |
| `get_motors` | 16 | 2,100 | **279** | hdr(6)+cnt(1)+17×16 | **87%** |
| `get_motor_state` | 1 | 148 | **13** | hdr(6)+cnt(1)+6×1 | **91%** |
| `get_motor_state` | 8 | 547 | **55** | hdr(6)+cnt(1)+6×8 | **90%** |
| `get_motor_state` | 16 | 946 | **103** | hdr(6)+cnt(1)+6×16 | **89%** |

#### `get_motors` 모터 1대당 Binary 레이아웃 (17 bytes)

```
id(1) + groupId(1) + subId(1) + type(1) + status(1)
+ position(2, uint16 LE) + velocity(2, uint16 LE, ×100 fixed-point)
+ minAngle(2, int16) + maxAngle(2, int16)
+ minRaw(2, uint16) + maxRaw(2, uint16)
```

#### `get_motor_state` 모터 1대당 Binary 레이아웃 (6 bytes)

```
id(1) + position(2, uint16 LE) + velocity(2, uint16 LE, ×100) + status(1)
```

---

## 5. RF 전송 효율 분석 (Fragment Protocol 반영)

조각 프로토콜 파라미터:
- Fragment payload: **30 bytes**
- Fragment overhead: **15 bytes** (header 13B + CRC 2B)
- DONE 메시지: **6 bytes**

**총 RF 바이트 = 조각수 × 15 + 메시지크기 + 6**

| 명령 | JSON B | 조각 수 | JSON RF | Binary B | 조각 수 | Binary RF | RF 절감 |
|------|--------|--------|---------|----------|--------|-----------|---------|
| ping req | 36 | 2 | **72** | 5 | 1 | **26** | **64%** |
| pong resp | 80 | 3 | **131** | 6 | 1 | **27** | **79%** |
| move req | 71 | 3 | **122** | 8 | 1 | **29** | **76%** |
| get_motors 1mot | 224 | 8 | **350** | 24 | 1 | **45** | **87%** |
| get_motors 8mot | 1,162 | 39 | **1,753** | 143 | 5 | **224** | **87%** |
| get_motors 16mot | 2,100 | 70 | **3,156** | 279 | 10 | **435** | **86%** |
| motor_state 1mot | 148 | 5 | **229** | 13 | 1 | **34** | **85%** |
| motor_state 8mot | 547 | 19 | **838** | 55 | 2 | **91** | **89%** |
| **motor_state 16mot** | **946** | **32** | **1,432** | **103** | **4** | **169** | **88%** |
| motion_ctrl | 71 | 3 | **122** | 6 | 1 | **27** | **78%** |

### 5.1 주기적 폴링 시나리오 (가장 중요)

`get_motor_state` 16대 모터 기준, **초당 2회 폴링**:

| 항목 | JSON | Binary | 절감 |
|------|------|--------|------|
| 요청 RF/초 | 2 × 113 = 226 B/s | 2 × 26 = 52 B/s | **77%** |
| 응답 RF/초 | 2 × 1,432 = 2,864 B/s | 2 × 169 = 338 B/s | **88%** |
| **합계 RF/초** | **3,090 B/s** | **390 B/s** | **87%** |
| 조각 수/초 | 2 × (3+32) = 70 | 2 × (1+4) = 10 | **86%** |

> Zigbee 실효 처리량 ~3,000 B/s 기준, JSON은 폴링만으로 대역폭 **~103%** 사용 (포화).
> Binary는 **~13%** 만 사용하여 다른 명령과 공존 가능.

---

## 6. STM32 메모리 영향 분석

### 6.1 Flash (코드 메모리)

| 항목 | JSON | Binary | 차이 |
|------|------|--------|------|
| JSON 파서 라이브러리 (cJSON 등) | ~15–25 KB | 0 KB | **-15~25 KB** |
| 문자열 상수 (키 이름들) | ~2 KB | 0 KB | **-2 KB** |
| JSON 직렬화/역직렬화 코드 | ~3–5 KB | 0 KB | **-3~5 KB** |
| 바이너리 파서/빌더 | 0 KB | ~1–2 KB | +1~2 KB |
| **합계 절감** | | | **-19~30 KB** |

> STM32F1 (64KB Flash) 기준 **30~47% 회수**, STM32F4 (256KB+) 기준 **7~12% 회수**

### 6.2 RAM (런타임 메모리)

| 항목 | JSON | Binary | 차이 |
|------|------|--------|------|
| 수신 버퍼 (JSON 문자열) | ~2 KB | ~0.3 KB | **-1.7 KB** |
| JSON 파스 트리 (cJSON 노드) | ~1–3 KB (동적) | 0 KB | **-1~3 KB** |
| 문자열 비교 임시 버퍼 | ~0.5 KB | 0 KB | **-0.5 KB** |
| 응답 빌드 버퍼 | ~1.5 KB | ~0.3 KB | **-1.2 KB** |
| **합계 절감** | | | **-4.4~6.4 KB** |

> STM32F1 (20KB RAM) 기준 **22~32% 회수**, STM32F4 (192KB+) 기준 **2~3% 회수**

### 6.3 CPU 부하

| 작업 | JSON | Binary |
|------|------|--------|
| 파싱 | 문자열 토큰화 + 트리 구축 (O(n), 분기 다수) | memcpy / struct cast (O(1)) |
| 키 매칭 | strcmp × 필드 수 | switch(cmd) 1회 |
| 숫자 변환 | atoi/atof per field | 불필요 (이미 바이너리) |
| 직렬화 | sprintf/snprintf 다수 호출 | memcpy / 직접 write |
| **상대 CPU 시간** | **1× (baseline)** | **~0.05–0.1×** |

---

## 7. 변경 포인트 (Change Points)

### 7.1 신규 생성 파일

| 파일 | 설명 |
|------|------|
| `Core/Protocol/BinaryProtocol.cs` | 프로토콜 상수, Enum, Header 구조체 |
| `Core/Protocol/BinarySerializer.cs` | 명령별 직렬화 (C# → byte[]) |
| `Core/Protocol/BinaryDeserializer.cs` | 명령별 역직렬화 (byte[] → 모델) |

### 7.2 수정 대상 파일 (PC / WinUI3)

| 파일 | 변경 내용 | 난이도 |
|------|----------|--------|
| `Infrastructure/SerialService.cs` | JSON serialize → `BinarySerializer.Encode()`, JSON parse → `BinaryDeserializer.Decode()` | ★★★ |
| `Infrastructure/VirtualDeviceManager.cs` | 가상 장치가 binary 요청 처리, binary 응답 반환 | ★★★ |
| `UI/ViewModels/DeviceDetailViewModel.cs` | JSON 파싱 로직 → 타입 안전한 binary 모델 수신 | ★★☆ |
| `UI/ViewModels/ScanDialogViewModel.cs` | ping 응답 처리 변경 | ★☆☆ |
| `Infrastructure/SerialTrafficTap.cs` | 로깅을 hex dump + 해석 형태로 변경 | ★★☆ |
| `UI/ViewModels/SerialMonitorViewModel.cs` | 모니터 표시를 hex/해석 뷰로 변경 | ★★☆ |
| `Core/Models/MotorState.cs` | 변경 없음 (모델 자체는 유지) | — |
| `Core/Models/MotorStatePatch.cs` | binary → patch 변환 로직 추가 | ★☆☆ |

### 7.3 수정 대상 (STM32 펌웨어)

| 모듈 | 변경 내용 | 난이도 |
|------|----------|--------|
| JSON 라이브러리 | **완전 제거** (cJSON/ArduinoJson 등) | ★☆☆ |
| 명령 파서 | JSON tokenizer → binary header + switch(cmd) | ★★★ |
| 응답 빌더 | sprintf → struct pack / memcpy | ★★☆ |
| 수신 버퍼 | 문자열 버퍼 → 고정 크기 바이트 버퍼 축소 | ★☆☆ |
| 빌드 시스템 | cJSON 소스/라이브러리 참조 제거 | ★☆☆ |

---

## 8. 고려사항 및 리스크

### 8.1 디버깅 가시성 저하

**문제:** JSON은 시리얼 모니터에서 바로 읽을 수 있지만, binary는 hex dump로만 보인다.

**대안:**
- PC 측 `SerialMonitorViewModel`에 **해석 뷰** 추가 — hex와 함께 사람이 읽을 수 있는 디코딩 결과 표시
- STM32 디버그 빌드에서 UART2로 JSON 로그 병행 출력 (릴리즈에서 제거)
- Wireshark-like 프로토콜 분석기 레이어 구현

### 8.2 엔디안 합의

**STM32는 Little-Endian**, x86/x64 PC도 Little-Endian → **LE로 통일** 권장.
단, XBee API 프레임은 Big-Endian이므로, Fragment Protocol 헤더(기존 BE)와 application payload(LE)의 경계를 명확히 해야 한다.

### 8.3 버전 호환성

**확정된 방식 (binary-protocol-spec §2.0 참조):**

1. **PING 기반 협상:** PC가 연결 시 최초 PING을 보내고, 응답 형식으로 프로토콜을 판별한다.
   - STM32가 binary PONG(`0x02 0x00 0x02 0x00 0x00 0x00`)을 반환 → binary v1.0
   - STM32가 JSON `{"cmd":"pong",...}`을 반환 → legacy JSON
2. **향후 버전 확장:** `cmd = 0xFE`를 `CMD_VERSION_NEGOTIATE`로 예약.
   v2+ 장치는 이 명령에 응답하고, v1 장치는 `ERR_UNKNOWN_CMD`를 반환하여 자연스럽게 버전 분기.

이 방식으로 application 헤더에 별도 버전 바이트 없이도 점진적 전환이 가능하다.

### 8.4 가변 길이 데이터

`get_file`, `save_file`, `get_files`는 **파일 내용**이 payload에 들어가므로 JSON이든 Binary든 크기가 크다.
이 명령들은 binary 헤더 + raw 바이트로 전환해도 **본문 자체는 줄일 수 없다** (텍스트 파일이면 동일).
다만 봉투(envelope) 오버헤드는 줄어든다.

### 8.5 에러 메시지

JSON에서는 `"message": "Unknown command"` 같은 자유 형식 에러 문자열을 보낼 수 있었지만,
binary에서는 **에러 코드 enum + 선택적 디버깅 문자열** 구조로 전달한다 (binary-protocol-spec §4.10 참조).

```
ERROR 응답: hdr(6) + error_code(1, uint8) + msg_len(1, uint8) + msg(0~255 bytes, UTF-8)
```

> `msg_len`은 uint8이므로 최대 255바이트까지 가능하나, STM32 메모리를 고려하여 **≤64B** 권장.
> `msg_len = 0`이면 error_code만 전달하며 메시지는 생략.

### 8.6 테스트 전략

1. **VirtualDeviceManager**를 먼저 binary로 전환하여 PC 단독 테스트
2. PC ↔ 가상장치 binary 통신 검증 완료 후 STM32 펌웨어 전환
3. 프로토콜 버전 분기를 통해 기존 JSON 장치와 신규 binary 장치 공존 가능

---

## 9. 종합 수치 요약

### 9.1 패킷 크기 (가장 빈번한 명령 기준)

| 시나리오 | JSON | Binary | 절감 |
|----------|------|--------|------|
| **ping 왕복** | 116 B | 11 B | **91%** |
| **move 요청** | 71 B | 8 B | **89%** |
| **motor_state 8대 왕복** | 609 B | 60 B | **90%** |
| **motor_state 16대 왕복** | 1,008 B | 108 B | **89%** |
| **get_motors 16대 응답** | 2,100 B | 279 B | **87%** |

### 9.2 RF 전송량 (Fragment 오버헤드 포함)

| 시나리오 | JSON RF | Binary RF | 절감 |
|----------|---------|-----------|------|
| **motor_state 16대 왕복 RF** | 1,545 B | 195 B | **87%** |
| **초당 2회 폴링 16대 RF** | 3,090 B/s | 390 B/s | **87%** |
| **Zigbee 대역폭 점유** | ~103% (포화) | ~13% | — |

### 9.3 STM32 메모리

| 리소스 | 절감량 | 비고 |
|--------|--------|------|
| **Flash** | **-19~30 KB** | JSON 라이브러리 + 문자열 상수 제거 |
| **RAM** | **-4.4~6.4 KB** | 파스 트리 + 버퍼 축소 |
| **CPU** | **~90-95% 감소** | 문자열 파싱 → 직접 struct 읽기 |

### 9.4 전송 지연 개선

| 시나리오 (16대 모터) | JSON 조각 수 | Binary 조각 수 | 조각 절감 |
|---------------------|-------------|---------------|----------|
| motor_state 요청 | 3 | 1 | **67%** |
| motor_state 응답 | 32 | 4 | **88%** |
| 왕복 조각 합계 | 35 | 5 | **86%** |

> 조각 수 감소 → 재전송 확률 감소 → NACK 라운드 감소 → **체감 응답 속도 대폭 개선**

---

## 10. 권장 마이그레이션 순서

```
Phase 1: PC 측 인프라
  ├─ BinaryProtocol.cs (상수, enum, struct)
  ├─ BinarySerializer.cs / BinaryDeserializer.cs
  └─ 단위 테스트

Phase 2: VirtualDeviceManager binary 지원
  ├─ 가상 장치 binary 요청 파싱
  ├─ 가상 장치 binary 응답 생성
  └─ DeviceDetailViewModel 연동 테스트

Phase 3: SerialService 전환
  ├─ SendCommandAsync / SendQueryAsync binary 인코딩
  ├─ HandleMessageReceived binary 디코딩
  └─ SerialMonitor hex 표시 + 해석 뷰

Phase 4: STM32 펌웨어 전환
  ├─ binary 파서 구현
  ├─ 응답 빌더 구현
  ├─ JSON 라이브러리 제거
  └─ 통합 테스트

Phase 5: (선택) 듀얼 프로토콜 지원 제거
  └─ JSON 코드 경로 완전 제거
```
