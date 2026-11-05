# CivicFix Pakistan

A complete municipal complaint management system for Pakistani cities.
Citizens report civic problems — garbage, broken roads, water shortages,
blocked drains, streetlights — with a photo and a map pin, and track
the report from submission to resolution. City staff work through the
reports in order, and admins manage the whole operation from one
dashboard.

## How it works

1. **A citizen reports a problem** — title, description, category,
   area, a photo of the problem, and a pin dropped on the map.
2. **The system helps route it** — it suggests a category from the
   description and the photo, so the report lands with the right team.
   A human always makes the final call.
3. **An admin assigns it** to a staff member and updates its status
   as work progresses.
4. **The citizen tracks it** with a tracking code (like `CFX-102`),
   watching it move: Submitted → Under Review → Assigned →
   In Progress → Resolved. Every change is recorded in a permanent
   audit trail, and a wrong status change can be undone.

## Features

**For citizens**
- Submit a report with a photo and a map location
- Track any report by its code, with a step-by-step progress view
- See all your own reports, plus the full history of each one

**For staff**
- A work queue in first-come-first-served order
- All active complaints with their photo, location, and history

**For admins**
- Operations dashboard: totals, resolution rate, charts by status,
  category, and area
- Search, filter, and sort complaints; export them as CSV
- A live map with a pin for every located complaint
- Assign complaints to staff, change statuses, and undo mistakes
- Manage the team: add staff members who can then log in
- A full audit feed of every action taken

**Accounts and security**
- Real login with email and password; passwords are stored salted
  and hashed, never in plain text
- Login tokens expire after 24 hours; logging out ends the session
- Each role sees only its own tools — citizens can't reach admin
  pages, and staff can't manage users

## Tech stack

| Part | Technology |
| --- | --- |
| Backend API | C++17, zero-dependency REST API (standard library + sockets) |
| Database | SQLite |
| Frontend | React 18 + Vite, Leaflet maps (OpenStreetMap) |
| Smart routing | Python + FastAPI service that suggests a category from the complaint text and photo |

The React app talks only to the C++ API; the API owns the database
and calls the Python service. The frontend never touches the database
directly.

## Project structure

```
CIvicFix-Pakistan/
├── backend-cpp/
│   ├── Include/              # Models and the database layer
│   ├── src/server.cpp        # REST API server
│   ├── src/Database.cpp      # SQLite access, migrations
│   └── src/main.cpp          # Console demo
├── frontend/                 # Vite + React 18 (Citizen / Staff / Admin)
├── ml-service/               # FastAPI service: text + photo categorisation
│   ├── app.py  train.py  requirements.txt
│   └── datasets/complaints.csv
├── database/
│   ├── civicfix.db           # Seeded: complaints CFX-101/102/103, 2 users, 5 areas
│   └── schema.sql
├── documentation/
│   ├── API.md                # API endpoints + examples
│   ├── LOCAL_TESTING.md      # Local testing guide
│   └── DEPLOYMENT.md         # Going-live guide
└── .gitignore
```

## Run it locally (3 terminals)

Full steps, a test checklist, and troubleshooting are in
`documentation/LOCAL_TESTING.md`. Short version, from the project
root, testing on a **copy** of the database:

**Terminal 1 — C++ API (port 8080):**
```
copy database\civicfix.db database\api_test.db
backend-cpp\civicfix_api.exe 8080 database\api_test.db
```
If you changed the C++ code, rebuild it first in VS Code:
`Ctrl+Shift+P` → "Tasks: Run Task" → "Build CivicFix API".
Stop the running API before rebuilding — Windows locks a running
`.exe` file, so the build can't replace it while it's running.

**Terminal 2 — React frontend (port 5173):**
```
cd frontend
npm install
npm run dev
```
Open http://localhost:5173. The dev server proxies `/api` to
http://localhost:8080, so the API must be running on 8080.

**Terminal 3 — Smart routing service (port 8000):**
```
cd ml-service
python -m venv .venv
.venv\Scripts\pip install -r requirements.txt
python train.py
uvicorn app:app --port 8000
```
The API still works if this service is down — new complaints just
save without a suggested category until it is back.

**Demo logins** (seeded automatically on first run — change them
for real use):
- Admin: `admin@civicfix.pk` / `admin123`
- Citizen: `ali@example.com` / `ali123`
- Staff: created by an admin, password `staff123` for the demo staff

## API

See `documentation/API.md` for the full endpoint list. Highlights:

- `GET /api/health`, `/api/areas`, `/api/complaints`, `/api/stats`
- `POST /api/auth/register`, `/api/auth/login`, `/api/auth/logout`,
  `GET /api/auth/me`
- `POST /api/complaints` (tracking code auto-generated, photo and
  map pin included), track by `GET /api/complaints/track/CFX-102`
- `PATCH /api/complaints/{id}/status` with validated transitions
  (Submitted → Under Review → Assigned → In Progress → Resolved)
- `POST /api/assignments`, `GET /api/queue`,
  `POST /api/admin/undo` — the last admin action can be undone

## Current status

Working today: login, report submission with photo and map pin,
tracking, staff queue, admin dashboard and map, assignments, status
changes with undo, audit trail, and category suggestions from text
and photos.

Still to do before public use: change the demo passwords, add
CORS headers (or keep the Vite proxy) for a deployed frontend, and
a hosted deployment — see `documentation/DEPLOYMENT.md`. The photo
categorisation is a general-purpose image classifier used as a
routing hint, not a model trained on civic photos, so low-confidence
results are flagged for a human to check.
