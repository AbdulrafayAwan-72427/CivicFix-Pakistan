import { useEffect, useRef, useState } from 'react'
import { CATEGORIES, NEXT_STATUSES, STATUSES, get, patch, post } from '../api.js'
import { areaShort, exportCsv, matchesSearch, resolutionRate, sortComplaints, statusClass } from '../utils.js'
import ComplaintDetails from '../components/ComplaintDetails.jsx'
import MapPicker from '../components/MapPicker.jsx'

// Admin view: dashboard, complaints with filters, status changes,
// staff assignment, admin actions and undo. Every action goes through
// the C++ API; error messages shown here come from the API itself.
//
// Second wave (web research on civic systems): search, area filter,
// date/priority sorting, pagination, CSV export, complaints-by-area,
// resolution rate, a recent-activity feed, a detail view with a
// progress stepper, and a refresh button with a last-updated time.
// All of these use data the API already returns. Photo upload and a
// real map are now real too (5 Nov 2026): complaints carry a photo
// and latitude/longitude, stored by the C++ API. SLA timers and due
// dates still do not exist in the DB, so those are still not faked.
const PAGE_SIZE = 10

function barColor(i) {
  return ['green', 'amber', 'blue', 'purple', 'red'][i % 5]
}

export default function AdminPage({ currentUser, users, areas = [], onUsersChanged }) {
  const [stats, setStats] = useState(null)
  const [complaints, setComplaints] = useState([])
  const [allComplaints, setAllComplaints] = useState([])
  const [statusFilter, setStatusFilter] = useState('')
  const [categoryFilter, setCategoryFilter] = useState('')
  const [areaFilter, setAreaFilter] = useState('')
  const [search, setSearch] = useState('')
  const [sortMode, setSortMode] = useState('priority')
  const [page, setPage] = useState(1)
  const [selected, setSelected] = useState(null)
  const [actions, setActions] = useState([])
  const [error, setError] = useState('')
  const [message, setMessage] = useState('')
  const [lastUpdated, setLastUpdated] = useState('')
  // Manage controls live in the detail panel only (see below), not in
  // every table row — that is what made the old rows so tall.
  const [selectedStatus, setSelectedStatus] = useState('')
  const [selectedNotes, setSelectedNotes] = useState('')
  const [selectedStaff, setSelectedStaff] = useState('')
  const detailRef = useRef(null)
  // Staff team management (see Team card below). The seeded database
  // only has Ali Khan (Citizen) and CivicFix Admin, so without this
  // there is nobody to assign anything to.
  const [staffForm, setStaffForm] = useState({ fullName: '', email: '', phone: '', areaId: '', password: '' })
  const [staffMsg, setStaffMsg] = useState('')
  const [staffError, setStaffError] = useState('')

  function selectComplaint(c) {
    setSelected(c)
    setSelectedStatus('')
    setSelectedNotes('')
    setSelectedStaff('')
  }
  // Scroll to the detail panel when a row's Manage button is clicked,
  // so the panel does not appear silently below the fold.
  useEffect(() => {
    if (selected && detailRef.current) detailRef.current.scrollIntoView({ behavior: 'smooth', block: 'start' })
  }, [selected])

  const staffUsers = users.filter((u) => u.role === 'Staff')

  async function loadAll() {
    try {
      const [s, a, all] = await Promise.all([get('/api/stats'), get('/api/admin/actions'), get('/api/complaints')])
      setStats(s); setActions(a); setAllComplaints(all)
      setLastUpdated(new Date().toLocaleTimeString())
    } catch (err) { setError(err.message) }
  }
  async function loadComplaints() {
    try {
      const params = new URLSearchParams()
      if (statusFilter) params.set('status', statusFilter)
      if (categoryFilter) params.set('category', categoryFilter)
      if (sortMode === 'priority') params.set('sort', 'priority')
      const query = params.toString()
      setComplaints(await get(`/api/complaints${query ? `?${query}` : ''}`))
    } catch (err) { setError(err.message) }
  }
  useEffect(() => { loadAll() }, [])
  useEffect(() => { loadComplaints(); setPage(1) }, [statusFilter, categoryFilter, sortMode])
  useEffect(() => { setPage(1) }, [search, areaFilter])

  async function changeStatus() {
    setError(''); setMessage('')
    if (!selected) return
    if (!selectedStatus) { setError('Choose a new status first.'); return }
    try {
      await patch(`/api/complaints/${selected.id}/status`, {
        newStatus: selectedStatus, changedByUserId: currentUser.id, notes: selectedNotes || '',
      })
      setMessage(`${selected.trackingCode} changed to ${selectedStatus}.`)
      // Refresh the selected complaint's own status too, so the detail
      // panel does not keep showing the old status after a change.
      const fresh = await get(`/api/complaints/${selected.id}`)
      setSelected(fresh)
      setSelectedStatus('')
      loadComplaints(); loadAll()
    } catch (err) { setError(err.message) }
  }

  async function assign() {
    setError(''); setMessage('')
    if (!selected) return
    const sid = Number(selectedStaff)
    if (!sid) { setError('Choose a staff member first.'); return }
    try {
      await post('/api/assignments', { complaintId: selected.id, staffId: sid, assignedByAdminId: currentUser.id })
      setMessage(`${selected.trackingCode} assigned to staff user ${sid}. Change its status to Assigned next.`)
      loadComplaints()
    } catch (err) { setError(err.message) }
  }

  async function createStaff(event) {
    event.preventDefault()
    setStaffMsg(''); setStaffError('')
    try {
      const created = await post('/api/users', {
        fullName: staffForm.fullName,
        email: staffForm.email,
        phone: staffForm.phone,
        role: 'Staff',
        areaId: Number(staffForm.areaId) || 1,
        password: staffForm.password,
      })
      setStaffMsg(`Staff member added: ${created.fullName} (ID ${created.id}). They now appear in the assignment dropdown.`)
      setStaffForm({ fullName: '', email: '', phone: '', areaId: '', password: '' })
      if (onUsersChanged) await onUsersChanged()
      loadAll()
    } catch (err) { setStaffError(err.message) }
  }

  // One-click demo staff, for testing assignment quickly. If a demo
  // staff member already exists, the API returns 409 "User already
  // exists" — that is treated as "already there", not a failure.
  async function addDemoStaff() {
    setStaffMsg(''); setStaffError('')
    const demo = [
      { fullName: 'Ahmed Raza', email: 'ahmed.staff@civicfix.pk', phone: '03009998888', role: 'Staff', areaId: 1, password: 'staff123' },
      { fullName: 'Fatima Noor', email: 'fatima.staff@civicfix.pk', phone: '03007776666', role: 'Staff', areaId: 1, password: 'staff123' },
    ]
    const added = []
    const existing = []
    for (const d of demo) {
      try { await post('/api/users', d); added.push(d.fullName) }
      catch (err) {
        if (String(err.message).includes('already exists')) existing.push(d.fullName)
        else { setStaffError(err.message); return }
      }
    }
    if (onUsersChanged) await onUsersChanged()
    loadAll()
    setStaffMsg(
      [
        added.length ? `Added: ${added.join(', ')}.` : '',
        existing.length ? `Already existed: ${existing.join(', ')}.` : '',
      ].filter(Boolean).join(' ') + ' They can now log in with password staff123.',
    )
  }

  async function undo() {
    setError(''); setMessage('')
    try {
      const result = await post('/api/admin/undo', { changedByUserId: currentUser.id, notes: 'Undone from the admin dashboard.' })
      setMessage(`Undone: ${result.complaintCode} restored to ${result.restoredStatus}.`)
      loadComplaints(); loadAll()
    } catch (err) { setError(err.message) }
  }

  // Client-side search + area filter + date sort, on top of the
  // API's status/category/priority filtering.
  const filtered = sortComplaints(
    complaints.filter((c) => matchesSearch(c, search) && (!areaFilter || String(c.areaId) === areaFilter)),
    sortMode,
  )
  const totalPages = Math.max(1, Math.ceil(filtered.length / PAGE_SIZE))
  const safePage = Math.min(page, totalPages)
  const pageItems = filtered.slice((safePage - 1) * PAGE_SIZE, safePage * PAGE_SIZE)

  const open = allComplaints.filter((c) => c.status !== 'Resolved' && c.status !== 'Rejected')
  const awaitingReview = allComplaints.filter((c) => c.status === 'Submitted').length
  const highPriorityOpen = open.filter((c) => c.priorityScore >= 4).length
  const inProgress = allComplaints.filter((c) => c.status === 'In Progress').length
  const total = stats ? stats.totalComplaints : 0

  const byArea = areas.map((a) => ({
    ...a,
    count: allComplaints.filter((c) => c.areaId === a.id).length,
  })).filter((a) => a.count > 0)

  return (
    <>
      <div className="attention-grid" style={{ marginBottom: 16 }}>
        <div className="attention">
          <h4>⚠️ Needs review</h4>
          <strong>{awaitingReview}</strong>
          <p>Submitted complaints waiting for a first decision.</p>
        </div>
        <div className="attention">
          <h4>🔥 High-priority open</h4>
          <strong>{highPriorityOpen}</strong>
          <p>Priority 4–5 complaints that are not resolved or rejected yet.</p>
        </div>
        <div className="attention">
          <h4>🕐 In progress</h4>
          <strong>{inProgress}</strong>
          <p>Complaints currently being worked on.</p>
        </div>
      </div>

      {stats ? (
        <div className="stat-grid" style={{ marginBottom: 16 }}>
          <div className="stat"><div className="stat-icon green">📋</div><div><strong>{stats.totalComplaints}</strong><span>Total complaints</span></div></div>
          <div className="stat"><div className="stat-icon green">✅</div><div><strong>{stats.resolvedComplaints}</strong><span>Resolved</span></div></div>
          <div className="stat"><div className="stat-icon amber">⏳</div><div><strong>{stats.pendingComplaints}</strong><span>Pending</span></div></div>
          <div className="stat"><div className="stat-icon blue">📈</div><div><strong>{resolutionRate(stats)}%</strong><span>Resolution rate</span></div></div>
          <div className="stat"><div className="stat-icon blue">👥</div><div><strong>{stats.totalUsers}</strong><span>Users</span></div></div>
          <div className="stat"><div className="stat-icon blue">📍</div><div><strong>{stats.totalAreas}</strong><span>Areas</span></div></div>
        </div>
      ) : <p>Loading stats…</p>}

      <div className="card" style={{ marginBottom: 16 }}>
        <h2 style={{ marginTop: 0 }}>Complaints map</h2>
        <p className="small">
          {allComplaints.filter((c) => c.latitude && c.longitude).length} of {allComplaints.length} complaints have a map pin
          (only complaints submitted with a location picked on the map). Click a pin to open that complaint.
        </p>
        <MapPicker
          readOnly height={320}
          markers={allComplaints.map((c) => ({
            ...c,
            onSelect: (m) => setSelected(m),
          }))}
        />
      </div>

      <div className="grid-main-side">
        <div className="card" id="complaints-card">
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', flexWrap: 'wrap', gap: 10 }}>
            <h2 style={{ margin: 0 }}>Complaints</h2>
            <div className="refresh-row">
              <span className="small">{lastUpdated ? `Updated ${lastUpdated}` : ''}</span>
              <button className="secondary small" onClick={() => { loadComplaints(); loadAll() }}>↻ Refresh</button>
              <button className="secondary small" onClick={() => exportCsv('civicfix-complaints.csv', filtered, areas)}>⬇ Export CSV</button>
            </div>
          </div>
          <div className="toolbar" style={{ marginTop: 14 }}>
            <label>Search
              <input className="search-input" placeholder="Search code, title, description…" value={search} onChange={(e) => setSearch(e.target.value)} />
            </label>
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
            <label>Area
              <select value={areaFilter} onChange={(e) => setAreaFilter(e.target.value)}>
                <option value="">All areas</option>
                {areas.map((a) => <option key={a.id} value={a.id}>{a.name} — {a.city}</option>)}
              </select>
            </label>
            <label>Sort
              <select value={sortMode} onChange={(e) => setSortMode(e.target.value)}>
                <option value="priority">Priority (high first)</option>
                <option value="newest">Newest first</option>
                <option value="oldest">Oldest first</option>
              </select>
            </label>
          </div>
          {error && <p className="error">{error}</p>}
          {message && <p className="success">{message}</p>}
          {staffUsers.length === 0 && <p className="small">No Staff users exist yet — add one in the Team card below, then assignment will work.</p>}
          <div className="table-wrap">
            <table>
              <thead><tr><th>Code</th><th>Title</th><th>Category</th><th>Area</th><th>Priority</th><th>Status</th><th></th></tr></thead>
              <tbody>
                {pageItems.map((c) => (
                  <tr key={c.id}>
                    <td><span className="clickable" onClick={() => selectComplaint(c)}><strong>{c.trackingCode}</strong></span></td>
                    <td><span className="clickable" onClick={() => selectComplaint(c)}>{c.title}</span></td>
                    <td>{c.category}</td>
                    <td>{areaShort(areas, c.areaId)}</td>
                    <td><span className={`pill-priority p${c.priorityScore}`}>P{c.priorityScore}</span></td>
                    <td><span className={statusClass(c.status)}>{c.status}</span></td>
                    <td><button className="secondary small" onClick={() => selectComplaint(c)}>Manage →</button></td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
          {filtered.length === 0 && <p className="small">No complaints match these filters. Try clearing the search or a filter.</p>}
          <div className="pagination">
            <button className="secondary small" disabled={safePage <= 1} onClick={() => setPage(safePage - 1)}>← Previous</button>
            <span>Page {safePage} of {totalPages} · Showing {pageItems.length} of {filtered.length} complaints</span>
            <button className="secondary small" disabled={safePage >= totalPages} onClick={() => setPage(safePage + 1)}>Next →</button>
          </div>
        </div>

        <div>
          <div className="card">
            <h2>By status</h2>
            {stats ? Object.entries(stats.byStatus).map(([k, v]) => (
              <div className="bar-row" key={k}>
                <div className="bar-head"><span>{k}</span><span>{v} · {total ? Math.round((v / total) * 100) : 0}%</span></div>
                <div className="bar-track"><div className="bar-fill" style={{ width: `${total ? (v / total) * 100 : 0}%` }} /></div>
              </div>
            )) : <p className="small">Loading…</p>}
          </div>
          <div className="card">
            <h2>By category</h2>
            {stats ? Object.entries(stats.byCategory).map(([k, v], i) => (
              <div className="bar-row" key={k}>
                <div className="bar-head"><span>{k}</span><span>{v} · {total ? Math.round((v / total) * 100) : 0}%</span></div>
                <div className="bar-track"><div className={`bar-fill ${barColor(i)}`} style={{ width: `${total ? (v / total) * 100 : 0}%` }} /></div>
              </div>
            )) : <p className="small">Loading…</p>}
          </div>
          <div className="card">
            <h2>By area</h2>
            {byArea.length === 0 && <p className="small">No complaints yet.</p>}
            {byArea.map((a, i) => (
              <div className="bar-row" key={a.id}>
                <div className="bar-head"><span>{a.name}</span><span>{a.count}</span></div>
                <div className="bar-track"><div className={`bar-fill ${barColor(i)}`} style={{ width: `${allComplaints.length ? (a.count / allComplaints.length) * 100 : 0}%` }} /></div>
              </div>
            ))}
            <p className="small">Areas come from the areas table; counts are real complaints in each area. The map above shows every complaint that was submitted with a location pin.</p>
          </div>
          <div className="card">
            <h2>Recent activity</h2>
            <p className="small">Latest admin actions, newest first. This is the audit trail — every status change is recorded permanently.</p>
            <div className="timeline">
              {actions.slice(0, 5).map((a) => (
                <div className="timeline-item" key={a.id}>
                  <span className="timeline-dot" />
                  <div>
                    <strong>{a.complaintCode}: {a.previousStatus} → {a.newStatus}</strong>
                    {a.isUndone && <span className="badge badge-rejected" style={{ marginLeft: 6 }}>Undone</span>}
                    {a.description && <p className="small" style={{ margin: '2px 0 0' }}>{a.description}</p>}
                  </div>
                </div>
              ))}
              {actions.length === 0 && <p className="small">No admin actions yet.</p>}
            </div>
          </div>
        </div>
      </div>

      {selected && (
        <div className="card detail-panel" id="complaint-detail" ref={detailRef}>
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
            <h2 style={{ margin: 0 }}>Complaint detail — {selected.trackingCode}</h2>
            <button className="secondary small" onClick={() => setSelected(null)}>✕ Close</button>
          </div>
          <div style={{ marginTop: 12 }}>
            <ComplaintDetails complaint={selected} areas={areas} users={users} />
          </div>
          <h3 style={{ marginTop: 16 }}>Manage this complaint</h3>
          <p className="small">These controls used to live inside every table row, which is what stretched every row tall. They now live here, once, for the complaint you picked.</p>
          <div className="toolbar">
            <label>New status
              <select value={selectedStatus} onChange={(e) => setSelectedStatus(e.target.value)}>
                <option value="">New status…</option>
                {(NEXT_STATUSES[selected.status] || []).map((s) => <option key={s}>{s}</option>)}
              </select>
            </label>
            <label>Notes (optional)
              <input placeholder="Notes (optional)" value={selectedNotes} onChange={(e) => setSelectedNotes(e.target.value)} />
            </label>
            <button onClick={changeStatus}>Change status</button>
          </div>
          <div className="toolbar">
            <label>Assign staff
              <select value={selectedStaff} onChange={(e) => setSelectedStaff(e.target.value)}>
                <option value="">Staff member…</option>
                {staffUsers.map((u) => <option key={u.id} value={u.id}>{u.fullName} (ID {u.id})</option>)}
              </select>
            </label>
            <button className="secondary" onClick={assign} disabled={staffUsers.length === 0}>Assign staff</button>
          </div>
          {staffUsers.length === 0 && <p className="small">Assign is disabled because there are no staff members yet. Add one in the Team card below, then come back here.</p>}
          {error && <p className="error">{error}</p>}
          {message && <p className="success">{message}</p>}
        </div>
      )}

      <div className="card" id="team-card">
        <h2>Team — Staff members</h2>
        <p className="small">
          Why this card exists: the seeded database only has Ali Khan (Citizen) and CivicFix Admin.
          That is why the assignment dropdown was empty and clicking Assign gave “Choose a staff member first.”
          Create a staff member here, and they immediately appear in the assignment dropdown above
          and in the demo user selector at the top of the page — switch to them there, then open the Staff tab to see the queue as that staff member.
        </p>
        {staffUsers.length === 0 ? (
          <p className="error">No staff members yet. Add one below to make assignment work.</p>
        ) : (
          <div className="table-wrap">
            <table>
              <thead><tr><th>Name</th><th>Email</th><th>Phone</th><th>Area</th><th>ID</th></tr></thead>
              <tbody>
                {staffUsers.map((u) => (
                  <tr key={u.id}><td><strong>{u.fullName}</strong></td><td>{u.email}</td><td>{u.phone || '—'}</td><td>{areaShort(areas, u.areaId)}</td><td>{u.id}</td></tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
        <form onSubmit={createStaff} style={{ marginTop: 14 }}>
          <label>Full name
            <input placeholder="e.g. Ahmed Raza" value={staffForm.fullName} onChange={(e) => setStaffForm((f) => ({ ...f, fullName: e.target.value }))} required />
          </label>
          <label>Email
            <input type="email" placeholder="e.g. ahmed.staff@civicfix.pk" value={staffForm.email} onChange={(e) => setStaffForm((f) => ({ ...f, email: e.target.value }))} required />
          </label>
          <label>Phone (optional)
            <input placeholder="e.g. 03009998888" value={staffForm.phone} onChange={(e) => setStaffForm((f) => ({ ...f, phone: e.target.value }))} />
          </label>
          <label>Password for this staff member
            <input type="password" placeholder="At least 6 characters" value={staffForm.password} onChange={(e) => setStaffForm((f) => ({ ...f, password: e.target.value }))} required minLength={6} />
          </label>
          <label>Area
            <select value={staffForm.areaId} onChange={(e) => setStaffForm((f) => ({ ...f, areaId: e.target.value }))}>
              <option value="">Gulberg (default)</option>
              {areas.map((a) => <option key={a.id} value={a.id}>{a.name} — {a.city}</option>)}
            </select>
          </label>
          <div className="row-actions">
            <button type="submit">Add staff member</button>
            <button type="button" className="secondary" onClick={addDemoStaff}>Add 2 demo staff</button>
          </div>
        </form>
        {staffMsg && <p className="success" style={{ marginTop: 12 }}>{staffMsg}</p>}
        {staffError && <p className="error" style={{ marginTop: 12 }}>{staffError}</p>}
        <p className="small">Note: staff created here go into whichever database the API is currently running on. If you are testing on api_test.db, they are test data only — run the same step on the real database if you want them permanently. The API has no “list my assignments” endpoint yet, so the Staff tab still shows the shared queue, not only one person's assignments — that is a real limitation, not hidden.</p>
      </div>

      <div className="card" id="admin-actions-card">
        <h2>Admin actions &amp; undo</h2>
        <p className="small">Undo restores the latest pending action. If the complaint has changed since then, the API rejects the undo — that is the safety check, not a bug.</p>
        <button onClick={undo}>Undo latest action</button>
        <div className="table-wrap" style={{ marginTop: 12 }}>
          <table>
            <thead><tr><th>Complaint</th><th>From</th><th>To</th><th>Undone?</th></tr></thead>
            <tbody>
              {actions.map((a) => (
                <tr key={a.id}><td>{a.complaintCode}</td><td>{a.previousStatus}</td><td>{a.newStatus}</td><td>{a.isUndone ? 'Yes' : 'No'}</td></tr>
              ))}
            </tbody>
          </table>
        </div>
      </div>
    </>
  )
}
