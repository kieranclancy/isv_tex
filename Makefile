OUTPUTS:= \
	jude.pdf \
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

%.pdf: %.tex isv-common.tex
# Put all 3 runs on the same line, so that a bad exit code from the
# first doesn't prevent the subsequent runs (which may fix some of the
# errors that the first run complains about)
	pdflatex $< ; pdflatex $< ; pdflatex $<

clean:
	rm *.pdf *.toc *.aux *.log *.fls