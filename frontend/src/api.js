// All data in this app comes from the C++ REST API.
// React never talks to SQLite, and never talks to any ML service.
//
// API base URL:
//   - Set VITE_API_URL to override it, e.g. VITE_API_URL=http://localhost:8080
//   - In `npm run dev` with no override, requests use the Vite proxy
//     (see vite.config.js), so the browser calls /api/... on port 5173
//     and Vite forwards them to http://localhost:8080.
//   - In a production build with no override, the default is
//     http://localhost:8080. That works from any origin now: the
//     C++ server sends CORS headers (8 Nov 2026) and answers
//     preflight OPTIONS requests, so a deployed frontend can call
//     it directly without a proxy.

// Auth (real login, added 5 Nov 2026): after logging in at
// POST /api/auth/login the server returns a token, stored in
// localStorage. Every request sends it as
// "Authorization: Bearer <token>"; the C++ server looks the token
// up in the sessions table and enforces the stored role
// server-side. No user id or role from the browser is trusted.
// Tokens expire after 24 hours; logging out deletes the token
// on the server and here.
const TOKEN_KEY = 'civicfix_token'
export function getAuthToken() { return localStorage.getItem(TOKEN_KEY) }
export function setAuthToken(token) {
  if (token) localStorage.setItem(TOKEN_KEY, token)
  else localStorage.removeItem(TOKEN_KEY)
}
export function clearAuthToken() { localStorage.removeItem(TOKEN_KEY) }

const configured = import.meta.env.VITE_API_URL
const API_BASE = configured !== undefined
  ? configured
  : (import.meta.env.DEV ? '' : 'http://localhost:8080')

export function apiUrl(endpoint) {
  if (API_BASE === '') return endpoint
  const base = API_BASE.replace(/\/$/, '')
  // Baseline fix, 9 Nov 2026 (handoff-2 phase 1): the old code stripped
  // the first 4 characters of EVERY endpoint when the base ended in
  // '/api', which is only right for '/api/...' endpoints. A photo path
  // '/uploads/demo.png' became '/apioads/demo.png' and the photo never
  // loaded on a deployed frontend. Only strip the '/api' prefix from
  // endpoints that actually start with it; anything else is appended
  // to the server root as-is.
  if (base.endsWith('/api') && endpoint.startsWith('/api/'))
    return base + endpoint.slice(4)
  if (base.endsWith('/api')) return base.slice(0, -4) + endpoint
  return base + endpoint
}

export async function api(endpoint, options = {}) {
  // Give up after 10 seconds instead of hanging forever: without a
  // timeout a stalled request left the app on "Connecting…" with no
  // way forward.
  const controller = new AbortController()
  const timer = setTimeout(() => controller.abort(), 10000)
  let response
  try {
    response = await fetch(apiUrl(endpoint), {
      signal: controller.signal,
      headers: {
        'Content-Type': 'application/json',
        ...(getAuthToken() ? { Authorization: `Bearer ${getAuthToken()}` } : {}),
        ...(options.headers || {}),
      },
      ...options,
    })
  } catch (err) {
    if (err.name === 'AbortError')
      throw new Error('The service did not respond within 10 seconds. Check that the backend is running, then try again.')
    // e.g. the C++ server is not running
    throw new Error('Could not reach the API. Is the C++ server running on port 8080?')
  } finally {
    clearTimeout(timer)
  }
  let data = null
  try {
    data = await response.json()
  } catch {
    data = null
  }
  if (!response.ok) {
    if (response.status === 401) {
      // The login has expired (or was ended elsewhere). Only the
      // initial load used to handle this, so a token that died
      // mid-session left the app showing errors on every action
      // instead of returning to the login page. Clear the dead
      // token and tell App.jsx to show the login page.
      clearAuthToken()
      window.dispatchEvent(new Event('civicfix:unauthorized'))
    }
    // The C++ API returns {"error": "..."} — show that message, not a generic one.
    const err = new Error((data && data.error) || `API request failed (${response.status})`)
    err.status = response.status
    throw err
  }
  return data
}

export const get = (endpoint) => api(endpoint)
export const post = (endpoint, body) =>
  api(endpoint, { method: 'POST', body: JSON.stringify(body) })
export const patch = (endpoint, body) =>
  api(endpoint, { method: 'PATCH', body: JSON.stringify(body) })

export const CATEGORIES = [
  'Garbage Collection',
  'Road Damage',
  'Water Supply',
  'Drainage',
  'Streetlight',
  'Other',
]

export const STATUSES = [
  'Submitted',
  'Under Review',
  'Assigned',
  'In Progress',
  'Resolved',
  'Rejected',
]

// The transitions the C++ API allows (see documentation/API.md).
// The API still validates them and returns 400 on a bad transition,
// so this list is only for showing sensible choices in the dropdown.
export const NEXT_STATUSES = {
  Submitted: ['Under Review', 'Rejected'],
  'Under Review': ['Assigned', 'Rejected'],
  Assigned: ['In Progress', 'Rejected'],
  'In Progress': ['Resolved', 'Rejected'],
  // Reopen (9 Nov 2026, handoff-2 phase 2): Admin only, back to
  // triage. The API enforces the Admin role; this list only decides
  // what the dropdown offers.
  Resolved: ['Under Review'],
  Rejected: ['Under Review'],
}
