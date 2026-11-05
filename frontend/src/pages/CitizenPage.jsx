import { useEffect, useState } from 'react'
import { CATEGORIES, get, post } from '../api.js'
import { matchesSearch } from '../utils.js'
import ComplaintDetails from '../components/ComplaintDetails.jsx'
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
// "My complaints" is filtered in the browser from /api/complaints, because
// the API has no per-citizen endpoint yet.
//
// Second wave: my complaints now load automatically when you switch
// user (no more "press Load" as the only way to see them), with my
// own open/resolved counts, a search box, and a refresh button —
// the resident-dashboard basics from the web research.
export default function CitizenPage({ currentUser, areas, users = [] }) {
  const [form, setForm] = useState({ title: '', description: '', category: 'Garbage Collection', priorityScore: 3, areaId: '' })
  const [submitMsg, setSubmitMsg] = useState('')
  const [submitError, setSubmitError] = useState('')
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
  const [mineSearch, setMineSearch] = useState('')
  const [mineError, setMineError] = useState('')

  function update(field, value) {
    setForm((f) => ({ ...f, [field]: value }))
  }

  async function submit(event) {
    event.preventDefault()
    setSubmitMsg('')
    setSubmitError('')
    try {
      const created = await post('/api/complaints', {
        citizenId: currentUser.id,
        title: form.title,
        description: form.description,
        category: form.category,
        priorityScore: Number(form.priorityScore),
        areaId: Number(form.areaId),
        latitude: location.latitude || undefined,
        longitude: location.longitude || undefined,
        photoBase64: photo.base64 || undefined,
        photoExt: photo.ext || undefined,
      })
      const aiNote = created.aiSuggestedCategory
        ? ` AI checked it and suggests "${created.aiSuggestedCategory}"${created.aiConfidence ? ` (${Math.round(created.aiConfidence * 100)}% sure)` : ''}${created.aiImageLabel ? `, and your photo looks like: ${created.aiImageLabel}` : ''}.`
        : ' The AI service was not running, so no AI check was done on this one.'
      setSubmitMsg(`Submitted! Your tracking code is ${created.trackingCode}. Keep it to track your complaint.${aiNote}`)
      setForm({ title: '', description: '', category: 'Garbage Collection', priorityScore: 3, areaId: '' })
      setLocation({ latitude: 0, longitude: 0 })
      setPhoto({ base64: '', ext: '', preview: '' })
      loadMine()
    } catch (err) {
      setSubmitError(err.message)
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
    setMineError('')
    try {
      const all = await get('/api/complaints')
      setMine(all.filter((c) => c.citizenId === currentUser.id))
    } catch (err) {
      setMineError(err.message)
    }
  }

  useEffect(() => { loadMine() }, [currentUser.id])

  const filteredMine = mine.filter((c) => matchesSearch(c, mineSearch))
  const myOpen = mine.filter((c) => c.status !== 'Resolved' && c.status !== 'Rejected').length
  const myResolved = mine.filter((c) => c.status === 'Resolved').length

  return (
    <>
      <div className="stat-grid" style={{ marginBottom: 16 }}>
        <div className="stat"><div className="stat-icon green">📋</div><div><strong>{mine.length}</strong><span>My complaints</span></div></div>
        <div className="stat"><div className="stat-icon amber">⏳</div><div><strong>{myOpen}</strong><span>Still open</span></div></div>
        <div className="stat"><div className="stat-icon green">✅</div><div><strong>{myResolved}</strong><span>Resolved</span></div></div>
      </div>

      <div className="grid-2">
        <div className="card" id="submit-card">
          <h2>Submit a complaint</h2>
          <p className="small">Submitted as {currentUser.fullName} (user ID {currentUser.id}). The tracking code is generated by the API.</p>
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
            <label>Priority (1 = lowest, 5 = highest)
              <select value={form.priorityScore} onChange={(e) => update('priorityScore', e.target.value)}>
                {[1, 2, 3, 4, 5].map((n) => <option key={n} value={n}>{n}</option>)}
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
            <button type="submit">Submit complaint</button>
          </form>
          {submitMsg && <p className="success" style={{ marginTop: 12 }}>{submitMsg}</p>}
          {submitError && <p className="error" style={{ marginTop: 12 }}>{submitError}</p>}
        </div>

        <div className="card" id="track-card">
          <h2>Track a complaint</h2>
          <p className="small">Enter the code you got when you submitted, e.g. CFX-102. The progress stepper below shows exactly where it is.</p>
          <form onSubmit={track}>
            <label>Tracking code
              <input placeholder="e.g. CFX-102" value={code} onChange={(e) => setCode(e.target.value)} required />
            </label>
            <button type="submit">Track</button>
          </form>
          {trackError && <p className="error" style={{ marginTop: 12 }}>{trackError}</p>}
          {tracked && <div style={{ marginTop: 12 }}><ComplaintDetails complaint={tracked} areas={areas} users={users} /></div>}
        </div>
      </div>

      <div className="card" id="my-complaints-card">
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', flexWrap: 'wrap', gap: 10 }}>
          <h2 style={{ margin: 0 }}>My complaints</h2>
          <div className="refresh-row">
            <input className="search-input" placeholder="Search my complaints…" value={mineSearch} onChange={(e) => setMineSearch(e.target.value)} />
            <button className="secondary small" onClick={loadMine}>↻ Refresh</button>
          </div>
        </div>
        {mineError && <p className="error">{mineError}</p>}
        {filteredMine.length === 0 && <p className="small">{mine.length === 0 ? `No complaints for ${currentUser.fullName} yet. Submit one above.` : 'No complaints match this search.'}</p>}
        {filteredMine.map((c) => <ComplaintDetails key={c.id} complaint={c} areas={areas} users={users} />)}
      </div>
    </>
  )
}
