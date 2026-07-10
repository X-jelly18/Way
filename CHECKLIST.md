# Pre-GitHub Push Checklist

Before pushing to GitHub for the first time, verify everything:

## Code Files (No Changes Needed)

- [ ] `src/index.ts` — Proxy code (ready as-is)
- [ ] `.github/workflows/deploy.yml` — GitHub Actions (ready as-is)
- [ ] `package.json` — Dependencies (ready as-is)
- [ ] `tsconfig.json` — TypeScript config (ready as-is)
- [ ] `wrangler.toml` — Cloudflare config (ready as-is)

## Configuration (Review/Update)

- [ ] `routes.json` — Check routes are correct
  ```json
  {
    "/maibhhhh": "http://south.ayanakojivps.shop",
    "/s1": "http://south2.ayanakojivps.shop",
    "/s2": "http://south3.ayanakojivps.shop",
    "/p6CAaNg": "https://pluto.plutoallin1.shop"
  }
  ```
- [ ] `wrangler.toml` — Update if:
  - [ ] You have a custom domain (add to `routes`)
  - [ ] You want different worker name (change `name =`)
  - Otherwise leave as-is

## Documentation

- [ ] `README.md` — Review, update with your info
- [ ] `QUICKSTART.md` — This is your setup guide
- [ ] `FILE_GUIDE.md` — Reference for file purposes

## Git Setup

- [ ] Repository created on GitHub.com
- [ ] Git initialized locally:
  ```bash
  git init
  cd repo-folder
  ```
- [ ] All files added:
  ```bash
  git add .
  git status  # Verify all files shown
  ```
- [ ] Commit ready:
  ```bash
  git commit -m "Initial commit: Cloudflare Workers proxy"
  ```
- [ ] Remote added:
  ```bash
  git remote add origin https://github.com/YOUR_USERNAME/nevermore-proxy.git
  ```

## GitHub Setup (After Push)

- [ ] Code pushed to GitHub:
  ```bash
  git push -u origin main
  ```
- [ ] Verify on GitHub.com — repo shows all files
- [ ] Got Cloudflare credentials:
  - [ ] API Token from https://dash.cloudflare.com/profile/api-tokens
  - [ ] Account ID from https://dash.cloudflare.com/
- [ ] Added 3 GitHub secrets (Settings → Secrets):
  - [ ] `CLOUDFLARE_API_TOKEN`
  - [ ] `CLOUDFLARE_ACCOUNT_ID`
  - [ ] `ADMIN_TOKEN` (random: `openssl rand -hex 32`)

## First Deployment

- [ ] Push to trigger deploy:
  ```bash
  git commit --allow-empty -m "Trigger deployment"
  git push origin main
  ```
- [ ] Watch GitHub Actions (Actions tab) ✅
- [ ] Deployment completes successfully (green ✓)
- [ ] Check Cloudflare dashboard for worker

## Testing

- [ ] Get worker URL from Cloudflare dashboard
- [ ] Test HTTP proxy:
  ```bash
  curl https://your-worker-url/maibhhhh
  ```
- [ ] Test WebSocket:
  ```bash
  wscat -c wss://your-worker-url/s1
  ```
- [ ] Test invalid route (should return 404):
  ```bash
  curl https://your-worker-url/invalid
  ```

## Everything Good? 🎉

You're done! Your proxy is:
- ✅ On GitHub
- ✅ Deployed to Cloudflare
- ✅ Live globally
- ✅ Auto-deploys on push

---

## Quick Command Reference

```bash
# Initialize repo
git init
cd your-repo-folder

# Stage all files
git add .

# Check status
git status

# Commit
git commit -m "Initial commit"

# Add remote (from GitHub)
git remote add origin https://github.com/username/nevermore-proxy.git

# Push to main branch
git push -u origin main

# Push future changes
git push origin main

# Check deployment status
# → Visit GitHub Actions tab
```

---

## If Something Goes Wrong

**File modified by mistake?**
```bash
git checkout src/index.ts  # Restore file
```

**Need to fix commit message?**
```bash
git commit --amend -m "Better message"
```

**Committed secret by accident?**
```bash
# Remove from history (ask for help)
git filter-branch --tree-filter 'rm -f .env'
```

**Want to rollback deployment?**
```bash
git revert HEAD
git push origin main
```

---

✅ All set! Follow QUICKSTART.md when ready.
