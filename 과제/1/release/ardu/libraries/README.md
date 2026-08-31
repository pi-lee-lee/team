# libraries/ — 비어 있다. 이유가 있다

다섯 스케치(p1~p5)의 `#include <…>` 는 전부 **Arduino AVR 코어**뿐이다
(`Arduino.h` · `Stream.h` · `avr/*` · `util/*` · `stdio.h` · `string.h` · `stdlib.h` · `inttypes.h`).
외부 폴더에서 만든 라이브러리는 **없다** — `SoftSerialBig.cpp/.h`(수신 링 128 B 소프트 시리얼)와
`LcdI2C.h`(자작 I2C LCD 드라이버)는 **각 스케치 폴더 안의 파일**이고 그대로 같이 굽힌다.
`~/Documents/Arduino/libraries` 의 `Servo`·`LiquidCrystal_I2C` 는 **쓰지 않는다**(p3·p4 는 Timer2 HW PWM · LCD 는 자작).

빌드 명령의 `--libraries 릴리즈/ardu/libraries` 는 이 빈 폴더를 가리킨다 — 코어 밖 어디도 참조하지 않음을 보이기 위한 것이다.
자립 증명(README.md §검증)은 스케치 안 파일 하나(`SoftSerialBig.cpp`)를 빼면 컴파일이 실패하는 것으로 한다.
