// One consistent empty state for every list: an icon, one-line
// title, one-line hint. Never a bare sentence, and never shown
// while the list is still loading — callers render this only after
// their request has finished.
export default function EmptyState({ icon = '📭', title, hint }) {
  return (
    <div className="empty-state">
      <div className="empty-icon" aria-hidden="true">{icon}</div>
      <strong>{title}</strong>
      {hint && <p className="small">{hint}</p>}
    </div>
  )
}
