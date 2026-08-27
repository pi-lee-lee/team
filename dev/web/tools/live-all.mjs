/**
 * live-all.mjs — **배포 직후 화면 셋을 한 번에 잰다.**
 *
 * ## 🔴 왜 도구인가 — 절차를 문서에 두면 낡는다
 * 배포 직후는 **가장 바쁜 순간**이고, 그때 명령 셋을 기억으로 재구성하면
 * 하나를 빠뜨린다. 그리고 빠뜨린 것은 **안 잰 축**이 되어 결함을 숨긴다.
 * 📖 docs/web/LEDGER.md §5.143 · §5.145
 *
 * ```
 * node web/tools/live-all.mjs                 9900 · 8080 · 8081 (기본)
 * node web/tools/live-all.mjs --secs 15       창 길이(기본 10초)
 * node web/tools/live-all.mjs --only 8081     하나만
 * ```
 *
 * ## ⚠ 이 도구가 **하지 않는 것**
 * ```
 * · 클릭하지 않는다 — 각 하니스의 `--live` 가 원래 클릭 0 이다(실기 자리를 예약하지 않는다)
 * · 판정하지 않는다 — 각 하니스의 초록/빨강을 **모아서 보여 줄 뿐**이다
 * 🔑 그리고 **미측정을 따로 센다.** 미측정은 "괜찮다" 가 아니라 **"못 쟀다"** 다 —
 *   배포 직후에는 그 목록이 다음에 무엇을 해야 하는지를 말해 준다
 * ```
 *
 * ⚠ **관측이 도는 중이면 monitor 에게 먼저 알려라.** 이 도구는 서버에 연결을 셋 더한다
 * (짧게 붙고 끊지만, 서버는 연결마다 스냅샷을 낸다).
 */
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
const run = promisify(execFile);

const arg = (n, d) => { const i = process.argv.indexOf('--' + n); return i >= 0 ? process.argv[i + 1] : d; };
const SECS = String(Math.max(5, Number(arg('secs', 10)) || 10));
const ONLY = arg('only', null);

const TARGETS = [
  { port: '9900', tool: 'boards.mjs',   what: '관제 화면' },
  { port: '8080', tool: 'user8080.mjs', what: '이용자 · 주차위치 확인' },
  { port: '8081', tool: 'user8081.mjs', what: '이용자 · 자리 예약' },
].filter((t) => !ONLY || t.port === ONLY);

/** 하니스 꼬리줄에서 수를 뽑는다. ⚠ **못 뽑으면 `null` 이다** — `0` 으로 읽지 마라(§5.140). */
function parse(out) {
  const m = out.match(/(\d+)\s*pass\s*\/\s*(\d+)\s*fail(?:\s*\/\s*(\d+)\s*미측정)?/);
  if (!m) return null;
  return { pass: Number(m[1]), fail: Number(m[2]), skip: m[3] ? Number(m[3]) : 0 };
}
/** 미측정 사유를 그대로 모은다 — **뭉치지 않는다.** 사유마다 다음에 할 일이 다르다. */
function skips(out) {
  return out.split('\n').filter((l) => l.includes('⏭')).map((l) => l.replace(/^\s*⏭\s*/, '').trim());
}

console.log('\n🔎 배포 직후 실기 측정 — 창 ' + SECS + '초 · 클릭 0 · 상행은 화면이 원래 내는 것만');

/* ══════════════════════════════════════════════════════════════════════
   🔴🔴 **첫 줄은 "배포가 실제로 됐나" 다.**
   재고 나서 *"이게 새 판인가?"* 를 묻기 시작하면 그 측정은 이미 값을 잃는다 —
   초록도 빨강도 **어느 판본의 것인지 모르면** 뜻이 없다.
   📖 docs/web/LEDGER.md §5.145 · README §3-F
   ⚠ 서버 로그 쪽 표지(`←ARD` 모드 · `⏱ 소크`)는 **내 영역이 아니다** — monitor·socket 이 잰다.
     여기서는 **화면이 답할 수 있는 것만** 묻는다(§"각 매체가 답할 수 있는 것만").
   ══════════════════════════════════════════════════════════════════════ */
console.log('\n[0] 무엇을 재는 중인가 — **배포가 됐는지부터**');
const { readFile } = await import('node:fs/promises');
const STAMP_RE = /<meta\s+name="screen-build"\s+content="([^"]*)">/;
const srcOf = { '9900': '서머리/server/index.html', '8080': '서머리/server/user8080.html',
                '8081': '서머리/server/user8081.html' };
const pathOf = { '9900': '/index.html', '8080': '/', '8081': '/' };
for (const t of TARGETS) {
  let served = null, repo = null, err = null;
  try {
    const r = await fetch('http://127.0.0.1:' + t.port + pathOf[t.port], { signal: AbortSignal.timeout(4000) });
    const html = await r.text();
    const m = html.match(STAMP_RE);
    served = m ? m[1] : '(표지 없음)';
  } catch (e) { err = (e && e.message) || String(e); }
  try {
    const m = (await readFile(srcOf[t.port], 'utf8')).match(STAMP_RE);
    repo = m ? m[1] : '(표지 없음)';
  } catch { repo = '(원본을 못 읽었다)'; }
  if (err) {
    console.log('  🔴 ' + t.port + '  서버에 못 붙었다 — ' + err);
    console.log('       → **아직 안 떴거나 포트가 다르다.** 아래 측정은 전부 공허하다');
  } else {
    /* 🔑 **같으면 배포 단계가 없는 구조다**(서버가 그 파일을 직접 읽는다).
       다르면 사이에 복사 단계가 있고, 그때는 **어느 쪽이 새것인지**를 따로 봐야 한다. */
    const same = served === repo;
    console.log('  ' + (same ? '✅' : '🔴') + ' ' + t.port + '  서버 "' + served + '"'
              + (same ? '  = 저장소 원본' : '  ≠ 저장소 원본 "' + repo + '"'));
    if (!same) console.log('       → **서버가 내주는 것이 우리가 고친 파일이 아니다.** 먼저 이것부터 닫아라');
  }
}
console.log('  ⚠ `__UNSTAMPED__` 를 "오래됐다" 로 읽지 마라 — 이 구조에서는 **표지를 찍는 주체가 없다**');
const rows = [];
for (const t of TARGETS) {
  process.stdout.write('\n── ' + t.port + ' ' + t.what + ' … ');
  let out = '';
  try {
    const r = await run('node', ['web/tools/' + t.tool, '--live', t.port, '--secs', SECS],
                        { cwd: process.cwd(), maxBuffer: 8 << 20 });
    out = r.stdout + r.stderr;
  } catch (e) {
    /* 하니스는 fail 이 있으면 **exit 1** 이다 — 그것도 정상 결과다. 출력은 그대로 쓴다. */
    out = String((e && e.stdout) || '') + String((e && e.stderr) || '');
    if (!out) out = '실행 실패: ' + (e && e.message ? e.message : String(e));
  }
  const n = parse(out);
  rows.push({ ...t, n, skips: skips(out), out });
  console.log(n ? (n.pass + ' pass / ' + n.fail + ' fail' + (n.skip ? ' / ' + n.skip + ' 미측정' : ''))
                : '🔴 결과를 못 읽었다');
  if (n && n.fail > 0) {
    for (const l of out.split('\n').filter((x) => x.includes('❌'))) console.log('     ' + l.trim());
  }
}

console.log('\n' + '─'.repeat(64));
let tp = 0, tf = 0, ts = 0, unread = 0;
for (const r of rows) {
  if (!r.n) { unread++; continue; }
  tp += r.n.pass; tf += r.n.fail; ts += r.n.skip;
}
console.log('  합계 — ' + tp + ' pass / ' + tf + ' fail' + (ts ? ' / ' + ts + ' 미측정' : ''));
/* 🔴 **결과를 못 읽은 것을 `0 fail` 에 섞지 마라.** 그건 "통과" 가 아니라 "못 쟀다" 다. */
if (unread) console.log('  🔴 결과를 못 읽은 화면 ' + unread + '개 — 이 합계는 그만큼 **분모가 빈다**');

if (ts) {
  console.log('\n  ⏭ 미측정 — **다음에 무엇을 해야 하는지**가 여기 있다');
  for (const r of rows) for (const s of r.skips) console.log('     · [' + r.port + '] ' + s);
}
/* 🔵 **`0/5` → `5/5` 의 전이**를 값으로 남긴다 — 카메라 배포가 됐는지의 판별자다.
   ★ 2026-08-27 03:2x 실측이 **`0/5`** 였다(옛 서버). **전이가 있으므로 이 수는 뜻이 있다.**
   ⚠ 못 읽으면 `null` 이다. `0` 으로 읽지 마라 — 그건 "안 온다" 가 아니라 "못 쟀다" 다 */
{
  const b = rows.find((r) => r.port === '9900');
  const m = b && b.out.match(/촬영 진행 다섯 칸이 실려 온다[^\n]*?\((\d)\/5\)/);
  const n = m ? Number(m[1]) : null;
  console.log('\n  📷 촬영 진행 다섯 칸 : '
    + (n === null ? '**못 읽었다**(관제 측정이 그 자리에 못 갔다)'
       : n === 5 ? '**5/5 — 카메라 봉투가 배포됐다**'
       : n + '/5 — ' + (n === 0 ? '**옛 서버다.** 화면은 안 깨지고 조용히 넘긴다'
                                : '🔴 **일부만 온다.** 화면이 나머지 칸을 조용히 못 그린다')));
}

console.log('\n  🔑 이것은 **지금 실기에서 그렇게 나온다** 는 진술이다.');
console.log('     주입 모드(`--live` 없이)의 초록과 **다른 진술**이라 섞어 적지 마라.');
process.exit(tf > 0 || unread > 0 ? 1 : 0);
