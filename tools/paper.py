#!/usr/bin/env python3
"""HTTP display client and raw LoRa packet encoder for Wireless Paper."""
import argparse
import json
from pathlib import Path
import secrets
import struct
import zlib
import time

WIDTH, HEIGHT, STRIDE, CHUNK = 250, 122, 32, 224
FRAME_SIZE = STRIDE * HEIGHT


def packets(body, kind=1, request_id=None, frame_size=FRAME_SIZE):
    """Yield WP/1 packets; send each as one raw LoRa payload."""
    if kind not in (1, 2) or not 0 < len(body) <= 8192:
        raise ValueError("kind must be 1=JSON or 2=frame; payload must be 1..8192 bytes")
    if kind == 2 and len(body) != frame_size:
        raise ValueError(f"frame must be exactly {frame_size} bytes")
    request_id = secrets.randbits(32) if request_id is None else request_id
    checksum = zlib.crc32(body)
    for offset in range(0, len(body), CHUNK):
        yield struct.pack("<2sBBIHHI", b"WP", 1, kind, request_id, len(body), offset, checksum) + body[offset:offset+CHUNK]


def image_frame(path, width=WIDTH, height=HEIGHT):
    from PIL import Image, ImageOps
    if width < 1 or height < 1 or ((width+7)//8)*height > 8192:
        raise ValueError("screen dimensions must fit a 1..8192 byte frame")
    stride = (width+7)//8
    source = Image.open(path).convert("RGBA")
    opaque = Image.new("RGBA", source.size, "white")
    opaque.alpha_composite(source)
    image = ImageOps.contain(opaque.convert("RGB"), (width, height), Image.Resampling.LANCZOS)
    page = Image.new("RGB", (width, height), "white")
    page.paste(image, ((width-image.width)//2, (height-image.height)//2))
    mono = page.convert("1")
    output = bytearray(stride*height)
    for y in range(height):
        for x in range(width):
            if not mono.getpixel((x, y)):
                output[y*stride+x//8] |= 0x80 >> (x % 8)
    return bytes(output)


def main():
    p = argparse.ArgumentParser(description=__doc__, epilog="Provided as is, without warranty of any kind. You accept all risk from installation and use.")
    p.add_argument("--url", help="Board URL, e.g. http://192.168.1.100")
    p.add_argument("--lora-out", type=Path, help="Write raw LoRa packet files here instead of sending HTTP")
    p.add_argument("--via-lora", action="store_true", help="Send display commands through this board to another WP receiver")
    p.add_argument("--width", type=int, help="Receiver image width for LoRa/file output (default 250)")
    p.add_argument("--height", type=int, help="Receiver image height for LoRa/file output (default 122)")
    sub = p.add_subparsers(dest="command", required=True)
    t = sub.add_parser("text"); t.add_argument("text"); t.add_argument("--size", type=int, choices=range(1, 9), default=2)
    t.add_argument("--color", choices=['black','white'], default='black')
    t.add_argument("--background", choices=['black','white'], default='white')
    t.add_argument("--x",type=int,default=4);t.add_argument("--y",type=int,default=4)
    t.add_argument("--no-wrap",action='store_true');t.add_argument("--keep",action='store_true')
    i = sub.add_parser("image"); i.add_argument("file", type=Path)
    j = sub.add_parser("json"); j.add_argument("file", type=Path)
    sub.add_parser("status")
    activation=sub.add_parser("activate");activation.add_argument("--off",action="store_true");activation.add_argument("--tx",choices=["on","off"])
    send=sub.add_parser("send");send.add_argument("payload");send.add_argument("--hex",action="store_true")
    send.add_argument("--radio",type=Path,help="Temporary modem settings JSON for this packet; does not save configuration")
    receive=sub.add_parser("receive");receive.add_argument("--after",type=int,default=0)
    config=sub.add_parser("radio");config.add_argument("file",type=Path,help="JSON modem settings")
    args = p.parse_args()
    import requests
    session=requests.Session();session.trust_env=False
    if args.command in ("status","send","receive","radio","activate"):
        if not args.url:p.error("this command requires --url")
        base=args.url.rstrip("/")
        if args.command=="status":r=session.get(base+"/api/status",timeout=10)
        elif args.command=="receive":r=session.get(base+"/api/lora/messages",params={"after":args.after},timeout=10)
        elif args.command=="radio":r=session.post(base+"/api/lora/config",json=json.loads(args.file.read_text()),timeout=20)
        elif args.command=="activate":
            setting={"enabled":not args.off}
            if args.tx is not None:setting["tx_enabled"]=args.tx=="on"
            r=session.post(base+"/api/lora/activate",json=setting,timeout=20)
        else:
            body={"hex" if args.hex else "text":args.payload}
            if args.radio:body["radio"]=json.loads(args.radio.read_text())
            r=session.post(base+"/api/lora/send",json=body,timeout=30)
        r.raise_for_status();print(json.dumps(r.json(),indent=2));return
    kind = 1
    width,height=args.width or WIDTH,args.height or HEIGHT
    if args.command == "image":
        if args.url and not args.via_lora and not args.lora_out:
            r=session.get(args.url.rstrip("/")+"/api/status",timeout=10);r.raise_for_status()
            dimensions=r.json();width,height=dimensions["width"],dimensions["height"]
        body, kind = image_frame(args.file,width,height), 2
    else:
        if args.command == "json": doc = json.loads(args.file.read_text())
        else: doc = {"text":args.text,"size":args.size,"x":args.x,"y":args.y,"color":args.color,"background":args.background,"wrap":not args.no_wrap,"clear":not args.keep}
        body = json.dumps(doc, separators=(",", ":"), ensure_ascii=True).encode()
    if args.via_lora:
        if not args.url:p.error("--via-lora requires --url for the adapter")
        for index,packet in enumerate(packets(body,kind,frame_size=((width+7)//8)*height)):
            while True:
                r=session.post(args.url.rstrip("/")+"/api/lora/send",json={"hex":packet.hex()},timeout=30)
                if r.status_code!=429:break
                wait=max(1,min(3600,int(r.headers.get("Retry-After","1"))))
                print(f"Waiting {wait}s for the adapter's airtime budget",flush=True);time.sleep(wait)
            r.raise_for_status();print(f"Sent fragment {index+1}: {len(packet)} bytes",flush=True)
        return
    if args.lora_out:
        args.lora_out.mkdir(parents=True, exist_ok=True)
        encoded = list(packets(body, kind,frame_size=((width+7)//8)*height))
        names = []
        for index, packet in enumerate(encoded):
            name = f"packet-{index:03d}.bin"; (args.lora_out / name).write_bytes(packet); names.append(name)
        (args.lora_out / "manifest.json").write_text(json.dumps({"packets": names, "settings": {"bandwidth_khz":125,"spreading_factor":7,"coding_rate":5,"sync_word":"0x12","preamble":8,"crc":True},"note":"Send each file as one raw LoRa packet, in order. Set the same frequency at both ends. No ACKs. Respect local airtime limits."}, indent=2)+"\n")
        print(f"Wrote {len(encoded)} LoRa packets to {args.lora_out}")
    else:
        if not args.url: p.error("supply --url or --lora-out")
        import requests
        r = session.post(args.url.rstrip("/")+("/api/frame" if kind==2 else "/api/display"), data=body,
                          headers={"Content-Type":"application/octet-stream" if kind==2 else "application/json"}, timeout=15)
        r.raise_for_status(); print(json.dumps(r.json(), indent=2))


if __name__ == "__main__":
    main()
