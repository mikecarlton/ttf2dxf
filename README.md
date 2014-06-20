Converts a TrueType font into DXF (for use with OpenSCAD or any other software that understands DXF).

Forked from http://www.thingiverse.com/thing:96714/ by Jeff Senn,
which was based on http://timeguy.com/cradek/truetype by Chris Radek.

### Installation on Mac OS X
```
brew install freetype
make
```

### Build Font
```
./tty2dxf -f ~/Library/Fonts/Times\ New\ Roman > times.dxf
```

### Original README
You will need freetype2 installed on your machine (with 'freetype_config' found in your path).
If not, then edit the Makefile to locate freetype includes and libraries.

Type 'make' to compile.

Type './ttf2dxf -f path_to_ttf_font_file > path_to_output.dxf' to generate a OpenSCAD compatible
version of a DXF file that represents each character in the font as a layer.

Each layer has a DIMENSION defined for character metrics:  minx, maxx, miny, maxy, advx, advy

See hello_world.scad for an example.
