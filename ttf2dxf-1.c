/*
This converts the characters in a TrueType font
into a OpenSCAD compatible DXF file that has one character per layer
with dimensions for minx, maxx, miny, maxy, advx, advy for each character.

Copyright 2013 Jeff Senn <jeffsenn@gmail.com>

This is free software; you can redistribute
it and/or modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2 of the
License, or (at your option) any later version.  This is distributed in
the hope that it will be useful, but WITHOUT ANY WARRANTY; without even
the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
PURPOSE.  See the GNU General Public License for more details.

This was inspired (and based on) TTT by Chris Radek <chris@timeguy.com>

*/

#include <stdio.h>
#include <ctype.h>
#include <getopt.h>
#include <locale.h>
#include <math.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

#undef __FTERRORS_H__
#define FT_ERRORDEF( e, v, s )	{ e, s },
#define FT_ERROR_START_LIST	{
#define FT_ERROR_END_LIST	{ 0, 0 } };

const struct ftError
{
  int	       err_code;
  const char*  err_msg;
} ft_errors[] =
#include FT_ERRORS_H

// define the number of linear segments we use to approximate beziers
// in the gcode and the number of polyline control points for dxf code.
int csteps=10;
// define the subdivision of curves into arcs: approximate curve length
// in font coordinates to get one arc pair (minimum of two arc pairs
// per curve)
double dsteps=200;
char *layer = 0;
wchar_t charcode = 0;
int genfont = 0;

#define NEQ(a,b) ((a).x != (b).x || (a).y != (b).y)
#define SQ(a) ((a)*(a))
#define CUBE(a) ((a)*(a)*(a))

typedef struct { double x, y; } P;

static double max(double a, double b) { if(a < b) return b; else return a; }

static P ft2p(const FT_Vector *v) { P r = {v->x, v->y}; return r; }
static double dot(P a, P b) { return a.x * b.x + a.y * b.y; }
static double mag(P a) { return sqrt(dot(a, a)); }
static P scale(P a, double b) { P r = {a.x*b, a.y*b}; return r; }
static P add(P a, P b) { P r = {a.x + b.x, a.y + b.y}; return r; }
static P add3(P a, P b, P c) {
    P r = {a.x + b.x + c.x, a.y + b.y + c.y}; return r;
}
static P add4(P a, P b, P c, P d) {
    P r = {a.x + b.x + c.x + d.x, a.y + b.y + c.y + d.y}; return r;
}
static P sub(P a, P b) { P r = {a.x - b.x, a.y - b.y}; return r; }
static P unit(P a) {
    double m = mag(a);
    if(m) {
	P r = {a.x/m, a.y/m };
	return r;
    } else {
	P r = {0, 0};
	return r;
    }
}

void line(P p) {
    printf("  10\n%.4f\n 20\n%.4f\n",
	   p.x, p.y);
}

void arc(P p1, P p2, P d) {
    d = unit(d);
    P p = sub(p2, p1);
    double den = 2 * (p.y*d.x - p.x*d.y);

    if(fabs(den) < 1e-10) {
	printf("G1 X[%.4f*#3+#5] Y[%.4f*#3+#6]\n", p2.x, p2.y);
	return;
    }

    double r = -dot(p,p)/den;

    double i = d.y*r;
    double j = -d.x*r;

    P c = {p1.x+i, p1.y+j};
    double st = atan2(p1.y-c.y, p1.x-c.x);
    double en = atan2(p2.y-c.y, p2.x-c.x);

    if(r < 0)
	while(en <= st) en += 2*M_PI;
    else
	while(en >= st) en -= 2*M_PI;

    double bulge = tan(fabs(en-st)/4);
    if(r > 0) bulge = -bulge;
    printf("  42\n%.4f\n 10\n%.4f\n  20\n%.4f\n",
	   bulge, p2.x, p2.y);
}

void biarc(P p0, P ts, P p4, P te, double r) {
    ts = unit(ts);
    te = unit(te);

    P v = sub(p0, p4);

    double c = dot(v,v);
    double b = 2 * dot(v, add(scale(ts, r), te));
    double a = 2 * r * (dot(ts, te)-1);

    double disc = b*b-4*a*c;
    
    if(a == 0 || disc < 0) {
	line(p4);
	return;
    }

    double disq = sqrt(disc);
    double beta1 = (-b - disq) / 2 / a;
    double beta2 = (-b + disq) / 2 / a;
    double beta = max(beta1, beta2);
    
    if(beta <= 0) {
	line(p4);
	return;
    }

    double alpha = beta*r;
    double ab = alpha+beta;
    P p1 = add(p0, scale(ts, alpha));
    P p3 = add(p4, scale(te, -beta));
    P p2 = add(scale(p1, beta/ab), scale(p3, alpha/ab));
    P tm = sub(p3, p2);

    arc(p0, p2, ts);
    arc(p2, p4, tm);    
}


static FT_Vector last_point;

struct extents
{
    long int minx;
    long int maxx;
    long int miny;
    long int maxy;
} glyph_extents, line_extents;

static FT_Vector advance;

// routine to print out hopefully-useful error messages
void handle_ft_error(char *where, int f, int x) 
{
    const struct ftError *e = &ft_errors[0];
    for(;e->err_msg && e->err_code != f;e++) ;
    if(e->err_msg) {
	fprintf(stderr, "Fatal error in %s: %s (%d) at line:%d\n", where, e->err_msg, f, x);
    } else {
	fprintf(stderr, "Fatal error in %s: %d at line:%d\n", where, f,x);
    }
    exit(x);
}

// resets extents struct members min and max to +big and -big respectively
// next call to extents_add_point(point) will set them to that point
void extents_reset( struct extents *e )
{
    e->maxx = -2000000000;
    e->maxy = -2000000000;
    e->minx =  2000000000;
    e->miny =  2000000000;
}

// updates extents struct to include the point
void extents_add_point( struct extents *e, const FT_Vector *point )
{
    if ( point->x > e->maxx ) e->maxx = point->x;
    if ( point->y > e->maxy ) e->maxy = point->y;
    if ( point->x < e->minx ) e->minx = point->x;
    if ( point->y < e->miny ) e->miny = point->y;
}


// updates extents struct e1 to include all of e2
void extents_add_extents( struct extents *e1, struct extents *e2 )
{
    if ( e2->maxx > e1->maxx ) e1->maxx = e2->maxx;
    if ( e2->maxy > e1->maxy ) e1->maxy = e2->maxy;
    if ( e2->minx < e1->minx ) e1->minx = e2->minx;
    if ( e2->miny < e1->miny ) e1->miny = e2->miny;
}

void maybe_output_layer() {
    if(genfont) {
      if(charcode < ' ' || charcode > '~') 
	printf("  8\n_%d\n", charcode);
      else
	//printf((charcode >= 'a' && charcode <= 'z') ? "  8\n%c_\n" : "  8\n%c\n", charcode);
	printf("  8\n%c\n", charcode);
    } else if(layer) {
      printf("  8\n%s\n", layer);
    }
  
}

// move with 'pen up' to a new position and then put 'pen down' 
int my_move_to( const FT_Vector* to, void* user )
{
    /* every move but the first one means we are starting a new polyline */
    /* make sure we terminate previous polyline with a seqend */
    printf("  0\nLWPOLYLINE\n  10\n%ld.000\n 20\n%ld.000\n", to->x, to->y);
    maybe_output_layer();
    last_point = *to;
    extents_add_point(&glyph_extents, to);

    return 0;
}

// plot with pen down to a new endpoint drawing a line segment 
// Linear Bézier curves (a line)
// B(t)=(1-t)P0 + tP1,	t in [0,1]. 
int my_line_to( const FT_Vector* to, void* user )
{
    printf("  10\n%ld.000\n 20\n%ld.000\n", to->x, to->y);
    last_point = *to;
    extents_add_point(&glyph_extents, to);

    return 0;
}

// draw a second order curve from current pos to 'to' using control
// Quadratic Bézier curves (a curve)
// B(t) = (1 - t)^2A + 2t(1 - t)B + t^2C,  t in [0,1]. 
int my_conic_to( const FT_Vector* control, const FT_Vector* to, void* user )
{
    int t;
    double x,y;
    FT_Vector point=last_point;
    double len=0;
    double l[csteps+1];

    l[0] = 0;
    for(t=1; t<=csteps; t++) {
	double tf = (double)t/(double)csteps;
	x = SQ(1-tf) * last_point.x + 2*tf*(1-tf) * control->x + SQ(tf) * to->x;
	y = SQ(1-tf) * last_point.y + 2*tf*(1-tf) * control->y + SQ(tf) * to->y;
	len += hypot(x-point.x, y-point.y);
	point.x = x;
	point.y = y;
	extents_add_point(&glyph_extents, &point);
    }

    P p0=ft2p(&last_point), p1=ft2p(control), p2=ft2p(to);
    P q0=sub(p1, p0), q1=sub(p2, p1);
    P ps=p0;
    P ts=q0;
    int steps = (int)max(2, len/dsteps);
    for(t=1; t<=steps; t++) {
	double tf = (double)t/(double)steps;
	double t1 = 1-tf;
	P p = add3(scale(p0, SQ(t1)), scale(p1, 2*tf*t1), scale(p2, SQ(tf)));
	P t = add(scale(q0, t1), scale(q1, tf));
	
	biarc(ps, ts, p, t, 1.0);

	ps = p; ts = t;
    }

    last_point = *to;
    return 0;
}

// draw a cubic spline from current pos to 'to' using control1,2
// Cubic Bézier curves ( a compound curve )
// B(t)=A(1-t)^3 + 3Bt(1-t)^2 + 3Ct^2(1-t) + Dt^3 , t in [0,1]. 
int my_cubic_to(const FT_Vector* control1, const FT_Vector* control2,
                                 const FT_Vector *to, void* user)
{
    int t;
    double x,y;
    FT_Vector point=last_point;
    double len=0;	    

    for(t=1; t<=csteps; t++) {
	double tf = (double)t/(double)csteps;
	x = CUBE(1-tf)*last_point.x	+ 
	    SQ(1-tf)*3*tf*control1->x	+
	    SQ(tf)*(1-tf)*3*control2->x +
	    CUBE(tf)*to->x;
	y = CUBE(1-tf)*last_point.y	+ 
	    SQ(1-tf)*3*tf*control1->y	+
	    SQ(tf)*(1-tf)*3*control2->y +
	    CUBE(tf)*to->y;;
	len += hypot(x-point.x, y-point.y);
	point.x = x;
	point.y = y;
	extents_add_point(&glyph_extents, &point);
    }

    int steps = (int)max(2, len/dsteps);
    P p0=ft2p(&last_point), p1=ft2p(control1), p2=ft2p(control2), p3=ft2p(to);
    P q0=sub(p1, p0), q1=sub(p2, p1), q2=sub(p3, p2);
    P ps=p0;
    P ts=q0;
    for(t=1; t<=steps; t++) {
	double tf = t*1.0/steps;
	double t1 = 1-tf;
	P p = add4(
	    scale(p0, CUBE(t1)), scale(p1, 3*tf*SQ(t1)),
	    scale(p2, 3*SQ(tf)*t1), scale(p3, CUBE(tf)));
	P t = add3(scale(q0, SQ(t1)), scale(q1, 2*tf*t1), scale(q2, SQ(tf)));
	
	biarc(ps, ts, p, t, 1.0);

	ps = p; ts = t;
    }

    last_point = *to;

    return 0;
}

static void my_draw_bitmap(FT_Bitmap *b, FT_Int x, FT_Int y, int linescale) {
    FT_Int i, j;
    static int oldbit;
    FT_Vector oldv = {99999,0};
    FT_Vector vbuf[100]; //freetype says no more than 32 ever?
    int spans = 0;
    int pitch = abs(b->pitch);
    static int odd=0;
    for(j = 0; j < b->rows; j++) {
        FT_Vector v;
        oldbit = 0;
        spans = 0;
        for(i = 0; i < pitch; i++) {
            unsigned char byte = b->buffer[j * pitch + i], mask, bits;
            for(bits = 0, mask = 0x80; mask; bits++, mask >>= 1) {
                unsigned char bit = byte & mask;
                v.x = i*8+bits+x;
                v.y = (y-j)*64*64/linescale-64*32/linescale;
                if(!oldbit && bit) {
                    v.x += 8;
                    oldv = v;
                    vbuf[spans++] = v;
                }
                if(oldbit && !bit) {
                    v.x -= 8;
                    if(oldv.x < v.x) {
                        vbuf[spans++] = v;
                    } else spans--;
                }
                oldbit = bit;
            }
        }
        if(oldbit) {
            v.x -= 8;
            vbuf[spans++] = v;
        }
        odd = !odd;
        spans /= 2;
        if(odd) {
            for (int i=spans-1; i>=0; i--) {
                my_move_to(vbuf+1+(i*2), (void*)1);
                my_line_to(vbuf+(i*2), (void*)1);
            }
        } else {
            for (int i=0; i<spans; i++) {
                my_move_to(vbuf+(i*2), (void*)1);
                my_line_to(vbuf+1+(i*2), (void*)1);
            }
        }
    }
}

// lookup glyph and extract all the shapes required to draw the outline
static long int render_char(FT_Face face, wchar_t c, long int offset, int linescale) {
    int error;
    int glyph_index;
    FT_Outline outline;
    FT_Outline_Funcs func_interface;

    charcode = c;

    error = FT_Set_Pixel_Sizes(face, 4096, linescale? linescale: 64);
    if(error) handle_ft_error("FT_Set_Pixel_Sizes", error, __LINE__);

    /* lookup glyph */
    glyph_index = FT_Get_Char_Index(face, (FT_ULong)c);
    /*for now silently handle missing glyph*/
    /*if(!glyph_index) handle_ft_error("FT_Get_Char_Index", 0, __LINE__);*/
    if(!glyph_index) return -1;

    /* load glyph */
    error = FT_Load_Glyph(face, glyph_index, FT_LOAD_NO_BITMAP | 
                                             FT_LOAD_NO_HINTING);
    if(error) handle_ft_error("FT_Load_Glyph", error, __LINE__);
    error = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_MONO);
    if(error) handle_ft_error("FT_Render_Glyph", error, __LINE__);

    if(linescale > 0)
        my_draw_bitmap(&face->glyph->bitmap, 
                       face->glyph->bitmap_left + offset,
                       face->glyph->bitmap_top,
                       linescale);


    error = FT_Set_Pixel_Sizes(face, 0, 64);
    if(error) handle_ft_error("FT_Set_Pixel_Sizes", error, __LINE__);
    error = FT_Load_Glyph(face, glyph_index, FT_LOAD_NO_BITMAP |
                                             FT_LOAD_NO_HINTING);
    if(error) handle_ft_error("FT_Load_Glyph", error, __LINE__);

    /* shortcut to the outline for our desired character */
    outline = face->glyph->outline;

    /* set up entries in the interface used by FT_Outline_Decompose() */
    func_interface.shift = 0;
    func_interface.delta = 0;
    func_interface.move_to = my_move_to;
    func_interface.line_to = my_line_to;
    func_interface.conic_to = my_conic_to;
    func_interface.cubic_to = my_cubic_to;

    /* offset the outline to the correct position in x */
    FT_Outline_Translate( &outline, offset, 0L );

    /* plot the current character */
    error = FT_Outline_Decompose( &outline, &func_interface, NULL);
    if(error) handle_ft_error("FT_Outline_Decompose", error, __LINE__);

    /* save advance in a global */
    advance.x = face->glyph->advance.x;
    advance.y = face->glyph->advance.y;

    /* offset will get bumped up by the x size of the char just plotted */
    return face->glyph->advance.x;
}

int main(int argc, char **argv) {
    FT_Library library;
    FT_Face face;
    int error;
    int i, l;
    long int offset;
    char *s;
    char *ttfont = 0;
    double scale = 0.0003;
    int linescale = 0;

    csteps=100;
    genfont = 1;

    while((i = getopt(argc, argv, "s:uf:c:l:L:F?")) != -1) {
	switch(i) {
	case 's':
	    dsteps=atof(optarg);
	    break;
	case 'f':
	    ttfont = optarg;
	    break;
	case 'F':
	    genfont = 1;
	    break;
        case 'c':
            scale = atof(optarg);
            break;
	case 'u':
	  setlocale(LC_CTYPE, "");
	  break;
        case 'l':
            linescale = atoi(optarg);
            if(linescale<24) linescale=24;
            break;
        case 'L':
	    layer=optarg;
	    break;
	case '?':
	    fprintf(stderr, "%s [-?] [-s steps] [-u] [-c scale] [-l linescale] [-L layername] [-f /some/file.ttf] 'The Text'\n", 
		    argv[0]);
	default:
	    return 99;
	}
    }
    if(!ttfont) { fprintf(stderr, "Please use -f to specify .ttf font file\n"); return 99; }
    

    error = FT_Init_FreeType(&library);
    if(error) handle_ft_error("FT_Init_FreeType", error, __LINE__);

    error = FT_New_Face(library, ttfont, 0, &face);
    if(error) handle_ft_error("FT_New_Face", error, __LINE__);

    /* An error can occur with a fixed-size font format (like FNT or PCF) 
      when trying to set the pixel size to a value that is not listed in the 
      face->fixed_sizes array.
    */
#define MYFSIZE 64
    error = FT_Set_Pixel_Sizes(face, 0, MYFSIZE);     
    if(error) handle_ft_error("FT_Set_Pixel_Sizes", error, __LINE__);

    
    /* grab the text string of extra chars to add beyond normal ASCII*/
    s=(optind < argc ) ? argv[optind] : 0;

    /* write out preamble */
    printf("  0\nSECTION\n  2\nENTITIES\n");

    extents_reset(&line_extents);
    offset = 0;

    if(genfont) {
      wchar_t wc;
      for(wc=' '; wc<127; wc++) {
	if(render_char(face, wc, offset, linescale) < 0) continue;
	extents_add_extents(&line_extents, &glyph_extents);
	printf(" 0\nDIMENSION\n 70\n70\n 1\nminx\n 13\n%ld\n",glyph_extents.minx);
	maybe_output_layer();
	printf(" 0\nDIMENSION\n 70\n70\n 1\nmaxx\n13\n%ld\n",glyph_extents.maxx);
	maybe_output_layer();
	printf(" 0\nDIMENSION\n 70\n6\n 1\nminy\n23\n%ld\n",glyph_extents.miny);
	maybe_output_layer();
	printf(" 0\nDIMENSION\n 70\n6\n 1\nmaxy\n23\n%ld\n",glyph_extents.maxy);
	maybe_output_layer();
	printf(" 0\nDIMENSION\n 70\n70\n 1\nadvx\n13\n%ld\n",advance.x);
	maybe_output_layer();
	printf(" 0\nDIMENSION\n 70\n6\n 1\nadvy\n23\n%ld\n",advance.y);
	maybe_output_layer();
      }
    }
    l = s ? strlen(s) : 0;
    while(l && *s) {
	wchar_t wc;
	long int off;
	int r = mbtowc(&wc, s, l);
	if(r==-1) { s++; continue; }
	extents_reset(&glyph_extents);
	off = render_char(face, wc, offset, linescale);
	if(off >= 0) {
	  if(!genfont) offset += off;
	  extents_add_extents(&line_extents, &glyph_extents);
	}
	s += r; l -= r;
    }
    /*todo - dimensions for !genfont*/
    /* write out the post amble stuff */
    printf("  0\nENDSEC\n  0\nEOF\n");
    return 0;
}

