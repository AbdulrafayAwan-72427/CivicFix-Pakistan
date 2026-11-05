import { useEffect, useState } from 'react'
import { get } from '../api.js'
import { areaName, matchesSearch } from '../utils.js'
import ComplaintDetails from '../components/ComplaintDetails.jsx'

// Staff view: the active work queue, in FIFO order, plus all active
// complaints. There is no per-staff assignment endpoint in the C++ API
// yet, so this page cannot show "only my assignments" — it shows the
// shared queue and every complaint that is not Resolved/Rejected.
//
// Second wave: search the queue, counts by status, area names
// (instead of bare area IDs), and a refresh button.
export default function StaffPage({ areas = [], users = [] }) {
  const [queue, setQueue] = useState([])
  const [active, setActive] = useState([])
  const [error, setError] = useState('')
  const [search, setSearch] = useState('')

  async function load() {
    try {
      setQueue(await get('/api/queue'))
      const all = await get('/api/complaints')
      setActive(all.filter((c) => c.status !== 'Resolved' && c.status !== 'Rejected'))
    } catch (err) { setError(err.message) }
  }

  useEffect(() => { load() }, [])

  const filteredQueue = queue.filter((c) => matchesSearch(c, search))
  const filteredActive = active.filter((c) => matchesSearch(c, search))
  const byStatus = {}
  active.forEach((c) => { byStatus[c.status] = (byStatus[c.status] || 0) + 1 })

  return (
    <>
      <div className="stat-grid" style={{ marginBottom: 16 }}>
        <div className="stat"><div className="stat-icon green">📋</div><div><strong>{queue.length}</strong><span>In the queue</span></div></div>
        <div className="stat"><div className="stat-icon amber">⏳</div><div><strong>{active.length}</strong><span>Active complaints</span></div></div>
        {Object.entries(byStatus).map(([s, n]) => (
          <div className="stat" key={s}><div className="stat-icon blue">🕐</div><div><strong>{n}</strong><span>{s}</span></div></div>
        ))}
      </div>

      <div className="card" id="work-queue-card">
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', flexWrap: 'wrap', gap: 10 }}>
          <h2 style={{ margin: 0 }}>Work queue (FIFO)</h2>
          <div className="refresh-row">
            <input className="search-input" placeholder="Search queue…" value={search} onChange={(e) => setSearch(e.target.value)} />
            <button className="secondary small" onClick={load}>↻ Refresh</button>
          </div>
        </div>
        <p className="small">This list comes from /api/queue, which uses the custom linked-list ComplaintQueue on the C++ server. First in, first out.</p>
        {error && <p className="error">{error}</p>}
        {filteredQueue.length === 0 && <p>No active complaints in the queue{search ? ' matching this search' : ''}.</p>}
        <ol className="queue-list">
          {filteredQueue.map((c, i) => (
            <li className="queue-item" key={c.id}>
              <span className="queue-num">{i + 1}</span>
              <div>
                <strong>{c.trackingCode} — {c.title}</strong>
                <span>
                  <span className={`badge badge-${c.status.toLowerCase().replace(/\s+/g, '-')}`}>{c.status}</span>{' '}
                  <span className={`pill-priority p${c.priorityScore}`}>P{c.priorityScore}</span>{' '}
                  {c.category} · {areaName(areas, c.areaId)}
                </span>
              </div>
            </li>
          ))}
        </ol>
      </div>
      <div className="card" id="active-complaints-card">
        <h2>Active complaints ({filteredActive.length})</h2>
        {filteredActive.map((c) => <ComplaintDetails key={c.id} complaint={c} areas={areas} users={users} />)}
      </div>
    </>
  )
}
