OUTPUTS:= \
	newtestament-ebook-red.pdf \
	newtestament-ebook-black.pdf \
	oldtestament-ebook-red.pdf \
	oldtestament-ebook-black.pdf \
	bible-ebook-red.pdf \
	bible-ebook-black.pdf

COMMON:= \
	size-ebook.tex \
	isv-common.tex \
	newtestament-books.tex \
	oldtestament-books \
	redletter.tex \
	blackletter.tex \
	books/*.tex

all: $(OUTPUTS)

%.pdf: %.tex
	pdflatex $<
	pdflatex $<
	pdflatex $<

clean:
	rm *.pdf *.toc *.aux *.log *.fls