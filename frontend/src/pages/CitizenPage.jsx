import { useEffect, useState } from 'react'
import { CATEGORIES, get, post } from '../api.js'
import { matchesSearch } from '../utils.js'
import ComplaintDetails from '../components/ComplaintDetails.jsx'
import EmptyState from '../components/EmptyState.jsx'
import Feedback from '../components/Feedback.jsx'
import MapPicker from '../components/MapPicker.jsx'

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
        ? ` Suggested category: ${created.aiSuggestedCategory}${created.aiConfidence ? ` (${Math.round(created.aiConfidence * 100)}% sure)` : ''}.`
        : ' We could not check the category automatically. An admin will review it.'
      setSubmitMsg(`Submitted! Your tracking code is ${created.trackingCode}. Keep it to track your complaint.${aiNote}`)
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

  async function track(event) {
    event.preventDefault()
    setTracked(null)
    setTrackError('')
    try {
      setTracked(await get(`/api/complaints/track/${encodeURIComponent(code.trim())}`))
    } catch (err) {
      setTrackError(err.message)
    }
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
        <div className="card" id="submit-card">
          <h2>Submit a complaint</h2>
          <p className="small">You will get a tracking code when you submit.</p>
          <form onSubmit={submit}>
            <label>Title
              <input placeholder="e.g. Broken road in Gulberg" value={form.title} onChange={(e) => update('title', e.target.value)} required />
            </label>
            <label>Describe the problem
              <textarea placeholder="What is wrong, and where exactly?" rows="4" value={form.description} onChange={(e) => update('description', e.target.value)} required />
            </label>
            <label>Category
              <select value={form.category} onChange={(e) => update('category', e.target.value)}>
                {CATEGORIES.map((c) => <option key={c}>{c}</option>)}
              </select>
            </label>
            <label>How urgent is this?
              <select value={form.severity} onChange={(e) => update('severity', e.target.value)}>
                <option value="low">Not urgent — it can wait</option>
                <option value="normal">Normal</option>
                <option value="high">Urgent — it is unsafe or blocking people</option>
              </select>
            </label>
            <label>Area
              <select value={form.areaId} onChange={(e) => {
                update('areaId', e.target.value)
                const picked = areas.find((a) => String(a.id) === e.target.value)
                if (picked && AREA_COORDS[picked.name] && !location.latitude) {
                  setLocation({ latitude: AREA_COORDS[picked.name][0], longitude: AREA_COORDS[picked.name][1] })
                }
              }} required>
                <option value="">Choose an area…</option>
                {areas.map((a) => <option key={a.id} value={a.id}>{a.name} — {a.city}</option>)}
              </select>
            </label>
            <label>Photo of the problem (optional, JPG/PNG, under 5 MB)
              <input type="file" accept="image/jpeg,image/png" onChange={(e) => onPhotoFile(e.target.files && e.target.files[0])} />
            </label>
            {photo.preview && (
              <img src={photo.preview} alt="Complaint preview" style={{ width: '100%', maxHeight: 240, objectFit: 'cover', borderRadius: 12 }} />
            )}
            <label>Where is it? (optional — click the map)
              <MapPicker
                latitude={location.latitude} longitude={location.longitude}
                onChange={(lat, lng) => setLocation({ latitude: lat, longitude: lng })}
              />
            </label>
            <button type="submit" disabled={submitBusy}>{submitBusy ? 'Submitting…' : 'Submit complaint'}</button>
          </form>
          <Feedback error={submitError} message={submitMsg} onClearMessage={() => setSubmitMsg('')} />
        </div>

        <div className="card" id="track-card">
          <h2>Track a complaint</h2>
          <p className="small">Enter the code you received when you submitted your report.</p>
          <form onSubmit={track}>
            <label>Tracking code
              <input placeholder="e.g. CFX-102" value={code} onChange={(e) => setCode(e.target.value)} required />
            </label>
            <button type="submit">Track</button>
          </form>
          <Feedback error={trackError} message="" />
          {tracked && <div className="mt-12"><ComplaintDetails complaint={tracked} areas={areas} users={users} currentUser={currentUser} /></div>}
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
