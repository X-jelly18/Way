# NevermoreSSH Proxy on Cloudflare Workers

Deploy an Express.js HTTP/WebSocket proxy to Cloudflare Workers with zero server costs.

## Features

✅ HTTP & WebSocket proxying  
✅ Dynamic route management  
✅ Automatic deployment via GitHub Actions  
✅ KV-backed route storage (optional)  
✅ Sub-50ms global latency  
✅ Cost: $0.15 per million requests (vs $20-100/mo VPS)

## Quick Start

### 1. Clone Repository
```bash
git clone https://github.com/yourusername/nevermore-proxy.git
cd nevermore-proxy
```

### 2. Install Dependencies
```bash
npm install
```

### 3. Setup Cloudflare Credentials

Get your credentials from https://dash.cloudflare.com/

```bash
wrangler login
```

### 4. Deploy to Cloudflare Workers

```bash
npm run deploy
```

Done! Worker is live at `https://nevermore-proxy.<your-domain>.workers.dev`

## GitHub Actions Deployment

Automatic deployment on push to `main`:

1. **Add secrets** to GitHub (Settings → Secrets and variables)
   - `CLOUDFLARE_API_TOKEN`
   - `CLOUDFLARE_ACCOUNT_ID`
   - `ADMIN_TOKEN`

See [GITHUB_SECRETS.md](./GITHUB_SECRETS.md) for detailed setup.

2. **Push code**
   ```bash
   git add .
   git commit -m "Update proxy routes"
   git push origin main
   ```

3. **Check deployment**
   - Go to **Actions** tab → Latest workflow run
   - Green ✓ = Success, Red ✗ = Check logs

## Configuration

### Routes

Edit `routes.json`:
```json
{
  "/maibhhhh": "http://south.ayanakojivps.shop",
  "/s1": "http://south2.ayanakojivps.shop",
  "/ws": "ws://backend.shop:8080"
}
```

Commit changes to redeploy:
```bash
git add routes.json
git commit -m "Add new route"
git push origin main
```

### Environment Variables

Set in `wrangler.toml` or GitHub Secrets:
```toml
[env.production.vars]
ADMIN_TOKEN = "your-secret-token"
```

## Usage

### HTTP Proxying
```bash
curl https://your-worker.dev/maibhhhh/path
```

### WebSocket Proxying
```bash
wscat -c wss://your-worker.dev/s1
```

### Update Routes (Runtime)
```bash
export WORKER_URL=https://your-worker.dev
export ADMIN_TOKEN=your-secret-token

./update-routes.sh update routes.json
# Or add single route:
./update-routes.sh set /new-route http://backend.shop
```

## Project Structure

```
src/
├── index.ts              # Main worker code (hardcoded routes)
└── index-kv.ts           # Optional KV version (dynamic routes)

.github/workflows/
└── deploy.yml            # Automatic deployment via GitHub Actions

routes.json              # Route definitions
wrangler.toml            # Cloudflare Workers configuration
```

## Choosing Versions

**Simple (index.ts):**
- Routes hardcoded in `routes.json`
- Deploy with git push
- Use if routes rarely change

**Advanced (index-kv.ts):**
- Routes stored in Cloudflare KV
- Update via REST API (`update-routes.sh`)
- No redeployment needed

To use KV version:
```bash
# 1. Create KV namespace
wrangler kv:namespace create ROUTES

# 2. Copy index-kv.ts → src/index.ts
cp src/index-kv.ts src/index.ts

# 3. Deploy
npm run deploy
```

## API Endpoints

### GET /invalid-path
Returns 404 with available routes:
```json
{
  "error": "Invalid path",
  "available_routes": ["/maibhhhh", "/s1", "/s2", "/p6CAaNg"]
}
```

### POST /admin/routes (KV version only)
Update routes dynamically:
```bash
curl -X POST https://your-worker.dev/admin/routes \
  -H "Authorization: Bearer $ADMIN_TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"new-route": "http://backend.shop"}'
```

## Monitoring

### View Logs
```bash
wrangler tail --env production
```

### Check Worker Status
```bash
wrangler deployments list
```

### Monitor Usage
https://dash.cloudflare.com → Workers → Analytics

## Security

⚠️ **Important:**
- Change `ADMIN_TOKEN` to a strong random value
- Use HTTPS for all production traffic
- Restrict routes to known backends
- Enable rate limiting for public endpoints

Generate secure token:
```bash
openssl rand -hex 32
```

## Troubleshooting

### Deployment fails
- Check GitHub Actions logs (Actions tab)
- Verify secrets are set correctly (Settings → Secrets)
- Ensure `wrangler.toml` is valid

### Routes not updating
- Confirm change pushed to `main` branch
- Wait 1-2 minutes for GitHub Actions to run
- Check Actions tab for deployment status

### WebSocket fails
- Ensure target supports WebSocket
- Check firewall allows connections
- Verify route path is correct

### High latency
- Check target backend performance
- Enable CF Cache Rules for static content
- Monitor with `wrangler tail`

## Contributing

1. Create feature branch: `git checkout -b feature/new-route`
2. Make changes to `routes.json` or `src/`
3. Test locally: `npm run dev`
4. Commit: `git commit -m "Add feature"`
5. Push: `git push origin feature/new-route`
6. Create Pull Request

## Deployment Checklist

Before production:
- [ ] All secrets added to GitHub
- [ ] `wrangler.toml` configured
- [ ] Routes in `routes.json` are correct
- [ ] Tested locally with `npm run dev`
- [ ] GitHub Actions workflow runs successfully
- [ ] Worker endpoint responding to requests
- [ ] WebSocket connections working
- [ ] Monitoring setup complete

## Docs & References

- [Cloudflare Workers Docs](https://developers.cloudflare.com/workers/)
- [Wrangler CLI](https://developers.cloudflare.com/workers/wrangler/)
- [WebSocket API](https://developers.cloudflare.com/workers/examples/websocket-proxy/)
- [GitHub Actions](https://docs.github.com/en/actions)

## License

MIT

## Support

For issues or questions:
1. Check [SETUP.md](./SETUP.md) for detailed setup
2. Review [GITHUB_SECRETS.md](./GITHUB_SECRETS.md) for auth errors
3. Check Actions logs for deployment issues
4. Open an issue on GitHub

---

**Made with ❤️ for NevermoreSSH Tunneling**
