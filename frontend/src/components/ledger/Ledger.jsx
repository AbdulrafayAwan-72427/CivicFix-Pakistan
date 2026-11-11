import React, { useId } from 'react'
import './ledger.css'

export function LedgerButton({ variant = 'primary', type = 'button', busy = false, busyLabel, disabled, children, ...props }) {
  return <button {...props} className="ledger-button" data-variant={variant} type={type}
    disabled={disabled || busy} aria-busy={busy || undefined}>{busy ? (busyLabel ?? children) : children}</button>
}

export function LedgerField({ label, hint, error, id, multiline = false, required, ...props }) {
  const generatedId = useId()
  const fieldId = id ?? generatedId
  const Control = multiline ? 'textarea' : 'input'
  const description = [props['aria-describedby'], hint && fieldId + '-hint', error && fieldId + '-error'].filter(Boolean).join(' ')
  return <div className="ledger-field">
    <label htmlFor={fieldId}>{label}{required && ' (required)'}</label>
    {hint && <p id={fieldId + '-hint'} className="ledger-note">{hint}</p>}
    <Control {...props} id={fieldId} className="ledger-control" required={required}
      aria-invalid={error ? true : props['aria-invalid']} aria-describedby={description || undefined} />
    {error && <p role="alert" id={fieldId + '-error'} className="ledger-error">{error}</p>}
  </div>
}

export function LedgerNav({ label, items, current, onSelect }) {
  return <nav aria-label={label} className="ledger-nav">
    {items.map(({ id, label: itemLabel }) => <button key={id} type="button" className="ledger-nav-item"
      aria-current={current === id ? 'page' : undefined} onClick={() => onSelect(id)}>{itemLabel}</button>)}
  </nav>
}

export function LedgerCard({ title, children, headingLevel = 2 }) {
  const titleId = useId()
  const Heading = 'h' + headingLevel
  return <section className="ledger-card" aria-labelledby={titleId}>
    <Heading className="ledger-title" id={titleId}>{title}</Heading>
    {children}
  </section>
}
