# Motor Position JSON Protocol

## Summary

이번 변경부터 모터 `position` 값은 각도가 아니라 장치 rawdata로 해석한다.
UI 표시는 rawdata를 모터별 범위 정보로 각도로 환산해 `120.0°(2048)` 형식으로 보여준다.

## Common Envelope

요청/응답의 기본 구조는 기존과 같다.

```json
{
  "src_id": 1,
  "tar_id": 2,
  "cmd": "get_motors",
  "payload": {}
}
```

```json
{
  "src_id": 2,
  "tar_id": 1,
  "cmd": "get_motors",
  "status": "ok",
  "payload": {}
}
```

## `get_motors`

초기 모터 목록과 함께 각도/raw 변환에 필요한 범위 정보를 내려준다.

### Response

```json
{
  "src_id": 2,
  "tar_id": 1,
  "cmd": "get_motors",
  "status": "ok",
  "payload": {
    "motors": [
      {
        "id": 1,
        "groupId": 1,
        "subId": 1,
        "type": "Servo",
        "status": "Normal",
        "position": 2048,
        "velocity": 0.5,
        "minAngle": 0,
        "maxAngle": 180,
        "minRaw": 0,
        "maxRaw": 3072
      }
    ]
  }
}
```

### Field Meaning

- `position`: 현재 raw 위치값
- `minAngle`: 각도 표시 최소값
- `maxAngle`: 각도 표시 최대값
- `minRaw`: rawdata 최소값
- `maxRaw`: rawdata 최대값

### Conversion Rule

각도 환산은 선형 매핑을 사용한다.

```text
angle = minAngle + ((position - minRaw) / (maxRaw - minRaw)) * (maxAngle - minAngle)
```

## `get_motor_state`

주기적 상태 갱신 응답이다.
`position`은 rawdata이며, 범위 메타데이터는 일반적으로 포함하지 않는다.

### Response

```json
{
  "src_id": 2,
  "tar_id": 1,
  "cmd": "get_motor_state",
  "status": "ok",
  "payload": {
    "motors": [
      {
        "id": 1,
        "position": 2080,
        "velocity": 0.5,
        "status": "Normal"
      }
    ]
  }
}
```

### Client Rule

- `get_motors`에서 받은 `minAngle/maxAngle/minRaw/maxRaw`를 보관한다.
- `get_motor_state`에서는 `position`만 rawdata로 갱신한다.
- 부분 응답이어도 기존 범위 정보는 유지한다.

## `move`

모터 이동 명령의 `payload.pos`도 rawdata 기준이다.
UI에서 슬라이더는 각도로 조작하더라도 전송 직전에 rawdata로 변환해서 보낸다.

### Request

```json
{
  "src_id": 1,
  "tar_id": 2,
  "cmd": "move",
  "payload": {
    "motorId": 1,
    "pos": 2048
  }
}
```

## Display Rule

- 화면 표시 형식: `{angle}°({raw})`
- 예시: `120.0°(2048)`
- 각도는 소수 1자리로 표시한다.

## Compatibility Note

기존 구현에서 `position`을 0~180 각도로 해석했다면, 이번 변경 이후에는 rawdata로 해석하도록 수정해야 한다.
각도 표시나 슬라이더 범위는 반드시 `get_motors`의 범위 정보와 함께 계산해야 한다.
