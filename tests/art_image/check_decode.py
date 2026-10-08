"""Decode real encoder output with Pillow's independent QOI implementation."""
import io
import struct
import sys
from pathlib import Path

from PIL import Image

bundle = Path(sys.argv[1]).read_bytes()
offset = records = 0
levels = [set(), set(), set()]
while offset < len(bundle):
    width, height, size = struct.unpack_from('<III', bundle, offset)
    offset += 12
    encoded = bundle[offset:offset + size]
    offset += size
    expected = bundle[offset:offset + width * height * 2]
    offset += width * height * 2
    assert encoded[:4] == b'qoif'
    assert struct.unpack_from('>II', encoded, 4) == (width, height)
    assert encoded[12:14] == b'\x03\x00'
    assert encoded[-8:] == b'\x00' * 7 + b'\x01'
    with Image.open(io.BytesIO(encoded)) as image:
        assert image.size == (width, height)
        for index, (red, green, blue, alpha) in enumerate(image.convert('RGBA').getdata()):
            assert alpha == 255, (records, index, alpha)
            quantized = (red * 31 // 255 << 11) | (green * 63 // 255 << 5) | (blue * 31 // 255)
            original = expected[index * 2] | (expected[index * 2 + 1] << 8)
            assert quantized == original, (records, index, quantized, original)
            levels[0].add((quantized >> 11) & 31)
            levels[1].add((quantized >> 5) & 63)
            levels[2].add(quantized & 31)
    records += 1
assert offset == len(bundle)
assert records == 809, records
assert levels == [set(range(32)), set(range(64)), set(range(32))]
print(f'Pillow QOI decode: {records} fixtures match RGB565; all 32/64/32 channel levels round-trip')
