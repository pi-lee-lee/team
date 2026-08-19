# 굽는 절차서 — **새벽 2시에 이 파일만 보고 따라갈 수 있어야 한다**

> **왜 이 파일이 있나**: 2026-08-16 사고들의 시작점이 전부 **그 순간에 순서를 떠올린 것**이었다.
> 절차를 기억에서 꺼내면 한 단계가 빠지고, 빠지는 단계는 늘 **기록**이다.
> **읽으면서 그대로 치도록 실제 명령까지 적어 둔다.**
>
> ⚠ 이 문서는 **순서가 곧 내용**이다. 단계를 건너뛰거나 바꾸지 마라.

---

## 0. 굽기 전 — 멈춰서 확인할 것 넷

**하나라도 아니오면 굽지 마라.**

- [ ] **루트가 순서를 줬는가?** 관측 창·다른 도메인의 배포와 겹치지 않는가
  (2026-08-16 순서: `monitor 판정 → (전원 지목 시) 보드 확인 → socket 배포 → 플래싱`)
- [ ] **socket 의 서버 교체와 같은 창이 아닌가?** 겹치면 **변수가 둘이 되어 어느 쪽 효과인지 못 가른다**
- [ ] **`DEBUG` 값이 A 팔과 같은가?** 다르면 A/B 비교가 그 자리에서 깨진다(원장 §8.4-1).
      **2단계 굽기는 `DEBUG=1` 확정**
- [ ] **무엇을 굽는지 커밋 해시로 말할 수 있는가?** "지금 소스"는 답이 아니다

```bash
git rev-parse --short HEAD          # 이 값을 5단계 보고에 적는다
git status --short -- 조별과제샘플/client.ino    # 비어 있어야 한다(커밋 안 된 변경 금지)
```

> 🔴 **커밋되지 않은 변경을 구우면 그 뒤 관측을 어느 소스에도 귀속할 수 없다.**
> 2026-08-16 에 "칩에 무엇이 있는지 모르는" 상태로 하루를 보냈다. 반드시 커밋 후에 굽는다.

---

## 1. 루트에 **사전** 통보 — 굽기 **전**이다

허락을 구하는 것이 아니라 **알리는 것**이다. 루트가 막지 않으면 진행한다.
루트는 다른 도메인의 관측·시험이 걸려 있는지 아는 **유일한 자리**다.

```
SendMessage → learn-1e
  "[플래싱 사전통보] <시각> 에 <커밋해시> 를 DEBUG=1 로 굽는다.
   포트 <포트> 를 잡는다. monitor tap 중단이 필요하다. 막을 이유가 있으면 지금 알려라."
```

---

## 2. `arduino/INTERVENTIONS.md` 에 **먼저** 적는다 ⚠ 굽기 전

> **사후 기록은 이미 늦다.** monitor 의 집계기는 여기 선언이 없으면 **자동으로 "자발"로 분류**한다.
> 기록되지 않은 개입 하나가 2026-08-16 에 **관측 4시간을 오염시켰다.**

개입 표에 한 줄 추가 (형식 그대로):

```
| <HH:MM:SS> | arduino-engineer | <커밋해시> 플래싱(DEBUG=1) | **예** | REQ-xxxx · 사전통보 완료 |
```

**시각은 초 단위로.** 그리고 **굽기 직전에 적는다** — 적고 나서 굽는다.

---

## 3. monitor 에 포트 통보 → **tap 중단 확인까지 받고** 진행

시리얼 포트는 **배타적 자원**이다(CLAUDE.md). tap 이 잡고 있으면 업로드가 안 된다.

⚠ **tap 을 내리면 미저장 캡처가 소실될 수 있다.** 내가 임의로 죽이지 마라 — **monitor 가 내린다.**
⚠ **DTR 리셋**: tap 을 내렸다 다시 올리는 것 자체가 **보드 리셋**이고 관측 경계를 만든다.
   그래서 **굽기와 tap 재개를 한 번으로 묶는다**(개입을 쪼개지 마라).

```
SendMessage → monitor-engineer
  "[포트] 루트 순서 확정됐다. <시각> 에 플래싱한다. tap 을 내리고 알려 달라.
   굽고 나서 바로 재개하면 그 DTR 이 새 구간의 t0 가 된다."
```

- [ ] monitor 로부터 **"내렸다"** 회신을 받았다
- [ ] 포트가 실제로 비었는지 확인:

```bash
PORT=$(arduino-cli board list | awk '/arduino:avr:uno/{print $1}')   # ← 이름을 박지 마라
echo "$PORT"                      # 예: /dev/cu.usbmodem21201 (⚠ 값이 자주 바뀐다 — 예시일 뿐이다)
lsof "$PORT"                      # 아무것도 안 나와야 한다
```

> ### 🔴 **포트 이름은 USB 재연결마다 바뀐다.** 실제로 `usbmodem21201` → `usbmodem1101` → **`usbmodem21201`(2026-08-18 이동 후)** 로 바뀌었고
> **monitor 의 탭이 그 때문에 죽었다**(2026-08-18 `07:22:37` · `포트 복구 실패 6회`).
> **이름을 박아 두면 다음 재연결에서 조용히 막힌다** — 원장 §5.7 이 `SERVER_IP` 로 데인 것과 같은 형태다.

---

## 4. 굽기

**포트 확인** (⚠ 이 명령도 포트를 열 수 있다 — 3단계 뒤에 한다):

```bash
arduino-cli board list
```

**위 `$PORT` 를 아래 모든 명령에 쓴다.** 이름을 손으로 적지 마라 — 재연결마다 바뀐다.

**빌드 → 업로드** (폴더명 == 스케치명 규칙 때문에 사본이 필요하다 · 원장 §4.2):

> ### 🔴 **2026-08-19 (REQ-0264) — 굽기 입력이 파일 하나가 아니다**
> 링크 계층이 **`조별과제샘플/EspLink_*.h` 넷**으로 분리됐다. **스케치만 복사하면 낡은 헤더로 굽는다.**
> **아래는 전부 `client.ino` 와 `EspLink_*.h` 를 같이 다룬다.**

```bash
mkdir -p arduino/.burn/client
cp 조별과제샘플/client.ino 조별과제샘플/EspLink_*.h arduino/.burn/client/
for f in 조별과제샘플/client.ino 조별과제샘플/EspLink_*.h; do \
  cmp "$f" "arduino/.burn/client/$(basename "$f")" || echo "🔴 불일치: $f"; done   # 🔴 한 줄이라도 뜨면 멈춰라
git log -1 --format='굽는 판본 %h %s' -- 조별과제샘플/client.ino 조별과제샘플/EspLink_*.h
git status --short -- 조별과제샘플/client.ino 조별과제샘플/EspLink_*.h   # 비어야 한다. 아니면 미커밋 채로 굽는 것이다
arduino-cli compile --fqbn arduino:avr:uno --output-dir arduino/.burn/out arduino/.burn/client
arduino-cli upload -p "$PORT" --fqbn arduino:avr:uno --input-dir arduino/.burn/out
```

> ⚠ **`cp` 뒤 스테이징에 남아 있는 옛 파일은 안 지워진다.** 헤더 이름이 바뀌거나 줄면
> **스테이징에 유령 파일이 남아 컴파일에 섞일 수 있다.** 이름을 바꿀 때는 `arduino/.burn/client` 를 비우고 시작해라.

> ### 🔴 **`cmp` 를 빼지 마라 — `cp` 를 건너뛰면 낡은 스테이징이 조용히 컴파일된다.**
> **`.burn/client/` 는 2026-08-19 부로 git 추적에서 뺐다**(원장 §24). 저장소에 사본이 없으므로
> **`git status` 가 그 낡음을 더는 안 알려 준다.** 그 자리를 이 `cmp` 가 대신한다.
>
> ⚠ **`git status` 가 비어 있지 않으면 "굽는 판본"은 그 커밋이 아니다.** 커밋하고 굽든지,
> **미커밋 채로 구웠다는 사실을 `INTERVENTIONS` 에 적든지 — 둘 중 하나를 반드시 해라.**
> 안 적으면 이후 관측을 **어느 소스에도 귀속할 수 없다.**

> `--input-dir` 로 **방금 빌드한 그것**을 올린다. 다시 빌드시키지 않는다 —
> 5단계에서 **대조할 대상과 올린 것이 같아야** 하기 때문이다.

---

## 5. 🔴 hex 대조 — **"무엇이 칩에 있는가"를 모르면 이후 관측이 전부 무의미하다**

플래시를 **다시 읽어서** 방금 올린 것과 **바이트로** 비교한다.

```bash
avrdude -c arduino -p atmega328p -P "$PORT" -b 115200 \
        -U flash:r:arduino/.burn/chip.hex:i
```

> `avrdude` 가 없으면 arduino-cli 가 품은 것을 쓴다:
> `ls ~/Library/Arduino15/packages/arduino/tools/avrdude/*/bin/avrdude`
> 설정 파일도 같이 넘겨야 한다: `-C .../etc/avrdude.conf`

**Intel HEX 를 눈으로 비교하지 마라** — 레코드 배치가 달라도 내용은 같을 수 있다(원장 §4.4).
**32768바이트 raw 로 펴서** 비교한다.

> ## 🔴 반드시 **응용 영역만** 비교한다 — 안 그러면 정상 플래싱에도 MISMATCH 가 뜬다
>
> `avrdude` 덤프는 **부트로더를 포함한 32768바이트 전부**다.
> 그런데 `client.ino.hex` 에는 **부트로더가 없다.** 그대로 비교하면 그 영역이 전부 차이로 잡힌다.
>
> **실측(2026-08-16, 이 절차서를 검증하다 발견):**
> 부트로더 포함본과 응용본의 차이 **496바이트 · 위치 `0x7E00`~`0x7FFF` · 응용 영역 차이 0.**
> → **경계는 `APP_END = 32256`**(= `arduino-cli` 가 말하는 "최대 32256 바이트").
>
> 이걸 모르고 돌리면 **"칩에 다른 게 있다"고 오판해 멀쩡한 펌웨어를 다시 굽게 된다.**

```bash
python3 - <<'PY'
APP_END = 32256          # 0x7E00 — 이 위는 부트로더다. 비교에서 제외한다

def raw(p, size=32768):
    m = bytearray([0xFF]) * size
    base = 0
    for ln in open(p):
        ln = ln.strip()
        if not ln.startswith(':'): continue
        n = int(ln[1:3], 16); addr = int(ln[3:7], 16); t = int(ln[7:9], 16)
        d = bytes.fromhex(ln[9:9 + n * 2])
        if t == 0: m[base + addr: base + addr + n] = d
        elif t == 2: base = int.from_bytes(d, 'big') * 16
        elif t == 4: base = int.from_bytes(d, 'big') * 65536
    return bytes(m)

a = raw('arduino/.burn/chip.hex')[:APP_END]
b = raw('arduino/.burn/out/client.ino.hex')[:APP_END]
diff = [i for i, (x, y) in enumerate(zip(a, b)) if x != y]
print('응용영역 다른 바이트:', len(diff))
print('IDENTICAL' if not diff else 'MISMATCH 첫 위치 0x%04X' % diff[0])
print('참고 — 프로그램 바이트 수:', sum(1 for x in b if x != 0xFF))
PY
```

- [ ] **`응용영역 다른 바이트: 0` · `IDENTICAL`** 이 나왔다
- [ ] 아니면 **멈추고 보고한다.** 불일치는 "칩에 다른 것이 있다"는 뜻이다

> ✅ **이 스크립트는 하드웨어 없이 검증했다**(2026-08-16):
> 같은 파일끼리 → 차이 0 · 부트로더 포함본과 → 496(전부 `0x7E00` 이상).
> **차이를 실제로 잡는다는 것까지 확인했다** — 항상 0 을 뱉는 스크립트가 아니다.
> ⚠ **"돌아간다"와 "차이를 잡는다"는 다르다.** 대조 스크립트는 후자까지 확인해야 믿을 수 있다.

⚠ **덤프도 리셋을 일으킨다.** 2단계에 이 행위도 함께 적혀 있어야 한다.

---

## 6. 사후 보고 — **REQ 없이 구웠어도 빠지지 않는다**

요청 없이 한 일은 원장에 안 남는다. **이 보고가 유일한 기록이다.**

```bash
team/bin/req.sh notice --from arduino-engineer \
  --title "<커밋해시> 플래싱 완료 — hex 대조 IDENTICAL" \
  --body-file requests/.body-arduino-engineer.md
rm requests/.body-arduino-engineer.md
```

본문에 반드시 넣을 것:

- [ ] **커밋 해시**와 `DEBUG` 값
- [ ] **굽기 시각**(초 단위)과 **hex 대조 결과**(다른 바이트 수)
- [ ] **빌드 수치**(플래시/RAM) — 다음 사람이 대조할 기준
- [ ] **monitor tap 재개 시각** = 새 구간의 **t0**
- [ ] **`arduino/INTERVENTIONS.md` 를 갱신했다는 사실**

그리고 SendMessage 로 **루트와 monitor 에 포인터**를 보낸다.

---

## 7. 굽고 나서 — 즉시 확인할 것

- [ ] `[CNT]` 줄이 60초 안에 나온다 (계수기가 살아 있다)
- [ ] 배너 `[PARKING NODE]` → `CWJAP OK` → `IP 확보` → 서버 수락까지 **약 12초**(원장 §3.1)
- [ ] 서버에 `device=P1` 로 등록된다 (socket 확인)

⚠ **첫 관측에서 곧바로 결론내지 마라.** 원장 §8 대로 **A 창에 자해가 0건**이었으므로
**"좋아졌다"가 안 나오는 것이 기본 예상**이다. 그것은 회귀가 아니다.

⚠ **`okto` 를 옛 기대치 0.33% 와 곧장 견주지 마라**(원장 §6.5-1) —
그 값은 **2단계 없는 칩**에서 나온 것이다. **2단계 칩에서 기대치를 새로 잡는 것이 첫 일이다.**

---

## 되돌리기 — 문제가 생기면

수정 전 판본이 **저장소에 있다**(`git clean` 으로도 안 사라진다):

```bash
mkdir -p arduino/.burn/base
cp arduino/.basefw/client.ino arduino/.burn/base/client.ino    # 커밋된 기준선
# 폴더명==스케치명 규칙 → base/client.ino 로 두고 굽는다
```

⚠ **되돌리는 것도 개입이다.** 1~6단계를 **똑같이** 밟는다. 급하다고 기록을 건너뛰지 마라 —
**급했던 순간의 미기록이 정확히 오늘 사고의 형태였다.**
