"""Test the actual wsock_write implementation with mocked lwIP allocation/TCP."""
from pathlib import Path
import re
root = Path(__file__).resolve().parents[2]
lwip = root / "rt-thread/components/net/lwip/lwip-2.1.2/src"
source = (lwip / "apps/websocket/websocket_client.c").read_text(encoding="utf-8")
header = (lwip / "include/lwip/apps/websocket_client.h").read_text(encoding="utf-8")
start = source.index("err_t\nwsock_write(")
end = source.index("\n/**", start)
constants = "\n".join(line for line in header.splitlines()
                      if re.match(r"#define (WSHDR|WSLEN|WSMSG|OPCODE_)", line))
out = root / "build/robot_host_tests/wsock_write_under_test.inc"
out.write_text(constants + "\n" + source[start:end], encoding="utf-8")
