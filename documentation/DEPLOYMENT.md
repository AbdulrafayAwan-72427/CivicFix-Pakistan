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
git commit -m "CivicFix Pakistan: C++ API, React frontend, ML service"
git branch -M main
git remote add origin https://github.com/AbdulrafayAwan-72427/CivicFix-Pakistan.git
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

Host `dist/` on Vercel, Netlify, or GitHub Pages. A static build
does not have the Vite dev proxy, so it will call the API directly
from the browser. That works because the API sends CORS headers
(see `documentation/API.md`). By default it allows any origin,
which is fine for local development; for a public deployment, set
the `CIVICFIX_ALLOWED_ORIGINS` environment variable on the machine
running the API to a comma-separated list of the real frontend
origin(s), for example:

```
CIVICFIX_ALLOWED_ORIGINS=https://civicfix.example.com
```

A browser from any other origin then gets no CORS header and its
requests are refused. The API prints the allowed origins when it
starts, so you can check the setting took effect.

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

Done already (verified on 8 Nov 2026):

- [x] Password hashing (salted SHA-256, 10,000 rounds) + a
      `password_hash` column + real login with session tokens
- [x] Role checks on API endpoints, including read endpoints —
      citizens only ever see their own complaints
- [x] CORS on the C++ API, with an origin allowlist via
      `CIVICFIX_ALLOWED_ORIGINS` (see section 3 above)
- [x] C++ API → ML service call wired in (React still must not call
      ML directly)

Still to do:

- [ ] Rebuild the API from the current source and restart it — do
      not keep running an older `civicfix_api.exe` from before the
      security fixes (check every protected endpoint returns 401
      without a token after each rebuild, not just `/api/health`)
- [ ] Change or disable the seeded demo passwords
      (`admin@civicfix.pk` / `admin123`, `ali@example.com` /
      `ali123`); never deploy with public default passwords, and
      do not publish real user data in a seed database
- [ ] Set `CIVICFIX_ALLOWED_ORIGINS` to the real frontend origin(s)
- [ ] Persistent storage for the database (volume or PostgreSQL)
- [ ] HTTPS on every public URL
- [ ] Dockerfile / build script for the API and ML service (needed
      for Render / Railway / Fly.io)
- [ ] ML model re-evaluated on real, unseen complaints (the current
      0.972 test score is on template-generated demo data)
- [ ] Secrets (if any are added later) in environment variables,
      never committed to Git
