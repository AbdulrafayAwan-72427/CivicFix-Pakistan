# CivicFix Pakistan — Project Review & Improvement Plan

**Reviewed:** 8 November 2026  
**Project:** `D:\UNI\Semister_3\DSA\Lab\CIvicFix-Pakistan`  
**Review type:** Local build/run, limited browser inspection, code and documentation review

## Executive opinion

CivicFix is a promising portfolio project with a real civic-service use case and a more ambitious scope than a typical student CRUD app. The combination of a C++ REST API, React frontend, SQLite, maps, role-based tools, audit history, and a category-suggestion service gives you plenty to discuss in an interview.

The login and account-creation screens are clean and coherent, and they fit a 390px-wide mobile viewport without visible horizontal overflow. The production frontend build and a fresh build of the current C++ API both succeeded.

**My honest assessment:** it is a strong prototype, but I would not call it deployment-ready yet. The most urgent concerns are that an older API executable is currently serving port 8080 and exposes the complaint list without a login, and that the admin map inserts complaint titles into popup HTML. The README also contains unresolved merge-conflict markers. Fix those before sharing a live demo or presenting the repository as secure.

This review did not modify application source code. This file is the only intended project change from the review task.

---

## What I verified

### Build and runtime

- `frontend`: `npm run build` passed (Vite transformed 44 modules and produced the production bundle).
- Current `backend-cpp` source: CMake configure and build passed. CMake emitted a non-fatal warning that the `SQLite::SQLite3` target name is deprecated.
- A fresh API built from the current source was run on **port 8081** with a separate copy of the seeded database in the Hermes scratch directory.
- On that fresh API, `/api/health` returned **200**, unauthenticated requests to `/api/auth/me`, `/api/users`, and `/api/complaints` returned **401**, and a CORS preflight returned **204**.
- I inspected login and account creation in the browser at desktop size and at 390px mobile width. No browser JavaScript errors appeared during those screens.
- A Vite frontend was started at **http://127.0.0.1:5174** and configured to use the fresh API on port 8081. The existing frontend setup on port 5173 proxies to port 8080.

### Not verified

- I did not sign in, submit a complaint, create an account, or exercise citizen/admin/staff workflows. Therefore, dashboard behavior and authenticated end-to-end flows still need manual or automated testing.
- I did not run the Python ML service or evaluate its predictions.
- The visual review covers login and signup only; I cannot claim that the admin dashboard, maps, tables, or complaint drawer have been visually QA-tested.

---

## Priority findings

### P0 — Do not expose the currently running port-8080 API

**Observed:** The already-running `civicfix_api.exe` on port 8080 is older than the current C++ source (the executable is dated 5 November, while relevant source changes are dated 8 November). On that running server, `GET /api/complaints` returned **200** with the seeded complaint list and no authentication. Some other endpoints did require login, so this looks like a stale/incomplete build rather than proof that the current source has the same behavior.

**Why it matters:** The frontend's Vite proxy points to port 8080 by default. If the old server is exposed to another machine or deployed, complaint details could be disclosed. Do not assume that a successful health check proves the server is running the latest code.

**Recommended fix:**

1. Stop the old API process when you are sure it is yours to stop; it was already running before this review, so it was left untouched.
2. Rebuild the API from the current `backend-cpp` source and restart it using a test database copy.
3. Check every protected endpoint without a token and verify it returns 401/403. Check with citizen, staff, and admin accounts that each role sees only the intended data.
4. Ensure the frontend proxy and API port point to the same fresh server.
5. Repeat these checks after every backend rebuild, not just after a health check.

The fresh API on 8081 passed the unauthenticated 401 checks, but full authorized role isolation still needs testing.

### P0 — Potential stored cross-site scripting in map popups

**Location:** `frontend/src/components/MapPicker.jsx`, around line 74.

The map popup is built from a string containing `m.trackingCode` and `m.title`, then passed to Leaflet as HTML. Complaint titles are user-provided. Unless they are safely sanitized elsewhere, a crafted title could execute markup or script when an admin opens the marker popup. This is a potential stored-XSS path and could put the admin session at risk.

**Recommended fix:** Build popup contents with DOM nodes and set user-provided values with `textContent`. Do not concatenate user-controlled strings into HTML. Add a regression test using a title containing HTML-like input and verify it is displayed literally.

### P0 — Resolve the merge conflict in the root README

**Location:** `README.md`, around lines 150–191.

The committed README contains literal `<<<<<<<`, `=======`, and `>>>>>>>` markers. This is immediately visible to anyone reviewing the repository and makes the documentation look unfinished.

**Recommended fix:** Keep the intended current text, remove all conflict markers and duplicated stale text, then search the repository for any remaining conflict markers before publishing.

### P1 — Restrict CORS before deployment

**Observed in current API:** Responses include `Access-Control-Allow-Origin: *` and allow authorization headers. This helped the local cross-origin test work, but a production API should generally allow only the real frontend origin(s).

**Recommended fix:** Configure an explicit origin allowlist for production and handle local development separately. Verify preflight and actual authenticated requests from the allowed origin, and confirm that an unrelated origin is not allowed.

### P1 — Remove or isolate seeded demo accounts for real deployment

**Locations:** `frontend/src/pages/LoginPage.jsx` and the API/database startup seeding logic. The login page displays demo credentials only when Vite is in development mode. That banner is not a substitute for changing server-side seeded passwords.

**Recommended fix:** Keep demo accounts confined to a clearly labelled local/demo database. Before deployment, rotate or disable seeded accounts, never rely on public default passwords, and ensure you do not publish real user data in a seed database. Do not run the Vite development server as a public production host.

### P1 — Make documentation match the implementation

The documentation currently disagrees with itself:

- `frontend/README.md` says there is no login/authentication and describes an old demo-user selector.
- `documentation/DEPLOYMENT.md` still lists password hashing, role checks, and CORS as work to do, although the current source and root README describe those features as implemented.
- The root `README.md` has a newer feature description, but also contains the unresolved conflict block noted above.

**Recommended fix:** Update all docs from the current source, not from old notes. Include the exact current build/run commands, auth requirements, public-vs-protected endpoints, seeded demo limitations, CORS policy, and what remains unimplemented. Remove outdated descriptions rather than appending another “status” section.

---

## Product, UX, and visual improvements

### Preserve the good parts

- The green-and-white visual system is restrained and appropriate for a civic service.
- Login/signup hierarchy is understandable; inputs have visible labels and the primary action is easy to identify.
- The account-creation layout fit the inspected mobile viewport without visible clipping or horizontal overflow.
- The README describes a useful end-to-end workflow rather than only listing technology names.

### Make tracking available without unnecessary friction

The app currently renders `LoginPage` whenever there is no logged-in user (`frontend/src/App.jsx`). The tracking-code form exists inside the citizen workspace. That means a person with a tracking code cannot reach the tracking UI until they log in or create an account.

**Improve it by:** providing a public tracking page that accepts a tracking code, or revising the product requirements and documentation if tracking is intentionally account-only. If tracking codes grant access to complaint details, treat them as secrets: make them hard to guess, limit what the public response reveals, and consider rate limiting.

### Clarify the geographic scope

The product is named *CivicFix Pakistan*, but the visible brand says Lahore and the supplied areas/map defaults are Lahore-specific. That is fine for an MVP, but the portfolio should state the scope plainly.

**Choose one:**

- Position the project as a Lahore pilot and explain how more cities would be added; or
- Add city/province selection and ensure areas, map centering, categories, and reporting flows work outside Lahore.

Avoid implying nationwide coverage when the current demo is Lahore-only.

### Improve the account experience

Possible follow-up improvements:

- Add a show/hide password control.
- Add a password-reset flow, or clearly label it as not implemented.
- Explain password requirements before the user submits the form.
- Announce login/register errors accessibly and preserve helpful form state after a failed request.
- Consider Urdu alongside English if local citizens are the intended audience; test text expansion and right-to-left layout rather than just translating labels.
- Separate demo-mode instructions from normal citizen account creation so demo credentials do not distract or encourage reuse.

### Test the authenticated dashboard before judging its visual polish

The dashboards, tables, maps, and complaint drawer were not inspected in an authenticated session. Before calling the UI complete, review them at desktop, tablet, and phone widths. Pay particular attention to:

- Whether navigation and action buttons remain usable on narrow screens.
- Whether data tables communicate their horizontal scrolling on phones.
- Empty, loading, error, and success states.
- Keyboard focus, screen-reader labels, contrast, and confirmation flows for destructive actions.
- Whether the staff view clearly distinguishes personal assignments from the general queue.

---

## Engineering and portfolio improvements

### Add tests that prove the important claims

The frontend package currently has build/dev/preview scripts but no frontend test or lint script. Add tests incrementally rather than trying to test everything at once.

Recommended coverage:

1. **API authorization:** unauthenticated requests are rejected where required; citizens cannot read another citizen's complaint; staff cannot use admin-only actions.
2. **Workflow rules:** valid complaint status transitions succeed; invalid transitions fail; audit history and undo behave correctly.
3. **Form validation:** required fields, area selection, photo type/size, invalid tracking code, and error feedback.
4. **Security regressions:** map popup treats HTML-like complaint titles as text; public tracking exposes only the intended fields.
5. **UI smoke tests:** login, registration, report submission, tracking, admin assignment, staff work queue, and mobile navigation.

Keep test data in a disposable database copy. Avoid running destructive workflow tests against `database/civicfix.db`.

### Make the ML claim measurable and honest

The deployment notes already say the current image classifier is general-purpose rather than trained on civic photos, and that the reported score uses generated demo data. That is an important limitation, not something to hide.

For a portfolio-quality ML story:

- Explain that the model provides a routing hint, not a decision.
- Evaluate text and image suggestions separately on representative held-out examples.
- Report precision/recall or a confusion matrix by category, not just one accuracy number.
- Include failure examples and low-confidence handling.
- Keep a human review step and do not imply deployment-grade accuracy from synthetic data.

### Improve reproducibility and repository presentation

- Provide one reliable local startup path (for example, a documented script or development compose setup) that starts the API, frontend, and optional ML service with consistent ports.
- Include a safe database reset/seed command for demos.
- Add a short architecture diagram and a data-flow explanation.
- Add screenshots or a brief demo walkthrough after the dashboards have been re-tested.
- Document what is implemented, what is simulated/demo-only, and what is future work in one authoritative section.
- Keep the project focused: explain how the DSA elements (queue, graph, sorting, action stack/history) are used in actual workflows and what trade-offs they demonstrate.

---

## Suggested order of work

### Stage 1 — Safety and repo hygiene

- [ ] Stop using the stale API on port 8080 for the demo; rebuild the current source and verify endpoint protection.
- [ ] Fix the unsafe Leaflet popup construction and add a regression test.
- [ ] Remove merge-conflict markers and reconcile the contradictory README/deployment instructions.
- [ ] Restrict CORS for production and rotate/disable default accounts before any public deployment.

### Stage 2 — Prove core workflows

- [ ] Add API tests for anonymous/citizen/staff/admin access.
- [ ] Test complaint creation, public tracking, assignments, status transitions, audit history, and undo with a disposable database.
- [ ] Confirm tracking-code privacy and rate limiting.
- [ ] Review the dashboard at mobile/tablet/desktop sizes and complete keyboard/accessibility checks.

### Stage 3 — Portfolio polish

- [ ] Decide whether the scope is Lahore-only or multi-city and update the UI/docs accordingly.
- [ ] Add a public tracking experience or explicitly document why tracking needs an account.
- [ ] Add a consistent setup/demo command and a clean resettable demo dataset.
- [ ] Present the ML evaluation with real limitations and representative metrics.
- [ ] Capture fresh screenshots and prepare a short demo path that starts from a clean database.

---

## How I can help next

I can work through this with you one change at a time, preserving your existing code and testing each change. A sensible first task is the **map-popup XSS fix plus a regression test**, followed by the **README conflict/doc cleanup** and then **safe local startup using only the rebuilt API**. I can also help add the authorization and workflow tests, improve the mobile/admin UX, and prepare a polished portfolio walkthrough.

When making code changes, I should keep using a disposable database and avoid stopping the already-running port-8080 process unless you confirm that process is yours and should be replaced.
