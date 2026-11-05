import { useEffect, useState } from 'react'
import { get, post, clearAuthToken, getAuthToken } from './api.js'
import LoginPage from './pages/LoginPage.jsx'
import CitizenPage from './pages/CitizenPage.jsx'
import AdminPage from './pages/AdminPage.jsx'
import StaffPage from './pages/StaffPage.jsx'

// Role-based sidebar (RBAC): the sidebar no longer lists Citizen,
// Admin and Staff together. The page and the sidebar are driven by
// the LOGGED-IN user's role — Admin sees only admin tools, Staff
// sees only staff tools, Citizen sees only citizen tools. The C++
// API enforces the same roles server-side with the login token
// (see api.js); the sidebar hiding is the UI half, not the
// security half. Nobody sees the app at all without logging in.
const PAGES = {
  citizen: { title: 'Citizen workspace', subtitle: 'Report a civic problem, track it, and follow it to resolution.', icon: '👤' },
  admin: { title: 'Operations Center', subtitle: 'Complaints, assignments, and the admin action history.', icon: '🛡️' },
  staff: { title: 'Staff workspace', subtitle: 'Your work queue and every complaint still open.', icon: '🧑‍🔧' },
}

export default function App() {
  const [tab, setTab] = useState('citizen')
  const [activeNav, setActiveNav] = useState('home')
  const [users, setUsers] = useState([])
  const [areas, setAreas] = useState([])
  const [currentUser, setCurrentUser] = useState(null)
  const [apiOk, setApiOk] = useState(null)
  const [loadError, setLoadError] = useState('')

  // If a token is saved from an earlier visit, check it with the
  // API (GET /api/auth/me). If it is missing or expired, the login
  // page is shown instead of the app.
  useEffect(() => {
    async function load() {
      try {
        const health = await get('/api/health')
        setApiOk(health.status === 'ok')
        if (!getAuthToken()) return
        const me = await get('/api/auth/me')
        setCurrentUser(me)
        const [loadedUsers, loadedAreas] = await Promise.all([get('/api/users'), get('/api/areas')])
        setUsers(loadedUsers)
        setAreas(loadedAreas)
      } catch (err) {
        setApiOk(false)
        setLoadError(err.message)
        if (err.status === 401) { clearAuthToken(); setCurrentUser(null) }
      }
    }
    load()
  }, [])

  const page = PAGES[tab]

  // RBAC: the visible page follows the logged-in user's role. A
  // Citizen can never land on the Admin or Staff page through this
  // app, even by picking a sidebar item — because those items are
  // not rendered for them.
  useEffect(() => {
    if (!currentUser) return
    const role = (currentUser.role || 'Citizen').toLowerCase()
    if (['citizen', 'admin', 'staff'].includes(role)) setTab(role)
    setActiveNav('home')
    window.scrollTo({ top: 0 })
  }, [currentUser]) // eslint-disable-line react-hooks/exhaustive-deps

  async function handleLoggedIn(user) {
    setCurrentUser(user)
    try {
      const [loadedUsers, loadedAreas] = await Promise.all([get('/api/users'), get('/api/areas')])
      setUsers(loadedUsers)
      setAreas(loadedAreas)
    } catch (err) {
      setLoadError(err.message)
    }
  }

  async function logout() {
    try { await post('/api/auth/logout', {}) } catch { /* token may already be expired */ }
    clearAuthToken()
    setCurrentUser(null)
    setUsers([])
    setAreas([])
  }

  // Sidebar shortcuts. These are NOT fake pages — the earlier
  // reference had Map, Reports, Analytics and Settings, which this
  // app does not have, so every shortcut below goes to a real
  // section that already exists on the current role's page.
  // The role never changes here: goTo only scrolls within the
  // current role's page.
  function goTo(targetId, navKey) {
    setActiveNav(navKey)
    let attempts = 0
    const scroll = () => {
      if (targetId) {
        const el = document.getElementById(targetId)
        if (el) { el.scrollIntoView({ behavior: 'smooth', block: 'start' }); return }
        if (attempts < 10) { attempts += 1; setTimeout(scroll, 150) }
      } else {
        window.scrollTo({ top: 0, behavior: 'smooth' })
      }
    }
    setTimeout(scroll, 100)
  }

  // Re-fetch users after Admin creates a staff member, so the new
  // staff user appears in the Admin staff list and the assignment
  // dropdown without a page refresh.
  async function reloadUsers() {
    try { setUsers(await get('/api/users')) } catch { /* banner already shows API errors */ }
  }

  if (apiOk === null && !currentUser) {
    return (
      <div className="app-shell" style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', minHeight: '100vh' }}>
        <p className="small">Connecting to the API…</p>
      </div>
    )
  }
  if (!currentUser) return <LoginPage onLoggedIn={handleLoggedIn} />

  return (
    <div className="app-shell">
      <aside className="sidebar">
        <div className="brand">
          <img className="brand-logo" src="/logo.webp" alt="CivicFix logo" />
          <div>
            <h1>CivicFix</h1>
            <p>Pakistan · City Dashboard</p>
          </div>
        </div>

        <nav className="nav" aria-label="Workspace">
          {/* One role only, per the logged-in user. No other role's
              links are rendered at all — they are not just hidden
              with CSS, they are absent from the page. */}
          <div className="nav-label">Workspace</div>
          {tab === 'citizen' && (
            <button className={`nav-btn ${activeNav === 'home' ? 'active' : ''}`} onClick={() => goTo(null, 'home')}>
              <span className="nav-icon">👤</span>Citizen
            </button>
          )}
          {tab === 'admin' && (
            <button className={`nav-btn ${activeNav === 'home' ? 'active' : ''}`} onClick={() => goTo(null, 'home')}>
              <span className="nav-icon">🛡️</span>Operations Center
            </button>
          )}
          {tab === 'staff' && (
            <button className={`nav-btn ${activeNav === 'home' ? 'active' : ''}`} onClick={() => goTo(null, 'home')}>
              <span className="nav-icon">🧑‍🔧</span>Work queue
            </button>
          )}

          <div className="nav-label">Operations</div>
          {tab === 'citizen' && (<>
            <button className={`nav-btn ${activeNav === 'submit' ? 'active' : ''}`} onClick={() => goTo('submit-card', 'submit')}>
              <span className="nav-icon">📝</span>Submit a report
            </button>
            <button className={`nav-btn ${activeNav === 'track' ? 'active' : ''}`} onClick={() => goTo('track-card', 'track')}>
              <span className="nav-icon">🔍</span>Track a report
            </button>
            <button className={`nav-btn ${activeNav === 'mine' ? 'active' : ''}`} onClick={() => goTo('my-complaints-card', 'mine')}>
              <span className="nav-icon">📄</span>My complaints
            </button>
          </>)}
          {tab === 'staff' && (<>
            <button className={`nav-btn ${activeNav === 'queue' ? 'active' : ''}`} onClick={() => goTo('work-queue-card', 'queue')}>
              <span className="nav-icon">⏳</span>Work queue
            </button>
            <button className={`nav-btn ${activeNav === 'active' ? 'active' : ''}`} onClick={() => goTo('active-complaints-card', 'active')}>
              <span className="nav-icon">🗂️</span>Active complaints
            </button>
          </>)}
          {tab === 'admin' && (
            <button className={`nav-btn ${activeNav === 'complaints' ? 'active' : ''}`} onClick={() => goTo('complaints-card', 'complaints')}>
              <span className="nav-icon">🗂️</span>Complaints
            </button>
          )}

          {tab === 'admin' && (<>
            <div className="nav-label">Management</div>
            <button className={`nav-btn ${activeNav === 'team' ? 'active' : ''}`} onClick={() => goTo('team-card', 'team')}>
              <span className="nav-icon">👥</span>Team
            </button>
            <button className={`nav-btn ${activeNav === 'actions' ? 'active' : ''}`} onClick={() => goTo('admin-actions-card', 'actions')}>
              <span className="nav-icon">↩️</span>Admin actions
            </button>
          </>)}
        </nav>

        <div className="sidebar-user">
          <strong>{currentUser.fullName}</strong>
          <span>{currentUser.role} · ID {currentUser.id}</span>
        </div>
      </aside>

      <div className="main-area">
        <header className="topbar">
          <div>
            <h2>{page.title}</h2>
            <p>{page.subtitle}</p>
          </div>
          <div className="topbar-actions">
            {apiOk === true && <span className="pill"><span className="dot" /> API connected</span>}
            {apiOk === false && <span className="pill bad"><span className="dot" /> API not reachable</span>}
            <span className="small" style={{ alignSelf: 'center' }}>
              Logged in as <strong>{currentUser.fullName}</strong> ({currentUser.role})
            </span>
            <button className="btn" type="button" onClick={logout}>Log out</button>
          </div>
        </header>

        <main className="content">
          {apiOk === false && (
            <div className="banner error">
              <strong>API is not reachable: {loadError}</strong>{' '}
              Start the C++ API on port 8080 first, then refresh this page:{' '}
              <code>backend-cpp\civicfix_api.exe 8080 database\api_test.db</code>
              <br />
              <span className="small">Tip: demo logins are admin@civicfix.pk / admin123 and ali@example.com / ali123.</span>
            </div>
          )}
          {apiOk === true && (
            <p className="small" style={{ marginTop: 0 }}>
              Logged in as {currentUser.fullName} ({currentUser.role}). The sidebar shows only
              this role's tools, and the API checks the login token's role for every change
              (status, assignment, staff, undo, new report). Your login stays valid for 24
              hours; use Log out in the top bar to end it sooner.
            </p>
          )}

          {tab === 'citizen' && <CitizenPage currentUser={currentUser} areas={areas} users={users} />}
          {tab === 'admin' && <AdminPage currentUser={currentUser} users={users} areas={areas} onUsersChanged={reloadUsers} />}
          {tab === 'staff' && <StaffPage areas={areas} users={users} />}
        </main>
      </div>
    </div>
  )
}
