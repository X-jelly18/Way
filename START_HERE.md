# 🚀 START HERE

Complete GitHub + Cloudflare setup for your proxy. All files ready to push.

---

## What You Have

13 files, organized and ready to push to GitHub:

```
.
├── .github/workflows/deploy.yml    ← GitHub Actions auto-deploy
├── src/index.ts                    ← Proxy code (HTTP + WebSocket)
├── .gitignore                      ← Protect secrets
├── .env.example                    ← Env var template
├── package.json                    ← Dependencies
├── tsconfig.json                   ← TypeScript config
├── wrangler.toml                   ← Cloudflare config
├── routes.json                     ← Your routes (EDIT THIS)
├── README.md                       ← Full docs
├── QUICKSTART.md                   ← Follow this next ⭐
├── FILE_GUIDE.md                   ← File explanations
├── DEPLOY_VIA_GITHUB.md           ← Detailed guide
├── CHECKLIST.md                    ← Pre-push verification
└── START_HERE.md                   ← This file
```

**Nothing needs to be changed. Everything is ready to use.**

---

## Next Steps (Pick One Path)

### Path A: I Know Git (Fastest)

```bash
# 1. Create repo on GitHub
# GitHub.com → New → Name: nevermore-proxy → Create

# 2. Clone these files into your repo folder
# (Copy all 13 files/folders into your local folder)

# 3. Push to GitHub
cd your-repo-folder
git init
git add .
git commit -m "Initial commit"
git remote add origin https://github.com/yourname/nevermore-proxy.git
git branch -M main
git push -u origin main

# 4. Read QUICKSTART.md for next steps
```

### Path B: I Don't Know Git (3 Steps)

1. **Download these files** (or download as ZIP)
2. **Go to GitHub.com**
   - Click **+** → **New repository**
   - Name: `nevermore-proxy`
   - Click **Create repository**
   - Copy the repo URL

3. **Follow QUICKSTART.md** (step by step walkthrough)

### Path C: Just Tell Me What To Do

1. Read **QUICKSTART.md** — everything step by step
2. Get Cloudflare credentials
3. Add GitHub secrets
4. Push code
5. Done ✅

---

## File Purpose Summary

| File | Purpose | Edit? |
|------|---------|-------|
| `QUICKSTART.md` | Setup guide | No |
| `routes.json` | Your routes | Yes (when routes change) |
| `src/index.ts` | Proxy code | No (unless changing logic) |
| `wrangler.toml` | CF config | No (unless custom domain) |
| `.github/workflows/deploy.yml` | Auto-deploy | No |
| Everything else | Config/docs | No |

---

## Read These In Order

1. **START_HERE.md** ← You are here
2. **QUICKSTART.md** ← Do this next
3. **FILE_GUIDE.md** ← Reference when needed
4. **DEPLOY_VIA_GITHUB.md** ← Full details

---

## What Happens

1. ✅ You push code to GitHub
2. ✅ GitHub Actions automatically runs
3. ✅ Your proxy deploys to Cloudflare Workers
4. ✅ It's live globally in ~2 minutes
5. ✅ Every future push auto-deploys

No manual `wrangler deploy` needed. Just `git push`.

---

## The 4-Minute Setup

```bash
# 1. Copy all 13 files to your folder (1 min)

# 2. Create GitHub repo (1 min)
#    GitHub.com → New repository → Copy URL

# 3. Push code (1 min)
git init
git add .
git commit -m "Initial"
git remote add origin YOUR_GITHUB_URL
git push -u origin main

# 4. Add secrets (1 min)
#    GitHub Settings → Secrets → Add 3 secrets

# Done! Deployment happens automatically 🚀
```

---

## Getting Cloudflare Credentials

Before pushing, get these 2 things:

**API Token:**
1. https://dash.cloudflare.com/profile/api-tokens
2. Create Token → Edit Cloudflare Workers → Use template
3. Copy token

**Account ID:**
1. https://dash.cloudflare.com/
2. Right sidebar shows Account ID
3. Copy it

---

## Getting GitHub Secrets

After pushing to GitHub:

1. Go to your repo on GitHub.com
2. Settings → Secrets and variables → Actions
3. Add 3 secrets:
   - `CLOUDFLARE_API_TOKEN` (from above)
   - `CLOUDFLARE_ACCOUNT_ID` (from above)
   - `ADMIN_TOKEN` (random: `openssl rand -hex 32`)

---

## Your Proxy Routes

Edit `routes.json` to your routes:

```json
{
  "/maibhhhh": "http://south.ayanakojivps.shop",
  "/s1": "http://south2.ayanakojivps.shop",
  "/s2": "http://south3.ayanakojivps.shop",
  "/p6CAaNg": "https://pluto.plutoallin1.shop"
}
```

Push to GitHub → Auto-deploys (~2 min) ✅

---

## Test Your Proxy

Once deployed, test it:

```bash
# Get your URL from Cloudflare dashboard
# https://dash.cloudflare.com → Workers → nevermore-proxy

# HTTP test
curl https://your-worker-url/maibhhhh

# WebSocket test
wscat -c wss://your-worker-url/s1

# Invalid route test (should return 404)
curl https://your-worker-url/invalid
```

---

## Support

| Question | Answer |
|----------|--------|
| How do I push to GitHub? | Read QUICKSTART.md |
| How do I add secrets? | Read QUICKSTART.md section 3 |
| How do I get Cloudflare creds? | Read QUICKSTART.md section 3 |
| Where is my worker URL? | Cloudflare dashboard → Workers → nevermore-proxy |
| How do I update routes? | Edit routes.json → git push |
| Deployment failed? | Check GitHub Actions logs |
| Worker not responding? | Wait 2 min, check URL, verify routes.json |

---

## TL;DR

1. Download these 13 files
2. Push to GitHub
3. Add 3 secrets
4. Deployment happens automatically
5. You have a live proxy 🎉

**Start with QUICKSTART.md →**

---

Made for NevermoreSSH 🔒
