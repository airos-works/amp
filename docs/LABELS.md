# Printing disc labels

Amp prints a label for a MiniDisc (or any disc) on a Niimbot B1 label printer over
Bluetooth: the album artwork, the disc title, the artist and the total time, on the B1's
usual 50 × 30 mm labels, which fit the face of a MiniDisc above the shutter.

## Using it

- Right-click an album in the grid, a playlist in the sidebar or the MiniDisc under
  DEVICES and choose **Print Label…**.
  - An album gives its title, album artist, length and cover.
  - A playlist gives its name, the artist its songs share (or *Various Artists*), its length
    and the cover of the album most of its songs come from.
  - The MiniDisc gives the disc's title and the time recorded on it. When the title names a
    library album ("Artist - Album", as Amp titles the discs it writes) or a playlist, the
    artist and artwork come from there.
- The window shows the label exactly as it will print: every dot, on the white of the label.
  Title and artist can be edited; the preview follows.
- **Printer:** the printer printed to last is asked for its state when the window opens
  ("B1 I811131558 is ready, battery 4 of 4, 272 labels left on the roll"). The first time,
  or with **Search for Printers** in the menu, Amp runs a Bluetooth inquiry for printers
  (devices of the imaging class with the printer bit) and asks each one what it is. Switch
  the printer on first; its Bluetooth is visible while it is on.
- **Copies** prints up to ten in one go; **Darkness** is the printer's density, 1 to 5
  (3 is the printer's default and suits the paper labels; Amp remembers the choice).
- The first connection pairs with the printer: air/OS's Bluetooth server does that on its
  own (the B1 asks for no PIN). Printing takes about five seconds.

## How it works

- **Transport** (`src/player/Rfcomm`): the B1 is a dual-mode device. Its Bluetooth LE side
  would need a GATT connection, and air/OS's server keeps a single LE link, often held by a
  mouse or keyboard. Its classic side offers a serial port, RFCOMM channel 1. Haiku has no
  RFCOMM, so `Rfcomm` implements the part of TS 07.10 that a serial port needs (SABM/UA/DISC,
  parameter negotiation with credit based flow control, modem status, UIH data with its
  FCS) in user land, on a classic L2CAP channel to PSM 3. L2CAP needs an ACL link first:
  when `connect()` says the host is unreachable, `Rfcomm::Open` asks the Bluetooth server for
  one (`BT_REQ_CREATE_CONN`, as the A2DP code does), which also pairs, and tries again.
- **Protocol** (`src/core/Niimbot`, portable and unit tested): packets are
  `55 55 <command> <length> <data> <xor> aa aa`; the connect packet has a `03` in front.
  NiimBlue (github.com/MultiMote/niimbluelib, MIT) documents the protocol; Amp follows its
  B1 print task: density, label type, print start (7-byte form), page start, page size
  (6-byte form: rows, columns, copies), the image rows, page end, then the print status is
  polled until the pages are out, and print end. Rows are sent as bitmaps with the dot count
  of each third of the head, as dot positions when a row has at most six dots, and runs of
  blank or equal rows as one packet with a repeat count.
- **Picture** (`src/ui/LabelRenderer`): 384 × 240 dots (the head's 48 mm at 8 dots per
  millimetre, by 30 mm), 14 dots of margin. The cover is drawn square at the left,
  stretched to the full tone range, sharpened a little and dithered with Atkinson's error
  diffusion (`niimbot::PrepareGrey`, `niimbot::Dither`), which keeps highlights open on a
  head that spreads its dots. The text is drawn in the system fonts and thresholded, not
  dithered: the title in bold, as large as fits in three lines (30 down to 17 dots), the
  artist below it, the total time at the bottom under a rule. Without artwork the text
  takes the whole label.
- **Jobs** (`src/player/LabelPrinter`): searching, asking and printing each run on a thread
  of their own and report to the window with `kMsgLabel*` messages.
- Only the B1 (model id 4096) is printed to; another Niimbot model is named in the window
  as not supported yet. Others need their own print task and head width (see NiimBlue's
  `printer_models.ts`).

## Testing without the application

`make labeltool` builds `labeltool`:

```sh
labeltool render label.png "Blood On The Tracks" "Bob Dylan" 51.7 cover.jpg   # no printer
labeltool search
labeltool info 13:11:08:58:6E:B3
labeltool print 13:11:08:58:6E:B3 "Blood On The Tracks" "Bob Dylan" 51.7 cover.jpg
```

The rendered PNG has one pixel per printed dot. `make check` covers the packets (taken from
a B1 with firmware 15.55), the parser, the row packets and the picture preparation.
