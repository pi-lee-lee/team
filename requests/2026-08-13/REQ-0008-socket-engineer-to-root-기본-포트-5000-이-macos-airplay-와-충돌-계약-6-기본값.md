---
id: REQ-0008
title: 기본 포트 5000 이 macOS AirPlay 와 충돌 — 계약 §6 기본값 검토 요청
from: socket-engineer
to: root
status: done
created: 2026-08-13T21:34:23+0900
updated: 2026-08-13T21:41:12+0900
files: ["docs/digitcam-contract.md"]
parent: none
---

# REQ-0008 · 기본 포트 5000 이 macOS AirPlay 와 충돌 — 계약 §6 기본값 검토 요청

**요청자** `socket-engineer` → **담당** `root`

## 요청 내용

계약 §6 의 기본값 server.port=5000 이 이 노트북(macOS)에서 쓸 수 없다. 실측: python3 net/test_server.py 가 '바인드 실패 0.0.0.0:5000 — [Errno 48] Address already in use' 로 종료하고, lsof -nP -iTCP:5000 -sTCP:LISTEN 결과 ControlCenter(PID 1088, AirPlay 수신기)가 IPv4/IPv6 양쪽으로 LISTEN 중이다. macOS Monterey 이후 기본 동작이라 이 머신만의 문제가 아니다. 더 나쁜 것은 앱 쪽 증상이다: 앱이 5000 으로 붙으면 AirPlay 에 연결이 성공한다(connect_ex=0, sendall 성공 — 실측). 즉 앱 화면에는 '연결됨'이 뜨고 전송도 성공한 것처럼 보이는데 값은 아무 데도 도착하지 않는다. 서버 로그가 비어 있으니 인식기나 전송 코드를 의심하게 되는데 엉뚱한 곳이다. 제안: 계약 §6 의 기본값을 5500 등 충돌 없는 포트로 바꾸고, android-engineer 의 assets/server.properties 기본값도 같이 맞춘다. 나는 계약을 고치지 않았다. net/README.md 에는 이 함정과 우회법(다른 포트를 쓴다)을 적어 뒀다.

## 왜 필요한가

계약 §11 완료 기준 5(폰에서 노트북 테스트 서버로 번호판 전송)가 기본 설정 그대로는 실패한다. 게다가 실패 모양이 '연결됨인데 아무것도 안 옴'이라 원인을 엉뚱한 도메인에서 찾게 된다.

## 대상 파일

- `docs/digitcam-contract.md`
## 완료 기준

계약 §6 의 server.port 기본값이 macOS 에서 점유되지 않는 값으로 정해지고, android-engineer 의 번들 server.properties 기본값이 그것과 일치한다

---

## 처리 결과

<!-- 담당자가 여기에 적는다. 처리 후 반드시:
     team/bin/req.sh done REQ-0008 --by root --note "<한 줄 요약>" -->

_(미처리)_

### 처리 완료 · root · 2026-08-13T21:41:12+0900

계약 6절 기본 포트를 5500 으로 변경. lsof 로 ControlCenter 점유 실측 확인, android-engineer 통지 완료

