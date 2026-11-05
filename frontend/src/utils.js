// Shared helpers for the redesigned frontend. Everything here works on
// data the C++ API already returns — no invented coordinates, times,
// SLA deadlines, or notifications, because the API has none of those.
// If a feature needs data the API does not return (photo upload, a
// real map, SLA timers), it is deliberately NOT faked here.

export const STATUS_STEPS = ['Submitted', 'Under Review', 'Assigned', 'In Progress', 'Resolved']

export function statusClass(status) {
  return `badge badge-${String(status).toLowerCase().replace(/\s+/g, '-')}`
}

export function areaName(areas, areaId) {
  const a = (areas || []).find((x) => x.id === areaId)
  return a ? `${a.name} — ${a.city}` : `Area ${areaId}`
}

// Short name only, for table columns where "Gulberg — Lahore" would
// wrap onto two lines and make the row taller for no reason.
export function areaShort(areas, areaId) {
  const a = (areas || []).find((x) => x.id === areaId)
  return a ? a.name : `Area ${areaId}`
}

export function userName(users, userId) {
  const u = (users || []).find((x) => x.id === userId)
  return u ? u.fullName : `User ${userId}`
}

// Client-side search: the API only filters by status/category, so
// searching by code, title, description or category happens here.
export function matchesSearch(c, q) {
  if (!q) return true
  const needle = q.trim().toLowerCase()
  return [c.trackingCode, c.title, c.description, c.category, c.status]
    .filter(Boolean)
    .some((v) => String(v).toLowerCase().includes(needle))
}

// Sort client-side. createdAt is "YYYY-MM-DD HH:MM:SS", so string
// comparison is chronological. Priority sort matches the API's
// Counting Sort order (5 first) when the API sort is not used.
export function sortComplaints(list, mode) {
  const arr = [...list]
  if (mode === 'newest') return arr.sort((a, b) => String(b.createdAt).localeCompare(String(a.createdAt)))
  if (mode === 'oldest') return arr.sort((a, b) => String(a.createdAt).localeCompare(String(b.createdAt)))
  if (mode === 'priority') return arr.sort((a, b) => b.priorityScore - a.priorityScore)
  return arr
}

export function resolutionRate(stats) {
  if (!stats || !stats.totalComplaints) return 0
  return Math.round((stats.resolvedComplaints / stats.totalComplaints) * 100)
}

// Export the currently filtered complaints as CSV, in the browser.
// No server change needed; this is the "Export" button the reference
// dashboards have, implemented honestly from real data.
export function exportCsv(filename, complaints, areas) {
  const esc = (v) => `"${String(v ?? '').replace(/"/g, '""')}"`
  const rows = [
    ['Tracking code', 'Title', 'Category', 'Priority', 'Status', 'Area', 'Citizen ID', 'Created', 'Updated'],
    ...complaints.map((c) => [
      c.trackingCode, c.title, c.category, c.priorityScore, c.status,
      areaName(areas, c.areaId), c.citizenId, c.createdAt, c.updatedAt,
    ]),
  ]
  const csv = rows.map((r) => r.map(esc).join(',')).join('\n')
  const url = URL.createObjectURL(new Blob([csv], { type: 'text/csv' }))
  const a = document.createElement('a')
  a.href = url
  a.download = filename
  a.click()
  URL.revokeObjectURL(url)
}
