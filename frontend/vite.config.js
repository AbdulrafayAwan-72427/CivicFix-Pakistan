import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

// In `npm run dev`, requests to /api/... are proxied to the C++ server.
// This avoids browser CORS problems, because the C++ server (by design,
// in this phase) does not send CORS headers. See README.md for the
// production note: a deployed frontend needs either the same proxy or
// CORS headers on the C++ server.
export default defineConfig({
  plugins: [react()],
  server: {
    port: 5173,
    proxy: {
      '/api': {
        target: 'http://localhost:8080',
        changeOrigin: true,
      },
    },
  },
})
