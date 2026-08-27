/**
 * shot.mjs — 화면을 **눈으로 볼 수 있게** 찍는다.
 *
 * 🔴 왜 있나 — DOM 검사는 **배치를 못 본다.**
 * 하니스가 초록이어도 글자가 겹치거나 격자가 화면 밖으로 나가는 것은 안 잡힌다.
 * 값으로 재는 것과 눈으로 보는 것은 **다른 진술**이고, 보고에서 갈라 말해야 한다.
 *
 * 사용:
 *   node web/tools/shot.mjs --file 서머리/server/user8080.html --out samples/shots/8080-idle.png
 *   node web/tools/shot.mjs --file <html> --out <png> --feed <json파일>   봉투를 넣고 찍는다
 *   node web/tools/shot.mjs --file <html> --out <png> --js "<표현식>"     찍기 전에 실행한다
 *   node web/tools/shot.mjs … --wait 4000                                 찍기 전에 기다린다
 *     🔑 카운트다운·경과 초처럼 **시간이 흘러야 보이는 것**을 잡는다
 *   node web/tools/shot.mjs --url http://127.0.0.1:8080/ --out <png>      실기
 *   옵션: --w 1280 --h 900   (기본 1280x900)
 */
import { writeFileSync, mkdirSync } from 'node:fs';
import { readFileSync } from 'node:fs';
import { dirname } from 'node:path';
import { launch, evaluate, sleep } from './cdp.mjs';

const arg = (n, d) => { const i = process.argv.indexOf('--' + n); return i >= 0 ? process.argv[i + 1] : d; };
const file = arg('file', null), url = arg('url', null), out = arg('out', null);
const feedPath = arg('feed', null), js = arg('js', null);
const W = Number(arg('w', 1280)), H = Number(arg('h', 900));
const WAIT = Math.max(0, Number(arg('wait', 0)) || 0);
if (!out || (!file && !url)) { console.error('필요: --out <png> 그리고 --file <html> 또는 --url <주소>'); process.exit(2); }

const URL_ = url || new URL('file://' + (file.startsWith('/') ? file : process.cwd() + '/' + file)).href;
let client = null;
try {
  client = await launch({ headless: true });
  await client.send('Page.enable');
  await client.send('Runtime.enable');
  await client.send('Emulation.setDeviceMetricsOverride', { width: W, height: H, deviceScaleFactor: 1, mobile: false });
  await client.send('Page.navigate', { url: URL_ });
  for (let i = 0; i < 100; i++) {
    if ((await evaluate(client, `document.readyState === 'complete'`).catch(() => false)) === true) break;
    await sleep(100);
  }
  await sleep(200);
  /* 🔑 봉투를 넣는 순서: 먼저 자료, 그 다음 조작. 조작이 자료보다 앞서면 빈 화면을 찍는다. */
  if (feedPath) {
    const arr = JSON.parse(readFileSync(feedPath, 'utf8'));
    for (const m of (Array.isArray(arr) ? arr : [arr])) {
      /* 🔑 **화면마다 봉투 입구가 다르다.** 이용자 화면은 `ws.onmessage`, 관제는 `handleServerMessage`.
         ⚠ 하나만 알면 다른 화면에서 `ReferenceError` 로 죽는다 — 있는 쪽을 쓴다. */
      await evaluate(client, `(() => {
        const m = ${JSON.stringify(JSON.stringify(m))};
        if (typeof handleServerMessage === 'function') { handleServerMessage(JSON.parse(m)); return 'direct'; }
        if (typeof ws === 'object' && ws && ws.onmessage) { ws.onmessage({ data: m }); return 'ws'; }
        throw new Error('봉투를 넣을 입구가 없다 — ws.onmessage 도 handleServerMessage 도 없다');
      })()`);
      await sleep(60);
    }
  }
  if (js) { await evaluate(client, js); await sleep(250); }
  /* 🔑 **시간이 흘러야 보이는 것**이 있다 — 카운트다운·경과 초 같은 것.
     찍기 전에 기다린다. ⚠ 그동안 봉투는 안 온다(주입 모드) — 그것이 실기와 다른 점이다. */
  if (WAIT > 0) { console.log('  · ' + WAIT + 'ms 기다린다 (시간이 흘러야 보이는 것)'); await sleep(WAIT); }
  const r = await client.send('Page.captureScreenshot', { format: 'png' });
  mkdirSync(dirname(out), { recursive: true });
  writeFileSync(out, Buffer.from(r.data, 'base64'));
  console.log('찍었다: ' + out + '  (' + W + 'x' + H + ')  ← ' + URL_);
} catch (e) {
  console.error('실패 — ' + (e && e.message ? e.message : String(e)));
  process.exitCode = 1;
} finally {
  if (client) await client.close().catch(() => {});
}
