# disciple/tests/data/

Data files for trying the reader by hand.

- `sample-page.png` is a hymn sheet at a typical browser viewport size,
  1200 x 720. It stands in for a captured page when no browser is
  broadcasting.

  It is deliberately text heavy and deliberately not a photograph.
  What matters about a Tabcast frame is whether small type survives
  being fitted to 480 x 800 and reduced to one bit a pixel, so the
  sample is chosen to make that visible rather than to look good.

  Post it to a running Apostle as Evangelist would, and every reader,
  emulated or real, shows it as Apostle reduced it for that reader:

  ```sh
  curl -X POST --data-binary @disciple/tests/data/sample-page.png \
       -H "Content-Type: image/png" -H "X-Tabcast-Sequence: 1" \
       -H "X-Tabcast-Session: sample" -H "X-Tabcast-Width: 1200" \
       -H "X-Tabcast-Height: 720" http://127.0.0.1:8420/api/v1/frame
  ```
