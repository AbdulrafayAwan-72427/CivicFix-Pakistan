# CivicFix Pakistan — C++ REST API

**Server file:** `backend-cpp/src/server.cpp`
**Status:** Implemented and tested with curl on a copy of the database
(4 November 2026). See the test summary at the bottom.

## Why this server is dependency-free

The roadmap planned cpp-httplib + nlohmann/json. Those libraries are
not installed on this machine and are not vendored in the project, so
this server uses only the C++ standard library and the OS socket API
(Winsock on Windows, POSIX sockets on Linux), plus the project's own
`Database` class and custom DSA classes.

The HTTP/JSON contract is the same contract a httplib/json version
would expose, so a React frontend written against this API does not
need to change if the server is later swapped for httplib.

Trade-offs to know for the viva:
- The server handles **one request at a time** (single-threaded).
  That keeps the single SQLite connection safe, and is fine for a
  demo and local React development. It is not a production
  multi-threaded server.
- JSON parsing supports **flat objects** (string / integer fields).
  That covers every endpoint below. It is not a general JSON parser.
- **Real login (token auth) — added 5 Nov 2026, replaces the old
  demo `X-User-Id` header.** `POST /api/auth/register` (Citizen
  only, self-registration) and `POST /api/auth/login` return
  `{"user": {...}, "token": "<64 hex chars>"}`. Passwords are
  stored as iterated salted SHA-256 — 10,000 rounds, format
  `iter10000$<salt>$<hash>` (new `password_hash` column added by
  migration in `Database::connect`); the API never returns the
  hash. Old single-round hashes (`<salt>$<sha256(salt+password)>`)
  are still accepted, and a successful login upgrades them to the
  iterated format automatically. Every later request sends
  `Authorization: Bearer <token>`; the server looks the token up
  in the `sessions` table (24-hour expiry) and checks the stored
  role. `GET /api/auth/me` returns the logged-in user;
  `POST /api/auth/logout` deletes the session. Seeded users get
  demo passwords on first run after the upgrade: Admin
  `admin@civicfix.pk` / `admin123`, Citizen `ali@example.com` /
  `ali123`, Staff `staff123` — change them for real use.
  `POST /api/users` (Admin) now needs a `password` (min 6 chars)
  for the new staff member. Missing/expired token → 401, wrong
  role or mismatched actor → 403. **Login rate limiting
  (8 Nov 2026):** 5 failed logins for one email within 15 minutes
  locks that email out until the window passes (429 "Too many
  failed login attempts"); a successful login clears the counter.
  Honest limit: tokens are random strings in SQLite (not signed
  JWTs) and the hash is iterated SHA-256 (not bcrypt/argon2) —
  real login for this project, but a production system would use
  bcrypt/argon2 + signed tokens + HTTPS.
- **Role-based access control (RBAC) — same rules as before, now
  enforced on the login token instead of a client-sent user id.**
  `POST /api/users`, status changes, assignments and undo are Admin
  only; `POST /api/complaints` and `POST /api/complaints/{id}/photo`
  are Citizen only, and the `citizenId` / `changedByUserId` /
  `assignedByAdminId` in the body must match the logged-in user.
  **Reads are locked down too (8 Nov 2026):** `GET /api/complaints`,
  `GET /api/complaints/{id}` and `GET /api/complaints/{id}/history`
  need a login, and a Citizen only ever sees their own complaints
  (Staff and Admin see all); `GET /api/queue` is Staff or Admin;
  `GET /api/stats` and `GET /api/admin/actions` are Admin only.
  `GET /api/users` needs a login, and only Admins get full records
  (email, phone) — Citizens and Staff get id, name and role only,
  which is all the frontend needs to show "Submitted by …".
  `GET /api/me/assignments` (8 Nov 2026) is Staff only and returns
  the complaints currently assigned to the logged-in staff member,
  in assignment order — the Staff page's "My assignments" view.
  **Operational lifecycle (11 Nov 2026):** rejecting a complaint
  requires a reason (`notes` in the status-change body — 400
  without one), stored on the complaint as `rejectionReason` and
  shown to the citizen; reopening a closed complaint clears the
  reason, the duplicate link and the resolution note.
  `POST /api/complaints/{id}/duplicate` and `POST
  /api/complaints/{id}/escalate` are Admin only; `GET`/`POST
  /api/complaints/{id}/notes` follows the same visibility as the
  complaint itself (Citizen own only, Staff and Admin all), and
  `GET /api/me/notifications` plus `POST
  /api/me/notifications/{id}/read` are per-user — one user cannot
  read or mark another user's notifications (404). Assignments
  are recorded in the complaint timeline as same-status history
  events ("Assigned to …" / "Reassigned from … to …") and the
  assignment reply reports `previousStaffId` and `reassigned`.
  Every lifecycle change and the notification it creates are
  written in one transaction, so a failure leaves no half-applied
  change behind.
  **CORS (8 Nov 2026, allowlist added Nov 2026):** every response
  carries CORS headers and preflight `OPTIONS` requests are
  answered with 204 — so a frontend served from any origin can
  call the API directly without a proxy. Which origins are allowed
  comes from the `CIVICFIX_ALLOWED_ORIGINS` environment variable
  (comma-separated, e.g.
  `CIVICFIX_ALLOWED_ORIGINS=https://civicfix.example.com,https://app.example.com`).
  Unset or empty means `*` (any origin) — the local-development
  default. The frontend uses Bearer tokens, not cookies, so a
  wildcard origin is safe for this setup, but a public deployment
  should set the variable to the real frontend origin(s) only: a
  browser from any other origin then gets no
  `Access-Control-Allow-Origin` header and its requests are
  refused. Allowed origins are echoed back exactly (with
  `Vary: Origin`); disallowed origins get no CORS headers at all.
  The API prints the allowed origins when it starts.
  **Urgency instead of self-chosen priority (8 Nov 2026):**
  `POST /api/complaints` accepts `severity` — "low" → priority 2,
  "normal" → 3, "high" → 5. A valid severity wins over any
  `priorityScore` in the same body; an invalid value returns 400.
  Without `severity`, `priorityScore` (1–5, default 3) works as
  before.
  Tracking a complaint by code (`GET /api/complaints/track/{code}`)
  stays public by design, and `GET /api/areas` and `/api/health`
  stay public too. **Tracking codes are random now** (e.g.
  `CFX-7K3M9P`, 6 characters, no 0/O/1/I) instead of sequential
  `CFX-101, 102, …`, so they cannot be counted through on the
  public tracking endpoint. Old codes (`CFX-101/102/103`) keep
  working as before. **Public tracking is minimal (9 Nov 2026):**
  it returns only `trackingCode`, `status` and `updatedAt` — no
  citizen id, description, coordinates or photo path. Logged-in
  views still return the full complaint.
  **Readiness (9 Nov 2026):** `GET /api/health` is process-only
  (the server is up). `GET /api/ready` is the database check:
  200 `{"status":"ready"}` when all core tables exist, 503
  `{"status":"not_ready"}` otherwise. A fresh database path now
  creates all core tables and seeds the five Lahore areas on
  first start, so registration works on an empty database.
  **Pending count (9 Nov 2026):** `GET /api/stats` counts
  `pendingComplaints` as active complaints only — not Resolved
  and not Rejected — the same rule the work queue uses.

## Map, photos and AI (added 5 Nov 2026)

- **Location:** complaints now carry `latitude` / `longitude`
  (new DB columns, added by migration in `Database::connect`, so
  old databases keep their data). `POST /api/complaints` accepts
  them; out of range → 400. `0,0` means "no location picked".
  The frontend has a Leaflet/OpenStreetMap picker (click, or "Use
  my location"), and the Admin page has a map with a pin per
  located complaint — click a pin to open it.
- **Photos:** `POST /api/complaints` accepts `photoBase64` +
  `photoExt` (`jpg`/`png`, under 5 MB). The decoded bytes must
  also start with the right magic bytes (JPEG `FF D8 FF`, PNG
  `89 50 4E 47`) — an extension alone does not prove a file is a
  photo (9 Nov 2026). The server saves the file
  under `uploads/<trackingCode>.<ext>` (gitignored) and stores
  that path in `photo_path`. `GET /uploads/<file>` serves it back
  (the Vite proxy forwards `/uploads` to the API). Existing
  complaints can gain a photo via `POST /api/complaints/{id}/photo`
  (Citizen only, own complaint only). Wrong extension → 400.
- **AI (C++ → ML, never React → ML):** after a complaint is
  saved, the server asks the ML service at `127.0.0.1:8000` —
  `POST /predict` with title + description, and `POST
  /detect-image` with the photo if there is one. The results are
  stored in `ai_category`, `ai_confidence`, `ai_image_label` and
  returned in every complaint JSON (`aiSuggestedCategory`,
  `aiConfidence`, `aiImageLabel`). If the ML service is down, the
  complaint is still saved and the AI fields stay empty — the
  frontend says so instead of inventing a prediction. The image
  model is MobileNetV2 (general ImageNet classifier, not trained
  on civic photos), so its mapping is a routing hint and
  low-confidence results are flagged for human review.
- All timestamps written by the API are UTC, matching SQLite's
  `CURRENT_TIMESTAMP` used by `updateComplaintStatus`.

## Build and run

### Windows (MSYS2 UCRT64, VS Code task "Build CivicFix API")

```
g++ -std=c++17 -Wall -Wextra -g -I backend-cpp/Include ^
  backend-cpp/src/server.cpp backend-cpp/src/models.cpp ^
  backend-cpp/src/ComplaintQueue.cpp backend-cpp/src/ActionStack.cpp ^
  backend-cpp/src/StatusHistory.cpp backend-cpp/src/Graph.cpp ^
  backend-cpp/src/Sorting.cpp backend-cpp/src/Database.cpp ^
  -o backend-cpp/civicfix_api.exe -lsqlite3 -lws2_32
```

Or press Ctrl+Shift+B and choose **Build CivicFix API**.

Then, from the project root:

```
backend-cpp\civicfix_api.exe 8080 database\civicfix.db
```

### Linux

```
g++ -std=c++17 -Wall -Wextra -I backend-cpp/Include \
  backend-cpp/src/server.cpp backend-cpp/src/models.cpp \
  backend-cpp/src/ComplaintQueue.cpp backend-cpp/src/ActionStack.cpp \
  backend-cpp/src/StatusHistory.cpp backend-cpp/src/Graph.cpp \
  backend-cpp/src/Sorting.cpp backend-cpp/src/Database.cpp \
  -o backend-cpp/civicfix_api -lsqlite3 -pthread
./backend-cpp/civicfix_api 8080 database/civicfix.db
```

With no arguments it uses port 8080 and `database/civicfix.db`.
Test it on a **copy** first if you do not want the real database changed:

```
copy database\civicfix.db database\api_test.db
backend-cpp\civicfix_api.exe 8080 database\api_test.db
```

## Endpoints

Base URL: `http://localhost:8080`

| Method | Endpoint | Purpose |
|---|---|---|
| GET | `/api/health` | Health check, process only (public) |
| GET | `/api/ready` | Database readiness: 200 ready, 503 not ready (public) |
| GET | `/api/areas` | List areas (public) |
| GET | `/api/users` | List users (login; Admin sees full records, others id/name/role only) |
| POST | `/api/users` | Register a user, Admin only (409 if email exists) |
| GET | `/api/complaints` | List complaints; `?status=`, `?category=`, `?sort=priority` (login; Citizen sees only their own) |
| POST | `/api/complaints` | Submit a complaint, Citizen only; tracking code is auto-generated and random (e.g. CFX-7K3M9P) |
| GET | `/api/complaints/{id}` | One complaint (login; Citizen own only) |
| GET | `/api/complaints/track/{code}` | Track by code, e.g. CFX-102 (public; returns trackingCode, status, updatedAt only) |
| GET | `/api/complaints/{id}/history` | Permanent status history (login; Citizen own only) |
| PATCH or PUT | `/api/complaints/{id}/status` | Change status, Admin only (validated transition; includes reopen, see below; rejecting requires a reason in `notes`) |
| POST | `/api/complaints/{id}/duplicate` | Mark as a duplicate of another report by tracking code, Admin only; closes as Resolved, linked to the original |
| POST | `/api/complaints/{id}/escalate` | Escalate to priority 5 with a `reason`, Admin only; notifies the assigned staff member |
| GET | `/api/complaints/{id}/notes` | Progress notes on a complaint (same visibility as the complaint) |
| POST | `/api/complaints/{id}/notes` | Add a progress note / citizen follow-up `{"body": "..."}` (max 2000 chars; same visibility as the complaint) |
| GET | `/api/me/notifications` | The logged-in user's in-app notifications, newest first |
| POST | `/api/me/notifications/{id}/read` | Mark one of your own notifications as read (another user's id → 404) |
| POST | `/api/users/{id}/disable` | Disable a user and delete all their sessions (Admin only; cannot disable yourself) |
| POST | `/api/users/{id}/enable` | Re-enable a disabled user (Admin only) |
| POST | `/api/users/{id}/reset-password` | Set a new password `{"password": "..."}` (min 6 chars) and delete all their sessions (Admin only; no self-service reset) |
| POST | `/api/assignments` | Assign a Staff user to a complaint, Admin only; reassigning closes the previous assignment, records it in the timeline, and returns `previousStaffId` + `reassigned` |
| GET | `/api/admin/actions` | All admin actions, newest first (Admin only) |
| GET | `/api/admin/actions/latest` | Latest pending (not undone) action (Admin only) |
| POST | `/api/admin/undo` | Undo the latest pending admin action, Admin only |
| GET | `/api/stats` | Dashboard counts: total, resolved, pending, by status, by category (Admin only) |
| GET | `/api/queue` | Active complaints in FIFO order, via the custom ComplaintQueue (Staff or Admin) |
| GET | `/api/me/assignments` | Complaints assigned to the logged-in staff member, in assignment order (Staff only) |

### Status transitions enforced by the API

```
Submitted    -> Under Review, Rejected
Under Review -> Assigned, Rejected
Assigned     -> In Progress, Rejected
In Progress  -> Resolved, Rejected
Resolved     -> Under Review (reopen, Admin only, 9 Nov 2026)
Rejected     -> Under Review (reopen, Admin only, 9 Nov 2026)
```

Reopening sends a closed complaint back to triage (Under Review,
not Submitted). It is recorded in `status_history` and
`admin_actions` like any other status change, so undo restores the
closed status. Staff and Citizens cannot reopen (403 — the status
endpoint is Admin only).

### Rejection reasons (11 Nov 2026)

Rejecting without a reason returns 400. The reason travels in
`notes` on the status change and is stored on the complaint as
`rejectionReason` (also in the history row), so the citizen sees
why their report was rejected. Reopening clears it, along with
`duplicateOf` and `resolutionNote`.

### Duplicates (11 Nov 2026)

`POST /api/complaints/{id}/duplicate` with
`{"duplicateOfCode": "CFX-...", "changedByUserId": 2}` closes the
report as Resolved, sets `duplicateOf` to the original's tracking
code and `resolutionNote` to "Duplicate of CFX-…". Rules: the
original must exist (404), a report cannot duplicate itself (400),
the original cannot itself be a duplicate (no chains, 400), and
an already-closed report cannot be marked a duplicate (400). The
citizen is notified. The duplicate no longer counts as pending
(it is Resolved) and is excluded from the work queue.

### Escalation (11 Nov 2026)

`POST /api/complaints/{id}/escalate` with `{"reason": "...",
"changedByUserId": 2}` sets `priorityScore` to 5 and `escalated`
to true, records an "Escalated: …" timeline event, and notifies
the assigned staff member (if any). An already-escalated or
closed complaint returns 400; a missing reason returns 400.

### Progress notes (11 Nov 2026)

`GET` / `POST /api/complaints/{id}/notes` is the progress thread:
staff post field updates, citizens post follow-ups, admins post
either. Visibility follows the complaint (Citizen own only). A
note is at most 2000 characters. Posting a note notifies whoever
needs to act on it: a citizen's follow-up notifies the assigned
staff member (or all Admins when nobody is assigned yet); a
staff/admin update notifies the citizen.

### Notifications (11 Nov 2026)

Notifications are **in-app only**. No email or SMS provider is
configured, so nothing is faked as sent outside the app. A
notification is created for: a status change (to the citizen), an
assignment or reassignment (to the new staff member and the
citizen), a duplicate marking (to the citizen), an escalation (to
the assigned staff member), and a new note (routed as above). Each
one is written in the same transaction as the event that caused
it. `GET /api/me/notifications` lists the logged-in user's own
notifications, newest first (latest 50); `POST
/api/me/notifications/{id}/read` marks one read.

An invalid transition returns `400` and changes nothing. Persistent
undo bypasses this on purpose: undo restores the previous status even
though that direction is not a forward transition.

### Example requests

```bash
curl http://localhost:8080/api/health

curl http://localhost:8080/api/complaints/track/CFX-102

curl -X POST http://localhost:8080/api/complaints \
  -H "Content-Type: application/json" \
  -d '{"citizenId":1,"title":"Streetlight not working",
       "description":"Streetlight out on main road",
       "category":"Streetlight","priorityScore":2,"areaId":1}'

curl -X PATCH http://localhost:8080/api/complaints/4/status \
  -H "Content-Type: application/json" \
  -d '{"newStatus":"Under Review","changedByUserId":2,
       "notes":"Reviewed via API"}'

curl -X POST http://localhost:8080/api/admin/undo \
  -H "Content-Type: application/json" \
  -d '{"changedByUserId":2,"notes":"Undo via API"}'

curl "http://localhost:8080/api/complaints?sort=priority"
```

`?sort=priority` uses the project's own stable Counting Sort
(priority 5 first). `/api/queue` uses the project's own linked-list
ComplaintQueue. The DSA classes are therefore part of the real API
workflow, not only the console demo.

## New Database methods added for the API

In `backend-cpp/Include/services/Database.hpp` /
`backend-cpp/src/Database.cpp`:

- `getComplaintById`, `getComplaintByTrackingCode`
- `getAllAreas`, `getAllUsers`
- `assignComplaint` (inserts into `assignments`; staff and complaint
  must exist, enforced by foreign keys)
- `getAllAdminActions` (newest first, including `is_undone`)

`AdminAction` gained an `isUndone` field (default `false`), so the
in-memory stack code still compiles unchanged.

## Test summary (4 November 2026, test database copy)

All of these were verified with curl against a copy of the uploaded
database; the delivered `database/civicfix.db` was not used for the
API test:

**Phase 3 (11 Nov 2026, `workspace/civicfix/
handoff2_phase3_test.sh`):** 59/59 pass on a seed-DB copy —
end-to-end trace (submit → triage → assign → progress notes →
resolve), rejection requires a reason and reopening clears it,
duplicate marking with all four failure rules, reassignment with
previous-staff reporting and queue updates, escalation to P5 with
notifications, notes role boundaries, notification inbox
read/ownership rules, and history unchanged after failed
transitions. The 47-check security matrix (`sec_test.sh`) and the
phase 2 suite (31/31) were re-run against the same build and still
pass; the original database is unchanged (md5 verified).

- Health, areas, complaints list, and tracking CFX-102: correct JSON
- New Staff user created (id 3), new complaint created as CFX-104
- Invalid transition Submitted → Resolved: `400`, unchanged
- Valid transitions Submitted → Under Review → Assigned: correct,
  with permanent history rows
- Assignment to Staff user id 3: accepted
- Stats: 4 complaints, by status and by category, correct
- `?sort=priority`: 5, 4, 3, 2 — Counting Sort order correct
- Queue: CFX-101, 102, 103, 104 in FIFO order
- Latest admin action, then undo via API: CFX-104 returned to
  Under Review, as expected
- Duplicate user registration: `409`
- Unknown complaint id: `404`

## Next steps after the API

1. React frontend against these endpoints (no direct SQLite or ML calls).
2. ML service (FastAPI) called by the C++ backend, not by React.
3. Real login is DONE (5 Nov 2026): salted SHA-256 passwords,
   token sessions, `POST /api/auth/login|register|logout` and
   `GET /api/auth/me`; role checks run on the token's user.
