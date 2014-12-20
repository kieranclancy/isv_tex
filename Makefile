TTFFILES=	urw-palladio-l-roman.ttf \

%.ttf:
	cat FONTS.md

all:	generate ebook-red-letter.profile $(TTFFILES)
	./generate ebook-red-letter.profile

generate:	generate.c parse_tex.c paragraph.c line.c
	gcc -g -Wall -I/usr/local/include -I/usr/local/include/freetype2 -o generate generate.c parse_tex.c paragraph.c line.c -L/usr/local/lib -lhpdf -lfreetype

clean:
	rm *.pdf *.toc *.aux *.log *.fls
