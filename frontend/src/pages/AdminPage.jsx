import { useEffect, useState } from 'react'
import { CATEGORIES, NEXT_STATUSES, STATUSES, get, patch, post, markDuplicate, escalateComplaint } from '../api.js'
import { CATEGORY_COLORS, areaShort, exportCsv, hasCoords, matchesSearch, resolutionRate, sortComplaints, statusClass } from '../utils.js'
import ComplaintDetails from '../components/ComplaintDetails.jsx'
import EmptyState from '../components/EmptyState.jsx'
import Feedback from '../components/Feedback.jsx'
import MapPicker from '../components/MapPicker.jsx'

// Admin view, split into tabs (Overview / Complaints / Team /
// Actions) so the primary task — managing a complaint — is not
// buried under analytics. The complaint detail opens in a side
// drawer, so the table stays visible behind it. Every action goes
// through the C++ API; error messages shown here come from the API.
const PAGE_SIZE = 10
const SECTIONS = [['overview', 'Overview'], ['complaints', 'Complaints'], ['team', 'Team'], ['actions', 'Actions']]

export default function AdminPage({ currentUser, users, areas = [], onUsersChanged, section = 'overview', onSectionChange }) {
  const [ownSection, setOwnSection] = useState('overview')
  const activeSection = onSectionChange ? section : ownSection
  const changeSection = onSectionChange || setOwnSection

  const [stats, setStats] = useState(null)
  const [complaints, setComplaints] = useState([])
  const [allComplaints, setAllComplaints] = useState([])
  const [actions, setActions] = useState([])
  const [loading, setLoading] = useState(true)
  const [listLoading, setListLoading] = useState(true)
  const [statusFilter, setStatusFilter] = useState('')
  const [categoryFilter, setCategoryFilter] = useState('')
  const [areaFilter, setAreaFilter] = useState('')
  const [searchInput, setSearchInput] = useState('')
  const [search, setSearch] = useState('')
  const [sortMode, setSortMode] = useState('priority')
  const [page, setPage] = useState(1)
  const [selected, setSelected] = useState(null)
  const [selectedStatus, setSelectedStatus] = useState('')
  const [selectedNotes, setSelectedNotes] = useState('')
  const [selectedStaff, setSelectedStaff] = useState('')
  const [duplicateCode, setDuplicateCode] = useState('')
  const [escalateReason, setEscalateReason] = useState('')
  const [panelError, setPanelError] = useState('')
  const [panelMessage, setPanelMessage] = useState('')
  const [panelBusy, setPanelBusy] = useState(false)
  const [confirming, setConfirming] = useState(null) // 'reject' | 'undo' | null
  const [error, setError] = useState('')
  const [message, setMessage] = useState('')
  const [lastUpdated, setLastUpdated] = useState('')
  const [staffForm, setStaffForm] = useState({ fullName: '', email: '', phone: '', areaId: '', password: '' })
  const [staffMsg, setStaffMsg] = useState('')
  const [staffError, setStaffError] = useState('')
  const [staffBusy, setStaffBusy] = useState(false)
  const [undoBusy, setUndoBusy] = useState(false)

  function selectComplaint(c) {
    setSelected(c)
    setSelectedStatus('')
    setSelectedNotes('')
    setSelectedStaff('')
    setDuplicateCode('')
    setEscalateReason('')
    setPanelError('')
    setPanelMessage('')
    setConfirming(null)
  }

  const staffUsers = users.filter((u) => u.role === 'Staff')

  async function loadAll() {
    setLoading(true)
    try {
      const [s, a, all] = await Promise.all([get('/api/stats'), get('/api/admin/actions'), get('/api/complaints')])
      setStats(s); setActions(a); setAllComplaints(all)
      setLastUpdated(new Date().toLocaleTimeString())
    } catch (err) { setError(err.message) } finally { setLoading(false) }
  }
  async function loadComplaints() {
    setListLoading(true)
    try {
      const params = new URLSearchParams()
      if (statusFilter) params.set('status', statusFilter)
      if (categoryFilter) params.set('category', categoryFilter)
      if (sortMode === 'priority') params.set('sort', 'priority')
      const query = params.toString()
      setComplaints(await get(`/api/complaints${query ? `?${query}` : ''}`))
    } catch (err) { setError(err.message) } finally { setListLoading(false) }
  }
  useEffect(() => { loadAll() }, [])
  useEffect(() => { loadComplaints(); setPage(1) }, [statusFilter, categoryFilter, sortMode])
  useEffect(() => { setPage(1) }, [search, areaFilter])
  useEffect(() => {
    const t = setTimeout(() => setSearch(searchInput), 250)
    return () => clearTimeout(t)
  }, [searchInput])

  async function changeStatus() {
    if (panelBusy || !selected) return
    if (!selectedStatus) { setPanelError('Choose a new status first.'); return }
    // Phase 3 (11 Nov 2026, handoff-2): the API requires a reason
    // to reject — the citizen sees it on their complaint — so ask
    // for it here instead of letting the API refuse the change.
    if (selectedStatus === 'Rejected' && !selectedNotes.trim()) { setPanelError('A reason is required to reject a complaint — the citizen will see it.'); return }
    // Rejecting ends the workflow for the citizen, so it asks first.
    if (selectedStatus === 'Rejected' && confirming !== 'reject') { setConfirming('reject'); return }
    setConfirming(null)
    setPanelError(''); setPanelMessage('')
    setPanelBusy(true)
    try {
      await patch(`/api/complaints/${selected.id}/status`, {
        newStatus: selectedStatus, changedByUserId: currentUser.id, notes: selectedNotes || '',
      })
      setPanelMessage(`${selected.trackingCode} changed to ${selectedStatus}.`)
      const fresh = await get(`/api/complaints/${selected.id}`)
      setSelected(fresh)
      setSelectedStatus('')
      loadComplaints(); loadAll()
    } catch (err) { setPanelError(err.message) } finally { setPanelBusy(false) }
  }

  async function assign() {
    if (panelBusy || !selected) return
    const sid = Number(selectedStaff)
    if (!sid) { setPanelError('Choose a staff member first.'); return }
    setPanelError(''); setPanelMessage('')
    setPanelBusy(true)
    try {
      const result = await post('/api/assignments', { complaintId: selected.id, staffId: sid, assignedByAdminId: currentUser.id })
      const name = staffUsers.find((u) => u.id === sid)?.fullName || `staff user ${sid}`
      setPanelMessage(result.reassigned
        ? `${selected.trackingCode} reassigned to ${name}. Its status is still ${selected.status} — update it when work begins.`
        : `${selected.trackingCode} assigned to ${name}. Its status is still ${selected.status} — update it when work begins.`)
      const fresh = await get(`/api/complaints/${selected.id}`)
      setSelected(fresh)
      loadComplaints()
    } catch (err) { setPanelError(err.message) } finally { setPanelBusy(false) }
  }

  // Phase 3 (11 Nov 2026, handoff-2): duplicates and escalation.
  // Both close or flag the case and notify the people involved;
  // the API validates the rules (no chains, no self-duplicate,
  // no re-escalation) and returns the updated complaint.
  async function markDup() {
    if (panelBusy || !selected) return
    const code = duplicateCode.trim().toUpperCase()
    if (!code) { setPanelError('Enter the tracking code of the original report.'); return }
    setPanelError(''); setPanelMessage('')
    setPanelBusy(true)
    try {
      const fresh = await markDuplicate(selected.id, code, currentUser.id, '')
      setSelected(fresh)
      setDuplicateCode('')
      setPanelMessage(`${selected.trackingCode} marked as a duplicate of ${code} and closed.`)
      loadComplaints(); loadAll()
    } catch (err) { setPanelError(err.message) } finally { setPanelBusy(false) }
  }

  async function escalate() {
    if (panelBusy || !selected) return
    if (!escalateReason.trim()) { setPanelError('Give a reason for the escalation.'); return }
    setPanelError(''); setPanelMessage('')
    setPanelBusy(true)
    try {
      const fresh = await escalateComplaint(selected.id, escalateReason.trim(), currentUser.id)
      setSelected(fresh)
      setEscalateReason('')
      setPanelMessage(`${selected.trackingCode} escalated to priority 5.`)
      loadComplaints(); loadAll()
    } catch (err) { setPanelError(err.message) } finally { setPanelBusy(false) }
  }

  async function createStaff(event) {
    event.preventDefault()
    if (staffBusy) return
    setStaffMsg(''); setStaffError('')
    setStaffBusy(true)
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
    } catch (err) { setStaffError(err.message) } finally { setStaffBusy(false) }
  }

  // Disable / enable a staff member (9 Nov 2026, handoff-2 phase 2).
  // Disable deletes all of their sessions, so a live login stops
  // working at once. The Admin cannot disable their own account —
  // the API returns 400 — and staff rows are the only ones with the
  // button, so this never touches the Admin themself here.
  async function toggleStaffActive(u) {
    if (staffBusy) return
    setStaffMsg(''); setStaffError('')
    setStaffBusy(true)
    try {
      const action = u.isActive === false ? 'enable' : 'disable'
      const updated = await post(`/api/users/${u.id}/${action}`, {})
      setStaffMsg(`${updated.fullName} is now ${updated.isActive === false ? 'disabled — their logins have been revoked' : 'active again'}.`)
      if (onUsersChanged) await onUsersChanged()
    } catch (err) { setStaffError(err.message) } finally { setStaffBusy(false) }
  }

  // One-click demo staff, for testing assignment quickly. If a demo
  // staff member already exists, the API returns 409 "User already
  // exists" — that is treated as "already there", not a failure.
  async function addDemoStaff() {
    if (staffBusy) return
    setStaffMsg(''); setStaffError('')
    setStaffBusy(true)
    const demo = [
      { fullName: 'Ahmed Raza', email: 'ahmed.staff@civicfix.pk', phone: '03009998888', role: 'Staff', areaId: 1, password: 'staff123' },
      { fullName: 'Fatima Noor', email: 'fatima.staff@civicfix.pk', phone: '03007776666', role: 'Staff', areaId: 1, password: 'staff123' },
    ]
    const added = []
    const existing = []
    try {
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
    } finally { setStaffBusy(false) }
  }

  async function undo() {
    if (undoBusy) return
    // Undo changes the audit trail, so it asks first.
    if (confirming !== 'undo') { setConfirming('undo'); return }
    setConfirming(null)
    setError(''); setMessage('')
    setUndoBusy(true)
    try {
      const result = await post('/api/admin/undo', { changedByUserId: currentUser.id, notes: 'Undone from the admin dashboard.' })
      setMessage(`Undone: ${result.complaintCode} restored to ${result.restoredStatus}.`)
      loadComplaints(); loadAll()
    } catch (err) { setError(err.message) } finally { setUndoBusy(false) }
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
  const located = allComplaints.filter((c) => hasCoords(c.latitude, c.longitude))

  const byArea = areas.map((a) => ({
    ...a,
    count: allComplaints.filter((c) => c.areaId === a.id).length,
  })).filter((a) => a.count > 0)

  const skeleton = (
    <div className="skeleton-list" aria-busy="true" aria-label="Loading">
      <div className="skeleton-row" /><div className="skeleton-row" /><div className="skeleton-row" />
    </div>
  )

  return (
    <>
      <nav className="tabs" role="tablist" aria-label="Admin sections">
        {SECTIONS.map(([key, label]) => (
          <button key={key} role="tab" aria-selected={activeSection === key}
            className={`tab ${activeSection === key ? 'active' : ''}`}
            onClick={() => changeSection(key)}>{label}</button>
        ))}
      </nav>
      <Feedback error={error} message={message} onClearMessage={() => setMessage('')} />

      {activeSection === 'overview' && (
        <>
          <div className="attention-grid mb-16">
            <div className="attention">
              <h4><span aria-hidden="true">⚠️</span> Needs review</h4>
              <strong>{awaitingReview}</strong>
              <p>Submitted complaints waiting for a first decision.</p>
            </div>
            <div className="attention">
              <h4><span aria-hidden="true">🔥</span> High-priority open</h4>
              <strong>{highPriorityOpen}</strong>
              <p><span className="pill-priority p4">P4</span> <span className="pill-priority p5">P5</span> open complaints, not resolved or rejected yet.</p>
            </div>
            <div className="attention">
              <h4><span aria-hidden="true">🕐</span> In progress</h4>
              <strong>{inProgress}</strong>
              <p>Complaints currently being worked on.</p>
            </div>
          </div>

          {stats ? (
            <div className="stat-grid mb-16">
              <div className="stat"><div className="stat-icon slate" aria-hidden="true">📋</div><div><strong>{stats.totalComplaints}</strong><span>Total complaints (all time)</span></div></div>
              <div className="stat"><div className="stat-icon green" aria-hidden="true">✅</div><div><strong>{stats.resolvedComplaints}</strong><span>Resolved (all time)</span></div></div>
              <div className="stat"><div className="stat-icon amber" aria-hidden="true">⏳</div><div><strong>{stats.pendingComplaints}</strong><span>Pending</span></div></div>
              <div className="stat"><div className="stat-icon green" aria-hidden="true">📈</div><div><strong>{resolutionRate(stats)}%</strong><span>Resolution rate (all time)</span></div></div>
              <div className="stat"><div className="stat-icon slate" aria-hidden="true">👥</div><div><strong>{stats.totalUsers}</strong><span>Users</span></div></div>
              <div className="stat"><div className="stat-icon slate" aria-hidden="true">📍</div><div><strong>{stats.totalAreas}</strong><span>Areas</span></div></div>
            </div>
          ) : loading ? skeleton : (
            <EmptyState icon="📊" title="Statistics unavailable" hint="Check the connection and refresh the page." />
          )}

          <div className="card mb-16">
            <h2 className="mt-0">Complaints map</h2>
            <p className="small">
              {located.length} of {allComplaints.length} complaints have a map pin
              (only complaints submitted with a location picked on the map). Click a pin to open that complaint.
            </p>
            <MapPicker
              readOnly height={320}
              markers={allComplaints.map((c) => ({
                ...c,
                onSelect: (m) => selectComplaint(m),
              }))}
            />
            <details className="map-list">
              <summary>{located.length} complaints with a location</summary>
              <ul>
                {located.map((c) => (
                  <li key={c.id}>
                    <button className="link-cell" onClick={() => selectComplaint(c)}>
                      {c.trackingCode} — {c.title} ({Number(c.latitude).toFixed(4)}, {Number(c.longitude).toFixed(4)})
                    </button>
                  </li>
                ))}
              </ul>
            </details>
          </div>

          <div className="grid-2">
            <div>
              <div className="card">
                <h2>By status</h2>
                {stats ? Object.entries(stats.byStatus).map(([k, v]) => (
                  <div className="bar-row" key={k}>
                    <div className="bar-head"><span>{k}</span><span>{v} · {total ? Math.round((v / total) * 100) : 0}%</span></div>
                    <div className="bar-track"><div className="bar-fill" style={{ width: `${total ? (v / total) * 100 : 0}%` }} /></div>
                  </div>
                )) : loading ? skeleton : <p className="small">Statistics unavailable.</p>}
              </div>
              <div className="card">
                <h2>By area</h2>
                {byArea.length === 0 && !loading && <p className="small">No complaints yet.</p>}
                {loading && byArea.length === 0 ? skeleton : byArea.map((a) => (
                  <div className="bar-row" key={a.id}>
                    <div className="bar-head"><span>{a.name}</span><span>{a.count}</span></div>
                    <div className="bar-track"><div className="bar-fill" style={{ width: `${allComplaints.length ? (a.count / allComplaints.length) * 100 : 0}%` }} /></div>
                  </div>
                ))}
                <p className="small">Counts are complaints per area. See the map above for exact locations.</p>
              </div>
            </div>
            <div>
              <div className="card">
                <h2>By category</h2>
                {stats ? Object.entries(stats.byCategory).map(([k, v]) => (
                  <div className="bar-row" key={k}>
                    <div className="bar-head"><span>{k}</span><span>{v} · {total ? Math.round((v / total) * 100) : 0}%</span></div>
                    <div className="bar-track"><div className={`bar-fill ${CATEGORY_COLORS[k] || 'green'}`} style={{ width: `${total ? (v / total) * 100 : 0}%` }} /></div>
                  </div>
                )) : loading ? skeleton : <p className="small">Statistics unavailable.</p>}
              </div>
              <div className="card">
                <h2>Recent activity</h2>
                <p className="small">Recent status changes, newest first.</p>
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
                  {actions.length === 0 && !loading && <p className="small">No admin actions yet.</p>}
                </div>
              </div>
            </div>
          </div>
        </>
      )}

      {activeSection === 'complaints' && (
        <div className="card" id="complaints-card">
          <div className="card-head">
            <h2 className="mt-0">Complaints</h2>
            <div className="refresh-row">
              <span className="small">{lastUpdated ? `Updated ${lastUpdated}` : ''}</span>
              <button className="secondary small" onClick={() => { loadComplaints(); loadAll() }} aria-label="Refresh complaints list"><span aria-hidden="true">↻</span> Refresh</button>
              <button className="secondary small" onClick={() => exportCsv('civicfix-complaints.csv', filtered, areas)} aria-label="Export filtered complaints as CSV"><span aria-hidden="true">⬇</span> Export CSV</button>
            </div>
          </div>
          <div className="toolbar mt-12">
            <label>Search
              <input className="search-input" placeholder="Search code, title, description…" value={searchInput} onChange={(e) => setSearchInput(e.target.value)} />
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
          {staffUsers.length === 0 && <p className="small">No staff members yet. Add one in Team to start assigning complaints.</p>}
          {listLoading ? skeleton : (
            <>
              <div className="table-wrap">
                <table>
                  <caption className="sr-only">Complaints, filterable by status, category and area</caption>
                  <thead><tr><th scope="col">Code</th><th scope="col">Title</th><th scope="col">Category</th><th scope="col">Area</th><th scope="col">Priority</th><th scope="col">Status</th><th scope="col"><span className="sr-only">Actions</span></th></tr></thead>
                  <tbody>
                    {pageItems.map((c) => (
                      <tr key={c.id}>
                        <td><button className="link-cell" onClick={() => selectComplaint(c)}><strong>{c.trackingCode}</strong></button></td>
                        <td><button className="link-cell" onClick={() => selectComplaint(c)}>{c.title}</button></td>
                        <td>{c.category}</td>
                        <td>{areaShort(areas, c.areaId)}</td>
                        <td><span className={`pill-priority p${c.priorityScore}`} title={`Priority ${c.priorityScore}`}>P{c.priorityScore}</span></td>
                        <td><span className={statusClass(c.status)}>{c.status}</span></td>
                        <td><button className="secondary small" onClick={() => selectComplaint(c)}>Manage →</button></td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>
              {filtered.length === 0 && (
                <EmptyState icon="🔍" title="No complaints match these filters" hint="Try clearing the search box or resetting a filter." />
              )}
            </>
          )}
          <div className="pagination">
            <button className="secondary small" disabled={safePage <= 1} onClick={() => setPage(safePage - 1)}>← Previous</button>
            <span>Page {safePage} of {totalPages} · Showing {pageItems.length} of {filtered.length} complaints</span>
            <button className="secondary small" disabled={safePage >= totalPages} onClick={() => setPage(safePage + 1)}>Next →</button>
          </div>
        </div>
      )}

      {activeSection === 'team' && (
        <div className="card" id="team-card">
          <h2>Team — Staff members</h2>
          <p className="small">
            Add a staff member here. They appear in the assignment dropdown above and can log in with the password you set. Disabling a staff member revokes their logins immediately.
          </p>
          {staffUsers.length === 0 ? (
            <EmptyState icon="👥" title="No staff members yet" hint="Add one below to make assignment work." />
          ) : (
            <div className="table-wrap">
              <table>
                <caption className="sr-only">Staff members</caption>
                <thead><tr><th scope="col">Name</th><th scope="col">Email</th><th scope="col">Phone</th><th scope="col">Area</th><th scope="col">ID</th><th scope="col">Status</th><th scope="col"><span className="sr-only">Access</span></th></tr></thead>
                <tbody>
                  {staffUsers.map((u) => (
                    <tr key={u.id}><td><strong>{u.fullName}</strong></td><td>{u.email}</td><td>{u.phone || '—'}</td><td>{areaShort(areas, u.areaId)}</td><td>{u.id}</td><td>{u.isActive === false ? 'Disabled' : 'Active'}</td><td><button type="button" className="secondary small" onClick={() => toggleStaffActive(u)} disabled={staffBusy}>{u.isActive === false ? 'Enable' : 'Disable'}</button></td></tr>
                  ))}
                </tbody>
              </table>
            </div>
          )}
          <form onSubmit={createStaff} className="mt-12">
            <label>Full name
              <input autoComplete="name" placeholder="e.g. Ahmed Raza" value={staffForm.fullName} onChange={(e) => setStaffForm((f) => ({ ...f, fullName: e.target.value }))} required />
            </label>
            <label>Email
              <input type="email" autoComplete="email" placeholder="e.g. ahmed.staff@civicfix.pk" value={staffForm.email} onChange={(e) => setStaffForm((f) => ({ ...f, email: e.target.value }))} required />
            </label>
            <label>Phone (optional)
              <input autoComplete="tel" placeholder="e.g. 03009998888" value={staffForm.phone} onChange={(e) => setStaffForm((f) => ({ ...f, phone: e.target.value }))} />
            </label>
            <label>Password for this staff member
              <input type="password" autoComplete="new-password" placeholder="At least 6 characters" value={staffForm.password} onChange={(e) => setStaffForm((f) => ({ ...f, password: e.target.value }))} required minLength={6} />
            </label>
            <label>Area
              <select value={staffForm.areaId} onChange={(e) => setStaffForm((f) => ({ ...f, areaId: e.target.value }))}>
                <option value="">Gulberg (default)</option>
                {areas.map((a) => <option key={a.id} value={a.id}>{a.name} — {a.city}</option>)}
              </select>
            </label>
            <div className="row-actions">
              <button type="submit" disabled={staffBusy}>{staffBusy ? 'Adding…' : 'Add staff member'}</button>
              <button type="button" className="secondary" onClick={addDemoStaff} disabled={staffBusy}>{staffBusy ? 'Adding…' : 'Add 2 demo staff'}</button>
            </div>
          </form>
          <Feedback error={staffError} message={staffMsg} onClearMessage={() => setStaffMsg('')} />
        </div>
      )}

      {activeSection === 'actions' && (
        <div className="card" id="admin-actions-card">
          <h2>Admin actions &amp; undo</h2>
          <p className="small">Undo reverses the most recent status change. If the complaint has changed since, the undo is refused.</p>
          <button onClick={undo} disabled={undoBusy}>{undoBusy ? 'Undoing…' : 'Undo latest action'}</button>
          {confirming === 'undo' && (
            <p className="error mt-12">Undo the latest action? This changes the complaint back to its previous status.
              <span className="row-actions">
                <button className="secondary small" onClick={undo} disabled={undoBusy}>Undo it</button>
                <button className="secondary small" onClick={() => setConfirming(null)}>Cancel</button>
              </span>
            </p>
          )}
          <div className="table-wrap mt-12">
            <table>
              <caption className="sr-only">Admin status changes, newest first</caption>
              <thead><tr><th scope="col">Complaint</th><th scope="col">From</th><th scope="col">To</th><th scope="col">Undone?</th></tr></thead>
              <tbody>
                {actions.map((a) => (
                  <tr key={a.id}><td>{a.complaintCode}</td><td>{a.previousStatus}</td><td>{a.newStatus}</td><td>{a.isUndone ? 'Yes' : 'No'}</td></tr>
                ))}
              </tbody>
            </table>
          </div>
          {actions.length === 0 && !loading && (
            <EmptyState icon="↩️" title="No admin actions yet" hint="Status changes will appear here. Assignments are not recorded here — the backend only logs status changes." />
          )}
        </div>
      )}

      {selected && (
        <>
          <div className="drawer-backdrop" onClick={() => setSelected(null)} aria-hidden="true" />
          <div className="drawer" role="dialog" aria-label={`Complaint ${selected.trackingCode}`}>
            <div className="card-head">
              <h2 className="mt-0">Complaint detail — {selected.trackingCode}</h2>
              <button className="secondary small" onClick={() => setSelected(null)} aria-label="Close complaint detail"><span aria-hidden="true">✕</span> Close</button>
            </div>
            <div className="mt-12">
              <ComplaintDetails complaint={selected} areas={areas} users={users} currentUser={currentUser} />
            </div>
            <h3 className="mt-16">Manage this complaint</h3>
            <p className="small">Change the status or assign this complaint to a staff member.</p>
            <div className="toolbar">
              <label>New status
                <select value={selectedStatus} onChange={(e) => setSelectedStatus(e.target.value)}>
                  <option value="">New status…</option>
                  {(NEXT_STATUSES[selected.status] || []).map((s) => <option key={s}>{s}</option>)}
                </select>
              </label>
              <label>Notes (required when rejecting)
                <input placeholder="Reason for rejection — the citizen will see it" value={selectedNotes} onChange={(e) => setSelectedNotes(e.target.value)} />
              </label>
              <button onClick={changeStatus} disabled={panelBusy}>{panelBusy ? 'Updating…' : 'Change status'}</button>
            </div>
            {confirming === 'reject' && (
              <p className="error">Reject {selected.trackingCode}? The citizen will see it as rejected, with this reason: “{selectedNotes.trim()}”.
                <span className="row-actions">
                  <button className="secondary small" onClick={changeStatus} disabled={panelBusy}>Reject it</button>
                  <button className="secondary small" onClick={() => setConfirming(null)}>Cancel</button>
                </span>
              </p>
            )}
            <div className="toolbar">
              <label>Assign staff
                <select value={selectedStaff} onChange={(e) => setSelectedStaff(e.target.value)}>
                  <option value="">Staff member…</option>
                  {staffUsers.map((u) => <option key={u.id} value={u.id}>{u.fullName} (ID {u.id})</option>)}
                </select>
              </label>
              <button className="secondary" onClick={assign} disabled={panelBusy || staffUsers.length === 0}>{panelBusy ? 'Assigning…' : 'Assign staff'}</button>
            </div>
            {staffUsers.length === 0 && <p className="small">Add a staff member in Team first.</p>}
            {selected.status !== 'Resolved' && selected.status !== 'Rejected' && (
              <>
                <h3 className="mt-16">Duplicate &amp; escalation</h3>
                <div className="toolbar">
                  <label>Mark as duplicate of
                    <input placeholder="e.g. CFX-7K3M9P" value={duplicateCode} onChange={(e) => setDuplicateCode(e.target.value)} />
                  </label>
                  <button className="secondary" onClick={markDup} disabled={panelBusy}>{panelBusy ? 'Updating…' : 'Mark duplicate'}</button>
                </div>
                <p className="small">Closes this report as a duplicate of the original. The citizen is told which report theirs duplicates.</p>
                {!selected.escalated ? (
                  <div className="toolbar">
                    <label>Escalate — reason
                      <input placeholder="Why is this urgent now?" value={escalateReason} onChange={(e) => setEscalateReason(e.target.value)} />
                    </label>
                    <button className="secondary" onClick={escalate} disabled={panelBusy}>{panelBusy ? 'Updating…' : 'Escalate to P5'}</button>
                  </div>
                ) : (
                  <p className="small">This complaint is already escalated (priority 5).</p>
                )}
              </>
            )}
            <Feedback error={panelError} message={panelMessage} onClearMessage={() => setPanelMessage('')} />
          </div>
        </>
      )}
    </>
  )
}
