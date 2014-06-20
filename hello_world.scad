module letter(l,font="font.dxf",h=100,i=0) {
    union() {	
	linear_extrude( height=h) import(font, layer=l[i]);
	translate([dxf_dim(file=font, name="advx",layer=l[i]),
		   dxf_dim(file=font, name="advy",layer=l[i]),
		   0])
	child();
    }
}

module word(wrd,font="font.dxf",h=100,i=0) {
    if(i < len(wrd)) {
	letter(wrd,font,h,i) word(wrd,font,h,i+1);
    } else {
	child();
    }
}

union() {
    translate([0,400,0])
    scale(0.1) word("HelloWorld!");
    scale(0.1)
    union() {
	letter("H")
	letter("e",h=200)
	letter("l",h=300)
	letter("l",h=400)
	letter("o",h=500);
    };
    translate([0,-400,0])
    scale(0.1)
    union() {
	rotate([0,20,0])
	letter("B")
	rotate([0,20,0])
	letter("e")
	rotate([0,20,0])
	letter("n")
	rotate([0,20,0])
	letter("d")
	rotate([0,20,0])
	letter("y");
    }
}