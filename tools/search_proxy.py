#!/usr/bin/env python3
from __future__ import annotations

import argparse
import html
import io
import os
import re
import subprocess
import tempfile
import threading
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

_proxy_server = None
USER_AGENT = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36"
IMG_CACHE: dict[str, bytes] = {}

def convert_image_to_bmp(raw_bytes: bytes, max_w: int = 160) -> bytes | None:
    try:
        from PIL import Image
        img = Image.open(io.BytesIO(raw_bytes)).convert("RGB")
        w, h = img.size
        if w > max_w:
            new_h = max(1, int(h * max_w / w))
            img = img.resize((max_w, new_h))
        buf = io.BytesIO()
        img.save(buf, format="BMP")
        return buf.getvalue()
    except Exception:
        pass
    try:
        with tempfile.NamedTemporaryFile(suffix=".in", delete=False) as f_in:
            f_in.write(raw_bytes)
            in_path = f_in.name
        out_path = in_path + ".bmp"
        cmd = ["/usr/bin/sips", "-s", "format", "bmp", "--resampleWidth", str(max_w), in_path, "--out", out_path]
        res = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=2.5)
        if res.returncode == 0 and os.path.exists(out_path):
            with open(out_path, "rb") as f_out: bmp_data = f_out.read()
            try: os.unlink(in_path)
            except: pass
            try: os.unlink(out_path)
            except: pass
            return bmp_data
        try: os.unlink(in_path)
        except: pass
    except Exception:
        pass
    return None

def clean_html_for_soma(raw_html: str, base_url: str = "") -> str:
    t = re.sub(r'(?is)<script.*?>.*?</script>', '', raw_html)
    t = re.sub(r'(?is)<style.*?>.*?</style>', '', t)
    t = re.sub(r'(?is)<noscript.*?>.*?</noscript>', '', t)
    t = re.sub(r'(?is)<svg.*?>.*?</svg>', '', t)
    t = re.sub(r'(?is)<!--.*?-->', '', t)
    t = re.sub(r'(?is)<div[^>]*?(cookie|consent|banner|overlay|ad-|ads)[^>]*?>.*?</div>', '', t)

    # Convertir todos los enlaces relativos a absolutos de forma robusta
    def fix_a_tag(m):
        tag = m.group(0)
        href_match = re.search(r'href=["\'](.*?)["\']', tag, re.I)
        if not href_match: return ""
        href = href_match.group(1).strip()
        if href.startswith("javascript:") or href.startswith("mailto:"): return ""
        if base_url:
            href = urllib.parse.urljoin(base_url, href)
        return f'<a href="{href}">'
    t = re.sub(r'(?is)<a\s+[^>]*>', fix_a_tag, t)

    img_count = [0]
    def fix_img_tag(m):
        if img_count[0] >= 2: return ""
        tag = m.group(0)
        src_match = (
            re.search(r'data-src=["\'](.*?)["\']', tag, re.I) or
            re.search(r'data-original=["\'](.*?)["\']', tag, re.I) or
            re.search(r'srcset=["\']([^\s,]+)', tag, re.I) or
            re.search(r'src=["\'](.*?)["\']', tag, re.I)
        )
        if not src_match: return ""
        img_url = src_match.group(1).strip()
        if base_url: img_url = urllib.parse.urljoin(base_url, img_url)
        if any(bad in img_url.lower() for bad in [".svg", ".ico", "pixel", "track", "beacon", "emoji", "avatar", "icon", "badge"]): return ""
        img_count[0] += 1
        proxy_img_url = f"http://10.0.2.2:8095/img?url={urllib.parse.quote(img_url, safe='')}"
        return f'<img src="{proxy_img_url}" width="160" height="90">'

    t = re.sub(r'(?is)<img[^>]*?>', fix_img_tag, t)
    return t

class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args): pass
    def _send(self, code: int, body: bytes, ctype: str):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path
        qs = urllib.parse.parse_qs(parsed.query)
        q = (qs.get("q") or qs.get("url") or [""])[0].strip()

        if path in ("/health", "/"):
            self._send(200, b"ok\n", "text/plain; charset=utf-8")
            return

        # PARCHE_RED_SEGURO_V1: búsqueda de noticias mediante Google News RSS.
        if path == "/search":
            if not q:
                self._send(400, b"Falta la consulta q", "text/plain; charset=utf-8")
                return
            search_url = "https://news.google.com/rss/search?" + urllib.parse.urlencode({
                "q": q, "hl": "es", "gl": "ES", "ceid": "ES:es"
            })
            req = urllib.request.Request(
                search_url,
                headers={"User-Agent": USER_AGENT,
                         "Accept": "application/rss+xml, application/xml;q=0.9, */*;q=0.8"},
            )
            try:
                with urllib.request.urlopen(req, timeout=12) as resp:
                    rss_data = resp.read(512 * 1024 + 1)
                if len(rss_data) > 512 * 1024:
                    self._send(502, b"El feed de noticias supera el limite de 512 KiB", "text/plain; charset=utf-8")
                    return
                root = ET.fromstring(rss_data)
                items = root.findall("./channel/item")
                rows = [f"Resultados de noticias para: {q}", "Fuente: Google News RSS", ""]
                for index, item in enumerate(items[:6], 1):
                    def field_text(tag: str) -> str:
                        node = item.find(tag)
                        if node is None:
                            return ""
                        value = " ".join("".join(node.itertext()).split())
                        value = html.unescape(re.sub(r"(?is)<[^>]*>", " ", value))
                        return " ".join(value.split())
                    title = field_text("title") or "(sin titulo)"
                    summary = field_text("description")
                    link = field_text("link")
                    published = field_text("pubDate")
                    rows.append(f"{index}. {title[:180]}")
                    if published:
                        rows.append(f"Fecha: {published[:80]}")
                    if link:
                        rows.append(f"Enlace: {link[:300]}")
                    if summary:
                        rows.append(f"Resumen: {summary[:220]}")
                    rows.append("")
                if not items:
                    rows.append("La fuente no devolvio noticias para esta consulta.")
                body = ("\n".join(rows)).encode("utf-8", errors="replace")
                self._send(200, body, "text/plain; charset=utf-8")
            except Exception as exc:
                message = f"No se pudo consultar Google News RSS: {exc}"
                self._send(502, message.encode("utf-8", errors="replace"), "text/plain; charset=utf-8")
            return

        # PARCHE_RED_SEGURO_V1: proxy HTTP(S) transparente para el curl del kernel.
        if path == "/fetch":
            if not q:
                self._send(400, b"Falta url", "text/plain; charset=utf-8")
                return
            target_info = urllib.parse.urlparse(q)
            if target_info.scheme not in ("http", "https") or not target_info.netloc:
                self._send(400, b"URL no valida: solo se permite http:// o https://", "text/plain; charset=utf-8")
                return
            req = urllib.request.Request(
                q,
                headers={"User-Agent": USER_AGENT, "Accept": "*/*"},
            )
            try:
                # El buffer HTTP del kernel para curl es de 4096 bytes. Limitar
                # el cuerpo evita que el proxy intente enviar grandes RSS/HTML
                # cuando el cliente SOMA cierra al llenar su propio buffer.
                with urllib.request.urlopen(req, timeout=15) as resp:
                    content_type = resp.headers.get_content_type()
                    payload = resp.read(3073)
                if len(payload) > 3072:
                    payload = payload[:2780] + b"\n...[respuesta truncada: limite de salida de SOMA]...\n"
                self._send(200, payload, content_type)
            except Exception as exc:
                message = f"Error al descargar URL externa: {exc}"
                self._send(502, message.encode("utf-8", errors="replace"), "text/plain; charset=utf-8")
            return

        if path in ("/img", "/image"):
            if not q: self._send(400, b"Falta url", "text/plain"); return
            if q in IMG_CACHE: self._send(200, IMG_CACHE[q], "image/bmp"); return
            parsed_target = urllib.parse.urlparse(q)
            referer = f"{parsed_target.scheme}://{parsed_target.netloc}/"
            req = urllib.request.Request(q, headers={"User-Agent": USER_AGENT, "Referer": referer, "Accept": "image/*,*/*;q=0.8"})
            try:
                with urllib.request.urlopen(req, timeout=4) as resp: raw_img = resp.read()
                bmp_data = convert_image_to_bmp(raw_img, max_w=160)
                if bmp_data:
                    IMG_CACHE[q] = bmp_data
                    self._send(200, bmp_data, "image/bmp")
                    return
            except Exception: pass
            self._send(404, b"Imagen no disponible", "text/plain")
            return

        if path in ("/web", "/proxy"):
            if not q: self._send(400, b"Falta url", "text/plain"); return
            target = q
            if not target.startswith("http://") and not target.startswith("https://"): target = "https://" + target
            req = urllib.request.Request(target, headers={"User-Agent": USER_AGENT, "Accept": "text/html,application/xhtml+xml"})
            try:
                with urllib.request.urlopen(req, timeout=10) as resp:
                    raw_data = resp.read()
                    charset = resp.headers.get_content_charset() or "utf-8"
                    html_text = raw_data.decode(charset, errors="replace")
                clean_doc = clean_html_for_soma(html_text, target)
                encoded = clean_doc.encode("utf-8", errors="replace")
                self._send(200, encoded, "text/html; charset=utf-8")
            except Exception as e:
                err_msg = f"<!DOCTYPE html><html><body><h1>Error Gateway</h1><p>{html.escape(str(e))}</p></body></html>"
                self._send(502, err_msg.encode("utf-8"), "text/html; charset=utf-8")
            return
        self._send(404, b"not found\n", "text/plain")

def start_background(host: str = "0.0.0.0", port: int = 8095) -> bool:
    global _proxy_server
    try:
        httpd = ThreadingHTTPServer((host, port), Handler)
        t = threading.Thread(target=httpd.serve_forever, daemon=True, name="SearchProxyThread")
        t.start()
        _proxy_server = httpd
        print(f"[GATEWAY] Proxy multimedia activo en puerto {port}")
        return True
    except Exception as e:
        print(f"[GATEWAY] AVISO: {e}")
        return False

def add_arguments(ap) -> None:
    ap.add_argument("--sin-busqueda", action="store_true", dest="sin_busqueda")
    ap.add_argument("--proxy-port", type=int, default=8095)

def ensure_proxy(args=None) -> None:
    port = getattr(args, "proxy_port", 8095) if args else 8095
    if getattr(args, "sin_busqueda", False): return
    try:
        with urllib.request.urlopen(f"http://127.0.0.1:{port}/health", timeout=0.8) as r:
            if r.status == 200: return
    except Exception: pass
    start_background("0.0.0.0", port)

if __name__ == "__main__":
    httpd = ThreadingHTTPServer(("0.0.0.0", 8095), Handler)
    print("[search_proxy] Gateway activo en puerto 8095")
    httpd.serve_forever()
