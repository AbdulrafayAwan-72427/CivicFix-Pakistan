import { useEffect, useState } from 'react'
import { apiUrl, get, addNote } from '../api.js'
import { STATUS_STEPS, areaName, hasCoords, statusClass, userName } from '../utils.js'
import MapPicker from './MapPicker.jsx'

// Shows one complaint with a visual progress stepper (the thing the
// reference dashboards all have, and the old table-only view lacked),
// plus its permanent status history as a timeline, loaded on demand.
//
// Phase 3 (11 Nov 2026, handoff-2): the detail also shows the
// lifecycle banners — the rejection reason, the duplicate link, the
// escalated flag — and the progress-notes thread, where staff post
// field updates and citizens post follow-ups. Notes only load when
// the complaint has a real id: the public track-by-code view returns
// a minimal object (code/status/updatedAt) with no id, so there is
// nothing to load there and the thread stays hidden.
export default function ComplaintDetails({ complaint, areas = [], users = [], currentUser = null }) {
  const [history, setHistory] = useState(null)
  const [showHistory, setShowHistory] = useState(false)
  const [error, setError] = useState('')
  const [loading, setLoading] = useState(false)
  const [notes, setNotes] = useState(null)
  const [notesError, setNotesError] = useState('')
  const [noteBody, setNoteBody] = useState('')
  const [noteBusy, setNoteBusy] = useState(false)
  const [noteMsg, setNoteMsg] = useState('')

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

  async function loadNotes() {
    if (!complaint.id) return
    setNotesError('')
    try {
      setNotes(await get(`/api/complaints/${complaint.id}/notes`))
    } catch (err) {
      setNotesError(err.message)
    }
  }

  useEffect(() => { loadNotes() }, [complaint.id]) // eslint-disable-line react-hooks/exhaustive-deps

  async function submitNote(event) {
    event.preventDefault()
    if (noteBusy || !noteBody.trim()) return
    setNotesError('')
    setNoteMsg('')
    setNoteBusy(true)
    try {
      const created = await addNote(complaint.id, noteBody.trim())
      setNotes((prev) => [...(prev || []), created])
      setNoteBody('')
      setNoteMsg('Note added.')
    } catch (err) {
      setNotesError(err.message)
    } finally {
      setNoteBusy(false)
    }
  }

  const stepIndex = STATUS_STEPS.indexOf(complaint.status)
  const rejected = complaint.status === 'Rejected'
  const isDuplicate = Boolean(complaint.duplicateOf)

  return (
    <div className="card">
      <h3>{complaint.trackingCode} — {complaint.title}</h3>
      <p>{complaint.description}</p>
      <p>
        <span className={statusClass(complaint.status)}>{complaint.status}</span>{' '}
        <span className="badge">{complaint.category}</span>{' '}
        <span className={`pill-priority p${complaint.priorityScore}`}>P{complaint.priorityScore}</span>{' '}
        {complaint.escalated && <span className="badge badge-rejected">Escalated</span>}
      </p>
      <p className="small">
        {areaName(areas, complaint.areaId)} · Submitted by {userName(users, complaint.citizenId)} · Created: {complaint.createdAt} · Updated: {complaint.updatedAt}
      </p>

      {rejected && complaint.rejectionReason && (
        <p className="error" style={{ marginTop: 0 }} role="alert">
          Rejected: {complaint.rejectionReason}
        </p>
      )}
      {rejected && !complaint.rejectionReason && (
        <p className="error" style={{ marginTop: 0 }}>This report was rejected and is closed.</p>
      )}
      {isDuplicate && (
        <p className="small" style={{ background: 'var(--amber-soft, #fef3c7)', borderRadius: 10, padding: '8px 10px' }}>
          Duplicate of <strong>{complaint.duplicateOf}</strong>
          {complaint.resolutionNote ? ` — ${complaint.resolutionNote}` : ''}. This report was closed as a duplicate; follow the original for updates.
        </p>
      )}
      {complaint.resolutionNote && !isDuplicate && (
        <p className="small">Resolution: {complaint.resolutionNote}</p>
      )}

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

      {rejected ? null : (
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
                {/* Same-status rows are timeline events, not status
                    changes (assignment, reassignment, escalation):
                    show the event text instead of "X → X". */}
                <strong>{h.oldStatus === h.newStatus ? (h.notes || 'Update') : `${h.oldStatus} → ${h.newStatus}`}</strong>
                <span className="small"> · {h.createdAt}</span>
                {h.oldStatus !== h.newStatus && h.notes && <p className="small" style={{ margin: '2px 0 0' }}>{h.notes}</p>}
              </div>
            </div>
          ))}
        </div>
      )}

      {complaint.id && (
        <div className="mt-16">
          <h4 className="mt-0">Progress notes</h4>
          {notesError && <p className="error" role="alert">{notesError}</p>}
          {notes === null && !notesError && <p className="small">Loading notes…</p>}
          {notes !== null && notes.length === 0 && <p className="small">No notes yet. Staff post field updates here; citizens can add follow-ups.</p>}
          {notes !== null && notes.length > 0 && (
            <div className="timeline">
              {notes.map((n) => (
                <div className="timeline-item" key={n.id}>
                  <span className="timeline-dot" />
                  <div>
                    <strong>{userName(users, n.authorId) || n.authorRole}</strong>{' '}
                    <span className="badge">{n.authorRole}</span>
                    <span className="small"> · {n.createdAt}</span>
                    <p className="small" style={{ margin: '2px 0 0' }}>{n.body}</p>
                  </div>
                </div>
              ))}
            </div>
          )}
          {currentUser && (
            <form onSubmit={submitNote} className="mt-12">
              <label>Add a note
                <textarea
                  rows="2"
                  placeholder={currentUser.role === 'Citizen' ? 'Add a follow-up — e.g. the problem is still there, or it is fixed.' : 'Post a progress update for this complaint.'}
                  value={noteBody}
                  onChange={(e) => setNoteBody(e.target.value)}
                  maxLength={2000}
                  required
                />
              </label>
              <button type="submit" className="secondary small" disabled={noteBusy}>{noteBusy ? 'Adding…' : 'Add note'}</button>
              {noteMsg && <span className="small" style={{ marginLeft: 8 }}>{noteMsg}</span>}
            </form>
          )}
        </div>
      )}
    </div>
  )
}
