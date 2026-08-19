# 이동 후 복구 절차 — 망이 바뀌면 이것만 보고 5분에 끝낸다

**왜 있나**: `client.ino` 의 SSID·비밀번호·서버 IP 가 **컴파일 상수**다.
**망이 바뀌면 재굽기 없이는 못 붙는다.** 2026-08-18 에 IP 문제로 시간을 썼고, 같은 값을 또 치르지 않으려고 남긴다.

⚠ **이 문서는 "붙는 것"까지만 책임진다.** 관측·판정은 monitor 의 몫이다.

---

## 0. 먼저 물어볼 것 셋 (사용자에게)

```
① 새 SSID          — 대소문자·공백·언더바까지 정확히
② 새 비밀번호       — ⚠ 특수문자 포함 여부를 반드시 확인한다
③ PC(서버)의 새 IP  — 아래 명령으로 사용자 화면에서 확인시키는 게 빠르다
```

**PC 의 새 IP 확인 명령** (서버가 도는 맥에서):
```sh
ipconfig getifaddr en0        # 무선. 값이 안 나오면 en1 을 시도한다
```
🔑 **장치와 PC 가 같은 서브넷인지 확인해라** — 앞 세 칸이 같아야 한다(`192.168.0.x`).
다르면 AP 가 격리(client isolation)를 걸었거나 다른 망에 붙은 것이다.

⚠ **비밀번호를 셸에 치지 마라.** `!` 가 들어가면 **히스토리 확장으로 값이 바뀐다**
(현재 값 `0424719222!!` 가 그 경우다). **편집기로만 넣는다.**

---

## 1. 고칠 줄 — `조별과제샘플/ardu/client.ino` 딱 셋

| 줄 | 현재 값 | 바꿀 것 |
|---|---|---|
| **91** | `#define WIFI_SSID    "3F_302"` | 새 SSID |
| **92** | `#define WIFI_PASS    "0424719222!!"` | 새 비밀번호 |
| **97** | `#define SERVER_IP    "192.168.0.29"` | PC 의 새 IP |

⚠ **줄번호는 소스가 바뀌면 밀린다. 반드시 `grep` 으로 다시 찾아라:**
```sh
grep -n "WIFI_SSID\|WIFI_PASS\|SERVER_IP" 조별과제샘플/ardu/client.ino
```
**안 고치는 것**: `SERVER_PORT "9991"`(98) · `DEVICE_ID "P1"`(99) — 망과 무관하다.
🔮 `DEVICE_ID` 는 별건이다(조원 충돌 회피용 `P1A` 변경이 대기 중 · socket 의 `park-dev` 락 이후).

---

## 2. 굽기 — 순서대로

⚠ **`조별과제샘플/` 은 폴더명과 파일명이 달라 `arduino-cli` 가 직접 못 연다.**
**빌드 전용 사본을 쓴다:**

```sh
# ① 사본 동기화 (⚠ 빠뜨리면 옛 소스를 굽는다 — 실제로 어긋나 있던 적이 있다)
# 🔴 2026-08-19 — **굽기 입력은 `조별과제샘플/ardu/` 폴더 전체다.** 헤더가 여덟이고 더 늘 수 있다.
#    **파일을 나열하면 늘 때마다 여기가 낡는다 — 하루에 세 번 깨졌다.** 폴더로 잡는다.
rm -rf arduino/.burn/client; mkdir -p arduino/.burn/client
cp 조별과제샘플/ardu/* arduino/.burn/client/
for f in 조별과제샘플/ardu/*; do \
  cmp "$f" "arduino/.burn/client/$(basename "$f")" || echo "🔴 불일치: $f"; done   # 한 줄이라도 뜨면 멈춰라

# ② 회귀 시험 — 굽기 전에 돌린다
bash arduino/test/run_stage1.sh | tail -3                          # 237 PASS / 0 FAIL (2026-08-19 기준)

# ③ 빌드 — 🔴 새 디렉토리 이름으로. 기존 것에 쓰면 hex 대조가 엉뚱한 걸 검증한다
ls arduino/.burn/                                                   # 안 쓴 이름을 고른다 (예: slot9)
arduino-cli compile --fqbn arduino:avr:uno --output-dir arduino/.burn/slot9 arduino/.burn/client

# ④ 커밋 — 🔴 굽기 전에. 칩 판본을 커밋 해시로 말할 수 있어야 한다
git commit -m "..." -- 조별과제샘플/ardu/   # ⚠ commit 에 경로를 준다(add 금지) · 폴더로 잡는다

# ⑤ 포트 확인 — 🔴 이름을 박지 마라. USB 재연결마다 바뀐다 (1101 과 21201 전례)
arduino-cli board list

# ⑥ 사전 기록 + 통보  ← 굽기 전이다
#   arduino/INTERVENTIONS.md 에 시각(초)·무엇을·왜
#   루트에 알리고, monitor 에 포트 반납을 요청해 반납 확인을 받는다

# ⑦ 업로드
arduino-cli upload -p PORT --fqbn arduino:avr:uno --input-dir arduino/.burn/slot9 arduino/.burn/client
```

### ⑧ hex 대조 — **생략하지 마라. 칩 판본을 모르면 이후 관측을 귀속 못 한다**
```sh
AV=$(ls ~/Library/Arduino15/packages/arduino/tools/avrdude/*/bin/avrdude | head -1)
CF=$(ls ~/Library/Arduino15/packages/arduino/tools/avrdude/*/etc/avrdude.conf | head -1)
"$AV" -C "$CF" -p atmega328p -c arduino -P PORT -b 115200 -U flash:r:arduino/.burn/chip-slot9.hex:i
python3 arduino/.burn/cmp.py arduino/.burn/chip-slot9.hex          # IDENTICAL 확인
```
**업로드 완료 시각(초 단위)을 적어라 — DTR 리셋이 다음 관측 창의 `t0` 다.**

---

## 3. 붙었는지 확인 — 세 곳을 본다

### ① 장치 시리얼 (가장 빠르다)
```sh
# ⚠ monitor 의 탭과 충돌한다. 열기 전에 알린다
screen PORT 9600      # 빠져나올 때 Ctrl-A 다음 K
```
사다리가 올라가는 것을 본다: `AT+CWJAP` → `AT+CIFSR` → `AT+CIPSTART` → `[NET] online`
```
🔴 CWJAP 에서 멈춤    → SSID/비밀번호가 틀렸다 (오타·특수문자)
🔴 CIPSTART 에서 멈춤 → 서버 IP 가 틀렸거나 서버가 안 떠 있다. 서브넷부터 확인
```

### ② 운영 계수기 `[CNT]` — 60초마다 나온다
```
online=1   ← 붙었다
up=(초)    ← 부팅 후 경과. 되감기면 리셋된 것이다
```

### ③ 서버 로그 — **상대 IP 를 같이 본다**
```sh
grep -a "P1" 서버로그파일 | tail -20
```
🔴 **`grep -a` 를 써라.** 제어문자가 섞이면 `grep` 이 바이너리로 보고 **조용히 0건**을 낸다
(2026-08-18 에 루트가 하루에 두 번 밟았다 — 256줄이 있는데 0건으로 보였다).

⚠ **같은 조원이 동일 카피 보드를 `device=P1` 로 돌린다.** 서버는 `device_id` 로 판정하므로
**상대 IP 를 안 보면 남의 보드를 우리 것으로 읽는다.** 접속 IP 를 반드시 대조해라.

---

## 4. 🔮 다음 세션 항목 (지금 하지 마라)

**이 값들을 런타임 설정으로 뺄 수 있나** — EEPROM 저장 + 시리얼 명령(`SET SSID ...`)으로.
**이동이 반복되면 매번 재굽기가 비용이다.**
```
⚠ 검토할 것: EEPROM 여유 · 설정 없을 때의 기본값 · 잘못된 값으로 벽돌이 되지 않게 하는 복구 경로
⚠ 그리고 굽기 없이 값이 바뀌면 "칩에 무엇이 있는가"를 hex 대조로 못 말하게 된다.
   그건 오늘 우리가 의지한 성질이다 — 설정 판본을 [CNT] 에 실어 보내는 등 대안이 필요하다
```

---

## ⚠ 훅 오탐 주의 (이 문서를 고칠 때)

이 문서를 **heredoc(`cat` + 리다이렉션)으로 쓰면 소유권 훅이 막는다.**
본문의 `chip-slot9.hex` 앞에 꺾쇠 자리표시자를 쓰면 훅이 그것을 **리다이렉션으로 읽고**
`.hex` 를 남의 파일로 판정한다. 그래서 이 문서는 자리표시자에 꺾쇠를 쓰지 않는다(`PORT`, `slot9`).
**우회하지 말고 Write 도구로 쓰거나 자리표시자를 바꿔라.**
