
import { AccountPool, SqliteStore, resolveDbFile } from 'file:///C:/Users/DBC/.dsh/profiles/desktop/node_modules/dsh-zcode-startplan/lib/pool.js'
import { buildZcodeHeaders, ZCODE_BILLING_CLAIM_URL } from 'file:///C:/Users/DBC/.dsh/profiles/desktop/node_modules/dsh-zcode-startplan/lib/upstream.js'
const store = await new SqliteStore(resolveDbFile(process.env))
const pool = new AccountPool({ store, log: () => {} })
const entries = pool.list({ withCredentials: true })
const candidates = ['zcode-v3-start-plan-1105', 'zcode-v3-start-plan-1105-wk', 'zcode-v3-start-plan-1104', 'zcode-v3-start-plan-1104-wk']
for (const e of entries) {
  const cred = { ...e.credential, app_version: '3.17.1' }
  for (const plan of candidates) {
    const t0 = Date.now()
    try {
      const res = await fetch(ZCODE_BILLING_CLAIM_URL, { method: 'POST',
        headers: buildZcodeHeaders(cred, { authorization: 'Bearer ' + cred.zcode_jwt }),
        body: JSON.stringify({ plan_id: plan }), signal: AbortSignal.timeout(60000) })
      const t = await res.text()
      let msg = ''; try { const jj = JSON.parse(t); msg = (jj.code ?? '') + ' ' + (jj.msg ?? '') + ' ' + (jj.data?.message ?? '') } catch { msg = t.slice(0,150) }
      console.log('claim ' + e.nickname + ' ' + plan + ' -> ' + msg + ' (' + (Date.now()-t0) + 'ms)')
    } catch (err) { console.log('claim ' + e.nickname + ' ' + plan + ' -> ERR ' + String(err?.message ?? err)) }
    await new Promise(r => setTimeout(r, 800))
  }
}
process.exit(0)
