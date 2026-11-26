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

A Disciple finds the Apostle to connect to by itself, over UDP on the
same port number. See [Discovery](#discovery).

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
| `0x08` | `discover` | Disciple to the LAN, UDP | JSON, who is asking |
| `0x09` | `announce` | Apostle to Disciple, UDP | JSON, where to connect |

`discover` and `announce` never appear on the TCP stream. A peer that
reads either there must treat it as it would any message it does not
expect.

### hello

Sent once, immediately after connecting.

```json
{
  "name": "disciple",
  "version": "0.1.0",
  "protocol": 1,
  "device": "sheep_3fa91c07",
  "width": 480,
  "height": 800,
  "grayLevels": 2,
  "accepts": ["image/gray-packed"]
}
```

`device` names the reader, for Apostle's log: Disciple sends its own
name, the one its Wi-Fi setup network uses. `width` and `height` are the
panel's, as the reader's display reports them, so readers with different
panels each get pages sized for their own. Disciple on inkerface asks
for two levels, because inkerface draws one bit deep on every reader.

`grayLevels` counts distinct **shades**, black and white included, not
bits. Four levels is two bits per pixel. The field is named for levels
because "gray bits: 4" reads equally well as four levels and as
sixteen, and a peer that guesses wrong sends four times the data in a
depth the panel cannot show. Bits per pixel is always
`ceil(log2(grayLevels))`.

`accepts` lets a device opt out of PNG decoding. When a Disciple
accepts `image/gray-packed`, Apostle decodes, scales, and quantises on
the host, which is the right trade for a slow reader CPU, and sends that
Disciple **only** `gray-packed` frames, reduced for the `width`, `height`
and `grayLevels` of its hello. The page is fitted (one scale for both
axes, centred, white margins), downscaled by averaging, converted with
Rec. 709 luminance, and quantised to the nearest level with no
dithering. `grayLevels` must pack into 1, 2, or 4 bits a pixel (2 to 4
levels, or 9 to 16), so that a byte always holds whole pixels, and the
panel must be at most 4096 by 4096; a hello outside those limits is not
offered `gray-packed`. A device whose display
layer is one bit deep should say `"grayLevels": 2` and receive one bit
frames, rather than say 4 and dither the greys away itself. For an
480x800 panel at four levels that is a 96000 byte frame, against
384000 bytes for the same picture at one byte per pixel.

### welcome

```json
{ "ok": true, "clientId": "c7f1", "serverVersion": "0.1.0", "keepalive": 30 }
```

`keepalive` is in seconds. Apostle sends `ping` after that much silence
and closes the connection if no `pong` arrives within the same window.

Straight after the `welcome`, Apostle sends the newest frame it holds,
if it holds one. A reader switched on in the middle of a broadcast
then shows the current page at once instead of waiting for the next
one, and a reader that reconnects catches up on any page it missed
while away. The two often arrive in the same read.

### frame

Payload is a fixed 16-byte header followed by the image:

```
 offset  size  field
 ------  ----  ---------------------------------------------
      0     4  sequence
      4     2  width
      6     2  height
      8     1  format      see below
      9     1  grayLevels  shades for raw formats, else 0
     10     6  reserved    must be 0
     16     n  image bytes
```

| `format` | Meaning |
| --- | --- |
| 1 | `png` |
| 2 | `jpeg` |
| 3 | `raw-gray8`, one byte per pixel, values spread across the full 0-255 range |
| 4 | `gray-packed`, `ceil(log2(grayLevels))` bits per pixel |

In `gray-packed`, pixels run left to right with the **most significant
bits of a byte holding the leftmost pixel**, and every row starts on a
byte boundary. Row padding matters for a width that is not a whole
number of bytes: at two bits per pixel a row of 480 pixels is exactly
120 bytes and needs none, but a 481 pixel row would be padded to 121.

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

### Liveness and reconnecting

Apostle pings a Disciple after `keepalive` seconds without hearing from
it, and a live Disciple answers, so on a healthy connection a Disciple
hears something at least every `keepalive` seconds plus a moment.

A Disciple that hears nothing at all for **twice `keepalive` plus five
seconds** must treat Apostle as gone, close the connection, and start
again from discovery. That is what notices a daemon whose host lost
power, which closes nothing and sends no `bye`.

After any lost connection, failed connect, or unanswered `discover`, a
Disciple waits before trying again: 1, 2, 4, 8, then every 15 seconds.
The wait resets once it is welcomed. A Disciple keeps showing the last
page it drew while it reconnects rather than replacing it with a status
screen; the frame sent after the next `welcome` replaces it.

### Discovery

A reader has no keyboard and no reason to know an address, so Apostle
is found rather than configured.

Transport: UDP, the same port number as the broadcast stream, default
**8421**. Apostle binds it on every interface, loopback included. Each
datagram carries exactly one message in the usual envelope; a datagram
that does not decode as one whole message is dropped.

1. Disciple sends `discover` to the broadcast address of every
   interface that is up, and to `127.0.0.1`. A plain
   `255.255.255.255` leaves through one interface only, which on a
   machine with several networks may be the wrong one; it is used only
   when no interface offers a broadcast address. Loopback is what lets
   a test client find a daemon on the same machine.
2. Apostle answers each `discover` with an `announce`, sent directly to
   the address and port the `discover` came from. Apostle sends
   nothing unasked: there is no beacon.
3. Disciple connects over TCP to the **source address of the
   `announce`** and the `port` it names. The first usable answer wins.
4. A Disciple that hears no answer sends `discover` again, on the
   schedule in [Liveness and reconnecting](#liveness-and-reconnecting).

A Disciple given an address explicitly, such as the test client's
`--host`, skips discovery and connects to it directly.

#### discover

```json
{ "name": "disciple", "version": "0.1.0", "protocol": 1 }
```

#### announce

```json
{ "name": "apostle", "version": "0.1.0", "protocol": 1,
  "port": 8421, "host": "choir-laptop" }
```

`port` is the TCP broadcast port to connect to, which need not be the
port the `discover` was sent to. `host` is the daemon's own name for
its machine, for display and logs only; Disciple connects to the
address the datagram came from, never to `host`. A Disciple ignores an
`announce` whose `protocol` it does not speak and keeps listening for
another.

Discovery is a convenience. An Apostle that cannot bind the UDP port
still serves every Disciple told its address.

### Registration

v1 has no pairing handshake. Any host that can reach the port and speak
`hello` joins the flock, and discovery makes the port easy to find:
anything on the LAN that sends a `discover` is told where it is. This is acceptable on a trusted LAN and is not
acceptable anywhere else. Pairing tokens are the first thing to add
before this leaves a private network.
