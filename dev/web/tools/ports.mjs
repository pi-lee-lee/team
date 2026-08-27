/**
 * ports.mjs — **운영 포트 목록을 정본에서 읽는다.** (2026-08-20)
 *
 * ## 🔴 왜 있나 — 손으로 든 목록은 조용히 빈다
 *
 * 여섯 도구가 `['9900','9991','5500']` 을 **각자** 하드코딩하고 있었다. 그 목록의 뜻은
 * *"이 포트는 운영이니 실수로 치지 마라"* 인데, **포트가 바뀌면 목록이 안 맞아서
 * 가드가 그대로 통과한다.**
 *
 * > ### **가드가 사라지는 게 아니라, 있는 채로 빈다.**
 *
 * 그리고 실제로 그렇게 됐다 — `config.h` 는 이미 `9990/8888/8911` 인데
 * 도구들은 아직 `9900/9991/5500` 을 지키고 있었다. **새 운영 포트가 무방비였다.**
 *
 * 🔑 같은 사고가 같은 날 셋 났다(잠금 목록의 경로가 이사로 전부 사라짐 · `cmp.py` 오용으로
 * 아무것도 안 보고 `exit=0` · 이 가드). **셋 다 목록을 손으로 들고 있어서 그랬다.**
 *
 * ## 그래서 규칙 하나
 * **정본에서 읽는다. 못 읽으면 통과가 아니라 경보다.**
 * 기본값으로 조용히 물러서지 않는다 — 물러서는 순간 이 파일이 또 하나의 손으로 든 목록이 된다.
 */
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

/** 🔴 정본. socket 소유 파일이고 **읽기만 한다**. */
export const CONFIG_H = fileURLToPath(new URL('../../조별과제샘플/server/config.h', import.meta.url));

/**
 * `config.h` 의 `static const int PORT_* = NNNN;` 를 전부 읽는다.
 * @returns {{ports: string[], named: Record<string,string>, source: string}}
 */
export function readProdPorts(path = CONFIG_H) {
  const CONFIG_H = path;   /* 🔑 인자를 받는 이유는 **음성 대조** 하나뿐이다 —
                              "정본이 깨졌을 때 정말 경보가 나는가"를 재려면 깨진 것을 줘 봐야 한다.
                              기본값은 언제나 진짜 정본이다. */
  let src;
  try { src = readFileSync(CONFIG_H, 'utf8'); }
  catch (e) {
    throw new Error('🔴 운영 포트 정본을 못 읽었다 (' + CONFIG_H + '): ' + e.message
      + '\n   → 가드가 빈 채로 도는 것보다 멈추는 것이 낫다. 경로가 바뀌었으면 이 파일을 고쳐라.');
  }
  const named = {};
  const re = /static\s+const\s+int\s+(PORT_[A-Z_]+)\s*=\s*(\d+)\s*;/g;
  let m;
  while ((m = re.exec(src)) !== null) named[m[1]] = m[2];

  /* 🔴 **이행 구간의 구멍** — 선언과 실제가 다를 수 있다.
     2026-08-20 실측: `config.h` 는 이미 `9990` 인데 **운영 서버는 아직 `9900` 에서 돌고 있었다.**
     선언만 지키면 **정작 도는 것이 무방비**가 된다 — 원래 문제의 거울상이다.
     🔑 그래서 같은 파일의 `옛 값:` 주석도 읽어 **둘 다** 지킨다.
     ⚠ **주석을 읽는다는 것을 알고 쓴다.** 정본 안이라 손으로 든 목록보다는 낫고,
     socket 이 이행이 끝나 그 줄을 지우면 **저절로 목록에서 빠진다**(만료 조건이 붙어 있다). */
  const legacy = {};
  const lm = src.match(/옛 값[^\n]*/);
  if (lm) {
    const nums = lm[0].match(/\d{2,5}/g) || [];
    nums.forEach((n, i) => { legacy['LEGACY_' + i] = n; });
  }
  const ports = [...new Set([...Object.values(named), ...Object.values(legacy)])];
  Object.assign(named, legacy);
  /* 🔴 **하나도 못 찾으면 통과가 아니라 경보다.** 파싱이 깨졌는데 빈 목록을 돌려주면
     모든 포트가 "운영이 아님"이 되어 **가드가 있는 채로 빈다** — 이 파일이 막으려는 바로 그것이다. */
  if (!ports.length) {
    throw new Error('🔴 정본에서 포트를 하나도 못 읽었다 (' + CONFIG_H + ')'
      + '\n   → `static const int PORT_* = N;` 형식이 바뀌었을 수 있다. **가드가 빈 채로 돌지 않게 멈춘다.**');
  }
  return { ports, named, source: CONFIG_H };
}

/**
 * 🔴 도구의 첫 줄에서 부른다. 운영 포트면 **거부하고 끝낸다.**
 * @param {string|number} port 대상 포트
 * @param {{allow?: boolean, label?: string}} opts `allow` 가 참이면 통과시키되 **크게 찍는다**
 */
export function guardProdPort(port, opts = {}) {
  const { ports, named, source } = readProdPorts();
  const p = String(port);
  const hit = Object.keys(named).find((k) => named[k] === p);
  if (!ports.includes(p)) return { ok: true, ports };

  if (!opts.allow) {
    console.error('🔴 운영 포트 거부: ' + p + ' (' + hit + ')'
      + '\n   정본: ' + source
      + '\n   지금 운영 포트: ' + JSON.stringify(named)
      + '\n   → 정말 운영에서 재야 하면 ' + (opts.label || '--allow-prod') + ' 를 손으로 줘라.');
    process.exit(2);
  }
  console.log('🔴🔴 운영 인스턴스(' + p + ' · ' + hit + ')에 붙는다 — 손으로 준 예외다.');
  console.log('     사용자 브라우저가 같은 화면을 볼 수 있다. 조작은 최소로, 전부 기록한다.\n');
  return { ok: true, ports, prod: true };
}
