# Complete GitHub Deployment Guide

Deploy NevermoreSSH Proxy to Cloudflare Workers via GitHub Actions.

## Step 1: Create GitHub Repository

### Option A: New Repository
```bash
# Create on GitHub.com
1. Go to https://github.com/new
2. Name: nevermore-proxy
3. Make it Public or Private
4. Add .gitignore: Node
5. Create repository
```

### Option B: Push Existing Code
```bash
cd nevermore-proxy
git init
git add .
git commit -m "Initial commit"
git remote add origin https://github.com/yourusername/nevermore-proxy.git
git branch -M main
git push -u origin main
```

---

## Step 2: Get Cloudflare Credentials

### Get API Token
1. Go to https://dash.cloudflare.com/profile/api-tokens
2. Click **"Create Token"**
3. Select template: **"Edit Cloudflare Workers"**
4. Click **"Use template"**
5. Name: `nevermore-proxy-github`
6. Permissions should show:
   - Account > Cloudflare Workers Scripts > Edit
7. Click **"Continue to summary"**
8. Click **"Create Token"**
9. **Copy the token** (you'll only see it once)

### Get Account ID
1. Go to https://dash.cloudflare.com/
2. In right sidebar, find **"Account ID"**
3. Copy it (looks like: `abc123def456xyz789`)

---

## Step 3: Add GitHub Secrets

1. Go to your GitHub repo
2. Click **Settings** (top menu)
3. Left sidebar → **Secrets and variables** → **Actions**
4. Click **"New repository secret"**

### Add Secret 1: API Token
- **Name:** `CLOUDFLARE_API_TOKEN`
- **Value:** Paste your API token from Step 2
- Click **"Add secret"**

### Add Secret 2: Account ID
- **Name:** `CLOUDFLARE_ACCOUNT_ID`
- **Value:** Paste your Account ID from Step 2
- Click **"Add secret"**

### Add Secret 3: Admin Token
- **Name:** `ADMIN_TOKEN`
- **Value:** Generate random token:
  ```bash
  openssl rand -hex 32
  # Copy the output
  ```
- Click **"Add secret"**

### Verify All Secrets Added
Go back to **Settings → Secrets → Actions**

You should see:
- ✅ `CLOUDFLARE_API_TOKEN`
- ✅ `CLOUDFLARE_ACCOUNT_ID`
- ✅ `ADMIN_TOKEN`

---

## Step 4: Configure Wrangler

Edit `wrangler.toml`:

```toml
name = "nevermore-proxy"
main = "src/index.ts"
compatibility_date = "2024-12-01"

# Route to your domain
routes = [
  { pattern = "proxy.ayanakojivps.shop/*", zone_name = "ayanakojivps.shop" }
]

# Environment variables
[env.production.vars]
ADMIN_TOKEN = "will-be-set-by-github-actions"
```

**If you don't have a domain yet:**
- Leave `routes` empty
- Worker deploys to: `nevermore-proxy-{random}.workers.dev`

---

## Step 5: Update src/index.ts

Choose your version:

### Simple Version (Hardcoded Routes)
Copy this to `src/index.ts`:
```typescript
type RouteMap = Record<string, string>;

const ROUTES: RouteMap = {
  "/maibhhhh": "http://south.ayanakojivps.shop",
  "/s1": "http://south2.ayanakojivps.shop",
  "/s2": "http://south3.ayanakojivps.shop",
  "/p6CAaNg": "https://pluto.plutoallin1.shop",
};

function getTarget(pathname: string): string | null {
  for (const [route, target] of Object.entries(ROUTES)) {
    if (pathname.startsWith(route)) {
      return target;
    }
  }
  return null;
}

export default {
  async fetch(request: Request): Promise<Response> {
    const url = new URL(request.url);
    const target = getTarget(url.pathname);

    if (!target) {
      return new Response("Invalid path", { status: 404 });
    }

    if (request.headers.get("upgrade") === "websocket") {
      return handleWebSocket(request, target);
    }

    return handleHTTP(request, target);
  },
};

async function handleHTTP(request: Request, target: string): Promise<Response> {
  const url = new URL(request.url);
  const targetUrl = new URL(url.pathname + url.search, target);

  try {
    const headers = new Headers(request.headers);
    headers.set("host", new URL(target).host);

    const proxiedRequest = new Request(targetUrl, {
      method: request.method,
      headers,
      body: ["GET", "HEAD"].includes(request.method) ? null : request.body,
    });

    const response = await fetch(proxiedRequest);
    const responseHeaders = new Headers(response.headers);
    responseHeaders.delete("connection");

    return new Response(response.body, {
      status: response.status,
      headers: responseHeaders,
    });
  } catch (err) {
    return new Response("Bad Gateway", { status: 502 });
  }
}

function handleWebSocket(request: Request, target: string): Response {
  const url = new URL(request.url);
  const targetUrl = new URL(url.pathname + url.search, target).toString();

  try {
    const webSocketPair = new WebSocketPair();
    const [client, server] = Object.values(webSocketPair);
    connectWebSocket(server as unknown as WebSocket, targetUrl);

    return new Response(null, {
      status: 101,
      webSocket: client as unknown as WebSocket,
    });
  } catch {
    return new Response("Bad Gateway", { status: 502 });
  }
}

async function connectWebSocket(server: WebSocket, targetUrl: string): Promise<void> {
  const targetWs = new WebSocket(targetUrl);

  server.addEventListener("message", (event) => {
    if (targetWs.readyState === WebSocket.OPEN) {
      targetWs.send(event.data);
    }
  });

  targetWs.addEventListener("message", (event) => {
    if (server.readyState === WebSocket.OPEN) {
      server.send(event.data);
    }
  });

  server.addEventListener("close", () => {
    if (targetWs.readyState === WebSocket.OPEN) targetWs.close();
  });

  targetWs.addEventListener("close", () => {
    if (server.readyState === WebSocket.OPEN) server.close();
  });

  server.accept();
}
```

---

## Step 6: Push to GitHub

```bash
# From your project directory
git add .
git commit -m "Setup Cloudflare Workers deployment"
git push origin main
```

---

## Step 7: Trigger Deployment

The GitHub Actions workflow automatically runs on push.

**Check deployment status:**

1. Go to your GitHub repo
2. Click **Actions** tab (top menu)
3. You'll see workflow running: "Deploy to Cloudflare Workers"
4. Wait for it to complete (1-2 minutes)
5. Green ✅ = Success | Red ❌ = Check logs

---

## Step 8: Get Your Worker URL

Once deployed successfully:

### With Custom Domain
If you set a route in `wrangler.toml`:
```
https://proxy.ayanakojivps.shop/
```

### Without Custom Domain
Cloudflare auto-assigns:
```
https://nevermore-proxy-{random}.workers.dev/
```

Find it:
1. Go to https://dash.cloudflare.com/
2. Left sidebar → **Workers**
3. Click your worker: `nevermore-proxy`
4. Copy **URL** from top-right

---

## Step 9: Test Your Worker

### Test HTTP Proxy
```bash
curl https://your-worker-url/maibhhhh
```

Should proxy to: `http://south.ayanakojivps.shop/`

### Test WebSocket
```bash
npm install -g wscat
wscat -c wss://your-worker-url/s1
```

### Test Invalid Route
```bash
curl https://your-worker-url/invalid
```

Should return 404 with available routes.

---

## Step 10: Update Routes (No Redeployment)

Edit `routes.json`:
```json
{
  "/maibhhhh": "http://south.ayanakojivps.shop",
  "/s1": "http://south2.ayanakojivps.shop",
  "/new-route": "https://new-backend.shop"
}
```

Commit and push:
```bash
git add routes.json
git commit -m "Add new route"
git push origin main
```

GitHub Actions redeploys automatically (takes ~2 min).

---

## Continuous Deployment Setup

Every push to `main` now:
1. ✅ Runs tests (if configured)
2. ✅ Deploys to Cloudflare Workers
3. ✅ Updates all routes
4. ✅ Keeps WebSocket proxy alive globally

**Check status anytime:**
- GitHub **Actions** tab → Latest workflow
- Cloudflare **Dashboard** → Workers → Deployments

---

## Troubleshooting Deployment

### "Unauthorized" Error
- Verify `CLOUDFLARE_API_TOKEN` is correct
- Token may have expired → generate new one
- Check token has "Edit Workers" permission

### "Not Found" Error
- Verify `CLOUDFLARE_ACCOUNT_ID` is correct
- Should not include email or domain
- Check it matches your Cloudflare account

### "Worker already exists"
- Worker name in `wrangler.toml` matches existing
- Either use different name or update existing

### Check Logs
```bash
# View full deployment logs
wrangler tail --env production

# Or in GitHub Actions:
# Click on workflow → See detailed logs
```

---

## Next Steps

- [ ] Verify deployment successful
- [ ] Test all routes work
- [ ] Setup custom domain (optional)
- [ ] Enable rate limiting in Cloudflare
- [ ] Add monitoring/alerts
- [ ] Document routes in README

---

## Summary

| Step | Command | Time |
|------|---------|------|
| 1. Create repo | `git init` | 2 min |
| 2. Get CF creds | Cloudflare Dashboard | 5 min |
| 3. Add secrets | GitHub Settings | 3 min |
| 4. Configure | Edit `wrangler.toml` | 2 min |
| 5. Add code | Copy `src/index.ts` | 1 min |
| 6. Push to GitHub | `git push origin main` | 1 min |
| 7. Deployment | Wait for Actions ✅ | 2 min |
| **Total** | | **16 min** |

**Result:** Live global proxy with automatic deploys on every push 🚀

---

**Questions?** Check:
- `GITHUB_SECRETS.md` — Secret setup help
- `SETUP.md` — Local testing
- Cloudflare Docs — https://developers.cloudflare.com/workers/
