# Quick Start: Push to GitHub

Complete this in order. Takes ~15 minutes.

## 1. Create GitHub Repository (5 min)

1. Go to https://github.com/new
2. Fill in:
   - **Repository name:** `nevermore-proxy`
   - **Description:** Cloudflare Workers SSH proxy
   - **Public/Private:** Choose
3. ✅ Create repository
4. Copy the HTTPS URL (e.g., `https://github.com/yourusername/nevermore-proxy.git`)

## 2. Push Code to GitHub (3 min)

```bash
# Initialize git
git init
git add .
git commit -m "Initial commit: Cloudflare Workers proxy"

# Add remote (use URL from Step 1)
git remote add origin https://github.com/yourusername/nevermore-proxy.git
git branch -M main
git push -u origin main
```

Done! Your code is now on GitHub.

## 3. Setup GitHub Secrets (5 min)

Before GitHub Actions can deploy, add 3 secrets:

### Get Cloudflare Credentials

**API Token:**
1. Go to https://dash.cloudflare.com/profile/api-tokens
2. Click "Create Token"
3. Select template: "Edit Cloudflare Workers"
4. Click "Use template" → "Continue to summary" → "Create Token"
5. Copy the token (appears only once)

**Account ID:**
1. Go to https://dash.cloudflare.com/
2. Right sidebar shows "Account ID" (e.g., `abc123xyz...`)
3. Copy it

### Add Secrets to GitHub

1. Go to your repo on GitHub
2. Click **Settings** → **Secrets and variables** → **Actions**
3. Click **New repository secret** (do this 3 times)

**Secret 1:**
- Name: `CLOUDFLARE_API_TOKEN`
- Value: Paste your API token
- Add secret ✓

**Secret 2:**
- Name: `CLOUDFLARE_ACCOUNT_ID`
- Value: Paste your Account ID
- Add secret ✓

**Secret 3:**
- Name: `ADMIN_TOKEN`
- Value: Generate random (run this in terminal):
  ```bash
  openssl rand -hex 32
  ```
  Copy output and paste here
- Add secret ✓

### Verify
Go back to **Settings → Secrets → Actions**
You should see all 3 secrets listed. ✅

## 4. Trigger Deployment (2 min)

Make a small change to trigger deployment:

```bash
# Edit README or make any change
git add .
git commit -m "Trigger deployment"
git push origin main
```

Go to your GitHub repo → **Actions** tab

You'll see "Deploy to Cloudflare Workers" running. Wait for ✅ (about 2 minutes).

## 5. Get Your Worker URL

Once deployment succeeds:

**Option A: With Custom Domain**
If you set a domain in `wrangler.toml`:
```
https://proxy.ayanakojivps.shop
```

**Option B: Cloudflare Auto-Assigned**
1. Go to https://dash.cloudflare.com/
2. Left sidebar → **Workers**
3. Click **nevermore-proxy**
4. Top right shows your URL: `https://nevermore-proxy-{random}.workers.dev`

## 6. Test Your Proxy

```bash
# HTTP
curl https://your-worker-url/maibhhhh

# WebSocket (requires wscat)
npm install -g wscat
wscat -c wss://your-worker-url/s1
```

---

## That's It! 🎉

Your proxy is now live on Cloudflare Workers.

**Every time you push to GitHub:**
- ✅ Automatically deploys to Cloudflare
- ✅ Updates routes
- ✅ Zero downtime

### Update Routes (No Redeployment)

Edit `routes.json`:
```json
{
  "/maibhhhh": "http://south.ayanakojivps.shop",
  "/new": "http://new-backend.shop"
}
```

Push to GitHub:
```bash
git add routes.json
git commit -m "Add new route"
git push origin main
```

Deployment auto-triggers (2 min).

---

## Troubleshooting

**Deployment fails?**
- Check Actions tab for error logs
- Verify all 3 secrets are set correctly
- Ensure API token has "Edit Workers" permission

**Worker not responding?**
- Wait 2 min after successful deployment
- Check worker URL in Cloudflare dashboard
- Verify routes.json has correct paths

**WebSocket fails?**
- Ensure target supports WebSocket
- Check path matches route (e.g., `/s1`)
- Test with: `wscat -c wss://your-url/s1`

---

See `DEPLOY_VIA_GITHUB.md` for detailed guide.
See `README.md` for full documentation.
