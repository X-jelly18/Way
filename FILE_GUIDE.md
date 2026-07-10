# Repository Files Guide

Complete explanation of every file in the project.

## Root Level Files

### `README.md`
Main documentation. Users read this first.
- Features overview
- Quick start
- Usage examples
- Troubleshooting

### `QUICKSTART.md` ⭐ START HERE
Step-by-step guide to:
1. Push code to GitHub
2. Add secrets
3. Trigger first deployment
4. Test your proxy

**Read this first when setting up.**

### `DEPLOY_VIA_GITHUB.md`
Detailed deployment guide with all the context.
- Get Cloudflare credentials
- Configure GitHub secrets
- Understand each config file
- Troubleshooting tips

### `FILE_GUIDE.md`
This file. Explains what each file does.

### `wrangler.toml`
Cloudflare Workers configuration.
- Worker name: `nevermore-proxy`
- Compatibility date: `2024-12-01`
- Production environment settings
- Routes (if using custom domain)

**Edit if:**
- You want different worker name
- You have custom domain to route to
- You want to change environment variables

### `package.json`
NPM dependencies and scripts.
```bash
npm run dev        # Local development
npm run deploy     # Manual deploy
npm run type-check # TypeScript check
```

**Automatically used by GitHub Actions for CI/CD.**

### `tsconfig.json`
TypeScript compiler configuration.
- Target: ES2020
- Strict mode enabled
- Module resolution for Cloudflare

**Don't change unless you know TypeScript.**

### `.gitignore`
Tells Git what NOT to commit.
- `node_modules/` (too large)
- `.env` (secrets)
- `.wrangler/` (build artifacts)

**Important: Prevents secrets from leaking to GitHub.**

### `.env.example`
Template for environment variables.
```bash
cp .env.example .env
# Fill in local values for development
```

**Don't commit `.env` — it stays local only.**

### `routes.json`
Route definitions (HTTP path → Backend target).
```json
{
  "/maibhhhh": "http://south.ayanakojivps.shop",
  "/s1": "http://south2.ayanakojivps.shop"
}
```

**Edit this to add/remove/update routes.**
**Changes auto-deploy when pushed to GitHub.**

---

## `.github/workflows/` Directory

### `deploy.yml`
GitHub Actions workflow for automatic deployment.

**What it does:**
1. Runs on every push to `main` branch
2. Installs dependencies
3. Authenticates with Cloudflare
4. Deploys Worker
5. Comments on PRs with status

**Triggered by:**
- `git push origin main`
- Changes to `src/` or `wrangler.toml`

**Uses secrets:**
- `CLOUDFLARE_API_TOKEN`
- `CLOUDFLARE_ACCOUNT_ID`

**You don't edit this unless you know GitHub Actions.**

---

## `src/` Directory

### `index.ts`
Main Worker code (HTTP + WebSocket proxy).

**What it does:**
1. Receives HTTP/WebSocket requests
2. Looks up target backend in `routes.json`
3. Proxies request to target
4. Returns response to client

**Edit this to:**
- Change proxy logic
- Add custom headers
- Modify error handling
- Add request validation

**You can modify it, but test locally first:**
```bash
npm run dev
```

---

## Complete File Structure

```
nevermore-proxy/
├── .github/
│   └── workflows/
│       └── deploy.yml              # GitHub Actions CI/CD
├── src/
│   └── index.ts                    # Main worker code
├── .gitignore                      # What to exclude from Git
├── .env.example                    # Example env vars (local only)
├── package.json                    # NPM dependencies
├── wrangler.toml                   # Cloudflare config
├── tsconfig.json                   # TypeScript config
├── routes.json                     # Route mappings
├── README.md                       # Full documentation
├── QUICKSTART.md                   # Quick setup guide
├── DEPLOY_VIA_GITHUB.md           # Detailed deployment
└── FILE_GUIDE.md                   # This file
```

---

## File Edit Matrix

| File | Edit? | Reason | When? |
|------|-------|--------|-------|
| `routes.json` | ✅ Yes | Add/remove routes | Every time routes change |
| `src/index.ts` | ✅ Yes | Custom logic | If proxy behavior needs change |
| `wrangler.toml` | ⚠️ Careful | Change worker config | Custom domain, env vars |
| `.github/workflows/deploy.yml` | ❌ No | Already configured | Only if familiar with Actions |
| `package.json` | ❌ No | Locked dependencies | Only if adding packages |
| `tsconfig.json` | ❌ No | TypeScript settings | Only if you know TS |
| `.gitignore` | ❌ No | Already configured | Prevents secret leaks |
| `README.md` | ✅ Yes | Update docs | Add your custom info |

---

## Workflow Summary

### Daily Use:
```bash
# Edit routes
vim routes.json

# Push to GitHub
git add routes.json
git commit -m "Add new route"
git push origin main

# GitHub Actions automatically:
# 1. Runs tests
# 2. Deploys to Cloudflare
# 3. Worker is live globally (~2 min)
```

### If Changing Logic:
```bash
# Edit code
vim src/index.ts

# Test locally
npm run dev

# Push to GitHub
git add src/index.ts
git commit -m "Fix proxy logic"
git push origin main

# Auto-deploys via GitHub Actions
```

### Emergency Rollback:
```bash
# Revert last commit
git revert HEAD
git push origin main

# Auto-redeploys previous version
```

---

## What Happens When You Push?

```
git push origin main
         ↓
GitHub receives code
         ↓
`.github/workflows/deploy.yml` runs automatically
         ↓
Installs dependencies (npm ci)
         ↓
Authenticates with Cloudflare using secrets
         ↓
Runs `wrangler deploy`
         ↓
Code deployed to all Cloudflare edge locations
         ↓
Your routes from `routes.json` are active
         ↓
Worker responds to requests globally (~50ms)
```

---

## Common Questions

**Q: Do I need to edit all these files?**
A: No. Only edit:
- `routes.json` (routes)
- `src/index.ts` (if changing proxy logic)
- `README.md` (documentation)

**Q: What if deployment fails?**
A: Check GitHub Actions logs (Actions tab). Likely causes:
- Invalid JSON in `routes.json`
- Secret not set
- Typo in `wrangler.toml`

**Q: Can I test locally?**
A: Yes:
```bash
npm install
npm run dev
# Runs on http://localhost:8787
```

**Q: How do I rollback?**
A: 
```bash
git revert HEAD
git push origin main
```

**Q: Do I need to run `wrangler deploy` manually?**
A: No. GitHub Actions does it automatically.

---

## Next Steps

1. **Read:** `QUICKSTART.md`
2. **Push code** to GitHub
3. **Add secrets** to GitHub
4. **Trigger deployment** with a commit
5. **Test** your proxy
6. **Update routes** as needed

All files are ready to use as-is. No modifications needed to get started!
