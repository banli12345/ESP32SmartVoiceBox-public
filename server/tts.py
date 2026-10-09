import asyncio
import sys
import edge_tts

async def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: python3 tts.py input.txt output.mp3")

    with open(sys.argv[1], "r", encoding="utf-8") as file:
        text = file.read().strip()

    communicate = edge_tts.Communicate(
        text,
        "zh-CN-XiaoxiaoNeural"
    )

    await communicate.save(sys.argv[2])

asyncio.run(main())
