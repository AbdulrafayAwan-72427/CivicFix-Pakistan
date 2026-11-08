import { useEffect, useState } from 'react'
import { get, post, setAuthToken } from '../api.js'

// Login / register page. Shown instead of the whole app until the
// user has a token. On success it hands the logged-in user + token
// back to App.jsx, which then loads the role's workspace.
export default function LoginPage({ onLoggedIn }) {
  const [mode, setMode] = useState('login') // 'login' | 'register'
  const [fullName, setFullName] = useState('')
  const [email, setEmail] = useState('')
  const [password, setPassword] = useState('')
  const [areas, setAreas] = useState([])
  const [areaId, setAreaId] = useState(1)
  const [error, setError] = useState('')
  const [busy, setBusy] = useState(false)

  // Areas are public (GET /api/areas needs no login), so the register
  // form can offer a real choice. Before this, every new citizen was
  // silently put in area 1 (Gulberg) with no way to pick another.
  // If the list fails to load, the form still works with area 1.
  useEffect(() => {
    get('/api/areas').then(setAreas).catch(() => { /* keep area 1 fallback */ })
  }, [])

  async function submit(event) {
    event.preventDefault()
    setError('')
    setBusy(true)
    try {
      const endpoint = mode === 'login' ? '/api/auth/login' : '/api/auth/register'
      const body = mode === 'login'
        ? { email, password }
        : { fullName, email, password, areaId }
      const result = await post(endpoint, body)
      setAuthToken(result.token)
      onLoggedIn(result.user)
    } catch (err) {
      setError(err.message)
    } finally {
      setBusy(false)
    }
  }

  return (
    <div className="login-wrap">
      <div className="card login-card">
        <div className="brand" style={{ marginBottom: 8 }}>
          <img className="brand-logo" src="/logo.webp" alt="CivicFix logo" />
          <div>
            <div className="brand-title">CivicFix</div>
            <p>Report civic problems and track them to resolution.</p>
          </div>
        </div>
        <h2 style={{ margin: '8px 0 4px' }}>{mode === 'login' ? 'Log in' : 'Create an account'}</h2>
        <p className="small" style={{ marginTop: 0 }}>
          {mode === 'login'
            ? 'Log in with your email and password to report and track civic problems.'
            : 'New here? Create a citizen account — it takes a minute.'}
        </p>
        <form onSubmit={submit} className="form-grid">
          {mode === 'register' && (
            <label>Full name
              <input autoComplete="name" value={fullName} onChange={(e) => setFullName(e.target.value)} required placeholder="e.g. Ali Khan" />
            </label>
          )}
          <label>Email
            <input type="email" autoComplete="email" value={email} onChange={(e) => setEmail(e.target.value)} required placeholder="you@example.com" />
          </label>
          <label>Password
            <input type="password" autoComplete={mode === 'login' ? 'current-password' : 'new-password'} value={password} onChange={(e) => setPassword(e.target.value)} required minLength={6} placeholder={mode === 'login' ? 'Your password' : 'At least 6 characters'} />
          </label>
          {mode === 'register' && (
            <label>Area
              <select value={areaId} onChange={(e) => setAreaId(Number(e.target.value))} required>
                {areas.length === 0
                  ? <option value={1}>Gulberg — Lahore</option>
                  : areas.map((a) => <option key={a.id} value={a.id}>{a.name} — {a.city}</option>)}
              </select>
            </label>
          )}
          {error && <div className="banner error">{error}</div>}
          <button className="btn-primary" type="submit" disabled={busy}>
            {busy ? 'Please wait…' : mode === 'login' ? 'Log in' : 'Create account'}
          </button>
        </form>
        <p className="small" style={{ textAlign: 'center' }}>
          {mode === 'login' ? (
            <>No account yet? <button className="link-btn" type="button" onClick={() => { setMode('register'); setError('') }}>Create one</button></>
          ) : (
            <>Already have an account? <button className="link-btn" type="button" onClick={() => { setMode('login'); setError('') }}>Log in</button></>
          )}
        </p>
        {import.meta.env.DEV && (
          <div className="banner" style={{ marginTop: 16 }}>
            <strong>Demo accounts</strong> (seeded, change the passwords for real use):<br />
            Admin — <code>admin@civicfix.pk</code> / <code>admin123</code><br />
            Citizen — <code>ali@example.com</code> / <code>ali123</code>
          </div>
        )}
      </div>
    </div>
  )
}
