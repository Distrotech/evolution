
# this is only a temporary makefile

# you need ps2eps, groff, pnmutils, and ghostscript installed

PICS=e-config-flow.pic
EPS=$(PICS:.pic=.eps)
PNG=$(PICS:.pic=.png)

all: $(EPS) $(PNG)

%.eps: %.pic
	groff -p $^ | ps2eps -f -l > $@

%.png: %.pic
	groff -p $^ \
	| gs -q -dSAFER -dNOPAUSE -dBATCH -r180 -sDEVICE=pnmraw -sOutputFile=- - -c quit \
	| pnmcrop \
	| pnmscale 0.5 \
	| pnmtopng > $@

