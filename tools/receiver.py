#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
wxhook 接收端：接收 DLL 的 /log 上报 与 转发的微信消息。
仅依赖 Python 标准库，只终端打印，不落盘。

运行：
    python tools/receiver.py [port]      # 默认端口 9999，与 config.h LOG_PORT 一致

约定：
    POST /log      <- DLL 异常/关键事件上报，体 {"message":"..."}
    POST /message  <- 03 接收消息转发（字段由 03 定义，原样打印）
    GET  /         <- 健康检查
"""
import json
import sys
import datetime
import http.server
import socketserver

# Windows 控制台按 UTF-8 输出，避免中文乱码
try:
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")
except Exception:
    pass


def enable_ansi() -> bool:
    """Windows 终端启用 VT 颜色处理；非 TTY 或失败则返回 False（随后禁用颜色，避免打印乱码）。"""
    if not sys.stdout.isatty():
        return False
    try:
        import ctypes
        import ctypes.wintypes
        kernel32 = ctypes.windll.kernel32
        ENABLE_VIRTUAL_TERMINAL_PROCESSING = 0x0004
        STD_OUTPUT_HANDLE = -11
        h = kernel32.GetStdHandle(STD_OUTPUT_HANDLE)
        mode = ctypes.wintypes.DWORD()
        if not kernel32.GetConsoleMode(h, ctypes.byref(mode)):
            return False
        if not kernel32.SetConsoleMode(h, mode.value | ENABLE_VIRTUAL_TERMINAL_PROCESSING):
            return False
        return True
    except Exception:
        return False


HOST = "0.0.0.0"
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 9999

# ANSI 颜色（终端不支持时自动置空，避免 \x1b[90m 之类乱码）
USE_COLOR = enable_ansi()
C_RESET = "\033[0m" if USE_COLOR else ""
C_TIME = "\033[90m" if USE_COLOR else ""
C_LOG = "\033[33m" if USE_COLOR else ""
C_MSG = "\033[32m" if USE_COLOR else ""
C_WARN = "\033[31m" if USE_COLOR else ""


def now() -> str:
    return datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    # ---- 基础工具 ----
    def _send(self, code: int = 200, body: str = ""):
        data = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        if data:
            try:
                self.wfile.write(data)
            except (BrokenPipeError, ConnectionResetError):
                pass

    def _read_body(self) -> bytes:
        length = int(self.headers.get("Content-Length", 0) or 0)
        return self.rfile.read(length) if length > 0 else b""

    def _parse_json(self, raw: bytes):
        if not raw:
            return {}
        return json.loads(raw.decode("utf-8", errors="replace"))

    # ---- 路由 ----
    def do_GET(self):
        if self.path in ("/", "/status"):
            self._send(200, '{"status":"ok"}')
        else:
            self._send(404, '{"error":"not found"}')

    def do_POST(self):
        raw = self._read_body()
        try:
            obj = self._parse_json(raw)
        except Exception as e:
            print(f"{C_TIME}{now()}{C_RESET} {C_WARN}[BADJSON]{C_RESET} {e} raw={raw!r}")
            self._send(400, f'{{"error":"bad json: {e}"}}')
            return

        if self.path == "/log":
            msg = obj.get("message", "") if isinstance(obj, dict) else str(obj)
            print(f"{C_TIME}{now()}{C_RESET} {C_LOG}[LOG]{C_RESET} {msg}")
            self._send(200, '{"ok":true}')

        elif self.path in ("/message", "/msg"):
            pretty = json.dumps(obj, ensure_ascii=False)
            print(f"{C_TIME}{now()}{C_RESET} {C_MSG}[MSG]{C_RESET} {pretty}")
            self._send(200, '{"ok":true}')

        else:
            print(f"{C_TIME}{now()}{C_RESET} {C_WARN}[??]{C_RESET} {self.path} {raw.decode('utf-8','replace')}")
            self._send(404, '{"error":"unknown path"}')

    # 静默默认访问日志
    def log_message(self, *args, **kwargs):
        pass


class ThreadingServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def main():
    srv = ThreadingServer((HOST, PORT), Handler)
    print(f"wxhook 接收端监听 http://{HOST}:{PORT}/")
    print(f"  POST /log      -> DLL 上报日志")
    print(f"  POST /message  -> 03 转发消息")
    print(f"  GET  /         -> 健康检查")
    print("Ctrl+C 退出\n")
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        print("\n退出")
    finally:
        srv.shutdown()


if __name__ == "__main__":
    main()
