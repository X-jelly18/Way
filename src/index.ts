/**
 * Cloudflare Workers HTTP/WebSocket Proxy
 * Converts Express.js proxy to CF Workers
 */

type RouteMap = Record<string, string>;

// Option 1: Hardcoded routes (simple, no KV needed)
const ROUTES: RouteMap = {
  "/maibhhhh": "http://south.ayanakojivps.shop",
  "/s1": "http://south2.ayanakojivps.shop",
  "/s2": "http://south3.ayanakojivps.shop",
  "/p6CAaNg": "https://pluto.plutoallin1.shop",
};

// Option 2: Load from env.ROUTES_JSON (set in wrangler.toml or dashboard)
// const ROUTES: RouteMap = JSON.parse(env.ROUTES_JSON || "{}");

function getTarget(pathname: string): string | null {
  for (const [route, target] of Object.entries(ROUTES)) {
    if (pathname.startsWith(route)) {
      return target;
    }
  }
  return null;
}

export default {
  async fetch(request: Request, env: Env, ctx: ExecutionContext): Promise<Response> {
    const url = new URL(request.url);
    const target = getTarget(url.pathname);

    if (!target) {
      return new Response("Invalid path", { status: 404 });
    }

    // WebSocket upgrade
    if (request.headers.get("upgrade") === "websocket") {
      return handleWebSocket(request, target);
    }

    // HTTP proxy
    return handleHTTP(request, target);
  },
};

/**
 * Handle HTTP proxying
 */
async function handleHTTP(request: Request, target: string): Promise<Response> {
  const url = new URL(request.url);
  const targetUrl = new URL(url.pathname + url.search, target);

  try {
    // Copy headers, excluding host
    const headers = new Headers(request.headers);
    headers.set("host", new URL(target).host);
    headers.delete("connection");
    headers.delete("upgrade");

    const proxiedRequest = new Request(targetUrl, {
      method: request.method,
      headers,
      body: ["GET", "HEAD"].includes(request.method) ? null : request.body,
    });

    const response = await fetch(proxiedRequest);
    const responseHeaders = new Headers(response.headers);

    // Remove hop-by-hop headers
    responseHeaders.delete("connection");
    responseHeaders.delete("keep-alive");
    responseHeaders.delete("transfer-encoding");
    responseHeaders.delete("upgrade");

    return new Response(response.body, {
      status: response.status,
      statusText: response.statusText,
      headers: responseHeaders,
    });
  } catch (err) {
    console.error("HTTP proxy error:", err);
    return new Response("Bad Gateway", { status: 502 });
  }
}

/**
 * Handle WebSocket proxying
 * Uses Cloudflare's native WebSocket support
 */
function handleWebSocket(request: Request, target: string): Response {
  const url = new URL(request.url);
  const targetUrl = new URL(url.pathname + url.search, target).toString();

  try {
    // Extract upgrade headers
    const webSocketPair = new WebSocketPair();
    const [client, server] = Object.values(webSocketPair);

    // Connect to target WebSocket
    connectWebSocket(server as unknown as WebSocket, targetUrl);

    return new Response(null, {
      status: 101,
      webSocket: client as unknown as WebSocket,
    });
  } catch (err) {
    console.error("WebSocket proxy error:", err);
    return new Response("Bad Gateway", { status: 502 });
  }
}

/**
 * Relay WebSocket frames between client and target
 */
async function connectWebSocket(server: WebSocket, targetUrl: string): Promise<void> {
  try {
    const targetWs = new WebSocket(targetUrl);

    // Forward client -> target
    server.addEventListener("message", (event) => {
      if (targetWs.readyState === WebSocket.OPEN) {
        targetWs.send(event.data);
      }
    });

    // Forward target -> client
    targetWs.addEventListener("message", (event) => {
      if (server.readyState === WebSocket.OPEN) {
        server.send(event.data);
      }
    });

    // Handle close
    server.addEventListener("close", () => {
      if (targetWs.readyState === WebSocket.OPEN) {
        targetWs.close();
      }
    });

    targetWs.addEventListener("close", () => {
      if (server.readyState === WebSocket.OPEN) {
        server.close();
      }
    });

    // Handle errors
    server.addEventListener("error", (err) => {
      console.error("Server WebSocket error:", err);
      if (targetWs.readyState === WebSocket.OPEN) {
        targetWs.close();
      }
    });

    targetWs.addEventListener("error", (err) => {
      console.error("Target WebSocket error:", err);
      if (server.readyState === WebSocket.OPEN) {
        server.close();
      }
    });

    server.accept();
  } catch (err) {
    console.error("WebSocket connection error:", err);
    server.close();
  }
}
