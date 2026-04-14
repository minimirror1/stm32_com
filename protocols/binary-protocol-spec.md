# Binary Protocol Specification v1.0

PC (WinUI3, C#) ↔ STM32 (C) 양측 구현의 단일 기준 문서.
기존 JSON 프로토콜이 전달하던 **모든 정보를 그대로 유지**하며, 인코딩만 binary packed struct로 변경한다.

---

## 1. 설계 원칙

| 원칙 | 설명 |
|------|------|
| **정보 무손실** | JSON이 전달하던 모든 필드를 binary에도 포함한다. 생략/축약 없음. |
| **Byte Order** | Application payload는 **Little-Endian** (STM32 ARM Cortex-M, x86 PC 모두 LE). |
| **Packed** | 모든 struct는 **1-byte aligned, no padding**. C: `#pragma pack(push,1)`, C#: `Pack=1`. |
| **하위 계층 불변** | Fragment Protocol, XBee API 프레임은 기존 그대로 유지. 변경은 application payload 레벨만. |
| **Fixed-point** | `velocity`는 `uint16, ×100` fixed-point. JSON `0.50` → binary `50`. |
| **String → Enum** | 고정 집합 문자열(`type`, `status`, `action`)은 uint8 enum으로 매핑. |
| **인코딩 반올림** | C# 모델의 `double` 값을 binary 정수로 인코딩 시 **반올림 후 클램프** 적용. `(uint16)Math.Clamp(Math.Round(value), 0, 65535)`. 소수점 position raw 값은 실제 발생하지 않는 것이 권장되나, 발생 시 가장 가까운 정수로 처리한다. |

---

## 2. 공통 헤더

### 2.0 프로토콜 버전 식별

현재 프로토콜은 **v1.0** 이다. 헤더에 별도 버전 바이트를 두지 않는 대신, 아래 방식으로 버전을 관리한다.

| 방식 | 설명 |
|------|------|
| **PING 기반 협상** | PC가 연결 시 최초 PING을 보내고, 응답 여부 및 형식으로 binary/JSON을 판별한다. STM32가 binary PONG(`0x02 0x00 0x02 0x00 0x00 0x00`)을 반환하면 binary v1.0, JSON `{"cmd":"pong",...}`을 반환하면 legacy JSON으로 판단한다. |
| **하위 호환** | 향후 v2.0이 필요할 경우, 현재 미사용 `cmd` 값 `0xFE`를 `CMD_VERSION_NEGOTIATE`로 예약한다. v2+ 장치는 이 명령에 응답하고, v1 장치는 `ERR_UNKNOWN_CMD`를 반환하여 자연스럽게 버전 분기된다. |
| **예약 값** | `cmd = 0xFE` — 향후 버전 협상용으로 예약. 현재 구현에서 사용 금지. |

### 2.1 요청 헤더 (Request Header) — 5 bytes

```
Offset  Size  Field       Type     Description
──────  ────  ──────────  ───────  ─────────────────────────────
0       1     src_id      uint8    송신자 ID (PC=0)
1       1     tar_id      uint8    수신자 ID (장치 ID, 255=broadcast)
2       1     cmd         uint8    Command enum (§3.1)
3       2     payload_len uint16   payload 바이트 수 (LE). 0이면 payload 없음.
──────  ────  ──────────  ───────  ─────────────────────────────
Total: 5 bytes
```

JSON 대응:
```
JSON                          → Binary
─────────────────────────────   ─────────────────
"src_id": 0                   → src_id = 0x00
"tar_id": 2                   → tar_id = 0x02
"cmd": "move"                 → cmd = 0x03
"payload": {...}              → payload_len + payload bytes
```

> **Broadcast 규칙:** `tar_id = 0xFF (255)` 는 브로드캐스트이며, 수신 장치는 **응답을 반환하지 않는다**.
> RF 충돌 방지를 위한 규칙으로, JSON과 binary 모두 동일하게 적용된다.

> **통신 방향 모델:**
> 현재 프로토콜은 **PC 주도 요청-응답 (Request-Response)** 모델이다.
> - PC가 `RequestHeader`를 보내고, STM32가 `ResponseHeader`로 응답한다.
> - STM32가 먼저 메시지를 보내는 (unsolicited push) 시나리오는 **v1.0에서 지원하지 않는다**.
>
> **향후 Push 확장 예약:**
> STM32→PC 비요청 이벤트(예: 센서 알람, 모터 장애 알림)가 필요해지면,
> `cmd = 0xE0~0xEF` 범위를 **EVENT** 명령 블록으로 예약한다.
> EVENT 패킷은 `ResponseHeader` 형식을 재사용하되, `status` 필드를 이벤트 서브타입으로 재해석한다.
> 현재 v1.0에서는 이 범위의 cmd 값을 **사용하지 않으며 수신 시 무시**한다.

### 2.2 응답 헤더 (Response Header) — 6 bytes

```
Offset  Size  Field       Type     Description
──────  ────  ──────────  ───────  ─────────────────────────────
0       1     src_id      uint8    응답 장치 ID
1       1     tar_id      uint8    요청자 ID (보통 0)
2       1     cmd         uint8    Command enum (§3.1)
3       1     status      uint8    ResponseStatus enum (§3.2)
4       2     payload_len uint16   payload 바이트 수 (LE)
──────  ────  ──────────  ───────  ─────────────────────────────
Total: 6 bytes
```

JSON 대응:
```
JSON                          → Binary
─────────────────────────────   ─────────────────
"src_id": 2                   → src_id = 0x02
"tar_id": 0                   → tar_id = 0x00
"cmd": "get_motors"           → cmd = 0x10
"status": "ok"                → status = 0x00
"payload": {...}              → payload_len + payload bytes
```

> **참고:** JSON에서 오류 응답은 `"cmd":"error"` 였으나, binary에서는 **원래 cmd를 유지**하고 `status` 필드로 오류를 표시한다. 알 수 없는 cmd 등 cmd 자체를 특정할 수 없는 경우에만 `cmd = 0xFF (ERROR)` 사용.

---

## 3. Enum 정의

> **미지 Enum 값 처리 규칙 (양측 공통):**
>
> | Enum | 미지 값 수신 시 처리 |
> |------|---------------------|
> | **Command** | `status = STATUS_ERROR`, `error_code = ERR_UNKNOWN_CMD`로 응답. 패킷의 나머지 payload는 파싱하지 않는다. |
> | **ResponseStatus** | 정의되지 않은 status 값은 `STATUS_ERROR`로 간주하고, payload를 `ErrorPayload`로 해석 시도한다. |
> | **MotorType** | 알 수 없는 type 값은 UI에서 `"Unknown"` 으로 표시하고, 해당 모터의 나머지 필드는 정상 파싱한다. 에러 발생시키지 않음. |
> | **MotorStatus** | MotorType과 동일. `"Unknown"` 상태로 표시. |
> | **MotionAction** | `status = STATUS_ERROR`, `error_code = ERR_INVALID_PARAM`으로 응답. |
> | **ErrorCode** | 알 수 없는 error_code는 `ERR_UNKNOWN (0x00)` 과 동일하게 처리. message 필드가 있으면 그대로 표시. |
>
> 이 규칙은 한쪽이 새 enum 값을 추가해도 상대방이 크래시하지 않는 **전방 호환성**을 보장한다.

### 3.1 Command (uint8) — `cmd` 필드

| 값 | 이름 | JSON 문자열 | 방향 |
|----|------|-------------|------|
| `0x01` | CMD_PING | `"ping"` | REQ |
| `0x02` | CMD_PONG | `"pong"` | RESP |
| `0x03` | CMD_MOVE | `"move"` | REQ/RESP |
| `0x04` | CMD_MOTION_CTRL | `"motion_ctrl"` | REQ/RESP |
| `0x10` | CMD_GET_MOTORS | `"get_motors"` | REQ/RESP |
| `0x11` | CMD_GET_MOTOR_STATE | `"get_motor_state"` | REQ/RESP |
| `0x20` | CMD_GET_FILES | `"get_files"` | REQ/RESP |
| `0x21` | CMD_GET_FILE | `"get_file"` | REQ/RESP |
| `0x22` | CMD_SAVE_FILE | `"save_file"` | REQ/RESP |
| `0x23` | CMD_VERIFY_FILE | `"verify_file"` | REQ/RESP |
| `0xFF` | CMD_ERROR | `"error"` | RESP only |

> **예약 범위 (현재 사용 금지):**
> - `0xE0~0xEF` — 향후 STM32→PC 비요청 이벤트(Push) 명령 블록 (§2.1 참조)
> - `0xFE` — 프로토콜 버전 협상용 (§2.0 참조)

### 3.2 ResponseStatus (uint8) — 응답 헤더 `status` 필드

| 값 | 이름 | JSON 문자열 |
|----|------|-------------|
| `0x00` | STATUS_OK | `"ok"` |
| `0x01` | STATUS_ERROR | `"error"` |

### 3.3 MotorType (uint8)

| 값 | 이름 | JSON 문자열 |
|----|------|-------------|
| `0x00` | MOTOR_SERVO | `"Servo"` |
| `0x01` | MOTOR_DC | `"DC"` |
| `0x02` | MOTOR_STEPPER | `"Stepper"` |

### 3.4 MotorStatus (uint8)

| 값 | 이름 | JSON 문자열 |
|----|------|-------------|
| `0x00` | MSTAT_NORMAL | `"Normal"` |
| `0x01` | MSTAT_ERROR | `"Error"` |
| `0x02` | MSTAT_OVERLOAD | `"Overload"` |
| `0x03` | MSTAT_DISCONNECTED | `"Disconnected"` |

### 3.5 MotionAction (uint8)

| 값 | 이름 | JSON 문자열 |
|----|------|-------------|
| `0x00` | ACTION_PLAY | `"play"` |
| `0x01` | ACTION_STOP | `"stop"` |
| `0x02` | ACTION_PAUSE | `"pause"` |
| `0x03` | ACTION_SEEK | `"seek"` |

### 3.6 ErrorCode (uint8) — ERROR payload용

| 값 | 이름 | JSON `message` 예시 |
|----|------|---------------------|
| `0x00` | ERR_UNKNOWN | (기본/미분류) |
| `0x01` | ERR_INVALID_INPUT | `"Invalid JSON"` |
| `0x02` | ERR_UNKNOWN_CMD | `"Unknown command: ..."` |
| `0x03` | ERR_INVALID_PARAM | `"Invalid path"` 등 |
| `0x04` | ERR_FILE_NOT_FOUND | `"File not found"` |
| `0x05` | ERR_MOTOR_NOT_FOUND | 모터 ID 없음 |

---

## 4. 명령별 Payload 상세

> 각 명령마다 **JSON 원본 → Binary 매핑**을 1:1로 명시한다.

---

### 4.1 PING (0x01) / PONG (0x02)

#### Request

```
[RequestHeader: 5B]  payload_len = 0
```

JSON 원본:
```json
{ "src_id": 0, "tar_id": 2, "cmd": "ping" }
```

> **[JSON 구현 주의]** PC `SerialService`에서 `SendQueryAsync("ping")` 경로는 `"payload":null`이 포함된 JSON을 보내지만,
> `PingDeviceAsync` 직접 경로는 payload 키가 없는 JSON을 보낸다. Binary에서는 `payload_len=0`으로 **통일**되므로 이 차이는 사라진다.

Binary (5 bytes):
```
00 02 01 00 00
│  │  │  └──┘ payload_len = 0
│  │  └ cmd = PING
│  └ tar_id = 2
└ src_id = 0
```

#### Response

```
[ResponseHeader: 6B]  cmd=0x02(PONG), status=0x00, payload_len = 0
```

JSON 원본:
```json
{ "src_id": 2, "tar_id": 0, "cmd": "pong", "status": "ok", "payload": { "message": "pong" } }
```

Binary (6 bytes):
```
02 00 02 00 00 00
│  │  │  │  └──┘ payload_len = 0
│  │  │  └ status = OK
│  │  └ cmd = PONG
│  └ tar_id = 0
└ src_id = 2
```

> **매핑 결정:** JSON의 `payload.message = "pong"` 은 cmd=PONG 자체로 동일한 정보이므로 payload 불필요. PC 측에서 `cmd == PONG && status == OK` 로 동일하게 판단.

---

### 4.2 MOVE (0x03)

#### Request

JSON 원본:
```json
{ "src_id": 0, "tar_id": 2, "cmd": "move", "payload": { "motorId": 1, "pos": 2048 } }
```

Payload (3 bytes):
```
Offset  Size  Field     Type    JSON 필드        비고
──────  ────  ────────  ──────  ──────────────   ────
0       1     motor_id  uint8   payload.motorId  모터 ID (1~255)
1       2     pos       uint16  payload.pos      raw position (LE)
──────  ────  ────────  ──────  ──────────────   ────
Total payload: 3 bytes
```

전체 패킷 (5+3 = 8 bytes):
```
00 02 03 03 00 01 00 08
│  │  │  └──┘  │  └──┘ pos = 2048 (0x0800 LE)
│  │  │  │     └ motor_id = 1
│  │  │  └ payload_len = 3
│  │  └ cmd = MOVE
│  └ tar_id = 2
└ src_id = 0
```

#### Response

JSON 원본:
```json
{ "src_id": 2, "tar_id": 0, "cmd": "move", "status": "ok",
  "payload": { "status": "moved", "deviceId": 2, "motorId": 1 } }
```

Payload (2 bytes):
```
Offset  Size  Field     Type    JSON 필드            비고
──────  ────  ────────  ──────  ──────────────────   ────
0       1     device_id uint8   payload.deviceId     응답 헤더 src_id와 동일값이지만 JSON 호환 유지
1       1     motor_id  uint8   payload.motorId      이동된 모터 ID
──────  ────  ────────  ──────  ──────────────────   ────
Total payload: 2 bytes
```

> **매핑:** JSON `payload.status = "moved"` → binary에서는 `ResponseHeader.cmd == MOVE && status == OK` 로 동일 의미. 별도 필드 불필요.

---

### 4.3 MOTION_CTRL (0x04)

#### Request

JSON 원본:
```json
{ "src_id": 0, "tar_id": 2, "cmd": "motion_ctrl",
  "payload": { "action": "seek", "time": 12.5 } }
```

Payload (5 bytes, 가변):
```
Offset  Size  Field     Type    JSON 필드         비고
──────  ────  ────────  ──────  ───────────────   ────
0       1     action    uint8   payload.action    MotionAction enum (§3.5)
1       4     time_ms   uint32  payload.time      밀리초 단위 (LE). action=SEEK일 때만 유효.
──────  ────  ────────  ──────  ───────────────   ────
```

- `action ≠ SEEK` 인 경우: `payload_len = 1` (time_ms 생략)
- `action == SEEK` 인 경우: `payload_len = 5`

> **[구현 주의] 가변 길이 접근:** STM32에서 `(MotionCtrlReqPayload*)buf`로 캐스팅하면 action ≠ SEEK 시에도 struct의 `time_ms` 필드에 접근할 수 있지만, 그 4바이트는 **유효하지 않은 쓰레기값**이다.
> 반드시 `hdr.payload_len`을 먼저 확인하고, `payload_len >= 5`일 때만 `time_ms`를 읽어야 한다.
>
> ```c
> MotionCtrlReqPayload *p = (MotionCtrlReqPayload *)(buf + sizeof(ReqHeader));
> uint8_t action = p->action;
> uint32_t time_ms = (hdr.payload_len >= 5) ? p->time_ms : 0;
> ```

> **단위 변환:** JSON `time`은 초(float). Binary는 `time_ms = (uint32)(time × 1000)`.
> 예: `12.5초` → `12500` (0x000030D4 LE: `D4 30 00 00`)

play 요청 예시 (5+1 = 6 bytes):
```
00 02 04 01 00 00
                  └ action = PLAY
```

seek 요청 예시 (5+5 = 10 bytes):
```
00 02 04 05 00 03 D4 30 00 00
                  │  └────────┘ time_ms = 12500 LE
                  └ action = SEEK
```

#### Response

JSON 원본:
```json
{ "src_id": 2, "tar_id": 0, "cmd": "motion_ctrl", "status": "ok",
  "payload": { "status": "executed", "action": "seek", "deviceId": 2 } }
```

Payload (2 bytes):
```
Offset  Size  Field     Type    JSON 필드            비고
──────  ────  ────────  ──────  ──────────────────   ────
0       1     action    uint8   payload.action       echo back (MotionAction enum)
1       1     device_id uint8   payload.deviceId     장치 ID
──────  ────  ────────  ──────  ──────────────────   ────
Total payload: 2 bytes
```

> **매핑:** JSON `payload.status = "executed"` → `ResponseHeader.cmd == MOTION_CTRL && status == OK`

---

### 4.4 GET_MOTORS (0x10)

#### Request

```
[RequestHeader: 5B]  payload_len = 0
```

JSON 원본:
```json
{ "src_id": 0, "tar_id": 2, "cmd": "get_motors", "payload": null }
```

#### Response

JSON 원본:
```json
{ "src_id": 2, "tar_id": 0, "cmd": "get_motors", "status": "ok",
  "payload": { "motors": [
    { "id": 1, "groupId": 1, "subId": 1, "type": "Servo", "status": "Normal",
      "position": 2048, "velocity": 0.5,
      "minAngle": 0, "maxAngle": 180, "minRaw": 0, "maxRaw": 3072 }
  ] } }
```

Payload 구조:
```
Offset  Size    Field        Type     JSON 필드
──────  ──────  ───────────  ───────  ──────────
0       1       motor_count  uint8    motors.length
1       N×17    motors[]     struct   motors[i]  (아래 MotorInfoEntry 참조)
```

**MotorInfoEntry** (17 bytes / 모터):
```
Offset  Size  Field      Type    JSON 필드           비고
──────  ────  ─────────  ──────  ──────────────────  ────
0       1     id         uint8   motors[i].id
1       1     group_id   uint8   motors[i].groupId
2       1     sub_id     uint8   motors[i].subId
3       1     type       uint8   motors[i].type      MotorType enum (§3.3)
4       1     status     uint8   motors[i].status    MotorStatus enum (§3.4)
5       2     position   uint16  motors[i].position  raw value (LE)
7       2     velocity   uint16  motors[i].velocity  ×100 fixed-point (LE)
9       2     min_angle  int16   motors[i].minAngle  ×10 fixed-point (LE). -90 → -900
11      2     max_angle  int16   motors[i].maxAngle  ×10 fixed-point (LE). 180 → 1800
13      2     min_raw    uint16  motors[i].minRaw    (LE)
15      2     max_raw    uint16  motors[i].maxRaw    (LE)
──────  ────  ─────────  ──────  ──────────────────  ────
Total: 17 bytes
```

> **velocity 변환:** JSON `0.5` → Binary `50`. 복원: `velocity / 100.0`
> **angle 변환:** JSON `180` → Binary `1800`. 복원: `angle / 10.0`. 소수 1자리 정밀도 보장.
> **angle 변환:** JSON `-90` → Binary `-900` (int16). 음수 각도 지원.

3대 모터 예시, payload_len = 1 + 3×17 = **52 bytes**:
```
[ResponseHeader 6B] payload_len=52
03                                                   ← motor_count = 3
01 01 01 00 00 00 08 32 00 00 00 08 07 00 00 00 0C  ← motor[0]
│  │  │  │  │  └──┘  └──┘  └──┘  └──┘  └──┘  └──┘
│  │  │  │  │  pos   vel   minA  maxA  minR  maxR
│  │  │  │  └ status=Normal(0x00)
│  │  │  └ type=Servo(0x00)
│  │  └ sub_id=1
│  └ group_id=1
└ id=1       (pos=2048, vel=0.50, angle 0°~180°, raw 0~3072)
02 01 02 01 01 00 03 64 00 00 00 08 07 00 00 00 0C  ← motor[1]
                                                     (id=2, DC(0x01), Error(0x01), pos=768, vel=1.00)
03 02 01 02 00 00 00 14 00 7C FC 84 03 00 00 FF 0F  ← motor[2]
                                                     (id=3, Stepper(0x02), Normal, pos=0, vel=0.20, angle -90°~90°)
```

필드별 계산 근거 (VirtualDeviceManager 기준):

| 모터 | 필드 | 값 | 바이너리 | 비고 |
|------|------|-----|---------|------|
| 1 | position | 2048 | `00 08` | 0x0800 LE |
| 1 | velocity | 0.5×100=50 | `32 00` | 0x0032 LE |
| 1 | max_angle | 180×10=1800 | `08 07` | 0x0708 LE |
| 1 | max_raw | 3072 | `00 0C` | 0x0C00 LE |
| 2 | position | 768 | `00 03` | 0x0300 LE |
| 2 | velocity | 1.0×100=100 | `64 00` | 0x0064 LE |
| 3 | velocity | 0.2×100=20 | `14 00` | 0x0014 LE |
| 3 | min_angle | -90×10=-900 | `7C FC` | 0xFC7C LE (int16 2의 보수) |
| 3 | max_angle | 90×10=900 | `84 03` | 0x0384 LE |
| 3 | max_raw | 4095 | `FF 0F` | 0x0FFF LE |

---

### 4.5 GET_MOTOR_STATE (0x11)

#### Request

```
[RequestHeader: 5B]  payload_len = 0
```

#### Response

JSON 원본:
```json
{ "src_id": 2, "tar_id": 0, "cmd": "get_motor_state", "status": "ok",
  "payload": { "motors": [
    { "id": 1, "position": 2080, "velocity": 0.5, "status": "Normal" }
  ] } }
```

Payload 구조:
```
Offset  Size    Field        Type     JSON 필드
──────  ──────  ───────────  ───────  ──────────
0       1       motor_count  uint8    motors.length
1       N×6     motors[]     struct   motors[i]  (아래 MotorStateEntry 참조)
```

**MotorStateEntry** (6 bytes / 모터):
```
Offset  Size  Field     Type    JSON 필드              비고
──────  ────  ────────  ──────  ────────────────────   ────
0       1     id        uint8   motors[i].id
1       2     position  uint16  motors[i].position     raw value (LE)
3       2     velocity  uint16  motors[i].velocity     ×100 fixed-point (LE)
5       1     status    uint8   motors[i].status       MotorStatus enum (§3.4)
──────  ────  ────────  ──────  ────────────────────   ────
Total: 6 bytes
```

3대 모터 예시, payload_len = 1 + 3×6 = **19 bytes**:
```
[ResponseHeader 6B] payload_len=19
03                        ← motor_count = 3
01 20 08 32 00 00         ← motor[0]: id=1, pos=2080, vel=50(0.50), status=Normal
02 00 03 64 00 01         ← motor[1]: id=2, pos=768,  vel=100(1.00), status=Error
03 00 00 14 00 00         ← motor[2]: id=3, pos=0,    vel=20(0.20),  status=Normal
```

> **부분 응답 지원:** `motor_count`가 전체 모터 수보다 적을 수 있다 (JSON과 동일한 동작).
> PC 측은 수신된 모터만 갱신하고, 나머지는 기존 상태를 유지한다.

---

### 4.6 GET_FILES (0x20)

#### Request

```
[RequestHeader: 5B]  payload_len = 0
```

#### Response

JSON 원본 구조:
```json
{ "src_id": 2, "tar_id": 0, "cmd": "get_files", "status": "ok",
  "payload": [
    { "Name": "Error", "Path": "Error", "isDirectory": true, "Size": 0,
      "Children": [
        { "Name": "err_lv.ini", "Path": "Error/err_lv.ini", "isDirectory": false, "Size": 23, "Children": [] }
      ] }
  ] }
```

> 파일 트리는 문자열(이름, 경로)이 대부분이라 binary 전환 시에도 문자열은 그대로 전달.
> JSON의 중첩 `Children` 트리 → **flat list + parent_index** 방식으로 평탄화.

Payload 구조:
```
Offset  Size    Field        Type     Description
──────  ──────  ───────────  ───────  ──────────
0       2       entry_count  uint16   전체 항목 수 (LE)
2       N       entries[]    struct   FileEntry 반복 (아래 참조)
```

**FileEntry** (가변 길이):
```
Offset  Size       Field          Type     JSON 필드           비고
──────  ─────────  ─────────────  ───────  ──────────────────  ────
0       1          flags          uint8    isDirectory         bit0: 1=dir, 0=file
1       2          parent_index   int16    ParentIndex         -1=루트 (LE)
3       4          size           uint32   Size                파일 크기 (LE). dir이면 0.
7       1          name_len       uint8    Name.length         이름 바이트 수 (UTF-8)
8       name_len   name           bytes    Name                UTF-8 인코딩
8+NL    2          path_len       uint16   Path.length         경로 바이트 수 (UTF-8, LE)
10+NL   path_len   path           bytes    Path                UTF-8 인코딩
```

> **size 타입:** C# `FileSystemItem.Size`는 `long`(64bit)이지만 binary는 `uint32`(32bit).
> Fragment Protocol 최대 메시지 크기가 10KB이므로 실질적으로 uint32로 충분하다.
> 단, 이론적으로 4GB 이상 파일은 표현 불가 — 임베디드 파일시스템 맥락에서 문제 없음.

> **미전송 필드:** `FileSystemItem`의 `Depth`, `Icon`, `Children` 은 **클라이언트 전용 UI 필드**이며 binary에서 전송하지 않는다.
> - `Depth`: 수신 후 `parent_index` 트리를 순회하여 클라이언트가 계산
> - `Icon`: UI 렌더링용 계산 프로퍼티, 전송 불필요
> - `Children`: flat list + `parent_index` 구조로 대체되므로 미사용

> **JSON 대비 절감 포인트:** JSON 키 이름(`"Name"`, `"Path"`, `"isDirectory"`, `"Size"`, `"Children"`)과 중첩 구조 오버헤드 제거.

> **[참고] 가상 장치 호환:** 현재 `VirtualDeviceManager`는 JSON으로 **중첩 `Children` 트리**를 반환하나,
> binary 전환 시 flat + `parent_index` 방식으로 통일된다. 이 과정에서 기존 UI `BuildFileTree`의
> 중첩↔평면 불일치 버그가 자연 해소된다.

**트리 복원:** PC 측에서 `parent_index` 기반으로 트리 재구축:

평탄화 순서:
1. DFS(깊이 우선) 순회로 모든 노드를 방문
2. 각 노드에 0-based index 부여
3. 자식 노드의 `parent_index` = 부모 노드의 index

---

### 4.7 GET_FILE (0x21)

#### Request

JSON 원본:
```json
{ "src_id": 0, "tar_id": 2, "cmd": "get_file", "payload": { "path": "Setting/MT_ST.TXT" } }
```

Payload:
```
Offset  Size       Field     Type     JSON 필드      비고
──────  ─────────  ────────  ───────  ────────────   ────
0       2          path_len  uint16   -              경로 바이트 수 (LE)
2       path_len   path      bytes    payload.path   UTF-8
```

#### Response (성공)

JSON 원본:
```json
{ "src_id": 2, "tar_id": 0, "cmd": "get_file", "status": "ok",
  "payload": { "path": "Setting/MT_ST.TXT", "content": "Value=1100" } }
```

Payload:
```
Offset          Size          Field        Type    JSON 필드          비고
──────          ─────────     ───────────  ──────  ────────────────   ────
0               2             path_len     uint16  -                  (LE)
2               path_len      path         bytes   payload.path       UTF-8
2+PL            2             content_len  uint16  -                  (LE)
4+PL            content_len   content      bytes   payload.content    UTF-8 raw
```

---

### 4.8 SAVE_FILE (0x22)

#### Request

JSON 원본:
```json
{ "src_id": 0, "tar_id": 2, "cmd": "save_file",
  "payload": { "path": "Setting/MT_ST.TXT", "content": "Value=1200" } }
```

Payload:
```
Offset          Size          Field        Type    JSON 필드          비고
──────          ─────────     ───────────  ──────  ────────────────   ────
0               2             path_len     uint16  -                  (LE)
2               path_len      path         bytes   payload.path       UTF-8
2+PL            2             content_len  uint16  -                  (LE)
4+PL            content_len   content      bytes   payload.content    UTF-8 raw
```

#### Response (성공)

JSON 원본:
```json
{ "src_id": 2, "tar_id": 0, "cmd": "save_file", "status": "ok",
  "payload": { "status": "saved", "path": "Setting/MT_ST.TXT" } }
```

Payload:
```
Offset     Size       Field     Type    JSON 필드       비고
──────     ─────────  ────────  ──────  ──────────────  ────
0          2          path_len  uint16  -               (LE)
2          path_len   path      bytes   payload.path    UTF-8
```

> **매핑:** JSON `payload.status = "saved"` → `ResponseHeader.cmd == SAVE_FILE && status == OK`

---

### 4.9 VERIFY_FILE (0x23)

#### Request

JSON 원본:
```json
{ "src_id": 0, "tar_id": 2, "cmd": "verify_file",
  "payload": { "path": "Setting/MT_ST.TXT", "content": "Value=1100" } }
```

Payload: SAVE_FILE 요청과 동일 구조.
```
Offset          Size          Field        Type    JSON 필드
──────          ─────────     ───────────  ──────  ────────────────
0               2             path_len     uint16  -
2               path_len      path         bytes   payload.path
2+PL            2             content_len  uint16  -
4+PL            content_len   content      bytes   payload.content
```

#### Response (성공)

JSON 원본:
```json
{ "src_id": 2, "tar_id": 0, "cmd": "verify_file", "status": "ok",
  "payload": { "match": true } }
```

Payload (1 byte):
```
Offset  Size  Field  Type   JSON 필드       비고
──────  ────  ─────  ─────  ──────────────  ────
0       1     match  uint8  payload.match   0x01=true, 0x00=false
```

---

### 4.10 ERROR (0xFF)

JSON 원본:
```json
{ "src_id": 2, "tar_id": 0, "cmd": "error", "status": "error",
  "message": "Unknown command: foo" }
```

> **JSON vs Binary 차이:**
> - **JSON 구현**: 오류 시 항상 `"cmd": "error"` (원래 명령을 폐기). `VirtualDeviceManager.ErrorResponse`가 이 방식.
> - **Binary**: 원래 cmd를 헤더에 유지하고 `status = ERROR`로 구분. 이는 **JSON보다 더 많은 정보**를 담는 확장이다.
>
> Binary로 전환 시, STM32/PC 양측 파서는 `status` 필드를 먼저 확인하고,
> `status == ERROR`이면 payload를 `ErrorPayload`로 해석해야 한다.

#### 방식 A: 원래 cmd를 아는 경우 (권장)
응답 헤더의 `cmd`에 원래 명령을 넣고, `status = STATUS_ERROR (0x01)`.

```
[ResponseHeader: cmd=원래cmd, status=0x01]
Payload:
Offset  Size      Field      Type    비고
──────  ────────  ─────────  ──────  ──────────────────────────────
0       1         error_code uint8   ErrorCode enum (§3.6)
1       1         msg_len    uint8   메시지 바이트 수 (0~255, 0이면 생략). STM32 권장 ≤64B.
2       msg_len   message    bytes   UTF-8 디버깅 메시지 (선택)
```

#### 방식 B: cmd를 특정할 수 없는 경우
`cmd = 0xFF (CMD_ERROR)`, payload 동일.

> **정보 보존:** JSON의 자유 형식 `message` 문자열은 `msg_len + message` 로 그대로 전달 가능.
> STM32에서 메모리가 부족하면 `msg_len = 0`으로 error_code만 전달해도 됨.
> ErrorCode 값은 §3.6 참조.

---

## 5. JSON ↔ Binary 필드 매핑 총괄표

| JSON 필드 | JSON 타입 | Binary 필드 | Binary 타입 | 위치 | 변환 규칙 |
|-----------|----------|-------------|-------------|------|----------|
| `src_id` | number | `src_id` | uint8 | Header[0] | 직접 |
| `tar_id` | number | `tar_id` | uint8 | Header[1] | 직접 |
| `cmd` | string | `cmd` | uint8 | Header[2] | §3.1 enum |
| `status` (봉투) | string | `status` | uint8 | RespHeader[3] | §3.2 enum |
| `payload` | object | payload bytes | - | Header 뒤 | 명령별 struct |
| `payload.motorId` | number | `motor_id` | uint8 | Payload | 직접 |
| `payload.pos` | number | `pos` | uint16 LE | Payload | §8 Round+Clamp (정수 raw이므로 실질적 직접, 소수 발생 시 반올림) |
| `payload.action` | string | `action` | uint8 | Payload | §3.5 enum |
| `payload.time` | number(초) | `time_ms` | uint32 LE | Payload | ×1000 |
| `motors[].id` | number | `id` | uint8 | Entry | 직접 |
| `motors[].groupId` | number | `group_id` | uint8 | Entry | 직접 |
| `motors[].subId` | number | `sub_id` | uint8 | Entry | 직접 |
| `motors[].type` | string | `type` | uint8 | Entry | §3.3 enum |
| `motors[].status` | string | `status` | uint8 | Entry | §3.4 enum |
| `motors[].position` | number | `position` | uint16 LE | Entry | §8 Round+Clamp (정수 raw) |
| `motors[].velocity` | number | `velocity` | uint16 LE | Entry | ×100 |
| `motors[].minAngle` | number | `min_angle` | int16 LE | Entry | ×10 |
| `motors[].maxAngle` | number | `max_angle` | int16 LE | Entry | ×10 |
| `motors[].minRaw` | number | `min_raw` | uint16 LE | Entry | §8 Round+Clamp (정수 raw) |
| `motors[].maxRaw` | number | `max_raw` | uint16 LE | Entry | §8 Round+Clamp (정수 raw) |
| `payload.path` | string | `path_len + path` | uint16+bytes | Payload | UTF-8 |
| `payload.content` | string | `content_len + content` | uint16+bytes | Payload | UTF-8 |
| `payload.match` | bool | `match` | uint8 | Payload | 0/1 |
| `payload.status` (inner) | string | (제거) | - | - | 헤더 cmd+status로 대체 |
| `payload.deviceId` | number | `device_id` | uint8 | Payload | 직접 |
| `message` (에러) | string | `msg_len + message` | uint8+bytes | Payload | UTF-8 |

---

## 6. C Struct 정의 (STM32)

```c
#pragma pack(push, 1)

/* ── Headers ── */
typedef struct {
    uint8_t  src_id;
    uint8_t  tar_id;
    uint8_t  cmd;
    uint16_t payload_len;   /* LE */
} ReqHeader;                /* 5 bytes */

typedef struct {
    uint8_t  src_id;
    uint8_t  tar_id;
    uint8_t  cmd;
    uint8_t  status;
    uint16_t payload_len;   /* LE */
} RespHeader;               /* 6 bytes */

/* ── MOVE Request Payload ── */
typedef struct {
    uint8_t  motor_id;
    uint16_t pos;           /* raw, LE */
} MoveReqPayload;           /* 3 bytes */

/* ── MOVE Response Payload ── */
typedef struct {
    uint8_t device_id;
    uint8_t motor_id;
} MoveRespPayload;          /* 2 bytes */

/* ── MOTION_CTRL Request Payload ── */
typedef struct {
    uint8_t  action;
    uint32_t time_ms;       /* LE, seek일 때만 유효 */
} MotionCtrlReqPayload;     /* 5 bytes (or 1 byte if !seek) */

/* ── MOTION_CTRL Response Payload ── */
typedef struct {
    uint8_t action;
    uint8_t device_id;
} MotionCtrlRespPayload;    /* 2 bytes */

/* ── GET_MOTORS 모터 1대분 ── */
typedef struct {
    uint8_t  id;
    uint8_t  group_id;
    uint8_t  sub_id;
    uint8_t  type;          /* MotorType enum */
    uint8_t  status;        /* MotorStatus enum */
    uint16_t position;      /* raw, LE */
    uint16_t velocity;      /* ×100, LE */
    int16_t  min_angle;     /* ×10, LE */
    int16_t  max_angle;     /* ×10, LE */
    uint16_t min_raw;       /* LE */
    uint16_t max_raw;       /* LE */
} MotorInfoEntry;           /* 17 bytes */

/* ── GET_MOTOR_STATE 모터 1대분 ── */
typedef struct {
    uint8_t  id;
    uint16_t position;      /* raw, LE */
    uint16_t velocity;      /* ×100, LE */
    uint8_t  status;        /* MotorStatus enum */
} MotorStateEntry;          /* 6 bytes */

/* ── VERIFY_FILE Response Payload ── */
typedef struct {
    uint8_t match;          /* 0x00=false, 0x01=true */
} VerifyFileRespPayload;    /* 1 byte */

/* ── ERROR Payload ── */
typedef struct {
    uint8_t error_code;     /* ErrorCode enum */
    uint8_t msg_len;
    /* followed by msg_len bytes of UTF-8 message */
} ErrorPayload;             /* 2 + msg_len bytes */

#pragma pack(pop)
```

> **[ARM Unaligned Access 주의]**
> ARM Cortex-M3/M4/M7은 하드웨어 레벨에서 unaligned access를 지원하지만,
> **Cortex-M0/M0+** 에서는 unaligned access 시 **HardFault** 가 발생한다.
>
> 위 struct들은 `#pragma pack(push, 1)` 으로 패딩이 제거되었으므로,
> `uint16_t`/`uint32_t` 필드가 홀수 오프셋에 위치할 수 있다.
>
> - **Cortex-M3 이상:** 직접 struct 포인터 캐스팅 가능
>   ```c
>   MotorInfoEntry *e = (MotorInfoEntry *)(buf + offset);
>   uint16_t pos = e->position;  // OK on M3+
>   ```
> - **Cortex-M0/M0+:** 바이트 단위로 조립하거나 `memcpy` 사용 필수
>   ```c
>   uint16_t pos;
>   memcpy(&pos, buf + offset + 5, sizeof(uint16_t));  // Safe on all ARM
>   ```
>
> **권장:** MCU 종류에 관계없이 이식성을 위해 `memcpy` 기반 디시리얼라이저를 작성하고,
> 성능이 중요한 경우에만 M3+ 전용 직접 캐스팅 경로를 `#ifdef` 로 분기한다.

---

## 7. C# Struct 정의 (PC / WinUI3)

```csharp
using System.Runtime.InteropServices;

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct ReqHeader
{
    public byte SrcId;
    public byte TarId;
    public byte Cmd;
    public ushort PayloadLen;
}

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct RespHeader
{
    public byte SrcId;
    public byte TarId;
    public byte Cmd;
    public byte Status;
    public ushort PayloadLen;
}

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct MoveReqPayload
{
    public byte MotorId;
    public ushort Pos;
}

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct MoveRespPayload
{
    public byte DeviceId;
    public byte MotorId;
}

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct MotionCtrlReqPayload
{
    public byte Action;
    public uint TimeMs;     // seek일 때만 유효. payload_len==1이면 TimeMs는 읽지 않는다.
}

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct MotionCtrlRespPayload
{
    public byte Action;
    public byte DeviceId;
}

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct MotorInfoEntry
{
    public byte Id;
    public byte GroupId;
    public byte SubId;
    public byte Type;
    public byte Status;
    public ushort Position;
    public ushort Velocity;     // ×100
    public short MinAngle;      // ×10
    public short MaxAngle;      // ×10
    public ushort MinRaw;
    public ushort MaxRaw;
}

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct MotorStateEntry
{
    public byte Id;
    public ushort Position;
    public ushort Velocity;     // ×100
    public byte Status;
}

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct VerifyFileRespPayload
{
    public byte Match;
}

// ErrorPayload: 헤더 뒤에 오는 가변 길이 구조.
// C# StructLayout으로 표현 불가한 가변 부분은 수동 읽기.
// byte[0] = error_code (ErrorCode enum §3.6)
// byte[1] = msg_len
// byte[2..2+msg_len-1] = UTF-8 message (msg_len==0이면 생략 가능)
//
// 읽기 예시:
//   byte errorCode = data[offset];
//   byte msgLen    = data[offset + 1];
//   string msg     = msgLen > 0
//       ? Encoding.UTF8.GetString(data, offset + 2, msgLen)
//       : string.Empty;
```

---

## 8. Fixed-Point 변환 규칙 (양측 공통)

### 8.1 변환 공식

| 필드 | JSON 타입 | Binary 타입 | 인코딩 (→Binary) | 디코딩 (→double) | 정밀도 |
|------|----------|-------------|-----------------|-----------------|--------|
| `position` | float64 | uint16 | `(uint16)Clamp(Round(v), 0, 65535)` | `(double)v` | 1 (정수 raw) |
| `velocity` | float64 | uint16 | `(uint16)Clamp(Round(v × 100), 0, 65535)` | `v / 100.0` | 0.01 |
| `minAngle` | float64 | int16 | `(int16)Clamp(Round(v × 10), -32768, 32767)` | `v / 10.0` | 0.1° |
| `maxAngle` | float64 | int16 | `(int16)Clamp(Round(v × 10), -32768, 32767)` | `v / 10.0` | 0.1° |
| `minRaw` | float64 | uint16 | `(uint16)Clamp(Round(v), 0, 65535)` | `(double)v` | 1 |
| `maxRaw` | float64 | uint16 | `(uint16)Clamp(Round(v), 0, 65535)` | `(double)v` | 1 |
| `time` | float64 (초) | uint32 (ms) | `(uint32)Clamp(Round(v × 1000), 0, 4294967295)` | `v / 1000.0` | 0.001초 |

> `Clamp(x, min, max)` = min보다 작으면 min, max보다 크면 max.
> C#: `Math.Clamp((long)Math.Round(value * scale), min, max)`
> C: `value < min ? min : (value > max ? max : value)`

### 8.2 범위 검증

| 필드 | Binary 범위 | 실제 유효 범위 | 초과 시 처리 |
|------|------------|---------------|-------------|
| position (uint16) | 0 ~ 65535 | 0 ~ 65535 raw | 클램프 |
| velocity (uint16, ×100) | 0 ~ 65535 | 0.00 ~ 655.35 | 클램프 |
| angle (int16, ×10) | -32768 ~ 32767 | -3276.8° ~ 3276.7° | 클램프 |
| raw (uint16) | 0 ~ 65535 | 0 ~ 65535 | 클램프 |
| time_ms (uint32) | 0 ~ 4294967295 | 0 ~ ~49.7일 | 클램프 |

### 8.3 C# 인코딩 헬퍼 예시

```csharp
static ushort EncodeU16(double value)
    => (ushort)Math.Clamp((long)Math.Round(value), 0, ushort.MaxValue);

static short EncodeI16(double value)
    => (short)Math.Clamp((long)Math.Round(value), short.MinValue, short.MaxValue);

// 사용
entry.Position = EncodeU16(motor.Position);          // raw 그대로
entry.Velocity = EncodeU16(motor.Velocity * 100.0);  // ×100
entry.MinAngle = EncodeI16(motor.MinAngle * 10.0);   // ×10
entry.MaxAngle = EncodeI16(motor.MaxAngle * 10.0);   // ×10
entry.MinRaw   = EncodeU16(motor.MinRaw);
entry.MaxRaw   = EncodeU16(motor.MaxRaw);
```

---

## 9. 패킷 크기 비교 (JSON vs Binary)

### 9.1 전 명령 비교

| 명령 | 방향 | JSON (B) | Binary (B) | 절감 |
|------|------|----------|------------|------|
| ping | REQ | 36 | **5** | 86% |
| pong | RESP | 80 | **6** | 92% |
| move | REQ | 71 | **8** | 89% |
| move | RESP | 95 | **8** | 92% |
| motion_ctrl (play) | REQ | 71 | **6** | 92% |
| motion_ctrl (seek) | REQ | 82 | **10** | 88% |
| motion_ctrl | RESP | 90 | **8** | 91% |
| get_motors (req) | REQ | 56 | **5** | 91% |
| get_motors (1대) | RESP | 224 | **24** | 89% |
| get_motors (8대) | RESP | 1,162 | **143** | 88% |
| get_motors (16대) | RESP | 2,100 | **279** | 87% |
| get_motor_state (req) | REQ | 62 | **5** | 92% |
| get_motor_state (1대) | RESP | 148 | **13** | 91% |
| get_motor_state (8대) | RESP | 547 | **55** | 90% |
| get_motor_state (16대) | RESP | 946 | **103** | 89% |
| verify_file | RESP | ~75 | **7** | 91% |

---

## 10. 구현 체크리스트

### PC (C#) 측

- [ ] `Core/Protocol/` 디렉토리에 enum, struct, serializer, deserializer 구현
- [ ] `SerialService.SendCommandAsync` — JSON serialize → `BinarySerializer.Encode()`
- [ ] `SerialService.SendQueryAsync` — 동일
- [ ] `SerialService.HandleMessageReceived` — JSON parse → `BinaryDeserializer.Decode()`
- [ ] `VirtualDeviceManager.ProcessCommand` — binary 요청 수신, binary 응답 반환
- [ ] `DeviceDetailViewModel` — 모터/파일 파싱을 typed struct 기반으로 변경
- [ ] `ScanDialogViewModel` — ping 응답 처리 변경
- [ ] `SerialTrafficTap` — hex dump + 해석 로깅
- [ ] `SerialMonitorViewModel` — hex/해석 뷰 표시
- [ ] 단위 테스트: 모든 명령의 encode↔decode roundtrip

### STM32 (C) 측

- [ ] `protocol.h` — 위 §6의 struct + enum 정의
- [ ] 명령 파서: `ReqHeader` 읽기 → `switch(hdr.cmd)` → payload cast
- [ ] 응답 빌더: struct 채우기 → `memcpy` 로 TX 버퍼에 복사
- [ ] JSON 라이브러리 (cJSON 등) 완전 제거
- [ ] 빌드 시스템에서 JSON 소스 참조 제거
- [ ] 수신 버퍼 크기 축소 (2KB → 512B)
- [ ] 통합 테스트: PC ↔ 실물 STM32 왕복

---

## Appendix A: 전체 패킷 예시 (Hex Dump)

### A.1 move 요청 (motor 1, pos 2048)

```
Hex:  00 02 03 03 00 01 00 08
      ├──────────┤ ├────────┤
       ReqHeader    Payload

ReqHeader:
  src_id     = 0x00 (PC)
  tar_id     = 0x02 (장치 2)
  cmd        = 0x03 (MOVE)
  payload_len= 0x0003 (3, LE)

Payload:
  motor_id   = 0x01 (모터 1)
  pos        = 0x0800 (2048, LE)
```

### A.2 get_motor_state 응답 (모터 3대)

```
Hex:  02 00 11 00 13 00
      03
      01 20 08 32 00 00
      02 00 03 64 00 01
      03 00 00 14 00 00

RespHeader:
  src_id     = 0x02 (장치 2)
  tar_id     = 0x00 (PC)
  cmd        = 0x11 (GET_MOTOR_STATE)
  status     = 0x00 (OK)
  payload_len= 0x0013 (19, LE)

Payload:
  motor_count= 3
  [0] id=1, pos=2080, vel=50(0.50), status=Normal(0x00)
  [1] id=2, pos=768,  vel=100(1.00), status=Error(0x01)
  [2] id=3, pos=0,    vel=20(0.20),  status=Normal(0x00)
```

### A.3 에러 응답

```
Hex:  02 00 FF 01 10 00
      04 0E 46 69 6C 65 20 6E 6F 74 20 66 6F 75 6E 64

RespHeader:
  src_id     = 0x02
  tar_id     = 0x00
  cmd        = 0xFF (ERROR)
  status     = 0x01 (ERROR)
  payload_len= 0x0010 (16, LE)   ← error_code(1) + msg_len(1) + "File not found"(14) = 16

Payload:
  error_code = 0x04 (FILE_NOT_FOUND)
  msg_len    = 0x0E (14)
  message    = "File not found" (UTF-8)
```

---

## Appendix B: 변경 이력

| 버전 | 일자 | 내용 |
|------|------|------|
| v1.0 | 2026-04-14 | 초안 작성 — JSON→Binary 전체 프로토콜 사양 |
| v1.0r1 | 2026-04-14 | 1차 수정 — Hex dump 오류 정정, C# 타입명 수정, ErrorCode 위치 재배치, 누락 struct 추가 |
| v1.0r2 | 2026-04-14 | 2차 수정 — 프로토콜 버전 협상(§2.0), 통신 방향 모델(§2.1), 미지 Enum 처리 규칙(§3), ARM unaligned access 주의사항(§6), msg_len 범위 정정(§4.10), 매핑표 변환 규칙 보완(§5), 예약 cmd 범위 명시(§3.1) |
