//
// api/negotiate/index.js -- doom-assist patch: Phase 1 multiplayer's only
// server-side piece.
//
// Everything else in the multiplayer design happens directly between the
// two players' browsers and Azure Web PubSub -- packet relay, group
// broadcast, the actual DOOM netcode protocol (see engine/net_websockets.c)
// -- none of it ever touches a server this project runs. The one thing a
// browser client genuinely can't do for itself is mint its own access
// token: that requires the Web PubSub connection string, a secret this
// function holds (as an app setting) and the browser never sees. So this
// function's entire job is: given a room code, hand back a client access
// URL that's allowed to join and publish to *that one room's* group and
// nothing else. It never sees a single game packet once that URL is
// handed out.
//
// Deployed as an Azure Static Web Apps "managed function" (see
// .github/workflows/azure-static-web-apps.yml's api_location: "api") --
// reachable at /api/negotiate from the same origin as the game itself, so
// shell.html's fetch() needs no CORS configuration.
//

const { WebPubSubServiceClient } = require("@azure/web-pubsub");

// Set as an SWA "application setting" (Azure Portal, or `az staticwebapp
// appsettings set`) once the Web PubSub resource exists -- never
// committed here. See README.md for the exact setting name this reads.
const connectionString = process.env.WEB_PUBSUB_CONNECTION_STRING;

// A hub is Web PubSub's own namespacing concept (roughly: one hub per
// application sharing an instance) -- doom-assist only ever needs one.
const hubName = "doomassist";

module.exports = async function (context, req) {
    if (!connectionString) {
        context.res = {
            status: 500,
            body: "WEB_PUBSUB_CONNECTION_STRING is not configured on this deployment.",
        };
        return;
    }

    const group = (req.query.group || "").trim();

    // Room codes are generated client-side (shell.html's mpGenerateRoomCode)
    // from a fixed short alphanumeric alphabet -- this isn't validating a
    // password, just guarding against something malformed being used as a
    // Web PubSub group name or role string.
    if (!/^[A-Za-z0-9]{1,16}$/.test(group)) {
        context.res = {
            status: 400,
            body: "Invalid or missing 'group' query parameter.",
        };
        return;
    }

    const serviceClient = new WebPubSubServiceClient(connectionString, hubName);

    // Scoped narrowly to this one room: this token can join and publish
    // to *this* group only, not any other room that happens to be active
    // on the same (shared, free-tier) Web PubSub instance at the same
    // time. See learn.microsoft.com's json.webpubsub.azure.v1 subprotocol
    // reference for what these two role strings grant.
    const token = await serviceClient.getClientAccessToken({
        roles: [
            `webpubsub.joinLeaveGroup.${group}`,
            `webpubsub.sendToGroup.${group}`,
        ],
    });

    context.res = {
        status: 200,
        headers: { "Content-Type": "application/json" },
        body: { url: token.url },
    };
};
