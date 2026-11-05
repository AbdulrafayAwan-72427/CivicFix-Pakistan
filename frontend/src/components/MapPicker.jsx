import { useEffect, useRef } from 'react'
import L from 'leaflet'
import 'leaflet/dist/leaflet.css'

// Map, using Leaflet + OpenStreetMap tiles (no API key needed).
// Two modes:
//  - picker: the user clicks the map (or "Use my location") to set
//    latitude/longitude for a new complaint. Centre: Lahore.
//  - readOnly + markers: show one complaint, or many markers for
//    the admin map. Clicking a marker calls markers[i].onSelect.
const LAHORE = [31.5204, 74.3587]

function pinIcon() {
  return L.divIcon({
    className: 'civicfix-pin',
    html: '<div style="font-size:26px;line-height:1;text-shadow:0 1px 3px rgba(0,0,0,.4)">📍</div>',
    iconSize: [26, 26],
    iconAnchor: [13, 26],
  })
}

export default function MapPicker({ latitude, longitude, onChange, readOnly = false, markers = null, height = 260 }) {
  const divRef = useRef(null)
  const mapRef = useRef(null)
  const markerRef = useRef(null)
  const layerRef = useRef(null)

  useEffect(() => {
    if (!divRef.current || mapRef.current) return
    const map = L.map(divRef.current, { scrollWheelZoom: !readOnly }).setView(
      latitude && longitude ? [latitude, longitude] : LAHORE, latitude && longitude ? 15 : 12,
    )
    L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png', {
      maxZoom: 19,
      attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a>',
    }).addTo(map)
    mapRef.current = map
    if (!readOnly) {
      map.on('click', (e) => { if (onChange) onChange(e.latlng.lat, e.latlng.lng) })
    }
    return () => { map.remove(); mapRef.current = null }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [])

  // Single picked point.
  useEffect(() => {
    const map = mapRef.current
    if (!map || markers) return
    if (latitude && longitude) {
      if (!markerRef.current) {
        markerRef.current = L.marker([latitude, longitude], { icon: pinIcon() }).addTo(map)
      } else {
        markerRef.current.setLatLng([latitude, longitude])
      }
      map.setView([latitude, longitude], Math.max(map.getZoom(), 14))
    } else if (markerRef.current) {
      map.removeLayer(markerRef.current)
      markerRef.current = null
    }
  }, [latitude, longitude, markers])

  // Many markers (admin map).
  useEffect(() => {
    const map = mapRef.current
    if (!map || !markers) return
    if (layerRef.current) map.removeLayer(layerRef.current)
    const group = L.layerGroup()
    const points = []
    markers.forEach((m) => {
      if (!m.latitude || !m.longitude) return
      points.push([m.latitude, m.longitude])
      const marker = L.marker([m.latitude, m.longitude], { icon: pinIcon() })
      marker.bindPopup(`<strong>${m.trackingCode || ''}</strong><br>${m.title || ''}`)
      if (m.onSelect) marker.on('click', () => m.onSelect(m))
      marker.addTo(group)
    })
    group.addTo(map)
    layerRef.current = group
    if (points.length > 0) map.fitBounds(points, { padding: [30, 30], maxZoom: 15 })
    else map.setView(LAHORE, 12)
  }, [markers])

  function useMyLocation() {
    if (!navigator.geolocation || !onChange) return
    navigator.geolocation.getCurrentPosition(
      (pos) => onChange(pos.coords.latitude, pos.coords.longitude),
      () => { /* user denied — they can still click the map */ },
    )
  }

  return (
    <div>
      <div ref={divRef} style={{ height, width: '100%', borderRadius: 12, overflow: 'hidden', border: '1px solid var(--border)' }} />
      {!readOnly && (
        <div style={{ display: 'flex', gap: 8, alignItems: 'center', marginTop: 8, flexWrap: 'wrap' }}>
          <button type="button" className="secondary small" onClick={useMyLocation}>📍 Use my location</button>
          <span className="small">
            {latitude && longitude
              ? `Picked: ${Number(latitude).toFixed(5)}, ${Number(longitude).toFixed(5)} — click the map to move the pin.`
              : 'Click the map to drop a pin where the problem is.'}
          </span>
        </div>
      )}
    </div>
  )
}
