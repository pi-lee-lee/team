#!/usr/bin/env node
/**
 * deploy-screen.mjs — 화면 배포와 그 **판정**  (REQ-0240 · 2026-08-19)
 *
 * ## 이 도구가 있는 까닭
 *
 * `serve_file()` 은 `index.html` 을 **cwd 상대경로**로 연다(`server.cpp:3585`, 폴백 없음).
 * 즉 **서버를 어느 디렉터리에서 띄웠는가**가 곧 **어느 화면이 나가는가**다.
 * 2026-08-19 에 :9900 이 **08-17 사본**을 내주고 있었는데 아무 신호도 없었다 —
 * 배포 절차서(`docs/net/DEPLOY-CHECKLIST.md` §0.5.1)의 확인이
 * `ls -l`(있나) 과 `curl → 200`(주나) 뿐이라 **낡은 사본이 둘 다 통과**했기 때문이다.
 *
 * > **존재형 검사는 유물을 통과시킨다. 물어야 할 것은 "있나"가 아니라 "같은가"다.**
 *
 * ## 쓰는 법
 *
 *   node web/tools/deploy-screen.mjs --check              # 기본. :9900 이 실제로 여는 파일을 잰다
 *   node web/tools/deploy-screen.mjs --check --port 10000 # 다른 인스턴스
 *   node web/tools/deploy-screen.mjs --check --target <경로>
 *   node web/tools/deploy-screen.mjs --check --url http://127.0.0.1:9900/index.html
 *   node web/tools/deploy-screen.mjs --deploy [--port 9900 | --target <경로>]
 *
 * ⚠ `--check` 의 기본 경로는 **파일시스템만 읽는다. 서버에 접속하지 않는다.**
 *   관측 창이 도는 동안에도 안전하다. `--url` 을 줄 때만 HTTP 요청이 나간다.
 *
 * 🔴 `--deploy` 는 **web-engineer 가 실행할 수 없다.** `~/parking-bin` 은 프로젝트 밖이라
 *   소유권 훅이 `outside_project_owner: root` 로 막는다. **root 나 socket-engineer 가 실행한다.**
 */
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { readFile, writeFile, copyFile, stat } from 'node:fs/promises';
import { resolve, join } from 'node:path';
import { compare, readStamp, contentSha, applyStamp, makeStamp, UNSTAMPED, SOURCE_HTML } from './screen-build.mjs';
import { readProdPorts } from './ports.mjs';

const pexec = promisify(execFile);
const argv = process.argv.slice(2);
const has = (f) => argv.includes(f);
const val = (f, d) => { const i = argv.indexOf(f); return i >= 0 && argv[i + 1] ? argv[i + 1] : d; };

/* 🔑 기본 원본은 **이 파일 위치 기준**이다(`screen-build.mjs` 의 `SOURCE_HTML`).
   ⚠ 전에는 cwd 상대 문자열이었다 — **어디서 실행하느냐로 원본이 달라지는** 형태이고,
   그것이 바로 이 도구가 잡으려는 결함(REQ-0240)과 같은 병이다. 도구가 그 병을 앓으면 안 된다. */
const SOURCE = resolve(val('--source', SOURCE_HTML));
/* 🔴 기본 포트도 **정본에서 읽는다**(`config.h` 의 `PORT_HTTP`). 손으로 `9900` 을 들고 있었더니
   서버가 9990 으로 옮긴 순간 **"포트를 듣는 프로세스가 없다"로 죽었다**(2026-08-20 실측).
   ⚠ 그건 시끄럽게 죽어서 그나마 나았다 — **조용히 빈 가드**와 대비된다(ports.mjs 머리말). */
const PORT = val('--port', (() => {
  try { return readProdPorts().named.PORT_HTTP || '9990'; } catch (e) { return '9990'; }
})());
const MODE = has('--deploy') ? 'deploy' : 'check';

let pass = 0, fail = 0, unknown = 0;
const ok = (n, c, d) => { if (c) { pass++; console.log('  ✅ ' + n); } else { fail++; console.log('  ❌ ' + n + (d ? '\n       → ' + d : '')); } };
/* 🔑 못 잰 것을 통과로도 실패로도 두지 않는다(원장 §5.40). */
const unk = (n, why) => { unknown++; console.log('  ⏭ ' + n + '  → 측정 불가: ' + why); };

/** 포트를 듣는 프로세스의 cwd 를 찾는다 → 그 디렉터리의 index.html 이 **실제로 서빙되는 파일**이다. */
async function servedPathFromPort(port) {
  let pid;
  try {
    const { stdout } = await pexec('lsof', ['-nP', '-iTCP:' + port, '-sTCP:LISTEN', '-Fp']);
    const m = stdout.match(/^p(\d+)/m);
    if (!m) return { err: '포트 ' + port + ' 를 듣는 프로세스가 없다' };
    pid = m[1];
  } catch (e) { return { err: 'lsof 실패(포트 ' + port + '): ' + e.message }; }
  try {
    const { stdout } = await pexec('lsof', ['-p', pid, '-a', '-d', 'cwd', '-Fn']);
    const m = stdout.match(/^n(.+)$/m);
    if (!m) return { err: 'pid ' + pid + ' 의 cwd 를 못 읽었다' };
    return { pid, cwd: m[1], path: join(m[1], 'index.html') };
  } catch (e) { return { err: 'cwd 조회 실패(pid ' + pid + '): ' + e.message }; }
}

async function gitShort() {
  try {
    const { stdout } = await pexec('git', ['rev-parse', '--short', 'HEAD']);
    const short = stdout.trim();
    const { stdout: st } = await pexec('git', ['status', '--porcelain', '--', SOURCE]);
    return short + (st.trim() ? '+dirty' : '');
  } catch { return 'nogit'; }
}

function localStamp() {
  /* 🔴 현지 시각으로 찍는다. 내 도구가 UTC 로 찍어 남의 로그와 시(hour)가 어긋난 적이 있다(원장 §5.64). */
  const d = new Date(), p = (n) => String(n).padStart(2, '0');
  return d.getFullYear() + '-' + p(d.getMonth() + 1) + '-' + p(d.getDate())
       + 'T' + p(d.getHours()) + ':' + p(d.getMinutes()) + ':' + p(d.getSeconds());
}

const source = await readFile(SOURCE, 'utf8');
const srcSha = contentSha(source);
console.log('\n화면 판본 — ' + (MODE === 'deploy' ? '배포' : '판정'));
console.log('  원본: ' + SOURCE);
console.log('  원본 내용 해시(정규화 후): ' + srcSha + '   git: ' + await gitShort() + '\n');

/* ── 서빙 대상 결정 ───────────────────────────────────────────── */
let target = val('--target', null);
let servedInfo = null;
const url = val('--url', null);

if (!target && !url) {
  servedInfo = await servedPathFromPort(PORT);
  if (servedInfo.err) {
    console.log('  ⚠ ' + servedInfo.err);
    console.log('    → --target 이나 --url 로 직접 지정해라.\n');
    process.exit(2);
  }
  target = servedInfo.path;
  console.log('  :' + PORT + ' 를 듣는 pid ' + servedInfo.pid + ' 의 cwd = ' + servedInfo.cwd);
  console.log('  🔑 그러므로 **실제로 서빙되는 파일**은 ' + target + '\n');
}

if (MODE === 'check') {
  let servedHtml = null, where = null;
  if (url) {
    where = url;
    console.log('  ⚠ HTTP 요청을 보낸다(관측 창을 건드릴 수 있다): ' + url);
    try {
      const r = await fetch(url);
      if (!r.ok) { ok('🔴 화면을 받았다 (' + url + ')', false, 'HTTP ' + r.status); }
      else servedHtml = await r.text();
    } catch (e) { ok('🔴 화면을 받았다 (' + url + ')', false, e.message); }
  } else {
    where = target;
    try { servedHtml = await readFile(target, 'utf8'); }
    catch (e) { ok('🔴 서빙 파일을 읽었다 (' + target + ')', false, e.message + ' — 없으면 GET / 는 404 다'); }
  }

  if (servedHtml !== null) {
    const c = compare(servedHtml, source);
    console.log('  · 서빙본 해시: ' + c.servedSha + '   표지: ' + (c.stamp.present ? c.stamp.raw : '(표지 없음)'));
    if (c.stamp.present && c.stamp.at) console.log('  · 배포 시각: ' + c.stamp.at + '   git: ' + c.stamp.git);
    console.log('');

    /* 🔴 긍정형 단언. "옛 판이 아니다"가 아니라 "받은 것 == 기대하는 것". */
    ok('🔴 서빙되는 화면의 내용이 저장소 원본과 같다  (' + c.servedSha + ' == ' + c.sourceSha + ')',
       c.same,
       '서빙본 ' + c.servedSha + ' ≠ 원본 ' + c.sourceSha + '\n         '
       + '서빙 위치: ' + where + '\n         '
       + '🔴 **화면이 낡았다.** 이 상태에서 화면을 재면 남의 판을 재고 내 것이라고 보고한다.');

    if (!c.stamp.present) {
      ok('표지가 페이지에 있다', false, '`<meta name="screen-build">` 가 없다 — 08-17 이전 판이거나 표지가 지워졌다');
    } else if (c.stamp.raw === UNSTAMPED) {
      unk('배포 유래(누가·언제 올렸나)', '표지가 __UNSTAMPED__ — 저장소 원본이 그대로 서빙되고 있다(배포 도구를 안 거쳤다)');
    } else if (c.stampHonest === null) {
      unk('표지가 자기 내용과 맞는가', '표지에 src= 가 없다');
    } else {
      /* 표지가 거짓말을 하는 경우: 배포 뒤 그 자리에서 내용만 고친 것. */
      ok('표지가 자기 내용과 맞다 (배포 뒤 손댄 흔적 없음)', c.stampHonest,
         '표지는 src=' + c.stamp.src + ' 라는데 실제 내용은 ' + c.servedSha + ' 다');
    }

    try {
      const s = await stat(where);
      console.log('  · 서빙본 mtime: ' + s.mtime.toLocaleString('ko-KR') + '  (' + s.size + 'B)');
    } catch {}
  }

  console.log('\n' + '─'.repeat(60));
  console.log('  ' + pass + ' pass / ' + fail + ' fail / ' + unknown + ' 미측정');
  if (fail > 0) {
    console.log('\n  🔴 고치는 법 — 둘 중 하나다:');
    console.log('     ① 배포:   node web/tools/deploy-screen.mjs --deploy --port ' + PORT);
    console.log('        (⚠ ~/parking-bin 은 프로젝트 밖이라 **root 나 socket 이 실행해야 한다**)');
    console.log('     ② 근본:   서버에 `--webroot` 를 넣어 cwd 의존을 없앤다 (socket · 미해결 항목)');
  }
  process.exit(fail > 0 ? 1 : 0);
}

/* ── 배포 ─────────────────────────────────────────────────────── */
if (MODE === 'deploy') {
  if (!target) { console.log('  ⚠ --target 또는 --port 가 필요하다'); process.exit(2); }
  const stamp = makeStamp({ src: srcSha, git: await gitShort(), at: localStamp() });
  const stamped = applyStamp(source, stamp);
  if (stamped === null) {
    console.log('  ❌ 원본에 `<meta name="screen-build">` 표지가 없다. 먼저 표지를 넣어라.');
    process.exit(2);
  }
  /* 직전 판을 남긴다 — socket 이 바이너리에 하는 것과 같은 규율(srv_parking.prev-*).
     🔴 **`--no-backup` 은 대상이 *git 이 추적하는 파일* 일 때 쓴다** — 직전 판을 git 이 이미 갖고 있고,
     그때 `.prev-*` 는 안전망이 아니라 **쓰레기**다. 특히 `조별과제샘플/dev_server/` 는
     **기여자에게 압축으로 나가는 폴더**라 그 안의 백업 파일이 그대로 배포된다.
     ⚠ 그리고 `.prev-*` 의 소유권은 `**/dev_server/**`(socket) 으로 떨어져 **web 이 지우지도 못한다.**
     🔑 `~/parking-bin` 처럼 **git 밖 경로**에는 쓰지 마라 — 거기서는 백업이 유일한 되돌림이다. */
  if (has('--no-backup')) {
    console.log('  · 직전 판 보관 생략(--no-backup) — git 이 직전 판을 갖고 있는 대상에만 써라');
  } else try {
    const prev = await readFile(target, 'utf8');
    const prevSha = contentSha(prev);
    if (prevSha !== srcSha) {
      await copyFile(target, target + '.prev-' + prevSha);
      console.log('  · 직전 판 보관: ' + target + '.prev-' + prevSha);
    }
  } catch { console.log('  · 직전 판 없음(새로 놓는다)'); }

  await writeFile(target, stamped, 'utf8');
  console.log('  ✅ 배포: ' + target);
  console.log('     표지: ' + stamp);

  /* 🔴 배포하고 끝내지 않는다 — 방금 쓴 것을 되읽어 확인한다. */
  const back = await readFile(target, 'utf8');
  const c = compare(back, source);
  ok('🔴 배포본이 원본과 같다 (되읽어 확인)', c.same, c.servedSha + ' ≠ ' + c.sourceSha);
  ok('표지가 자기 내용과 맞다', c.stampHonest === true, JSON.stringify(c.stamp));
  console.log('\n  ⚠ 서버 재기동은 필요 없다 — `serve_file()` 이 요청마다 파일을 연다.');
  console.log('  ' + pass + ' pass / ' + fail + ' fail');
  process.exit(fail > 0 ? 1 : 0);
}
