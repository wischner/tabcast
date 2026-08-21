# disciple/tests/data/

Data files used by tests and by the F5 debug launch.

- `sample-page.png` is a hymn sheet at a typical browser viewport size,
  1200 x 720. It stands in for a captured page until Evangelist and
  Apostle can deliver a real one.

  It is deliberately text heavy and deliberately not a photograph.
  What matters about a Tabcast frame is whether small type survives
  being fitted to 480 x 800 and reduced to four grey levels, so the
  sample is chosen to make that visible rather than to look good.

  Render it the way the reader will:

  ```sh
  bin/disciple --replay disciple/tests/data/sample-page.png
  ```
