import { useState } from 'react'
import { get } from '../api.js'

// Shows one complaint plus its permanent status history, loaded on demand.
export default function ComplaintDetails({ complaint }) {
  const [history, setHistory] = useState(null)
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

  return (
    <div className="card">
      <h3>{complaint.trackingCode} — {complaint.title}</h3>
      <p>{complaint.description}</p>
      <p>
        <span className="badge">{complaint.status}</span>{' '}
        <span className="badge">{complaint.category}</span>{' '}
        Priority: {complaint.priorityScore} · Area ID: {complaint.areaId} · Citizen ID: {complaint.citizenId}
      </p>
      <p className="small">Created: {complaint.createdAt} · Updated: {complaint.updatedAt}</p>
      {history === null && (
        <button className="secondary" onClick={loadHistory} disabled={loading}>
          {loading ? 'Loading…' : 'View status history'}
        </button>
      )}
      {error && <p className="error">{error}</p>}
      {history && (
        <table>
          <thead>
            <tr><th>From</th><th>To</th><th>Notes</th><th>When</th></tr>
          </thead>
          <tbody>
            {history.length === 0 && <tr><td colSpan="4">No history yet.</td></tr>}
            {history.map((h) => (
              <tr key={h.id}>
                <td>{h.oldStatus}</td><td>{h.newStatus}</td>
                <td>{h.notes}</td><td>{h.createdAt}</td>
              </tr>
            ))}
          </tbody>
        </table>
      )}
    </div>
  )
}
