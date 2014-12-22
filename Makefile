SRCS=generate.c parse_tex.c paragraph.c line.c \
     footnotes.c crossref.c record.c test.c
HEADERS=generate.h Makefile

COPT=-fsanitize=bounds -fsanitize-undefined-trap-on-error

TTFFILES=	urw-palladio-l-roman.ttf \


%.ttf:
	cat FONTS.md

all:	generate ebook-red-letter.profile $(TTFFILES)
	./generate ebook-red-letter.profile

generate:	$(SRCS) $(HEADERS)
	gcc -g -Wall -I/usr/local/include -I/usr/local/include/freetype2 -o generate $(SRCS) -L/usr/local/lib -lhpdf -lfreetype $(COPT)

clean:
	rm *.pdf *.toc *.aux *.log *.fls
