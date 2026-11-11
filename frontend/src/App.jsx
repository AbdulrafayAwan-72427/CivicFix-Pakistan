import { useEffect, useState } from 'react'
import { get, post, clearAuthToken, getAuthToken } from './api.js'
import LoginPage from './pages/LoginPage.jsx'
import CitizenPage from './pages/CitizenPage.jsx'
import AdminPage from './pages/AdminPage.jsx'
import StaffPage from './pages/StaffPage.jsx'
import NotificationsBell from './components/NotificationsBell.jsx'

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
  const [adminSection, setAdminSection] = useState('overview')
  const [users, setUsers] = useState([])
  const [areas, setAreas] = useState([])
  const [currentUser, setCurrentUser] = useState(null)
  const [apiOk, setApiOk] = useState(null)
  const [loadError, setLoadError] = useState('')
  // The API-down banner used to show a Windows-only command to
  // everyone; show the right one for this machine instead.
  const isWindows = typeof navigator !== 'undefined' && navigator.userAgent.includes('Windows')
  const reduceMotion = typeof window !== 'undefined' && window.matchMedia('(prefers-reduced-motion: reduce)').matches

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
        console.error('API load failed:', err)
        if (err.status === 401) { clearAuthToken(); setCurrentUser(null) }
      }
    }
    load()
  }, [])

  // A token can also die mid-session (24-hour expiry, or logged
  // out elsewhere). api.js fires this event on any 401, so the
  // app returns to the login page instead of erroring on every
  // action until the next refresh.
  useEffect(() => {
    const onUnauthorized = () => { clearAuthToken(); setCurrentUser(null) }
    window.addEventListener('civicfix:unauthorized', onUnauthorized)
    return () => window.removeEventListener('civicfix:unauthorized', onUnauthorized)
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
    setAdminSection('overview')
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
    const behavior = reduceMotion ? 'auto' : 'smooth'
    let attempts = 0
    const scroll = () => {
      if (targetId) {
        const el = document.getElementById(targetId)
        if (el) { el.scrollIntoView({ behavior, block: 'start' }); return }
        if (attempts < 10) { attempts += 1; setTimeout(scroll, 150) }
      } else {
        window.scrollTo({ top: 0, behavior })
      }
    }
    setTimeout(scroll, 100)
  }

  // Admin sidebar buttons switch the admin tab (Overview /
  // Complaints / Team / Actions) instead of scrolling.
  function goAdminSection(sectionKey, navKey) {
    setAdminSection(sectionKey)
    setActiveNav(navKey)
    window.scrollTo({ top: 0, behavior: reduceMotion ? 'auto' : 'smooth' })
  }

  // Re-fetch users after Admin creates a staff member, so the new
  // staff user appears in the Admin staff list and the assignment
  // dropdown without a page refresh.
  async function reloadUsers() {
    try { setUsers(await get('/api/users')) } catch { /* banner already shows API errors */ }
  }

  if (apiOk === null && !currentUser) {
    return (
      <div className="app-shell">
        <div className="loading-screen">
          <div className="spinner" aria-hidden="true" />
          <p className="small">Connecting to the service…</p>
        </div>
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
            <div className="brand-title">CivicFix</div>
            <p>Lahore, Pakistan</p>
          </div>
        </div>

        <nav className="nav" aria-label="Sections">
          {/* One role only, per the logged-in user. No other role's
              links are rendered at all — they are not just hidden
              with CSS, they are absent from the page. Group labels
              name the sections, not the org chart. */}
          {tab === 'citizen' && (<>
            <div className="nav-label">Home</div>
            <button className={`nav-btn ${activeNav === 'home' ? 'active' : ''}`} onClick={() => goTo(null, 'home')}>
              <span className="nav-icon" aria-hidden="true">👤</span>Citizen
            </button>
            <div className="nav-label">My reports</div>
            <button className={`nav-btn ${activeNav === 'submit' ? 'active' : ''}`} onClick={() => goTo('submit-card', 'submit')}>
              <span className="nav-icon" aria-hidden="true">📝</span>Submit a report
            </button>
            <button className={`nav-btn ${activeNav === 'track' ? 'active' : ''}`} onClick={() => goTo('track-card', 'track')}>
              <span className="nav-icon" aria-hidden="true">🔍</span>Track a report
            </button>
            <button className={`nav-btn ${activeNav === 'mine' ? 'active' : ''}`} onClick={() => goTo('my-complaints-card', 'mine')}>
              <span className="nav-icon" aria-hidden="true">📄</span>My complaints
            </button>
          </>)}
          {tab === 'staff' && (<>
            <div className="nav-label">Work</div>
            <button className={`nav-btn ${activeNav === 'home' ? 'active' : ''}`} onClick={() => goTo(null, 'home')}>
              <span className="nav-icon" aria-hidden="true">🧑‍🔧</span>Work queue
            </button>
            <button className={`nav-btn ${activeNav === 'queue' ? 'active' : ''}`} onClick={() => goTo('work-queue-card', 'queue')}>
              <span className="nav-icon" aria-hidden="true">⏳</span>Work queue
            </button>
            <button className={`nav-btn ${activeNav === 'active' ? 'active' : ''}`} onClick={() => goTo('active-complaints-card', 'active')}>
              <span className="nav-icon" aria-hidden="true">🗂️</span>Active complaints
            </button>
          </>)}
          {tab === 'admin' && (<>
            <div className="nav-label">Overview</div>
            <button className={`nav-btn ${activeNav === 'home' ? 'active' : ''}`} onClick={() => goAdminSection('overview', 'home')}>
              <span className="nav-icon" aria-hidden="true">🛡️</span>Operations Center
            </button>
            <div className="nav-label">Manage</div>
            <button className={`nav-btn ${activeNav === 'complaints' ? 'active' : ''}`} onClick={() => goAdminSection('complaints', 'complaints')}>
              <span className="nav-icon" aria-hidden="true">🗂️</span>Complaints
            </button>
            <div className="nav-label">Admin</div>
            <button className={`nav-btn ${activeNav === 'team' ? 'active' : ''}`} onClick={() => goAdminSection('team', 'team')}>
              <span className="nav-icon" aria-hidden="true">👥</span>Team
            </button>
            <button className={`nav-btn ${activeNav === 'actions' ? 'active' : ''}`} onClick={() => goAdminSection('actions', 'actions')}>
              <span className="nav-icon" aria-hidden="true">↩️</span>Admin actions
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
            <h1>{page.title}</h1>
            <p>{page.subtitle}</p>
          </div>
          <div className="topbar-actions">
            {apiOk === true && <span className="pill"><span className="dot" /> API connected</span>}
            {apiOk === false && <span className="pill bad"><span className="dot" /> API not reachable</span>}
            <NotificationsBell />
            <span className="small topbar-user">
              Logged in as <strong>{currentUser.fullName}</strong> ({currentUser.role})
            </span>
            <button className="btn" type="button" onClick={logout}>Log out</button>
          </div>
        </header>

        <main className="content">
          {apiOk === false && (
            <div className="banner error">
              <strong>Cannot reach the service.</strong>{' '}
              The backend is not responding on port 8080. Start it, then refresh:{' '}
              <code>{isWindows
                ? 'backend-cpp\\civicfix_api.exe 8080 database\\api_test.db'
                : './backend-cpp/civicfix_api 8080 database/api_test.db'}</code>
            </div>
          )}

          {tab === 'citizen' && <CitizenPage currentUser={currentUser} areas={areas} users={users} onNavigate={(targetId, navKey) => goTo(targetId, navKey)} />}
          {tab === 'admin' && <AdminPage currentUser={currentUser} users={users} areas={areas} onUsersChanged={reloadUsers} section={adminSection} onSectionChange={setAdminSection} />}
          {tab === 'staff' && <StaffPage currentUser={currentUser} areas={areas} users={users} />}
        </main>
      </div>
    </div>
  )
}
