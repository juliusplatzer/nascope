package push;

import io.vertx.core.AbstractVerticle;
import io.vertx.core.Promise;
import io.vertx.core.http.HttpServer;
import io.vertx.core.http.ServerWebSocket;
import io.vertx.core.json.JsonObject;
import store.AirportFilter;
import store.TargetStore;

import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;

/**
 * WebSocket push verticle.
 *
 * Pipeline position:
 *   EventBus({@value TargetStore#DIFF_ADDRESS}) → fan-out → WebSocket clients
 *
 * Responsibilities:
 *   1. Serve a WebSocket endpoint at ws://{host}:{port}/ws.
 *   2. Subscribe to target diffs from TargetStore on the EventBus.
 *   3. Fan-out each diff frame to all connected clients.
 *   4. Drop slow/closed clients without blocking the event loop.
 *
 * Inbound commands from the UI (e.g. setAirports) are forwarded to the EventBus.
 */
public final class WebSocketPush extends AbstractVerticle {

    public static final int DEFAULT_PORT = 8080;

    private final Set<ServerWebSocket> clients = ConcurrentHashMap.newKeySet();

    @Override
    public void start(Promise<Void> startPromise) {
        int port = intEnv("WS_PORT", DEFAULT_PORT);

        HttpServer server = vertx.createHttpServer();

        server.webSocketHandler(ws -> {
            clients.add(ws);

            ws.closeHandler(v     -> clients.remove(ws));
            ws.exceptionHandler(e -> { clients.remove(ws); ws.close(); });

            // Inbound: route recognised command types to the EventBus.
            ws.textMessageHandler(text -> {
                JsonObject msg;
                try { msg = new JsonObject(text); } catch (Exception ignored) { return; }
                switch (msg.getString("type", "")) {
                    case "setAirports" -> vertx.eventBus().publish(AirportFilter.ADDRESS, msg);
                }
            });

            // Hello frame confirms the connection is live
            ws.writeTextMessage(new JsonObject()
                    .put("type", "connected")
                    .put("clients", clients.size())
                    .encode());
        });

        // Subscribe to diffs and fan-out. encode() once, write String to all clients.
        // Clients whose write queue is full are skipped — stale frames are worthless.
        vertx.eventBus().<JsonObject>consumer(TargetStore.DIFF_ADDRESS, msg -> {
            String frame = msg.body().encode();
            for (ServerWebSocket ws : clients) {
                if (!ws.isClosed() && !ws.writeQueueFull()) ws.writeTextMessage(frame);
            }
        });

        server.listen(port)
                .onSuccess(s -> {
                    System.out.println("[WS] Listening on ws://0.0.0.0:" + port + "/ws");
                    startPromise.complete();
                })
                .onFailure(startPromise::fail);
    }

    private static int intEnv(String key, int def) {
        try { return Integer.parseInt(System.getenv(key).strip()); } catch (Exception e) { return def; }
    }
}
