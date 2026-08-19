/**
 * 🔴 폴백 경로의 신선도 문구를 측정한다 — 원장 §5.7 의 거짓 경보.
 *
 * **재는 것**: `data_log.json` 의 `last_frame_ts` 가 얼어 있을 때 화면이 그것을
 * *"N 전 수신"* 이라고 **단정**하는가, 아니면 **상한이라고 말하는가.**
 *
 * 실측 근거(원장 §5.29): 파일의 그 값이 **40초 넘게** 한 값이었고 같은 시각 WS 와
 * **96,558ms** 벌어져 있었다. 장치는 1Hz 로 멀쩡히 보내고 있었다.
 * → 그 상태에서 "80초 전 수신"은 **멀쩡한 장치를 죽은 것처럼 말하는 거짓 경보**다.
 *
 * ⚠ **주입이다.** 파일 경로 스냅샷을 `applySnapshot(frame, 'file')` 로 직접 넣는다 —
 *   전송 계층만 건너뛰고 `normalizeSnapshot → renderFrameAge` 는 진짜 경로다.
 *   서버·포트·트래픽이 없다(`file://` + 데모).
 *
 * 사용: node web/tools/fallback-age.mjs            (--head 로 창을 본다)
 */
import { launch, evaluate, sleep } from './cdp.mjs';

const argv = process.argv.slice(2);
const HEAD = argv.includes('--head');
const URL_ = new URL('../../조별과제샘플/web/index.html', import.meta.url).href + '?demo=1';

let pass = 0, fail = 0;
function ok(name, cond, detail) {
  if (cond) { pass++; console.log('  ✅ ' + name); }
  else { fail++; console.log('  ❌ ' + name + (detail ? '  → ' + detail : '')); }
}

const SLOTS = ['A1','A2','A3','A4','A5','B1','B2','B3','B4','B5']
  .map((id, i) => ({ id, occupied: i % 3 === 0 ? 1 : 0, reserved: 0, user_id: null, reserved_at: null, overridden: 0 }));

/** 파일 경로 스냅샷을 넣고 '마지막 프레임' 줄을 읽는다. */
const inject = (tsAgo, lfAgo, online) => `(() => {
  const now = Date.now();
  applySnapshot({ ts: now - ${tsAgo}, device: { online: ${online}, device_id: 'P1',
                  last_frame_ts: now - ${lfAgo} },
                  test_mode: { armed: false, override_count: 0 }, slots: ${JSON.stringify(SLOTS)} }, 'file');
  return document.getElementById('dev-frame').textContent;
})()`;

/* 🔴 §5.18 — 실패 집계는 catch 에서도 올린다. */
let client = null;
try {
  console.log('\n대상: ' + URL_ + '\n(서버 미사용 · 트래픽 0 · 파일 경로 주입)\n');
  client = await launch({ headless: !HEAD });
  await client.send('Page.enable');
  await client.send('Runtime.enable');
  await client.send('Page.navigate', { url: URL_ });

  let ready = false;
  for (let i = 0; i < 100; i++) {
    ready = await evaluate(client, `document.readyState === 'complete' && !!document.getElementById('dev-frame')`).catch(() => false);
    if (ready === true) break;
    await sleep(100);
  }
  ok('화면이 떴다', ready === true);
  if (ready !== true) throw new Error('화면 준비 실패');
  await sleep(300);

  /* ── 사례 1 — §5.29 에서 실제로 관측한 모양 ─────────────────────
     ts == last_frame_ts 였고(모든 표본) 그 기록이 약 80초 묵어 있었다. */
  console.log('[1] 실측된 모양 — 기록이 80초 묵었고 ts == last_frame_ts (장치는 살아 있었다)');
  const t1 = await evaluate(client, inject(80000, 80000, true));
  console.log('  · ' + JSON.stringify(t1));
  /* 🔴 이것이 이 수정의 본체다. "80초 전 수신"은 **장치가 80초 침묵했다는 단정**이고,
     실측 당시 그것은 거짓이었다(1Hz 로 보내고 있었다). */
  ok('장치 침묵을 단정하지 않는다 ("전 수신" 이 없다)', !/전 수신/.test(String(t1)), t1);
  ok('상한이라고 말한다 ("최대"·"상한")', /최대/.test(String(t1)) && /상한/.test(String(t1)), t1);
  ok('모른다고 말한다', /모릅니다|알 수 없/.test(String(t1)), t1);
  ok('기록의 나이를 따로 말한다', /기록은/.test(String(t1)), t1);

  /* ── 사례 2 — 기록은 방금인데 그 시점에 이미 침묵이 길었다 ────────
     이때는 상한이 좁다(기록이 새것이므로). 문구가 그걸 구분해 주는지 본다. */
  console.log('\n[2] 기록은 1초 전인데 그 시점에 이미 30초 침묵이었다');
  const t2 = await evaluate(client, inject(1000, 31000, false));
  console.log('  · ' + JSON.stringify(t2));
  ok('기록 시점 침묵을 그대로 말한다', /기록 시점에 이미/.test(String(t2)), t2);
  /* ⚠ `online:false` 도 **얼어 있는 값**이다. 지금 사실로 말하면 안 된다 —
     기록이 새것이라 이 경우엔 거의 맞지만, **문구가 근거를 밝혀야** 오래된 기록에서 안 속는다. */
  ok('오프라인을 "기록 시점"으로 한정한다', /기록 시점엔 오프라인/.test(String(t2)), t2);
  ok('여기서도 침묵을 단정하지 않는다', !/전 수신/.test(String(t2)), t2);

  /* ── 사례 3 — 🔴 대조군: WS 경로는 고치지 않았다 ─────────────────
     WS 스냅샷은 프레임마다 새로 만들어져 값이 실시간이다. 여기서 "전 수신"은 **참**이고,
     상한 문구로 바꾸면 정확한 정보를 잃는다. **정확한 경로를 흐리지 않았는지**를 잰다. */
  console.log('\n[3] 대조군 — WS 경로는 그대로 단정해도 맞다(값이 실시간이다)');
  const t3 = await evaluate(client, `(() => {
    const now = Date.now();
    applySnapshot({ ts: now, device: { online: true, device_id: 'P1', last_frame_ts: now - 1200 },
      test_mode: { armed: false, override_count: 0 }, slots: ${JSON.stringify(SLOTS)} }, 'ws');
    return document.getElementById('dev-frame').textContent;
  })()`);
  console.log('  · ' + JSON.stringify(t3));
  ok('WS 경로는 "전 수신"으로 단정한다(값이 실시간이라 참이다)', /전 수신/.test(String(t3)), t3);
  ok('WS 경로에 상한 문구를 붙이지 않았다', !/상한/.test(String(t3)), t3);

  /* ── 사례 4 — 값이 없을 때(개정 9 이전 기록)는 기존 문구 유지 ── */
  console.log('\n[4] last_frame_ts 가 없는 기록 — 기존 문구가 유지되는가');
  const t4 = await evaluate(client, `(() => {
    const now = Date.now();
    applySnapshot({ ts: now - 5000, device: { online: true, device_id: 'P1' },
      test_mode: { armed: false, override_count: 0 }, slots: ${JSON.stringify(SLOTS)} }, 'file');
    return document.getElementById('dev-frame').textContent;
  })()`);
  console.log('  · ' + JSON.stringify(t4));
  ok('없는 값을 숫자로 취급하지 않는다', /알 수 없음/.test(String(t4)), t4);
} catch (e) {
  fail++;
  console.log('  💥 예외로 중단: ' + (e && e.message ? e.message : e));
} finally {
  if (client && client.close) { try { await client.close(); } catch { /* 종료 실패는 결과가 아니다 */ } }
}

console.log('\n' + (fail === 0 ? '통과' : '실패') + ' — ' + pass + ' pass / ' + fail + ' fail\n');
process.exit(fail === 0 ? 0 : 1);
