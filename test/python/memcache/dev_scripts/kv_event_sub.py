#!/usr/bin/env python3
"""
ZMQ SUB subscriber for MemCache KV events.

Listens to the ZMQ PUB endpoint and decodes msgpack-encoded event batches
into human-readable JSON.

Wire format (per ZMQ multipart message):
  Frame 1: topic  (utf-8 string, e.g. "kv" or "kv@model_name")
  Frame 2: seq    (8-byte big-endian uint64 sequence number)
  Frame 3: payload (msgpack array: [event_map, ...])

Usage:
  python3 kv_event_sub.py [--endpoint tcp://127.0.0.1:5557] [--topic kv]
"""

import argparse
import json
import struct
import sys

import msgpack
import zmq


def decode_value(v):
    if isinstance(v, bytes):
        try:
            return v.decode("utf-8")
        except UnicodeDecodeError:
            return v.hex()
    if isinstance(v, list):
        return [decode_value(item) for item in v]
    if isinstance(v, dict):
        return {decode_value(k): decode_value(val) for k, val in v.items()}
    return v


def decode_event(event_obj):
    if not isinstance(event_obj, dict):
        return repr(event_obj)
    return {decode_value(k): decode_value(v) for k, v in event_obj.items()}


def decode_batch(payload: bytes):
    obj = msgpack.unpackb(payload, raw=True)
    if isinstance(obj, dict) and "events" in obj:
        events_array = obj["events"]
        decoded_events = [decode_event(e) for e in events_array]
        return {"events": decoded_events}
    if isinstance(obj, (list, tuple)):
        decoded_events = [decode_event(e) for e in obj]
        return {"events": decoded_events}
    return {"raw": repr(obj)}


def main():
    parser = argparse.ArgumentParser(description="MemCache KV event subscriber")
    parser.add_argument(
        "--endpoint",
        default="tcp://127.0.0.1:5557",
        help="ZMQ PUB endpoint to connect to (default: tcp://127.0.0.1:5557)",
    )
    parser.add_argument(
        "--topic",
        default="kv",
        help="ZMQ topic prefix to subscribe to (default: kv, empty string = all)",
    )
    args = parser.parse_args()

    ctx = zmq.Context.instance()
    sock = ctx.socket(zmq.SUB)
    sock.connect(args.endpoint)
    if args.topic:
        sock.setsockopt_string(zmq.SUBSCRIBE, args.topic)
    else:
        sock.setsockopt_string(zmq.SUBSCRIBE, "")

    print(f"Subscribed to {args.endpoint} (topic={args.topic!r})", file=sys.stderr)
    print("Waiting for events...", file=sys.stderr)

    try:
        while True:
            frames = sock.recv_multipart()
            if len(frames) != 3:
                print(
                    json.dumps({"error": f"expected 3 frames, got {len(frames)}"}),
                    flush=True,
                )
                continue

            topic_frame, seq_frame, payload_frame = frames
            try:
                batch = decode_batch(payload_frame)
            except Exception as exc:
                batch = {"decode_error": str(exc), "payload_hex": payload_frame.hex()}

            output = batch
            print(json.dumps(output, ensure_ascii=False, indent=2), flush=True)
            print(flush=True)
    except KeyboardInterrupt:
        print("\nInterrupted, exiting.", file=sys.stderr)
    finally:
        sock.close(linger=0)


if __name__ == "__main__":
    main()
