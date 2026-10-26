#!/usr/bin/env python3
"""Original Haiku rounded loudspeaker and flowing blue eighth note. MIT licence."""
from icon_art import Drawing, INK

d = Drawing()
d.shadow([(11,49),(32,58),(62,58),(41,49)])
# A mounted driver: metal flange, rolled rubber surround, recessed paper cone,
# and a domed dust cap. Opposing cone/cap lighting makes the depth readable.
d.ellipse(27,32,19,23,d.gradient('87959d','3e505c'),INK)
d.ellipse(23,30,18,22,d.gradient('f8faec','a1b3b7',(9,10),(40,50)),INK)
d.ellipse(23,30,14.5,18,d.gradient('5c6265','171c21',(12,14),(35,46)),INK,1)
d.ellipse(23,30,12,15,d.gradient('252b30','707a7f',(14,18),(33,43)),'151b20',1)
d.ellipse(24,31,5.5,7,d.gradient('68737a','141b22',(20,25),(28,37)),'11171c',1)
# Small dark mounting screws reinforce the metal flange at larger sizes.
for x, y in [(12,15),(34,15),(12,45),(34,45)]:
    d.ellipse(x,y,1,1.2,'343e44')
    d.line([(x-.5,y),(x+.5,y)],'a8b5ba',.4,lod=(.75,4))
# One continuous note silhouette avoids separate blocky stem/head pieces.
d.shape([
    ((43,17),(43,17),(46,20)),
    ((54,22),(48,20),(60,26)),
    ((54,35),(59,30),(54,35)),
    ((48,27),(53,30),(48,27)),
    (48,47),
    ((40,56),(49,53),(34,59)),
    ((31,51),(31,57),(31,46)),
    ((43,44),(34,44),(43,44)),
],d.gradient('a8d9f1','3679b7',(40,17),(48,56)),INK)
d.save(__file__, 'resources/branding/amp-icon.hvif')
