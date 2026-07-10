type RouteMap = Record<string, string>;

const ROUTES: RouteMap = {
  "/maibhhhh": "http://south.ayanakojivps.shop",
  "/s1": "http://south2.ayanakojivps.shop",
  "/btc": "https://six2.ayanakojivps.shop",
  "/p6CAaNg": "https://six3.ayanakojivps.shop",
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
      return new Response(
        JSON.stringify({ error: "Invalid path", available_routes: Object.keys(ROUTES) }),
        {
          status: 404,
          headers: { "content-type": "application/json" },
        }
      );
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
 * Handle HTTP proxying with persistent connections
 */
async function handleHTTP(request: Request, target: string): Promise<Response> {
  const url = new URL(request.url);
  const targetUrl = new URL(url.pathname + url.search, target);

  try {
    const headers = new Headers(request.headers);
    headers.set("host", new URL(target).host);
    
    // Remove hop-by-hop headers
    headers.delete("connection");
    headers.delete("proxy-connection");
    headers.delete("keep-alive");
    headers.delete("transfer-encoding");
    headers.delete("upgrade");
    headers.delete("te");
    headers.delete("trailer");
    
    // Set persistent connection
    headers.set("connection", "keep-alive");

    const proxiedRequest = new Request(targetUrl, {
      method: request.method,
      headers,
      body: ["GET", "HEAD"].includes(request.method) ? null : request.body,
    });

    const response = await fetch(proxiedRequest);
    const responseHeaders = new Headers(response.headers);

    // Remove hop-by-hop headers from response
    const hopByHopHeaders = [
      "connection",
      "keep-alive",
      "transfer-encoding",
      "upgrade",
      "te",
      "trailer",
      "proxy-authenticate",
      "proxy-authorization",
    ];
    hopByHopHeaders.forEach((h) => responseHeaders.delete(h));

    // Keep connection alive
    responseHeaders.set("connection", "keep-alive");

    return new Response(response.body, {
      status: response.status,
      statusText: response.statusText,
      headers: responseHeaders,
    });
  } catch (err) {
    console.error("HTTP proxy error:", err);
    return new Response(
      JSON.stringify({ error: "Bad Gateway", details: String(err) }),
      {
        status: 502,
        headers: { "content-type": "application/json" },
      }
    );
  }
}

/**
 * Handle WebSocket proxying
 */
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
  } catch (err) {
    console.error("WebSocket proxy error:", err);
    return new Response("Bad Gateway", { status: 502 });
  }
}

/**
 * Relay WebSocket frames bidirectionally with error handling
 */
async function connectWebSocket(server: WebSocket, targetUrl: string): Promise<void> {
  let targetWs: WebSocket | null = null;

  try {
    targetWs = new WebSocket(targetUrl);

    // Forward server -> target
    server.addEventListener("message", (event) => {
      if (targetWs && targetWs.readyState === WebSocket.OPEN) {
        try {
          targetWs.send(event.data);
        } catch (err) {
          console.error("Error sending to target:", err);
        }
      }
    });

    if (targetWs) {
      // Forward target -> server
      targetWs.addEventListener("message", (event) => {
        if (server.readyState === WebSocket.OPEN) {
          try {
            server.send(event.data);
          } catch (err) {
            console.error("Error sending to client:", err);
          }
        }
      });

      // Handle close from server
      server.addEventListener("close", () => {
        if (targetWs && targetWs.readyState === WebSocket.OPEN) {
          targetWs.close();
        }
      });

      // Handle close from target
      targetWs.addEventListener("close", () => {
        if (server.readyState === WebSocket.OPEN) {
          server.close();
        }
      });

      // Handle error from server
      server.addEventListener("error", (err) => {
        console.error("Server WebSocket error:", err);
        if (targetWs && targetWs.readyState === WebSocket.OPEN) {
          targetWs.close();
        }
      });

      // Handle error from target
      targetWs.addEventListener("error", (err) => {
        console.error("Target WebSocket error:", err);
        if (server.readyState === WebSocket.OPEN) {
          server.close();
        }
      });
    }

    server.accept();
  } catch (err) {
    console.error("WebSocket connection error:", err);
    if (targetWs && targetWs.readyState === WebSocket.OPEN) {
      targetWs.close();
    }
    server.close();
  }
}
