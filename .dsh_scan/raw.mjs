
import { AccountPool, SqliteStore, resolveDbFile } from 'file:///C:/Users/DBC/.dsh/profiles/desktop/node_modules/dsh-zcode-startplan/lib/pool.js'
import { buildZcodeHeaders, ZCODE_BILLING_BALANCE_URL } from 'file:///C:/Users/DBC/.dsh/profiles/desktop/node_modules/dsh-zcode-startplan/lib/upstream.js'
const store = await new SqliteStore(resolveDbFile(process.env))
const pool = new AccountPool({ store, log: () => {} })
const entries = pool.list({ withCredentials: true })
for (const e of entries) {
  const cred = { ...e.credential, app_version: '3.17.1' }
  const url = ZCODE_BILLING_BALANCE_URL + '?app_version=3.17.1&platform=win32'
  const res = await fetch(url, { headers: buildZcodeHeaders(cred, { authorization: 'Bearer ' + cred.zcode_jwt, json: false }), signal: AbortSignal.timeout(180000) })
  const t = await res.text()
  console.log('=== ' + e.nickname + ' status=' + res.status + ' ===')
  console.log(t.slice(0, 1200))
}
process.exit(0)
