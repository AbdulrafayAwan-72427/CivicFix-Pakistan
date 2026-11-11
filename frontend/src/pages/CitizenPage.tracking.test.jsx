// @vitest-environment jsdom
// Regression tests for the Civic Ledger tracking rollout fixes
// (11 Nov 2026, after commit 5274271):
//  1. The public lookup result shows ONLY the Ledger summary —
//     tracking code, status, last update — and never the full
//     ComplaintDetails view. The public endpoint returns only
//     { trackingCode, status, updatedAt }, and ComplaintDetails
//     expects a full complaint, so it rendered "undefined" fields
//     and offered a history view for a case with no history.
//  2. There is no duplicate Ledger nav on the tracking screen;
//     the citizen sidebar in App.jsx owns navigation.
// Run with: npm test
import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { render, screen, waitFor, within, cleanup } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import CitizenPage from './CitizenPage.jsx'
import { get, post } from '../api.js'

// jsdom has no scrollIntoView implementation; stub it so the
// "Track this complaint" scroll does not spam the console.
Element.prototype.scrollIntoView = vi.fn()

vi.mock('../api.js', async (importOriginal) => {
  const orig = await importOriginal()
  return { ...orig, get: vi.fn(), post: vi.fn() }
})
// Leaflet needs a real map layout; the map is not under test here.
vi.mock('../components/MapPicker.jsx', () => ({ default: () => null }))

const CURRENT_USER = { id: 1, fullName: 'Demo Citizen', role: 'Citizen' }
const AREAS = [{ id: 1, name: 'Gulberg', city: 'Lahore' }]
const TRACK_URL = '/api/complaints/track/CFX-102'
// Exactly what the public endpoint returns — nothing else.
const MINIMAL_TRACK = { trackingCode: 'CFX-102', status: 'Under Review', updatedAt: '2026-11-11 10:00:00' }
const FULL_COMPLAINT = {
  id: 2, citizenId: 1, trackingCode: 'CFX-102', title: 'Overflowing bins',
  description: 'Bins overflowing on our street', category: 'Garbage Collection',
  status: 'Under Review', priorityScore: 3, areaId: 1,
  createdAt: '2026-11-11 09:00:00', updatedAt: '2026-11-11 10:00:00',
}

function routeMine(complaints = []) {
  get.mockImplementation((url) => {
    if (url === '/api/complaints') return Promise.resolve(complaints)
    if (url === TRACK_URL) return Promise.resolve(MINIMAL_TRACK)
    if (url === '/api/complaints/2/notes') return Promise.resolve([])
    if (url === '/api/complaints/2/history') return Promise.resolve([
      { id: 1, oldStatus: 'Submitted', newStatus: 'Under Review', notes: '', createdAt: '2026-11-11 09:30:00' },
    ])
    return Promise.reject(new Error(`unexpected GET ${url}`))
  })
}

function trackSection() {
  return document.getElementById('track-card')
}

async function submitCode(user, code) {
  const input = screen.getByLabelText(/tracking code/i)
  await user.clear(input)
  await user.type(input, code)
  await user.click(screen.getByRole('button', { name: 'Track complaint' }))
}

beforeEach(() => {
  vi.clearAllMocks()
  routeMine([])
})
// Testing Library's auto-cleanup needs a global afterEach, which
// Vitest does not provide without globals: true — unmount each
// render explicitly so later tests do not see earlier pages.
afterEach(() => cleanup())

describe('public tracking result (Ledger summary only)', () => {
  it('renders the summary from exactly { trackingCode, status, updatedAt }', async () => {
    const user = userEvent.setup()
    render(<CitizenPage currentUser={CURRENT_USER} areas={AREAS} users={[]} />)
    await submitCode(user, 'CFX-102')

    const section = trackSection()
    await waitFor(() => expect(within(section).getByText('Complaint CFX-102')).toBeTruthy())
    expect(within(section).getByText(/Under Review/)).toBeTruthy()
    expect(within(section).getByText(/last updated 2026-11-11 10:00:00/)).toBeTruthy()
    expect(within(section).getByRole('button', { name: 'Track another code' })).toBeTruthy()
  })

  it('shows no private-detail fields and no history view', async () => {
    const user = userEvent.setup()
    render(<CitizenPage currentUser={CURRENT_USER} areas={AREAS} users={[]} />)
    await submitCode(user, 'CFX-102')
    await waitFor(() => expect(screen.getByText('Complaint CFX-102')).toBeTruthy())

    const section = trackSection()
    expect(within(section).queryByText('View status history')).toBeNull()
    expect(within(section).queryByText(/undefined/)).toBeNull()
    expect(within(section).queryByText(/Area undefined/)).toBeNull()
    expect(within(section).queryByText(/Submitted by/)).toBeNull()
  })

  it('sends no history or notes request for the public lookup', async () => {
    const user = userEvent.setup()
    render(<CitizenPage currentUser={CURRENT_USER} areas={AREAS} users={[]} />)
    await submitCode(user, 'CFX-102')
    await waitFor(() => expect(screen.getByText('Complaint CFX-102')).toBeTruthy())

    const called = get.mock.calls.map(([url]) => url)
    expect(called).toContain(TRACK_URL)
    expect(called.filter((url) => url.includes('/history'))).toEqual([])
    expect(called.filter((url) => url.includes('/notes'))).toEqual([])
  })

  it('shows a loading state while the lookup runs', async () => {
    const user = userEvent.setup()
    let resolveTrack
    get.mockImplementation((url) => {
      if (url === '/api/complaints') return Promise.resolve([])
      if (url === TRACK_URL) return new Promise((resolve) => { resolveTrack = () => resolve(MINIMAL_TRACK) })
      return Promise.reject(new Error(`unexpected GET ${url}`))
    })
    render(<CitizenPage currentUser={CURRENT_USER} areas={AREAS} users={[]} />)
    await submitCode(user, 'CFX-102')

    const button = screen.getByRole('button', { name: /looking up complaint/i })
    expect(button.disabled).toBe(true)
    expect(button.getAttribute('aria-busy')).toBe('true')
    resolveTrack()
    await waitFor(() => expect(screen.getByText('Complaint CFX-102')).toBeTruthy())
  })

  it('shows unknown-code feedback on the field', async () => {
    const user = userEvent.setup()
    get.mockImplementation((url) => {
      if (url === '/api/complaints') return Promise.resolve([])
      if (url === '/api/complaints/track/CFX-XXXXXX') return Promise.reject(new Error('Complaint not found'))
      return Promise.reject(new Error(`unexpected GET ${url}`))
    })
    render(<CitizenPage currentUser={CURRENT_USER} areas={AREAS} users={[]} />)
    await submitCode(user, 'CFX-XXXXXX')

    // The submit and My-complaints Feedback components also render
    // empty role=alert regions, so scope to the tracking section.
    const error = await within(trackSection()).findByText('Complaint not found')
    expect(error.getAttribute('role')).toBe('alert')
    expect(error.id).toBe('ledger-track-code-error')
    const input = screen.getByLabelText(/tracking code/i)
    expect(input.getAttribute('aria-invalid')).toBe('true')
    expect(screen.queryByText('Complaint CFX-XXXXXX')).toBeNull()
  })

  it('submits from the keyboard and returns focus after Track another code', async () => {
    const user = userEvent.setup()
    render(<CitizenPage currentUser={CURRENT_USER} areas={AREAS} users={[]} />)
    const input = screen.getByLabelText(/tracking code/i)
    await user.click(input)
    await user.clear(input)
    await user.keyboard('CFX-102{Enter}')
    await waitFor(() => expect(screen.getByText('Complaint CFX-102')).toBeTruthy())

    // Tab order: the field is followed by the Track button.
    await user.click(screen.getByRole('button', { name: 'Track another code' }))
    expect(document.activeElement).toBe(input)
    expect(screen.queryByText('Complaint CFX-102')).toBeNull()
  })

  it('has no duplicate Ledger nav and keeps the track-card target', () => {
    render(<CitizenPage currentUser={CURRENT_USER} areas={AREAS} users={[]} />)
    expect(document.querySelector('.ledger-nav')).toBeNull()
    expect(screen.queryByLabelText('Citizen sections')).toBeNull()
    expect(document.getElementById('track-card')).toBeTruthy()
  })
})

describe('authenticated My complaints (full details preserved)', () => {
  it('still opens the full detail view with history', async () => {
    const user = userEvent.setup()
    routeMine([FULL_COMPLAINT])
    render(<CitizenPage currentUser={CURRENT_USER} areas={AREAS} users={[]} />)

    // The detail heading is "CFX-102 — Overflowing bins" in one
    // element, so match by substring.
    await waitFor(() => expect(screen.getByText(/Overflowing bins/)).toBeTruthy())
    const historyButton = screen.getByRole('button', { name: /view status history/i })
    expect(historyButton).toBeTruthy()
    await user.click(historyButton)
    await waitFor(() => expect(screen.getByText('Submitted → Under Review')).toBeTruthy())
    expect(get.mock.calls.map(([url]) => url)).toContain('/api/complaints/2/history')
  })
})

describe('submit a complaint (Civic Ledger)', () => {
  const CREATED = {
    id: 5, citizenId: 1, trackingCode: 'CFX-6JNQ86', title: 'Overflowing bins',
    description: 'Bins overflowing on our street', category: 'Garbage Collection',
    status: 'Submitted', priorityScore: 3, areaId: 1,
    aiSuggestedCategory: 'Garbage Collection', aiConfidence: 0.8,
    createdAt: '2026-11-11 08:04:03', updatedAt: '2026-11-11 08:04:03',
  }

  async function fillAndSubmit(user) {
    await user.type(screen.getByLabelText(/Title/), 'Overflowing bins')
    await user.type(screen.getByLabelText(/describe the problem/i), 'Bins overflowing on our street')
    await user.selectOptions(screen.getByLabelText(/Area/), '1')
    await user.click(screen.getByRole('button', { name: 'Submit complaint' }))
  }

  it('uses the Ledger theme and keeps every field, validation and map label', () => {
    render(<CitizenPage currentUser={CURRENT_USER} areas={AREAS} users={[]} />)
    const section = document.getElementById('submit-card')
    expect(section.className).toContain('civic-ledger')
    expect(within(section).getByText('Submit a complaint')).toBeTruthy()
    expect(screen.getByLabelText(/Title/)).toBeTruthy()
    expect(screen.getByLabelText(/describe the problem/i)).toBeTruthy()
    expect(screen.getByLabelText('Category')).toBeTruthy()
    expect(screen.getByLabelText(/how urgent is this/i)).toBeTruthy()
    expect(screen.getByLabelText(/Area/)).toBeTruthy()
    expect(screen.getByLabelText(/photo of the problem/i)).toBeTruthy()
    expect(screen.getByText(/where is it/i)).toBeTruthy()
    // Required validation is preserved on the same fields.
    expect(screen.getByLabelText(/Title/).required).toBe(true)
    expect(screen.getByLabelText(/describe the problem/i).required).toBe(true)
    expect(screen.getByLabelText(/Area/).required).toBe(true)
    // No duplicate nav was added with the theme.
    expect(document.querySelector('.ledger-nav')).toBeNull()
  })

  it('sends the same payload and shows a copyable code', async () => {
    const user = userEvent.setup()
    post.mockResolvedValue(CREATED)
    render(<CitizenPage currentUser={CURRENT_USER} areas={AREAS} users={[]} />)
    await fillAndSubmit(user)

    await waitFor(() => expect(screen.getByText('Complaint submitted')).toBeTruthy())
    expect(post).toHaveBeenCalledWith('/api/complaints', expect.objectContaining({
      citizenId: 1,
      title: 'Overflowing bins',
      description: 'Bins overflowing on our street',
      category: 'Garbage Collection',
      severity: 'normal',
      areaId: 1,
    }))
    const codeInput = screen.getByLabelText(/your tracking code/i)
    expect(codeInput.value).toBe('CFX-6JNQ86')
    expect(codeInput.readOnly).toBe(true)
    expect(screen.getByText(/Suggested category: Garbage Collection \(80% sure\)/)).toBeTruthy()
  })

  it('copies the code and tracks the new report from the success card', async () => {
    const user = userEvent.setup()
    const writeText = vi.fn().mockResolvedValue()
    // navigator.clipboard is getter-only in jsdom — define it.
    Object.defineProperty(navigator, 'clipboard', { value: { writeText }, configurable: true })
    post.mockResolvedValue(CREATED)
    get.mockImplementation((url) => {
      if (url === '/api/complaints') return Promise.resolve([])
      if (url === '/api/complaints/track/CFX-6JNQ86') {
        return Promise.resolve({ trackingCode: 'CFX-6JNQ86', status: 'Submitted', updatedAt: '2026-11-11 08:04:03' })
      }
      return Promise.reject(new Error(`unexpected GET ${url}`))
    })
    render(<CitizenPage currentUser={CURRENT_USER} areas={AREAS} users={[]} />)
    await fillAndSubmit(user)
    await waitFor(() => expect(screen.getByText('Complaint submitted')).toBeTruthy())

    await user.click(screen.getByRole('button', { name: 'Copy code' }))
    expect(writeText).toHaveBeenCalledWith('CFX-6JNQ86')
    await waitFor(() => expect(screen.getByRole('button', { name: 'Copied' })).toBeTruthy())

    await user.click(screen.getByRole('button', { name: 'Track this complaint' }))
    await waitFor(() => expect(screen.getByText('Complaint CFX-6JNQ86')).toBeTruthy())
    // The status is inside a <strong> in the summary line.
    expect(within(trackSection()).getByText('Submitted')).toBeTruthy()
    expect(get.mock.calls.map(([url]) => url)).toContain('/api/complaints/track/CFX-6JNQ86')
    expect(Element.prototype.scrollIntoView).toHaveBeenCalled()
  })

  it('keeps the form and shows the error when submit fails', async () => {
    const user = userEvent.setup()
    post.mockRejectedValue(new Error('Area is required'))
    render(<CitizenPage currentUser={CURRENT_USER} areas={AREAS} users={[]} />)
    await fillAndSubmit(user)

    await waitFor(() => expect(screen.getByText('Area is required')).toBeTruthy())
    expect(screen.queryByText('Complaint submitted')).toBeNull()
    expect(screen.getByLabelText(/Title/).value).toBe('Overflowing bins')
  })
})
