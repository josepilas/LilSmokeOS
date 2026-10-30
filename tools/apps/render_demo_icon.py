"""Rasterize the licensed Lucide vector for the native package icon."""
from pathlib import Path
import subprocess,xml.etree.ElementTree as ET
P=Path(__file__).resolve().parents[2]
Q=Path('/root/lilsmoke-applications-20261009');Q.mkdir(exist_ok=True)
demo=P/'tools/apps/demo'
tree=ET.parse(demo/'lucide-gamepad-2.svg');root=tree.getroot()
root.set('width','160');root.set('height','160');root.set('stroke','#368dc9');root.set('stroke-width','1.65')
children=list(root);outline=children[-1]
root.remove(outline);root.insert(0,outline);outline.set('fill','#f6fcff')
for child,color in zip(children[:-1],['#244e68','#244e68','#ec776b','#62b9a2']):child.set('stroke',color)
source=Q/'button-tester-icon.svg';tree.write(source,encoding='utf-8',xml_declaration=True)
subprocess.run(['convert','-background','none',str(source),str(demo/'icon.png')],check=True)
print(demo/'icon.png')
