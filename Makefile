TTFFILES=	urw-palladio-l-roman.ttf \

%.ttf:
	cat FONTS.md

all:	generate ebook-red-letter.profile $(TTFFILES)
	./generate ebook-red-letter.profile

generate:	generate.c parse_tex.c
	gcc -g -Wall -I/usr/local/include -o generate generate.c parse_tex.c -L/usr/local/lib -lhpdf

clean:
	rm *.pdf *.toc *.aux *.log *.fls
