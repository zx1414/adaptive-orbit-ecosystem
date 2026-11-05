
import { AccountPool, SqliteStore, resolveDbFile } from 'file:///C:/Users/DBC/.dsh/profiles/desktop/node_modules/dsh-zcode-startplan/lib/pool.js'
import { buildZcodeHeaders, ZCODE_BILLING_BALANCE_URL } from 'file:///C:/Users/DBC/.dsh/profiles/desktop/node_modules/dsh-zcode-startplan/lib/upstream.js'
const store = await new SqliteStore(resolveDbFile(process.env))
const pool = new AccountPool({ store, log: () => {} })
const entries = pool.list({ withCredentials: true })
const fmt = (s) => s ? new Date(s*1000).toLocaleString('zh-CN', { timeZone: 'Asia/Shanghai', hour12: false }) : null
for (const e of entries) {
  const cred = { ...e.credential, app_version: '3.17.1' }
  const url = ZCODE_BILLING_BALANCE_URL + '?app_version=3.17.1&platform=win32'
  let d = {}
  try {
    const res = await fetch(url, { headers: buildZcodeHeaders(cred, { authorization: 'Bearer ' + cred.zcode_jwt, json: false }), signal: AbortSignal.timeout(180000) })
    const j = await res.json(); d = j.data ?? {}
  } catch (err) { console.log('=== ' + e.nickname + ' BALANCE ERR ' + String(err?.message ?? err)); continue }
  console.log('=== ' + e.nickname + ' (server ' + fmt(d.server_time) + ') ===')
  for (const p of d.plans ?? []) {
    console.log('  [plan] ' + p.plan_id + ' "' + p.name + '" 至 ' + fmt(p.ends_at))
    for (const en of p.entitlements ?? []) console.log('      [ent] ' + en.show_name + ' grant=' + en.grant_units + ' effective=' + fmt(en.effective_at))
  }
  for (const b of d.balances ?? []) console.log('  [bal] ' + b.show_name + ' | ' + b.plan_id + ' | ' + b.available_units + '/' + b.total_units + ' | 至 ' + fmt(b.expires_at))
}
process.exit(0)
