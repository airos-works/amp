#!/usr/bin/env python3
"""Original Haiku loudspeaker cabinet and music note. MIT licence; no traced artwork."""
from icon_art import Drawing, INK, plane

d = Drawing()
p = plane(7, 12)
project=lambda point: p([point])[0]
d.shadow([(7,46),(32,58.5),(62,58.5),(37,46)])
# Cabinet: the floor-contact edges use the 2:1 front / 1:1 right perspective.
d.shape([(7,12),(18,1),(43,13.5),(32,24.5)],d.gradient('edce9d','b08757'),INK)
d.shape([(32,24.5),(43,13.5),(43,47.5),(32,58.5)],d.gradient('946c49','594538'),INK)
d.shape(p([(0,0),(25,0),(25,34),(0,34)]),d.gradient('d9b781','a88053'),INK)
# A tweeter and one large cone keep the speaker recognizable at 16px.
d.ellipse(12.5,8,4,4,d.gradient('cbd0bd','59666a'),'343c3c',1.4,project)
d.ellipse(12.5,23,9,9,d.gradient('9aa89c','414e50'),INK,1.7,project)
d.ellipse(12.5,23,6.5,6.5,d.gradient('667a78','2d3f45'),None,2,project)
d.ellipse(11,22,3,3,d.gradient('b8ccba','6f8989'),None,2,project)
# A single teal eighth note is a flat, high-contrast music symbol.
d.shape([(45,13),(57,18),(57,29),(49,24),(49,48),(43,50),(43,18)],
        d.gradient('9cdad2','3c8e95',(42,15),(53,52)),INK)
d.ellipse(42,49,8,5.5,d.gradient('8ad0c9','327a89',(38,45),(47,54)),INK)
d.save(__file__, 'resources/branding/amp-icon.hvif')
