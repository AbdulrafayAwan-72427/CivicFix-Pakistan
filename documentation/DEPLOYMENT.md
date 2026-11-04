# CivicFix Pakistan — GitHub and Going Live

## 1. GitHub (the step for now)

The project is ready to push. The root `.gitignore` already excludes
built binaries (`.exe`), `frontend/node_modules/`, `frontend/dist/`,
Python virtual environments, test databases, and generated ML model
files (`ml-service/models/*.joblib` — a fresh clone regenerates them
with `python train.py`).

The real `database/civicfix.db` (seeded, 48 KB) **is** included, so a
clone works locally without rebuilding the database.

From the project root:

```
git init
git add .
git status          # check: no .exe, no node_modules, no test DBs
git commit -m "CivicFix Pakistan: C++ API, DSA viva package, frontend, ML service"
git branch -M main
git remote add origin https://github.com/<your-username>/CivicFix-Pakistan.git
git push -u origin main
```

Create the empty repo on github.com first, with no README and no
`.gitignore` there. Use a GitHub Personal Access Token as the
password if push asks for a login.

## 2. Local first — always test here before going live

See `documentation/LOCAL_TESTING.md`. The 3-terminal setup is:

| Service | Local URL |
|---|---|
| C++ API | http://localhost:8080 |
| React frontend (Vite dev) | http://localhost:5173 |
| ML service | http://127.0.0.1:8000 |

Do not skip this step. A deployed version that has never run locally
is much harder to debug.

## 3. Going live — what each part needs

There is no single "deploy" button for this stack, because the parts
have different needs. Do not try to put everything on a static host.

### Frontend (static — easiest)

The React app builds to static files:

```
cd frontend
npm run build        # output: frontend/dist/
```

Host `dist/` on Vercel, Netlify, or GitHub Pages. **But:** a static
build does not have the Vite dev proxy, so it will call the API
directly from the browser. That only works if one of these is true:

- The API sends CORS headers (it does not yet — this is a small
  change to `backend-cpp/src/server.cpp`: add
  `Access-Control-Allow-Origin` to responses, and handle OPTIONS),
  or
- The frontend is served behind the same proxy / domain as the API.

Until CORS is added, treat a static-only deployment as "frontend
demo, API still local" — not a fully live system.

### C++ API + SQLite (needs a real server)

The API is a long-running C++ program, not a static site. Options:

- **VPS** (e.g. a small Ubuntu server): build with g++ + sqlite3,
  run `civicfix_api 8080 database/civicfix.db` behind a process
  manager (systemd) and a reverse proxy (nginx) with HTTPS.
- **Render / Railway / Fly.io:** these can run a custom binary, but
  you will need a Dockerfile / build script, which is not in this
  repo yet. That is the next packaging step if you choose this route.

**SQLite warning:** on most cloud hosts, the local filesystem is
ephemeral — a redeploy can wipe `civicfix.db`. For a real live
service with real complaints, either use a persistent volume for
the database file, or migrate the data layer to PostgreSQL later.
For a university demo, a VPS with a persistent disk is the simplest
honest option.

### ML service (needs a Python server)

Run with uvicorn, on the same private network as the API if possible:

```
cd ml-service
pip install -r requirements.txt
python train.py
uvicorn app:app --host 127.0.0.1 --port 8000
```

Do not expose port 8000 publicly without authentication and rate
limiting — the service currently has none, and is bound to
`127.0.0.1` for local testing on purpose.

## 4. Before any public launch — checklist

- [ ] Password hashing + a `password_hash` column + real login
- [ ] Role checks on API admin endpoints (do not trust
      `changedByUserId` from the client alone)
- [ ] CORS on the C++ API, or a same-origin proxy
- [ ] C++ API → ML service call wired in (React still must not call
      ML directly)
- [ ] Persistent storage for the database (volume or PostgreSQL)
- [ ] HTTPS on every public URL
- [ ] ML model re-evaluated on real, unseen complaints (the current
      0.972 test score is on template-generated demo data)
- [ ] Secrets (if any are added later) in environment variables,
      never committed to Git
