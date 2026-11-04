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
  (see `vite.config.js`). This is deliberate: the C++ server does
  not send CORS headers, so a browser page on port 5173 calling
  `http://localhost:8080` directly would be blocked by the browser.
- A production build defaults to `http://localhost:8080` directly.
  For a real deployment, either serve the frontend behind the same
  proxy, or add CORS headers (`Access-Control-Allow-Origin`) to the
  C++ server — that backend change is intentionally not in this
  frontend-only task.

## No login yet — please read this

The C++ API has no authentication (the `users` table has no password
column), so this app does **not** fake a login. The header has a demo
user selector: pick an existing user (seeded defaults are citizen
ID 1, Ali Khan, and admin ID 2, CivicFix Admin). That user's ID is
sent as `citizenId` / `changedByUserId` / `assignedByAdminId`.
Anyone could pick any user, so do not treat this as security. Real
login needs password hashing and tokens on the backend first.

## What is in the app

- **Citizen tab** — submit a complaint (title, description, category,
  priority 1–5, area from `/api/areas`), track by tracking code
  (try `CFX-102`), view details + status history, and list "my"
  complaints (filtered in the browser, because the API has no
  per-citizen endpoint yet).
- **Admin tab** — dashboard from `/api/stats`, complaints list with
  status/category filters and `?sort=priority`, status changes
  (only the transitions the API allows are offered; a bad transition
  shows the API's own 400 error message), staff assignment,
  admin actions, and undo of the latest action.
- **Staff tab** — the FIFO work queue from `/api/queue` plus all
  active complaints. There is no per-staff assignment endpoint in
  the API yet, so this page cannot show "only my assignments".

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
- `src/App.jsx` — header, demo user selector, tab navigation
- `src/pages/CitizenPage.jsx`, `src/pages/AdminPage.jsx`, `src/pages/StaffPage.jsx`
- `src/components/ComplaintDetails.jsx` — complaint details + history
