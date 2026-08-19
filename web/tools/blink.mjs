/**
 * 링크가 잠깐 끊기는 동안 **화면이 무엇을 하는가** — 진짜 크롬으로 잰다.
 *
 * 사전 등록: docs/web/FINDING-link-blink-2026-08-17.md §2 (재는 것을 관측 전에 정했다)
 *
 * 🔴 안전 규칙 — e2e.mjs 와 같다. 기본값 없음 · 운영 포트 거부.
 *   운영에는 진짜 보드가 붙어 있고 그 자료가 지금 유일한 자료다.
 *
 * 사용: node web/tools/blink.mjs --port 10500 --case A --seconds 24
 *       node web/tools/blink.mjs --port 10500 --case B --n 3
 */
import { launch, evaluate, waitFor, sleep } from './cdp.mjs';
import { assertServedIsCurrent } from './screen-build.mjs';
import { writeFileSync, mkdirSync, readFileSync, appendFileSync } from 'node:fs';

const argv = process.argv.slice(2);
const arg = (k, d) => { const i = argv.indexOf(k); return i >= 0 ? argv[i + 1] : d; };
const PORT = arg('--port', null);
const CASE = arg('--case', 'A');
const SECONDS = Number(arg('--seconds', 24));
const N = Number(arg('--n', 3));
const TRIGGER = arg('--trigger', 'offline');   // offline | silence — B 에서 언제 확인을 누를 것인가
const OUT = new URL('../artifacts/', import.meta.url);

if (!PORT) { console.error('--port 를 반드시 줘라 (기본값 없음)'); process.exit(2); }
const FORBIDDEN = ['9900', '9991', '5500'];
if (FORBIDDEN.includes(String(PORT))) {
  console.error('🔴 ' + PORT + ' 는 운영 포트다. 중단.');
  process.exit(2);
}
const BASE = 'http://127.0.0.1:' + PORT + '/';
mkdirSync(OUT, { recursive: true });

/* 서버 로그 — **장치 쪽을 읽는 유일한 창**이다(§6.1). 화면만 봐서는 장치가 무엇을 들고
   있는지 알 수 없다. 기본 경로는 포트 오프셋에서 유도한다: 10500 → +600 → test+600.log */
const LOG = arg('--log', process.env.HOME + '/parking-logs/parking-server.test+' + (Number(PORT) - 9900) + '.log');
const logLines = () => { try { return readFileSync(LOG, 'utf8').split('\n'); } catch (e) { return ['(로그를 못 읽었다: ' + e.message + ')']; } };

/* 🔴 개입 구간을 파일에 남긴다 — 1차 창에서 두 조각의 시각을 "추정"으로 넘겨야 했다.
   monitor 는 이 값으로 관측을 가른다. 사람 기억이 아니라 파일이 원천이어야 한다. */
const stamp = () => new Date().toISOString().replace('T', ' ').slice(0, 19);
const RUNLOG = new URL('blink-runs.tsv', OUT);
const markRun = (what) => { try { appendFileSync(RUNLOG, stamp() + '\t' + PORT + '\t' + CASE + '\t' + what + '\n'); } catch { } };

/* 화면에서 한 번에 걷어 오는 것 — M1~M7 전부. 사전 등록한 항목만 본다. */
const SAMPLE = `(() => {
  const s = (typeof state !== 'undefined' && state.snapshot) ? state.snapshot : null;
  const d = s && s.device ? s.device : {};
  const txt = (id) => { const e = document.getElementById(id); return e ? e.textContent.trim() : '(없음)'; };
  return {
    t: Date.now(),
    online: (d.online === undefined ? null : d.online),
    lf: (d.last_frame_ts === undefined ? null : d.last_frame_ts),
    age: (typeof d.last_frame_ts === 'number') ? (Date.now() - d.last_frame_ts) : null,
    link: (typeof state !== 'undefined') ? state.link : null,
    conn: txt('conn-text'),
    banner: txt('slots-banner'),
    devOnline: txt('dev-online'),
    devFrame: txt('dev-frame'),
    tiles: [...document.querySelectorAll('.tile')].map(b =>
      (b.dataset.slot || '?') + ':' + b.querySelector('.tile__state').textContent.trim()
      + '/' + b.getAttribute('aria-disabled')),
    /* 사용자가 실제로 읽는 줄. 상태 문구는 "빈 자리" 그대로라 이 줄이 없으면
       "화면이 뭐라고 말하는가"에 답할 수 없다. */
    meta1: (() => { const b = document.querySelector('.tile'); const m = b && b.querySelector('.tile__meta'); return m ? m.textContent.trim() : '(없음)'; })(),
    dialogOpen: !!(document.getElementById('confirm-dialog') || {}).open,
    msg: txt('messages').replace(/\\s+/g, ' ').slice(-200)
  };
})()`;

async function goto(client, url) {
  /* 🔴 **낡은 화면을 재지 않는다** — 서빙본이 저장소 원본과 다르면 던진다(원장 §5.85).
     :9900 이 08-17 사본을 이틀간 내주는 동안 이 하니스들은 아무 말도 안 했다. */
  await assertServedIsCurrent(url);
  await client.send('Page.navigate', { url });
  await waitFor(client,
    `document.readyState === 'complete' && !!document.getElementById('conn-text') && !!document.querySelector('.tile')`,
    { what: '문서 준비 (' + url + ')', timeout: 15000 });
}

async function click(client, selector) {
  const box = await evaluate(client, `(() => {
    const e = document.querySelector(${JSON.stringify(selector)});
    if (!e) return null;
    e.scrollIntoView({block:'center'});
    const r = e.getBoundingClientRect();
    return { x: r.left + r.width/2, y: r.top + r.height/2, w: r.width, h: r.height };
  })()`);
  if (!box) throw new Error('선택자를 찾지 못했다: ' + selector);
  if (box.w === 0 || box.h === 0) throw new Error('크기 0: ' + selector);
  const p = { x: Math.round(box.x), y: Math.round(box.y), button: 'left', clickCount: 1 };
  await client.send('Input.dispatchMouseEvent', { type: 'mouseMoved', ...p });
  await client.send('Input.dispatchMouseEvent', { type: 'mousePressed', ...p });
  await client.send('Input.dispatchMouseEvent', { type: 'mouseReleased', ...p });
}

/** 두 표본에서 **사전 등록한 항목만** 골라 비교한다(본 뒤에 항목을 늘리지 않는다). */
const KEYS = ['online', 'conn', 'banner', 'devOnline', 'tiles', 'msg'];
function diff(a, b) {
  const out = [];
  for (const k of KEYS) {
    const x = JSON.stringify(a[k]), y = JSON.stringify(b[k]);
    if (x !== y) out.push({ key: k, from: a[k], to: b[k] });
  }
  return out;
}

const log = [];
const say = (s) => { console.log(s); log.push(s); };
const note = (s) => say('  · ' + s);
let failed = 0;
let ran = 0;          // 🔴 **분모**. 이게 없으면 `실패 0건` 이 건강인지 미실행인지 안 갈린다
/** 합격/불합격을 세는 단언. e2e.mjs 와 같은 형태로 맞춘다(초록이 곧 통과). */
function check(name, cond, detail) {
  ran++;
  const ok = !!cond;
  if (!ok) failed++;
  say('  ' + (ok ? '✅' : '❌') + ' ' + name + (detail !== undefined ? '  → ' + detail : ''));
  return ok;
}

/**
 * 판정 한 줄. **분자만 찍지 않는다.**
 *
 * 🔴 2026-08-17 (monitor 지적) — `catch` 에서 실패를 올리는 것만으로는 안 닫힌다.
 * `check()` 를 한 번도 안 도는 **새 경로**가 생기면 또 `실패 0건 → 통과` 가 된다.
 * `0` 이 (a)건강 (b)미실행 (c)못셈 중 무엇인지 구별이 안 되는 것이 원인이고,
 * **분모를 같이 찍으면 경로를 열거하지 않아도 전부 잡힌다**:
 *
 *     검사 12건 중 실패 1건
 *     검사 0건 중 실패 0건      ← 그 자체로 빨강. 조건문이 필요 없다
 */
function verdict() {
  if (ran === 0) return '🔴 검사 0건 — 아무것도 재지 못했다 (통과가 아니다)';
  return (failed ? '실패' : '통과') + ' — 검사 ' + ran + '건 중 실패 ' + failed + '건';
}

/* ⚠ 2026-08-17: markRun() 을 정의만 해 놓고 **호출을 안 해서** 첫 창의 조각 시각을 파일로
   못 남겼다(monitor 에게 "이번엔 파일로 찍는다"고 말해 둔 것이 안 지켜졌다).
   선언과 호출은 다른 일이다 — 도구를 만들었다고 도구가 도는 것이 아니다. */
markRun('시작');
const client = await launch({ headless: true });
try {
  await client.send('Page.enable');
  await client.send('Runtime.enable');

  await goto(client, BASE + 'index.html');
  await waitFor(client, `document.getElementById('conn-text').textContent.includes('WebSocket')`,
                { what: 'WS 연결', timeout: 20000 });

  /* 붙은 곳이 진짜 그 포트인지 — 운영으로 새지 않았음을 스스로 증명한다 */
  const wsUrl = await evaluate(client, `(transport.ws && transport.ws.url) || '(없음)'`);
  say('WS = ' + wsUrl);
  if (!String(wsUrl).includes(':' + PORT + '/')) throw new Error('🔴 대상이 아닌 곳에 붙었다: ' + wsUrl);

  const base = await evaluate(client, SAMPLE);
  say('기준선: online=' + base.online + ' link=' + base.link + ' conn=' + JSON.stringify(base.conn));

  if (CASE === 'A') {
    /* A. 조작 없이 끊김 — 사람이 아무것도 안 하는 동안 화면이 스스로 무엇을 하는가.
       fake 가 8초 주기로 알아서 끊으므로 우리는 250ms 마다 표본만 뜬다. */
    say('\n[A] 조작 없이 ' + SECONDS + '초 관찰 (250ms 표본)');
    const samples = [];
    const t0 = Date.now();
    while (Date.now() - t0 < SECONDS * 1000) {
      samples.push(await evaluate(client, SAMPLE));
      await sleep(250);
    }
    /* 관측 전용 케이스라도 **단언이 하나는 있어야 한다.** 0표본은 "조용했다"가 아니라
       "재지 못했다"이고, 그 둘을 안 가르면 미실행이 관측으로 보고된다. */
    check('표본을 실제로 걷었다', samples.length > 0, samples.length + '표본');
    const ages = samples.map(s => s.age).filter(v => typeof v === 'number');
    const changes = [];
    for (let i = 1; i < samples.length; i++) {
      const d = diff(samples[i - 1], samples[i]);
      if (d.length) changes.push({ atMs: samples[i].t - t0, d });
    }
    say('표본 ' + samples.length + '개 · last_frame_ts 나이 최대 ' + Math.max(...ages) + 'ms · 최소 ' + Math.min(...ages) + 'ms');
    say('침묵 흔적(나이 1500ms 초과 표본) = ' + ages.filter(v => v > 1500).length + '개');
    say('online=false 표본 = ' + samples.filter(s => s.online === false).length + '개');
    say('사전 등록 항목의 변화 = ' + changes.length + '건');
    for (const c of changes.slice(0, 12)) say('  · +' + c.atMs + 'ms ' + JSON.stringify(c.d).slice(0, 300));
    writeFileSync(new URL('blink-A.json', OUT), JSON.stringify({ port: PORT, samples }, null, 2));
    say('원자료 web/artifacts/blink-A.json');
  }

  if (CASE === 'B') {
    /* B. 대화상자가 열려 있는 동안 끊김 → 끊긴 채로 확인을 누른다.
       예측(사전 등록): online=false 면 requestReserve() 가 확인 뒤 재검사에서 걸러
       "그 사이 자리 상태가 바뀌어 예약하지 않았습니다" 가 뜬다. */
    for (let i = 1; i <= N; i++) {
      say('\n[B' + i + '] 대화상자를 연 채 끊김을 기다린다');
      /* ⚠ 먼저 **연결된 순간**을 기다린다. online=false 인 동안에는 빈 자리가 전부 잠겨
         (§3.2) 대화상자를 열 수조차 없다 — 1회차에서 실제로 그래서 건너뛰었다.
         "끊긴 채로 확인을 누른다"를 재려면 **누를 때는 붙어 있어야** 한다. */
      await waitFor(client, `(typeof state !== 'undefined' && state.snapshot && state.snapshot.device.online === true)`,
                    { what: '장치 연결 복귀', timeout: 30000 });
      const free = await evaluate(client, `(() => {
        const b = [...document.querySelectorAll('.tile')].find(x =>
          /빈 자리/.test(x.querySelector('.tile__state').textContent) && x.getAttribute('aria-disabled') !== 'true');
        return b ? b.dataset.slot : null; })()`);
      if (!free) { say('  누를 수 있는 빈 자리가 없다 — 건너뛴다'); break; }
      await click(client, `.tile[data-slot="${free}"]`);
      await waitFor(client, `!!document.getElementById('confirm-dialog').open`, { what: '대화상자', timeout: 5000 });
      say('  ' + free + ' 대화상자 열림. 침묵/오프라인 구간을 기다린다(최대 20초)');

      /* 끊김의 한가운데에서 누른다. online=false 가 최우선, 없으면 프레임 침묵(나이>1200ms) */
      const t0 = Date.now();
      let hit = null;
      while (Date.now() - t0 < 30000) {
        const s = await evaluate(client, SAMPLE);
        /* 🔴 방아쇠를 인자로 고정한다. 섞으면 안 된다 —
           `--mute-for 6` 에서는 프레임 침묵이 3.5초 **먼저** 오므로, 침묵을 방아쇠로 두면
           online 이 아직 true 인 순간에 눌러 버리고 그건 ② 를 한 번 더 재는 것이다. */
        if (TRIGGER === 'offline' && s.online === false) { hit = { why: 'online=false', s }; break; }
        if (TRIGGER === 'silence' && typeof s.age === 'number' && s.age > 1200) { hit = { why: '프레임 침묵 ' + s.age + 'ms', s }; break; }
        await sleep(150);
      }
      if (!hit) { say('  🔴 20초 안에 끊김 구간을 못 잡았다 — 이 회차는 무효'); await click(client, '#confirm-dialog button[value="cancel"]').catch(() => {}); continue; }
      say('  끊김 포착: ' + hit.why + ' → 지금 확인을 누른다');

      await click(client, '#confirm-dialog button[value="ok"]');
      await sleep(1200);
      const after = await evaluate(client, SAMPLE);
      const tile = (after.tiles.find(t => t.startsWith(free + ':')) || '(없음)');
      say('  결과 칸  = ' + tile);
      say('  결과 문구 = ' + JSON.stringify(after.msg.slice(-140)));
      say('  online 은 그때 ' + hit.s.online + ' 였다');

      /* 상태를 남기지 않는다 — 예약이 잡혔으면 취소까지 하고 끝낸다 */
      if (/내 예약/.test(tile)) {
        await sleep(1500);
        await click(client, `.tile[data-slot="${free}"]`).catch(() => {});
        await waitFor(client, `!!document.getElementById('confirm-dialog').open`, { timeout: 4000, what: '취소 대화상자' }).catch(() => {});
        await click(client, '#confirm-dialog button[value="ok"]').catch(() => {});
        await sleep(1200);
        const back = await evaluate(client, SAMPLE);
        say('  정리: ' + (back.tiles.find(t => t.startsWith(free + ':')) || '(없음)'));
      }
    }
  }

  if (CASE === 'C') {
    /* C. 예약은 장치에 반영됐는데 ack 이 유실된다 (`fake_arduino --drop-rate 1.0`).
       사전 등록 §6.1 — **불일치 = 화면이 "예약 아님"으로 그리는 동안 장치는 예약을 들고 있음.**
       화면은 브라우저에서, 장치 쪽은 **서버 로그**에서 읽는다. 두 계측기의 시계가 다르다. */
    const tgt = (slot) => `(() => { const b = document.querySelector('.tile[data-slot="${slot}"]');
      return b ? { state: b.querySelector('.tile__state').textContent.trim(), view: b.dataset.view || '', dis: b.getAttribute('aria-disabled') } : null; })()`;

    for (let i = 1; i <= N; i++) {
      say('\n[C' + i + '] 예약을 보내고 ack 이 유실되는 것을 본다');
      await waitFor(client, `(typeof state !== 'undefined' && state.snapshot && state.snapshot.device.online === true)`,
                    { what: '장치 연결', timeout: 30000 });
      const free = await evaluate(client, `(() => {
        const b = [...document.querySelectorAll('.tile')].find(x =>
          /빈 자리/.test(x.querySelector('.tile__state').textContent) && x.getAttribute('aria-disabled') !== 'true');
        return b ? b.dataset.slot : null; })()`);
      if (!free) { say('  누를 수 있는 빈 자리가 없다 — 건너뛴다'); break; }

      const logBefore = logLines().length;
      await click(client, `.tile[data-slot="${free}"]`);
      await waitFor(client, `!!document.getElementById('confirm-dialog').open`, { what: '대화상자', timeout: 5000 });
      await click(client, '#confirm-dialog button[value="ok"]');
      const tSend = Date.now();
      say('  ' + free + ' 예약 송신 (' + stamp() + ')');

      /* C1·C2 — 롤백까지 몇 초, 그리고 문구가 무엇인가 */
      let tRollback = null, rollMsg = '';
      while (Date.now() - tSend < 20000) {
        const t = await evaluate(client, tgt(free));
        if (t && !/pending/.test(t.view) && !/요청|예약 중/.test(t.state) && Date.now() - tSend > 1500) {
          if (!/내 예약/.test(t.state)) {
            tRollback = Date.now();
            rollMsg = await evaluate(client, `document.getElementById('messages').textContent.replace(/\\s+/g,' ').slice(-200)`);
            break;
          }
        }
        await sleep(250);
      }
      if (tRollback === null) { say('  🔴 20초 안에 롤백이 안 왔다 — 이 회차 무효'); continue; }

      check('C1 롤백이 실제로 왔다', true, (tRollback - tSend) + 'ms');
      const dt = tRollback - tSend;
      say('  C1 롤백까지 = ' + dt + 'ms → ' + (dt < 5200 ? '**서버 실패 응답(≈4.5초)이 이겼다**' : '**내 자체 타임아웃(6초)이 이겼다**'));
      say('  C2 문구 = ' + JSON.stringify(rollMsg.slice(-150)));
      say('  화면의 그 칸 = ' + JSON.stringify(await evaluate(client, tgt(free))));

      /* C3·C4·C5 — 장치 쪽. 자가 치유가 돈다면 다음 S 프레임(~1.1초)에 로그가 말한다 */
      await sleep(6000);
      const fresh = logLines().slice(logBefore);
      const mism = fresh.filter(l => /불일치/.test(l));
      const redis = fresh.filter(l => /재하달|dispatch|→ARD C/.test(l));
      /* 단언으로 올린다 — say() 로만 적으면 **분모에 안 잡히고**, 그러면 이 케이스가
         통째로 안 돌아도 "검사 0건"이 아니라 조용히 지나간다. */
      check('C5 서버가 불일치를 감지했다 (0건이면 socket 의 소스 판독이 틀린 것이고 불일치가 영구다)',
            mism.length > 0, mism.length + '건');
      say('  C5 서버 로그의 불일치 줄 = ' + (mism.length ? mism.length + '건' : '**0건**'));
      for (const l of mism.slice(0, 3)) say('     ' + l.trim());
      for (const l of redis.slice(0, 3)) say('     ' + l.trim());
      say('  C3/C4 판정 근거 — 위 줄이 있으면 불일치가 감지·치유된 것이고, 0건이면 **아무도 안 고쳤다**');
      say('  최종 화면 = ' + JSON.stringify(await evaluate(client, tgt(free))));
      writeFileSync(new URL('blink-C' + i + '-log.txt', OUT), fresh.join('\n'));
      say('  이 회차의 서버 로그 조각 web/artifacts/blink-C' + i + '-log.txt');
    }
  }

  if (CASE === 'D') {
    /* D. 1초 아이들이 **실제 WS 경로**에서 도는가.
       지금까지는 데모 경로(`demo.send`)로만 봤다 — 실제는 `ws.send` 이고, 둘 다 같은
       stamp() 를 지난다는 것은 **판독**이었다. 그것을 실측으로 바꾼다. */
    const tileOf = (slot) => `(() => { const b = document.querySelector('.tile[data-slot="${slot}"]');
      return b ? { state: b.querySelector('.tile__state').textContent.trim(),
                   meta: b.querySelector('.tile__meta').textContent.trim(),
                   dis: b.getAttribute('aria-disabled') } : null; })()`;
    const lockedCount = `[...document.querySelectorAll('.tile')].filter(x => x.getAttribute('aria-disabled')==='true').length`;

    /* 0) 장치가 정말 붙어 있는가 — 얼었나 진동하나(§3.1.1). 측정 전에 이것부터 본다. */
    const lfs = [];
    for (let i = 0; i < 12; i++) { lfs.push(await evaluate(client, `(state.snapshot&&state.snapshot.device.last_frame_ts)||null`)); await sleep(250); }
    const uniq = [...new Set(lfs)].length;
    check('장치가 붙어 있다 (last_frame_ts 가 진동한다 — 얼어 있으면 부재)', uniq > 2, uniq + '개의 서로 다른 값 / 12표본');

    const free = await evaluate(client, `(() => {
      const b = [...document.querySelectorAll('.tile')].find(x =>
        /빈 자리/.test(x.querySelector('.tile__state').textContent) && x.getAttribute('aria-disabled') !== 'true');
      return b ? b.dataset.slot : null; })()`);
    if (!free) { say('🔴 누를 수 있는 빈 자리가 없다'); }
    else {
      const before = await evaluate(client, `${lockedCount}`);
      say('\n[D] ' + free + ' 예약 — 실제 WS 경로');
      await click(client, `.tile[data-slot="${free}"]`);
      await waitFor(client, `!!document.getElementById('confirm-dialog').open`, { what: '대화상자', timeout: 5000 });
      await click(client, '#confirm-dialog button[value="ok"]');
      const t0 = Date.now();

      /* 아이들이 즉시 걸리는가 — 여기가 데모/WS 가 갈릴 수 있던 자리다 */
      await sleep(150);
      const during = await evaluate(client, tileOf(free));
      const lockedDuring = await evaluate(client, `${lockedCount}`);
      check('보내자마자 아이들이 걸린다 (칸 전체가 잠긴다)', lockedDuring === 10, lockedDuring + '칸 잠김 (누르기 전 ' + before + ')');
      const other = await evaluate(client, `(() => { const b=[...document.querySelectorAll('.tile')].find(x=>x.dataset.slot!=='${free}');
        return b ? b.querySelector('.tile__meta').textContent.trim() : ''; })()`);
      check('남은 시간이 **다른 칸에도** 보인다 (전역이라는 증거)', /잠시 뒤 가능/.test(other), JSON.stringify(other));
      note('예약한 칸 = ' + JSON.stringify(during));

      /* 카운트다운이 실제로 줄어드는가 */
      await sleep(500);
      const mid = await evaluate(client, `(() => { const b=[...document.querySelectorAll('.tile')].find(x=>x.dataset.slot!=='${free}');
        return b ? b.querySelector('.tile__meta').textContent.trim() : ''; })()`);
      note('+0.65초 즈음 = ' + JSON.stringify(mid));

      /* 스스로 풀리는가 */
      await waitFor(client, `[...document.querySelectorAll('.tile')].filter(x => x.getAttribute('aria-disabled')==='true').length < 10`,
                    { what: '아이들 해제', timeout: 5000 });
      const releasedAt = Date.now() - t0;
      check('아이들이 스스로 풀린다', releasedAt >= 900 && releasedAt <= 2000, releasedAt + 'ms 만에 해제');

      /* 예약 자체는 정상으로 끝났는가 — 아이들이 왕복을 깨지 않았다는 확인 */
      const after = await evaluate(client, tileOf(free));
      check('예약이 정상 처리됐다 (아이들이 왕복을 깨지 않는다)', /내 예약/.test(after.state), JSON.stringify(after));

      /* 정리 — 시험이 상태를 남기지 않는다 */
      if (/내 예약/.test(after.state)) {
        await click(client, `.tile[data-slot="${free}"]`);
        await waitFor(client, `!!document.getElementById('confirm-dialog').open`, { what: '취소 대화상자', timeout: 5000 });
        await click(client, '#confirm-dialog button[value="ok"]');
        await waitFor(client, `!/내 예약/.test((document.querySelector('.tile[data-slot="${free}"]').querySelector('.tile__state').textContent))`,
                      { what: '예약 해제', timeout: 15000 }).catch(() => {});
        say('  정리: ' + JSON.stringify(await evaluate(client, tileOf(free))));
      }
    }
  }

  const end = await evaluate(client, SAMPLE);
  say('\n종료 시점: online=' + end.online + ' conn=' + JSON.stringify(end.conn));
  writeFileSync(new URL('blink-' + CASE + '.log', OUT), log.join('\n'));
} catch (e) {
  /* 🔴 2026-08-17: 여기서 `failed` 를 안 올려서 **죽은 실행이 "통과"로 찍혔다.**
     `failed` 는 check() 만 올리는데, 예외로 중단되면 check() 가 아예 안 돈다 →
     0건 실패 → "통과". **아무것도 못 쟀는데 초록이 나오는 검사기**였다.
     `process.exitCode` 로는 사람이 읽는 줄과 tsv 가 안 고쳐진다. 발견 경위:
     monitor 의 "도구를 만들면 돌아간 증거 한 줄을 같이 내라"를 따라 실제로 돌려 봤더니
     첫 줄에 💥 가 뜨는데 마지막 줄이 "통과"였다. **안 돌려 봤으면 못 봤다.** */
  failed++;
  say('💥 ' + e.message);
  writeFileSync(new URL('blink-' + CASE + '.log', OUT), log.join('\n'));
  process.exitCode = 1;
} finally {
  await client.close();
  /* 판정을 적는 곳이 넷이다(콘솔·로그 파일·구간 기록·종료 코드). **전부 같은 값을 봐야 한다** —
     하나만 맞으면 맞는 쪽은 아무도 안 본다. 종료 코드는 셸이 보고 사람은 마지막 줄을 본다. */
  const v = verdict();
  if (ran === 0) process.exitCode = 1;
  markRun('종료 · ' + v);
  say('\n' + v + ' · 구간 기록 web/artifacts/blink-runs.tsv');
}
