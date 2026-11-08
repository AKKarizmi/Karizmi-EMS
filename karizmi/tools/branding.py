"""Rasterize this project's polygon-only SVG into Windows ICO/PNG assets.

No AI regeneration: coordinates/colors are read from the organization SVG.
Requires Pillow. Unsupported SVG elements fail instead of silently changing art.
"""
from pathlib import Path
import math
import re
import xml.etree.ElementTree as ET
from PIL import Image, ImageDraw

root = Path(__file__).resolve().parents[2]
svg = ET.parse(root / 'branding/karizmi-ak-vector.svg').getroot()
ns = {'s': 'http://www.w3.org/2000/svg'}
size = 1024
def color(text): return tuple(bytes.fromhex(text.lstrip('#')))
linear = svg.find('.//s:linearGradient', ns)
stops = [(float(s.attrib['offset']), color(s.attrib['stop-color'])) for s in linear]
glow = svg.find('.//s:radialGradient', ns)
glow_color = color(glow[0].attrib['stop-color'])
radius = float(glow.attrib['r'])
image = Image.new('RGBA', (size,size))
pixels = image.load()
for y in range(size):
    for x in range(size):
        fraction = (x + size - 1 - y) / (2*(size-1))
        a,b = next((a,b) for a,b in zip(stops,stops[1:]) if a[0] <= fraction <= b[0])
        t = (fraction-a[0])/(b[0]-a[0])
        base = [a[1][i]*(1-t)+b[1][i]*t for i in range(3)]
        alpha = max(0,1-math.hypot(x/(size-1),y/(size-1)-1)/radius)*float(glow[0].attrib['stop-opacity'])
        pixels[x,y] = tuple(round(base[i]*(1-alpha)+glow_color[i]*alpha) for i in range(3))+(255,)
draw = ImageDraw.Draw(image)
for path in svg.findall('.//s:path',ns):
    commands = re.findall(r'[A-Za-z]',path.attrib['d'])
    if any(c not in ('M','L','Z') for c in commands): raise ValueError('Unsupported SVG path')
    coordinates = list(map(float,re.findall(r'-?\d+(?:\.\d+)?',path.attrib['d'])))
    draw.polygon([(coordinates[i]*size/1200,coordinates[i+1]*size/1200) for i in range(0,len(coordinates),2)],fill='white')
image.resize((256,256),Image.Resampling.LANCZOS).save(root/'branding/karizmi.png')
image.save(root/'branding/karizmi.ico',sizes=[(16,16),(24,24),(32,32),(48,48),(64,64),(128,128),(256,256)])
print('Generated branding/karizmi.ico and karizmi.png from the SVG.')
