# Steady-state QUERY2 against module 0.3.0, after issue #68 (journal 0313)

Test host (Mac mini, Debian 13, 6.12.111), data of `2026-10-13-macmini-ondemand` (`ns3`),
method of journal 0139. `driver.sh`: the driver, run as
`sh driver.sh <src> d9ee667 <reps> <modules...>`. `confirm/`: 12 repetitions of 0.3.0
(`99302cb`), `main` (`d9ee667`) and batch 44 (`9851dd8`); `confirm-*.csv` are
`bench/compare.py` outputs against 0.3.0. `merges/`: 6 repetitions of 0.3.0 and the module of
each merge that touched `layer/` from `fcf6dbc` to `d9ee667`; `cmp-m-<merge>-<shape>.csv`
against 0.3.0. `runs.txt` and `environment.txt`: per-run lines and the host state. The raw
per-call records stay on the test host.
