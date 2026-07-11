const ROUTES: Record<string, string> = {
  "/maibhhhh": "https://south.ayanakojivps.shop",
  "/s1": "https://south2.ayanakojivps.shop",
  "/btc": "https://six2.ayanakojivps.shop",
  "/p6CAaNg": "https://six3.ayanakojivps.shop",
};

export default {
  async fetch(request: Request): Promise<Response> {
    const url = new URL(request.url);
    const backendUrl = ROUTES[url.pathname];

    if (!backendUrl) {
      return new Response("Not found", { status: 404 });
    }

    // WebSocket upgrade for v2ray
    if (request.headers.get("upgrade") === "websocket") {
      return handleWebSocket(request, backendUrl, url.pathname + url.search);
    }

    try {
      const targetUrl = new URL(url.pathname + url.search, backendUrl);
      
      const response = await fetch(targetUrl, {
        method: request.method,
        headers: request.headers,
        body: request.body,
      });

      return new Response(response.body, {
        status: response.status,
        headers: response.headers,
      });
    } catch (err) {
      return new Response("Bad Gateway", { status: 502 });
    }
  },
};

function handleWebSocket(request: Request, backend: string, path: string): Response {
  const targetUrl = new URL(path, backend).toString();

  try {
    const webSocketPair = new WebSocketPair();
    const [client, server] = Object.values(webSocketPair);

    relayWebSocket(server as unknown as WebSocket, targetUrl);

    return new Response(null, {
      status: 101,
      webSocket: client as unknown as WebSocket,
    });
  } catch (err) {
    return new Response("WebSocket Error", { status: 502 });
  }
}

async function relayWebSocket(server: WebSocket, targetUrl: string): Promise<void> {
  const targetWs = new WebSocket(targetUrl);

  server.addEventListener("message", (event) => {
    if (targetWs.readyState === WebSocket.OPEN) targetWs.send(event.data);
  });

  targetWs.addEventListener("message", (event) => {
    if (server.readyState === WebSocket.OPEN) server.send(event.data);
  });

  server.addEventListener("close", () => {
    if (targetWs.readyState === WebSocket.OPEN) targetWs.close();
  });

  targetWs.addEventListener("close", () => {
    if (server.readyState === WebSocket.OPEN) server.close();
  });

  server.accept();
}
