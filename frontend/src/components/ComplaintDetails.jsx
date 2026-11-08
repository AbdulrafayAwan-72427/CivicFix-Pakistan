import { useState } from 'react'
import { apiUrl, get } from '../api.js'
import { STATUS_STEPS, areaName, hasCoords, statusClass, userName } from '../utils.js'
import MapPicker from './MapPicker.jsx'

// Shows one complaint with a visual progress stepper (the thing the
// reference dashboards all have, and the old table-only view lacked),
// plus its permanent status history as a timeline, loaded on demand.
export default function ComplaintDetails({ complaint, areas = [], users = [] }) {
  const [history, setHistory] = useState(null)
  const [showHistory, setShowHistory] = useState(false)
  const [error, setError] = useState('')
  const [loading, setLoading] = useState(false)

  async function loadHistory() {
    setError('')
    setLoading(true)
    try {
      setHistory(await get(`/api/complaints/${complaint.id}/history`))
    } catch (err) {
      setError(err.message)
    } finally {
      setLoading(false)
    }
  }

  function toggleHistory() {
    const next = !showHistory
    setShowHistory(next)
    if (next && history === null) loadHistory()
  }

  const stepIndex = STATUS_STEPS.indexOf(complaint.status)
  const rejected = complaint.status === 'Rejected'

  return (
    <div className="card">
      <h3>{complaint.trackingCode} — {complaint.title}</h3>
      <p>{complaint.description}</p>
      <p>
        <span className={statusClass(complaint.status)}>{complaint.status}</span>{' '}
        <span className="badge">{complaint.category}</span>{' '}
        <span className={`pill-priority p${complaint.priorityScore}`}>P{complaint.priorityScore}</span>
      </p>
      <p className="small">
        {areaName(areas, complaint.areaId)} · Submitted by {userName(users, complaint.citizenId)} · Created: {complaint.createdAt} · Updated: {complaint.updatedAt}
      </p>

      {complaint.photoPath && (
        <img
          src={apiUrl(`/${complaint.photoPath}`)}
          alt={`Photo for ${complaint.trackingCode}`}
          style={{ width: '100%', maxHeight: 320, objectFit: 'cover', borderRadius: 12, marginTop: 4 }}
        />
      )}

      {(complaint.aiSuggestedCategory || complaint.aiImageLabel) && (
        <p className="small" style={{ background: 'var(--green-100)', borderRadius: 10, padding: '8px 10px' }}>
          <span aria-hidden="true">🤖</span>{' '}
          {complaint.aiSuggestedCategory
            ? <>Suggested category: {complaint.aiSuggestedCategory}{complaint.aiConfidence ? ` (${Math.round(complaint.aiConfidence * 100)}% sure)` : ''}. {complaint.aiSuggestedCategory !== complaint.category ? `You selected ${complaint.category} — an admin will confirm.` : ''}</>
            : 'Suggested category: none.'}
          {complaint.aiImageLabel ? ` Photo looks like: ${complaint.aiImageLabel}.` : ' Photo not checked automatically.'}
        </p>
      )}

      {hasCoords(complaint.latitude, complaint.longitude) ? (
        <div style={{ marginTop: 8 }}>
          <MapPicker latitude={complaint.latitude} longitude={complaint.longitude} readOnly height={220} />
          <p className="small">Location: {Number(complaint.latitude).toFixed(5)}, {Number(complaint.longitude).toFixed(5)}</p>
        </div>
      ) : null}

      {rejected ? (
        <p className="error" style={{ marginTop: 0 }}>This report was rejected and is closed.</p>
      ) : (
        <ol className="stepper" aria-label="Complaint progress">
          {STATUS_STEPS.map((step, i) => {
            const state = i < stepIndex ? 'done' : i === stepIndex ? 'current' : 'upcoming'
            return (
              <li key={step} className={`step ${state}`}>
                <span className="step-dot" aria-hidden="true">{i < stepIndex ? '✓' : i + 1}</span>
                <span className="step-label">{step}</span>
                <span className="sr-only">
                  {state === 'done' ? ' — completed' : state === 'current' ? ' — current step' : ' — not started'}
                </span>
              </li>
            )
          })}
        </ol>
      )}

      <button className="secondary small" onClick={toggleHistory} disabled={loading}
        aria-expanded={showHistory} aria-controls={`history-${complaint.id}`}>
        {loading ? 'Loading…' : showHistory ? 'Hide status history' : 'View status history'}
      </button>
      {error && <p className="error" role="alert">{error}</p>}
      {showHistory && history && (
        <div className="timeline" id={`history-${complaint.id}`} style={{ marginTop: 12 }}>
          {history.length === 0 && <p className="small">No history yet.</p>}
          {history.map((h) => (
            <div className="timeline-item" key={h.id}>
              <span className="timeline-dot" />
              <div>
                <strong>{h.oldStatus} → {h.newStatus}</strong>
                <span className="small"> · {h.createdAt}</span>
                {h.notes && <p className="small" style={{ margin: '2px 0 0' }}>{h.notes}</p>}
              </div>
            </div>
          ))}
        </div>
      )}
    </div>
  )
}
