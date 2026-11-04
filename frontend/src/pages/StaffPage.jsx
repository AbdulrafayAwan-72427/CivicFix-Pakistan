import { useEffect, useState } from 'react'
import { get } from '../api.js'
import ComplaintDetails from '../components/ComplaintDetails.jsx'

// Staff view: the active work queue, in FIFO order, plus all active
// complaints. There is no per-staff assignment endpoint in the C++ API
// yet, so this page cannot show "only my assignments" — it shows the
// shared queue and every complaint that is not Resolved/Rejected.
export default function StaffPage() {
  const [queue, setQueue] = useState([])
  const [active, setActive] = useState([])
  const [error, setError] = useState('')

  useEffect(() => {
    async function load() {
      try {
        setQueue(await get('/api/queue'))
        const all = await get('/api/complaints')
        setActive(all.filter((c) => c.status !== 'Resolved' && c.status !== 'Rejected'))
      } catch (err) { setError(err.message) }
    }
    load()
  }, [])

  return (
    <>
      <div className="card">
        <h2>Work queue (FIFO)</h2>
        <p className="small">This list comes from /api/queue, which uses the custom linked-list ComplaintQueue on the C++ server.</p>
        {error && <p className="error">{error}</p>}
        {queue.length === 0 && <p>No active complaints in the queue.</p>}
        <ol>
          {queue.map((c) => <li key={c.id}>{c.trackingCode} — {c.title} ({c.status}, priority {c.priorityScore})</li>)}
        </ol>
      </div>
      <div className="card">
        <h2>Active complaints</h2>
        {active.map((c) => <ComplaintDetails key={c.id} complaint={c} />)}
      </div>
    </>
  )
}
