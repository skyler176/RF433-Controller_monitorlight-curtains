import zenoh


ROUTER = "tcp/127.0.0.1:7447"
PREFIX = "humanex/cx9-ba/cx9-monitor-light"


config = zenoh.Config()
config.insert_json5("mode", "'client'")
config.insert_json5("connect/endpoints", f"['{ROUTER}']")

with zenoh.open(config) as session:
    live = list(session.liveliness().get(f"{PREFIX}/live", timeout=3.0))
    info = list(session.get(f"{PREFIX}/info", timeout=5.0))

    print(f"LIVE: {len(live)}")
    print("INFO:")
    for reply in info:
        if reply.ok:
            print(reply.ok.payload)
