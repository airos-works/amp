#!/usr/bin/env python3
"""Original Haiku rounded loudspeaker and flowing blue eighth note. MIT licence."""
from icon_art import Drawing, INK

d = Drawing()
d.shadow([(11,49),(32,58),(62,58),(41,49)])
# A shallow circular speaker, with a lit metal rim and a dark, recessed cone.
d.ellipse(27,32,19,23,d.gradient('87959d','3e505c'),INK)
d.ellipse(23,30,18,22,d.gradient('f8faec','a1b3b7',(9,10),(40,50)),INK)
d.ellipse(24,31,14.5,18,d.gradient('9dacaf','3b515e',(12,15),(39,49)),'52646b',1)
d.ellipse(24,32,11.5,14,d.gradient('647b84','293e4e',(14,19),(35,48)))
d.ellipse(21.5,29,5,6,d.gradient('c0d1cc','718b96',(17,23),(26,35)))
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
