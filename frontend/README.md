# CivicFix Pakistan — React Frontend

Simple Vite + React app. It talks **only** to the C++ REST API
(see `../documentation/API.md`). It never talks to SQLite directly,
and never talks to any ML service — that is the C++ backend's job.

## Run it

1. Start the C++ API first, from the project root. On Windows:

   ```
   backend-cpp\civicfix_api.exe 8080 database\civicfix.db
   ```

   On Linux, use `backend-cpp/civicfix_api` instead of the `.exe`.
   Test it on a copy first if you do not want the real database changed:
   `copy database\civicfix.db database\api_test.db`, then pass
   `database\api_test.db` as the second argument.

2. In this folder:

   ```
   npm install
   npm run dev
   ```

   Open the URL Vite prints (usually http://localhost:5173).

3. To make a production build:

   ```
   npm run build
   npm run preview
   ```

## API base URL

Set `VITE_API_URL` to change where the app calls, for example:

```
VITE_API_URL=http://localhost:8080 npm run dev
```

If you do not set it:
- `npm run dev` calls `/api/...` on the same origin (port 5173),
  and Vite proxies those requests to `http://localhost:8080`
  (see `vite.config.js`).
- A production build defaults to `http://localhost:8080` directly.
  That works because the C++ API now sends CORS headers (see
  `../documentation/API.md`): a deployed frontend can call it
  without a proxy. For a public deployment, restrict the API's
  allowed origins with the `CIVICFIX_ALLOWED_ORIGINS` environment
  variable instead of leaving it open to any origin.

## Login and roles

The app has real login. The API stores salted, iterated password
hashes and issues a session token (24h expiry); the token is kept
in `localStorage` and sent as a Bearer header on every request.
If a login expires mid-session, the app returns to the login page.

Roles decide what you see — a Citizen sees Submit / Track /
My complaints, Staff sees the work queue, and an Admin sees the
Operations Center, Team and Admin actions. Other roles' pages are
not rendered, and the API enforces the same rules server-side.

Demo accounts (seeded in `database/civicfix.db` — change these
passwords before any real use):

- Admin: `admin@civicfix.pk` / `admin123`
- Citizen: `ali@example.com` / `ali123`
- Staff: `staff123` (password for staff accounts created in the app)

The login page shows the demo accounts only in development builds.
You can also create a new Citizen account from the login page.

## What is in the app

- **Citizen** — submit a complaint (title, description, category,
  area from `/api/areas`, how urgent it is in plain words, an
  optional photo and a map pin), track by tracking code, view
  details + status history, and list "my" complaints (the API
  filters these server-side to the logged-in citizen).
- **Admin** — Operations Center with stats, complaints list with
  search / area filter / sort / pagination, CSV export, charts,
  audit feed, and a detail drawer; staff assignment, admin
  actions, and undo of the latest action; a Team page to add
  staff members.
- **Staff** — the FIFO work queue from `/api/queue`, a "My
  assignments" view (`/api/me/assignments`), and active complaints.
- **Map** — Leaflet + OpenStreetMap, no API key. Citizens drop a
  pin when reporting; the admin map shows complaint pins. Map
  popups are built from DOM nodes with `textContent`, so a
  complaint title is always shown as plain text, never as HTML.
- **Photos and AI** — a photo (JPG/PNG, under 5 MB) can be attached
  to a complaint; the backend asks the ML service for a category
  suggestion and an image label, shown as a routing hint with its
  confidence. If the ML service is down, the complaint still saves.

## Status transitions (enforced by the API)

```
Submitted    -> Under Review, Rejected
Under Review -> Assigned, Rejected
Assigned     -> In Progress, Rejected
In Progress  -> Resolved, Rejected
Resolved / Rejected -> terminal
```

## Files

- `src/api.js` — fetch wrapper, API base URL, categories, statuses, transitions
- `src/App.jsx` — header, role-based navigation, login gating
- `src/pages/LoginPage.jsx`, `src/pages/CitizenPage.jsx`, `src/pages/AdminPage.jsx`, `src/pages/StaffPage.jsx`
- `src/components/ComplaintDetails.jsx` — complaint details + history
- `src/components/MapPicker.jsx` — Leaflet map picker and admin map pins
- `src/components/Feedback.jsx` — success/error messages
- `src/components/EmptyState.jsx` — empty-list states
- `src/utils.js` — shared helpers (including `hasCoords`)
