#!/usr/bin/env python3
"""vtshot.py - run a program on a pseudo-terminal, or replay its output, through a
small terminal emulator, and save the screen as a PNG (colours included).

    tools/vtshot.py [--rows 30] [--cols 100] [--secs 6] [--key T:KEYS]...
                    [--no-utf8] [--raw out.bin] --png shot.png -- command [args]
    tools/vtshot.py [--rows ...] --replay out.bin --png shot.png

Enough of xterm/VT220 for what VTOP sends: CUP, ED, SGR (bold, reverse, 30-37,
40-47, 38;5, 48;5, 38;2, 48;2), DEC Special Graphics (ESC ( 0 / ESC ( B), UTF-8.
DSR 6 is answered: with the cursor where a UTF-8 terminal would put it, or with
--no-utf8 where a VT would (each byte of a UTF-8 character one column), which
tests VTOP's terminal check (D6). --key 3:g sends "g" 3 s after the start."""
import argparse
import os
import pty
import select
import struct
import sys
import time
import fcntl
import termios

DEC = {'q': '─', 'x': '│', 'l': '┌', 'k': '┐', 'm': '└', 'j': '┘', 't': '├', 'u': '┤',
       'w': '┬', 'v': '┴', 'n': '┼', 'o': '⎺', 'p': '⎻', 'r': '⎼', 's': '⎽', '~': '·',
       'a': '▒', '`': '◆'}
BASE = [(0, 0, 0), (205, 49, 49), (13, 188, 121), (229, 229, 16), (36, 114, 200),
        (188, 63, 188), (17, 168, 205), (229, 229, 229)]
DEFAULT_FG, DEFAULT_BG = (204, 204, 204), (24, 24, 28)


def xterm256(n):
    if n < 8:
        return BASE[n]
    if n < 16:
        return tuple(min(255, c + 60) for c in BASE[n - 8])
    if n < 232:
        n -= 16
        lv = [0, 95, 135, 175, 215, 255]
        return (lv[n // 36], lv[n // 6 % 6], lv[n % 6])
    g = 8 + (n - 232) * 10
    return (g, g, g)


class Screen:
    def __init__(self, rows, cols, utf8):
        self.rows, self.cols, self.utf8 = rows, cols, utf8
        self.clear()
        self.r = self.c = 0
        self.fg = self.bg = None
        self.bold = self.rev = False
        self.gfx = False
        self.replies = b''

    def clear(self):
        self.cell = [[(' ', None, None, False, False) for _ in range(self.cols)]
                     for _ in range(self.rows)]

    def put(self, ch):
        if self.gfx and ch in DEC:
            ch = DEC[ch]
        if self.c >= self.cols:
            self.c = self.cols - 1
        if 0 <= self.r < self.rows:
            self.cell[self.r][self.c] = (ch, self.fg, self.bg, self.bold, self.rev)
        self.c += 1

    def sgr(self, params):
        p = [int(x) if x else 0 for x in params.split(';')] if params else [0]
        i = 0
        while i < len(p):
            v = p[i]
            if v == 0:
                self.fg = self.bg = None
                self.bold = self.rev = False
            elif v == 1:
                self.bold = True
            elif v == 7:
                self.rev = True
            elif 30 <= v <= 37:
                self.fg = BASE[v - 30]
            elif 40 <= v <= 47:
                self.bg = BASE[v - 40]
            elif v in (38, 48) and i + 1 < len(p):
                if p[i + 1] == 5 and i + 2 < len(p):
                    col = xterm256(p[i + 2]); i += 2
                elif p[i + 1] == 2 and i + 4 < len(p):
                    col = tuple(p[i + 2:i + 5]); i += 4
                else:
                    col = None
                if v == 38:
                    self.fg = col
                else:
                    self.bg = col
            elif v == 39:
                self.fg = None
            elif v == 49:
                self.bg = None
            i += 1

    def feed(self, data):
        s = data.decode('utf-8', errors='replace') if self.utf8 else data.decode('latin-1')
        i = 0
        while i < len(s):
            ch = s[i]
            if ch == '\x1b' and i + 1 < len(s):
                n = s[i + 1]
                if n == '[':
                    j = i + 2
                    while j < len(s) and not ('@' <= s[j] <= '~'):
                        j += 1
                    if j >= len(s):
                        break
                    params, final = s[i + 2:j], s[j]
                    self.csi(params, final)
                    i = j + 1
                    continue
                if n in '()' and i + 2 < len(s):
                    if n == '(':
                        self.gfx = s[i + 2] == '0'
                    i += 3
                    continue
                i += 2
                continue
            if ch == '\r':
                self.c = 0
            elif ch == '\n':
                self.r = min(self.rows - 1, self.r + 1)
            elif ch >= ' ':
                self.put(ch)
            i += 1

    def csi(self, params, final):
        if final == 'H':
            p = [int(x) if x else 1 for x in params.split(';')] if params else [1, 1]
            self.r, self.c = p[0] - 1, (p[1] - 1 if len(p) > 1 else 0)
        elif final == 'J' and params == '2':
            self.clear()
        elif final == 'm':
            self.sgr(params)
        elif final == 'n' and params == '6':
            self.replies += ('\x1b[%d;%dR' % (self.r + 1, self.c + 1)).encode()

    def png(self, path, mono=False):
        from PIL import Image, ImageDraw, ImageFont
        font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf', 15)
        # braille and scan lines are missing from the mono font
        extra = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', 15)
        bold = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf', 15)
        cw, ch = 9, 18
        img = Image.new('RGB', (self.cols * cw, self.rows * ch), DEFAULT_BG)
        d = ImageDraw.Draw(img)
        for r in range(self.rows):
            for c in range(self.cols):
                t, fg, bg, b, rev = self.cell[r][c]
                fg = DEFAULT_FG if mono or not fg else fg
                bg = DEFAULT_BG if mono or not bg else bg
                if rev:
                    fg, bg = bg, fg
                if bg != DEFAULT_BG:
                    d.rectangle([c * cw, r * ch, c * cw + cw - 1, r * ch + ch - 1], fill=bg)
                scan = {'⎺': 1, '⎻': 3, '⎼': 7, '⎽': 9}
                if t in scan:      # DEC scan lines: no font has them, so draw them
                    y = r * ch + (scan[t] - 1) * (ch - 2) // 8
                    d.rectangle([c * cw, y, c * cw + cw - 1, y + 1], fill=fg)
                elif t != ' ':
                    o = ord(t)
                    f = extra if 0x2800 <= o <= 0x28FF or 0x23BA <= o <= 0x23BD else (bold if b else font)
                    d.text((c * cw, r * ch), t, font=f, fill=fg)
        img.save(path)

    def text(self):
        return '\n'.join(''.join(x[0] for x in row).rstrip() for row in self.cell)


def run(args, scr):
    pid, fd = pty.fork()
    if pid == 0:
        os.execvp(args.command[0], args.command)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack('HHHH', args.rows, args.cols, 0, 0))
    keys = sorted((float(k.split(':', 1)[0]), k.split(':', 1)[1]) for k in args.key)
    start = time.time()
    raw = b''
    while time.time() - start < args.secs:
        while keys and time.time() - start >= keys[0][0]:
            os.write(fd, keys.pop(0)[1].encode())
        r, _, _ = select.select([fd], [], [], 0.05)
        if r:
            try:
                data = os.read(fd, 65536)
            except OSError:
                break
            if not data:
                break
            raw += data
            scr.feed(data)
            if scr.replies:
                os.write(fd, scr.replies)
                scr.replies = b''
    try:
        os.write(fd, b'q')
        time.sleep(0.3)
        os.kill(pid, 9)
    except OSError:
        pass
    os.waitpid(pid, 0)
    return raw


def ptydrive_bytes(data, step):
    """PTYDRIVE prints what the terminal got with control characters as <ESC>, <CR>,
    <LF> plus a newline, <XX>, events as {CHAR}, and '---- label' lines between
    steps; bytes from 0x80 up are as they were. Back to bytes, up to the end of step."""
    import re
    out = []
    for line in data.split(b'\n'):
        m = re.match(rb'^---- (\S+)', line)
        if m:
            if step and out and current == step:
                break
            current = m.group(1).decode()
            continue
        if not line.startswith(b'---- '):
            out.append(line)
        current = locals().get('current', '')
    text = b'\n'.join(out)
    text = re.sub(rb'\{[A-Z]+\}', b'', text)
    text = text.replace(b'<LF>\n', b'\n')
    text = re.sub(rb'<ESC>', b'\x1b', text)
    text = re.sub(rb'<CR>', b'\r', text)
    text = re.sub(rb'<([0-9A-F]{2})>', lambda m: bytes([int(m.group(1), 16)]), text)
    return text


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rows', type=int, default=30)
    ap.add_argument('--cols', type=int, default=100)
    ap.add_argument('--secs', type=float, default=6)
    ap.add_argument('--key', action='append', default=[])
    ap.add_argument('--no-utf8', action='store_true')
    ap.add_argument('--mono', action='store_true', help='ignore colours, as a monochrome VT does')
    ap.add_argument('--raw')
    ap.add_argument('--replay')
    ap.add_argument('--ptydrive', help='a PTYDRIVE transcript (control characters made visible)')
    ap.add_argument('--step', help='with --ptydrive: the screen after this step')
    ap.add_argument('--png')
    ap.add_argument('command', nargs='*')
    args = ap.parse_args()
    scr = Screen(args.rows, args.cols, not args.no_utf8)
    if args.ptydrive:
        scr.feed(ptydrive_bytes(open(args.ptydrive, 'rb').read(), args.step))
    elif args.replay:
        scr.feed(open(args.replay, 'rb').read())
    else:
        raw = run(args, scr)
        if args.raw:
            open(args.raw, 'wb').write(raw)
        # live feeding only answers DSR; a sequence split between reads is whole here
        scr = Screen(args.rows, args.cols, not args.no_utf8)
        scr.feed(raw)
    if args.png:
        scr.png(args.png, args.mono)
    print(scr.text())


if __name__ == '__main__':
    main()
