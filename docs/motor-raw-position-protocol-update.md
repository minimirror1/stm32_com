# Motor Raw Position Protocol Update

## Summary

PC 측 프로토콜 변경에 맞춰 모터 `position`은 이제 각도가 아니라 raw position 값으로 처리한다.
STM32 펌웨어는 raw 값을 그대로 송수신하고, 각도 표시용 범위 메타데이터는 `get_motors` 응답에 함께 제공한다.

## Protocol Rules

### `get_motors`

- `position`은 현재 raw 위치값이다.
- 각 모터는 다음 메타데이터를 함께 제공한다.
  - `minAngle`
  - `maxAngle`
  - `minRaw`
  - `maxRaw`
- GUI는 이 정보를 보관해 raw 값을 각도로 환산한다.

### `get_motor_state`

- `position`은 raw 위치값이다.
- 일반적으로 범위 메타데이터는 포함하지 않는다.
- 부분 응답이어도 기존 `get_motors` 메타데이터는 유지하는 전제를 따른다.

### `move`

- `payload.motorId`와 `payload.pos`를 사용한다.
- `payload.pos`는 raw position 값이다.
- 펌웨어는 요청 payload를 파싱해 해당 모터에 raw 목표값을 전달한다.

## Firmware Changes

### HAL Interface

`Lib/stm32_json_com/Inc/device_hal.h`

- `AppMotorInfo.position`을 raw 값으로 변경
- `AppMotorState.position`을 raw 값으로 변경
- `AppMotorInfo`에 각도/raw 변환용 범위 필드 추가
- `App_Move(uint8_t motor_id, int32_t raw_pos)`로 시그니처 변경

### JSON Layer

`Lib/stm32_json_com/Src/json_com.c`

- `move` 요청에서 `motorId`, `pos`를 검증 후 `App_Move()` 호출
- `get_motors` 응답에 `minAngle`, `maxAngle`, `minRaw`, `maxRaw` 추가
- `get_motor_state` 응답은 raw `position`만 반환
- 잘못된 `move` 요청에는 명확한 에러 메시지 반환

### Mock Device

`Core/Src/device_mock.c`

- mock motor 데이터를 raw position 기준으로 변경
- 모터별 `min_angle`, `max_angle`, `min_raw`, `max_raw` 추가
- `App_Move()`가 raw 범위를 확인하고 상태를 갱신하도록 변경
- `App_GetMotorState()`가 raw 범위 내에서만 position을 변동하도록 변경

### Weak Stub Docs

`Core/Src/device_real.c`

- 각도 기반 예제를 raw 기반 예제로 갱신
- downstream 구현자가 `position`을 각도로 오해하지 않도록 주석 정리

## Client Conversion Rule

GUI 각도 환산식:

```text
angle = minAngle + ((position - minRaw) / (maxRaw - minRaw)) * (maxAngle - minAngle)
```

표시 형식:

```text
{angle}({raw})
```

예시:

```text
120.0(2048)
```

## Notes

- 이번 변경 이후 펌웨어는 각도 변환을 수행하지 않는다.
- 각도 표시 책임은 PC/UI 측에 있다.
- `velocity`의 의미와 단위는 이번 변경 범위에 포함되지 않는다.
- 현재 워크스페이스에는 STM32 빌드 툴체인이 없어 실제 빌드 검증은 수행하지 못했다.
