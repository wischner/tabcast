# scripts/

What the top-level `Makefile` runs, and the service unit. Build logic
itself does not belong here: each part builds through its own
`Makefile`, and the top-level one is the only one to use.

| Script | Run by | What it does |
| --- | --- | --- |
| `inkerface.py` | `make` | Keeps `deps/inkerface/` at inkerface's latest release: asks GitHub which is latest, and downloads and unpacks it when it is not the one there. Standard library only. |
| `deps.sh` | `make` | Downloads a pinned, checksummed file into `deps/`, if it is not there already. |
| `run.sh` | `make run` | Starts Apostle, Evangelist in Firefox, and Disciple together, and stops them together. |
| `firefox.py` | `run.sh` | Starts a Firefox with a throwaway profile and Evangelist loaded as a temporary add-on, through Firefox's remote debugging protocol, as Mozilla's web-ext does. Standard library only. |
| `apostle.service` | systemd | The unit for the broadcast daemon. It runs apostle in the foreground on purpose, so systemd can supervise it and capture its log. See the Apostle README. |

The tools a build needs are in the build image, `docker/Dockerfile`,
not here.
