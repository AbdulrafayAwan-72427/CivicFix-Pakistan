import { useEffect, useRef } from 'react'

// One place for every error/success message in the app.
// Errors are announced assertively and stay until the user acts;
// success messages announce politely and dismiss themselves after
// 5 seconds, so old banners never pile up next to new ones.
export default function Feedback({ error, message, onClearMessage }) {
  const clearRef = useRef(onClearMessage)
  clearRef.current = onClearMessage
  useEffect(() => {
    if (!message) return
    const t = setTimeout(() => { if (clearRef.current) clearRef.current() }, 5000)
    return () => clearTimeout(t)
  }, [message])
  return (
    <>
      <div role="alert" aria-live="assertive">{error && <p className="error">{error}</p>}</div>
      <div role="status" aria-live="polite">{message && <p className="success">{message}</p>}</div>
    </>
  )
}
