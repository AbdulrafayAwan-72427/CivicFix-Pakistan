import { useEffect, useState } from 'react'
import { get } from '../api.js'
import { areaName, areaShort, matchesSearch, statusClass } from '../utils.js'
import ComplaintDetails from '../components/ComplaintDetails.jsx'
import EmptyState from '../components/EmptyState.jsx'
import Feedback from '../components/Feedback.jsx'

// Staff view: the active work queue in FIFO order, the complaints
// assigned to this staff member, and all active complaints as one
// compact table with a single detail panel — not a stack of full
// cards repeating the same complaint.
const PAGE_SIZE = 10

export default function StaffPage({ areas = [], users = [] }) {
  const [queue, setQueue] = useState([])
  const [active, setActive] = useState([])
  const [myAssignments, setMyAssignments] = useState(undefined) // undefined = loading, null = endpoint unavailable
  const [error, setError] = useState('')
  const [searchInput, setSearchInput] = useState('')
  const [search, setSearch] = useState('')
  const [loading, setLoading] = useState(true)
  const [page, setPage] = useState(1)
  const [selected, setSelected] = useState(null)

  async function load() {
    setLoading(true)
    try {
      setQueue(await get('/api/queue'))
      const all = await get('/api/complaints')
      setActive(all.filter((c) => c.status !== 'Resolved' && c.status !== 'Rejected'))
      try { setMyAssignments(await get('/api/me/assignments')) }
      catch { setMyAssignments(null) }
    } catch (err) { setError(err.message); setMyAssignments(null) } finally { setLoading(false) }
  }

  useEffect(() => { load() }, [])
  useEffect(() => {
    const t = setTimeout(() => setSearch(searchInput), 250)
    return () => clearTimeout(t)
  }, [searchInput])
  useEffect(() => { setPage(1) }, [search])

  const filteredQueue = queue.filter((c) => matchesSearch(c, search))
  const filteredActive = active.filter((c) => matchesSearch(c, search))
  const totalPages = Math.max(1, Math.ceil(filteredActive.length / PAGE_SIZE))
  const safePage = Math.min(page, totalPages)
  const pageItems = filteredActive.slice((safePage - 1) * PAGE_SIZE, safePage * PAGE_SIZE)
  const byStatus = {}
  active.forEach((c) => { byStatus[c.status] = (byStatus[c.status] || 0) + 1 })

  const skeleton = (
    <div className="skeleton-list" aria-busy="true" aria-label="Loading">
      <div className="skeleton-row" /><div className="skeleton-row" /><div className="skeleton-row" />
    </div>
  )

  return (
    <>
      <Feedback error={error} message="" />

      <div className="stat-grid mb-16">
        <div className="stat"><div className="stat-icon green" aria-hidden="true">📋</div><div><strong>{queue.length}</strong><span>In the queue</span></div></div>
        <div className="stat"><div className="stat-icon amber" aria-hidden="true">⏳</div><div><strong>{active.length}</strong><span>Active complaints</span></div></div>
        {Object.entries(byStatus).map(([s, n]) => (
          <div className="stat" key={s}><div className="stat-icon slate" aria-hidden="true">🕐</div><div><strong>{n}</strong><span>{s}</span></div></div>
        ))}
      </div>

      <div className="card mb-16" id="my-assignments-card">
        <h2 className="mt-0">My assignments</h2>
        {myAssignments === undefined && loading ? skeleton : myAssignments === null ? (
          <p className="small">These are all open complaints for your city. Assignment to individual staff is coming soon.</p>
        ) : myAssignments.length === 0 ? (
          <EmptyState icon="📭" title="No complaints assigned to you yet" hint="New assignments from an admin will appear here." />
        ) : (
          <div className="table-wrap">
            <table>
              <caption className="sr-only">Complaints assigned to you</caption>
              <thead><tr><th scope="col">Code</th><th scope="col">Title</th><th scope="col">Category</th><th scope="col">Area</th><th scope="col">Priority</th><th scope="col">Status</th><th scope="col"><span className="sr-only">Actions</span></th></tr></thead>
              <tbody>
                {myAssignments.map((c) => (
                  <tr key={c.id}>
                    <td><strong>{c.trackingCode}</strong></td>
                    <td>{c.title}</td>
                    <td>{c.category}</td>
                    <td>{areaShort(areas, c.areaId)}</td>
                    <td><span className={`pill-priority p${c.priorityScore}`} title={`Priority ${c.priorityScore}`}>P{c.priorityScore}</span></td>
                    <td><span className={statusClass(c.status)}>{c.status}</span></td>
                    <td><button className="secondary small" onClick={() => setSelected(c)}>View →</button></td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
      </div>

      <div className="card mb-16" id="work-queue-card">
        <div className="card-head">
          <h2 className="mt-0">Work queue</h2>
          <div className="refresh-row">
            <input className="search-input" placeholder="Search queue…" value={searchInput} onChange={(e) => setSearchInput(e.target.value)} />
            <button className="secondary small" onClick={load} aria-label="Refresh work queue"><span aria-hidden="true">↻</span> Refresh</button>
          </div>
        </div>
        <p className="small">Oldest first — work through them in order.</p>
        {loading ? skeleton : filteredQueue.length === 0 ? (
          <EmptyState icon="📭" title={search ? 'No complaints match this search' : 'No active complaints in the queue'} hint={search ? 'Try a different search.' : 'New complaints will appear here.'} />
        ) : (
          <ol className="queue-list">
            {filteredQueue.map((c, i) => (
              <li className="queue-item" key={c.id}>
                <span className="queue-num">{i + 1}</span>
                <div>
                  <strong>{c.trackingCode} — {c.title}</strong>
                  <span>
                    <span className={statusClass(c.status)}>{c.status}</span>{' '}
                    <span className={`pill-priority p${c.priorityScore}`} title={`Priority ${c.priorityScore}`}>P{c.priorityScore}</span>{' '}
                    {c.category} · {areaName(areas, c.areaId)}
                  </span>
                </div>
              </li>
            ))}
          </ol>
        )}
      </div>

      <div className="card" id="active-complaints-card">
        <h2>Active complaints ({filteredActive.length})</h2>
        {loading ? skeleton : filteredActive.length === 0 ? (
          <EmptyState icon="📭" title={search ? 'No active complaints match this search' : 'No active complaints'} hint={search ? 'Try a different search.' : 'Complaints that are not resolved or rejected will appear here.'} />
        ) : (
          <>
            <div className="table-wrap">
              <table>
                <caption className="sr-only">All active complaints</caption>
                <thead><tr><th scope="col">Code</th><th scope="col">Title</th><th scope="col">Category</th><th scope="col">Area</th><th scope="col">Priority</th><th scope="col">Status</th><th scope="col"><span className="sr-only">Actions</span></th></tr></thead>
                <tbody>
                  {pageItems.map((c) => (
                    <tr key={c.id}>
                      <td><strong>{c.trackingCode}</strong></td>
                      <td>{c.title}</td>
                      <td>{c.category}</td>
                      <td>{areaShort(areas, c.areaId)}</td>
                      <td><span className={`pill-priority p${c.priorityScore}`} title={`Priority ${c.priorityScore}`}>P{c.priorityScore}</span></td>
                      <td><span className={statusClass(c.status)}>{c.status}</span></td>
                      <td><button className="secondary small" onClick={() => setSelected(c)}>View →</button></td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
            <div className="pagination">
              <button className="secondary small" disabled={safePage <= 1} onClick={() => setPage(safePage - 1)}>← Previous</button>
              <span>Page {safePage} of {totalPages} · Showing {pageItems.length} of {filteredActive.length} complaints</span>
              <button className="secondary small" disabled={safePage >= totalPages} onClick={() => setPage(safePage + 1)}>Next →</button>
            </div>
          </>
        )}
        {selected && (
          <div className="mt-16">
            <div className="card-head">
              <h3 className="mt-0">Complaint detail — {selected.trackingCode}</h3>
              <button className="secondary small" onClick={() => setSelected(null)} aria-label="Close complaint detail"><span aria-hidden="true">✕</span> Close</button>
            </div>
            <ComplaintDetails complaint={selected} areas={areas} users={users} />
          </div>
        )}
      </div>
    </>
  )
}
