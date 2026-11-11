import { useEffect, useState } from 'react'
import { CATEGORIES, get, post } from '../api.js'
import { matchesSearch } from '../utils.js'
import ComplaintDetails from '../components/ComplaintDetails.jsx'
import EmptyState from '../components/EmptyState.jsx'
import Feedback from '../components/Feedback.jsx'
import MapPicker from '../components/MapPicker.jsx'
import { LedgerButton, LedgerCard, LedgerField } from '../components/ledger/Ledger.jsx'

// Civic Ledger rollout (11 Nov 2026): the Track a complaint section
// below uses the approved Ledger kit. Everything else on this page
// keeps the existing styles until the theme is reviewed. Navigation
// stays in the citizen sidebar in App.jsx — there is no second nav
// on this screen (removed 11 Nov 2026: the Ledger nav always marked
// Track as active, even after another section was selected).

// Approximate centres, so picking an area drops the pin nearby —
// the citizen can still drag/click it to the exact spot.
const AREA_COORDS = {
  Gulberg: [31.5101, 74.3441],
  'Model Town': [31.4852, 74.3248],
  'Johar Town': [31.4697, 74.3006],
  'DHA Phase 5': [31.4750, 74.4200],
  'Faisal Town': [31.4700, 74.3500],
}

// Citizen view: submit a complaint, track one by code, and see my complaints.
// The API returns only this citizen's own complaints (see the security
// fix, 8 Nov 2026); the browser filter below is only for the search box.
export default function CitizenPage({ currentUser, areas, users = [] }) {
  const [form, setForm] = useState({ title: '', description: '', category: 'Garbage Collection', severity: 'normal', areaId: '' })
  const [submitMsg, setSubmitMsg] = useState('')
  const [submitError, setSubmitError] = useState('')
  const [submitBusy, setSubmitBusy] = useState(false)
  const [submittedCode, setSubmittedCode] = useState('')
  const [copied, setCopied] = useState(false)
  const [location, setLocation] = useState({ latitude: 0, longitude: 0 })
  const [photo, setPhoto] = useState({ base64: '', ext: '', preview: '' })

  function onPhotoFile(file) {
    if (!file) return
    if (!['image/jpeg', 'image/png'].includes(file.type)) {
      setSubmitError('Photo must be a JPG or PNG.')
      return
    }
    if (file.size > 5 * 1024 * 1024) {
      setSubmitError('Photo must be under 5 MB.')
      return
    }
    const reader = new FileReader()
    reader.onload = () => {
      const dataUrl = String(reader.result)
      setPhoto({ base64: dataUrl.split(',')[1] || '', ext: file.type === 'image/png' ? 'png' : 'jpg', preview: dataUrl })
      setSubmitError('')
    }
    reader.readAsDataURL(file)
  }
  const [code, setCode] = useState('CFX-102')
  const [tracked, setTracked] = useState(null)
  const [trackError, setTrackError] = useState('')
  const [trackBusy, setTrackBusy] = useState(false)
  const [mine, setMine] = useState([])
  const [mineLoading, setMineLoading] = useState(true)
  const [mineSearchInput, setMineSearchInput] = useState('')
  const [mineSearch, setMineSearch] = useState('')
  const [mineError, setMineError] = useState('')

  function update(field, value) {
    setForm((f) => ({ ...f, [field]: value }))
  }

  async function submit(event) {
    event.preventDefault()
    if (submitBusy) return
    setSubmitMsg('')
    setSubmitError('')
    setSubmitBusy(true)
    try {
      const created = await post('/api/complaints', {
        citizenId: currentUser.id,
        title: form.title,
        description: form.description,
        category: form.category,
        severity: form.severity,
        areaId: Number(form.areaId),
        latitude: location.latitude || undefined,
        longitude: location.longitude || undefined,
        photoBase64: photo.base64 || undefined,
        photoExt: photo.ext || undefined,
      })
      const aiNote = created.aiSuggestedCategory
        ? `Suggested category: ${created.aiSuggestedCategory}${created.aiConfidence ? ` (${Math.round(created.aiConfidence * 100)}% sure)` : ''}.`
        : 'We could not check the category automatically. An admin will review it.'
      // The tracking code lives in its own copyable card below (see the
      // success block); the message keeps only the AI note.
      setSubmitMsg(aiNote)
      setSubmittedCode(created.trackingCode)
      setCopied(false)
      setForm({ title: '', description: '', category: 'Garbage Collection', severity: 'normal', areaId: '' })
      setLocation({ latitude: 0, longitude: 0 })
      setPhoto({ base64: '', ext: '', preview: '' })
      loadMine()
    } catch (err) {
      setSubmitError(err.message)
    } finally {
      setSubmitBusy(false)
    }
  }

  // Copies the tracking code returned by the last successful submit.
  // Clipboard API first, with a select-and-copy fallback for browsers
  // without it (and for the jsdom test, which has no clipboard).
  async function copyCode() {
    try {
      if (navigator.clipboard?.writeText) {
        await navigator.clipboard.writeText(submittedCode)
      } else {
        const el = document.getElementById('ledger-submitted-code')
        el?.select()
        document.execCommand('copy')
      }
      setCopied(true)
      setTimeout(() => setCopied(false), 2000)
    } catch {
      setSubmitError('Could not copy the code. Select it and copy it manually.')
    }
  }

  async function track(event, codeOverride) {
    if (event) event.preventDefault()
    if (trackBusy) return
    const lookupCode = (codeOverride ?? code).trim()
    if (!lookupCode) return
    setCode(lookupCode)
    setTracked(null)
    setTrackError('')
    setTrackBusy(true)
    try {
      // Same call as before: public track-by-code, minimal response
      // (trackingCode / status / updatedAt). Permissions stay in the
      // backend; this screen never shows anyone else's complaints.
      setTracked(await get(`/api/complaints/track/${encodeURIComponent(lookupCode)}`))
    } catch (err) {
      setTrackError(err.message)
    } finally {
      setTrackBusy(false)
    }
    // Show the result (or the error) in the tracker below.
    document.getElementById('track-card')?.scrollIntoView({ behavior: 'smooth', block: 'start' })
  }

  async function loadMine() {
    setMineLoading(true)
    setMineError('')
    try {
      const all = await get('/api/complaints')
      setMine(all.filter((c) => c.citizenId === currentUser.id))
    } catch (err) {
      setMineError(err.message)
    } finally {
      setMineLoading(false)
    }
  }

  useEffect(() => { loadMine() }, [currentUser.id])
  useEffect(() => {
    const t = setTimeout(() => setMineSearch(mineSearchInput), 250)
    return () => clearTimeout(t)
  }, [mineSearchInput])

  const filteredMine = mine.filter((c) => matchesSearch(c, mineSearch))
  const myOpen = mine.filter((c) => c.status !== 'Resolved' && c.status !== 'Rejected').length
  const myResolved = mine.filter((c) => c.status === 'Resolved').length

  const skeleton = (
    <div className="skeleton-list" aria-busy="true" aria-label="Loading">
      <div className="skeleton-row" /><div className="skeleton-row" /><div className="skeleton-row" />
    </div>
  )

  return (
    <>
      <div className="stat-grid mb-16">
        <div className="stat"><div className="stat-icon green" aria-hidden="true">📋</div><div><strong>{mine.length}</strong><span>My complaints</span></div></div>
        <div className="stat"><div className="stat-icon amber" aria-hidden="true">⏳</div><div><strong>{myOpen}</strong><span>Still open</span></div></div>
        <div className="stat"><div className="stat-icon green" aria-hidden="true">✅</div><div><strong>{myResolved}</strong><span>Resolved</span></div></div>
      </div>

      <div className="grid-2">
        <div className="civic-ledger" id="submit-card">
          <div className="ledger-masthead">
            <img src="/logo.webp" alt="" aria-hidden="true" />
            <div>
              <p className="ledger-masthead-title">CivicFix</p>
              <p className="ledger-masthead-sub">Civic Ledger — submit a complaint</p>
            </div>
          </div>
          <LedgerCard title="Submit a complaint">
            <p className="ledger-note">You will get a tracking code when you submit.</p>
            <form onSubmit={submit}>
              <LedgerField
                label="Title"
                placeholder="e.g. Broken road in Gulberg"
                value={form.title}
                onChange={(e) => update('title', e.target.value)}
                required
              />
              <LedgerField
                label="Describe the problem"
                hint="What is wrong, and where exactly?"
                multiline
                rows="4"
                value={form.description}
                onChange={(e) => update('description', e.target.value)}
                required
              />
              <div className="ledger-field">
                <label htmlFor="ledger-category">Category</label>
                <select id="ledger-category" className="ledger-control" value={form.category} onChange={(e) => update('category', e.target.value)}>
                  {CATEGORIES.map((c) => <option key={c}>{c}</option>)}
                </select>
              </div>
              <div className="ledger-field">
                <label htmlFor="ledger-severity">How urgent is this?</label>
                <select id="ledger-severity" className="ledger-control" value={form.severity} onChange={(e) => update('severity', e.target.value)}>
                  <option value="low">Not urgent — it can wait</option>
                  <option value="normal">Normal</option>
                  <option value="high">Urgent — it is unsafe or blocking people</option>
                </select>
              </div>
              <div className="ledger-field">
                <label htmlFor="ledger-area">Area</label>
                <select id="ledger-area" className="ledger-control" value={form.areaId} onChange={(e) => {
                  update('areaId', e.target.value)
                  const picked = areas.find((a) => String(a.id) === e.target.value)
                  if (picked && AREA_COORDS[picked.name] && !location.latitude) {
                    setLocation({ latitude: AREA_COORDS[picked.name][0], longitude: AREA_COORDS[picked.name][1] })
                  }
                }} required>
                  <option value="">Choose an area…</option>
                  {areas.map((a) => <option key={a.id} value={a.id}>{a.name} — {a.city}</option>)}
                </select>
              </div>
              <div className="ledger-field">
                <label htmlFor="ledger-photo">Photo of the problem (optional, JPG/PNG, under 5 MB)</label>
                <input id="ledger-photo" type="file" className="ledger-control" accept="image/jpeg,image/png" onChange={(e) => onPhotoFile(e.target.files && e.target.files[0])} />
              </div>
              {photo.preview && (
                <img src={photo.preview} alt="Complaint preview" style={{ width: '100%', maxHeight: 240, objectFit: 'cover', borderRadius: 12 }} />
              )}
              <div className="ledger-field">
                <label>Where is it? (optional — click the map)</label>
                <MapPicker
                  latitude={location.latitude} longitude={location.longitude}
                  onChange={(lat, lng) => setLocation({ latitude: lat, longitude: lng })}
                />
              </div>
              <LedgerButton type="submit" busy={submitBusy} busyLabel="Submitting…">
                Submit complaint
              </LedgerButton>
            </form>
          </LedgerCard>
          {submittedCode && (
            <LedgerCard title="Complaint submitted">
              <LedgerField
                label="Your tracking code"
                hint="Save this code — you need it to track your complaint."
                id="ledger-submitted-code"
                value={submittedCode}
                readOnly
              />
              <div style={{ display: 'flex', flexWrap: 'wrap', gap: 8 }}>
                <LedgerButton variant="secondary" type="button" onClick={copyCode}>
                  {copied ? 'Copied' : 'Copy code'}
                </LedgerButton>
                <LedgerButton type="button" onClick={() => track(null, submittedCode)}>
                  Track this complaint
                </LedgerButton>
              </div>
              {submitMsg && <p className="ledger-note" role="status">{submitMsg}</p>}
            </LedgerCard>
          )}
          <Feedback error={submitError} message="" onClearMessage={() => setSubmitMsg('')} />
        </div>

        <div className="civic-ledger" id="track-card">
          <div className="ledger-masthead">
            <img src="/logo.webp" alt="" aria-hidden="true" />
            <div>
              <p className="ledger-masthead-title">CivicFix</p>
              <p className="ledger-masthead-sub">Civic Ledger — track a complaint</p>
            </div>
          </div>
          <LedgerCard title="Track a complaint">
            <p className="ledger-note">Enter the code you received when you submitted your report.</p>
            <form onSubmit={track}>
              <LedgerField
                label="Tracking code"
                hint="Codes look like CFX-102."
                id="ledger-track-code"
                placeholder="e.g. CFX-102"
                value={code}
                onChange={(e) => setCode(e.target.value)}
                error={trackError}
                required
              />
              <LedgerButton type="submit" busy={trackBusy} busyLabel="Looking up complaint…">
                Track complaint
              </LedgerButton>
            </form>
          </LedgerCard>
          {tracked && (
            <LedgerCard title={`Complaint ${tracked.trackingCode}`}>
              <p className="ledger-note" role="status">
                Status: <strong>{tracked.status}</strong>
                {tracked.updatedAt ? ` — last updated ${tracked.updatedAt}.` : '.'}
              </p>
              <LedgerButton
                variant="secondary"
                type="button"
                onClick={() => { setTracked(null); document.getElementById('ledger-track-code')?.focus() }}
              >
                Track another code
              </LedgerButton>
              {/* The public lookup returns only trackingCode, status
                  and updatedAt — it deliberately does NOT go through
                  ComplaintDetails, which expects a full complaint
                  (title, area, citizen, priority, history) and would
                  render "undefined" fields. Full details stay in
                  My complaints below, which uses the authenticated
                  list. */}
            </LedgerCard>
          )}
        </div>
      </div>

      <div className="card" id="my-complaints-card">
        <div className="card-head">
          <h2 className="mt-0">My complaints</h2>
          <div className="refresh-row">
            <input className="search-input" placeholder="Search my complaints…" value={mineSearchInput} onChange={(e) => setMineSearchInput(e.target.value)} />
            <button className="secondary small" onClick={loadMine} aria-label="Refresh my complaints"><span aria-hidden="true">↻</span> Refresh</button>
          </div>
        </div>
        <Feedback error={mineError} message="" />
        {mineLoading ? skeleton : filteredMine.length === 0 ? (
          <EmptyState
            icon="📭"
            title={mine.length === 0 ? `No complaints for ${currentUser.fullName} yet` : 'No complaints match this search'}
            hint={mine.length === 0 ? 'Submit one above.' : 'Try a different search.'}
          />
        ) : (
          filteredMine.map((c) => <ComplaintDetails key={c.id} complaint={c} areas={areas} users={users} currentUser={currentUser} />)
        )}
      </div>
    </>
  )
}
