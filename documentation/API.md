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
  stored as salted SHA-256 (`<salt>$<sha256(salt+password)>`, new
  `password_hash` column added by migration in
  `Database::connect`); the API never returns the hash. Every later
  request sends `Authorization: Bearer <token>`; the server looks
  the token up in the `sessions` table (24-hour expiry) and checks
  the stored role. `GET /api/auth/me` returns the logged-in user;
  `POST /api/auth/logout` deletes the session. Seeded users get
  demo passwords on first run after the upgrade: Admin
  `admin@civicfix.pk` / `admin123`, Citizen `ali@example.com` /
  `ali123`, Staff `staff123` — change them for real use.
  `POST /api/users` (Admin) now needs a `password` (min 6 chars)
  for the new staff member. Missing/expired token → 401, wrong
  role or mismatched actor → 403. Honest limit: tokens are random
  strings in SQLite (not signed JWTs) and the hash is salted
  SHA-256 (not bcrypt/argon2) — real login for this project, but a
  production system would use bcrypt/argon2 + signed tokens + HTTPS.
- **Role-based access control (RBAC) — same rules as before, now
  enforced on the login token instead of a client-sent user id.**
  `POST /api/users`, status changes, assignments and undo are Admin
  only; `POST /api/complaints` and `POST /api/complaints/{id}/photo`
  are Citizen only, and the `citizenId` / `changedByUserId` /
  `assignedByAdminId` in the body must match the logged-in user.
  `GET /api/users` now needs a login; tracking a complaint by code
  stays public by design.

## Map, photos and AI (added 5 Nov 2026)

- **Location:** complaints now carry `latitude` / `longitude`
  (new DB columns, added by migration in `Database::connect`, so
  old databases keep their data). `POST /api/complaints` accepts
  them; out of range → 400. `0,0` means "no location picked".
  The frontend has a Leaflet/OpenStreetMap picker (click, or "Use
  my location"), and the Admin page has a map with a pin per
  located complaint — click a pin to open it.
- **Photos:** `POST /api/complaints` accepts `photoBase64` +
  `photoExt` (`jpg`/`png`, under 5 MB). The server saves the file
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
| GET | `/api/health` | Health check |
| GET | `/api/areas` | List areas |
| GET | `/api/users` | List users |
| POST | `/api/users` | Register a user (409 if email exists) |
| GET | `/api/complaints` | List complaints; `?status=`, `?category=`, `?sort=priority` |
| POST | `/api/complaints` | Submit a complaint; tracking code is auto-generated (CFX-104, …) |
| GET | `/api/complaints/{id}` | One complaint |
| GET | `/api/complaints/track/{code}` | Track by code, e.g. CFX-102 |
| GET | `/api/complaints/{id}/history` | Permanent status history |
| PATCH or PUT | `/api/complaints/{id}/status` | Change status (validated transition) |
| POST | `/api/assignments` | Assign a Staff user to a complaint |
| GET | `/api/admin/actions` | All admin actions, newest first |
| GET | `/api/admin/actions/latest` | Latest pending (not undone) action |
| POST | `/api/admin/undo` | Undo the latest pending admin action |
| GET | `/api/stats` | Dashboard counts: total, resolved, pending, by status, by category |
| GET | `/api/queue` | Active complaints in FIFO order, via the custom ComplaintQueue |

### Status transitions enforced by the API

```
Submitted    -> Under Review, Rejected
Under Review -> Assigned, Rejected
Assigned     -> In Progress, Rejected
In Progress  -> Resolved, Rejected
Resolved / Rejected -> (terminal, no further change)
```

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
