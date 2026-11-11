import { useEffect, useRef, useState } from 'react'
import { getNotifications, markNotificationRead } from '../api.js'

// In-app notification bell (Phase 3, 11 Nov 2026, handoff-2).
// The API delivers notifications only inside the app — no email or
// SMS provider is configured, so nothing here pretends a message
// was sent elsewhere. The list refreshes when opened and every
// minute while the app is open; opening an item marks it read.
export default function NotificationsBell() {
  const [items, setItems] = useState([])
  const [open, setOpen] = useState(false)
  const [error, setError] = useState('')
  const boxRef = useRef(null)

  async function load() {
    try {
      setItems(await getNotifications())
      setError('')
    } catch (err) {
      setError(err.message)
    }
  }

  useEffect(() => {
    load()
    const t = setInterval(load, 60000)
    return () => clearInterval(t)
  }, [])

  useEffect(() => {
    if (!open) return
    function onClick(event) {
      if (boxRef.current && !boxRef.current.contains(event.target)) setOpen(false)
    }
    document.addEventListener('mousedown', onClick)
    return () => document.removeEventListener('mousedown', onClick)
  }, [open])

  const unread = items.filter((n) => !n.isRead).length

  async function openItem(n) {
    if (!n.isRead) {
      try {
        await markNotificationRead(n.id)
        setItems((prev) => prev.map((x) => (x.id === n.id ? { ...x, isRead: true } : x)))
      } catch { /* stays unread; the bell still shows it */ }
    }
  }

  return (
    <div className="bell" ref={boxRef}>
      <button
        type="button"
        className="secondary small bell-btn"
        onClick={() => { setOpen((o) => !o); load() }}
        aria-expanded={open}
        aria-label={unread > 0 ? `${unread} unread notifications` : 'Notifications'}
      >
        <span aria-hidden="true">🔔</span>
        {unread > 0 && <span className="bell-badge">{unread}</span>}
      </button>
      {open && (
        <div className="bell-dropdown" role="dialog" aria-label="Notifications">
          <div className="bell-head">
            <strong>Notifications</strong>
            <button type="button" className="secondary small" onClick={() => setOpen(false)}>Close</button>
          </div>
          {error && <p className="error" role="alert">{error}</p>}
          {items.length === 0 && !error && <p className="small">No notifications yet. Assignments, status changes and notes will appear here.</p>}
          {items.slice(0, 10).map((n) => (
            <button key={n.id} type="button" className={`bell-item ${n.isRead ? 'read' : 'unread'}`} onClick={() => openItem(n)}>
              <span>{n.message}</span>
              <span className="small">{n.createdAt}</span>
            </button>
          ))}
        </div>
      )}
    </div>
  )
}
