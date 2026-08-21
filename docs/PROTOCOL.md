# Tabcast Wire Protocol

Two hops, two protocols.

1. **Evangelist to Apostle** - HTTP/1.1 over loopback. Simple, because
   the browser is the client and a browser speaks HTTP well.
2. **Apostle to Disciple** - length-framed TCP over the LAN. Simple,
   because an e-ink reader should not carry an HTTP stack.

Version `v1` of both. Anything not described here is not part of the
protocol and may change.

---

## 1. Evangelist to Apostle

Transport: HTTP/1.1, `127.0.0.1`, default port **8420**.

Apostle must bind loopback only. It must never bind `0.0.0.0` for this
endpoint.

### POST /api/v1/frame

Uploads one captured frame. The body is the raw image, not JSON and not
base64.

Request headers:

| Header | Required | Meaning |
| --- | --- | --- |
| `Content-Type` | yes | `image/png` or `image/jpeg` |
| `Content-Length` | yes | Body length in bytes |
| `X-Tabcast-Sequence` | yes | Frame counter, starts at 1 per session |
| `X-Tabcast-Session` | yes | Opaque id, stable for one broadcast |
| `X-Tabcast-Width` | yes | Captured pixel width |
| `X-Tabcast-Height` | yes | Captured pixel height |
| `X-Tabcast-Title` | no | Page title, RFC 8187 percent-encoded UTF-8 |
| `X-Tabcast-Url` | no | Page URL, percent-encoded |

Success response `200 OK`, `application/json`:

```json
{ "ok": true, "sequence": 42, "clients": 3 }
```

`clients` is the number of Disciples the frame was fanned out to. It is
advisory and Evangelist shows it in the control bar.

Error responses use the same shape:

```json
{ "ok": false, "error": "frame too large" }
```

| Status | When |
| --- | --- |
| `400` | Missing or malformed required header |
| `413` | Body exceeds the configured frame size limit |
| `415` | `Content-Type` is not a supported image type |
| `503` | Apostle is running but not ready to accept frames |

### GET /api/v1/health

Liveness and capability probe. Evangelist calls it to light the status
dot in the control bar.

```json
{
  "ok": true,
  "name": "apostle",
  "version": "0.1.0",
  "protocol": 1,
  "clients": 3,
  "maxFrameBytes": 8388608
}
```

### CORS

Evangelist posts from its background context with a host permission for
`http://127.0.0.1/*`, so the request is not subject to page CORS. A
browser devtools page or a curl test may still be, so Apostle should
answer `OPTIONS` and send:

```
Access-Control-Allow-Origin: *
Access-Control-Allow-Methods: GET, POST, OPTIONS
Access-Control-Allow-Headers: Content-Type, X-Tabcast-Sequence,
    X-Tabcast-Session, X-Tabcast-Width, X-Tabcast-Height,
    X-Tabcast-Title, X-Tabcast-Url
```

### Authentication

None in v1. The endpoint is loopback only, so any process on the host
can post frames. If Tabcast ever grows a non-loopback ingest path, that
path needs a shared token before it ships.

---

## 2. Apostle to Disciple

Transport: TCP, default port **8421**, Apostle listens, Disciple
connects and stays connected. Apostle pushes. Disciple never polls.

All integers are **big-endian**, unsigned.

### Frame envelope

Every message on the socket, in both directions:

```
 offset  size  field
 ------  ----  ---------------------------------------------
      0     4  magic       'T' 'C' 'S' 'T'  (0x54435354)
      4     1  version     1
      5     1  type        see message types
      6     2  flags       reserved, must be 0
      8     4  length      payload byte count, max 16777216
     12  len   payload
```

A peer that reads a bad magic or an unknown version must close the
connection. It must not attempt resynchronisation.

### Message types

| Type | Name | Direction | Payload |
| --- | --- | --- | --- |
| `0x01` | `hello` | Disciple to Apostle | JSON, device description |
| `0x02` | `welcome` | Apostle to Disciple | JSON, assigned id and server info |
| `0x03` | `frame` | Apostle to Disciple | Frame header plus image bytes |
| `0x04` | `ack` | Disciple to Apostle | JSON, sequence acknowledged |
| `0x05` | `ping` | either | Empty |
| `0x06` | `pong` | either | Empty |
| `0x07` | `bye` | either | JSON, optional reason |

### hello

Sent once, immediately after connecting.

```json
{
  "name": "disciple",
  "version": "0.1.0",
  "protocol": 1,
  "device": "XTEINK-X4",
  "width": 1404,
  "height": 1872,
  "grayBits": 4,
  "accepts": ["image/png", "image/raw-gray8"]
}
```

`accepts` lets a device opt out of PNG decoding. When a Disciple
accepts `image/raw-gray8`, Apostle may decode and downscale on the
host, which is the right tradeoff for a slow reader CPU.

### welcome

```json
{ "ok": true, "clientId": "c7f1", "serverVersion": "0.1.0", "keepalive": 30 }
```

`keepalive` is in seconds. Apostle sends `ping` after that much silence
and closes the connection if no `pong` arrives within the same window.

### frame

Payload is a fixed 16-byte header followed by the image:

```
 offset  size  field
 ------  ----  ---------------------------------------------
      0     4  sequence
      4     2  width
      6     2  height
      8     1  format      1 = png, 2 = jpeg, 3 = raw-gray8
      9     1  grayBits    bits per pixel for raw formats, else 0
     10     6  reserved    must be 0
     16     n  image bytes
```

Apostle sends a frame to a Disciple only in a format that Disciple
listed in `accepts`.

### ack

```json
{ "sequence": 42, "renderMs": 780 }
```

Optional but recommended. `renderMs` is how long the panel refresh
took, which lets Apostle avoid queueing frames faster than a slow
device can draw them.

### Flow control

Apostle keeps at most one pending frame per Disciple. If a new frame
arrives while the previous one is unacknowledged, the pending frame is
**replaced**, not queued. A reader that falls behind shows the newest
page, never a backlog of stale ones.

### Registration

v1 has no pairing handshake. Any host that can reach the port and speak
`hello` joins the flock. This is acceptable on a trusted LAN and is not
acceptable anywhere else. Pairing tokens are the first thing to add
before this leaves a private network.
