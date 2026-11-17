# AmmarServerGames

Run the game test suites with `./run_tests.sh`. The parent AmmarServer checkout
registers the same script with CTest as `AmmarServerGames_tests`.  FRISK build,
run, backup, admin, map-pack and load-test documentation lives in
[`frisk/README.md`](frisk/README.md).

## FRISK HTTPS (optional)

FRISK itself only needs plain HTTP: on a phone the microphone button opens the
system recorder through `<input type="file" capture>`, and clips upload and play
normally on the LAN. Browsers only expose in-page hold-to-record
(`MediaRecorder`/`getUserMedia`) and notifications in a secure context, so use
HTTPS if you want those.

Build the parent tree with OpenSSL and start frisk with a certificate and key:

```sh
cmake -S . -B build -DUSE_OPENSSL=ON
cmake --build build --target frisk

mkdir -p key
openssl req -x509 -newkey rsa:2048 -nodes \
  -keyout key/frisk.key -out key/frisk.crt -days 365 \
  -subj "/CN=frisk.local"

FRISK_PORT=8090 ./src/AmmarServerGames/frisk/frisk \
  -sslcert key/frisk.crt -sslkey key/frisk.key
```

The TLS listener uses AmmarServer's `HTTPS_PORT`, which defaults to 443 (and
therefore may need root or `CAP_NET_BIND_SERVICE`), while the ordinary HTTP
listener stays on `FRISK_PORT`. Visit `https://<host>/` and accept the
self-signed certificate once. Without `-DUSE_OPENSSL=ON` the server still runs
over plain HTTP; the phone file-input path remains available.
