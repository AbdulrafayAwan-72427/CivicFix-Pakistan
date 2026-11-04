import { useEffect, useState } from 'react'
import { CATEGORIES, NEXT_STATUSES, STATUSES, get, patch, post } from '../api.js'

// Admin view: dashboard, complaints with filters, status changes,
// staff assignment, admin actions and undo. Every action goes through
// the C++ API; error messages shown here come from the API itself.
export default function AdminPage({ currentUser, users }) {
  const [stats, setStats] = useState(null)
  const [complaints, setComplaints] = useState([])
  const [statusFilter, setStatusFilter] = useState('')
  const [categoryFilter, setCategoryFilter] = useState('')
  const [sortPriority, setSortPriority] = useState(false)
  const [actions, setActions] = useState([])
  const [error, setError] = useState('')
  const [message, setMessage] = useState('')
  // Per-complaint controls, keyed by complaint id.
  const [newStatus, setNewStatus] = useState({})
  const [notes, setNotes] = useState({})
  const [staffId, setStaffId] = useState({})

  const staffUsers = users.filter((u) => u.role === 'Staff')

  async function loadStats() {
    try { setStats(await get('/api/stats')) } catch (err) { setError(err.message) }
  }
  async function loadComplaints() {
    try {
      const params = new URLSearchParams()
      if (statusFilter) params.set('status', statusFilter)
      if (categoryFilter) params.set('category', categoryFilter)
      if (sortPriority) params.set('sort', 'priority')
      const query = params.toString()
      setComplaints(await get(`/api/complaints${query ? `?${query}` : ''}`))
    } catch (err) { setError(err.message) }
  }
  async function loadActions() {
    try { setActions(await get('/api/admin/actions')) } catch (err) { setError(err.message) }
  }
  useEffect(() => { loadStats(); loadActions() }, [])
  useEffect(() => { loadComplaints() }, [statusFilter, categoryFilter, sortPriority])

  async function changeStatus(complaint) {
    setError(''); setMessage('')
    const target = newStatus[complaint.id]
    if (!target) { setError('Choose a new status first.'); return }
    try {
      await patch(`/api/complaints/${complaint.id}/status`, {
        newStatus: target, changedByUserId: currentUser.id, notes: notes[complaint.id] || '',
      })
      setMessage(`${complaint.trackingCode} changed to ${target}.`)
      loadComplaints(); loadStats(); loadActions()
    } catch (err) { setError(err.message) }
  }

  async function assign(complaint) {
    setError(''); setMessage('')
    const sid = Number(staffId[complaint.id])
    if (!sid) { setError('Choose a staff member first.'); return }
    try {
      await post('/api/assignments', { complaintId: complaint.id, staffId: sid, assignedByAdminId: currentUser.id })
      setMessage(`${complaint.trackingCode} assigned to staff user ${sid}. Change its status to Assigned next.`)
      loadComplaints()
    } catch (err) { setError(err.message) }
  }

  async function undo() {
    setError(''); setMessage('')
    try {
      const result = await post('/api/admin/undo', { changedByUserId: currentUser.id, notes: 'Undone from the admin dashboard.' })
      setMessage(`Undone: ${result.complaintCode} restored to ${result.restoredStatus}.`)
      loadComplaints(); loadStats(); loadActions()
    } catch (err) { setError(err.message) }
  }

  return (
    <>
      <div className="card">
        <h2>Dashboard</h2>
        {stats ? (
          <>
            <div className="stat-grid">
              <div className="stat"><strong>{stats.totalComplaints}</strong>Total complaints</div>
              <div className="stat"><strong>{stats.resolvedComplaints}</strong>Resolved</div>
              <div className="stat"><strong>{stats.pendingComplaints}</strong>Pending</div>
              <div className="stat"><strong>{stats.totalUsers}</strong>Users</div>
              <div className="stat"><strong>{stats.totalAreas}</strong>Areas</div>
            </div>
            <p className="small">By status: {Object.entries(stats.byStatus).map(([k, v]) => `${k}: ${v}`).join(', ') || '—'}</p>
            <p className="small">By category: {Object.entries(stats.byCategory).map(([k, v]) => `${k}: ${v}`).join(', ') || '—'}</p>
          </>
        ) : <p>Loading stats…</p>}
      </div>

      <div className="card">
        <h2>Complaints</h2>
        <div className="toolbar">
          <label>Status
            <select value={statusFilter} onChange={(e) => setStatusFilter(e.target.value)}>
              <option value="">All</option>
              {STATUSES.map((s) => <option key={s}>{s}</option>)}
            </select>
          </label>
          <label>Category
            <select value={categoryFilter} onChange={(e) => setCategoryFilter(e.target.value)}>
              <option value="">All</option>
              {CATEGORIES.map((c) => <option key={c}>{c}</option>)}
            </select>
          </label>
          <label>
            <span><input type="checkbox" checked={sortPriority} onChange={(e) => setSortPriority(e.target.checked)} /> Sort by priority (Counting Sort on the server)</span>
          </label>
        </div>
        {error && <p className="error">{error}</p>}
        {message && <p className="success">{message}</p>}
        <table>
          <thead><tr><th>Code</th><th>Title</th><th>Category</th><th>Priority</th><th>Status</th><th>Actions</th></tr></thead>
          <tbody>
            {complaints.map((c) => (
              <tr key={c.id}>
                <td>{c.trackingCode}</td><td>{c.title}</td><td>{c.category}</td>
                <td>{c.priorityScore}</td><td><span className="badge">{c.status}</span></td>
                <td>
                  <div className="row-actions">
                    <select value={newStatus[c.id] || ''} onChange={(e) => setNewStatus((m) => ({ ...m, [c.id]: e.target.value }))}>
                      <option value="">New status…</option>
                      {(NEXT_STATUSES[c.status] || []).map((s) => <option key={s}>{s}</option>)}
                    </select>
                    <input placeholder="Notes (optional)" value={notes[c.id] || ''} onChange={(e) => setNotes((m) => ({ ...m, [c.id]: e.target.value }))} />
                    <button onClick={() => changeStatus(c)}>Change status</button>
                  </div>
                  <div className="row-actions">
                    <select value={staffId[c.id] || ''} onChange={(e) => setStaffId((m) => ({ ...m, [c.id]: e.target.value }))}>
                      <option value="">Staff member…</option>
                      {staffUsers.map((u) => <option key={u.id} value={u.id}>{u.fullName} (ID {u.id})</option>)}
                    </select>
                    <button className="secondary" onClick={() => assign(c)}>Assign staff</button>
                  </div>
                  {staffUsers.length === 0 && <p className="small">No Staff users exist yet — create one via the API (see README) to test assignment.</p>}
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>

      <div className="card">
        <h2>Admin actions &amp; undo</h2>
        <p className="small">Undo restores the latest pending action. If the complaint has changed since then, the API rejects the undo — that is the safety check, not a bug.</p>
        <button onClick={undo}>Undo latest action</button>
        <table>
          <thead><tr><th>Complaint</th><th>From</th><th>To</th><th>Undone?</th></tr></thead>
          <tbody>
            {actions.map((a) => (
              <tr key={a.id}><td>{a.complaintCode}</td><td>{a.previousStatus}</td><td>{a.newStatus}</td><td>{a.isUndone ? 'Yes' : 'No'}</td></tr>
            ))}
          </tbody>
        </table>
      </div>
    </>
  )
}
