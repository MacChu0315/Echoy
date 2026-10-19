"""Check the actual scene font metrics at the battery label's four-character limit."""
import json, math
from pathlib import Path
from PIL import ImageFont
root=Path(__file__).resolve().parents[2]
ui=root/'projects/echoy/ui'
scene=json.loads((ui/'main.json').read_text())
labels=[o for o in scene['objects'] if o['name']=='battery_text' or o['name'].endswith('_battery_text')]
assert len(labels)==6
for label in labels:
    font=ImageFont.truetype(str(ui/label['font']),label['font_size'])
    for text in ['--%','0%','9%','37%','99%','100%']:
        needed=math.ceil(font.getlength(text))
        assert needed<=label['w'],f"{label['name']} clips {text}: text {needed}px, box {label['w']}px"
    assert label['x']+label['w']<=480
print('PASS all six battery labels fit unknown, one-, two- and three-digit percentages with the actual fonts')
