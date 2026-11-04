import { useEffect, useState } from 'react'
import { get } from './api.js'
import CitizenPage from './pages/CitizenPage.jsx'
import AdminPage from './pages/AdminPage.jsx'
import StaffPage from './pages/StaffPage.jsx'

export default function App() {
  const [tab, setTab] = useState('citizen')
  const [users, setUsers] = useState([])
  const [areas, setAreas] = useState([])
  const [currentUserId, setCurrentUserId] = useState(1)
  const [apiOk, setApiOk] = useState(null)
  const [loadError, setLoadError] = useState('')

  useEffect(() => {
    async function load() {
      try {
        const health = await get('/api/health')
        setApiOk(health.status === 'ok')
        const [loadedUsers, loadedAreas] = await Promise.all([get('/api/users'), get('/api/areas')])
        setUsers(loadedUsers)
        setAreas(loadedAreas)
        // Default to the seeded citizen (ID 1) if present, else the first user.
        if (loadedUsers.some((u) => u.id === 1)) setCurrentUserId(1)
        else if (loadedUsers.length > 0) setCurrentUserId(loadedUsers[0].id)
      } catch (err) {
        setApiOk(false)
        setLoadError(err.message)
      }
    }
    load()
  }, [])

  const currentUser = users.find((u) => u.id === currentUserId) || { id: currentUserId, fullName: 'User', role: 'Citizen' }

  return (
    <>
      <header>
        <h1>CivicFix Pakistan</h1>
        <p>Report a civic problem, track it, and follow it to resolution.</p>
      </header>
      <main>
        <div className={`banner ${apiOk ? 'ok' : ''}`}>
          <strong>Demo — no login yet.</strong>{' '}
          Authentication is not implemented in the C++ API yet (the users table
          has no password column), so instead of a login form you pick an
          existing user below. This is for demo only: anyone could pick any
          user, and <code>changedByUserId</code> is not proof of identity.
          {apiOk === false && <> API is not reachable: {loadError}</>}
          {apiOk === true && <> API is connected.</>}
        </div>

        <div className="toolbar">
          <label>Acting as (demo user selector)
            <select value={currentUserId} onChange={(e) => setCurrentUserId(Number(e.target.value))}>
              {users.map((u) => <option key={u.id} value={u.id}>{u.fullName} — {u.role} (ID {u.id})</option>)}
            </select>
          </label>
          <span className="small">Tip: citizen ID 1 is Ali Khan, admin ID 2 is CivicFix Admin (seeded users).</span>
        </div>

        <div className="tabs">
          <button className={tab === 'citizen' ? 'active' : ''} onClick={() => setTab('citizen')}>Citizen</button>
          <button className={tab === 'admin' ? 'active' : ''} onClick={() => setTab('admin')}>Admin</button>
          <button className={tab === 'staff' ? 'active' : ''} onClick={() => setTab('staff')}>Staff</button>
        </div>

        {tab === 'citizen' && <CitizenPage currentUser={currentUser} areas={areas} />}
        {tab === 'admin' && <AdminPage currentUser={currentUser} users={users} />}
        {tab === 'staff' && <StaffPage />}
      </main>
    </>
  )
}
